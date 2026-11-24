#include "test_framework.hpp"
#include "stl_loader.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace faset;

namespace {

struct TriSpec {
    Vec3 v0, v1, v2;
    uint16_t attr = 0;
};

void write_binary_stl(const std::string& path, const char* header80, const std::vector<TriSpec>& tris) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    char hdr[80]{};
    if (header80) {
        std::strncpy(hdr, header80, 79);
    }
    f.write(hdr, 80);
    uint32_t count = static_cast<uint32_t>(tris.size());
    f.write(reinterpret_cast<const char*>(&count), 4);
    for (const auto& t : tris) {
        float n[3] = {0.0f, 0.0f, 0.0f};  // deliberately zero normal: loader ignores STL normal
        f.write(reinterpret_cast<const char*>(n), 12);
        f.write(reinterpret_cast<const char*>(&t.v0), 12);
        f.write(reinterpret_cast<const char*>(&t.v1), 12);
        f.write(reinterpret_cast<const char*>(&t.v2), 12);
        f.write(reinterpret_cast<const char*>(&t.attr), 2);
    }
}

std::vector<TriSpec> make_cube_tris(float side) {
    Vec3 p000{0, 0, 0}, p100{side, 0, 0}, p110{side, side, 0}, p010{0, side, 0};
    Vec3 p001{0, 0, side}, p101{side, 0, side}, p111{side, side, side}, p011{0, side, side};
    return {
        // -Z bottom
        {p000, p010, p110}, {p000, p110, p100},
        // +Z top
        {p001, p101, p111}, {p001, p111, p011},
        // -Y front
        {p000, p100, p101}, {p000, p101, p001},
        // +Y back
        {p010, p011, p111}, {p010, p111, p110},
        // -X left
        {p000, p001, p011}, {p000, p011, p010},
        // +X right
        {p100, p110, p111}, {p100, p111, p101},
    };
}

}  // namespace

TEST(binary_stl_cube_bbox_quant_volume_area) {
    const std::string p = "/tmp/faset_test_cube.stl";
    auto tris = make_cube_tris(10.0f);
    write_binary_stl(p, "faset binary test", tris);

    MeshData m = load_stl(p);
    CHECK(m.ok);
    CHECK(m.is_binary);
    CHECK_EQ(m.tri_count, 12u);
    CHECK_EQ(m.vertices.size(), 36u);
    CHECK_NEAR(m.bbox_min.x, 0.0f, 1e-5f);
    CHECK_NEAR(m.bbox_min.y, 0.0f, 1e-5f);
    CHECK_NEAR(m.bbox_min.z, 0.0f, 1e-5f);
    CHECK_NEAR(m.bbox_max.x, 10.0f, 1e-5f);
    CHECK_NEAR(m.bbox_max.y, 10.0f, 1e-5f);
    CHECK_NEAR(m.bbox_max.z, 10.0f, 1e-5f);
    CHECK_NEAR(m.volume_mm3, 1000.0, 0.1);
    CHECK_NEAR(m.area_mm2, 600.0, 0.1);

    for (size_t i = 0; i < tris.size(); ++i) {
        Vec3 r0 = m.dequantize(m.vertices[i * 3 + 0]);
        Vec3 r1 = m.dequantize(m.vertices[i * 3 + 1]);
        Vec3 r2 = m.dequantize(m.vertices[i * 3 + 2]);
        CHECK_NEAR(r0.x, tris[i].v0.x, 0.0005f);
        CHECK_NEAR(r0.y, tris[i].v0.y, 0.0005f);
        CHECK_NEAR(r0.z, tris[i].v0.z, 0.0005f);
        CHECK_NEAR(r1.x, tris[i].v1.x, 0.0005f);
        CHECK_NEAR(r2.z, tris[i].v2.z, 0.0005f);
    }
    std::remove(p.c_str());
}

TEST(binary_stl_solid_header_trap) {
    const std::string p = "/tmp/faset_test_solid_trap.stl";
    auto tris = make_cube_tris(5.0f);
    write_binary_stl(p, "solid this_looks_like_ascii_but_is_binary", tris);

    MeshData m = load_stl(p);
    CHECK(m.ok);
    CHECK(m.is_binary);
    CHECK_EQ(m.tri_count, 12u);
    CHECK_NEAR(m.bbox_max.x, 5.0f, 1e-5f);
    std::remove(p.c_str());
}

