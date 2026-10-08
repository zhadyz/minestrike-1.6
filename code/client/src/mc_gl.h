// Minimal OpenGL access for the Minecraft layer. The engine owns the GL context; we draw into it
// from the client callbacks and must leave its state exactly as we found it.
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>
#include "glext.h"

namespace mcgl
{
extern PFNGLUSEPROGRAMPROC UseProgram;
extern PFNGLGENBUFFERSPROC GenBuffers;
extern PFNGLBINDBUFFERPROC BindBuffer;
extern PFNGLBUFFERDATAPROC BufferData;
extern PFNGLBUFFERSUBDATAPROC BufferSubData;
extern PFNGLDELETEBUFFERSPROC DeleteBuffers;
extern PFNGLACTIVETEXTUREPROC ActiveTexture;
extern PFNGLCLIENTACTIVETEXTUREPROC ClientActiveTexture;
extern PFNGLBINDVERTEXARRAYPROC BindVertexArray;
extern PFNGLGENERATEMIPMAPPROC GenerateMipmap;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray;
extern PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate;
extern PFNGLCREATESHADERPROC CreateShader;
extern PFNGLSHADERSOURCEPROC ShaderSource;
extern PFNGLCOMPILESHADERPROC CompileShader;
extern PFNGLGETSHADERIVPROC GetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
extern PFNGLCREATEPROGRAMPROC CreateProgram;
extern PFNGLATTACHSHADERPROC AttachShader;
extern PFNGLLINKPROGRAMPROC LinkProgram;
extern PFNGLGETPROGRAMIVPROC GetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
extern PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation;
extern PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
extern PFNGLUNIFORM1IPROC Uniform1i;
extern PFNGLUNIFORM1FPROC Uniform1f;
extern PFNGLUNIFORM3FPROC Uniform3f;
extern PFNGLUNIFORM4FPROC Uniform4f;
extern PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
extern PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
extern PFNGLTEXIMAGE3DPROC TexImage3D;
extern PFNGLTEXSUBIMAGE3DPROC TexSubImage3D;

// Compile+link a program; returns 0 on failure (logged).
GLuint BuildProgram(const char* name, const char* vs, const char* fs, const char* const* attribs, int nattribs);

bool Init();          // resolve extension entry points (needs a current context)
bool Ready();

// Saves and restores the engine's GL state around our drawing.
struct StateGuard
{
	GLint program = 0, arrayBuffer = 0, elementBuffer = 0, vao = 0, activeTex = 0, clientActiveTex = 0;
	GLint boundTex0 = 0, matrixMode = 0;
	StateGuard();
	~StateGuard();
};
} // namespace mcgl
