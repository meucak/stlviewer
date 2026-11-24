#pragma once

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#define GL_GLEXT_PROTOTYPES 0
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#include <string>

namespace faset {

struct GlFuncs {
    void* lib = nullptr;
    int major = 0;
    int minor = 0;
    std::string renderer_str;
    std::string version_str;

    // GLX (explicit function pointer signatures avoid glxext.h typedef gaps)
    GLXFBConfig* (*xChooseFBConfig)(Display*, int, const int*, int*) = nullptr;
    XVisualInfo* (*xGetVisualFromFBConfig)(Display*, GLXFBConfig) = nullptr;
    GLXContext (*xCreateContextAttribsARB)(Display*, GLXFBConfig, GLXContext, Bool, const int*) = nullptr;
    Bool (*xMakeCurrent)(Display*, GLXDrawable, GLXContext) = nullptr;
    void (*xDestroyContext)(Display*, GLXContext) = nullptr;
    void (*xSwapBuffers)(Display*, GLXDrawable) = nullptr;
    void (*(*xGetProcAddressARB)(const GLubyte*))(void) = nullptr;
    const char* (*xQueryExtensionsString)(Display*, int) = nullptr;
    void (*xSwapIntervalEXT)(Display*, GLXDrawable, int) = nullptr;
    int (*xSwapIntervalMESA)(unsigned int) = nullptr;

    // Core GL 1.x-3.3
    void (*GetIntegerv)(GLenum, GLint*) = nullptr;
    const GLubyte* (*GetString)(GLenum) = nullptr;
    GLenum (*GetError)(void) = nullptr;
    void (*Enable)(GLenum) = nullptr;
    void (*Disable)(GLenum) = nullptr;
    void (*DepthFunc)(GLenum) = nullptr;
    void (*DepthMask)(GLboolean) = nullptr;
    void (*BlendFunc)(GLenum, GLenum) = nullptr;
    void (*BlendEquation)(GLenum) = nullptr;
    void (*CullFace)(GLenum) = nullptr;
    void (*FrontFace)(GLenum) = nullptr;
    void (*PolygonMode)(GLenum, GLenum) = nullptr;
    void (*PolygonOffset)(GLfloat, GLfloat) = nullptr;
    void (*Viewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
    void (*ClearColor)(GLfloat, GLfloat, GLfloat, GLfloat) = nullptr;
    void (*ClearDepth)(GLdouble) = nullptr;
    void (*Clear)(GLbitfield) = nullptr;
    void (*DrawArrays)(GLenum, GLint, GLsizei) = nullptr;
    void (*ReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
    void (*PixelStorei)(GLenum, GLint) = nullptr;
    void (*Flush)(void) = nullptr;
    void (*Finish)(void) = nullptr;

    void (*GenTextures)(GLsizei, GLuint*) = nullptr;
    void (*DeleteTextures)(GLsizei, const GLuint*) = nullptr;
    void (*BindTexture)(GLenum, GLuint) = nullptr;
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
    void (*TexParameteri)(GLenum, GLenum, GLint) = nullptr;
    PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;
    PFNGLGENERATEMIPMAPPROC GenerateMipmap = nullptr;

    PFNGLGENBUFFERSPROC GenBuffers = nullptr;
    PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
    PFNGLBINDBUFFERPROC BindBuffer = nullptr;
    PFNGLBUFFERDATAPROC BufferData = nullptr;
    PFNGLBUFFERSUBDATAPROC BufferSubData = nullptr;

    PFNGLGENVERTEXARRAYSPROC GenVertexArrays = nullptr;
    PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays = nullptr;
    PFNGLBINDVERTEXARRAYPROC BindVertexArray = nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
    PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray = nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;

    PFNGLCREATESHADERPROC CreateShader = nullptr;
    PFNGLDELETESHADERPROC DeleteShader = nullptr;
    PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
    PFNGLCOMPILESHADERPROC CompileShader = nullptr;
    PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
    PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;

    PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
    PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
    PFNGLATTACHSHADERPROC AttachShader = nullptr;
    PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
    PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
    PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
    PFNGLUSEPROGRAMPROC UseProgram = nullptr;
    PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
    PFNGLUNIFORM1IPROC Uniform1i = nullptr;
    PFNGLUNIFORM1FPROC Uniform1f = nullptr;
    PFNGLUNIFORM2FPROC Uniform2f = nullptr;
    PFNGLUNIFORM3FPROC Uniform3f = nullptr;
    PFNGLUNIFORM4FPROC Uniform4f = nullptr;
    PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv = nullptr;

    PFNGLGENFRAMEBUFFERSPROC GenFramebuffers = nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers = nullptr;
    PFNGLBINDFRAMEBUFFERPROC BindFramebuffer = nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D = nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus = nullptr;
    PFNGLDRAWBUFFERSPROC DrawBuffers = nullptr;

    bool load_glx(std::string& err);
    bool load_gl_core(std::string& err);
    void unload();
};

}  // namespace faset
