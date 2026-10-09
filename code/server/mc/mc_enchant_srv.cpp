// Enchanting. Right-click an enchanting table: its screen opens (mc_gui.cpp on the client). Put an item
// from the inventory on the table and pick one of the three offers. As in Minecraft, the offers ask for a
// level that grows with the bookshelves around the table (up to 15, two cells out with air between) and
// cost 1, 2 or 3 levels and as many lapis lazuli; what an offer gives comes from the item's enchantability
// and the level (mc_enchant.cpp), and only one of its enchantments is shown beforehand. With mc_gui 0 a
// numbered text menu does the same job.
// What the enchantments do is applied where the damage, wear and digging are worked out (EnchantXxx below).
#include "precompiled.h"

#include "mc_server.h"
#include "mc_blocks.h"
#include "mc_classic.h"
#include "mc_enchant.h"

namespace mc
{
void ShowMenuText(CBasePlayer* pl, int keys, const char* text); // mc_main.cpp
extern bool HasFloor(int x, int y, int z);
extern mcc::Classic* ClassicWorld();

static int msgEnch = 0, msgEnchUi = 0;
static const int MENU_ITEMS = 40, MENU_OFFERS = 41;
static const float B2U = 40.0f;

struct EnchantState
{
	int table[3] = {0, 0, 0};
	unsigned seed = 0;
	mci::Stack* pick[8] = {};
	int numPick = 0;
	mci::Stack* item = nullptr;
	int slot = 0; // the item's inventory-screen slot while the screen is open (0: nothing on the table)
	int levels[3] = {0, 0, 0};
	uint16_t offer[3] = {0, 0, 0};
};
static EnchantState g_ench[MAX_CLIENTS + 1];

void EnchantInit()
{
	msgEnch = REG_USER_MSG(MCMSG_ENCH, -1);
	msgEnchUi = REG_USER_MSG(MCMSG_ENCHUI, -1);
}

// The enchanted slots of a player's inventory, numbered as on the inventory screen (5-8 armor, 9-35
// storage, 36-44 hotbar, 45 the cursor). Sent with every inventory update.
void SendEnchants(CBasePlayer* pl)
{
	if (!pl || pl->IsBot() || !msgEnch)
		return;
	McPlayer& mp = P(pl);
	int slots[48], n = 0;
	uint16_t ench[48];
	auto add = [&](int slot, const mci::Stack& s) {
		if (!s.Empty() && s.ench && n < 48)
		{
			slots[n] = slot;
			ench[n++] = s.ench;
		}
	};
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
		add(5 + i, mp.armor[i]);
	for (int i = 0; i < 27; i++)
		add(9 + i, mp.inv[i]);
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
		add(36 + i, mp.hotbar[i]);
	add(45, mp.cursor);
	MESSAGE_BEGIN(MSG_ONE, msgEnch, nullptr, pl->edict());
	WRITE_BYTE(n);
	for (int i = 0; i < n; i++)
	{
		WRITE_BYTE(slots[i]);
		WRITE_SHORT(ench[i]);
	}
	MESSAGE_END();
}

// EnchantmentTableBlock.BOOKSHELF_OFFSETS: the ring two cells out, on the table's level and the one above,
// each shelf counting when the cell between it and the table is empty
int BookshelvesAround(int x, int y, int z)
{
	int shelf = mcw::FindBlock("bookshelf"), n = 0;
	for (int h = 0; h < 2; h++)
		for (int dx = -2; dx <= 2; dx++)
			for (int dy = -2; dy <= 2; dy++)
			{
				if (abs(dx) != 2 && abs(dy) != 2)
					continue;
				if ((int)mcw::CellType(g_world.Get(x + dx, y + dy, z + h)) != shelf)
					continue;
				mcw::Cell between = g_world.Get(x + dx / 2, y + dy / 2, z + h);
				mcw::LocalBox lb[4];
				if (!between || mcw::ShapeBoxes(g_world.ShapeAt(x + dx / 2, y + dy / 2, z + h), mcw::CellState(between), lb) == 0)
					n++;
			}
	return n > 15 ? 15 : n;
}

static void ShowOffers(CBasePlayer* pl)
{
	EnchantState& st = g_ench[pl->entindex()];
	McPlayer& mp = P(pl);
	if (!st.item || st.item->Empty())
		return;
	const mci::ItemDef& d = mci::Item(st.item->id);
	int shelves = BookshelvesAround(st.table[0], st.table[1], st.table[2]);
	mce::SlotLevels(shelves, st.seed, st.levels);
	char buf[512];
	Q_snprintf(buf, sizeof(buf), "\\yEnchant %s\\w  (%d bookshel%s, you are level %d)\n\n", d.display, shelves, shelves == 1 ? "f" : "ves", mp.xpLevel);
	int keys = (1 << 8) | (1 << 9);
	for (int i = 0; i < 3; i++)
	{
		char lines[4][32], line[96];
		if (!st.levels[i])
		{
			// too few bookshelves for this slot this time
			st.offer[i] = 0;
			Q_snprintf(line, sizeof(line), "\\d%d. -\\w\n", i + 1);
			Q_strlcat(buf, line);
			continue;
		}
		st.offer[i] = mce::Roll(d, st.levels[i], st.seed + (unsigned)i * 977u);
		if (!st.offer[i])
			st.offer[i] = mce::Make(1, 0, 0, 0);
		mce::Describe(d, st.offer[i], lines);
		bool can = mp.creative || (mp.xpLevel >= st.levels[i] && mp.xpLevel >= i + 1);
		// like the table: one enchantment is shown, the rest is a surprise
		Q_snprintf(line, sizeof(line), "%s%d. %s . . . ?  \\d(level %d, costs %d)\\w\n", can ? "\\w" : "\\d", i + 1, lines[0], st.levels[i], i + 1);
		Q_strlcat(buf, line);
		if (can)
			keys |= 1 << i;
	}
	Q_strlcat(buf, "\n9. Back\n0. Close");
	mp.menu = MENU_OFFERS;
	ShowMenuText(pl, keys, buf);
}

static bool CanEnchant(const mci::Stack& s)
{
	return !s.Empty() && !mci::IsCsToken(s.id) && !s.ench && mce::Enchantable(mci::Item(s.id));
}

static void Apply(CBasePlayer* pl, mci::Stack& item, uint16_t ench, int cost, const float* at);

// ---------------------------------------------------------------------------------------------
// The screen

// The client's mc_gui decides between the screen and the text menu (a listen server shares its cvars)
static bool UsesScreen()
{
	static cvar_t* cv = nullptr;
	if (!cv)
		cv = CVAR_GET_POINTER("mc_gui");
	return msgEnchUi && (!cv || cv->value != 0.0f);
}

static mci::Stack* SlotStack(McPlayer& mp, int slot)
{
	if (slot >= 9 && slot <= 35)
		return &mp.inv[slot - 9];
	if (slot >= 36 && slot <= 44)
		return &mp.hotbar[slot - 36];
	return nullptr;
}

// Lapis lazuli in the storage and the hotbar; take > 0 removes that many
static int Lapis(McPlayer& mp, int take)
{
	int lapis = mci::FindItem("lapis_lazuli"), n = 0;
	for (int slot = 9; slot <= 44; slot++)
	{
		mci::Stack& s = *SlotStack(mp, slot);
		if (s.Empty() || s.id != lapis)
			continue;
		n += s.count;
		int t = min(take, (int)s.count);
		take -= t;
		s.count -= (uint8_t)t;
		if (!s.count)
			s = mci::Stack();
	}
	return n;
}

// The three offers for the item on the table (nothing on it, or nothing to enchant: none)
static void Offers(CBasePlayer* pl)
{
	EnchantState& st = g_ench[pl->entindex()];
	for (int i = 0; i < 3; i++)
		st.levels[i] = st.offer[i] = 0;
	if (!st.item || !CanEnchant(*st.item))
		return;
	const mci::ItemDef& d = mci::Item(st.item->id);
	mce::SlotLevels(BookshelvesAround(st.table[0], st.table[1], st.table[2]), st.seed, st.levels);
	for (int i = 0; i < 3; i++)
		if (st.levels[i])
		{
			st.offer[i] = mce::Roll(d, st.levels[i], st.seed + (unsigned)i * 977u);
			if (!st.offer[i])
				st.offer[i] = mce::Make(1, 0, 0, 0);
		}
}

static void SendUi(CBasePlayer* pl, bool open)
{
	EnchantState& st = g_ench[pl->entindex()];
	Offers(pl);
	MESSAGE_BEGIN(MSG_ONE, msgEnchUi, nullptr, pl->edict());
	WRITE_BYTE(open ? 1 : 0);
	WRITE_BYTE(st.slot);
	for (int i = 0; i < 3; i++)
	{
		// the one enchantment shown beforehand: the first of main, fire, knockback, Unbreaking
		uint16_t e = st.offer[i];
		int kind = mce::Main(e) ? 0 : mce::Fire(e) ? 2 : mce::Knock(e) ? 3 : 1;
		int level = kind == 0 ? mce::Main(e) : kind == 2 ? mce::Fire(e) : kind == 3 ? mce::Knock(e) : mce::Unbreaking(e);
		WRITE_BYTE(st.levels[i]);
		WRITE_BYTE(kind);
		WRITE_BYTE(e ? level : 0);
	}
	WRITE_LONG((int)st.seed);
	MESSAGE_END();
}

// Right-click on an enchanting table
void EnchantOpen(CBasePlayer* pl, int x, int y, int z)
{
	EnchantState& st = g_ench[pl->entindex()];
	McPlayer& mp = P(pl);
	st.table[0] = x;
	st.table[1] = y;
	st.table[2] = z;
	if (!st.seed)
		st.seed = (unsigned)RANDOM_LONG(1, 0x7FFFFFFF);
	if (UsesScreen())
	{
		// the held item goes on the table if it can be enchanted
		bool held = mp.mcItemActive && mp.selected >= 0 && mp.selected < mcp::HOTBAR_SIZE && CanEnchant(mp.hotbar[mp.selected]);
		st.slot = held ? 36 + mp.selected : 0;
		st.item = held ? &mp.hotbar[mp.selected] : nullptr;
		FxSwing(pl->entindex());
		SendUi(pl, true);
		return;
	}
	st.numPick = 0;
	// the held item first, then the rest of the hotbar, then the armor worn
	if (mp.mcItemActive && mp.selected >= 0 && mp.selected < mcp::HOTBAR_SIZE && CanEnchant(mp.hotbar[mp.selected]))
		st.pick[st.numPick++] = &mp.hotbar[mp.selected];
	for (int i = 0; i < mcp::HOTBAR_SIZE && st.numPick < 8; i++)
		if (CanEnchant(mp.hotbar[i]) && !(st.numPick && st.pick[0] == &mp.hotbar[i]))
			st.pick[st.numPick++] = &mp.hotbar[i];
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS && st.numPick < 8; i++)
		if (CanEnchant(mp.armor[i]))
			st.pick[st.numPick++] = &mp.armor[i];
	FxSwing(pl->entindex());
	if (!st.numPick)
	{
		Toast(pl, 1, "Nothing to enchant: bring a sword, axe, bow, tool or armor that is not enchanted yet");
		return;
	}
	char buf[512];
	Q_snprintf(buf, sizeof(buf), "\\yEnchanting table\\w  (you are level %d)\n\n", mp.xpLevel);
	int keys = 1 << 9;
	for (int i = 0; i < st.numPick; i++)
	{
		char line[64];
		Q_snprintf(line, sizeof(line), "%d. %s\n", i + 1, mci::Item(st.pick[i]->id).display);
		Q_strlcat(buf, line);
		keys |= 1 << i;
	}
	Q_strlcat(buf, "\n0. Close");
	mp.menu = MENU_ITEMS;
	ShowMenuText(pl, keys, buf);
}

