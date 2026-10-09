// Bots with blocks and TNT.
//
// Blocks and TNT are dear ($200 a stone block, $100 one of planks, $2000 the TNT), and what is set down is
// gone with the round. A bot buys blocks when it is rich enough to have its guns as well, more readily when
// blocks have been winning its side rounds, and keeps what it has not used. Stone stops every bullet;
// planks stop pistols only, and a pickaxe is the wrong tool for them: it buys planks against a side that is
// short of money this round or that dug with pickaxes the round before, else stone. A rare rich one buys
// TNT; a few buy a tool, more of them when their side met walls the round before, an axe when those were of
// wood (BotTacticsSpawn). What they do with them is
// never on a cue: each use is a bot noticing what its situation
// offers and deciding whether to take it (the "Judgement" section): how much the moment asks for it, against
// the bot's nerve this round and a throw of the dice, and its side's mood for that kind of thing this round.
//  - Holding a spot in the open for a while: cover where it stands (two pillars and a gap to shoot through,
//    or a low block with a pillar on one flank), or two blocks stacked under its feet where something close
//    by can be looked over. It faces where it last saw the enemy, else the way in the other side is expected
//    to take, else the lane in front of it.
//  - The enemy near and out of sight with a gap between: hurt or outnumbered and falling back, it shuts the
//    way behind it; defending, it shuts it when it hears them coming. Only gaps that really close the way.
//  - A way into a site its side defends: shut, it sends the enemy the long way round, or makes them dig in
//    the open, or costs them TNT. Against that the builder stands there with stone in its hands: it weighs
//    how soon the enemy can be at the gap against how long the wall takes, and whether a mate covers it.
//    Shot at while it builds, it drops the job and fights. Afterwards it stays behind its wall and listens,
//    or leaves it standing alone and goes to the other bomb site: the wall as an ambush, or as a sixth
//    player. The other side cannot tell which (GiveWallAt).
//  - A wall in its way: one bot digs (the one with a pickaxe, if any) and the others cover it; whoever
//    defends the wall and hears the digging waits for the block to break.
//  - A Counter-Terrorist at the bomb with enemies about and time in hand: blocks up, then the defuse.
//  - Shot at in the open from far off: two blocks between itself and the shooter.
//  - TNT: to the cover an enemy holds; through a wall of blocks in its way; and through a thin wall of a
//    bomb site when the walk round to its doors is long (FindBreaches), to come in where nobody watches.
//  - Held up by blocks somebody placed, it goes around if that is not much longer, else digs through. Bots
//    only ever mine placed blocks, never the map.
//  - The ground in its way (craters, mostly), for a bot that has blocks: in a hole it cannot jump out of it
//    sets a block under its own feet, as often as it takes; a trench it bridges or climbs through,
//    whichever takes fewer blocks.
// Three things are learned from the rounds played, and kept per map in <game>/mc_brain_<map>.txt:
//  - what the human players do at a wall across their way: dig through it, have it blown open, or turn
//    away. Against players who come through walls a builder stays behind its wall more often; against
//    players who turn away it leaves more often.
//  - which way in the human players take (as a Terrorist going for a site, and as a Counter-Terrorist
//    coming back to a planted bomb): recency-weighted counts, also conditioned on the way they took the
//    round before. Cover faces where they are expected.
//  - which kinds of chance are worth taking: per side and kind of moment, a Thompson-sampling bandit between
//    taking such chances this round and leaving them (the side's mood), paid with the round's result and
//    forgetting slowly, so it follows a player who adapts.
// The console command mc_brain prints what has been learned; mc_bot_learn 0 keeps to even chances.
#include "precompiled.h"

#include "mc_server.h"
#include "mc_blocks.h"
#include "mc_classic.h"
#include "mc_move.h"

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <vector>

namespace mc
{
static cvar_t cv_tactics = {"mc_bot_tactics", "1", FCVAR_SERVER, 1.0f, nullptr};
static cvar_t cv_blocks = {"mc_bot_blocks", "8", FCVAR_SERVER, 8.0f, nullptr};       // the most stone blocks a builder buys
static cvar_t cv_builders = {"mc_bot_builders", "35", FCVAR_SERVER, 35.0f, nullptr}; // % of bots that think of buying blocks in a round
static cvar_t cv_pocket = {"mc_bot_pocket", "0", FCVAR_SERVER, 0.0f, nullptr};       // blocks every bot is given free, to get over bad ground
static cvar_t cv_tntBots = {"mc_bot_tnt", "8", FCVAR_SERVER, 8.0f, nullptr};         // % that buy TNT and a flint and steel when rich
static cvar_t cv_learn = {"mc_bot_learn", "1", FCVAR_SERVER, 1.0f, nullptr};
// % of bots with the money for it (a rifle and armor besides) that buy the parts of an iron golem, when
// their side has none
static cvar_t cv_golemBots = {"mc_bot_golem", "40", FCVAR_SERVER, 40.0f, nullptr};
// % of bots at Counter-Strike's money ceiling, a rifle in hand already, that spend all of it on a wither when
// their side may raise one
static cvar_t cv_witherBots = {"mc_bot_wither", "50", FCVAR_SERVER, 50.0f, nullptr};
// a bot's mine: 0 the TNT stands on the floor beside the plate, just inside the way in (its blast reaches
// about four blocks); 1 it is buried under the plate (nothing shows but the plate; the hole smothers the
// blast: a quarter of a man's health at a block and a half)
static cvar_t cv_mineBuried = {"mc_bot_mine_buried", "0", FCVAR_SERVER, 0.0f, nullptr};

// a bot's own hotbar slots (bots carry no weapon tokens; 5..8 are the bow, arrows, sword and food)
static const int SLOT_BLOCKS = 0, SLOT_TNT = 1, SLOT_FLINT = 2, SLOT_PICK = 3, SLOT_HAND = 4;
static const float CELL = 40.0f;
static const float MAX_SIDE = 320.0f; // how far a gap is measured to either side
static const float MAX_GAP = 250.0f;  // the widest gap that still counts as a doorway
enum
{
	MAX_WALL = 12,
	MAX_PAIRS = 24,
	MAX_ENT = 16,
	MAX_JOB_CELLS = 12
};

size_t ChangedCells(); // mc_world_srv.cpp: how many cells differ from the map as loaded
mcc::Classic* ClassicWorld(); // mc_world_srv.cpp
static int g_stone = 0, g_planks = 0, g_tnt = 0, g_flint = 0, g_pick = 0, g_axe = 0, g_plate = 0, g_iron = 0, g_pumpkin = 0, g_soul = 0, g_skull = 0;
static bool g_golemBuyer[4]; // a bot of this side carries the parts of a golem this round
static void Items()
{
	g_stone = mci::FindItem("stone");
	g_planks = mci::FindItem("oak_planks");
	g_tnt = mci::FindItem("tnt");
	g_flint = mci::FindItem("flint_and_steel");
	g_pick = mci::FindItem("wooden_pickaxe");
	g_axe = mci::FindItem("wooden_axe");
	g_plate = mci::FindItem("stone_pressure_plate");
	g_iron = mci::FindItem("iron_block");
	g_pumpkin = mci::FindItem("carved_pumpkin");
	g_soul = mci::FindItem("soul_sand");
	g_skull = mci::FindItem("wither_skeleton_skull");
}

// the parts of an iron golem: four iron blocks where its blocks go, the carved pumpkin where its TNT goes
static bool HasGolemParts(CBasePlayer* bot)
{
	const McPlayer& mp = P(bot);
	return g_iron > 0 && !mp.hotbar[SLOT_BLOCKS].Empty() && mp.hotbar[SLOT_BLOCKS].id == g_iron && mp.hotbar[SLOT_BLOCKS].count >= 4 &&
		   !mp.hotbar[SLOT_TNT].Empty() && mp.hotbar[SLOT_TNT].id == g_pumpkin;
}
// ... and of a wither: four soul sand, three skulls
static bool HasWitherParts(CBasePlayer* bot)
{
	const McPlayer& mp = P(bot);
	return g_soul > 0 && !mp.hotbar[SLOT_BLOCKS].Empty() && mp.hotbar[SLOT_BLOCKS].id == g_soul && mp.hotbar[SLOT_BLOCKS].count >= 4 &&
		   !mp.hotbar[SLOT_TNT].Empty() && mp.hotbar[SLOT_TNT].id == g_skull && mp.hotbar[SLOT_TNT].count >= 3;
}

// What it builds with: stone, which stops every bullet, or planks, at half the price: they stop pistols and
// submachine guns but not rifles, they burn, and a pickaxe is the wrong tool for them.
static int Blocks(CBasePlayer* bot)
{
	const mci::Stack& s = P(bot).hotbar[SLOT_BLOCKS];
	return (!s.Empty() && (s.id == g_stone || s.id == g_planks)) ? s.count : 0;
}
static bool Wooden(CBasePlayer* bot)
{
	const mci::Stack& s = P(bot).hotbar[SLOT_BLOCKS];
	return !s.Empty() && s.id == g_planks;
}
static bool HasTnt(CBasePlayer* bot)
{
	const mci::Stack& s = P(bot).hotbar[SLOT_TNT];
	return !s.Empty() && s.id == g_tnt;
}
static bool HasFlint(CBasePlayer* bot)
{
	const mci::Stack& s = P(bot).hotbar[SLOT_FLINT];
	return !s.Empty() && s.id == g_flint;
}
// the lid of a mine (in the slot of its bare hand: a plate in the hand digs no worse)
static bool HasPlate(CBasePlayer* bot)
{
	const mci::Stack& s = P(bot).hotbar[SLOT_HAND];
	return !s.Empty() && s.id == g_plate;
}
// a tool to take a wall down with: a pickaxe (for stone) or an axe (for wood)
static bool HasPick(CBasePlayer* bot)
{
	const mci::Stack& s = P(bot).hotbar[SLOT_PICK];
	return !s.Empty() && (mci::Item(s.id).type == mci::IT_PICKAXE || mci::Item(s.id).type == mci::IT_AXE);
}

// (tests on the dedicated server, where nobody plays: the bot that stands in for the player counts as one)
static int g_standIn = 0;
void TacticsStandIn(int index) { g_standIn = index; }
static bool IsHuman(CBasePlayer* p) { return p && (!p->IsBot() || p->entindex() == g_standIn); }

static void Say(CBasePlayer* bot, const char* text)
{
	// (one line every few seconds at most: a bot that talks all the time is not listened to)
	static float last[33];
	int who = bot->entindex();
	if (who < 1 || who > 32 || (gpGlobals->time >= last[who] && gpGlobals->time - last[who] < 4.0f))
		return;
	last[who] = gpGlobals->time;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* h = UTIL_PlayerByIndex(i);
		if (h && !h->IsBot() && h->m_iTeam == bot->m_iTeam)
			Toast(h, 0, "<%s> %s", STRING(bot->pev->netname), text);
	}
}

// ---------------------------------------------------------------------------------------------
// A bot's view and feet for one frame. The engine takes a bot's view and movement from the command its AI
// made, after PreThink: what the mod wants of it goes into that command (BotControlMove, called from the
// movement hook).

struct Control
{
	bool look = false, hold = false, steer = false, jump = false, stand = false;
	Vector view, steerTo;
	Vector wish; // where the bot's own command wanted to go (world, units a second)
};
static Control g_ctl[MAX_CLIENTS + 1];

void BotLook(CBasePlayer* bot, const Vector& viewAngles)
{
	Control& c = g_ctl[bot->entindex()];
	c.look = true;
	c.view = viewAngles;
	bot->pev->v_angle = viewAngles; // for what reads it before the move
}

static void LookAt(CBasePlayer* bot, const Vector& point)
{
	Vector ang = UTIL_VecToAngles(point - (bot->pev->origin + bot->pev->view_ofs));
	ang.x = -ang.x;
	if (ang.x < -180.0f)
		ang.x += 360.0f;
	ang.z = 0.0f;
	BotLook(bot, ang);
}
static void Hold(CBasePlayer* bot) { g_ctl[bot->entindex()].hold = true; }
static void Jump(CBasePlayer* bot) { g_ctl[bot->entindex()].jump = true; }
static void Stand(CBasePlayer* bot) { g_ctl[bot->entindex()].stand = true; }
static void Steer(CBasePlayer* bot, const Vector& to)
{
	Control& c = g_ctl[bot->entindex()];
	c.steer = true;
	c.steerTo = to;
}

Vector BotWish(CBasePlayer* bot)
{
	int i = bot ? bot->entindex() : 0;
	return (i >= 1 && i <= MAX_CLIENTS) ? g_ctl[i].wish : Vector(0, 0, 0);
}

void BotControlMove(struct playermove_s* pm)
{
	int i = pm->player_index + 1;
	if (i < 1 || i > MAX_CLIENTS)
		return;
	Control& c = g_ctl[i];
	CBasePlayer* pl = UTIL_PlayerByIndex(i);
	if (!pl || !pl->IsBot())
	{
		c.look = c.hold = c.steer = c.jump = c.stand = false;
		return;
	}
	usercmd_t& cmd = pm->cmd;
	float yaw = cmd.viewangles[1] * ((float)M_PI / 180.0f);
	Vector f(cosf(yaw), sinf(yaw), 0), r(sinf(yaw), -cosf(yaw), 0);
	c.wish = f * cmd.forwardmove + r * cmd.sidemove;
	if (c.look)
	{
		cmd.viewangles[0] = c.view.x;
		cmd.viewangles[1] = c.view.y;
		cmd.viewangles[2] = 0.0f;
	}
	if (c.hold)
	{
		cmd.forwardmove = cmd.sidemove = cmd.upmove = 0.0f;
		cmd.buttons &= ~IN_JUMP;
		// stand still at once: left to friction a bot slides on a dozen units, off the block it is to stand on
		if (pm->onground != -1)
			pm->velocity[0] = pm->velocity[1] = 0.0f;
	}
	else if (c.steer)
	{
		yaw = cmd.viewangles[1] * ((float)M_PI / 180.0f);
		f = Vector(cosf(yaw), sinf(yaw), 0);
		r = Vector(sinf(yaw), -cosf(yaw), 0);
		Vector d(c.steerTo.x - pm->origin[0], c.steerTo.y - pm->origin[1], 0);
		float len = d.Length();
		// (never under 90: ground friction holds a player that wants less than 60 units a second where it is)
		float speed = len < 1.0f ? 0.0f : min(250.0f, max(90.0f, len * 6.0f));
		if (len >= 1.0f)
			d = d / len;
		cmd.forwardmove = DotProduct(d, f) * speed;
		cmd.sidemove = DotProduct(d, r) * speed;
		cmd.upmove = 0.0f;
		cmd.buttons &= ~IN_JUMP;
	}
	if (c.stand)
		cmd.buttons &= ~IN_DUCK;
	if (c.jump)
		cmd.buttons |= IN_JUMP;
	c.look = c.hold = c.steer = c.jump = c.stand = false;
}

// ---------------------------------------------------------------------------------------------
// The world, as these rules see it

static double Millis()
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void ToCell(const Vector& p, int out[3])
{
	float q[3] = {p.x, p.y, p.z};
	g_world.ToBlock(q, out);
}
static Vector CellMid(const int c[3]) { return CellCenter(c[0], c[1], c[2]); }
static bool Placed(const int c[3]) { return IsPlacedBlock(c[0], c[1], c[2]); }
static bool Takes(const int c[3]) { return CellTakesBlock(c[0], c[1], c[2]); }
// A placed block worth breaking to get past: not a table or a slab one walks around or over.
static bool Wallish(const int c[3])
{
	if (!Placed(c))
		return false;
	mcw::ShapeKind s = g_world.ShapeAt(c[0], c[1], c[2]);
	return s != mcw::SHAPE_TABLE && s != mcw::SHAPE_SLAB;
}

// How much of a line through the whole mod world (the map and the placed blocks) is free: 0..1
static float FreeLine(const Vector& a, const Vector& b, bool* startSolid = nullptr)
{
	MC_WHERE("tactics: a line through the world");
	float s[3] = {a.x, a.y, a.z}, e[3] = {b.x, b.y, b.z}, zero[3] = {0, 0, 0};
	mcw::Trace t;
	mcm::WorldTrace(s, e, zero, zero, t);
	if (startSolid)
		*startSolid = t.startsolid;
	return t.startsolid ? 0.0f : t.fraction;
}

static bool GroundBelow(const Vector& p, float up, float down, float* z)
{
	bool ss = false;
	float f = FreeLine(p + Vector(0, 0, up), p - Vector(0, 0, down), &ss);
	if (ss || f >= 1.0f)
		return false;
	*z = p.z + up - f * (up + down);
	return true;
}

// What is in the way between an eye and the middle of a cell: 0 nothing, 1 a placed block (out), 2 the map
static int Blocker(const Vector& eye, const Vector& mid, int out[3])
{
	float f = FreeLine(eye, mid);
	if (f >= 1.0f)
		return 0;
	Vector dir = (mid - eye).Normalize();
	ToCell(eye + (mid - eye) * f + dir * 3.0f, out);
	return Placed(out) ? 1 : 2;
}

// ---------------------------------------------------------------------------------------------
// The ways into the bomb sites

struct Entrance
{
	int site = 0;
	Vector pos;             // on the ground, in the gap
	int axis = 0;           // a wall across it runs along x (0) or y (1)
	float lo = 0, hi = 0;   // the gap along that axis (world coordinates)
	float toSite = 1.0f;    // +1 / -1: the direction along the other axis that leads to the site
	float width = 0;
	int rank[2] = {-1, -1}; // its place among the site's ways in for attackers from the T (0) / CT (1) spawn
	Vector stand;           // where a builder stands: on the site's side
	int cells[MAX_WALL][3];
	int numCells = 0, cols = 0;
	unsigned pairs[MAX_PAIRS][2]; // the navigation links that pass through it
	int numPairs = 0;
	bool walled = false;
	int team = 0;             // whose wall stands in it (the side that built it)
	float from[2] = {0, 0};   // how far it is from the T (0) / CT (1) spawn on foot
	float around[2] = {0, 0}; // how much further that side walks to the site when it is shut (3000: no other way)
};
static std::vector<Entrance> g_ent;
static std::vector<Entrance> g_dyn; // gaps walled this round that are not ways into a site (a bot's own idea)
static bool g_entReady = false;
static int g_numWalled = 0;
static bool g_testNoWayAround = false; // tests: a bot at a wall goes through it, whatever else there is

// Does the step from p to q pass through the gap? dir > 0: towards the site
static bool Crosses(const Entrance& e, const Vector& p, const Vector& q, float* dir = nullptr)
{
	int t = 1 - e.axis;
	float w = e.pos[t];
	float a = p[t] - w, b = q[t] - w;
	if ((a > 0.0f) == (b > 0.0f))
		return false;
	float k = a / (a - b);
	float u = p[e.axis] + (q[e.axis] - p[e.axis]) * k;
	if (u < e.lo - 16.0f || u > e.hi + 16.0f)
		return false;
	float z = p.z + (q.z - p.z) * k;
	if (fabsf(z - e.pos.z) > 90.0f)
		return false;
	if (dir)
		*dir = (b - a) * e.toSite;
	return true;
}

// A step of a route over the mesh, from one area into the next, is walked centre, portal, centre.
static bool LinkCrosses(const Entrance& e, CNavArea* from, CNavArea* to, int dir)
{
	Vector c0 = *from->GetCenter(), c1 = *to->GetCenter();
	if (dir < 0 || dir >= NUM_DIRECTIONS)
		return Crosses(e, c0, c1);
	Vector portal;
	float half;
	from->ComputePortal(to, (NavDirType)dir, &portal, &half);
	portal.z = from->GetZ(&portal);
	return Crosses(e, c0, portal) || Crosses(e, portal, c1);
}
static int LinkDir(CNavArea* from, CNavArea* to)
{
	for (int d = 0; d < NUM_DIRECTIONS; d++)
		if (from->IsConnected(to, (NavDirType)d))
			return d;
	return -1;
}

// Shortest routes that may not pass through the gaps given
struct RouteCost
{
	const std::vector<Entrance>* shut;
	const Entrance* alsoShut;
	float operator()(CNavArea* area, CNavArea* from, const CNavLadder* ladder)
	{
		if (!from)
			return 0.0f;
		if (ladder)
			return ladder->m_length + from->GetCostSoFar();
		int dir = LinkDir(from, area);
		for (const Entrance& e : *shut)
			if (LinkCrosses(e, from, area, dir))
				return -1.0f;
		if (alsoShut && LinkCrosses(*alsoShut, from, area, dir))
			return -1.0f;
		return (*area->GetCenter() - *from->GetCenter()).Length() + from->GetCostSoFar();
	}
};

// A route from an area to a site; the points come back from the site outwards.
static bool Route(CNavArea* start, CNavArea* goal, const Vector& goalPos, const std::vector<Entrance>& shut, const Entrance* alsoShut,
	std::vector<Vector>* pts, float* length)
{
	if (!start || !goal)
		return false;
	if (start == goal)
	{
		if (length)
			*length = 0.0f;
		if (pts)
		{
			pts->clear();
			pts->push_back(*goal->GetCenter());
		}
		return true;
	}
	RouteCost cost{&shut, alsoShut};
	{
		MC_WHERE("tactics: a route over the mesh");
		if (!NavAreaBuildPath(start, goal, &goalPos, cost))
			return false;
	}
	if (length)
		*length = goal->GetCostSoFar();
	if (pts)
	{
		pts->clear();
		pts->push_back(*goal->GetCenter());
		int guard = 0;
		for (CNavArea* a = goal; a->GetParent() && guard < 2000; a = a->GetParent(), guard++)
		{
			CNavArea* p = a->GetParent();
			int how = a->GetParentHow();
			if (how >= GO_NORTH && how <= GO_WEST)
			{
				Vector portal;
				float half;
				p->ComputePortal(a, (NavDirType)how, &portal, &half);
				portal.z = p->GetZ(&portal);
				pts->push_back(portal);
			}
			pts->push_back(*p->GetCenter());
		}
	}
	return true;
}

// The free width through a point on the ground along an axis: how far the world leaves room either way
static void Gap(const Vector& p, int axis, float* lo, float* hi)
{
	static const float kHeights[3] = {14.0f, 38.0f, 62.0f};
	float l = MAX_SIDE, h = MAX_SIDE;
	Vector a(0, 0, 0);
	a[axis] = 1.0f;
	for (float up : kHeights)
	{
		Vector s = p + Vector(0, 0, up);
		bool ss = false;
		float f = FreeLine(s, s + a * MAX_SIDE, &ss);
		h = min(h, ss ? 0.0f : f * MAX_SIDE);
		f = FreeLine(s, s - a * MAX_SIDE, &ss);
		l = min(l, ss ? 0.0f : f * MAX_SIDE);
	}
	*lo = p[axis] - l;
	*hi = p[axis] + h;
}

static float ZoneDist(const CCSBotManager::Zone* z, const Vector& p)
{
	Vector lo = z->m_extent.lo, hi = z->m_extent.hi;
	if (z->m_isLegacy || hi.x - lo.x < 1.0f || hi.y - lo.y < 1.0f)
	{
		lo = z->m_center - Vector(128, 128, 0);
		hi = z->m_center + Vector(128, 128, 0);
	}
	float dx = max(max(lo.x - p.x, 0.0f), p.x - hi.x), dy = max(max(lo.y - p.y, 0.0f), p.y - hi.y);
	return sqrtf(dx * dx + dy * dy);
}

