// Server voxel world: loading, engine trace wrappers, block edits, mining, placing, doors, explosions,
// and streaming block changes to clients.
#include "precompiled.h"

#include "mc_server.h"
#include "mc_blocks.h"
#include "mc_move.h"
#include "mc_classic.h"
#include "mc_items.h"
#include "mc_enchant.h"

#include <unordered_map>
#include <vector>

float StepHeight = 18.0f; // ReGameDLL bot nav step height (nav.h, patched to extern)

namespace mc
{
mcw::World g_world;
bool g_worldLoaded = false;
static std::vector<mcw::Cell> g_cells;
static std::vector<mcw::Cell> g_originalCells; // as loaded, for the per-round reset
static std::unordered_map<uint32_t, mcw::Cell> g_changed; // flat index -> cell, every edit since load
struct PendingVox
{
	int16_t x, y, z;
	mcw::Cell c;
};
static std::vector<PendingVox> g_pending;
static uint32_t g_worldCrc = 0;
// classic mode: a real CS map is the world (see mc_classic.h)
static mcc::Classic g_classic;
static bool g_classicMode = false;
bool ClassicMode() { return g_classicMode; }
mcc::Classic* ClassicWorld() { return g_classicMode ? &g_classic : nullptr; }
// no blocks this near the other side's spawn points, in units (0: anywhere)
static cvar_t cv_spawnGuard = {"mc_spawn_guard", "320", FCVAR_SERVER, 320.0f, nullptr};
// ... for so many seconds from the start of a round: afterwards it is ground like any other
static cvar_t cv_spawnGuardTime = {"mc_spawn_guard_time", "25", FCVAR_SERVER, 25.0f, nullptr};
static std::vector<Vector> g_spawns[2]; // the Terrorists' spawn points, the Counter-Terrorists'
static bool g_spawnsKnown = false;
static std::unordered_map<uint32_t, uint16_t> g_owner; // flat index -> who set the block down: side, and player index << 8
// the planted bomb, remembered from the plant (Counter-Strike's search by class name does not find it)
static edict_t* g_bombEd = nullptr;
static int g_bombSerial = 0;
static bool g_blastThroughAll = false; // (a wither's blue skull: every block but bedrock is as soft as dirt to it)

extern int MsgVox();
extern int MsgBreak();
extern int MsgHello();
extern void PrimeTnt(const float* origin, int fuse);
extern void SpawnFallingBlock(int x, int y, int z, mcw::Cell cell);
static bool HullBlocksPlace(int x, int y, int z);

static const float B2U = 40.0f;

// ---------------------------------------------------------------------------------------------
// Engine trace wrappers: every trace the game DLL makes (bullets, bot vision, grenades, use, ...)
// also collides with voxels.

static void (*o_TraceLine)(const float*, const float*, int, edict_t*, TraceResult*);
static void (*o_TraceHull)(const float*, const float*, int, int, edict_t*, TraceResult*);
static int (*o_TraceMonsterHull)(edict_t*, const float*, const float*, int, edict_t*, TraceResult*);
static int (*o_PointContents)(const float*);

// (what the last Merge stopped at, when that was the mod's world: for the bullet rules below)
static struct
{
	bool hit = false;
	Vector pos;
	mcw::Cell cell = 0;
} g_mergeHit;

static void Merge(TraceResult* ptr, const float* v1, const float* v2, const float mins[3], const float maxs[3])
{
	if (!g_worldLoaded)
		return;
	mcw::Trace vt;
	mcm::WorldTrace(v1, v2, mins, maxs, vt); // placed blocks + the classic map
	if (vt.startsolid)
		ptr->fStartSolid = TRUE;
	if (vt.allsolid)
	{
		ptr->fAllSolid = TRUE;
		ptr->flFraction = 0.0f;
		ptr->vecEndPos = Vector(v1[0], v1[1], v1[2]);
		ptr->pHit = INDEXENT(0);
		return;
	}
	if (vt.hit && vt.fraction < ptr->flFraction)
	{
		g_mergeHit.hit = true;
		g_mergeHit.pos = Vector(vt.endpos[0], vt.endpos[1], vt.endpos[2]);
		g_mergeHit.cell = vt.cell;
		ptr->flFraction = vt.fraction;
		ptr->vecEndPos = Vector(vt.endpos[0], vt.endpos[1], vt.endpos[2]);
		ptr->vecPlaneNormal = Vector(vt.normal[0], vt.normal[1], vt.normal[2]);
		ptr->flPlaneDist = DotProduct(ptr->vecEndPos, ptr->vecPlaneNormal);
		ptr->pHit = INDEXENT(0);
		ptr->iHitgroup = 0;
		ptr->fInOpen = FALSE;
	}
}

// ---------------------------------------------------------------------------------------------
// Bullets and blocks. What a block is made of decides what a bullet does at it:
//  - what a pickaxe is for (stone, bricks, obsidian, metal): the bullet stops. No gun goes through.
//  - what an axe is for (planks, logs, wooden doors, crates): Counter-Strike's rule for wood. A gun that
//    goes through walls (the rifles, the Deagle) comes out the other side with 60% of its damage; pistols,
//    submachine guns and buckshot stop.
//  - glass: the bullet goes on and the glass shatters. Iron bars: the bullet goes on.
//  - the rest (wool, leaves, hay, and on a classic map sand, gravel and dirt): the bullet goes on as if
//    nothing were there. They hide, they do not protect.
// A look that Counter-Strike lets through glass (a bot's eyes) goes through glass blocks and iron bars too.
enum
{
	BM_HARD = mcw::BULLET_STOPS,
	BM_WOOD = mcw::BULLET_WOOD,
	BM_SOFT = mcw::BULLET_PASSES,
	BM_GLASS = mcw::BULLET_SHATTERS,
	BM_BARS = mcw::BULLET_BARS
};
static uint8_t g_bulletMat[1024], g_passBullet[1024], g_passSight[1024];
static int g_matBlocks = -1;
static bool g_matClassic = false;

static int BulletMaterialOf(const mcw::BlockDef& d) { return mcw::BulletClassOf(d, g_classicMode); }

static void MatTables()
{
	// (a classic map registers a block for each of its textures when it loads)
	if (g_matBlocks == mcw::g_numDynBlocks && g_matClassic == g_classicMode)
		return;
	g_matBlocks = mcw::g_numDynBlocks;
	g_matClassic = g_classicMode;
	memset(g_bulletMat, 0, sizeof(g_bulletMat));
	memset(g_passBullet, 0, sizeof(g_passBullet));
	memset(g_passSight, 0, sizeof(g_passSight));
	for (int i = 1; i < 1024; i++)
	{
		if (i >= mcw::g_numBlocks && !(i >= mcw::DYN_BLOCK_BASE && i < mcw::DYN_BLOCK_BASE + mcw::g_numDynBlocks))
			continue;
		int m = BulletMaterialOf(mcw::Block((uint16_t)i));
		g_bulletMat[i] = (uint8_t)m;
		g_passBullet[i] = m == BM_SOFT || m == BM_GLASS || m == BM_BARS;
		g_passSight[i] = m == BM_GLASS || m == BM_BARS;
	}
}

// 0 stops every bullet, 1 lets the guns through that go through walls, 2 stops none (the shop says which)
int BlockBulletClass(const char* blockName)
{
	int type = blockName ? mcw::FindBlock(blockName) : -1;
	if (type < 0)
		return 0;
	int m = BulletMaterialOf(mcw::Block((uint16_t)type));
	return m == BM_HARD ? 0 : m == BM_WOOD ? 1 : 2;
}

// What the last bullet's trace stopped at, if that was a block: Counter-Strike asks about it right after
// (the texture under the hit, and whether the bullet may go on through it).
static struct
{
	bool valid = false;
	float start[3];
	Vector pos;
	int mat = BM_HARD;
} g_bulletStop;
void BreakBlock(int x, int y, int z, CBasePlayer* by, bool drop);

// The glass a bullet went through on its way from a to b
static void ShatterGlass(const Vector& a, const Vector& b)
{
	float o[3], d[3], tMax[3], tDelta[3];
	int c[3], step[3];
	for (int k = 0; k < 3; k++)
	{
		o[k] = (a[k] - g_world.origin[k]) / B2U;
		d[k] = (b[k] - a[k]) / B2U;
		c[k] = (int)floorf(o[k]);
		step[k] = d[k] > 0.0f ? 1 : -1;
		if (fabsf(d[k]) < 1e-6f)
			tMax[k] = tDelta[k] = 1e30f;
		else
		{
			tMax[k] = ((d[k] > 0.0f ? c[k] + 1 : c[k]) - o[k]) / d[k];
			tDelta[k] = (float)step[k] / d[k];
		}
	}
	for (int guard = 0; guard < 700; guard++)
	{
		mcw::Cell cell = g_world.Get(c[0], c[1], c[2]);
		if (cell && g_bulletMat[mcw::CellType(cell) & 1023] == BM_GLASS)
			BreakBlock(c[0], c[1], c[2], nullptr, false);
		int k = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
		if (tMax[k] > 1.0f)
			break;
		c[k] += step[k];
		tMax[k] += tDelta[k];
	}
}

// whose bullet is in the air (a hit dealt in the same frame by that player with DMG_BULLET is his bullet's)
static edict_t* g_bulletBy = nullptr;
static float g_bulletTime = -1.0f;
bool WasBulletOf(entvars_t* shooter) { return shooter && g_bulletBy && shooter == &g_bulletBy->v && g_bulletTime == gpGlobals->time; }

static void W_TraceLine(const float* v1, const float* v2, int noMonsters, edict_t* skip, TraceResult* ptr)
{
	MC_WHERE("an engine line trace through the mod's world");
	// Counter-Strike marks the traces its bullets make (FireBullets, FireBuckshots, FireBullets3)
	bool bullet = (gpGlobals->trace_flags & FTRACE_BULLET) != 0;
	if (bullet)
	{
		g_bulletBy = skip;
		g_bulletTime = gpGlobals->time;
	}
	bool throughGlass = (noMonsters & 0x100) != 0;
	// players drawn as Minecraft models are hit where the model is, not where CS's hidden one is (mc_hitbox.cpp)
	bool rigs = (noMonsters & 0xFF) != ignore_monsters && HitRigsHide(skip);
	o_TraceLine(v1, v2, noMonsters, skip, ptr);
	gpGlobals->trace_flags &= ~FTRACE_BULLET;
	if (rigs)
		HitRigsRestore();
	static const float zero[3] = {0, 0, 0};
	if (g_worldLoaded && (bullet || throughGlass))
	{
		MatTables();
		mcw::g_tracePass = bullet ? g_passBullet : g_passSight;
	}
	g_mergeHit.hit = false;
	Merge(ptr, v1, v2, zero, zero);
	mcw::g_tracePass = nullptr;
	if (rigs)
		HitRigsTrace(v1, v2, ptr);
	if (!bullet || !g_worldLoaded)
		return;
	g_bulletStop.valid = false;
	if (g_mergeHit.hit && g_mergeHit.cell && ptr->pHit == INDEXENT(0) && (ptr->vecEndPos - g_mergeHit.pos).Length() < 0.5f)
	{
		g_bulletStop.valid = true;
		g_bulletStop.pos = g_mergeHit.pos;
		g_bulletStop.mat = g_bulletMat[mcw::CellType(g_mergeHit.cell) & 1023];
		for (int k = 0; k < 3; k++)
			g_bulletStop.start[k] = v1[k];
	}
	ShatterGlass(Vector(v1[0], v1[1], v1[2]), ptr->vecEndPos);
}

// May the bullet go on through what it hit? Not through a block a pickaxe is for.
bool H_IsPenetrable(IReGameHook_IsPenetrableEntity* chain, Vector& src, Vector& end, entvars_t* attacker, edict_t* hit)
{
	if (g_bulletStop.valid && g_bulletStop.mat == BM_HARD && (end - g_bulletStop.pos).Length() < 1.0f)
		return false;
	return chain->callNext(src, end, attacker, hit);
}

static void HullSize(int hull, float mins[3], float maxs[3])
{
	static const float hm[4][3] = {{0, 0, 0}, {-16, -16, -36}, {-32, -32, -32}, {-16, -16, -18}};
	static const float hx[4][3] = {{0, 0, 0}, {16, 16, 36}, {32, 32, 32}, {16, 16, 18}};
	if (hull < 0 || hull > 3)
		hull = 0;
	for (int i = 0; i < 3; i++)
	{
		mins[i] = hm[hull][i];
		maxs[i] = hx[hull][i];
	}
}

static void W_TraceHull(const float* v1, const float* v2, int noMonsters, int hull, edict_t* skip, TraceResult* ptr)
{
	MC_WHERE("an engine hull trace through the mod's world");
	o_TraceHull(v1, v2, noMonsters, hull, skip, ptr);
	float mins[3], maxs[3];
	HullSize(hull, mins, maxs);
	Merge(ptr, v1, v2, mins, maxs);
}

static int W_TraceMonsterHull(edict_t* ent, const float* v1, const float* v2, int noMonsters, edict_t* skip, TraceResult* ptr)
{
	int r = o_TraceMonsterHull(ent, v1, v2, noMonsters, skip, ptr);
	if (ent)
		Merge(ptr, v1, v2, ent->v.mins, ent->v.maxs);
	return (ptr->fAllSolid || ptr->fStartSolid || ptr->flFraction < 1.0f) ? 1 : r;
}

static int W_PointContents(const float* v)
{
	if (g_worldLoaded && mcm::WorldPointSolid(v))
		return CONTENTS_SOLID;
	return o_PointContents(v);
}

// Bullet penetration and impact sounds read the texture under a trace; on a classic map the engine only
// knows the empty shell, so answer with the classic surface.
static const char* (*o_TraceTexture)(edict_t*, const float*, const float*);
static const char* W_TraceTexture(edict_t* ent, const float* v1, const float* v2)
{
	// the bullet that has just stopped at a block: wood counts as Counter-Strike's wood, the rest as concrete
	if (g_bulletStop.valid && v1[0] == g_bulletStop.start[0] && v1[1] == g_bulletStop.start[1] && v1[2] == g_bulletStop.start[2] &&
		(!ent || ent == INDEXENT(0)))
		return g_bulletStop.mat == BM_WOOD ? "64crate0" : "";
	if (g_classicMode && (!ent || ent == INDEXENT(0)))
	{
		if (const char* t = mcm::WorldTraceTexture(v1, v2))
			return t;
	}
	return o_TraceTexture(ent, v1, v2);
}

void InstallEngineTraceWrappers()
{
	CVAR_REGISTER(&cv_spawnGuard);
	CVAR_REGISTER(&cv_spawnGuardTime);
	o_TraceTexture = g_engfuncs.pfnTraceTexture;
	g_engfuncs.pfnTraceTexture = W_TraceTexture;
	o_TraceLine = g_engfuncs.pfnTraceLine;
	o_TraceHull = g_engfuncs.pfnTraceHull;
	o_TraceMonsterHull = g_engfuncs.pfnTraceMonsterHull;
	o_PointContents = g_engfuncs.pfnPointContents;
	g_engfuncs.pfnTraceLine = W_TraceLine;
	g_engfuncs.pfnTraceHull = W_TraceHull;
	g_engfuncs.pfnTraceMonsterHull = W_TraceMonsterHull;
	g_engfuncs.pfnPointContents = W_PointContents;
}

// ---------------------------------------------------------------------------------------------
// Loading

static uint32_t Crc32(const uint8_t* p, size_t n)
{
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; i++)
	{
		c ^= p[i];
		for (int k = 0; k < 8; k++)
			c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}

bool LoadWorldFile(const char* path)
{
	int len = 0;
	byte* data = LOAD_FILE_FOR_ME(path, &len);
	if (!data)
		return false;
	bool ok = false;
	const byte* p = data;
	const byte* endp = data + len;
	auto rd32 = [&](void* out) {
		if (p + 4 > endp)
			return false;
		memcpy(out, p, 4);
		p += 4;
		return true;
	};
	do
	{
		if (len < 48 || memcmp(p, "MCW1", 4))
			break;
		p += 4;
		int version, sx, sy, sz, paletteCount, runCount;
		float origin[3], bs;
		if (!rd32(&version) || !rd32(&sx) || !rd32(&sy) || !rd32(&sz) || !rd32(&origin[0]) || !rd32(&origin[1]) ||
			!rd32(&origin[2]) || !rd32(&bs) || !rd32(&paletteCount))
			break;
		if (version != 1 || sx <= 0 || sy <= 0 || sz <= 0 || sx * (size_t)sy * sz > 64u * 1024 * 1024 || paletteCount <= 0 ||
			paletteCount > 1024)
			break;
		std::vector<uint16_t> remap(paletteCount, 0);
		for (int i = 0; i < paletteCount; i++)
		{
			if (p + 32 > endp)
				break;
			char name[33];
			memcpy(name, p, 32);
			name[32] = 0;
			p += 32;
			int id = mcw::FindBlock(name);
			if (id < 0)
			{
				McLog("world: unknown block '%s' -> stone", name);
				id = mcw::FindBlock("stone");
			}
			remap[i] = (uint16_t)id;
		}
		if (!rd32(&runCount))
			break;
		size_t total = (size_t)sx * sy * sz, pos = 0;
		g_cells.assign(total, 0);
		bool bad = false;
		for (int r = 0; r < runCount; r++)
		{
			if (p + 8 > endp)
			{
				bad = true;
				break;
			}
			uint32_t n;
			uint16_t cell;
			memcpy(&n, p, 4);
			memcpy(&cell, p + 4, 2);
			p += 8;
			uint16_t pal = mcw::CellType(cell);
			mcw::Cell c = pal < remap.size() ? mcw::MakeCell(remap[pal], mcw::CellState(cell)) : 0;
			if (pal == 0)
				c = 0;
			if (pos + n > total)
			{
				bad = true;
				break;
			}
			for (uint32_t k = 0; k < n; k++)
				g_cells[pos++] = c;
		}
		if (bad || pos != total)
			break;
		g_world.sx = sx;
		g_world.sy = sy;
		g_world.sz = sz;
		g_world.origin[0] = origin[0];
		g_world.origin[1] = origin[1];
		g_world.origin[2] = origin[2];
		g_world.cells = g_cells.data();
		g_world.shapeOfType = mcw::g_shapeOfType;
		g_worldCrc = Crc32(data, len);
		ok = true;
	} while (0);
	FREE_FILE(data);
	return ok;
}

// Classic mode: maps/<map>.mcc names the classic BSP and the dig grid.
static bool LoadClassic(const char* path)
{
	int len = 0;
	byte* data = LOAD_FILE_FOR_ME(path, &len);
	if (!data)
		return false;
	std::string text((const char*)data, (size_t)len);
	FREE_FILE(data);
	mcc::Config cfg;
	if (!cfg.Parse(text.c_str()))
	{
		McLog("classic: bad config %s", path);
		return false;
	}
	g_cells.assign((size_t)cfg.size[0] * cfg.size[1] * cfg.size[2], 0);
	g_world.sx = cfg.size[0];
	g_world.sy = cfg.size[1];
	g_world.sz = cfg.size[2];
	for (int i = 0; i < 3; i++)
		g_world.origin[i] = cfg.origin[i];
	g_world.cells = g_cells.data();
	g_world.shapeOfType = mcw::g_shapeOfType;
	g_classic.grid = &g_world;
	char gd[256], full[512];
	GET_GAME_DIR(gd);
	Q_snprintf(full, sizeof(full), "%s/%s", gd, cfg.bsp);
	if (!g_classic.Load(full))
	{
		McLog("classic: could not load %s", full);
		return false;
	}
	mcc::RegisterClassicBlocks(g_classic);
	g_originalCells = g_cells;
	g_worldLoaded = true;
	g_classicMode = true;
	mcm::SetWorld(&g_world);
	mcm::SetClassic(&g_classic);
	g_worldCrc = Crc32((const uint8_t*)text.data(), text.size()) ^ (uint32_t)g_classic.map.faces.size();
	if (cfg.sky[0])
		CVAR_SET_STRING("sv_skyname", cfg.sky);
	RedstoneRescan();
	McLog("classic: %s on a %dx%dx%d dig grid, %d texture blocks", full, g_world.sx, g_world.sy, g_world.sz, mcw::g_numDynBlocks);
	return true;
}

void OnServerActivate()
{
	extern void TestMapChanged();
	TestMapChanged();
	// our settings (+ optional test overrides written by tools/launch.ps1)
	SERVER_COMMAND("exec csmc.cfg\n");
	SERVER_COMMAND("exec csmc_test.cfg\n");
	g_worldLoaded = false;
	FireReset();
	g_changed.clear();
	g_owner.clear();
	g_spawnsKnown = false;
	g_pending.clear();
	mcm::SetWorld(nullptr);
	StepHeight = 18.0f;
	CVAR_SET_FLOAT("sv_stepsize", 18.0f);
	char path[256];
	g_classicMode = false;
	mcm::SetClassic(nullptr);
	Q_snprintf(path, sizeof(path), "maps/%s.mcc", STRING(gpGlobals->mapname));
	if (LoadClassic(path))
		return;
	mcw::ClearDynamicBlocks();
	mci::ClearDynamicItems();
	Q_snprintf(path, sizeof(path), "maps/%s.mcw", STRING(gpGlobals->mapname));
	if (LoadWorldFile(path))
	{
		g_worldLoaded = true;
		g_originalCells = g_cells;
		mcm::SetWorld(&g_world);
		RedstoneRescan();
		McLog("world: loaded %s (%dx%dx%d) crc %08x", path, g_world.sx, g_world.sy, g_world.sz, g_worldCrc);
		// Minecraft step height: 0.6 blocks; Minecraft daytime sky
		CVAR_SET_FLOAT("sv_stepsize", 24.0f);
		StepHeight = 24.0f;
		CVAR_SET_STRING("sv_skyname", "mcsky");
	}
	else
	{
		McLog("world: no voxel world for %s", STRING(gpGlobals->mapname));
	}
}

void OnServerDeactivate()
{
	BotTacticsMapEnd();
	g_worldLoaded = false;
	mcm::SetWorld(nullptr);
}

// Counter-Strike rounds vs Minecraft persistence: restore the map at round restart (mc_world_reset 1).
size_t ChangedCells() { return g_changed.size(); }

void ResetWorld()
{
	FireReset();
	g_owner.clear();
	g_bombEd = nullptr;
	if (!g_worldLoaded || g_changed.empty())
		return;
	g_cells = g_originalCells;
	g_world.cells = g_cells.data();
	g_changed.clear();
	g_pending.clear();
	RedstoneRescan();
	MESSAGE_BEGIN(MSG_ALL, MsgHello());
	WRITE_BYTE(3); // world enabled + reset
	WRITE_LONG((int)g_worldCrc);
	WRITE_STRING(STRING(gpGlobals->mapname));
	MESSAGE_END();
	McLog("world: reset for the new round");
}

void SendWorldToClient(edict_t* ent)
{
	if (!MsgHello())
		return;
	MESSAGE_BEGIN(MSG_ONE, MsgHello(), nullptr, ent);
	WRITE_BYTE(g_worldLoaded ? 1 : 0);
	WRITE_LONG((int)g_worldCrc);
	WRITE_STRING(STRING(gpGlobals->mapname));
	MESSAGE_END();
	if (!g_worldLoaded || g_changed.empty())
		return;
	// replay every edit made since the map loaded
	std::vector<PendingVox> all;
	all.reserve(g_changed.size());
	for (auto& kv : g_changed)
	{
		uint32_t idx = kv.first;
		int x = idx % g_world.sx;
		int y = (idx / g_world.sx) % g_world.sy;
		int z = idx / (g_world.sx * g_world.sy);
		all.push_back({(int16_t)x, (int16_t)y, (int16_t)z, kv.second});
	}
	for (size_t i = 0; i < all.size(); i += 20)
	{
		MESSAGE_BEGIN(MSG_ONE, MsgVox(), nullptr, ent);
		size_t n = min((size_t)20, all.size() - i);
		WRITE_BYTE((int)n);
		for (size_t k = 0; k < n; k++)
		{
			WRITE_SHORT(all[i + k].x);
			WRITE_SHORT(all[i + k].y);
			WRITE_SHORT(all[i + k].z);
			WRITE_SHORT(all[i + k].c);
		}
		MESSAGE_END();
	}
}

static void FlushPending()
{
	// the C4 crater breaks thousands of cells at once: at most 10 messages a frame, the rest follow
	size_t limit = min(g_pending.size(), (size_t)(10 * 20));
	for (size_t i = 0; i < limit; i += 20)
	{
		MESSAGE_BEGIN(MSG_ALL, MsgVox());
		size_t n = min((size_t)20, limit - i);
		WRITE_BYTE((int)n);
		for (size_t k = 0; k < n; k++)
		{
			WRITE_SHORT(g_pending[i + k].x);
			WRITE_SHORT(g_pending[i + k].y);
			WRITE_SHORT(g_pending[i + k].z);
			WRITE_SHORT(g_pending[i + k].c);
		}
		MESSAGE_END();
	}
	g_pending.erase(g_pending.begin(), g_pending.begin() + limit);
}

// ---------------------------------------------------------------------------------------------
// Block edits

static Vector BlockCenter(int x, int y, int z)
{
	return Vector(g_world.origin[0] + (x + 0.5f) * B2U, g_world.origin[1] + (y + 0.5f) * B2U, g_world.origin[2] + (z + 0.5f) * B2U);
}

static void CheckFalling(int x, int y, int z);

// A block somebody set down, as opposed to the map itself: what a wall is made of, and all a bot may mine.
bool IsPlacedBlock(int x, int y, int z)
{
	if (!g_worldLoaded || !g_world.InBounds(x, y, z))
		return false;
	mcw::Cell c = g_world.Get(x, y, z);
	if (!c)
		return false;
	if (g_classicMode)
	{
		if (mcw::CellType(c) == g_classic.carvedType)
			return false;
	}
	else if (g_changed.find((uint32_t)(((size_t)z * g_world.sy + y) * g_world.sx + x)) == g_changed.end())
		return false; // a block map: only what changed since it was loaded
	mcw::LocalBox lb[4];
	return mcw::ShapeBoxes(g_world.ShapeAt(x, y, z), mcw::CellState(c), lb) > 0;
}

// Whose block it is: the side of whoever set it down. A block of your own side comes away in your hands at
// once (MineFrame), so nobody is walled in by a teammate and a builder can open his own wall again.
static uint32_t CellIndex(int x, int y, int z) { return (uint32_t)(((size_t)z * g_world.sy + y) * g_world.sx + x); }
int BlockOwner(int x, int y, int z)
{
	if (!g_worldLoaded || !g_world.InBounds(x, y, z))
		return 0;
	auto it = g_owner.find(CellIndex(x, y, z));
	return it == g_owner.end() ? 0 : (it->second & 0xFF);
}
// Who set it down, if he is still there and on the same side (TNT lit by redstone is his: a mine's kills)
CBasePlayer* BlockPlacer(int x, int y, int z)
{
	if (!g_worldLoaded || !g_world.InBounds(x, y, z))
		return nullptr;
	auto it = g_owner.find(CellIndex(x, y, z));
	if (it == g_owner.end())
		return nullptr;
	CBasePlayer* p = UTIL_PlayerByIndex(it->second >> 8);
	return (p && p->m_iTeam == (it->second & 0xFF)) ? p : nullptr;
}
static void SetOwner(int x, int y, int z, CBasePlayer* pl)
{
	if (pl && (pl->m_iTeam == TERRORIST || pl->m_iTeam == CT) && g_world.InBounds(x, y, z))
		g_owner[CellIndex(x, y, z)] = (uint16_t)(pl->m_iTeam | (pl->entindex() << 8));
}

// ---------------------------------------------------------------------------------------------
// Where no block may go
//  - Around a planted bomb: its own column and the eight around it, three cells high. Whatever stands there
//    when the bomb is planted comes down. A wall further out is a wall like any other: it can be dug.
//  - Near the other side's spawn points (mc_spawn_guard units; 0: anywhere), in the first seconds of a round
//    (mc_spawn_guard_time): nobody is walled in where he spawns. Later it is ground like any other.

static CGrenade* PlantedBomb()
{
	edict_t* e = g_bombEd;
	if (!e || e->free || e->serialnumber != g_bombSerial || !e->pvPrivateData || (e->v.flags & FL_KILLME))
		return nullptr;
	CGrenade* g = static_cast<CGrenade*>(CBaseEntity::Instance(e));
	return (g && g->m_bIsC4 && !g->m_bJustBlew) ? g : nullptr;
}

// The cell the bomb lies in (it drops to the floor from the planter's hands: the floor under it counts)
static void BombCellAt(const Vector& at, int out[3])
{
	static const float zero[3] = {0, 0, 0};
	float s[3] = {at.x, at.y, at.z + 2.0f}, e[3] = {at.x, at.y, at.z - 80.0f};
	mcw::Trace t;
	mcm::WorldTrace(s, e, zero, zero, t);
	float p[3] = {at.x, at.y, ((t.hit && !t.startsolid) ? t.endpos[2] : at.z) + 8.0f};
	g_world.ToBlock(p, out);
}

CGrenade* PlantedBombEnt() { return PlantedBomb(); }

bool BombKeepsFree(int x, int y, int z)
{
	if (!g_worldLoaded)
		return false;
	CGrenade* g = PlantedBomb();
	if (!g)
		return false;
	int b[3];
	BombCellAt(g->pev->origin, b);
	return abs(x - b[0]) <= 1 && abs(y - b[1]) <= 1 && z >= b[2] && z <= b[2] + 2;
}

void BreakBlock(int x, int y, int z, CBasePlayer* by, bool drop);
void BombClearsSpace(CGrenade* bomb)
{
	if (!g_worldLoaded || !bomb)
		return;
	g_bombEd = bomb->edict();
	g_bombSerial = g_bombEd->serialnumber;
	int b[3], n = 0;
	BombCellAt(bomb->pev->origin, b);
	for (int dz = 0; dz <= 2; dz++)
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
				if (IsPlacedBlock(b[0] + dx, b[1] + dy, b[2] + dz))
				{
					BreakBlock(b[0] + dx, b[1] + dy, b[2] + dz, nullptr, true);
					n++;
				}
	if (n)
		McLog("bomb: planted at %d %d %d, %d blocks around it came down", b[0], b[1], b[2], n);
}

// Why this player may not set a block into the cell (nullptr: he may)
static const char* PlaceRefusal(CBasePlayer* pl, int x, int y, int z)
{
	if (!pl || P(pl).creative)
		return nullptr;
	if (BombKeepsFree(x, y, z))
		return "The bomb keeps the space around it free";
	if (cv_spawnGuard.value > 0.0f && (pl->m_iTeam == TERRORIST || pl->m_iTeam == CT) && CSGameRules() &&
		(CSGameRules()->IsFreezePeriod() || gpGlobals->time - CSGameRules()->m_fRoundStartTime < cv_spawnGuardTime.value))
	{
		if (!g_spawnsKnown)
		{
			g_spawnsKnown = true;
			static const char* kSpawn[2] = {"info_player_deathmatch", "info_player_start"};
			for (int s = 0; s < 2; s++)
			{
				g_spawns[s].clear();
				CBaseEntity* e = nullptr;
				while ((e = UTIL_FindEntityByClassname(e, kSpawn[s])) != nullptr)
					g_spawns[s].push_back(e->pev->origin);
			}
		}
		Vector c = BlockCenter(x, y, z);
		for (const Vector& s : g_spawns[pl->m_iTeam == TERRORIST ? 1 : 0])
			if ((c - s).Length2D() < cv_spawnGuard.value && fabsf(c.z - s.z) < 160.0f)
				return "No building at the enemy spawn this early in the round";
	}
	return nullptr;
}

void SetBlock(int x, int y, int z, mcw::Cell c, bool broadcast)
{
	if (!g_worldLoaded || !g_world.InBounds(x, y, z))
		return;
	if (g_world.Get(x, y, z) == c)
		return;
	if (mcw::CellType(g_world.Get(x, y, z)) != mcw::CellType(c))
		g_owner.erase(CellIndex(x, y, z)); // (a door that swings stays whose it was)
	g_world.Set(x, y, z, c);
	RedstoneCellChanged(x, y, z, c);
	uint32_t idx = (uint32_t)(((size_t)z * g_world.sy + y) * g_world.sx + x);
	g_changed[idx] = c;
	if (broadcast)
		g_pending.push_back({(int16_t)x, (int16_t)y, (int16_t)z, c});
	CheckFalling(x, y, z + 1);
}

static void CheckFalling(int x, int y, int z)
{
	mcw::Cell above = g_world.Get(x, y, z);
	if (!above)
		return;
	const mcw::BlockDef& d = mcw::Block(mcw::CellType(above));
	if (!(d.flags & mcw::BF_FALLS))
		return;
	if (g_world.Get(x, y, z - 1) != 0)
		return;
	SetBlock(x, y, z, 0);
	SpawnFallingBlock(x, y, z, above);
}

static int SoundGroupBase(const mcw::BlockDef& d) { return mcs::MCS_BLOCK_BASE + d.sound * 4; }

void BreakBlock(int x, int y, int z, CBasePlayer* by, bool drop)
{
	mcw::Cell c = g_world.Get(x, y, z);
	if (!c)
		return;
	uint16_t type = mcw::CellType(c);
	const mcw::BlockDef& d = mcw::Block(type);
	Vector center = BlockCenter(x, y, z);
	FxSound(SoundGroupBase(d) + 0, center, 1.0f, 0.8f);
	FxParticles(mcp::PK_BLOCK_BREAK, center, 64, c);
	McLog("break block %d %d %d (%s)", x, y, z, d.name);
	if (by && !by->IsBot())
	{
		const mci::ItemDef& held = mci::Item(HeldStack(by).id);
		if (d.sound == mcw::SOUND_STONE && held.type == mci::IT_PICKAXE)
			Award(by, ADV_STONE_AGE);
		if (strstr(d.name, "diamond_ore"))
			Award(by, ADV_DIAMONDS);
	}

	// doors are two cells: break the other half too
	auto emptied = [&](mcw::Cell was) -> mcw::Cell {
		// a block placed into a dug-out classic cell leaves the hole behind
		return (g_classicMode && (was & mcc::CARVED_FLAG)) ? mcw::MakeCell(g_classic.carvedType, 0) : 0;
	};
	if (d.shape == mcw::SHAPE_DOOR)
	{
		int other = (mcw::CellState(c) & 8) ? z - 1 : z + 1;
		mcw::Cell oc = g_world.Get(x, y, other);
		if (mcw::CellType(oc) == type)
			SetBlock(x, y, other, emptied(oc));
	}
	SetBlock(x, y, z, emptied(c));

	if (drop && d.drop == nullptr)
	{
		int item = mci::FindItem(d.name);
		if (item > 0)
			SpawnItemEntity(center, item, 1, nullptr);
	}
	else if (drop && d.drop && d.drop[0])
	{
		int item = mci::FindItem(d.drop);
		if (item > 0)
			SpawnItemEntity(center, item, 1, nullptr);
		if (!strcmp(d.drop, "diamond") || !strcmp(d.drop, "emerald") || !strcmp(d.drop, "coal"))
			DropXp(center, RANDOM_LONG(1, 5)); // ores drop xp
	}
}

// Classic mode: dig a cell of the classic map. Doors come down as a whole.
void CarveCell(int x, int y, int z, CBasePlayer* by, bool drop)
{
	if (!g_classicMode || !g_classic.Diggable(x, y, z))
		return;
	std::vector<int> cells;
	if (g_classic.Info(x, y, z).material == mcc::MAT_DOOR)
		g_classic.DoorCells(x, y, z, cells);
	if (cells.empty())
		cells = {x, y, z};
	for (size_t i = 0; i + 2 < cells.size(); i += 3)
	{
		int cx = cells[i], cy = cells[i + 1], cz = cells[i + 2];
		int type = mcc::CellBlockType(g_classic, cx, cy, cz);
		const mcw::BlockDef& d = mcw::Block((uint16_t)type);
		Vector center = BlockCenter(cx, cy, cz);
		mcw::Cell virt = mcw::MakeCell((uint16_t)type, 0);
		if (i == 0)
		{
			FxSound(SoundGroupBase(d) + 0, center, 1.0f, 0.8f);
			if (by && !by->IsBot())
			{
				const mci::ItemDef& held = mci::Item(HeldStack(by).id);
				if (d.sound == mcw::SOUND_STONE && held.type == mci::IT_PICKAXE)
					Award(by, ADV_STONE_AGE);
				if (strstr(d.name, "diamond_ore"))
					Award(by, ADV_DIAMONDS);
			}
		}
		if (i < 3 * 8)
			FxParticles(mcp::PK_BLOCK_BREAK, center, cells.size() > 3 ? 24 : 64, virt);
		SetBlock(cx, cy, cz, mcw::MakeCell(g_classic.carvedType, 0));
		if (drop)
		{
			int item = mci::FindItem(d.drop && d.drop[0] ? d.drop : d.name);
			if (item > 0)
				SpawnItemEntity(center, item, 1, nullptr);
			if (d.drop && (!strcmp(d.drop, "diamond") || !strcmp(d.drop, "coal") || !strcmp(d.drop, "redstone") || !strcmp(d.drop, "lapis_lazuli")))
				DropXp(center, RANDOM_LONG(1, 5));
		}
	}
	McLog("carve %d %d %d (%s, %d cells)", x, y, z, mcw::Block((uint16_t)mcc::CellBlockType(g_classic, x, y, z)).name, (int)cells.size() / 3);
}

// ---------------------------------------------------------------------------------------------
// Mining (left click hold) and using/placing (right click)

static bool PickTarget(CBasePlayer* pl, int b[3], int* face, float* dist, bool* classic = nullptr)
{
	if (!g_worldLoaded)
		return false;
	UTIL_MakeVectors(pl->pev->v_angle);
	Vector eye = pl->pev->origin + pl->pev->view_ofs;
	float dir[3] = {gpGlobals->v_forward.x, gpGlobals->v_forward.y, gpGlobals->v_forward.z};
	float start[3] = {eye.x, eye.y, eye.z};
	bool cl = false;
	if (!mcm::WorldPick(start, dir, mci::BLOCK_REACH, b, face, dist, &cl))
		return false;
	if (classic)
		*classic = cl;
	// an entity or the BSP world in front of the block blocks the pick
	TraceResult tr;
	o_TraceLine(start, eye + gpGlobals->v_forward * (*dist), dont_ignore_monsters, pl->edict(), &tr);
	if (tr.flFraction < 1.0f && tr.pHit && tr.pHit != INDEXENT(0))
		return false;
	return true;
}

static void SendBreakStage(CBasePlayer* pl, const int b[3], int stage)
{
	MESSAGE_BEGIN(MSG_ALL, MsgBreak());
	WRITE_BYTE(pl->entindex());
	WRITE_SHORT(b[0]);
	WRITE_SHORT(b[1]);
	WRITE_SHORT(b[2]);
	WRITE_BYTE(stage); // 0..9, 255 = none
	MESSAGE_END();
}

static bool ToolMatches(const mci::ItemDef& tool, const mcw::BlockDef& block)
{
	switch (block.tool)
	{
	case mcw::TOOL_PICKAXE: return tool.type == mci::IT_PICKAXE;
	case mcw::TOOL_AXE: return tool.type == mci::IT_AXE;
	case mcw::TOOL_SHOVEL: return tool.type == mci::IT_SHOVEL;
	default: return true;
	}
}

void MineFrame(CBasePlayer* pl, bool holding, const int* forced, bool mapToo)
{
	McPlayer& mp = P(pl);
	int b[3], face;
	float dist;
	bool classicHit = false;
	bool has;
	if (forced)
	{
		// a bot works on the block it chose (mc_bottactics.cpp), and only on blocks somebody set down:
		// bots never dig the map
		b[0] = forced[0];
		b[1] = forced[1];
		b[2] = forced[2];
		dist = (BlockCenter(b[0], b[1], b[2]) - (pl->pev->origin + pl->pev->view_ofs)).Length();
		// (mapToo: the one cell of the map a bot may dig out, for the mine it buries)
		bool placed = IsPlacedBlock(b[0], b[1], b[2]);
		classicHit = !placed && mapToo && g_classicMode && g_classic.Diggable(b[0], b[1], b[2]) && !g_classic.Carved(b[0], b[1], b[2]);
		has = holding && (placed || classicHit) && dist <= mci::BLOCK_REACH + B2U * 0.5f;
	}
	else
	{
		has = holding && PickTarget(pl, b, &face, &dist, &classicHit);
		if (has && classicHit && !g_classic.Diggable(b[0], b[1], b[2]))
			has = false; // sky, bedrock, outside the dig grid
	}
	static bool lastHas = false;
	if (!forced && holding && has != lastHas)
	{
		lastHas = has;
		McLog("mine: target %s %d %d %d dist %.0f", has ? "yes" : "no", b[0], b[1], b[2], has ? dist : 0.0f);
	}
	if (!has || b[0] != mp.mineBlock[0] || b[1] != mp.mineBlock[1] || b[2] != mp.mineBlock[2])
	{
		if (mp.mineStageSent >= 0)
			SendBreakStage(pl, mp.mineBlock, 255);
		mp.mineStageSent = -1;
		mp.mineProgress = 0.0f;
		mp.mineBlock[0] = has ? b[0] : -1;
		mp.mineBlock[1] = has ? b[1] : -1;
		mp.mineBlock[2] = has ? b[2] : -1;
		if (!has)
			return;
	}
	mcw::Cell c = g_world.Get(b[0], b[1], b[2]);
	if (classicHit)
		c = mcw::MakeCell((uint16_t)mcc::CellBlockType(g_classic, b[0], b[1], b[2]), 0);
	const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
	if (d.hardness < 0.0f)
		return; // bedrock
	auto breakIt = [&](bool drop) {
		if (classicHit)
			CarveCell(b[0], b[1], b[2], pl, drop);
		else
			BreakBlock(b[0], b[1], b[2], pl, drop);
	};

	const mci::Stack& held = HeldStack(pl);
	const mci::ItemDef& tool = held.Empty() ? mci::g_items[0] : mci::Item(held.id);
	bool correct = ToolMatches(tool, d) && d.tool != mcw::TOOL_NONE;
	bool canHarvest = d.tool != mcw::TOOL_PICKAXE || tool.type == mci::IT_PICKAXE; // stone-like needs a pickaxe
	// Efficiency: the level squared plus one, on top of the right tool's speed
	float toolSpeed = tool.miningSpeed;
	if (correct && !held.Empty() && mce::Main(held.ench))
		toolSpeed += mce::Main(held.ench) * mce::Main(held.ench) + 1.0f;
	float secs = mp.creative ? 0.0f : mci::BreakSeconds(d.hardness, toolSpeed, correct, canHarvest);
	bool ownSide = !classicHit && pl->m_iTeam != 0 && BlockOwner(b[0], b[1], b[2]) == pl->m_iTeam;
	if (ownSide)
		secs = 0.0f; // a block of your own side comes away at once

	float dt = gpGlobals->frametime;
	if (secs <= 0.0f)
	{
		if (gpGlobals->time - mp.mineLastHitSound < 0.15f)
			return; // creative break delay
		mp.mineLastHitSound = gpGlobals->time;
		breakIt(!mp.creative);
		mp.mineProgress = 0.0f;
		if (ownSide)
			FxSwing(pl->entindex());
		return;
	}
	mp.mineProgress += dt / secs;
	if (gpGlobals->time - mp.mineLastHitSound >= 0.2f)
	{
		mp.mineLastHitSound = gpGlobals->time;
		Vector hit = BlockCenter(b[0], b[1], b[2]);
		FxSound(SoundGroupBase(d) + 1, hit, 0.25f, 0.5f);
		FxParticles(mcp::PK_BLOCK_HIT, hit, 4, c);
		FxSwing(pl->entindex());
	}
	int stage = (int)(mp.mineProgress * 10.0f);
	if (stage > 9)
		stage = 9;
	if (stage != mp.mineStageSent)
	{
		SendBreakStage(pl, b, stage);
		mp.mineStageSent = stage;
	}
	if (mp.mineProgress >= 1.0f)
	{
		SendBreakStage(pl, b, 255);
		mp.mineStageSent = -1;
		mp.mineProgress = 0.0f;
		breakIt(canHarvest);
		if (!held.Empty() && tool.durability > 0)
			DamageHeld(pl, (tool.type == mci::IT_SWORD) ? 2 : 1);
	}
}

static int YawFacing(float yaw)
{
	// player looking towards +X => facing 0 ...
	int f = (int)floorf((yaw + 45.0f) / 90.0f) & 3;
	return f; // 0=+X,1=+Y,2=-X,3=-Y
}

// Something to rest on under cell (x,y,z): a solid block below, or the classic map's floor inside the
// cell (classic surfaces are not on the grid).
bool HasFloor(int x, int y, int z)
{
	mcw::Cell below = g_world.Get(x, y, z - 1);
	if (below && !(g_classicMode && mcw::CellType(below) == g_classic.carvedType))
	{
		mcw::LocalBox lb[4];
		if (mcw::ShapeBoxes(g_world.ShapeAt(x, y, z - 1), mcw::CellState(below), lb) > 0)
			return true;
	}
	if (g_classicMode)
	{
		Vector c = BlockCenter(x, y, z);
		// placement keeps the floor within half a cell of the cell bottom (see UseBlockTarget)
		float s[3] = {c.x, c.y, c.z + B2U * 0.5f - 0.5f}, e[3] = {c.x, c.y, c.z - B2U - 1.0f}, zero[3] = {0, 0, 0};
		mcc::Result r;
		g_classic.Trace(s, e, zero, zero, r);
		return r.hit && !r.startsolid && r.normal[2] > 0.7f;
	}
	return false;
}

// Is the redstone part at (x,y,z) still held up by something (block or classic surface)?
bool PartSupported(int x, int y, int z)
{
	mcw::Cell c = g_world.Get(x, y, z);
	mcw::ShapeKind s = g_world.ShapeAt(x, y, z);
	int attach = mcw::AttachOf(s, mcw::CellState(c));
	if (attach == 0)
		return HasFloor(x, y, z);
	int a[3];
	mcw::AttachVec(attach, a);
	mcw::Cell n = g_world.Get(x + a[0], y + a[1], z + a[2]);
	if (n && !(g_classicMode && mcw::CellType(n) == g_classic.carvedType))
	{
		mcw::LocalBox lb[4];
		if (mcw::ShapeBoxes(g_world.ShapeAt(x + a[0], y + a[1], z + a[2]), mcw::CellState(n), lb) > 0)
			return true;
	}
	if (g_classicMode)
	{
		// The classic map's walls are not on the grid, and some lean: the wall behind a part can be most of
		// a cell away, and a leaning one can pass beside the middle of the cell. Look along the attach
		// direction through the middle and through eight lines around it, a cell past the cell's face (the
		// client draws the part on the surface it finds the same way, SurfaceOffset in mc_world_cl.cpp).
		Vector ctr = BlockCenter(x, y, z);
		int u = a[0] ? 1 : 0, v = a[2] ? 1 : 2; // the two axes across the attach direction
		for (int i = -1; i <= 1; i++)
			for (int j = -1; j <= 1; j++)
			{
				float c[3] = {ctr.x, ctr.y, ctr.z};
				c[u] += i * 12.0f;
				c[v] += j * 12.0f;
				float st[3], en[3], zero[3] = {0, 0, 0};
				for (int k = 0; k < 3; k++)
				{
					st[k] = c[k] - a[k] * 18.0f;
					en[k] = c[k] + a[k] * (B2U * 1.5f);
				}
				mcc::Result r;
				g_classic.Trace(st, en, zero, zero, r);
				if (r.hit && !r.startsolid)
					return true;
			}
	}
	return false;
}

static bool HullBlocksPlace(int x, int y, int z)
{
	Vector mins(g_world.origin[0] + x * B2U, g_world.origin[1] + y * B2U, g_world.origin[2] + z * B2U);
	Vector maxs = mins + Vector(B2U, B2U, B2U);
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (!p || !p->IsAlive())
			continue;
		if (p->pev->absmin.x < maxs.x - 0.1f && p->pev->absmax.x > mins.x + 0.1f && p->pev->absmin.y < maxs.y - 0.1f &&
			p->pev->absmax.y > mins.y + 0.1f && p->pev->absmin.z < maxs.z - 0.1f && p->pev->absmax.z > mins.z + 0.1f)
			return true;
	}
	return false;
}

