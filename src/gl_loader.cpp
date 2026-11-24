#include "gl_loader.hpp"

#include <cstring>
#include <dlfcn.h>

namespace faset {

namespace {

template <typename Fn, typename GpaFn>
bool resolve_sym(void* lib, GpaFn gpa, const char* name, Fn& out, bool required = true) {
    void* sym = nullptr;
    if (gpa) {
        sym = reinterpret_cast<void*>(gpa(reinterpret_cast<const GLubyte*>(name)));
    }
    if (!sym && lib) {
        sym = ::dlsym(lib, name);
    }
    out = reinterpret_cast<Fn>(sym);
    return !required || (sym != nullptr);
}

}  // namespace

bool GlFuncs::load_glx(std::string& err) {
    if (!lib) {
        lib = ::dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!lib) {
            lib = ::dlopen("libGL.so", RTLD_LAZY | RTLD_LOCAL);
        }
        if (!lib) {
            err = "Failed to load libGL.so.1 (OpenGL driver not available)";
            return false;
        }
    }

    using GpaProc = void (*(*)(const GLubyte*))(void);
    xGetProcAddressARB = reinterpret_cast<GpaProc>(::dlsym(lib, "glXGetProcAddressARB"));
    if (!xGetProcAddressARB) {
        xGetProcAddressARB = reinterpret_cast<GpaProc>(::dlsym(lib, "glXGetProcAddress"));
    }
    if (!xGetProcAddressARB) {
        err = "Missing glXGetProcAddressARB in libGL";
        return false;
    }

    bool ok = true;
    ok &= resolve_sym(lib, xGetProcAddressARB, "glXChooseFBConfig", xChooseFBConfig);
    ok &= resolve_sym(lib, xGetProcAddressARB, "glXGetVisualFromFBConfig", xGetVisualFromFBConfig);
    ok &= resolve_sym(lib, xGetProcAddressARB, "glXCreateContextAttribsARB", xCreateContextAttribsARB);
    ok &= resolve_sym(lib, xGetProcAddressARB, "glXMakeCurrent", xMakeCurrent);
    ok &= resolve_sym(lib, xGetProcAddressARB, "glXDestroyContext", xDestroyContext);
    ok &= resolve_sym(lib, xGetProcAddressARB, "glXSwapBuffers", xSwapBuffers);
    ok &= resolve_sym(lib, xGetProcAddressARB, "glXQueryExtensionsString", xQueryExtensionsString);

    resolve_sym(lib, xGetProcAddressARB, "glXSwapIntervalEXT", xSwapIntervalEXT, false);
    resolve_sym(lib, xGetProcAddressARB, "glXSwapIntervalMESA", xSwapIntervalMESA, false);

    if (!ok) {
        err = "Missing required GLX 1.3+ / ARB_create_context functions";
        return false;
    }
    return true;
}

bool GlFuncs::load_gl_core(std::string& err) {
    if (!lib || !xGetProcAddressARB) {
        err = "GLX must be loaded before GL core symbols";
        return false;
    }

    bool ok = true;
    auto R = [&](const char* name, auto& fn) {
        if (!resolve_sym(lib, xGetProcAddressARB, name, fn, true)) {
            if (ok) err = std::string("Missing GL symbol: ") + name;
            ok = false;
        }
    };

    R("glGetIntegerv", GetIntegerv);
    R("glGetString", GetString);
    R("glGetError", GetError);
    R("glEnable", Enable);
    R("glDisable", Disable);
    R("glDepthFunc", DepthFunc);
    R("glDepthMask", DepthMask);
    R("glBlendFunc", BlendFunc);
    R("glBlendEquation", BlendEquation);
    R("glCullFace", CullFace);
    R("glFrontFace", FrontFace);
    R("glPolygonMode", PolygonMode);
    R("glPolygonOffset", PolygonOffset);
    R("glViewport", Viewport);
    R("glClearColor", ClearColor);
    R("glClearDepth", ClearDepth);
    R("glClear", Clear);
    R("glDrawArrays", DrawArrays);
    R("glReadPixels", ReadPixels);
    R("glPixelStorei", PixelStorei);
    R("glFlush", Flush);
    R("glFinish", Finish);

    R("glGenTextures", GenTextures);
    R("glDeleteTextures", DeleteTextures);
    R("glBindTexture", BindTexture);
    R("glTexImage2D", TexImage2D);
    R("glTexParameteri", TexParameteri);
    R("glActiveTexture", ActiveTexture);
    R("glGenerateMipmap", GenerateMipmap);

    R("glGenBuffers", GenBuffers);
    R("glDeleteBuffers", DeleteBuffers);
    R("glBindBuffer", BindBuffer);
    R("glBufferData", BufferData);
    R("glBufferSubData", BufferSubData);

    R("glGenVertexArrays", GenVertexArrays);
    R("glDeleteVertexArrays", DeleteVertexArrays);
    R("glBindVertexArray", BindVertexArray);
    R("glEnableVertexAttribArray", EnableVertexAttribArray);
    R("glDisableVertexAttribArray", DisableVertexAttribArray);
    R("glVertexAttribPointer", VertexAttribPointer);

    R("glCreateShader", CreateShader);
    R("glDeleteShader", DeleteShader);
    R("glShaderSource", ShaderSource);
    R("glCompileShader", CompileShader);
    R("glGetShaderiv", GetShaderiv);
    R("glGetShaderInfoLog", GetShaderInfoLog);

    R("glCreateProgram", CreateProgram);
    R("glDeleteProgram", DeleteProgram);
    R("glAttachShader", AttachShader);
    R("glLinkProgram", LinkProgram);
    R("glGetProgramiv", GetProgramiv);
    R("glGetProgramInfoLog", GetProgramInfoLog);
    R("glUseProgram", UseProgram);
    R("glGetUniformLocation", GetUniformLocation);
    R("glUniform1i", Uniform1i);
    R("glUniform1f", Uniform1f);
    R("glUniform2f", Uniform2f);
    R("glUniform3f", Uniform3f);
    R("glUniform4f", Uniform4f);
    R("glUniformMatrix4fv", UniformMatrix4fv);

    R("glGenFramebuffers", GenFramebuffers);
    R("glDeleteFramebuffers", DeleteFramebuffers);
    R("glBindFramebuffer", BindFramebuffer);
    R("glFramebufferTexture2D", FramebufferTexture2D);
    R("glCheckFramebufferStatus", CheckFramebufferStatus);
    R("glDrawBuffers", DrawBuffers);

    if (!ok) return false;

    GetIntegerv(GL_MAJOR_VERSION, &major);
    GetIntegerv(GL_MINOR_VERSION, &minor);
    const GLubyte* rs = GetString(GL_RENDERER);
    const GLubyte* vs = GetString(GL_VERSION);
    renderer_str = rs ? reinterpret_cast<const char*>(rs) : "Unknown";
    version_str = vs ? reinterpret_cast<const char*>(vs) : "Unknown";

    if (major < 3 || (major == 3 && minor < 3)) {
        err = "OpenGL 3.3+ required, got: " + version_str;
        return false;
    }
    return true;
}

void GlFuncs::unload() {
    if (lib) {
        ::dlclose(lib);
        lib = nullptr;
    }
}

}  // namespace faset
