// Network protocol shared by the server game DLL and the client proxy.
#pragma once
#include <stdint.h>

namespace mcp
{
// ---------------------------------------------------------------------------------------------
// User messages (server -> client). Names are at most 11 characters.
#define MCMSG_INV "McInv"     // hotbar + armor, see WriteInv/ReadInv
#define MCMSG_STAT "McStat"   // xp level/progress, armor points, mode
#define MCMSG_FX "McFx"       // effects: sounds, particles
#define MCMSG_VOX "McVox"     // voxel block changes
#define MCMSG_BREAK "McBreak" // block mining crack overlay
#define MCMSG_HELLO "McHello" // map/world info + server settings
#define MCMSG_TOAST "McToast" // Minecraft-style chat/actionbar text: byte kind, string
#define MCMSG_INVMAIN "McInvM" // survival inventory: 27 storage slots, 4 craft slots, craft result, cursor

enum FxType : uint8_t
{
	FX_SOUND = 1,     // short sound id, coord x y z, byte volume*200, byte pitch*100, short entindex (0 = static)
	FX_PARTICLES = 2, // byte kind, coord x y z, byte count, short data (block type, colour, ...)
	FX_HURT = 3,      // short entindex: red damage tint
	FX_DEATH = 4,     // short entindex, coord x y z: death poof
	FX_EXPLOSION = 5, // coord x y z, byte power*10
	FX_FIREWORK = 6,  // coord x y z, byte shape, long colour
	FX_TOTEM = 7,     // short entindex
	FX_LEVELUP = 8,   // short entindex
	FX_SWING = 9,     // short entindex (arm swing of a player holding an MC item)
};

enum ParticleKind : uint8_t
{
	PK_CRIT = 1,
	PK_ENCHANT_CRIT,
	PK_SWEEP,
	PK_BLOCK_BREAK,  // data = block cell
	PK_BLOCK_HIT,    // data = block cell
	PK_POOF,
	PK_SMOKE,
	PK_FIREWORK_SPARK, // data = rgb565 colour
	PK_HEART,
	PK_DAMAGE,
	PK_TOTEM,
	PK_XP_SPLASH,
	PK_PORTAL,         // enderman teleport / ender pearl
};

// ---------------------------------------------------------------------------------------------
// Entity markers: non-player entities rendered by the client's Minecraft renderer carry
//   iuser4 = MCE_MAGIC << 24 | kind << 16 | data (16 bits)
// in their entity state; the engine-side model is invisible.
static const int MCE_MAGIC = 0x6D;
enum EntKind : uint8_t
{
	MCE_XPORB = 1,     // data = orb size index (0..10)
	MCE_ITEM = 2,      // data = item id; entity skin = stack count
	MCE_FIREWORK = 3,  // data = 0
	MCE_ARROW = 4,
	MCE_PEARL = 5,
	MCE_TNT = 6,       // data = fuse ticks left
	MCE_FALLING = 7,   // data = block cell
	MCE_XPBOTTLE = 8,
};
inline int MakeEntMarker(int kind, int data) { return (MCE_MAGIC << 24) | ((kind & 0xFF) << 16) | (data & 0xFFFF); }
inline bool IsMcEnt(int iuser4) { return ((iuser4 >> 24) & 0xFF) == MCE_MAGIC; }
inline int EntKindOf(int iuser4) { return (iuser4 >> 16) & 0xFF; }
inline int EntDataOf(int iuser4) { return iuser4 & 0xFFFF; }

// ---------------------------------------------------------------------------------------------
// Player flags in pev->iuser4 (sent in both clientdata and player entity state).
// Bit 0 is Counter-Strike's own "on train" flag - preserve it.
static const int MCPF_TRAIN = 1 << 0;
static const int MCPF_ELYTRA = 1 << 8;    // wearing an elytra
static const int MCPF_GLIDING = 1 << 9;   // currently gliding
static const int MCPF_MCITEM = 1 << 10;   // holding the Minecraft item "weapon" (client draws the MC hand)
static const int MCPF_CREATIVE = 1 << 11; // creative mode
static const int MCPF_FLYING = 1 << 12;   // creative flight active
static const int MCPF_ARMOR = 1 << 13;    // wearing any Minecraft armor piece
static const int MCPF_XBOW_LOADED = 1 << 14; // the held crossbow is loaded
static const int MCPF_USING = 1 << 15;       // drawing a bow / charging a crossbow / eating
static const int MCPF_SWELL_SHIFT = 1;     // creeper fuse in ticks (0..30), bits 1-5
static const int MCPF_SWELL_MASK = 0x1F << MCPF_SWELL_SHIFT;
inline int SwellOf(int iuser4) { return (iuser4 & MCPF_SWELL_MASK) >> MCPF_SWELL_SHIFT; }
// bits 16..31: item id held (for third-person rendering of other players)
inline int HeldItemOf(int iuser4) { return (iuser4 >> 16) & 0xFFFF; }

// Firework boost: remaining boost time in seconds stored in pev->fuser4? No - fuser4 is CS long-jump
// cooldown. We use pev->vuser1[0] (sent in clientdata, see delta.lst) for boost time remaining.

// ---------------------------------------------------------------------------------------------
// Client -> server console commands
//   mc_select <1-9>        select hotbar slot (6-9 are Minecraft slots; 1-5 mirror CS weapon slots)
//   mc_give <item> [count] creative give
//   mc_menu                open creative inventory (server text menu)
//   mc_drop                drop the held Minecraft item
//   mc_swap <slot>         swap held item into the given hotbar slot

static const int HOTBAR_SIZE = 9;
static const int FIRST_MC_SLOT = 5; // first slot filled with Minecraft items by default (all 9 hold anything)
} // namespace mcp