// The cells of a wall across the gap, two high, bottom row first, and where its builder stands
static void PlanWall(Entrance& e)
{
	MC_WHERE("tactics: planning a wall");
	e.numCells = e.cols = 0;
	int t = 1 - e.axis;
	Vector toSite(0, 0, 0);
	toSite[t] = e.toSite;
	e.stand = e.pos + toSite * 84.0f;
	static const float kBack[3] = {84.0f, 64.0f, 108.0f};
	for (float back : kBack)
	{
		Vector s = e.pos + toSite * back;
		float gz;
		if (GroundBelow(s, 40.0f, 80.0f, &gz) && FreeLine(e.pos + Vector(0, 0, 40), Vector(s.x, s.y, gz + 40.0f)) >= 1.0f)
		{
			e.stand = Vector(s.x, s.y, gz + 36.0f);
			break;
		}
	}
	if (e.width > MAX_GAP)
		return;
	float o = g_world.origin[e.axis];
	int j0 = (int)floorf((e.lo - o) / CELL), j1 = (int)floorf((e.hi - o) / CELL);
	int row = (int)floorf((e.pos[t] - g_world.origin[t]) / CELL);
	int low[MAX_WALL][3], high[MAX_WALL][3], nLow = 0, nHigh = 0;
	for (int j = j0; j <= j1 && nLow < MAX_WALL / 2 && nHigh < MAX_WALL / 2; j++)
	{
		float cmin = o + j * CELL, cmax = cmin + CELL;
		if (min(e.hi, cmax) - max(e.lo, cmin) < 22.0f)
			continue; // what stays open beside the wall is too narrow to walk through
		Vector c(0, 0, e.pos.z);
		c[e.axis] = cmin + CELL * 0.5f;
		c[t] = g_world.origin[t] + (row + 0.5f) * CELL;
		float gz = e.pos.z;
		GroundBelow(c, 30.0f, 60.0f, &gz);
		// the lowest cell that leaves nothing to crawl under and whose top cannot be jumped
		int k = (int)ceilf((gz - 8.0f - g_world.origin[2]) / CELL);
		int cell[3];
		cell[e.axis] = j;
		cell[t] = row;
		bool any = false;
		for (int up = 0; up < 2; up++)
		{
			cell[2] = k + up;
			if (!Takes(cell))
				continue;
			int* dst = up ? high[nHigh++] : low[nLow++];
			dst[0] = cell[0];
			dst[1] = cell[1];
			dst[2] = cell[2];
			any = true;
		}
		if (any)
			e.cols++;
	}
	for (int i = 0; i < nLow; i++, e.numCells++)
		memcpy(e.cells[e.numCells], low[i], sizeof(low[i]));
	for (int i = 0; i < nHigh; i++, e.numCells++)
		memcpy(e.cells[e.numCells], high[i], sizeof(high[i]));
}

static void FindPairs(Entrance& e)
{
	e.numPairs = 0;
	for (CNavArea* a : TheNavAreaList)
	{
		const Extent* x = a->GetExtent();
		if (x->lo.x > e.pos.x + 500.0f || x->hi.x < e.pos.x - 500.0f || x->lo.y > e.pos.y + 500.0f || x->hi.y < e.pos.y - 500.0f)
			continue;
		for (int d = 0; d < NUM_DIRECTIONS; d++)
			for (const NavConnect& c : *a->GetAdjacentList((NavDirType)d))
				if (e.numPairs < MAX_PAIRS && LinkCrosses(e, a, c.area, d))
				{
					e.pairs[e.numPairs][0] = a->GetID();
					e.pairs[e.numPairs][1] = c.area->GetID();
					e.numPairs++;
				}
	}
}

static bool SameGap(const Entrance& a, const Entrance& b)
{
	if (a.site != b.site)
		return false;
	if (a.axis == b.axis && fabsf(a.pos[1 - a.axis] - b.pos[1 - a.axis]) < 60.0f && a.lo < b.hi && b.lo < a.hi && fabsf(a.pos.z - b.pos.z) < 90.0f)
		return true;
	return (a.width > MAX_GAP || b.width > MAX_GAP) && (a.pos - b.pos).Length() < 220.0f;
}

static void FindEntrances()
{
	g_ent.clear();
	g_entReady = true;
	g_numWalled = 0;
	double t0 = Millis();
	Items();
	if (!TheCSBots() || TheCSBots()->GetScenario() != CCSBotManager::SCENARIO_DEFUSE_BOMB || TheNavAreaList.empty() || !g_worldLoaded)
		return;
	static const char* kSpawn[2] = {"info_player_deathmatch", "info_player_start"};
	for (int zi = 0; zi < TheCSBots()->GetZoneCount(); zi++)
	{
		const CCSBotManager::Zone* zone = TheCSBots()->GetZone(zi);
		Vector goalPos = zone->m_center;
		CNavArea* goal = TheNavAreaGrid.GetNearestNavArea(&goalPos);
		for (int side = 0; side < 2; side++)
		{
			CBaseEntity* spawn = UTIL_FindEntityByClassname(nullptr, kSpawn[side]);
			if (!spawn || !goal)
				continue;
			Vector from = spawn->pev->origin;
			CNavArea* start = TheNavAreaGrid.GetNearestNavArea(&from);
			std::vector<Entrance> shut; // this side's ways in so far: the next route must find another
			for (int n = 0; n < 3; n++)
			{
				std::vector<Vector> pts;
				float length = 0.0f;
				if (!Route(start, goal, goalPos, shut, nullptr, &pts, &length))
					break;
				// every 16 units along the route, the gaps it passes through
				std::vector<Entrance> cands;
				float along = 0.0f;
				for (size_t k = 0; k + 1 < pts.size() && along < 1500.0f; k++)
				{
					Vector seg = pts[k + 1] - pts[k];
					float len = seg.Length();
					if (len < 1.0f)
						continue;
					Vector out = seg / len; // away from the site
					for (float s = 0.0f; s < len; s += 16.0f)
					{
						Vector p = pts[k] + out * s;
						float gz;
						if (!GroundBelow(p, 40.0f, 100.0f, &gz))
							continue;
						p.z = gz;
						float zd = ZoneDist(zone, p);
						if (zd < 30.0f || zd > 750.0f)
							continue;
						for (int axis = 0; axis < 2; axis++)
						{
							int t = 1 - axis;
							if (fabsf(out[t]) < 0.35f)
								continue; // the route runs along this line, not through it
							Entrance e;
							e.site = zi;
							e.pos = p;
							e.axis = axis;
							Gap(p, axis, &e.lo, &e.hi);
							e.width = e.hi - e.lo;
							e.toSite = out[t] < 0.0f ? 1.0f : -1.0f;
							cands.push_back(e);
						}
					}
					along += len;
				}
				if (cands.empty())
					break;
				std::stable_sort(cands.begin(), cands.end(), [](const Entrance& a, const Entrance& b) { return a.width < b.width - 10.0f; });
				// the narrowest gap that is a way in: shutting it makes the route clearly longer (a gap beside
				// a crate is not one)
				const Entrance* found = nullptr;
				std::vector<const Entrance*> tried;
				for (const Entrance& c : cands)
				{
					if (c.width > MAX_GAP || tried.size() >= 10)
						break;
					bool seen = false;
					for (const Entrance* r : tried)
						if (SameGap(*r, c))
							seen = true;
					if (seen)
						continue;
					tried.push_back(&c);
					float around = 0.0f;
					if (!Route(start, goal, goalPos, shut, &c, nullptr, &around) || around - length >= 140.0f)
					{
						found = &c;
						break;
					}
				}
				Entrance e;
				if (found)
					e = *found;
				else
				{
					// an open approach: remembered as a way in (for the routes players take), never walled
					const Entrance* best = nullptr;
					for (const Entrance& c : cands)
						if (!best || fabsf(ZoneDist(zone, c.pos) - 200.0f) < fabsf(ZoneDist(zone, best->pos) - 200.0f))
							best = &c;
					e = *best;
					if (e.width <= MAX_GAP)
						e.width = MAX_GAP + 1.0f;
				}
				bool again = false;
				for (const Entrance& s : shut)
					if (SameGap(s, e))
						again = true;
				if (again)
					break;
				shut.push_back(e);
				int at = -1;
				for (size_t i = 0; i < g_ent.size(); i++)
					if (SameGap(g_ent[i], e))
						at = (int)i;
				if (at < 0 && (int)g_ent.size() < MAX_ENT)
				{
					g_ent.push_back(e);
					at = (int)g_ent.size() - 1;
				}
				if (at >= 0 && g_ent[at].rank[side] < 0)
					g_ent[at].rank[side] = n;
			}
		}
	}
	for (size_t i = 0; i < g_ent.size(); i++)
	{
		Entrance& e = g_ent[i];
		PlanWall(e);
		FindPairs(e);
		const CCSBotManager::Zone* zone = TheCSBots()->GetZone(e.site);
		Vector goalPos = zone->m_center;
		CNavArea* goal = TheNavAreaGrid.GetNearestNavArea(&goalPos);
		CNavArea* gapArea = TheNavAreaGrid.GetNearestNavArea(&e.pos);
		std::vector<Entrance> none;
		for (int side = 0; side < 2; side++)
		{
			CBaseEntity* spawn = UTIL_FindEntityByClassname(nullptr, kSpawn[side]);
			if (!spawn)
				continue;
			Vector at = spawn->pev->origin;
			CNavArea* start = TheNavAreaGrid.GetNearestNavArea(&at);
			float open = 0.0f, shut = 0.0f;
			if (!Route(start, gapArea, e.pos, none, nullptr, nullptr, &e.from[side]))
				e.from[side] = 4000.0f;
			if (Route(start, goal, goalPos, none, nullptr, nullptr, &open))
				e.around[side] = Route(start, goal, goalPos, none, &e, nullptr, &shut) ? max(0.0f, shut - open) : 3000.0f;
		}
		McLog("tactics: way in #%d to site %d at (%.0f %.0f %.0f): %.0f wide along %s, order T %d CT %d, a wall takes %d blocks in %d columns, %d links",
			(int)i, e.site, e.pos.x, e.pos.y, e.pos.z, e.width, e.axis ? "y" : "x", e.rank[0], e.rank[1], e.numCells, e.cols, e.numPairs);
	}
	McLog("tactics: %d ways into %d bomb sites, worked out in %.0f ms", (int)g_ent.size(), TheCSBots()->GetZoneCount(), Millis() - t0);
}

// What going through a walled way in costs a bot's route planning (cs_bot.h PathCost): the time to dig
// through or blow it up, as distance. With another way not much longer, it goes around.
// A mine: TNT in a floor cell a bot dug out, a pressure plate on it. Its own side keeps off it; of the
// other side, whoever has noticed the plate does.
struct Mine
{
	int lid[3];
	Vector pos;
	int team;
	unsigned seen, judged; // the players (bit by index) who have noticed it / have had their look at it
};
static std::vector<Mine> g_mines;

float BotWallCost(CBasePlayer* bot, CNavArea* area, CNavArea* from)
{
	if (area && bot)
		for (const Mine& m : g_mines)
			if ((m.team == bot->m_iTeam || (m.seen & (1u << (bot->entindex() & 31)))) && area->IsOverlapping(&m.pos))
				return 6000.0f;
	if (!g_numWalled || !from || !area || g_testNoWayAround)
		return 0.0f;
	unsigned a = from->GetID(), b = area->GetID();
	for (const std::vector<Entrance>* list : {&g_ent, &g_dyn})
		for (const Entrance& e : *list)
		{
			if (!e.walled)
				continue;
			for (int i = 0; i < e.numPairs; i++)
				if (e.pairs[i][0] == a && e.pairs[i][1] == b)
				{
					if (e.team == bot->m_iTeam)
						return 8000.0f; // its own side's wall: as good as no way
					return HasPick(bot) ? 700.0f : (HasTnt(bot) && HasFlint(bot)) ? 1500.0f : 3600.0f;
				}
		}
	return 0.0f;
}

// ---------------------------------------------------------------------------------------------
// What is learned: the ways in the human players take

enum
{
	ROLE_ATTACK, // a human Terrorist going for a site
	ROLE_RETAKE, // a human Counter-Terrorist coming back to a planted bomb
	NUM_ROLES
};
static const char* kRoleName[NUM_ROLES] = {"as a Terrorist going in", "as a Counter-Terrorist coming back to the bomb"};
struct RouteModel
{
	float marg[MAX_ENT];              // how often each way in was taken, recent rounds counting more
	float ctx[MAX_ENT + 1][MAX_ENT];  // the same, by the way taken the round before (MAX_ENT: none known)
	int hits, total;                  // how often the guess made before the round was right
};
static RouteModel g_route[NUM_ROLES];

// The chance of each way in (of one site, or of any: site < 0), given the way taken the round before
static void RouteProbs(int role, int last, int site, float out[MAX_ENT])
{
	const RouteModel& m = g_route[role];
	int side = role == ROLE_ATTACK ? 0 : 1;
	float prior[MAX_ENT], sumPrior = 0.0f, sumMarg = 0.0f, sumCtx = 0.0f;
	if (last < 0 || last > MAX_ENT)
		last = MAX_ENT;
	for (int e = 0; e < MAX_ENT; e++)
	{
		bool ok = e < (int)g_ent.size() && (site < 0 || g_ent[e].site == site);
		// before anything is known: the shorter way is the likelier one
		prior[e] = !ok ? 0.0f : g_ent[e].rank[side] >= 0 ? 1.0f / (1.0f + g_ent[e].rank[side]) : 0.15f;
		out[e] = 0.0f;
		if (!ok)
			continue;
		sumPrior += prior[e];
		sumMarg += m.marg[e];
		sumCtx += m.ctx[last][e];
	}
	if (sumPrior <= 0.0f)
		return;
	for (int e = 0; e < MAX_ENT; e++)
	{
		if (prior[e] <= 0.0f)
			continue;
		float overall = (m.marg[e] + prior[e] / sumPrior) / (sumMarg + 1.0f);
		out[e] = cv_learn.value == 0.0f ? prior[e] / sumPrior : (m.ctx[last][e] + 2.0f * overall) / (sumCtx + 2.0f);
	}
}

static void RouteSeen(int role, int last, int e)
{
	if (e < 0 || e >= MAX_ENT || cv_learn.value == 0.0f)
		return;
	RouteModel& m = g_route[role];
	if (last < 0 || last > MAX_ENT)
		last = MAX_ENT;
	const float keep = 0.92f;
	for (int i = 0; i < MAX_ENT; i++)
	{
		m.marg[i] *= keep;
		for (int l = 0; l <= MAX_ENT; l++)
			m.ctx[l][i] *= keep;
	}
	m.marg[e] += 1.0f;
	m.ctx[last][e] += 1.0f;
}

// ---------------------------------------------------------------------------------------------
// What is learned: what the human players do at a wall across their way. They dig through it, or it is
// blown open, or they turn away from it. A builder decides by it whether to stay behind its wall (an ambush
// for whoever comes through) or to leave the wall standing alone and go where the fight is.

enum
{
	ANS_DIG,
	ANS_BLAST,
	ANS_AWAY,
	NUM_ANS
};
static const char* kAnsName[NUM_ANS] = {"dug through it", "had it blown open", "turned away from it"};
static float g_answer[NUM_ANS]; // how often each, recent walls counting more
static int g_answerTotal = 0;

static void AnswerSeen(int a)
{
	if (a < 0 || a >= NUM_ANS || cv_learn.value == 0.0f)
		return;
	for (float& v : g_answer)
		v *= 0.9f;
	g_answer[a] += 1.0f;
	g_answerTotal++;
}
// How likely they come through a wall rather than turn away from it (nothing known: even)
static float AnswerThrough()
{
	float sum = g_answer[ANS_DIG] + g_answer[ANS_BLAST] + g_answer[ANS_AWAY];
	return cv_learn.value == 0.0f ? 0.5f : (g_answer[ANS_DIG] + g_answer[ANS_BLAST] + 1.0f) / (sum + 2.0f);
}
// ... and how likely the wall is blown open
static float AnswerBlast()
{
	float sum = g_answer[ANS_DIG] + g_answer[ANS_BLAST] + g_answer[ANS_AWAY];
	return cv_learn.value == 0.0f ? 0.25f : (g_answer[ANS_BLAST] + 0.5f) / (sum + 2.0f);
}

// ---------------------------------------------------------------------------------------------
// What is learned: which tactic wins rounds

