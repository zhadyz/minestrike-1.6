#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO_FAIL_REASON
#include "stb_image.h"

#include "mc_tex.h"
#include "mc_client.h"

#include <string>
#include <unordered_map>

namespace mctex
{
void Image::Free()
{
	if (rgba)
		stbi_image_free(rgba);
	rgba = nullptr;
	w = h = 0;
}

const char* TexRoot()
{
	static char root[MAX_PATH] = {};
	if (!root[0])
	{
		HMODULE h = nullptr;
		GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&TexRoot, &h);
		GetModuleFileNameA(h, root, sizeof(root));
		char* s = strrchr(root, '\\'); // ...\cstrike\cl_dlls\client.dll
		if (s) *s = 0;
		s = strrchr(root, '\\');
		if (s) *s = 0;
		strcat(root, "\\mc\\textures\\");
	}
	return root;
}

bool LoadImage(const char* rel, Image& out)
{
	std::string path = std::string(TexRoot()) + rel + ".png";
	for (char& c : path)
		if (c == '/')
			c = '\\';
	int n = 0;
	out.rgba = stbi_load(path.c_str(), &out.w, &out.h, &n, 4);
	return out.rgba != nullptr;
}

struct Entry
{
	Tex tex;
	Image img;
	bool tried = false;
};
static std::unordered_map<std::string, Entry> g_cache;

static Entry& Load(const char* rel)
{
	Entry& e = g_cache[rel];
	if (e.tried)
		return e;
	const char* ext = strstr(rel, "#bsp:");
	if (ext && ext != rel)
	{
		auto it = g_cache.find(ext);
		if (it != g_cache.end() && it->second.tried)
		{
			e = it->second; // shares the GL texture and pixels
			return e;
		}
		return e; // not registered yet: try again later
	}
	e.tried = true;
	if (!LoadImage(rel, e.img))
	{
		mc::Log("tex: missing %s", rel);
		return e;
	}
	// animated textures (e.g. fire, sea lantern) are vertical strips: use the first frame
	int h = e.img.h;
	if (h > e.img.w && h % e.img.w == 0)
		h = e.img.w;
	glGenTextures(1, &e.tex.id);
	glBindTexture(GL_TEXTURE_2D, e.tex.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, e.img.w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, e.img.rgba);
	e.tex.w = e.img.w;
	e.tex.h = h;
	return e;
}

const Tex& Get(const char* rel) { return Load(rel).tex; }

const Image* GetImage(const char* rel)
{
	Entry& e = Load(rel);
	return e.img.rgba ? &e.img : nullptr;
}

void RegisterExternal(const char* key, GLuint id, int w, int h, const uint8_t* rgba)
{
	Entry& e = g_cache[key];
	e.tried = true;
	e.tex.id = id;
	e.tex.w = w;
	e.tex.h = h;
	e.img.w = w;
	e.img.h = h;
	e.img.rgba = new uint8_t[(size_t)w * h * 4];
	memcpy(e.img.rgba, rgba, (size_t)w * h * 4);
	// forget stale lookups made before registration (e.g. after a map change)
	for (auto& kv : g_cache)
		if (kv.first != key && strstr(kv.first.c_str(), key) && kv.first.size() == strlen(key) + (kv.first.find(key)))
			kv.second = e;
}

void Bind(const char* rel)
{
	const Tex& t = Get(rel);
	glBindTexture(GL_TEXTURE_2D, t.id);
}
} // namespace mctex
