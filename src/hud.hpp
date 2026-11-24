#pragma once

#include "gl_loader.hpp"
#include "stl_loader.hpp"
#include "viewer_state.hpp"
#include "camera.hpp"

#include <string>
#include <vector>

namespace faset {

enum class HudCommand {
    NoAction,
    OpenFile,
    CycleMatcap,
    ToggleWire,
    ToggleSlice,
    ToggleMeasure,
    ToggleXray,
    ToggleTwoSided,
    ToggleTheme,
    ResetView,
    ToggleHelp,
    SliceAxisX,
    SliceAxisY,
    SliceAxisZ,
    SliceFlip,
    SliceScrub,
    MeasureClear,
};

struct HudRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    HudCommand cmd = HudCommand::NoAction;

    bool contains(float px, float py) const {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

class Hud {
public:
    bool init(GlFuncs& gl, std::string& err);
    void destroy(GlFuncs& gl);

    // Rebuilds layout for current frame and renders directly to bound FBO 0.
    // axis_tips_px: 3 screen positions (X, Y, Z tips in top-left origin pixels) or nullptr.
    void render(GlFuncs& gl,
                int win_w,
                int win_h,
                const MeshData* mesh,
                const Camera& cam,
                const ViewerState& state,
                int ssaa_samples,
                const Vec2* axis_tips_px);

    // Hit testing in top-left pixel coordinates
    bool update_hover(float mx, float my);
    HudCommand on_mouse_down(float mx, float my, float& out_slice_t);
    bool on_mouse_drag(float mx, float my, float& out_slice_t);
    void on_mouse_up();
    bool is_pointer_over_ui(float mx, float my) const;
    bool is_scrubbing_slice() const { return scrubbing_slice_; }

private:
    struct ShapeVert {
        float x, y;
        float lx, ly;
        float hw, hh;
        float radius;
        float fr, fg, fb, fa;
        float br, bg, bb, ba;
    };

    struct GlyphVert {
        float x, y;
        float u, v;
        float r, g, b, a;
    };

    struct ButtonBox {
        HudRect rect;
        std::string label;
        bool active = false;
    };

    GLuint shape_prog_ = 0;
    GLuint shape_vao_ = 0;
    GLuint shape_vbo_ = 0;
    GLint u_shape_vp_ = -1;

    GLuint text_prog_ = 0;
    GLuint text_vao_ = 0;
    GLuint text_vbo_ = 0;
    GLuint font_tex_ = 0;
    GLint u_text_vp_ = -1;
    GLint u_text_atlas_ = -1;

    std::vector<ShapeVert> shape_verts_;
    std::vector<GlyphVert> text_verts_;
    std::vector<HudRect> hit_rects_;
    HudRect slice_track_{};
    bool has_slice_track_ = false;
    bool scrubbing_slice_ = false;
    float hover_x_ = -1.0f;
    float hover_y_ = -1.0f;

    void push_rect(float x, float y, float w, float h, float rad,
                   const Vec4& fill, const Vec4& border);
    void push_text(float x, float y, const std::string& utf8, const Vec4& col);
    static int measure_utf8_px(const std::string& utf8);
};

}  // namespace faset
