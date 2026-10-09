// Redstone (Java 1.21 rules, simplified where noted). Runs every redstone tick (2 game ticks, 0.1 s):
//  - sources: levers, buttons, pressure plates, lit redstone torches, powered repeaters, redstone blocks;
//  - a lever/button strongly powers the block it is attached to, a torch the block above it, a repeater
//    the block in front of it, a pressure plate the block below it;
//  - dust takes 15 from an adjacent source or strongly powered block and loses 1 per dust; it weakly powers
//    the block under it and the blocks it points into;
//  - lamps light, TNT ignites and doors open while powered; a torch goes out while the block it hangs
//    on is powered (one tick later); a repeater copies its input after 1-4 ticks.
// Not modelled: torch burnout, quasi-connectivity, comparators, observers.
#include "precompiled.h"

#include "mc_blocks.h"
#include "mc_classic.h"
#include "mc_server.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace mc
{
extern void PrimeTnt(const float* origin, int fuse);
bool ClassicMode();
mcc::Classic* ClassicWorld();

static const float RS_TICK = 0.1f;
static std::unordered_set<uint32_t> g_parts;          // cells that take part in redstone
static std::unordered_map<uint32_t, int> g_timer;      // buttons: ticks left pressed; repeaters: ticks the input has differed
static std::unordered_map<uint32_t, bool> g_doorPower; // doors: last powered state (edges toggle them)
static float g_nextTick = 0.0f;
static int g_types[16];                                  // block ids, see Types()
enum
{
	T_WIRE,
	T_TORCH,
	T_LEVER,
	T_STONE_BUTTON,
	T_OAK_BUTTON,
	T_STONE_PLATE,
	T_OAK_PLATE,
	T_REPEATER,
	T_LAMP,
	T_BLOCK,
	T_COUNT
};

static void Types()
{
	static const char* names[T_COUNT] = {"redstone_wire", "redstone_torch", "lever", "stone_button", "oak_button",
		"stone_pressure_plate", "oak_pressure_plate", "repeater", "redstone_lamp", "redstone_block"};
	for (int i = 0; i < T_COUNT; i++)
		g_types[i] = mcw::FindBlock(names[i]);
}

static inline uint32_t Idx(int x, int y, int z) { return (uint32_t)(((size_t)z * g_world.sy + y) * g_world.sx + x); }
static inline void Pos(uint32_t i, int& x, int& y, int& z)
{
	x = (int)(i % g_world.sx);
	y = (int)((i / g_world.sx) % g_world.sy);
	z = (int)(i / ((size_t)g_world.sx * g_world.sy));
}

static bool IsPart(mcw::Cell c)
{
	if (!c)
		return false;
	uint16_t t = mcw::CellType(c);
	const mcw::BlockDef& d = mcw::Block(t);
	return mcw::IsRedstoneShape(d.shape) || t == g_types[T_LAMP] || t == g_types[T_BLOCK] || (d.flags & mcw::BF_EXPLOSIVE) ||
		d.shape == mcw::SHAPE_DOOR;
}

void RedstoneRescan()
{
	Types();
	g_parts.clear();
	g_timer.clear();
	g_doorPower.clear();
	if (!g_worldLoaded)
		return;
	size_t n = (size_t)g_world.sx * g_world.sy * g_world.sz;
	for (size_t i = 0; i < n; i++)
		if (IsPart(g_world.cells[i]))
			g_parts.insert((uint32_t)i);
}

void RedstoneCellChanged(int x, int y, int z, mcw::Cell c)
{
	uint32_t i = Idx(x, y, z);
	if (IsPart(c))
		g_parts.insert(i);
	else
	{
		g_parts.erase(i);
		g_timer.erase(i);
		g_doorPower.erase(i);
	}
}

// ---------------------------------------------------------------------------------------------
// Queries on the current cells

static const int kDir[6][3] = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}; // 0..3 horizontal

static inline uint16_t TypeAt(int x, int y, int z) { return mcw::CellType(g_world.Get(x, y, z)); }
static inline uint16_t StateAt(int x, int y, int z) { return mcw::CellState(g_world.Get(x, y, z)) & 31; }
static inline bool Is(int x, int y, int z, int t) { return g_types[t] > 0 && TypeAt(x, y, z) == g_types[t]; }

// A block that carries power: a full opaque cube (stone, planks, lamps...). The classic map itself does not.
static bool Conductor(int x, int y, int z)
{
	mcw::Cell c = g_world.Get(x, y, z);
	if (!c)
		return false;
	const mcw::BlockDef& d = mcw::Block(mcw::CellType(c));
	return d.shape == mcw::SHAPE_CUBE && !(d.flags & mcw::BF_TRANSPARENT) && mcw::CellType(c) != g_types[T_BLOCK];
}

