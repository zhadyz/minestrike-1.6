// Enchantments, by Minecraft's rules (EnchantmentMenu and EnchantmentHelper.selectEnchantment), for the
// enchantments that mean something here. An item carries them packed in Stack::ench:
//   bits 0-2  the main one, by item: Sharpness (sword, axe, mace) I-V, Power (bow) I-V,
//             Protection (armor) I-IV, Efficiency (pickaxe, shovel) I-V
//   bits 3-4  Unbreaking I-III (anything with durability)
//   bits 5-6  Fire Aspect (sword, axe) I-II, Flame (bow) I
//   bits 7-8  Knockback (sword, axe) I-II, Punch (bow) I-II
#pragma once
#include "mc_items.h"

namespace mce
{
inline int Main(uint16_t e) { return e & 7; }
inline int Unbreaking(uint16_t e) { return (e >> 3) & 3; }
inline int Fire(uint16_t e) { return (e >> 5) & 3; }
inline int Knock(uint16_t e) { return (e >> 7) & 3; }
inline uint16_t Make(int main, int unbreaking, int fire, int knock)
{
	return (uint16_t)((main & 7) | ((unbreaking & 3) << 3) | ((fire & 3) << 5) | ((knock & 3) << 7));
}

// Names of the item's enchantments by kind (nullptr: the item cannot have that one)
const char* MainName(const mci::ItemDef& d);
const char* FireName(const mci::ItemDef& d);
const char* KnockName(const mci::ItemDef& d);
inline bool Enchantable(const mci::ItemDef& d) { return MainName(d) != nullptr; }

// "Sharpness IV", "Unbreaking III", ... Returns the number of lines written (at most 4).
int Describe(const mci::ItemDef& d, uint16_t ench, char lines[4][32]);

// EnchantmentMenu: the level each of the table's three slots asks for, with `shelves` bookshelves around it
// (0: the slot offers nothing this time)
void SlotLevels(int shelves, unsigned seed, int out[3]);
// EnchantmentHelper.selectEnchantment: what a slot of that level gives the item (0: nothing fits)
uint16_t Roll(const mci::ItemDef& d, int level, unsigned seed);
} // namespace mce
