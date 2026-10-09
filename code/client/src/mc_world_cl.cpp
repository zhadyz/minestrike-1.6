// Client voxel world: loads maps/<map>.mcw, applies server edits, Minecraft-style lighting
// (skylight + block light flood fill, smooth lighting with ambient occlusion, per-face shading),
// chunked VBO meshes drawn with a texture-array shader, crack overlay and block outline.
#include "mc_blocks.h"
#include "mc_classic.h"
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
// classic mode (mc_classic_cl.cpp): a real CS map is the world, this module only draws placed blocks
static bool g_classicMode = false;
bool ClassicLoad(const char* mapname, const std::string& cstrikeDir, mcw::World& grid, std::vector<mcw::Cell>& cells);
void ClassicUnload();
void ClassicCellChanged(int x, int y, int z);
void ClassicResetAll();
void ClassicDraw();
float ClassicLightAt(const float* p);
float ClassicCellBrightness(int x, int y, int z);
mcc::Classic* ClassicClient();
static int g_tPlainTorch = -1;
// block light on classic maps (BlockLightUpdate)
static std::vector<uint8_t> g_bl;     // level 0..15 per cell
static std::vector<uint8_t> g_blOpen; // untouched classic cells: 0 not asked yet, 1 open, 2 solid
static std::vector<uint32_t> g_blLit; // the cells holding light (to clear them)
static bool g_blDirty = true;

static const float BS = 40.0f;
static const int CH = 16; // chunk size in blocks

static mcw::World g_w;
static std::vector<mcw::Cell> g_cells;
static std::vector<uint8_t> g_light; // (sky << 4) | block
static bool g_loaded = false;
static bool g_lightDirty = false;

struct Chunk
{
	GLuint vbo = 0;
	int count = 0;
	bool dirty = true;
};
static std::vector<Chunk> g_chunks;
static int g_ncx, g_ncy, g_ncz;

// texture array
static GLuint g_texArray = 0;
static std::unordered_map<std::string, int> g_layerOf;
static std::vector<std::string> g_layerNames;
struct BlockLayers
{
	int top, side, bottom, sideTop; // sideTop: doors' upper half texture
};
static std::vector<BlockLayers> g_blockLayers;

static GLuint g_prog = 0, g_vao = 0;
static GLint u_tex = -1, u_fogColor = -1, u_fogStart = -1, u_fogEnd = -1, u_bl = -1, u_blOrg = -1, u_blInv = -1, u_blOn = -1;

struct Breaking
{
	int x = 0, y = 0, z = 0, stage = -1;
};
static Breaking g_breaking[33];

const mcw::World* WorldGet() { return g_loaded ? &g_w : nullptr; }

// ---------------------------------------------------------------------------------------------
// Loading

static bool ReadFileBytes(const char* path, std::vector<uint8_t>& out)
{
	FILE* f = fopen(path, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	out.resize(n > 0 ? n : 0);
	size_t got = n > 0 ? fread(out.data(), 1, n, f) : 0;
	fclose(f);
	return got == (size_t)n;
}

static bool ParseWorld(const std::vector<uint8_t>& d)
{
	const uint8_t* p = d.data();
	const uint8_t* end = p + d.size();
	auto rd = [&](void* o, size_t n) {
		if (p + n > end)
			return false;
		memcpy(o, p, n);
		p += n;
		return true;
	};
	char magic[4];
	int ver, sx, sy, sz, pal, runs;
	float org[3], bs;
	if (!rd(magic, 4) || memcmp(magic, "MCW1", 4) || !rd(&ver, 4) || !rd(&sx, 4) || !rd(&sy, 4) || !rd(&sz, 4) || !rd(org, 12) ||
		!rd(&bs, 4) || !rd(&pal, 4))
		return false;
	if (ver != 1 || sx <= 0 || sy <= 0 || sz <= 0 || pal <= 0 || pal > 1024)
		return false;
	std::vector<uint16_t> remap(pal);
	for (int i = 0; i < pal; i++)
	{
		char name[33] = {};
		if (!rd(name, 32))
			return false;
		int id = mcw::FindBlock(name);
		remap[i] = (uint16_t)(id < 0 ? mcw::FindBlock("stone") : id);
	}
	if (!rd(&runs, 4))
		return false;
	size_t total = (size_t)sx * sy * sz, pos = 0;
	g_cells.assign(total, 0);
	for (int r = 0; r < runs; r++)
	{
		uint32_t n;
		uint16_t c, pad;
		if (!rd(&n, 4) || !rd(&c, 2) || !rd(&pad, 2) || pos + n > total)
			return false;
		uint16_t t = mcw::CellType(c);
		mcw::Cell cc = t == 0 ? 0 : mcw::MakeCell(t < remap.size() ? remap[t] : 0, mcw::CellState(c));
		for (uint32_t k = 0; k < n; k++)
			g_cells[pos++] = cc;
	}
	if (pos != total)
		return false;
	g_w.sx = sx;
	g_w.sy = sy;
	g_w.sz = sz;
	for (int i = 0; i < 3; i++)
		g_w.origin[i] = org[i];
	g_w.cells = g_cells.data();
	g_w.shapeOfType = mcw::g_shapeOfType;
	return true;
}

static int Layer(const char* name)
{
	if (!name)
		return 0;
	auto it = g_layerOf.find(name);
	if (it != g_layerOf.end())
		return it->second;
	int idx = (int)g_layerNames.size();
	g_layerNames.push_back(name);
	g_layerOf[name] = idx;
	return idx;
}

static void RegisterRedstoneLayers();

struct FireAnim
{
	int layer = 0, frames = 0, shown = -1;
	std::vector<uint8_t> rgba; // the whole strip, 16x16 frames top to bottom
};
static FireAnim g_fire[2];

static void BuildTextureArray()
{
	if (g_texArray)
		return;
	g_layerNames.clear();
	g_layerOf.clear();
	Layer("stone"); // layer 0 fallback
	g_blockLayers.resize(mcw::g_numBlocks);
	for (int i = 0; i < mcw::g_numBlocks; i++)
	{
		const mcw::BlockDef& b = mcw::g_blocks[i];
		BlockLayers bl;
		bl.top = Layer(b.texTop);
		bl.side = Layer(b.texSide);
		bl.bottom = Layer(b.texBottom);
		bl.sideTop = bl.side;
		if (b.shape == mcw::SHAPE_DOOR)
			bl.sideTop = Layer(b.texTop); // door registry: top texture = upper half
		g_blockLayers[i] = bl;
	}
	RegisterRedstoneLayers();
	int n = (int)g_layerNames.size();
	glGenTextures(1, &g_texArray);
	glBindTexture(GL_TEXTURE_2D_ARRAY, g_texArray);
	mcgl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 16, 16, n, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	std::vector<uint8_t> buf(16 * 16 * 4);
	for (int i = 0; i < n; i++)
	{
		mctex::Image img;
		std::string rel = "block/" + g_layerNames[i];
		if (!mctex::LoadImage(rel.c_str(), img))
		{
			Log("world: missing block texture %s", rel.c_str());
			for (int k = 0; k < 256; k++)
			{
				bool c = ((k % 16) / 8 + (k / 16) / 8) & 1;
				buf[k * 4 + 0] = c ? 255 : 0;
				buf[k * 4 + 1] = 0;
				buf[k * 4 + 2] = c ? 255 : 0;
				buf[k * 4 + 3] = 255;
			}
		}
		else
		{
			// take the first 16x16 frame (resample if the texture is a different size)
			for (int y = 0; y < 16; y++)
				for (int x = 0; x < 16; x++)
				{
					int sx = x * img.w / 16, sy = y * img.w / 16; // square frames
					memcpy(&buf[(y * 16 + x) * 4], &img.rgba[(sy * img.w + sx) * 4], 4);
				}
			img.Free();
		}
		mcgl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, 16, 16, 1, GL_RGBA, GL_UNSIGNED_BYTE, buf.data());
	}
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAX_LEVEL, 4);
	if (mcgl::GenerateMipmap)
		mcgl::GenerateMipmap(GL_TEXTURE_2D_ARRAY);
	Log("world: texture array with %d layers", n);
	// fire is animated: keep its strips to step the two layers through their frames (AnimateFire)
	static const char* fireTex[2] = {"fire_0", "fire_1"};
	for (int i = 0; i < 2; i++)
	{
		g_fire[i].layer = Layer(fireTex[i]);
		g_fire[i].frames = 0;
		g_fire[i].shown = -1;
		mctex::Image img;
		std::string rel = std::string("block/") + fireTex[i];
		if (g_fire[i].layer >= n || !mctex::LoadImage(rel.c_str(), img))
			continue;
		if (img.w == 16 && img.h % 16 == 0)
		{
			g_fire[i].frames = img.h / 16;
			g_fire[i].rgba.assign(img.rgba, img.rgba + (size_t)img.w * img.h * 4);
		}
		img.Free();
	}
}

