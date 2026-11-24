#include "hud.hpp"
#include "font_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace faset {

namespace {

const char* SHAPE_VS = R"GLSL(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aLocal;
layout(location = 2) in vec2 aHalfSize;
layout(location = 3) in float aRadius;
layout(location = 4) in vec4 aFill;
layout(location = 5) in vec4 aBorder;

uniform vec2 uViewport;

out vec2 vLocal;
out vec2 vHalfSize;
out float vRadius;
out vec4 vFill;
out vec4 vBorder;

void main() {
    vec2 ndc = (aPos / uViewport) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vLocal = aLocal;
    vHalfSize = aHalfSize;
    vRadius = aRadius;
    vFill = aFill;
    vBorder = aBorder;
}
)GLSL";

const char* SHAPE_FS = R"GLSL(#version 330 core
in vec2 vLocal;
in vec2 vHalfSize;
in float vRadius;
in vec4 vFill;
in vec4 vBorder;

out vec4 FragColor;

void main() {
    float r = min(vRadius, min(vHalfSize.x, vHalfSize.y));
    vec2 q = abs(vLocal) - (vHalfSize - vec2(r));
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float outerAlpha = 1.0 - smoothstep(-0.75, 0.75, d);
    if (outerAlpha <= 0.002) discard;
    float borderMix = smoothstep(-1.5, -0.35, d);
    vec4 col = mix(vFill, vBorder, borderMix * vBorder.a);
    FragColor = vec4(col.rgb, col.a * outerAlpha);
}
)GLSL";

const char* TEXT_VS = R"GLSL(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aCol;

uniform vec2 uViewport;

out vec2 vUV;
out vec4 vCol;

void main() {
    vec2 ndc = (aPos / uViewport) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vCol = aCol;
}
)GLSL";

const char* TEXT_FS = R"GLSL(#version 330 core
in vec2 vUV;
in vec4 vCol;
uniform sampler2D uAtlas;
out vec4 FragColor;

void main() {
    float a = texture(uAtlas, vUV).r;
    if (a < 0.1) discard;
    FragColor = vec4(vCol.rgb, vCol.a * a);
}
)GLSL";

GLuint compile_program(GlFuncs& gl, const char* vs_src, const char* fs_src, std::string& err) {
    auto compile_stage = [&](GLenum type, const char* src) -> GLuint {
        GLuint s = gl.CreateShader(type);
        gl.ShaderSource(s, 1, &src, nullptr);
        gl.CompileShader(s);
        GLint ok = 0;
        gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024]{};
            gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
            err = std::string("HUD shader compile error: ") + log;
            gl.DeleteShader(s);
            return 0;
        }
        return s;
    };

    GLuint vs = compile_stage(GL_VERTEX_SHADER, vs_src);
    if (!vs) return 0;
    GLuint fs = compile_stage(GL_FRAGMENT_SHADER, fs_src);
    if (!fs) {
        gl.DeleteShader(vs);
        return 0;
    }

    GLuint prog = gl.CreateProgram();
    gl.AttachShader(prog, vs);
    gl.AttachShader(prog, fs);
    gl.LinkProgram(prog);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);

    GLint ok = 0;
    gl.GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        gl.GetProgramInfoLog(prog, sizeof(log), nullptr, log);
        err = std::string("HUD shader link error: ") + log;
        gl.DeleteProgram(prog);
        return 0;
    }
    return prog;
}

std::string format_count(uint32_t n) {
    char buf[32];
    if (n >= 1000000u) {
        std::snprintf(buf, sizeof(buf), "%.2fM", static_cast<double>(n) / 1e6);
    } else if (n >= 1000u) {
        std::snprintf(buf, sizeof(buf), "%.1fK", static_cast<double>(n) / 1e3);
    } else {
        std::snprintf(buf, sizeof(buf), "%u", n);
    }
    return buf;
}

std::string base_filename(const std::string& path) {
    size_t slash = path.find_last_of('/');
    return (slash == std::string::npos) ? path : path.substr(slash + 1);
}

}  // namespace

