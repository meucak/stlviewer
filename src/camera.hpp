#pragma once

#include "math3d.hpp"

namespace faset {

enum class ViewPreset { Iso, Front, Back, Right, Left, Top, Bottom };
enum class UpAxis { ZUp, YUp };

class Camera {
public:
    Camera();

    void frame_bounds(const Vec3& bmin, const Vec3& bmax, int vp_w, int vp_h, bool animate = false);
    void set_preset(ViewPreset preset, bool animate = true);
    void orbit(float dx_pixels, float dy_pixels, int vp_w, int vp_h);
    void pan(float dx_pixels, float dy_pixels, int vp_w, int vp_h);
    void zoom(float wheel_steps, float mouse_x_px, float mouse_y_px, int vp_w, int vp_h);
    void set_ortho(bool enable);
    void toggle_ortho() { set_ortho(!ortho_); }
    void set_up_axis(UpAxis axis);
    void toggle_up_axis() { set_up_axis(up_axis_ == UpAxis::ZUp ? UpAxis::YUp : UpAxis::ZUp); }
    void set_pivot(const Vec3& world_pivot, bool animate = true);
    void set_scene_radius(float r);

    bool step(float dt);
    void snap();

    bool is_animating() const { return anim_active_; }
    bool is_ortho() const { return ortho_; }
    UpAxis up_axis() const { return up_axis_; }
    const Vec3& target() const { return cur_.target; }
    float distance() const { return cur_.distance; }
    float fov_deg() const { return fov_deg_; }

    Vec3 eye() const;
    Vec3 forward() const;
    Vec3 right() const;
    Vec3 up() const;

    void compute_near_far(float& z_near, float& z_far) const;
    Mat4 view_matrix() const;
    Mat4 proj_matrix(int vp_w, int vp_h, float jitter_ndc_x = 0.0f, float jitter_ndc_y = 0.0f) const;
    Mat4 view_proj(int vp_w, int vp_h, float jitter_ndc_x = 0.0f, float jitter_ndc_y = 0.0f) const;

private:
    struct State {
        Vec3 target{0.0f, 0.0f, 0.0f};
        float yaw = -PI * 0.25f;
        float pitch = 0.6154797f;  // atan(1/sqrt(2)) ~ 35.264 deg
        float distance = 100.0f;
    };

    State cur_{};
    State dst_{};
    bool anim_active_ = false;
    bool ortho_ = false;
    UpAxis up_axis_ = UpAxis::ZUp;
    float fov_deg_ = 40.0f;
    float scene_radius_ = 50.0f;

    Vec3 world_up_vec() const;
    Vec3 dir_from_angles(float yaw, float pitch) const;
    static float wrap_angle_diff(float from, float to);
    void clamp_state(State& s) const;
};

}  // namespace faset
