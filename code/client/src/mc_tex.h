// Texture loading for the Minecraft layer: PNGs straight from cstrike/mc/textures (the real Minecraft
// asset tree), nearest filtering like Minecraft.
#pragma once
#include "mc_gl.h"
#include <stdint.h>

namespace mctex
{
struct Image
{
	int w = 0, h = 0;
	uint8_t* rgba = nullptr; // w*h*4, owned
	void Free();
};

// Path relative to cstrike/mc/textures without extension, e.g. "item/diamond_sword".
bool LoadImage(const char* rel, Image& out);
const char* TexRoot();

struct Tex
{
	GLuint id = 0;
	int w = 0, h = 0;
};
// Cached 2D texture; returns id 0 if missing (logged once).
const Tex& Get(const char* rel);
// Pixel access to a cached image (for item extrusion meshes, font widths, ...). nullptr if missing.
const Image* GetImage(const char* rel);

void Bind(const char* rel);
// A texture made elsewhere (e.g. a classic map texture, key "#bsp:<n>"); any lookup containing the key
// ("block/#bsp:3", "#bsp:3") resolves to it. rgba (w*h*4) is copied for GetImage.
void RegisterExternal(const char* key, GLuint id, int w, int h, const uint8_t* rgba);
} // namespace mctex