bool Hud::init(GlFuncs& gl, std::string& err) {
    shape_prog_ = compile_program(gl, SHAPE_VS, SHAPE_FS, err);
    if (!shape_prog_) return false;
    text_prog_ = compile_program(gl, TEXT_VS, TEXT_FS, err);
    if (!text_prog_) return false;

    u_shape_vp_ = gl.GetUniformLocation(shape_prog_, "uViewport");
    u_text_vp_ = gl.GetUniformLocation(text_prog_, "uViewport");
    u_text_atlas_ = gl.GetUniformLocation(text_prog_, "uAtlas");

    gl.GenVertexArrays(1, &shape_vao_);
    gl.GenBuffers(1, &shape_vbo_);
    gl.BindVertexArray(shape_vao_);
    gl.BindBuffer(GL_ARRAY_BUFFER, shape_vbo_);
    const GLsizei s_stride = sizeof(ShapeVert);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, s_stride, reinterpret_cast<void*>(offsetof(ShapeVert, x)));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, s_stride, reinterpret_cast<void*>(offsetof(ShapeVert, lx)));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, s_stride, reinterpret_cast<void*>(offsetof(ShapeVert, hw)));
    gl.EnableVertexAttribArray(3);
    gl.VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, s_stride, reinterpret_cast<void*>(offsetof(ShapeVert, radius)));
    gl.EnableVertexAttribArray(4);
    gl.VertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, s_stride, reinterpret_cast<void*>(offsetof(ShapeVert, fr)));
    gl.EnableVertexAttribArray(5);
    gl.VertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, s_stride, reinterpret_cast<void*>(offsetof(ShapeVert, br)));

    gl.GenVertexArrays(1, &text_vao_);
    gl.GenBuffers(1, &text_vbo_);
    gl.BindVertexArray(text_vao_);
    gl.BindBuffer(GL_ARRAY_BUFFER, text_vbo_);
    const GLsizei t_stride = sizeof(GlyphVert);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, t_stride, reinterpret_cast<void*>(offsetof(GlyphVert, x)));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, t_stride, reinterpret_cast<void*>(offsetof(GlyphVert, u)));
    gl.EnableVertexAttribArray(2);
    gl.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, t_stride, reinterpret_cast<void*>(offsetof(GlyphVert, r)));
    gl.BindVertexArray(0);

    int aw = 0, ah = 0;
    auto atlas = build_font_atlas_r8(aw, ah);
    gl.GenTextures(1, &font_tex_);
    gl.BindTexture(GL_TEXTURE_2D, font_tex_);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl.TexImage2D(GL_TEXTURE_2D, 0, GL_R8, aw, ah, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl.BindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void Hud::destroy(GlFuncs& gl) {
    if (font_tex_) gl.DeleteTextures(1, &font_tex_);
    if (shape_vbo_) gl.DeleteBuffers(1, &shape_vbo_);
    if (shape_vao_) gl.DeleteVertexArrays(1, &shape_vao_);
    if (text_vbo_) gl.DeleteBuffers(1, &text_vbo_);
    if (text_vao_) gl.DeleteVertexArrays(1, &text_vao_);
    if (shape_prog_) gl.DeleteProgram(shape_prog_);
    if (text_prog_) gl.DeleteProgram(text_prog_);
    font_tex_ = shape_vbo_ = shape_vao_ = text_vbo_ = text_vao_ = shape_prog_ = text_prog_ = 0;
}

int Hud::measure_utf8_px(const std::string& utf8) {
    int count = 0;
    const char* p = utf8.data();
    const char* end = p + utf8.size();
    while (p < end) {
        decode_utf8_next(p, end);
        ++count;
    }
    return count * FONT_GLYPH_W;
}

void Hud::push_rect(float x, float y, float w, float h, float rad,
                    const Vec4& fill, const Vec4& border) {
    float hw = w * 0.5f;
    float hh = h * 0.5f;
    float pad = 1.0f;
    float x0 = x - pad, y0 = y - pad;
    float x1 = x + w + pad, y1 = y + h + pad;
    float l0x = -hw - pad, l1x = hw + pad;
    float l0y = -hh - pad, l1y = hh + pad;

    auto v = [&](float px, float py, float lx, float ly) -> ShapeVert {
        return {px, py, lx, ly, hw, hh, rad,
                fill.x, fill.y, fill.z, fill.w,
                border.x, border.y, border.z, border.w};
    };
    shape_verts_.push_back(v(x0, y0, l0x, l0y));
    shape_verts_.push_back(v(x1, y0, l1x, l0y));
    shape_verts_.push_back(v(x1, y1, l1x, l1y));
    shape_verts_.push_back(v(x0, y0, l0x, l0y));
    shape_verts_.push_back(v(x1, y1, l1x, l1y));
    shape_verts_.push_back(v(x0, y1, l0x, l1y));
}

void Hud::push_text(float x, float y, const std::string& utf8, const Vec4& col) {
    float cx = std::floor(x + 0.5f);
    float cy = std::floor(y + 0.5f);
    const char* p = utf8.data();
    const char* end = p + utf8.size();

    constexpr float INV_COLS = 1.0f / static_cast<float>(FONT_ATLAS_COLS);
    constexpr float INV_ROWS = 1.0f / static_cast<float>(FONT_ATLAS_ROWS);

    while (p < end) {
        uint32_t cp = decode_utf8_next(p, end);
        int gi = font_glyph_index(cp);
        int col_i = gi % FONT_ATLAS_COLS;
        int row_i = gi / FONT_ATLAS_COLS;

        float u0 = static_cast<float>(col_i) * INV_COLS;
        float v0 = static_cast<float>(row_i) * INV_ROWS;
        float u1 = u0 + INV_COLS;
        float v1 = v0 + INV_ROWS;

        float x0 = cx;
        float y0 = cy;
        float x1 = cx + static_cast<float>(FONT_GLYPH_W);
        float y1 = cy + static_cast<float>(FONT_GLYPH_H);

        text_verts_.push_back({x0, y0, u0, v0, col.x, col.y, col.z, col.w});
        text_verts_.push_back({x1, y0, u1, v0, col.x, col.y, col.z, col.w});
        text_verts_.push_back({x1, y1, u1, v1, col.x, col.y, col.z, col.w});
        text_verts_.push_back({x0, y0, u0, v0, col.x, col.y, col.z, col.w});
        text_verts_.push_back({x1, y1, u1, v1, col.x, col.y, col.z, col.w});
        text_verts_.push_back({x0, y1, u0, v1, col.x, col.y, col.z, col.w});

        cx += static_cast<float>(FONT_GLYPH_W);
    }
}

void Hud::render(GlFuncs& gl,
                 int win_w,
                 int win_h,
                 const MeshData* mesh,
                 const Camera& cam,
                 const ViewerState& state,
                 int ssaa_samples,
                 const Vec2* axis_tips_px) {
    shape_verts_.clear();
    text_verts_.clear();
    hit_rects_.clear();
    has_slice_track_ = false;

    const bool dark = state.dark_theme;
    const Vec4 glass_bg   = dark ? Vec4{0.10f, 0.115f, 0.14f, 0.86f} : Vec4{0.97f, 0.98f, 0.99f, 0.90f};
    const Vec4 glass_bdr  = dark ? Vec4{0.28f, 0.31f, 0.37f, 0.72f}  : Vec4{0.72f, 0.76f, 0.82f, 0.90f};
    const Vec4 btn_hover  = dark ? Vec4{0.22f, 0.25f, 0.30f, 0.95f}  : Vec4{0.86f, 0.89f, 0.94f, 0.95f};
    const Vec4 btn_active = dark ? Vec4{0.22f, 0.48f, 0.84f, 0.95f}  : Vec4{0.14f, 0.43f, 0.80f, 0.95f};
    const Vec4 text_pri   = dark ? Vec4{0.93f, 0.95f, 0.97f, 1.0f}   : Vec4{0.11f, 0.13f, 0.17f, 1.0f};
    const Vec4 text_sec   = dark ? Vec4{0.66f, 0.71f, 0.78f, 1.0f}   : Vec4{0.35f, 0.40f, 0.47f, 1.0f};
    const Vec4 text_on_ac = Vec4{0.98f, 0.99f, 1.0f, 1.0f};
    const Vec4 accent_col = dark ? Vec4{0.35f, 0.64f, 0.98f, 1.0f}   : Vec4{0.12f, 0.42f, 0.80f, 1.0f};

    // Always draw crisp axis labels X / Y / Z near bottom-right triad if provided
    if (axis_tips_px && state.show_hud) {
        const char* names[3] = {"X", "Y", "Z"};
        const Vec4 cols[3] = {
            dark ? Vec4{0.96f, 0.42f, 0.38f, 0.95f} : Vec4{0.82f, 0.20f, 0.18f, 0.95f},
            dark ? Vec4{0.42f, 0.88f, 0.48f, 0.95f} : Vec4{0.16f, 0.62f, 0.22f, 0.95f},
            dark ? Vec4{0.42f, 0.68f, 0.98f, 0.95f} : Vec4{0.14f, 0.42f, 0.84f, 0.95f},
        };
        for (int i = 0; i < 3; ++i) {
            push_text(axis_tips_px[i].x - 4.0f, axis_tips_px[i].y - 8.0f, names[i], cols[i]);
        }
    }

    if (state.show_hud) {
        // 1. Top-left Info Pill
        if (mesh && mesh->ok) {
            char line[512];
            std::string fname = base_filename(mesh->path);
            std::string tris = format_count(mesh->tri_count);
            char sib[32] = "";
            if (state.sibling_count > 1 && state.sibling_index >= 0) {
                std::snprintf(sib, sizeof(sib), " [%d/%d]", state.sibling_index + 1, state.sibling_count);
            }
            char mode_tag[64] = "";
            if (cam.is_ortho()) std::strcat(mode_tag, " · Ortho");
            if (cam.up_axis() == UpAxis::YUp) std::strcat(mode_tag, " · Y-Up");

            std::snprintf(line, sizeof(line),
                          "%s%s  ·  %s tri  ·  %.1f×%.1f×%.1f mm  ·  %.1f cm³  ·  %.0f ms%s",
                          fname.c_str(), sib, tris.c_str(),
                          mesh->bbox_extent.x, mesh->bbox_extent.y, mesh->bbox_extent.z,
                          mesh->volume_mm3 * 0.001, mesh->load_ms, mode_tag);

            int tw = measure_utf8_px(line);
            int max_w = std::max(220, win_w - 32);
            float pw = static_cast<float>(std::min(tw + 28, max_w));
            float ph = 32.0f;
            push_rect(16.0f, 16.0f, pw, ph, 9.0f, glass_bg, glass_bdr);
            push_text(30.0f, 24.0f, line, text_pri);
        } else {
            // Empty state centered drop-zone card
            float cw = std::min(460.0f, static_cast<float>(win_w) - 32.0f);
            float ch = 104.0f;
            float cx = (static_cast<float>(win_w) - cw) * 0.5f;
            float cy = (static_cast<float>(win_h) - ch) * 0.5f - 20.0f;
            HudRect drop_r{cx, cy, cw, ch, HudCommand::OpenFile};
            hit_rects_.push_back(drop_r);
            bool hov = drop_r.contains(hover_x_, hover_y_);
            push_rect(cx, cy, cw, ch, 14.0f, hov ? btn_hover : glass_bg, glass_bdr);
            std::string t1 = "stlviewer  ·  Hardware STL Viewer";
            std::string t2 = "Drop an .stl file here or click / press O to open";
            push_text(cx + (cw - static_cast<float>(measure_utf8_px(t1))) * 0.5f, cy + 28.0f, t1, text_pri);
            push_text(cx + (cw - static_cast<float>(measure_utf8_px(t2))) * 0.5f, cy + 56.0f, t2, text_sec);
        }

        // Subtle top-right SSAA indicator only when settled or status message present
        if (!state.status_msg.empty()) {
            int tw = measure_utf8_px(state.status_msg);
            float pw = static_cast<float>(tw + 24);
            float px = static_cast<float>(win_w) - pw - 16.0f;
            push_rect(px, 16.0f, pw, 30.0f, 8.0f, glass_bg, glass_bdr);
            push_text(px + 12.0f, 23.0f, state.status_msg, accent_col);
        } else if (mesh && mesh->ok && ssaa_samples >= 16) {
            const std::string tag = "16× SSAA";
            float pw = static_cast<float>(measure_utf8_px(tag) + 20);
            float px = static_cast<float>(win_w) - pw - 16.0f;
            push_rect(px, 16.0f, pw, 28.0f, 8.0f, glass_bg, glass_bdr);
            push_text(px + 10.0f, 22.0f, tag, text_sec);
        }

        // 2. Bottom-center Floating Toolbar Pill
        std::vector<ButtonBox> btns;
        btns.reserve(10);
        auto add_btn = [&](HudCommand cmd, const std::string& lbl, bool act) {
            btns.push_back({{0, 0, 0, 28.0f, cmd}, lbl, act});
        };
        add_btn(HudCommand::OpenFile, "O Open", false);
        add_btn(HudCommand::CycleMatcap, std::string("M ") + matcap_name(state.matcap), false);
        add_btn(HudCommand::ToggleWire, "W Wire", state.wireframe);
        add_btn(HudCommand::ToggleSlice, "S Slice", state.slice_enabled);
        add_btn(HudCommand::ToggleMeasure, "D Meas", state.measure_mode);
        add_btn(HudCommand::ToggleXray, "X XRay", state.xray);
        add_btn(HudCommand::ToggleTwoSided, "B 2Side", state.two_sided);
        add_btn(HudCommand::ToggleTheme, state.dark_theme ? "T Dark" : "T Light", false);
        add_btn(HudCommand::ResetView, "R Reset", false);
        add_btn(HudCommand::ToggleHelp, "?", state.show_help);

        float gap = 4.0f;
        float pad_x = 8.0f;
        float total_w = 12.0f;
        for (auto& b : btns) {
            b.rect.w = static_cast<float>(measure_utf8_px(b.label)) + pad_x * 2.0f;
            total_w += b.rect.w;
        }
        total_w += gap * static_cast<float>(btns.size() - 1);

        float bar_h = 38.0f;
        float bar_y = static_cast<float>(win_h) - bar_h - 14.0f;
        float bar_x = std::floor((static_cast<float>(win_w) - total_w) * 0.5f);
        if (bar_x < 12.0f) bar_x = 12.0f;

        push_rect(bar_x, bar_y, total_w, bar_h, 11.0f, glass_bg, glass_bdr);

        float bx = bar_x + 6.0f;
        float by = bar_y + 5.0f;
        for (auto& b : btns) {
            b.rect.x = bx;
            b.rect.y = by;
            hit_rects_.push_back(b.rect);
            bool hov = b.rect.contains(hover_x_, hover_y_);
            if (b.active) {
                push_rect(bx, by, b.rect.w, b.rect.h, 7.0f, btn_active, Vec4{0, 0, 0, 0});
                push_text(bx + pad_x, by + 6.0f, b.label, text_on_ac);
            } else if (hov) {
                push_rect(bx, by, b.rect.w, b.rect.h, 7.0f, btn_hover, Vec4{0, 0, 0, 0});
                push_text(bx + pad_x, by + 6.0f, b.label, text_pri);
            } else {
                push_text(bx + pad_x, by + 6.0f, b.label, text_pri);
            }
            bx += b.rect.w + gap;
        }

        float sub_y = bar_y - 44.0f;

        // 3. Contextual Sub-pill: Slice Controls
        if (state.slice_enabled && mesh && mesh->ok) {
            float axis_min = (&mesh->bbox_min.x)[state.slice_axis];
            float axis_ext = (&mesh->bbox_extent.x)[state.slice_axis];
            float pos_mm = axis_min + state.slice_t * axis_ext;

            char mm_buf[32];
            std::snprintf(mm_buf, sizeof(mm_buf), "%.1f mm", pos_mm);

            float sw = 440.0f;
            float sh = 36.0f;
            float sx = std::floor((static_cast<float>(win_w) - sw) * 0.5f);
            push_rect(sx, sub_y, sw, sh, 10.0f, glass_bg, glass_bdr);

            float cx = sx + 8.0f;
            float cy = sub_y + 5.0f;
            const char* ax_names[3] = {"X", "Y", "Z"};
            const HudCommand ax_cmds[3] = {HudCommand::SliceAxisX, HudCommand::SliceAxisY, HudCommand::SliceAxisZ};
            for (int i = 0; i < 3; ++i) {
                HudRect ar{cx, cy, 26.0f, 26.0f, ax_cmds[i]};
                hit_rects_.push_back(ar);
                bool act = (state.slice_axis == i);
                bool hov = ar.contains(hover_x_, hover_y_);
                if (act || hov) {
                    push_rect(ar.x, ar.y, ar.w, ar.h, 6.0f, act ? btn_active : btn_hover, Vec4{0, 0, 0, 0});
                }
                push_text(ar.x + 9.0f, ar.y + 5.0f, ax_names[i], act ? text_on_ac : text_pri);
                cx += 30.0f;
            }

            HudRect flip_r{cx + 2.0f, cy, 56.0f, 26.0f, HudCommand::SliceFlip};
            hit_rects_.push_back(flip_r);
            bool f_hov = flip_r.contains(hover_x_, hover_y_);
            if (state.slice_flip || f_hov) {
                push_rect(flip_r.x, flip_r.y, flip_r.w, flip_r.h, 6.0f,
                          state.slice_flip ? btn_active : btn_hover, Vec4{0, 0, 0, 0});
            }
            push_text(flip_r.x + 8.0f, flip_r.y + 5.0f, "I Flip", state.slice_flip ? text_on_ac : text_pri);
            cx += 66.0f;

            // Scrubber track
            float track_w = 190.0f;
            float track_x = cx + 4.0f;
            slice_track_ = {track_x, cy, track_w, 26.0f, HudCommand::SliceScrub};
            has_slice_track_ = true;
            hit_rects_.push_back(slice_track_);

            Vec4 track_bg = dark ? Vec4{0.22f, 0.25f, 0.30f, 0.9f} : Vec4{0.78f, 0.81f, 0.86f, 0.9f};
            push_rect(track_x, cy + 10.0f, track_w, 6.0f, 3.0f, track_bg, Vec4{0, 0, 0, 0});
            float fill_w = std::max(6.0f, state.slice_t * track_w);
            push_rect(track_x, cy + 10.0f, fill_w, 6.0f, 3.0f, accent_col, Vec4{0, 0, 0, 0});
            float knob_x = std::clamp(track_x + state.slice_t * track_w - 6.0f, track_x, track_x + track_w - 12.0f);
            push_rect(knob_x, cy + 6.0f, 12.0f, 14.0f, 4.0f, text_on_ac, glass_bdr);

            push_text(track_x + track_w + 10.0f, cy + 5.0f, mm_buf, text_pri);
            sub_y -= 42.0f;
        }

        // 4. Contextual Sub-pill: Measure Status
        if (state.measure_mode) {
            char mbuf[160];
            if (!state.measure_has_a) {
                std::snprintf(mbuf, sizeof(mbuf), "Measure: Click surface for Point A  ·  Esc cancel");
            } else if (!state.measure_has_b) {
                std::snprintf(mbuf, sizeof(mbuf), "Point A set · Click surface for Point B  ·  Esc clear");
            } else {
                Vec3 d = state.measure_b - state.measure_a;
                float dist = length(d);
                std::snprintf(mbuf, sizeof(mbuf),
                              "Dist: %.2f mm  (ΔX %.2f · ΔY %.2f · ΔZ %.2f)  ·  Clear",
                              dist, std::fabs(d.x), std::fabs(d.y), std::fabs(d.z));
            }
            float mw = static_cast<float>(measure_utf8_px(mbuf) + 28);
            float mh = 32.0f;
            float mx = std::floor((static_cast<float>(win_w) - mw) * 0.5f);
            HudRect mr{mx, sub_y, mw, mh, HudCommand::MeasureClear};
            hit_rects_.push_back(mr);
            push_rect(mx, sub_y, mw, mh, 9.0f, glass_bg, glass_bdr);
            push_text(mx + 14.0f, sub_y + 8.0f, mbuf, text_pri);
        }

        // 5. Centered Help Overlay
        if (state.show_help) {
            float hw = std::min(560.0f, static_cast<float>(win_w) - 48.0f);
            float hh = 328.0f;
            float hx = std::floor((static_cast<float>(win_w) - hw) * 0.5f);
            float hy = std::floor((static_cast<float>(win_h) - hh) * 0.5f - 16.0f);
            push_rect(hx, hy, hw, hh, 14.0f, glass_bg, glass_bdr);
            push_text(hx + 22.0f, hy + 18.0f, "stlviewer — Controls & Shortcuts", text_pri);

            const char* left_lines[] = {
                "Left Drag        Orbit",
                "Shift / Mid / R  Pan",
                "Scroll           Zoom",
                "Dbl-Click        Pivot / Reset",
                "Drop .stl        Load model",
                "1 - 6 / 0        CAD / Iso views",
                "P                Persp / Ortho",
                "U / G            Z/Y-Up / Grid",
            };
            const char* right_lines[] = {
                "M                Cycle MatCap",
                "W / X / B        Wire / XRay / 2Side",
                "S / Tab / I      Slice / Axis / Flip",
                "D / Esc          Measure / Cancel",
                "[ / ]            Prev / Next STL",
                "O / R / T        Open / Reset / Theme",
                "H / ? / F1       Hide HUD / Help",
                "Q                Quit",
            };
            float ly = hy + 54.0f;
            for (int i = 0; i < 8; ++i) {
                push_text(hx + 22.0f, ly, left_lines[i], text_sec);
                push_text(hx + 292.0f, ly, right_lines[i], text_sec);
                ly += 26.0f;
            }
            push_text(hx + 22.0f, hy + hh - 34.0f, "Press ? / F1 / Esc or click ? to close", accent_col);
        }
    }

    if (shape_verts_.empty() && text_verts_.empty()) return;

    gl.Disable(GL_DEPTH_TEST);
    gl.DepthMask(GL_FALSE);
    gl.Disable(GL_CULL_FACE);
    gl.Enable(GL_BLEND);
    gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (!shape_verts_.empty()) {
        gl.UseProgram(shape_prog_);
        gl.Uniform2f(u_shape_vp_, static_cast<float>(win_w), static_cast<float>(win_h));
        gl.BindVertexArray(shape_vao_);
        gl.BindBuffer(GL_ARRAY_BUFFER, shape_vbo_);
        gl.BufferData(GL_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(shape_verts_.size() * sizeof(ShapeVert)),
                      shape_verts_.data(), GL_STREAM_DRAW);
        gl.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(shape_verts_.size()));
    }

    if (!text_verts_.empty()) {
        gl.UseProgram(text_prog_);
        gl.Uniform2f(u_text_vp_, static_cast<float>(win_w), static_cast<float>(win_h));
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, font_tex_);
        gl.Uniform1i(u_text_atlas_, 0);
        gl.BindVertexArray(text_vao_);
        gl.BindBuffer(GL_ARRAY_BUFFER, text_vbo_);
        gl.BufferData(GL_ARRAY_BUFFER,
                      static_cast<GLsizeiptr>(text_verts_.size() * sizeof(GlyphVert)),
                      text_verts_.data(), GL_STREAM_DRAW);
        gl.DrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(text_verts_.size()));
    }

    gl.BindVertexArray(0);
    gl.UseProgram(0);
    gl.Disable(GL_BLEND);
    gl.DepthMask(GL_TRUE);
}