static void AttachPos(int x, int y, int z, int out[3])
{
	uint16_t t = TypeAt(x, y, z);
	int a[3];
	mcw::AttachVec(mcw::AttachOf(mcw::Block(t).shape, StateAt(x, y, z)), a);
	out[0] = x + a[0];
	out[1] = y + a[1];
	out[2] = z + a[2];
}

static bool TorchLit(int x, int y, int z) { return Is(x, y, z, T_TORCH) && !(StateAt(x, y, z) & 8); }
static bool LeverOn(int x, int y, int z) { return Is(x, y, z, T_LEVER) && (StateAt(x, y, z) & 8); }
static bool ButtonOn(int x, int y, int z) { return (Is(x, y, z, T_STONE_BUTTON) || Is(x, y, z, T_OAK_BUTTON)) && (StateAt(x, y, z) & 8); }
static bool PlateOn(int x, int y, int z) { return (Is(x, y, z, T_STONE_PLATE) || Is(x, y, z, T_OAK_PLATE)) && (StateAt(x, y, z) & 1); }
static int RepeaterFacing(int x, int y, int z) { return StateAt(x, y, z) & 3; }
static bool RepeaterOn(int x, int y, int z) { return Is(x, y, z, T_REPEATER) && (StateAt(x, y, z) & 16); }

// Does the part at (sx,sy,sz) send power into the neighbouring cell (tx,ty,tz)? (sources only)
static bool SourcePowers(int sx, int sy, int sz, int tx, int ty, int tz)
{
	if (Is(sx, sy, sz, T_BLOCK) || LeverOn(sx, sy, sz) || ButtonOn(sx, sy, sz) || PlateOn(sx, sy, sz))
		return true;
	if (TorchLit(sx, sy, sz))
	{
		int a[3];
		AttachPos(sx, sy, sz, a);
		return !(a[0] == tx && a[1] == ty && a[2] == tz); // not into the block it hangs on
	}
	if (RepeaterOn(sx, sy, sz))
	{
		const int* f = kDir[RepeaterFacing(sx, sy, sz)];
		return sx + f[0] == tx && sy + f[1] == ty && sz + f[2] == tz;
	}
	return false;
}

// Dust connects towards horizontal direction d (for drawing and for where it points)?
static bool DustConnects(int x, int y, int z, int d)
{
	int nx = x + kDir[d][0], ny = y + kDir[d][1];
	if (Is(nx, ny, z, T_WIRE))
		return true;
	uint16_t t = TypeAt(nx, ny, z);
	if (t && (t == g_types[T_TORCH] || t == g_types[T_LEVER] || t == g_types[T_STONE_BUTTON] || t == g_types[T_OAK_BUTTON] ||
				 t == g_types[T_STONE_PLATE] || t == g_types[T_OAK_PLATE] || t == g_types[T_BLOCK]))
		return true;
	if (t && t == g_types[T_REPEATER])
		return (RepeaterFacing(nx, ny, z) & 1) == (d & 1); // only along its axis
	// stepping up onto a block, or down off one
	if (!Conductor(x, y, z + 1) && Is(nx, ny, z + 1, T_WIRE))
		return true;
	if (!Conductor(nx, ny, z) && Is(nx, ny, z - 1, T_WIRE))
		return true;
	return false;
}

// Horizontal directions the dust points into (powers): its connections; a lone dot is a cross; a single
// connection makes a straight line through.
static int DustPoints(int x, int y, int z)
{
	int mask = 0;
	for (int d = 0; d < 4; d++)
		if (DustConnects(x, y, z, d))
			mask |= 1 << d;
	if (mask == 0)
		return 15;
	if (mask == 1 || mask == 2 || mask == 4 || mask == 8)
	{
		int d = mask == 1 ? 0 : mask == 2 ? 1 : mask == 4 ? 2 : 3;
		mask |= 1 << ((d + 2) & 3);
	}
	return mask;
}

// ---------------------------------------------------------------------------------------------
// The tick

struct Change
{
	int x, y, z;
	mcw::Cell c;
};

static mcw::Cell WithState(int x, int y, int z, uint16_t st)
{
	mcw::Cell old = g_world.Get(x, y, z);
	return (mcw::Cell)(mcw::MakeCell(mcw::CellType(old), st & 31) | (old & mcc::CARVED_FLAG));
}

static mcw::Cell Emptied(mcw::Cell was)
{
	mcc::Classic* cl = ClassicWorld();
	return (cl && (was & mcc::CARVED_FLAG)) ? mcw::MakeCell(cl->carvedType, 0) : 0;
}

