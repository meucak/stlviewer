#include "renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace faset {

namespace {

const char* FULLSCREEN_VS = R"GLSL(#version 330 core
out vec2 vUV;
void main() {
    const vec2 pos[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    gl_Position = vec4(pos[gl_VertexID], 0.0, 1.0);
    vUV = pos[gl_VertexID] * 0.5 + 0.5;
}
)GLSL";

const char* BG_FS = R"GLSL(#version 330 core
in vec2 vUV;
uniform int uDark;
out vec4 FragColor;

void main() {
    float t = clamp(vUV.y, 0.0, 1.0);
    vec2 d = vUV - vec2(0.5);
    float vig = dot(d, d) * 0.35;
    vec3 col;
    if (uDark != 0) {
        vec3 bot = vec3(0.011, 0.014, 0.019);
        vec3 top = vec3(0.027, 0.033, 0.045);
        col = mix(bot, top, smoothstep(0.0, 1.0, t)) * (1.0 - vig * 0.45);
    } else {
        vec3 bot = vec3(0.58, 0.62, 0.68);
        vec3 top = vec3(0.84, 0.87, 0.91);
        col = mix(bot, top, smoothstep(0.0, 1.0, t)) * (1.0 - vig * 0.12);
    }
    FragColor = vec4(col, 1.0);
}
)GLSL";

const char* MESH_VS = R"GLSL(#version 330 core
layout(location = 0) in vec4 aPos;

uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uBboxMin;
uniform vec3 uQuantScale;
uniform vec4 uClipPlane;
uniform int uClipOn;

out vec3 vViewPos;

void main() {
    vec3 worldPos = uBboxMin + aPos.xyz * uQuantScale;
    vec4 vp = uView * vec4(worldPos, 1.0);
    vViewPos = vp.xyz;
    gl_ClipDistance[0] = (uClipOn != 0) ? dot(vec4(worldPos, 1.0), uClipPlane) : 1.0;
    gl_Position = uProj * vp;
}
)GLSL";

const char* MESH_FS = R"GLSL(#version 330 core
in vec3 vViewPos;

uniform sampler2D uMatcap;
uniform int uXray;
uniform int uWire;
uniform int uTwoSided;
uniform int uDark;

out vec4 FragColor;

void main() {
    if (uWire != 0) {
        vec3 wc = (uDark != 0) ? vec3(0.18, 0.48, 0.88) : vec3(0.04, 0.14, 0.30);
        FragColor = vec4(wc, 0.85);
        return;
    }

    vec3 dpdx = dFdx(vViewPos);
    vec3 dpdy = dFdy(vViewPos);
    vec3 crossN = cross(dpdx, dpdy);
    float len2 = dot(crossN, crossN);
    vec3 N = (len2 > 1e-20) ? (crossN * inversesqrt(len2)) : vec3(0.0, 0.0, 1.0);

    bool backFace = !gl_FrontFacing;
    if (backFace) {
        N = -N;
    }
    if (N.z < 0.0) {
        // Grazing sub-pixel derivative guard
        N.z = 0.0;
        float xy2 = dot(N.xy, N.xy);
        if (xy2 > 1e-8) N.xy *= inversesqrt(xy2);
    }

    if (uXray != 0) {
        float rim = pow(clamp(1.0 - abs(N.z), 0.0, 1.0), 1.85);
        vec3 base = (uDark != 0) ? vec3(0.16, 0.56, 0.96) : vec3(0.04, 0.24, 0.54);
        vec3 col = base * (0.28 + 1.35 * rim);
        float alpha = clamp(0.14 + 0.72 * rim, 0.14, 0.88);
        FragColor = vec4(col, alpha);
        return;
    }

    vec2 uv = N.xy * 0.492 + 0.5;
    vec3 col = texture(uMatcap, uv).rgb;
    if (backFace) {
        // Subtle warm terracotta interior read when sliced or inspected two-sided
        vec3 backTint = vec3(0.55, 0.22, 0.14);
        col = mix(col * 0.75, backTint * (0.45 + 0.65 * clamp(N.z, 0.0, 1.0)), 0.68);
    }
    FragColor = vec4(col, 1.0);
}
)GLSL";

const char* GRID_VS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uVP;
out vec3 vWorld;
void main() {
    vWorld = aPos;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)GLSL";

const char* GRID_FS = R"GLSL(#version 330 core
in vec3 vWorld;

uniform vec3 uCenter;
uniform float uStep;
uniform float uRadius;
uniform int uYUp;
uniform int uDark;

out vec4 FragColor;

float grid_line(vec2 coord, float spacing) {
    vec2 p = coord / spacing;
    vec2 g = abs(fract(p - 0.5) - 0.5) / max(fwidth(p), vec2(1e-5));
    float d = min(g.x, g.y);
    return 1.0 - min(d, 1.0);
}

