#include "test_framework.hpp"
#include "math3d.hpp"
#include "camera.hpp"

#include <cmath>

using namespace faset;

TEST(vec3_basic_ops) {
    Vec3 a{1.0f, 2.0f, 3.0f};
    Vec3 b{4.0f, -5.0f, 6.0f};
    CHECK_NEAR(dot(a, b), 4.0f - 10.0f + 18.0f, 1e-6f);
    Vec3 c = cross(Vec3{1, 0, 0}, Vec3{0, 1, 0});
    CHECK_NEAR(c.x, 0.0f, 1e-6f);
    CHECK_NEAR(c.y, 0.0f, 1e-6f);
    CHECK_NEAR(c.z, 1.0f, 1e-6f);
    Vec3 n = normalize(Vec3{3.0f, 0.0f, 4.0f});
    CHECK_NEAR(length(n), 1.0f, 1e-6f);
    CHECK_NEAR(n.x, 0.6f, 1e-6f);
    CHECK_NEAR(n.z, 0.8f, 1e-6f);
}

TEST(mat4_inverse_and_unproject_roundtrip) {
    Mat4 v = Mat4::look_at(Vec3{100.0f, -80.0f, 60.0f}, Vec3{10.0f, 20.0f, 5.0f}, Vec3{0.0f, 0.0f, 1.0f});
    Mat4 p = Mat4::perspective(40.0f * 3.1415926535f / 180.0f, 16.0f / 9.0f, 10.0f, 500.0f);
    Mat4 vp = p * v;
    Mat4 inv_vp{};
    CHECK(Mat4::inverse(vp, inv_vp));

    Vec3 orig{14.25f, 19.5f, 7.75f};
    Vec3 win{};
    CHECK(Mat4::project(orig, vp, 1366, 768, win));
    CHECK(win.x > 0.0f && win.x < 1366.0f);
    CHECK(win.y > 0.0f && win.y < 768.0f);
    CHECK(win.z > 0.0f && win.z < 1.0f);

    Vec3 rec{};
    CHECK(Mat4::unproject(win, inv_vp, 1366, 768, rec));
    CHECK_NEAR(rec.x, orig.x, 1e-4f);
    CHECK_NEAR(rec.y, orig.y, 1e-4f);
    CHECK_NEAR(rec.z, orig.z, 1e-4f);
}

TEST(frustum_sphere_culling) {
    Mat4 v = Mat4::look_at(Vec3{0.0f, -10.0f, 0.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 0.0f, 1.0f});
    Mat4 p = Mat4::perspective(60.0f * 3.1415926535f / 180.0f, 1.0f, 1.0f, 100.0f);
    Frustum f = Frustum::from_view_proj(p * v);

    CHECK(f.intersects_sphere(Vec3{0.0f, 0.0f, 0.0f}, 1.0f));
    CHECK(!f.intersects_sphere(Vec3{0.0f, -20.0f, 0.0f}, 1.0f));  // behind camera
    CHECK(!f.intersects_sphere(Vec3{0.0f, 150.0f, 0.0f}, 2.0f));  // beyond far plane
    CHECK(!f.intersects_sphere(Vec3{40.0f, 0.0f, 0.0f}, 1.0f));   // far right
    CHECK(!f.intersects_sphere(Vec3{-40.0f, 0.0f, 0.0f}, 1.0f));  // far left
    CHECK(!f.intersects_sphere(Vec3{0.0f, 0.0f, 40.0f}, 1.0f));   // far top
    CHECK(!f.intersects_sphere(Vec3{0.0f, 0.0f, -40.0f}, 1.0f));  // far bottom
    // Straddling near boundary should be kept
    CHECK(f.intersects_sphere(Vec3{0.0f, -9.0f, 0.0f}, 1.0f));
}

TEST(halton_jitter_bounds) {
    for (uint32_t i = 0; i < 16; ++i) {
        float jx = 0.0f, jy = 0.0f;
        subpixel_jitter_ndc(i, 1000, 500, jx, jy);
        CHECK(jx >= -0.001f && jx <= 0.001f);
        CHECK(jy >= -0.002f && jy <= 0.002f);
    }
    // Sample 0 and sample 1 must differ
    float x0, y0, x1, y1;
    subpixel_jitter_ndc(0, 1000, 1000, x0, y0);
    subpixel_jitter_ndc(1, 1000, 1000, x1, y1);
    CHECK(std::fabs(x0 - x1) > 1e-6f || std::fabs(y0 - y1) > 1e-6f);
}