// Minecraft's fire textures run at one frame per tick (fire_0 starts half way through its strip)
static void AnimateFire()
{
	int tick = (int)(gEngfuncs.GetClientTime() * 20.0);
	bool changed = false;
	for (int i = 0; i < 2; i++)
	{
		FireAnim& f = g_fire[i];
		if (f.frames <= 0)
			continue;
		int frame = (tick + (i == 0 ? f.frames / 2 : 0)) % f.frames;
		if (frame == f.shown)
			continue;
		f.shown = frame;
		mcgl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, f.layer, 16, 16, 1, GL_RGBA, GL_UNSIGNED_BYTE, &f.rgba[(size_t)frame * 16 * 16 * 4]);
		changed = true;
	}
	if (changed && mcgl::GenerateMipmap)
		mcgl::GenerateMipmap(GL_TEXTURE_2D_ARRAY);
}

static const char* kVS = R"(#version 330 compatibility
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aUV;
layout(location = 2) in vec4 aCol;
out vec3 vUV;
out vec4 vCol;
out float vDist;
out vec3 vPos;
void main() {
	gl_Position = gl_ModelViewProjectionMatrix * vec4(aPos, 1.0);
	vPos = aPos;
	vUV = aUV;
	vCol = aCol;
	vDist = length((gl_ModelViewMatrix * vec4(aPos, 1.0)).xyz);
}
)";

static const char* kFS = R"(#version 330 compatibility
uniform sampler2DArray uTex;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
in vec3 vUV;
in vec4 vCol;
in float vDist;
out vec4 fragColor;
uniform sampler3D uBL;
uniform vec3 uBLOrg;
uniform vec3 uBLInv;
uniform float uBLOn;
in vec3 vPos;
// Minecraft block light (fire, torches, glowstone): the level half a cell off the surface on the viewer's
// side, through LightTexture's curve and warm tint. It lifts what is in shade and leaves daylight alone.
vec3 BlockLight(vec3 base)
{
	if (uBLOn <= 0.0)
		return base;
	vec3 n = normalize(cross(dFdx(vPos), dFdy(vPos)));
	if (dot(n, gl_ModelViewMatrixInverse[3].xyz - vPos) < 0.0)
		n = -n;
	float lv = texture(uBL, (vPos + n * 20.0 - uBLOrg) * uBLInv).r;
	float b = lv / (4.0 - 3.0 * lv) * uBLOn;
	vec3 blk = vec3(b, b * ((b * 0.6 + 0.4) * 0.6 + 0.4), b * (b * b * 0.6 + 0.4));
	return min(base + blk, max(base, vec3(1.0)));
}
void main() {
	vec4 t = texture(uTex, vUV);
	if (t.a < 0.5) discard;
	vec3 c = t.rgb * BlockLight(vCol.rgb);
	float f = clamp((vDist - uFogStart) / (uFogEnd - uFogStart), 0.0, 1.0);
	fragColor = vec4(mix(c, uFogColor, f), 1.0);
}
)";

static void InitGL()
{
	if (g_prog || !mcgl::Ready())
		return;
	static const char* attribs[] = {"aPos", "aUV", "aCol"};
	g_prog = mcgl::BuildProgram("voxel", kVS, kFS, attribs, 3);
	if (!g_prog)
		return;
	u_tex = mcgl::GetUniformLocation(g_prog, "uTex");
	u_fogColor = mcgl::GetUniformLocation(g_prog, "uFogColor");
	u_fogStart = mcgl::GetUniformLocation(g_prog, "uFogStart");
	u_fogEnd = mcgl::GetUniformLocation(g_prog, "uFogEnd");
	u_bl = mcgl::GetUniformLocation(g_prog, "uBL");
	u_blOrg = mcgl::GetUniformLocation(g_prog, "uBLOrg");
	u_blInv = mcgl::GetUniformLocation(g_prog, "uBLInv");
	u_blOn = mcgl::GetUniformLocation(g_prog, "uBLOn");
	if (mcgl::GenVertexArrays)
		mcgl::GenVertexArrays(1, &g_vao);
	BuildTextureArray();
}

void WorldInit() {}

void WorldUnload()
{
	for (auto& c : g_chunks)
		if (c.vbo)
			mcgl::DeleteBuffers(1, &c.vbo);
	g_chunks.clear();
	g_cells.clear();
	g_light.clear();
	g_bl.clear();
	g_blOpen.clear();
	g_blLit.clear();
	g_blDirty = true;
	g_loaded = false;
	if (g_classicMode)
		ClassicUnload();
	g_classicMode = false;
	mcm::SetWorld(nullptr);
	for (auto& b : g_breaking)
		b.stage = -1;
}

static void ComputeLight();
static int g_blTexDims[3] = {0, 0, 0};

void WorldLoadForMap(const char* mapname)
{
	char path[MAX_PATH];
	snprintf(path, sizeof(path), "%s..\\maps\\%s.mcw", mctex::TexRoot(), mapname);
	// TexRoot = ...\cstrike\mc\textures\ -> go up to cstrike
	std::string p = mctex::TexRoot();
	size_t cut = p.rfind("\\mc\\textures\\");
	if (cut != std::string::npos)
		p = p.substr(0, cut) + "\\maps\\" + mapname + ".mcw";
	if (cut != std::string::npos)
	{
		std::string cs = mctex::TexRoot();
		cs = cs.substr(0, cut) + "\\";
		if (ClassicLoad(mapname, cs, g_w, g_cells))
		{
			g_classicMode = true;
			g_ncx = (g_w.sx + CH - 1) / CH;
			g_ncy = (g_w.sy + CH - 1) / CH;
			g_ncz = (g_w.sz + CH - 1) / CH;
			g_chunks.assign((size_t)g_ncx * g_ncy * g_ncz, Chunk());
			g_loaded = true;
			g_lightDirty = true;
			mcm::SetWorld(&g_w);
			Log("world: classic map for %s", mapname);
			return;
		}
	}
	g_classicMode = false;
	std::vector<uint8_t> bytes;
	if (!ReadFileBytes(p.c_str(), bytes) || !ParseWorld(bytes))
	{
		Log("world: no voxel world for %s (%s)", mapname, p.c_str());
		return;
	}
	g_ncx = (g_w.sx + CH - 1) / CH;
	g_ncy = (g_w.sy + CH - 1) / CH;
	g_ncz = (g_w.sz + CH - 1) / CH;
	g_chunks.assign((size_t)g_ncx * g_ncy * g_ncz, Chunk());
	g_loaded = true;
	g_lightDirty = true;
	mcm::SetWorld(&g_w);
	Log("world: loaded %s %dx%dx%d, %d chunks", mapname, g_w.sx, g_w.sy, g_w.sz, (int)g_chunks.size());
}

static void MarkDirtyAround(int x, int y, int z)
{
	for (int dz = -1; dz <= 1; dz++)
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
			{
				int cx = (x + dx) / CH, cy = (y + dy) / CH, cz = (z + dz) / CH;
				if (x + dx < 0 || y + dy < 0 || z + dz < 0 || cx >= g_ncx || cy >= g_ncy || cz >= g_ncz)
					continue;
				g_chunks[((size_t)cz * g_ncy + cy) * g_ncx + cx].dirty = true;
			}
}

void WorldApplyChange(int x, int y, int z, mcw::Cell c)
{
	if (!g_loaded || !g_w.InBounds(x, y, z))
		return;
	g_w.Set(x, y, z, c);
	MarkDirtyAround(x, y, z);
	if (g_classicMode)
	{
		ClassicCellChanged(x, y, z);
		g_blDirty = true;
	}
	else
		g_lightDirty = true;
}

bool WorldIsClassic() { return g_loaded && g_classicMode; }

void WorldResetCells()
{
	if (!g_loaded)
		return;
	std::fill(g_cells.begin(), g_cells.end(), (mcw::Cell)0);
	for (auto& c : g_chunks)
		c.dirty = true;
	if (g_classicMode)
		ClassicResetAll();
	g_blDirty = true;
}

void WorldSetBreak(int breaker, int x, int y, int z, int stage)
{
	if (breaker < 0 || breaker > 32)
		return;
	g_breaking[breaker].x = x;
	g_breaking[breaker].y = y;
	g_breaking[breaker].z = z;
	g_breaking[breaker].stage = stage;
}

// ---------------------------------------------------------------------------------------------
// Lighting

static inline bool Opaque(mcw::Cell c)
{
	if (!c)
		return false;
	const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
	return d.shape == mcw::SHAPE_CUBE && !(d.flags & mcw::BF_TRANSPARENT);
}

static inline bool OpaqueAt(int x, int y, int z)
{
	if (!g_w.InBounds(x, y, z))
		return false;
	return Opaque(g_w.Get(x, y, z));
}

static int EmitLevel(mcw::Cell c);