enum Tactic
{
	TAC_SETUP,  // cover or height where it holds
	TAC_WALL,   // a wall across the enemy's way
	TAC_SHIELD, // blocks up before a defuse
	TAC_FIRE,   // cover thrown up under fire
	TAC_TNT,    // TNT: cover blown out, walls blown through, a way blown in
	NUM_TAC
};
enum
{
	NUM_CTX = NUM_TAC * 2 // each for the Terrorists, then for the Counter-Terrorists
};
static int CtxOf(int team, int tactic) { return (team == CT ? NUM_TAC : 0) + tactic; }
static const int kArms[NUM_CTX] = {2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
static const char* kTacName[NUM_TAC] = {"holding a spot", "the enemy near with a gap between", "at the bomb with enemies about", "shot at in the open",
	"TNT in the pack"};
static const char* kArmName[NUM_TAC][2] = {{"hold it as it is", "build cover or height"},
	{"leave the gap open", "wall it"},
	{"defuse in the open", "blocks up first"},
	{"fight on", "throw up cover"},
	{"keep it in the pack", "use it"}};
static const char* CtxName(int c)
{
	static char buf[96];
	Q_snprintf(buf, sizeof(buf), "%s, %s", c >= NUM_TAC ? "Counter-Terrorists" : "Terrorists", kTacName[c % NUM_TAC]);
	return buf;
}
struct Arm
{
	float a = 1.0f, b = 1.0f; // rounds won / lost with it (and one each to start from), fading
	int plays = 0;
};
static Arm g_arm[NUM_CTX][4];
static int g_choice[NUM_CTX];
static bool g_chance[NUM_CTX]; // did this round put the decision to the team at all?

static float Normal()
{
	float u1 = RANDOM_FLOAT(0.0001f, 1.0f), u2 = RANDOM_FLOAT(0.0f, 1.0f);
	return sqrtf(-2.0f * logf(u1)) * cosf(6.2831853f * u2);
}
// Gamma(k, 1), k >= 1 (Marsaglia and Tsang)
static float GammaSample(float k)
{
	float d = k - 1.0f / 3.0f, c = 1.0f / sqrtf(9.0f * d);
	for (int guard = 0; guard < 64; guard++)
	{
		float x = Normal(), v = 1.0f + c * x;
		if (v <= 0.0f)
			continue;
		v = v * v * v;
		float u = RANDOM_FLOAT(0.0001f, 1.0f);
		if (u < 1.0f - 0.0331f * x * x * x * x || logf(u) < 0.5f * x * x + d * (1.0f - v + logf(v)))
			return d * v;
	}
	return d;
}
// Thompson sampling, once a round for each kind of moment: draw a win rate for taking such chances and one
// for leaving them from what is known of each, and go with the better draw. What has done well is done
// more, what was hardly tried still gets its turns, and no two rounds need be in the same mood.
static int Choose(int ctx)
{
	if (cv_learn.value == 0.0f)
		return RANDOM_LONG(0, kArms[ctx] - 1); // nothing learned: even chances
	int best = 0;
	float bestV = -1.0f;
	for (int i = 0; i < kArms[ctx]; i++)
	{
		float x = GammaSample(g_arm[ctx][i].a), y = GammaSample(g_arm[ctx][i].b);
		float v = x / (x + y);
		if (v > bestV)
		{
			bestV = v;
			best = i;
		}
	}
	return best;
}
static void Reward(int ctx, int arm, bool won)
{
	if (cv_learn.value == 0.0f)
		return;
	const float keep = 0.95f; // about the last twenty rounds count
	for (int i = 0; i < kArms[ctx]; i++)
	{
		g_arm[ctx][i].a = 1.0f + (g_arm[ctx][i].a - 1.0f) * keep;
		g_arm[ctx][i].b = 1.0f + (g_arm[ctx][i].b - 1.0f) * keep;
	}
	(won ? g_arm[ctx][arm].a : g_arm[ctx][arm].b) += 1.0f;
	g_arm[ctx][arm].plays++;
}

// ---------------------------------------------------------------------------------------------
// The memory file

static char g_brainMap[64] = "";

static void BrainPath(char* path, size_t size)
{
	char gd[256];
	GET_GAME_DIR(gd);
	Q_snprintf(path, size, "%s/mc_brain_%s.txt", gd, g_brainMap);
}

static void SaveBrain()
{
	if (!g_brainMap[0] || cv_learn.value == 0.0f)
		return;
	char path[512];
	BrainPath(path, sizeof(path));
	FILE* f = fopen(path, "w");
	if (!f)
		return;
	fprintf(f, "MineStrike bot memory v2 for %s: what the bots have learned here (delete the file to forget)\n", g_brainMap);
	for (int c = 0; c < NUM_CTX; c++)
		for (int i = 0; i < kArms[c]; i++)
			fprintf(f, "arm %d %d %.4f %.4f %d\n", c, i, g_arm[c][i].a, g_arm[c][i].b, g_arm[c][i].plays);
	for (int a = 0; a < NUM_ANS; a++)
		fprintf(f, "answer %d %.4f\n", a, g_answer[a]);
	fprintf(f, "answers %d\n", g_answerTotal);
	for (size_t e = 0; e < g_ent.size(); e++)
		fprintf(f, "ent %d %.0f %.0f %.0f\n", (int)e, g_ent[e].pos.x, g_ent[e].pos.y, g_ent[e].pos.z);
	for (int r = 0; r < NUM_ROLES; r++)
	{
		fprintf(f, "acc %d %d %d\n", r, g_route[r].hits, g_route[r].total);
		for (int e = 0; e < MAX_ENT; e++)
			if (g_route[r].marg[e] > 0.001f)
				fprintf(f, "marg %d %d %.4f\n", r, e, g_route[r].marg[e]);
		for (int l = 0; l <= MAX_ENT; l++)
			for (int e = 0; e < MAX_ENT; e++)
				if (g_route[r].ctx[l][e] > 0.001f)
					fprintf(f, "ctx %d %d %d %.4f\n", r, l == MAX_ENT ? -1 : l, e, g_route[r].ctx[l][e]);
	}
	fclose(f);
}

static void LoadBrain()
{
	for (int c = 0; c < NUM_CTX; c++)
		for (int i = 0; i < 4; i++)
			g_arm[c][i] = Arm();
	memset(g_route, 0, sizeof(g_route));
	memset(g_answer, 0, sizeof(g_answer));
	g_answerTotal = 0;
	Q_strlcpy(g_brainMap, STRING(gpGlobals->mapname));
	char path[512];
	BrainPath(path, sizeof(path));
	FILE* f = fopen(path, "r");
	if (!f)
		return;
	// the ways in are matched by where they are: the list may have changed since the file was written
	int remap[MAX_ENT];
	for (int& r : remap)
		r = -1;
	char line[256];
	int lines = 0;
	if (!fgets(line, sizeof(line), f) || !strstr(line, "memory v2"))
	{
		fclose(f); // written by an older build: its numbers meant something else
		return;
	}
	while (fgets(line, sizeof(line), f))
	{
		int a, b, c, n;
		float x, y, z;
		if (sscanf(line, "arm %d %d %f %f %d", &a, &b, &x, &y, &n) == 5)
		{
			if (a >= 0 && a < NUM_CTX && b >= 0 && b < kArms[a] && x >= 1.0f && y >= 1.0f && x < 1000.0f && y < 1000.0f)
			{
				g_arm[a][b].a = x;
				g_arm[a][b].b = y;
				g_arm[a][b].plays = n;
			}
		}
		else if (sscanf(line, "answer %d %f", &a, &x) == 2)
		{
			if (a >= 0 && a < NUM_ANS && x >= 0.0f && x < 1000.0f)
				g_answer[a] = x;
		}
		else if (sscanf(line, "answers %d", &a) == 1)
			g_answerTotal = max(0, a);
		else if (sscanf(line, "ent %d %f %f %f", &a, &x, &y, &z) == 4)
		{
			if (a >= 0 && a < MAX_ENT)
				for (size_t e = 0; e < g_ent.size(); e++)
					if ((g_ent[e].pos - Vector(x, y, z)).Length() < 80.0f)
						remap[a] = (int)e;
		}
		else if (sscanf(line, "acc %d %d %d", &a, &b, &c) == 3)
		{
			if (a >= 0 && a < NUM_ROLES && b >= 0 && c >= b)
			{
				g_route[a].hits = b;
				g_route[a].total = c;
			}
		}
		else if (sscanf(line, "marg %d %d %f", &a, &b, &x) == 3)
		{
			if (a >= 0 && a < NUM_ROLES && b >= 0 && b < MAX_ENT && remap[b] >= 0 && x > 0.0f && x < 1000.0f)
				g_route[a].marg[remap[b]] = x;
		}
		else if (sscanf(line, "ctx %d %d %d %f", &a, &b, &c, &x) == 4)
		{
			int l = b < 0 ? MAX_ENT : (b < MAX_ENT ? remap[b] : -1);
			if (a >= 0 && a < NUM_ROLES && l >= 0 && c >= 0 && c < MAX_ENT && remap[c] >= 0 && x > 0.0f && x < 1000.0f)
				g_route[a].ctx[l][remap[c]] = x;
		}
		lines++;
	}
	fclose(f);
	McLog("tactics: memory read from %s (%d lines)", path, lines);
}

// ---------------------------------------------------------------------------------------------
// A wall of a bomb site thin enough for TNT, with a long walk round it (FindBreaches)
struct Breach
{
	int site;
	Vector out, in; // on the ground either side of the wall: out where the TNT goes, in on the site's side
	Vector dir;     // from in to out
	float thick, saved;
};
static std::vector<Breach> g_breach;

// ---------------------------------------------------------------------------------------------
// Jobs: one thing a bot has taken on with its blocks, its hands or its TNT

enum
{
	JOB_NONE,
	JOB_BUILD,
	JOB_PILLAR,
	JOB_MINE,
	JOB_TNT,
	JOB_BRIDGE,
	JOB_TRAP // a mine: a floor cell dug out, TNT in it, a plate on top
};
enum
{
	PH_GO,
	PH_WORK,
	PH_RUN,    // from its own TNT
	PH_THROUGH // through the hole the TNT made
};
enum
{
	AFTER_NOTHING,
	AFTER_POST, // take the position it built
	AFTER_LEAVE // leave the wall standing alone and go where the other way in is
};
static const char* kJobName[] = {"no", "building", "stacking up", "digging", "TNT", "bridging", "laying a mine"};
struct Job
{
	int kind = JOB_NONE, phase = PH_GO;
	int cells[MAX_JOB_CELLS][3];
	int num = 0, at = 0, tries = 0, done = 0;
	Vector stand;
	float arrive = 24.0f; // how near the stand point counts as there
	float nextAct = 0, nextMove = 0, deadline = 0;
	bool urgent = false;  // goes on with an enemy in view (cover thrown up under fire)
	int after = AFTER_NOTHING;
	Vector post, watch;   // AFTER_POST: where it then stands, and what it watches from there
	float postFor = 0;
	bool tntDown = false;
	float blast = 0;
	Vector flee;
	Vector way[16]; // the way back from the TNT: its own trail, newest first
	int wayNum = 0, wayAt = 0;
	int breach = -1; // the TNT is to open this wall (g_breach): the bot goes through afterwards
	bool wall = false; // a wall across a way in (GiveWallAt)
	int site = -1;     // ... of this bomb site
	bool wood = false; // ... of planks
	bool golem = false; // a mob's body: the T from where its blocks go, then its head or heads (WitGolem)
	int heads = 1;      // ... the last so many cells from the other slot (a golem's pumpkin, a wither's three skulls)
	bool shield = false; // blocks put up at the bomb before a defuse (WitShield)
	float slowSince = 0;
	bool climbing = false; // PH_THROUGH: setting a block under its feet to get up out of the crater
	bool gunOut = false;   // an enemy showed while it worked: it has its gun in its hands again
};
static Job g_job[MAX_CLIENTS + 1];

// A bot at the position it built: it stays there on its feet and watches the way the enemy is expected.
struct Post
{
	bool on = false, high = false;
	bool cover = false; // watching over a mate at work for a moment, whatever else is going on
	Vector spot, watch;
	float until = 0;
};
static Post g_post[MAX_CLIENTS + 1];

struct Track
{
	// bots
	Vector anchor;
	float anchorTime = 0;               // since when it has not got anywhere
	float nextProbe = 0, nextFlush = 0, nextCover = 0, nextCrumb = 0;
	float sentAround = -100.0f;         // when it was last told to find a way around a wall
	// this round's temper: how readily it takes a chance with its blocks, and how long it holds a spot
	// before it thinks of improving it
	float nerve = 0.5f, settle = 8.0f;
	bool builder = false; // this round it also builds cover and walls with its blocks
	float nextGround = 0, nextWatch = 0;
	unsigned denySeen = 0; // the ways in it has weighed shutting this round
	Vector holdAt;
	float holdSince = 0, nextWit = 0, nextWall = 0;
	bool holdJudged = false, shieldJudged = false, breachJudged = false, mineJudged = false;
	float nextGolem = 0;
	float blockedSince = 0, blockedSeen = 0; // pushing at placed blocks: since when, and when last
	Vector blockedAt;
	float groundZ = 0;                  // its feet when it last stood on something
	float hp = 0, hurtTime = -100.0f;   // health last frame, and when it last dropped
	Vector crumbs[16];                  // where it has been, every half second
	int crumbAt = 0, crumbNum = 0;
	// humans
	Vector prev;
	bool prevValid = false;
	bool met = false;                   // it has come up against a wall of the other side's this round
	Vector metAt;
	bool metDug = false;                // ... and somebody of its side has been digging at it
	int answer = -1;                    // what became of it (ANS_*)
	int crossed[NUM_ROLES] = {-1, -1};  // the way in taken this round
	int last[NUM_ROLES] = {-1, -1};     // ... and the round before
	int guess[NUM_ROLES] = {-1, -1};    // what was expected of them
	int team = 0;
};
static Track g_track[MAX_CLIENTS + 1];

struct Plan
{
	bool live = false; // between the end of the freeze time and the end of the round
	float begin = 0;
	// the bomb
	edict_t* bombEd = nullptr;
	int bombSerial = 0;
	float plantTime = 0;
	bool plantDecided = false, plantGiven = false, siteKnown = false;
	int site = -1, plantTries = 0;
	// setting up at the start
	bool holdDecided = false, holdGiven = false;
	int holdTries = 0;
};
static Plan g_plan;
static bool g_wasFreeze = true;

// ---------------------------------------------------------------------------------------------
// The round's story. What was done with blocks and TNT that is worth telling is noted as it happens (a
// wall and what its builder did after, a way blown in, a wall dug through, a player turning away from one, a
// death by TNT), each with a weight; when the round is over the two weightiest go to everybody in one line
// with how the round was won. What a bot decided in secret (stayed behind its wall, left it) is told then.

struct Told
{
	float weight, time;
	char text[128];
};
static std::vector<Told> g_story;

void StoryTell(float weight, const char* fmt, ...)
{
	Told t;
	t.weight = weight;
	t.time = gpGlobals->time;
	va_list ap;
	va_start(ap, fmt);
	Q_vsnprintf(t.text, sizeof(t.text), fmt, ap);
	va_end(ap);
	if (g_story.size() < 64)
		g_story.push_back(t);
	McLog("story: (%.0f) %s", weight, t.text);
}

// Where something happened, in words a player knows. Known places of de_dust2 by name; elsewhere, and on
// other maps, the bomb site it is nearest to.
static const char* PlaceName(const Vector& p)
{
	static char buf[4][40];
	static int at = 0;
	char* out = buf[at = (at + 1) & 3];
	const char* map = STRING(gpGlobals->mapname);
	bool dust2 = !strncmp(map, "de_dust2", 8);
	if (dust2)
	{
		struct Mark
		{
			const char* name;
			float x, y, r;
		};
		static const Mark kMarks[] = {{"the B tunnels", -1950, 1788, 330}, {"the B doors", -1350, 2138, 260}, {"the B site wall", -1310, 2420, 170},
			{"A short", 351, 2021, 330}, {"long A", 1370, 2340, 330}};
		for (const Mark& m : kMarks)
			if ((Vector(m.x, m.y, 0) - Vector(p.x, p.y, 0)).Length() < m.r)
			{
				Q_strlcpy(out, m.name, 40);
				return out;
			}
	}
	const CCSBotManager::Zone* best = nullptr;
	float bd = 1100.0f;
	for (int z = 0; TheCSBots() && z < TheCSBots()->GetZoneCount(); z++)
	{
		const CCSBotManager::Zone* zone = TheCSBots()->GetZone(z);
		float d = (zone->m_center - p).Length2D();
		if (d < bd)
		{
			bd = d;
			best = zone;
		}
	}
	if (!best)
		Q_strlcpy(out, "a passage between the sites", 40);
	else if (dust2)
		Q_snprintf(out, 40, "bomb site %s", best->m_center.x > 0.0f ? "A" : "B");
	else
		Q_snprintf(out, 40, "bomb site %d", best->m_index + 1);
	return out;
}

static void TellStory(int winStatus, int event)
{
	if (g_story.empty())
		return;
	std::stable_sort(g_story.begin(), g_story.end(), [](const Told& a, const Told& b) { return a.weight != b.weight ? a.weight > b.weight : a.time > b.time; });
	const char* how = event == ROUND_TARGET_BOMB ? "the bomb went off" : event == ROUND_BOMB_DEFUSED ? "the bomb was defused" :
		event == ROUND_TARGET_SAVED ? "the clock ran out" : "the other side is dead";
	char line[400];
	int len = Q_snprintf(line, sizeof(line), "Round to the %s, %s.", winStatus == WINSTATUS_CTS ? "Counter-Terrorists" : "Terrorists", how);
	for (size_t i = 0; i < g_story.size() && i < 2; i++)
	{
		if (g_story[i].weight < 2.0f && i > 0)
			break; // (cover and the like are told only when there is nothing else)
		char one[140];
		Q_strlcpy(one, g_story[i].text);
		if (one[0] >= 'a' && one[0] <= 'z')
			one[0] = (char)(one[0] - 'a' + 'A');
		if (len + (int)strlen(one) + 3 < 180) // (one chat line)
			len += Q_snprintf(line + len, sizeof(line) - len, " %s.", one);
	}
	McLog("story: %s", line);
	ToastAll(0, "%s", line);
	g_story.clear();
}

static CGrenade* Bomb()
{
	edict_t* e = g_plan.bombEd;
	if (!e || e->free || e->serialnumber != g_plan.bombSerial || !e->pvPrivateData || (e->v.flags & FL_KILLME))
		return nullptr;
	CGrenade* g = static_cast<CGrenade*>(CBaseEntity::Instance(e));
	return (g && g->m_bIsC4 && !g->m_bJustBlew) ? g : nullptr;
}

static void StartJob(CBasePlayer* bot, const Job& j, const char* what)
{
	g_job[bot->entindex()] = j;
	g_post[bot->entindex()].on = false;
	McLog("tactics: %s (%s) %s: %d cells, %s", STRING(bot->pev->netname), bot->m_iTeam == CT ? "CT" : "T", what, j.num,
		j.phase == PH_GO ? "on the way" : "at work");
}

static const char* OneOf(std::initializer_list<const char*> lines);

static void Finish(CBasePlayer* bot, const char* why)
{
	int i = bot->entindex();
	Job j = g_job[i];
	if (j.kind == JOB_NONE)
		return;
	McLog("tactics: %s is done %s (%s, %d done)", STRING(bot->pev->netname), kJobName[j.kind], why, j.done);
	if (j.kind == JOB_MINE || j.kind == JOB_TRAP)
		MineFrame(bot, false); // the cracks go
	g_job[i] = Job();
	if (!bot->IsAlive())
		return;
	CCSBot* ai = static_cast<CCSBot*>(bot);
	ai->EquipBestWeapon(true);
	bool built = j.done >= (j.kind == JOB_PILLAR ? j.num : min(j.num, 4));
	// for the round's story
	const char* name = STRING(bot->pev->netname);
	const char* of = j.wood ? "planks" : "stone";
	if (j.kind == JOB_TNT && j.breach >= 0 && !strcmp(why, "through the wall"))
		StoryTell(5.0f, "%s blew a way in through %s", name, PlaceName(bot->pev->origin));
	else if (j.kind == JOB_TNT && !strcmp(why, "the TNT went off"))
		StoryTell(4.0f, "%s set off TNT at %s", name, PlaceName(bot->pev->origin));
	else if (j.kind == JOB_MINE && !strcmp(why, "through"))
		StoryTell(3.0f, "%s dug through the blocks at %s", name, PlaceName(bot->pev->origin));
	else if (j.kind == JOB_TRAP && !strcmp(why, "laid"))
		StoryTell(3.0f, "%s laid a mine at %s", name, PlaceName(CellMid(j.cells[1])));
	else if (built && j.kind == JOB_BUILD && j.shield)
		StoryTell(4.0f, "%s put blocks up at the bomb before defusing", name);
	else if (built && j.kind == JOB_BUILD && j.wall && j.after == AFTER_POST)
		StoryTell(3.0f, "%s walled %s with %s and waited behind it", name, PlaceName(j.stand), of);
	else if (built && j.kind == JOB_BUILD && j.wall && j.after == AFTER_LEAVE)
		StoryTell(3.0f, "%s walled %s with %s and left it to stand alone", name, PlaceName(j.stand), of);
	else if (built && j.kind == JOB_BUILD && j.wall)
		StoryTell(3.0f, "%s walled %s with %s", name, PlaceName(j.stand), of);
	else if (built && j.kind == JOB_PILLAR && j.after == AFTER_POST)
		StoryTell(1.0f, "%s took the high ground at %s on two blocks", name, PlaceName(bot->pev->origin));
	else if (built && j.kind == JOB_BUILD && j.after == AFTER_POST)
		StoryTell(1.0f, "%s built cover at %s", name, PlaceName(bot->pev->origin));
	if (j.kind == JOB_BRIDGE || (j.kind == JOB_PILLAR && j.after == AFTER_NOTHING))
		return; // (it was on its way somewhere: its own AI goes on)
	if (j.after == AFTER_POST && built)
	{
		Post& p = g_post[i];
		p.on = true;
		p.cover = false;
		p.high = j.kind == JOB_PILLAR;
		p.spot = p.high ? bot->pev->origin : j.post;
		p.watch = j.watch;
		p.until = gpGlobals->time + j.postFor;
		if (!p.high)
			ai->Hide(&p.spot, j.postFor, true); // its own AI holds the place too
		if (j.wall)
		{
			McLog("tactics: %s stays behind its wall and listens", STRING(bot->pev->netname));
			Say(bot, OneOf({"Wall's up. I'm staying on it.", "Holding behind the wall.", "Let them try to come through."}));
		}
		return;
	}
	if (j.after == AFTER_LEAVE && built)
	{
		// the wall stands alone now: to the other bomb site, or (the bomb down) back to the bomb
		Vector to = bot->pev->origin;
		bool have = false;
		if (CGrenade* bomb = Bomb())
		{
			to = bomb->pev->origin;
			have = true;
		}
		else if (TheCSBots())
			for (int z = 0; z < TheCSBots()->GetZoneCount() && !have; z++)
			{
				const CCSBotManager::Zone* zone = TheCSBots()->GetZone(z);
				if (zone && zone->m_index != j.site)
				{
					to = zone->m_center;
					have = true;
				}
			}
		McLog("tactics: %s leaves its wall standing alone%s", STRING(bot->pev->netname), have ? " and goes where the other way in is" : "");
		Say(bot, OneOf({"Wall's up. Rotating.", "That way is shut. Moving over.", "Sealed. I'm needed elsewhere."}));
		if (have)
			ai->MoveTo(&to, FASTEST_ROUTE);
		else
			ai->Idle();
		return;
	}
	if (!j.urgent)
		ai->Idle(); // on with its round
}

// A teammate within the blast of TNT at a point: the fuse waits
static bool MateInBlast(CBasePlayer* bot, const Vector& at)
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (p && p != bot && p->IsAlive() && p->m_iTeam == bot->m_iTeam && (p->pev->origin - at).Length() < 300.0f)
			return true;
	}
	return false;
}

// Can somebody walk from one point on the ground to another in a straight line? Steps up too high to jump
// are climbed with a block under the feet each (blocks: how many it has; needs: how many the way takes).
static bool WalkThrough(const Vector& a, const Vector& b, int blocks, int* needs = nullptr)
{
	Vector d = b - a;
	d.z = 0.0f;
	float len = d.Length();
	int need = 0;
	if (len >= 1.0f)
	{
		d = d / len;
		float top = max(a.z, b.z) + 56.0f, prev = a.z;
		for (float u = 0.0f; u <= len; u += 12.0f)
		{
			Vector p(a.x + d.x * u, a.y + d.y * u, top);
			bool ss = false;
			float f = FreeLine(p, p - Vector(0, 0, 400), &ss);
			float gz = top - f * 400.0f;
			if (ss || f >= 1.0f)
				return false; // something still stands there, or there is no bottom
			if (gz - prev > 44.0f)
				need += (int)ceilf((gz - prev - 44.0f) / CELL);
			prev = gz;
		}
	}
	if (needs)
		*needs = need;
	return need <= blocks;
}

// Where to run from TNT: back the way it came, far enough
static Vector FleeSpot(CBasePlayer* bot, const Vector& tnt)
{
	const Track& t = g_track[bot->entindex()];
	Vector org = bot->pev->origin, best = org, prev = org;
	float bestD = (best - tnt).Length();
	for (int k = 1; k <= t.crumbNum; k++)
	{
		const Vector& c = t.crumbs[(t.crumbAt - k + 32) % 16];
		if ((c - prev).Length() > 220.0f)
			break; // the trail is broken here (a respawn)
		prev = c;
		float d = (c - tnt).Length();
		if (d > bestD)
		{
			bestD = d;
			best = c;
		}
		if (d >= 400.0f)
			break;
	}
	if (bestD < 300.0f)
	{
		// no trail to follow: straight away from it, as far as the ground goes
		Vector away = org - tnt;
		away.z = 0.0f;
		away = away.Normalize();
		static const float kDist[3] = {400.0f, 320.0f, 240.0f};
		for (float dist : kDist)
		{
			Vector p = tnt + away * dist;
			float gz;
			if ((p - tnt).Length() > bestD && GroundBelow(Vector(p.x, p.y, org.z), 30.0f, 80.0f, &gz) && FreeLine(org, Vector(p.x, p.y, gz + 36.0f)) >= 1.0f)
				return Vector(p.x, p.y, gz + 36.0f);
		}
	}
	return best;
}

static bool BaseCell(const Vector& ground, int out[3], float* top);

