#include "mc_items.h"

#include <math.h>
#include <string.h>
#include <string>

namespace mci
{
// clang-format off
#define TOOL(n, d, t, dmg, spd, mine, dur, rar) { n, d, t, n, 1, dmg, spd, mine, 0, 0, 0.f, 0.f, dur, nullptr, nullptr, rar }
#define ARMOR(n, d, slot, pts, tough, kb, dur, layer, rar) { n, d, IT_ARMOR, n, 1, 1.f, 4.f, 1.f, slot, pts, tough, kb, dur, nullptr, layer, rar }
#define BLOCKITEM(n, d) { n, d, IT_BLOCK, nullptr, 64, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, n, nullptr, 0 }
#define SIMPLE(n, d, t, stack, rar) { n, d, t, n, stack, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, nullptr, nullptr, rar }

const ItemDef g_items[] = {
	{ "air", "Air", IT_NONE, nullptr, 0, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, nullptr, nullptr, 0 },

	TOOL("wooden_sword",     "Wooden Sword",     IT_SWORD,   4.f, 1.6f, 2.f,  59,  0),
	TOOL("stone_sword",      "Stone Sword",      IT_SWORD,   5.f, 1.6f, 4.f,  131, 0),
	TOOL("iron_sword",       "Iron Sword",       IT_SWORD,   6.f, 1.6f, 6.f,  250, 0),
	TOOL("golden_sword",     "Golden Sword",     IT_SWORD,   4.f, 1.6f, 12.f, 32,  0),
	TOOL("diamond_sword",    "Diamond Sword",    IT_SWORD,   7.f, 1.6f, 8.f,  1561,0),
	TOOL("netherite_sword",  "Netherite Sword",  IT_SWORD,   8.f, 1.6f, 9.f,  2031,0),
	TOOL("wooden_axe",       "Wooden Axe",       IT_AXE,     7.f, 0.8f, 2.f,  59,  0),
	TOOL("stone_axe",        "Stone Axe",        IT_AXE,     9.f, 0.8f, 4.f,  131, 0),
	TOOL("iron_axe",         "Iron Axe",         IT_AXE,     9.f, 0.9f, 6.f,  250, 0),
	TOOL("golden_axe",       "Golden Axe",       IT_AXE,     7.f, 1.0f, 12.f, 32,  0),
	TOOL("diamond_axe",      "Diamond Axe",      IT_AXE,     9.f, 1.0f, 8.f,  1561,0),
	TOOL("netherite_axe",    "Netherite Axe",    IT_AXE,    10.f, 1.0f, 9.f,  2031,0),
	TOOL("wooden_pickaxe",   "Wooden Pickaxe",   IT_PICKAXE, 2.f, 1.2f, 2.f,  59,  0),
	TOOL("stone_pickaxe",    "Stone Pickaxe",    IT_PICKAXE, 3.f, 1.2f, 4.f,  131, 0),
	TOOL("iron_pickaxe",     "Iron Pickaxe",     IT_PICKAXE, 4.f, 1.2f, 6.f,  250, 0),
	TOOL("golden_pickaxe",   "Golden Pickaxe",   IT_PICKAXE, 2.f, 1.2f, 12.f, 32,  0),
	TOOL("diamond_pickaxe",  "Diamond Pickaxe",  IT_PICKAXE, 5.f, 1.2f, 8.f,  1561,0),
	TOOL("netherite_pickaxe","Netherite Pickaxe",IT_PICKAXE, 6.f, 1.2f, 9.f,  2031,0),
	TOOL("wooden_shovel",    "Wooden Shovel",    IT_SHOVEL,  2.5f,1.0f, 2.f,  59,  0),
	TOOL("stone_shovel",     "Stone Shovel",     IT_SHOVEL,  3.5f,1.0f, 4.f,  131, 0),
	TOOL("iron_shovel",      "Iron Shovel",      IT_SHOVEL,  4.5f,1.0f, 6.f,  250, 0),
	TOOL("golden_shovel",    "Golden Shovel",    IT_SHOVEL,  2.5f,1.0f, 12.f, 32,  0),
	TOOL("diamond_shovel",   "Diamond Shovel",   IT_SHOVEL,  5.5f,1.0f, 8.f,  1561,0),
	TOOL("netherite_shovel", "Netherite Shovel", IT_SHOVEL,  6.5f,1.0f, 9.f,  2031,0),
	TOOL("mace",             "Mace",             IT_MACE,    6.f, 0.6f, 1.f,  500, 3),

	ARMOR("leather_helmet",       "Leather Cap",          SLOT_HEAD,  1, 0.f, 0.f,  55,  "leather", 0),
	ARMOR("leather_chestplate",   "Leather Tunic",        SLOT_CHEST, 3, 0.f, 0.f,  80,  "leather", 0),
	ARMOR("leather_leggings",     "Leather Pants",        SLOT_LEGS,  2, 0.f, 0.f,  75,  "leather", 0),
	ARMOR("leather_boots",        "Leather Boots",        SLOT_FEET,  1, 0.f, 0.f,  65,  "leather", 0),
	ARMOR("chainmail_helmet",     "Chainmail Helmet",     SLOT_HEAD,  2, 0.f, 0.f,  165, "chainmail", 0),
	ARMOR("chainmail_chestplate", "Chainmail Chestplate", SLOT_CHEST, 5, 0.f, 0.f,  240, "chainmail", 0),
	ARMOR("chainmail_leggings",   "Chainmail Leggings",   SLOT_LEGS,  4, 0.f, 0.f,  225, "chainmail", 0),
	ARMOR("chainmail_boots",      "Chainmail Boots",      SLOT_FEET,  1, 0.f, 0.f,  195, "chainmail", 0),
	ARMOR("iron_helmet",          "Iron Helmet",          SLOT_HEAD,  2, 0.f, 0.f,  165, "iron", 0),
	ARMOR("iron_chestplate",      "Iron Chestplate",      SLOT_CHEST, 6, 0.f, 0.f,  240, "iron", 0),
	ARMOR("iron_leggings",        "Iron Leggings",        SLOT_LEGS,  5, 0.f, 0.f,  225, "iron", 0),
	ARMOR("iron_boots",           "Iron Boots",           SLOT_FEET,  2, 0.f, 0.f,  195, "iron", 0),
	ARMOR("golden_helmet",        "Golden Helmet",        SLOT_HEAD,  2, 0.f, 0.f,  77,  "gold", 0),
	ARMOR("golden_chestplate",    "Golden Chestplate",    SLOT_CHEST, 5, 0.f, 0.f,  112, "gold", 0),
	ARMOR("golden_leggings",      "Golden Leggings",      SLOT_LEGS,  3, 0.f, 0.f,  105, "gold", 0),
	ARMOR("golden_boots",         "Golden Boots",         SLOT_FEET,  1, 0.f, 0.f,  91,  "gold", 0),
	ARMOR("diamond_helmet",       "Diamond Helmet",       SLOT_HEAD,  3, 2.f, 0.f,  363, "diamond", 0),
	ARMOR("diamond_chestplate",   "Diamond Chestplate",   SLOT_CHEST, 8, 2.f, 0.f,  528, "diamond", 0),
	ARMOR("diamond_leggings",     "Diamond Leggings",     SLOT_LEGS,  6, 2.f, 0.f,  495, "diamond", 0),
	ARMOR("diamond_boots",        "Diamond Boots",        SLOT_FEET,  3, 2.f, 0.f,  429, "diamond", 0),
	ARMOR("netherite_helmet",     "Netherite Helmet",     SLOT_HEAD,  3, 3.f, 0.1f, 407, "netherite", 0),
	ARMOR("netherite_chestplate", "Netherite Chestplate", SLOT_CHEST, 8, 3.f, 0.1f, 592, "netherite", 0),
	ARMOR("netherite_leggings",   "Netherite Leggings",   SLOT_LEGS,  6, 3.f, 0.1f, 555, "netherite", 0),
	ARMOR("netherite_boots",      "Netherite Boots",      SLOT_FEET,  3, 3.f, 0.1f, 481, "netherite", 0),
	ARMOR("turtle_helmet",        "Turtle Shell",         SLOT_HEAD,  2, 0.f, 0.f,  275, "turtle_scute", 0),

	{ "elytra", "Elytra", IT_ELYTRA, "elytra", 1, 1.f, 4.f, 1.f, SLOT_CHEST, 0, 0.f, 0.f, 432, nullptr, nullptr, 1 },
	SIMPLE("firework_rocket",          "Firework Rocket",          IT_FIREWORK,   64, 0),
	SIMPLE("totem_of_undying",         "Totem of Undying",         IT_TOTEM,      1,  1),
	SIMPLE("golden_apple",             "Golden Apple",             IT_FOOD,       64, 2),
	{ "enchanted_golden_apple", "Enchanted Golden Apple", IT_FOOD, "golden_apple", 64, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, nullptr, nullptr, 3 },
	SIMPLE("cooked_beef",              "Steak",                    IT_FOOD,       64, 0),
	SIMPLE("cooked_porkchop",          "Cooked Porkchop",          IT_FOOD,       64, 0),
	SIMPLE("cooked_chicken",           "Cooked Chicken",           IT_FOOD,       64, 0),
	SIMPLE("bread",                    "Bread",                    IT_FOOD,       64, 0),
	SIMPLE("baked_potato",             "Baked Potato",             IT_FOOD,       64, 0),
	SIMPLE("apple",                    "Apple",                    IT_FOOD,       64, 0),
	SIMPLE("carrot",                   "Carrot",                   IT_FOOD,       64, 0),
	SIMPLE("golden_carrot",            "Golden Carrot",            IT_FOOD,       64, 0),
	SIMPLE("cookie",                   "Cookie",                   IT_FOOD,       64, 0),
	SIMPLE("bow",                      "Bow",                      IT_BOW,        1,  0),
	{ "crossbow", "Crossbow", IT_CROSSBOW, "crossbow_standby", 1, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, nullptr, nullptr, 0 },
	SIMPLE("arrow",                    "Arrow",                    IT_ARROW,      64, 0),
	SIMPLE("ender_pearl",              "Ender Pearl",              IT_PEARL,      16, 0),
	SIMPLE("experience_bottle",        "Bottle o' Enchanting",     IT_XP_BOTTLE,  64, 1),
	SIMPLE("flint_and_steel",          "Flint and Steel",          IT_FLINT_STEEL,1,  0),
	SIMPLE("diamond",                  "Diamond",                  IT_MATERIAL,   64, 0),
	SIMPLE("emerald",                  "Emerald",                  IT_MATERIAL,   64, 0),
	SIMPLE("coal",                     "Coal",                     IT_MATERIAL,   64, 0),
	SIMPLE("raw_iron",                 "Raw Iron",                 IT_MATERIAL,   64, 0),
	SIMPLE("raw_gold",                 "Raw Gold",                 IT_MATERIAL,   64, 0),
	SIMPLE("gunpowder",                "Gunpowder",                IT_MATERIAL,   64, 0),
	SIMPLE("rotten_flesh",             "Rotten Flesh",             IT_MATERIAL,   64, 0),
	SIMPLE("lapis_lazuli",             "Lapis Lazuli",             IT_MATERIAL,   64, 0),
	// redstone: block items with a flat icon (texture) where Minecraft draws one
	{ "redstone", "Redstone Dust", IT_BLOCK, "redstone", 64, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, "redstone_wire", nullptr, 0 },
	{ "redstone_torch", "Redstone Torch", IT_BLOCK, "block/redstone_torch", 64, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, "redstone_torch", nullptr, 0 },
	{ "lever", "Lever", IT_BLOCK, "block/lever", 64, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, "lever", nullptr, 0 },
	{ "repeater", "Redstone Repeater", IT_BLOCK, "repeater", 64, 1.f, 4.f, 1.f, 0, 0, 0.f, 0.f, 0, "repeater", nullptr, 0 },
	BLOCKITEM("stone_button", "Stone Button"),
	BLOCKITEM("oak_button", "Oak Button"),
	BLOCKITEM("stone_pressure_plate", "Stone Pressure Plate"),
	BLOCKITEM("oak_pressure_plate", "Oak Pressure Plate"),
	BLOCKITEM("redstone_lamp", "Redstone Lamp"),
	BLOCKITEM("redstone_block", "Block of Redstone"),

	BLOCKITEM("stone", "Stone"),
	BLOCKITEM("cobblestone", "Cobblestone"),
	BLOCKITEM("stone_bricks", "Stone Bricks"),
	BLOCKITEM("sandstone", "Sandstone"),
	BLOCKITEM("cut_sandstone", "Cut Sandstone"),
	BLOCKITEM("smooth_sandstone", "Smooth Sandstone"),
	BLOCKITEM("chiseled_sandstone", "Chiseled Sandstone"),
	BLOCKITEM("sand", "Sand"),
	BLOCKITEM("red_sand", "Red Sand"),
	BLOCKITEM("gravel", "Gravel"),
	BLOCKITEM("dirt", "Dirt"),
	BLOCKITEM("grass_block", "Grass Block"),
	BLOCKITEM("oak_planks", "Oak Planks"),
	BLOCKITEM("spruce_planks", "Spruce Planks"),
	BLOCKITEM("dark_oak_planks", "Dark Oak Planks"),
	BLOCKITEM("oak_log", "Oak Log"),
	BLOCKITEM("spruce_log", "Spruce Log"),
	BLOCKITEM("bricks", "Bricks"),
	BLOCKITEM("terracotta", "Terracotta"),
	BLOCKITEM("white_terracotta", "White Terracotta"),
	BLOCKITEM("orange_terracotta", "Orange Terracotta"),
	BLOCKITEM("yellow_terracotta", "Yellow Terracotta"),
	BLOCKITEM("smooth_stone", "Smooth Stone"),
	BLOCKITEM("andesite", "Andesite"),
	BLOCKITEM("iron_block", "Block of Iron"),
	BLOCKITEM("gold_block", "Block of Gold"),
	BLOCKITEM("diamond_block", "Block of Diamond"),
	BLOCKITEM("glass", "Glass"),
	BLOCKITEM("iron_bars", "Iron Bars"),
	BLOCKITEM("oak_door", "Oak Door"),
	BLOCKITEM("spruce_door", "Spruce Door"),
	BLOCKITEM("iron_door", "Iron Door"),
	BLOCKITEM("barrel", "Barrel"),
	BLOCKITEM("crafting_table", "Crafting Table"),
	BLOCKITEM("bookshelf", "Bookshelf"),
	BLOCKITEM("tnt", "TNT"),
	BLOCKITEM("obsidian", "Obsidian"),
	BLOCKITEM("glowstone", "Glowstone"),
	BLOCKITEM("white_wool", "White Wool"),
	BLOCKITEM("hay_block", "Hay Bale"),
	BLOCKITEM("cactus", "Cactus"),
	BLOCKITEM("sandstone_slab", "Sandstone Slab"),
	BLOCKITEM("oak_slab", "Oak Slab"),
	BLOCKITEM("sandstone_stairs", "Sandstone Stairs"),
	BLOCKITEM("oak_stairs", "Oak Stairs"),
	BLOCKITEM("diamond_ore", "Diamond Ore"),
	BLOCKITEM("oak_leaves", "Oak Leaves"),
	BLOCKITEM("deepslate", "Deepslate"),
	BLOCKITEM("bedrock", "Bedrock"),
	BLOCKITEM("cobweb", "Cobweb"),
	BLOCKITEM("dead_bush", "Dead Bush"),
};
// clang-format on
const int g_numItems = (int)(sizeof(g_items) / sizeof(g_items[0]));

ItemDef g_dynItems[MAX_DYN_ITEMS];
int g_numDynItems = 0;
static std::string g_dynItemStrings[MAX_DYN_ITEMS][4];

int RegisterDynamicItem(const ItemDef& def)
{
	if (g_numDynItems >= MAX_DYN_ITEMS)
		return -1;
	int i = g_numDynItems++;
	std::string* str = g_dynItemStrings[i];
	str[0] = def.name ? def.name : "";
	str[1] = def.display ? def.display : "";
	str[2] = def.texture ? def.texture : "";
	str[3] = def.blockName ? def.blockName : "";
	g_dynItems[i] = def;
	g_dynItems[i].name = str[0].c_str();
	g_dynItems[i].display = str[1].c_str();
	g_dynItems[i].texture = def.texture ? str[2].c_str() : nullptr;
	g_dynItems[i].blockName = def.blockName ? str[3].c_str() : nullptr;
	return DYN_ITEM_BASE + i;
}

void ClearDynamicItems() { g_numDynItems = 0; }

int ItemTotal() { return (g_numItems - 1) + g_numDynItems; }

int ItemIdAt(int index)
{
	if (index < g_numItems - 1)
		return index + 1;
	return DYN_ITEM_BASE + (index - (g_numItems - 1));
}

int FindItem(const char* name)
{
	if (!name)
		return -1;
	if (!strncmp(name, "minecraft:", 10))
		name += 10;
	for (int i = 1; i < g_numItems; i++)
		if (!strcmp(g_items[i].name, name))
			return i;
	for (int i = 0; i < g_numDynItems; i++)
		if (!strcmp(g_dynItems[i].name, name))
			return DYN_ITEM_BASE + i;
	return -1;
}

float ArmorReduce(float damage, float armor, float toughness)
{
	// CombatRules.getDamageAfterAbsorb (Java 1.21)
	float t = 2.0f + toughness / 4.0f;
	float eff = armor - damage / t;
	if (eff < armor * 0.2f)
		eff = armor * 0.2f;
	if (eff > 20.0f)
		eff = 20.0f;
	return damage * (1.0f - eff / 25.0f);
}

int XpToNext(int level)
{
	if (level >= 30)
		return 112 + (level - 30) * 9;
	if (level >= 15)
		return 37 + (level - 15) * 5;
	return 7 + level * 2;
}

int XpTotalAtLevel(int level)
{
	int total = 0;
	for (int l = 0; l < level; l++)
		total += XpToNext(l);
	return total;
}

int XpOrbValue(int v)
{
	if (v >= 2477) return 2477;
	if (v >= 1237) return 1237;
	if (v >= 617) return 617;
	if (v >= 307) return 307;
	if (v >= 149) return 149;
	if (v >= 73) return 73;
	if (v >= 37) return 37;
	if (v >= 17) return 17;
	if (v >= 7) return 7;
	if (v >= 3) return 3;
	return 1;
}

float BreakSeconds(float hardness, float toolSpeed, bool correctTool, bool canHarvest)
{
	if (hardness < 0.0f)
		return -1.0f; // unbreakable
	if (hardness == 0.0f)
		return 0.0f;
	// Block.getDestroyProgress: speed / hardness / (canHarvest ? 30 : 100) per tick
	float speed = correctTool ? toolSpeed : 1.0f;
	float perTick = speed / hardness / (canHarvest ? 30.0f : 100.0f);
	if (perTick >= 1.0f)
		return 0.0f; // instamine
	float ticks = ceilf(1.0f / perTick);
	return ticks * TICK;
}
} // namespace mci
