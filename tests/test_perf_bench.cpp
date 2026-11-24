#include "test_framework.hpp"
#include "camera.hpp"
#include "culling.hpp"
#include "stl_loader.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace faset;

TEST(perf_one_million_triangles_load_quant_cull) {
    const std::string p = "/tmp/faset_test_1m_stress.stl";
    constexpr uint32_t TRIS = 1000000;

    // Generate 1,000,000-triangle (~50 MB) binary STL spanning 100 x 100 x 100 mm
    {
        std::ofstream f(p, std::ios::binary | std::ios::trunc);
        char hdr[80] = "faset 1M synthetic stress test";
        f.write(hdr, 80);
        f.write(reinterpret_cast<const char*>(&TRIS), 4);

        // Write in 64K-tri buffered blocks for fast test setup
        constexpr uint32_t BLK = 16384;
        std::vector<uint8_t> buf(static_cast<size_t>(BLK) * 50, 0);
        uint32_t written = 0;
        while (written < TRIS) {
            uint32_t count = std::min(BLK, TRIS - written);
            for (uint32_t i = 0; i < count; ++i) {
                uint32_t g = written + i;
                uint32_t ix = g % 1000;
                uint32_t iy = g / 1000;
                float x = static_cast<float>(ix) * 0.1f;
                float y = static_cast<float>(iy) * 0.1f;
                float z = (g & 1) ? 100.0f : 0.0f;
                float tri[9] = {
                    x, y, z,
                    x + 0.08f, y, z,
                    x, y + 0.08f, z + 0.05f
                };
                uint8_t* dst = buf.data() + static_cast<size_t>(i) * 50;
                std::memcpy(dst + 12, tri, 36);
            }
            f.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(count) * 50);
            written += count;
        }
    }

    // Warm pass (page cache populated, matching real repeated opens)
    MeshData m = load_stl(p);
    CHECK(m.ok);
    CHECK(m.is_binary);
    CHECK_EQ(m.tri_count, TRIS);
    CHECK_EQ(m.chunks.size(), static_cast<size_t>((TRIS + CHUNK_TRIS - 1) / CHUNK_TRIS));

    // Check quantization error on 100 mm span (< 0.002 mm)
    Vec3 q0 = m.dequantize(m.vertices[0]);
    CHECK_NEAR(q0.x, 0.0f, 0.002f);
    CHECK_NEAR(q0.y, 0.0f, 0.002f);
    CHECK_NEAR(q0.z, 0.0f, 0.002f);

    Vec3 q_last = m.dequantize(m.vertices[(TRIS - 1) * 3 + 2]);
    // Last tri (999999): ix=999 (x=99.9), iy=999 (y=99.9), z=100.0 + 0.05 = 100.05
    CHECK_NEAR(q_last.x, 99.9f, 0.002f);
    CHECK_NEAR(q_last.y, 99.9f + 0.08f, 0.002f);
    CHECK_NEAR(q_last.z, 100.05f, 0.002f);

    // Performance gates on Sandy Bridge 4-thread CPU
    CHECK(m.load_ms < 500.0);

    Camera cam;
    cam.frame_bounds(m.bbox_min, m.bbox_max, 1280, 720, false);
    Frustum fr = Frustum::from_view_proj(cam.view_proj(1280, 720));
    CullOptions opts{true, false, cam.eye(), cam.forward()};
    std::vector<DrawSpan> spans;
    CullStats cs{};

    auto c0 = std::chrono::steady_clock::now();
    cull_chunks(m.chunks, fr, opts, spans, &cs);
    auto c1 = std::chrono::steady_clock::now();
    double cull_ms = std::chrono::duration<double, std::milli>(c1 - c0).count();
    CHECK(cull_ms < 1.0);
    CHECK(cs.visible_chunks > 0);

    std::printf("    [perf 1M tris] load=%.2f ms, cull=%.4f ms (%zu chunks, vram=%.2f MB)\n",
                m.load_ms, cull_ms, m.chunks.size(),
                static_cast<double>(m.vram_bytes) / (1024.0 * 1024.0));

    std::remove(p.c_str());
}
