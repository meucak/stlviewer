#include "test_framework.hpp"
#include "culling.hpp"
#include "stl_loader.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

using namespace faset;

namespace {

void write_bin_stl_raw(const std::string& path, const std::vector<Vec3>& verts) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    char hdr[80]{};
    f.write(hdr, 80);
    uint32_t tris = static_cast<uint32_t>(verts.size() / 3);
    f.write(reinterpret_cast<const char*>(&tris), 4);
    float n[3] = {0, 0, 0};
    uint16_t attr = 0;
    for (uint32_t i = 0; i < tris; ++i) {
        f.write(reinterpret_cast<const char*>(n), 12);
        f.write(reinterpret_cast<const char*>(&verts[i * 3 + 0]), 12);
        f.write(reinterpret_cast<const char*>(&verts[i * 3 + 1]), 12);
        f.write(reinterpret_cast<const char*>(&verts[i * 3 + 2]), 12);
        f.write(reinterpret_cast<const char*>(&attr), 2);
    }
}

std::vector<Vec3> make_single_plane_patch() {
    // 16x16 grid on Z=0 pointing +Z, small area
    std::vector<Vec3> v;
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            float x0 = static_cast<float>(x), x1 = x0 + 1.0f;
            float y0 = static_cast<float>(y), y1 = y0 + 1.0f;
            v.push_back({x0, y0, 0}); v.push_back({x1, y0, 0}); v.push_back({x1, y1, 0});
            v.push_back({x0, y0, 0}); v.push_back({x1, y1, 0}); v.push_back({x0, y1, 0});
        }
    }
    return v;
}