bool Hud::update_hover(float mx, float my) {
    int prev_idx = -1;
    int new_idx = -1;
    for (size_t i = 0; i < hit_rects_.size(); ++i) {
        if (hit_rects_[i].contains(hover_x_, hover_y_)) prev_idx = static_cast<int>(i);
        if (hit_rects_[i].contains(mx, my)) new_idx = static_cast<int>(i);
    }
    hover_x_ = mx;
    hover_y_ = my;
    return prev_idx != new_idx;
}

HudCommand Hud::on_mouse_down(float mx, float my, float& out_slice_t) {
    for (size_t i = hit_rects_.size(); i > 0; --i) {
        const auto& r = hit_rects_[i - 1];
        if (r.contains(mx, my)) {
            if (r.cmd == HudCommand::SliceScrub && has_slice_track_) {
                scrubbing_slice_ = true;
                out_slice_t = std::clamp((mx - slice_track_.x) / slice_track_.w, 0.0f, 1.0f);
            }
            return r.cmd;
        }
    }
    return HudCommand::NoAction;
}

bool Hud::on_mouse_drag(float mx, float my, float& out_slice_t) {
    (void)my;
    if (!scrubbing_slice_ || !has_slice_track_) return false;
    out_slice_t = std::clamp((mx - slice_track_.x) / slice_track_.w, 0.0f, 1.0f);
    return true;
}

void Hud::on_mouse_up() {
    scrubbing_slice_ = false;
}

bool Hud::is_pointer_over_ui(float mx, float my) const {
    for (const auto& r : hit_rects_) {
        if (r.contains(mx, my)) return true;
    }
    return false;
}

}  // namespace faset