TEST(ascii_stl_crlf_scientific_negative) {
    const std::string p = "/tmp/faset_test_ascii.stl";
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        f << "solid test_part\r\n"
          << "  facet normal 9.9 9.9 9.9\r\n"
          << "\touter loop\r\n"
          << "      vertex   -1.25e-02   0.0   +2.5E+01\r\n"
          << "      vertex 10.0 -5.5 25.0\r\n"
          << "      vertex 0.0 12.5 25.0\r\n"
          << "    endloop\r\n"
          << "  endfacet\r\n"
          << "facet normal 0 0 0\n"
          << "  outer loop\n"
          << "    vertex 1.0 2.0 3.0\n"
          << "    vertex 4.0 5.0 6.0\n"
          << "    vertex 7.0 8.0 9.0\n"
          << "  endloop\n"
          << "endfacet\n"
          << "endsolid test_part\r\n";
    }

    MeshData m = load_stl(p);
    CHECK(m.ok);
    CHECK(!m.is_binary);
    CHECK_EQ(m.tri_count, 2u);
    CHECK_NEAR(m.bbox_min.x, -0.0125f, 1e-5f);
    CHECK_NEAR(m.bbox_min.y, -5.5f, 1e-5f);
    CHECK_NEAR(m.bbox_min.z, 3.0f, 1e-5f);
    CHECK_NEAR(m.bbox_max.x, 10.0f, 1e-5f);
    CHECK_NEAR(m.bbox_max.y, 12.5f, 1e-5f);
    CHECK_NEAR(m.bbox_max.z, 25.0f, 1e-5f);
    std::remove(p.c_str());
}

TEST(utf8_turkish_path_and_flat_axis_and_nan) {
    const std::string p = "/tmp/faset_test_\xc3\xa7" "amurluk_\xc4\x9f\xc3\xbc\xc5\x9f\xc3\xb6\xc3\xa7.stl";
    float nanf = std::numeric_limits<float>::quiet_NaN();
    std::vector<TriSpec> tris = {
        {{1.0f, 2.0f, 5.0f}, {11.0f, 2.0f, 5.0f}, {11.0f, 12.0f, 5.0f}},
        {{1.0f, 2.0f, 5.0f}, {11.0f, 12.0f, 5.0f}, {1.0f, 12.0f, 5.0f}},
        {{nanf, 0.0f, 5.0f}, {0.0f, 0.0f, 5.0f}, {1.0f, 1.0f, 5.0f}},
    };
    write_binary_stl(p, "utf8 flat nan", tris);

    MeshData m = load_stl(p);
    CHECK(m.ok);
    CHECK_NEAR(m.bbox_min.x, 1.0f, 1e-5f);
    CHECK_NEAR(m.bbox_max.x, 11.0f, 1e-5f);
    CHECK_NEAR(m.bbox_min.z, 5.0f, 1e-5f);
    CHECK_NEAR(m.bbox_max.z, 5.0f, 1e-5f);
    Vec3 d0 = m.dequantize(m.vertices[0]);
    CHECK(std::isfinite(d0.x) && std::isfinite(d0.y) && std::isfinite(d0.z));
    CHECK_NEAR(d0.z, 5.0f, 1e-5f);
    // NaN vertex should be sanitized cleanly and never produce non-finite dequantize
    Vec3 dnan = m.dequantize(m.vertices[6]);
    CHECK(std::isfinite(dnan.x) && std::isfinite(dnan.y) && std::isfinite(dnan.z));
    std::remove(p.c_str());
}

TEST(empty_nonexistent_corrupt_files) {
    MeshData missing = load_stl("/tmp/definitely_does_not_exist_faset_xyz.stl");
    CHECK(!missing.ok);
    CHECK(!missing.error.empty());

    const std::string empty_p = "/tmp/faset_test_empty.stl";
    { std::ofstream f(empty_p, std::ios::binary | std::ios::trunc); }
    MeshData empty = load_stl(empty_p);
    CHECK(!empty.ok);
    CHECK(!empty.error.empty());
    std::remove(empty_p.c_str());

    const std::string corrupt_p = "/tmp/faset_test_corrupt.stl";
    {
        std::ofstream f(corrupt_p, std::ios::binary | std::ios::trunc);
        char junk[100]{1, 2, 3, 4};
        f.write(junk, 100);
    }
    MeshData corrupt = load_stl(corrupt_p);
    CHECK(!corrupt.ok);
    CHECK(!corrupt.error.empty());
    std::remove(corrupt_p.c_str());
}

TEST(chunking_bounds_cover_all_vertices) {
    const std::string p = "/tmp/faset_test_chunks.stl";
    std::vector<TriSpec> tris;
    tris.reserve(10000);
    for (uint32_t i = 0; i < 10000; ++i) {
        float x = static_cast<float>(i % 100);
        float y = static_cast<float>(i / 100);
        tris.push_back({{x, y, 0.0f}, {x + 0.5f, y, 0.0f}, {x, y + 0.5f, 1.0f}});
    }
    write_binary_stl(p, "chunk test", tris);

    MeshData m = load_stl(p);
    CHECK(m.ok);
    CHECK_EQ(m.tri_count, 10000u);
    CHECK(m.chunks.size() >= 3u);

    uint32_t sum_tris = 0;
    uint32_t expected_first = 0;
    for (const auto& ch : m.chunks) {
        CHECK_EQ(ch.first_vert, expected_first);
        CHECK_EQ(ch.vert_count, ch.tri_count * 3u);
        expected_first += ch.vert_count;
        sum_tris += ch.tri_count;
        for (uint32_t vi = ch.first_vert; vi < ch.first_vert + ch.vert_count; ++vi) {
            Vec3 v = m.dequantize(m.vertices[vi]);
            float d = length(v - ch.center);
            CHECK(d <= ch.radius + 1e-2f);
        }
    }
    CHECK_EQ(sum_tris, m.tri_count);
    std::remove(p.c_str());
}
