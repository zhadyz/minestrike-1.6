#include "mc_blocks.h"

#include <stdlib.h>
#include <string.h>
#include <string>

#include <string.h>

namespace mcw
{
// clang-format off
const BlockDef g_blocks[] = {
	// name                     shape         top                         side                        bottom                      hard  tool          sound         flags                       drop
	{ "air",                    SHAPE_NONE,   nullptr,                    nullptr,                    nullptr,                    0.0f, TOOL_NONE,    SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "bedrock",                SHAPE_CUBE,   "bedrock",                  "bedrock",                  "bedrock",                 -1.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "stone",                  SHAPE_CUBE,   "stone",                    "stone",                    "stone",                    1.5f, TOOL_PICKAXE, SOUND_STONE,  0,                          "cobblestone" },
	{ "cobblestone",            SHAPE_CUBE,   "cobblestone",              "cobblestone",              "cobblestone",              2.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "stone_bricks",           SHAPE_CUBE,   "stone_bricks",             "stone_bricks",             "stone_bricks",             1.5f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "sandstone",              SHAPE_CUBE,   "sandstone_top",            "sandstone",                "sandstone_bottom",         0.8f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "cut_sandstone",          SHAPE_CUBE,   "sandstone_top",            "cut_sandstone",            "sandstone_top",            0.8f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "smooth_sandstone",       SHAPE_CUBE,   "sandstone_top",            "sandstone_top",            "sandstone_top",            2.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "chiseled_sandstone",     SHAPE_CUBE,   "sandstone_top",            "chiseled_sandstone",       "sandstone_top",            0.8f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "sand",                   SHAPE_CUBE,   "sand",                     "sand",                     "sand",                     0.5f, TOOL_SHOVEL,  SOUND_SAND,   BF_FALLS,                   nullptr },
	{ "red_sand",               SHAPE_CUBE,   "red_sand",                 "red_sand",                 "red_sand",                 0.5f, TOOL_SHOVEL,  SOUND_SAND,   BF_FALLS,                   nullptr },
	{ "gravel",                 SHAPE_CUBE,   "gravel",                   "gravel",                   "gravel",                   0.6f, TOOL_SHOVEL,  SOUND_GRAVEL, BF_FALLS,                   nullptr },
	{ "dirt",                   SHAPE_CUBE,   "dirt",                     "dirt",                     "dirt",                     0.5f, TOOL_SHOVEL,  SOUND_GRAVEL, 0,                          nullptr },
	{ "grass_block",            SHAPE_CUBE,   "grass_block_top",          "grass_block_side",         "dirt",                     0.6f, TOOL_SHOVEL,  SOUND_GRASS,  0,                          "dirt" },
	{ "oak_planks",             SHAPE_CUBE,   "oak_planks",               "oak_planks",               "oak_planks",               2.0f, TOOL_AXE,     SOUND_WOOD,   BF_FLAMMABLE,               nullptr },
	{ "spruce_planks",          SHAPE_CUBE,   "spruce_planks",            "spruce_planks",            "spruce_planks",            2.0f, TOOL_AXE,     SOUND_WOOD,   BF_FLAMMABLE,               nullptr },
	{ "dark_oak_planks",        SHAPE_CUBE,   "dark_oak_planks",          "dark_oak_planks",          "dark_oak_planks",          2.0f, TOOL_AXE,     SOUND_WOOD,   BF_FLAMMABLE,               nullptr },
	{ "oak_log",                SHAPE_CUBE,   "oak_log_top",              "oak_log",                  "oak_log_top",              2.0f, TOOL_AXE,     SOUND_WOOD,   BF_FLAMMABLE,               nullptr },
	{ "spruce_log",             SHAPE_CUBE,   "spruce_log_top",           "spruce_log",               "spruce_log_top",           2.0f, TOOL_AXE,     SOUND_WOOD,   BF_FLAMMABLE,               nullptr },
	{ "bricks",                 SHAPE_CUBE,   "bricks",                   "bricks",                   "bricks",                   2.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "terracotta",             SHAPE_CUBE,   "terracotta",               "terracotta",               "terracotta",               1.25f,TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "white_terracotta",       SHAPE_CUBE,   "white_terracotta",         "white_terracotta",         "white_terracotta",         1.25f,TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "orange_terracotta",      SHAPE_CUBE,   "orange_terracotta",        "orange_terracotta",        "orange_terracotta",        1.25f,TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "yellow_terracotta",      SHAPE_CUBE,   "yellow_terracotta",        "yellow_terracotta",        "yellow_terracotta",        1.25f,TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "brown_terracotta",       SHAPE_CUBE,   "brown_terracotta",         "brown_terracotta",         "brown_terracotta",         1.25f,TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "light_gray_terracotta",  SHAPE_CUBE,   "light_gray_terracotta",    "light_gray_terracotta",    "light_gray_terracotta",    1.25f,TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "smooth_stone",           SHAPE_CUBE,   "smooth_stone",             "smooth_stone",             "smooth_stone",             2.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "andesite",               SHAPE_CUBE,   "andesite",                 "andesite",                 "andesite",                 1.5f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "polished_andesite",      SHAPE_CUBE,   "polished_andesite",        "polished_andesite",        "polished_andesite",        1.5f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "iron_block",             SHAPE_CUBE,   "iron_block",               "iron_block",               "iron_block",               5.0f, TOOL_PICKAXE, SOUND_METAL,  0,                          nullptr },
	{ "glass",                  SHAPE_CUBE,   "glass",                    "glass",                    "glass",                    0.3f, TOOL_NONE,    SOUND_GLASS,  BF_TRANSPARENT | BF_ALPHATEST, "" },
	{ "iron_bars",              SHAPE_PANE,   "iron_bars",                "iron_bars",                "iron_bars",                5.0f, TOOL_PICKAXE, SOUND_METAL,  BF_TRANSPARENT | BF_ALPHATEST, nullptr },
	{ "oak_door",               SHAPE_DOOR,   "oak_door_top",             "oak_door_bottom",          "oak_door_bottom",          3.0f, TOOL_AXE,     SOUND_WOOD,   BF_TRANSPARENT | BF_ALPHATEST | BF_FLAMMABLE, nullptr },
	{ "spruce_door",            SHAPE_DOOR,   "spruce_door_top",          "spruce_door_bottom",       "spruce_door_bottom",       3.0f, TOOL_AXE,     SOUND_WOOD,   BF_TRANSPARENT | BF_ALPHATEST | BF_FLAMMABLE, nullptr },
	{ "iron_door",              SHAPE_DOOR,   "iron_door_top",            "iron_door_bottom",         "iron_door_bottom",         5.0f, TOOL_PICKAXE, SOUND_METAL,  BF_TRANSPARENT | BF_ALPHATEST, nullptr },
	{ "barrel",                 SHAPE_CUBE,   "barrel_top",               "barrel_side",              "barrel_bottom",            2.5f, TOOL_AXE,     SOUND_WOOD,   BF_FLAMMABLE,               nullptr },
	{ "crafting_table",         SHAPE_CUBE,   "crafting_table_top",       "crafting_table_front",     "oak_planks",               2.5f, TOOL_AXE,     SOUND_WOOD,   BF_FLAMMABLE,               nullptr },
	{ "bookshelf",              SHAPE_CUBE,   "oak_planks",               "bookshelf",                "oak_planks",               1.5f, TOOL_AXE,     SOUND_WOOD,   BF_FLAMMABLE,               nullptr },
	{ "tnt",                    SHAPE_CUBE,   "tnt_top",                  "tnt_side",                 "tnt_bottom",               0.0f, TOOL_NONE,    SOUND_GRASS,  BF_EXPLOSIVE,               nullptr },
	{ "coal_ore",               SHAPE_CUBE,   "coal_ore",                 "coal_ore",                 "coal_ore",                 3.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          "coal" },
	{ "iron_ore",               SHAPE_CUBE,   "iron_ore",                 "iron_ore",                 "iron_ore",                 3.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          "raw_iron" },
	{ "gold_ore",               SHAPE_CUBE,   "gold_ore",                 "gold_ore",                 "gold_ore",                 3.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          "raw_gold" },
	{ "diamond_ore",            SHAPE_CUBE,   "diamond_ore",              "diamond_ore",              "diamond_ore",              3.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          "diamond" },
	{ "emerald_ore",            SHAPE_CUBE,   "emerald_ore",              "emerald_ore",              "emerald_ore",              3.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          "emerald" },
	{ "sandstone_slab",         SHAPE_SLAB,   "sandstone_top",            "sandstone",                "sandstone_bottom",         2.0f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "smooth_sandstone_slab",  SHAPE_SLAB,   "sandstone_top",            "sandstone_top",            "sandstone_top",            2.0f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "cut_sandstone_slab",     SHAPE_SLAB,   "sandstone_top",            "cut_sandstone",            "sandstone_top",            2.0f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "stone_brick_slab",       SHAPE_SLAB,   "stone_bricks",             "stone_bricks",             "stone_bricks",             2.0f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "oak_slab",               SHAPE_SLAB,   "oak_planks",               "oak_planks",               "oak_planks",               2.0f, TOOL_AXE,     SOUND_WOOD,   BF_TRANSPARENT | BF_FLAMMABLE, nullptr },
	{ "spruce_slab",            SHAPE_SLAB,   "spruce_planks",            "spruce_planks",            "spruce_planks",            2.0f, TOOL_AXE,     SOUND_WOOD,   BF_TRANSPARENT | BF_FLAMMABLE, nullptr },
	{ "smooth_stone_slab",      SHAPE_SLAB,   "smooth_stone",             "smooth_stone_slab_side",   "smooth_stone",             2.0f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "cobblestone_slab",       SHAPE_SLAB,   "cobblestone",              "cobblestone",              "cobblestone",              2.0f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "sandstone_stairs",       SHAPE_STAIRS, "sandstone_top",            "sandstone",                "sandstone_bottom",         0.8f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "oak_stairs",             SHAPE_STAIRS, "oak_planks",               "oak_planks",               "oak_planks",               2.0f, TOOL_AXE,     SOUND_WOOD,   BF_TRANSPARENT | BF_FLAMMABLE, nullptr },
	{ "stone_brick_stairs",     SHAPE_STAIRS, "stone_bricks",             "stone_bricks",             "stone_bricks",             1.5f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "glowstone",              SHAPE_CUBE,   "glowstone",                "glowstone",                "glowstone",                0.3f, TOOL_NONE,    SOUND_GLASS,  BF_EMISSIVE,                nullptr },
	{ "dead_bush",              SHAPE_CROSS,  "dead_bush",                "dead_bush",                "dead_bush",                0.0f, TOOL_NONE,    SOUND_GRASS,  BF_TRANSPARENT | BF_ALPHATEST | BF_FLAMMABLE, "" },
	{ "cactus",                 SHAPE_CUBE,   "cactus_top",               "cactus_side",              "cactus_bottom",            0.4f, TOOL_NONE,    SOUND_WOOL,   0,                          nullptr },
	{ "hay_block",              SHAPE_CUBE,   "hay_block_top",            "hay_block_side",           "hay_block_top",            0.5f, TOOL_NONE,    SOUND_GRASS,  BF_FLAMMABLE,               nullptr },
	{ "white_wool",             SHAPE_CUBE,   "white_wool",               "white_wool",               "white_wool",               0.8f, TOOL_NONE,    SOUND_WOOL,   BF_FLAMMABLE,               nullptr },
	{ "cobweb",                 SHAPE_CROSS,  "cobweb",                   "cobweb",                   "cobweb",                   4.0f, TOOL_NONE,    SOUND_WOOL,   BF_TRANSPARENT | BF_ALPHATEST, "" },
	{ "oak_leaves",             SHAPE_CUBE,   "oak_leaves",               "oak_leaves",               "oak_leaves",               0.2f, TOOL_NONE,    SOUND_GRASS,  BF_TRANSPARENT | BF_ALPHATEST | BF_FLAMMABLE, "" },
	{ "deepslate",              SHAPE_CUBE,   "deepslate_top",            "deepslate",                "deepslate_top",            3.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "deepslate_diamond_ore",  SHAPE_CUBE,   "deepslate_diamond_ore",    "deepslate_diamond_ore",    "deepslate_diamond_ore",    4.5f, TOOL_PICKAXE, SOUND_STONE,  0,                          "diamond" },
	{ "obsidian",               SHAPE_CUBE,   "obsidian",                 "obsidian",                 "obsidian",                50.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          nullptr },
	{ "gold_block",             SHAPE_CUBE,   "gold_block",               "gold_block",               "gold_block",               3.0f, TOOL_PICKAXE, SOUND_METAL,  0,                          nullptr },
	{ "diamond_block",          SHAPE_CUBE,   "diamond_block",            "diamond_block",            "diamond_block",            5.0f, TOOL_PICKAXE, SOUND_METAL,  0,                          nullptr },
	{ "redstone_ore",           SHAPE_CUBE,   "redstone_ore",             "redstone_ore",             "redstone_ore",             3.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          "redstone" },
	{ "lapis_ore",              SHAPE_CUBE,   "lapis_ore",                "lapis_ore",                "lapis_ore",                3.0f, TOOL_PICKAXE, SOUND_STONE,  0,                          "lapis_lazuli" },
	// classic mode: a cell of the classic map that has been dug out (empty, never drawn)
	{ "carved",                 SHAPE_NONE,   "stone",                    "stone",                    "stone",                   -1.0f, TOOL_NONE,    SOUND_STONE,  BF_TRANSPARENT,             "" },
	// redstone (mc_redstone.cpp). Textures for other states (lit lamp, unlit torch, powered repeater,
	// dust lines) are looked up by the renderer.
	{ "redstone_wire",          SHAPE_DUST,   "redstone_dust_dot",        "redstone_dust_line0",      "redstone_dust_line1",      0.0f, TOOL_NONE,    SOUND_STONE,  BF_TRANSPARENT | BF_ALPHATEST, "redstone" },
	{ "redstone_torch",         SHAPE_TORCH,  "redstone_torch",           "redstone_torch",           "redstone_torch_off",       0.0f, TOOL_NONE,    SOUND_WOOD,   BF_TRANSPARENT | BF_ALPHATEST | BF_EMISSIVE, nullptr },
	{ "lever",                  SHAPE_LEVER,  "lever",                    "cobblestone",              "cobblestone",              0.5f, TOOL_NONE,    SOUND_STONE,  BF_TRANSPARENT | BF_ALPHATEST, nullptr },
	{ "stone_button",           SHAPE_BUTTON, "stone",                    "stone",                    "stone",                    0.5f, TOOL_NONE,    SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "oak_button",             SHAPE_BUTTON, "oak_planks",               "oak_planks",               "oak_planks",               0.5f, TOOL_NONE,    SOUND_WOOD,   BF_TRANSPARENT,             nullptr },
	{ "stone_pressure_plate",   SHAPE_PLATE,  "stone",                    "stone",                    "stone",                    0.5f, TOOL_PICKAXE, SOUND_STONE,  BF_TRANSPARENT,             nullptr },
	{ "oak_pressure_plate",     SHAPE_PLATE,  "oak_planks",               "oak_planks",               "oak_planks",               0.5f, TOOL_AXE,     SOUND_WOOD,   BF_TRANSPARENT,             nullptr },
	{ "repeater",               SHAPE_REPEATER,"repeater",                "smooth_stone",             "smooth_stone",             0.0f, TOOL_NONE,    SOUND_STONE,  BF_TRANSPARENT | BF_ALPHATEST, nullptr },
	{ "redstone_lamp",          SHAPE_CUBE,   "redstone_lamp",            "redstone_lamp",            "redstone_lamp",            0.3f, TOOL_NONE,    SOUND_GLASS,  0,                          nullptr },
	{ "redstone_block",         SHAPE_CUBE,   "redstone_block",           "redstone_block",           "redstone_block",           5.0f, TOOL_PICKAXE, SOUND_METAL,  0,                          nullptr },
	// fire (mc_fire.cpp): lit with flint and steel, eats BF_FLAMMABLE blocks and the classic map's crates and doors
	// a plain torch: light level 14, stands on a floor or leans from a wall, pops off when what holds it goes
	{ "torch",                  SHAPE_TORCH,  "torch",                    "torch",                    "torch",                    0.0f, TOOL_NONE,    SOUND_WOOD,   BF_TRANSPARENT | BF_ALPHATEST | BF_EMISSIVE, nullptr },
	{ "fire",                   SHAPE_FIRE,   "fire_0",                   "fire_0",                   "fire_1",                   0.0f, TOOL_NONE,    SOUND_WOOL,   BF_TRANSPARENT | BF_ALPHATEST | BF_EMISSIVE, "" },
};
// clang-format on
const int g_numBlocks = (int)(sizeof(g_blocks) / sizeof(g_blocks[0]));
uint8_t g_shapeOfType[1024];

BlockDef g_dynBlocks[MAX_DYN_BLOCKS];
int g_numDynBlocks = 0;
static std::string g_dynStrings[MAX_DYN_BLOCKS][5];

void InitBlockRegistry()
{
	memset(g_shapeOfType, 0, sizeof(g_shapeOfType));
	for (int i = 0; i < g_numBlocks; i++)
		g_shapeOfType[i] = (uint8_t)g_blocks[i].shape;
	for (int i = 0; i < g_numDynBlocks; i++)
		g_shapeOfType[DYN_BLOCK_BASE + i] = (uint8_t)g_dynBlocks[i].shape;
}

int RegisterDynamicBlock(const BlockDef& def)
{
	if (g_numDynBlocks >= MAX_DYN_BLOCKS)
		return -1;
	int i = g_numDynBlocks++;
	std::string* str = g_dynStrings[i];
	str[0] = def.name ? def.name : "";
	str[1] = def.texTop ? def.texTop : "";
	str[2] = def.texSide ? def.texSide : "";
	str[3] = def.texBottom ? def.texBottom : "";
	str[4] = def.drop ? def.drop : "";
	g_dynBlocks[i] = def;
	g_dynBlocks[i].name = str[0].c_str();
	g_dynBlocks[i].texTop = str[1].c_str();
	g_dynBlocks[i].texSide = str[2].c_str();
	g_dynBlocks[i].texBottom = str[3].c_str();
	g_dynBlocks[i].drop = def.drop ? str[4].c_str() : nullptr;
	g_shapeOfType[DYN_BLOCK_BASE + i] = (uint8_t)def.shape;
	return DYN_BLOCK_BASE + i;
}

void ClearDynamicBlocks()
{
	for (int i = 0; i < g_numDynBlocks; i++)
		g_shapeOfType[DYN_BLOCK_BASE + i] = 0;
	g_numDynBlocks = 0;
}

int BspTextureOf(const char* tex)
{
	if (!tex || strncmp(tex, "#bsp:", 5))
		return -1;
	return atoi(tex + 5);
}

int FindBlock(const char* name)
{
	for (int i = 0; i < g_numBlocks; i++)
		if (!strcmp(g_blocks[i].name, name))
			return i;
	for (int i = 0; i < g_numDynBlocks; i++)
		if (!strcmp(g_dynBlocks[i].name, name))
			return DYN_BLOCK_BASE + i;
	return -1;
}
} // namespace mcw