static Vector Center(int x, int y, int z)
{
	return Vector(g_world.origin[0] + (x + 0.5f) * 40.0f, g_world.origin[1] + (y + 0.5f) * 40.0f, g_world.origin[2] + (z + 0.5f) * 40.0f);
}

static bool PlatePressedNow(int x, int y, int z, bool wooden)
{
	Vector mn(g_world.origin[0] + x * 40.0f + 2.5f, g_world.origin[1] + y * 40.0f + 2.5f, g_world.origin[2] + z * 40.0f - 4.0f);
	Vector mx = mn + Vector(35.0f, 35.0f, 44.0f); // the plate sits on the surface somewhere in the cell
	CBaseEntity* list[32];
	int n = UTIL_EntitiesInBox(list, 32, mn, mx, 0);
	for (int i = 0; i < n; i++)
	{
		CBaseEntity* e = list[i];
		if (!e || e->pev->solid == SOLID_NOT && !e->IsPlayer())
			continue;
		if (e->IsPlayer())
		{
			if (e->IsAlive())
				return true;
			continue;
		}
		if (wooden && FClassnameIs(e->pev, "mc_item"))
			return true; // dropped items press wooden plates
	}
	return false;
}

static void Tick()
{
	if (g_parts.empty())
		return;
	std::vector<uint32_t> parts(g_parts.begin(), g_parts.end());
	std::unordered_map<uint32_t, int> strong, weak, dust;

	// 1. strong power from sources into conductors
	for (uint32_t i : parts)
	{
		int x, y, z;
		Pos(i, x, y, z);
		int t[3] = {x, y, z};
		if (LeverOn(x, y, z) || ButtonOn(x, y, z))
			AttachPos(x, y, z, t);
		else if (TorchLit(x, y, z))
			t[2] = z + 1;
		else if (RepeaterOn(x, y, z))
		{
			const int* f = kDir[RepeaterFacing(x, y, z)];
			t[0] += f[0];
			t[1] += f[1];
		}
		else if (PlateOn(x, y, z))
			t[2] = z - 1;
		else
			continue;
		if (Conductor(t[0], t[1], t[2]))
			strong[Idx(t[0], t[1], t[2])] = 15;
	}

	// 2. dust levels: 15 next to a source or a strongly powered block, then minus one per dust
	std::vector<uint32_t> bucket[16];
	for (uint32_t i : parts)
	{
		int x, y, z;
		Pos(i, x, y, z);
		if (!Is(x, y, z, T_WIRE))
			continue;
		int in = 0;
		for (int d = 0; d < 6 && in < 15; d++)
		{
			int nx = x + kDir[d][0], ny = y + kDir[d][1], nz = z + kDir[d][2];
			if (!g_world.InBounds(nx, ny, nz))
				continue;
			if (SourcePowers(nx, ny, nz, x, y, z))
				in = 15;
			else if (Conductor(nx, ny, nz) && strong.count(Idx(nx, ny, nz)))
				in = 15;
		}
		dust[i] = in;
		if (in > 0)
			bucket[in].push_back(i);
	}
	for (int lvl = 15; lvl >= 2; lvl--)
		for (size_t k = 0; k < bucket[lvl].size(); k++)
		{
			uint32_t i = bucket[lvl][k];
			if (dust[i] != lvl)
				continue;
			int x, y, z;
			Pos(i, x, y, z);
			for (int d = 0; d < 4; d++)
			{
				int nx = x + kDir[d][0], ny = y + kDir[d][1];
				int cand[3][3] = {{nx, ny, z}, {nx, ny, z + 1}, {nx, ny, z - 1}};
				for (int c = 0; c < 3; c++)
				{
					int cx = cand[c][0], cy = cand[c][1], cz = cand[c][2];
					if (!Is(cx, cy, cz, T_WIRE))
						continue;
					if (c == 1 && Conductor(x, y, z + 1))
						continue; // a block above cuts the step up
					if (c == 2 && Conductor(nx, ny, z))
						continue; // a block beside cuts the step down
					uint32_t j = Idx(cx, cy, cz);
					auto it = dust.find(j);
					if (it != dust.end() && it->second < lvl - 1)
					{
						it->second = lvl - 1;
						bucket[lvl - 1].push_back(j);
					}
				}
			}
		}

	// 3. weak power from dust into the block under it and the blocks it points into
	for (auto& kv : dust)
	{
		if (kv.second <= 0)
			continue;
		int x, y, z;
		Pos(kv.first, x, y, z);
		if (Conductor(x, y, z - 1))
		{
			int& w = weak[Idx(x, y, z - 1)];
			w = w > kv.second ? w : kv.second;
		}
		int pts = DustPoints(x, y, z);
		for (int d = 0; d < 4; d++)
			if ((pts & (1 << d)) && Conductor(x + kDir[d][0], y + kDir[d][1], z))
			{
				int& w = weak[Idx(x + kDir[d][0], y + kDir[d][1], z)];
				w = w > kv.second ? w : kv.second;
			}
	}

	auto blockPowered = [&](int x, int y, int z) {
		if (!g_world.InBounds(x, y, z))
			return false;
		uint32_t i = Idx(x, y, z);
		return strong.count(i) > 0 || weak.count(i) > 0;
	};
	// is the cell (x,y,z) fed by any neighbour (source, dust pointing at it, powered block)?
	auto fed = [&](int x, int y, int z) {
		for (int d = 0; d < 6; d++)
		{
			int nx = x + kDir[d][0], ny = y + kDir[d][1], nz = z + kDir[d][2];
			if (!g_world.InBounds(nx, ny, nz))
				continue;
			if (SourcePowers(nx, ny, nz, x, y, z))
				return true;
			if (Is(nx, ny, nz, T_WIRE))
			{
				auto it = dust.find(Idx(nx, ny, nz));
				if (it == dust.end() || it->second <= 0)
					continue;
				if (d == 4)
					return true; // dust on top
				if (d < 4 && (DustPoints(nx, ny, nz) & (1 << ((d + 2) & 3))))
					return true;
				continue;
			}
			if (Conductor(nx, ny, nz) && blockPowered(nx, ny, nz))
				return true;
		}
		return false;
	};

	// parts that lost what held them up pop off and drop (dust on a block that got mined, ...)
	for (uint32_t i : parts)
	{
		int x, y, z;
		Pos(i, x, y, z);
		if (mcw::IsRedstoneShape(g_world.ShapeAt(x, y, z)) && !PartSupported(x, y, z))
		{
			McLog("redstone: %s at %d %d %d lost its support", mcw::Block(TypeAt(x, y, z)).name, x, y, z);
			BreakBlock(x, y, z, nullptr, true);
		}
	}
	parts.assign(g_parts.begin(), g_parts.end());

	// 4. new states
	std::vector<Change> changes;
	for (uint32_t i : parts)
	{
		int x, y, z;
		Pos(i, x, y, z);
		mcw::Cell c = g_world.Get(x, y, z);
		uint16_t t = mcw::CellType(c), st = mcw::CellState(c) & 31;
		const mcw::BlockDef& d = mcw::Block(t);
		if (t == g_types[T_WIRE])
		{
			int p = dust[i];
			if ((st & 15) != p)
				changes.push_back({x, y, z, WithState(x, y, z, (uint16_t)((st & ~15) | p))});
		}
		else if (t == g_types[T_LAMP])
		{
			bool on = fed(x, y, z) || blockPowered(x, y, z);
			if (on != ((st & 1) != 0))
				changes.push_back({x, y, z, WithState(x, y, z, on ? 1 : 0)});
		}
		else if (d.flags & mcw::BF_EXPLOSIVE)
		{
			if (fed(x, y, z) || blockPowered(x, y, z))
			{
				Vector o = Center(x, y, z) - Vector(0, 0, 20);
				changes.push_back({x, y, z, Emptied(c)});
				PrimeTntBy(o, 80, BlockPlacer(x, y, z)); // (whoever set it down: his blast, his kills)
				McLog("redstone: TNT ignited at %d %d %d", x, y, z);
			}
		}
		else if (t == g_types[T_TORCH])
		{
			int a[3];
			AttachPos(x, y, z, a);
			bool lit = !blockPowered(a[0], a[1], a[2]);
			if (lit == ((st & 8) != 0))
				changes.push_back({x, y, z, WithState(x, y, z, (uint16_t)(lit ? (st & ~8) : (st | 8)))});
		}
		else if (t == g_types[T_REPEATER])
		{
			int f = st & 3;
			int bx = x - kDir[f][0], by = y - kDir[f][1];
			bool in = false;
			if (g_world.InBounds(bx, by, z))
			{
				if (SourcePowers(bx, by, z, x, y, z))
					in = true;
				else if (Is(bx, by, z, T_WIRE))
				{
					auto it = dust.find(Idx(bx, by, z));
					in = it != dust.end() && it->second > 0;
				}
				else if (Conductor(bx, by, z) && blockPowered(bx, by, z))
					in = true;
			}
			bool on = (st & 16) != 0;
			if (in != on)
			{
				int delay = ((st >> 2) & 3) + 1;
				if (++g_timer[i] >= delay)
				{
					g_timer[i] = 0;
					changes.push_back({x, y, z, WithState(x, y, z, (uint16_t)(in ? (st | 16) : (st & ~16)))});
				}
			}
			else
				g_timer[i] = 0;
		}
		else if (t == g_types[T_STONE_BUTTON] || t == g_types[T_OAK_BUTTON])
		{
			if ((st & 8) && --g_timer[i] <= 0)
			{
				g_timer.erase(i);
				changes.push_back({x, y, z, WithState(x, y, z, (uint16_t)(st & ~8))});
				bool oak = t == g_types[T_OAK_BUTTON];
				FxSound(oak ? mcs::MCS_WOOD_BUTTON_OFF : mcs::MCS_STONE_BUTTON_OFF, Center(x, y, z), 1.0f, 1.0f);
			}
		}
		else if (t == g_types[T_STONE_PLATE] || t == g_types[T_OAK_PLATE])
		{
			bool oak = t == g_types[T_OAK_PLATE];
			bool on = PlatePressedNow(x, y, z, oak);
			if (on != ((st & 1) != 0))
			{
				changes.push_back({x, y, z, WithState(x, y, z, on ? 1 : 0)});
				int snd = oak ? (on ? mcs::MCS_WOOD_PLATE_ON : mcs::MCS_WOOD_PLATE_OFF) : (on ? mcs::MCS_STONE_PLATE_ON : mcs::MCS_STONE_PLATE_OFF);
				FxSound(snd, Center(x, y, z), 1.0f, 1.0f);
			}
		}
		else if (d.shape == mcw::SHAPE_DOOR && !(st & 8))
		{
			// lower half decides for both; an edge in power opens/closes it, players can still use it
			bool on = fed(x, y, z) || fed(x, y, z + 1);
			auto it = g_doorPower.find(i);
			bool was = it != g_doorPower.end() && it->second;
			if (on != was)
			{
				g_doorPower[i] = on;
				bool open = (st & 4) != 0;
				if (open != on)
				{
					extern void ToggleDoor(int x, int y, int z);
					ToggleDoor(x, y, z);
				}
			}
		}
	}
	for (const Change& ch : changes)
		SetBlock(ch.x, ch.y, ch.z, ch.c);
}

