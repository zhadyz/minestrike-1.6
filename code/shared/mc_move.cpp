#ifdef MC_CLIENT
#include "hlsdk_client.h"
#include "in_buttons.h"
#include "const.h"
#else
#include "precompiled.h"
#endif

#include "mc_move.h"
#include "mc_blocks.h"
#include "mc_classic.h"
#include "mc_protocol.h"
#include "mc_sounds_gen.h"
#include "mc_world.h"

#include <math.h>
#include <string.h>

#ifndef CONTENTS_SOLID
#define CONTENTS_SOLID -2
#endif
#ifndef CONTENTS_EMPTY
#define CONTENTS_EMPTY -1
#endif
#ifndef PM_NORMAL
#define PM_NORMAL 0x00000000
#endif

namespace mcm
{
static const mcw::World* g_world = nullptr;
static playermove_s* g_pm = nullptr;

// Engine originals, saved the first time we see each pmove struct's callbacks.
static pmtrace_t (*o_PlayerTrace)(float*, float*, int, int) = nullptr;
static pmtrace_t (*o_PlayerTraceEx)(float*, float*, int, int (*)(physent_t*)) = nullptr;
static pmtrace_t* (*o_TraceLine)(float*, float*, int, int, int) = nullptr;
static pmtrace_t* (*o_TraceLineEx)(float*, float*, int, int, int (*)(physent_t*)) = nullptr;
static int (*o_PointContents)(float*, int*) = nullptr;
static int (*o_TruePointContents)(float*) = nullptr;
static int (*o_TestPlayerPosition)(float*, pmtrace_t*) = nullptr;
static int (*o_TestPlayerPositionEx)(float*, pmtrace_t*, int (*)(physent_t*)) = nullptr;
static void (*o_PlaySound)(int, const char*, float, float, int, int) = nullptr;
static const char* (*o_TraceTexture)(int, float*, float*) = nullptr;

static mcc::Classic* g_classic = nullptr;

void SetWorld(const mcw::World* w) { g_world = w; }
const mcw::World* GetWorld() { return g_world; }
void SetClassic(mcc::Classic* c) { g_classic = c; }
mcc::Classic* GetClassic() { return g_classic; }

static bool ClassicOn() { return g_classic && g_classic->map.loaded; }

float g_lastBadTrace[6];
int g_worldBadTraces = 0;

void WorldTrace(const float start[3], const float end[3], const float mins[3], const float maxs[3], mcw::Trace& out,
	bool* classicStartsolid)
{
	out = mcw::Trace();
	for (int i = 0; i < 3; i++)
		out.endpos[i] = end[i];
	for (int i = 0; i < 3; i++)
		if (!isfinite(start[i]) || !isfinite(end[i]))
		{
			// remember it for the log; a non-finite position is a bug in the caller
			g_worldBadTraces++;
			for (int k = 0; k < 3; k++)
			{
				g_lastBadTrace[k] = start[k];
				g_lastBadTrace[3 + k] = end[k];
			}
			if (classicStartsolid)
				*classicStartsolid = false;
			return;
		}
	if (classicStartsolid)
		*classicStartsolid = false;
	if (g_world)
		mcw::TraceBox(*g_world, start, end, mins, maxs, out);
	if (!ClassicOn())
		return;
	mcc::Result r;
	g_classic->Trace(start, end, mins, maxs, r);
	if (r.startsolid)
	{
		out.startsolid = true;
		// the engine-exact path follows pmove's rule (a trace that starts in solid does not move);
		// the carve-aware path lets a box that grazes a piece slide out of it
		if (classicStartsolid && !r.fallback)
			*classicStartsolid = true;
	}
	if (r.allsolid)
	{
		out.allsolid = true;
		out.fraction = 0.0f;
		out.hit = true;
		for (int i = 0; i < 3; i++)
			out.endpos[i] = start[i];
		return;
	}
	if (r.hit && r.fraction < out.fraction)
	{
		out.fraction = r.fraction;
		out.hit = true;
		out.cell = 0;
		float behind[3];
		for (int i = 0; i < 3; i++)
		{
			out.endpos[i] = r.endpos[i];
			out.normal[i] = r.normal[i];
			behind[i] = r.endpos[i] - r.normal[i] * 1.0f;
		}
		if (g_world)
			g_world->ToBlock(behind, out.block);
	}
}

bool WorldPointSolid(const float p[3])
{
	if (g_world && mcw::PointSolid(*g_world, p))
		return true;
	return ClassicOn() && g_classic->PointContents(p) == mcb::CONT_SOLID;
}

bool WorldTestBox(const float origin[3], const float mins[3], const float maxs[3])
{
	if (g_world && mcw::TestBox(*g_world, origin, mins, maxs))
		return true;
	return ClassicOn() && g_classic->TestBox(origin, mins, maxs);
}

bool WorldPick(const float start[3], const float dir[3], float maxDist, int block[3], int* face, float* dist, bool* classic)
{
	int vb[3], vf = 0;
	float vd = 1e9f;
	bool vhit = g_world && mcw::PickBlock(*g_world, start, dir, maxDist, vb, &vf, &vd);
	int cb[3], cf = 0;
	float cd = 1e9f;
	bool chit = ClassicOn() && g_classic->PickCell(start, dir, maxDist, cb, &cf, &cd);
	if (classic)
		*classic = false;
	if (!vhit && !chit)
		return false;
	if (vhit && (!chit || vd <= cd))
	{
		for (int i = 0; i < 3; i++)
			block[i] = vb[i];
		*face = vf;
		*dist = vd;
		return true;
	}
	// a redstone part or plant lying on the classic surface sits in the cell in front of it, drawn on
	// the surface (not on the grid): aiming at that surface targets the part
	if (g_world)
	{
		static const int off[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
		float q[3];
		for (int i = 0; i < 3; i++)
			q[i] = start[i] + dir[i] * cd + off[cf][i] * 1.0f;
		int p[3];
		g_world->ToBlock(q, p);
		mcw::Cell c = g_world->Get(p[0], p[1], p[2]);
		if (c && mcw::IsPickOnlyShape(g_world->ShapeAt(p[0], p[1], p[2])))
		{
			for (int i = 0; i < 3; i++)
				block[i] = p[i];
			*face = cf;
			*dist = cd;
			return true;
		}
	}
	for (int i = 0; i < 3; i++)
		block[i] = cb[i];
	*face = cf;
	*dist = cd;
	if (classic)
		*classic = true;
	return true;
}

const char* WorldTraceTexture(const float start[3], const float end[3])
{
	if (!ClassicOn())
		return nullptr;
	return g_classic->TextureAt(start, end);
}

static void HullFor(int usehull, float mins[3], float maxs[3])
{
	if (!g_pm || usehull < 0 || usehull > 3)
		usehull = 0;
	for (int i = 0; i < 3; i++)
	{
		mins[i] = g_pm ? g_pm->player_mins[usehull][i] : 0.0f;
		maxs[i] = g_pm ? g_pm->player_maxs[usehull][i] : 0.0f;
	}
}

// Merge the mod world (placed blocks + classic map) into an engine trace: the nearer blocker wins.
// The engine's pmove zeroes the fraction of a trace that starts in solid; the classic map follows that.
static void MergeVoxel(pmtrace_t& tr, const float* start, const float* end, const float mins[3], const float maxs[3])
{
	if (!g_world && !ClassicOn())
		return;
	mcw::Trace vt;
	bool classicSS = false;
	WorldTrace(start, end, mins, maxs, vt, &classicSS);
	if (vt.startsolid)
		tr.startsolid = 1;
	if (vt.allsolid || classicSS)
	{
		if (vt.allsolid)
			tr.allsolid = 1;
		tr.fraction = 0.0f;
		for (int i = 0; i < 3; i++)
			tr.endpos[i] = start[i];
		tr.ent = 0;
		return;
	}
	if (vt.hit && vt.fraction < tr.fraction)
	{
		tr.fraction = vt.fraction;
		for (int i = 0; i < 3; i++)
		{
			tr.endpos[i] = vt.endpos[i];
			tr.plane.normal[i] = vt.normal[i];
		}
		tr.plane.dist = tr.endpos[0] * vt.normal[0] + tr.endpos[1] * vt.normal[1] + tr.endpos[2] * vt.normal[2];
		tr.ent = 0; // the world
		tr.hitgroup = 0;
		tr.inopen = 0;
	}
}

static pmtrace_t W_PlayerTrace(float* start, float* end, int traceFlags, int ignore_pe)
{
	pmtrace_t tr = o_PlayerTrace(start, end, traceFlags, ignore_pe);
	float mins[3], maxs[3];
	HullFor(g_pm ? g_pm->usehull : 0, mins, maxs);
	MergeVoxel(tr, start, end, mins, maxs);
	return tr;
}

static pmtrace_t W_PlayerTraceEx(float* start, float* end, int traceFlags, int (*ignore)(physent_t*))
{
	pmtrace_t tr = o_PlayerTraceEx(start, end, traceFlags, ignore);
	float mins[3], maxs[3];
	HullFor(g_pm ? g_pm->usehull : 0, mins, maxs);
	MergeVoxel(tr, start, end, mins, maxs);
	return tr;
}

static pmtrace_t* W_TraceLine(float* start, float* end, int flags, int usehull, int ignore_pe)
{
	pmtrace_t* tr = o_TraceLine(start, end, flags, usehull, ignore_pe);
	if (tr && (g_world || ClassicOn()))
	{
		float mins[3], maxs[3];
		HullFor(usehull, mins, maxs);
		MergeVoxel(*tr, start, end, mins, maxs);
	}
	return tr;
}

static pmtrace_t* W_TraceLineEx(float* start, float* end, int flags, int usehull, int (*ignore)(physent_t*))
{
	pmtrace_t* tr = o_TraceLineEx(start, end, flags, usehull, ignore);
	if (tr && (g_world || ClassicOn()))
	{
		float mins[3], maxs[3];
		HullFor(usehull, mins, maxs);
		MergeVoxel(*tr, start, end, mins, maxs);
	}
	return tr;
}

static int W_PointContents(float* p, int* truecontents)
{
	if (WorldPointSolid(p))
	{
		if (truecontents)
			*truecontents = CONTENTS_SOLID;
		return CONTENTS_SOLID;
	}
	return o_PointContents(p, truecontents);
}

static int W_TruePointContents(float* p)
{
	if (WorldPointSolid(p))
		return CONTENTS_SOLID;
	return o_TruePointContents(p);
}

static int VoxelBlocksPosition(float* pos)
{
	if (!g_world && !ClassicOn())
		return 0;
	float mins[3], maxs[3];
	HullFor(g_pm ? g_pm->usehull : 0, mins, maxs);
	return WorldTestBox(pos, mins, maxs) ? 1 : 0;
}

static int W_TestPlayerPosition(float* pos, pmtrace_t* ptrace)
{
	int r = o_TestPlayerPosition(pos, ptrace);
	if (r == -1 && VoxelBlocksPosition(pos))
		return 0; // blocked by the world
	return r;
}

static int W_TestPlayerPositionEx(float* pos, pmtrace_t* ptrace, int (*ignore)(physent_t*))
{
	int r = o_TestPlayerPositionEx(pos, ptrace, ignore);
	if (r == -1 && VoxelBlocksPosition(pos))
		return 0;
	return r;
}

void MergeIntoPmTrace(pmtrace_t& tr, const float* start, const float* end, const float mins[3], const float maxs[3])
{
	MergeVoxel(tr, start, end, mins, maxs);
}

// CS footsteps and impact sounds pick a material from the texture under the trace (sound/materials.txt).
// On a classic map the engine only knows the empty shell, so answer with the classic surface.
static const char* W_TraceTexture(int ground, float* vstart, float* vend)
{
	if (ClassicOn() && ground <= 0)
	{
		const char* t = g_classic->TextureAt(vstart, vend);
		if (t)
			return t;
	}
	return o_TraceTexture ? o_TraceTexture(ground, vstart, vend) : nullptr;
}

// Minecraft footsteps: when standing on a voxel block, CS's footstep samples are swapped for the
// block's Minecraft step sound group (stone, wood, sand, gravel, grass, glass, metal, wool). On the
// classic map itself (and in dug cells) CS's own footsteps play, by the dust2 texture underfoot.
static void W_PlaySound(int channel, const char* sample, float volume, float attenuation, int fFlags, int pitch)
{
	if (g_world && g_pm && sample && !strncmp(sample, "player/pl_", 10) && strncmp(sample, "player/pl_jump", 14) &&
		strncmp(sample, "player/pl_fallpain", 18) && strncmp(sample, "player/pl_pain", 14))
	{
		float feet[3] = {g_pm->origin[0], g_pm->origin[1], g_pm->origin[2] + g_pm->player_mins[g_pm->usehull][2] - 4.0f};
		int b[3];
		g_world->ToBlock(feet, b);
		// only blocks you can stand on count (not dug-out cells, dust, plates or plants)
		auto standable = [&](int x, int y, int z) -> mcw::Cell {
			mcw::Cell cc = g_world->Get(x, y, z);
			if (!cc || (ClassicOn() && mcw::CellType(cc) == g_classic->carvedType))
				return 0;
			mcw::LocalBox lb[4];
			return mcw::ShapeBoxes(g_world->ShapeAt(x, y, z), mcw::CellState(cc), lb) > 0 ? cc : 0;
		};
		mcw::Cell c = standable(b[0], b[1], b[2]);
		if (!c)
			c = standable(b[0], b[1], b[2] - 1);
		if (c)
		{
			const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
			if (d.sound == mcw::SOUND_WOOL)
				return; // wool underfoot makes no sound: a quiet way laid across a floor
			const mcs::SoundEvent& ev = mcs::g_sounds[mcs::MCS_BLOCK_BASE + d.sound * 4 + 3];
			int n = ev.numVariants;
			int k = (n > 1 && g_pm->RandomLong) ? g_pm->RandomLong(0, n - 1) : 0;
			o_PlaySound(channel, ev.variants[k].file, volume * 0.8f, attenuation, fFlags, pitch);
			return;
		}
	}
	o_PlaySound(channel, sample, volume, attenuation, fFlags, pitch);
}

template <typename T> static void Swap(T& slot, T& orig, T wrapper)
{
	if (slot != wrapper)
	{
		orig = slot;
		slot = wrapper;
	}
}

void InstallTraceWrappers(playermove_s* pm)
{
	g_pm = pm;
	if (!pm)
		return;
	Swap(pm->PM_PlayerTrace, o_PlayerTrace, &W_PlayerTrace);
	Swap(pm->PM_PlayerTraceEx, o_PlayerTraceEx, &W_PlayerTraceEx);
	Swap(pm->PM_TraceLine, o_TraceLine, &W_TraceLine);
	Swap(pm->PM_TraceLineEx, o_TraceLineEx, &W_TraceLineEx);
	Swap(pm->PM_PointContents, o_PointContents, &W_PointContents);
	Swap(pm->PM_TruePointContents, o_TruePointContents, &W_TruePointContents);
	Swap(pm->PM_TestPlayerPosition, o_TestPlayerPosition, &W_TestPlayerPosition);
	Swap(pm->PM_TestPlayerPositionEx, o_TestPlayerPositionEx, &W_TestPlayerPositionEx);
	Swap(pm->PM_PlaySound, o_PlaySound, &W_PlaySound);
	Swap(pm->PM_TraceTexture, o_TraceTexture, &W_TraceTexture);
}

// ---------------------------------------------------------------------------------------------
// Elytra

static void Forward(const float angles[3], float f[3])
{
	const float d2r = 3.14159265358979f / 180.0f;
	float p = angles[0] * d2r, y = angles[1] * d2r;
	f[0] = cosf(p) * cosf(y);
	f[1] = cosf(p) * sinf(y);
	f[2] = -sinf(p);
}

// Slide-move with collision, adapted from the Half-Life SDK's PM_FlyMove. Returns true if we
// touched ground (a surface with normal z > 0.7) this move.
static bool SlideMove(playermove_s* pm)
{
	const int MAX_CLIP = 5;
	float planes[MAX_CLIP][3];
	int numplanes = 0;
	float time_left = pm->frametime;
	float original[3], primal[3];
	bool landed = false;
	for (int i = 0; i < 3; i++)
		original[i] = primal[i] = pm->velocity[i];

	for (int bump = 0; bump < 4; bump++)
	{
		if (!pm->velocity[0] && !pm->velocity[1] && !pm->velocity[2])
			break;
		float end[3];
		for (int i = 0; i < 3; i++)
			end[i] = pm->origin[i] + time_left * pm->velocity[i];
		pmtrace_t tr = pm->PM_PlayerTrace(pm->origin, end, PM_NORMAL, -1);
		if (tr.allsolid)
		{
			for (int i = 0; i < 3; i++)
				pm->velocity[i] = 0.0f;
			break;
		}
		if (tr.fraction > 0.0f)
		{
			for (int i = 0; i < 3; i++)
			{
				pm->origin[i] = tr.endpos[i];
				original[i] = pm->velocity[i];
			}
			numplanes = 0;
		}
		if (tr.fraction == 1.0f)
			break;
		if (tr.plane.normal[2] > 0.7f)
			landed = true;
		time_left -= time_left * tr.fraction;
		if (numplanes >= MAX_CLIP)
		{
			for (int i = 0; i < 3; i++)
				pm->velocity[i] = 0.0f;
			break;
		}
		for (int i = 0; i < 3; i++)
			planes[numplanes][i] = tr.plane.normal[i];
		numplanes++;

		// clip velocity against all planes we have touched
		int i;
		for (i = 0; i < numplanes; i++)
		{
			float backoff = original[0] * planes[i][0] + original[1] * planes[i][1] + original[2] * planes[i][2];
			for (int k = 0; k < 3; k++)
			{
				pm->velocity[k] = original[k] - planes[i][k] * backoff;
				if (pm->velocity[k] > -0.1f && pm->velocity[k] < 0.1f)
					pm->velocity[k] = 0.0f;
			}
			int j;
			for (j = 0; j < numplanes; j++)
				if (j != i &&
					pm->velocity[0] * planes[j][0] + pm->velocity[1] * planes[j][1] + pm->velocity[2] * planes[j][2] < 0)
					break;
			if (j == numplanes)
				break;
		}
		if (i == numplanes)
		{
			if (numplanes != 2)
			{
				for (int k = 0; k < 3; k++)
					pm->velocity[k] = 0.0f;
				break;
			}
			// slide along the crease
			float dir[3] = {planes[0][1] * planes[1][2] - planes[0][2] * planes[1][1],
				planes[0][2] * planes[1][0] - planes[0][0] * planes[1][2],
				planes[0][0] * planes[1][1] - planes[0][1] * planes[1][0]};
			float d = dir[0] * pm->velocity[0] + dir[1] * pm->velocity[1] + dir[2] * pm->velocity[2];
			for (int k = 0; k < 3; k++)
				pm->velocity[k] = dir[k] * d;
		}
		if (pm->velocity[0] * primal[0] + pm->velocity[1] * primal[1] + pm->velocity[2] * primal[2] <= 0)
		{
			for (int k = 0; k < 3; k++)
				pm->velocity[k] = 0.0f;
			break;
		}
	}
	return landed;
}

static void GlideMove(playermove_s* pm)
{
	float look[3];
	Forward(pm->angles, look);
	const float d2r = 3.14159265358979f / 180.0f;
	float pitch = pm->angles[0] * d2r;
	float k = pm->frametime * TICKS_PER_SEC; // Minecraft ticks covered by this command

	// to Minecraft units (blocks per tick)
	float v[3];
	for (int i = 0; i < 3; i++)
		v[i] = pm->velocity[i] / MC_TO_UNITS;

	float lookH = sqrtf(look[0] * look[0] + look[1] * look[1]);
	float velH = sqrtf(v[0] * v[0] + v[1] * v[1]);
	float cosp = cosf(pitch);
	float lift = cosp * cosp;
	const float gravity = 0.08f;

	// LivingEntity.updateFallFlyingMovement, applied as a per-tick rate over k ticks
	v[2] += gravity * (-1.0f + lift * 0.75f) * k;
	if (v[2] < 0.0f && lookH > 0.0f)
	{
		float d = v[2] * -0.1f * lift * k;
		v[0] += look[0] * d / lookH;
		v[1] += look[1] * d / lookH;
		v[2] += d;
	}
	if (pitch < 0.0f && lookH > 0.0f)
	{
		float d = velH * -sinf(pitch) * 0.04f * k;
		v[0] -= look[0] * d / lookH;
		v[1] -= look[1] * d / lookH;
		v[2] += d * 3.2f;
	}
	if (lookH > 0.0f)
	{
		float s = 0.1f * k;
		if (s > 1.0f)
			s = 1.0f;
		v[0] += (look[0] / lookH * velH - v[0]) * s;
		v[1] += (look[1] / lookH * velH - v[1]) * s;
	}
	float dxy = powf(0.99f, k), dz = powf(0.98f, k);
	v[0] *= dxy;
	v[1] *= dxy;
	v[2] *= dz;

	// Firework rocket boost (FireworkRocketEntity.tick while attached to a gliding player)
	if (pm->vuser1[0] > 0.0f)
	{
		float s = 0.5f * k;
		if (s > 1.0f)
			s = 1.0f;
		for (int i = 0; i < 3; i++)
			v[i] += look[i] * 0.1f * k + (look[i] * 1.5f - v[i]) * s;
	}

	for (int i = 0; i < 3; i++)
		pm->velocity[i] = v[i] * MC_TO_UNITS;

	// clamp to the engine's max velocity so nothing explodes
	float maxv = pm->movevars ? pm->movevars->maxvelocity : 2000.0f;
	for (int i = 0; i < 3; i++)
	{
		if (pm->velocity[i] > maxv)
			pm->velocity[i] = maxv;
		if (pm->velocity[i] < -maxv)
			pm->velocity[i] = -maxv;
	}

	bool landed = SlideMove(pm);
	pm->flFallVelocity = -pm->velocity[2];
	if (landed)
	{
		pm->iuser4 &= ~mcp::MCPF_GLIDING;
		pm->onground = 0;
	}
	else
	{
		pm->onground = -1;
	}
}

bool PreMove(playermove_s* pm)
{
	if (!pm || pm->dead || pm->spectator || pm->iuser1 > 0)
		return false;

	// firework boost timer
	if (pm->vuser1[0] > 0.0f)
	{
		pm->vuser1[0] -= pm->frametime;
		if (pm->vuser1[0] < 0.0f)
			pm->vuser1[0] = 0.0f;
	}

	int flags = pm->iuser4;
	bool hasElytra = (flags & mcp::MCPF_ELYTRA) != 0;
	bool gliding = (flags & mcp::MCPF_GLIDING) != 0;

	if (gliding && (!hasElytra || pm->waterlevel >= 2 || pm->movetype != MOVETYPE_WALK))
	{
		pm->iuser4 &= ~mcp::MCPF_GLIDING;
		return false;
	}

	if (!gliding)
	{
		bool jumpPressed = (pm->cmd.buttons & IN_JUMP) && !(pm->oldbuttons & IN_JUMP);
		if (hasElytra && jumpPressed && pm->onground == -1 && pm->waterlevel < 2 && pm->movetype == MOVETYPE_WALK)
		{
			pm->iuser4 |= mcp::MCPF_GLIDING;
			gliding = true;
		}
	}

	if (!gliding)
		return false;

	GlideMove(pm);
	pm->oldbuttons = pm->cmd.buttons;
	return true;
}
} // namespace mcm