void ToggleDoor(int x, int y, int z)
{
	mcw::Cell c = g_world.Get(x, y, z);
	uint16_t type = mcw::CellType(c), st = mcw::CellState(c);
	int lower = (st & 8) ? z - 1 : z;
	for (int k = 0; k < 2; k++)
	{
		mcw::Cell h = g_world.Get(x, y, lower + k);
		if (mcw::CellType(h) == type)
			SetBlock(x, y, lower + k, mcw::MakeCell(type, mcw::CellState(h) ^ 4));
	}
	bool iron = !strcmp(mcw::Block(type).name, "iron_door");
	bool nowOpen = (st ^ 4) & 4;
	int snd = iron ? (nowOpen ? mcs::MCS_DOOR_IRON_OPEN : mcs::MCS_DOOR_IRON_CLOSE) : (nowOpen ? mcs::MCS_DOOR_WOOD_OPEN : mcs::MCS_DOOR_WOOD_CLOSE);
	FxSound(snd, BlockCenter(x, y, lower), 1.0f, 0.9f + RANDOM_FLOAT(0.0f, 0.1f));
}

// The enchanting table is drawn standing on the floor under it, which on a classic map is not on the grid
// (the client's SurfaceOffset): the height of its base.
static float TableBaseZ(int x, int y, int z)
{
	float bottom = g_world.origin[2] + z * B2U;
	if (!g_classicMode)
		return bottom;
	mcw::Cell below = g_world.Get(x, y, z - 1);
	if (below && mcw::CellType(below) != g_classic.carvedType)
	{
		mcw::LocalBox lb[4];
		if (mcw::ShapeBoxes(g_world.ShapeAt(x, y, z - 1), mcw::CellState(below), lb) > 0)
			return bottom;
	}
	Vector c = BlockCenter(x, y, z);
	float s[3] = {c.x, c.y, c.z + B2U * 0.45f}, e[3] = {c.x, c.y, c.z - B2U}, zero[3] = {0, 0, 0};
	mcc::Result r;
	g_classic.Trace(s, e, zero, zero, r);
	return (r.hit && !r.startsolid) ? r.endpos[2] : bottom;
}

