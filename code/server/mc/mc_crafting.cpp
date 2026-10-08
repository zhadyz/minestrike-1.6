// 2x2 crafting (the inventory's grid). A handful of Minecraft recipes that make sense in this world;
// the grid is matched shapelessly unless a recipe says otherwise.
#include "precompiled.h"

#include "mc_server.h"

namespace mc
{
struct Recipe
{
	const char* in[4]; // ingredients (nullptr = unused); "#sand" = any sand block, "#crate" = any crate block
	const char* out;
	int count;
};

static bool Matches(const char* want, int id)
{
	const mci::ItemDef& d = mci::Item(id);
	if (!strcmp(want, "#sand"))
		return d.type == mci::IT_BLOCK && (strstr(d.name, "sand") != nullptr) && !strstr(d.name, "sandstone") && !strstr(d.name, "wll") &&
			!strstr(d.name, "wall");
	if (!strcmp(want, "#crate"))
		return d.type == mci::IT_BLOCK && (strstr(d.name, "crt") || strstr(d.name, "crate") || strstr(d.name, "planks"));
	return !strcmp(d.name, want);
}

static const Recipe kRecipes[] = {
	{{"gunpowder", "gunpowder", "#sand", "#sand"}, "tnt", 1},                    // TNT (Minecraft needs a table; close enough)
	{{"#crate", nullptr, nullptr, nullptr}, "oak_planks", 4},                    // a crate breaks down into planks
	{{"diamond", "diamond", "diamond", "diamond"}, "diamond_block", 1},
	{{"diamond_block", nullptr, nullptr, nullptr}, "diamond", 9},
	{{"gold_ingot", "gold_ingot", "gold_ingot", "gold_ingot"}, "gold_block", 1},
	{{"cobblestone", "cobblestone", "cobblestone", "cobblestone"}, "stone_bricks", 4},
};

int CraftResult(const mci::Stack craft[4], int* count)
{
	*count = 0;
	int have[4], nh = 0;
	for (int i = 0; i < 4; i++)
		if (!craft[i].Empty())
			have[nh++] = craft[i].id;
	if (!nh)
		return 0;
	for (const Recipe& r : kRecipes)
	{
		int nw = 0;
		for (const char* w : r.in)
			if (w)
				nw++;
		if (nw != nh)
			continue;
		bool used[4] = {false, false, false, false};
		bool ok = true;
		for (const char* w : r.in)
		{
			if (!w)
				continue;
			bool found = false;
			for (int i = 0; i < nh && !found; i++)
				if (!used[i] && Matches(w, have[i]))
					used[i] = found = true;
			if (!found)
			{
				ok = false;
				break;
			}
		}
		if (!ok)
			continue;
		int id = mci::FindItem(r.out);
		if (id <= 0)
			continue;
		*count = r.count;
		return id;
	}
	return 0;
}
} // namespace mc