static void RunJob(CBasePlayer* bot, CCSBot* ai, Job& j)
{
	static const char* kWhere[] = {"tactics: no job", "tactics: a building job", "tactics: a stacking job", "tactics: a digging job", "tactics: a TNT job",
		"tactics: a bridging job", "tactics: a mine-laying job"};
	MC_WHERE(kWhere[j.kind]);
	float now = gpGlobals->time;
	McPlayer& mp = P(bot);
	Vector org = bot->pev->origin, eye = org + bot->pev->view_ofs;
	auto holding = [&](int slot) {
		if (mp.selected == slot && mp.mcItemActive)
			return true;
		SelectSlot(bot, slot);
		return false;
	};
	auto stepTo = [&](const Vector& mid) {
		// closer to a cell out of reach, on this side of it
		Vector d = org - mid;
		d.z = 0.0f;
		float len = d.Length();
		j.stand = len > 1.0f ? mid + d * (90.0f / len) : org;
		j.stand.z = org.z;
		j.phase = PH_GO;
	};
	if (j.phase == PH_GO)
	{
		Vector d = j.stand - org;
		float dz = fabsf(d.z);
		d.z = 0.0f;
		float dist = d.Length();
		if (dist < j.arrive && dz < 70.0f)
		{
			j.phase = PH_WORK;
			j.nextAct = now + 0.2f;
			return;
		}
		TraceResult tr;
		tr.flFraction = 0.0f;
		if (dist < 140.0f && dz < 50.0f)
			UTIL_TraceHull(org, Vector(j.stand.x, j.stand.y, org.z), ignore_monsters, human_hull, bot->edict(), &tr);
		if (tr.flFraction > 0.9f)
			Steer(bot, j.stand); // the last steps straight
		else if (now >= j.nextMove)
		{
			j.nextMove = now + 1.5f;
			ai->MoveTo(&j.stand, FASTEST_ROUTE);
		}
		return;
	}
	if (j.phase == PH_THROUGH)
	{
		// through the hole it made: straight, the mesh knows nothing of it
		float dist = (j.way[j.wayAt] - org).Length2D();
		if (dist < 30.0f && ++j.wayAt >= j.wayNum)
		{
			Finish(bot, "through the wall");
			return;
		}
		if (j.wayAt == 0 && dist > 130.0f)
		{
			if (now >= j.nextMove)
			{
				j.nextMove = now + 1.5f;
				ai->MoveTo(&j.way[0], FASTEST_ROUTE);
			}
			return;
		}
		bool down = (bot->pev->flags & FL_ONGROUND) != 0;
		float feet = org.z + bot->pev->mins.z;
		if (j.climbing)
		{
			// out of the crater the way a player gets out of a hole: jump, and a block under the feet
			Hold(bot);
			Stand(bot);
			const int* c = j.cells[1];
			Vector mid = CellMid(c);
			LookAt(bot, Vector(mid.x, mid.y, mid.z - 60.0f));
			if (Blocks(bot) < 1 || !Takes(c))
				j.climbing = false;
			else if (!holding(SLOT_BLOCKS))
				;
			else if (feet >= mid.z + CELL * 0.5f + 1.0f)
			{
				if (BotPlaceBlock(bot, c[0], c[1], c[2]))
				{
					j.climbing = false;
					j.slowSince = now + 0.6f; // (first land on it)
					ai->EquipBestWeapon(true);
				}
			}
			else if (down && now >= j.nextAct)
			{
				Jump(bot);
				j.nextAct = now + 1.45f;
			}
			return;
		}
		Steer(bot, j.way[j.wayAt]);
		Stand(bot);
		if (bot->pev->velocity.Length2D() > 40.0f || !down)
			j.slowSince = max(j.slowSince, now);
		else if (now - j.slowSince > 0.35f && now >= j.nextAct)
		{
			// held up: a step it can jump, or the side of the crater, which takes a block under its feet
			Vector to = j.way[j.wayAt] - org;
			to.z = 0.0f;
			Vector ahead = org + to.Normalize() * 30.0f;
			float gz = feet;
			GroundBelow(Vector(ahead.x, ahead.y, feet), 140.0f, 20.0f, &gz);
			float top;
			if (gz - feet > 44.0f && Blocks(bot) > 0 && BaseCell(Vector(org.x, org.y, feet), j.cells[1], &top))
			{
				j.climbing = true;
				j.nextAct = now;
			}
			else
			{
				Jump(bot);
				j.nextAct = now + 1.45f;
			}
		}
		return;
	}
	if (j.phase == PH_RUN)
	{
		if (now >= j.blast && j.breach >= 0 && j.breach < (int)g_breach.size())
		{
			const Breach& b = g_breach[j.breach];
			if (FreeLine(b.out + Vector(0, 0, 40), b.in + Vector(0, 0, 40)) < 1.0f)
			{
				Finish(bot, "the wall held");
				return;
			}
			int needs = 0;
			if (!WalkThrough(b.out, b.in - b.dir * 40.0f, Blocks(bot), &needs))
			{
				McLog("tactics: %s looks at its hole: the crater takes %d blocks to climb and it has %d", STRING(bot->pev->netname), needs, Blocks(bot));
				Finish(bot, "the hole is no way through");
				return;
			}
			McLog("tactics: %s has a hole in the wall and goes through", STRING(bot->pev->netname));
			j.phase = PH_THROUGH;
			j.way[0] = b.out + Vector(0, 0, 36);
			j.way[1] = b.in - b.dir * 40.0f + Vector(0, 0, 36);
			j.wayNum = 2;
			j.wayAt = 0;
			j.nextMove = j.nextAct = 0.0f;
			j.slowSince = now;
			j.deadline = now + 12.0f;
			return;
		}
		if (now >= j.blast)
		{
			Finish(bot, "the TNT went off");
			return;
		}
		// back along its own trail, its feet moved from here: its AI may be busy with an enemy, and a bot
		// that stays to fight beside its own fuse dies of it
		while (j.wayAt < j.wayNum && (j.way[j.wayAt] - org).Length2D() < 36.0f)
			j.wayAt++;
		if (j.wayAt < j.wayNum)
		{
			Steer(bot, j.way[j.wayAt]);
			Stand(bot);
			return;
		}
		if ((org - CellMid(j.cells[0])).Length() >= 400.0f)
			return; // far enough: the rest is its own business
		if (now >= j.nextMove)
		{
			j.nextMove = now + 1.0f;
			ai->MoveTo(&j.flee, FASTEST_ROUTE);
		}
		return;
	}
	Hold(bot);
	if (j.kind == JOB_TRAP)
	{
		// cells[0]: the floor cell that comes out and takes the TNT; cells[1]: the cell over it, for the plate
		const int* hole = j.cells[0];
		const int* lid = j.cells[1];
		Vector mid = CellMid(lid);
		if ((mid - eye).Length() > mci::BLOCK_REACH - 20.0f)
		{
			stepTo(mid);
			return;
		}
		LookAt(bot, CellMid(hole));
		mcc::Classic* cw = ClassicWorld();
		if (j.at == 0)
		{
			if (cw && cw->Carved(hole[0], hole[1], hole[2]))
			{
				MineFrame(bot, false);
				j.at = 1;
				j.nextAct = now + 0.3f;
				return;
			}
			if (!holding(HasPick(bot) ? SLOT_PICK : SLOT_HAND))
				return;
			MineFrame(bot, true, hole, true);
			return;
		}
		if (now < j.nextAct)
			return;
		if (j.at == 1)
		{
			if (!holding(SLOT_TNT))
				return;
			if (BotPlaceBlock(bot, hole[0], hole[1], hole[2]))
			{
				j.at = 2;
				j.done++;
				j.tries = 0;
				j.nextAct = now + 0.35f;
			}
			else if (++j.tries > 12)
				Finish(bot, "cannot set the TNT in");
			else
				j.nextAct = now + 0.25f;
			return;
		}
		if (!holding(SLOT_HAND))
			return;
		if (BotPlacePlate(bot, lid[0], lid[1], lid[2]))
		{
			j.done++;
			Mine m;
			memcpy(m.lid, lid, sizeof(m.lid));
			m.pos = CellMid(lid) - Vector(0, 0, 18);
			m.team = bot->m_iTeam;
			m.seen = m.judged = 0;
			g_mines.push_back(m);
			Say(bot, OneOf({"Mine is live. Keep off the plate.", "Mined. Do not walk through here.", "It's armed. Stay clear of the door."}));
			Finish(bot, "laid");
		}
		else if (++j.tries > 12)
			Finish(bot, "cannot set the plate on");
		else
			j.nextAct = now + 0.25f;
		return;
	}
	if (j.kind == JOB_BUILD)
	{
		while (j.at < j.num && !Takes(j.cells[j.at]))
			j.at++; // already there (a mate's block, or the one just set)
		if (j.at >= j.num)
		{
			Finish(bot, "built");
			return;
		}
		// (a golem: the iron from where its blocks go, then the pumpkin from its own slot)
		int slot = (j.golem && j.at >= j.num - j.heads) ? SLOT_TNT : SLOT_BLOCKS;
		if (j.golem ? mp.hotbar[slot].Empty() : Blocks(bot) <= 0)
		{
			Finish(bot, "out of blocks");
			return;
		}
		const int* c = j.cells[j.at];
		Vector mid = CellMid(c);
		LookAt(bot, mid);
		if (!holding(slot) || now < j.nextAct)
			return;
		if (BotPlaceBlock(bot, c[0], c[1], c[2]))
		{
			j.at++;
			j.done++;
			j.tries = 0;
			j.nextAct = now + (j.urgent ? 0.15f : 0.3f);
			return;
		}
		j.nextAct = now + 0.25f;
		if ((mid - eye).Length() > mci::BLOCK_REACH)
			stepTo(mid);
		else if (++j.tries > (j.urgent ? 3 : 10))
		{
			j.at++; // somebody stands in it
			j.tries = 0;
		}
		return;
	}
	if (j.kind == JOB_PILLAR)
	{
		// the way a player goes up: jump, and set the block under the feet
		Vector base = CellMid(j.cells[0]);
		float off = (Vector(base.x, base.y, org.z) - org).Length2D();
		bool down = (bot->pev->flags & FL_ONGROUND) != 0;
		// (down beside the pillar: one block of it can still be jumped on, more cannot)
		float have = j.at > 0 ? CellMid(j.cells[j.at - 1]).z + CELL * 0.5f : -99999.0f;
		if (down && j.at > 0 && (off >= 19.0f || org.z + bot->pev->mins.z < have - 3.0f))
		{
			j.after = AFTER_NOTHING;
			Finish(bot, "fell off");
			return;
		}
		if (j.at >= j.num)
		{
			if (down)
				Finish(bot, "up");
			return;
		}
		if (Blocks(bot) <= 0)
		{
			Finish(bot, "out of blocks");
			return;
		}
		const int* c = j.cells[j.at];
		Vector mid = CellMid(c);
		LookAt(bot, Vector(mid.x, mid.y, mid.z - 60.0f));
		if (!holding(SLOT_BLOCKS))
			return;
		float feet = org.z + bot->pev->mins.z, top = mid.z + CELL * 0.5f;
		if (feet >= top + 1.0f)
		{
			if (BotPlaceBlock(bot, c[0], c[1], c[2]))
			{
				j.at++;
				j.done++;
				j.tries = 0;
			}
		}
		else if ((bot->pev->flags & FL_ONGROUND) && now >= j.nextAct)
		{
			// (Counter-Strike takes a quarter off a jump made within 1.3 s of the last: too low for a block)
			Jump(bot);
			j.nextAct = now + 1.45f;
			if (++j.tries > 5)
				Finish(bot, "cannot get up");
		}
		return;
	}
	if (j.kind == JOB_BRIDGE)
	{
		// a block out over the trench from the middle of the last one, a step onto it, the next
		if (j.at >= j.num)
		{
			g_ctl[bot->entindex()].hold = false;
			Steer(bot, j.way[0]);
			if ((j.way[0] - org).Length2D() < 20.0f)
				Finish(bot, "across");
			return;
		}
		if (Blocks(bot) <= 0)
		{
			Finish(bot, "out of blocks");
			return;
		}
		const int* c = j.cells[j.at];
		Vector mid = CellMid(c), from = j.at ? CellMid(j.cells[j.at - 1]) : j.stand;
		if ((Vector(from.x, from.y, org.z) - org).Length2D() > 4.0f)
		{
			g_ctl[bot->entindex()].hold = false;
			Steer(bot, from);
			return;
		}
		LookAt(bot, mid);
		if (!holding(SLOT_BLOCKS) || now < j.nextAct)
			return;
		j.nextAct = now + 0.25f;
		if (!Takes(c) || BotPlaceBlock(bot, c[0], c[1], c[2]))
		{
			j.at++;
			j.done++;
			j.tries = 0;
		}
		else if (++j.tries > 12)
			Finish(bot, "cannot lay it");
		return;
	}
	if (j.kind == JOB_MINE)
	{
		while (j.at < j.num && !Placed(j.cells[j.at]))
			j.at++;
		if (j.at >= j.num)
		{
			Finish(bot, "through");
			return;
		}
		int target[3];
		Vector mid = CellMid(j.cells[j.at]);
		int in = Blocker(eye, mid, target);
		if (in == 2)
		{
			if (++j.tries > 30)
				Finish(bot, "cannot get at it");
			return;
		}
		if (in == 0)
			memcpy(target, j.cells[j.at], sizeof(target));
		Vector tmid = CellMid(target);
		if ((tmid - eye).Length() > mci::BLOCK_REACH)
		{
			stepTo(tmid);
			return;
		}
		LookAt(bot, tmid);
		if (!holding(HasPick(bot) ? SLOT_PICK : SLOT_HAND))
			return;
		bool was = Placed(target);
		MineFrame(bot, true, target);
		if (was && !Placed(target))
			j.done++;
		return;
	}
	// TNT: set it down, light it, run
	const int* c = j.cells[0];
	Vector mid = CellMid(c);
	LookAt(bot, mid);
	if (MateInBlast(bot, mid))
	{
		if (now >= j.nextAct)
		{
			j.nextAct = now + 0.3f;
			if (++j.tries > 12)
				Finish(bot, "a teammate is too close");
		}
		return;
	}
	if (!j.tntDown)
	{
		if (!HasTnt(bot) || !HasFlint(bot))
		{
			Finish(bot, "nothing to blast with");
			return;
		}
		if (!holding(SLOT_TNT) || now < j.nextAct)
			return;
		j.nextAct = now + 0.3f;
		if (BotPlaceBlock(bot, c[0], c[1], c[2]))
			j.tntDown = true;
		else if (++j.tries > 8)
			Finish(bot, "no room for the TNT");
		return;
	}
	if (!holding(SLOT_FLINT) || now < j.nextAct)
		return;
	j.nextAct = now + 0.3f;
	if (BotLightTnt(bot, c[0], c[1], c[2]))
	{
		Say(bot, "Fire in the hole!");
		j.done++;
		j.phase = PH_RUN;
		j.blast = now + 4.3f;
		j.deadline = now + 6.0f;
		j.flee = FleeSpot(bot, mid);
		j.nextMove = 0.0f;
		const Track& t = g_track[bot->entindex()];
		Vector prev = org;
		float reach = (org - mid).Length();
		for (int k = 1; k <= t.crumbNum && j.wayNum < 16; k++)
		{
			const Vector& c = t.crumbs[(t.crumbAt - k + 32) % 16];
			if ((c - prev).Length() > 220.0f)
				break; // the trail is broken here (a respawn)
			prev = c;
			float d = (c - mid).Length();
			if (d < reach + 16.0f)
				continue; // not away from the TNT: it stood about there, or stepped back from the wall
			j.way[j.wayNum++] = c;
			reach = d;
			if (d >= 420.0f)
				break;
		}
		if (reach < 300.0f && j.wayNum < 16)
		{
			// no trail to follow that far: straight away from it, as far as the way is free
			Vector from = j.wayNum ? j.way[j.wayNum - 1] : org, away = from - mid;
			away.z = 0.0f;
			away = away.Normalize();
			float open = FreeLine(from, from + away * 420.0f) * 420.0f;
			if (open > 100.0f)
				j.way[j.wayNum++] = from + away * (open - 30.0f);
		}
		ai->EquipBestWeapon(true);
	}
	else if (++j.tries > 8)
		Finish(bot, "could not light it");
}

// At the position it built. false: it has left it.
static bool RunPost(CBasePlayer* bot, CCSBot* ai, bool fight, CBasePlayer* enemy)
{
	Post& p = g_post[bot->entindex()];
	Vector org = bot->pev->origin;
	Vector d = p.spot - org;
	float dz = fabsf(d.z);
	d.z = 0.0f;
	float dist = d.Length();
	const char* why = nullptr;
	if (gpGlobals->time > p.until)
		why = "long enough";
	else if (!g_plan.live)
		why = "the round is over";
	else if (bot->pev->health < 40.0f)
		why = "badly hurt";
	else if (bot->m_iTeam == CT && Bomb() && !p.cover)
		why = "the bomb is down";
	else if (fight && enemy && (enemy->pev->origin - org).Length() < 220.0f)
		why = "the enemy is on top of it";
	else if (dist > 140.0f || dz > (p.high ? 30.0f : 70.0f))
		why = "moved off it";
	if (why)
	{
		p.on = false;
		McLog("tactics: %s leaves its position (%s)", STRING(bot->pev->netname), why);
		if (!fight)
			ai->Idle();
		return false;
	}
	if (dist > 6.0f && !p.high)
		Steer(bot, p.spot);
	else
		Hold(bot);
	Stand(bot); // crouched, it would see nothing over its own cover
	if (!fight)
		LookAt(bot, p.watch);
	return true;
}

// The column of placed blocks a bot is pushing against (its cells bottom first); 0: none
static int BlockingColumn(CBasePlayer* bot, const Vector& wish, float feet, int out[3][3])
{
	Vector dir(wish.x, wish.y, 0);
	float len = dir.Length();
	if (len < 1.0f)
		return 0;
	dir = dir / len;
	static const float kTurn[5] = {0.0f, 30.0f, -30.0f, 60.0f, -60.0f};
	for (float turn : kTurn)
	{
		float a = turn * (float)M_PI / 180.0f;
		Vector d(dir.x * cosf(a) - dir.y * sinf(a), dir.x * sinf(a) + dir.y * cosf(a), 0);
		Vector p = bot->pev->origin + d * 38.0f;
		int n = 0, lastZ = -99999;
		static const float kUp[3] = {4.0f, 38.0f, 68.0f};
		for (float up : kUp)
		{
			int c[3];
			ToCell(Vector(p.x, p.y, feet + up), c);
			if (c[2] == lastZ || !Wallish(c))
				continue;
			lastZ = c[2];
			memcpy(out[n++], c, sizeof(c));
		}
		if (n)
			return n;
	}
	return 0;
}

// A place for TNT against an enemy last seen at a spot: on the way there, close to it, out of its sight
static bool FlushSpot(CBasePlayer* bot, const Vector& enemy, Vector* stand, int cell[3])
{
	MC_WHERE("tactics: a place for TNT by an enemy's cover");
	Vector org = bot->pev->origin;
	CNavArea* start = TheNavAreaGrid.GetNearestNavArea(&org);
	CNavArea* goal = TheNavAreaGrid.GetNearestNavArea(&enemy);
	std::vector<Entrance> none;
	std::vector<Vector> pts;
	if (!Route(start, goal, enemy, none, nullptr, &pts, nullptr))
		return false;
	Vector head = enemy + Vector(0, 0, 20);
	for (const Vector& p : pts) // from the enemy's end
	{
		float gz;
		if (!GroundBelow(p, 40.0f, 80.0f, &gz))
			continue;
		Vector at(p.x, p.y, gz + 36.0f);
		float d = (at - enemy).Length();
		if (d < 90.0f)
			continue;
		if (d > 280.0f)
			break;
		if (FreeLine(at + Vector(0, 0, 17), head) >= 1.0f)
			continue; // it would be shot while lighting the fuse
		// the TNT goes a block towards the enemy, or beside the bot
		Vector to = enemy - at;
		to.z = 0.0f;
		to = to.Normalize();
		const Vector tries[3] = {to, Vector(-to.y, to.x, 0), Vector(to.y, -to.x, 0)};
		for (const Vector& t : tries)
		{
			Vector q = at + t * 56.0f;
			float qz;
			if (!GroundBelow(q, 30.0f, 60.0f, &qz))
				continue;
			ToCell(Vector(q.x, q.y, qz + 12.0f), cell);
			if (!Takes(cell))
			{
				cell[2]++;
				if (!Takes(cell))
					continue;
			}
			if (FreeLine(at + Vector(0, 0, 17), CellMid(cell)) < 1.0f)
				continue;
			*stand = at;
			return true;
		}
	}
	return false;
}

// The lowest cell over a point on the ground that a block can go in (the cell the floor is in, or the one
// above when the floor fills most of that); how far its top is above the floor comes back too.
static bool BaseCell(const Vector& ground, int out[3], float* top)
{
	ToCell(ground + Vector(0, 0, 2), out);
	if (!Takes(out))
	{
		out[2]++;
		if (!Takes(out))
			return false;
	}
	*top = g_world.origin[2] + (out[2] + 1) * CELL - ground.z;
	return true;
}

// A trench across a bot's way: the cells that carry it over, level with the ground it stands on, from the
// last firm cell to the first firm one beyond. Only a trench (the ground comes back within a few cells),
// and only one its blocks reach across.
static bool PlanBridge(CBasePlayer* bot, const Vector& dir, Job& j)
{
	MC_WHERE("tactics: planning a bridge");
	Vector org = bot->pev->origin;
	float feet = org.z + bot->pev->mins.z;
	int ax = fabsf(dir.x) >= fabsf(dir.y) ? 0 : 1, step = dir[ax] > 0.0f ? 1 : -1;
	int c[3];
	ToCell(Vector(org.x, org.y, feet + 1.0f), c);
	// the layer of cells whose tops lie at the height of the ground (a step of 18 up at most, 22 down)
	c[2] = (int)floorf((feet + 18.0f - g_world.origin[2]) / CELL) - 1;
	auto cellAt = [&](int i, int out[3]) {
		memcpy(out, c, sizeof(int) * 3);
		out[ax] += step * i;
	};
	int first = -1, last = -1;
	for (int i = 0; i <= 8 && last < 0; i++)
	{
		int cc[3];
		cellAt(i, cc);
		Vector m = CellMid(cc);
		float gz = 0.0f;
		bool ss = false;
		Vector top(m.x, m.y, feet + 30.0f);
		float f = FreeLine(top, top - Vector(0, 0, 290), &ss);
		if (ss)
			return false; // something stands there
		gz = feet + 30.0f - f * 290.0f;
		bool firm = f < 1.0f && fabsf(gz - feet) <= 24.0f;
		if (first < 0)
		{
			if (firm)
				continue;
			if (i == 0 || (f < 1.0f && feet - gz < 56.0f))
				return false; // it stands over it already, or it is only a dip
			first = i;
		}
		else if (firm)
			last = i - 1;
	}
	if (first < 0 || last < 0)
		return false; // no trench, or no other side to it
	int n = last - first + 1;
	if (n > MAX_JOB_CELLS || n > Blocks(bot))
		return false;
	j = Job();
	j.kind = JOB_BRIDGE;
	for (int i = 0; i < n; i++)
	{
		cellAt(first + i, j.cells[i]);
		if (!Takes(j.cells[i]))
			return false;
	}
	j.num = n;
	int edge[3], over[3];
	cellAt(first - 1, edge);
	cellAt(last + 1, over);
	Vector e = CellMid(edge), o = CellMid(over);
	j.stand = Vector(e.x, e.y, org.z);
	j.arrive = 4.0f;
	j.way[0] = Vector(o.x, o.y, org.z);
	j.deadline = gpGlobals->time + 6.0f + n * 1.5f;
	return true;
}

// Caught in the open by somebody far off: two blocks, one on the other, between the two of them.
static bool QuickCover(CBasePlayer* bot, CBasePlayer* enemy)
{
	Vector org = bot->pev->origin, feet = org + Vector(0, 0, bot->pev->mins.z);
	Vector to = enemy->pev->origin - org;
	int ax = fabsf(to.x) >= fabsf(to.y) ? 0 : 1, step = to[ax] > 0.0f ? 1 : -1;
	int b[3];
	float top;
	if (!BaseCell(feet, b, &top) || top > 62.0f)
		return false;
	// the next cell that way, or the one after it when the bot's own body reaches into the next
	float edge = g_world.origin[ax] + (b[ax] + (step > 0 ? 1 : 0)) * CELL;
	Job j;
	j.kind = JOB_BUILD;
	j.phase = PH_WORK;
	j.urgent = true;
	j.stand = org;
	j.num = 2;
	for (int k = 0; k < 2; k++)
	{
		memcpy(j.cells[k], b, sizeof(b));
		j.cells[k][ax] += step * (fabsf(edge - org[ax]) < 18.0f ? 2 : 1);
		j.cells[k][2] += k;
		if (!Takes(j.cells[k]))
			return false;
	}
	j.deadline = gpGlobals->time + 2.5f;
	StartJob(bot, j, "throws up cover under fire");
	Say(bot, OneOf({"Taking fire! Blocks up.", "I'm shot at. Throwing up cover.", "Under fire here!"}));
	return true;
}

static bool Mood(int team, int tactic);
static bool Dares(const Track& t, float worth);
static bool PlanBridge(CBasePlayer* bot, const Vector& dir, Job& j);
static bool WitHold(CBasePlayer* bot, CCSBot* ai, Track& t);
static bool WitWall(CBasePlayer* bot, CCSBot* ai, Track& t);
static bool WitShield(CBasePlayer* bot, CCSBot* ai, Track& t);
static bool WitBreach(CBasePlayer* bot, CCSBot* ai, Track& t);
static bool WitDeny(CBasePlayer* bot, CCSBot* ai, Track& t);
static bool WitMine(CBasePlayer* bot, CCSBot* ai, Track& t);
static bool WitGolem(CBasePlayer* bot, CCSBot* ai, Track& t);
static bool WalkThrough(const Vector& a, const Vector& b, int blocks, int* needs);

// Somebody mining a placed block: heard by the other side
struct Dig
{
	int team;
	Vector at;
	float time;
};
static std::vector<Dig> g_digs;
static int g_wallsMet[4]; // how often a side's bots ran into a wall this round
static int g_woodMet[4];  // ... a wall of wood
static int g_pickDigs[4]; // a side dug at somebody's blocks with pickaxes this round