static void ComputeLight()
{
	if (g_classicMode)
	{
		// light comes from the classic map's lightmaps (LightAt asks it directly)
		g_lightDirty = false;
		for (auto& c : g_chunks)
			c.dirty = true;
		return;
	}
	size_t total = (size_t)g_w.sx * g_w.sy * g_w.sz;
	g_light.assign(total, 0);
	std::vector<uint32_t> queue;
	queue.reserve(total / 4);
	auto idx = [&](int x, int y, int z) { return ((size_t)z * g_w.sy + y) * g_w.sx + x; };
	// skylight: straight down from the sky at full strength
	for (int y = 0; y < g_w.sy; y++)
		for (int x = 0; x < g_w.sx; x++)
			for (int z = g_w.sz - 1; z >= 0; z--)
			{
				if (Opaque(g_cells[idx(x, y, z)]))
					break;
				g_light[idx(x, y, z)] = 15 << 4;
				queue.push_back((uint32_t)idx(x, y, z));
			}
	// block light sources
	for (size_t i = 0; i < total; i++)
		if (int lv = EmitLevel(g_cells[i]))
		{
			g_light[i] |= (uint8_t)lv;
			queue.push_back((uint32_t)i);
		}
	// flood fill both channels
	static const int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (size_t qi = 0; qi < queue.size(); qi++)
	{
		uint32_t i = queue[qi];
		int x = (int)(i % g_w.sx), y = (int)((i / g_w.sx) % g_w.sy), z = (int)(i / ((size_t)g_w.sx * g_w.sy));
		int sky = g_light[i] >> 4, blk = g_light[i] & 15;
		for (auto& o : d)
		{
			int nx = x + o[0], ny = y + o[1], nz = z + o[2];
			if (!g_w.InBounds(nx, ny, nz))
				continue;
			size_t ni = idx(nx, ny, nz);
			if (Opaque(g_cells[ni]))
				continue;
			int ns = g_light[ni] >> 4, nb = g_light[ni] & 15;
			bool changed = false;
			if (sky - 1 > ns)
			{
				ns = sky - 1;
				changed = true;
			}
			if (blk - 1 > nb)
			{
				nb = blk - 1;
				changed = true;
			}
			if (changed)
			{
				g_light[ni] = (uint8_t)((ns << 4) | nb);
				queue.push_back((uint32_t)ni);
			}
		}
	}
	g_lightDirty = false;
	for (auto& c : g_chunks)
		c.dirty = true;
}

static inline float LightAt(int x, int y, int z)
{
	if (g_classicMode)
		return ClassicCellBrightness(x, y, z);
	if (!g_w.InBounds(x, y, z))
		return z >= g_w.sz ? 1.0f : 0.0f;
	uint8_t l = g_light[((size_t)z * g_w.sy + y) * g_w.sx + x];
	int sky = l >> 4, blk = l & 15;
	float lv = (sky > blk ? sky : blk) / 15.0f;
	// LightTexture brightness curve with Minecraft's default gamma (0.5 "moody"/"bright" mix)
	float b = lv / (4.0f - 3.0f * lv);
	float g = 1.0f - powf(1.0f - b, 4.0f);
	return 0.04f + 0.96f * (b * 0.5f + g * 0.5f);
}

// ---------------------------------------------------------------------------------------------
// Block light on classic maps. The classic map is lit by its baked lightmaps, which know nothing of a fire
// or a torch, so their light is kept apart: Minecraft's flood fill over the 40-unit cells (a source's level,
// one less per cell, stopped by opaque blocks and by cells whose middle is inside the map's solid), held in
// a 3D texture both renderers sample per pixel. On voxel maps ComputeLight above does it, baked in the mesh.
static GLuint g_blTex = 0;
static double g_blNext = 0.0;

static bool BlockLightPasses(size_t i, int x, int y, int z)
{
	mcw::Cell c = g_cells[i];
	if (c)
		return !Opaque(c); // a placed block, or a dug-out cell
	uint8_t& o = g_blOpen[i];
	if (!o)
	{
		mcc::Classic* cl = ClassicClient();
		float p[3] = {g_w.origin[0] + (x + 0.5f) * BS, g_w.origin[1] + (y + 0.5f) * BS, g_w.origin[2] + (z + 0.5f) * BS};
		o = (cl && cl->PointContents(p) == mcb::CONT_SOLID) ? 2 : 1;
	}
	return o == 1;
}

