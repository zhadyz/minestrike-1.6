// The economy: Minecraft items are bought with Counter-Strike money, by players and bots alike, from one
// price list (mci::Price in mc_items.cpp), under Counter-Strike's own buying rules (in a buy zone, during
// buy time). Every way of getting an item outside creative mode is a purchase: the inventory screen (B),
// the number-key transfer, kits, the text menu, /give. What you carry when you die lands on the ground and
// is anyone's; what you carry when you survive is still yours next round.
// Also here: the standing of Minecraft weapons next to guns (their damage scale, and who holds one).
#include "precompiled.h"

#include "mc_server.h"

namespace mc
{
static cvar_t cv_economy = {"mc_economy", "1", FCVAR_SERVER, 1.0f, nullptr};        // 0: items are free, as in creative
static cvar_t cv_buyAnywhere = {"mc_buy_anywhere", "0", FCVAR_SERVER, 0.0f, nullptr}; // 1: no buy zone or buy time needed
static cvar_t cv_weaponScale = {"mc_weapon_scale", "1.5", FCVAR_SERVER, 1.5f, nullptr}; // Minecraft weapon damage against guns' 100 HP
static cvar_t cv_obsidianMax = {"mc_obsidian_max", "2", FCVAR_SERVER, 2.0f, nullptr};   // the most obsidian a player can buy up to (0: no limit)

void EconomyInit()
{
	CVAR_REGISTER(&cv_obsidianMax);
	CVAR_REGISTER(&cv_economy);
	CVAR_REGISTER(&cv_buyAnywhere);
	CVAR_REGISTER(&cv_weaponScale);
}

// Damage of swords, axes, the mace and arrows is Minecraft's own table, times this
float McWeaponScale() { return cv_weaponScale.value > 0.0f ? cv_weaponScale.value : 1.0f; }

// Is the player fighting with a Minecraft weapon or tool in hand?
bool HoldsMcWeapon(CBasePlayer* pl)
{
	const mci::Stack& s = HeldStack(pl);
	if (s.Empty() || mci::IsCsToken(s.id))
		return false;
	switch (mci::Item(s.id).type)
	{
	case mci::IT_SWORD:
	case mci::IT_AXE:
	case mci::IT_PICKAXE:
	case mci::IT_SHOVEL:
	case mci::IT_MACE:
	case mci::IT_BOW:
	case mci::IT_CROSSBOW:
		return true;
	default:
		return false;
	}
}

// A sword, an axe or the mace in hand: the player moves 5% faster and is not slowed by being hit
bool HoldsMcMelee(CBasePlayer* pl)
{
	const mci::Stack& s = HeldStack(pl);
	if (s.Empty() || mci::IsCsToken(s.id))
		return false;
	int t = mci::Item(s.id).type;
	return t == mci::IT_SWORD || t == mci::IT_AXE || t == mci::IT_MACE;
}

// Does this player pay for items? (not in creative mode, not with the economy switched off)
bool EconomyOn(CBasePlayer* pl) { return cv_economy.value != 0.0f && !P(pl).creative; }

// Buy up to `count` of an item: as many as the money and the inventory allow. Outside the economy the
// items are simply given. Returns how many the player got.
int BuyItem(CBasePlayer* pl, int itemId, int count, bool announce)
{
	if (!pl || !pl->IsAlive() || !mci::ValidItem(itemId) || count <= 0)
		return 0;
	const mci::ItemDef& d = mci::Item(itemId);
	if (!EconomyOn(pl))
		return GiveItem(pl, itemId, count, announce) ? count : 0;
	int price = mci::Price(itemId);
	if (price <= 0)
	{
		Toast(pl, 1, "%s is not for sale", d.display);
		return 0;
	}
	if (cv_buyAnywhere.value == 0.0f && !pl->CanPlayerBuy(true))
		return 0; // Counter-Strike has said why (not in a buy zone, buy time over)
	int afford = pl->m_iAccount / price;
	if (afford <= 0)
	{
		Toast(pl, 1, "%s costs $%d (you have $%d)", d.display, price, pl->m_iAccount);
		return 0;
	}
	int room = (d.type == mci::IT_ARMOR || d.type == mci::IT_ELYTRA) ? 1 : RoomFor(pl, itemId, 0);
	if (d.type == mci::IT_BLOCK && d.blockName && !strcmp(d.blockName, "obsidian") && cv_obsidianMax.value > 0.0f)
	{
		// the one block neither a bullet nor TNT moves: a couple for a spot that must hold, never a whole wall
		int have = 0;
		McPlayer& mp = P(pl);
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
			if (!mp.hotbar[i].Empty() && mp.hotbar[i].id == itemId)
				have += mp.hotbar[i].count;
		for (const auto& s : mp.inv)
			if (!s.Empty() && s.id == itemId)
				have += s.count;
		int left = (int)cv_obsidianMax.value - have;
		if (left <= 0)
		{
			Toast(pl, 1, "You can carry %d Obsidian at most", (int)cv_obsidianMax.value);
			return 0;
		}
		room = min(room, left);
	}
	int n = min(count, min(afford, room));
	if (n <= 0)
	{
		Toast(pl, 1, "No room for %s", d.display);
		return 0;
	}
	if (!GiveItem(pl, itemId, n, false))
		return 0;
	pl->AddAccount(-n * price, RT_PLAYER_BOUGHT_SOMETHING);
	if (n > 1)
		Toast(pl, 1, "Bought %d %s for $%d", n, d.display, n * price);
	else
		Toast(pl, 1, "Bought %s for $%d", d.display, price);
	McLog("buy: %s bought %d %s for $%d (now $%d)", STRING(pl->pev->netname), n, d.name, n * price, pl->m_iAccount);
	return n;
}

// A kit is its items at their prices, all or nothing
bool BuyKit(CBasePlayer* pl, const char* const* items, const int* counts, int num, const char* name, bool free)
{
	if (!pl || !pl->IsAlive())
		return false;
	bool pay = !free && EconomyOn(pl);
	int total = 0;
	for (int i = 0; i < num; i++)
		total += mci::Price(mci::FindItem(items[i])) * counts[i];
	if (pay)
	{
		if (cv_buyAnywhere.value == 0.0f && !pl->CanPlayerBuy(true))
			return false;
		if (pl->m_iAccount < total)
		{
			Toast(pl, 1, "The %s kit costs $%d (you have $%d)", name, total, pl->m_iAccount);
			return false;
		}
	}
	for (int i = 0; i < num; i++)
		GiveItem(pl, mci::FindItem(items[i]), counts[i], false);
	if (pay)
	{
		pl->AddAccount(-total, RT_PLAYER_BOUGHT_SOMETHING);
		Toast(pl, 0, "Bought the \\aq%s\\w kit for $%d", name, total);
		McLog("buy: %s bought the %s kit for $%d (now $%d)", STRING(pl->pev->netname), name, total, pl->m_iAccount);
	}
	else
		Toast(pl, 0, "Equipped the \\aq%s\\w kit", name);
	return true;
}
} // namespace mc