// The enchanting table the player is looking at, by the box it is drawn in rather than its cell
static bool TableInView(CBasePlayer* pl, int out[3])
{
	static int table = -2;
	if (table == -2)
		table = mcw::FindBlock("enchanting_table");
	if (!g_worldLoaded || table < 0)
		return false;
	UTIL_MakeVectors(pl->pev->v_angle);
	Vector eye = pl->pev->origin + pl->pev->view_ofs, fwd = gpGlobals->v_forward;
	float best = mci::BLOCK_REACH;
	bool found = false;
	for (float d = 0.0f; d <= mci::BLOCK_REACH; d += 20.0f)
	{
		Vector p = eye + fwd * d;
		float pf[3] = {p.x, p.y, p.z};
		int c[3];
		g_world.ToBlock(pf, c);
		for (int dz = -2; dz <= 1; dz++)
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
				{
					int x = c[0] + dx, y = c[1] + dy, z = c[2] + dz;
					if (!g_world.InBounds(x, y, z) || (int)mcw::CellType(g_world.Get(x, y, z)) != table)
						continue;
					float base = TableBaseZ(x, y, z);
					float lo[3] = {g_world.origin[0] + x * B2U, g_world.origin[1] + y * B2U, base};
					float hi[3] = {lo[0] + B2U, lo[1] + B2U, base + B2U * 0.75f};
					// where the view ray enters that box
					float t0 = 0.0f, t1 = best;
					bool miss = false;
					for (int a = 0; a < 3 && !miss; a++)
					{
						float o = eye[a], dir = fwd[a];
						if (fabsf(dir) < 1e-6f)
						{
							miss = o < lo[a] || o > hi[a];
							continue;
						}
						float ta = (lo[a] - o) / dir, tb = (hi[a] - o) / dir;
						if (ta > tb)
						{
							float t = ta;
							ta = tb;
							tb = t;
						}
						t0 = max(t0, ta);
						t1 = min(t1, tb);
						miss = t0 > t1;
					}
					if (!miss && t0 < best)
					{
						best = t0;
						out[0] = x;
						out[1] = y;
						out[2] = z;
						found = true;
					}
				}
	}
	if (!found)
		return false;
	if (g_classicMode)
	{
		// not through a wall of the map
		Vector end = eye + fwd * best;
		float s[3] = {eye.x, eye.y, eye.z}, e[3] = {end.x, end.y, end.z}, zero[3] = {0, 0, 0};
		mcc::Result r;
		g_classic.Trace(s, e, zero, zero, r);
		if (r.hit && r.fraction * best < best - 2.0f)
			return false;
	}
	return true;
}

