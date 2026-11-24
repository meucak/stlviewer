#include "stl_loader.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace faset {

namespace {

class MappedFile {
public:
    ~MappedFile() { close_map(); }

    bool open_read(const std::string& path, std::string& err) {
        fd_ = ::open(path.c_str(), O_RDONLY);
        if (fd_ < 0) {
            err = "Cannot open file: " + path;
            return false;
        }
        struct stat st{};
        if (::fstat(fd_, &st) != 0) {
            err = "Cannot stat file: " + path;
            close_map();
            return false;
        }
        if (!S_ISREG(st.st_mode)) {
            err = "Not a regular file: " + path;
            close_map();
            return false;
        }
        if (st.st_size <= 0) {
            err = "File is empty: " + path;
            close_map();
            return false;
        }
        size_ = static_cast<size_t>(st.st_size);
        void* ptr = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (ptr == MAP_FAILED) {
            err = "mmap failed for: " + path;
            close_map();
            return false;
        }
        data_ = static_cast<const uint8_t*>(ptr);
        ::madvise(ptr, size_, MADV_SEQUENTIAL | MADV_WILLNEED);
        return true;
    }

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    int fd_ = -1;
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;

    void close_map() {
        if (data_) {
            ::munmap(const_cast<uint8_t*>(data_), size_);
            data_ = nullptr;
            size_ = 0;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
};

inline bool is_finite_vec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline Vec3 load_vec3_unaligned(const uint8_t* p) {
    Vec3 v;
    std::memcpy(&v, p, 12);
    return v;
}

inline uint16_t quantize_coord(float val, float min_val, float inv_scale) {
    float t = (val - min_val) * inv_scale;
    t = std::clamp(t, 0.0f, 1.0f);
    return static_cast<uint16_t>(t * 65535.0f + 0.5f);
}

inline uint32_t choose_threads(size_t items, size_t min_per_thread) {
    if (items < min_per_thread * 2) return 1;
    uint32_t hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    uint32_t max_by_work = static_cast<uint32_t>(items / min_per_thread);
    return std::clamp(hw, 1u, std::min(8u, max_by_work));
}

struct Pass1Stats {
    Vec3 bmin{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 bmax{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };
    double volume6 = 0.0;
    double area2 = 0.0;
    bool has_valid = false;
};

void accumulate_tri_stats(const Vec3& v0, const Vec3& v1, const Vec3& v2, Pass1Stats& st) {
    if (!is_finite_vec(v0) || !is_finite_vec(v1) || !is_finite_vec(v2)) {
        return;
    }
    st.has_valid = true;
    st.bmin = min_v(st.bmin, min_v(v0, min_v(v1, v2)));
    st.bmax = max_v(st.bmax, max_v(v0, max_v(v1, v2)));

    double ax = v0.x, ay = v0.y, az = v0.z;
    double bx = v1.x, by = v1.y, bz = v1.z;
    double cx = v2.x, cy = v2.y, cz = v2.z;

    // Cross(v1, v2)
    double c12x = by * cz - bz * cy;
    double c12y = bz * cx - bx * cz;
    double c12z = bx * cy - by * cx;
    st.volume6 += ax * c12x + ay * c12y + az * c12z;

    // Edge cross for area
    double e1x = bx - ax, e1y = by - ay, e1z = bz - az;
    double e2x = cx - ax, e2y = cy - ay, e2z = cz - az;
    double nx = e1y * e2z - e1z * e2y;
    double ny = e1z * e2x - e1x * e2z;
    double nz = e1x * e2y - e1y * e2x;
    st.area2 += std::sqrt(nx * nx + ny * ny + nz * nz);
}

void finalize_chunk_cone_and_sphere(const MeshData& mesh,
                                    uint32_t chunk_idx,
                                    uint32_t t_begin,
                                    uint32_t t_end,
                                    const Vec3* raw_tris_optional,
                                    const uint8_t* bin_recs_optional,
                                    std::vector<QuantizedVertex>& verts,
                                    std::vector<RenderChunk>& chunks,
                                    const Vec3& inv_scale) {
    RenderChunk& ch = chunks[chunk_idx];
    ch.first_vert = t_begin * 3;
    ch.tri_count = t_end - t_begin;
    ch.vert_count = ch.tri_count * 3;

    Vec3 cmin{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    Vec3 cmax{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };

    Vec3 sum_n{0.0f, 0.0f, 0.0f};
    uint32_t valid_normals = 0;

    // Temporary scratch on stack/small reuse: we also want exact radius from dequantized vertices
    for (uint32_t t = t_begin; t < t_end; ++t) {
        Vec3 v0{}, v1{}, v2{};
        uint16_t attr = 0;
        if (bin_recs_optional) {
            const uint8_t* rec = bin_recs_optional + static_cast<size_t>(t) * 50;
            v0 = load_vec3_unaligned(rec + 12);
            v1 = load_vec3_unaligned(rec + 24);
            v2 = load_vec3_unaligned(rec + 36);
            std::memcpy(&attr, rec + 48, 2);
        } else {
            v0 = raw_tris_optional[static_cast<size_t>(t) * 3 + 0];
            v1 = raw_tris_optional[static_cast<size_t>(t) * 3 + 1];
            v2 = raw_tris_optional[static_cast<size_t>(t) * 3 + 2];
        }

        if (!is_finite_vec(v0) || !is_finite_vec(v1) || !is_finite_vec(v2)) {
            v0 = v1 = v2 = mesh.bbox_min;
        }

        auto store_q = [&](const Vec3& p, uint32_t vi) {
            QuantizedVertex q{
                quantize_coord(p.x, mesh.bbox_min.x, inv_scale.x),
                quantize_coord(p.y, mesh.bbox_min.y, inv_scale.y),
                quantize_coord(p.z, mesh.bbox_min.z, inv_scale.z),
                attr,
            };
            verts[vi] = q;
            Vec3 dq = mesh.dequantize(q);
            cmin = min_v(cmin, dq);
            cmax = max_v(cmax, dq);
            return dq;
        };

        Vec3 d0 = store_q(v0, t * 3 + 0);
        Vec3 d1 = store_q(v1, t * 3 + 1);
        Vec3 d2 = store_q(v2, t * 3 + 2);

        Vec3 cr = cross(d1 - d0, d2 - d0);
        float len = length(cr);
        if (len > 1e-12f) {
            sum_n += cr * (1.0f / len);
            ++valid_normals;
        }
    }

    if (cmin.x > cmax.x) {
        ch.center = mesh.bbox_min;
        ch.radius = 0.0f;
        ch.cone_cullable = false;
        return;
    }

    ch.center = (cmin + cmax) * 0.5f;
    float max_r2 = 0.0f;
    for (uint32_t vi = ch.first_vert; vi < ch.first_vert + ch.vert_count; ++vi) {
        Vec3 dq = mesh.dequantize(verts[vi]);
        max_r2 = std::max(max_r2, length_sq(dq - ch.center));
    }
    ch.radius = std::sqrt(max_r2);

    float sum_len = length(sum_n);
    if (valid_normals == 0 || sum_len < 1e-6f) {
        ch.cone_cullable = false;
        return;
    }

    ch.cone_axis = sum_n * (1.0f / sum_len);
    float min_dot = 1.0f;
    for (uint32_t t = t_begin; t < t_end; ++t) {
        Vec3 d0 = mesh.dequantize(verts[t * 3 + 0]);
        Vec3 d1 = mesh.dequantize(verts[t * 3 + 1]);
        Vec3 d2 = mesh.dequantize(verts[t * 3 + 2]);
        Vec3 cr = cross(d1 - d0, d2 - d0);
        float len = length(cr);
        if (len > 1e-12f) {
            Vec3 n = cr * (1.0f / len);
            min_dot = std::min(min_dot, dot(n, ch.cone_axis));
        }
    }

    if (min_dot <= 0.05f) {
        ch.cone_cullable = false;
        return;
    }
    float sin_alpha = std::sqrt(std::max(0.0f, 1.0f - min_dot * min_dot));
    ch.cone_cutoff = std::min(0.999f, sin_alpha + 1e-4f);
    ch.cone_cullable = true;
}

void compute_quant_scale(MeshData& m, const Pass1Stats& st) {
    m.bbox_min = st.bmin;
    m.bbox_max = st.bmax;
    m.bbox_extent = st.bmax - st.bmin;
    m.quant_scale = {
        m.bbox_extent.x > 1e-12f ? m.bbox_extent.x : 0.0f,
        m.bbox_extent.y > 1e-12f ? m.bbox_extent.y : 0.0f,
        m.bbox_extent.z > 1e-12f ? m.bbox_extent.z : 0.0f,
    };
    m.volume_mm3 = std::fabs(st.volume6) / 6.0;
    m.area_mm2 = st.area2 * 0.5;
}

bool load_binary_stl(const uint8_t* data, uint32_t tri_count, MeshData& out) {
    if (tri_count == 0) {
        out.error = "STL file contains 0 triangles";
        return false;
    }
    const uint8_t* recs = data + 84;
    uint32_t threads = choose_threads(tri_count, 16384);

    std::vector<Pass1Stats> p1(threads);
    if (threads == 1) {
        for (uint32_t t = 0; t < tri_count; ++t) {
            const uint8_t* r = recs + static_cast<size_t>(t) * 50;
            accumulate_tri_stats(
                load_vec3_unaligned(r + 12),
                load_vec3_unaligned(r + 24),
                load_vec3_unaligned(r + 36),
                p1[0]);
        }
    } else {
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (uint32_t tid = 0; tid < threads; ++tid) {
            uint32_t t0 = static_cast<uint32_t>((static_cast<uint64_t>(tri_count) * tid) / threads);
            uint32_t t1 = static_cast<uint32_t>((static_cast<uint64_t>(tri_count) * (tid + 1)) / threads);
            pool.emplace_back([&, tid, t0, t1]() {
                Pass1Stats local{};
                for (uint32_t t = t0; t < t1; ++t) {
                    const uint8_t* r = recs + static_cast<size_t>(t) * 50;
                    accumulate_tri_stats(
                        load_vec3_unaligned(r + 12),
                        load_vec3_unaligned(r + 24),
                        load_vec3_unaligned(r + 36),
                        local);
                }
                p1[tid] = local;
            });
        }
        for (auto& th : pool) th.join();
    }

    Pass1Stats merged{};
    for (const auto& s : p1) {
        if (!s.has_valid) continue;
        merged.has_valid = true;
        merged.bmin = min_v(merged.bmin, s.bmin);
        merged.bmax = max_v(merged.bmax, s.bmax);
        merged.volume6 += s.volume6;
        merged.area2 += s.area2;
    }
    if (!merged.has_valid) {
        out.error = "STL contains no finite triangles";
        return false;
    }

    compute_quant_scale(out, merged);
    Vec3 inv_scale{
        out.quant_scale.x > 0.0f ? (1.0f / out.quant_scale.x) : 0.0f,
        out.quant_scale.y > 0.0f ? (1.0f / out.quant_scale.y) : 0.0f,
        out.quant_scale.z > 0.0f ? (1.0f / out.quant_scale.z) : 0.0f,
    };

    out.tri_count = tri_count;
    out.vertices.resize(static_cast<size_t>(tri_count) * 3);
    uint32_t num_chunks = (tri_count + CHUNK_TRIS - 1) / CHUNK_TRIS;
    out.chunks.resize(num_chunks);

    uint32_t p2_threads = choose_threads(num_chunks, 2);
    if (p2_threads == 1) {
        for (uint32_t c = 0; c < num_chunks; ++c) {
            uint32_t t0 = c * CHUNK_TRIS;
            uint32_t t1 = std::min(tri_count, t0 + CHUNK_TRIS);
            finalize_chunk_cone_and_sphere(out, c, t0, t1, nullptr, recs, out.vertices, out.chunks, inv_scale);
        }
    } else {
        std::vector<std::thread> pool;
        pool.reserve(p2_threads);
        for (uint32_t tid = 0; tid < p2_threads; ++tid) {
            uint32_t c0 = (num_chunks * tid) / p2_threads;
            uint32_t c1 = (num_chunks * (tid + 1)) / p2_threads;
            pool.emplace_back([&, c0, c1]() {
                for (uint32_t c = c0; c < c1; ++c) {
                    uint32_t t0 = c * CHUNK_TRIS;
                    uint32_t t1 = std::min(tri_count, t0 + CHUNK_TRIS);
                    finalize_chunk_cone_and_sphere(
                        out, c, t0, t1, nullptr, recs, out.vertices, out.chunks, inv_scale);
                }
            });
        }
        for (auto& th : pool) th.join();
    }

    return true;
}

inline bool is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

inline const char* skip_ws(const char* p, const char* end) {
    while (p < end && is_ws(*p)) ++p;
    return p;
}

// Fast locale-independent float parser
bool fast_parse_float(const char*& p, const char* end, float& out) {
    p = skip_ws(p, end);
    if (p >= end) return false;

    bool neg = false;
    if (*p == '-') {
        neg = true;
        ++p;
    } else if (*p == '+') {
        ++p;
    }
    if (p >= end) return false;

    uint64_t mant = 0;
    int digits = 0;
    int frac_digits = 0;
    bool has_digit = false;

    while (p < end && *p >= '0' && *p <= '9') {
        has_digit = true;
        if (digits < 18) {
            mant = mant * 10 + static_cast<uint64_t>(*p - '0');
            ++digits;
        } else {
            // Excess integer digits shift decimal exponent right
            --frac_digits;
        }
        ++p;
    }

    if (p < end && *p == '.') {
        ++p;
        while (p < end && *p >= '0' && *p <= '9') {
            has_digit = true;
            if (digits < 18) {
                mant = mant * 10 + static_cast<uint64_t>(*p - '0');
                ++digits;
                ++frac_digits;
            }
            ++p;
        }
    }

    if (!has_digit) return false;

    int exp10 = -frac_digits;
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        bool exp_neg = false;
        if (p < end && (*p == '-' || *p == '+')) {
            exp_neg = (*p == '-');
            ++p;
        }
        int e = 0;
        bool has_e = false;
        while (p < end && *p >= '0' && *p <= '9') {
            has_e = true;
            if (e < 1000) e = e * 10 + (*p - '0');
            ++p;
        }
        if (has_e) {
            exp10 += exp_neg ? -e : e;
        }
    }

    static constexpr double POW10_POS[] = {
        1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,  1e8,  1e9,  1e10, 1e11,
        1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    double val = static_cast<double>(mant);
    if (exp10 > 0) {
        if (exp10 <= 22) {
            val *= POW10_POS[exp10];
        } else {
            val *= std::pow(10.0, static_cast<double>(exp10));
        }
    } else if (exp10 < 0) {
        int n = -exp10;
        if (n <= 22) {
            val /= POW10_POS[n];
        } else {
            val *= std::pow(10.0, static_cast<double>(exp10));
        }
    }
    if (neg) val = -val;
    out = static_cast<float>(val);
    return true;
}

void parse_ascii_slice(const char* cur, const char* end, std::vector<Vec3>& out_verts) {
    Vec3 tri[3]{};
    int vc = 0;
    while (cur < end) {
        cur = skip_ws(cur, end);
        if (cur >= end) break;

        // Match "vertex" followed by whitespace
        if ((end - cur) >= 7 && std::memcmp(cur, "vertex", 6) == 0 && is_ws(cur[6])) {
            cur += 6;
            float x = 0.0f, y = 0.0f, z = 0.0f;
            if (fast_parse_float(cur, end, x) &&
                fast_parse_float(cur, end, y) &&
                fast_parse_float(cur, end, z)) {
                tri[vc++] = {x, y, z};
                if (vc == 3) {
                    out_verts.push_back(tri[0]);
                    out_verts.push_back(tri[1]);
                    out_verts.push_back(tri[2]);
                    vc = 0;
                }
            }
        }
        // Advance to next newline
        const void* nl = std::memchr(cur, '\n', static_cast<size_t>(end - cur));
        if (!nl) break;
        cur = static_cast<const char*>(nl) + 1;
    }
}

const char* find_next_endfacet_after(const char* p, const char* end) {
    while (p < end) {
        const void* hit = std::memchr(p, 'e', static_cast<size_t>(end - p));
        if (!hit) return end;
        const char* c = static_cast<const char*>(hit);
        if ((end - c) >= 8 && std::memcmp(c, "endfacet", 8) == 0 &&
            (c == p || is_ws(c[-1])) &&
            ((c + 8) == end || is_ws(c[8]))) {
            const void* nl = std::memchr(c + 8, '\n', static_cast<size_t>(end - (c + 8)));
            return nl ? static_cast<const char*>(nl) + 1 : end;
        }
        p = c + 1;
    }
    return end;
}

bool load_ascii_stl(const uint8_t* data, size_t size, MeshData& out) {
    const char* begin = reinterpret_cast<const char*>(data);
    const char* end = begin + size;

    uint32_t threads = choose_threads(size, 262144);
    std::vector<Vec3> raw_verts;

    if (threads == 1) {
        raw_verts.reserve(size / 140 * 3);
        parse_ascii_slice(begin, end, raw_verts);
    } else {
        std::vector<const char*> splits(threads + 1);
        splits[0] = begin;
        splits[threads] = end;
        for (uint32_t i = 1; i < threads; ++i) {
            const char* approx = begin + (size * i) / threads;
            if (approx < splits[i - 1]) approx = splits[i - 1];
            splits[i] = find_next_endfacet_after(approx, end);
        }
        std::vector<std::vector<Vec3>> per_thread(threads);
        std::vector<std::thread> pool;
        pool.reserve(threads);
        for (uint32_t tid = 0; tid < threads; ++tid) {
            pool.emplace_back([&, tid]() {
                const char* s0 = splits[tid];
                const char* s1 = splits[tid + 1];
                if (s1 > s0) {
                    per_thread[tid].reserve(static_cast<size_t>(s1 - s0) / 140 * 3);
                    parse_ascii_slice(s0, s1, per_thread[tid]);
                }
            });
        }
        for (auto& th : pool) th.join();

        size_t total = 0;
        for (const auto& v : per_thread) total += v.size();
        raw_verts.reserve(total);
        for (const auto& v : per_thread) {
            raw_verts.insert(raw_verts.end(), v.begin(), v.end());
        }
    }

    uint32_t tri_count = static_cast<uint32_t>(raw_verts.size() / 3);
    if (tri_count == 0) {
        out.error = "ASCII STL contains 0 triangles";
        return false;
    }

    Pass1Stats st{};
    for (uint32_t t = 0; t < tri_count; ++t) {
        accumulate_tri_stats(raw_verts[t * 3 + 0], raw_verts[t * 3 + 1], raw_verts[t * 3 + 2], st);
    }
    if (!st.has_valid) {
        out.error = "ASCII STL contains no finite triangles";
        return false;
    }

    compute_quant_scale(out, st);
    Vec3 inv_scale{
        out.quant_scale.x > 0.0f ? (1.0f / out.quant_scale.x) : 0.0f,
        out.quant_scale.y > 0.0f ? (1.0f / out.quant_scale.y) : 0.0f,
        out.quant_scale.z > 0.0f ? (1.0f / out.quant_scale.z) : 0.0f,
    };

    out.tri_count = tri_count;
    out.vertices.resize(static_cast<size_t>(tri_count) * 3);
    uint32_t num_chunks = (tri_count + CHUNK_TRIS - 1) / CHUNK_TRIS;
    out.chunks.resize(num_chunks);
    for (uint32_t c = 0; c < num_chunks; ++c) {
        uint32_t t0 = c * CHUNK_TRIS;
        uint32_t t1 = std::min(tri_count, t0 + CHUNK_TRIS);
        finalize_chunk_cone_and_sphere(
            out, c, t0, t1, raw_verts.data(), nullptr, out.vertices, out.chunks, inv_scale);
    }

    return true;
}

}  // namespace

MeshData load_stl(const std::string& path) {
    auto t0 = std::chrono::steady_clock::now();
    MeshData out;
    out.path = path;

    MappedFile mf;
    if (!mf.open_read(path, out.error)) {
        return out;
    }
    out.file_bytes = mf.size();

    bool parsed = false;
    if (mf.size() >= 84) {
        uint32_t tri_count = 0;
        std::memcpy(&tri_count, mf.data() + 80, 4);
        uint64_t expected_bin_bytes = 84ULL + static_cast<uint64_t>(tri_count) * 50ULL;
        if (mf.size() == expected_bin_bytes) {
            out.is_binary = true;
            parsed = load_binary_stl(mf.data(), tri_count, out);
        }
    }

    if (!parsed && out.error.empty()) {
        // Check ASCII "solid" signature
        const char* p = reinterpret_cast<const char*>(mf.data());
        const char* end = p + mf.size();
        p = skip_ws(p, end);
        if ((end - p) >= 5 && std::memcmp(p, "solid", 5) == 0 &&
            ((end - p) == 5 || is_ws(p[5]))) {
            out.is_binary = false;
            parsed = load_ascii_stl(mf.data(), mf.size(), out);
        } else {
            out.error = "Unrecognized or truncated STL file";
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    out.load_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (parsed) {
        out.ok = true;
        out.vram_bytes = static_cast<uint64_t>(out.vertices.size()) * sizeof(QuantizedVertex);
    }
    return out;
}

}  // namespace faset
