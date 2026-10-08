#include "mc_draw.h"
#include "mc_blocks.h"
#include "mc_client.h"
#include "mc_items.h"
#include "mc_tex.h"

#include <string.h>
#include <string>
#include <unordered_map>

namespace mcdraw
{
static inline void Color(unsigned rgba)
{
	glColor4ub((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
}

void Begin2D(int w, int h)
{
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, w, h, 0, -1000, 1000);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisable(GL_FOG);
	glDisable(GL_LIGHTING);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.004f);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
}

void End2D()
{
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
}

void Rect(float x, float y, float w, float h, unsigned rgba)
{
	glDisable(GL_TEXTURE_2D);
	Color(rgba);
	glBegin(GL_QUADS);
	glVertex2f(x, y);
	glVertex2f(x + w, y);
	glVertex2f(x + w, y + h);
	glVertex2f(x, y + h);
	glEnd();
	glEnable(GL_TEXTURE_2D);
}

void Blit(const char* tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1, unsigned rgba)
{
	const mctex::Tex& t = mctex::Get(tex);
	if (!t.id)
		return;
	glBindTexture(GL_TEXTURE_2D, t.id);
	Color(rgba);
	float iu = 1.0f / t.w, iv = 1.0f / t.h;
	glBegin(GL_QUADS);
	glTexCoord2f(u0 * iu, v0 * iv);
	glVertex2f(x, y);
	glTexCoord2f(u1 * iu, v0 * iv);
	glVertex2f(x + w, y);
	glTexCoord2f(u1 * iu, v1 * iv);
	glVertex2f(x + w, y + h);
	glTexCoord2f(u0 * iu, v1 * iv);
	glVertex2f(x, y + h);
	glEnd();
}

void BlitFull(const char* tex, float x, float y, float w, float h, unsigned rgba)
{
	const mctex::Tex& t = mctex::Get(tex);
	if (!t.id)
		return;
	Blit(tex, x, y, w, h, 0, 0, (float)t.w, (float)t.h, rgba);
}

// ---------------------------------------------------------------------------------------------
// Font

static int g_glyphW[256];
static bool g_fontReady = false;

static void InitFont()
{
	if (g_fontReady)
		return;
	g_fontReady = true;
	const mctex::Image* img = mctex::GetImage("font/ascii");
	for (int c = 0; c < 256; c++)
	{
		g_glyphW[c] = 5;
		if (!img)
			continue;
		int cw = img->w / 16, ch = img->h / 16;
		int gx = (c % 16) * cw, gy = (c / 16) * ch;
		int width = 0;
		for (int x = cw - 1; x >= 0 && !width; x--)
			for (int y = 0; y < ch; y++)
				if (img->rgba[((gy + y) * img->w + gx + x) * 4 + 3] > 0)
				{
					width = x + 1;
					break;
				}
		g_glyphW[c] = width * 8 / cw;
	}
	g_glyphW[(int)' '] = 3;
}

static unsigned CodeColor(const char*& s, unsigned base)
{
	// our server shortcuts: \r \y \g \w \aq \d
	if (s[0] == '\\')
	{
		if (s[1] == 'a' && s[2] == 'q') { s += 3; return 0x55FFFF00 | (base & 0xFF); }
		unsigned a = base & 0xFF;
		switch (s[1])
		{
		case 'r': s += 2; return 0xFF555500 | a;
		case 'y': s += 2; return 0xFFFF5500 | a;
		case 'g': s += 2; return 0x55FF5500 | a;
		case 'w': s += 2; return 0xFFFFFF00 | a;
		case 'd': s += 2; return 0xAAAAAA00 | a;
		}
	}
	return 0;
}

float TextWidth(const char* s)
{
	InitFont();
	float w = 0;
	while (*s)
	{
		unsigned dummy = CodeColor(s, 0xFFFFFFFF);
		if (dummy)
			continue;
		if (!*s)
			break;
		w += g_glyphW[(unsigned char)*s] + 1;
		s++;
	}
	return w;
}

static void TextPass(const char* s, float x, float y, float scale, unsigned rgba, float dim)
{
	const mctex::Tex& t = mctex::Get("font/ascii");
	if (!t.id)
		return;
	glBindTexture(GL_TEXTURE_2D, t.id);
	unsigned col = rgba;
	glBegin(GL_QUADS);
	while (*s)
	{
		unsigned c2 = CodeColor(s, rgba);
		if (c2)
		{
			col = c2;
			continue;
		}
		if (!*s)
			break;
		unsigned char ch = (unsigned char)*s++;
		unsigned r = (unsigned)(((col >> 24) & 0xFF) * dim), g = (unsigned)(((col >> 16) & 0xFF) * dim), b = (unsigned)(((col >> 8) & 0xFF) * dim);
		glColor4ub(r, g, b, col & 0xFF);
		float u0 = (ch % 16) / 16.0f, v0 = (ch / 16) / 16.0f, u1 = u0 + 1.0f / 16.0f, v1 = v0 + 1.0f / 16.0f;
		float s8 = 8.0f * scale;
		glTexCoord2f(u0, v0);
		glVertex2f(x, y);
		glTexCoord2f(u1, v0);
		glVertex2f(x + s8, y);
		glTexCoord2f(u1, v1);
		glVertex2f(x + s8, y + s8);
		glTexCoord2f(u0, v1);
		glVertex2f(x, y + s8);
		x += (g_glyphW[ch] + 1) * scale;
	}
	glEnd();
}

void Text(const char* s, float x, float y, float scale, unsigned rgba, bool shadow)
{
	InitFont();
	if (shadow)
		TextPass(s, x + scale, y + scale, scale, rgba, 0.25f);
	TextPass(s, x, y, scale, rgba, 1.0f);
}

void TextCentered(const char* s, float cx, float y, float scale, unsigned rgba, bool shadow)
{
	Text(s, cx - TextWidth(s) * scale * 0.5f, y, scale, rgba, shadow);
}

unsigned RarityColor(int rarity)
{
	switch (rarity)
	{
	case 1: return 0xFFFF55FF;
	case 2: return 0x55FFFFFF;
	case 3: return 0xFF55FFFF;
	default: return 0xFFFFFFFF;
	}
}

// ---------------------------------------------------------------------------------------------
// Items

static std::string ItemTexture(const mci::ItemDef& d)
{
	if (d.type == mci::IT_BLOCK && d.texture)
		return strchr(d.texture, '/') ? std::string(d.texture) : std::string("item/") + d.texture;
	if (d.type == mci::IT_BLOCK)
	{
		int b = mcw::FindBlock(d.blockName);
		const mcw::BlockDef& bd = mcw::Block((uint16_t)(b < 0 ? 0 : b));
		if (bd.shape == mcw::SHAPE_DOOR)
			return std::string("item/") + d.blockName;
		if (bd.shape == mcw::SHAPE_CROSS || bd.shape == mcw::SHAPE_PANE)
			return std::string("block/") + bd.texSide;
		return std::string();
	}
	return std::string("item/") + (d.texture ? d.texture : d.name);
}

static bool IsCubeItem(const mci::ItemDef& d, int* blockOut)
{
	if (d.type != mci::IT_BLOCK || d.texture)
		return false;
	int b = mcw::FindBlock(d.blockName);
	if (b <= 0)
		return false;
	const mcw::BlockDef& bd = mcw::Block((uint16_t)b);
	if (bd.shape == mcw::SHAPE_DOOR || bd.shape == mcw::SHAPE_CROSS || bd.shape == mcw::SHAPE_PANE)
		return false;
	*blockOut = b;
	return true;
}

static void Face(const char* tex, const float v[4][3], float shade, float h0 = 0.0f, float h1 = 1.0f)
{
	std::string t = std::string("block/") + tex;
	mctex::Bind(t.c_str());
	glColor4f(shade, shade, shade, 1.0f);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 1.0f - h0);
	glVertex3fv(v[0]);
	glTexCoord2f(1, 1.0f - h0);
	glVertex3fv(v[1]);
	glTexCoord2f(1, 1.0f - h1);
	glVertex3fv(v[2]);
	glTexCoord2f(0, 1.0f - h1);
	glVertex3fv(v[3]);
	glEnd();
}