void BotTacticsThink(CBasePlayer* bot)
{
	MC_WHERE("tactics: a bot's think");
	int i = bot->entindex();
	Job& j = g_job[i];
	Track& t = g_track[i];
	if (!bot->IsAlive() || cv_tactics.value == 0.0f || !g_worldLoaded || i == g_standIn)
	{
		j = Job();
		g_post[i].on = false;
		return;
	}
	float now = gpGlobals->time;
	Vector org = bot->pev->origin;
	CCSBot* ai = static_cast<CCSBot*>(bot);
	if (now >= t.nextCrumb || t.nextCrumb > now + 5.0f)
	{
		t.nextCrumb = now + 0.5f;
		if (bot->pev->flags & FL_ONGROUND)
		{
			t.crumbs[t.crumbAt] = org;
			t.crumbAt = (t.crumbAt + 1) % 16;
			if (t.crumbNum < 16)
				t.crumbNum++;
		}
	}
	if ((org - t.anchor).Length2D() > 14.0f || t.anchorTime > now)
	{
		t.anchor = org;
		t.anchorTime = now;
	}
	if (bot->pev->health < t.hp - 0.5f)
		t.hurtTime = now;
	t.hp = bot->pev->health;
	if (CSGameRules()->IsFreezePeriod())
	{
		t.anchorTime = now;
		return;
	}
	CBasePlayer* enemy = ai->GetEnemy();
	bool fight = enemy && enemy->IsAlive() && ai->IsEnemyVisible();
	if (j.kind != JOB_NONE || g_post[i].on || fight)
	{
		t.holdSince = now; // (holding a spot is counted from when things went quiet)
		t.holdJudged = false;
	}
	if (j.kind != JOB_NONE)
	{
		t.anchorTime = now; // at work is not held up
		bool hands = !j.urgent && (j.phase == PH_GO || j.phase == PH_WORK) && j.kind != JOB_TNT;
		if (now > j.deadline)
			Finish(bot, "out of time");
		else if (hands && now - t.hurtTime < 0.25f)
			Finish(bot, "shot at with stone in its hands: drops it and fights"); // (the risk of building)
		else if (fight && !j.urgent && j.phase != PH_RUN && j.phase != PH_THROUGH)
		{
			// the fight first
			if (j.kind == JOB_MINE || j.kind == JOB_TRAP)
				MineFrame(bot, false);
			if (!j.gunOut)
			{
				j.gunOut = true;
				ai->EquipBestWeapon(true);
			}
		}
		else
		{
			j.gunOut = false;
			RunJob(bot, ai, j);
		}
		return;
	}
	if (g_post[i].on && RunPost(bot, ai, fight, enemy))
	{
		t.anchorTime = now;
		return;
	}
	if (fight)
	{
		// shot at from far off with nothing to stand behind
		if (t.builder && Blocks(bot) >= 2 && now - t.hurtTime < 0.5f && (bot->pev->flags & FL_ONGROUND) && (now >= t.nextCover || t.nextCover > now + 30.0f) &&
			(enemy->pev->origin - org).Length() > 450.0f)
		{
			t.nextCover = now + 8.0f;
			if (Mood(bot->m_iTeam, TAC_FIRE) && Dares(t, 0.8f))
				QuickCover(bot, enemy);
		}
		return;
	}

	// held up by blocks somebody placed: around, through, or blow them up
	const Vector& wish = g_ctl[i].wish;
	if (bot->pev->flags & FL_ONGROUND)
		t.groundZ = org.z + bot->pev->mins.z;
	if (ChangedCells() && (now >= t.nextProbe || t.nextProbe > now + 5.0f))
	{
		t.nextProbe = now + 0.15f;
		int col[3][3];
		int n = wish.Length2D() > 60.0f ? BlockingColumn(bot, wish, t.groundZ, col) : 0;
		if (n > 0)
		{
			// (a bot's own unsticking jumps and sidesteps: held up is pushing at the blocks for a while
			// without getting away from the place)
			if (t.blockedSince <= 0.0f || (org - t.blockedAt).Length2D() > 50.0f)
			{
				t.blockedSince = now;
				t.blockedAt = org;
			}
			t.blockedSeen = now;
		}
		else if (now - t.blockedSeen > 0.5f)
			t.blockedSince = 0.0f;
		float held = n > 0 ? now - t.blockedSince : 0.0f;
		if (held > 0.8f)
		{
			// a wall across a way in, first met: is there another way that is not much longer?
			bool wall = false, own = false;
			for (const std::vector<Entrance>* list : {&g_ent, &g_dyn})
				for (const Entrance& e : *list)
					if (e.walled && (e.pos - org).Length() < 170.0f)
					{
						wall = true;
						own = own || e.team == bot->m_iTeam;
					}
			if (own && !bot->m_bHasC4 && !(bot->m_iTeam == CT && Bomb()))
			{
				// its own side's wall: it stands, and nobody of that side takes it down without need
				if (now - t.sentAround > 6.0f)
				{
					t.sentAround = now;
					McLog("tactics: %s is at its own side's wall and turns back", STRING(bot->pev->netname));
					ai->Idle();
				}
				t.blockedSince = 0.0f;
				return;
			}
			if (wall && now - t.sentAround > 15.0f && bot->m_iTeam >= 0 && bot->m_iTeam < 4)
			{
				g_wallsMet[bot->m_iTeam]++; // (next round its side thinks of tools: pickaxes for stone, axes for wood)
				mcw::Cell met = g_world.Get(col[0][0], col[0][1], col[0][2]);
				if (met && mcw::Block(mcw::CellType(met)).tool == mcw::TOOL_AXE)
					g_woodMet[bot->m_iTeam]++;
			}
			if (wall && now - t.sentAround > 15.0f && !g_testNoWayAround)
			{
				t.sentAround = now;
				t.blockedSince = 0.0f;
				McLog("tactics: %s meets a wall and plans its route again", STRING(bot->pev->netname));
				Say(bot, OneOf({"Walled off. Going around.", "They've shut this way. Finding another.", "Wall here. Taking the long way."}));
				ai->Idle();
				return;
			}
			int around = 0;
			for (int dx = -1; dx <= 1; dx++)
				for (int dy = -1; dy <= 1; dy++)
					for (int dz = -1; dz <= 1; dz++)
					{
						int c[3] = {col[0][0] + dx, col[0][1] + dy, col[0][2] + dz};
						if (Wallish(c))
							around++;
					}
			// a pillar or a mate's cover is walked around; only what really shuts the way is broken at once
			if (around < 4 && held < 2.5f)
				return;
			t.blockedSince = 0.0f;
			Job nj;
			nj.stand = org;
			if (HasTnt(bot) && HasFlint(bot) && around >= 4 && Mood(bot->m_iTeam, TAC_TNT))
			{
				// blow it up: the TNT where the bot stands now, lit from a step back
				Vector dir = Vector(wish.x, wish.y, 0).Normalize();
				nj.kind = JOB_TNT;
				nj.num = 1;
				ToCell(Vector(org.x, org.y, org.z + bot->pev->mins.z + 14.0f), nj.cells[0]);
				if (!Takes(nj.cells[0]))
					nj.cells[0][2]++;
				// a step back, out of the cell the TNT goes in: straight back, or back and to a side
				nj.deadline = now + 12.0f;
				Vector mid = CellMid(nj.cells[0]);
				bool room = false;
				static const float kTurn[3] = {0.0f, 40.0f, -40.0f};
				for (int k = 0; k < 3 && !room && Takes(nj.cells[0]); k++)
				{
					float a = kTurn[k] * (float)M_PI / 180.0f;
					Vector back(-(dir.x * cosf(a) - dir.y * sinf(a)), -(dir.x * sinf(a) + dir.y * cosf(a)), 0);
					nj.stand = Vector(mid.x, mid.y, org.z) + back * 78.0f;
					float gz;
					room = FreeLine(org, nj.stand) >= 1.0f && GroundBelow(nj.stand, 20.0f, 70.0f, &gz);
				}
				if (room)
				{
					StartJob(bot, nj, "held up by a wall of blocks with TNT in its pack: blasts through");
					Say(bot, OneOf({"This wall is coming down. Stand back.", "TNT on the wall. Get clear.", "I'll open it. Back off."}));
					return;
				}
				McLog("tactics: %s has TNT for the wall but no room to set it down", STRING(bot->pev->netname));
			}
			// one digs, and whoever else comes up covers it: a bot at a wall has its hands full
			Vector wallAt = CellMid(col[0]);
			CBasePlayer* digger = nullptr;
			for (int k = 1; k <= gpGlobals->maxClients; k++)
			{
				CBasePlayer* m = UTIL_PlayerByIndex(k);
				if (m && m != bot && m->IsAlive() && m->m_iTeam == bot->m_iTeam && g_job[k].kind == JOB_MINE && (m->pev->origin - org).Length() < 260.0f)
					digger = m;
			}
			if (digger && around >= 4)
			{
				Vector back = org - Vector(wish.x, wish.y, 0).Normalize() * 96.0f;
				float bz;
				if (GroundBelow(back, 30.0f, 60.0f, &bz) && FreeLine(org, Vector(back.x, back.y, bz + 36.0f)) >= 1.0f)
				{
					Post& p = g_post[i];
					p.on = true;
					p.high = false;
					p.cover = true;
					p.spot = Vector(back.x, back.y, bz + 36.0f);
					p.watch = wallAt;
					p.until = now + 6.0f;
					McLog("tactics: %s covers %s, who is digging through the wall", STRING(bot->pev->netname), STRING(digger->pev->netname));
					Say(bot, OneOf({"Dig. I've got you.", "Covering the digger.", "Keep at it, I'm watching."}));
					return;
				}
			}
			nj = Job();
			nj.kind = JOB_MINE;
			nj.phase = PH_WORK;
			nj.stand = org;
			nj.num = n;
			memcpy(nj.cells, col, sizeof(int) * 3 * n);
			nj.deadline = now + 40.0f;
			StartJob(bot, nj, HasPick(bot) ? "held up by blocks: digs through with the tool it has" : "held up by blocks, no tool for it: digs through by hand");
			Say(bot, HasPick(bot) ? OneOf({"Digging through. Cover me.", "Breaking this wall down.", "Give me a second with this wall."})
								  : OneOf({"No tool for this wall. It will take a while.", "Digging by hand. Cover me.", "This wall will take me some time."}));
			return;
		}
	}

	// the ground itself in its way: a trench across it, or the hole it stands in
	if (Blocks(bot) > 0 && wish.Length2D() > 60.0f && (bot->pev->flags & FL_ONGROUND) && (now >= t.nextGround || t.nextGround > now + 5.0f))
	{
		t.nextGround = now + 0.12f;
		Vector dir = Vector(wish.x, wish.y, 0).Normalize();
		float feet = org.z + bot->pev->mins.z, gz = feet;
		Job nj;
		bool has = GroundBelow(Vector(org.x, org.y, feet) + dir * 46.0f, 30.0f, 220.0f, &gz);
		if ((!has || feet - gz > 56.0f) && PlanBridge(bot, dir, nj))
		{
			// across it, or down into it and up the other side: whichever takes fewer blocks (they are dear)
			int climb = 99;
			WalkThrough(Vector(org.x, org.y, feet), Vector(nj.way[0].x, nj.way[0].y, feet), 99, &climb);
			if (nj.num <= climb)
			{
				char what[128];
				Q_snprintf(what, sizeof(what), "a trench across its way (a climb through it would take %d blocks): bridges it with %d", climb, nj.num);
				StartJob(bot, nj, what);
				return;
			}
			nj = Job();
		}
		if (now - t.anchorTime > 0.6f)
		{
			// not getting on: what is in front of its feet?
			bool ss = false;
			Vector up = Vector(org.x, org.y, feet + 150.0f) + dir * 30.0f;
			float f = FreeLine(up, up - Vector(0, 0, 170), &ss);
			float rise = ss ? 999.0f : 150.0f - f * 170.0f;
			float top;
			if (rise > 44.0f && rise <= 150.0f && BaseCell(Vector(org.x, org.y, feet), nj.cells[0], &top) && top <= 41.0f)
			{
				// the side of a hole, too high to jump: up on a block of its own
				nj.kind = JOB_PILLAR;
				nj.phase = PH_WORK;
				nj.num = 1;
				nj.stand = org;
				nj.deadline = now + 6.0f;
				StartJob(bot, nj, "in a hole it cannot jump out of: a block under its feet");
				return;
			}
			if (rise > 18.0f && rise <= 44.0f)
			{
				Jump(bot);
				t.nextGround = now + 1.45f;
			}
		}
	}

	// a mine of the other side's ahead: a bot has one look at it as it comes near. A skilled one sees the
	// plate more often than not, and goes round; the rest walk on.
	for (Mine& m : g_mines)
	{
		unsigned bit = 1u << (i & 31);
		if (m.team == bot->m_iTeam || (m.judged & bit) || (m.pos - org).Length() > 240.0f)
			continue;
		if (FreeLine(org + bot->pev->view_ofs, m.pos + Vector(0, 0, 6)) < 1.0f)
			continue;
		m.judged |= bit;
		float skill = ai->GetProfile() ? ai->GetProfile()->GetSkill() : 0.5f;
		bool sees = RANDOM_FLOAT(0.0f, 1.0f) < 0.15f + 0.6f * skill;
		McLog("tactics: %s comes up to a mine at %s and %s the plate (skill %.2f)", STRING(bot->pev->netname), PlaceName(m.pos), sees ? "sees" : "does not see",
			skill);
		if (sees)
		{
			m.seen |= bit;
			Say(bot, OneOf({"Mine! Going around.", "There's a plate on the floor here. Not this way.", "Trap at the door. Finding another way."}));
			ai->Idle();
			return;
		}
	}

	// somebody is digging at a wall of its side, in its sight: it waits for the block to break
	if (now >= t.nextWatch || t.nextWatch > now + 5.0f)
		for (const Dig& d : g_digs)
		{
			Vector eye = org + bot->pev->view_ofs;
			float far2 = (d.at - eye).Length();
			if (d.team == bot->m_iTeam || now - d.time > 0.6f || far2 > 700.0f || far2 < 60.0f || FreeLine(eye, d.at) * far2 < far2 - 36.0f)
				continue; // (not its side's wall, or not in its sight)
			LookAt(bot, d.at);
			Hold(bot);
			Stand(bot);
			if (now - t.sentAround > 0.0f && now >= t.nextWatch + 4.0f)
				McLog("tactics: %s hears the enemy digging at a wall and waits for the block to break", STRING(bot->pev->netname));
				Say(bot, OneOf({"They're digging at the wall!", "Someone is at the wall. I'm waiting for them.", "I hear digging."}));
			t.nextWatch = now; // (every frame while it lasts)
			return;
		}

	// TNT for an enemy behind cover
	if (HasTnt(bot) && HasFlint(bot) && g_plan.live && (now >= t.nextFlush || t.nextFlush > now + 5.0f))
	{
		t.nextFlush = now + 1.0f;
		float seen = ai->GetTimeSinceLastSawEnemy();
		if (seen > 1.2f && seen < 7.0f && !bot->m_bHasC4 && !ai->IsDefusingBomb())
		{
			Vector last = ai->GetLastKnownEnemyPosition();
			float d = (last - org).Length();
			Job nj;
			if (d > 260.0f && d < 1000.0f && FlushSpot(bot, last, &nj.stand, nj.cells[0]))
			{
				t.nextFlush = now + 12.0f; // (it has made up its mind about this one)
				if (Mood(bot->m_iTeam, TAC_TNT) && Dares(t, 0.75f))
				{
					nj.kind = JOB_TNT;
					nj.num = 1;
					nj.deadline = now + 10.0f;
					StartJob(bot, nj, "the enemy is behind cover it can get close to: takes its TNT there");
					return;
				}
			}
		}
	}

	// what else the moment offers
	if (!g_plan.live)
		return;
	if (WitShield(bot, ai, t))
		return;
	if (now >= t.nextWit || t.nextWit > now + 5.0f)
	{
		t.nextWit = now + RANDOM_FLOAT(0.4f, 0.8f);
		if (!WitGolem(bot, ai, t) && !WitBreach(bot, ai, t) && !WitWall(bot, ai, t) && !WitMine(bot, ai, t) && !WitDeny(bot, ai, t))
			WitHold(bot, ai, t);
	}
}

// ---------------------------------------------------------------------------------------------
// Positions: where a bot sets up, and what it builds there

struct PostPlan
{
	int cells[6][3];
	int num = 0;
	Vector work; // where it stands to build
	Vector post; // where it stands afterwards
	float eye = 0; // the height it will look out from
};

// A firing position on a spot, facing a point: two pillars a cell apart to shoot between, and a block to
// stand behind in the gap where the floor lets that be low enough to see over.
static bool PlanSlit(const Vector& ground, const Vector& watch, PostPlan& out)
{
	int b[3];
	float top;
	if (!BaseCell(ground, b, &top) || top < 18.0f || top > 62.0f)
		return false;
	Vector to = watch - ground;
	int ax = fabsf(to.x) >= fabsf(to.y) ? 0 : 1, step = to[ax] > 0.0f ? 1 : -1, side = 1 - ax;
	Vector mid = CellMid(b);
	float gz;
	if (!GroundBelow(Vector(mid.x, mid.y, ground.z), 30.0f, 40.0f, &gz) || fabsf(gz - ground.z) > 12.0f)
		return false;
	// the cell it will stand in, the gap in front of it and the cell it builds from must all be free
	int up[3] = {b[0], b[1], b[2] + 1}, gap[3] = {b[0], b[1], b[2]}, gapUp[3], back[3] = {b[0], b[1], b[2]};
	gap[ax] += step;
	back[ax] -= step;
	memcpy(gapUp, gap, sizeof(gap));
	gapUp[2]++;
	if (!Takes(b) || !Takes(up) || !Takes(gap) || !Takes(gapUp) || !Takes(back))
		return false;
	Vector work = CellMid(back);
	if (!GroundBelow(Vector(work.x, work.y, ground.z), 30.0f, 40.0f, &gz) || fabsf(gz - ground.z) > 12.0f)
		return false;
	out.num = 0;
	for (int h = 0; h < 2; h++)
		for (int s = -1; s <= 1; s += 2)
		{
			int c[3] = {gap[0], gap[1], gap[2] + h};
			c[side] += s;
			if (!Takes(c))
				return false;
			Vector cm = CellMid(c);
			if (h == 0 && (!GroundBelow(Vector(cm.x, cm.y, ground.z), 30.0f, 40.0f, &gz) || fabsf(gz - ground.z) > 14.0f))
				return false; // a step or a slope: the pillars would not stand level
			memcpy(out.cells[out.num++], c, sizeof(c));
		}
	if (top <= 44.0f)
		memcpy(out.cells[out.num++], gap, sizeof(gap)); // low enough to see and shoot over
	out.work = Vector(work.x, work.y, ground.z + 36.0f);
	out.post = Vector(mid.x, mid.y, ground.z + 36.0f);
	out.eye = ground.z + 53.0f;
	return FreeLine(out.work, out.post) >= 1.0f;
}

// The high ground on a spot: two blocks under its own feet.
static bool PlanPerch(const Vector& ground, PostPlan& out)
{
	int b[3];
	float top;
	if (!BaseCell(ground, b, &top) || top > 41.0f)
		return false; // the first block has to go in under a jump
	int up[3] = {b[0], b[1], b[2] + 1};
	Vector mid = CellMid(b);
	float gz;
	if (!Takes(up) || !GroundBelow(Vector(mid.x, mid.y, ground.z), 30.0f, 40.0f, &gz) || fabsf(gz - ground.z) > 12.0f)
		return false;
	if (FreeLine(Vector(mid.x, mid.y, ground.z + 10.0f), Vector(mid.x, mid.y, ground.z + top + 40.0f + 80.0f)) < 1.0f)
		return false; // no head room
	memcpy(out.cells[0], b, sizeof(b));
	memcpy(out.cells[1], up, sizeof(up));
	out.num = 2;
	out.work = out.post = Vector(mid.x, mid.y, ground.z + 36.0f);
	out.eye = ground.z + top + 40.0f + 53.0f;
	return true;
}

// A smaller piece of cover, three blocks: one to stand behind and a pillar two high beside it, on one flank.
static bool PlanHalf(const Vector& ground, const Vector& watch, PostPlan& out)
{
	int b[3];
	float top;
	if (!BaseCell(ground, b, &top) || top < 18.0f || top > 44.0f)
		return false; // the block in front has to be low enough to see over
	Vector to = watch - ground;
	int ax = fabsf(to.x) >= fabsf(to.y) ? 0 : 1, step = to[ax] > 0.0f ? 1 : -1, side = 1 - ax;
	int up[3] = {b[0], b[1], b[2] + 1}, front[3] = {b[0], b[1], b[2]}, back[3] = {b[0], b[1], b[2]};
	front[ax] += step;
	back[ax] -= step;
	Vector mid = CellMid(b), work = CellMid(back);
	float gz;
	if (!Takes(b) || !Takes(up) || !Takes(front) || !Takes(back))
		return false;
	if (!GroundBelow(Vector(mid.x, mid.y, ground.z), 30.0f, 40.0f, &gz) || fabsf(gz - ground.z) > 12.0f)
		return false;
	if (!GroundBelow(Vector(work.x, work.y, ground.z), 30.0f, 40.0f, &gz) || fabsf(gz - ground.z) > 12.0f)
		return false;
	int first = RANDOM_LONG(0, 1) ? 1 : -1;
	for (int n = 0; n < 2; n++)
	{
		int low[3] = {front[0], front[1], front[2]}, high[3];
		low[side] += n ? -first : first;
		memcpy(high, low, sizeof(low));
		high[2]++;
		Vector cm = CellMid(low);
		if (!Takes(low) || !Takes(high) || !GroundBelow(Vector(cm.x, cm.y, ground.z), 30.0f, 40.0f, &gz) || fabsf(gz - ground.z) > 14.0f)
			continue;
		memcpy(out.cells[0], front, sizeof(front));
		memcpy(out.cells[1], low, sizeof(low));
		memcpy(out.cells[2], high, sizeof(high));
		out.num = 3;
		out.work = Vector(work.x, work.y, ground.z + 36.0f);
		out.post = Vector(mid.x, mid.y, ground.z + 36.0f);
		out.eye = ground.z + 53.0f;
		return FreeLine(out.work, out.post) >= 1.0f;
	}
	return false;
}

// The best spot around a point to set up and watch another from. On the ground it has to see what it
// watches; a perch is a spot that does not, until the bot stands two blocks higher.
static bool FindPost(bool high, const Vector& anchor, float rMin, float rMax, const Entrance* inside, const Vector& watch, const Vector* keep,
	CBasePlayer* self, PostPlan& best)
{
	MC_WHERE("tactics: looking for a spot to set up");
	float bestScore = -1e9f;
	int looked = 0;
	double t0 = Millis();
	struct Timed
	{
		double t0;
		int* looked;
		bool high;
		~Timed() { McLog("tactics: looked at %d spots for %s in %.1f ms", *looked, high ? "high ground" : "a firing position", Millis() - t0); }
	} timed{t0, &looked, high};
	for (CNavArea* a : TheNavAreaList)
	{
		const Extent* x = a->GetExtent();
		if (x->lo.x > anchor.x + rMax || x->hi.x < anchor.x - rMax || x->lo.y > anchor.y + rMax || x->hi.y < anchor.y - rMax)
			continue;
		if (a->GetAttributes() & (NAV_JUMP | NAV_CROUCH))
			continue;
		// a few points of the area: its middle, and for a big one the middles of its quarters
		int n = (x->SizeX() > 150.0f && x->SizeY() > 150.0f) ? 5 : 1;
		for (int k = 0; k < n && looked < 400; k++)
		{
			Vector p = *a->GetCenter();
			if (k)
			{
				p.x += ((k - 1) & 1 ? 0.25f : -0.25f) * x->SizeX();
				p.y += ((k - 1) & 2 ? 0.25f : -0.25f) * x->SizeY();
			}
			p.z = a->GetZ(&p);
			float d = (p - anchor).Length();
			if (d < rMin || d > rMax || fabsf(p.z - anchor.z) > 120.0f)
				continue;
			if (inside)
			{
				// on the site's side of the way in
				Vector in(0, 0, 0);
				in[1 - inside->axis] = inside->toSite;
				if (DotProduct(p - inside->pos, in) < 150.0f)
					continue;
			}
			float gz;
			if (!GroundBelow(p, 30.0f, 50.0f, &gz))
				continue;
			p.z = gz;
			looked++;
			// not where somebody stands or has set up already
			bool taken = false;
			for (int i = 1; i <= gpGlobals->maxClients && !taken; i++)
			{
				CBasePlayer* o = UTIL_PlayerByIndex(i);
				if (o && o != self && o->IsAlive() && ((o->pev->origin - p).Length2D() < 90.0f || (g_post[i].on && (g_post[i].spot - p).Length2D() < 160.0f)))
					taken = true;
			}
			if (taken)
				continue;
			// the cheap test first: from the ground a firing position has to see what it watches, and a
			// perch must not (else there is nothing there to look over)
			bool sees = FreeLine(Vector(p.x, p.y, p.z + 53.0f), watch) >= 1.0f;
			if (sees == high)
				continue;
			PostPlan plan;
			if (high ? !PlanPerch(p, plan) : !PlanSlit(p, watch, plan))
				continue;
			Vector eye(plan.post.x, plan.post.y, plan.eye);
			if (FreeLine(eye, watch) < 1.0f)
				continue;
			if (high && FreeLine(Vector(eye.x, eye.y, p.z + 53.0f), watch) >= 1.0f)
				continue; // nothing to look over here: the ground would do
			float wd = (p - watch).Length();
			float score = -fabsf(wd - 420.0f);
			if (keep)
				score += FreeLine(eye, *keep) >= 1.0f ? 300.0f : 0.0f; // the bomb in view too
			if (score > bestScore)
			{
				bestScore = score;
				best = plan;
			}
		}
	}
	return bestScore > -1e8f;
}

// ---------------------------------------------------------------------------------------------
// Judgement: what a bot with blocks or TNT makes of the moment it is in
//
// Nothing here happens on a cue. Each of these is a bot noticing that its situation offers something, and
// deciding whether to take it: how much the moment asks for it (worth), against the bot's nerve this round
// and a throw of the dice (Dares), and the side's mood for that kind of thing this round (Mood: what has
// been winning rounds sets it). The same situation does not always get the same answer, and a side builds
// only a few things a round.

// The chance of each way in being the one the other side takes: its humans' (what has been learned of them),
// or with no human there its bots' shortest.
static int EntranceOdds(int role, int site, float sum[MAX_ENT])
{
	int enemyTeam = role == ROLE_ATTACK ? TERRORIST : CT, humans = 0;
	for (int e = 0; e < MAX_ENT; e++)
		sum[e] = 0.0f;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* h = UTIL_PlayerByIndex(i);
		if (!IsHuman(h) || !h->IsAlive() || h->m_iTeam != enemyTeam)
			continue;
		float p[MAX_ENT];
		RouteProbs(role, g_track[i].last[role], site, p);
		for (int e = 0; e < MAX_ENT; e++)
			sum[e] += p[e];
		humans++;
	}
	if (!humans)
		RouteProbs(role, -1, site, sum);
	return humans;
}

static const char* OneOf(std::initializer_list<const char*> lines) { return lines.begin()[RANDOM_LONG(0, (int)lines.size() - 1)]; }

// A side does not build all the time: a few things a round, with breathing space between them.
static int g_teamBuilt[4];
static float g_teamNext[4];
static bool TeamMay(int team) { return team >= 0 && team < 4 && g_teamBuilt[team] < 3 && gpGlobals->time >= g_teamNext[team]; }
static void TeamDid(int team)
{
	g_teamBuilt[team]++;
	g_teamNext[team] = gpGlobals->time + RANDOM_FLOAT(6.0f, 15.0f);
}
static bool Dares(const Track& t, float worth) { return worth * (0.45f + 0.55f * t.nerve) > RANDOM_FLOAT(0.3f, 0.7f); }
static bool Mood(int team, int tactic)
{
	int c = CtxOf(team, tactic);
	g_chance[c] = true; // the round put the question
	return g_choice[c] == 1;
}

// mayLeave: a wall put up with time in hand. Its builder then stays behind it and listens (whoever digs at
// it has its hands full, whoever blows it walks into a rifle), or leaves it to stand alone and goes where the
// other way in is: the wall holds this one for it, and frees a gun. The other side cannot tell which. What
// its players have done at walls so far makes one or the other likelier: against players who come through
// walls the builder stays more often, against players who turn away from them it leaves more often. And the
// more often walls have been blown open, the further back it waits.
static void GiveWallAt(CBasePlayer* bot, Entrance& e, const char* why, bool mayLeave = false)
{
	Job j;
	j.kind = JOB_BUILD;
	j.wall = true;
	j.wood = Wooden(bot);
	j.site = e.site;
	j.num = min(e.numCells, (int)MAX_JOB_CELLS);
	memcpy(j.cells, e.cells, sizeof(int) * 3 * j.num);
	j.stand = e.stand;
	j.deadline = gpGlobals->time + 20.0f;
	e.team = bot->m_iTeam;
	float through = AnswerThrough(), blast = AnswerBlast(), pStay = mayLeave ? 0.25f + 0.6f * through : 1.0f;
	bool stay = RANDOM_FLOAT(0.0f, 1.0f) < pStay;
	Vector back(0, 0, 0);
	back[1 - e.axis] = e.toSite;
	float gz = 0.0f;
	bool spotOk = false;
	Vector spot;
	for (float off : {110.0f + 170.0f * blast, 110.0f})
	{
		spot = e.stand + back * off;
		spotOk = GroundBelow(spot, 30.0f, 60.0f, &gz) && FreeLine(e.stand, Vector(spot.x, spot.y, gz + 36.0f)) >= 1.0f;
		if (spotOk)
			break;
	}
	if (!stay)
		j.after = AFTER_LEAVE;
	else if (spotOk)
	{
		j.after = AFTER_POST;
		j.post = Vector(spot.x, spot.y, gz + 36.0f);
		j.watch = e.pos + Vector(0, 0, 40);
		j.postFor = RANDOM_FLOAT(14.0f, 30.0f);
	}
	if (mayLeave)
		McLog("tactics: %s will %s its wall afterwards (the players have come through %.0f%% of the walls they met, %d met so far; %.0f%% to stay)",
			STRING(bot->pev->netname), stay ? "stay behind" : "leave", through * 100.0f, g_answerTotal, pStay * 100.0f);
	char what[128];
	Q_snprintf(what, sizeof(what), "%s: walls a gap %.0f wide with %s", why, e.width, Wooden(bot) ? "planks" : "stone");
	StartJob(bot, j, what);
	Say(bot, OneOf({"Blocking this off.", "Nobody comes through here.", "Sealing it.", "Wall going up."}));
}