static void BlockLightUpdate()
{
	double now = gEngfuncs.GetClientTime();
	if (!g_classicMode || !g_blDirty || (now < g_blNext && now > g_blNext - 1.0))
		return;
	g_blDirty = false;
	g_blNext = now + 0.1; // a spreading fire changes cells in bursts: ten updates a second is plenty
	size_t total = (size_t)g_w.sx * g_w.sy * g_w.sz;
	bool fresh = g_bl.size() != total;
	if (fresh)
	{
		g_bl.assign(total, 0);
		g_blOpen.assign(total, 0);
		g_blLit.clear();
	}
	for (uint32_t i : g_blLit)
		g_bl[i] = 0;
	g_blLit.clear();
	std::vector<uint32_t> queue;
	for (size_t i = 0; i < total; i++)
		if (g_cells[i])
			if (int lv = EmitLevel(g_cells[i]))
			{
				g_bl[i] = (uint8_t)lv;
				queue.push_back((uint32_t)i);
				g_blLit.push_back((uint32_t)i);
			}
	static const int d[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (size_t qi = 0; qi < queue.size(); qi++)
	{
		uint32_t i = queue[qi];
		int x = (int)(i % g_w.sx), y = (int)((i / g_w.sx) % g_w.sy), z = (int)(i / ((size_t)g_w.sx * g_w.sy));
		int lv = g_bl[i] - 1;
		if (lv <= 0)
			continue;
		for (auto& o : d)
		{
			int nx = x + o[0], ny = y + o[1], nz = z + o[2];
			if (!g_w.InBounds(nx, ny, nz))
				continue;
			size_t ni = ((size_t)nz * g_w.sy + ny) * g_w.sx + nx;
			if (g_bl[ni] >= lv || !BlockLightPasses(ni, nx, ny, nz))
				continue;
			if (!g_bl[ni])
				g_blLit.push_back((uint32_t)ni);
			g_bl[ni] = (uint8_t)lv;
			queue.push_back((uint32_t)ni);
		}
	}
	if (!mcgl::Ready())
	{
		g_blDirty = true;
		return;
	}
	// levels as 0..1 in a one-channel 3D texture (smoothed between cells by the sampler)
	static std::vector<uint8_t> px;
	px.assign(total, 0);
	for (uint32_t i : g_blLit)
		px[i] = (uint8_t)(g_bl[i] * 17);
	if (!g_blTex)
		glGenTextures(1, &g_blTex);
	glBindTexture(GL_TEXTURE_3D, g_blTex);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	if (fresh || g_blTexDims[0] != g_w.sx || g_blTexDims[1] != g_w.sy || g_blTexDims[2] != g_w.sz)
	{
		mcgl::TexImage3D(GL_TEXTURE_3D, 0, GL_R8, g_w.sx, g_w.sy, g_w.sz, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		g_blTexDims[0] = g_w.sx;
		g_blTexDims[1] = g_w.sy;
		g_blTexDims[2] = g_w.sz;
	}
	else
		mcgl::TexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, g_w.sx, g_w.sy, g_w.sz, GL_RED, GL_UNSIGNED_BYTE, px.data());
	glBindTexture(GL_TEXTURE_3D, 0);
}

// For the shaders: the texture, the grid's corner, 1 / its size in units, and the light's strength (0 = no
// block light anywhere: skip it). The strength wavers a little, like Minecraft's torch light.
bool BlockLightTexture(GLuint* tex, float org[3], float inv[3], float* strength)
{
	if (!g_loaded || !g_classicMode || !g_blTex || g_blLit.empty())
		return false;
	*tex = g_blTex;
	for (int i = 0; i < 3; i++)
		org[i] = g_w.origin[i];
	inv[0] = 1.0f / (g_w.sx * BS);
	inv[1] = 1.0f / (g_w.sy * BS);
	inv[2] = 1.0f / (g_w.sz * BS);
	double t = gEngfuncs.GetClientTime();
	*strength = 0.97f + 0.03f * (float)(sin(t * 11.3) * sin(t * 6.1));
	return true;
}

// Block light as brightness 0..1 at a point (entities, players, the hand)
static float BlockLightAt(const float* p)
{
	if (g_blLit.empty() || g_bl.empty())
		return 0.0f;
	int b[3];
	g_w.ToBlock(p, b);
	if (!g_w.InBounds(b[0], b[1], b[2]))
		return 0.0f;
	float lv = g_bl[((size_t)b[2] * g_w.sy + b[1]) * g_w.sx + b[0]] / 15.0f;
	return lv / (4.0f - 3.0f * lv);
}

float WorldLightAtPos(const float* p)
{
	if (g_loaded && g_classicMode)
	{
		float l = ClassicLightAt(p) + BlockLightAt(p);
		return l > 1.0f ? 1.0f : l;
	}
	if (!g_loaded || g_light.empty())
		return 1.0f;
	int b[3];
	g_w.ToBlock(p, b);
	if (!g_w.InBounds(b[0], b[1], b[2]))
		return 1.0f;
	if (OpaqueAt(b[0], b[1], b[2]))
		b[2]++;
	return LightAt(b[0], b[1], b[2]);
}

// ---------------------------------------------------------------------------------------------
// Meshing

#pragma pack(push, 1)
struct Vtx
{
	float x, y, z;
	float u, v, layer;
	uint8_t r, g, b, a;
};
#pragma pack(pop)

static const float kFaceShade[6] = {0.6f, 0.6f, 0.8f, 0.8f, 1.0f, 0.5f}; // +X -X +Y -Y +Z -Z

struct FaceDef
{
	int n[3];       // normal
	int c[4][3];    // corner offsets (0/1) of the unit cube, CCW seen from outside
	int u[3], v[3]; // texture axes
};
// corners for each face (outward CCW), and which axes map to u/v
static const FaceDef kFaces[6] = {
	{{1, 0, 0}, {{1, 0, 0}, {1, 1, 0}, {1, 1, 1}, {1, 0, 1}}, {0, 1, 0}, {0, 0, -1}},
	{{-1, 0, 0}, {{0, 1, 0}, {0, 0, 0}, {0, 0, 1}, {0, 1, 1}}, {0, -1, 0}, {0, 0, -1}},
	{{0, 1, 0}, {{1, 1, 0}, {0, 1, 0}, {0, 1, 1}, {1, 1, 1}}, {-1, 0, 0}, {0, 0, -1}},
	{{0, -1, 0}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {1, 0, 0}, {0, 0, -1}},
	{{0, 0, 1}, {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {1, 0, 0}, {0, -1, 0}},
	{{0, 0, -1}, {{0, 1, 0}, {1, 1, 0}, {1, 0, 0}, {0, 0, 0}}, {1, 0, 0}, {0, 1, 0}},
};

static inline uint32_t Tint(const mcw::BlockDef& d, int face)
{
	if (!strcmp(d.name, "grass_block") && face == 4)
		return 0x91BD59;
	if (!strcmp(d.name, "oak_leaves"))
		return 0x77AB2F;
	return 0xFFFFFF;
}

// Emit one face of a box (box in block-local [0,1]) of cell (x,y,z). Smooth lighting samples the
// cells in front of the face when the face lies on the cell boundary, else the cell itself.
static float g_forceLight = -1.0f; // >= 0: light every vertex with this (light-emitting blocks)

static void EmitBoxFace(std::vector<Vtx>& out, int x, int y, int z, int face, const float bmin[3], const float bmax[3], int layer,
	uint32_t tint, bool smooth)
{
	if (g_forceLight >= 0.0f)
		smooth = false;
	const FaceDef& f = kFaces[face];
	bool onBoundary = (f.n[0] > 0 && bmax[0] >= 1.0f) || (f.n[0] < 0 && bmin[0] <= 0.0f) || (f.n[1] > 0 && bmax[1] >= 1.0f) ||
		(f.n[1] < 0 && bmin[1] <= 0.0f) || (f.n[2] > 0 && bmax[2] >= 1.0f) || (f.n[2] < 0 && bmin[2] <= 0.0f);
	int fx = x + (onBoundary ? f.n[0] : 0), fy = y + (onBoundary ? f.n[1] : 0), fz = z + (onBoundary ? f.n[2] : 0);
	float shade = kFaceShade[face];
	float tr = ((tint >> 16) & 0xFF) / 255.0f, tg = ((tint >> 8) & 0xFF) / 255.0f, tb = (tint & 0xFF) / 255.0f;

	Vtx v[4];
	float ao[4];
	for (int k = 0; k < 4; k++)
	{
		float lp[3];
		for (int a = 0; a < 3; a++)
			lp[a] = f.c[k][a] ? bmax[a] : bmin[a];
		v[k].x = g_w.origin[0] + (x + lp[0]) * BS;
		v[k].y = g_w.origin[1] + (y + lp[1]) * BS;
		v[k].z = g_w.origin[2] + (z + lp[2]) * BS;
		// texture coords: project the local position onto the face's u/v axes (block pixels)
		float u = 0, vv = 0;
		for (int a = 0; a < 3; a++)
		{
			if (f.u[a])
				u = f.u[a] > 0 ? lp[a] : 1.0f - lp[a];
			if (f.v[a])
				vv = f.v[a] > 0 ? lp[a] : 1.0f - lp[a];
		}
		v[k].u = u;
		v[k].v = vv;
		v[k].layer = (float)layer;
		float light;
		if (smooth && onBoundary)
		{
			// corner direction along the two in-plane axes
			int s1[3] = {0, 0, 0}, s2[3] = {0, 0, 0};
			int ax1 = -1, ax2 = -1;
			for (int a = 0; a < 3; a++)
			{
				if (f.n[a])
					continue;
				int dir = f.c[k][a] ? 1 : -1;
				if (ax1 < 0)
				{
					ax1 = a;
					s1[a] = dir;
				}
				else
				{
					ax2 = a;
					s2[a] = dir;
				}
			}
			bool o1 = OpaqueAt(fx + s1[0], fy + s1[1], fz + s1[2]);
			bool o2 = OpaqueAt(fx + s2[0], fy + s2[1], fz + s2[2]);
			bool oc = OpaqueAt(fx + s1[0] + s2[0], fy + s1[1] + s2[1], fz + s1[2] + s2[2]);
			float sum = LightAt(fx, fy, fz);
			int n = 1;
			if (!o1)
			{
				sum += LightAt(fx + s1[0], fy + s1[1], fz + s1[2]);
				n++;
			}
			if (!o2)
			{
				sum += LightAt(fx + s2[0], fy + s2[1], fz + s2[2]);
				n++;
			}
			if (!oc && !(o1 && o2))
			{
				sum += LightAt(fx + s1[0] + s2[0], fy + s1[1] + s2[1], fz + s1[2] + s2[2]);
				n++;
			}
			light = sum / n;
			int occ = (o1 && o2) ? 3 : (int)o1 + (int)o2 + (int)oc;
			static const float aoTab[4] = {1.0f, 0.8f, 0.65f, 0.5f};
			ao[k] = aoTab[occ];
		}
		else
		{
			light = g_forceLight >= 0.0f ? g_forceLight : LightAt(fx, fy, fz);
			ao[k] = 1.0f;
		}
		float c = light * ao[k] * shade;
		v[k].r = (uint8_t)fminf(255.0f, c * tr * 255.0f);
		v[k].g = (uint8_t)fminf(255.0f, c * tg * 255.0f);
		v[k].b = (uint8_t)fminf(255.0f, c * tb * 255.0f);
		v[k].a = 255;
	}
	// flip the quad diagonal to avoid AO anisotropy
	if (ao[0] + ao[2] < ao[1] + ao[3])
	{
		out.push_back(v[1]);
		out.push_back(v[2]);
		out.push_back(v[3]);
		out.push_back(v[1]);
		out.push_back(v[3]);
		out.push_back(v[0]);
	}
	else
	{
		out.push_back(v[0]);
		out.push_back(v[1]);
		out.push_back(v[2]);
		out.push_back(v[0]);
		out.push_back(v[2]);
		out.push_back(v[3]);
	}
}

// ---------------------------------------------------------------------------------------------
// Redstone parts: small textured cuboids authored in pixels (0..16) as if standing on the floor, then
// turned to their attach side. On a classic map they are moved onto the real surface (not the grid).

static int g_lyLampOn = 0, g_lyTorchOff = 0, g_lyTorch = 0, g_lyRepeaterOn = 0, g_lyDot = 0, g_lyLine = 0, g_lyLever = 0,
	g_lyCobble = 0, g_lySmooth = 0;
static int g_tWire = -1, g_tTorch = -1, g_tLever = -1, g_tStoneButton = -1, g_tOakButton = -1, g_tStonePlate = -1,
	g_tOakPlate = -1, g_tRepeater = -1, g_tLamp = -1, g_tRsBlock = -1;

static void RegisterRedstoneLayers()
{
	g_lyLampOn = Layer("redstone_lamp_on");
	g_lyTorch = Layer("redstone_torch");
	g_lyTorchOff = Layer("redstone_torch_off");
	g_lyRepeaterOn = Layer("repeater_on");
	g_lyDot = Layer("redstone_dust_dot");
	g_lyLine = Layer("redstone_dust_line0");
	g_lyLever = Layer("lever");
	g_lyCobble = Layer("cobblestone");
	g_lySmooth = Layer("smooth_stone");
}

static void FindRedstoneTypes()
{
	g_tWire = mcw::FindBlock("redstone_wire");
	g_tTorch = mcw::FindBlock("redstone_torch");
	g_tPlainTorch = mcw::FindBlock("torch");
	g_tLever = mcw::FindBlock("lever");
	g_tStoneButton = mcw::FindBlock("stone_button");
	g_tOakButton = mcw::FindBlock("oak_button");
	g_tStonePlate = mcw::FindBlock("stone_pressure_plate");
	g_tOakPlate = mcw::FindBlock("oak_pressure_plate");
	g_tRepeater = mcw::FindBlock("repeater");
	g_tLamp = mcw::FindBlock("redstone_lamp");
	g_tRsBlock = mcw::FindBlock("redstone_block");
}

// Light level a block gives off in its current state (Minecraft: lit lamp 15, lit redstone torch 7).
static int EmitLevel(mcw::Cell c)
{
	if (!c)
		return 0;
	if (g_tWire < 0)
		FindRedstoneTypes();
	int t = mcw::CellType(c);
	if (t == g_tLamp)
		return (mcw::CellState(c) & 1) ? 15 : 0;
	if (t == g_tTorch)
		return (mcw::CellState(c) & 8) ? 0 : 7;
	if (t == g_tPlainTorch)
		return 14;
	return (mcw::Block((uint16_t)t).flags & mcw::BF_EMISSIVE) ? 15 : 0;
}

struct Xf
{
	float m[3][3];
	float t[3];
};
static Xf XfId()
{
	Xf x = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {0, 0, 0}};
	return x;
}
static Xf XfMul(const Xf& a, const Xf& b) // a after b
{
	Xf r;
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
			r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
		r.t[i] = a.m[i][0] * b.t[0] + a.m[i][1] * b.t[1] + a.m[i][2] * b.t[2] + a.t[i];
	}
	return r;
}
static Xf XfTrans(float x, float y, float z)
{
	Xf r = XfId();
	r.t[0] = x;
	r.t[1] = y;
	r.t[2] = z;
	return r;
}
// right-handed rotation about an axis (0 X, 1 Y, 2 Z) through a pivot (pixels)
static Xf XfRot(int axis, float deg, float px, float py, float pz)
{
	float a = deg * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a);
	Xf r = XfId();
	if (axis == 0)
	{
		r.m[1][1] = c;
		r.m[1][2] = -s;
		r.m[2][1] = s;
		r.m[2][2] = c;
	}
	else if (axis == 1)
	{
		r.m[0][0] = c;
		r.m[0][2] = s;
		r.m[2][0] = -s;
		r.m[2][2] = c;
	}
	else
	{
		r.m[0][0] = c;
		r.m[0][1] = -s;
		r.m[1][0] = s;
		r.m[1][1] = c;
	}
	return XfMul(XfTrans(px, py, pz), XfMul(r, XfTrans(-px, -py, -pz)));
}
// floor-authored -> attach side (0 floor, 1..4 wall at +X,+Y,-X,-Y, 5 ceiling); proper rotations
static Xf XfAttach(int attach)
{
	static const float M[6][12] = {
		{1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0},
		{0, 0, -1, 0, 1, 0, 1, 0, 0, 16, 0, 0},  // (16-z, y, x)
		{1, 0, 0, 0, 0, -1, 0, 1, 0, 0, 16, 0},  // (x, 16-z, y)
		{0, 0, 1, 0, 1, 0, -1, 0, 0, 0, 0, 16},  // (z, y, 16-x)
		{1, 0, 0, 0, 0, 1, 0, -1, 0, 0, 0, 16},  // (x, z, 16-y)
		{1, 0, 0, 0, -1, 0, 0, 0, -1, 0, 16, 16}, // (x, 16-y, 16-z)
	};
	int a = attach >= 0 && attach < 6 ? attach : 0;
	Xf r;
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
			r.m[i][j] = M[a][i * 3 + j];
		r.t[i] = M[a][9 + i];
	}
	return r;
}

