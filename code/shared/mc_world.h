// Shared voxel world: storage, block shapes, and collision queries.
// Compiled into BOTH the client proxy (client.dll) and the server game DLL (mp.dll), so the two sides
// compute identical collision results and client-side movement prediction stays exact.
//
// Coordinates follow GoldSrc: X/Y horizontal, Z up, units are engine units.
// One block is MC_BLOCK_SIZE units on each side. Block (bx,by,bz) occupies
//   [origin + b*MC_BLOCK_SIZE, origin + (b+1)*MC_BLOCK_SIZE) on each axis.
//
// No engine headers here: plain C++17, no exceptions, no allocation in queries.
#pragma once
#include <stdint.h>
#include <stddef.h>

#define MC_BLOCK_SIZE 40.0f

namespace mcw
{
// ---------------------------------------------------------------------------------------------
// Cells
//
// A cell is uint16: low 10 bits = block type id (index into the global block registry, 0 = air),
// high 6 bits = per-shape state.
typedef uint16_t Cell;
inline uint16_t CellType(Cell c) { return c & 0x3FF; }
inline uint16_t CellState(Cell c) { return c >> 10; }
inline Cell MakeCell(uint16_t type, uint16_t state) { return (Cell)((type & 0x3FF) | ((state & 0x3F) << 10)); }

// Shape kinds. State bit meanings per shape:
//   SHAPE_CUBE:   unused
//   SHAPE_SLAB:   bit0 = top half (0 = bottom slab occupying z in [0,0.5], 1 = top slab z in [0.5,1])
//   SHAPE_STAIRS: bits0-1 = facing (0=+X,1=+Y,2=-X,3=-Y: the direction the full-height back part is on),
//                 bit2 = upside down
//   SHAPE_DOOR:   bits0-1 = facing (0=+X,1=+Y,2=-X,3=-Y: side of the cell the closed panel sits on),
//                 bit2 = open, bit3 = upper half, bit4 = hinge right
//   SHAPE_PANE:   (iron bars / glass panes) treated as a thin centre post + arms; v1: full-height thin
//                 box 7/16..9/16 on both axes
//   SHAPE_CROSS:  plants, no collision
//   SHAPE_NONE:   air / non-solid (no collision)
// Redstone components (no collision; picked with PickBoxes). "attach" = where the supporting block is:
// 0 = below (floor), 1..4 = wall at +X,+Y,-X,-Y, 5 = above (ceiling).
//   SHAPE_DUST:     bits0-3 = power level 0..15
//   SHAPE_TORCH:    bits0-2 = attach (0..4), bit3 = unlit
//   SHAPE_LEVER:    bits0-2 = attach (0..5), bit3 = on, bit4 = floor/ceiling lever runs along Y
//   SHAPE_BUTTON:   bits0-2 = attach (0..5), bit3 = pressed, bit4 = floor/ceiling button runs along Y
//   SHAPE_PLATE:    bit0 = pressed
//   SHAPE_REPEATER: bits0-1 = output facing (0=+X,1=+Y,2=-X,3=-Y), bits2-3 = delay-1, bit4 = powered
enum ShapeKind : uint8_t
{
	SHAPE_NONE = 0,
	SHAPE_CUBE,
	SHAPE_SLAB,
	SHAPE_STAIRS,
	SHAPE_DOOR,
	SHAPE_PANE,
	SHAPE_CROSS,
	SHAPE_DUST,
	SHAPE_TORCH,
	SHAPE_LEVER,
	SHAPE_BUTTON,
	SHAPE_PLATE,
	SHAPE_REPEATER,
};

inline bool IsRedstoneShape(ShapeKind s) { return s >= SHAPE_DUST && s <= SHAPE_REPEATER; }
// Shapes the player can target but not collide with (plants, redstone parts).
inline bool IsPickOnlyShape(ShapeKind s) { return s == SHAPE_CROSS || IsRedstoneShape(s); }
// Attach direction (towards the supporting block) of a torch/lever/button state.
inline int AttachOf(ShapeKind s, uint16_t state)
{
	return (s == SHAPE_TORCH || s == SHAPE_LEVER || s == SHAPE_BUTTON) ? (state & 7) : 0;
}
inline void AttachVec(int attach, int out[3])
{
	static const int v[6][3] = {{0, 0, -1}, {1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}};
	int a = attach >= 0 && attach < 6 ? attach : 0;
	out[0] = v[a][0];
	out[1] = v[a][1];
	out[2] = v[a][2];
}

// Door panel thickness in block units (Minecraft: 3/16).
static const float DOOR_THICKNESS = 3.0f / 16.0f;

// A box in block-local units, each component in [0,1].
struct LocalBox
{
	float mins[3];
	float maxs[3];
};

// Returns the number of collision boxes (0..4) for a shape+state, writing them to out.
// This is the single source of truth for block collision; renderers should match it.
int ShapeBoxes(ShapeKind shape, uint16_t state, LocalBox out[4]);
// Selection boxes (what the crosshair targets and the outline shows): ShapeBoxes, plus boxes for
// the pick-only shapes (plants, redstone parts).
int PickBoxes(ShapeKind shape, uint16_t state, LocalBox out[4]);

// ---------------------------------------------------------------------------------------------
// World

struct World
{
	int sx = 0, sy = 0, sz = 0;        // size in blocks
	float origin[3] = {0, 0, 0};       // world-space position of block (0,0,0)'s min corner
	Cell* cells = nullptr;             // sx*sy*sz cells, index = (bz*sy + by)*sx + bx
	const uint8_t* shapeOfType = nullptr; // [1024] ShapeKind per block type id (from the registry)

