// Shared item registry (client + server). Numbers follow Minecraft Java 1.21 where they exist.
#pragma once
#include <stdint.h>

namespace mci
{
enum ItemType : uint8_t
{
	IT_NONE = 0,
	IT_SWORD,
	IT_AXE,
	IT_PICKAXE,
	IT_SHOVEL,
	IT_MACE,
	IT_ARMOR,
	IT_ELYTRA,
	IT_FIREWORK,
	IT_BLOCK,
	IT_FOOD,
	IT_TOTEM,
	IT_BOW,
	IT_ARROW,
	IT_PEARL,
	IT_XP_BOTTLE,
	IT_FLINT_STEEL,
	IT_MATERIAL,
	IT_CROSSBOW,
};

enum ArmorSlot : uint8_t
{
	SLOT_HEAD = 0,
	SLOT_CHEST,
	SLOT_LEGS,
	SLOT_FEET,
	NUM_ARMOR_SLOTS
};

struct ItemDef
{
	const char* name;        // minecraft id without prefix, e.g. "diamond_sword"
	const char* display;     // "Diamond Sword"
	ItemType type;
	const char* texture;     // item texture (textures/item/<texture>.png), nullptr for block items
	uint8_t maxStack;
	float attackDamage;      // Minecraft attack damage (hearts*2); 1 = fist
	float attackSpeed;       // attacks per second at full charge (Minecraft "attack speed")
	float miningSpeed;       // tool efficiency (1 = hand)
	uint8_t armorSlot;       // for IT_ARMOR/IT_ELYTRA
	uint8_t armorPoints;
	float toughness;
	float knockbackResist;
	int durability;          // 0 = unbreakable
	const char* blockName;   // for IT_BLOCK: block registry name
	const char* armorTexture;// equipment texture layer (textures/entity/equipment/<...>.png) for worn armor
	uint8_t rarity;          // 0 common (white), 1 uncommon (yellow), 2 rare (aqua), 3 epic (light purple)
};

extern const ItemDef g_items[];
extern const int g_numItems;

// Items registered at map load (classic mode: a block item per classic texture), ids from DYN_ITEM_BASE.
static const int DYN_ITEM_BASE = 2000;
static const int MAX_DYN_ITEMS = 256;
extern ItemDef g_dynItems[MAX_DYN_ITEMS];
extern int g_numDynItems;
int RegisterDynamicItem(const ItemDef& def); // strings copied; returns the item id or -1
void ClearDynamicItems();
inline bool ValidItem(int id) { return (id > 0 && id < g_numItems) || (id >= DYN_ITEM_BASE && id < DYN_ITEM_BASE + g_numDynItems); }
// All item ids in registry order (static then dynamic), for listings.
int ItemIdAt(int index); // index 0..ItemTotal()-1 -> id (skips the "air" entry)
int ItemTotal();

int FindItem(const char* name);           // -1 if unknown; accepts optional "minecraft:" prefix
int Price(int id);                        // Counter-Strike dollars for one of the item; 0 = not for sale
inline const ItemDef& Item(int id)
{
	if (id >= DYN_ITEM_BASE && id < DYN_ITEM_BASE + g_numDynItems)
		return g_dynItems[id - DYN_ITEM_BASE];
	return g_items[(id > 0 && id < g_numItems) ? id : 0];
}

// One inventory stack. id 0 = empty.
// A Counter-Strike weapon slot shown as an inventory entry ("token"), so guns can be moved around the
// hotbar and storage like items: id = CS_TOKEN_BASE + CS slot (0 primary, 1 pistol, 2 knife,
// 3 grenades, 4 C4). Only exists while the player owns a weapon in that slot.
static const int CS_TOKEN_BASE = 3000;
inline bool IsCsToken(int id) { return id >= CS_TOKEN_BASE && id < CS_TOKEN_BASE + 5; }

struct Stack
{
	uint16_t id = 0;
	uint8_t count = 0;
	uint16_t damage = 0;
	uint16_t ench = 0; // enchantments, packed (mc_enchant.h)
	bool Empty() const { return id == 0 || count == 0; }
};

// Minecraft constants converted to CS: 1 Minecraft health point (half a heart) = 5 CS hit points.
static const float HP_PER_MC = 5.0f;
static const float UNITS_PER_BLOCK = 40.0f;
static const float TICK = 0.05f;          // Minecraft tick in seconds
static const float ATTACK_REACH = 3.0f * UNITS_PER_BLOCK;
static const float BLOCK_REACH = 4.5f * UNITS_PER_BLOCK;

// Minecraft damage reduction from armor (Java 1.21): damage in MC points.
float ArmorReduce(float damage, float armorPoints, float toughness);

// XP curve (Java): points needed to go from level L to L+1, and total points at level L.
int XpToNext(int level);
int XpTotalAtLevel(int level);

// Split an XP amount into orb sizes the way Minecraft does (ExperienceOrb.getExperienceValue).
int XpOrbValue(int remaining);

// Minecraft block-breaking time in seconds for a block hardness with a tool.
// canHarvest = tool type matches the block's required tool (or block needs none).
float BreakSeconds(float hardness, float toolSpeed, bool correctTool, bool canHarvest);
} // namespace mci