// Where the supporting surface really is, relative to the cell boundary, in pixels (classic maps:
// surfaces are not on the grid). Zero next to a solid block or off classic maps.
static void SurfaceOffset(int x, int y, int z, int attach, float off[3])
{
	off[0] = off[1] = off[2] = 0.0f;
	mcc::Classic* cl = mcm::GetClassic();
	if (!g_classicMode || !cl)
		return;
	int a[3];
	mcw::AttachVec(attach, a);
	mcw::Cell sup = g_w.Get(x + a[0], y + a[1], z + a[2]);
	if (sup && mcw::CellType(sup) != cl->carvedType)
	{
		mcw::LocalBox lb[4];
		if (mcw::ShapeBoxes(g_w.ShapeAt(x + a[0], y + a[1], z + a[2]), mcw::CellState(sup), lb) > 0)
			return;
	}
	// through the middle of the cell first; a leaning wall can pass beside it, so then the nearest hit of
	// eight lines around it (the server's support test looks along the same lines, PartSupported)
	float c[3] = {g_w.origin[0] + (x + 0.5f) * BS, g_w.origin[1] + (y + 0.5f) * BS, g_w.origin[2] + (z + 0.5f) * BS};
	int u = a[0] ? 1 : 0, v = a[2] ? 1 : 2;
	float best = 1e9f;
	for (int n = 0; n < 9; n++)
	{
		static const int order[9][2] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
		float p[3] = {c[0], c[1], c[2]}, s[3], e[3], zero[3] = {0, 0, 0};
		p[u] += order[n][0] * 12.0f;
		p[v] += order[n][1] * 12.0f;
		for (int i = 0; i < 3; i++)
		{
			s[i] = p[i] - a[i] * BS * 0.45f;
			e[i] = p[i] + a[i] * BS * 1.5f;
		}
		mcc::Result r;
		cl->Trace(s, e, zero, zero, r);
		if (!r.hit || r.startsolid)
			continue;
		float d = 0.0f;
		for (int i = 0; i < 3; i++)
			if (a[i])
				d = (r.endpos[i] - (c[i] + a[i] * BS * 0.5f)) * a[i];
		if (d < best)
		{
			best = d;
			for (int i = 0; i < 3; i++)
				off[i] = a[i] ? a[i] * d / BS * 16.0f : 0.0f;
		}
		if (n == 0)
			break; // the middle line found the wall
	}
}

static inline uint8_t C8(float v) { return (uint8_t)(v <= 0.0f ? 0 : v >= 1.0f ? 255 : v * 255.0f); }

// One cuboid [lo,hi] (pixels) through transform xf. layers per face (+X -X +Y -Y +Z -Z), -1 = skip.
// vShift moves the side faces' texture rows (pixels).
static void EmitPart(std::vector<Vtx>& out, int x, int y, int z, const float lo[3], const float hi[3], const Xf& xf,
	const int layers[6], uint32_t tint, float light, bool emissive, float vShift = 0.0f)
{
	float tr = ((tint >> 16) & 0xFF) / 255.0f, tg = ((tint >> 8) & 0xFF) / 255.0f, tb = (tint & 0xFF) / 255.0f;
	for (int f = 0; f < 6; f++)
	{
		if (layers[f] < 0)
			continue;
		const FaceDef& fd = kFaces[f];
		// shade from the turned normal
		float n[3];
		for (int i = 0; i < 3; i++)
			n[i] = xf.m[i][0] * fd.n[0] + xf.m[i][1] * fd.n[1] + xf.m[i][2] * fd.n[2];
		int dom = fabsf(n[0]) > fabsf(n[1]) ? (fabsf(n[0]) > fabsf(n[2]) ? 0 : 2) : (fabsf(n[1]) > fabsf(n[2]) ? 1 : 2);
		float shade = dom == 2 ? (n[2] > 0 ? 1.0f : 0.5f) : (dom == 0 ? 0.6f : 0.8f);
		float k = emissive ? 1.0f : light * shade;
		Vtx v[4];
		for (int c = 0; c < 4; c++)
		{
			float lp[3];
			for (int a = 0; a < 3; a++)
				lp[a] = fd.c[c][a] ? hi[a] : lo[a];
			float u = 0, vv = 0;
			for (int a = 0; a < 3; a++)
			{
				if (fd.u[a])
					u = fd.u[a] > 0 ? lp[a] / 16.0f : 1.0f - lp[a] / 16.0f;
				if (fd.v[a])
					vv = fd.v[a] > 0 ? lp[a] / 16.0f : 1.0f - lp[a] / 16.0f;
			}
			if (f < 4)
				vv += vShift / 16.0f;
			float p[3];
			for (int i = 0; i < 3; i++)
				p[i] = xf.m[i][0] * lp[0] + xf.m[i][1] * lp[1] + xf.m[i][2] * lp[2] + xf.t[i];
			v[c].x = g_w.origin[0] + (x + p[0] / 16.0f) * BS;
			v[c].y = g_w.origin[1] + (y + p[1] / 16.0f) * BS;
			v[c].z = g_w.origin[2] + (z + p[2] / 16.0f) * BS;
			v[c].u = u;
			v[c].v = vv;
			v[c].layer = (float)layers[f];
			v[c].r = C8(k * tr);
			v[c].g = C8(k * tg);
			v[c].b = C8(k * tb);
			v[c].a = 255;
		}
		const int tri[6] = {0, 1, 2, 0, 2, 3};
		for (int t : tri)
			out.push_back(v[t]);
	}
}

// A flat quad (pixels), drawn from both sides, with explicit UVs.
static void EmitFlat(std::vector<Vtx>& out, int x, int y, int z, const float p[4][3], const float uv[4][2], int layer, uint32_t tint,
	float light)
{
	float tr = ((tint >> 16) & 0xFF) / 255.0f, tg = ((tint >> 8) & 0xFF) / 255.0f, tb = (tint & 0xFF) / 255.0f;
	Vtx v[4];
	for (int c = 0; c < 4; c++)
	{
		v[c].x = g_w.origin[0] + (x + p[c][0] / 16.0f) * BS;
		v[c].y = g_w.origin[1] + (y + p[c][1] / 16.0f) * BS;
		v[c].z = g_w.origin[2] + (z + p[c][2] / 16.0f) * BS;
		v[c].u = uv[c][0];
		v[c].v = uv[c][1];
		v[c].layer = (float)layer;
		v[c].r = C8(light * tr);
		v[c].g = C8(light * tg);
		v[c].b = C8(light * tb);
		v[c].a = 255;
	}
	const int tri[12] = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
	for (int t : tri)
		out.push_back(v[t]);
}