void RedstoneFrame()
{
	if (!g_worldLoaded)
		return;
	if (gpGlobals->time < g_nextTick && g_nextTick - gpGlobals->time < 1.0f)
		return;
	g_nextTick = gpGlobals->time + RS_TICK;
	Tick();
}

// Right-click on a redstone part (unless sneaking). Returns true if it was one.
bool RedstoneUse(CBasePlayer* pl, int x, int y, int z)
{
	if (!g_worldLoaded)
		return false;
	mcw::Cell c = g_world.Get(x, y, z);
	uint16_t t = mcw::CellType(c), st = mcw::CellState(c) & 31;
	if (!t)
		return false;
	if (t == g_types[T_LEVER])
	{
		bool on = !(st & 8);
		SetBlock(x, y, z, WithState(x, y, z, (uint16_t)(on ? (st | 8) : (st & ~8))));
		FxSound(mcs::MCS_LEVER_CLICK, Center(x, y, z), 0.3f, on ? 0.6f : 0.5f);
		McLog("redstone: lever %s at %d %d %d", on ? "on" : "off", x, y, z);
		return true;
	}
	if (t == g_types[T_STONE_BUTTON] || t == g_types[T_OAK_BUTTON])
	{
		if (!(st & 8))
		{
			bool oak = t == g_types[T_OAK_BUTTON];
			SetBlock(x, y, z, WithState(x, y, z, (uint16_t)(st | 8)));
			g_timer[Idx(x, y, z)] = oak ? 15 : 10; // 30 / 20 game ticks
			FxSound(oak ? mcs::MCS_WOOD_BUTTON_ON : mcs::MCS_STONE_BUTTON_ON, Center(x, y, z), 1.0f, 1.0f);
			McLog("redstone: button pressed at %d %d %d", x, y, z);
		}
		return true;
	}
	if (t == g_types[T_REPEATER])
	{
		uint16_t delay = (uint16_t)((((st >> 2) & 3) + 1) & 3);
		SetBlock(x, y, z, WithState(x, y, z, (uint16_t)((st & ~12) | (delay << 2))));
		FxSound(mcs::MCS_LEVER_CLICK, Center(x, y, z), 0.3f, 0.55f);
		return true;
	}
	return false;
}
} // namespace mc
