#include "mc_gl.h"
#include "mc_client.h"

namespace mcgl
{
PFNGLUSEPROGRAMPROC UseProgram;
PFNGLGENBUFFERSPROC GenBuffers;
PFNGLBINDBUFFERPROC BindBuffer;
PFNGLBUFFERDATAPROC BufferData;
PFNGLBUFFERSUBDATAPROC BufferSubData;
PFNGLDELETEBUFFERSPROC DeleteBuffers;
PFNGLACTIVETEXTUREPROC ActiveTexture;
PFNGLCLIENTACTIVETEXTUREPROC ClientActiveTexture;
PFNGLBINDVERTEXARRAYPROC BindVertexArray;
PFNGLGENERATEMIPMAPPROC GenerateMipmap;
PFNGLDISABLEVERTEXATTRIBARRAYPROC DisableVertexAttribArray;
PFNGLBLENDFUNCSEPARATEPROC BlendFuncSeparate;
PFNGLCREATESHADERPROC CreateShader;
PFNGLSHADERSOURCEPROC ShaderSource;
PFNGLCOMPILESHADERPROC CompileShader;
PFNGLGETSHADERIVPROC GetShaderiv;
PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
PFNGLCREATEPROGRAMPROC CreateProgram;
PFNGLATTACHSHADERPROC AttachShader;
PFNGLLINKPROGRAMPROC LinkProgram;
PFNGLGETPROGRAMIVPROC GetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
PFNGLBINDATTRIBLOCATIONPROC BindAttribLocation;
PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
PFNGLUNIFORM1IPROC Uniform1i;
PFNGLUNIFORM1FPROC Uniform1f;
PFNGLUNIFORM3FPROC Uniform3f;
PFNGLUNIFORM4FPROC Uniform4f;
PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
PFNGLTEXIMAGE3DPROC TexImage3D;
PFNGLTEXSUBIMAGE3DPROC TexSubImage3D;

static bool g_ready = false;

template <typename T> static bool Get(T& out, const char* name)
{
	out = (T)wglGetProcAddress(name);
	if (!out)
		mc::Log("gl: missing %s", name);
	return out != nullptr;
}

bool Init()
{
	if (g_ready)
		return true;
	if (!wglGetCurrentContext())
		return false;
	bool ok = true;
	ok &= Get(UseProgram, "glUseProgram");
	ok &= Get(GenBuffers, "glGenBuffers");
	ok &= Get(BindBuffer, "glBindBuffer");
	ok &= Get(BufferData, "glBufferData");
	ok &= Get(BufferSubData, "glBufferSubData");
	ok &= Get(DeleteBuffers, "glDeleteBuffers");
	ok &= Get(ActiveTexture, "glActiveTexture");
	ok &= Get(ClientActiveTexture, "glClientActiveTexture");
	Get(BindVertexArray, "glBindVertexArray"); // optional
	Get(GenerateMipmap, "glGenerateMipmap");
	Get(DisableVertexAttribArray, "glDisableVertexAttribArray");
	Get(BlendFuncSeparate, "glBlendFuncSeparate");
	ok &= Get(CreateShader, "glCreateShader");
	ok &= Get(ShaderSource, "glShaderSource");
	ok &= Get(CompileShader, "glCompileShader");
	ok &= Get(GetShaderiv, "glGetShaderiv");
	ok &= Get(GetShaderInfoLog, "glGetShaderInfoLog");
	ok &= Get(CreateProgram, "glCreateProgram");
	ok &= Get(AttachShader, "glAttachShader");
	ok &= Get(LinkProgram, "glLinkProgram");
	ok &= Get(GetProgramiv, "glGetProgramiv");
	ok &= Get(GetProgramInfoLog, "glGetProgramInfoLog");
	ok &= Get(BindAttribLocation, "glBindAttribLocation");
	ok &= Get(GetUniformLocation, "glGetUniformLocation");
	ok &= Get(Uniform1i, "glUniform1i");
	ok &= Get(Uniform1f, "glUniform1f");
	ok &= Get(Uniform3f, "glUniform3f");
	ok &= Get(Uniform4f, "glUniform4f");
	ok &= Get(VertexAttribPointer, "glVertexAttribPointer");
	ok &= Get(EnableVertexAttribArray, "glEnableVertexAttribArray");
	Get(GenVertexArrays, "glGenVertexArrays");
	ok &= Get(TexImage3D, "glTexImage3D");
	ok &= Get(TexSubImage3D, "glTexSubImage3D");

	const char* ver = (const char*)glGetString(GL_VERSION);
	const char* ren = (const char*)glGetString(GL_RENDERER);
	GLint profile = 0;
	glGetIntegerv(0x9126 /*GL_CONTEXT_PROFILE_MASK*/, &profile);
	while (glGetError() != GL_NO_ERROR) {}
	mc::Log("gl: version '%s' renderer '%s' profile mask 0x%x ok=%d", ver ? ver : "?", ren ? ren : "?", profile, ok);
	g_ready = ok;
	return ok;
}

bool Ready() { return g_ready; }

static GLuint Compile(const char* name, GLenum type, const char* src)
{
	GLuint sh = CreateShader(type);
	ShaderSource(sh, 1, &src, nullptr);
	CompileShader(sh);
	GLint ok = 0;
	GetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[2048];
		GetShaderInfoLog(sh, sizeof(log), nullptr, log);
		mc::Log("gl: %s %s shader failed: %s", name, type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		return 0;
	}
	return sh;
}

GLuint BuildProgram(const char* name, const char* vs, const char* fs, const char* const* attribs, int nattribs)
{
	GLuint v = Compile(name, GL_VERTEX_SHADER, vs), f = Compile(name, GL_FRAGMENT_SHADER, fs);
	if (!v || !f)
		return 0;
	GLuint p = CreateProgram();
	AttachShader(p, v);
	AttachShader(p, f);
	for (int i = 0; i < nattribs; i++)
		BindAttribLocation(p, i, attribs[i]);
	LinkProgram(p);
	GLint ok = 0;
	GetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		char log[2048];
		GetProgramInfoLog(p, sizeof(log), nullptr, log);
		mc::Log("gl: %s link failed: %s", name, log);
		return 0;
	}
	mc::Log("gl: built program %s", name);
	return p;
}

StateGuard::StateGuard()
{
	glGetIntegerv(GL_CURRENT_PROGRAM, &program);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &elementBuffer);
	if (BindVertexArray)
		glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTex);
	glGetIntegerv(GL_CLIENT_ACTIVE_TEXTURE, &clientActiveTex);
	glGetIntegerv(GL_MATRIX_MODE, &matrixMode);
	ActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTex0);
	glPushAttrib(GL_ALL_ATTRIB_BITS);
	glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
	UseProgram(0);
	if (BindVertexArray)
		BindVertexArray(0);
	BindBuffer(GL_ARRAY_BUFFER, 0);
	BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
}

StateGuard::~StateGuard()
{
	glPopClientAttrib();
	glPopAttrib();
	ActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, boundTex0);
	ActiveTexture(activeTex);
	ClientActiveTexture(clientActiveTex);
	if (BindVertexArray)
		BindVertexArray(vao);
	BindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
	BindBuffer(GL_ELEMENT_ARRAY_BUFFER, elementBuffer);
	UseProgram(program);
	glMatrixMode(matrixMode);
}
} // namespace mcgl