std::vector<Vec3> make_subdivided_sphere(float r) {
    // Build an octahedron subdivided 2x with separate faces so each face/chunk tests cones
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<Vec3> pts = {
        {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
        { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
        { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1},
    };
    for (auto& p : pts) p = normalize(p) * r;
    const int idx[20][3] = {
        {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
        {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
        {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
        {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1}
    };
    std::vector<Vec3> out;
    for (int f = 0; f < 20; ++f) {
        Vec3 a = pts[idx[f][0]], b = pts[idx[f][1]], c = pts[idx[f][2]];
        // Subdivide into 4x4 small tris
        auto sub = [&](auto& self, Vec3 p0, Vec3 p1, Vec3 p2, int depth) -> void {
            if (depth == 0) {
                out.push_back(p0); out.push_back(p1); out.push_back(p2);
                return;
            }
            Vec3 m01 = normalize((p0 + p1) * 0.5f) * r;
            Vec3 m12 = normalize((p1 + p2) * 0.5f) * r;
            Vec3 m20 = normalize((p2 + p0) * 0.5f) * r;
            self(self, p0, m01, m20, depth - 1);
            self(self, m01, p1, m12, depth - 1);
            self(self, m20, m12, p2, depth - 1);
            self(self, m01, m12, m20, depth - 1);
        };
        sub(sub, a, b, c, 2);
    }
    return out;
}

}  // namespace

TEST(frustum_culling_and_span_coalescing) {
    std::vector<RenderChunk> chunks(4);
    chunks[0] = {0, 300, 100, Vec3{0, 0, 0}, 1.0f, Vec3{0, -1, 0}, 0.1f, true};
    chunks[1] = {300, 300, 100, Vec3{0, 1000, 0}, 1.0f, Vec3{0, -1, 0}, 0.1f, true};  // beyond far
    chunks[2] = {600, 300, 100, Vec3{2, 2, 0}, 1.0f, Vec3{0, -1, 0}, 0.1f, true};
    chunks[3] = {900, 300, 100, Vec3{2, 3, 0}, 1.0f, Vec3{0, -1, 0}, 0.1f, true};

    Mat4 v = Mat4::look_at(Vec3{0, -20, 0}, Vec3{0, 0, 0}, Vec3{0, 0, 1});
    Mat4 p = Mat4::perspective(45.0f * DEG2RAD, 1.0f, 1.0f, 200.0f);
    Frustum fr = Frustum::from_view_proj(p * v);

    std::vector<DrawSpan> spans;
    CullStats stats{};
    CullOptions opts{};
    opts.backface_cone_cull = false;
    opts.eye = Vec3{0, -20, 0};
    opts.cam_forward = Vec3{0, 1, 0};

    cull_chunks(chunks, fr, opts, spans, &stats);
    CHECK_EQ(stats.visible_chunks, 3u);
    CHECK_EQ(stats.frustum_culled, 1u);
    CHECK_EQ(stats.cone_culled, 0u);
    // chunks 0 visible, chunk 1 culled, chunks 2+3 coalesced => 2 draw spans!
    CHECK_EQ(spans.size(), 2u);
    CHECK_EQ(spans[0].first_vert, 0);
    CHECK_EQ(spans[0].vert_count, 300);
    CHECK_EQ(spans[1].first_vert, 600);
    CHECK_EQ(spans[1].vert_count, 600);
}

TEST(conservative_normal_cone_zero_false_culls) {
    // Test both a flat +Z plane and a multi-chunk sphere from 64 camera directions
    const std::string p_plane = "/tmp/faset_cull_plane.stl";
    const std::string p_sphere = "/tmp/faset_cull_sphere.stl";
    write_bin_stl_raw(p_plane, make_single_plane_patch());
    write_bin_stl_raw(p_sphere, make_subdivided_sphere(25.0f));

    MeshData m_plane = load_stl(p_plane);
    MeshData m_sphere = load_stl(p_sphere);
    CHECK(m_plane.ok);
    CHECK(m_sphere.ok);

    // Split sphere into small per-face chunks (~16 tris each) so cones are tight and active
    std::vector<RenderChunk> tight_chunks;
    for (uint32_t t0 = 0; t0 < m_sphere.tri_count; t0 += 16) {
        uint32_t t1 = std::min(m_sphere.tri_count, t0 + 16);
        RenderChunk ch{};
        ch.first_vert = t0 * 3;
        ch.tri_count = t1 - t0;
        ch.vert_count = ch.tri_count * 3;
        Vec3 cmin = m_sphere.dequantize(m_sphere.vertices[ch.first_vert]);
        Vec3 cmax = cmin;
        Vec3 sum_n{};
        for (uint32_t t = t0; t < t1; ++t) {
            Vec3 v0 = m_sphere.dequantize(m_sphere.vertices[t * 3 + 0]);
            Vec3 v1 = m_sphere.dequantize(m_sphere.vertices[t * 3 + 1]);
            Vec3 v2 = m_sphere.dequantize(m_sphere.vertices[t * 3 + 2]);
            cmin = min_v(cmin, min_v(v0, min_v(v1, v2)));
            cmax = max_v(cmax, max_v(v0, max_v(v1, v2)));
            sum_n += normalize(cross(v1 - v0, v2 - v0));
        }
        ch.center = (cmin + cmax) * 0.5f;
        float r2 = 0.0f;
        for (uint32_t vi = ch.first_vert; vi < ch.first_vert + ch.vert_count; ++vi) {
            r2 = std::max(r2, length_sq(m_sphere.dequantize(m_sphere.vertices[vi]) - ch.center));
        }
        ch.radius = std::sqrt(r2);
        ch.cone_axis = normalize(sum_n);
        float md = 1.0f;
        for (uint32_t t = t0; t < t1; ++t) {
            Vec3 v0 = m_sphere.dequantize(m_sphere.vertices[t * 3 + 0]);
            Vec3 v1 = m_sphere.dequantize(m_sphere.vertices[t * 3 + 1]);
            Vec3 v2 = m_sphere.dequantize(m_sphere.vertices[t * 3 + 2]);
            md = std::min(md, dot(normalize(cross(v1 - v0, v2 - v0)), ch.cone_axis));
        }
        ch.cone_cullable = (md > 0.05f);
        ch.cone_cutoff = std::min(0.999f, std::sqrt(std::max(0.0f, 1.0f - md * md)) + 1e-4f);
        tight_chunks.push_back(ch);
    }

    auto verify_mesh_sweep = [&](const MeshData& m, const std::vector<RenderChunk>& chunks, float cam_dist) {
        int total_culled_by_cone = 0;
        for (int i = 0; i < 64; ++i) {
            // Fibonacci sphere camera sweep
            float y = 1.0f - (2.0f * (static_cast<float>(i) + 0.5f)) / 64.0f;
            float rad = std::sqrt(std::max(0.0f, 1.0f - y * y));
            float theta = 2.39996323f * static_cast<float>(i);
            Vec3 dir{std::cos(theta) * rad, std::sin(theta) * rad, y};
            Vec3 center = (m.bbox_min + m.bbox_max) * 0.5f;
            Vec3 eye = center + dir * cam_dist;
            Vec3 up = std::fabs(dir.z) > 0.9f ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
            Mat4 vp = Mat4::perspective(45.0f * DEG2RAD, 1.0f, 0.5f, cam_dist * 4.0f) *
                      Mat4::look_at(eye, center, up);
            Frustum fr = Frustum::from_view_proj(vp);

            CullOptions opts{};
            opts.backface_cone_cull = true;
            opts.is_ortho = false;
            opts.eye = eye;
            opts.cam_forward = normalize(center - eye);

            std::vector<DrawSpan> spans;
            CullStats stats{};
            cull_chunks(chunks, fr, opts, spans, &stats);
            total_culled_by_cone += static_cast<int>(stats.cone_culled);

            // Check every culled chunk: ZERO triangles may be front-facing!
            std::vector<bool> vis(chunks.size(), false);
            for (const auto& sp : spans) {
                for (size_t c = 0; c < chunks.size(); ++c) {
                    if (static_cast<int32_t>(chunks[c].first_vert) >= sp.first_vert &&
                        static_cast<int32_t>(chunks[c].first_vert + chunks[c].vert_count) <= sp.first_vert + sp.vert_count) {
                        vis[c] = true;
                    }
                }
            }
            for (size_t c = 0; c < chunks.size(); ++c) {
                if (!vis[c] && fr.intersects_sphere(chunks[c].center, chunks[c].radius)) {
                    for (uint32_t t = 0; t < chunks[c].tri_count; ++t) {
                        uint32_t vi = chunks[c].first_vert + t * 3;
                        Vec3 v0 = m.dequantize(m.vertices[vi + 0]);
                        Vec3 v1 = m.dequantize(m.vertices[vi + 1]);
                        Vec3 v2 = m.dequantize(m.vertices[vi + 2]);
                        Vec3 n = cross(v1 - v0, v2 - v0);
                        // Front-facing iff dot(v0 - eye, n) <= 0
                        CHECK(dot(v0 - eye, n) > 0.0f);
                    }
                }
            }

            // When backface_cone_cull=false, ZERO chunks are cone-culled
            CullOptions off = opts;
            off.backface_cone_cull = false;
            CullStats st_off{};
            cull_chunks(chunks, fr, off, spans, &st_off);
            CHECK_EQ(st_off.cone_culled, 0u);
        }
        CHECK(total_culled_by_cone > 0);
    };

    verify_mesh_sweep(m_plane, m_plane.chunks, 40.0f);
    verify_mesh_sweep(m_sphere, tight_chunks, 90.0f);

    std::remove(p_plane.c_str());
    std::remove(p_sphere.c_str());
}
