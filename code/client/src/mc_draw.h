// Drawing helpers shared by HUD, hand and entity renderers (fixed-function GL, compat profile).
#pragma once
#include "mc_gl.h"

namespace mcdraw
{
// 2D (pixel coordinates, origin top-left) - caller sets up the ortho projection via Begin2D.
void Begin2D(int w, int h);
void End2D();
void Rect(float x, float y, float w, float h, unsigned rgba);
// Draw a sub-rectangle (u0,v0)-(u1,v1) in texture pixels of a texture at x,y size w,h.
void Blit(const char* tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, unsigned rgba = 0xFFFFFFFF);
void BlitFull(const char* tex, float x, float y, float w, float h, unsigned rgba = 0xFFFFFFFF);

// Minecraft font (textures/font/ascii.png). Returns advance width in font pixels (before scale).
float TextWidth(const char* s);
// Draws with Minecraft's drop shadow. Supports Minecraft colour codes as \xA7X (§) and "\\y" style
// shortcuts used by our server messages (\\r red, \\y yellow, \\g green, \\aq aqua, \\w white).
void Text(const char* s, float x, float y, float scale, unsigned rgba, bool shadow = true);
void TextCentered(const char* s, float cx, float y, float scale, unsigned rgba, bool shadow = true);

// Item rendering
void ItemIcon(int itemId, float x, float y, float size); // GUI icon (flat sprite or isometric block)
void BlockIcon(int blockType, float x, float y, float size);
// 3D: draws the item centred at the current modelview origin, 1 unit = 1 Minecraft block-pixel*16
// (i.e. the item spans [-0.5,0.5]); extruded sprite for items, cube for blocks.
void Item3D(int itemId, float brightness = 1.0f, const char* texOverride = nullptr); // texOverride: e.g. "item/bow_pulling_1"
void Cube3D(int blockType, float brightness = 1.0f);
// Extruded sprite straight from a texture path (e.g. "item/crossbow_standby").
void Texture3D(const char* tex);

// Glint (enchanted items): additive scrolling overlay over the last item drawn.
unsigned RarityColor(int rarity);
} // namespace mcdraw