static void GivePosition(CBasePlayer* bot, const PostPlan& plan, const Vector& watch, bool high, const char* why)
{
	Job j;
	j.kind = high ? JOB_PILLAR : JOB_BUILD;
	j.num = high ? plan.num : min(plan.num, Blocks(bot));
	memcpy(j.cells, plan.cells, sizeof(int) * 3 * j.num);
	j.stand = plan.work;
	j.arrive = high ? 5.0f : 14.0f;
	j.after = AFTER_POST;
	j.post = plan.post;
	j.watch = watch;
	j.postFor = RANDOM_FLOAT(30.0f, 55.0f);
	j.deadline = gpGlobals->time + 20.0f;
	char what[128];
	Q_snprintf(what, sizeof(what), "watching %s: %s", why, high ? "stacks up for the high ground" : "builds cover");
	StartJob(bot, j, what);
	if (high)
		Say(bot, OneOf({"Going up.", "Taking the high ground.", "I'll watch from above."}));
	else
		Say(bot, OneOf({"Setting up here.", "Digging in.", "Holding from cover."}));
}

// Where a bot that holds a spot expects trouble from: where it last saw the enemy, else the way in the other
// side is expected to take (what has been learned of the player), else whatever it is looking down.
static bool Threat(CBasePlayer* bot, CCSBot* ai, Vector* at, const char** why)
{
	Vector eye = bot->pev->origin + bot->pev->view_ofs;
	if (ai->GetTimeSinceLastSawEnemy() < 25.0f)
	{
		Vector last = ai->GetLastKnownEnemyPosition() + Vector(0, 0, 20);
		if ((last - eye).Length() > 250.0f && FreeLine(eye, last) >= 1.0f)
		{
			*at = last;
			*why = "where it last saw the enemy";
			return true;
		}
	}
	bool planted = Bomb() != nullptr;
	int role = (bot->m_iTeam == CT && !planted) ? ROLE_ATTACK : (bot->m_iTeam == TERRORIST && planted) ? ROLE_RETAKE : -1;
	if (role >= 0)
	{
		float p[MAX_ENT];
		EntranceOdds(role, planted ? g_plan.site : -1, p);
		for (int n = 0; n < 3; n++)
		{
			int best = -1;
			for (int e = 0; e < (int)g_ent.size(); e++)
				if (p[e] > 0.0f && (best < 0 || p[e] > p[best]))
					best = e;
			if (best < 0)
				break;
			p[best] = 0.0f;
			Vector w = g_ent[best].pos + Vector(0, 0, 40);
			float d = (w - eye).Length();
			if (d > 250.0f && d < 1100.0f && FreeLine(eye, w) >= 1.0f)
			{
				*at = w;
				*why = "the way in the enemy is expected to take";
				return true;
			}
		}
	}
	UTIL_MakeVectors(Vector(0, bot->pev->v_angle.y, 0));
	Vector fwd = gpGlobals->v_forward;
	float open = FreeLine(eye, eye + fwd * 900.0f) * 900.0f;
	if (open >= 350.0f)
	{
		*at = eye + fwd * (open - 20.0f);
		*why = "the lane in front of it";
		return true;
	}
	return false;
}

// It has held a spot for a while and nothing has happened: is the spot worth improving? Cover where it
// stands in the open, or two blocks under its feet where something close by could be looked over.
static bool WitHold(CBasePlayer* bot, CCSBot* ai, Track& t)
{
	MC_WHERE("tactics: judging a held spot");
	float now = gpGlobals->time;
	Vector org = bot->pev->origin;
	bool still = bot->pev->velocity.Length2D() < 8.0f && (bot->pev->flags & FL_ONGROUND);
	if (!still || (org - t.holdAt).Length2D() > 40.0f)
	{
		t.holdAt = org;
		t.holdSince = now;
		t.holdJudged = false;
		return false;
	}
	if (t.holdJudged || now - t.holdSince < t.settle)
		return false;
	t.holdJudged = true; // one judgement for one stay
	int team = bot->m_iTeam;
	bool planted = Bomb() != nullptr;
	// who has reason to dig in: whoever defends (Counter-Terrorists until the bomb is down, Terrorists after
	// it), and anybody who has just had the enemy in front of it
	bool defends = (team == CT && !planted) || (team == TERRORIST && planted) || ai->GetTimeSinceLastSawEnemy() < 25.0f;
	if (!defends || !t.builder || Blocks(bot) < 2 || bot->m_bHasC4 || ai->IsDefusingBomb() || !TeamMay(team))
		return false;
	Vector threat;
	const char* why = "";
	if (!Threat(bot, ai, &threat, &why))
		return false;
	Vector feet = org + Vector(0, 0, bot->pev->mins.z), eye = org + bot->pev->view_ofs;
	// how much the spot leaves showing: nothing between it and the threat even when it ducks, and far to be shot from
	bool lowCover = FreeLine(feet + Vector(0, 0, 28), threat) < 1.0f;
	float worth = (lowCover ? 0.4f : 1.0f) * min(1.0f, (threat - eye).Length() / 450.0f);
	PostPlan plan;
	bool high = false, found = false;
	if (FindPost(true, feet, 0.0f, 170.0f, nullptr, threat, nullptr, bot, plan))
	{
		// something close by to look over from two blocks up: little of the bot shows there
		found = high = true;
		worth = max(worth, 0.85f);
	}
	else if (Blocks(bot) >= 3 && FreeLine(eye, threat) >= 1.0f)
	{
		// two shapes of cover, by the bot's taste and its blocks
		bool slit = Blocks(bot) >= 4 && RANDOM_LONG(0, 2) != 0;
		found = (slit && PlanSlit(feet, threat, plan)) || PlanHalf(feet, threat, plan) || (Blocks(bot) >= 4 && PlanSlit(feet, threat, plan));
	}
	if (!found)
		return false;
	worth *= 1.0f - 0.03f * plan.num; // (the blocks are dear)
	if (!Mood(team, TAC_SETUP) || !Dares(t, worth))
	{
		McLog("tactics: %s has held an open spot %.0f s (worth %.2f, nerve %.2f) and leaves it as it is", STRING(bot->pev->netname), now - t.holdSince,
			worth, t.nerve);
		return false;
	}
	TeamDid(team);
	GivePosition(bot, plan, threat, high, why);
	return true;
}

// The narrowest gap on the enemy's way to a bot, near the bot: something to wall. Only a gap that shuts the
// way (walking round it is a long way or no way), that the bot has the blocks for, and that the enemy is
// still well short of.
static bool GapToward(CBasePlayer* bot, const Vector& threat, int blocks, Entrance& best)
{
	MC_WHERE("tactics: looking for a gap to wall");
	Vector org = bot->pev->origin, feet = org + Vector(0, 0, bot->pev->mins.z);
	CNavArea* mine = TheNavAreaGrid.GetNearestNavArea(&org);
	CNavArea* theirs = TheNavAreaGrid.GetNearestNavArea(&threat);
	std::vector<Entrance> none;
	std::vector<Vector> pts;
	float length = 0.0f;
	if (!Route(theirs, mine, org, none, nullptr, &pts, &length) || length < 450.0f)
		return false;
	float along = 0.0f, bestW = 1e9f;
	bool found = false;
	for (size_t k = 0; k + 1 < pts.size() && along < 420.0f; k++)
	{
		Vector seg = pts[k + 1] - pts[k];
		float len = seg.Length();
		if (len < 1.0f)
			continue;
		Vector out = seg / len; // away from the bot, towards the enemy
		for (float s = 0.0f; s < len; s += 16.0f)
		{
			if (length - (along + s) < 320.0f)
				break; // the enemy would be at it before it stands
			Vector p = pts[k] + out * s;
			float gz, d = (p - feet).Length();
			if (d < 70.0f || d > 320.0f || !GroundBelow(p, 40.0f, 80.0f, &gz) || fabsf(gz - feet.z) > 60.0f)
				continue;
			p.z = gz;
			for (int axis = 0; axis < 2; axis++)
			{
				int t = 1 - axis;
				if (fabsf(out[t]) < 0.45f)
					continue;
				Entrance e;
				e.site = -1;
				e.pos = p;
				e.axis = axis;
				Gap(p, axis, &e.lo, &e.hi);
				e.width = e.hi - e.lo;
				e.toSite = out[t] < 0.0f ? 1.0f : -1.0f; // (the bot's side of it)
				if (e.width > 170.0f || e.width >= bestW - 4.0f)
					continue;
				PlanWall(e);
				if (e.numCells < 2 || e.numCells > blocks)
					continue;
				best = e;
				bestW = e.width;
				found = true;
			}
		}
		along += len;
	}
	if (!found)
		return false;
	float around = 0.0f;
	return !Route(theirs, mine, org, none, &best, nullptr, &around) || around - length >= 350.0f;
}

// The enemy is close and out of sight, and there is a gap between: falling back hurt or outnumbered, a bot
// shuts the way behind it; a defender that hears them coming shuts it in their faces.
static bool WitWall(CBasePlayer* bot, CCSBot* ai, Track& t)
{
	float now = gpGlobals->time;
	if (now < t.nextWall && t.nextWall < now + 30.0f)
		return false;
	t.nextWall = now + 0.7f;
	int team = bot->m_iTeam;
	if (!t.builder || Blocks(bot) < 2 || bot->m_bHasC4 || ai->IsDefusingBomb() || !(bot->pev->flags & FL_ONGROUND) || !TeamMay(team))
		return false;
	Vector org = bot->pev->origin, threat;
	CGrenade* bomb = Bomb();
	bool defends = (team == CT && !bomb) || (team == TERRORIST && bomb);
	const char* why = nullptr;
	float worth = 0.0f, seen = ai->GetTimeSinceLastSawEnemy();
	const Vector* noise = ai->GetNoisePosition();
	if (seen > 0.8f && seen < 6.0f && (bot->pev->health < 70.0f || ai->GetNearbyEnemyCount() > ai->GetNearbyFriendCount() + 1))
	{
		threat = ai->GetLastKnownEnemyPosition();
		why = bot->pev->health < 70.0f ? "hurt and falling back" : "outnumbered and falling back";
		worth = 0.95f;
	}
	else if (defends && noise)
	{
		threat = *noise;
		why = "hears the enemy coming";
		worth = 0.8f;
	}
	else
		return false;
	float d = (threat - org).Length();
	if (d < 350.0f || d > 1400.0f)
		return false;
	Entrance gap;
	if (!GapToward(bot, threat, Blocks(bot), gap))
		return false;
	int ta = 1 - gap.axis;
	auto beyond = [&](const Vector& p) { return (p[ta] - gap.pos[ta]) * gap.toSite < -24.0f; };
	// nobody of its own side shut out, no enemy at it already, and not across its own way to the bomb
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (!p || p == bot || !p->IsAlive())
			continue;
		float pd = (p->pev->origin - gap.pos).Length();
		if (p->m_iTeam == team ? (beyond(p->pev->origin) && pd < 700.0f) : pd < 260.0f)
			return false;
	}
	if (bomb && team == CT && beyond(bomb->pev->origin))
		return false;
	worth *= 1.0f - 0.03f * gap.numCells;
	if (!Mood(team, TAC_WALL) || !Dares(t, worth))
	{
		t.nextWall = now + 8.0f;
		McLog("tactics: %s, %s, could wall a gap %.0f wide and does not", STRING(bot->pev->netname), why, gap.width);
		return false;
	}
	TeamDid(team);
	GiveWallAt(bot, gap, why);
	if (g_dyn.size() < 6)
	{
		FindPairs(gap);
		g_dyn.push_back(gap);
	}
	return true;
}

// A way into the site its side defends, near enough to shut. What speaks for it: how likely the enemy is to
// come this way, and what it then costs them (the long way round, or digging in the open, or TNT). What
// speaks against: the builder stands there with stone in its hands, and the sooner the enemy can be at the
// gap, the likelier it dies doing it. Each way in is weighed once a round by a bot that comes near it.
static bool WitDeny(CBasePlayer* bot, CCSBot* ai, Track& t)
{
	MC_WHERE("tactics: judging a way in");
	int team = bot->m_iTeam;
	CGrenade* bomb = Bomb();
	bool defends = (team == CT && !bomb) || (team == TERRORIST && bomb);
	if (!defends || !t.builder || Blocks(bot) < 2 || bot->m_bHasC4 || !(bot->pev->flags & FL_ONGROUND) || !TeamMay(team))
		return false;
	float now = gpGlobals->time;
	Vector org = bot->pev->origin;
	int role = team == CT ? ROLE_ATTACK : ROLE_RETAKE, enemySide = team == CT ? 0 : 1;
	for (int e = 0; e < (int)g_ent.size(); e++)
	{
		Entrance& en = g_ent[e];
		if ((t.denySeen & (1u << e)) || en.walled || en.numCells < 2 || en.numCells > Blocks(bot) || (bomb && en.site != g_plan.site))
			continue;
		float walk = (en.stand - org).Length();
		if (walk > 700.0f)
			continue;
		t.denySeen |= 1u << e;
		// what it is worth
		float odds[MAX_ENT], sum = 0.0f;
		EntranceOdds(role, bomb ? g_plan.site : -1, odds);
		for (float v : odds)
			sum += v;
		float likely = sum > 0.0f ? odds[e] / sum : 0.0f;
		float reward = (0.4f + 0.6f * min(1.0f, likely * 2.0f)) * (0.5f + 0.5f * min(1.0f, en.around[enemySide] / 1500.0f));
		// when the enemy can be at it: from its spawn since the round began; once the bomb is down, from
		// wherever the nearest of them is; at once, when it is heard there
		float eta = bomb ? 99.0f : g_plan.begin + en.from[enemySide] / 250.0f - now;
		bool covered = false;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(i);
			if (!p || p == bot || !p->IsAlive())
				continue;
			if (p->m_iTeam != team)
			{
				if (bomb && (p->m_iTeam == CT || p->m_iTeam == TERRORIST))
					eta = min(eta, (p->pev->origin - en.pos).Length() / 250.0f);
			}
			else if ((p->pev->origin - en.pos).Length() < 600.0f && FreeLine(p->pev->origin + p->pev->view_ofs, en.pos + Vector(0, 0, 40)) >= 1.0f)
				covered = true;
		}
		const Vector* noise = ai->GetNoisePosition();
		if (noise && (*noise - en.pos).Length() < 500.0f)
			eta = min(eta, 1.0f);
		float takes = walk / 240.0f + 0.5f + 0.33f * en.numCells;
		float safety = max(0.1f, min(1.0f, 0.5f + (eta - takes) / 6.0f));
		if (covered)
			safety = min(1.0f, safety + 0.3f);
		// (a way in shut is worth a good deal: a wall likely to be met, with time to build it, is a chance a
		// bot of middling nerve takes about every other time its side is in the mood)
		float worth = min(1.0f, 1.5f * reward * safety * (1.0f - 0.03f * en.numCells));
		bool mood = Mood(team, TAC_WALL), go = mood && Dares(t, worth);
		McLog("tactics: %s weighs shutting way in #%d (%d blocks): the enemy is expected there with %.0f%%, shut it sends them %.0f units round; they "
			  "could be at it in %.1f s, the wall takes %.1f s, %s: worth %.2f, %s",
			STRING(bot->pev->netname), e, en.numCells, likely * 100.0f, en.around[enemySide], eta, takes, covered ? "a mate has it in sight" : "nobody covers",
			worth, go ? "it does it" : !mood ? "its side is not for walls this round" : "it does not dare");
		if (!go)
			continue;
		TeamDid(team);
		char why[96];
		Q_snprintf(why, sizeof(why), "shuts way in #%d to site %d", e, en.site);
		GiveWallAt(bot, en, why, true);
		// a mate close by is asked to watch the gap meanwhile
		CBasePlayer* mate = nullptr;
		float best = 700.0f;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(i);
			if (!p || p == bot || !p->IsBot() || !p->IsAlive() || p->m_iTeam != team || g_job[i].kind != JOB_NONE || g_post[i].on || i == g_standIn)
				continue;
			float d = (p->pev->origin - en.stand).Length();
			if (d < best)
			{
				best = d;
				mate = p;
			}
		}
		if (mate && !covered)
		{
			Vector sideways(0, 0, 0);
			sideways[en.axis] = RANDOM_LONG(0, 1) ? 70.0f : -70.0f;
			Vector spot = en.stand + sideways;
			float gz;
			if (GroundBelow(spot, 30.0f, 60.0f, &gz))
			{
				Post& mp = g_post[mate->entindex()];
				mp.on = true;
				mp.high = false;
				mp.cover = true;
				mp.spot = Vector(spot.x, spot.y, gz + 36.0f);
				mp.watch = en.pos + Vector(0, 0, 40);
				mp.until = now + takes + 4.0f;
				static_cast<CCSBot*>(mate)->MoveTo(&mp.spot, FASTEST_ROUTE);
				McLog("tactics: %s covers %s at the wall", STRING(mate->pev->netname), STRING(bot->pev->netname));
				Say(mate, OneOf({"Covering you.", "I've got the gap.", "Build, I'm watching."}));
			}
		}
		return true;
	}
	return false;
}

// A bot with the parts of an iron golem, at the first quiet moment of the round: the T of iron on the
// ground in front of it, the pumpkin on top. Shot at while it builds, it drops the job like any builder.
static bool WitGolem(CBasePlayer* bot, CCSBot* ai, Track& t)
{
	float now = gpGlobals->time;
	bool wither = HasWitherParts(bot);
	if ((!wither && !HasGolemParts(bot)) || !(bot->pev->flags & FL_ONGROUND) || now - g_plan.begin < 1.5f || (now < t.nextGolem && t.nextGolem < now + 30.0f))
		return false;
	t.nextGolem = now + 1.0f;
	if (ai->GetTimeSinceLastSawEnemy() < 4.0f || (wither ? !WitherAllowed(bot->m_iTeam) : TeamMobsAlive(bot->m_iTeam, TM_GOLEM) >= GolemLimit()))
		return false;
	Vector feet = bot->pev->origin + Vector(0, 0, bot->pev->mins.z + 10.0f);
	UTIL_MakeVectors(Vector(0, bot->pev->v_angle.y, 0));
	int first = fabsf(gpGlobals->v_forward.x) >= fabsf(gpGlobals->v_forward.y) ? (gpGlobals->v_forward.x > 0 ? 0 : 1) : (gpGlobals->v_forward.y > 0 ? 2 : 3);
	static const int kDir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
	for (int k = 0; k < 4; k++)
	{
		const int* d = kDir[(first + k) & 3];
		int base[3];
		ToCell(feet + Vector((float)d[0], (float)d[1], 0) * 80.0f, base);
		Job j;
		j.kind = JOB_BUILD;
		j.golem = true;
		j.num = wither ? 7 : 5;
		j.heads = wither ? 3 : 1;
		// the T (its foot, its middle, an arm to either side), then what goes on top: a golem's pumpkin on the
		// middle, a wither's skulls on the arms and the middle
		const int cells[7][3] = {{base[0], base[1], base[2]}, {base[0], base[1], base[2] + 1}, {base[0] - d[1], base[1] - d[0], base[2] + 1},
			{base[0] + d[1], base[1] + d[0], base[2] + 1}, {wither ? base[0] - d[1] : base[0], wither ? base[1] - d[0] : base[1], base[2] + 2},
			{base[0] + d[1], base[1] + d[0], base[2] + 2}, {base[0], base[1], base[2] + 2}};
		bool room = true;
		for (int c = 0; c < j.num && room; c++)
			room = Takes(cells[c]);
		float gz;
		if (!room || !GroundBelow(CellMid(cells[0]), 10.0f, 30.0f, &gz) || FreeLine(bot->pev->origin, CellMid(cells[1])) < 1.0f)
			continue;
		memcpy(j.cells, cells, sizeof(int) * 3 * j.num);
		j.stand = bot->pev->origin;
		j.phase = PH_WORK;
		j.deadline = now + 15.0f;
		StartJob(bot, j, wither ? "has the parts of a wither and a quiet moment: builds it" : "has the parts of an iron golem and a quiet moment: builds it");
		Say(bot, wither ? OneOf({"Raising a wither. Stand well back.", "Soul sand and skulls: get away from here.", "A wither is going up. Clear the place."})
						: OneOf({"Building a golem. Cover me.", "Iron and a pumpkin: give me a moment.", "A golem is going up here."}));
		return true;
	}
	return false;
}

// The floor cell for a mine in a way in: the middle of the gap, where the floor lies on the grid (TNT set
// into a floor that does not would stand proud of it, and fool nobody)
bool HasFloor(int x, int y, int z); // mc_world_srv.cpp
static bool MineCells(const Entrance& e, int hole[3], int lid[3], bool buried)
{
	if (e.numCells < 2 || !ClassicWorld())
		return false;
	int lowZ = 1 << 30, n = 0, pick[MAX_WALL];
	for (int c = 0; c < e.numCells; c++)
		lowZ = min(lowZ, e.cells[c][2]);
	for (int c = 0; c < e.numCells; c++)
		if (e.cells[c][2] == lowZ)
			pick[n++] = c;
	if (!n)
		return false;
	memcpy(lid, e.cells[pick[n / 2]], sizeof(int) * 3);
	if (!buried)
	{
		// on the floor, one cell into the site from the plate: past the frame of the way in, seen from inside only
		memcpy(hole, lid, sizeof(int) * 3);
		hole[1 - e.axis] += e.toSite > 0.0f ? 1 : -1;
		return Takes(lid) && Takes(hole) && HasFloor(lid[0], lid[1], lid[2]);
	}
	hole[0] = lid[0];
	hole[1] = lid[1];
	hole[2] = lid[2] - 1;
	Vector mid = CellMid(lid);
	float gz;
	if (!Takes(lid) || !GroundBelow(mid, 10.0f, 60.0f, &gz) || fabsf(gz - (mid.z - CELL * 0.5f)) > 3.0f)
		return false;
	mcc::Classic* cw = ClassicWorld();
	return cw->Diggable(hole[0], hole[1], hole[2]) && !cw->Carved(hole[0], hole[1], hole[2]);
}

static bool GiveMine(CBasePlayer* bot, const Entrance& e, const char* why)
{
	Job j;
	j.kind = JOB_TRAP;
	j.num = 2;
	bool buried = cv_mineBuried.value != 0.0f;
	if (!MineCells(e, j.cells[0], j.cells[1], buried))
		return false;
	j.at = buried ? 0 : 1; // (nothing to dig when the TNT stands on the floor)
	j.stand = e.stand;
	j.arrive = 30.0f;
	j.deadline = gpGlobals->time + 25.0f;
	StartJob(bot, j, why);
	Say(bot, OneOf({"Mining the door. Stay off it.", "Putting a mine in here.", "TNT and a plate going in at the door."}));
	return true;
}

// A defender with TNT and a plate, near a way into the site its side holds, with time before the enemy can
// be there: a mine under the middle of the gap. It does not kill the man who trips it (the fuse is four
// seconds); it takes whoever stops there to fight, or follows him through, and leaves a crater in the way.
static bool WitMine(CBasePlayer* bot, CCSBot* ai, Track& t)
{
	MC_WHERE("tactics: judging a mine");
	int team = bot->m_iTeam;
	CGrenade* bomb = Bomb();
	bool defends = (team == CT && !bomb) || (team == TERRORIST && bomb);
	if (!defends || t.mineJudged || !HasTnt(bot) || !HasPlate(bot) || bot->m_bHasC4 || !(bot->pev->flags & FL_ONGROUND) || !TeamMay(team))
		return false;
	float now = gpGlobals->time;
	Vector org = bot->pev->origin;
	int role = team == CT ? ROLE_ATTACK : ROLE_RETAKE, enemySide = team == CT ? 0 : 1;
	for (int e = 0; e < (int)g_ent.size(); e++)
	{
		Entrance& en = g_ent[e];
		int hole[3], lid[3];
		float walk = (en.stand - org).Length();
		if (en.walled || walk > 700.0f || (bomb && en.site != g_plan.site) || !MineCells(en, hole, lid, cv_mineBuried.value != 0.0f))
			continue;
		bool mined = false;
		for (const Mine& m : g_mines)
			mined = mined || (m.pos - en.pos).Length() < 200.0f;
		if (mined)
			continue;
		t.mineJudged = true; // one look a round
		float odds[MAX_ENT], sum = 0.0f;
		EntranceOdds(role, bomb ? g_plan.site : -1, odds);
		for (float v : odds)
			sum += v;
		float likely = sum > 0.0f ? odds[e] / sum : 0.0f;
		float eta = bomb ? 99.0f : g_plan.begin + en.from[enemySide] / 250.0f - now;
		if (bomb)
			for (int k = 1; k <= gpGlobals->maxClients; k++)
			{
				CBasePlayer* p = UTIL_PlayerByIndex(k);
				if (p && p->IsAlive() && p->m_iTeam == CT)
					eta = min(eta, (p->pev->origin - en.pos).Length() / 250.0f);
			}
		float takes = walk / 240.0f + (HasPick(bot) ? 2.5f : 5.5f);
		float safety = max(0.1f, min(1.0f, 0.4f + (eta - takes) / 6.0f));
		float worth = min(1.0f, 1.4f * (0.4f + 0.6f * min(1.0f, likely * 2.0f)) * safety);
		// (not a matter of the side's mood for TNT, which is learned from blowing ways in: a mine is a
		// defender's own decision)
		bool go = Dares(t, worth);
		McLog("tactics: %s weighs a mine at %s: the enemy is expected there with %.0f%%, could be at it in %.1f s, the mine takes %.1f s: worth %.2f, %s",
			STRING(bot->pev->netname), PlaceName(en.pos), likely * 100.0f, eta, takes, worth, go ? "it lays it" : "it does not dare");
		if (!go)
			return false;
		char why[96];
		Q_snprintf(why, sizeof(why), "lays a mine at %s", PlaceName(en.pos));
		if (GiveMine(bot, en, why))
		{
			TeamDid(team);
			return true;
		}
		return false;
	}
	return false;
}

