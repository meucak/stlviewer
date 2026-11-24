#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace faset {

constexpr float PI = 3.14159265358979323846f;
constexpr float DEG2RAD = PI / 180.0f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator*(const Vec3& o) const { return {x * o.x, y * o.y, z * o.z}; }
    constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    constexpr Vec3& operator+=(const Vec3& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
    constexpr Vec3& operator-=(const Vec3& o) {
        x -= o.x;
        y -= o.y;
        z -= o.z;
        return *this;
    }
    constexpr Vec3& operator*=(float s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) { return {v.x * s, v.y * s, v.z * s}; }

inline constexpr float dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline constexpr Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

inline constexpr float length_sq(const Vec3& v) { return dot(v, v); }

inline float length(const Vec3& v) { return std::sqrt(length_sq(v)); }

inline Vec3 normalize(const Vec3& v) {
    float len = length(v);
    if (len > 1e-12f) {
        return v * (1.0f / len);
    }
    return {0.0f, 0.0f, 0.0f};
}

inline Vec3 min_v(const Vec3& a, const Vec3& b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}

inline Vec3 max_v(const Vec3& a, const Vec3& b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}

inline constexpr Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
    return a + (b - a) * t;
}

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

struct Mat4 {
    // Column-major: m[col * 4 + row]
    float m[16] = {};

    static constexpr Mat4 identity() {
        Mat4 r{};
        r.m[0] = 1.0f;
        r.m[5] = 1.0f;
        r.m[10] = 1.0f;
        r.m[15] = 1.0f;
        return r;
    }

    static Mat4 perspective(float fov_y_rad, float aspect, float z_near, float z_far) {
        Mat4 r{};
        float f = 1.0f / std::tan(fov_y_rad * 0.5f);
        r.m[0] = f / std::max(aspect, 1e-6f);
        r.m[5] = f;
        r.m[10] = -(z_far + z_near) / (z_far - z_near);
        r.m[11] = -1.0f;
        r.m[14] = -(2.0f * z_far * z_near) / (z_far - z_near);
        return r;
    }

    static Mat4 ortho(float left, float right, float bottom, float top, float z_near, float z_far) {
        Mat4 r = identity();
        r.m[0] = 2.0f / (right - left);
        r.m[5] = 2.0f / (top - bottom);
        r.m[10] = -2.0f / (z_far - z_near);
        r.m[12] = -(right + left) / (right - left);
        r.m[13] = -(top + bottom) / (top - bottom);
        r.m[14] = -(z_far + z_near) / (z_far - z_near);
        return r;
    }

    static Mat4 look_at(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = normalize(target - eye);
        Vec3 s = normalize(cross(f, up));
        Vec3 u = cross(s, f);
        Mat4 r = identity();
        r.m[0] = s.x;
        r.m[4] = s.y;
        r.m[8] = s.z;
        r.m[12] = -dot(s, eye);

        r.m[1] = u.x;
        r.m[5] = u.y;
        r.m[9] = u.z;
        r.m[13] = -dot(u, eye);

        r.m[2] = -f.x;
        r.m[6] = -f.y;
        r.m[10] = -f.z;
        r.m[14] = dot(f, eye);
        return r;
    }

