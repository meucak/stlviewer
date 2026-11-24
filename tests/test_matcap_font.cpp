#include "test_framework.hpp"
#include "matcap.hpp"
#include "font_data.hpp"

#include <chrono>
#include <cstring>
#include <string>

using namespace faset;

TEST(matcap_presets_and_perf) {
    auto t0 = std::chrono::steady_clock::now();
    auto clay = generate_matcap_rgb8(MatcapKind::Clay);
    auto alum = generate_matcap_rgb8(MatcapKind::Aluminum);
    auto slate = generate_matcap_rgb8(MatcapKind::Slate);
    auto norm = generate_matcap_rgb8(MatcapKind::Normals);
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    CHECK_EQ(clay.size(), static_cast<size_t>(MATCAP_SIZE * MATCAP_SIZE * 3));
    CHECK_EQ(alum.size(), clay.size());
    CHECK_EQ(slate.size(), clay.size());
    CHECK_EQ(norm.size(), clay.size());
    CHECK(ms < 100.0);

    // Center pixel (nx=0, ny=0, nz=1) should be valid and distinct across presets
    size_t c = (static_cast<size_t>(MATCAP_SIZE / 2) * MATCAP_SIZE + (MATCAP_SIZE / 2)) * 3;
    CHECK(clay[c] > 80 && clay[c + 1] > 80 && clay[c + 2] > 80);
    CHECK(std::memcmp(&clay[c], &slate[c], 3) != 0);
    CHECK(std::memcmp(&clay[c], &norm[c], 3) != 0);
    // Normal map at center (nz=1 -> R~128, G~128, B~255)
    CHECK(norm[c + 2] > 235);
    CHECK(std::string(matcap_name(MatcapKind::Clay)) == "Clay");
}

TEST(font_utf8_and_atlas_lookup) {
    const char* txt = "çğİöşü×·Δ";
    const char* p = txt;
    const char* end = txt + std::strlen(txt);
    uint32_t expected[] = {0x00E7, 0x011F, 0x0130, 0x00F6, 0x015F, 0x00FC, 0x00D7, 0x00B7, 0x0394};
    for (uint32_t cp : expected) {
        uint32_t got = decode_utf8_next(p, end);
        CHECK_EQ(got, cp);
        int idx = font_glyph_index(got);
        CHECK(idx >= 0 && idx < FONT_GLYPH_COUNT);
        const uint8_t* rows = font_glyph_rows(idx);
        int lit = 0;
        for (int r = 0; r < FONT_GLYPH_H; ++r) {
            if (rows[r] != 0) ++lit;
        }
        CHECK(lit > 0);
    }
    CHECK_EQ(p, end);

    int aw = 0, ah = 0;
    auto atlas = build_font_atlas_r8(aw, ah);
    CHECK_EQ(aw, 128);
    CHECK_EQ(ah, 128);
    CHECK_EQ(atlas.size(), static_cast<size_t>(aw * ah));
}