void main() {
    vec2 plane = (uYUp != 0) ? vWorld.xz : vWorld.xy;
    vec2 center2 = (uYUp != 0) ? uCenter.xz : uCenter.xy;

    float dist = length(plane - center2);
    float fade = exp(-(dist * dist) / max(uRadius * uRadius * 0.42, 1e-4));
    if (fade < 0.005) discard;

    float minor = grid_line(plane, uStep);
    float major = grid_line(plane, uStep * 10.0);

    // Highlight world origin axes faintly
    vec2 fw = max(fwidth(plane), vec2(1e-5));
    float ax0 = 1.0 - min(abs(plane.y) / (fw.y * 1.4), 1.0);
    float ax1 = 1.0 - min(abs(plane.x) / (fw.x * 1.4), 1.0);

    vec3 minorCol = (uDark != 0) ? vec3(0.18, 0.21, 0.27) : vec3(0.32, 0.36, 0.42);
    vec3 majorCol = (uDark != 0) ? vec3(0.28, 0.33, 0.42) : vec3(0.20, 0.24, 0.30);
    vec3 xCol     = (uDark != 0) ? vec3(0.75, 0.24, 0.24) : vec3(0.72, 0.16, 0.16);
    vec3 yCol     = (uDark != 0) ? vec3(0.24, 0.68, 0.32) : vec3(0.14, 0.52, 0.20);
    if (uYUp != 0) {
        yCol = (uDark != 0) ? vec3(0.26, 0.48, 0.82) : vec3(0.14, 0.34, 0.72);
    }

    float a = max(minor * 0.22, major * 0.48);
    vec3 col = mix(minorCol, majorCol, smoothstep(0.1, 0.9, major));
    if (ax0 > 0.01) {
        col = mix(col, xCol, ax0 * 0.8);
        a = max(a, ax0 * 0.65);
    }
    if (ax1 > 0.01) {
        col = mix(col, yCol, ax1 * 0.8);
        a = max(a, ax1 * 0.65);
    }

    a *= fade;
    if (a < 0.004) discard;
    FragColor = vec4(col, a);
}
)GLSL";

const char* LINE_VS = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
uniform mat4 uVP;
out vec4 vCol;
void main() {
    gl_Position = uVP * vec4(aPos, 1.0);
    vCol = aCol;
}
)GLSL";

const char* LINE_FS = R"GLSL(#version 330 core
in vec4 vCol;
out vec4 FragColor;
void main() {
    FragColor = vCol;
}
)GLSL";

const char* ACCUM_FS = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
uniform float uWeight;
out vec4 FragColor;
void main() {
    vec3 c = texture(uTex, vUV).rgb;
    FragColor = vec4(c, uWeight);
}
)GLSL";

const char* RESOLVE_FS = R"GLSL(#version 330 core
in vec2 vUV;
uniform sampler2D uTex;
out vec4 FragColor;

vec3 linear_to_srgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, lo, lessThanEqual(c, vec3(0.0031308)));
}

void main() {
    vec3 c = texture(uTex, vUV).rgb;
    vec3 s = linear_to_srgb(c);
    // Subtle 1-LSB triangular screen-space dither to prevent gradient banding
    float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    s += (n - 0.5) / 255.0;
    FragColor = vec4(clamp(s, 0.0, 1.0), 1.0);
}
)GLSL";

GLuint build_prog(GlFuncs& gl, const char* vs_src, const char* fs_src, const char* tag, std::string& err) {
    auto comp = [&](GLenum type, const char* src) -> GLuint {
        GLuint s = gl.CreateShader(type);
        gl.ShaderSource(s, 1, &src, nullptr);
        gl.CompileShader(s);
        GLint ok = 0;
        gl.GetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024]{};
            gl.GetShaderInfoLog(s, sizeof(log), nullptr, log);
            err = std::string(tag) + " shader compile error: " + log;
            gl.DeleteShader(s);
            return 0;
        }
        return s;
    };
    GLuint vs = comp(GL_VERTEX_SHADER, vs_src);
    if (!vs) return 0;
    GLuint fs = comp(GL_FRAGMENT_SHADER, fs_src);
    if (!fs) {
        gl.DeleteShader(vs);
        return 0;
    }
    GLuint p = gl.CreateProgram();
    gl.AttachShader(p, vs);
    gl.AttachShader(p, fs);
    gl.LinkProgram(p);
    gl.DeleteShader(vs);
    gl.DeleteShader(fs);
    GLint ok = 0;
    gl.GetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024]{};
        gl.GetProgramInfoLog(p, sizeof(log), nullptr, log);
        err = std::string(tag) + " program link error: " + log;
        gl.DeleteProgram(p);
        return 0;
    }
    return p;
}

struct LineVert {
    float x, y, z;
    float r, g, b, a;
};