// Spend `cost` levels (Player.onEnchantmentPerformed: giveExperienceLevels(-cost))
static void TakeLevels(CBasePlayer* pl, int cost)
{
	McPlayer& mp = P(pl);
	if (mp.creative)
		return;
	mp.xpLevel = max(0, mp.xpLevel - cost);
	mp.xpTotal = mci::XpTotalAtLevel(mp.xpLevel);
	mp.statDirty = true;
}

static void Apply(CBasePlayer* pl, mci::Stack& item, uint16_t ench, int cost, const float* at)
{
	item.ench = ench;
	TakeLevels(pl, cost);
	P(pl).invDirty = true;
	g_ench[pl->entindex()].seed = (unsigned)RANDOM_LONG(1, 0x7FFFFFFF); // the next offers are new ones
	FxSound(mcs::MCS_ENCHANT, at, 1.0f, 0.9f + RANDOM_FLOAT(0.0f, 0.1f));
	char lines[4][32], all[160] = "";
	int n = mce::Describe(mci::Item(item.id), ench, lines);
	for (int i = 0; i < n; i++)
	{
		Q_strlcat(all, i ? ", " : "");
		Q_strlcat(all, lines[i]);
	}
	if (!pl->IsBot())
		Toast(pl, 0, "Enchanted your %s: \\aq%s", mci::Item(item.id).display, all);
	McLog("enchant: %s's %s gets %s for %d level%s (level %d left)", STRING(pl->pev->netname), mci::Item(item.id).name, all, cost, cost == 1 ? "" : "s",
		P(pl).xpLevel);
}