bool UseBlockTarget(CBasePlayer* pl, const mci::Stack& held)
{
	{
		int tb[3];
		if (!(pl->pev->button & IN_DUCK) && TableInView(pl, tb))
		{
			EnchantOpen(pl, tb[0], tb[1], tb[2]);
			return true;
		}
	}
	int b[3], face;
	float dist;
	bool classicHit = false;
	if (!PickTarget(pl, b, &face, &dist, &classicHit))
		return false;
	mcw::Cell c = classicHit ? 0 : g_world.Get(b[0], b[1], b[2]);
	const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
	const mci::ItemDef& hd = held.Empty() ? mci::g_items[0] : mci::Item(held.id);

	// interact first (like Minecraft, unless sneaking)
	if (!(pl->pev->button & IN_DUCK))
	{
		if (d.shape == mcw::SHAPE_DOOR && strcmp(d.name, "iron_door"))
		{
			ToggleDoor(b[0], b[1], b[2]);
			FxSwing(pl->entindex());
			return true;
		}
		if (!classicHit && RedstoneUse(pl, b[0], b[1], b[2]))
		{
			FxSwing(pl->entindex());
			return true;
		}
		if ((d.flags & mcw::BF_EXPLOSIVE) && hd.type == mci::IT_FLINT_STEEL)
		{
			Vector o = BlockCenter(b[0], b[1], b[2]) - Vector(0, 0, 20);
			SetBlock(b[0], b[1], b[2], (g_classicMode && (c & mcc::CARVED_FLAG)) ? mcw::MakeCell(g_classic.carvedType, 0) : 0);
			McLog("tnt: lit with flint and steel at %d %d %d", b[0], b[1], b[2]);
			PrimeTntBy(o, 80, pl);
			DamageHeld(pl, 1);
			FxSwing(pl->entindex());
			return true;
		}
	}
	bool flint = hd.type == mci::IT_FLINT_STEEL;
	if (!flint && (hd.type != mci::IT_BLOCK || !hd.blockName))
		return false;

	// place against the hit face (flint and steel: the fire goes where a block would)
	static const int off[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
	int p[3] = {b[0] + off[face][0], b[1] + off[face][1], b[2] + off[face][2]};
	if (classicHit)
	{
		// The classic map's surfaces don't sit on the 40-unit grid: put the block in the cell just in
		// front of the clicked surface (so it rests on the floor / against the wall), one cell further
		// out if that cell's centre is still inside the wall.
		UTIL_MakeVectors(pl->pev->v_angle);
		Vector eye = pl->pev->origin + pl->pev->view_ofs;
		Vector hit = eye + gpGlobals->v_forward * dist;
		float q[3] = {hit.x + off[face][0] * 1.0f, hit.y + off[face][1] * 1.0f, hit.z + off[face][2] * 1.0f};
		g_world.ToBlock(q, p);
		Vector ctr = BlockCenter(p[0], p[1], p[2]);
		float cp[3] = {ctr.x, ctr.y, ctr.z};
		if (!g_classic.Carved(p[0], p[1], p[2]) && g_classic.PointContents(cp) == mcb::CONT_SOLID)
			for (int k = 0; k < 3; k++)
				p[k] += off[face][k];
	}
	if (!g_world.InBounds(p[0], p[1], p[2]))
	{
		McLog("place: refused, %d %d %d out of the grid", p[0], p[1], p[2]);
		return true;
	}
	mcw::Cell existing = g_world.Get(p[0], p[1], p[2]);
	bool carvedThere = g_classicMode && g_classic.IsCarved(existing);
	if (carvedThere && mcw::CellType(existing) == g_classic.carvedType)
		existing = 0; // a dug-out cell is empty
	if (existing && mcw::Block(mcw::CellType(existing)).shape != mcw::SHAPE_CROSS && mcw::Block(mcw::CellType(existing)).shape != mcw::SHAPE_FIRE)
	{
		McLog("place: refused, %d %d %d holds %s", p[0], p[1], p[2], mcw::Block(mcw::CellType(existing)).name);
		return true;
	}
	if (g_classicMode && !carvedThere)
	{
		// not inside a wall of the classic map
		Vector ctr = BlockCenter(p[0], p[1], p[2]);
		float cp[3] = {ctr.x, ctr.y, ctr.z};
		if (g_classic.PointContents(cp) == mcb::CONT_SOLID)
		{
			McLog("place: refused, %d %d %d centre is inside the classic map", p[0], p[1], p[2]);
			return true;
		}
	}
	if (!flint)
		if (const char* no = PlaceRefusal(pl, p[0], p[1], p[2]))
		{
			static float told[MAX_CLIENTS + 1];
			int who = pl->entindex();
			if (who >= 1 && who <= MAX_CLIENTS && (gpGlobals->time < told[who] || gpGlobals->time - told[who] > 1.5f))
			{
				told[who] = gpGlobals->time;
				Toast(pl, 1, "%s", no);
				McLog("place: refused at %d %d %d: %s", p[0], p[1], p[2], no);
			}
			return true;
		}
	if (flint)
	{
		if (FireLight(p[0], p[1], p[2], pl))
		{
			FxSound(mcs::MCS_FLINT_USE, BlockCenter(p[0], p[1], p[2]), 1.0f, 0.8f + RANDOM_FLOAT(0.0f, 0.4f));
			DamageHeld(pl, 1);
		}
		FxSwing(pl->entindex());
		return true;
	}
	const uint16_t keepFlag = carvedThere ? mcc::CARVED_FLAG : 0;
	int type = mcw::FindBlock(hd.blockName);
	if (type < 0)
		return true;
	const mcw::BlockDef& nd = mcw::Block((uint16_t)type);
	uint16_t state = 0;
	int facing = YawFacing(pl->pev->v_angle.y);
	switch (nd.shape)
	{
	case mcw::SHAPE_SLAB:
	{
		// top slab when clicking the upper half of a side face or the bottom face
		UTIL_MakeVectors(pl->pev->v_angle);
		Vector eye = pl->pev->origin + pl->pev->view_ofs;
		float hitZ = eye.z + gpGlobals->v_forward.z * dist;
		float localZ = (hitZ - (g_world.origin[2] + p[2] * B2U)) / B2U;
		if (face == 5 || (face < 4 && localZ > 0.5f))
			state = 1;
		break;
	}
	case mcw::SHAPE_STAIRS:
		state = (uint16_t)facing;
		break;
	case mcw::SHAPE_DUST:
	case mcw::SHAPE_PLATE:
	case mcw::SHAPE_REPEATER:
		if (!HasFloor(p[0], p[1], p[2]))
		{
			McLog("place: refused, nothing under %d %d %d for %s", p[0], p[1], p[2], nd.name);
			return true;
		}
		if (nd.shape == mcw::SHAPE_REPEATER)
			state = (uint16_t)facing; // outputs away from the player
		break;
	case mcw::SHAPE_TORCH:
	case mcw::SHAPE_LEVER:
	case mcw::SHAPE_BUTTON:
	{
		// clicked face -> where the supporting block is (0 below, 1..4 wall at +X,+Y,-X,-Y, 5 above)
		static const int attachOfFace[6] = {3, 1, 4, 2, 0, 5};
		int attach = attachOfFace[face];
		if (attach == 5 && nd.shape == mcw::SHAPE_TORCH)
			return true; // torches don't hang from ceilings
		if (attach == 0 && !HasFloor(p[0], p[1], p[2]))
			return true;
		state = (uint16_t)attach;
		if ((attach == 0 || attach == 5) && (facing & 1))
			state |= 16; // floor/ceiling lever or button along Y
		break;
	}
	case mcw::SHAPE_DOOR:
	{
		if (!g_world.InBounds(p[0], p[1], p[2] + 1))
			return true;
		{
			mcw::Cell up = g_world.Get(p[0], p[1], p[2] + 1);
			if (up && !(g_classicMode && mcw::CellType(up) == g_classic.carvedType))
				return true;
		}
		// door panel sits on the side facing away from the player
		uint16_t f = (uint16_t)((facing + 2) & 3);
		state = f;
		if (HullBlocksPlace(p[0], p[1], p[2]) || HullBlocksPlace(p[0], p[1], p[2] + 1))
			return true;
		{
			mcw::Cell up = g_world.Get(p[0], p[1], p[2] + 1);
			uint16_t upFlag = (g_classicMode && g_classic.IsCarved(up)) ? mcc::CARVED_FLAG : 0;
			SetBlock(p[0], p[1], p[2], mcw::MakeCell((uint16_t)type, state) | keepFlag);
			SetBlock(p[0], p[1], p[2] + 1, mcw::MakeCell((uint16_t)type, state | 8) | upFlag);
			SetOwner(p[0], p[1], p[2], pl);
			SetOwner(p[0], p[1], p[2] + 1, pl);
		}
		FxSound(SoundGroupBase(nd) + 2, BlockCenter(p[0], p[1], p[2]), 1.0f, 0.8f);
		FxSwing(pl->entindex());
		ConsumeHeld(pl, 1);
		return true;
	}
	default:
		break;
	}
	if (!mcw::IsPickOnlyShape(nd.shape) && HullBlocksPlace(p[0], p[1], p[2]))
	{
		McLog("place: refused, a player stands in %d %d %d", p[0], p[1], p[2]);
		return true;
	}
	SetBlock(p[0], p[1], p[2], mcw::MakeCell((uint16_t)type, state) | keepFlag);
	SetOwner(p[0], p[1], p[2], pl);
	McLog("place: %s at %d %d %d", nd.name, p[0], p[1], p[2]);
	TryBuildMob(p[0], p[1], p[2], pl);
	FxSound(SoundGroupBase(nd) + 2, BlockCenter(p[0], p[1], p[2]), 1.0f, 0.8f);
	FxSwing(pl->entindex());
	ConsumeHeld(pl, 1);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Bots (mc_bottactics.cpp) pick the cell themselves instead of clicking on a face. The conditions are a
// player's: the block in the hand, the cell within reach, free, not inside the classic map, nobody in it.

Vector CellCenter(int x, int y, int z) { return BlockCenter(x, y, z); }

bool CellTakesBlock(int x, int y, int z)
{
	if (!g_worldLoaded || !g_world.InBounds(x, y, z))
		return false;
	mcw::Cell e = g_world.Get(x, y, z);
	bool carved = g_classicMode && g_classic.IsCarved(e);
	if (carved && mcw::CellType(e) == g_classic.carvedType)
		e = 0; // a dug-out cell is empty
	if (e)
	{
		mcw::ShapeKind s = mcw::Block(mcw::CellType(e)).shape;
		if (s != mcw::SHAPE_CROSS && s != mcw::SHAPE_FIRE)
			return false;
	}
	if (g_classicMode && !carved)
	{
		Vector ctr = BlockCenter(x, y, z);
		float cp[3] = {ctr.x, ctr.y, ctr.z};
		if (g_classic.PointContents(cp) == mcb::CONT_SOLID)
			return false;
	}
	return true;
}

static bool BotReaches(CBasePlayer* bot, int x, int y, int z)
{
	return (BlockCenter(x, y, z) - (bot->pev->origin + bot->pev->view_ofs)).Length() <= mci::BLOCK_REACH + B2U * 0.5f;
}

bool BotPlaceBlock(CBasePlayer* bot, int x, int y, int z)
{
	const mci::Stack& held = HeldStack(bot);
	const mci::ItemDef& hd = held.Empty() ? mci::g_items[0] : mci::Item(held.id);
	if (hd.type != mci::IT_BLOCK || !hd.blockName)
		return false;
	int type = mcw::FindBlock(hd.blockName);
	if (type < 0 || mcw::Block((uint16_t)type).shape != mcw::SHAPE_CUBE)
		return false;
	if (!CellTakesBlock(x, y, z) || !BotReaches(bot, x, y, z) || HullBlocksPlace(x, y, z) || PlaceRefusal(bot, x, y, z))
		return false;
	const uint16_t keepFlag = (g_classicMode && g_classic.IsCarved(g_world.Get(x, y, z))) ? mcc::CARVED_FLAG : 0;
	const mcw::BlockDef& nd = mcw::Block((uint16_t)type);
	SetBlock(x, y, z, mcw::MakeCell((uint16_t)type, 0) | keepFlag);
	SetOwner(x, y, z, bot);
	McLog("place: bot %s sets %s at %d %d %d", STRING(bot->pev->netname), nd.name, x, y, z);
	FxSound(SoundGroupBase(nd) + 2, BlockCenter(x, y, z), 1.0f, 0.8f);
	FxSwing(bot->entindex());
	ConsumeHeld(bot, 1);
	TryBuildMob(x, y, z, bot);
	return true;
}

// Minecraft's iron golem: a carved pumpkin set on top of four iron blocks in a T (one under it, one to either
// side of that, one below). The blocks go, and a golem stands there for the side of whoever set the pumpkin.
bool TryBuildMob(int x, int y, int z, CBasePlayer* pl)
{
	static int pumpkin = -2, iron = -2;
	if (pumpkin == -2)
	{
		pumpkin = mcw::FindBlock("carved_pumpkin");
		iron = mcw::FindBlock("iron_block");
	}
	static int skull = -2, soul = -2;
	if (skull == -2)
	{
		skull = mcw::FindBlock("wither_skeleton_skull");
		soul = mcw::FindBlock("soul_sand");
	}
	if (pl && skull >= 0 && soul >= 0 && mcw::CellType(g_world.Get(x, y, z)) == skull && (pl->m_iTeam == TERRORIST || pl->m_iTeam == CT))
	{
		// Minecraft's wither: four soul sand in a T, three skulls in a row on its top, the last skull set brings it
		auto at = [&](int a, int b, int c, int type) { mcw::Cell v = g_world.Get(a, b, c); return v && mcw::CellType(v) == type; };
		for (int ax = 0; ax < 2; ax++)
			for (int off = -1; off <= 1; off++)
			{
				int m[3] = {x, y, z};
				m[ax] -= off; // the middle skull
				bool ok = at(m[0], m[1], m[2] - 2, soul);
				for (int k = -1; k <= 1 && ok; k++)
				{
					int q[3] = {m[0], m[1], m[2]};
					q[ax] += k;
					ok = at(q[0], q[1], q[2], skull) && at(q[0], q[1], q[2] - 1, soul);
				}
				if (!ok)
					continue;
				if (!WitherAllowed(pl->m_iTeam))
				{
					Toast(pl, 1, "Your side cannot raise a wither yet");
					McLog("mob: %s completed a wither, but its side may not have one now", STRING(pl->pev->netname));
					return false;
				}
				for (int k = -1; k <= 1; k++)
					for (int dz = 0; dz >= -2; dz--)
					{
						int q[3] = {m[0], m[1], m[2] + dz};
						if (dz > -2)
							q[ax] += k;
						else if (k != 0)
							continue;
						mcw::Cell v = g_world.Get(q[0], q[1], q[2]);
						FxParticles(mcp::PK_BLOCK_BREAK, BlockCenter(q[0], q[1], q[2]), 24, v);
						SetBlock(q[0], q[1], q[2], (g_classicMode && (v & mcc::CARVED_FLAG)) ? mcw::MakeCell(g_classic.carvedType, 0) : 0);
					}
				TeamMobRequest(TM_WITHER, pl->m_iTeam, BlockCenter(m[0], m[1], m[2] - 2) - Vector(0, 0, B2U * 0.5f), pl->pev->v_angle.y, pl);
				return true;
			}
		return false;
	}
	if (!pl || pumpkin < 0 || iron < 0 || mcw::CellType(g_world.Get(x, y, z)) != pumpkin)
		return false;
	auto is = [&](int a, int b, int c) { mcw::Cell v = g_world.Get(a, b, c); return v && mcw::CellType(v) == iron; };
	if (!is(x, y, z - 1) || !is(x, y, z - 2))
		return false;
	int ax = (is(x - 1, y, z - 1) && is(x + 1, y, z - 1)) ? 0 : (is(x, y - 1, z - 1) && is(x, y + 1, z - 1)) ? 1 : -1;
	if (ax < 0 || (pl->m_iTeam != TERRORIST && pl->m_iTeam != CT))
		return false;
	if (TeamMobsAlive(pl->m_iTeam, TM_GOLEM) >= GolemLimit())
	{
		Toast(pl, 1, "Your side has its iron golem already");
		McLog("mob: %s completed a golem, but its side has %d already", STRING(pl->pev->netname), GolemLimit());
		return false;
	}
	int cells[5][3] = {{x, y, z}, {x, y, z - 1}, {x, y, z - 2}, {x, y, z - 1}, {x, y, z - 1}};
	cells[3][ax] -= 1;
	cells[4][ax] += 1;
	Vector feet = BlockCenter(x, y, z - 2) - Vector(0, 0, B2U * 0.5f);
	for (const int* c : cells)
	{
		mcw::Cell v = g_world.Get(c[0], c[1], c[2]);
		FxParticles(mcp::PK_BLOCK_BREAK, BlockCenter(c[0], c[1], c[2]), 24, v);
		SetBlock(c[0], c[1], c[2], (g_classicMode && (v & mcc::CARVED_FLAG)) ? mcw::MakeCell(g_classic.carvedType, 0) : 0);
	}
	TeamMobRequest(TM_GOLEM, pl->m_iTeam, feet, pl->pev->v_angle.y, pl);
	return true;
}

void ExplodeThroughAll(const float* origin, float power, CBaseEntity* attacker)
{
	g_blastThroughAll = true;
	Explode(origin, power, nullptr, attacker, true);
	g_blastThroughAll = false;
}

// A wither that was hurt breaks the blocks around itself a second later (Minecraft: a box three wide and
// four high about it): blocks somebody set down, and what of the map can be dug. Returns how many went.
int WitherBreaks(const Vector& origin, CBasePlayer* by)
{
	if (!g_worldLoaded)
		return 0;
	int c[3], n = 0;
	float q[3] = {origin.x, origin.y, origin.z - 30.0f};
	g_world.ToBlock(q, c);
	for (int dz = 0; dz <= 3; dz++)
		for (int dy = -1; dy <= 1; dy++)
			for (int dx = -1; dx <= 1; dx++)
			{
				int x = c[0] + dx, y = c[1] + dy, z = c[2] + dz;
				if (IsPlacedBlock(x, y, z))
				{
					if (mcw::Block(mcw::CellType(g_world.Get(x, y, z))).hardness < 0.0f)
						continue;
					BreakBlock(x, y, z, nullptr, true);
					n++;
				}
				else if (g_classicMode && dz >= 1 && g_classic.Diggable(x, y, z) && !g_classic.Carved(x, y, z))
				{
					// (not the floor it stands on: the cells its body is in)
					Vector ctr = BlockCenter(x, y, z);
					float cp[3] = {ctr.x, ctr.y, ctr.z};
					if (g_classic.PointContents(cp) != mcb::CONT_SOLID)
						continue;
					CarveCell(x, y, z, nullptr, false);
					n++;
				}
			}
	return n;
}

// A pressure plate from the bot's hand onto the block under the cell (the lid of a mine)
bool BotPlacePlate(CBasePlayer* bot, int x, int y, int z)
{
	const mci::Stack& held = HeldStack(bot);
	const mci::ItemDef& hd = held.Empty() ? mci::g_items[0] : mci::Item(held.id);
	if (hd.type != mci::IT_BLOCK || !hd.blockName)
		return false;
	int type = mcw::FindBlock(hd.blockName);
	if (type < 0 || mcw::Block((uint16_t)type).shape != mcw::SHAPE_PLATE)
		return false;
	if (!CellTakesBlock(x, y, z) || !BotReaches(bot, x, y, z) || !HasFloor(x, y, z) || PlaceRefusal(bot, x, y, z))
		return false;
	const uint16_t keepFlag = (g_classicMode && g_classic.IsCarved(g_world.Get(x, y, z))) ? mcc::CARVED_FLAG : 0;
	const mcw::BlockDef& nd = mcw::Block((uint16_t)type);
	SetBlock(x, y, z, mcw::MakeCell((uint16_t)type, 0) | keepFlag);
	SetOwner(x, y, z, bot);
	McLog("place: bot %s sets %s at %d %d %d", STRING(bot->pev->netname), nd.name, x, y, z);
	FxSound(SoundGroupBase(nd) + 2, BlockCenter(x, y, z), 1.0f, 0.8f);
	FxSwing(bot->entindex());
	ConsumeHeld(bot, 1);
	return true;
}

bool BotLightTnt(CBasePlayer* bot, int x, int y, int z)
{
	const mci::Stack& held = HeldStack(bot);
	if (held.Empty() || mci::Item(held.id).type != mci::IT_FLINT_STEEL || !BotReaches(bot, x, y, z))
		return false;
	mcw::Cell c = g_world.Get(x, y, z);
	if (!c || !(mcw::Block(mcw::CellType(c)).flags & mcw::BF_EXPLOSIVE))
		return false;
	Vector o = BlockCenter(x, y, z) - Vector(0, 0, 20);
	SetBlock(x, y, z, (g_classicMode && (c & mcc::CARVED_FLAG)) ? mcw::MakeCell(g_classic.carvedType, 0) : 0);
	McLog("tnt: bot %s lights it at %d %d %d", STRING(bot->pev->netname), x, y, z);
	PrimeTntBy(o, 80, bot);
	DamageHeld(bot, 1);
	FxSwing(bot->entindex());
	return true;
}

// ---------------------------------------------------------------------------------------------
// Explosions (Explosion.explode, Java): rays from the centre, block resistance eats intensity.

static float BlastResistance(const mcw::BlockDef& d)
{
	if (d.hardness < 0.0f)
		return 3600000.0f;
	if (g_blastThroughAll)
		return 0.8f;
	if (!strcmp(d.name, "obsidian"))
		return 1200.0f;
	if (d.tool == mcw::TOOL_PICKAXE && d.hardness >= 1.5f)
		return 6.0f;
	if (d.flags & mcw::BF_EXPLOSIVE)
		return 0.0f;
	if (d.tool == mcw::TOOL_AXE)
		return 3.0f;
	return d.hardness;
}

void Explode(const float* origin, float power, CBaseEntity* source, CBaseEntity* attacker, bool spareMates)
{
	MC_WHERE("Explode");
	FxExplosion(origin, power);
	if (!source)
		ExplosionHurt(origin, power, attacker, spareMates); // TNT/creeper blasts (HE/C4 already dealt Counter-Strike damage)
	FxSound(mcs::MCS_EXPLODE, origin, 4.0f, (1.0f + (RANDOM_FLOAT(0, 1) - RANDOM_FLOAT(0, 1)) * 0.2f) * 0.7f);
	if (!g_worldLoaded)
		return;
	float o[3] = {(origin[0] - g_world.origin[0]) / B2U, (origin[1] - g_world.origin[1]) / B2U, (origin[2] - g_world.origin[2]) / B2U};
	std::vector<uint32_t> toBreak;
	std::unordered_map<uint32_t, bool> seen;
	for (int i = 0; i < 16; i++)
		for (int j = 0; j < 16; j++)
			for (int k = 0; k < 16; k++)
			{
				if (i != 0 && i != 15 && j != 0 && j != 15 && k != 0 && k != 15)
					continue;
				float dx = i / 15.0f * 2.0f - 1.0f, dy = j / 15.0f * 2.0f - 1.0f, dz = k / 15.0f * 2.0f - 1.0f;
				float len = sqrtf(dx * dx + dy * dy + dz * dz);
				dx /= len;
				dy /= len;
				dz /= len;
				float intensity = power * (0.7f + RANDOM_FLOAT(0.0f, 1.0f) * 0.6f);
				float x = o[0], y = o[1], z = o[2];
				for (; intensity > 0.0f; intensity -= 0.22500001f)
				{
					int bx = (int)floorf(x), by = (int)floorf(y), bz = (int)floorf(z);
					if (!g_world.InBounds(bx, by, bz))
						break;
					mcw::Cell c = g_world.Get(bx, by, bz);
					if (g_classicMode)
					{
						if (mcw::CellType(c) == g_classic.carvedType)
							c = 0; // dug out: air
						else if (!c && g_classic.Diggable(bx, by, bz))
							c = mcw::MakeCell((uint16_t)mcc::CellBlockType(g_classic, bx, by, bz), 0); // the classic map itself
					}
					if (c)
					{
						intensity -= (BlastResistance(mcw::Block(mcw::CellType(c))) + 0.3f) * 0.3f;
						if (intensity > 0.0f)
						{
							uint32_t idx = (uint32_t)(((size_t)bz * g_world.sy + by) * g_world.sx + bx);
							if (!seen[idx])
							{
								seen[idx] = true;
								toBreak.push_back(idx);
							}
						}
					}
					x += dx * 0.3f;
					y += dy * 0.3f;
					z += dz * 0.3f;
				}
			}
	int particles = 0;
	for (uint32_t idx : toBreak)
	{
		int x = idx % g_world.sx, y = (idx / g_world.sx) % g_world.sy, z = idx / (g_world.sx * g_world.sy);
		mcw::Cell c = g_world.Get(x, y, z);
		if (g_classicMode && !c)
		{
			// the classic map: dig the cell (rarely dropping it, like Minecraft's explosion loot)
			if (!g_classic.Diggable(x, y, z))
				continue;
			int type = mcc::CellBlockType(g_classic, x, y, z);
			mcw::Cell virt = mcw::MakeCell((uint16_t)type, 0);
			if (particles++ < 24)
				FxParticles(mcp::PK_BLOCK_BREAK, BlockCenter(x, y, z), 6, virt);
			SetBlock(x, y, z, mcw::MakeCell(g_classic.carvedType, 0));
			if (RANDOM_FLOAT(0.0f, 1.0f) < 0.3f / power)
			{
				const mcw::BlockDef& vd = mcw::Block((uint16_t)type);
				int item = mci::FindItem(vd.drop && vd.drop[0] ? vd.drop : vd.name);
				if (item > 0)
					SpawnItemEntity(BlockCenter(x, y, z), item, 1, nullptr);
			}
			continue;
		}
		if (g_classicMode && mcw::CellType(c) == g_classic.carvedType)
			continue;
		const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
		if (d.flags & mcw::BF_EXPLOSIVE)
		{
			Vector bc = BlockCenter(x, y, z) - Vector(0, 0, 20);
			SetBlock(x, y, z, (g_classicMode && (c & mcc::CARVED_FLAG)) ? mcw::MakeCell(g_classic.carvedType, 0) : 0);
			PrimeTnt(bc, RANDOM_LONG(10, 30)); // chain reaction with a short fuse
			continue;
		}
		if (particles++ < 24)
			FxParticles(mcp::PK_BLOCK_BREAK, BlockCenter(x, y, z), 6, c);
		SetBlock(x, y, z, (g_classicMode && (c & mcc::CARVED_FLAG)) ? mcw::MakeCell(g_classic.carvedType, 0) : 0);
		if (RANDOM_FLOAT(0.0f, 1.0f) < 0.3f / power)
		{
			int item = mci::FindItem(d.drop && d.drop[0] ? d.drop : d.name);
			if (item > 0)
				SpawnItemEntity(BlockCenter(x, y, z), item, 1, nullptr);
		}
	}
	McLog("explosion power %.1f broke %d blocks", power, (int)toBreak.size());
}

void H_ExplodeHe(IReGameHook_CGrenade_ExplodeHeGrenade* chain, CGrenade* g, TraceResult* tr, int bits)
{
	Vector o = g->pev->origin;
	chain->callNext(g, tr, bits);
	Explode(o, 3.0f, g, nullptr);
}

// The C4 is one enormous TNT: everything that can be blown up within mc_bomb_radius blocks goes (an
// ellipsoid squashed to a bowl below the bomb, with a ragged rim), fireballs fill the site, and a ring of
// primed TNT goes off around it over the next two seconds. The world comes back at the round restart.
cvar_t g_cvBombRadius = {"mc_bomb_radius", "11", FCVAR_SERVER, 11.0f, nullptr};

static void BombCrater(const Vector& at)
{
	float R = g_cvBombRadius.value;
	if (!g_worldLoaded || R <= 0.0f)
		return;
	float o[3] = {(at.x - g_world.origin[0]) / B2U, (at.y - g_world.origin[1]) / B2U, (at.z - g_world.origin[2]) / B2U};
	int r = (int)ceilf(R) + 1;
	int cx = (int)floorf(o[0]), cy = (int)floorf(o[1]), cz = (int)floorf(o[2]);
	int broken = 0, particles = 0;
	for (int z = cz - r; z <= cz + r; z++)
		for (int y = cy - r; y <= cy + r; y++)
			for (int x = cx - r; x <= cx + r; x++)
			{
				if (!g_world.InBounds(x, y, z))
					continue;
				float dx = x + 0.5f - o[0], dy = y + 0.5f - o[1], dz = z + 0.5f - o[2];
				if (dz < 0.0f)
					dz *= 3.0f; // a bowl a third as deep as it is wide
				uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
				h ^= h >> 13;
				h *= 0x5bd1e995u;
				h ^= h >> 15;
				float edge = R * (0.8f + 0.2f * (float)(h & 1023) / 1023.0f);
				if (dx * dx + dy * dy + dz * dz > edge * edge)
					continue;
				mcw::Cell c = g_world.Get(x, y, z);
				if (g_classicMode && !c)
				{
					// the classic map itself
					if (!g_classic.Diggable(x, y, z))
						continue;
					if (particles++ < 40)
					{
						mcw::Cell virt = mcw::MakeCell((uint16_t)mcc::CellBlockType(g_classic, x, y, z), 0);
						FxParticles(mcp::PK_BLOCK_BREAK, BlockCenter(x, y, z), 8, virt);
					}
					SetBlock(x, y, z, mcw::MakeCell(g_classic.carvedType, 0));
					broken++;
					continue;
				}
				if (!c || (g_classicMode && mcw::CellType(c) == g_classic.carvedType))
					continue;
				const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
				if (BlastResistance(d) > 100.0f)
					continue; // bedrock, obsidian
				mcw::Cell empty = (g_classicMode && (c & mcc::CARVED_FLAG)) ? mcw::MakeCell(g_classic.carvedType, 0) : 0;
				if (d.flags & mcw::BF_EXPLOSIVE)
				{
					SetBlock(x, y, z, empty);
					Vector bc = BlockCenter(x, y, z) - Vector(0, 0, 20);
					PrimeTnt(bc, RANDOM_LONG(10, 40));
					continue;
				}
				if (particles++ < 40)
					FxParticles(mcp::PK_BLOCK_BREAK, BlockCenter(x, y, z), 8, c);
				SetBlock(x, y, z, empty);
				broken++;
			}
	// fireballs across the site, then the ring of TNT
	for (int k = 0; k < 6; k++)
	{
		float a = RANDOM_FLOAT(0.0f, 6.2831853f), d = RANDOM_FLOAT(0.2f, 0.7f) * R * B2U;
		Vector p = at + Vector(cosf(a) * d, sinf(a) * d, RANDOM_FLOAT(10.0f, 90.0f));
		FxExplosion(p, 6.0f);
	}
	int ring = 8;
	for (int k = 0; k < ring; k++)
	{
		float a = (k + RANDOM_FLOAT(0.0f, 0.6f)) * 6.2831853f / ring, d = RANDOM_FLOAT(0.55f, 0.9f) * R * B2U;
		Vector p = at + Vector(cosf(a) * d, sinf(a) * d, 30.0f);
		PrimeTnt(p, RANDOM_LONG(12, 50));
	}
	McLog("bomb: crater radius %.0f blocks, %d cells blown away, %d TNT around it", R, broken, ring);
}

void H_ExplodeBomb(IReGameHook_CGrenade_ExplodeBomb* chain, CGrenade* g, TraceResult* tr, int bits)
{
	Vector o = g->pev->origin;
	chain->callNext(g, tr, bits);
	Explode(o, 8.0f, g, nullptr);
	BombCrater(o);
}

// ---------------------------------------------------------------------------------------------
// Engine physics knows nothing about voxels: before the engine moves tossed/bouncing entities this
// frame, clip their motion against voxels ourselves.

static void VoxelPhysicsPrepass()
{
	MC_WHERE("VoxelPhysicsPrepass");
	float dt = gpGlobals->frametime;
	if (dt <= 0.0f)
		return;
	for (int i = gpGlobals->maxClients + 1; i < gpGlobals->maxEntities; i++)
	{
		edict_t* e = INDEXENT(i);
		if (!e || e->free || !e->pvPrivateData)
			continue;
		entvars_t* v = &e->v;
		if (v->movetype != MOVETYPE_TOSS && v->movetype != MOVETYPE_BOUNCE && v->movetype != MOVETYPE_BOUNCEMISSILE)
			continue;
		if ((v->flags & FL_ONGROUND) && v->velocity.IsZero())
		{
			// resting on a voxel that got removed? then fall again
			float below[3] = {v->origin.x, v->origin.y, v->origin.z + v->mins.z - 1.0f};
			if (v->groundentity == INDEXENT(0) && !mcm::WorldPointSolid(below))
			{
				TraceResult tr;
				o_TraceLine(v->origin, Vector(below[0], below[1], below[2]), ignore_monsters, e, &tr);
				if (tr.flFraction >= 1.0f)
					v->flags &= ~FL_ONGROUND;
			}
			continue;
		}
		if (!isfinite(v->velocity.x) || !isfinite(v->velocity.y) || !isfinite(v->velocity.z))
		{
			McLog("entity %s had a non-finite velocity; stopped it", STRING(v->classname));
			v->velocity = g_vecZero;
			continue;
		}
		float g = (v->gravity != 0.0f ? v->gravity : 1.0f) * CVAR_GET_FLOAT("sv_gravity");
		Vector vel = v->velocity;
		vel.z -= g * dt;
		Vector end = v->origin + vel * dt;
		mcw::Trace vt;
		mcm::WorldTrace(v->origin, end, v->mins, v->maxs, vt);
		if (!vt.hit || vt.startsolid)
			continue;
		Vector n(vt.normal[0], vt.normal[1], vt.normal[2]);
		UTIL_SetOrigin(v, Vector(vt.endpos[0], vt.endpos[1], vt.endpos[2]));
		float backoff = DotProduct(v->velocity, n) * (v->movetype == MOVETYPE_BOUNCE ? 1.5f : 1.0f);
		v->velocity = v->velocity - n * backoff;
		if (n.z > 0.7f)
		{
			if (v->velocity.z < 60.0f || v->movetype != MOVETYPE_BOUNCE)
			{
				v->flags |= FL_ONGROUND;
				v->groundentity = INDEXENT(0);
				v->velocity = g_vecZero;
				v->avelocity = g_vecZero;
			}
		}
		// let the entity react (grenade bounce sounds/damping, C4 landing, ...)
		CBaseEntity* ent = CBaseEntity::Instance(e);
		CBaseEntity* world = CBaseEntity::Instance(INDEXENT(0));
		if (ent && world)
			ent->Touch(world);
	}
}

// Find the first door cell (lower half) in the world; returns false if none.
bool FindDoor(int out[3])
{
	if (!g_worldLoaded)
		return false;
	for (int z = 0; z < g_world.sz; z++)
		for (int y = 0; y < g_world.sy; y++)
			for (int x = 0; x < g_world.sx; x++)
			{
				mcw::Cell c = g_world.Get(x, y, z);
				if (c && mcw::Block(mcw::CellType(c)).shape == mcw::SHAPE_DOOR && !(mcw::CellState(c) & 8))
				{
					out[0] = x;
					out[1] = y;
					out[2] = z;
					return true;
				}
			}
	return false;
}

void WorldStartFrame()
{
	// traces with NaN/inf positions are refused (they used to hang the hull check): report where
	static int lastBad = 0, lastBadHull = 0;
	if (mcm::g_worldBadTraces != lastBad || mcb::g_badTraces != lastBadHull)
	{
		const float* b = mcm::g_lastBadTrace;
		McLog("bad traces: world %d, hull %d; last %.1f %.1f %.1f -> %.1f %.1f %.1f", mcm::g_worldBadTraces, mcb::g_badTraces, b[0], b[1],
			b[2], b[3], b[4], b[5]);
		lastBad = mcm::g_worldBadTraces;
		lastBadHull = mcb::g_badTraces;
	}
	if (!g_worldLoaded)
		return;
	VoxelPhysicsPrepass();
	RedstoneFrame();
	if (!g_pending.empty())
		FlushPending();
}
} // namespace mc
