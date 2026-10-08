// Fire, by Minecraft's rules (a port of FireBlock.tick with its own odds). Flint and steel lights it
// (UseBlockTarget in mc_world_srv.cpp). A fire is a block of its own in an empty cell, aged 0 to 15, ticking
// every 1.5 to 2 seconds. Each tick it may burn out a neighbour (planks: 20 chances in 300 from the side,
// in 250 from below or above) and may set light to nearby air that touches something flammable (a couple of
// chances in a hundred per cell), so a fire takes hold slowly and a stack of wood burns for minutes. With
// nothing flammable beside it a fire dies: at once in mid-air, after a while on a floor.
// What burns: BF_FLAMMABLE blocks, TNT (primed), and on a classic map the crates and wooden doors
// (mcc::MAT_WOOD / MAT_DOOR cells, burning like planks; a door comes down as a whole, the way it does when
// dug). Standing in fire burns, and you stay alight for 8 seconds after stepping out.
#include "precompiled.h"

#include "mc_server.h"
#include "mc_blocks.h"
#include "mc_classic.h"

#include <vector>

namespace mc
{
extern mcc::Classic* ClassicWorld();                                 // mc_world_srv.cpp
extern void CarveCell(int x, int y, int z, CBasePlayer* by, bool drop);
extern bool HasFloor(int x, int y, int z);
extern void PrimeTnt(const float* origin, int fuse);                 // mc_entities.cpp

static cvar_t cv_fireSpeed = {"mc_fire_speed", "1", FCVAR_SERVER, 0.0f, nullptr}; // 1 = Minecraft's pace; 3 = three times as fast

static const float B2U = 40.0f;
static const int MAX_FLAMES = 256;
static const int nb6[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};

struct Flame
{
	int16_t x, y, z;
	int16_t age;  // FireBlock.AGE 0..15; -1 = gone
	float next;   // time of the next tick
	int owner;    // player index of whoever lit it (0 = nobody), for the kill
};
static std::vector<Flame> g_flames;
static int g_fireType = -1;

static Vector Center(int x, int y, int z)
{
	return Vector(g_world.origin[0] + (x + 0.5f) * B2U, g_world.origin[1] + (y + 0.5f) * B2U, g_world.origin[2] + (z + 0.5f) * B2U);
}

static bool IsFire(mcw::Cell c) { return c && (int)mcw::CellType(c) == g_fireType; }

// FireBlock.getFireTickDelay: 30 + nextInt(10) game ticks
static float TickDelay()
{
	float speed = cv_fireSpeed.value > 0.05f ? cv_fireSpeed.value : 1.0f;
	return (30 + RANDOM_LONG(0, 9)) / 20.0f / speed;
}

// FireBlock.setFlammable(block, igniteOdds, burnOdds): how readily fire catches beside it, and how readily
// it burns out
struct Odds
{
	int ignite, burn;
};
static Odds OddsOfBlock(uint16_t type)
{
	const mcw::BlockDef& d = mcw::Block(type);
	if (d.flags & mcw::BF_EXPLOSIVE)
		return {15, 100};
	bool classicWood = type >= mcw::DYN_BLOCK_BASE && d.sound == mcw::SOUND_WOOD; // a block of a crate or door texture
	if (!(d.flags & mcw::BF_FLAMMABLE) && !classicWood)
		return {0, 0};
	if (strstr(d.name, "leaves") || strstr(d.name, "wool"))
		return {30, 60};
	if (strstr(d.name, "_log"))
		return {5, 5};
	if (!strcmp(d.name, "bookshelf"))
		return {30, 20};
	if (!strcmp(d.name, "hay_block"))
		return {60, 20};
	if (!strcmp(d.name, "dead_bush"))
		return {60, 100};
	return {5, 20}; // planks, and what is made of them
}

// The odds of what is in the cell. A flame may sit in a cell the classic map's wood reaches into: that wood
// counts too.
static Odds OddsAt(int x, int y, int z)
{
	if (!g_world.InBounds(x, y, z))
		return {0, 0};
	mcw::Cell c = g_world.Get(x, y, z);
	mcc::Classic* cl = ClassicWorld();
	if (c && !IsFire(c) && !(cl && mcw::CellType(c) == cl->carvedType))
		return OddsOfBlock(mcw::CellType(c));
	if (cl && !cl->IsCarved(c) && cl->Diggable(x, y, z))
	{
		uint8_t m = cl->Info(x, y, z).material;
		if (m == mcc::MAT_WOOD || m == mcc::MAT_DOOR)
			return {5, 20}; // crates and doors burn like planks
	}
	return {0, 0};
}

// Room for a flame: air, a dug-out cell, or on a classic map a cell whose middle is in the open
static bool Empty(int x, int y, int z)
{
	if (!g_world.InBounds(x, y, z))
		return false;
	mcw::Cell c = g_world.Get(x, y, z);
	mcc::Classic* cl = ClassicWorld();
	if (c)
		return cl && mcw::CellType(c) == cl->carvedType;
	if (!cl)
		return true;
	Vector ctr = Center(x, y, z);
	float p[3] = {ctr.x, ctr.y, ctr.z};
	return cl->PointContents(p) != mcb::CONT_SOLID;
}

// FireBlock.isValidFireLocation: something flammable on any side
static bool HasFuel(int x, int y, int z)
{
	if (OddsAt(x, y, z).ignite > 0)
		return true;
	for (const auto& d : nb6)
		if (OddsAt(x + d[0], y + d[1], z + d[2]).ignite > 0)
			return true;
	return false;
}

// FireBlock.getIgniteOdds: for an empty cell, the best ignite odds among its neighbours
static int IgniteOdds(int x, int y, int z)
{
	if (!Empty(x, y, z))
		return 0;
	int best = OddsAt(x, y, z).ignite;
	for (const auto& d : nb6)
		best = max(best, OddsAt(x + d[0], y + d[1], z + d[2]).ignite);
	return best;
}

static bool Light(int x, int y, int z, int owner, int age)
{
	if (g_fireType < 0 || !Empty(x, y, z))
		return false;
	// a flame whose cell was just dug from under it (the wood it sat in burned) simply burns on
	bool have = false;
	for (const Flame& f : g_flames)
		have = have || (f.age >= 0 && f.x == x && f.y == y && f.z == z);
	if (!have && (int)g_flames.size() >= MAX_FLAMES)
		return false;
	mcc::Classic* cl = ClassicWorld();
	uint16_t keep = (cl && cl->IsCarved(g_world.Get(x, y, z))) ? mcc::CARVED_FLAG : 0;
	SetBlock(x, y, z, mcw::MakeCell((uint16_t)g_fireType, 0) | keep);
	if (!have)
		g_flames.push_back({(int16_t)x, (int16_t)y, (int16_t)z, (int16_t)age, gpGlobals->time + TickDelay(), owner});
	return true;
}

// What a block leaves behind when it is gone: the hole it was placed in, or nothing
static mcw::Cell Emptied(mcw::Cell was)
{
	mcc::Classic* cl = ClassicWorld();
	return (cl && (was & mcc::CARVED_FLAG)) ? mcw::MakeCell(cl->carvedType, 0) : 0;
}

// FireBlock.checkBurnOut: the cell (x,y,z) beside a fire of this age may burn out, leaving fire or nothing.
// own: the fire's own cell (classic wood reaching into it), where the fire stays.
static void BurnOut(int x, int y, int z, int chance, int age, int owner, bool own = false)
{
	if (RANDOM_LONG(0, chance - 1) >= OddsAt(x, y, z).burn)
		return;
	bool fire = own || RANDOM_LONG(0, age + 9) < 5; // the younger the fire, the likelier it moves in
	int newAge = min(age + RANDOM_LONG(0, 4) / 4, 15);
	mcw::Cell c = g_world.Get(x, y, z);
	mcc::Classic* cl = ClassicWorld();
	Vector ctr = Center(x, y, z);
	if (c && !IsFire(c))
	{
		const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
		if (d.shape == mcw::SHAPE_DOOR)
		{
			int other = (mcw::CellState(c) & 8) ? z - 1 : z + 1;
			mcw::Cell oc = g_world.Get(x, y, other);
			if (mcw::CellType(oc) == mcw::CellType(c))
				SetBlock(x, y, other, Emptied(oc));
		}
		SetBlock(x, y, z, Emptied(c));
		McLog("fire: %s burned out at %d %d %d", d.name, x, y, z);
		if (d.flags & mcw::BF_EXPLOSIVE)
		{
			PrimeTnt(ctr - Vector(0, 0, 20), 80); // TntBlock.explode
			return;
		}
		FxParticles(mcp::PK_BLOCK_BREAK, ctr, 24, c);
		if (fire)
			Light(x, y, z, owner, newAge);
		return;
	}
	if (!cl)
		return;
	bool door = cl->Info(x, y, z).material == mcc::MAT_DOOR;
	CarveCell(x, y, z, nullptr, false);
	McLog("fire: the classic map's %s burned out at %d %d %d", door ? "door" : "wood", x, y, z);
	if (fire)
		Light(x, y, z, owner, newAge);
}

// FireBlock.tick. False: the flame is gone.
static bool Tick(Flame& f)
{
	if (!IsFire(g_world.Get(f.x, f.y, f.z)))
		return false; // blown up, built over, the round restarted
	auto remove = [&]() {
		SetBlock(f.x, f.y, f.z, Emptied(g_world.Get(f.x, f.y, f.z)));
		return false;
	};
	int age = f.age;
	f.age = (int16_t)min(15, age + RANDOM_LONG(0, 2) / 2);
	if (!HasFuel(f.x, f.y, f.z))
	{
		// nothing to burn: gone at once in mid-air, after a while on a floor
		if (!HasFloor(f.x, f.y, f.z) || age > 3)
			return remove();
		return true;
	}
	if (age == 15 && RANDOM_LONG(0, 3) == 0 && OddsAt(f.x, f.y, f.z - 1).ignite == 0)
		return remove();
	BurnOut(f.x, f.y, f.z, 300, age, f.owner, true);
	for (int n = 0; n < 6; n++)
		BurnOut(f.x + nb6[n][0], f.y + nb6[n][1], f.z + nb6[n][2], n < 4 ? 300 : 250, age, f.owner);
	// spread: the air around (one cell out, one down, four up) that touches something flammable
	for (int dx = -1; dx <= 1; dx++)
		for (int dy = -1; dy <= 1; dy++)
			for (int dz = -1; dz <= 4; dz++)
			{
				if (!dx && !dy && !dz)
					continue;
				int chance = 100 + (dz > 1 ? (dz - 1) * 100 : 0);
				int odds = IgniteOdds(f.x + dx, f.y + dy, f.z + dz);
				if (odds <= 0)
					continue;
				int n = (odds + 40 + 2 * 7) / (age + 30); // difficulty normal
				if (n > 0 && RANDOM_LONG(0, chance - 1) <= n)
					Light(f.x + dx, f.y + dy, f.z + dz, f.owner, min(15, age + RANDOM_LONG(0, 4) / 4));
			}
	return IsFire(g_world.Get(f.x, f.y, f.z));
}

// Flint and steel on the cell (x,y,z) (BaseFireBlock.canBePlacedAt: on a floor, or against something that
// burns). False: no fire can be lit there.
bool FireLight(int x, int y, int z, CBasePlayer* by)
{
	if (g_fireType < 0)
		g_fireType = mcw::FindBlock("fire");
	if (!Empty(x, y, z) || !(HasFloor(x, y, z) || HasFuel(x, y, z)) || !Light(x, y, z, by ? by->entindex() : 0, 0))
		return false;
	McLog("fire: lit at %d %d %d by %s", x, y, z, by ? STRING(by->pev->netname) : "nobody");
	return true;
}

void FireInit() { CVAR_REGISTER(&cv_fireSpeed); }
void FireReset() { g_flames.clear(); }
int FireCount() { return (int)g_flames.size(); }

static int FlameOwnerAt(int x, int y, int z)
{
	for (const Flame& f : g_flames)
		if (f.x == x && f.y == y && f.z == z)
			return f.owner;
	return 0;
}

// Players in the flames burn (one Minecraft health point every half second) and stay alight for 8 seconds
// after stepping out (one every second), as in Minecraft. The kill goes to whoever lit the fire.
static void BurnPlayers()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* pl = UTIL_PlayerByIndex(i);
		if (!pl)
			continue;
		McPlayer& mp = P(i);
		if (!pl->IsAlive() || pl->pev->solid == SOLID_NOT || mp.fireUntil > gpGlobals->time + 20.0f)
		{
			mp.fireUntil = 0.0f;
			continue;
		}
		bool inFire = false;
		if (!g_flames.empty() && !mp.creative)
		{
			int lo[3], hi[3];
			float a[3] = {pl->pev->absmin.x + 2.0f, pl->pev->absmin.y + 2.0f, pl->pev->absmin.z + 2.0f};
			float b[3] = {pl->pev->absmax.x - 2.0f, pl->pev->absmax.y - 2.0f, pl->pev->absmax.z - 2.0f};
			g_world.ToBlock(a, lo);
			g_world.ToBlock(b, hi);
			for (int z = lo[2]; z <= hi[2] && !inFire; z++)
				for (int y = lo[1]; y <= hi[1] && !inFire; y++)
					for (int x = lo[0]; x <= hi[0] && !inFire; x++)
						if (g_world.InBounds(x, y, z) && IsFire(g_world.Get(x, y, z)))
						{
							inFire = true;
							mp.fireOwner = FlameOwnerAt(x, y, z);
						}
		}
		if (inFire)
			mp.fireUntil = gpGlobals->time + 8.0f;
		if (gpGlobals->time >= mp.fireUntil)
			continue;
		if (gpGlobals->time >= mp.fireFlames || mp.fireFlames > gpGlobals->time + 2.0f)
		{
			mp.fireFlames = gpGlobals->time + 0.2f;
			FxParticles(mcp::PK_FLAME, pl->pev->origin, 4);
		}
		if (gpGlobals->time < mp.fireNext && mp.fireNext < gpGlobals->time + 2.0f)
			continue;
		mp.fireNext = gpGlobals->time + (inFire ? 0.5f : 1.0f);
		CBasePlayer* owner = mp.fireOwner > 0 ? UTIL_PlayerByIndex(mp.fireOwner) : nullptr;
		entvars_t* world = VARS(INDEXENT(0));
		// Counter-Strike's kevlar is no help against fire (and would round a small burn down to nothing)
		float kevlar = pl->pev->armorvalue;
		pl->pev->armorvalue = 0.0f;
		pl->TakeDamage(world, owner ? owner->pev : world, 1.0f * mci::HP_PER_MC, DMG_BURN);
		pl->pev->armorvalue = kevlar;
	}
}