void append_diamond_3d(std::vector<LineVert>& out, const Vec3& p, float r, const Vec4& col) {
    Vec3 px{p.x + r, p.y, p.z}, nx{p.x - r, p.y, p.z};
    Vec3 py{p.x, p.y + r, p.z}, ny{p.x, p.y - r, p.z};
    Vec3 pz{p.x, p.y, p.z + r}, nz{p.x, p.y, p.z - r};
    auto seg = [&](const Vec3& a, const Vec3& b) {
        out.push_back({a.x, a.y, a.z, col.x, col.y, col.z, col.w});
        out.push_back({b.x, b.y, b.z, col.x, col.y, col.z, col.w});
    };
    seg(px, py); seg(py, nx); seg(nx, ny); seg(ny, px);
    seg(px, pz); seg(pz, nx); seg(nx, nz); seg(nz, px);
    seg(py, pz); seg(pz, ny); seg(ny, nz); seg(nz, py);
}

}  // namespace

bool Renderer::create_fbos(GlFuncs& gl, int w, int h, std::string& err) {
    destroy_fbos(gl);

    auto try_build = [&](GLint internal_fmt, GLenum type) -> bool {
        gl.GenTextures(1, &scene_color_);
        gl.BindTexture(GL_TEXTURE_2D, scene_color_);
        gl.TexImage2D(GL_TEXTURE_2D, 0, internal_fmt, w, h, 0, GL_RGBA, type, nullptr);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        gl.GenTextures(1, &scene_depth_);
        gl.BindTexture(GL_TEXTURE_2D, scene_depth_);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        gl.GenFramebuffers(1, &scene_fbo_);
        gl.BindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_color_, 0);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, scene_depth_, 0);
        GLenum db = GL_COLOR_ATTACHMENT0;
        gl.DrawBuffers(1, &db);
        bool ok1 = (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        gl.GenTextures(1, &accum_color_);
        gl.BindTexture(GL_TEXTURE_2D, accum_color_);
        gl.TexImage2D(GL_TEXTURE_2D, 0, internal_fmt, w, h, 0, GL_RGBA, type, nullptr);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        gl.GenFramebuffers(1, &accum_fbo_);
        gl.BindFramebuffer(GL_FRAMEBUFFER, accum_fbo_);
        gl.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, accum_color_, 0);
        gl.DrawBuffers(1, &db);
        bool ok2 = (gl.CheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);

        gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl.BindTexture(GL_TEXTURE_2D, 0);
        return ok1 && ok2;
    };

    if (try_build(GL_RGBA16F, GL_HALF_FLOAT)) {
        fp16_fbo_ = true;
        return true;
    }
    destroy_fbos(gl);
    if (try_build(GL_RGBA8, GL_UNSIGNED_BYTE)) {
        fp16_fbo_ = false;
        return true;
    }
    destroy_fbos(gl);
    err = "Failed to create complete scene/accumulation framebuffers";
    return false;
}

void Renderer::destroy_fbos(GlFuncs& gl) {
    if (scene_fbo_) gl.DeleteFramebuffers(1, &scene_fbo_);
    if (accum_fbo_) gl.DeleteFramebuffers(1, &accum_fbo_);
    if (scene_color_) gl.DeleteTextures(1, &scene_color_);
    if (scene_depth_) gl.DeleteTextures(1, &scene_depth_);
    if (accum_color_) gl.DeleteTextures(1, &accum_color_);
    scene_fbo_ = accum_fbo_ = scene_color_ = scene_depth_ = accum_color_ = 0;
}

