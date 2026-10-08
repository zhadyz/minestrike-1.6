// Classic mode on the client: the mod draws the classic map (de_dust2) itself, straight from its BSP:
// WAD textures in a texture array, the baked lightmaps in an atlas, faces grouped in 8-cell chunks. Where
// players have dug, faces are cut cell by cell, the cut walls show cross-sections of the map's solid
// space (textured like the surface they belong to), and placed classic-texture blocks are drawn here too.
#include "mc_classic.h"
#include "mc_blocks.h"
#include "mc_client.h"
#include "mc_gl.h"
#include "mc_move.h"
#include "mc_state.h"
#include "mc_tex.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc
{
static mcc::Classic g_classic;
static bool g_on = false;
static const mcw::World* g_grid = nullptr;

// ---------------------------------------------------------------------------------------------
// Textures

struct MipGL
{
	int w = 64, h = 64;
	bool alpha = false;
	bool draw = true;
	int layer = -1;
};
static std::vector<MipGL> g_mips;
static int g_oreLayer[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
static GLuint g_texArr = 0, g_lmTex = 0, g_prog = 0, g_vao = 0;
static GLint u_tex = -1, u_lm = -1, u_scale = -1, u_gamma = -1, u_texGamma = -1;
static const int TEXDIM = 256;
static const int ATLAS = 2048;

static bool ReadAll(const std::string& path, std::vector<uint8_t>& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(n > 0 ? (size_t)n : 0);
	bool ok = n > 0 && fread(out.data(), 1, (size_t)n, f) == (size_t)n;
	fclose(f);
	return ok;
}

struct Wad
{
	std::vector<uint8_t> data;
	std::unordered_map<std::string, std::pair<int, int>> lumps; // lower name -> (offset, size)
};

static std::string Lower(std::string s)
{
	for (char& c : s)
		c = (char)tolower((unsigned char)c);
	return s;
}

static bool LoadWad(const std::string& path, Wad& w)
{
	if (!ReadAll(path, w.data) || w.data.size() < 12 || memcmp(w.data.data(), "WAD3", 4))
		return false;
	int n, ofs;
	memcpy(&n, &w.data[4], 4);
	memcpy(&ofs, &w.data[8], 4);
	for (int i = 0; i < n; i++)
	{
		size_t e = (size_t)ofs + (size_t)i * 32;
		if (e + 32 > w.data.size())
			break;
		int pos, disk;
		memcpy(&pos, &w.data[e], 4);
		memcpy(&disk, &w.data[e + 4], 4);
		char name[17] = {};
		memcpy(name, &w.data[e + 16], 16);
		w.lumps[Lower(name)] = {pos, disk};
	}
	return true;
}

// 8-bit miptex -> RGBA (mip 0). Palette index 255 of '{' textures is transparent.
static bool DecodeMiptex(const uint8_t* m, size_t avail, bool alphaTex, int& w, int& h, std::vector<uint8_t>& rgba)
{
	if (avail < 40)
		return false;
	uint32_t off[4];
	memcpy(&w, m + 16, 4);
	memcpy(&h, m + 20, 4);
	memcpy(off, m + 24, 16);
	if (w <= 0 || h <= 0 || w > 1024 || h > 1024 || off[0] == 0)
		return false;
	size_t palOff = off[3] + (size_t)(w / 8) * (h / 8) + 2;
	if (off[0] + (size_t)w * h > avail || palOff + 768 > avail)
		return false;
	const uint8_t* pix = m + off[0];
	const uint8_t* pal = m + palOff;
	rgba.resize((size_t)w * h * 4);
	for (int i = 0; i < w * h; i++)
	{
		int p = pix[i];
		rgba[i * 4] = pal[p * 3];
		rgba[i * 4 + 1] = pal[p * 3 + 1];
		rgba[i * 4 + 2] = pal[p * 3 + 2];
		rgba[i * 4 + 3] = (alphaTex && p == 255) ? 0 : 255;
	}
	return true;
}

// Resample to TEXDIM x TEXDIM with wrap-around bilinear filtering (nearest for alpha-tested textures).
static void Resample(const std::vector<uint8_t>& src, int w, int h, bool nearest, std::vector<uint8_t>& dst)
{
	dst.resize((size_t)TEXDIM * TEXDIM * 4);
	for (int y = 0; y < TEXDIM; y++)
		for (int x = 0; x < TEXDIM; x++)
		{
			float u = (x + 0.5f) * w / TEXDIM - 0.5f, v = (y + 0.5f) * h / TEXDIM - 0.5f;
			uint8_t* d = &dst[((size_t)y * TEXDIM + x) * 4];
			if (nearest)
			{
				int sx = ((int)floorf(u + 0.5f) % w + w) % w, sy = ((int)floorf(v + 0.5f) % h + h) % h;
				memcpy(d, &src[((size_t)sy * w + sx) * 4], 4);
				continue;
			}
			int x0 = (int)floorf(u), y0 = (int)floorf(v);
			float fx = u - x0, fy = v - y0;
			for (int c = 0; c < 4; c++)
			{
				auto at = [&](int xx, int yy) {
					xx = (xx % w + w) % w;
					yy = (yy % h + h) % h;
					return (float)src[((size_t)yy * w + xx) * 4 + c];
				};
				float a = at(x0, y0) * (1 - fx) + at(x0 + 1, y0) * fx;
				float b = at(x0, y0 + 1) * (1 - fx) + at(x0 + 1, y0 + 1) * fx;
				d[c] = (uint8_t)(a * (1 - fy) + b * fy + 0.5f);
			}
		}
}

static void LoadTextures(const std::string& cstrike)
{
	const mcb::Map& m = g_classic.map;
	g_mips.assign(m.miptex.size(), MipGL());
	std::vector<Wad> wads;
	{
		// the map's "wad" key lists them; look in cstrike/ then valve/
		const char* wk = nullptr;
		std::vector<mcb::EntityKV> ents = mcb::ParseEntities(m.entities);
		if (!ents.empty())
			wk = ents[0].Get("wad");
		std::string list = wk ? wk : "halflife.wad;cs_dust.wad;decals.wad";
		size_t p = 0;
		while (p < list.size())
		{
			size_t q = list.find(';', p);
			std::string item = list.substr(p, q == std::string::npos ? std::string::npos : q - p);
			p = q == std::string::npos ? list.size() : q + 1;
			size_t slash = item.find_last_of("\\/");
			std::string base = slash == std::string::npos ? item : item.substr(slash + 1);
			if (base.empty())
				continue;
			Wad w;
			if (LoadWad(cstrike + base, w) || LoadWad(cstrike + "..\\valve\\" + base, w))
				wads.push_back(std::move(w));
		}
	}
	int layers = (int)m.miptex.size() + 6;
	glGenTextures(1, &g_texArr);
	glBindTexture(GL_TEXTURE_2D_ARRAY, g_texArr);
	mcgl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, TEXDIM, TEXDIM, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	std::vector<uint8_t> rgba, big;
	for (size_t i = 0; i < m.miptex.size(); i++)
	{
		MipGL& g = g_mips[i];
		const std::string& name = m.miptex[i].name;
		g.w = m.miptex[i].width > 0 ? m.miptex[i].width : 64;
		g.h = m.miptex[i].height > 0 ? m.miptex[i].height : 64;
		g.alpha = !name.empty() && name[0] == '{';
		std::string ln = Lower(name);
		g.draw = !(ln == "sky" || ln == "aaatrigger" || ln == "clip" || ln == "origin" || ln == "null" || ln == "hint" || ln == "skip");
		bool ok = false;
		int w = 0, h = 0;
		if (m.miptex[i].lumpOffset >= 0)
			ok = DecodeMiptex(&m.textureLump[m.miptex[i].lumpOffset], m.textureLump.size() - m.miptex[i].lumpOffset, g.alpha, w, h, rgba);
		for (size_t k = 0; !ok && k < wads.size(); k++)
		{
			auto it = wads[k].lumps.find(ln);
			if (it != wads[k].lumps.end() && it->second.first >= 0 && (size_t)it->second.first < wads[k].data.size())
				ok = DecodeMiptex(&wads[k].data[it->second.first], wads[k].data.size() - it->second.first, g.alpha, w, h, rgba);
		}
		if (!ok)
		{
			Log("classic: texture %s missing", name.c_str());
			w = h = 8;
			rgba.assign(8 * 8 * 4, 0);
			for (int p = 0; p < 64; p++)
			{
				bool on = ((p & 7) + (p >> 3)) & 1;
				rgba[p * 4] = on ? 255 : 0;
				rgba[p * 4 + 2] = on ? 255 : 0;
				rgba[p * 4 + 3] = 255;
			}
		}
		Resample(rgba, w, h, g.alpha, big);
		g.layer = (int)i;
		mcgl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, g.layer, TEXDIM, TEXDIM, 1, GL_RGBA, GL_UNSIGNED_BYTE, big.data());
		// also as a plain 2D texture for item icons and particles ("#bsp:<index>")
		GLuint t2 = 0;
		glGenTextures(1, &t2);
		glBindTexture(GL_TEXTURE_2D, t2);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
		char key[32];
		snprintf(key, sizeof(key), "#bsp:%d", (int)i);
		mctex::RegisterExternal(key, t2, w, h, rgba.data());
		glBindTexture(GL_TEXTURE_2D_ARRAY, g_texArr);
	}
	// Minecraft ores (buried below the map)
	for (int ore = 1; ore <= 6; ore++)
	{
		char rel[64];
		snprintf(rel, sizeof(rel), "block/%s", mcc::Classic::OreName(ore));
		mctex::Image img;
		if (!mctex::LoadImage(rel, img))
			continue;
		std::vector<uint8_t> src(img.rgba, img.rgba + (size_t)img.w * img.w * 4);
		Resample(src, img.w, img.w, true, big);
		img.Free();
		g_oreLayer[ore] = (int)m.miptex.size() + ore - 1;
		mcgl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, g_oreLayer[ore], TEXDIM, TEXDIM, 1, GL_RGBA, GL_UNSIGNED_BYTE, big.data());
	}
	if (mcgl::GenerateMipmap)
		mcgl::GenerateMipmap(GL_TEXTURE_2D_ARRAY);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

// ---------------------------------------------------------------------------------------------
// Lightmaps

struct FaceRI
{
	bool draw = false;
	int layer = 0;
	float texScale[2] = {1, 1};  // 1 / texture size
	bool hasLM = false;
	int ax = 0, ay = 0;          // atlas position of luxel (0,0)
	float tmin[2] = {0, 0};
	float avg = 0.5f;            // mean light (cross-sections borrow it)
	float normal[3] = {0, 0, 1};
	float area = 0;
};
static std::vector<FaceRI> g_faces;

static void BuildLightmaps()
{
	const mcb::Map& m = g_classic.map;
	g_faces.assign(m.faces.size(), FaceRI());
	std::vector<uint8_t> atlas((size_t)ATLAS * ATLAS * 3, 0);
	int sx = 0, sy = 0, rowH = 0;
	for (size_t f = 0; f < m.faces.size(); f++)
	{
		FaceRI& ri = g_faces[f];
		const mcb::Face& fc = m.faces[f];
		if (fc.texinfo < 0 || fc.texinfo >= (int)m.texinfo.size())
			continue;
		const mcb::TexInfo& ti = m.texinfo[fc.texinfo];
		if (ti.miptex < 0 || ti.miptex >= (int)g_mips.size() || !g_mips[ti.miptex].draw)
			continue;
		ri.draw = true;
		ri.layer = g_mips[ti.miptex].layer;
		ri.texScale[0] = 1.0f / g_mips[ti.miptex].w;
		ri.texScale[1] = 1.0f / g_mips[ti.miptex].h;
		m.FaceNormal((int)f, ri.normal);
		float mins[2] = {1e9f, 1e9f}, maxs[2] = {-1e9f, -1e9f};
		int nv = m.FaceVertexCount((int)f);
		std::vector<float> poly((size_t)nv * 3);
		for (int i = 0; i < nv; i++)
		{
			float v[3];
			m.FaceVertex((int)f, i, v);
			memcpy(&poly[i * 3], v, 12);
			for (int a = 0; a < 2; a++)
			{
				float s = v[0] * ti.vecs[a][0] + v[1] * ti.vecs[a][1] + v[2] * ti.vecs[a][2] + ti.vecs[a][3];
				mins[a] = fminf(mins[a], s);
				maxs[a] = fmaxf(maxs[a], s);
			}
		}
		for (int i = 1; i + 1 < nv; i++)
		{
			float* a = &poly[0];
			float* b = &poly[i * 3];
			float* c = &poly[(i + 1) * 3];
			float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, w[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
			float cr[3] = {u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0]};
			ri.area += 0.5f * sqrtf(cr[0] * cr[0] + cr[1] * cr[1] + cr[2] * cr[2]);
		}
		if (fc.lightofs < 0 || fc.styles[0] == 255)
		{
			ri.avg = 1.0f;
			continue;
		}
		int bmin[2], bmax[2];
		for (int a = 0; a < 2; a++)
		{
			bmin[a] = (int)floorf(mins[a] / 16.0f);
			bmax[a] = (int)ceilf(maxs[a] / 16.0f);
			ri.tmin[a] = bmin[a] * 16.0f;
		}
		int lw = bmax[0] - bmin[0] + 1, lh = bmax[1] - bmin[1] + 1;
		if (lw <= 0 || lh <= 0 || lw > 256 || lh > 256)
			continue;
		int nstyles = 0;
		while (nstyles < 4 && fc.styles[nstyles] != 255)
			nstyles++;
		size_t need = (size_t)lw * lh * 3 * nstyles;
		if ((size_t)fc.lightofs + need > m.lighting.size())
			continue;
		if (sx + lw + 2 > ATLAS)
		{
			sx = 0;
			sy += rowH;
			rowH = 0;
		}
		if (sy + lh + 2 > ATLAS)
			continue; // atlas full (not for dust2)
		ri.hasLM = true;
		ri.ax = sx + 1;
		ri.ay = sy + 1;
		double sum = 0;
		for (int y = -1; y <= lh; y++)
			for (int x = -1; x <= lw; x++)
			{
				int cx = x < 0 ? 0 : (x >= lw ? lw - 1 : x), cy = y < 0 ? 0 : (y >= lh ? lh - 1 : y);
				int rgb[3] = {0, 0, 0};
				for (int s = 0; s < nstyles; s++)
				{
					const uint8_t* src = &m.lighting[fc.lightofs + ((size_t)s * lw * lh + (size_t)cy * lw + cx) * 3];
					for (int k = 0; k < 3; k++)
						rgb[k] += src[k];
				}
				uint8_t* d = &atlas[((size_t)(ri.ay + y) * ATLAS + (ri.ax + x)) * 3];
				for (int k = 0; k < 3; k++)
					d[k] = (uint8_t)(rgb[k] > 255 ? 255 : rgb[k]);
				if (x >= 0 && y >= 0 && x < lw && y < lh)
					sum += (rgb[0] + rgb[1] + rgb[2]) / (3.0 * 255.0);
			}
		ri.avg = (float)(sum / (lw * lh));
		sx += lw + 2;
		rowH = rowH > lh + 2 ? rowH : lh + 2;
	}
	glGenTextures(1, &g_lmTex);
	glBindTexture(GL_TEXTURE_2D, g_lmTex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, ATLAS, ATLAS, 0, GL_RGB, GL_UNSIGNED_BYTE, atlas.data());
	glBindTexture(GL_TEXTURE_2D, 0);
	Log("classic: lightmap atlas filled to row %d of %d", sy + rowH, ATLAS);
}

// ---------------------------------------------------------------------------------------------
// Chunks

static const int CH = 8;
struct Vtx
{
	float x, y, z;
	float u, v, layer;
	float lu, lv, light; // lu < 0: use 'light' instead of the lightmap
};
struct Chunk
{
	GLuint vbo = 0;
	int count = 0;
	bool dirty = true;
	std::vector<int> faces;
};
static std::vector<Chunk> g_chunks;
static int g_ncx = 0, g_ncy = 0, g_ncz = 0;
static std::unordered_map<uint32_t, float> g_cellLight;

static void ChunkBox(int cx, int cy, int cz, float mn[3], float mx[3])
{
	int c[3] = {cx, cy, cz};
	for (int i = 0; i < 3; i++)
	{
		mn[i] = g_grid->origin[i] + c[i] * CH * MC_BLOCK_SIZE;
		mx[i] = mn[i] + CH * MC_BLOCK_SIZE;
	}
}

static void IndexFaces()
{
	const mcb::Map& m = g_classic.map;
	g_ncx = (g_grid->sx + CH - 1) / CH;
	g_ncy = (g_grid->sy + CH - 1) / CH;
	g_ncz = (g_grid->sz + CH - 1) / CH;
	g_chunks.assign((size_t)g_ncx * g_ncy * g_ncz, Chunk());
	std::vector<int> list;
	const mcb::Model& w = m.models[0];
	for (int f = w.firstface; f < w.firstface + w.numfaces; f++)
		list.push_back(f);
	for (int md : m.drawModels)
		for (int f = m.models[md].firstface; f < m.models[md].firstface + m.models[md].numfaces; f++)
			list.push_back(f);
	for (int f : list)
	{
		if (!g_faces[f].draw)
			continue;
		float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
		for (int i = 0; i < m.FaceVertexCount(f); i++)
		{
			float v[3];
			m.FaceVertex(f, i, v);
			for (int k = 0; k < 3; k++)
			{
				mn[k] = fminf(mn[k], v[k]);
				mx[k] = fmaxf(mx[k], v[k]);
			}
		}
		int c0[3], c1[3], dims[3] = {g_ncx, g_ncy, g_ncz};
		for (int k = 0; k < 3; k++)
		{
			c0[k] = (int)floorf((mn[k] - 0.01f - g_grid->origin[k]) / (CH * MC_BLOCK_SIZE));
			c1[k] = (int)floorf((mx[k] + 0.01f - g_grid->origin[k]) / (CH * MC_BLOCK_SIZE));
			c0[k] = c0[k] < 0 ? 0 : c0[k];
			c1[k] = c1[k] >= dims[k] ? dims[k] - 1 : c1[k];
		}
		for (int z = c0[2]; z <= c1[2]; z++)
			for (int y = c0[1]; y <= c1[1]; y++)
				for (int x = c0[0]; x <= c1[0]; x++)
					g_chunks[((size_t)z * g_ncy + y) * g_ncx + x].faces.push_back(f);
	}
}

typedef float P3[3];

static void EmitPoly(std::vector<Vtx>& out, const P3* poly, int n, int f)
{
	const mcb::Map& m = g_classic.map;
	const FaceRI& ri = g_faces[f];
	const mcb::TexInfo& ti = m.texinfo[m.faces[f].texinfo];
	auto vert = [&](const float* p) {
		Vtx v;
		v.x = p[0];
		v.y = p[1];
		v.z = p[2];
		float s = p[0] * ti.vecs[0][0] + p[1] * ti.vecs[0][1] + p[2] * ti.vecs[0][2] + ti.vecs[0][3];
		float t = p[0] * ti.vecs[1][0] + p[1] * ti.vecs[1][1] + p[2] * ti.vecs[1][2] + ti.vecs[1][3];
		v.u = s * ri.texScale[0];
		v.v = t * ri.texScale[1];
		v.layer = (float)ri.layer;
		if (ri.hasLM)
		{
			v.lu = ((s - ri.tmin[0]) / 16.0f + 0.5f + ri.ax) / ATLAS;
			v.lv = ((t - ri.tmin[1]) / 16.0f + 0.5f + ri.ay) / ATLAS;
			v.light = 1.0f;
		}
		else
		{
			v.lu = -1.0f;
			v.lv = 0.0f;
			v.light = 1.0f;
		}
		return v;
	};
	for (int i = 1; i + 1 < n; i++)
	{
		out.push_back(vert(poly[0]));
		out.push_back(vert(poly[i]));
		out.push_back(vert(poly[i + 1]));
	}
}

// World-aligned planar texturing for surfaces the map never had (cut walls, placed blocks).
static void EmitPlanar(std::vector<Vtx>& out, const P3* poly, int n, int axis, int layer, float texW, float texH, float light)
{
	auto vert = [&](const float* p) {
		Vtx v;
		v.x = p[0];
		v.y = p[1];
		v.z = p[2];
		float a, b;
		if (axis == 0)
		{
			a = p[1];
			b = -p[2];
		}
		else if (axis == 1)
		{
			a = p[0];
			b = -p[2];
		}
		else
		{
			a = p[0];
			b = p[1];
		}
		v.u = a / texW;
		v.v = b / texH;
		v.layer = (float)layer;
		v.lu = -1.0f;
		v.lv = 0.0f;
		v.light = light;
		return v;
	};
	for (int i = 1; i + 1 < n; i++)
	{
		out.push_back(vert(poly[0]));
		out.push_back(vert(poly[i]));
		out.push_back(vert(poly[i + 1]));
	}
}

// Keep the parts of a polygon that lie in the classic map's solid space (world + solid brush models).
static void SolidParts(const mcb::ClipNode* nodes, int num, const std::vector<float>& poly, std::vector<std::vector<float>>& out)
{
	const mcb::Map& m = g_classic.map;
	while (num >= 0)
	{
		const mcb::ClipNode& nd = nodes[num];
		const mcb::Plane& pl = m.planes[nd.planenum];
		int n = (int)poly.size() / 3;
		float dmin = 1e9f, dmax = -1e9f;
		for (int i = 0; i < n; i++)
		{
			float d = poly[i * 3] * pl.normal[0] + poly[i * 3 + 1] * pl.normal[1] + poly[i * 3 + 2] * pl.normal[2] - pl.dist;
			dmin = fminf(dmin, d);
			dmax = fmaxf(dmax, d);
		}
		if (dmin >= -0.01f)
		{
			num = nd.children[0];
			continue;
		}
		if (dmax <= 0.01f)
		{
			num = nd.children[1];
			continue;
		}
		static P3 a[64], fr[64], bk[64];
		int na = n < 64 ? n : 64;
		memcpy(a, poly.data(), sizeof(float) * 3 * na);
		float pf[4] = {-pl.normal[0], -pl.normal[1], -pl.normal[2], -pl.dist}; // keeps the front side
		float pb[4] = {pl.normal[0], pl.normal[1], pl.normal[2], pl.dist};
		int nf = mcc::ClipPolygon(a, na, pf, fr, 64);
		int nb = mcc::ClipPolygon(a, na, pb, bk, 64);
		std::vector<float> f2(fr[0], fr[0] + nf * 3), b2(bk[0], bk[0] + nb * 3);
		if (nf >= 3)
			SolidParts(nodes, nd.children[0], f2, out);
		if (nb >= 3)
			SolidParts(nodes, nd.children[1], b2, out);
		return;
	}
	if (num == mcb::CONT_SOLID && poly.size() >= 9)
		out.push_back(poly);
}

static float CellLight(int x, int y, int z)
{
	uint32_t key = (uint32_t)((x + 512) & 1023) | ((uint32_t)((y + 512) & 1023) << 10) | ((uint32_t)((z + 512) & 1023) << 20);
	auto it = g_cellLight.find(key);
	if (it != g_cellLight.end())
		return it->second;
	float light = 0.3f;
	for (int dz = 0; dz <= 8; dz++)
	{
		const std::vector<int>* fl = g_classic.FacesInCell(x, y, z + dz);
		if (!fl)
			continue;
		float best = -1, area = 0;
		for (int f : *fl)
			if (g_faces[f].draw && g_faces[f].area > area)
			{
				area = g_faces[f].area;
				best = g_faces[f].avg;
			}
		if (best >= 0)
		{
			light = best * powf(0.8f, (float)dz);
			break;
		}
	}
	g_cellLight[key] = light;
	return light;
}

static void BuildChunk(int ci)
{
	Chunk& ch = g_chunks[ci];
	ch.dirty = false;
	int cx = ci % g_ncx, cy = (ci / g_ncx) % g_ncy, cz = ci / (g_ncx * g_ncy);
	float mn[3], mx[3];
	ChunkBox(cx, cy, cz, mn, mx);
	const mcb::Map& m = g_classic.map;
	std::vector<Vtx> verts;
	// carved cells in (and around) the chunk
	bool anyCarved = false;
	for (int z = cz * CH; z < (cz + 1) * CH && !anyCarved; z++)
		for (int y = cy * CH; y < (cy + 1) * CH && !anyCarved; y++)
			for (int x = cx * CH; x < (cx + 1) * CH; x++)
				if (g_classic.Carved(x, y, z))
				{
					anyCarved = true;
					break;
				}
	float chunkPlanes[6][4] = {{1, 0, 0, mx[0]}, {-1, 0, 0, -mn[0]}, {0, 1, 0, mx[1]}, {0, -1, 0, -mn[1]}, {0, 0, 1, mx[2]}, {0, 0, -1, -mn[2]}};
	static P3 a[64], b[64];
	for (int f : ch.faces)
	{
		int n = m.FaceVertexCount(f);
		if (n > 64)
			n = 64;
		for (int i = 0; i < n; i++)
			m.FaceVertex(f, i, a[i]);
		for (int p = 0; p < 6 && n >= 3; p++)
		{
			n = mcc::ClipPolygon(a, n, chunkPlanes[p], b, 64);
			memcpy(a, b, sizeof(P3) * n);
		}
		if (n < 3)
			continue;
		if (!anyCarved)
		{
			EmitPoly(verts, a, n, f);
			continue;
		}
		// cut by the cell grid and drop the parts whose solid side was dug out
		std::vector<std::vector<float>> pieces = {std::vector<float>(a[0], a[0] + n * 3)};
		for (int axis = 0; axis < 3; axis++)
		{
			std::vector<std::vector<float>> next;
			for (auto& pc : pieces)
			{
				float lo = 1e9f, hi = -1e9f;
				for (size_t i = axis; i < pc.size(); i += 3)
				{
					lo = fminf(lo, pc[i]);
					hi = fmaxf(hi, pc[i]);
				}
				int k0 = (int)floorf((lo - g_grid->origin[axis]) / MC_BLOCK_SIZE) + 1;
				int k1 = (int)floorf((hi - g_grid->origin[axis]) / MC_BLOCK_SIZE);
				std::vector<float> cur = pc;
				for (int k = k0; k <= k1 && cur.size() >= 9; k++)
				{
					float x = g_grid->origin[axis] + k * MC_BLOCK_SIZE;
					float below[4] = {0, 0, 0, x}, above[4] = {0, 0, 0, -x};
					below[axis] = 1;
					above[axis] = -1;
					static P3 src[64], lo3[64], hi3[64];
					int ns = (int)cur.size() / 3;
					memcpy(src, cur.data(), sizeof(P3) * ns);
					int nl = mcc::ClipPolygon(src, ns, below, lo3, 64);
					int nh = mcc::ClipPolygon(src, ns, above, hi3, 64);
					if (nl >= 3)
						next.emplace_back(lo3[0], lo3[0] + nl * 3);
					cur.assign(hi3[0], hi3[0] + (nh >= 3 ? nh * 3 : 0));
				}
				if (cur.size() >= 9)
					next.push_back(cur);
			}
			pieces.swap(next);
		}
		const float* nrm = g_faces[f].normal;
		for (auto& pc : pieces)
		{
			int np = (int)pc.size() / 3;
			float c[3] = {0, 0, 0};
			for (int i = 0; i < np; i++)
				for (int k = 0; k < 3; k++)
					c[k] += pc[i * 3 + k] / np;
			float behind[3] = {c[0] - nrm[0] * 0.5f, c[1] - nrm[1] * 0.5f, c[2] - nrm[2] * 0.5f};
			int cell[3];
			g_grid->ToBlock(behind, cell);
			if (g_classic.Carved(cell[0], cell[1], cell[2]))
				continue;
			memcpy(a, pc.data(), sizeof(P3) * np);
			EmitPoly(verts, a, np, f);
		}
	}
	if (anyCarved)
	{
		// cut walls: the solid space of intact neighbours, seen from the dug-out cell
		static const int dir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
		for (int z = cz * CH; z < (cz + 1) * CH; z++)
			for (int y = cy * CH; y < (cy + 1) * CH; y++)
				for (int x = cx * CH; x < (cx + 1) * CH; x++)
				{
					if (!g_classic.Carved(x, y, z))
						continue;
					for (int d = 0; d < 6; d++)
					{
						int nx = x + dir[d][0], ny = y + dir[d][1], nz = z + dir[d][2];
						if (g_classic.Carved(nx, ny, nz) || g_classic.Pieces(nx, ny, nz).empty())
							continue;
						int axis = d / 2;
						float cmn[3], cmx[3];
						g_classic.CellBox(x, y, z, cmn, cmx);
						float plane = dir[d][axis] > 0 ? cmx[axis] : cmn[axis];
						float off = dir[d][axis] * 0.05f; // sample the neighbour's side of the boundary
						int u = (axis + 1) % 3, v = (axis + 2) % 3;
						std::vector<float> sq(12);
						float cu[4] = {cmn[u], cmx[u], cmx[u], cmn[u]}, cv[4] = {cmn[v], cmn[v], cmx[v], cmx[v]};
						for (int k = 0; k < 4; k++)
						{
							sq[k * 3 + axis] = plane + off;
							sq[k * 3 + u] = cu[k];
							sq[k * 3 + v] = cv[k];
						}
						std::vector<std::vector<float>> parts;
						SolidParts(m.hull0.data(), m.models[0].headnode[0], sq, parts);
						for (int sm : m.solidModels)
							SolidParts(m.hull0.data(), m.models[sm].headnode[0], sq, parts);
						if (parts.empty())
							continue;
						const mcc::CellInfo& info = g_classic.Info(nx, ny, nz);
						int layer = 0;
						float tw = 128, th = 128;
						if (info.ore && g_oreLayer[info.ore] >= 0)
						{
							layer = g_oreLayer[info.ore];
							tw = th = MC_BLOCK_SIZE;
						}
						else if (info.miptex >= 0 && info.miptex < (int)g_mips.size())
						{
							layer = g_mips[info.miptex].layer;
							tw = (float)g_mips[info.miptex].w;
							th = (float)g_mips[info.miptex].h;
						}
						float shade = axis == 2 ? (dir[d][2] < 0 ? 1.0f : 0.6f) : (axis == 0 ? 0.8f : 0.7f);
						float light = CellLight(nx, ny, nz) * shade;
						for (auto& pt : parts)
						{
							int np = (int)pt.size() / 3;
							if (np > 64)
								np = 64;
							for (int k = 0; k < np; k++)
							{
								a[k][0] = pt[k * 3];
								a[k][1] = pt[k * 3 + 1];
								a[k][2] = pt[k * 3 + 2];
								a[k][axis] = plane;
							}
							EmitPlanar(verts, a, np, axis, layer, tw, th, light);
						}
					}
				}
	}
	// placed blocks with a classic texture
	for (int z = cz * CH; z < (cz + 1) * CH; z++)
		for (int y = cy * CH; y < (cy + 1) * CH; y++)
			for (int x = cx * CH; x < (cx + 1) * CH; x++)
			{
				mcw::Cell c = g_grid->Get(x, y, z);
				if (!c)
					continue;
				const mcw::BlockDef& bd = mcw::Block(mcw::CellType(c));
				int mt = mcw::BspTextureOf(bd.texSide);
				if (mt < 0 || mt >= (int)g_mips.size())
					continue;
				float cmn[3], cmx[3];
				g_classic.CellBox(x, y, z, cmn, cmx);
				static const int dir[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
				float light = CellLight(x, y, z);
				for (int d = 0; d < 6; d++)
				{
					mcw::Cell nc = g_grid->Get(x + dir[d][0], y + dir[d][1], z + dir[d][2]);
					if (nc && g_grid->ShapeAt(x + dir[d][0], y + dir[d][1], z + dir[d][2]) == mcw::SHAPE_CUBE &&
						!(mcw::Block(mcw::CellType(nc)).flags & mcw::BF_TRANSPARENT))
						continue;
					int axis = d / 2;
					int u = (axis + 1) % 3, v = (axis + 2) % 3;
					float plane = dir[d][axis] > 0 ? cmx[axis] : cmn[axis];
					float cu[4] = {cmn[u], cmx[u], cmx[u], cmn[u]}, cv[4] = {cmn[v], cmn[v], cmx[v], cmx[v]};
					for (int k = 0; k < 4; k++)
					{
						a[k][axis] = plane;
						a[k][u] = cu[k];
						a[k][v] = cv[k];
					}
					float shade = axis == 2 ? (dir[d][2] > 0 ? 1.0f : 0.5f) : (axis == 0 ? 0.8f : 0.7f);
					EmitPlanar(verts, a, 4, axis, g_mips[mt].layer, (float)g_mips[mt].w, (float)g_mips[mt].h, light * shade + 0.15f);
				}
			}
	ch.count = (int)verts.size();
	if (!ch.vbo)
		mcgl::GenBuffers(1, &ch.vbo);
	mcgl::BindBuffer(GL_ARRAY_BUFFER, ch.vbo);
	mcgl::BufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vtx), verts.empty() ? nullptr : verts.data(), GL_STATIC_DRAW);
	mcgl::BindBuffer(GL_ARRAY_BUFFER, 0);
}