	bool InBounds(int x, int y, int z) const { return x >= 0 && y >= 0 && z >= 0 && x < sx && y < sy && z < sz; }
	Cell Get(int x, int y, int z) const { return InBounds(x, y, z) ? cells[((size_t)z * sy + y) * sx + x] : 0; }
	void Set(int x, int y, int z, Cell c)
	{
		if (InBounds(x, y, z))
			cells[((size_t)z * sy + y) * sx + x] = c;
	}
	ShapeKind ShapeAt(int x, int y, int z) const
	{
		Cell c = Get(x, y, z);
		return c ? (ShapeKind)shapeOfType[CellType(c)] : SHAPE_NONE;
	}
	// World position -> block coordinate (floor). Valid even outside bounds.
	void ToBlock(const float p[3], int b[3]) const;
};

// ---------------------------------------------------------------------------------------------
// Collision queries. Semantics deliberately mirror GoldSrc's trace_t / pmtrace_t so callers can
// merge a voxel result with an engine result by taking the smaller fraction.

static const float DIST_EPSILON = 0.03125f; // same as the engine

struct Trace
{
	float fraction = 1.0f;     // 0..1 of the move completed before contact
	float endpos[3] = {0, 0, 0};
	float normal[3] = {0, 0, 0}; // surface normal of the face hit (axis aligned), zero if no hit
	bool startsolid = false;   // the box at 'start' already overlaps solid voxels
	bool allsolid = false;     // the box is in solid for the whole move (start AND end overlap)
	bool hit = false;          // fraction < 1 because of a voxel
	int block[3] = {-1, -1, -1}; // block that stopped the move (valid when hit)
	Cell cell = 0;
};

// Sweep an axis-aligned box (mins/maxs relative to the moving point; mins <= 0 <= maxs typically)
// from start to end against all solid voxel boxes.
//  - Touching a surface exactly (distance 0) is NOT overlap; only penetration deeper than
//    1/1024 unit counts for startsolid.
//  - On a hit, fraction is pulled back so the box stops DIST_EPSILON short of the surface along the
//    move (like the engine), never below 0. endpos = start + fraction*(end-start).
//  - If startsolid, the trace still sweeps: fraction reports the first entry into a *different*
//    blocker; if the box remains overlapping at end, allsolid = true and fraction = 0.
//  - Blocks outside the world bounds are empty (air).
//  - Point traces are mins = maxs = 0.
void TraceBox(const World& w, const float start[3], const float end[3], const float mins[3], const float maxs[3],
	Trace& out);

// True if the box at 'origin' penetrates any solid voxel box (same tolerance as above).
bool TestBox(const World& w, const float origin[3], const float mins[3], const float maxs[3]);

// True if the point is strictly inside a solid voxel box.
bool PointSolid(const World& w, const float p[3]);

// Block picking for mining/placing (Minecraft-style): walk the ray voxel by voxel (Amanatidis-Woo
// DDA) up to maxDist units, testing the actual shape boxes of each non-air cell (so a slab or open
// door is only hit where its geometry is). Returns true on a hit; block = hit cell, face = outward
// normal axis of the hit face as 0..5 (0=+X,1=-X,2=+Y,3=-Y,4=+Z,5=-Z), dist = distance along ray.
// Also counts SHAPE_CROSS cells (plants) as pickable using a 0.2..0.8 centred box.
bool PickBlock(const World& w, const float start[3], const float dir[3], float maxDist, int block[3], int* face,
	float* dist);

} // namespace mcw