bool Renderer::init(GlFuncs& gl, int width, int height, std::string& err) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);

    bg_prog_ = build_prog(gl, FULLSCREEN_VS, BG_FS, "bg", err);
    if (!bg_prog_) return false;
    u_bg_dark_ = gl.GetUniformLocation(bg_prog_, "uDark");

    mesh_prog_ = build_prog(gl, MESH_VS, MESH_FS, "mesh", err);
    if (!mesh_prog_) return false;
    u_mesh_view_ = gl.GetUniformLocation(mesh_prog_, "uView");
    u_mesh_proj_ = gl.GetUniformLocation(mesh_prog_, "uProj");
    u_mesh_bmin_ = gl.GetUniformLocation(mesh_prog_, "uBboxMin");
    u_mesh_qscale_ = gl.GetUniformLocation(mesh_prog_, "uQuantScale");
    u_mesh_clip_plane_ = gl.GetUniformLocation(mesh_prog_, "uClipPlane");
    u_mesh_clip_on_ = gl.GetUniformLocation(mesh_prog_, "uClipOn");
    u_mesh_matcap_ = gl.GetUniformLocation(mesh_prog_, "uMatcap");
    u_mesh_xray_ = gl.GetUniformLocation(mesh_prog_, "uXray");
    u_mesh_wire_ = gl.GetUniformLocation(mesh_prog_, "uWire");
    u_mesh_twosided_ = gl.GetUniformLocation(mesh_prog_, "uTwoSided");
    u_mesh_dark_ = gl.GetUniformLocation(mesh_prog_, "uDark");

    grid_prog_ = build_prog(gl, GRID_VS, GRID_FS, "grid", err);
    if (!grid_prog_) return false;
    u_grid_vp_ = gl.GetUniformLocation(grid_prog_, "uVP");
    u_grid_center_ = gl.GetUniformLocation(grid_prog_, "uCenter");
    u_grid_step_ = gl.GetUniformLocation(grid_prog_, "uStep");
    u_grid_radius_ = gl.GetUniformLocation(grid_prog_, "uRadius");
    u_grid_yup_ = gl.GetUniformLocation(grid_prog_, "uYUp");
    u_grid_dark_ = gl.GetUniformLocation(grid_prog_, "uDark");

    line_prog_ = build_prog(gl, LINE_VS, LINE_FS, "line", err);
    if (!line_prog_) return false;
    u_line_vp_ = gl.GetUniformLocation(line_prog_, "uVP");

    accum_prog_ = build_prog(gl, FULLSCREEN_VS, ACCUM_FS, "accum", err);
    if (!accum_prog_) return false;
    u_accum_tex_ = gl.GetUniformLocation(accum_prog_, "uTex");
    u_accum_weight_ = gl.GetUniformLocation(accum_prog_, "uWeight");

    resolve_prog_ = build_prog(gl, FULLSCREEN_VS, RESOLVE_FS, "resolve", err);
    if (!resolve_prog_) return false;
    u_resolve_tex_ = gl.GetUniformLocation(resolve_prog_, "uTex");

    gl.GenVertexArrays(1, &empty_vao_);

    gl.GenVertexArrays(1, &mesh_vao_);
    gl.GenBuffers(1, &mesh_vbo_);
    gl.BindVertexArray(mesh_vao_);
    gl.BindBuffer(GL_ARRAY_BUFFER, mesh_vbo_);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 4, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(QuantizedVertex), nullptr);

    gl.GenVertexArrays(1, &line_vao_);
    gl.GenBuffers(1, &line_vbo_);
    gl.BindVertexArray(line_vao_);
    gl.BindBuffer(GL_ARRAY_BUFFER, line_vbo_);
    gl.EnableVertexAttribArray(0);
    gl.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVert), reinterpret_cast<void*>(offsetof(LineVert, x)));
    gl.EnableVertexAttribArray(1);
    gl.VertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(LineVert), reinterpret_cast<void*>(offsetof(LineVert, r)));
    gl.BindVertexArray(0);

    // Upload procedural studio MatCaps
    gl.GenTextures(static_cast<int>(MatcapKind::Count), matcap_tex_);
    gl.PixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (int i = 0; i < static_cast<int>(MatcapKind::Count); ++i) {
        auto pixels = generate_matcap_rgb8(static_cast<MatcapKind>(i));
        gl.BindTexture(GL_TEXTURE_2D, matcap_tex_[i]);
        gl.TexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8, MATCAP_SIZE, MATCAP_SIZE, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        gl.GenerateMipmap(GL_TEXTURE_2D);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl.TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    gl.BindTexture(GL_TEXTURE_2D, 0);

    if (!create_fbos(gl, width_, height_, err)) return false;
    accum_count_ = 0;
    return true;
}

void Renderer::destroy(GlFuncs& gl) {
    destroy_fbos(gl);
    gl.DeleteTextures(static_cast<int>(MatcapKind::Count), matcap_tex_);
    for (auto& t : matcap_tex_) t = 0;

    if (mesh_vbo_) gl.DeleteBuffers(1, &mesh_vbo_);
    if (mesh_vao_) gl.DeleteVertexArrays(1, &mesh_vao_);
    if (line_vbo_) gl.DeleteBuffers(1, &line_vbo_);
    if (line_vao_) gl.DeleteVertexArrays(1, &line_vao_);
    if (empty_vao_) gl.DeleteVertexArrays(1, &empty_vao_);

    if (bg_prog_) gl.DeleteProgram(bg_prog_);
    if (mesh_prog_) gl.DeleteProgram(mesh_prog_);
    if (grid_prog_) gl.DeleteProgram(grid_prog_);
    if (line_prog_) gl.DeleteProgram(line_prog_);
    if (accum_prog_) gl.DeleteProgram(accum_prog_);
    if (resolve_prog_) gl.DeleteProgram(resolve_prog_);

    mesh_vbo_ = mesh_vao_ = line_vbo_ = line_vao_ = empty_vao_ = 0;
    bg_prog_ = mesh_prog_ = grid_prog_ = line_prog_ = accum_prog_ = resolve_prog_ = 0;
}

