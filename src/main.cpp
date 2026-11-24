#include "culling.hpp"
#include "platform_x11.hpp"
#include "stl_loader.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::printf(
        "stlviewer 1.0.0 — Bare-Metal Hardware STL Viewer (C++20 / X11-GLX / OpenGL 3.3 Core)\n\n"
        "Usage:\n"
        "  stlviewer [file.stl]                 Launch interactive 3D viewer\n"
        "  stlviewer --info <file.stl>          Print STL geometry, memory & load stats\n"
        "  stlviewer --bench <file.stl> [--runs N]  Benchmark mmap+parse+quantize+cull\n"
        "  stlviewer --self-test-gl <file.stl>  Verify real OpenGL 3.3 Core rendering & picking\n"
        "  stlviewer --screenshot <out.ppm> <file.stl> [options]\n"
        "      --width W --height H --theme dark|light --matcap 0..3\n"
        "      --slice --wire --xray --twosided --no-hud\n"
        "  stlviewer --version                  Print version\n"
        "  stlviewer --help                     Print this help\n");
}

int cmd_info(const std::string& path) {
    faset::MeshData m = faset::load_stl(path);
    if (!m.ok) {
        std::fprintf(stderr, "Error: %s\n", m.error.c_str());
        return 1;
    }
    std::printf("File       : %s\n", m.path.c_str());
    std::printf("Format     : %s\n", m.is_binary ? "Binary STL" : "ASCII STL");
    std::printf("File Size  : %.2f MB (%llu bytes)\n",
                static_cast<double>(m.file_bytes) / (1024.0 * 1024.0),
                static_cast<unsigned long long>(m.file_bytes));
    std::printf("Triangles  : %u (%zu chunks)\n", m.tri_count, m.chunks.size());
    std::printf("VRAM (VBO) : %.2f MB (8 bytes/vertex quantized UNORM16)\n",
                static_cast<double>(m.vram_bytes) / (1024.0 * 1024.0));
    std::printf("Load Time  : %.2f ms\n", m.load_ms);
    std::printf("BBox Min   : (%.3f, %.3f, %.3f) mm\n", m.bbox_min.x, m.bbox_min.y, m.bbox_min.z);
    std::printf("BBox Max   : (%.3f, %.3f, %.3f) mm\n", m.bbox_max.x, m.bbox_max.y, m.bbox_max.z);
    std::printf("Dimensions : %.3f x %.3f x %.3f mm\n",
                m.bbox_extent.x, m.bbox_extent.y, m.bbox_extent.z);
    std::printf("Volume     : %.3f cm^3 (%.1f mm^3)\n", m.volume_mm3 * 0.001, m.volume_mm3);
    std::printf("Area       : %.3f cm^2 (%.1f mm^2)\n", m.area_mm2 * 0.01, m.area_mm2);
    return 0;
}

