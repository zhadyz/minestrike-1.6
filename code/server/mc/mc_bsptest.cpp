// mc_bsptest [count]: on a normal map (the engine loaded the real BSP), fire random hull traces through
// both the engine and the mod's own BSP collision (mcb/mcc) and report how often they disagree. Also
// times the carve-aware fallback path (convex pieces) against the fast path.
#include "precompiled.h"

#include "mc_blocks.h"
#include "mc_classic.h"
#include "mc_server.h"

#include <chrono>

namespace mc
{
static void BspTest()
{
	int count = CMD_ARGC() > 1 ? atoi(CMD_ARGV(1)) : 2000;
	char gd[256], path[512];
	GET_GAME_DIR(gd);
	Q_snprintf(path, sizeof(path), "%s/maps/%s.bsp", gd, STRING(gpGlobals->mapname));
	static mcc::Classic cl;
	static std::vector<mcw::Cell> cells;
	static mcw::World grid;
	if (!cl.map.loaded || cl.map.path != path)
	{
		if (!cl.map.Load(path))
		{
			McLog("bsptest: could not load %s", path);
			return;
		}
		const mcb::Model& w = cl.map.models[0];
		grid = mcw::World();
		for (int k = 0; k < 3; k++)
			grid.origin[k] = floorf(w.mins[k] / 40.0f) * 40.0f - 40.0f;
		grid.sx = (int)((w.maxs[0] - grid.origin[0]) / 40.0f) + 2;
		grid.sy = (int)((w.maxs[1] - grid.origin[1]) / 40.0f) + 2;
		grid.sz = (int)((w.maxs[2] - grid.origin[2]) / 40.0f) + 2;
		cells.assign((size_t)grid.sx * grid.sy * grid.sz, 0);
		grid.cells = cells.data();
		grid.shapeOfType = mcw::g_shapeOfType;
		cl.grid = &grid;
		cl.Load(path);
		McLog("bsptest: loaded %s: %d planes, %d nodes, %d clipnodes, %d faces, %d solid models", path, (int)cl.map.planes.size(),
			(int)cl.map.hull0.size(), (int)cl.map.clipnodes.size(), (int)cl.map.faces.size(), (int)cl.map.solidModels.size());
	}
	const mcb::Model& w = cl.map.models[0];
	static const int hullOf[4] = {point_hull, human_hull, large_hull, head_hull};
	int tested = 0, badFrac = 0, badSolid = 0, badNormal = 0, contentsBad = 0;
	double tEngine = 0, tMine = 0;
	float worst = 0;
	for (int i = 0; i < count; i++)
	{
		int hull = i % 4;
		float mins[3], maxs[3];
		mcb::Map::HullExtents(hull, mins, maxs);
		Vector s(RANDOM_FLOAT(w.mins[0], w.maxs[0]), RANDOM_FLOAT(w.mins[1], w.maxs[1]), RANDOM_FLOAT(w.mins[2], w.maxs[2]));
		// prefer starts in open space (most real traces start there)
		if (i % 5 != 0 && cl.map.HullPointContents(hull, 0, s) == mcb::CONT_SOLID)
		{
			i--;
			continue;
		}
		Vector dir(RANDOM_FLOAT(-1, 1), RANDOM_FLOAT(-1, 1), RANDOM_FLOAT(-1, 1) * 0.5f);
		dir = dir.Normalize();
		Vector e = s + dir * RANDOM_FLOAT(1.0f, 900.0f);
		TraceResult tr;
		auto t0 = std::chrono::high_resolution_clock::now();
		g_engfuncs.pfnTraceHull(s, e, ignore_monsters, hullOf[hull], nullptr, &tr);
		auto t1 = std::chrono::high_resolution_clock::now();
		mcc::Result r;
		cl.Trace(s, e, mins, maxs, r);
		auto t2 = std::chrono::high_resolution_clock::now();
		tEngine += std::chrono::duration<double, std::micro>(t1 - t0).count();
		tMine += std::chrono::duration<double, std::micro>(t2 - t1).count();
		tested++;
		float d = (Vector(r.endpos) - tr.vecEndPos).Length();
		if (d > 0.01f)
		{
			badFrac++;
			if (d > worst)
				worst = d;
			if (badFrac <= 5)
				McLog("bsptest: hull %d frac engine %.5f mine %.5f (endpos off %.2f) ss %d/%d", hull, tr.flFraction, r.fraction, d,
					tr.fStartSolid, (int)r.startsolid);
		}
		if ((tr.fStartSolid != 0) != r.startsolid || (tr.fAllSolid != 0) != r.allsolid)
			badSolid++;
		if (tr.flFraction < 1.0f && r.fraction < 1.0f && (Vector(r.normal) - tr.vecPlaneNormal).Length() > 0.001f)
			badNormal++;
		if (hull == 0 && (POINT_CONTENTS(s) == CONTENTS_SOLID) != (cl.PointContents(s) == mcb::CONT_SOLID))
			contentsBad++;
	}
	McLog("bsptest: %d traces: endpos mismatches %d (worst %.2f units), solid flags %d, normals %d, point contents %d; "
		  "engine %.2f us/trace, mine %.2f us/trace",
		tested, badFrac, worst, badSolid, badNormal, contentsBad, tEngine / tested, tMine / tested);

	// carve-aware path vs fast path on intact geometry (pieces + box sweeps)
	int slowBad = 0, perHull[4] = {0, 0, 0, 0}, perHullN[4] = {0, 0, 0, 0};
	float slowWorst = 0;
	double tSlow = 0;
	int slowN = count / 4;
	for (int i = 0; i < slowN; i++)
	{
		int hull = i % 4;
		float mins[3], maxs[3];
		mcb::Map::HullExtents(hull, mins, maxs);
		Vector s(RANDOM_FLOAT(w.mins[0], w.maxs[0]), RANDOM_FLOAT(w.mins[1], w.maxs[1]), RANDOM_FLOAT(w.mins[2], w.maxs[2]));
		if (cl.map.HullPointContents(hull, 0, s) == mcb::CONT_SOLID)
		{
			i--;
			continue;
		}
		Vector dir(RANDOM_FLOAT(-1, 1), RANDOM_FLOAT(-1, 1), RANDOM_FLOAT(-1, 1) * 0.5f);
		dir = dir.Normalize();
		Vector e = s + dir * RANDOM_FLOAT(1.0f, 300.0f);
		mcc::Result fast, slow;
		cl.Trace(s, e, mins, maxs, fast);
		auto t0 = std::chrono::high_resolution_clock::now();
		cl.TraceSlow(s, e, mins, maxs, slow);
		auto t1 = std::chrono::high_resolution_clock::now();
		tSlow += std::chrono::duration<double, std::micro>(t1 - t0).count();
		float d = (Vector(fast.endpos) - Vector(slow.endpos)).Length();
		perHullN[hull]++;
		if (d > 0.5f)
		{
			perHull[hull]++;
			if (hull == 0 && perHull[0] <= 3)
				McLog("bsptest: point mismatch start %.1f %.1f %.1f end %.1f %.1f %.1f fast %.4f slow %.4f ss %d/%d", s.x, s.y, s.z, e.x, e.y, e.z,
					fast.fraction, slow.fraction, (int)fast.startsolid, (int)slow.startsolid);
			slowBad++;
			if (d > slowWorst)
				slowWorst = d;
			if (slowBad <= 5)
				McLog("bsptest: pieces vs hull, hull %d: frac %.4f vs %.4f, off %.2f, n %.2f %.2f %.2f vs %.2f %.2f %.2f", hull, fast.fraction,
					slow.fraction, d, fast.normal[0], fast.normal[1], fast.normal[2], slow.normal[0], slow.normal[1], slow.normal[2]);
		}
	}
	McLog("bsptest: pieces mismatches per hull: point %d/%d, human %d/%d, large %d/%d, duck %d/%d", perHull[0], perHullN[0], perHull[1],
		perHullN[1], perHull[2], perHullN[2], perHull[3], perHullN[3]);
	McLog("bsptest: pieces path: %d traces, %d differ by > 0.5 units (worst %.2f), %.1f us/trace (cold+warm cache)", slowN, slowBad,
		slowWorst, tSlow / (slowN ? slowN : 1));
}

void RegisterBspTest() { ADD_SERVER_COMMAND("mc_bsptest", BspTest); }
} // namespace mc