// A Counter-Terrorist at the bomb with enemies about and time in hand: blocks between the bomb and where
// the shots would come from, then the defuse behind them.
static bool WitShield(CBasePlayer* bot, CCSBot* ai, Track& t)
{
	MC_WHERE("tactics: judging a defuse");
	CGrenade* bomb = Bomb();
	if (!bomb || bot->m_iTeam != CT || t.shieldJudged || !t.builder || Blocks(bot) < 2 || !(bot->pev->flags & FL_ONGROUND))
		return false;
	Vector org = bot->pev->origin, at = bomb->pev->origin;
	float d = (at - org).Length(), now = gpGlobals->time;
	if (d > 280.0f || d < 60.0f)
		return false;
	t.shieldJudged = true; // one judgement for one bomb
	int enemies = 0;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (p && p->IsAlive() && p->m_iTeam == TERRORIST)
			enemies++;
	}
	// time for it? the blocks, then the defuse, and something to spare
	float left = bomb->m_flC4Blow - now, need = (bot->m_bHasDefuser ? 5.0f : 10.0f) + 5.5f;
	if (!enemies || left < need || !TeamMay(CT))
		return false;
	Vector threat;
	const char* why = "on the bomb's most open side";
	if (ai->GetTimeSinceLastSawEnemy() < 30.0f)
	{
		threat = ai->GetLastKnownEnemyPosition();
		why = "towards where it last saw them";
	}
	else
	{
		float best = -1.0f;
		for (int k = 0; k < 8; k++)
		{
			float a = k * (float)M_PI / 4.0f;
			Vector dir(cosf(a), sinf(a), 0), from = at + Vector(0, 0, 30);
			float open = FreeLine(from, from + dir * 900.0f);
			if (open > best)
			{
				best = open;
				threat = at + dir * 400.0f;
			}
		}
	}
	int b[3];
	float top;
	if (!BaseCell(at, b, &top))
		return false;
	Vector to = threat - at;
	int ax = fabsf(to.x) >= fabsf(to.y) ? 0 : 1, step = to[ax] > 0.0f ? 1 : -1, side = 1 - ax, flip = RANDOM_LONG(0, 1) ? 1 : -1;
	Job j;
	j.kind = JOB_BUILD;
	j.shield = true;
	// a wall beyond the bomb, three wide when the blocks reach: the middle and one end two high first. Two
	// cells out: the bomb keeps the cells around it free (BombKeepsFree).
	static const int kOrder[5][2] = {{0, 0}, {0, 1}, {1, 0}, {-1, 0}, {1, 1}};
	for (const int* o : kOrder)
	{
		int c[3] = {b[0], b[1], b[2] + o[1]};
		c[ax] += 2 * step;
		c[side] += o[0] * flip;
		if (Takes(c) && j.num < Blocks(bot))
			memcpy(j.cells[j.num++], c, sizeof(c));
	}
	Vector mid = CellMid(b), back(0, 0, 0);
	back[ax] = (float)-step;
	Vector stand = Vector(mid.x, mid.y, org.z) + back * 60.0f; // it works from the bomb's other side
	float gz;
	if (j.num < 2 || !GroundBelow(stand, 30.0f, 60.0f, &gz))
		return false;
	j.stand = Vector(stand.x, stand.y, gz + 36.0f);
	if (!Mood(CT, TAC_SHIELD) || !Dares(t, 0.85f))
	{
		McLog("tactics: %s could shield the defuse (%.0f s left) and goes straight for the bomb", STRING(bot->pev->netname), left);
		return false;
	}
	j.deadline = now + 8.0f;
	TeamDid(CT);
	char what[128];
	Q_snprintf(what, sizeof(what), "%.0f s on the bomb, %d enemies about: shields the defuse %s", left, enemies, why);
	StartJob(bot, j, what);
	Say(bot, OneOf({"Blocks up, then I defuse.", "Covering the bomb first.", "Defusing behind cover."}));
	return true;
}

// ---------------------------------------------------------------------------------------------
// A new way in: a wall of a bomb site thin enough for TNT, with a long walk round it

static float RouteLength(CNavArea* from, CNavArea* to, const Vector& toPos)
{
	std::vector<Entrance> none;
	float len = 0.0f;
	return Route(from, to, toPos, none, nullptr, nullptr, &len) ? len : -1.0f;
}

static void FindBreaches()
{
	g_breach.clear();
	mcc::Classic* cl = ClassicWorld();
	if (!cl || !TheCSBots())
		return;
	double t0 = Millis();
	std::vector<Breach> all;
	int nRays = 0, nWall = 0, nThin = 0, nRoom = 0, nGround = 0, nArea = 0, nDig = 0;
	for (int zi = 0; zi < TheCSBots()->GetZoneCount(); zi++)
	{
		const CCSBotManager::Zone* zone = TheCSBots()->GetZone(zi);
		for (CNavArea* a : TheNavAreaList)
		{
			Vector c = *a->GetCenter();
			if (ZoneDist(zone, c) > 420.0f || fabsf(c.z - zone->m_center.z) > 140.0f)
				continue;
			const Extent* x = a->GetExtent();
			int n = (x->SizeX() > 150.0f && x->SizeY() > 150.0f) ? 5 : 1;
			for (int k = 0; k < n; k++)
			{
				Vector p = c;
				if (k)
				{
					p.x += ((k - 1) & 1 ? 0.25f : -0.25f) * x->SizeX();
					p.y += ((k - 1) & 2 ? 0.25f : -0.25f) * x->SizeY();
				}
				p.z = a->GetZ(&p);
				float gz;
				if (!GroundBelow(p, 30.0f, 50.0f, &gz))
					continue;
				p.z = gz;
				static const float kDir[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
				for (const float* dd : kDir)
				{
					Vector dir(dd[0], dd[1], 0), eye = p + Vector(0, 0, 40);
					float face = FreeLine(eye, eye + dir * 200.0f) * 200.0f;
					nRays++;
					if (face >= 199.0f || face < 24.0f)
						continue; // no wall that way
					nWall++;
					float thick = 0.0f;
					for (float s = 12.0f; s <= 96.0f && thick <= 0.0f; s += 6.0f)
					{
						Vector q = eye + dir * (face + s);
						float q3[3] = {q.x, q.y, q.z};
						if (!mcm::WorldPointSolid(q3))
							thick = s;
					}
					if (thick <= 0.0f)
						continue; // too thick for one blast
					nThin++;
					// room for somebody behind it, and ground to stand on at about the same level
					Vector behind = eye + dir * (face + thick + 4.0f);
					bool room = true;
					for (float up = -24.0f; up <= 24.0f && room; up += 24.0f)
						room = FreeLine(behind + Vector(0, 0, up), behind + Vector(0, 0, up) + dir * 64.0f) >= 1.0f;
					Vector o = p + dir * (face + thick + 36.0f);
					float oz;
					if (!room)
						continue;
					nRoom++;
					if (!GroundBelow(o, 50.0f, 90.0f, &oz) || fabsf(oz - p.z) > 56.0f)
						continue;
					nGround++;
					o.z = oz;
					Vector on = o + Vector(0, 0, 10);
					CNavArea* outside = TheNavAreaGrid.GetNavArea(&on, 80.0f);
					int mc[3];
					ToCell(eye + dir * (face + thick * 0.5f), mc);
					if (!outside || outside == a)
						continue;
					nArea++;
					if (!cl->Diggable(mc[0], mc[1], mc[2]))
						continue;
					nDig++;
					float walk = RouteLength(outside, a, p);
					if (walk >= 0.0f && walk < 600.0f)
						continue; // there is a door near enough
					Breach b;
					b.site = zi;
					b.out = o;
					b.in = p + dir * max(0.0f, face - 34.0f);
					b.dir = dir;
					b.thick = thick;
					b.saved = walk < 0.0f ? 3000.0f : walk;
					all.push_back(b);
				}
			}
		}
	}
	std::stable_sort(all.begin(), all.end(), [](const Breach& a, const Breach& b) { return a.saved > b.saved; });
	int per[CCSBotManager::MAX_ZONES] = {0};
	for (const Breach& b : all)
	{
		bool dup = per[b.site] >= 6;
		for (const Breach& k : g_breach)
			if (k.site == b.site && DotProduct(k.dir, b.dir) > 0.9f && (k.out - b.out).Length() < 180.0f)
				dup = true; // the same wall from the same side
		if (dup)
			continue;
		per[b.site]++;
		g_breach.push_back(b);
		McLog("tactics: site %d has a wall %.0f thick at (%.0f %.0f %.0f) that TNT could open: %.0f units shorter than walking round", b.site,
			b.thick, b.out.x, b.out.y, b.out.z, b.saved);
	}
	McLog("tactics: %d walls worth breaching in %.0f ms (%d looks: %d at a wall, %d thin enough, %d with room behind, %d with ground there, %d on the mesh, %d that can be dug, %d a long walk round)",
		(int)g_breach.size(), Millis() - t0, nRays, nWall, nThin, nRoom, nGround, nArea, nDig, (int)all.size());
}

static bool GiveBreach(CBasePlayer* bot, int index, const char* what)
{
	const Breach& b = g_breach[index];
	Vector face = b.out - b.dir * 36.0f; // the wall's outer face, on the ground
	Job j;
	j.kind = JOB_TNT;
	j.num = 1;
	ToCell(face + b.dir * 20.0f + Vector(0, 0, 14), j.cells[0]);
	if (!Takes(j.cells[0]))
		j.cells[0][2]++;
	Vector stand = face + b.dir * 104.0f;
	float gz;
	if (!Takes(j.cells[0]) || !GroundBelow(stand, 40.0f, 80.0f, &gz))
		return false;
	j.stand = Vector(stand.x, stand.y, gz + 36.0f);
	j.breach = index;
	j.deadline = gpGlobals->time + 30.0f;
	StartJob(bot, j, what);
	return true;
}

// On its way into a bomb site with TNT in its pack (a Counter-Terrorist to the planted bomb, a Terrorist to
// plant): is there a wall to go through instead of the doors everybody watches?
static bool WitBreach(CBasePlayer* bot, CCSBot* ai, Track& t)
{
	MC_WHERE("tactics: judging a breach");
	if (t.breachJudged || g_breach.empty() || !HasTnt(bot) || !HasFlint(bot))
		return false;
	float now = gpGlobals->time;
	CGrenade* bomb = Bomb();
	Vector org = bot->pev->origin, goal;
	int team = bot->m_iTeam, site = -1;
	if (team == CT && bomb)
	{
		// (not at once: it takes a moment to think of it, a different moment each time)
		if (now - g_plan.plantTime < t.settle * 0.5f)
			return false;
		goal = bomb->pev->origin;
		site = g_plan.site;
	}
	else if (team == TERRORIST && !bomb && (bot->m_bHasC4 || ai->GetTask() == CCSBot::PLANT_BOMB) && ai->HasPath())
	{
		if (now - g_plan.begin < t.settle)
			return false;
		goal = ai->GetPathEndpoint();
		const CCSBotManager::Zone* zone = TheCSBots()->GetClosestZone(&goal);
		site = zone ? zone->m_index : -1;
		if (zone && ZoneDist(zone, goal) > 300.0f)
			return false; // not on its way to a site
	}
	else
		return false;
	t.breachJudged = true; // one judgement a round
	CNavArea* mine = TheNavAreaGrid.GetNearestNavArea(&org);
	CNavArea* there = TheNavAreaGrid.GetNearestNavArea(&goal);
	float direct = RouteLength(mine, there, goal);
	if (site < 0 || direct < 900.0f)
		return false;
	// a way in that is walled makes the wall look better still
	float walled = 0.0f;
	for (const Entrance& e : g_ent)
		if (e.site == site && e.walled)
			walled = 800.0f;
	int best = -1;
	float bestGain = 0.0f, bestTo = 0.0f;
	for (int i = 0; i < (int)g_breach.size(); i++)
	{
		const Breach& b = g_breach[i];
		if (b.site != site)
			continue;
		float to = RouteLength(mine, TheNavAreaGrid.GetNearestNavArea(&b.out), b.out);
		if (to < 0.0f)
			continue;
		float gain = direct - (to + b.thick + (b.in - goal).Length()) + walled;
		if (gain > bestGain)
		{
			bestGain = gain;
			bestTo = to;
			best = i;
		}
	}
	if (best < 0 || bestGain < 400.0f)
		return false;
	const Breach& b = g_breach[best];
	if (bomb)
	{
		// the walk there, the fuse and the way through, the walk to the bomb, the defuse
		float need = bestTo / 230.0f + 8.0f + (b.in - goal).Length() / 230.0f + (bot->m_bHasDefuser ? 5.0f : 10.0f) + 3.0f;
		if (bomb->m_flC4Blow - now < need)
			return false;
	}
	float worth = min(1.0f, 0.45f + bestGain / 2500.0f);
	if (!Mood(team, TAC_TNT) || !Dares(t, worth))
	{
		McLog("tactics: %s thinks of blowing a way into site %d (%.0f units shorter) and takes the doors", STRING(bot->pev->netname), site, bestGain);
		return false;
	}
	char what[128];
	Q_snprintf(what, sizeof(what), "the walk into site %d is %.0f units, a wall on the way is %.0f thick: blows a way in", site, direct, b.thick);
	if (!GiveBreach(bot, best, what))
		return false;
	Say(bot, OneOf({"Making my own door.", "Going through the wall.", "Stand clear of the wall."}));
	return true;
}

// The bomb is down: where it is, and what is expected of the humans coming back for it
static void PlantSeen()
{
	Plan& pl = g_plan;
	CGrenade* bomb = Bomb();
	if (!bomb || pl.siteKnown || gpGlobals->time - pl.plantTime < 0.6f)
		return;
	pl.siteKnown = true;
	const CCSBotManager::Zone* zone = TheCSBots()->GetClosestZone(&bomb->pev->origin);
	pl.site = zone ? zone->m_index : -1;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* h = UTIL_PlayerByIndex(i);
		if (!IsHuman(h) || !h->IsAlive() || h->m_iTeam != CT)
			continue;
		float p[MAX_ENT];
		RouteProbs(ROLE_RETAKE, g_track[i].last[ROLE_RETAKE], pl.site, p);
		int best = -1;
		for (int e = 0; e < MAX_ENT; e++)
			if (p[e] > 0.0f && (best < 0 || p[e] > p[best]))
				best = e;
		g_track[i].guess[ROLE_RETAKE] = best;
		g_track[i].crossed[ROLE_RETAKE] = -1;
	}
}

static void RoundBegin()
{
	Plan& pl = g_plan;
	edict_t* bombEd = pl.bombEd;
	int serial = pl.bombSerial;
	float plantTime = pl.plantTime;
	pl = Plan();
	pl.bombEd = bombEd; // (a test may have a bomb down already)
	pl.bombSerial = serial;
	pl.plantTime = plantTime;
	pl.live = true;
	pl.begin = gpGlobals->time;
	for (int c = 0; c < NUM_CTX; c++)
	{
		g_choice[c] = Choose(c);
		g_chance[c] = false;
	}
	g_dyn.clear();
	g_digs.clear();
	g_mines.clear();
	g_story.clear();
	memset(g_teamBuilt, 0, sizeof(g_teamBuilt));
	memset(g_teamNext, 0, sizeof(g_teamNext));
	memset(g_golemBuyer, 0, sizeof(g_golemBuyer));
	memset(g_wallsMet, 0, sizeof(g_wallsMet));
	memset(g_woodMet, 0, sizeof(g_woodMet));
	memset(g_pickDigs, 0, sizeof(g_pickDigs));
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		Track& t = g_track[i];
		CBasePlayer* h = UTIL_PlayerByIndex(i);
		t.crossed[0] = t.crossed[1] = t.guess[0] = t.guess[1] = -1;
		t.prevValid = false;
		t.met = t.metDug = false;
		t.answer = -1;
		if (!IsHuman(h))
			continue;
		if (h->m_iTeam != t.team)
		{
			t.team = h->m_iTeam;
			t.last[0] = t.last[1] = -1;
		}
		if (h->m_iTeam == TERRORIST && h->IsAlive())
		{
			float p[MAX_ENT];
			RouteProbs(ROLE_ATTACK, t.last[ROLE_ATTACK], -1, p);
			int best = -1;
			for (int e = 0; e < MAX_ENT; e++)
				if (p[e] > 0.0f && (best < 0 || p[e] > p[best]))
					best = e;
			t.guess[ROLE_ATTACK] = best;
			if (best >= 0)
				McLog("tactics: %s is expected through way in #%d (site %d, %.0f%%)", STRING(h->pev->netname), best, g_ent[best].site, p[best] * 100.0f);
		}
	}
}

static void RoundOver(int winStatus, int event)
{
	Plan& pl = g_plan;
	if (!pl.live)
		return;
	pl.live = false;
	if (winStatus != WINSTATUS_CTS && winStatus != WINSTATUS_TERRORISTS)
		return;
	for (int c = 0; c < NUM_CTX; c++)
	{
		if (!g_chance[c])
			continue;
		bool won = winStatus == (c < NUM_TAC ? WINSTATUS_TERRORISTS : WINSTATUS_CTS);
		Reward(c, g_choice[c], won);
		McLog("tactics: %s: the mood was \"%s\" and they %s the round (now %.1f won, %.1f lost)", CtxName(c), kArmName[c % NUM_TAC][g_choice[c]], won ? "won" : "lost",
			g_arm[c][g_choice[c]].a - 1.0f, g_arm[c][g_choice[c]].b - 1.0f);
	}
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		Track& t = g_track[i];
		CBasePlayer* h = UTIL_PlayerByIndex(i);
		if (!IsHuman(h))
			continue;
		if (t.met && t.answer < 0 && h->IsAlive())
		{
			t.answer = ANS_AWAY;
			AnswerSeen(ANS_AWAY);
			McLog("tactics: %s met a wall and left it standing to the end of the round", STRING(h->pev->netname));
		}
		for (int r = 0; r < NUM_ROLES; r++)
		{
			int e = t.crossed[r];
			if (e < 0)
				continue;
			if (t.guess[r] >= 0 && cv_learn.value != 0.0f)
			{
				g_route[r].total++;
				if (t.guess[r] == e)
					g_route[r].hits++;
			}
			McLog("tactics: %s came through way in #%d %s (expected #%d; right %d of %d rounds)", STRING(h->pev->netname), e, kRoleName[r], t.guess[r],
				g_route[r].hits, g_route[r].total);
			RouteSeen(r, t.last[r], e);
			t.last[r] = e;
		}
	}
	TellStory(winStatus, event);
	SaveBrain();
}

static void TrackHumans()
{
	bool planted = Bomb() != nullptr;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		Track& t = g_track[i];
		CBasePlayer* h = UTIL_PlayerByIndex(i);
		if (!IsHuman(h) || !h->IsAlive() || (h->m_iTeam != TERRORIST && h->m_iTeam != CT))
		{
			t.prevValid = false;
			continue;
		}
		Vector feet = h->pev->origin + Vector(0, 0, h->pev->mins.z);
		if (t.prevValid && (feet - t.prev).Length() < 200.0f && (feet - t.prev).Length() > 0.5f)
		{
			for (int e = 0; e < (int)g_ent.size(); e++)
			{
				float dir = 0.0f;
				if (!Crosses(g_ent[e], t.prev, feet, &dir) || dir <= 0.0f)
					continue;
				int role = -1;
				if (h->m_iTeam == TERRORIST && !planted)
					role = ROLE_ATTACK;
				else if (h->m_iTeam == CT && planted && g_ent[e].site == g_plan.site)
					role = ROLE_RETAKE;
				if (role >= 0 && t.crossed[role] != e)
				{
					t.crossed[role] = e;
					McLog("tactics: %s comes in through way in #%d (site %d)", STRING(h->pev->netname), e, g_ent[e].site);
				}
			}
		}
		t.prev = feet;
		t.prevValid = true;
		// a wall of the other side's across its way: what becomes of it?
		if (t.answer >= 0)
			continue;
		if (!t.met)
		{
			for (const std::vector<Entrance>* list : {&g_ent, &g_dyn})
				for (const Entrance& e : *list)
					if (!t.met && e.walled && e.team != 0 && e.team != h->m_iTeam && (e.pos - feet).Length2D() < 230.0f && fabsf(e.pos.z - feet.z) < 110.0f)
					{
						t.met = true;
						t.metDug = false;
						t.metAt = e.pos;
						McLog("tactics: %s comes up against a wall at (%.0f %.0f %.0f)", STRING(h->pev->netname), e.pos.x, e.pos.y, e.pos.z);
					}
			continue;
		}
		bool stands = false;
		for (const std::vector<Entrance>* list : {&g_ent, &g_dyn})
			for (const Entrance& e : *list)
				if (e.walled && (e.pos - t.metAt).Length() < 10.0f)
					stands = true;
		for (const Dig& d : g_digs)
			if (d.team == h->m_iTeam && (d.at - t.metAt).Length() < 220.0f)
				t.metDug = true;
		int a = -1;
		if (!stands)
			a = t.metDug ? ANS_DIG : ANS_BLAST;
		else if ((feet - t.metAt).Length2D() > 700.0f)
			a = ANS_AWAY;
		if (a >= 0)
		{
			t.answer = a;
			AnswerSeen(a);
			if (a == ANS_DIG)
				StoryTell(3.0f, "%s dug through the wall at %s", STRING(h->pev->netname), PlaceName(t.metAt));
			else if (a == ANS_BLAST)
				StoryTell(3.0f, "the wall at %s was blown open", PlaceName(t.metAt));
			else
				StoryTell(2.0f, "%s turned away from the wall at %s", STRING(h->pev->netname), PlaceName(t.metAt));
			McLog("tactics: %s met a wall and %s (so far: dug %.1f, blown %.1f, turned away %.1f)", STRING(h->pev->netname), kAnsName[a], g_answer[ANS_DIG],
				g_answer[ANS_BLAST], g_answer[ANS_AWAY]);
		}
	}
}

void BotTacticsFrame()
{
	if (cv_tactics.value == 0.0f || !g_worldLoaded)
		return;
	if (!g_entReady)
	{
		// (the mesh is loaded with the map; the first frames are for the world to settle)
		if (gpGlobals->time < 3.0f || !TheCSBots() || TheNavAreaList.empty())
			return;
		FindEntrances();
		FindBreaches();
		LoadBrain();
	}
	if (g_plan.live)
		TrackHumans();
	static float next = 0.0f;
	float now = gpGlobals->time;
	if (now < next && next < now + 1.0f)
		return;
	next = now + 0.25f;
	bool freeze = CSGameRules()->IsFreezePeriod() != FALSE;
	if (g_wasFreeze && !freeze)
		RoundBegin();
	g_wasFreeze = freeze;
	if (freeze || !g_plan.live)
		return;
	g_numWalled = 0;
	for (std::vector<Entrance>* list : {&g_ent, &g_dyn})
		for (Entrance& e : *list)
		{
			int placed = 0;
			for (int c = 0; c < e.numCells; c++)
				if (Placed(e.cells[c]))
					placed++;
			e.walled = e.numCells > 0 && placed * 4 >= e.numCells * 3;
			if (e.walled)
				g_numWalled++;
		}
	PlantSeen();
	g_digs.clear();
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		const McPlayer& mp = P(i);
		if (p && p->IsAlive() && mp.mineProgress > 0.02f && mp.mineBlock[0] >= 0 && Placed(mp.mineBlock))
		{
			g_digs.push_back({p->m_iTeam, CellMid(mp.mineBlock), gpGlobals->time});
			const mci::Stack& held = HeldStack(p);
			if (!held.Empty() && mci::Item(held.id).type == mci::IT_PICKAXE && p->m_iTeam >= 0 && p->m_iTeam < 4)
				g_pickDigs[p->m_iTeam] = 1;
		}
	}
}