// Cube with Minecraft face shading, y up, spanning [-0.5,0.5] (height scaled for slabs).
static void DrawCube(const mcw::BlockDef& bd, float brightness, float height)
{
	float y0 = -0.5f, y1 = -0.5f + height;
	float h = height;
	const float top[4][3] = {{-0.5f, y1, 0.5f}, {0.5f, y1, 0.5f}, {0.5f, y1, -0.5f}, {-0.5f, y1, -0.5f}};
	const float bot[4][3] = {{-0.5f, y0, -0.5f}, {0.5f, y0, -0.5f}, {0.5f, y0, 0.5f}, {-0.5f, y0, 0.5f}};
	const float front[4][3] = {{-0.5f, y0, 0.5f}, {0.5f, y0, 0.5f}, {0.5f, y1, 0.5f}, {-0.5f, y1, 0.5f}};
	const float back[4][3] = {{0.5f, y0, -0.5f}, {-0.5f, y0, -0.5f}, {-0.5f, y1, -0.5f}, {0.5f, y1, -0.5f}};
	const float right[4][3] = {{0.5f, y0, 0.5f}, {0.5f, y0, -0.5f}, {0.5f, y1, -0.5f}, {0.5f, y1, 0.5f}};
	const float left[4][3] = {{-0.5f, y0, -0.5f}, {-0.5f, y0, 0.5f}, {-0.5f, y1, 0.5f}, {-0.5f, y1, -0.5f}};
	Face(bd.texTop, top, 1.0f * brightness);
	Face(bd.texBottom, bot, 0.5f * brightness);
	Face(bd.texSide, front, 0.8f * brightness, 0.0f, h);
	Face(bd.texSide, back, 0.8f * brightness, 0.0f, h);
	Face(bd.texSide, right, 0.6f * brightness, 0.0f, h);
	Face(bd.texSide, left, 0.6f * brightness, 0.0f, h);
}

