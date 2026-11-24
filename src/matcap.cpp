#include "matcap.hpp"
#include "math3d.hpp"

#include <algorithm>
#include <cmath>

namespace faset {

namespace {

inline uint8_t linear_to_srgb_u8(float x) {
    x = std::clamp(x, 0.0f, 1.0f);
    float s = (x <= 0.0031308f) ? (x * 12.92f) : (1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(s * 255.0f + 0.5f, 0.0f, 255.0f));
}

inline Vec3 aces_filmic(const Vec3& c) {
    auto f = [](float x) {
        return std::clamp((x * (2.51f * x + 0.03f)) / (x * (2.43f * x + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return {f(c.x), f(c.y), f(c.z)};
}

inline float smoothstep(float e0, float e1, float x) {
    float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

}  // namespace

const char* matcap_name(MatcapKind kind) {
    switch (kind) {
        case MatcapKind::Clay:     return "Clay";
        case MatcapKind::Aluminum: return "Aluminum";
        case MatcapKind::Slate:    return "Slate";
        case MatcapKind::Normals:  return "Normals";
        default:                   return "Clay";
    }
}

std::vector<uint8_t> generate_matcap_rgb8(MatcapKind kind) {
    std::vector<uint8_t> buf(static_cast<size_t>(MATCAP_SIZE * MATCAP_SIZE * 3));

    const Vec3 key_dir = normalize(Vec3{0.46f, 0.62f, 0.64f});
    const Vec3 fill_dir = normalize(Vec3{-0.62f, 0.28f, 0.73f});
    const Vec3 rim_dir = normalize(Vec3{-0.25f, -0.78f, 0.57f});
    const Vec3 view_dir{0.0f, 0.0f, 1.0f};
    const Vec3 half_key = normalize(key_dir + view_dir);
    const Vec3 half_fill = normalize(fill_dir + view_dir);

    for (int y = 0; y < MATCAP_SIZE; ++y) {
        float ny = 1.0f - (static_cast<float>(y) + 0.5f) / (MATCAP_SIZE * 0.5f);
        for (int x = 0; x < MATCAP_SIZE; ++x) {
            float nx = (static_cast<float>(x) + 0.5f) / (MATCAP_SIZE * 0.5f) - 1.0f;
            float r2 = nx * nx + ny * ny;
            float ux = nx, uy = ny;
            if (r2 > 0.992f) {
                float s = std::sqrt(0.992f / r2);
                ux *= s;
                uy *= s;
                r2 = 0.992f;
            }
            float nz = std::sqrt(std::max(0.0f, 1.0f - r2));
            Vec3 n{ux, uy, nz};

            size_t idx = (static_cast<size_t>(y) * MATCAP_SIZE + static_cast<size_t>(x)) * 3;

            if (kind == MatcapKind::Normals) {
                buf[idx + 0] = static_cast<uint8_t>(std::clamp((n.x * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f));
                buf[idx + 1] = static_cast<uint8_t>(std::clamp((n.y * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f));
                buf[idx + 2] = static_cast<uint8_t>(std::clamp((n.z * 0.5f + 0.5f) * 255.0f + 0.5f, 0.0f, 255.0f));
                continue;
            }

            float fresnel = std::pow(1.0f - std::clamp(n.z, 0.0f, 1.0f), 3.0f);
            float n_dot_k = std::max(0.0f, dot(n, key_dir));
            float n_dot_f = std::max(0.0f, dot(n, fill_dir));
            float n_dot_r = std::max(0.0f, dot(n, rim_dir));
            float wrap_k = std::clamp((dot(n, key_dir) + 0.42f) / 1.42f, 0.0f, 1.0f);

            Vec3 lin{};
            if (kind == MatcapKind::Clay) {
                Vec3 albedo{0.62f, 0.58f, 0.53f};
                Vec3 ambient{0.11f, 0.115f, 0.125f};
                Vec3 key_col{1.05f, 0.99f, 0.91f};
                Vec3 fill_col{0.26f, 0.32f, 0.40f};
                float spec = std::pow(std::max(0.0f, dot(n, half_key)), 18.0f) * 0.06f;
                float bounce = std::max(0.0f, -n.y * 0.7f + 0.3f) * 0.07f;
                lin = albedo * (ambient + key_col * (wrap_k * 0.85f) + fill_col * (n_dot_f * 0.65f)) +
                      Vec3{bounce * 0.75f, bounce * 0.68f, bounce * 0.60f} +
                      Vec3{1.0f, 0.97f, 0.92f} * spec +
                      Vec3{0.45f, 0.48f, 0.54f} * (fresnel * 0.16f + n_dot_r * 0.08f);
            } else if (kind == MatcapKind::Aluminum) {
                // Studio anodized reflection environment
                Vec3 r = view_dir * (2.0f * n.z) - n;  // reflection dir (inverted Z for env)
                float ry = -r.y;
                float rx = -r.x;
                float horizon = smoothstep(-0.18f, 0.24f, ry);
                Vec3 env_sky = lerp(Vec3{0.16f, 0.18f, 0.22f}, Vec3{0.58f, 0.65f, 0.74f}, smoothstep(0.0f, 0.85f, ry));
                Vec3 env_gnd = lerp(Vec3{0.07f, 0.08f, 0.10f}, Vec3{0.16f, 0.17f, 0.19f}, smoothstep(-1.0f, -0.05f, ry));
                Vec3 env = lerp(env_gnd, env_sky, horizon);
                // Crisp overhead studio softbox strip
                float strip = smoothstep(0.25f, 0.70f, ry) * smoothstep(0.55f, 0.10f, std::fabs(rx - 0.18f * ry));
                env += Vec3{0.85f, 0.88f, 0.92f} * strip;
                float spec_k = std::pow(std::max(0.0f, dot(n, half_key)), 36.0f) * 0.55f;
                float spec_f = std::pow(std::max(0.0f, dot(n, half_fill)), 20.0f) * 0.22f;
                Vec3 metal_tint{0.76f, 0.80f, 0.85f};
                lin = metal_tint * (env * 0.82f + Vec3{0.18f, 0.19f, 0.21f} * n_dot_k) +
                      Vec3{0.96f, 0.98f, 1.0f} * (spec_k + spec_f + fresnel * 0.28f);
            } else {  // MatcapKind::Slate
                Vec3 albedo{0.16f, 0.21f, 0.28f};
                Vec3 ambient{0.06f, 0.075f, 0.095f};
                float spec1 = std::pow(std::max(0.0f, dot(n, half_key)), 48.0f) * 0.42f;
                float spec2 = std::pow(std::max(0.0f, dot(n, half_fill)), 16.0f) * 0.12f;
                lin = albedo * (ambient + Vec3{0.95f, 0.97f, 1.0f} * (n_dot_k * 0.95f) +
                                Vec3{0.35f, 0.45f, 0.60f} * (n_dot_f * 0.65f)) +
                      Vec3{0.92f, 0.96f, 1.0f} * (spec1 + spec2) +
                      Vec3{0.36f, 0.52f, 0.72f} * (fresnel * 0.30f + n_dot_r * 0.12f);
            }

            Vec3 tone = aces_filmic(lin);
            buf[idx + 0] = linear_to_srgb_u8(tone.x);
            buf[idx + 1] = linear_to_srgb_u8(tone.y);
            buf[idx + 2] = linear_to_srgb_u8(tone.z);
        }
    }
    return buf;
}

}  // namespace faset
