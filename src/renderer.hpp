#pragma once

#include "camera.hpp"
#include "culling.hpp"
#include "gl_loader.hpp"
#include "hud.hpp"
#include "matcap.hpp"
#include "stl_loader.hpp"
#include "viewer_state.hpp"

#include <string>
#include <vector>

namespace faset {

class Renderer {
public:
    static constexpr int MAX_SSAA_SAMPLES = 16;

    bool init(GlFuncs& gl, int width, int height, std::string& err);
    void destroy(GlFuncs& gl);

    void resize(GlFuncs& gl, int width, int height);
    bool upload_mesh(GlFuncs& gl, const MeshData& mesh, std::string& err);
    void clear_mesh();

    void reset_accumulation();
    bool is_settled() const { return accum_count_ >= MAX_SSAA_SAMPLES; }
    int accum_count() const { return accum_count_; }

    // Renders one progressive SSAA sample (or final HUD/triad refresh if already settled).
    // Returns CullStats for the drawn sample.
    CullStats render_frame(GlFuncs& gl,
                           const MeshData* mesh,
                           const Camera& cam,
                           const ViewerState& state,
                           Hud& hud);

    // 0.01 ms depth buffer picking at window coordinates (top-left origin).
    bool pick_world_point(GlFuncs& gl, int x, int y, const Camera& cam, Vec3& out_world) const;

    // Writes current default framebuffer (width x height) to a binary PPM file.
    bool save_ppm(GlFuncs& gl, const char* path) const;

    // Diagnostics for --self-test-gl
    uint64_t read_center_pixel_rgb(GlFuncs& gl, int x, int y) const;
    bool using_fp16_fbo() const { return fp16_fbo_; }

private:
    int width_ = 1280;
    int height_ = 720;
    int accum_count_ = 0;
    float last_jx_ = 0.0f;
    float last_jy_ = 0.0f;
    bool has_mesh_ = false;
    bool fp16_fbo_ = false;

    // Geometry VAO/VBO
    GLuint mesh_vao_ = 0;
    GLuint mesh_vbo_ = 0;
    GLuint empty_vao_ = 0;
    GLuint line_vao_ = 0;
    GLuint line_vbo_ = 0;

    // Framebuffers
    GLuint scene_fbo_ = 0;
    GLuint scene_color_ = 0;
    GLuint scene_depth_ = 0;
    GLuint accum_fbo_ = 0;
    GLuint accum_color_ = 0;

    // MatCaps
    GLuint matcap_tex_[static_cast<int>(MatcapKind::Count)] = {};

    // Programs + Uniforms
    GLuint bg_prog_ = 0;
    GLint u_bg_dark_ = -1;

    GLuint mesh_prog_ = 0;
    GLint u_mesh_view_ = -1;
    GLint u_mesh_proj_ = -1;
    GLint u_mesh_bmin_ = -1;
    GLint u_mesh_qscale_ = -1;
    GLint u_mesh_clip_plane_ = -1;
    GLint u_mesh_clip_on_ = -1;
    GLint u_mesh_matcap_ = -1;
    GLint u_mesh_xray_ = -1;
    GLint u_mesh_wire_ = -1;
    GLint u_mesh_twosided_ = -1;
    GLint u_mesh_dark_ = -1;

    GLuint grid_prog_ = 0;
    GLint u_grid_vp_ = -1;
    GLint u_grid_cam_ = -1;
    GLint u_grid_center_ = -1;
    GLint u_grid_step_ = -1;
    GLint u_grid_radius_ = -1;
    GLint u_grid_yup_ = -1;
    GLint u_grid_dark_ = -1;

    GLuint line_prog_ = 0;
    GLint u_line_vp_ = -1;

    GLuint accum_prog_ = 0;
    GLint u_accum_tex_ = -1;
    GLint u_accum_weight_ = -1;

    GLuint resolve_prog_ = 0;
    GLint u_resolve_tex_ = -1;

    std::vector<DrawSpan> spans_;

    bool create_fbos(GlFuncs& gl, int w, int h, std::string& err);
    void destroy_fbos(GlFuncs& gl);
};

}  // namespace faset
