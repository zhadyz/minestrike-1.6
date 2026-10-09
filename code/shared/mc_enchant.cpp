#include "mc_enchant.h"

#include <stdio.h>
#include <string.h>

namespace mce
{
using namespace mci;

static bool Blade(const ItemDef& d) { return d.type == IT_SWORD || d.type == IT_AXE || d.type == IT_MACE; }

const char* MainName(const ItemDef& d)
{
	if (Blade(d))
		return "Sharpness";
	if (d.type == IT_BOW)
		return "Power";
	if (d.type == IT_ARMOR)
		return "Protection";
	if (d.type == IT_PICKAXE || d.type == IT_SHOVEL)
		return "Efficiency";
	return nullptr;
}
const char* FireName(const ItemDef& d) { return (d.type == IT_SWORD || d.type == IT_AXE) ? "Fire Aspect" : d.type == IT_BOW ? "Flame" : nullptr; }
const char* KnockName(const ItemDef& d) { return (d.type == IT_SWORD || d.type == IT_AXE) ? "Knockback" : d.type == IT_BOW ? "Punch" : nullptr; }

int Describe(const ItemDef& d, uint16_t ench, char lines[4][32])
{
	static const char* roman[8] = {"", "I", "II", "III", "IV", "V", "VI", "VII"};
	int n = 0;
	auto add = [&](const char* name, int level) {
		if (name && level > 0 && n < 4)
			snprintf(lines[n++], 32, "%s %s", name, roman[level & 7]);
	};
	add(MainName(d), Main(ench));
	add(FireName(d), Fire(ench));
	add(KnockName(d), Knock(ench));
	add("Unbreaking", Unbreaking(ench));
	return n;
}

// java.util.Random would do; any repeatable generator serves: the offers shown must be the ones given
struct Rng
{
	unsigned s;
	int Next(int n) // 0..n-1
	{
		s = s * 1664525u + 1013904223u;
		return n > 0 ? (int)((s >> 8) % (unsigned)n) : 0;
	}
};

void SlotLevels(int shelves, unsigned seed, int out[3])
{
	if (shelves > 15)
		shelves = 15;
	Rng r = {seed ^ 0x9E3779B9u};
	// EnchantmentHelper.getEnchantmentCost for slots 0, 1, 2
	for (int slot = 0; slot < 3; slot++)
	{
		int base = r.Next(8) + 1 + (shelves >> 1) + r.Next(shelves + 1);
		int lv = slot == 0 ? (base / 3 > 1 ? base / 3 : 1) : slot == 1 ? base * 2 / 3 + 1 : (base > shelves * 2 ? base : shelves * 2);
		out[slot] = lv < slot + 1 ? 0 : lv; // below its own cost in levels the slot offers nothing
	}
}

// Item.getEnchantmentValue by material
static int Enchantability(const ItemDef& d)
{
	const char* n = d.name;
	auto has = [&](const char* p) { return !strncmp(n, p, strlen(p)); };
	if (d.type == IT_BOW || d.type == IT_MACE)
		return d.type == IT_MACE ? 15 : 1;
	bool armor = d.type == IT_ARMOR;
	if (has("wooden_") || has("leather_"))
		return 15;
	if (has("stone_"))
		return 5;
	if (has("golden_"))
		return armor ? 25 : 22;
	if (has("chainmail_"))
		return 12;
	if (has("diamond_"))
		return 10;
	if (has("netherite_"))
		return 15;
	return armor ? 9 : 14; // iron (and the turtle shell)
}

struct Candidate
{
	int kind; // 0 main, 1 unbreaking, 2 fire, 3 knock
	int level, weight;
};

uint16_t Roll(const ItemDef& d, int level, unsigned seed)
{
	if (!Enchantable(d) || level <= 0)
		return 0;
	Rng r = {seed ^ (unsigned)(level * 7919)};
	// the modified level: the slot's level, the item's enchantability, and 15% either way
	int e = Enchantability(d);
	level += 1 + r.Next(e / 4 + 1) + r.Next(e / 4 + 1);
	float f = (r.Next(1000) / 1000.0f + r.Next(1000) / 1000.0f - 1.0f) * 0.15f;
	level = (int)(level + level * f + 0.5f);
	if (level < 1)
		level = 1;

	// every enchantment the item can take whose cost range holds the level, at its highest such level
	// (EnchantmentHelper.getAvailableEnchantmentResults): the list all the draws are made from
	Candidate c[4];
	int n = 0;
	auto offer = [&](int kind, int maxLevel, int min1, int perLevel, int span, int weight) {
		for (int lv = maxLevel; lv >= 1; lv--)
		{
			int lo = min1 + (lv - 1) * perLevel;
			if (level >= lo && level <= lo + span)
			{
				c[n++] = {kind, lv, weight};
				return;
			}
		}
	};
	if (Blade(d))
		offer(0, 5, 1, 11, 20, 10); // Sharpness
	else if (d.type == IT_BOW)
		offer(0, 5, 1, 10, 15, 10); // Power
	else if (d.type == IT_ARMOR)
		offer(0, 4, 1, 11, 11, 10); // Protection
	else
		offer(0, 5, 1, 10, 50, 10); // Efficiency
	if (d.durability > 0)
		offer(1, 3, 5, 8, 50, 5); // Unbreaking
	if (d.type == IT_SWORD || d.type == IT_AXE)
	{
		offer(2, 2, 10, 20, 50, 2); // Fire Aspect
		offer(3, 2, 5, 20, 50, 5);  // Knockback
	}
	else if (d.type == IT_BOW)
	{
		offer(2, 1, 20, 0, 30, 2);  // Flame
		offer(3, 2, 12, 20, 25, 2); // Punch
	}
	int levels[4] = {0, 0, 0, 0};
	auto draw = [&]() {
		int total = 0;
		for (int i = 0; i < n; i++)
			total += c[i].weight;
		int w = r.Next(total);
		for (int i = 0; i < n; i++)
		{
			w -= c[i].weight;
			if (w < 0)
			{
				levels[c[i].kind] = c[i].level;
				c[i] = c[--n];
				return;
			}
		}
	};
	if (n)
	{
		draw();
		// more of them, less likely each time
		while (n && r.Next(50) <= level)
		{
			draw();
			level /= 2;
		}
	}
	return Make(levels[0], levels[1], levels[2], levels[3]);
}
} // namespace mce