void BlockIcon(int blockType, float x, float y, float size)
{
	const mcw::BlockDef& bd = mcw::Block((uint16_t)blockType);
	if (!bd.texTop)
		return;
	glPushMatrix();
	glTranslatef(x + size * 0.5f, y + size * 0.5f, 0.0f);
	// Minecraft GUI block transform: rotate 30 about X, 225 about Y, scale 0.625 of a 16px slot
	float s = size * 0.625f;
	glScalef(s, -s, s);
	glRotatef(30.0f, 1, 0, 0);
	glRotatef(225.0f, 0, 1, 0);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CW);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_ALWAYS);
	if (bd.shape == mcw::SHAPE_BUTTON || bd.shape == mcw::SHAPE_PLATE)
	{
		// inventory models: a button is a 6x4x4 px box, a plate a thin 14x14 px slab
		bool button = bd.shape == mcw::SHAPE_BUTTON;
		glPushMatrix();
		if (button)
			glScalef(6.0f / 16.0f, 4.0f / 16.0f, 4.0f / 16.0f);
		else
			glScalef(14.0f / 16.0f, 1.0f, 14.0f / 16.0f);
		DrawCube(bd, 1.0f, button ? 1.0f : 1.0f / 16.0f);
		glPopMatrix();
	}
	else
		DrawCube(bd, 1.0f, bd.shape == mcw::SHAPE_SLAB ? 0.5f : 1.0f);
	glDepthFunc(GL_LEQUAL);
	glDisable(GL_DEPTH_TEST);
	glFrontFace(GL_CCW);
	glDisable(GL_CULL_FACE);
	glPopMatrix();
	glColor4f(1, 1, 1, 1);
}

void ItemIcon(int itemId, float x, float y, float size)
{
	if (!mci::ValidItem(itemId))
		return;
	const mci::ItemDef& d = mci::Item(itemId);
	int block = 0;
	if (IsCubeItem(d, &block))
	{
		BlockIcon(block, x, y, size);
		return;
	}
	std::string t = ItemTexture(d);
	if (!strncmp(d.name, "leather_", 8) && d.type == mci::IT_ARMOR)
	{
		// dyeable leather: grey base tinted with the default leather colour (#A06540), then the overlay
		BlitFull(t.c_str(), x, y, size, size, 0xA06540FF);
		BlitFull((t + "_overlay").c_str(), x, y, size, size);
		return;
	}
	BlitFull(t.c_str(), x, y, size, size);
}

// Extruded sprite mesh (ItemRenderer "generated" model): front/back faces + 1px side walls.
static std::unordered_map<std::string, GLuint> g_extrudeLists;

