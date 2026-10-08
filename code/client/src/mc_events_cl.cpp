// Counter-Strike's weapon events (closed-source cs_client.dll) trace bullets with the engine's event API,
// which only knows the engine's world: on a classic map that is an empty shell, on the block map the
// blocks are invisible to it. The client gets a copy of the event API whose traces also hit the mod
// world, and whose texture query names the classic surface, so impacts, sparks and CS's impact sounds
// (by material, from sound/materials.txt) happen where bullets really land.
#include "hlsdk_client.h"
#include "mc_client.h"
#include "mc_move.h"

namespace mc
{
static event_api_t g_ev;
static event_api_t* g_origEv = nullptr;
static int g_hull = 2;

static void W_SetTraceHull(int hull)
{
	g_hull = hull;
	g_origEv->EV_SetTraceHull(hull);
}

static void W_PlayerTrace(float* start, float* end, int traceFlags, int ignore_pe, struct pmtrace_s* tr)
{
	g_origEv->EV_PlayerTrace(start, end, traceFlags, ignore_pe, tr);
	// event hulls: 0 standing, 1 ducked, 2 point, 3 large
	static const float hm[4][3] = {{-16, -16, -36}, {-16, -16, -18}, {0, 0, 0}, {-32, -32, -32}};
	static const float hM[4][3] = {{16, 16, 36}, {16, 16, 18}, {0, 0, 0}, {32, 32, 32}};
	int h = g_hull >= 0 && g_hull < 4 ? g_hull : 2;
	mcm::MergeIntoPmTrace(*tr, start, end, hm[h], hM[h]);
}

static const char* W_TraceTexture(int ground, float* vstart, float* vend)
{
	if (const char* t = mcm::WorldTraceTexture(vstart, vend))
		return t;
	return g_origEv->EV_TraceTexture(ground, vstart, vend);
}

void InstallEventApi(cl_enginefunc_t* e)
{
	if (!e || !e->pEventAPI || e->pEventAPI == &g_ev)
		return;
	g_origEv = e->pEventAPI;
	g_ev = *g_origEv;
	g_ev.EV_SetTraceHull = W_SetTraceHull;
	g_ev.EV_PlayerTrace = W_PlayerTrace;
	g_ev.EV_TraceTexture = W_TraceTexture;
	e->pEventAPI = &g_ev;
}
} // namespace mc
