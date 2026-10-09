// The buy menu, in the shape of Minecraft's creative inventory screen (CreativeModeInventoryScreen): tabs, a
// 9x5 item grid with a scroller, the hotbar row, tooltips, a Kits tab laid out like Minecraft's saved
// hotbars, and a button for Counter-Strike's own buy menu. One tab holds Counter-Strike's guns and
// equipment, the others the Minecraft items; outside creative mode every item carries its price and the
// title bar shows the player's money. B opens it. While it is open the mouse drives a cursor instead of
// the view (GuiPreCreateMove/GuiCreateMove turn the view change into cursor motion and undo it), the
// player stands still, and clicks/keys never reach the game's bindings.
#include "mc_client.h"
#include "mc_draw.h"
#include "mc_state.h"
#include "mc_enchant.h"
#include "mc_tex.h"
#include "mc_sounds_gen.h"
#include "mc_blocks.h"
#include "mc_classic.h"
#include "mc_move.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

namespace mc
{
float GuiScale();
void DrawSlotItem(int slot, float x, float y, float s);
void DrawCsWeaponIcon(int id, float x, float y, float size);
const char* CsWeaponDisplayName(int id);

// GoldSrc key codes (keydefs.h)
static const int K_ESCAPE = 27, K_MWHEELDOWN = 239, K_MWHEELUP = 240, K_MOUSE1 = 241, K_MOUSE2 = 242;

// texture-space layout of gui/container/creative_inventory/tab_items.png
static const int IMG_W = 195, IMG_H = 136, GRID_X = 9, GRID_Y = 18, COLS = 9, ROWS = 5, HOTBAR_Y = 112;
static const int SCROLL_X = 175, SCROLL_Y = 18, SCROLL_H = 112;

enum TabKind
{
	TAB_CS,
	TAB_BLOCKS,
	TAB_TOOLS,
	TAB_COMBAT,
	TAB_FOOD,
	TAB_INGREDIENTS,
	TAB_KITS,
	NUM_TABS
};
struct TabDef
{
	const char* title;
	const char* icon; // item name
	bool top;
	int column;       // 0-4 from the left, 5-6 aligned right
};
static const TabDef kTabs[NUM_TABS] = {
	{"Counter-Strike", nullptr, true, 1}, // drawn with a rifle
	{"Building Blocks", "chiseled_sandstone", true, 0},
	{"Tools & Utilities", "diamond_pickaxe", false, 0},
	{"Combat", "diamond_sword", false, 1},
	{"Food & Drinks", "golden_apple", false, 2},
	{"Ingredients", "gunpowder", false, 3},
	{"Kits", "diamond_chestplate", true, 5},
};

struct Kit
{
	const char* name;
	const char* items[7];
	int counts[7];
};
// Mirrors GiveKit on the server (mc_main.cpp)
static const Kit kKits[] = {
	{"Full Diamond", {"diamond_sword", "diamond_axe", "diamond_helmet", "diamond_chestplate", "diamond_leggings", "diamond_boots"}, {1, 1, 1, 1, 1, 1}},
	{"Netherite + Mace", {"netherite_sword", "mace", "netherite_helmet", "netherite_chestplate", "netherite_leggings", "netherite_boots"}, {1, 1, 1, 1, 1, 1}},
	{"Elytra + Fireworks", {"elytra", "firework_rocket"}, {1, 64}},
	{"Miner", {"diamond_pickaxe", "diamond_shovel", "tnt", "cobblestone"}, {1, 1, 16, 64}},
	{"Totem + Golden Apples", {"totem_of_undying", "golden_apple"}, {1, 16}},
};
static const int NUM_KITS = sizeof(kKits) / sizeof(kKits[0]);

// Counter-Strike's own wares: the weapon id for the picture (0: equipment, pictured with a Minecraft item),
// the buy command, the price, who may buy it (0 anyone, 1 Terrorists, 2 Counter-Terrorists)
struct CsWare
{
	int weapon;
	const char* icon;
	const char* cmd;
	const char* name;
	int price, team;
};
static const CsWare kCsWares[] = {
	{17, nullptr, "glock", "Glock-18", 400, 0}, {16, nullptr, "usp", "USP", 500, 0}, {1, nullptr, "p228", "P228", 600, 0},
	{26, nullptr, "deagle", "Desert Eagle", 650, 0}, {11, nullptr, "fn57", "Five-SeveN", 750, 2}, {10, nullptr, "elites", "Dual Elites", 800, 1},
	{21, nullptr, "m3", "M3", 1700, 0}, {5, nullptr, "xm1014", "XM1014", 3000, 0}, {23, nullptr, "tmp", "TMP", 1250, 2},
	{7, nullptr, "mac10", "MAC-10", 1400, 1}, {19, nullptr, "mp5", "MP5", 1500, 0}, {12, nullptr, "ump45", "UMP45", 1700, 0},
	{30, nullptr, "p90", "P90", 2350, 0}, {14, nullptr, "galil", "Galil", 2000, 1}, {15, nullptr, "famas", "FAMAS", 2250, 2},
	{28, nullptr, "ak47", "AK-47", 2500, 1}, {22, nullptr, "m4a1", "M4A1", 3100, 2}, {27, nullptr, "sg552", "SG 552", 3500, 1},
	{8, nullptr, "aug", "AUG", 3500, 2}, {3, nullptr, "scout", "Scout", 2750, 0}, {18, nullptr, "awp", "AWP", 4750, 0},
	{24, nullptr, "g3sg1", "G3SG1", 5000, 1}, {13, nullptr, "sg550", "SG 550", 4200, 2}, {20, nullptr, "m249", "M249", 5750, 0},
	{0, "iron_chestplate", "vest", "Kevlar", 650, 0}, {0, "iron_helmet", "vesthelm", "Kevlar + Helmet", 1000, 0},
	{25, nullptr, "flash", "Flashbang", 200, 0}, {4, nullptr, "hegren", "HE Grenade", 300, 0}, {9, nullptr, "sgren", "Smoke Grenade", 300, 0},
	{0, "flint_and_steel", "defuser", "Defuse Kit", 200, 2}, {0, "golden_carrot", "nvgs", "Night Vision", 1250, 0},
	{0, "gunpowder", "primammo", "Primary Ammo", 0, 0}, {0, "raw_iron", "secammo", "Pistol Ammo", 0, 0},
};
static const int NUM_CS_WARES = sizeof(kCsWares) / sizeof(kCsWares[0]);

// items cost money unless the player is in creative mode or the server runs without the economy
static bool ShopPays()
{
	return !g_cl.creative && gEngfuncs.pfnGetCvarFloat((char*)"mc_economy") != 0.0f;
}

// the local player's team (1 Terrorists, 2 Counter-Terrorists, 0 unknown), from the bits the server packs
// into playerclass
static int LocalTeam()
{
	cl_entity_t* me = gEngfuncs.GetLocalPlayer();
	return me ? (me->curstate.playerclass >> 12) & 3 : 0;
}

// A price tag in the corner of a grid cell: green when the player can pay, red when not
static void PriceTag(int price, float x, float y, float s)
{
	if (price <= 0)
		return;
	char buf[12];
	snprintf(buf, sizeof(buf), "$%d", price);
	float ts = s * 0.5f;
	mcdraw::Text(buf, x + 16.5f * s - mcdraw::TextWidth(buf) * ts, y + 12.5f * s, ts, g_cl.money >= price ? 0x55FF55FF : 0xFF5555FF);
}

static bool g_open = false;
static int g_kind = 0; // 0 creative inventory, 1 survival inventory (Shift+E), 2 enchanting table
static bool g_shift = false, g_ctrl = false;
static bool ShiftDown() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }
static bool CtrlDown() { return (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0; }
static int g_forceShift = -1; // test aid: mc_guiclick overrides
static int g_tab = TAB_CS;
static int g_scrollRow = 0;
static float g_mx = 0, g_my = 0;         // cursor, screen pixels
static float g_savedAngles[3];
static float g_preAngles[3];
static int g_items[512];
static int g_numItems = 0;
static int g_listTab = -1;

static bool InTab(const mci::ItemDef& d, int tab)
{
	switch (tab)
	{
	case TAB_BLOCKS: return d.type == mci::IT_BLOCK;
	case TAB_TOOLS:
		// (with what enchanting needs: the table, bookshelves and lapis lazuli are also on their own tabs)
		return d.type == mci::IT_PICKAXE || d.type == mci::IT_SHOVEL || d.type == mci::IT_AXE || d.type == mci::IT_FLINT_STEEL ||
			d.type == mci::IT_ELYTRA || d.type == mci::IT_FIREWORK || d.type == mci::IT_PEARL || d.type == mci::IT_XP_BOTTLE ||
			!strcmp(d.name, "enchanting_table") || !strcmp(d.name, "bookshelf") || !strcmp(d.name, "lapis_lazuli");
	case TAB_COMBAT:
		return d.type == mci::IT_SWORD || d.type == mci::IT_AXE || d.type == mci::IT_MACE || d.type == mci::IT_BOW || d.type == mci::IT_ARROW ||
			d.type == mci::IT_ARMOR || d.type == mci::IT_TOTEM;
	case TAB_FOOD: return d.type == mci::IT_FOOD;
	case TAB_INGREDIENTS: return d.type == mci::IT_MATERIAL;
	}
	return false;
}

static void BuildList()
{
	if (g_listTab == g_tab)
		return;
	g_listTab = g_tab;
	g_numItems = 0;
	if (g_tab == TAB_CS)
	{
		// Counter-Strike wares the player's team may buy, as negative entries (-1 - index)
		int team = LocalTeam();
		for (int k = 0; k < NUM_CS_WARES; k++)
			if (!kCsWares[k].team || !team || kCsWares[k].team == team)
				g_items[g_numItems++] = -1 - k;
		return;
	}
	for (int k = 0; k < mci::ItemTotal() && g_numItems < 512; k++)
	{
		int id = mci::ItemIdAt(k);
		if (InTab(mci::Item(id), g_tab))
			g_items[g_numItems++] = id;
	}
}

static int TotalRows()
{
	if (g_tab == TAB_KITS)
		return NUM_KITS;
	return (g_numItems + COLS - 1) / COLS;
}

static int MaxScroll()
{
	int r = TotalRows() - ROWS;
	return r > 0 ? r : 0;
}

bool GuiOpen() { return g_open; }

static void ClickSound()
{
	SoundPlay(mcs::MCS_UI_CLICK, nullptr, 0.25f, 1.0f, 0);
}

void GuiShow()
{
	if (g_open)
		return;
	g_kind = 0;
	g_open = true;
	g_mx = g_cl.screenW * 0.5f;
	g_my = g_cl.screenH * 0.5f;
	// level the view while the screen is up so vertical mouse motion is never eaten by the pitch clamp
	gEngfuncs.GetViewAngles(g_savedAngles);
	float level[3] = {0.0f, g_savedAngles[1], 0.0f};
	gEngfuncs.SetViewAngles(level);
	g_listTab = -1; // the Counter-Strike tab depends on the team
	BuildList();
}

void GuiShowInventory()
{
	if (g_open && g_kind == 1)
		return;
	if (g_open)
		g_open = false;
	g_kind = 1;
	g_mx = g_cl.screenW * 0.5f;
	g_my = g_cl.screenH * 0.5f;
	gEngfuncs.GetViewAngles(g_savedAngles);
	float level[3] = {0.0f, g_savedAngles[1], 0.0f};
	gEngfuncs.SetViewAngles(level);
	g_open = true;
	SoundPlay(mcs::MCS_UI_CLICK, nullptr, 0.15f, 1.0f, 0);
}

void GuiHide()
{
	if (!g_open)
		return;
	g_open = false;
	if (g_kind == 1)
		gEngfuncs.pfnServerCmd((char*)"mc_invclose"); // cursor stack and crafting grid go back
	if (g_kind == 2)
		gEngfuncs.pfnServerCmd((char*)"mc_ench_close");
	float a[3];
	gEngfuncs.GetViewAngles(a);
	g_savedAngles[1] = a[1];
	gEngfuncs.SetViewAngles(g_savedAngles);
}

// ---------------------------------------------------------------------------------------------
// Layout helpers (screen pixels)

struct Layout
{
	float s, left, top;
};

static Layout GetLayout()
{
	Layout L;
	L.s = GuiScale();
	L.left = floorf((g_cl.screenW - IMG_W * L.s) * 0.5f);
	L.top = floorf((g_cl.screenH - IMG_H * L.s) * 0.5f);
	return L;
}

static void TabRect(const Layout& L, int tab, float& x, float& y, float& w, float& h)
{
	const TabDef& t = kTabs[tab];
	int col = t.column;
	float gx = col < 5 ? col * 27.0f : IMG_W - 27.0f * (7 - col) + 1.0f;
	float gy = t.top ? -28.0f : IMG_H - 4.0f;
	x = L.left + gx * L.s;
	y = L.top + gy * L.s;
	w = 26 * L.s;
	h = 32 * L.s;
}

static bool Inside(float px, float py, float x, float y, float w, float h)
{
	return px >= x && py >= y && px < x + w && py < y + h;
}

// grid cell under the cursor (-1 if none)
static int HoverCell(const Layout& L, int* row = nullptr, int* col = nullptr)
{
	float gx = (g_mx - L.left) / L.s - GRID_X, gy = (g_my - L.top) / L.s - GRID_Y;
	if (gx < 0 || gy < 0)
		return -1;
	int c = (int)(gx / 18), r = (int)(gy / 18);
	if (c >= COLS || r >= ROWS || fmodf(gx, 18) >= 16.5f || fmodf(gy, 18) >= 16.5f)
		return -1;
	if (row)
		*row = r;
	if (col)
		*col = c;
	return r * COLS + c;
}

static int HoverHotbar(const Layout& L)
{
	float gx = (g_mx - L.left) / L.s - GRID_X, gy = (g_my - L.top) / L.s - HOTBAR_Y;
	if (gx < 0 || gy < 0 || gy >= 16.5f)
		return -1;
	int c = (int)(gx / 18);
	if (c >= mcp::HOTBAR_SIZE || fmodf(gx, 18) >= 16.5f)
		return -1;
	return c;
}

static void ButtonRect(const Layout& L, float& x, float& y, float& w, float& h)
{
	w = 150 * L.s;
	h = 20 * L.s;
	x = floorf((g_cl.screenW - w) * 0.5f);
	y = L.top + (IMG_H + 30) * L.s;
}

// item under the cursor in the grid (0 if none); kit index via *kit for the Kits tab
static int HoverItem(const Layout& L, int* kit)
{
	int r, c;
	if (HoverCell(L, &r, &c) < 0)
		return 0;
	if (g_tab == TAB_KITS)
	{
		int k = g_scrollRow + r;
		if (k >= NUM_KITS || c >= 7 || !kKits[k].items[c])
			return 0;
		if (kit)
			*kit = k;
		return mci::FindItem(kKits[k].items[c]);
	}
	int idx = (g_scrollRow + r) * COLS + c;
	return idx < g_numItems ? g_items[idx] : 0;
}

// ---------------------------------------------------------------------------------------------
// Drawing

struct TipLine
{
	char text[64];
	unsigned color;
};

// TooltipRenderUtil: dark purple box with a violet gradient frame; the first line sits 2 px apart
static void TooltipLines(const TipLine* lines, int n, float s)
{
	float w = 0;
	for (int i = 0; i < n; i++)
		w = fmaxf(w, mcdraw::TextWidth(lines[i].text));
	float h = 8.0f + (n > 1 ? 2.0f + (n - 1) * 10.0f : 0.0f);
	float x = g_mx / s + 12, y = g_my / s - 12;
	if ((x + w + 4) * s > g_cl.screenW)
		x = g_mx / s - 16 - w;
	if (x < 4)
		x = fmaxf(4.0f, g_cl.screenW / s - w - 4); // too wide for either side of the cursor: as far right as it fits
	if ((y + h + 4) * s > g_cl.screenH)
		y = g_cl.screenH / s - h - 4;
	if (y < 4)
		y = 4;
	auto R = [&](float rx, float ry, float rw, float rh, unsigned c) { mcdraw::Rect(rx * s, ry * s, rw * s, rh * s, c); };
	const unsigned bg = 0x100010F0, b0 = 0x5000FF50, b1 = 0x28007F50;
	R(x - 3, y - 4, w + 6, 1, bg);
	R(x - 3, y + h + 3, w + 6, 1, bg);
	R(x - 3, y - 3, w + 6, h + 6, bg);
	R(x - 4, y - 3, 1, h + 6, bg);
	R(x + w + 3, y - 3, 1, h + 6, bg);
	R(x - 3, y - 2, 1, h + 4, b0);
	R(x + w + 2, y - 2, 1, h + 4, b0);
	R(x - 3, y - 3, w + 6, 1, b0);
	R(x - 3, y + h + 2, w + 6, 1, b1);
	float ly = y;
	for (int i = 0; i < n; i++)
	{
		mcdraw::Text(lines[i].text, x * s, ly * s, s, lines[i].color);
		ly += i == 0 ? 12.0f : 10.0f;
	}
}

static void Tooltip(const char* line1, unsigned color1, const char* line2, float s)
{
	TipLine l[2];
	snprintf(l[0].text, sizeof(l[0].text), "%s", line1);
	l[0].color = color1;
	int n = 1;
	if (line2)
	{
		snprintf(l[1].text, sizeof(l[1].text), "%s", line2);
		l[1].color = 0xAAAAAAFF;
		n = 2;
	}
	TooltipLines(l, n, s);
}

static void Num(char* out, size_t n, float v)
{
	// ItemAttributeModifiers: up to two decimals, no trailing zeros
	snprintf(out, n, "%.2f", v);
	char* e = out + strlen(out) - 1;
	while (*e == '0')
		*e-- = 0;
	if (*e == '.')
		*e = 0;
}

// ItemStack.getTooltipLines: name, then the attribute block ("When in Main Hand:" ...)
static int g_tipEnch = 0; // the enchantments of the stack the next tooltip is for (0: none)
static void ItemTooltip(int itemId, const char* hint, float s)
{
	const mci::ItemDef& d = mci::Item(itemId);
	TipLine l[12];
	int n = 0;
	auto add = [&](unsigned c, const char* fmt, auto... args) {
		if (n < 12)
		{
			snprintf(l[n].text, sizeof(l[n].text), fmt, args...);
			l[n++].color = c;
		}
	};
	// an enchanted item's name is aqua, and its enchantments come first, like Minecraft's
	add(g_tipEnch ? 0x55FFFFFF : mcdraw::RarityColor(d.rarity), "%s", d.display);
	if (g_tipEnch)
	{
		char el[4][32];
		int en = mce::Describe(d, (uint16_t)g_tipEnch, el);
		for (int i = 0; i < en; i++)
			add(0xAAAAAAFF, "%s", el[i]);
	}
	// the economy (mc_economy.cpp): what one costs, unless items are free (creative mode, mc_economy 0)
	if (!g_cl.creative && gEngfuncs.pfnGetCvarFloat((char*)"mc_economy") != 0.0f)
	{
		int price = mci::Price(itemId);
		if (price > 0)
			add(0xFFFF55FF, d.maxStack > 1 ? "$%d each" : "$%d", price);
	}
	char a[16], b[16];
	bool weapon = d.type == mci::IT_SWORD || d.type == mci::IT_AXE || d.type == mci::IT_MACE || d.type == mci::IT_PICKAXE || d.type == mci::IT_SHOVEL;
	if (weapon)
	{
		Num(a, sizeof(a), d.attackDamage);
		Num(b, sizeof(b), d.attackSpeed);
		add(0xFFFFFFFF, " ");
		add(0xAAAAAAFF, "When in Main Hand:");
		add(0x00AA00FF, " %s Attack Damage", a);
		add(0x00AA00FF, " %s Attack Speed", b);
	}
	else if (d.type == mci::IT_ARMOR)
	{
		static const char* slots[] = {"When on Head:", "When on Body:", "When on Legs:", "When on Feet:"};
		add(0xFFFFFFFF, " ");
		add(0xAAAAAAFF, "%s", slots[d.armorSlot < 4 ? d.armorSlot : 0]);
		add(0x5555FFFF, "+%d Armor", d.armorPoints);
		if (d.toughness > 0.0f)
		{
			Num(a, sizeof(a), d.toughness);
			add(0x5555FFFF, "+%s Armor Toughness", a);
		}
		if (d.knockbackResist > 0.0f)
		{
			Num(a, sizeof(a), d.knockbackResist * 10.0f);
			add(0x5555FFFF, "+%s Knockback Resistance", a);
		}
	}
	// a block that can stand in a bullet's way: what it does there (mcw::BulletClassOf)
	if (d.type == mci::IT_BLOCK && d.blockName)
	{
		int type = mcw::FindBlock(d.blockName);
		if (type > 0)
		{
			const mcw::BlockDef& b = mcw::Block((uint16_t)type);
			bool solid = b.shape == mcw::SHAPE_CUBE || b.shape == mcw::SHAPE_SLAB || b.shape == mcw::SHAPE_STAIRS || b.shape == mcw::SHAPE_PANE ||
				b.shape == mcw::SHAPE_DOOR;
			if (solid && !(b.flags & mcw::BF_EXPLOSIVE))
			{
				const mcc::Classic* cm = mcm::GetClassic();
				switch (mcw::BulletClassOf(b, cm && cm->map.loaded))
				{
				case mcw::BULLET_STOPS:
					add(0x55FF55FF, "Stops every bullet");
					break;
				case mcw::BULLET_WOOD:
					add(0xFFFF55FF, "Stops pistols and SMGs");
					add(0xAAAAAAFF, "Rifles go through it. It burns.");
					break;
				case mcw::BULLET_SHATTERS:
					add(0xFF5555FF, "Stops no bullet");
					add(0xAAAAAAFF, "Shatters when shot");
					break;
				case mcw::BULLET_BARS:
					add(0xFF5555FF, "Stops no bullet");
					add(0xAAAAAAFF, "To see and shoot through");
					break;
				default:
					add(0xFF5555FF, "Stops no bullet");
					break;
				}
				if (!strcmp(b.name, "obsidian"))
					add(0xAAAAAAFF, "TNT does not move it");
				// what the mobs are built from (mc_mobs.cpp)
				if (!strcmp(b.name, "carved_pumpkin"))
				{
					add(0x55FFFFFF, "On four iron blocks in a T:");
					add(0x55FFFFFF, "an iron golem for your side");
				}
				else if (!strcmp(b.name, "iron_block"))
					add(0xAAAAAAFF, "Four in a T, a carved pumpkin on top: an iron golem");
				else if (!strcmp(b.name, "wither_skeleton_skull"))
				{
					add(0x55FFFFFF, "Three on four soul sand in a T:");
					add(0x55FFFFFF, "a wither for your side");
				}
				else if (!strcmp(b.name, "soul_sand"))
					add(0xAAAAAAFF, "Four in a T, three skulls on top: a wither");
			}
		}
	}
	if (hint)
		add(0x555555FF, "%s", hint);
	TooltipLines(l, n, s);
}

static void DrawCursor()
{
	// a plain arrow, 1 px outline
	static const char* rows[] = {
		"X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X", "X.......X", "X........X", "X.....XXXX",
		"X..X..X", "X.X X..X", "XX  X..X", "X    X..X", "     X..X", "      XX"};
	float p = g_cl.screenH >= 900 ? 2.0f : 1.0f;
	for (int r = 0; r < 17; r++)
		for (int c = 0; rows[r][c]; c++)
		{
			char ch = rows[r][c];
			if (ch == ' ')
				continue;
			mcdraw::Rect(g_mx + c * p, g_my + r * p, p, p, ch == 'X' ? 0x000000FF : 0xFFFFFFFF);
		}
}

static void DrawTab(const Layout& L, int tab, bool selected)
{
	const TabDef& t = kTabs[tab];
	float x, y, w, h;
	TabRect(L, tab, x, y, w, h);
	char spr[96];
	snprintf(spr, sizeof(spr), "gui/sprites/container/creative_inventory/tab_%s_%s_%d", t.top ? "top" : "bottom",
		selected ? "selected" : "unselected", t.column + 1);
	mcdraw::BlitFull(spr, x, y, w, h);
	int icon = t.icon ? mci::FindItem(t.icon) : 0;
	if (icon > 0)
		mcdraw::ItemIcon(icon, x + 5 * L.s, y + (t.top ? 9 : 7) * L.s, 16 * L.s);
	else if (!t.icon)
		DrawCsWeaponIcon(28, x + 5 * L.s, y + (t.top ? 9 : 7) * L.s, 16 * L.s); // an AK-47 for the Counter-Strike tab
}

static void DrawInventoryScreen();
static void DrawEnchantScreen();
static bool EnchantKey(int keynum, const char* binding);
static void EnchLayout(float& s, float& left, float& top);
void GuiDraw()
{
	if (!g_open)
		return;
	if (g_kind == 1)
	{
		DrawInventoryScreen();
		return;
	}
	if (g_kind == 2)
	{
		DrawEnchantScreen();
		return;
	}
	BuildList();
	if (g_scrollRow > MaxScroll())
		g_scrollRow = MaxScroll();
	Layout L = GetLayout();
	float s = L.s;
	mcdraw::Begin2D(g_cl.screenW, g_cl.screenH);
	// Screen.renderBackground: the world dims behind the screen
	mcdraw::Rect(0, 0, (float)g_cl.screenW, (float)g_cl.screenH, 0x101010B0);

	for (int t = 0; t < NUM_TABS; t++)
		if (t != g_tab)
			DrawTab(L, t, false);
	mcdraw::Blit("gui/container/creative_inventory/tab_items", L.left, L.top, IMG_W * s, IMG_H * s, 0, 0, IMG_W, IMG_H);
	DrawTab(L, g_tab, true);
	mcdraw::Text(kTabs[g_tab].title, L.left + 8 * s, L.top + 6 * s, s, 0x404040FF, false);
	bool pays = ShopPays();
	if (pays)
	{
		char money[16];
		snprintf(money, sizeof(money), "$%d", g_cl.money);
		mcdraw::Text(money, L.left + (IMG_W - 26 - mcdraw::TextWidth(money)) * s, L.top + 6 * s, s, 0x1F7A1FFF, false);
	}

	// grid
	int kitHover = -1;
	int hoverItem = HoverItem(L, &kitHover);
	int hoverCell = HoverCell(L);
	for (int r = 0; r < ROWS; r++)
	{
		for (int c = 0; c < COLS; c++)
		{
			float x = L.left + (GRID_X + c * 18) * s, y = L.top + (GRID_Y + r * 18) * s;
			int id = 0, count = 1;
			if (g_tab == TAB_KITS)
			{
				int k = g_scrollRow + r;
				if (k < NUM_KITS && c < 7 && kKits[k].items[c])
				{
					id = mci::FindItem(kKits[k].items[c]);
					count = kKits[k].counts[c];
				}
			}
			else
			{
				int idx = (g_scrollRow + r) * COLS + c;
				if (idx < g_numItems)
					id = g_items[idx];
			}
			if (id < 0)
			{
				// a Counter-Strike ware
				const CsWare& w = kCsWares[-1 - id];
				if (w.weapon)
					DrawCsWeaponIcon(w.weapon, x, y, 16 * s);
				else
					mcdraw::ItemIcon(mci::FindItem(w.icon), x, y, 16 * s);
				PriceTag(w.price, x, y, s);
			}
			if (id > 0)
			{
				mcdraw::ItemIcon(id, x, y, 16 * s);
				if (count > 1)
				{
					char buf[8];
					snprintf(buf, sizeof(buf), "%d", count);
					mcdraw::Text(buf, x + 17 * s - mcdraw::TextWidth(buf) * s, y + 9 * s, s, 0xFFFFFFFF);
				}
				if (pays && g_tab != TAB_KITS)
					PriceTag(mci::Price(id), x, y, s);
			}
			if (pays && g_tab == TAB_KITS && c == 7 && g_scrollRow + r < NUM_KITS)
			{
				// the whole row's price at its end
				const Kit& kit = kKits[g_scrollRow + r];
				int total = 0;
				for (int i = 0; i < 7 && kit.items[i]; i++)
					total += mci::Price(mci::FindItem(kit.items[i])) * kit.counts[i];
				char buf[12];
				snprintf(buf, sizeof(buf), "$%d", total);
				mcdraw::Text(buf, x + 1 * s, y + 5 * s, s * 0.75f, g_cl.money >= total ? 0x1F7A1FFF : 0xAA2020FF, false);
			}
			bool hot = g_tab == TAB_KITS ? (kitHover == g_scrollRow + r && hoverCell >= 0) : (hoverCell == r * COLS + c);
			if (hot && (id != 0 || g_tab != TAB_KITS))
				mcdraw::Rect(x, y, 16 * s, 16 * s, 0xFFFFFF80);
		}
	}
	// hotbar row
	int hb = HoverHotbar(L);
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
	{
		float x = L.left + (GRID_X + i * 18) * s, y = L.top + HOTBAR_Y * s;
		DrawSlotItem(i, x, y, s);
		if (hb == i)
			mcdraw::Rect(x, y, 16 * s, 16 * s, 0xFFFFFF80);
	}
	// scroller
	int maxS = MaxScroll();
	float frac = maxS > 0 ? (float)g_scrollRow / maxS : 0.0f;
	mcdraw::BlitFull(maxS > 0 ? "gui/sprites/container/creative_inventory/scroller" : "gui/sprites/container/creative_inventory/scroller_disabled",
		L.left + SCROLL_X * s, L.top + (SCROLL_Y + (SCROLL_H - 17) * frac) * s, 12 * s, 15 * s);

	// Counter-Strike buy menu button
	float bx, by, bw, bh;
	ButtonRect(L, bx, by, bw, bh);
	bool bhot = Inside(g_mx, g_my, bx, by, bw, bh);
	const char* btex = bhot ? "gui/sprites/widget/button_highlighted" : "gui/sprites/widget/button";
	mcdraw::Blit(btex, bx, by, bw * 0.5f, bh, 0, 0, 75, 20);
	mcdraw::Blit(btex, bx + bw * 0.5f, by, bw * 0.5f, bh, 125, 0, 200, 20);
	mcdraw::TextCentered("Counter-Strike Buy Menu", bx + bw * 0.5f, by + 6 * s, s, bhot ? 0xFFFFA0FF : 0xFFFFFFFF);

	// tooltips
	if (hoverItem < 0)
	{
		const CsWare& w = kCsWares[-1 - hoverItem];
		char line[64];
		if (w.price > 0)
			snprintf(line, sizeof(line), "$%d  -  click to buy", w.price);
		else
			snprintf(line, sizeof(line), "click to buy");
		Tooltip(w.name, 0xFFAA00FF, line, s);
	}
	else if (hoverItem > 0)
	{
		const mci::ItemDef& d = mci::Item(hoverItem);
		if (g_tab == TAB_KITS && kitHover >= 0)
		{
			char t[64];
			snprintf(t, sizeof(t), "%s kit", kKits[kitHover].name);
			Tooltip(t, 0x55FFFFFF, "Click to equip the whole row", s);
		}
		else
			ItemTooltip(hoverItem, pays ? (d.maxStack > 1 ? "Click: as many as you can pay for, right-click: one" : "Click to buy")
									   : d.type == mci::IT_ARMOR || d.type == mci::IT_ELYTRA ? "Click to wear" : "Click: stack, right-click: one, 6-9: slot", s);
	}
	else if (hb >= 0 && g_cl.hotbarId[hb] > 0)
	{
		g_tipEnch = g_cl.ench[36 + hb];
		ItemTooltip(g_cl.hotbarId[hb], "Right-click to destroy", s);
		g_tipEnch = 0;
	}
	else
	{
		for (int t = 0; t < NUM_TABS; t++)
		{
			float x, y, w, h;
			TabRect(L, t, x, y, w, h);
			if (Inside(g_mx, g_my, x, y, w, h))
				Tooltip(kTabs[t].title, 0xFFFFFFFF, nullptr, s);
		}
	}
	DrawCursor();
	mcdraw::End2D();
}

// ---------------------------------------------------------------------------------------------
// Input

static void Click(bool right)
{
	Layout L = GetLayout();
	for (int t = 0; t < NUM_TABS; t++)
	{
		float x, y, w, h;
		TabRect(L, t, x, y, w, h);
		if (Inside(g_mx, g_my, x, y, w, h))
		{
			if (t != g_tab)
			{
				g_tab = t;
				g_scrollRow = 0;
				BuildList();
				ClickSound();
			}
			return;
		}
	}
	float bx, by, bw, bh;
	ButtonRect(L, bx, by, bw, bh);
	if (Inside(g_mx, g_my, bx, by, bw, bh))
	{
		ClickSound();
		GuiHide();
		gEngfuncs.pfnClientCmd((char*)"mc_csbuy\n");
		return;
	}
	int kit = -1;
	int item = HoverItem(L, &kit);
	if (item < 0)
	{
		// a Counter-Strike ware: Counter-Strike's own buy command (it checks the buy zone, the time, the money)
		gEngfuncs.pfnServerCmd((char*)kCsWares[-1 - item].cmd);
		ClickSound();
		return;
	}
	if (item > 0)
	{
		char cmd[96];
		if (g_tab == TAB_KITS)
			snprintf(cmd, sizeof(cmd), "mc_kit %d", kit);
		else
		{
			const mci::ItemDef& d = mci::Item(item);
			snprintf(cmd, sizeof(cmd), "mc_take %s %d", d.name, right ? 1 : d.maxStack);
		}
		gEngfuncs.pfnServerCmd(cmd);
		SoundPlay(mcs::MCS_ITEM_PICKUP, nullptr, 0.2f, 1.4f, 0);
		return;
	}
	int hb = HoverHotbar(L);
	if (hb >= 0 && right && g_cl.hotbarId[hb] > 0)
	{
		char cmd[48];
		snprintf(cmd, sizeof(cmd), "mc_setslot %d 0", hb + 1);
		gEngfuncs.pfnServerCmd(cmd);
		ClickSound();
	}
	else if (hb >= 0 && !right)
	{
		char cmd[32];
		snprintf(cmd, sizeof(cmd), "mc_select %d", hb + 1);
		gEngfuncs.pfnServerCmd(cmd);
	}
}

// Returns false when the key was consumed by the screen.
static bool InventoryKey(int keynum, const char* binding);
bool GuiKey(int down, int keynum, const char* binding)
{
	const int K_SHIFT = 134, K_CTRL = 133;
	if (keynum == K_SHIFT)
		g_shift = down != 0;
	if (keynum == K_CTRL)
		g_ctrl = down != 0;
	if (!g_open)
	{
		// Shift+E: the Minecraft inventory (E alone stays Counter-Strike's +use)
		if (down && keynum == 'e' && ShiftDown() && g_cl.health > 0)
		{
			GuiShowInventory();
			return false;
		}
		// Shift+B: the Minecraft item menu (creative inventory) to spawn items; B alone stays CS's buy menu
		if (down && keynum == 'b' && ShiftDown() && g_cl.health > 0)
		{
			GuiShow();
			return false;
		}
		// Shift+M: pick a character (Minecraft characters or any CS model); M alone stays the team menu
		if (down && keynum == 'm' && ShiftDown())
		{
			gEngfuncs.pfnServerCmd((char*)"mc_character");
			return false;
		}
		return true;
	}
	if (binding && (strstr(binding, "toggleconsole") || strstr(binding, "screenshot") || strstr(binding, "snapshot")))
		return true;
	if (keynum == K_SHIFT || keynum == K_CTRL)
		return false;
	if (!down)
		return false;
	if (g_kind == 1)
		return InventoryKey(keynum, binding);
	if (g_kind == 2)
		return EnchantKey(keynum, binding);
	if (keynum == K_MOUSE1 || keynum == K_MOUSE2)
		Click(keynum == K_MOUSE2);
	else if (keynum == K_MWHEELUP && g_scrollRow > 0)
		g_scrollRow--;
	else if (keynum == K_MWHEELDOWN && g_scrollRow < MaxScroll())
		g_scrollRow++;
	else if (keynum == K_ESCAPE || keynum == 'e' || (binding && !strcmp(binding, "buy")))
		GuiHide();
	else if (keynum >= '1' && keynum <= '9' && g_cl.hotbarId[keynum - '1'] >= 0)
	{
		// Minecraft: hover an item and press a hotbar number to put it in that slot
		Layout L = GetLayout();
		int item = (g_tab == TAB_KITS || g_tab == TAB_CS) ? 0 : HoverItem(L, nullptr);
		if (item > 0)
		{
			const mci::ItemDef& d = mci::Item(item);
			char cmd[96];
			snprintf(cmd, sizeof(cmd), "mc_setslot %d %s %d", keynum - '0', d.name, d.maxStack);
			gEngfuncs.pfnServerCmd(cmd);
			SoundPlay(mcs::MCS_ITEM_PICKUP, nullptr, 0.2f, 1.4f, 0);
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Survival inventory (InventoryScreen): armor, 2x2 crafting, 27 storage slots, hotbar, player preview

void PlayerPreview(float x0, float y0, float x1, float y1, float lookX, float lookY);

static const int INV_W = 176, INV_H = 166;

// GUI-pixel position of a Minecraft InventoryMenu slot (-1 when the slot isn't on this screen)
static bool SlotPos(int slot, int& x, int& y)
{
	if (slot == 0) { x = 154; y = 28; return true; }
	if (slot >= 1 && slot <= 4) { x = 98 + ((slot - 1) % 2) * 18; y = 18 + ((slot - 1) / 2) * 18; return true; }
	if (slot >= 5 && slot <= 8) { x = 8; y = 8 + (slot - 5) * 18; return true; }
	if (slot >= 9 && slot <= 35) { x = 8 + ((slot - 9) % 9) * 18; y = 84 + ((slot - 9) / 9) * 18; return true; }
	if (slot >= 36 && slot <= 44) { x = 8 + (slot - 36) * 18; y = 142; return true; }
	if (slot == 45) { x = 77; y = 62; return true; }
	if (slot == 46) { x = INV_W + 6; y = 142; return true; } // trash
	return false;
}

static void InvLayout(float& s, float& left, float& top)
{
	s = GuiScale();
	left = floorf((g_cl.screenW - INV_W * s) * 0.5f);
	top = floorf((g_cl.screenH - INV_H * s) * 0.5f);
}

static int InvHoverSlot()
{
	float s, left, top;
	InvLayout(s, left, top);
	for (int slot = 0; slot <= 46; slot++)
	{
		int x, y;
		if (!SlotPos(slot, x, y))
			continue;
		int size = slot == 0 ? 24 : 16;
		int off = slot == 0 ? -4 : 0;
		if (Inside(g_mx, g_my, left + (x + off) * s, top + (y + off) * s, size * s, size * s))
			return slot;
	}
	if (Inside(g_mx, g_my, left + (INV_W + 2) * s, top + 138 * s, 24 * s, 24 * s))
		return -1; // the trash slot's frame
	return Inside(g_mx, g_my, left, top, INV_W * s, INV_H * s) ? -1 : -999; // -999: outside the window
}

static void SlotItem(int slot, int& id, int& count, int& dmg)
{
	id = count = dmg = 0;
	if (slot == 0) { id = g_cl.craftResId; count = g_cl.craftResCount; }
	else if (slot >= 1 && slot <= 4) { id = g_cl.craftId[slot - 1]; count = g_cl.craftCount[slot - 1]; dmg = g_cl.craftDamage[slot - 1]; }
	else if (slot >= 5 && slot <= 8) { id = g_cl.armorId[slot - 5]; count = id ? 1 : 0; dmg = g_cl.armorDamage[slot - 5]; }
	else if (slot >= 9 && slot <= 35) { id = g_cl.invId[slot - 9]; count = g_cl.invCount[slot - 9]; dmg = g_cl.invDamage[slot - 9]; }
	else if (slot >= 36 && slot <= 44) { id = g_cl.hotbarId[slot - 36]; count = g_cl.hotbarCount[slot - 36]; dmg = g_cl.hotbarDamage[slot - 36]; }
}

static void DrawStack(int id, int count, float x, float y, float s)
{
	if (id < 0)
	{
		DrawCsWeaponIcon(-id, x, y, 16 * s); // a gun
		return;
	}
	if (id == 0)
		return;
	mcdraw::ItemIcon(id, x, y, 16 * s);
	if (count > 1)
	{
		char buf[8];
		snprintf(buf, sizeof(buf), "%d", count);
		mcdraw::Text(buf, x + 17 * s - mcdraw::TextWidth(buf) * s, y + 9 * s, s, 0xFFFFFFFF);
	}
}

static void DrawInventoryScreen()
{
	float s, left, top;
	InvLayout(s, left, top);
	mcdraw::Begin2D(g_cl.screenW, g_cl.screenH);
	mcdraw::Rect(0, 0, (float)g_cl.screenW, (float)g_cl.screenH, 0x101010B0);
	mcdraw::Blit("gui/container/inventory", left, top, INV_W * s, INV_H * s, 0, 0, INV_W, INV_H);
	mcdraw::Text("Crafting", left + 97 * s, top + 8 * s, s, 0x404040FF, false);
	// player preview, looking at the mouse like Minecraft's
	{
		float px0 = left + 26 * s, py0 = top + 8 * s, px1 = left + 75 * s, py1 = top + 78 * s;
		float cx = (px0 + px1) * 0.5f, cy = py0 + (py1 - py0) * 0.35f;
		mcdraw::End2D(); // the preview sets up its own projection
		PlayerPreview(px0, py0, px1, py1, (cx - g_mx) / s, (cy - g_my) / s);
		mcdraw::Begin2D(g_cl.screenW, g_cl.screenH);
	}
	static const char* empty[4] = {"gui/sprites/container/slot/helmet", "gui/sprites/container/slot/chestplate",
		"gui/sprites/container/slot/leggings", "gui/sprites/container/slot/boots"};
	int hover = InvHoverSlot();
	for (int slot = 0; slot <= 44; slot++)
	{
		int x, y;
		if (!SlotPos(slot, x, y))
			continue;
		float sx = left + x * s, sy = top + y * s;
		int id, count, dmg;
		SlotItem(slot, id, count, dmg);
		if (slot >= 5 && slot <= 8 && id <= 0)
			mcdraw::BlitFull(empty[slot - 5], sx, sy, 16 * s, 16 * s);
		if (slot >= 36 && slot <= 44)
			DrawSlotItem(slot - 36, sx, sy, s); // CS weapon icons in slots 1-5
		else
			DrawStack(id, count, sx, sy, s);
		if (hover == slot)
			mcdraw::Rect(sx, sy, 16 * s, 16 * s, 0xFFFFFF80);
	}
	// trash slot (not in Minecraft's survival screen: drop junk here to delete it)
	{
		float tx = left + (INV_W + 6) * s, ty = top + 142 * s;
		mcdraw::Rect(tx - 4 * s, ty - 4 * s, 24 * s, 24 * s, 0xC6C6C6FF);
		mcdraw::Rect(tx - 4 * s, ty - 4 * s, 24 * s, 1 * s, 0xFFFFFFFF);
		mcdraw::Rect(tx - 4 * s, ty - 4 * s, 1 * s, 24 * s, 0xFFFFFFFF);
		mcdraw::Rect(tx - 4 * s, ty + 19 * s, 24 * s, 1 * s, 0x555555FF);
		mcdraw::Rect(tx + 19 * s, ty - 4 * s, 1 * s, 24 * s, 0x555555FF);
		mcdraw::Rect(tx - 1 * s, ty - 1 * s, 18 * s, 18 * s, 0x8B8B8BFF);
		mcdraw::BlitFull("item/barrier", tx, ty, 16 * s, 16 * s);
		if (hover == 46)
		{
			mcdraw::Rect(tx, ty, 16 * s, 16 * s, 0xFFFFFF80);
			if (g_cl.cursorId <= 0)
				Tooltip("Trash", 0xFF5555FF, "Drop an item here to delete it", s);
		}
	}
	// the stack on the cursor
	if (g_cl.cursorId != 0)
		DrawStack(g_cl.cursorId, g_cl.cursorCount, g_mx - 8 * s, g_my - 8 * s, s);
	else if (hover >= 0)
	{
		int id, count, dmg;
		SlotItem(hover, id, count, dmg);
		if (id > 0)
		{
			g_tipEnch = (hover >= 5 && hover <= 45) ? g_cl.ench[hover] : 0;
			ItemTooltip(id, nullptr, s);
			g_tipEnch = 0;
		}
		else if (id < 0)
			Tooltip(CsWeaponDisplayName(-id), 0xFFAA00FF, "Counter-Strike weapon: move it anywhere, throw it out to drop it", s);
	}
	DrawCursor();
	mcdraw::End2D();
}

static bool InventoryKey(int keynum, const char* binding)
{
	if (keynum == K_MOUSE1 || keynum == K_MOUSE2)
	{
		int slot = InvHoverSlot();
		if (slot == -1 || slot == 45)
			return false;
		char cmd[64];
		bool sh = g_forceShift >= 0 ? g_forceShift != 0 : ShiftDown();
		snprintf(cmd, sizeof(cmd), "mc_click %d %d %d", slot, keynum == K_MOUSE2 ? 1 : 0, sh ? 1 : 0);
		gEngfuncs.pfnServerCmd(cmd);
		return false;
	}
	if (keynum == 'q')
	{
		int slot = InvHoverSlot();
		if (slot >= 0)
		{
			char cmd[48];
			snprintf(cmd, sizeof(cmd), "mc_invdrop %d %d", slot, CtrlDown() ? 1 : 0);
			gEngfuncs.pfnServerCmd(cmd);
		}
		return false;
	}
	if (keynum == K_ESCAPE || keynum == 'e' || (binding && !strcmp(binding, "buy")))
		GuiHide();
	return false;
}

// test aid: mc_guicursor grid <col> <row> | tab <n> | hotbar <i> | button | ench <offer 0-2> | close
static void Cmd_GuiCursor()
{
	Layout L = GetLayout();
	const char* kind = gEngfuncs.Cmd_Argv(1);
	int a = atoi(gEngfuncs.Cmd_Argv(2)), b = atoi(gEngfuncs.Cmd_Argv(3));
	if (!strcmp(kind, "grid"))
	{
		g_mx = L.left + (GRID_X + a * 18 + 8) * L.s;
		g_my = L.top + (GRID_Y + b * 18 + 8) * L.s;
	}
	else if (!strcmp(kind, "hotbar"))
	{
		g_mx = L.left + (GRID_X + a * 18 + 8) * L.s;
		g_my = L.top + (HOTBAR_Y + 8) * L.s;
	}
	else if (!strcmp(kind, "tab") && a >= 0 && a < NUM_TABS)
	{
		float x, y, w, h;
		TabRect(L, a, x, y, w, h);
		g_mx = x + w * 0.5f;
		g_my = y + h * 0.5f;
	}
	else if (!strcmp(kind, "invslot"))
	{
		int x, y;
		float sc, left, top;
		InvLayout(sc, left, top);
		if (SlotPos(a, x, y))
		{
			g_mx = left + (x + 8) * sc;
			g_my = top + (y + 8) * sc;
		}
	}
	else if (!strcmp(kind, "click"))
	{
		// test aid: mc_guicursor click <button 0/1> <shift 0/1>, a mouse click without the mouse
		g_forceShift = b;
		GuiKey(1, a ? K_MOUSE2 : K_MOUSE1, nullptr);
		g_forceShift = -1;
	}
	else if (!strcmp(kind, "ench"))
	{
		// the enchanting screen: the middle of an offer
		float s, left, top;
		EnchLayout(s, left, top);
		g_mx = left + (60 + 54) * s;
		g_my = top + (14 + 19 * a + 9) * s;
	}
	else if (!strcmp(kind, "close"))
		GuiHide();
	else if (!strcmp(kind, "button"))
	{
		float x, y, w, h;
		ButtonRect(L, x, y, w, h);
		g_mx = x + w * 0.5f;
		g_my = y + h * 0.5f;
	}
}

static void Cmd_Inventory()
{
	if (g_open && g_kind == 1)
		GuiHide();
	else if (g_cl.health > 0)
		GuiShowInventory();
}

void GuiInit()
{
	gEngfuncs.pfnAddCommand((char*)"mc_guicursor", Cmd_GuiCursor);
	// what Shift+B does: the Minecraft item menu (bindable)
	gEngfuncs.pfnAddCommand((char*)"mc_itemmenu", [] {
		if (g_cl.health > 0)
			GuiShow();
	});
	gEngfuncs.pfnAddCommand((char*)"mc_inventory", Cmd_Inventory); // bindable; Shift+E opens it too
}

void GuiPreCreateMove()
{
	gEngfuncs.GetViewAngles(g_preAngles);
}

// The official client has just applied this frame's mouse motion to the view: turn it into cursor
// motion (inverting IN_MouseMove: yaw -= m_yaw * dx * sens, pitch += m_pitch * dy * sens) and undo it.
void GuiCreateMove(usercmd_t* cmd)
{
	if (!g_open)
		return;
	float now[3];
	gEngfuncs.GetViewAngles(now);
	float dyaw = now[1] - g_preAngles[1];
	while (dyaw > 180.0f)
		dyaw -= 360.0f;
	while (dyaw < -180.0f)
		dyaw += 360.0f;
	float dpitch = now[0] - g_preAngles[0];
	float sens = gEngfuncs.pfnGetCvarFloat((char*)"sensitivity");
	float myaw = gEngfuncs.pfnGetCvarFloat((char*)"m_yaw");
	float mpitch = gEngfuncs.pfnGetCvarFloat((char*)"m_pitch");
	if (sens <= 0.0f)
		sens = 3.0f;
	if (myaw == 0.0f)
		myaw = 0.022f;
	if (mpitch == 0.0f)
		mpitch = 0.022f;
	g_mx += -dyaw / (myaw * sens);
	g_my += dpitch / (mpitch * sens);
	g_mx = fmaxf(0.0f, fminf((float)g_cl.screenW - 1, g_mx));
	g_my = fmaxf(0.0f, fminf((float)g_cl.screenH - 1, g_my));
	gEngfuncs.SetViewAngles(g_preAngles);
	cmd->viewangles[0] = g_preAngles[0];
	cmd->viewangles[1] = g_preAngles[1];
	cmd->viewangles[2] = g_preAngles[2];
	cmd->forwardmove = cmd->sidemove = cmd->upmove = 0.0f;
	cmd->buttons &= ~(IN_ATTACK | IN_ATTACK2 | IN_JUMP | IN_DUCK | IN_USE | IN_RELOAD | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
}
// ---------------------------------------------------------------------------------------------
// The enchanting table's screen (EnchantmentScreen): the item slot, the lapis lazuli slot, three offers
// written in the Standard Galactic Alphabet with the level each asks for, and the player's inventory. The
// server decides everything (mc_enchant_srv.cpp); this draws what it sent and sends the clicks back. The
// item stays in the inventory on the server; the screen shows it in the table's slot instead.

static const int ENCH_W = 176, ENCH_H = 166;

static void EnchLayout(float& s, float& left, float& top)
{
	s = GuiScale();
	left = floorf((g_cl.screenW - ENCH_W * s) * 0.5f);
	top = floorf((g_cl.screenH - ENCH_H * s) * 0.5f);
}

// 9..44: an inventory slot (storage, hotbar); 100: the item slot; 101: the lapis slot; 200..202: an offer
static int EnchHover()
{
	float s, left, top;
	EnchLayout(s, left, top);
	for (int slot = 9; slot <= 44; slot++)
	{
		int x = slot <= 35 ? 8 + ((slot - 9) % 9) * 18 : 8 + (slot - 36) * 18, y = slot <= 35 ? 84 + ((slot - 9) / 9) * 18 : 142;
		if (Inside(g_mx, g_my, left + x * s, top + y * s, 16 * s, 16 * s))
			return slot;
	}
	if (Inside(g_mx, g_my, left + 15 * s, top + 47 * s, 16 * s, 16 * s))
		return 100;
	if (Inside(g_mx, g_my, left + 35 * s, top + 47 * s, 16 * s, 16 * s))
		return 101;
	for (int i = 0; i < 3; i++)
		if (Inside(g_mx, g_my, left + 60 * s, top + (14 + 19 * i) * s, 108 * s, 19 * s))
			return 200 + i;
	return -1;
}

static int EnchLapis()
{
	int lapis = mci::FindItem("lapis_lazuli"), n = 0;
	for (int i = 0; i < 27; i++)
		if (g_cl.invId[i] == lapis)
			n += g_cl.invCount[i];
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
		if (g_cl.hotbarId[i] == lapis)
			n += g_cl.hotbarCount[i];
	return n;
}

static bool EnchCan(int i, int lapis)
{
	int level = g_cl.enchLevel[i];
	return level > 0 && (g_cl.creative || (g_cl.xpLevel >= level && g_cl.xpLevel >= i + 1 && lapis >= i + 1));
}

// Text in the Standard Galactic Alphabet (font/ascii_sga.png: the same 16x16 grid as the main font),
// wrapped at maxW font pixels over at most two lines
static void DrawSga(const char* text, float x, float y, float s, unsigned color, float maxW)
{
	float cx = 0.0f, cy = 0.0f;
	for (const char* p = text; *p; p++)
	{
		if (*p == ' ')
		{
			// wrap before a word that would not fit
			float w = 0.0f;
			for (const char* q = p + 1; *q && *q != ' '; q++)
				w += 6.0f;
			cx += 4.0f;
			if (cx + w > maxW)
			{
				cx = 0.0f;
				cy += 9.0f;
				if (cy > 9.0f)
					return;
			}
			continue;
		}
		int c = (unsigned char)*p;
		float u = (float)((c % 16) * 8), v = (float)((c / 16) * 8);
		mcdraw::Blit("font/ascii_sga", x + cx * s, y + cy * s, 8 * s, 8 * s, u, v, u + 8, v + 8, color);
		cx += 6.0f;
	}
}

// EnchantmentNames: a few words picked by the offer's seed
static void EnchWords(unsigned seed, char* out, size_t n)
{
	static const char* words[] = {"the", "elder", "scrolls", "klaatu", "berata", "niktu", "xyzzy", "bless", "curse", "light", "darkness", "fire", "air",
		"earth", "water", "hot", "dry", "cold", "wet", "ignite", "snuff", "embiggen", "twist", "shorten", "stretch", "fiddle", "destroy", "imbue", "galvanize",
		"enchant", "free", "limited", "range", "of", "towards", "inside", "sphere", "cube", "self", "other", "ball", "mental", "physical", "grow", "shrink",
		"demon", "elemental", "spirit", "animal", "creature", "beast", "humanoid", "undead", "fresh", "stale", "phnglui", "mglwnafh", "cthulhu", "rlyeh",
		"wgahnagl", "fhtagn", "baguette"};
	const int count = sizeof(words) / sizeof(words[0]);
	out[0] = 0;
	unsigned r = seed * 2654435761u + 12345u;
	int k = 3 + (int)((r >> 20) % 2);
	for (int i = 0; i < k; i++)
	{
		r = r * 1664525u + 1013904223u;
		if (i)
			strncat(out, " ", n - strlen(out) - 1);
		strncat(out, words[(r >> 10) % count], n - strlen(out) - 1);
	}
}

// What the server let us see of an offer: "Sharpness IV"
static bool EnchClue(int i, char* out, size_t n)
{
	static const char* roman[8] = {"", "I", "II", "III", "IV", "V", "VI", "VII"};
	int id = 0, count, dmg;
	if (g_cl.enchSlot >= 9 && g_cl.enchSlot <= 44)
		SlotItem(g_cl.enchSlot, id, count, dmg);
	else if (g_cl.enchSlot >= 5 && g_cl.enchSlot <= 8)
		id = g_cl.armorId[g_cl.enchSlot - 5];
	if (id <= 0 || !g_cl.enchClueLevel[i])
		return false;
	const mci::ItemDef& d = mci::Item(id);
	int kind = g_cl.enchClueKind[i];
	const char* name = kind == 0 ? mce::MainName(d) : kind == 1 ? "Unbreaking" : kind == 2 ? mce::FireName(d) : mce::KnockName(d);
	if (!name)
		return false;
	snprintf(out, n, "%s %s", name, roman[g_cl.enchClueLevel[i] & 7]);
	return true;
}

static void DrawEnchantScreen()
{
	float s, left, top;
	EnchLayout(s, left, top);
	mcdraw::Begin2D(g_cl.screenW, g_cl.screenH);
	mcdraw::Rect(0, 0, (float)g_cl.screenW, (float)g_cl.screenH, 0x101010B0);
	mcdraw::Blit("gui/container/enchanting_table", left, top, ENCH_W * s, ENCH_H * s, 0, 0, ENCH_W, ENCH_H);
	mcdraw::Text("Enchant", left + 12 * s, top + 5 * s, s, 0x404040FF, false);
	mcdraw::Text("Inventory", left + 8 * s, top + 72 * s, s, 0x404040FF, false);
	int hover = EnchHover(), lapis = EnchLapis();

	// the item in the table's slot, and the lapis lazuli
	int tid = 0, tcount = 0, tdmg = 0;
	if (g_cl.enchSlot >= 9 && g_cl.enchSlot <= 44)
		SlotItem(g_cl.enchSlot, tid, tcount, tdmg);
	else if (g_cl.enchSlot >= 5 && g_cl.enchSlot <= 8)
		tid = g_cl.armorId[g_cl.enchSlot - 5];
	if (tid > 0)
		DrawStack(tid, 1, left + 15 * s, top + 47 * s, s);
	if (lapis > 0)
		DrawStack(mci::FindItem("lapis_lazuli"), lapis > 64 ? 64 : lapis, left + 35 * s, top + 47 * s, s);
	else
		mcdraw::BlitFull("gui/sprites/container/slot/lapis_lazuli", left + 35 * s, top + 47 * s, 16 * s, 16 * s);
	if (hover == 100 || hover == 101)
		mcdraw::Rect(left + (hover == 100 ? 15 : 35) * s, top + 47 * s, 16 * s, 16 * s, 0xFFFFFF80);

	// the three offers
	for (int i = 0; i < 3; i++)
	{
		float x = left + 60 * s, y = top + (14 + 19 * i) * s;
		int level = g_cl.enchLevel[i];
		if (!level || tid <= 0)
		{
			mcdraw::BlitFull("gui/sprites/container/enchanting_table/enchantment_slot_disabled", x, y, 108 * s, 19 * s);
			continue;
		}
		bool can = EnchCan(i, lapis), hot = can && hover == 200 + i;
		mcdraw::BlitFull(!can ? "gui/sprites/container/enchanting_table/enchantment_slot_disabled"
							  : hot ? "gui/sprites/container/enchanting_table/enchantment_slot_highlighted" : "gui/sprites/container/enchanting_table/enchantment_slot",
			x, y, 108 * s, 19 * s);
		char icon[80], words[96], num[8];
		snprintf(icon, sizeof(icon), "gui/sprites/container/enchanting_table/level_%d%s", i + 1, can ? "" : "_disabled");
		mcdraw::BlitFull(icon, x + 1 * s, y + 1 * s, 16 * s, 16 * s);
		EnchWords(g_cl.enchSeed + (unsigned)i * 977u, words, sizeof(words));
		DrawSga(words, x + 20 * s, y + 2 * s, s, !can ? 0x342F25FF : hot ? 0xFFFF80FF : 0x685E4AFF, 86.0f);
		snprintf(num, sizeof(num), "%d", level);
		mcdraw::Text(num, x + (106 - mcdraw::TextWidth(num)) * s, y + 9 * s, s, can ? 0x80FF20FF : 0x407F10FF);
	}

	// the inventory, without the item that sits in the table
	for (int slot = 9; slot <= 44; slot++)
	{
		int x = slot <= 35 ? 8 + ((slot - 9) % 9) * 18 : 8 + (slot - 36) * 18, y = slot <= 35 ? 84 + ((slot - 9) / 9) * 18 : 142;
		float sx = left + x * s, sy = top + y * s;
		if (slot != g_cl.enchSlot)
		{
			if (slot >= 36)
				DrawSlotItem(slot - 36, sx, sy, s);
			else
			{
				int id, count, dmg;
				SlotItem(slot, id, count, dmg);
				DrawStack(id, count, sx, sy, s);
			}
		}
		if (hover == slot)
			mcdraw::Rect(sx, sy, 16 * s, 16 * s, 0xFFFFFF80);
	}

	// tooltips
	if (hover >= 200 && tid > 0 && g_cl.enchLevel[hover - 200])
	{
		int i = hover - 200, level = g_cl.enchLevel[i];
		TipLine l[5];
		int n = 0;
		char clue[48];
		if (EnchClue(i, clue, sizeof(clue)))
		{
			snprintf(l[n].text, sizeof(l[n].text), "%s . . . ?", clue);
			l[n++].color = 0xFFFFFFFF;
		}
		if (!g_cl.creative && g_cl.xpLevel < level)
		{
			snprintf(l[n].text, sizeof(l[n].text), "Level Requirement: %d", level);
			l[n++].color = 0xFF5555FF;
		}
		else
		{
			snprintf(l[n].text, sizeof(l[n].text), "%d Lapis Lazuli", i + 1);
			l[n++].color = (g_cl.creative || lapis >= i + 1) ? 0xAAAAAAFF : 0xFF5555FF;
			snprintf(l[n].text, sizeof(l[n].text), i == 0 ? "%d Enchantment Level" : "%d Enchantment Levels", i + 1);
			l[n++].color = 0xAAAAAAFF;
		}
		TooltipLines(l, n, s);
	}
	else if (hover >= 9 && hover <= 44 && hover != g_cl.enchSlot)
	{
		int id, count, dmg;
		SlotItem(hover, id, count, dmg);
		if (id > 0)
		{
			g_tipEnch = g_cl.ench[hover];
			ItemTooltip(id, !g_tipEnch && mce::Enchantable(mci::Item(id)) ? "Click to put it on the table" : nullptr, s);
			g_tipEnch = 0;
		}
	}
	else if (hover == 100 && tid > 0)
	{
		g_tipEnch = g_cl.ench[g_cl.enchSlot];
		ItemTooltip(tid, "Click to take it back", s);
		g_tipEnch = 0;
	}
	else if (hover == 101)
		Tooltip("Lapis Lazuli", 0xFFFFFFFF, "An offer costs 1, 2 or 3 of it, from your inventory", s);
	DrawCursor();
	mcdraw::End2D();
}

// Returns false when the key was consumed by the screen.
static bool EnchantKey(int keynum, const char* binding)
{
	if (keynum == K_ESCAPE || keynum == 'e')
	{
		GuiHide();
		return false;
	}
	if (keynum != K_MOUSE1 && keynum != K_MOUSE2)
		return false;
	int hover = EnchHover();
	char cmd[48];
	if (hover >= 9 && hover <= 44)
	{
		int id, count, dmg;
		SlotItem(hover, id, count, dmg);
		if (id > 0 && !g_cl.ench[hover] && mce::Enchantable(mci::Item(id)))
		{
			snprintf(cmd, sizeof(cmd), "mc_ench_item %d", hover);
			gEngfuncs.pfnServerCmd(cmd);
			ClickSound();
		}
	}
	else if (hover == 100 && g_cl.enchSlot)
	{
		gEngfuncs.pfnServerCmd((char*)"mc_ench_item 0");
		ClickSound();
	}
	else if (hover >= 200 && EnchCan(hover - 200, EnchLapis()))
	{
		snprintf(cmd, sizeof(cmd), "mc_ench_pick %d", hover - 200);
		gEngfuncs.pfnServerCmd(cmd);
	}
	return false;
}

// The server opened or closed the table (MCMSG_ENCHUI)
void GuiEnchant(bool open)
{
	if (open)
	{
		if (g_open && g_kind == 2)
			return;
		if (g_open)
			GuiHide();
		g_kind = 2;
		g_mx = g_cl.screenW * 0.5f;
		g_my = g_cl.screenH * 0.5f;
		gEngfuncs.GetViewAngles(g_savedAngles);
		float level[3] = {0.0f, g_savedAngles[1], 0.0f};
		gEngfuncs.SetViewAngles(level);
		g_open = true;
	}
	else if (g_open && g_kind == 2)
		GuiHide();
}

} // namespace mc