void BotTacticsRoundRestart()
{
	for (int i = 0; i <= MAX_CLIENTS; i++)
	{
		g_job[i] = Job();
		g_post[i] = Post();
		g_ctl[i].look = g_ctl[i].hold = g_ctl[i].steer = g_ctl[i].jump = g_ctl[i].stand = false;
	}
	g_plan = Plan();
	g_wasFreeze = true;
	g_numWalled = 0;
	g_dyn.clear();
	for (Entrance& e : g_ent)
	{
		e.walled = false;
		e.team = 0;
	}
}

void BotTacticsMapEnd()
{
	SaveBrain();
	g_ent.clear();
	g_breach.clear();
	g_entReady = false;
	g_brainMap[0] = 0;
	BotTacticsRoundRestart();
	for (Track& t : g_track)
		t = Track();
}

// ---------------------------------------------------------------------------------------------
// Shopping, at the spawn

void BotTacticsSpawn(CBasePlayer* bot)
{
	int i = bot->entindex();
	g_job[i] = Job();
	g_post[i] = Post();
	Track& t = g_track[i];
	t.crumbNum = t.crumbAt = 0;
	t.anchorTime = gpGlobals->time;
	t.hp = bot->pev->health;
	t.nerve = RANDOM_FLOAT(0.2f, 1.0f);
	t.settle = RANDOM_FLOAT(4.0f, 13.0f);
	t.holdAt = bot->pev->origin;
	t.holdSince = gpGlobals->time;
	t.holdJudged = t.shieldJudged = t.breachJudged = t.mineJudged = false;
	t.nextWit = t.nextWall = 0.0f;
	t.denySeen = 0;
	if (cv_tactics.value == 0.0f || !g_worldLoaded || !TheCSBots() || TheCSBots()->GetScenario() != CCSBotManager::SCENARIO_DEFUSE_BOMB)
		return;
	Mob mob = MobOf(bot);
	if (mob == MOB_CREEPER || mob == MOB_ENDERMAN)
		return;
	if (!g_stone)
		Items();
	McPlayer& mp = P(bot);
	auto put = [&](int slot, int id, int count) {
		mp.hotbar[slot] = mci::Stack();
		mp.hotbar[slot].id = (uint16_t)id;
		mp.hotbar[slot].count = (uint8_t)count;
	};
	int spent = 0, money = bot->m_iAccount;
	// An iron golem: $3500 for its parts (four iron blocks and a carved pumpkin), a great deal of money for a
	// great deal of trouble for the other side. One bot of a side that has no golem, with a rifle's money
	// besides; it carries nothing else to build with that round. Parts it still carries it keeps.
	bool golemParts = HasGolemParts(bot);
	int side = bot->m_iTeam;
	if (!golemParts && g_iron > 0 && g_pumpkin > 0 && side >= 0 && side < 4 && !g_golemBuyer[side] && Blocks(bot) == 0 && !HasTnt(bot) &&
		TeamMobsAlive(side, TM_GOLEM) < GolemLimit() && RANDOM_LONG(1, 100) <= (int)cv_golemBots.value)
	{
		int cost = 4 * mci::Price(g_iron) + mci::Price(g_pumpkin);
		if (money >= cost + 3200)
		{
			spent += cost;
			put(SLOT_BLOCKS, g_iron, 4);
			put(SLOT_TNT, g_pumpkin, 1);
			golemParts = true;
		}
	}
	// A wither: all the money a player can hold. A bot at Counter-Strike's ceiling that has its rifle
	// already, when its side may raise one.
	bool witherParts = HasWitherParts(bot);
	if (!golemParts && !witherParts && g_soul > 0 && g_skull > 0 && side >= 0 && side < 4 && Blocks(bot) == 0 && !HasTnt(bot) && WitherAllowed(side) &&
		bot->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] && RANDOM_LONG(1, 100) <= (int)cv_witherBots.value)
	{
		int cost = 4 * mci::Price(g_soul) + 3 * mci::Price(g_skull);
		if (money >= cost)
		{
			spent += cost;
			put(SLOT_BLOCKS, g_soul, 4);
			put(SLOT_TNT, g_skull, 3);
			witherParts = true;
		}
	}
	if (witherParts)
	{
		McLog("tactics: %s carries the parts of a wither this round", STRING(bot->pev->netname));
		golemParts = true; // (from here on the same: its pack is spoken for)
	}
	if (golemParts)
	{
		g_golemBuyer[side] = true;
		money = spent; // (nothing more: its pack is spoken for)
	}
	// Blocks are dear and gone with the round they are set down in: is it wise to buy them this round? A
	// bot thinks of it some rounds, more often when blocks have been winning its side rounds; it buys only
	// with its guns and armor paid for, as many as what is left over reaches for. What it carried through
	// the last round it keeps.
	// Of what: stone stops every bullet. Planks cost half and stop pistols and submachine guns only, so they
	// do against a side that is short of money this round; and a pickaxe is the wrong tool for them, so
	// they do against a side that came through the last walls with pickaxes. A read, and not always right.
	int have = Blocks(bot);
	int armed = 0, total = 0, enemy = bot->m_iTeam == CT ? TERRORIST : CT;
	for (int k = 1; k <= gpGlobals->maxClients; k++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(k);
		if (!p || p->m_iTeam != enemy)
			continue;
		total++;
		bool gun = p->m_iAccount >= 2000; // (the money for a rifle, or nearly: the round's pay is still to come for some)
		for (int slot = PRIMARY_WEAPON_SLOT; slot <= PISTOL_SLOT && !gun; slot++)
			for (CBasePlayerItem* it = p->m_rgpPlayerItems[slot]; it && !gun; it = it->m_pNext)
				switch (it->m_iId)
				{
				case WEAPON_AK47: case WEAPON_M4A1: case WEAPON_AUG: case WEAPON_SG552: case WEAPON_GALIL: case WEAPON_FAMAS:
				case WEAPON_AWP: case WEAPON_SCOUT: case WEAPON_G3SG1: case WEAPON_SG550: case WEAPON_M249: case WEAPON_DEAGLE:
					gun = true;
					break;
				default:
					break;
				}
		armed += gun ? 1 : 0;
	}
	float share = total > 0 ? (float)armed / total : 1.0f;
	bool picks = g_pickDigs[enemy] > 0;
	float pWood = share < 0.34f ? 0.85f : share < 0.67f ? 0.35f : 0.1f;
	if (picks)
		pWood = max(pWood, 0.65f);
	bool wood = g_planks > 0 && RANDOM_FLOAT(0.0f, 1.0f) < pWood;
	const bool carried = have > 0;
	if (carried)
		wood = Wooden(bot); // what it carries
	int mat = wood ? g_planks : g_stone, price = mci::Price(mat);
	float edge = 0.0f; // how much better its side's rounds went with blocks in play than without
	for (int tac = TAC_SETUP; tac <= TAC_FIRE; tac++)
	{
		const Arm* a = g_arm[CtxOf(bot->m_iTeam, tac)];
		edge += (a[1].a / (a[1].a + a[1].b) - a[0].a / (a[0].a + a[0].b)) / 4.0f;
	}
	float keen = cv_builders.value * max(0.4f, min(1.8f, 1.0f + 2.5f * edge));
	if (cv_pocket.value > have && !golemParts)
	{
		have = min(64, (int)cv_pocket.value);
		put(SLOT_BLOCKS, mat, have);
	}
	const int kGuns = 3200; // a rifle, armor and something over
	t.builder = have >= 4; // (what it still carries from a round it built in)
	bool bought = false;
	if (RANDOM_FLOAT(0.0f, 100.0f) < keen && price > 0)
	{
		int cap = max(0, (int)cv_blocks.value) - have, n = min(cap, (money - spent - kGuns) / price);
		if (!wood && have == 0 && n < 4 && g_planks > 0 && min(cap, (money - spent - kGuns) / mci::Price(g_planks)) >= 4)
		{
			// what is left over reaches for a wall in wood, not in stone
			wood = true;
			mat = g_planks;
			price = mci::Price(mat);
			n = min(cap, (money - spent - kGuns) / price);
		}
		if (n > 0 && have + n >= 3)
		{
			spent += n * price;
			put(SLOT_BLOCKS, mat, have + n);
			t.builder = true;
			bought = true;
		}
	}
	else if (have < 2 && price > 0 && money - spent >= kGuns + 1000 + (2 - have) * price)
	{
		// not for building: two blocks against the holes the grenades and the TNT leave (a bot in a crater
		// with none stays there)
		spent += (2 - have) * price;
		put(SLOT_BLOCKS, mat, 2);
	}
	// TNT with a flint and steel: very dear, and only for a bot with a rifle's money left over (and a few
	// blocks with it: the way out of the crater it makes)
	bool flint = HasFlint(bot);
	if (!HasTnt(bot) && (flint || RANDOM_LONG(1, 100) <= (int)cv_tntBots.value))
	{
		int cost = mci::Price(g_tnt) + (flint ? 0 : mci::Price(g_flint)) + max(0, 3 - Blocks(bot)) * price;
		if (money - spent >= cost + 3200)
		{
			spent += cost;
			put(SLOT_TNT, g_tnt, 1);
			if (!flint)
				put(SLOT_FLINT, g_flint, 1);
			if (Blocks(bot) < 3)
				put(SLOT_BLOCKS, mat, 3);
			if (g_plate > 0)
			{
				put(SLOT_HAND, g_plate, 1); // (the lid, should the TNT go into a floor)
				spent += mci::Price(g_plate);
			}
		}
	}
	// a pickaxe for one in five, and for half of a side that ran into walls the round before: with it a wall
	// comes down in a second or two, not a quarter of a minute (the digger has its hands full either way)
	// (an axe when the walls it met were of wood: a pickaxe is no faster at planks than bare hands)
	bool walls = bot->m_iTeam >= 0 && bot->m_iTeam < 4 && g_wallsMet[bot->m_iTeam] > 0;
	int tool = (walls && g_axe > 0 && g_woodMet[bot->m_iTeam] * 2 > g_wallsMet[bot->m_iTeam]) ? g_axe : g_pick;
	if (!HasPick(bot) && RANDOM_LONG(1, 100) <= (walls ? 50 : 20) && money - spent >= mci::Price(tool) + 400)
	{
		spent += mci::Price(tool);
		put(SLOT_PICK, tool, 1);
	}
	if (!spent)
		return;
	bot->AddAccount(-spent, RT_PLAYER_BOUGHT_SOMETHING);
	if (!bot->HasNamedPlayerItem("weapon_mcitem"))
		bot->GiveNamedItem("weapon_mcitem");
	mp.invDirty = true;
	char packed[48];
	if (witherParts)
		Q_strlcpy(packed, "the parts of a wither");
	else if (golemParts)
		Q_strlcpy(packed, "the parts of an iron golem");
	else
		Q_snprintf(packed, sizeof(packed), "%d blocks of %s", Blocks(bot), Wooden(bot) ? "planks" : "stone");
	McLog("bot %s spent $%d: %s%s%s%s, $%d left", STRING(bot->pev->netname), spent, packed,
		t.builder ? " (a builder this round)" : "", HasTnt(bot) ? ", TNT and a flint and steel" : "",
		!HasPick(bot) ? "" : mci::Item(mp.hotbar[SLOT_PICK].id).type == mci::IT_AXE ? ", an axe" : ", a pickaxe", bot->m_iAccount);
	if (golemParts && !witherParts)
		McLog("tactics: %s carries the parts of an iron golem this round", STRING(bot->pev->netname));
	if (bought)
		McLog("tactics: %s builds with %s this round: it reads %d of the other side's %d as able to field a gun that goes through wood%s%s",
			STRING(bot->pev->netname), Wooden(bot) ? "planks" : "stone", armed, total, picks ? ", and they dug with pickaxes last round" : "",
			carried ? " (it adds to what it carried over)" : "");
}

// ---------------------------------------------------------------------------------------------
// Hooks

static CGrenade* H_PlantBomb(IReGameHook_PlantBomb* chain, entvars_t* owner, Vector& start, Vector& velocity)
{
	CGrenade* g = chain->callNext(owner, start, velocity);
	if (g)
	{
		BombClearsSpace(g); // nothing stays standing against a planted bomb
		g_plan.bombEd = g->edict();
		g_plan.bombSerial = g->edict()->serialnumber;
		g_plan.plantTime = gpGlobals->time;
		g_plan.plantDecided = g_plan.plantGiven = g_plan.siteKnown = false;
		g_plan.plantTries = 0;
	}
	return g;
}

static bool H_RoundEnd(IReGameHook_RoundEnd* chain, int winStatus, ScenarioEventEndRound event, float tmDelay)
{
	bool ok = chain->callNext(winStatus, event, tmDelay);
	if (ok)
		RoundOver(winStatus, (int)event);
	return ok;
}

void BotTacticsInit()
{
	CVAR_REGISTER(&cv_tactics);
	CVAR_REGISTER(&cv_blocks);
	CVAR_REGISTER(&cv_builders);
	CVAR_REGISTER(&cv_pocket);
	CVAR_REGISTER(&cv_tntBots);
	CVAR_REGISTER(&cv_learn);
	CVAR_REGISTER(&cv_golemBots);
	CVAR_REGISTER(&cv_witherBots);
	CVAR_REGISTER(&cv_mineBuried);
	g_ReGameHookchains.m_PlantBomb.registerHook(&H_PlantBomb, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_RoundEnd.registerHook(&H_RoundEnd, HC_PRIORITY_DEFAULT);
}

// mc_brain: what the bots have learned on this map, to the console of who asked (nobody: to the log)
bool BotTacticsCommand(CBasePlayer* pl, const char* cmd)
{
	auto out = [&](const char* fmt, ...) {
		char buf[240];
		va_list ap;
		va_start(ap, fmt);
		Q_vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
		va_end(ap);
		if (!pl || pl->IsBot())
		{
			McLog("brain: %s", buf);
			return;
		}
		Q_strlcat(buf, "\n");
		CLIENT_PRINTF(pl->edict(), print_console, buf);
	};
	out("MineStrike bot memory for %s (learning %s)", g_brainMap[0] ? g_brainMap : STRING(gpGlobals->mapname), cv_learn.value != 0.0f ? "on" : "off");
	out("Ways into the bomb sites: %d, walls TNT could open: %d", (int)g_ent.size(), (int)g_breach.size());
	for (size_t e = 0; e < g_ent.size(); e++)
	{
		const Entrance& en = g_ent[e];
		if (en.numCells > 0)
			out("  #%d site %d: %.0f units wide, a wall takes %d blocks%s", (int)e, en.site, en.width, en.numCells, en.walled ? " (walled now)" : "");
		else
			out("  #%d site %d: open ground, cannot be walled", (int)e, en.site);
	}
	for (int c = 0; c < NUM_CTX; c++)
	{
		out("%s:", CtxName(c));
		for (int i = 0; i < kArms[c]; i++)
		{
			const Arm& a = g_arm[c][i];
			out("  %-24s the mood in %3d rounds, win rate estimated %2.0f%%", kArmName[c % NUM_TAC][i], a.plays, 100.0f * a.a / (a.a + a.b));
		}
	}
	for (int r = 0; r < NUM_ROLES; r++)
	{
		float p[MAX_ENT];
		RouteProbs(r, -1, -1, p);
		float sum = 0.0f;
		for (float v : p)
			sum += v;
		char line[200] = "";
		for (int e = 0; e < (int)g_ent.size() && sum > 0.0f; e++)
		{
			char one[32];
			Q_snprintf(one, sizeof(one), " #%d %.0f%%", e, 100.0f * p[e] / sum);
			Q_strlcat(line, one);
		}
		out("The humans' way in %s:%s", kRoleName[r], line);
		out("  guessed right before the round: %d of %d", g_route[r].hits, g_route[r].total);
	}
	out("The humans at a wall across their way (%d met): dug through %.1f, had it blown open %.1f, turned away %.1f", g_answerTotal, g_answer[ANS_DIG],
		g_answer[ANS_BLAST], g_answer[ANS_AWAY]);
	out("  so a builder stays behind its wall %.0f%% of the time, and leaves it standing alone the rest", 100.0f * (0.25f + 0.6f * AnswerThrough()));
	return true;
}

// ---------------------------------------------------------------------------------------------
// For the test scenarios (mc_test.cpp)

int TacticsEntrances()
{
	if (!g_entReady)
	{
		FindEntrances();
		FindBreaches();
		LoadBrain();
	}
	return (int)g_ent.size();
}
bool TacticsEntrance(int i, Vector* pos, Vector* stand, int* site, int* cells)
{
	if (i < 0 || i >= (int)g_ent.size())
		return false;
	const Entrance& e = g_ent[i];
	*pos = e.pos;
	*stand = e.stand;
	*site = e.site;
	*cells = e.numCells;
	return true;
}
// A point on a way in's far side (dist > 0, away from the site) or near side (dist < 0)
Vector TacticsOutside(int i, float dist)
{
	const Entrance& e = g_ent[i];
	Vector out(0, 0, 0);
	out[1 - e.axis] = -e.toSite;
	return e.pos + out * dist + Vector(0, 0, 36);
}
void TacticsKit(CBasePlayer* bot, int blocks, bool tnt, bool pick)
{
	Items();
	McPlayer& mp = P(bot);
	for (int s = SLOT_BLOCKS; s <= SLOT_HAND; s++)
		mp.hotbar[s] = mci::Stack();
	auto put = [&](int slot, int id, int count) {
		mp.hotbar[slot].id = (uint16_t)id;
		mp.hotbar[slot].count = (uint8_t)count;
	};
	if (blocks > 0)
		put(SLOT_BLOCKS, g_stone, blocks);
	if (tnt)
	{
		put(SLOT_TNT, g_tnt, 1);
		put(SLOT_FLINT, g_flint, 1);
	}
	if (pick)
		put(SLOT_PICK, g_pick, 1);
	if (!bot->HasNamedPlayerItem("weapon_mcitem"))
		bot->GiveNamedItem("weapon_mcitem");
	g_track[bot->entindex()].builder = blocks > 0;
}
// the parts of an iron golem in its pack (what it then does with them is its own business: WitGolem)
void TacticsGolemKit(CBasePlayer* bot)
{
	Items();
	McPlayer& mp = P(bot);
	for (int s = SLOT_BLOCKS; s <= SLOT_HAND; s++)
		mp.hotbar[s] = mci::Stack();
	mp.hotbar[SLOT_BLOCKS].id = (uint16_t)g_iron;
	mp.hotbar[SLOT_BLOCKS].count = 4;
	mp.hotbar[SLOT_TNT].id = (uint16_t)g_pumpkin;
	mp.hotbar[SLOT_TNT].count = 1;
	if (!bot->HasNamedPlayerItem("weapon_mcitem"))
		bot->GiveNamedItem("weapon_mcitem");
	g_track[bot->entindex()].nextGolem = 0.0f;
}
bool TacticsMine(CBasePlayer* bot, int ent)
{
	Items();
	McPlayer& mp = P(bot);
	for (int s = SLOT_BLOCKS; s <= SLOT_HAND; s++)
		mp.hotbar[s] = mci::Stack();
	mp.hotbar[SLOT_TNT].id = (uint16_t)g_tnt;
	mp.hotbar[SLOT_TNT].count = 1;
	mp.hotbar[SLOT_HAND].id = (uint16_t)g_plate;
	mp.hotbar[SLOT_HAND].count = 1;
	if (!bot->HasNamedPlayerItem("weapon_mcitem"))
		bot->GiveNamedItem("weapon_mcitem");
	return ent >= 0 && ent < (int)g_ent.size() && GiveMine(bot, g_ent[ent], "told to bury a mine");
}
int TacticsMines(Vector* lid)
{
	if (!g_mines.empty() && lid)
		*lid = g_mines[0].pos;
	return (int)g_mines.size();
}
// The bomb site the Terrorists are expected at, by the ways in their players have taken (-1: no idea)
int TacticsExpectedSite()
{
	if (!g_entReady || g_ent.empty())
		return -1;
	float odds[MAX_ENT];
	EntranceOdds(ROLE_ATTACK, -1, odds);
	int best = -1;
	for (int e = 0; e < (int)g_ent.size(); e++)
		if (odds[e] > 0.0f && (best < 0 || odds[e] > odds[best]))
			best = e;
	return best >= 0 ? g_ent[best].site : -1;
}
void TacticsAnswer(int a) { AnswerSeen(a); }
float TacticsStayChance() { return 0.25f + 0.6f * AnswerThrough(); }
void TacticsOwnWall(int ent, int team)
{
	if (ent >= 0 && ent < (int)g_ent.size())
		g_ent[ent].team = team;
}
int TacticsMet(CBasePlayer* p) { return g_track[p->entindex()].met ? 1 + g_track[p->entindex()].answer : -1; }
void TacticsForgetMet(CBasePlayer* p)
{
	Track& t = g_track[p->entindex()];
	t.met = t.metDug = false;
	t.answer = -1;
}
// what it carries to build with: so many blocks of stone, or (negative) of planks
int TacticsBlocksOf(CBasePlayer* bot) { return Wooden(bot) ? -Blocks(bot) : Blocks(bot); }
// A wall put up with time in hand, and what its builder is to do afterwards: leave it, or stay behind it
bool TacticsWallThen(CBasePlayer* bot, int ent, bool leave)
{
	if (ent < 0 || ent >= (int)g_ent.size() || g_ent[ent].numCells < 1)
		return false;
	GiveWallAt(bot, g_ent[ent], "told to", true);
	Job& j = g_job[bot->entindex()];
	if (leave)
		j.after = AFTER_LEAVE;
	else if (j.after == AFTER_LEAVE)
		j.after = AFTER_NOTHING;
	return true;
}
bool TacticsWall(CBasePlayer* bot, int ent)
{
	if (ent < 0 || ent >= (int)g_ent.size() || g_ent[ent].numCells < 1)
		return false;
	GiveWallAt(bot, g_ent[ent], "told to");
	g_job[bot->entindex()].after = AFTER_NOTHING; // (tests move on at once, and the wall is nobody's)
	g_ent[ent].team = 0;
	return true;
}
// A firing position (or the high ground) watching a way in, somewhere on the site's side of it
bool TacticsPosition(CBasePlayer* bot, int ent, bool high, Vector* post)
{
	if (ent < 0 || ent >= (int)g_ent.size())
		return false;
	const Entrance& e = g_ent[ent];
	Vector watch = e.pos + Vector(0, 0, 40);
	PostPlan plan;
	if (!FindPost(high, e.pos, 220.0f, 800.0f, &e, watch, nullptr, bot, plan))
		return false;
	GivePosition(bot, plan, watch, high, "a way in, told to");
	*post = plan.post;
	return true;
}
bool TacticsCover(CBasePlayer* bot, CBasePlayer* from) { return QuickCover(bot, from); }
void TacticsNoWayAround(bool on) { g_testNoWayAround = on; }
void TacticsMood(int tactic, bool on)
{
	g_choice[CtxOf(TERRORIST, tactic)] = g_choice[CtxOf(CT, tactic)] = on ? 1 : 0;
}
int TacticsBreaches() { return (int)g_breach.size(); }
bool TacticsBreachInfo(int i, Vector* out, Vector* in)
{
	if (i < 0 || i >= (int)g_breach.size())
		return false;
	*out = g_breach[i].out;
	*in = g_breach[i].in;
	return true;
}
bool TacticsBreach(CBasePlayer* bot, int i, Vector* out, Vector* in)
{
	if (i < 0 || i >= (int)g_breach.size())
		return false;
	*out = g_breach[i].out;
	*in = g_breach[i].in;
	return GiveBreach(bot, i, "told to blow a way in");
}
int TacticsJob(CBasePlayer* bot) { return g_job[bot->entindex()].kind; }
bool TacticsPosted(CBasePlayer* bot) { return g_post[bot->entindex()].on; }
int TacticsWalled(int ent)
{
	if (ent < 0 || ent >= (int)g_ent.size())
		return -1;
	int placed = 0;
	for (int c = 0; c < g_ent[ent].numCells; c++)
		if (Placed(g_ent[ent].cells[c]))
			placed++;
	return placed;
}
void TacticsWallNow(int ent)
{
	// (a test's wall, without a builder)
	if (ent < 0 || ent >= (int)g_ent.size())
		return;
	for (int c = 0; c < g_ent[ent].numCells; c++)
		SetBlock(g_ent[ent].cells[c][0], g_ent[ent].cells[c][1], g_ent[ent].cells[c][2], mcw::MakeCell((uint16_t)mcw::FindBlock("stone"), 0));
}
} // namespace mc
