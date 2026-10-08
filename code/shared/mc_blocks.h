// Global block registry shared by client and server. World files store block NAMES in a palette and
// are remapped to these ids at load time, so ids can change between builds without breaking maps.
#pragma once
#include "mc_world.h"

namespace mcw
{
// Texture slots per face, by name of a PNG in assets/minecraft/textures/block/ (without .png).
struct BlockDef
{
	const char* name;       // Minecraft id without the "minecraft:" prefix
	ShapeKind shape;
	const char* texTop;     // +Z face
	const char* texSide;    // horizontal faces
	const char* texBottom;  // -Z face
	float hardness;         // Minecraft hardness (seconds-ish scale; -1 = unbreakable)
	uint8_t tool;           // preferred tool: see TOOL_* below
	uint8_t sound;          // step/break sound group: see SOUND_* below
	uint8_t flags;          // BF_* below
	const char* drop;       // item dropped when mined (nullptr = itself)
};

enum : uint8_t
{
	TOOL_NONE = 0,
	TOOL_PICKAXE,
	TOOL_AXE,
	TOOL_SHOVEL,
};

enum : uint8_t
{
	SOUND_STONE = 0,
	SOUND_WOOD,
	SOUND_SAND,
	SOUND_GRAVEL,
	SOUND_GRASS,
	SOUND_GLASS,
	SOUND_METAL,
	SOUND_WOOL,
};

enum : uint8_t
{
	BF_TRANSPARENT = 1, // does not hide neighbouring faces (glass, leaves, doors, slabs...)
	BF_ALPHATEST = 2,   // render with alpha test (glass, bars, plants)
	BF_EMISSIVE = 4,    // glowstone etc.
	BF_FALLS = 8,       // sand/gravel: falls when unsupported
	BF_EXPLOSIVE = 16,  // TNT
};

// Registry, index = block type id. Entry 0 is air.
extern const BlockDef g_blocks[];
extern const int g_numBlocks;
// ShapeKind per type id, sized 1024 for World::shapeOfType.
extern uint8_t g_shapeOfType[1024];

// Blocks registered at map load (classic mode: one per texture of the classic map). Their type ids
// start at DYN_BLOCK_BASE; both DLLs register the same list in the same order, so ids agree.
static const int DYN_BLOCK_BASE = 900;
static const int MAX_DYN_BLOCKS = 1024 - DYN_BLOCK_BASE;
extern BlockDef g_dynBlocks[MAX_DYN_BLOCKS];
extern int g_numDynBlocks;
int RegisterDynamicBlock(const BlockDef& def); // returns the type id, -1 if full; strings are copied
void ClearDynamicBlocks();
// Texture marker for dynamic blocks drawn with a classic map texture: "#bsp:<miptex index>"
int BspTextureOf(const char* tex); // miptex index, or -1 for a regular Minecraft texture

void InitBlockRegistry();
int FindBlock(const char* name); // -1 if unknown
inline const BlockDef& Block(uint16_t type)
{
	if (type >= DYN_BLOCK_BASE && type < DYN_BLOCK_BASE + g_numDynBlocks)
		return g_dynBlocks[type - DYN_BLOCK_BASE];
	return g_blocks[type < (uint16_t)g_numBlocks ? type : 0];
}
} // namespace mcw