void Renderer::resize(GlFuncs& gl, int width, int height) {
    int w = std::max(1, width);
    int h = std::max(1, height);
    if (w == width_ && h == height_) return;
    width_ = w;
    height_ = h;
    std::string dummy_err;
    create_fbos(gl, width_, height_, dummy_err);
    accum_count_ = 0;
}

bool Renderer::upload_mesh(GlFuncs& gl, const MeshData& mesh, std::string& err) {
    if (!mesh.ok || mesh.vertices.empty()) {
        has_mesh_ = false;
        return false;
    }
    while (gl.GetError() != GL_NO_ERROR) {}
    gl.BindBuffer(GL_ARRAY_BUFFER, mesh_vbo_);
    GLsizeiptr bytes = static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(QuantizedVertex));
    gl.BufferData(GL_ARRAY_BUFFER, bytes, mesh.vertices.data(), GL_STATIC_DRAW);
    gl.BindBuffer(GL_ARRAY_BUFFER, 0);
    GLenum ge = gl.GetError();
    if (ge == GL_OUT_OF_MEMORY) {
        err = "GPU out of memory uploading STL VBO";
        has_mesh_ = false;
        return false;
    }
    has_mesh_ = true;
    accum_count_ = 0;
    return true;
}

void Renderer::clear_mesh() {
    has_mesh_ = false;
    accum_count_ = 0;
}

void Renderer::reset_accumulation() {
    accum_count_ = 0;
}