bool EnchantMenuSelect(CBasePlayer* pl, int menu, int key)
{
	if (menu != MENU_ITEMS && menu != MENU_OFFERS)
		return false;
	EnchantState& st = g_ench[pl->entindex()];
	McPlayer& mp = P(pl);
	Vector tc(g_world.origin[0] + (st.table[0] + 0.5f) * B2U, g_world.origin[1] + (st.table[1] + 0.5f) * B2U, g_world.origin[2] + (st.table[2] + 0.5f) * B2U);
	// walked away, or the table is gone
	if ((pl->pev->origin - tc).Length() > 5.0f * B2U ||
		(int)mcw::CellType(g_world.Get(st.table[0], st.table[1], st.table[2])) != mcw::FindBlock("enchanting_table"))
		return true;
	if (menu == MENU_ITEMS)
	{
		if (key >= 1 && key <= st.numPick && CanEnchant(*st.pick[key - 1]))
		{
			st.item = st.pick[key - 1];
			ShowOffers(pl);
		}
		return true;
	}
	if (key == 9)
	{
		EnchantOpen(pl, st.table[0], st.table[1], st.table[2]);
		return true;
	}
	int i = key - 1;
	if (i < 0 || i > 2 || !st.item || !CanEnchant(*st.item) || !st.levels[i] || !st.offer[i])
		return true;
	if (!mp.creative && (mp.xpLevel < st.levels[i] || mp.xpLevel < i + 1))
	{
		Toast(pl, 1, "That needs level %d (you are level %d)", st.levels[i], mp.xpLevel);
		return true;
	}
	Apply(pl, *st.item, st.offer[i], i + 1, tc);
	return true;
}