TEST(camera_frame_orbit_clamp_presets_and_ortho) {
    Camera cam;
    cam.frame_bounds(Vec3{-10.0f, -20.0f, 0.0f}, Vec3{30.0f, 20.0f, 10.0f}, 1280, 720, false);
    CHECK_NEAR(cam.target().x, 10.0f, 1e-4f);
    CHECK_NEAR(cam.target().y, 0.0f, 1e-4f);
    CHECK_NEAR(cam.target().z, 5.0f, 1e-4f);
    CHECK(!cam.is_animating());

    // All 8 corners of bbox must project inside viewport
    Mat4 vp = cam.view_proj(1280, 720);
    Vec3 bmin{-10.0f, -20.0f, 0.0f};
    Vec3 bmax{30.0f, 20.0f, 10.0f};
    for (int i = 0; i < 8; ++i) {
        Vec3 corner{
            (i & 1) ? bmax.x : bmin.x,
            (i & 2) ? bmax.y : bmin.y,
            (i & 4) ? bmax.z : bmin.z
        };
        Vec3 w{};
        CHECK(Mat4::project(corner, vp, 1280, 720, w));
        CHECK(w.x >= 0.0f && w.x <= 1280.0f);
        CHECK(w.y >= 0.0f && w.y <= 720.0f);
    }

    // Extreme vertical orbit must clamp pitch cleanly without gimbal flip
    cam.orbit(0.0f, -5000.0f, 1280, 720);
    CHECK(cam.up().z > 0.0f);
    cam.orbit(0.0f, 10000.0f, 1280, 720);
    CHECK(cam.up().z > 0.0f);

    // Preset Top with animation settles within ~1.0s
    cam.set_preset(ViewPreset::Top, true);
    CHECK(cam.is_animating());
    for (int f = 0; f < 120; ++f) {
        cam.step(1.0f / 60.0f);
    }
    CHECK(!cam.is_animating());
    Vec3 fwd = cam.forward();
    CHECK_NEAR(fwd.z, -1.0f, 0.05f);

    // Perspective <-> Ortho preserves projected size at target
    cam.set_preset(ViewPreset::Iso, false);
    Vec3 p_top = cam.target() + cam.up() * 10.0f;
    Vec3 w_persp{}, w_ortho{};
    CHECK(Mat4::project(p_top, cam.view_proj(1280, 720), 1280, 720, w_persp));
    cam.set_ortho(true);
    CHECK(Mat4::project(p_top, cam.view_proj(1280, 720), 1280, 720, w_ortho));
    CHECK_NEAR(w_persp.y, w_ortho.y, 1.0f);

    // Ortho jitter shifts projected x/y uniformly regardless of depth along view ray
    float jx = 0.0f, jy = 0.0f;
    subpixel_jitter_ndc(1, 1280, 720, jx, jy);
    Vec3 near_pt = cam.target();
    Vec3 far_pt = cam.target() + cam.forward() * 50.0f;
    Vec3 wn0{}, wn1{}, wf0{}, wf1{};
    CHECK(Mat4::project(near_pt, cam.view_proj(1280, 720, 0.0f, 0.0f), 1280, 720, wn0));
    CHECK(Mat4::project(near_pt, cam.view_proj(1280, 720, jx, jy), 1280, 720, wn1));
    CHECK(Mat4::project(far_pt, cam.view_proj(1280, 720, 0.0f, 0.0f), 1280, 720, wf0));
    CHECK(Mat4::project(far_pt, cam.view_proj(1280, 720, jx, jy), 1280, 720, wf1));
    CHECK_NEAR(wn1.x - wn0.x, wf1.x - wf0.x, 1e-4f);
    CHECK_NEAR(wn1.y - wn0.y, wf1.y - wf0.y, 1e-4f);
    CHECK(std::fabs(wn1.x - wn0.x) > 0.05f || std::fabs(wn1.y - wn0.y) > 0.05f);
}