    friend Mat4 operator*(const Mat4& a, const Mat4& b) {
        Mat4 r{};
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                r.m[col * 4 + row] =
                    a.m[0 * 4 + row] * b.m[col * 4 + 0] +
                    a.m[1 * 4 + row] * b.m[col * 4 + 1] +
                    a.m[2 * 4 + row] * b.m[col * 4 + 2] +
                    a.m[3 * 4 + row] * b.m[col * 4 + 3];
            }
        }
        return r;
    }

    static Vec4 mul(const Mat4& a, const Vec4& v) {
        return {
            a.m[0] * v.x + a.m[4] * v.y + a.m[8] * v.z + a.m[12] * v.w,
            a.m[1] * v.x + a.m[5] * v.y + a.m[9] * v.z + a.m[13] * v.w,
            a.m[2] * v.x + a.m[6] * v.y + a.m[10] * v.z + a.m[14] * v.w,
            a.m[3] * v.x + a.m[7] * v.y + a.m[11] * v.z + a.m[15] * v.w,
        };
    }

    static bool inverse(const Mat4& in, Mat4& out) {
        double m[16];
        for (int i = 0; i < 16; ++i) m[i] = static_cast<double>(in.m[i]);
        double inv[16];

        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
                 m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
                 m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
                 m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
                  m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];

        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
                 m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
                 m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
                 m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
                  m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];

        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
                 m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
                 m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
                  m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
                  m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];

        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
                 m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
                 m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
                  m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
                  m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
        if (std::fabs(det) < 1e-18) return false;
        double inv_det = 1.0 / det;
        for (int i = 0; i < 16; ++i) out.m[i] = static_cast<float>(inv[i] * inv_det);
        return true;
    }

    static bool project(const Vec3& world, const Mat4& vp, int vp_w, int vp_h, Vec3& out_win) {
        Vec4 clip = mul(vp, Vec4{world.x, world.y, world.z, 1.0f});
        if (std::fabs(clip.w) < 1e-8f) return false;
        float inv_w = 1.0f / clip.w;
        float ndc_x = clip.x * inv_w;
        float ndc_y = clip.y * inv_w;
        float ndc_z = clip.z * inv_w;
        out_win.x = (ndc_x * 0.5f + 0.5f) * static_cast<float>(vp_w);
        out_win.y = (ndc_y * 0.5f + 0.5f) * static_cast<float>(vp_h);
        out_win.z = ndc_z * 0.5f + 0.5f;
        return clip.w > 0.0f;
    }

    static bool unproject(const Vec3& win, const Mat4& inv_vp, int vp_w, int vp_h, Vec3& out_world) {
        if (vp_w <= 0 || vp_h <= 0) return false;
        float ndc_x = (win.x / static_cast<float>(vp_w)) * 2.0f - 1.0f;
        float ndc_y = (win.y / static_cast<float>(vp_h)) * 2.0f - 1.0f;
        float ndc_z = win.z * 2.0f - 1.0f;
        Vec4 w = mul(inv_vp, Vec4{ndc_x, ndc_y, ndc_z, 1.0f});
        if (std::fabs(w.w) < 1e-10f) return false;
        float inv_w = 1.0f / w.w;
        out_world = {w.x * inv_w, w.y * inv_w, w.z * inv_w};
        return true;
    }
};

struct Plane {
    Vec3 n{};
    float d = 0.0f;
};

struct Frustum {
    Plane planes[6]{};

    static Frustum from_view_proj(const Mat4& vp) {
        auto row = [&](int r) {
            return Vec4{vp.m[0 + r], vp.m[4 + r], vp.m[8 + r], vp.m[12 + r]};
        };
        Vec4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
        auto make_plane = [](const Vec4& v) {
            float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
            float inv = (len > 1e-12f) ? (1.0f / len) : 1.0f;
            return Plane{{v.x * inv, v.y * inv, v.z * inv}, v.w * inv};
        };
        auto add = [](const Vec4& a, const Vec4& b) {
            return Vec4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
        };
        auto sub = [](const Vec4& a, const Vec4& b) {
            return Vec4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
        };

        Frustum f{};
        f.planes[0] = make_plane(add(r3, r0));  // Left
        f.planes[1] = make_plane(sub(r3, r0));  // Right
        f.planes[2] = make_plane(add(r3, r1));  // Bottom
        f.planes[3] = make_plane(sub(r3, r1));  // Top
        f.planes[4] = make_plane(add(r3, r2));  // Near
        f.planes[5] = make_plane(sub(r3, r2));  // Far
        return f;
    }

    bool intersects_sphere(const Vec3& center, float radius) const {
        for (int i = 0; i < 6; ++i) {
            float dist = dot(planes[i].n, center) + planes[i].d;
            if (dist < -radius) return false;
        }
        return true;
    }
};

inline float halton(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float r = 0.0f;
    uint32_t i = index;
    while (i > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(i % base);
        i /= base;
    }
    return r;
}

inline void subpixel_jitter_ndc(uint32_t sample_idx, int vp_w, int vp_h, float& jx, float& jy) {
    if (sample_idx == 0 || vp_w <= 0 || vp_h <= 0) {
        jx = 0.0f;
        jy = 0.0f;
        return;
    }
    float ox = halton(sample_idx, 2) - 0.5f;
    float oy = halton(sample_idx, 3) - 0.5f;
    jx = ox * (2.0f / static_cast<float>(vp_w));
    jy = oy * (2.0f / static_cast<float>(vp_h));
}

}  // namespace faset