// ---------------------------------------------------------------------------------------------
// Shader

static const char* kVS = R"(#version 330 compatibility
in vec3 aPos;
in vec3 aUV;
in vec3 aLM;
out vec3 vUV;
out vec3 vLM;
void main()
{
	gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);
	vUV = aUV;
	vLM = aLM;
}
)";

static const char* kFS = R"(#version 330 compatibility
uniform sampler2DArray uTex;
uniform sampler2D uLM;
uniform float uScale;
uniform float uGamma;
uniform float uTexGamma;
in vec3 vUV;
in vec3 vLM;
out vec4 fragColor;
void main()
{
	vec4 t = texture(uTex, vUV);
	if (t.a < 0.5)
		discard;
	vec3 l = vLM.x >= 0.0 ? texture(uLM, vLM.xy).rgb : vec3(vLM.z);
	l = pow(l, vec3(uGamma)) * uScale;
	fragColor = vec4(min(pow(t.rgb, vec3(uTexGamma)) * l, vec3(1.0)), 1.0);
}
)";

static cvar_t* g_cvGamma = nullptr;
static cvar_t* g_cvScale = nullptr;
static cvar_t* g_cvTexGamma = nullptr;

static bool InitGL()
{
	if (g_prog)
		return true;
	if (!mcgl::Ready())
		return false;
	static const char* attribs[] = {"aPos", "aUV", "aLM"};
	g_prog = mcgl::BuildProgram("classic", kVS, kFS, attribs, 3);
	if (!g_prog)
		return false;
	u_tex = mcgl::GetUniformLocation(g_prog, "uTex");
	u_lm = mcgl::GetUniformLocation(g_prog, "uLM");
	u_scale = mcgl::GetUniformLocation(g_prog, "uScale");
	u_gamma = mcgl::GetUniformLocation(g_prog, "uGamma");
	u_texGamma = mcgl::GetUniformLocation(g_prog, "uTexGamma");
	if (mcgl::GenVertexArrays)
		mcgl::GenVertexArrays(1, &g_vao);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Public

bool ClassicActive() { return g_on; }
mcc::Classic* ClassicClient() { return g_on ? &g_classic : nullptr; }

bool ClassicLoad(const char* mapname, const std::string& cstrikeDir, mcw::World& grid, std::vector<mcw::Cell>& cells)
{
	g_on = false;
	std::vector<uint8_t> txt;
	if (!ReadAll(cstrikeDir + "maps\\" + mapname + ".mcc", txt))
		return false;
	txt.push_back(0);
	mcc::Config cfg;
	if (!cfg.Parse((const char*)txt.data()))
		return false;
	cells.assign((size_t)cfg.size[0] * cfg.size[1] * cfg.size[2], 0);
	grid.sx = cfg.size[0];
	grid.sy = cfg.size[1];
	grid.sz = cfg.size[2];
	for (int i = 0; i < 3; i++)
		grid.origin[i] = cfg.origin[i];
	grid.cells = cells.data();
	grid.shapeOfType = mcw::g_shapeOfType;
	g_classic.grid = &grid;
	g_grid = &grid;
	std::string bsp = cstrikeDir + cfg.bsp;
	for (char& c : bsp)
		if (c == '/')
			c = '\\';
	if (!g_classic.Load(bsp.c_str()))
	{
		Log("classic: could not load %s", bsp.c_str());
		return false;
	}
	mcc::RegisterClassicBlocks(g_classic);
	mcm::SetClassic(&g_classic);
	g_cellLight.clear();
	if (!g_cvGamma)
	{
		g_cvGamma = gEngfuncs.pfnRegisterVariable((char*)"mc_classic_lightgamma", (char*)"0.75", 0);
		g_cvScale = gEngfuncs.pfnRegisterVariable((char*)"mc_classic_lightscale", (char*)"1.55", 0);
		g_cvTexGamma = gEngfuncs.pfnRegisterVariable((char*)"mc_classic_texgamma", (char*)"0.85", 0);
	}
	if (InitGL())
	{
		LoadTextures(cstrikeDir);
		BuildLightmaps();
		IndexFaces();
	}
	g_on = true;
	Log("classic: %s loaded, %d faces, %d texture blocks, %d chunks", bsp.c_str(), (int)g_classic.map.faces.size(), mcw::g_numDynBlocks,
		(int)g_chunks.size());
	return true;
}

void ClassicUnload()
{
	for (auto& c : g_chunks)
		if (c.vbo)
			mcgl::DeleteBuffers(1, &c.vbo);
	g_chunks.clear();
	if (g_texArr)
		glDeleteTextures(1, &g_texArr);
	if (g_lmTex)
		glDeleteTextures(1, &g_lmTex);
	g_texArr = g_lmTex = 0;
	g_on = false;
	g_grid = nullptr;
	mcm::SetClassic(nullptr);
}

void ClassicCellChanged(int x, int y, int z)
{
	if (!g_on)
		return;
	for (int dz = -1; dz <= 1; dz++)
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
			{
				int gx = x + dx, gy = y + dy, gz = z + dz;
				if (gx < 0 || gy < 0 || gz < 0)
					continue;
				int cx = gx / CH, cy = gy / CH, cz = gz / CH;
				if (cx >= g_ncx || cy >= g_ncy || cz >= g_ncz)
					continue;
				g_chunks[((size_t)cz * g_ncy + cy) * g_ncx + cx].dirty = true;
			}
}

void ClassicResetAll()
{
	for (auto& c : g_chunks)
		c.dirty = true;
}

void ClassicDraw()
{
	if (!g_on || !g_prog || !g_texArr)
		return;
	int rebuilt = 0;
	for (size_t i = 0; i < g_chunks.size() && rebuilt < 48; i++)
		if (g_chunks[i].dirty && (!g_chunks[i].faces.empty() || g_chunks[i].vbo || true))
		{
			BuildChunk((int)i);
			rebuilt++;
		}
	mcgl::UseProgram(g_prog);
	if (g_vao)
		mcgl::BindVertexArray(g_vao);
	mcgl::ActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, g_texArr);
	mcgl::ActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, g_lmTex);
	mcgl::Uniform1i(u_tex, 0);
	mcgl::Uniform1i(u_lm, 1);
	mcgl::Uniform1f(u_gamma, g_cvGamma ? g_cvGamma->value : 0.75f);
	mcgl::Uniform1f(u_scale, g_cvScale ? g_cvScale->value : 1.55f);
	mcgl::Uniform1f(u_texGamma, g_cvTexGamma ? g_cvTexGamma->value : 0.85f);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_CULL_FACE);
	mcgl::EnableVertexAttribArray(0);
	mcgl::EnableVertexAttribArray(1);
	mcgl::EnableVertexAttribArray(2);
	for (auto& ch : g_chunks)
	{
		if (!ch.count)
			continue;
		mcgl::BindBuffer(GL_ARRAY_BUFFER, ch.vbo);
		mcgl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)0);
		mcgl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)12);
		mcgl::VertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void*)24);
		glDrawArrays(GL_TRIANGLES, 0, ch.count);
	}
	if (mcgl::DisableVertexAttribArray)
	{
		mcgl::DisableVertexAttribArray(0);
		mcgl::DisableVertexAttribArray(1);
		mcgl::DisableVertexAttribArray(2);
	}
	mcgl::BindBuffer(GL_ARRAY_BUFFER, 0);
	mcgl::ActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	mcgl::ActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

// Light level 0..1 at a point (for Minecraft entities and placed blocks): the lightmap brightness of
// the classic surface under it.
// The classic renderer's light curve (pow(gamma) * scale), so Minecraft blocks placed in the map sit in
// the same light as its walls.
static float Perceived(float raw)
{
	float g = g_cvGamma ? g_cvGamma->value : 0.75f, sc = g_cvScale ? g_cvScale->value : 1.6f;
	float l = powf(raw > 0.0f ? raw : 0.0f, g) * sc;
	return l < 0.08f ? 0.08f : (l > 1.0f ? 1.0f : l);
}

float ClassicCellBrightness(int x, int y, int z)
{
	if (!g_on)
		return 1.0f;
	return Perceived(CellLight(x, y, z - 1));
}

float ClassicLightAt(const float* p)
{
	if (!g_on || !g_grid)
		return 1.0f;
	int b[3];
	g_grid->ToBlock(p, b);
	return Perceived(CellLight(b[0], b[1], b[2] - 1));
}
} // namespace mc