CullStats Renderer::render_frame(GlFuncs& gl,
                                 const MeshData* mesh,
                                 const Camera& cam,
                                 const ViewerState& state,
                                 Hud& hud) {
    CullStats cstats{};

    if (accum_count_ < MAX_SSAA_SAMPLES) {
        const int sample_idx = accum_count_;
        float jx = 0.0f, jy = 0.0f;
        subpixel_jitter_ndc(static_cast<uint32_t>(sample_idx), width_, height_, jx, jy);
        last_jx_ = jx;
        last_jy_ = jy;

        Mat4 view = cam.view_matrix();
        Mat4 proj = cam.proj_matrix(width_, height_, jx, jy);
        Mat4 vp = proj * view;

        // 1. Render scene into scene_fbo_
        gl.BindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
        gl.Viewport(0, 0, width_, height_);

        gl.Disable(GL_DEPTH_TEST);
        gl.DepthMask(GL_FALSE);
        gl.Disable(GL_CULL_FACE);
        gl.Disable(GL_BLEND);

        // Background gradient
        gl.UseProgram(bg_prog_);
        gl.Uniform1i(u_bg_dark_, state.dark_theme ? 1 : 0);
        gl.BindVertexArray(empty_vao_);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);

        // Clear depth for 3D scene
        gl.DepthMask(GL_TRUE);
        gl.ClearDepth(1.0);
        gl.Clear(GL_DEPTH_BUFFER_BIT);
        gl.Enable(GL_DEPTH_TEST);
        gl.DepthFunc(GL_LESS);

        bool model_valid = (has_mesh_ && mesh && mesh->ok);
        if (model_valid) {
            bool use_cull = (!state.two_sided && !state.slice_enabled && !state.xray);
            if (use_cull) {
                gl.Enable(GL_CULL_FACE);
                gl.CullFace(GL_BACK);
                gl.FrontFace(GL_CCW);
            } else {
                gl.Disable(GL_CULL_FACE);
            }

            // Compute culling using unjittered frustum slightly padded
            Frustum fr = Frustum::from_view_proj(cam.view_proj(width_, height_));
            CullOptions copts{};
            copts.backface_cone_cull = use_cull;
            copts.is_ortho = cam.is_ortho();
            copts.eye = cam.eye();
            copts.cam_forward = cam.forward();
            cull_chunks(mesh->chunks, fr, copts, spans_, &cstats);

            float clip_plane[4] = {0, 0, 0, 1};
            if (state.slice_enabled) {
                gl.Enable(GL_CLIP_DISTANCE0);
                float ax_min = (&mesh->bbox_min.x)[state.slice_axis];
                float ax_ext = (&mesh->bbox_extent.x)[state.slice_axis];
                float pos = ax_min + state.slice_t * ax_ext;
                float sign = state.slice_flip ? 1.0f : -1.0f;
                clip_plane[state.slice_axis] = sign;
                clip_plane[3] = -sign * pos;
            } else {
                gl.Disable(GL_CLIP_DISTANCE0);
            }

            gl.UseProgram(mesh_prog_);
            gl.UniformMatrix4fv(u_mesh_view_, 1, GL_FALSE, view.m);
            gl.UniformMatrix4fv(u_mesh_proj_, 1, GL_FALSE, proj.m);
            gl.Uniform3f(u_mesh_bmin_, mesh->bbox_min.x, mesh->bbox_min.y, mesh->bbox_min.z);
            gl.Uniform3f(u_mesh_qscale_, mesh->quant_scale.x, mesh->quant_scale.y, mesh->quant_scale.z);
            gl.Uniform4f(u_mesh_clip_plane_, clip_plane[0], clip_plane[1], clip_plane[2], clip_plane[3]);
            gl.Uniform1i(u_mesh_clip_on_, state.slice_enabled ? 1 : 0);
            gl.Uniform1i(u_mesh_twosided_, state.two_sided ? 1 : 0);
            gl.Uniform1i(u_mesh_dark_, state.dark_theme ? 1 : 0);

            int mc = std::clamp(static_cast<int>(state.matcap), 0, static_cast<int>(MatcapKind::Count) - 1);
            gl.ActiveTexture(GL_TEXTURE0);
            gl.BindTexture(GL_TEXTURE_2D, matcap_tex_[mc]);
            gl.Uniform1i(u_mesh_matcap_, 0);

            gl.BindVertexArray(mesh_vao_);

            if (state.xray) {
                gl.DepthMask(GL_FALSE);
                gl.Enable(GL_BLEND);
                gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                gl.Uniform1i(u_mesh_xray_, 1);
                gl.Uniform1i(u_mesh_wire_, 0);
                for (const auto& sp : spans_) {
                    gl.DrawArrays(GL_TRIANGLES, sp.first_vert, sp.vert_count);
                }
                gl.Disable(GL_BLEND);
                gl.DepthMask(GL_TRUE);
            } else {
                gl.Uniform1i(u_mesh_xray_, 0);
                gl.Uniform1i(u_mesh_wire_, 0);
                for (const auto& sp : spans_) {
                    gl.DrawArrays(GL_TRIANGLES, sp.first_vert, sp.vert_count);
                }
            }

            if (state.wireframe) {
                gl.Disable(GL_CULL_FACE);
                gl.Enable(GL_BLEND);
                gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                gl.Enable(GL_POLYGON_OFFSET_LINE);
                gl.PolygonOffset(-1.0f, -1.0f);
                gl.PolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                gl.Uniform1i(u_mesh_xray_, 0);
                gl.Uniform1i(u_mesh_wire_, 1);
                for (const auto& sp : spans_) {
                    gl.DrawArrays(GL_TRIANGLES, sp.first_vert, sp.vert_count);
                }
                gl.PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                gl.Disable(GL_POLYGON_OFFSET_LINE);
                gl.Disable(GL_BLEND);
            }

            if (state.slice_enabled) {
                gl.Disable(GL_CLIP_DISTANCE0);
            }
        }

        // Ground Grid
        if (state.show_grid) {
            Vec3 gc = model_valid ? (mesh->bbox_min + mesh->bbox_max) * 0.5f : Vec3{0, 0, 0};
            float mr = model_valid ? std::max(length(mesh->bbox_extent) * 0.5f, 1.0f) : 50.0f;
            float g_span = std::max(mr * 10.0f, cam.distance() * 6.0f);
            float log_s = std::floor(std::log10(std::max(mr * 0.45f, 0.05f)));
            float step = std::pow(10.0f, log_s);

            float ground_level = 0.0f;
            LineVert quad[6]{};
            if (cam.up_axis() == UpAxis::ZUp) {
                ground_level = model_valid ? (mesh->bbox_min.z - mr * 0.001f) : 0.0f;
                float x0 = gc.x - g_span, x1 = gc.x + g_span;
                float y0 = gc.y - g_span, y1 = gc.y + g_span;
                quad[0] = {x0, y0, ground_level, 0, 0, 0, 0};
                quad[1] = {x1, y0, ground_level, 0, 0, 0, 0};
                quad[2] = {x1, y1, ground_level, 0, 0, 0, 0};
                quad[3] = {x0, y0, ground_level, 0, 0, 0, 0};
                quad[4] = {x1, y1, ground_level, 0, 0, 0, 0};
                quad[5] = {x0, y1, ground_level, 0, 0, 0, 0};
            } else {
                ground_level = model_valid ? (mesh->bbox_min.y - mr * 0.001f) : 0.0f;
                float x0 = gc.x - g_span, x1 = gc.x + g_span;
                float z0 = gc.z - g_span, z1 = gc.z + g_span;
                quad[0] = {x0, ground_level, z0, 0, 0, 0, 0};
                quad[1] = {x1, ground_level, z0, 0, 0, 0, 0};
                quad[2] = {x1, ground_level, z1, 0, 0, 0, 0};
                quad[3] = {x0, ground_level, z0, 0, 0, 0, 0};
                quad[4] = {x1, ground_level, z1, 0, 0, 0, 0};
                quad[5] = {x0, ground_level, z1, 0, 0, 0, 0};
            }

            gl.Disable(GL_CULL_FACE);
            gl.DepthMask(GL_FALSE);
            gl.Enable(GL_BLEND);
            gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            gl.UseProgram(grid_prog_);
            gl.UniformMatrix4fv(u_grid_vp_, 1, GL_FALSE, vp.m);
            gl.Uniform3f(u_grid_center_, gc.x, gc.y, gc.z);
            gl.Uniform1f(u_grid_step_, step);
            gl.Uniform1f(u_grid_radius_, std::max(mr * 4.5f, cam.distance() * 1.4f));
            gl.Uniform1i(u_grid_yup_, cam.up_axis() == UpAxis::YUp ? 1 : 0);
            gl.Uniform1i(u_grid_dark_, state.dark_theme ? 1 : 0);

            gl.BindVertexArray(line_vao_);
            gl.BindBuffer(GL_ARRAY_BUFFER, line_vbo_);
            gl.BufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
            gl.DrawArrays(GL_TRIANGLES, 0, 6);

            gl.Disable(GL_BLEND);
            gl.DepthMask(GL_TRUE);
        }

        // 3D Measurement pins & line
        if (state.measure_has_a) {
            std::vector<LineVert> mverts;
            float r_pin = std::max(cam.distance() * 0.012f, 0.1f);
            Vec4 pin_col{0.98f, 0.62f, 0.15f, 1.0f};
            Vec4 line_col{0.35f, 0.78f, 1.00f, 1.0f};
            append_diamond_3d(mverts, state.measure_a, r_pin, pin_col);
            if (state.measure_has_b) {
                append_diamond_3d(mverts, state.measure_b, r_pin, pin_col);
                mverts.push_back({state.measure_a.x, state.measure_a.y, state.measure_a.z,
                                  line_col.x, line_col.y, line_col.z, line_col.w});
                mverts.push_back({state.measure_b.x, state.measure_b.y, state.measure_b.z,
                                  line_col.x, line_col.y, line_col.z, line_col.w});
            }

            gl.UseProgram(line_prog_);
            gl.UniformMatrix4fv(u_line_vp_, 1, GL_FALSE, vp.m);
            gl.BindVertexArray(line_vao_);
            gl.BindBuffer(GL_ARRAY_BUFFER, line_vbo_);
            gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mverts.size() * sizeof(LineVert)),
                          mverts.data(), GL_STREAM_DRAW);

            gl.DepthMask(GL_FALSE);
            gl.DepthFunc(GL_LEQUAL);
            gl.DrawArrays(GL_LINES, 0, static_cast<GLsizei>(mverts.size()));

            // Faint see-through pass for buried internal segments
            gl.Enable(GL_BLEND);
            gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            gl.DepthFunc(GL_GREATER);
            for (auto& v : mverts) v.a *= 0.30f;
            gl.BufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(mverts.size() * sizeof(LineVert)),
                          mverts.data(), GL_STREAM_DRAW);
            gl.DrawArrays(GL_LINES, 0, static_cast<GLsizei>(mverts.size()));
            gl.DepthFunc(GL_LESS);
            gl.Disable(GL_BLEND);
            gl.DepthMask(GL_TRUE);
        }

        // 2. Accumulate scene_color_ -> accum_fbo_
        ++accum_count_;
        gl.BindFramebuffer(GL_FRAMEBUFFER, accum_fbo_);
        gl.Disable(GL_DEPTH_TEST);
        gl.DepthMask(GL_FALSE);
        gl.Disable(GL_CULL_FACE);
        if (accum_count_ == 1) {
            gl.Disable(GL_BLEND);
        } else {
            gl.Enable(GL_BLEND);
            gl.BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        gl.UseProgram(accum_prog_);
        gl.ActiveTexture(GL_TEXTURE0);
        gl.BindTexture(GL_TEXTURE_2D, scene_color_);
        gl.Uniform1i(u_accum_tex_, 0);
        gl.Uniform1f(u_accum_weight_, 1.0f / static_cast<float>(accum_count_));
        gl.BindVertexArray(empty_vao_);
        gl.DrawArrays(GL_TRIANGLES, 0, 3);
        gl.Disable(GL_BLEND);
    }

    // 3. Resolve accum_color_ -> Default Framebuffer 0
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.Viewport(0, 0, width_, height_);
    gl.Disable(GL_DEPTH_TEST);
    gl.DepthMask(GL_FALSE);
    gl.Disable(GL_CULL_FACE);
    gl.Disable(GL_BLEND);

    gl.UseProgram(resolve_prog_);
    gl.ActiveTexture(GL_TEXTURE0);
    gl.BindTexture(GL_TEXTURE_2D, accum_color_);
    gl.Uniform1i(u_resolve_tex_, 0);
    gl.BindVertexArray(empty_vao_);
    gl.DrawArrays(GL_TRIANGLES, 0, 3);

    // 4. Bottom-right crisp 3D Axis Triad (drawn unjittered directly on FBO 0)
    Vec2 axis_tips[3]{};
    const Vec2* tips_ptr = nullptr;
    if (state.show_hud) {
        Mat4 v = cam.view_matrix();
        float cx = static_cast<float>(width_) - 54.0f;
        float cy = static_cast<float>(height_) - 54.0f;
        float arm = 24.0f;
        float label_arm = 35.0f;

        Vec3 dirs[3] = {
            {v.m[0], v.m[1], v.m[2]},   // world +X in view space
            {v.m[4], v.m[5], v.m[6]},   // world +Y in view space
            {v.m[8], v.m[9], v.m[10]},  // world +Z in view space
        };
        const Vec4 cols[3] = {
            state.dark_theme ? Vec4{0.96f, 0.36f, 0.32f, 1.0f} : Vec4{0.82f, 0.18f, 0.16f, 1.0f},
            state.dark_theme ? Vec4{0.34f, 0.88f, 0.42f, 1.0f} : Vec4{0.14f, 0.60f, 0.20f, 1.0f},
            state.dark_theme ? Vec4{0.36f, 0.64f, 0.98f, 1.0f} : Vec4{0.12f, 0.40f, 0.84f, 1.0f},
        };

        auto to_ndc = [&](float px, float py) -> Vec3 {
            return {
                (px / static_cast<float>(width_)) * 2.0f - 1.0f,
                1.0f - (py / static_cast<float>(height_)) * 2.0f,
                0.0f
            };
        };

        LineVert triad[6]{};
        Vec3 o_ndc = to_ndc(cx, cy);
        for (int i = 0; i < 3; ++i) {
            float sx = cx + dirs[i].x * arm;
            float sy = cy - dirs[i].y * arm;
            axis_tips[i] = {cx + dirs[i].x * label_arm, cy - dirs[i].y * label_arm};
            Vec3 e_ndc = to_ndc(sx, sy);
            triad[i * 2 + 0] = {o_ndc.x, o_ndc.y, 0.0f, cols[i].x, cols[i].y, cols[i].z, 1.0f};
            triad[i * 2 + 1] = {e_ndc.x, e_ndc.y, 0.0f, cols[i].x, cols[i].y, cols[i].z, 1.0f};
        }

        Mat4 id = Mat4::identity();
        gl.UseProgram(line_prog_);
        gl.UniformMatrix4fv(u_line_vp_, 1, GL_FALSE, id.m);
        gl.BindVertexArray(line_vao_);
        gl.BindBuffer(GL_ARRAY_BUFFER, line_vbo_);
        gl.BufferData(GL_ARRAY_BUFFER, sizeof(triad), triad, GL_STREAM_DRAW);
        gl.DrawArrays(GL_LINES, 0, 6);
        tips_ptr = axis_tips;
    }

    // 5. Render crisp 2D Apple-HIG HUD
    hud.render(gl, width_, height_, mesh, cam, state, accum_count_, tips_ptr);

    return cstats;
}