int cmd_bench(const std::string& path, int runs) {
    runs = std::clamp(runs, 1, 50);
    double sum_ms = 0.0, min_ms = 1e9, max_ms = 0.0;
    faset::MeshData last;
    for (int i = 0; i < runs; ++i) {
        last = faset::load_stl(path);
        if (!last.ok) {
            std::fprintf(stderr, "Error: %s\n", last.error.c_str());
            return 1;
        }
        sum_ms += last.load_ms;
        min_ms = std::min(min_ms, last.load_ms);
        max_ms = std::max(max_ms, last.load_ms);
    }
    double avg_ms = sum_ms / runs;
    double mtris_s = (static_cast<double>(last.tri_count) / 1e6) / (min_ms * 0.001);
    double mb_s = (static_cast<double>(last.file_bytes) / (1024.0 * 1024.0)) / (min_ms * 0.001);

    // Also benchmark CPU-side frustum + cone culling across 64 viewpoints
    faset::Camera cam;
    cam.frame_bounds(last.bbox_min, last.bbox_max, 1280, 720, false);
    std::vector<faset::DrawSpan> spans;
    faset::CullStats cs{};
    auto c0 = std::chrono::steady_clock::now();
    uint64_t total_cone_culled = 0;
    for (int i = 0; i < 64; ++i) {
        cam.orbit(20.0f, 0.0f, 1280, 720);
        faset::Frustum fr = faset::Frustum::from_view_proj(cam.view_proj(1280, 720));
        faset::CullOptions opts{true, false, cam.eye(), cam.forward()};
        faset::cull_chunks(last.chunks, fr, opts, spans, &cs);
        total_cone_culled += cs.cone_culled;
    }
    auto c1 = std::chrono::steady_clock::now();
    double cull_us = std::chrono::duration<double, std::micro>(c1 - c0).count() / 64.0;

    std::printf("Bench (%d run%s): %s [%s, %u tris, %.2f MB -> %.2f MB VRAM]\n",
                runs, runs == 1 ? "" : "s", last.path.c_str(),
                last.is_binary ? "Binary" : "ASCII",
                last.tri_count,
                static_cast<double>(last.file_bytes) / (1024.0 * 1024.0),
                static_cast<double>(last.vram_bytes) / (1024.0 * 1024.0));
    std::printf("  Load+Quant : min %.2f ms  |  avg %.2f ms  |  max %.2f ms  (%.2f M tris/s, %.0f MB/s)\n",
                min_ms, avg_ms, max_ms, mtris_s, mb_s);
    std::printf("  Chunk Cull : %.1f us/frame (%zu chunks, avg %.1f cone-culled/frame)\n",
                cull_us, last.chunks.size(), static_cast<double>(total_cone_culled) / 64.0);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) {
        return faset::run_viewer("");
    }

    std::string mode;
    std::string file_arg;
    std::string out_ppm;
    int bench_runs = 5;
    faset::ScreenshotOpts shot{};

    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strcmp(a, "--version") == 0 || std::strcmp(a, "-v") == 0) {
            std::printf("stlviewer 1.0.0 (C++20 / X11-GLX / OpenGL 3.3 Core)\n");
            return 0;
        } else if (std::strcmp(a, "--help") == 0 || std::strcmp(a, "-h") == 0) {
            print_usage();
            return 0;
        } else if (std::strcmp(a, "--info") == 0) {
            mode = "info";
        } else if (std::strcmp(a, "--bench") == 0) {
            mode = "bench";
        } else if (std::strcmp(a, "--runs") == 0 && i + 1 < argc) {
            bench_runs = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--self-test-gl") == 0) {
            mode = "self-test-gl";
        } else if (std::strcmp(a, "--screenshot") == 0 && i + 1 < argc) {
            mode = "screenshot";
            out_ppm = argv[++i];
        } else if (std::strcmp(a, "--width") == 0 && i + 1 < argc) {
            shot.width = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--height") == 0 && i + 1 < argc) {
            shot.height = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--theme") == 0 && i + 1 < argc) {
            shot.dark_theme = (std::strcmp(argv[++i], "light") != 0);
        } else if (std::strcmp(a, "--matcap") == 0 && i + 1 < argc) {
            shot.matcap = std::atoi(argv[++i]);
        } else if (std::strcmp(a, "--slice") == 0) {
            shot.slice = true;
        } else if (std::strcmp(a, "--wire") == 0) {
            shot.wire = true;
        } else if (std::strcmp(a, "--xray") == 0) {
            shot.xray = true;
        } else if (std::strcmp(a, "--twosided") == 0) {
            shot.two_sided = true;
        } else if (std::strcmp(a, "--no-hud") == 0) {
            shot.show_hud = false;
        } else if (a[0] != '-') {
            file_arg = a;
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", a);
            return 1;
        }
    }

    if (mode == "info") {
        if (file_arg.empty()) {
            std::fprintf(stderr, "Usage: stlviewer --info <file.stl>\n");
            return 1;
        }
        return cmd_info(file_arg);
    }
    if (mode == "bench") {
        if (file_arg.empty()) {
            std::fprintf(stderr, "Usage: stlviewer --bench <file.stl> [--runs N]\n");
            return 1;
        }
        return cmd_bench(file_arg, bench_runs);
    }
    if (mode == "self-test-gl") {
        if (file_arg.empty()) {
            std::fprintf(stderr, "Usage: stlviewer --self-test-gl <file.stl>\n");
            return 1;
        }
        return faset::run_self_test_gl(file_arg);
    }
    if (mode == "screenshot") {
        if (out_ppm.empty() || file_arg.empty()) {
            std::fprintf(stderr, "Usage: stlviewer --screenshot <out.ppm> <file.stl>\n");
            return 1;
        }
        shot.out_ppm = out_ppm;
        shot.stl_path = file_arg;
        return faset::run_screenshot(shot);
    }

    return faset::run_viewer(file_arg);
}