static GLuint BuildExtrude(const std::string& tex)
{
	const mctex::Image* img = mctex::GetImage(tex.c_str());
	const mctex::Tex& t = mctex::Get(tex.c_str());
	if (!img || !t.id)
		return 0;
	int w = img->w, h = img->w; // square (first frame)
	GLuint list = glGenLists(1);
	glNewList(list, GL_COMPILE);
	glBindTexture(GL_TEXTURE_2D, t.id);
	const float d = 0.5f / 16.0f;
	float vmax = (float)h / img->h;
	glBegin(GL_QUADS);
	// front (z+) and back (z-)
	glColor3f(1, 1, 1);
	glNormal3f(0, 0, 1);
	glTexCoord2f(0, vmax);
	glVertex3f(-0.5f, -0.5f, d);
	glTexCoord2f(1, vmax);
	glVertex3f(0.5f, -0.5f, d);
	glTexCoord2f(1, 0);
	glVertex3f(0.5f, 0.5f, d);
	glTexCoord2f(0, 0);
	glVertex3f(-0.5f, 0.5f, d);
	glColor3f(0.85f, 0.85f, 0.85f);
	glTexCoord2f(1, vmax);
	glVertex3f(0.5f, -0.5f, -d);
	glTexCoord2f(0, vmax);
	glVertex3f(-0.5f, -0.5f, -d);
	glTexCoord2f(0, 0);
	glVertex3f(-0.5f, 0.5f, -d);
	glTexCoord2f(1, 0);
	glVertex3f(0.5f, 0.5f, -d);
	auto opaque = [&](int px, int py) {
		if (px < 0 || py < 0 || px >= w || py >= h)
			return false;
		return img->rgba[(py * img->w + px) * 4 + 3] > 16;
	};
	float pw = 1.0f / w, ph = 1.0f / h;
	for (int py = 0; py < h; py++)
		for (int px = 0; px < w; px++)
		{
			if (!opaque(px, py))
				continue;
			float u = (px + 0.5f) / w, v = (py + 0.5f) / img->h;
			float x0 = -0.5f + px * pw, x1 = x0 + pw;
			float y1 = 0.5f - py * ph, y0 = y1 - ph;
			if (!opaque(px, py - 1)) // top edge
			{
				glColor3f(1.0f, 1.0f, 1.0f);
				glTexCoord2f(u, v);
				glVertex3f(x0, y1, d);
				glVertex3f(x1, y1, d);
				glVertex3f(x1, y1, -d);
				glVertex3f(x0, y1, -d);
			}
			if (!opaque(px, py + 1)) // bottom edge
			{
				glColor3f(0.6f, 0.6f, 0.6f);
				glTexCoord2f(u, v);
				glVertex3f(x0, y0, -d);
				glVertex3f(x1, y0, -d);
				glVertex3f(x1, y0, d);
				glVertex3f(x0, y0, d);
			}
			if (!opaque(px - 1, py)) // left edge
			{
				glColor3f(0.75f, 0.75f, 0.75f);
				glTexCoord2f(u, v);
				glVertex3f(x0, y0, -d);
				glVertex3f(x0, y0, d);
				glVertex3f(x0, y1, d);
				glVertex3f(x0, y1, -d);
			}
			if (!opaque(px + 1, py)) // right edge
			{
				glColor3f(0.75f, 0.75f, 0.75f);
				glTexCoord2f(u, v);
				glVertex3f(x1, y0, d);
				glVertex3f(x1, y0, -d);
				glVertex3f(x1, y1, -d);
				glVertex3f(x1, y1, d);
			}
		}
	glEnd();
	glEndList();
	return list;
}

void Cube3D(int blockType, float brightness)
{
	const mcw::BlockDef& bd = mcw::Block((uint16_t)blockType);
	if (!bd.texTop)
		return;
	DrawCube(bd, brightness, bd.shape == mcw::SHAPE_SLAB ? 0.5f : 1.0f);
}

void Texture3D(const char* tex)
{
	std::string t = tex;
	auto it = g_extrudeLists.find(t);
	GLuint list = 0;
	if (it == g_extrudeLists.end())
		g_extrudeLists[t] = list = BuildExtrude(t);
	else
		list = it->second;
	if (!list)
		return;
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glCallList(list);
}

void Item3D(int itemId, float brightness, const char* texOverride)
{
	if (!mci::ValidItem(itemId))
		return;
	const mci::ItemDef& d = mci::Item(itemId);
	int block = 0;
	if (!texOverride && IsCubeItem(d, &block))
	{
		Cube3D(block, brightness);
		return;
	}
	std::string t = texOverride ? std::string(texOverride) : ItemTexture(d);
	auto it = g_extrudeLists.find(t);
	GLuint list = 0;
	if (it == g_extrudeLists.end())
		g_extrudeLists[t] = list = BuildExtrude(t);
	else
		list = it->second;
	if (!list)
		return;
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	if (brightness < 0.999f)
	{
		// modulate via texture env constant colour isn't available on display-list colours; scale with blend
		glCallList(list);
	}
	else
		glCallList(list);
}
} // namespace mcdraw