bool Renderer::pick_world_point(GlFuncs& gl, int x, int y, const Camera& cam, Vec3& out_world) const {
    if (!scene_fbo_ || x < 0 || y < 0 || x >= width_ || y >= height_) return false;
    int gl_y = height_ - 1 - y;
    float depth = 1.0f;
    gl.BindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    gl.ReadPixels(x, gl_y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);

    if (depth >= 0.99999f) return false;
    Mat4 inv_vp{};
    if (!Mat4::inverse(cam.view_proj(width_, height_, last_jx_, last_jy_), inv_vp)) return false;
    return Mat4::unproject({static_cast<float>(x) + 0.5f, static_cast<float>(gl_y) + 0.5f, depth},
                           inv_vp, width_, height_, out_world);
}

bool Renderer::save_ppm(GlFuncs& gl, const char* path) const {
    if (width_ <= 0 || height_ <= 0) return false;
    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 3);
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(0, 0, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());

    FILE* fp = std::fopen(path, "wb");
    if (!fp) return false;
    std::fprintf(fp, "P6\n%d %d\n255\n", width_, height_);
    const size_t row_bytes = static_cast<size_t>(width_) * 3;
    for (int row = height_ - 1; row >= 0; --row) {
        std::fwrite(pixels.data() + static_cast<size_t>(row) * row_bytes, 1, row_bytes, fp);
    }
    std::fclose(fp);
    return true;
}

uint64_t Renderer::read_center_pixel_rgb(GlFuncs& gl, int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    uint8_t rgb[3] = {0, 0, 0};
    gl.BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl.PixelStorei(GL_PACK_ALIGNMENT, 1);
    gl.ReadPixels(x, height_ - 1 - y, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, rgb);
    return (static_cast<uint64_t>(rgb[0]) << 16) |
           (static_cast<uint64_t>(rgb[1]) << 8) |
            static_cast<uint64_t>(rgb[2]);
}

}  // namespace faset
