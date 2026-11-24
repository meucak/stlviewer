#include "camera.hpp"

#include <algorithm>
#include <cmath>

namespace faset {

namespace {
constexpr float MAX_PITCH = 1.565f;  // ~89.67 deg, prevents gimbal flip with look_at
constexpr float MIN_FOV_SIN = 0.1f;
}

Camera::Camera() {
    set_preset(ViewPreset::Iso, false);
}

Vec3 Camera::world_up_vec() const {
    return up_axis_ == UpAxis::ZUp ? Vec3{0.0f, 0.0f, 1.0f} : Vec3{0.0f, 1.0f, 0.0f};
}

Vec3 Camera::dir_from_angles(float yaw, float pitch) const {
    float cp = std::cos(pitch);
    float sp = std::sin(pitch);
    if (up_axis_ == UpAxis::ZUp) {
        return {cp * std::cos(yaw), cp * std::sin(yaw), sp};
    }
    return {cp * std::sin(yaw), sp, cp * std::cos(yaw)};
}

float Camera::wrap_angle_diff(float from, float to) {
    float d = std::fmod(to - from, 2.0f * PI);
    if (d > PI) d -= 2.0f * PI;
    if (d < -PI) d += 2.0f * PI;
    return d;
}

void Camera::clamp_state(State& s) const {
    s.pitch = std::clamp(s.pitch, -MAX_PITCH, MAX_PITCH);
    float min_dist = std::max(scene_radius_ * 0.01f, 1e-3f);
    float max_dist = std::max(scene_radius_ * 200.0f, 1000.0f);
    s.distance = std::clamp(s.distance, min_dist, max_dist);
}

void Camera::frame_bounds(const Vec3& bmin, const Vec3& bmax, int vp_w, int vp_h, bool animate) {
    Vec3 center = (bmin + bmax) * 0.5f;
    Vec3 extent = bmax - bmin;
    float radius = std::max(length(extent) * 0.5f, 1e-2f);
    scene_radius_ = radius;

    float aspect = (vp_w > 0 && vp_h > 0) ? (static_cast<float>(vp_w) / static_cast<float>(vp_h)) : 1.777f;
    float half_fov_y = fov_deg_ * 0.5f * DEG2RAD;
    float half_fov_x = std::atan(std::tan(half_fov_y) * aspect);
    float min_half_fov = std::min(half_fov_y, half_fov_x);
    float sin_f = std::max(std::sin(min_half_fov), MIN_FOV_SIN);

    dst_.target = center;
    dst_.distance = (radius / sin_f) * 1.12f;
    clamp_state(dst_);

    if (!animate) {
        cur_ = dst_;
        anim_active_ = false;
    } else {
        anim_active_ = true;
    }
}

void Camera::set_preset(ViewPreset preset, bool animate) {
    float yaw = -PI * 0.25f;
    float pitch = 0.6154797f;

    if (up_axis_ == UpAxis::ZUp) {
        switch (preset) {
            case ViewPreset::Iso:    yaw = -PI * 0.25f; pitch = 0.6154797f; break;
            case ViewPreset::Front:  yaw = -PI * 0.50f; pitch = 0.0f;       break;
            case ViewPreset::Back:   yaw =  PI * 0.50f; pitch = 0.0f;       break;
            case ViewPreset::Right:  yaw =  0.0f;       pitch = 0.0f;       break;
            case ViewPreset::Left:   yaw =  PI;         pitch = 0.0f;       break;
            case ViewPreset::Top:    yaw = -PI * 0.50f; pitch =  MAX_PITCH; break;
            case ViewPreset::Bottom: yaw = -PI * 0.50f; pitch = -MAX_PITCH; break;
        }
    } else {
        switch (preset) {
            case ViewPreset::Iso:    yaw =  PI * 0.25f; pitch = 0.6154797f; break;
            case ViewPreset::Front:  yaw =  0.0f;       pitch = 0.0f;       break;
            case ViewPreset::Back:   yaw =  PI;         pitch = 0.0f;       break;
            case ViewPreset::Right:  yaw =  PI * 0.50f; pitch = 0.0f;       break;
            case ViewPreset::Left:   yaw = -PI * 0.50f; pitch = 0.0f;       break;
            case ViewPreset::Top:    yaw =  0.0f;       pitch =  MAX_PITCH; break;
            case ViewPreset::Bottom: yaw =  0.0f;       pitch = -MAX_PITCH; break;
        }
    }

    dst_.yaw = cur_.yaw + wrap_angle_diff(cur_.yaw, yaw);
    dst_.pitch = pitch;
    clamp_state(dst_);

    if (!animate) {
        cur_.yaw = yaw;
        cur_.pitch = dst_.pitch;
        dst_.yaw = yaw;
        anim_active_ = false;
    } else {
        anim_active_ = true;
    }
}

void Camera::orbit(float dx_pixels, float dy_pixels, int vp_w, int vp_h) {
    float scale = 2.0f * PI / static_cast<float>(std::max(std::min(vp_w, vp_h), 200));
    cur_.yaw -= dx_pixels * scale;
    cur_.pitch += dy_pixels * scale;
    clamp_state(cur_);
    dst_ = cur_;
    anim_active_ = false;
}

void Camera::pan(float dx_pixels, float dy_pixels, int vp_w, int vp_h) {
    if (vp_h <= 0) return;
    float half_h = cur_.distance * std::tan(fov_deg_ * 0.5f * DEG2RAD);
    float world_per_px = (2.0f * half_h) / static_cast<float>(vp_h);
    (void)vp_w;
    Vec3 shift = (-right() * dx_pixels + up() * dy_pixels) * world_per_px;
    cur_.target += shift;
    dst_ = cur_;
    anim_active_ = false;
}

void Camera::zoom(float wheel_steps, float mouse_x_px, float mouse_y_px, int vp_w, int vp_h) {
    if (std::fabs(wheel_steps) < 1e-5f) return;

    auto ray_plane_hit = [&](float mx, float my, Vec3& hit) -> bool {
        if (vp_w <= 0 || vp_h <= 0) return false;
        Mat4 inv_vp{};
        if (!Mat4::inverse(view_proj(vp_w, vp_h), inv_vp)) return false;
        float gl_y = static_cast<float>(vp_h) - 1.0f - my;
        Vec3 p0{}, p1{};
        if (!Mat4::unproject({mx, gl_y, 0.0f}, inv_vp, vp_w, vp_h, p0)) return false;
        if (!Mat4::unproject({mx, gl_y, 1.0f}, inv_vp, vp_w, vp_h, p1)) return false;
        Vec3 ray_dir = normalize(p1 - p0);
        Vec3 n = forward();
        float denom = dot(ray_dir, n);
        if (std::fabs(denom) < 1e-6f) return false;
        float t = dot(cur_.target - p0, n) / denom;
        hit = p0 + ray_dir * t;
        return true;
    };

    Vec3 before{};
    bool has_cursor = (mouse_x_px >= 0.0f && mouse_y_px >= 0.0f && vp_w > 0 && vp_h > 0);
    if (has_cursor) has_cursor = ray_plane_hit(mouse_x_px, mouse_y_px, before);

    float factor = std::pow(0.86f, wheel_steps);
    cur_.distance *= factor;
    clamp_state(cur_);

    if (has_cursor) {
        Vec3 after{};
        if (ray_plane_hit(mouse_x_px, mouse_y_px, after)) {
            cur_.target += (before - after);
        }
    }

    dst_ = cur_;
    anim_active_ = false;
}

void Camera::set_ortho(bool enable) {
    ortho_ = enable;
}

void Camera::set_up_axis(UpAxis axis) {
    if (up_axis_ == axis) return;
    up_axis_ = axis;
    set_preset(ViewPreset::Iso, false);
}

void Camera::set_pivot(const Vec3& world_pivot, bool animate) {
    dst_.target = world_pivot;
    if (!animate) {
        cur_.target = world_pivot;
        anim_active_ = false;
    } else {
        anim_active_ = true;
    }
}

void Camera::set_scene_radius(float r) {
    scene_radius_ = std::max(r, 1e-3f);
}

bool Camera::step(float dt) {
    if (!anim_active_) return false;
    float alpha = 1.0f - std::exp(-14.0f * std::clamp(dt, 0.0f, 0.1f));
    cur_.target = lerp(cur_.target, dst_.target, alpha);
    cur_.yaw += (dst_.yaw - cur_.yaw) * alpha;
    cur_.pitch += (dst_.pitch - cur_.pitch) * alpha;
    cur_.distance += (dst_.distance - cur_.distance) * alpha;

    float err_t = length(dst_.target - cur_.target);
    float err_a = std::fabs(dst_.yaw - cur_.yaw) + std::fabs(dst_.pitch - cur_.pitch);
    float err_d = std::fabs(dst_.distance - cur_.distance) / std::max(dst_.distance, 1e-4f);
    if (err_t < scene_radius_ * 1e-4f && err_a < 5e-4f && err_d < 5e-4f) {
        snap();
        return false;
    }
    return true;
}

void Camera::snap() {
    cur_ = dst_;
    anim_active_ = false;
}

Vec3 Camera::eye() const {
    return cur_.target + dir_from_angles(cur_.yaw, cur_.pitch) * cur_.distance;
}

Vec3 Camera::forward() const {
    return -dir_from_angles(cur_.yaw, cur_.pitch);
}

Vec3 Camera::right() const {
    return normalize(cross(forward(), world_up_vec()));
}

Vec3 Camera::up() const {
    return cross(right(), forward());
}

void Camera::compute_near_far(float& z_near, float& z_far) const {
    float r = std::max(scene_radius_, 1e-3f);
    float dist = cur_.distance;
    if (ortho_) {
        float span = std::max(dist + r * 4.0f, r * 10.0f);
        z_near = -span;
        z_far = span;
    } else {
        z_far = std::max(dist + r * 4.0f, r * 10.0f);
        float min_near = std::max(z_far / 8000.0f, std::min(dist * 0.01f, r * 0.002f));
        float tight_near = dist - r * 1.25f;
        z_near = std::max(min_near, tight_near);
    }
}

Mat4 Camera::view_matrix() const {
    return Mat4::look_at(eye(), cur_.target, world_up_vec());
}

Mat4 Camera::proj_matrix(int vp_w, int vp_h, float jitter_ndc_x, float jitter_ndc_y) const {
    float aspect = (vp_w > 0 && vp_h > 0) ? (static_cast<float>(vp_w) / static_cast<float>(vp_h)) : 1.777f;
    float zn = 0.1f, zf = 1000.0f;
    compute_near_far(zn, zf);

    Mat4 p{};
    if (ortho_) {
        float half_h = cur_.distance * std::tan(fov_deg_ * 0.5f * DEG2RAD);
        float half_w = half_h * aspect;
        p = Mat4::ortho(-half_w, half_w, -half_h, half_h, zn, zf);
        p.m[12] += jitter_ndc_x;
        p.m[13] += jitter_ndc_y;
    } else {
        p = Mat4::perspective(fov_deg_ * DEG2RAD, aspect, zn, zf);
        p.m[8] -= jitter_ndc_x;
        p.m[9] -= jitter_ndc_y;
    }
    return p;
}

Mat4 Camera::view_proj(int vp_w, int vp_h, float jitter_ndc_x, float jitter_ndc_y) const {
    return proj_matrix(vp_w, vp_h, jitter_ndc_x, jitter_ndc_y) * view_matrix();
}

}  // namespace faset