void FireFrame()
{
	if (!g_worldLoaded)
		return;
	if (g_fireType < 0)
		g_fireType = mcw::FindBlock("fire");
	BurnPlayers();
	if (g_flames.empty())
		return;
	// ticking a flame lights others (appended): walk by index, only the ones that were there
	size_t n = g_flames.size();
	for (size_t i = 0; i < n; i++)
	{
		if (gpGlobals->time < g_flames[i].next && g_flames[i].next < gpGlobals->time + 5.0f)
			continue;
		Flame f = g_flames[i]; // a copy: the tick may grow the list
		f.next = gpGlobals->time + TickDelay();
		if (!Tick(f))
			f.age = -1;
		g_flames[i] = f;
	}
	size_t w = 0;
	for (size_t i = 0; i < g_flames.size(); i++)
		if (g_flames[i].age >= 0)
			g_flames[w++] = g_flames[i];
	g_flames.resize(w);

	// crackle, sparks and smoke
	static float nextSound = 0.0f, nextPuff = 0.0f;
	if (!g_flames.empty() && (gpGlobals->time >= nextSound || nextSound > gpGlobals->time + 5.0f))
	{
		nextSound = gpGlobals->time + RANDOM_FLOAT(0.7f, 1.3f);
		const Flame& f = g_flames[RANDOM_LONG(0, (int)g_flames.size() - 1)];
		FxSound(mcs::MCS_FIRE_AMBIENT, Center(f.x, f.y, f.z), 1.0f + RANDOM_FLOAT(0.0f, 1.0f), 0.3f + RANDOM_FLOAT(0.0f, 0.7f));
	}
	if (!g_flames.empty() && (gpGlobals->time >= nextPuff || nextPuff > gpGlobals->time + 5.0f))
	{
		nextPuff = gpGlobals->time + 0.3f;
		for (int k = 0; k < 3; k++)
		{
			const Flame& f = g_flames[RANDOM_LONG(0, (int)g_flames.size() - 1)];
			Vector top = Center(f.x, f.y, f.z) + Vector(0, 0, 20);
			FxParticles(k ? mcp::PK_FLAME : mcp::PK_SMOKE, top, k ? 2 : 1);
		}
	}
}
} // namespace mc