// RedStoneWireBlock colours by power
static uint32_t DustColor(int power)
{
	float f = power / 15.0f;
	float r = f * 0.6f + (f > 0.0f ? 0.4f : 0.3f);
	float g = f * f * 0.7f - 0.5f, b = f * f * 0.6f - 0.7f;
	g = g < 0 ? 0 : g;
	b = b < 0 ? 0 : b;
	return ((uint32_t)C8(r) << 16) | ((uint32_t)C8(g) << 8) | C8(b);
}

static bool ConductorCl(int x, int y, int z)
{
	mcw::Cell c = g_w.Get(x, y, z);
	return c && Opaque(c) && (int)mcw::CellType(c) != g_tRsBlock;
}

static bool IsType(int x, int y, int z, int t) { return t > 0 && (int)mcw::CellType(g_w.Get(x, y, z)) == t; }

// same rule as the server (mc_redstone.cpp DustConnects)
static bool DustConnectsCl(int x, int y, int z, int d)
{
	static const int dir[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
	int nx = x + dir[d][0], ny = y + dir[d][1];
	int t = (int)mcw::CellType(g_w.Get(nx, ny, z));
	if (t && (t == g_tWire || t == g_tTorch || t == g_tLever || t == g_tStoneButton || t == g_tOakButton || t == g_tStonePlate ||
				 t == g_tOakPlate || t == g_tRsBlock))
		return true;
	if (t && t == g_tRepeater)
		return ((mcw::CellState(g_w.Get(nx, ny, z)) & 3) & 1) == (d & 1);
	if (!ConductorCl(x, y, z + 1) && IsType(nx, ny, z + 1, g_tWire))
		return true;
	if (!ConductorCl(nx, ny, z) && IsType(nx, ny, z - 1, g_tWire))
		return true;
	return false;
}

static void DustQuad(std::vector<Vtx>& out, int x, int y, int z, float zz, float x0, float y0, float x1, float y1, int layer,
	bool alongX, uint32_t col, float light)
{
	float p[4][3] = {{x0, y0, zz}, {x1, y0, zz}, {x1, y1, zz}, {x0, y1, zz}};
	float uv[4][2];
	for (int c = 0; c < 4; c++)
	{
		float px = p[c][0] / 16.0f, py = p[c][1] / 16.0f;
		uv[c][0] = alongX ? py : px;
		uv[c][1] = alongX ? px : 1.0f - py;
	}
	EmitFlat(out, x, y, z, p, uv, layer, col, light);
}

static void MeshRedstone(std::vector<Vtx>& out, int x, int y, int z, const mcw::BlockDef& d, uint16_t type, uint16_t state)
{
	if (g_tWire < 0)
		FindRedstoneTypes();
	float light = LightAt(x, y, z);
	int attach = mcw::AttachOf(d.shape, state);
	float off[3];
	SurfaceOffset(x, y, z, attach, off);
	Xf place = XfTrans(off[0], off[1], off[2]);
	const BlockLayers& bl = g_blockLayers[type];
	switch (d.shape)
	{
	case mcw::SHAPE_DUST:
	{
		int power = state & 15;
		uint32_t col = DustColor(power);
		light += (1.0f - light) * power / 15.0f * 0.6f; // powered dust reads bright even in the shade
		float zz = 0.25f + off[2];
		int m = 0;
		for (int k = 0; k < 4; k++)
			if (DustConnectsCl(x, y, z, k))
				m |= 1 << k;
		if (m == 0)
		{
			DustQuad(out, x, y, z, zz, 0, 0, 16, 16, g_lyLine, false, col, light);
			DustQuad(out, x, y, z, zz, 0, 0, 16, 16, g_lyLine, true, col, light);
		}
		else if (m == 1 || m == 4 || m == 5)
			DustQuad(out, x, y, z, zz, 0, 0, 16, 16, g_lyLine, true, col, light);
		else if (m == 2 || m == 8 || m == 10)
			DustQuad(out, x, y, z, zz, 0, 0, 16, 16, g_lyLine, false, col, light);
		else
		{
			DustQuad(out, x, y, z, zz, 0, 0, 16, 16, g_lyDot, false, col, light);
			if (m & 1)
				DustQuad(out, x, y, z, zz, 8, 0, 16, 16, g_lyLine, true, col, light);
			if (m & 4)
				DustQuad(out, x, y, z, zz, 0, 0, 8, 16, g_lyLine, true, col, light);
			if (m & 2)
				DustQuad(out, x, y, z, zz, 0, 8, 16, 16, g_lyLine, false, col, light);
			if (m & 8)
				DustQuad(out, x, y, z, zz, 0, 0, 16, 8, g_lyLine, false, col, light);
		}
		// climbing up the side of a block to dust on top of it
		static const int dir[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
		for (int k = 0; k < 4; k++)
		{
			int nx = x + dir[k][0], ny = y + dir[k][1];
			if (!ConductorCl(nx, ny, z) || ConductorCl(x, y, z + 1) || !IsType(nx, ny, z + 1, g_tWire))
				continue;
			const float e = 15.75f, s = 0.25f;
			static const float uv[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
			float p[4][3];
			const float q[4][4][3] = {
				{{e, 0, 0}, {e, 16, 0}, {e, 16, 16}, {e, 0, 16}},
				{{16, e, 0}, {0, e, 0}, {0, e, 16}, {16, e, 16}},
				{{s, 16, 0}, {s, 0, 0}, {s, 0, 16}, {s, 16, 16}},
				{{0, s, 0}, {16, s, 0}, {16, s, 16}, {0, s, 16}},
			};
			memcpy(p, q[k], sizeof(p));
			EmitFlat(out, x, y, z, p, uv, g_lyLine, col, light);
		}
		break;
	}
	case mcw::SHAPE_TORCH:
	{
		// the plain torch is the same model with its own texture, always lit
		bool plain = (int)type != g_tTorch;
		bool lit = plain || !(state & 8);
		int ly = plain ? g_blockLayers[type].side : lit ? g_lyTorch : g_lyTorchOff;
		const int layers[6] = {ly, ly, ly, ly, ly, -1};
		const float lo[3] = {7, 7, 0}, hi[3] = {9, 9, 10};
		Xf xf = XfId();
		if (attach >= 1 && attach <= 4)
		{
			// leaning 22.5 degrees out from the wall, base 3.5 px up the wall (torch_wall model)
			static const float pivot[5][3] = {{8, 8, 0}, {16, 8, 3.5f}, {8, 16, 3.5f}, {0, 8, 3.5f}, {8, 0, 3.5f}};
			int axis = (attach == 1 || attach == 3) ? 1 : 0;
			float ang = attach == 1 ? -22.5f : attach == 3 ? 22.5f : attach == 2 ? 22.5f : -22.5f;
			xf = XfMul(XfTrans(pivot[attach][0] - 8, pivot[attach][1] - 8, pivot[attach][2]), XfRot(axis, ang, 8, 8, 0));
		}
		EmitPart(out, x, y, z, lo, hi, XfMul(place, xf), layers, 0xFFFFFF, light, lit);
		break;
	}
	case mcw::SHAPE_LEVER:
	{
		bool on = (state & 8) != 0;
		bool alongY = (attach == 0 || attach == 5) ? (state & 16) != 0 : (attach == 2 || attach == 4);
		Xf base = XfMul(place, XfAttach(attach));
		if (!alongY)
			base = XfMul(base, XfRot(2, 90.0f, 8, 8, 0));
		const int lb[6] = {g_lyCobble, g_lyCobble, g_lyCobble, g_lyCobble, g_lyCobble, g_lyCobble};
		const float blo[3] = {5, 4, 0}, bhi[3] = {11, 12, 3};
		EmitPart(out, x, y, z, blo, bhi, base, lb, 0xFFFFFF, light, false);
		const int hl[6] = {g_lyLever, g_lyLever, g_lyLever, g_lyLever, g_lyLever, -1};
		const float hlo[3] = {7, 7, 1}, hhi[3] = {9, 9, 11};
		EmitPart(out, x, y, z, hlo, hhi, XfMul(base, XfRot(0, on ? 45.0f : -45.0f, 8, 8, 1)), hl, 0xFFFFFF, light, false, 1.0f);
		break;
	}
	case mcw::SHAPE_BUTTON:
	{
		bool alongY = (attach == 0 || attach == 5) ? (state & 16) != 0 : (attach == 1 || attach == 3);
		Xf xf = XfMul(place, XfAttach(attach));
		if (alongY)
			xf = XfMul(xf, XfRot(2, 90.0f, 8, 8, 0));
		const float lo[3] = {5, 6, 0}, hi[3] = {11, 10, (state & 8) ? 1.0f : 2.0f};
		const int ly[6] = {bl.side, bl.side, bl.side, bl.side, bl.top, bl.bottom};
		EmitPart(out, x, y, z, lo, hi, xf, ly, 0xFFFFFF, light, false);
		break;
	}
	case mcw::SHAPE_PLATE:
	{
		const float lo[3] = {1, 1, 0}, hi[3] = {15, 15, (state & 1) ? 0.5f : 1.0f};
		const int ly[6] = {bl.side, bl.side, bl.side, bl.side, bl.top, -1};
		EmitPart(out, x, y, z, lo, hi, place, ly, 0xFFFFFF, light, false);
		break;
	}
	case mcw::SHAPE_REPEATER:
	{
		int facing = state & 3, delay = ((state >> 2) & 3) + 1;
		bool on = (state & 16) != 0;
		// authored with the output towards +Y
		Xf xf = XfMul(place, XfRot(2, (facing - 1) * 90.0f, 8, 8, 0));
		const int ly[6] = {g_lySmooth, g_lySmooth, g_lySmooth, g_lySmooth, on ? g_lyRepeaterOn : bl.top, g_lySmooth};
		const float lo[3] = {0, 0, 0}, hi[3] = {16, 16, 2};
		EmitPart(out, x, y, z, lo, hi, xf, ly, 0xFFFFFF, light, false);
		int tl = on ? g_lyTorch : g_lyTorchOff;
		const int tly[6] = {tl, tl, tl, tl, tl, -1};
		const float t1lo[3] = {7, 12, 2}, t1hi[3] = {9, 14, 7};
		EmitPart(out, x, y, z, t1lo, t1hi, xf, tly, 0xFFFFFF, light, on, -3.0f);
		float y0 = 8.0f - 2.0f * (delay - 1);
		const float t2lo[3] = {7, y0, 2}, t2hi[3] = {9, y0 + 2, 7};
		EmitPart(out, x, y, z, t2lo, t2hi, xf, tly, 0xFFFFFF, light, on, -3.0f);
		break;
	}
	default:
		break;
	}
}

static int LayerFor(const mcw::BlockDef& d, uint16_t type, uint16_t state, int face)
{
	if ((int)type == g_tLamp && (state & 1))
		return g_lyLampOn;
	const BlockLayers& bl = g_blockLayers[type];
	if (d.shape == mcw::SHAPE_DOOR)
		return (state & 8) ? bl.sideTop : bl.side;
	if (face == 4)
		return bl.top;
	if (face == 5)
		return bl.bottom;
	return bl.side;
}

static void MeshChunk(int cx, int cy, int cz, std::vector<Vtx>& out)
{
	if (g_tWire < 0)
		FindRedstoneTypes();
	static const int off[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	for (int z = cz * CH; z < (cz + 1) * CH && z < g_w.sz; z++)
		for (int y = cy * CH; y < (cy + 1) * CH && y < g_w.sy; y++)
			for (int x = cx * CH; x < (cx + 1) * CH && x < g_w.sx; x++)
			{
				mcw::Cell c = g_w.Get(x, y, z);
				if (!c)
					continue;
				uint16_t type = mcw::CellType(c), state = mcw::CellState(c);
				if (type >= g_blockLayers.size())
					continue;
				const mcw::BlockDef& d = mcw::Block(type);
				if (d.shape == mcw::SHAPE_CROSS)
				{
					// two crossed quads, double sided, slightly lower than a full block like plants
					int layer = g_blockLayers[type].side;
					float l = LightAt(x, y, z);
					uint8_t col = (uint8_t)(l * 255.0f);
					float x0 = g_w.origin[0] + x * BS, y0 = g_w.origin[1] + y * BS, z0 = g_w.origin[2] + z * BS;
					const float q[2][4][2] = {{{0.15f, 0.15f}, {0.85f, 0.85f}, {0.85f, 0.85f}, {0.15f, 0.15f}}, {{0.15f, 0.85f}, {0.85f, 0.15f}, {0.85f, 0.15f}, {0.15f, 0.85f}}};
					for (int s = 0; s < 2; s++)
					{
						Vtx v[4];
						for (int k = 0; k < 4; k++)
						{
							v[k].x = x0 + q[s][k][0] * BS;
							v[k].y = y0 + q[s][k][1] * BS;
							v[k].z = z0 + ((k == 2 || k == 3) ? BS : 0.0f);
							v[k].u = (k == 0 || k == 3) ? 0.0f : 1.0f;
							v[k].v = (k == 2 || k == 3) ? 0.0f : 1.0f;
							v[k].layer = (float)layer;
							v[k].r = v[k].g = v[k].b = col;
							v[k].a = 255;
						}
						const int tri[12] = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
						for (int t : tri)
							out.push_back(v[t]);
					}
					continue;
				}
				if (mcw::IsRedstoneShape(d.shape))
				{
					MeshRedstone(out, x, y, z, d, type, state);
					continue;
				}
				if (d.shape == mcw::SHAPE_TABLE)
				{
					// the enchanting table, 12 px high, standing on the floor under it (on a classic map the
					// floor is not on the grid: SurfaceOffset finds it)
					float off[3];
					SurfaceOffset(x, y, z, 0, off);
					const BlockLayers& tl = g_blockLayers[type];
					const int layers[6] = {tl.side, tl.side, tl.side, tl.side, tl.top, tl.bottom};
					const float lo[3] = {0, 0, 0}, hi[3] = {16, 16, 12};
					EmitPart(out, x, y, z, lo, hi, XfTrans(off[0], off[1], off[2]), layers, 0xFFFFFF, LightAt(x, y, z), false);
					continue;
				}
				if (d.shape == mcw::SHAPE_FIRE)
				{
					// Minecraft's fire on a floor: a sheet of flame leaning in from each side of the cell and
					// two crossed ones through the middle, taller than the cell, full bright, seen from both sides
					float x0 = g_w.origin[0] + x * BS, y0 = g_w.origin[1] + y * BS, z0 = g_w.origin[2] + z * BS;
					auto sheet = [&](float ax, float ay, float bx, float by, float inX, float inY, float h, int layer) {
						// bottom edge a -> b on the floor, top edge moved in by (inX, inY)
						const float p[4][3] = {{ax, ay, 0}, {bx, by, 0}, {bx + inX, by + inY, h}, {ax + inX, ay + inY, h}};
						Vtx v[4];
						for (int k = 0; k < 4; k++)
						{
							v[k].x = x0 + p[k][0] * BS;
							v[k].y = y0 + p[k][1] * BS;
							v[k].z = z0 + p[k][2] * BS;
							v[k].u = (k == 0 || k == 3) ? 0.0f : 1.0f;
							v[k].v = (k == 2 || k == 3) ? 0.0f : 1.0f;
							v[k].layer = (float)layer;
							v[k].r = v[k].g = v[k].b = v[k].a = 255;
						}
						const int tri[12] = {0, 1, 2, 0, 2, 3, 0, 2, 1, 0, 3, 2};
						for (int t : tri)
							out.push_back(v[t]);
					};
					const BlockLayers& bl = g_blockLayers[type];
					const float e = 0.02f, in = 0.3f, h = 1.4f;
					sheet(e, 0, e, 1, in, 0, h, bl.side);
					sheet(1 - e, 1, 1 - e, 0, -in, 0, h, bl.side);
					sheet(1, e, 0, e, 0, in, h, bl.side);
					sheet(0, 1 - e, 1, 1 - e, 0, -in, h, bl.side);
					sheet(0, 0, 1, 1, 0, 0, h, bl.bottom);
					sheet(0, 1, 1, 0, 0, 0, h, bl.bottom);
					continue;
				}
				// a lit lamp is a light source: full bright like in Minecraft (also on classic maps)
				g_forceLight = ((int)type == g_tLamp && (state & 1)) ? 1.0f : -1.0f;
				mcw::LocalBox boxes[4];
				int nb = mcw::ShapeBoxes(d.shape, state, boxes);
				if (d.shape == mcw::SHAPE_CUBE)
					nb = 1, boxes[0] = {{0, 0, 0}, {1, 1, 1}};
				for (int bi = 0; bi < nb; bi++)
				{
					const mcw::LocalBox& b = boxes[bi];
					for (int f = 0; f < 6; f++)
					{
						// cull faces on the cell boundary hidden by an opaque full neighbour
						bool boundary = (f == 0 && b.maxs[0] >= 1.0f) || (f == 1 && b.mins[0] <= 0.0f) || (f == 2 && b.maxs[1] >= 1.0f) ||
							(f == 3 && b.mins[1] <= 0.0f) || (f == 4 && b.maxs[2] >= 1.0f) || (f == 5 && b.mins[2] <= 0.0f);
						if (boundary && OpaqueAt(x + off[f][0], y + off[f][1], z + off[f][2]))
							continue;
						if (boundary && d.shape == mcw::SHAPE_CUBE && (d.flags & mcw::BF_TRANSPARENT))
						{
							// glass next to the same glass: hide the shared face
							mcw::Cell nc = g_w.Get(x + off[f][0], y + off[f][1], z + off[f][2]);
							if (mcw::CellType(nc) == type)
								continue;
						}
						EmitBoxFace(out, x, y, z, f, b.mins, b.maxs, LayerFor(d, type, state, f), Tint(d, f), true);
					}
				}
				g_forceLight = -1.0f;
			}
}

static void RebuildDirty(int budget)
{
	std::vector<Vtx> verts;
	for (size_t i = 0; i < g_chunks.size() && budget > 0; i++)
	{
		Chunk& ch = g_chunks[i];
		if (!ch.dirty)
			continue;
		int cx = (int)(i % g_ncx), cy = (int)((i / g_ncx) % g_ncy), cz = (int)(i / ((size_t)g_ncx * g_ncy));
		verts.clear();
		MeshChunk(cx, cy, cz, verts);
		if (!ch.vbo)
			mcgl::GenBuffers(1, &ch.vbo);
		mcgl::BindBuffer(GL_ARRAY_BUFFER, ch.vbo);
		mcgl::BufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vtx), verts.empty() ? nullptr : verts.data(), GL_STATIC_DRAW);
		ch.count = (int)verts.size();
		ch.dirty = false;
		budget--;
	}
}

// ---------------------------------------------------------------------------------------------
// Drawing

static void DrawOutlineAndCracks()
{
	mcgl::UseProgram(0);
	// crack overlays
	for (int i = 0; i <= 32; i++)
	{
		Breaking& b = g_breaking[i];
		if (b.stage < 0 || !g_w.InBounds(b.x, b.y, b.z))
			continue;
		mcw::Cell c = g_w.Get(b.x, b.y, b.z);
		if (!c)
		{
			b.stage = -1;
			continue;
		}
		char tex[64];
		snprintf(tex, sizeof(tex), "block/destroy_stage_%d", b.stage);
		mctex::Bind(tex);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
		glDepthMask(GL_FALSE);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-3.0f, -3.0f);
		glColor4f(1, 1, 1, 1);
		mcw::LocalBox boxes[4];
		const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
		int nb = mcw::PickBoxes(d.shape, mcw::CellState(c), boxes);
		if (nb == 0)
			nb = 1, boxes[0] = {{0, 0, 0}, {1, 1, 1}};
		glBegin(GL_QUADS);
		for (int bi = 0; bi < nb; bi++)
			for (int f = 0; f < 6; f++)
			{
				const FaceDef& fd = kFaces[f];
				for (int k = 0; k < 4; k++)
				{
					float lp[3];
					for (int a = 0; a < 3; a++)
						lp[a] = fd.c[k][a] ? boxes[bi].maxs[a] : boxes[bi].mins[a];
					float u = 0, vv = 0;
					for (int a = 0; a < 3; a++)
					{
						if (fd.u[a])
							u = fd.u[a] > 0 ? lp[a] : 1.0f - lp[a];
						if (fd.v[a])
							vv = fd.v[a] > 0 ? lp[a] : 1.0f - lp[a];
					}
					glTexCoord2f(u, vv);
					glVertex3f(g_w.origin[0] + (b.x + lp[0]) * BS, g_w.origin[1] + (b.y + lp[1]) * BS, g_w.origin[2] + (b.z + lp[2]) * BS);
				}
			}
		glEnd();
		glDisable(GL_POLYGON_OFFSET_FILL);
		glDepthMask(GL_TRUE);
	}

	// targeted block outline (Minecraft: black lines, 40% alpha)
	if (!g_cl.mcItemActive || g_cl.health <= 0)
		return;
	int b[3], face;
	float dist;
	bool classicHit = false;
	if (!mcm::WorldPick(g_cl.vieworg, g_cl.forward, mci::BLOCK_REACH, b, &face, &dist, &classicHit))
		return;
	mcw::Cell c = g_w.Get(b[0], b[1], b[2]);
	const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
	mcw::LocalBox boxes[4];
	int nb = mcw::PickBoxes(d.shape, mcw::CellState(c), boxes);
	if (classicHit)
		nb = 1, boxes[0] = {{0, 0, 0}, {1, 1, 1}}; // a cell of the classic map
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(0, 0, 0, 0.4f);
	glLineWidth(2.0f);
	glDepthMask(GL_FALSE);
	const float e = 0.002f;
	glBegin(GL_LINES);
	for (int bi = 0; bi < nb; bi++)
	{
		float mn[3], mx[3];
		for (int a = 0; a < 3; a++)
		{
			mn[a] = g_w.origin[a] + (b[a] + boxes[bi].mins[a] - e) * BS;
			mx[a] = g_w.origin[a] + (b[a] + boxes[bi].maxs[a] + e) * BS;
		}
		float cs[8][3];
		for (int k = 0; k < 8; k++)
		{
			cs[k][0] = (k & 1) ? mx[0] : mn[0];
			cs[k][1] = (k & 2) ? mx[1] : mn[1];
			cs[k][2] = (k & 4) ? mx[2] : mn[2];
		}
		static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
		for (auto& ed : edges)
		{
			glVertex3fv(cs[ed[0]]);
			glVertex3fv(cs[ed[1]]);
		}
	}
	glEnd();
	glDepthMask(GL_TRUE);
	glEnable(GL_TEXTURE_2D);
}

// Minecraft "fast" clouds: textures/environment/clouds.png, one texel = 12 blocks, drifting west.
void WorldDrawClouds()
{
	if (!g_loaded || g_classicMode)
		return;
	const mctex::Tex& t = mctex::Get("environment/clouds");
	if (!t.id)
		return;
	glBindTexture(GL_TEXTURE_2D, t.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	float z = g_w.origin[2] + g_w.sz * BS + 520.0f;
	float cx = g_cl.vieworg[0], cy = g_cl.vieworg[1];
	const float R = 12000.0f;
	const float texel = 12.0f * BS; // units per cloud texel
	float drift = (float)(g_cl.time * 20.0 * 0.03 * BS); // 0.03 blocks/tick
	auto uv = [&](float x, float y, float& u, float& v) {
		u = (x + drift) / (texel * t.w);
		v = y / (texel * t.h);
	};
	glColor4f(1.0f, 1.0f, 1.0f, 0.8f);
	glBegin(GL_QUADS);
	float u, v;
	uv(cx - R, cy - R, u, v); glTexCoord2f(u, v); glVertex3f(cx - R, cy - R, z);
	uv(cx + R, cy - R, u, v); glTexCoord2f(u, v); glVertex3f(cx + R, cy - R, z);
	uv(cx + R, cy + R, u, v); glTexCoord2f(u, v); glVertex3f(cx + R, cy + R, z);
	uv(cx - R, cy + R, u, v); glTexCoord2f(u, v); glVertex3f(cx - R, cy + R, z);
	glEnd();
	glDepthMask(GL_TRUE);
	glBindTexture(GL_TEXTURE_2D, t.id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void WorldDraw()
{
	if (!g_loaded)
		return;
	BlockLightUpdate();
	if (g_classicMode)
		ClassicDraw();
	InitGL();
	if (!g_prog || !g_texArray)
		return;
	if (g_lightDirty)
		ComputeLight();
	RebuildDirty(64);

	mcgl::UseProgram(g_prog);
	if (g_vao)
		mcgl::BindVertexArray(g_vao);
	mcgl::ActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, g_texArray);
	AnimateFire();
	mcgl::Uniform1i(u_tex, 0);
	{
		GLuint bl = 0;
		float org[3], inv[3], on = 0.0f;
		if (BlockLightTexture(&bl, org, inv, &on))
		{
			mcgl::ActiveTexture(GL_TEXTURE2);
			glBindTexture(GL_TEXTURE_3D, bl);
			mcgl::ActiveTexture(GL_TEXTURE0);
			mcgl::Uniform3f(u_blOrg, org[0], org[1], org[2]);
			mcgl::Uniform3f(u_blInv, inv[0], inv[1], inv[2]);
		}
		else
			on = 0.0f;
		mcgl::Uniform1i(u_bl, 2);
		mcgl::Uniform1f(u_blOn, on);
	}
	mcgl::Uniform3f(u_fogColor, 0.75f, 0.85f, 1.0f);
	mcgl::Uniform1f(u_fogStart, 3000.0f);
	mcgl::Uniform1f(u_fogEnd, 9000.0f);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glDisable(GL_ALPHA_TEST);
	glEnable(GL_CULL_FACE);
	glCullFace(GL_BACK);
	glFrontFace(GL_CCW);

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
		mcgl::VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vtx), (void*)24);
		glDrawArrays(GL_TRIANGLES, 0, ch.count);
	}
	if (mcgl::DisableVertexAttribArray)
	{
		mcgl::DisableVertexAttribArray(0);
		mcgl::DisableVertexAttribArray(1);
		mcgl::DisableVertexAttribArray(2);
	}
	mcgl::BindBuffer(GL_ARRAY_BUFFER, 0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
	glDisable(GL_CULL_FACE);
	DrawOutlineAndCracks();
}
} // namespace mc
