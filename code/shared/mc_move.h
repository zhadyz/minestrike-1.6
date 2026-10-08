// Shared player-movement extensions, compiled into both DLLs:
//  - wraps the engine's pmove trace callbacks so every movement trace also collides with voxels,
//  - Minecraft elytra gliding + firework boost (exact Java formulas, integrated per frame).
// Client and server must run identical code on identical inputs so prediction stays in sync.
#pragma once

struct playermove_s;

namespace mcw
{
struct World;
struct Trace;
}
namespace mcc
{
class Classic;
}

namespace mcm
{
// World used for movement collision in this DLL (nullptr = no voxel world on this map).
void SetWorld(const mcw::World* w);
const mcw::World* GetWorld();

// Classic map used as the world in this DLL (nullptr = none).
void SetClassic(mcc::Classic* c);
mcc::Classic* GetClassic();

// The whole mod world for a box sweep: placed blocks + the classic map (if any). Engine-like result
// (startsolid/allsolid/fraction/normal); classicStartsolid tells which layer started in solid.
void WorldTrace(const float start[3], const float end[3], const float mins[3], const float maxs[3], mcw::Trace& out,
	bool* classicStartsolid = nullptr);
bool WorldPointSolid(const float p[3]);
extern int g_worldBadTraces;     // traces refused for NaN/inf positions
extern float g_lastBadTrace[6];  // the last one's start and end
bool WorldTestBox(const float origin[3], const float mins[3], const float maxs[3]);
// Mining/placing target along a ray: placed blocks and classic surfaces, nearest first.
// classic = true when the hit is the classic map (block = the cell behind the surface).
bool WorldPick(const float start[3], const float dir[3], float maxDist, int block[3], int* face, float* dist, bool* classic);

// The texture the engine's TraceTexture would report for the mod world: on a classic map, the classic
// surface hit (CS footsteps, bullet impact sounds and penetration use it); nullptr = ask the engine.
const char* WorldTraceTexture(const float start[3], const float end[3]);
// Merge the mod world into an engine point/hull trace result (the nearer blocker wins).
void MergeIntoPmTrace(struct pmtrace_s& tr, const float* start, const float* end, const float mins[3], const float maxs[3]);

// Point the pmove trace callbacks at our wrappers (idempotent; call before every PM run).
void InstallTraceWrappers(struct playermove_s* pm);

// Run before the regular CS movement code. Returns true if Minecraft movement handled this command
// completely (elytra gliding) and the regular PM_Move must be skipped.
bool PreMove(struct playermove_s* pm);

// Elytra/boost tuning
static const float UNITS_PER_BLOCK = 40.0f;
static const float TICKS_PER_SEC = 20.0f;
// velocity conversion: Minecraft blocks/tick <-> engine units/second
static const float MC_TO_UNITS = UNITS_PER_BLOCK * TICKS_PER_SEC; // 800
} // namespace mcm
