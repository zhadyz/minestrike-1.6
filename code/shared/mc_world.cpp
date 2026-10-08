// Shared voxel world: shape tables and collision queries. See mc_world.h for the contract and
// NOTES.md for the design. Depends only on mc_world.h and the C standard library.
//
// All geometry is evaluated in double precision. Every float input is exactly representable in a
// double, and every block / shape coordinate is a dyadic multiple of 2.5 units added to a float origin, so
// "touching" comparisons (box face == block face) are exact rather than rounding-dependent. The same code
// in client.dll and mp.dll therefore yields the same bits.
#include "mc_world.h"
#include <math.h>

namespace mcw
{

// ---------------------------------------------------------------------------------------------
// Shapes

static inline LocalBox MakeBox(float x0, float y0, float z0, float x1, float y1, float z1)
{
	LocalBox b = {{x0, y0, z0}, {x1, y1, z1}};
	return b;
}

int ShapeBoxes(ShapeKind shape, uint16_t state, LocalBox out[4])
{
	switch (shape)
	{
	case SHAPE_CUBE:
		out[0] = MakeBox(0, 0, 0, 1, 1, 1);
		return 1;

	case SHAPE_SLAB:
		if (state & 1)
			out[0] = MakeBox(0, 0, 0.5f, 1, 1, 1);
		else
			out[0] = MakeBox(0, 0, 0, 1, 1, 0.5f);
		return 1;

	case SHAPE_STAIRS:
	{
		const int facing = state & 3;
		const bool upsideDown = (state & 4) != 0;
		// Full-footprint half slab, then the full-height half ("back") on the facing side.
		const float slabZ0 = upsideDown ? 0.5f : 0.0f;
		const float backZ0 = upsideDown ? 0.0f : 0.5f;
		out[0] = MakeBox(0, 0, slabZ0, 1, 1, slabZ0 + 0.5f);
		switch (facing)
		{
		case 0: out[1] = MakeBox(0.5f, 0, backZ0, 1, 1, backZ0 + 0.5f); break;    // +X
		case 1: out[1] = MakeBox(0, 0.5f, backZ0, 1, 1, backZ0 + 0.5f); break;    // +Y
		case 2: out[1] = MakeBox(0, 0, backZ0, 0.5f, 1, backZ0 + 0.5f); break;    // -X
		default: out[1] = MakeBox(0, 0, backZ0, 1, 0.5f, backZ0 + 0.5f); break;   // -Y
		}
		return 2;
	}

	case SHAPE_DOOR:
	{
		const int facing = state & 3;
		const bool open = (state & 4) != 0;
		const bool hingeRight = (state & 16) != 0;
		// Closed: panel on the facing side. Open: it swings a quarter turn about the hinge.
		const int side = open ? (facing + (hingeRight ? 3 : 1)) & 3 : facing;
		LocalBox b = MakeBox(0, 0, 0, 1, 1, 1);
		switch (side)
		{
		case 0: b.mins[0] = 1.0f - DOOR_THICKNESS; break;
		case 1: b.mins[1] = 1.0f - DOOR_THICKNESS; break;
		case 2: b.maxs[0] = DOOR_THICKNESS; break;
		default: b.maxs[1] = DOOR_THICKNESS; break;
		}
		out[0] = b;
		return 1;
	}

	case SHAPE_PANE:
		out[0] = MakeBox(7.0f / 16.0f, 7.0f / 16.0f, 0, 9.0f / 16.0f, 9.0f / 16.0f, 1);
		return 1;

	case SHAPE_NONE:
	case SHAPE_CROSS:
	default:
		return 0;
	}
}

// A box given in pixels for a part on the floor, turned so its "down" points at the attach direction.
static LocalBox AttachedBox(int attach, float x0, float y0, float z0, float x1, float y1, float z1)
{
	const float s = 1.0f / 16.0f;
	float lo[3] = {x0 * s, y0 * s, z0 * s}, hi[3] = {x1 * s, y1 * s, z1 * s};
	LocalBox b;
	auto set = [&](int axis, float a, float c) {
		b.mins[axis] = a < c ? a : c;
		b.maxs[axis] = a < c ? c : a;
	};
	switch (attach)
	{
	case 1: set(0, 1 - lo[2], 1 - hi[2]); set(1, lo[1], hi[1]); set(2, lo[0], hi[0]); break; // wall at +X
	case 3: set(0, lo[2], hi[2]); set(1, lo[1], hi[1]); set(2, lo[0], hi[0]); break;         // wall at -X
	case 2: set(0, lo[0], hi[0]); set(1, 1 - lo[2], 1 - hi[2]); set(2, lo[1], hi[1]); break; // wall at +Y
	case 4: set(0, lo[0], hi[0]); set(1, lo[2], hi[2]); set(2, lo[1], hi[1]); break;         // wall at -Y
	case 5: set(0, lo[0], hi[0]); set(1, lo[1], hi[1]); set(2, 1 - lo[2], 1 - hi[2]); break; // ceiling
	default: set(0, lo[0], hi[0]); set(1, lo[1], hi[1]); set(2, lo[2], hi[2]); break;        // floor
	}
	return b;
}

int PickBoxes(ShapeKind shape, uint16_t state, LocalBox out[4])
{
	const float s = 1.0f / 16.0f;
	switch (shape)
	{
	case SHAPE_CROSS:
		out[0] = MakeBox(0.2f, 0.2f, 0.0f, 0.8f, 0.8f, 0.8f);
		return 1;
	case SHAPE_DUST:
		out[0] = MakeBox(0, 0, 0, 1, 1, 1 * s);
		return 1;
	case SHAPE_TORCH:
		if ((state & 7) == 0)
			out[0] = MakeBox(6 * s, 6 * s, 0, 10 * s, 10 * s, 10 * s);
		else
			out[0] = AttachedBox(state & 7, 3, 5.5f, 0, 13, 10.5f, 5);
		return 1;
	case SHAPE_LEVER:
		out[0] = AttachedBox(state & 7, 4, 4, 0, 12, 12, 10);
		return 1;
	case SHAPE_BUTTON:
	{
		// the 6px side runs along Y for floor/ceiling buttons with bit4, and for buttons on X walls
		int a = state & 7;
		bool alongY = (a == 0 || a == 5) ? (state & 16) != 0 : (a == 1 || a == 3);
		float h = (state & 8) ? 1.0f : 2.0f;
		out[0] = alongY ? AttachedBox(a, 6, 5, 0, 10, 11, h) : AttachedBox(a, 5, 6, 0, 11, 10, h);
		return 1;
	}
	case SHAPE_PLATE:
		out[0] = MakeBox(1 * s, 1 * s, 0, 15 * s, 15 * s, ((state & 1) ? 0.5f : 1.0f) * s);
		return 1;
	case SHAPE_REPEATER:
		out[0] = MakeBox(0, 0, 0, 1, 1, 2 * s);
		return 1;
	default:
		return ShapeBoxes(shape, state, out);
	}
}

// ---------------------------------------------------------------------------------------------
// Internal helpers

namespace
{

const double kBlock = (double)MC_BLOCK_SIZE;
const double kDistEps = (double)DIST_EPSILON;
// Penetration deeper than this counts as overlap (startsolid / TestBox / PointSolid). Power of two so
// that comparisons against block faces stay exact.
const double kTol = 1.0 / 1024.0;
// Slack added to swept bounds before the cell lookup so that floating-point error in interpolated
// positions can never drop a touched cell.
const double kPad = 1.0 / 4096.0;
// A swept volume touching more cells than this is walked in segments instead of as one bounding volume.
const double kMaxDirectCells = 64.0;
// Segment length (units) used for the walk; one block keeps each segment's bounds to a few cells.
const double kSegmentLen = kBlock;

// Ties between equally early entries prefer Z, then X, then Y (so a diagonal landing on an edge reports
// the floor rather than the wall).
const int kAxisOrder[3] = {2, 0, 1};

struct WBox
{
	double lo[3];
	double hi[3];
};

struct CellRange
{
	int lo[3];
	int hi[3];
	bool Contains(int x, int y, int z) const
	{
		return x >= lo[0] && x <= hi[0] && y >= lo[1] && y <= hi[1] && z >= lo[2] && z <= hi[2];
	}
};

inline bool Finite3(const float v[3])
{
	// Written so that NaN fails the comparison.
	return fabsf(v[0]) < 1.0e30f && fabsf(v[1]) < 1.0e30f && fabsf(v[2]) < 1.0e30f;
}

inline bool WorldValid(const World& w)
{
	return w.cells && w.shapeOfType && w.sx > 0 && w.sy > 0 && w.sz > 0;
}

// Collision boxes of one cell in world space. Cross cells contribute their pick box only when asked.
int LoadBoxes(const World& w, int bx, int by, int bz, Cell c, bool includeCross, WBox out[4])
{
	const uint16_t type = CellType(c);
	if (type == 0)
		return 0;
	const double base[3] = {(double)w.origin[0] + (double)bx * kBlock, (double)w.origin[1] + (double)by * kBlock,
		(double)w.origin[2] + (double)bz * kBlock};
	const ShapeKind shape = (ShapeKind)w.shapeOfType[type];
	if (shape == SHAPE_CUBE)
	{
		for (int a = 0; a < 3; ++a)
		{
			out[0].lo[a] = base[a];
			out[0].hi[a] = base[a] + kBlock;
		}
		return 1;
	}
	LocalBox lb[4];
	int n;
	if (IsPickOnlyShape(shape))
	{
		if (!includeCross)
			return 0;
		n = PickBoxes(shape, CellState(c), lb);
	}
	else
		n = ShapeBoxes(shape, CellState(c), lb);
	for (int i = 0; i < n; ++i)
		for (int a = 0; a < 3; ++a)
		{
			out[i].lo[a] = base[a] + (double)lb[i].mins[a] * kBlock;
			out[i].hi[a] = base[a] + (double)lb[i].maxs[a] * kBlock;
		}
	return n;
}

// Cells whose extent intersects the box [lo,hi] (clamped to the world). 'closed' also accepts cells that
// only touch the box at a face (needed when a sweep may end exactly in contact); otherwise only cells
// whose open interior is entered. Returns false when no cell qualifies.
bool CellSpan(const World& w, const double lo[3], const double hi[3], bool closed, CellRange& r)
{
	const int n[3] = {w.sx, w.sy, w.sz};
	for (int a = 0; a < 3; ++a)
	{
		const double rl = (lo[a] - (double)w.origin[a]) / kBlock;
		const double rh = (hi[a] - (double)w.origin[a]) / kBlock;
		double c0, c1;
		if (closed)
		{
			c0 = ceil(rl) - 1.0;
			c1 = floor(rh);
		}
		else
		{
			c0 = floor(rl);
			c1 = ceil(rh) - 1.0;
		}
		const double top = (double)(n[a] - 1);
		if (!(c1 >= 0.0 && c0 <= top)) // also rejects NaN
			return false;
		if (c0 < 0.0)
			c0 = 0.0;
		if (c1 > top)
			c1 = top;
		if (c0 > c1)
			return false;
		r.lo[a] = (int)c0;
		r.hi[a] = (int)c1;
	}
	return true;
}

// Box [mlo,mhi] penetrates box b by more than kTol on every axis.
inline bool Penetrates(const double mlo[3], const double mhi[3], const WBox& b)
{
	for (int a = 0; a < 3; ++a)
		if (!(mhi[a] - b.lo[a] > kTol && b.hi[a] - mlo[a] > kTol))
			return false;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Swept box state

struct Best
{
	bool valid;
	double t;       // entry time clamped to >= 0
	size_t key;     // cellIndex*4 + boxIndex, the canonical tie-break
	int axis;       // entry axis (the axis whose face was crossed last)
	int block[3];
	Cell cell;
};

struct StartHit
{
	bool valid;
	size_t key;
	int block[3];
	Cell cell;
	WBox box;
};

struct Sweep
{
	const World* w;
	double p[3];   // start
	double v[3];   // end - start
	double mn[3];
	double mx[3];
	bool moving;
	Best best;
	StartHit startHit;
};

// Visits every box of one cell: records overlap with the start position, and the earliest entry of the
// moving box into the others.
//
// The moving box (centre p, extents mn..mx) against a block box [lo,hi] is the point p against the
// Minkowski box E = [lo - mx, hi - mn]. The entry time on each axis is the exact time the point crosses
// the face of E. The exit time (and the "already inside" test for axes with no motion) use faces pulled
// inward by kTol, so a box that penetrates by <= kTol is treated as merely touching: it neither counts as
// overlapping at the start nor gets blocked by that surface when sliding along it.
inline void VisitCell(Sweep& s, int bx, int by, int bz)
{
	const World& w = *s.w;
	const size_t idx = ((size_t)bz * (size_t)w.sy + (size_t)by) * (size_t)w.sx + (size_t)bx;
	const Cell c = w.cells[idx];
	if (!c)
		return;
	WBox boxes[4];
	const int nb = LoadBoxes(w, bx, by, bz, c, false, boxes);
	for (int i = 0; i < nb; ++i)
	{
		const WBox& b = boxes[i];
		double elo[3], ehi[3];
		for (int a = 0; a < 3; ++a)
		{
			elo[a] = b.lo[a] - s.mx[a];
			ehi[a] = b.hi[a] - s.mn[a];
		}
		const size_t key = idx * 4 + (size_t)i;

		// Overlap at the start: ignore this box for the sweep, remember it for the allsolid report.
		if (s.p[0] > elo[0] + kTol && s.p[0] < ehi[0] - kTol && s.p[1] > elo[1] + kTol && s.p[1] < ehi[1] - kTol &&
			s.p[2] > elo[2] + kTol && s.p[2] < ehi[2] - kTol)
		{
			if (!s.startHit.valid || key < s.startHit.key)
			{
				s.startHit.valid = true;
				s.startHit.key = key;
				s.startHit.block[0] = bx;
				s.startHit.block[1] = by;
				s.startHit.block[2] = bz;
				s.startHit.cell = c;
				s.startHit.box = b;
			}
			continue;
		}
		if (!s.moving)
			continue;

		// Slab test. A hit needs entry <= 1 and no later than the best so far; we cut off at both.
		const double cutoff = (s.best.valid && s.best.t < 1.0) ? s.best.t : 1.0;
		double tIn = -HUGE_VAL, tOut = HUGE_VAL;
		int axisIn = -1;
		bool miss = false;
		for (int k = 0; k < 3; ++k)
		{
			const int a = kAxisOrder[k];
			const double va = s.v[a], pa = s.p[a];
			if (va == 0.0)
			{
				// No motion on this axis: must already be inside (by more than kTol), else it never overlaps.
				if (!(pa > elo[a] + kTol && pa < ehi[a] - kTol))
				{
					miss = true;
					break;
				}
				continue;
			}
			double tn, tf;
			if (va > 0.0)
			{
				tn = (elo[a] - pa) / va;
				tf = (ehi[a] - kTol - pa) / va;
			}
			else
			{
				tn = (ehi[a] - pa) / va;
				tf = (elo[a] + kTol - pa) / va;
			}
			if (tn > tIn)
			{
				tIn = tn;
				axisIn = a;
			}
			if (tf < tOut)
				tOut = tf;
			if (tIn > cutoff || tIn >= tOut || tOut <= 0.0)
			{
				miss = true;
				break;
			}
		}
		if (miss)
			continue;

		const double t = tIn > 0.0 ? tIn : 0.0; // sub-tolerance penetration entered "in the past": hit at 0
		if (!s.best.valid || t < s.best.t || (t == s.best.t && key < s.best.key))
		{
			s.best.valid = true;
			s.best.t = t;
			s.best.key = key;
			s.best.axis = axisIn;
			s.best.block[0] = bx;
			s.best.block[1] = by;
			s.best.block[2] = bz;
			s.best.cell = c;
		}
	}
}

// Visit every cell of 'r' except those already covered by 'skip' (the previous segment's range).
void VisitRange(Sweep& s, const CellRange& r, const CellRange* skip)
{
	for (int z = r.lo[2]; z <= r.hi[2]; ++z)
		for (int y = r.lo[1]; y <= r.hi[1]; ++y)
			for (int x = r.lo[0]; x <= r.hi[0]; ++x)
			{
				if (skip && skip->Contains(x, y, z))
					continue;
				VisitCell(s, x, y, z);
			}
}

// Bounds of the moving box over the parameter interval [ta,tb], padded.
void SweptBounds(const Sweep& s, double ta, double tb, double lo[3], double hi[3])
{
	for (int a = 0; a < 3; ++a)
	{
		const double pa = s.p[a] + s.v[a] * ta;
		const double pb = s.p[a] + s.v[a] * tb;
		lo[a] = (pa < pb ? pa : pb) + s.mn[a] - kPad;
		hi[a] = (pa > pb ? pa : pb) + s.mx[a] + kPad;
	}
}

// Outward normal (axis, sign) of the box face the start box would leave through with the least
// movement. Used only to give an allsolid result a usable normal.
void EscapeNormal(const Sweep& s, const WBox& b, float normal[3])
{
	double bestDepth = HUGE_VAL;
	int bestAxis = 2;
	float bestSign = 1.0f;
	for (int k = 0; k < 3; ++k)
	{
		const int a = kAxisOrder[k];
		const double up = b.hi[a] - (s.p[a] + s.mn[a]);   // push toward +a until clear
		const double down = (s.p[a] + s.mx[a]) - b.lo[a]; // push toward -a until clear
		if (up < bestDepth)
		{
			bestDepth = up;
			bestAxis = a;
			bestSign = 1.0f;
		}
		if (down < bestDepth)
		{
			bestDepth = down;
			bestAxis = a;
			bestSign = -1.0f;
		}
	}
	normal[0] = normal[1] = normal[2] = 0.0f;
	normal[bestAxis] = bestSign;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// World

void World::ToBlock(const float p[3], int b[3]) const
{
	for (int a = 0; a < 3; ++a)
	{
		double c = floor(((double)p[a] - (double)origin[a]) / kBlock);
		// Keep the cast defined for absurd or NaN input.
		if (!(c > -1.0e9))
			c = -1.0e9;
		if (c > 1.0e9)
			c = 1.0e9;
		b[a] = (int)c;
	}
}

// ---------------------------------------------------------------------------------------------
// TestBox / PointSolid

bool TestBox(const World& w, const float origin[3], const float mins[3], const float maxs[3])
{
	if (!WorldValid(w) || !Finite3(origin) || !Finite3(mins) || !Finite3(maxs))
		return false;
	double lo[3], hi[3];
	for (int a = 0; a < 3; ++a)
	{
		lo[a] = (double)origin[a] + (double)mins[a];
		hi[a] = (double)origin[a] + (double)maxs[a];
	}
	CellRange r;
	if (!CellSpan(w, lo, hi, false, r))
		return false;
	for (int z = r.lo[2]; z <= r.hi[2]; ++z)
		for (int y = r.lo[1]; y <= r.hi[1]; ++y)
			for (int x = r.lo[0]; x <= r.hi[0]; ++x)
			{
				const Cell c = w.cells[((size_t)z * (size_t)w.sy + (size_t)y) * (size_t)w.sx + (size_t)x];
				if (!c)
					continue;
				WBox boxes[4];
				const int nb = LoadBoxes(w, x, y, z, c, false, boxes);
				for (int i = 0; i < nb; ++i)
					if (Penetrates(lo, hi, boxes[i]))
						return true;
			}
	return false;
}

bool PointSolid(const World& w, const float p[3])
{
	static const float zero[3] = {0.0f, 0.0f, 0.0f};
	return TestBox(w, p, zero, zero);
}

// ---------------------------------------------------------------------------------------------
// TraceBox

static void TraceImpl(const World& w, const float start[3], const float end[3], const float mins[3],
	const float maxs[3], Trace& out)
{
	out = Trace();
	for (int a = 0; a < 3; ++a)
		out.endpos[a] = end[a];
	if (!WorldValid(w) || !Finite3(start) || !Finite3(end) || !Finite3(mins) || !Finite3(maxs))
		return;

	Sweep s;
	s.w = &w;
	s.best.valid = false;
	s.best.t = 0.0;
	s.best.key = 0;
	s.best.axis = 2;
	s.best.cell = 0;
	s.best.block[0] = s.best.block[1] = s.best.block[2] = -1;
	s.startHit.valid = false;
	s.startHit.key = 0;
	s.startHit.cell = 0;
	s.startHit.block[0] = s.startHit.block[1] = s.startHit.block[2] = -1;
	double len2 = 0.0;
	for (int a = 0; a < 3; ++a)
	{
		s.p[a] = (double)start[a];
		s.v[a] = (double)end[a] - (double)start[a];
		s.mn[a] = (double)mins[a];
		s.mx[a] = (double)maxs[a];
		len2 += s.v[a] * s.v[a];
	}
	s.moving = len2 > 0.0;

	// Parameter range during which the box can touch the world at all: the point p + v*t against the
	// world bounds grown by the box. Everything outside is air, so the walk only needs this interval.
	const int dims[3] = {w.sx, w.sy, w.sz};
	double tw0 = 0.0, tw1 = 1.0;
	for (int a = 0; a < 3; ++a)
	{
		const double wlo = (double)w.origin[a] - s.mx[a];
		const double whi = (double)w.origin[a] + (double)dims[a] * kBlock - s.mn[a];
		if (s.v[a] == 0.0)
		{
			if (s.p[a] < wlo || s.p[a] > whi)
				return;
		}
		else
		{
			double ta = (wlo - s.p[a]) / s.v[a];
			double tb = (whi - s.p[a]) / s.v[a];
			if (ta > tb)
			{
				const double tmp = ta;
				ta = tb;
				tb = tmp;
			}
			if (ta > tw0)
				tw0 = ta;
			if (tb < tw1)
				tw1 = tb;
		}
	}
	if (tw0 > tw1)
		return;

	// Broadphase. Cheap sweeps use one bounding volume; long or fat ones walk the path in segments of about
	// one block, each visiting only its own (small) bounding volume. Every box is always tested against the
	// full move, so segments only decide which boxes get looked at, never the result. Walking in order
	// lets us stop at the first segment that contains a hit: any box not yet visited enters later.
	double lo[3], hi[3];
	SweptBounds(s, tw0, tw1, lo, hi);
	CellRange full;
	if (CellSpan(w, lo, hi, true, full))
	{
		const double cells = (double)(full.hi[0] - full.lo[0] + 1) * (double)(full.hi[1] - full.lo[1] + 1) *
			(double)(full.hi[2] - full.lo[2] + 1);
		if (cells <= kMaxDirectCells || !s.moving)
			VisitRange(s, full, nullptr);
		else
		{
			const double span = (tw1 - tw0) * sqrt(len2);
			int segs = (int)ceil(span / kSegmentLen);
			if (segs < 1)
				segs = 1;
			CellRange prev = full;
			bool havePrev = false;
			for (int k = 0; k < segs; ++k)
			{
				const double ta = (k == 0) ? tw0 : tw0 + (tw1 - tw0) * ((double)k / (double)segs);
				const double tb = (k == segs - 1) ? tw1 : tw0 + (tw1 - tw0) * ((double)(k + 1) / (double)segs);
				SweptBounds(s, ta, tb, lo, hi);
				CellRange r;
				if (CellSpan(w, lo, hi, true, r))
				{
					VisitRange(s, r, havePrev ? &prev : nullptr);
					prev = r;
					havePrev = true;
				}
				else
					havePrev = false;
				if (s.best.valid && s.best.t <= tb)
					break;
			}
		}
	}

	// Stuck in solid: the box is allsolid when it is still overlapping at the end of the move.
	if (s.startHit.valid)
	{
		out.startsolid = true;
		if (TestBox(w, end, mins, maxs))
		{
			out.allsolid = true;
			out.hit = true;
			out.fraction = 0.0f;
			for (int a = 0; a < 3; ++a)
			{
				out.endpos[a] = start[a];
				out.block[a] = s.startHit.block[a];
			}
			out.cell = s.startHit.cell;
			EscapeNormal(s, s.startHit.box, out.normal);
			return;
		}
	}

	if (!s.best.valid)
		return;

	const double frac = s.best.t - kDistEps / sqrt(len2);
	out.fraction = frac > 0.0 ? (float)frac : 0.0f;
	for (int a = 0; a < 3; ++a)
		out.endpos[a] = (float)(s.p[a] + (double)out.fraction * s.v[a]);
	out.normal[s.best.axis] = s.v[s.best.axis] > 0.0 ? -1.0f : 1.0f;
	for (int a = 0; a < 3; ++a)
		out.block[a] = s.best.block[a];
	out.cell = s.best.cell;
	out.hit = true;
}

// The public entry copies its inputs first, so callers may pass out.endpos (or any part of 'out') as start/end.
void TraceBox(const World& w, const float start[3], const float end[3], const float mins[3], const float maxs[3],
	Trace& out)
{
	const float s[3] = {start[0], start[1], start[2]};
	const float e[3] = {end[0], end[1], end[2]};
	const float mn[3] = {mins[0], mins[1], mins[2]};
	const float mx[3] = {maxs[0], maxs[1], maxs[2]};
	TraceImpl(w, s, e, mn, mx, out);
}

// ---------------------------------------------------------------------------------------------
// PickBlock

namespace
{

// Ray p + d*t against box b, closed intervals (a ray grazing a face counts). Returns false when the line
// misses. tIn/tOut may be negative; axisIn is the axis with the latest entry (ties prefer Z, X, Y).
bool RayBox(const double p[3], const double d[3], const WBox& b, double& tIn, double& tOut, int& axisIn)
{
	tIn = -HUGE_VAL;
	tOut = HUGE_VAL;
	axisIn = -1;
	for (int k = 0; k < 3; ++k)
	{
		const int a = kAxisOrder[k];
		if (d[a] == 0.0)
		{
			if (p[a] < b.lo[a] || p[a] > b.hi[a])
				return false;
			continue;
		}
		double t1 = (b.lo[a] - p[a]) / d[a];
		double t2 = (b.hi[a] - p[a]) / d[a];
		if (t1 > t2)
		{
			const double tmp = t1;
			t1 = t2;
			t2 = tmp;
		}
		if (t1 > tIn)
		{
			tIn = t1;
			axisIn = a;
		}
		if (t2 < tOut)
			tOut = t2;
		if (tIn > tOut)
			return false;
	}
	return axisIn >= 0;
}

} // namespace

bool PickBlock(const World& w, const float start[3], const float dir[3], float maxDist, int block[3], int* face,
	float* dist)
{
	if (!WorldValid(w) || !Finite3(start) || !Finite3(dir) || !(maxDist >= 0.0f))
		return false;
	double p[3], d[3];
	double len2 = 0.0;
	for (int a = 0; a < 3; ++a)
	{
		p[a] = (double)start[a];
		d[a] = (double)dir[a];
		len2 += d[a] * d[a];
	}
	if (!(len2 > 1.0e-24))
		return false;
	const double len = sqrt(len2);
	for (int a = 0; a < 3; ++a)
		d[a] /= len;

	// Clip the ray to the world so rays that start outside can still pick the boundary blocks and the
	// walk never leaves the grid.
	const int dims[3] = {w.sx, w.sy, w.sz};
	double t0 = 0.0, t1 = (double)maxDist;
	for (int a = 0; a < 3; ++a)
	{
		const double wlo = (double)w.origin[a];
		const double whi = wlo + (double)dims[a] * kBlock;
		if (d[a] == 0.0)
		{
			if (p[a] < wlo || p[a] > whi)
				return false;
		}
		else
		{
			double ta = (wlo - p[a]) / d[a];
			double tb = (whi - p[a]) / d[a];
			if (ta > tb)
			{
				const double tmp = ta;
				ta = tb;
				tb = tmp;
			}
			if (ta > t0)
				t0 = ta;
			if (tb < t1)
				t1 = tb;
		}
	}
	if (!(t0 <= t1))
		return false;

	// Amanatidis-Woo setup. tMax[a] = ray parameter of the next cell boundary on axis a.
	int c[3], step[3];
	double tMax[3], tDelta[3];
	for (int a = 0; a < 3; ++a)
	{
		const double q = p[a] + d[a] * t0;
		double cf = floor((q - (double)w.origin[a]) / kBlock);
		if (cf < 0.0)
			cf = 0.0;
		if (cf > (double)(dims[a] - 1))
			cf = (double)(dims[a] - 1);
		c[a] = (int)cf;
		const double cellLo = (double)w.origin[a] + (double)c[a] * kBlock;
		if (d[a] > 0.0)
		{
			step[a] = 1;
			tMax[a] = (cellLo + kBlock - p[a]) / d[a];
			tDelta[a] = kBlock / d[a];
		}
		else if (d[a] < 0.0)
		{
			step[a] = -1;
			tMax[a] = (cellLo - p[a]) / d[a];
			tDelta[a] = -kBlock / d[a];
		}
		else
		{
			step[a] = 0;
			tMax[a] = HUGE_VAL;
			tDelta[a] = HUGE_VAL;
		}
	}

	for (;;)
	{
		const size_t idx = ((size_t)c[2] * (size_t)w.sy + (size_t)c[1]) * (size_t)w.sx + (size_t)c[0];
		const Cell cell = w.cells[idx];
		if (cell)
		{
			WBox boxes[4];
			const int nb = LoadBoxes(w, c[0], c[1], c[2], cell, true, boxes);
			bool found = false;
			double bestT = 0.0;
			int bestFace = 0;
			for (int i = 0; i < nb; ++i)
			{
				double tIn, tOut;
				int axisIn;
				if (!RayBox(p, d, boxes[i], tIn, tOut, axisIn) || tOut <= 0.0)
					continue;
				double t;
				int f;
				if (tIn > 0.0)
				{
					t = tIn;
					f = 2 * axisIn + (d[axisIn] > 0.0 ? 1 : 0);
				}
				else
				{
					// The origin is inside (or on the surface of) this box.
					t = 0.0;
					const WBox& bx = boxes[i];
					const bool strictlyInside = p[0] > bx.lo[0] && p[0] < bx.hi[0] && p[1] > bx.lo[1] &&
						p[1] < bx.hi[1] && p[2] > bx.lo[2] && p[2] < bx.hi[2];
					if (strictlyInside)
					{
						// Face opposite to the ray direction's dominant axis (the face the ray "came in
						// through"); ties prefer Z, X, Y.
						int dom = 2;
						double domAbs = -1.0;
						for (int k = 0; k < 3; ++k)
						{
							const int a = kAxisOrder[k];
							const double m = fabs(d[a]);
							if (m > domAbs)
							{
								domAbs = m;
								dom = a;
							}
						}
						f = 2 * dom + (d[dom] > 0.0 ? 1 : 0);
					}
					else
						f = 2 * axisIn + (d[axisIn] > 0.0 ? 1 : 0);
				}
				if (!found || t < bestT)
				{
					found = true;
					bestT = t;
					bestFace = f;
				}
			}
			if (found)
			{
				// Boxes live inside their cell, so no later cell can hold anything nearer.
				if (bestT > (double)maxDist)
					return false;
				if (block)
				{
					block[0] = c[0];
					block[1] = c[1];
					block[2] = c[2];
				}
				if (face)
					*face = bestFace;
				if (dist)
					*dist = (float)bestT;
				return true;
			}
		}

		int a = 0;
		if (tMax[1] < tMax[a])
			a = 1;
		if (tMax[2] < tMax[a])
			a = 2;
		if (!(tMax[a] <= t1))
			return false;
		c[a] += step[a];
		if (c[a] < 0 || c[a] >= dims[a])
			return false;
		tMax[a] += tDelta[a];
	}
}

} // namespace mcw