// The screen's clicks: mc_ench_item <slot> (0 takes the item back), mc_ench_pick <0-2>, mc_ench_close
bool EnchantCommand(CBasePlayer* pl, const char* cmd)
{
	EnchantState& st = g_ench[pl->entindex()];
	McPlayer& mp = P(pl);
	if (!Q_strcmp(cmd, "mc_ench_close"))
	{
		st.slot = 0;
		st.item = nullptr;
		return true;
	}
	Vector tc(g_world.origin[0] + (st.table[0] + 0.5f) * B2U, g_world.origin[1] + (st.table[1] + 0.5f) * B2U, g_world.origin[2] + (st.table[2] + 0.5f) * B2U);
	if (!pl->IsAlive() || (pl->pev->origin - tc).Length() > 5.0f * B2U ||
		(int)mcw::CellType(g_world.Get(st.table[0], st.table[1], st.table[2])) != mcw::FindBlock("enchanting_table"))
	{
		// walked away, or the table is gone
		st.slot = 0;
		st.item = nullptr;
		SendUi(pl, false);
		return true;
	}
	if (!Q_strcmp(cmd, "mc_ench_item"))
	{
		int slot = atoi(CMD_ARGV(1));
		mci::Stack* s = SlotStack(mp, slot);
		bool ok = s && CanEnchant(*s);
		st.slot = ok ? slot : 0;
		st.item = ok ? s : nullptr;
		SendUi(pl, true);
		return true;
	}
	if (!Q_strcmp(cmd, "mc_ench_pick"))
	{
		int i = atoi(CMD_ARGV(1));
		Offers(pl);
		if (i < 0 || i > 2 || !st.item || !st.levels[i] || !st.offer[i])
			return true;
		if (!mp.creative && (mp.xpLevel < st.levels[i] || mp.xpLevel < i + 1 || Lapis(mp, 0) < i + 1))
		{
			Toast(pl, 1, mp.xpLevel < st.levels[i] ? "That needs level %d" : "That needs %d lapis lazuli", mp.xpLevel < st.levels[i] ? st.levels[i] : i + 1);
			return true;
		}
		if (!mp.creative)
			Lapis(mp, i + 1);
		Apply(pl, *st.item, st.offer[i], i + 1, tc);
		SendUi(pl, true); // the item stays on the table, enchanted: no more offers for it
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// What the enchantments do

// Sharpness: 0.5 per level + 0.5 Minecraft damage points (melee)
float EnchantMeleeBonus(const mci::Stack& held)
{
	if (held.Empty() || !held.ench || !mce::Main(held.ench))
		return 0.0f;
	int t = mci::Item(held.id).type;
	return (t == mci::IT_SWORD || t == mci::IT_AXE || t == mci::IT_MACE) ? 0.5f * mce::Main(held.ench) + 0.5f : 0.0f;
}

// Protection: 4% off any damage per level worn, at most 80% (EnchantmentHelper.getDamageProtection)
float EnchantProtection(CBasePlayer* pl)
{
	McPlayer& mp = P(pl);
	int levels = 0;
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
		if (!mp.armor[i].Empty() && mci::Item(mp.armor[i].id).type == mci::IT_ARMOR)
			levels += mce::Main(mp.armor[i].ench);
	return min(levels, 20) * 0.04f;
}

// Unbreaking: does this use wear the item? (tools 1 in level+1; armor 60% + 40% in level+1)
bool EnchantWears(const mci::Stack& s, bool armor)
{
	int lv = mce::Unbreaking(s.ench);
	if (!lv)
		return true;
	if (armor && RANDOM_FLOAT(0.0f, 1.0f) < 0.6f)
		return true;
	return RANDOM_LONG(0, lv) == 0;
}

// Fire Aspect / Flame: the victim burns (4 s per level of Fire Aspect, 5 s for Flame); the kill is the attacker's
void EnchantIgnite(CBaseEntity* victim, CBasePlayer* by, float seconds)
{
	if (!victim || !victim->IsPlayer() || seconds <= 0.0f)
		return;
	McPlayer& mp = P((CBasePlayer*)victim);
	if (mp.fireUntil < gpGlobals->time + seconds)
		mp.fireUntil = gpGlobals->time + seconds;
	mp.fireOwner = by ? by->entindex() : 0;
}

// ---------------------------------------------------------------------------------------------
// Bots: with levels to spend and gear that is not enchanted yet, a bot in a quiet moment sets down the
// enchanting table it bought and enchants one thing with the best offer it can pay for.
bool BotEnchant(CBasePlayer* bot, mci::Stack* gear[], int numGear)
{
	McPlayer& mp = P(bot);
	if (mp.xpLevel < 1)
		return false;
	mci::Stack* item = nullptr;
	for (int i = 0; i < numGear && !item; i++)
		if (gear[i] && CanEnchant(*gear[i]))
			item = gear[i];
	if (!item)
		return false;
	// a free cell on the floor beside it: ahead first, then round
	int table = mcw::FindBlock("enchanting_table"), c[3] = {0, 0, 0};
	Vector at;
	bool found = false;
	float feet = bot->pev->origin.z - ((bot->pev->flags & FL_DUCKING) ? 18.0f : 36.0f);
	for (int k = 0; k < 8 && !found && table >= 0; k++)
	{
		float yaw = (bot->pev->v_angle.y + k * 45.0f) * (float)M_PI / 180.0f;
		at = Vector(bot->pev->origin.x + cosf(yaw) * 56.0f, bot->pev->origin.y + sinf(yaw) * 56.0f, feet + 1.0f);
		float o[3] = {at.x, at.y, at.z};
		g_world.ToBlock(o, c);
		// the cell a player's placement would use: the one just above the floor, or the next one up when the
		// floor runs through the upper half of it (its middle is inside the map)
		mcc::Classic* cl = ClassicWorld();
		for (int up = 0; up < 2 && !found; up++, c[2]++)
		{
			if (!g_world.InBounds(c[0], c[1], c[2]) || g_world.Get(c[0], c[1], c[2]) || !HasFloor(c[0], c[1], c[2]))
				break;
			Vector ctr(g_world.origin[0] + (c[0] + 0.5f) * B2U, g_world.origin[1] + (c[1] + 0.5f) * B2U, g_world.origin[2] + (c[2] + 0.5f) * B2U);
			float p[3] = {ctr.x, ctr.y, ctr.z};
			found = !(cl && cl->PointContents(p) == mcb::CONT_SOLID);
			if (found)
				break;
		}
	}
	if (!found)
		return false;
	SetBlock(c[0], c[1], c[2], mcw::MakeCell((uint16_t)table, 0));
	FxSound(mcs::MCS_BLOCK_BASE + mcw::SOUND_STONE * 4 + 2, at, 1.0f, 0.8f);
	FxSwing(bot->entindex());
	// the best of the three offers it has the levels for
	int levels[3];
	unsigned seed = (unsigned)RANDOM_LONG(1, 0x7FFFFFFF);
	mce::SlotLevels(BookshelvesAround(c[0], c[1], c[2]), seed, levels);
	for (int i = 2; i >= 0; i--)
		if (levels[i] && mp.xpLevel >= levels[i] && mp.xpLevel >= i + 1)
		{
			uint16_t e = mce::Roll(mci::Item(item->id), levels[i], seed + (unsigned)i * 977u);
			Apply(bot, *item, e ? e : mce::Make(1, 0, 0, 0), i + 1, at);
			return true;
		}
	return true; // the table stands; not enough levels for any offer this time
}
} // namespace mc
