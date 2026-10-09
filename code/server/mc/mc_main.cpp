// Server-side Minecraft layer: state, messages, inventory, XP, effects, commands, hook wiring.
#include "precompiled.h"

#include <map>
#include <string>

#include "mc_server.h"
#include "mc_blocks.h"
#include "mc_move.h"

#include <stdarg.h>
#include <stdio.h>

namespace mc
{
static McPlayer g_players[MAX_CLIENTS + 1];
static cvar_t cv_buyInv = {"mc_buy_opens_inventory", "1", FCVAR_SERVER, 1.0f, nullptr};
static cvar_t cv_xpPerKill = {"mc_xp_per_kill", "12", FCVAR_SERVER, 12.0f, nullptr};
static cvar_t cv_autokit = {"mc_autokit", "0", FCVAR_SERVER, 0.0f, nullptr};
static cvar_t cv_autojoin = {"mc_autojoin", "0", FCVAR_SERVER, 0.0f, nullptr};
static cvar_t cv_worldReset = {"mc_world_reset", "1", FCVAR_SERVER, 1.0f, nullptr};
static cvar_t cv_botArmor = {"mc_bot_armor", "1", FCVAR_SERVER, 1.0f, nullptr};
static int msgInv, msgStat, msgFx, msgVox, msgBreak, msgHello, msgToast, msgInvMain;
static FILE* g_log = nullptr;

McPlayer& P(int index) { return g_players[(index >= 1 && index <= MAX_CLIENTS) ? index : 0]; }
McPlayer& P(CBasePlayer* pl) { return P(pl ? pl->entindex() : 0); }

void McLog(const char* fmt, ...)
{
	if (!g_log)
	{
		char path[512];
		char gd[256];
		GET_GAME_DIR(gd);
		Q_snprintf(path, sizeof(path), "%s/logs", gd);
		CreateDirectoryA(path, nullptr);
		Q_strlcat(path, "/mc_server.log");
		g_log = fopen(path, "w");
		if (!g_log)
			return;
	}
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	Q_vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	fprintf(g_log, "[%8.2f] %s\n", gpGlobals ? gpGlobals->time : 0.0f, line);
	fflush(g_log);
	if (!strncmp(line, "SHOT ", 5))
	{
		extern void TestShotLogged(const char* name); // mc_test.cpp: the game takes the picture itself
		TestShotLogged(line + 5);
	}
}

// ---------------------------------------------------------------------------------------------
// Messages

void RegisterMessages()
{
	if (msgInv)
		return;
	msgInv = REG_USER_MSG(MCMSG_INV, -1);
	msgStat = REG_USER_MSG(MCMSG_STAT, -1);
	msgFx = REG_USER_MSG(MCMSG_FX, -1);
	msgVox = REG_USER_MSG(MCMSG_VOX, -1);
	msgBreak = REG_USER_MSG(MCMSG_BREAK, -1);
	msgHello = REG_USER_MSG(MCMSG_HELLO, -1);
	msgToast = REG_USER_MSG(MCMSG_TOAST, -1);
	msgInvMain = REG_USER_MSG(MCMSG_INVMAIN, -1);
	extern void CharactersInit();
	CharactersInit();
	EnchantInit();
}

int MsgVox() { return msgVox; }
int MsgBreak() { return msgBreak; }
int MsgHello() { return msgHello; }

static void WriteFloatCoord(float f) { WRITE_COORD(f); }

void FxSound(int sound, const float* origin, float volume, float pitch, int entindex, edict_t* only)
{
	if (!msgFx || sound < 0 || sound >= mcs::MCS_COUNT)
		return;
	if (only)
		MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, msgFx, nullptr, only);
	else
		MESSAGE_BEGIN(MSG_BROADCAST, msgFx);
	WRITE_BYTE(mcp::FX_SOUND);
	WRITE_SHORT(sound);
	WriteFloatCoord(origin[0]);
	WriteFloatCoord(origin[1]);
	WriteFloatCoord(origin[2]);
	WRITE_BYTE((int)clamp(volume * 200.0f, 0.0f, 255.0f));
	WRITE_BYTE((int)clamp(pitch * 100.0f, 1.0f, 255.0f));
	WRITE_SHORT(entindex);
	MESSAGE_END();
}

void FxParticles(int kind, const float* origin, int count, int data)
{
	if (!msgFx)
		return;
	MESSAGE_BEGIN(MSG_BROADCAST, msgFx);
	WRITE_BYTE(mcp::FX_PARTICLES);
	WRITE_BYTE(kind);
	WriteFloatCoord(origin[0]);
	WriteFloatCoord(origin[1]);
	WriteFloatCoord(origin[2]);
	WRITE_BYTE(clamp(count, 0, 255));
	WRITE_SHORT(data);
	MESSAGE_END();
}

void FxHurt(int entindex)
{
	if (!msgFx)
		return;
	MESSAGE_BEGIN(MSG_BROADCAST, msgFx);
	WRITE_BYTE(mcp::FX_HURT);
	WRITE_SHORT(entindex);
	MESSAGE_END();
}

void FxDeath(int entindex, const float* origin)
{
	if (!msgFx)
		return;
	MESSAGE_BEGIN(MSG_BROADCAST, msgFx);
	WRITE_BYTE(mcp::FX_DEATH);
	WRITE_SHORT(entindex);
	WriteFloatCoord(origin[0]);
	WriteFloatCoord(origin[1]);
	WriteFloatCoord(origin[2]);
	MESSAGE_END();
}

void FxExplosion(const float* origin, float power)
{
	if (!msgFx)
		return;
	MESSAGE_BEGIN(MSG_BROADCAST, msgFx);
	WRITE_BYTE(mcp::FX_EXPLOSION);
	WriteFloatCoord(origin[0]);
	WriteFloatCoord(origin[1]);
	WriteFloatCoord(origin[2]);
	WRITE_BYTE((int)clamp(power * 10.0f, 0.0f, 255.0f));
	MESSAGE_END();
}

void FxFirework(const float* origin, int shape, int color)
{
	if (!msgFx)
		return;
	MESSAGE_BEGIN(MSG_BROADCAST, msgFx);
	WRITE_BYTE(mcp::FX_FIREWORK);
	WriteFloatCoord(origin[0]);
	WriteFloatCoord(origin[1]);
	WriteFloatCoord(origin[2]);
	WRITE_BYTE(shape);
	WRITE_LONG(color);
	MESSAGE_END();
}

void FxSwing(int entindex)
{
	if (!msgFx)
		return;
	MESSAGE_BEGIN(MSG_BROADCAST, msgFx);
	WRITE_BYTE(mcp::FX_SWING);
	WRITE_SHORT(entindex);
	MESSAGE_END();
}

static void ToastV(edict_t* to, int kind, const char* fmt, va_list ap)
{
	if (!msgToast)
		return;
	char buf[180];
	Q_vsnprintf(buf, sizeof(buf), fmt, ap);
	if (to)
		MESSAGE_BEGIN(MSG_ONE, msgToast, nullptr, to);
	else
		MESSAGE_BEGIN(MSG_ALL, msgToast);
	WRITE_BYTE(kind);
	WRITE_STRING(buf);
	MESSAGE_END();
}

void Toast(CBasePlayer* pl, int kind, const char* fmt, ...)
{
	if (!pl || pl->IsBot())
		return;
	va_list ap;
	va_start(ap, fmt);
	ToastV(pl->edict(), kind, fmt, ap);
	va_end(ap);
}

void SendAdvancementToast(CBasePlayer* pl, int adv)
{
	// McToast kind 2: "<frame>|<icon item>|<title>" (client draws Minecraft's advancement toast)
	Toast(pl, 2, "%d|%s|%s", AdvancementFrame(adv), AdvancementIcon(adv), AdvancementTitle(adv));
}

void ToastAll(int kind, const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	ToastV(nullptr, kind, fmt, ap);
	va_end(ap);
}

// ---------------------------------------------------------------------------------------------
// Inventory

static const int CS_SLOT_FOR_HOTBAR[5] = {PRIMARY_WEAPON_SLOT, PISTOL_SLOT, KNIFE_SLOT, GRENADE_SLOT, C4_SLOT};

static int CsWeaponIdInSlot(CBasePlayer* pl, int hotbar)
{
	if (hotbar < 0 || hotbar >= 5)
		return 0;
	CBasePlayerItem* item = pl->m_rgpPlayerItems[CS_SLOT_FOR_HOTBAR[hotbar]];
	while (item)
	{
		if (item->m_iId != WEAPON_GLOCK) // weapon_mcitem lives in the knife list; skip it
			return item->m_iId;
		item = item->m_pNext;
	}
	return 0;
}

int CsWeaponInSlot(CBasePlayer* pl, int csSlot) { return CsWeaponIdInSlot(pl, csSlot); }

// Keep one token per owned CS weapon slot somewhere in the hotbar/storage: a new weapon's token goes
// back where that slot's token last was (or the first free hotbar slot), a lost weapon's token goes away.
void SyncCsTokens(CBasePlayer* pl)
{
	if (!pl || pl->IsBot())
		return;
	McPlayer& mp = P(pl);
	auto at = [&](int p) -> mci::Stack& { return p < mcp::HOTBAR_SIZE ? mp.hotbar[p] : mp.inv[p - mcp::HOTBAR_SIZE]; };
	const int N = mcp::HOTBAR_SIZE + 27;
	bool changed = false;
	for (int s = 0; s < 5; s++)
	{
		int tok = mci::CS_TOKEN_BASE + s;
		bool has = pl->IsAlive() && CsWeaponIdInSlot(pl, s) != 0;
		int where = -1;
		for (int p = 0; p < N && where < 0; p++)
			if (!at(p).Empty() && at(p).id == tok)
				where = p;
		bool onCursor = !mp.cursor.Empty() && mp.cursor.id == tok;
		if (has && where < 0 && !onCursor)
		{
			int p = mp.csPref[s];
			if (p < 0 || p >= N || !at(p).Empty())
			{
				p = -1;
				for (int q = 0; q < N && p < 0; q++)
					if (at(q).Empty())
						p = q;
			}
			if (p >= 0)
			{
				mci::Stack& t = at(p);
				t.id = (uint16_t)tok;
				t.count = 1;
				t.damage = 0;
				changed = true;
			}
		}
		else if (!has)
		{
			if (where >= 0)
			{
				mp.csPref[s] = where;
				at(where) = mci::Stack();
				changed = true;
			}
			if (onCursor)
			{
				mp.cursor = mci::Stack();
				changed = true;
			}
		}
		else if (where >= 0)
			mp.csPref[s] = where;
	}
	if (changed)
		mp.invDirty = true;
}

// Throwing a gun out of the inventory drops it like CS's drop (the knife stays).
bool DropCsWeapon(CBasePlayer* pl, int csSlot)
{
	int id = CsWeaponIdInSlot(pl, csSlot);
	if (!id || id == WEAPON_KNIFE)
		return false;
	WeaponInfoStruct* info = GetWeaponInfo(id);
	if (!info || !info->entityName)
		return false;
	pl->DropPlayerItem(info->entityName);
	return true;
}

void SendInventory(CBasePlayer* pl)
{
	if (!pl || pl->IsBot() || !msgInv)
		return;
	McPlayer& mp = P(pl);
	MESSAGE_BEGIN(MSG_ONE, msgInv, nullptr, pl->edict());
	WRITE_BYTE(mp.selected);
	WRITE_BYTE((mp.mcItemActive ? 1 : 0) | (mp.creative ? 2 : 0));
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
	{
		const mci::Stack& s = mp.hotbar[i];
		if (!s.Empty() && mci::IsCsToken(s.id))
		{
			WRITE_SHORT(-CsWeaponIdInSlot(pl, s.id - mci::CS_TOKEN_BASE)); // negative = CS weapon id
			WRITE_BYTE(1);
			WRITE_SHORT(0);
		}
		else
		{
			WRITE_SHORT(s.Empty() ? 0 : s.id);
			WRITE_BYTE(s.count);
			WRITE_SHORT(s.damage);
		}
	}
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
	{
		WRITE_SHORT(mp.armor[i].Empty() ? 0 : mp.armor[i].id);
		WRITE_SHORT(mp.armor[i].damage);
	}
	MESSAGE_END();
	SendEnchants(pl);
	if (msgInvMain)
	{
		// storage, crafting grid, craft result and cursor
		extern int CraftResult(const mci::Stack craft[4], int* count);
		int rc = 0;
		int rid = CraftResult(mp.craft, &rc);
		MESSAGE_BEGIN(MSG_ONE, msgInvMain, nullptr, pl->edict());
		auto put = [&](const mci::Stack& s) {
			if (!s.Empty() && mci::IsCsToken(s.id))
			{
				WRITE_SHORT(-CsWeaponIdInSlot(pl, s.id - mci::CS_TOKEN_BASE));
				WRITE_BYTE(1);
				WRITE_SHORT(0);
				return;
			}
			WRITE_SHORT(s.Empty() ? 0 : s.id);
			WRITE_BYTE(s.Empty() ? 0 : s.count);
			WRITE_SHORT(s.Empty() ? 0 : s.damage);
		};
		for (const auto& s : mp.inv)
			put(s);
		for (const auto& s : mp.craft)
			put(s);
		WRITE_SHORT(rid > 0 ? rid : 0);
		WRITE_BYTE(rid > 0 ? rc : 0);
		put(mp.cursor);
		MESSAGE_END();
	}
	mp.invDirty = false;
	{
		CBasePlayerItem* it2 = pl->m_rgpPlayerItems[2];
		CBasePlayerItem* it3 = pl->m_rgpPlayerItems[3];
		McLog("inv slots: pistol %s knife %s active %s", it2 ? STRING(it2->pev->classname) : "-", it3 ? STRING(it3->pev->classname) : "-",
			pl->m_pActiveItem ? STRING(pl->m_pActiveItem->pev->classname) : "-");
	}
	McLog("inv -> %d: sel %d mc %d hotbar %d %d %d %d %d | %d %d %d %d", pl->entindex(), mp.selected, mp.mcItemActive,
		-CsWeaponIdInSlot(pl, 0), -CsWeaponIdInSlot(pl, 1), -CsWeaponIdInSlot(pl, 2), -CsWeaponIdInSlot(pl, 3), -CsWeaponIdInSlot(pl, 4),
		mp.hotbar[5].id, mp.hotbar[6].id, mp.hotbar[7].id, mp.hotbar[8].id);
}

void SendStats(CBasePlayer* pl)
{
	if (!pl || pl->IsBot() || !msgStat)
		return;
	McPlayer& mp = P(pl);
	int base = mci::XpTotalAtLevel(mp.xpLevel);
	int need = mci::XpToNext(mp.xpLevel);
	float tough = 0.0f;
	float armor = ArmorPoints(pl, &tough);
	MESSAGE_BEGIN(MSG_ONE, msgStat, nullptr, pl->edict());
	WRITE_SHORT(mp.xpLevel);
	WRITE_SHORT(mp.xpTotal - base);
	WRITE_SHORT(need);
	WRITE_BYTE((int)armor);
	WRITE_BYTE(mp.creative ? 1 : 0);
	WRITE_BYTE(mp.food);
	WRITE_BYTE((int)(mp.saturation + 0.5f));
	WRITE_BYTE((int)(mp.absorption / mci::HP_PER_MC + 0.5f)); // absorption in Minecraft health points
	WRITE_LONG(pl->m_iAccount);                               // money, for the shop screen's price tags
	MESSAGE_END();
	mp.statDirty = false;
}

const mci::Stack& HeldStack(CBasePlayer* pl)
{
	static mci::Stack empty;
	McPlayer& mp = P(pl);
	if (!mp.mcItemActive || mp.selected < 0 || mp.selected >= mcp::HOTBAR_SIZE || mci::IsCsToken(mp.hotbar[mp.selected].id))
		return empty;
	return mp.hotbar[mp.selected];
}

void ConsumeHeld(CBasePlayer* pl, int n)
{
	McPlayer& mp = P(pl);
	if (mp.creative || mp.selected < 0 || mp.selected >= mcp::HOTBAR_SIZE)
		return;
	mci::Stack& s = mp.hotbar[mp.selected];
	if (s.Empty() || mci::IsCsToken(s.id))
		return;
	if (s.count <= n)
		s = mci::Stack();
	else
		s.count -= n;
	mp.invDirty = true;
}

void DamageHeld(CBasePlayer* pl, int amount)
{
	McPlayer& mp = P(pl);
	if (mp.creative || mp.selected < 0 || mp.selected >= mcp::HOTBAR_SIZE)
		return;
	mci::Stack& s = mp.hotbar[mp.selected];
	const mci::ItemDef& def = mci::Item(s.id);
	if (s.Empty() || mci::IsCsToken(s.id) || def.durability <= 0 || !EnchantWears(s, false))
		return;
	s.damage += amount;
	if (s.damage >= def.durability)
	{
		FxSound(mcs::MCS_ITEM_BREAK, pl->pev->origin, 0.8f, 0.8f + RANDOM_FLOAT(0.0f, 0.4f));
		Toast(pl, 1, "Your %s broke!", def.display);
		s = mci::Stack();
	}
	mp.invDirty = true;
}

float ArmorPoints(CBasePlayer* pl, float* toughness)
{
	McPlayer& mp = P(pl);
	float pts = 0.0f, tough = 0.0f;
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
	{
		if (mp.armor[i].Empty())
			continue;
		const mci::ItemDef& d = mci::Item(mp.armor[i].id);
		pts += d.armorPoints;
		tough += d.toughness;
	}
	if (toughness)
		*toughness = tough;
	return pts;
}

static void UpdatePlayerFlags(CBasePlayer* pl);
void UpdatePlayerFlagsPublic(CBasePlayer* pl) { UpdatePlayerFlags(pl); }

static void UpdatePlayerFlags(CBasePlayer* pl)
{
	McPlayer& mp = P(pl);
	int f = pl->pev->iuser4 & (mcp::MCPF_TRAIN | mcp::MCPF_GLIDING | mcp::MCPF_FLYING | mcp::MCPF_SWELL_MASK | mcp::MCPF_MOB_SWING);
	if (!mp.armor[mci::SLOT_CHEST].Empty() && mci::Item(mp.armor[mci::SLOT_CHEST].id).type == mci::IT_ELYTRA)
		f |= mcp::MCPF_ELYTRA;
	else
		f &= ~mcp::MCPF_GLIDING;
	if (mp.mcItemActive)
		f |= mcp::MCPF_MCITEM;
	if (mp.creative)
		f |= mcp::MCPF_CREATIVE;
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
		if (!mp.armor[i].Empty() && mci::Item(mp.armor[i].id).type == mci::IT_ARMOR)
			f |= mcp::MCPF_ARMOR;
	int held = HeldStack(pl).Empty() ? 0 : HeldStack(pl).id;
	f |= (held & 0xFFFF) << 16;
	extern bool CrossbowLoaded(CBasePlayer * pl);
	extern bool UsingItem(CBasePlayer * pl);
	if (CrossbowLoaded(pl))
		f |= mcp::MCPF_XBOW_LOADED;
	if (UsingItem(pl))
		f |= mcp::MCPF_USING;
	pl->pev->iuser4 = f;
	if (!pl->IsBot())
		CheckArmorAdvancements(pl);

	// armor visuals for the client's Minecraft player renderer: 3 bits per slot (head, chest, legs, feet)
	static const char* mats[] = {"", "leather", "chainmail", "iron", "golden", "diamond", "netherite", "turtle"};
	int code = 0;
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
	{
		const mci::Stack& st = mp.armor[i];
		if (st.Empty() || mci::Item(st.id).type != mci::IT_ARMOR)
			continue;
		const char* n = mci::Item(st.id).name;
		for (int m = 1; m < 8; m++)
			if (!strncmp(n, mats[m], strlen(mats[m])))
			{
				code |= m << (i * 3);
				break;
			}
	}
	if (!code && pl->m_iKevlar != ARMOR_NONE && pl->pev->armorvalue > 0)
	{
		code |= 3 << 3; // kevlar shows as an iron chestplate
		if (pl->m_iKevlar == ARMOR_VESTHELM)
			code |= 3;  // + iron helmet
	}
	pl->pev->playerclass = code | ((pl->m_iTeam & 3) << 12); // + team for the outline colour (1 T, 2 CT)
}

// Bots stop at iron: diamond and netherite (90% / 99% damage cut) would make them unkillable.
static bool BotMayWear(CBasePlayer* pl, const mci::ItemDef& d)
{
	return !pl->IsBot() || d.type != mci::IT_ARMOR || (strncmp(d.name, "diamond_", 8) && strncmp(d.name, "netherite_", 10));
}

void EquipArmor(CBasePlayer* pl, int itemId)
{
	const mci::ItemDef& d = mci::Item(itemId);
	if (d.type != mci::IT_ARMOR && d.type != mci::IT_ELYTRA)
		return;
	if (!BotMayWear(pl, d))
		return;
	McPlayer& mp = P(pl);
	mci::Stack old = mp.armor[d.armorSlot];
	mp.armor[d.armorSlot].id = (uint16_t)itemId;
	mp.armor[d.armorSlot].count = 1;
	mp.armor[d.armorSlot].damage = 0;
	if (!old.Empty() && old.id != itemId)
	{
		// Minecraft swaps: the replaced piece goes back to the hotbar if there's room.
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
			if (mp.hotbar[i].Empty())
			{
				mp.hotbar[i] = old;
				break;
			}
	}
	int snd = d.type == mci::IT_ELYTRA ? mcs::MCS_EQUIP_ELYTRA
		: (!strncmp(d.name, "diamond", 7) || !strncmp(d.name, "netherite", 9)) ? mcs::MCS_EQUIP_DIAMOND
		: mcs::MCS_EQUIP_GENERIC;
	FxSound(snd, pl->pev->origin, 1.0f, 1.0f, pl->entindex());
	// Counter-Strike kevlar would double-dip with Minecraft armor: Minecraft armor replaces it.
	if (d.type == mci::IT_ARMOR)
	{
		pl->pev->armorvalue = 0;
		pl->m_iKevlar = ARMOR_NONE;
	}
	mp.invDirty = mp.statDirty = true;
	UpdatePlayerFlags(pl);
}

bool GiveItem(CBasePlayer* pl, int itemId, int count, bool announce, bool pickup, int damage, int ench)
{
	if (!pl || !mci::ValidItem(itemId) || count <= 0)
		return false;
	const mci::ItemDef& d = mci::Item(itemId);
	McPlayer& mp = P(pl);

	if (!BotMayWear(pl, d))
		return false; // a bot leaves diamond/netherite armor on the ground
	if (d.type == mci::IT_ARMOR || d.type == mci::IT_ELYTRA)
	{
		if (mp.armor[d.armorSlot].Empty() || mp.armor[d.armorSlot].id != itemId)
		{
			EquipArmor(pl, itemId);
			mp.armor[d.armorSlot].damage = (uint16_t)damage; // a piece picked up keeps its wear and enchantments
			mp.armor[d.armorSlot].ench = (uint16_t)ench;
			if (announce)
				Toast(pl, 0, "Gave 1 [%s] to %s", d.display, STRING(pl->pev->netname));
			return true;
		}
	}

	int left = count;
	// Inventory.add: merge into matching stacks (hotbar first, then storage), then empty slots
	mci::Stack* slots[mcp::HOTBAR_SIZE + 27];
	int nslots = 0, hotbarEnd;
	// new items fill hotbar 6-9 first, then storage, then hotbar 1-5 (where guns sit by default), so a
	// bought gun finds its usual slot; the player can still move anything anywhere
	int slotIndex[mcp::HOTBAR_SIZE + 27];
	for (int i = mcp::FIRST_MC_SLOT; i < mcp::HOTBAR_SIZE; i++)
	{
		slotIndex[nslots] = i;
		slots[nslots++] = &mp.hotbar[i];
	}
	hotbarEnd = nslots;
	for (auto& s : mp.inv)
	{
		slotIndex[nslots] = -1;
		slots[nslots++] = &s;
	}
	for (int i = 0; i < mcp::FIRST_MC_SLOT; i++)
	{
		slotIndex[nslots] = i;
		slots[nslots++] = &mp.hotbar[i];
	}
	for (int i = 0; i < nslots && left > 0; i++)
	{
		mci::Stack& s = *slots[i];
		if (!s.Empty() && s.id == itemId && s.damage == damage && s.count < d.maxStack)
		{
			int n = min(left, (int)d.maxStack - (int)s.count);
			s.count += n;
			left -= n;
		}
	}
	int firstPlaced = -1;
	for (int i = 0; i < nslots && left > 0; i++)
	{
		mci::Stack& s = *slots[i];
		if (s.Empty())
		{
			int n = min(left, (int)d.maxStack);
			s.id = (uint16_t)itemId;
			s.count = (uint8_t)n;
			s.damage = (uint16_t)damage;
			s.ench = (uint16_t)ench;
			left -= n;
			if (firstPlaced < 0 && slotIndex[i] >= 0)
				firstPlaced = slotIndex[i];
			(void)hotbarEnd;
		}
	}

	if (left == count && pickup)
		return false; // picking up with a full hotbar: the item stays on the ground (Minecraft)
	if (left == count)
	{
		// hotbar full: replace the selected Minecraft slot (creative-style)
		int slot = (mp.selected >= 0 && mp.selected < mcp::HOTBAR_SIZE && !mci::IsCsToken(mp.hotbar[mp.selected].id)) ? mp.selected : -1;
		for (int i = mcp::FIRST_MC_SLOT; slot < 0 && i < mcp::HOTBAR_SIZE; i++)
			if (!mci::IsCsToken(mp.hotbar[i].id))
				slot = i;
		for (int i = 0; slot < 0 && i < mcp::HOTBAR_SIZE; i++)
			if (!mci::IsCsToken(mp.hotbar[i].id))
				slot = i;
		if (slot < 0)
			return false;
		mp.hotbar[slot].id = (uint16_t)itemId;
		mp.hotbar[slot].count = (uint8_t)min(count, (int)d.maxStack);
		mp.hotbar[slot].damage = 0;
		firstPlaced = slot;
		left = 0;
	}
	mp.invDirty = true;
	if (announce)
		Toast(pl, 0, "Gave %d [%s] to %s", count - left, d.display, STRING(pl->pev->netname));
	// Like Minecraft creative: auto-select a freshly given item if we're holding nothing.
	if (firstPlaced >= 0 && (!mp.mcItemActive || HeldStack(pl).Empty()))
		SelectSlot(pl, firstPlaced);
	UpdatePlayerFlags(pl);
	return true;
}

void SelectSlot(CBasePlayer* pl, int slot)
{
	if (!pl || slot < 0 || slot >= mcp::HOTBAR_SIZE || !pl->IsAlive())
		return;
	McPlayer& mp = P(pl);
	SyncCsTokens(pl);
	const mci::Stack& here = mp.hotbar[slot];
	if (!here.Empty() && mci::IsCsToken(here.id))
	{
		int csSlot = here.id - mci::CS_TOKEN_BASE;
		int id = CsWeaponIdInSlot(pl, csSlot);
		if (!id)
			return;
		// pressing the grenade slot again cycles through the grenades, like CS
		if (pl->m_pActiveItem && pl->m_pActiveItem->iItemSlot() == CS_SLOT_FOR_HOTBAR[csSlot] && mp.selected == slot)
		{
			CBasePlayerItem* next = pl->m_pActiveItem->m_pNext;
			while (next && next->m_iId == WEAPON_GLOCK)
				next = next->m_pNext;
			if (!next)
				next = pl->m_rgpPlayerItems[CS_SLOT_FOR_HOTBAR[csSlot]];
			while (next && next->m_iId == WEAPON_GLOCK)
				next = next->m_pNext;
			if (next && next != pl->m_pActiveItem)
				id = next->m_iId;
		}
		WeaponInfoStruct* info = GetWeaponInfo(id);
		if (info && info->entityName)
			pl->SelectItem(info->entityName);
		mp.selected = slot;
		mp.invDirty = true;
		return;
	}
	mp.selected = slot;
	if (!pl->HasNamedPlayerItem("weapon_mcitem"))
		pl->GiveNamedItem("weapon_mcitem");
	CBasePlayerItem* active = pl->m_pActiveItem;
	if (!active || active->m_iId != WEAPON_GLOCK)
		pl->SelectItem("weapon_mcitem");
	mp.invDirty = true;
	UpdatePlayerFlags(pl);
}

// ---------------------------------------------------------------------------------------------
// XP

void GiveXp(CBasePlayer* pl, int amount)
{
	if (!pl || amount <= 0)
		return;
	McPlayer& mp = P(pl);
	int oldLevel = mp.xpLevel;
	mp.xpTotal += amount;
	while (mp.xpTotal >= mci::XpTotalAtLevel(mp.xpLevel + 1))
		mp.xpLevel++;
	if (mp.xpLevel > oldLevel && mp.xpLevel % 5 == 0 && gpGlobals->time - mp.lastLevelSound > 5.0f)
	{
		// Player.giveExperienceLevels: the level-up chime every 5 levels
		float vol = mp.xpLevel > 30 ? 1.0f : mp.xpLevel / 30.0f;
		FxSound(mcs::MCS_LEVELUP, pl->pev->origin, max(0.4f, vol) * 0.75f, 1.0f, pl->entindex(), pl->edict());
		mp.lastLevelSound = gpGlobals->time;
	}
	mp.statDirty = true;
}

// ---------------------------------------------------------------------------------------------
// Commands

static const char* kMenuCategories[] = {"Combat", "Armor", "Tools", "Blocks", "Utility", "Kits"};
static const int kNumCategories = ARRAYSIZE(kMenuCategories);

static bool ItemInCategory(const mci::ItemDef& d, int cat)
{
	switch (cat)
	{
	case 0: return d.type == mci::IT_SWORD || d.type == mci::IT_AXE || d.type == mci::IT_MACE || d.type == mci::IT_BOW || d.type == mci::IT_ARROW;
	case 1: return d.type == mci::IT_ARMOR || d.type == mci::IT_ELYTRA;
	case 2: return d.type == mci::IT_PICKAXE || d.type == mci::IT_SHOVEL || d.type == mci::IT_FLINT_STEEL;
	case 3: return d.type == mci::IT_BLOCK;
	case 4: return d.type == mci::IT_FIREWORK || d.type == mci::IT_FOOD || d.type == mci::IT_TOTEM || d.type == mci::IT_PEARL || d.type == mci::IT_XP_BOTTLE || d.type == mci::IT_MATERIAL;
	}
	return false;
}

void ShowMenuText(CBasePlayer* pl, int keys, const char* text)
{
	// A user message holds 192 bytes at most: long menus go out in pieces, each but the last flagged
	// "more to come" (the client joins them).
	const int kChunk = 170;
	int len = (int)strlen(text);
	for (int at = 0; at == 0 || at < len; at += kChunk)
	{
		char piece[kChunk + 1];
		int n = min(kChunk, len - at);
		memcpy(piece, text + at, n);
		piece[n] = 0;
		MESSAGE_BEGIN(MSG_ONE, gmsgShowMenu, nullptr, pl->edict());
		WRITE_SHORT(keys);
		WRITE_CHAR(-1);
		WRITE_BYTE(at + kChunk < len ? 1 : 0);
		WRITE_STRING(piece);
		MESSAGE_END();
	}
}

void OpenMenu(CBasePlayer* pl, int menu, int page)
{
	McPlayer& mp = P(pl);
	mp.menu = menu;
	mp.menuPage = page;
	char buf[512];
	int keys = (1 << 9); // 0 = exit
	if (menu == 1)
	{
		Q_strlcpy(buf, "\\yCreative Inventory\\w\n\n");
		for (int i = 0; i < kNumCategories; i++)
		{
			char line[64];
			Q_snprintf(line, sizeof(line), "%d. %s\n", i + 1, kMenuCategories[i]);
			Q_strlcat(buf, line);
			keys |= 1 << i;
		}
		Q_strlcat(buf, "\n\\r7. \\wCounter-Strike buy menu\n");
		keys |= 1 << 6;
		Q_strlcat(buf, "\n0. Close");
	}
	else if (menu >= 10 && menu < 10 + kNumCategories)
	{
		int cat = menu - 10;
		int ids[256], n = 0;
		for (int i = 1; i < mci::g_numItems && n < 256; i++)
			if (ItemInCategory(mci::g_items[i], cat))
				ids[n++] = i;
		if (cat == 5)
		{
			Q_strlcpy(buf, "\\yKits\\w\n\n1. Full Diamond (sword, axe, armor)\n2. Full Netherite + Mace\n3. Elytra + 64 Fireworks\n4. Miner (pickaxe, shovel, TNT, blocks)\n5. Totem + Golden Apples\n\n0. Back");
			keys |= 0x1F;
		}
		else
		{
			int perPage = 7;
			int pages = (n + perPage - 1) / perPage;
			if (page >= pages)
				page = 0;
			mp.menuPage = page;
			Q_snprintf(buf, sizeof(buf), "\\y%s\\w  (%d/%d)\n\n", kMenuCategories[cat], page + 1, max(1, pages));
			for (int k = 0; k < perPage && page * perPage + k < n; k++)
			{
				char line[80];
				const mci::ItemDef& d = mci::g_items[ids[page * perPage + k]];
				Q_snprintf(line, sizeof(line), "%d. %s\n", k + 1, d.display);
				Q_strlcat(buf, line);
				keys |= 1 << k;
			}
			if (pages > 1)
			{
				Q_strlcat(buf, "\n8. Next page");
				keys |= 1 << 7;
			}
			Q_strlcat(buf, "\n9. Back\n0. Close");
			keys |= 1 << 8;
		}
	}
	ShowMenuText(pl, keys, buf);
}

// A kit is bought as a whole at the sum of its items' prices (free: the test autokit, creative mode)
static void GiveKit(CBasePlayer* pl, int kit, bool free = false)
{
	struct KitDef
	{
		const char* name;
		const char* items[6];
		int counts[6];
		int num;
	};
	static const KitDef kits[5] = {
		{"Full Diamond", {"diamond_sword", "diamond_axe", "diamond_helmet", "diamond_chestplate", "diamond_leggings", "diamond_boots"}, {1, 1, 1, 1, 1, 1}, 6},
		{"Netherite", {"netherite_sword", "mace", "netherite_helmet", "netherite_chestplate", "netherite_leggings", "netherite_boots"}, {1, 1, 1, 1, 1, 1}, 6},
		{"Elytra", {"elytra", "firework_rocket"}, {1, 64}, 2},
		{"Miner", {"diamond_pickaxe", "diamond_shovel", "tnt", "cobblestone"}, {1, 1, 1, 8}, 4},
		{"Totem", {"totem_of_undying", "golden_apple"}, {1, 16}, 2},
	};
	if (kit < 0 || kit > 4)
		return;
	if (BuyKit(pl, kits[kit].items, kits[kit].counts, kits[kit].num, kits[kit].name, free) && kit == 2)
		Toast(pl, 0, "Jump while falling to glide, right-click rockets to boost!");
}

static void HandleMenuSelect(CBasePlayer* pl, int key)
{
	McPlayer& mp = P(pl);
	int menu = mp.menu;
	mp.menu = 0;
	if (key == 10 || key == 0)
		return; // closed
	extern bool CharacterMenuSelect(CBasePlayer * pl, int menu, int key);
	if (CharacterMenuSelect(pl, menu, key))
		return;
	if (EnchantMenuSelect(pl, menu, key))
		return;
	if (menu == 1)
	{
		if (key >= 1 && key <= kNumCategories)
			OpenMenu(pl, 10 + key - 1, 0);
		else if (key == 7)
		{
			mp.passBuy = true; // the next "buy" goes to Counter-Strike's own buy menu
			CLIENT_COMMAND(pl->edict(), "mc_csbuy\n");
		}
		return;
	}
	if (menu >= 10 && menu < 10 + kNumCategories)
	{
		int cat = menu - 10;
		if (cat == 5)
		{
			if (key >= 1 && key <= 5)
				GiveKit(pl, key - 1); // a kit is one-and-done: the menu closes, the number keys are yours again
			else if (key == 9)
				OpenMenu(pl, 1, 0);
			return;
		}
		if (key == 8)
		{
			OpenMenu(pl, menu, mp.menuPage + 1);
			return;
		}
		if (key == 9)
		{
			OpenMenu(pl, 1);
			return;
		}
		int ids[256], n = 0;
		for (int i = 1; i < mci::g_numItems && n < 256; i++)
			if (ItemInCategory(mci::g_items[i], cat))
				ids[n++] = i;
		int idx = mp.menuPage * 7 + (key - 1);
		if (idx >= 0 && idx < n)
		{
			const mci::ItemDef& d = mci::Item(ids[idx]);
			BuyItem(pl, ids[idx], d.maxStack > 1 ? d.maxStack : 1, true);
		}
		OpenMenu(pl, menu, mp.menuPage); // keep it open, Minecraft creative-style
	}
}

static void ChatCommand(CBasePlayer* pl, const char* text)
{
	char buf[192];
	Q_strlcpy(buf, text);
	char* argv[8];
	int argc = 0;
	for (char* t = strtok(buf, " "); t && argc < 8; t = strtok(nullptr, " "))
		argv[argc++] = t;
	if (!argc)
		return;
	McPlayer& mp = P(pl);
	const char* c = argv[0] + 1; // skip '/'
	if (!Q_stricmp(c, "give") && argc >= 2)
	{
		// /give <item> [count]  or  /give @s <item> [count]
		int a = 1;
		if (argv[a][0] == '@' && argc >= 3)
			a++;
		int id = mci::FindItem(argv[a]);
		if (id < 0)
		{
			Toast(pl, 0, "\\rUnknown item '%s'", argv[a]);
			return;
		}
		int count = argc > a + 1 ? atoi(argv[a + 1]) : 1;
		BuyItem(pl, id, clamp(count, 1, 64 * 4), true);
	}
	else if (!Q_stricmp(c, "gamemode") && argc >= 2)
	{
		mp.creative = !Q_stricmp(argv[1], "creative") || !Q_stricmp(argv[1], "1") || !Q_stricmp(argv[1], "c");
		mp.statDirty = mp.invDirty = true;
		UpdatePlayerFlags(pl);
		Toast(pl, 0, "Set own game mode to %s Mode", mp.creative ? "Creative" : "Survival");
	}
	else if (!Q_stricmp(c, "character") || !Q_stricmp(c, "skin"))
	{
		extern bool CharacterCommand(CBasePlayer * pl, const char* arg);
		CharacterCommand(pl, argc >= 2 ? CMD_ARGV(2) : "");
	}
	else if (!Q_stricmp(c, "kit"))
	{
		OpenMenu(pl, 10 + 5);
	}
	else if (!Q_stricmp(c, "xp") && argc >= 2)
	{
		int n = atoi(argv[argc >= 3 && !Q_stricmp(argv[1], "add") ? 2 : 1]);
		bool levels = argc >= 3 && strstr(argv[argc - 1], "level");
		if (levels)
			n = mci::XpTotalAtLevel(mp.xpLevel + n) - mp.xpTotal;
		GiveXp(pl, n);
		Toast(pl, 0, "Gave %d experience to %s", n, STRING(pl->pev->netname));
	}
	else if (!Q_stricmp(c, "kill"))
	{
		pl->TakeDamage(pl->pev, pl->pev, 10000.0f, DMG_GENERIC);
	}
	else if (!Q_stricmp(c, "clear"))
	{
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
			mp.hotbar[i] = mci::Stack();
		for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
			mp.armor[i] = mci::Stack();
		mp.invDirty = mp.statDirty = true;
		UpdatePlayerFlags(pl);
		Toast(pl, 0, "Removed all items from %s", STRING(pl->pev->netname));
	}
	else if (!Q_stricmp(c, "help"))
	{
		Toast(pl, 0, "/give <item> [count], /kit, /gamemode creative|survival, /xp <n> [levels], /clear, /kill. Press B for the creative inventory.");
	}
	else
	{
		Toast(pl, 0, "\\rUnknown command. Type /help");
	}
}

bool ClientCommand(CBasePlayer* pl, const char* cmd, const char* args)
{
	if (!pl)
		return false;
	McPlayer& mp = P(pl);
	if (!Q_strcmp(cmd, "menuselect") && mp.menu)
	{
		HandleMenuSelect(pl, atoi(CMD_ARGV(1)));
		return true;
	}
	if (!Q_strcmp(cmd, "mc_select"))
	{
		SelectSlot(pl, atoi(CMD_ARGV(1)) - 1);
		return true;
	}
	if (!Q_strcmp(cmd, "mc_character"))
	{
		extern bool CharacterCommand(CBasePlayer * pl, const char* arg);
		return CharacterCommand(pl, CMD_ARGC() > 1 ? CMD_ARGV(1) : "");
	}
	if (!Q_strcmp(cmd, "mc_give"))
	{
		int id = mci::FindItem(CMD_ARGV(1));
		if (id > 0)
			BuyItem(pl, id, CMD_ARGC() > 2 ? atoi(CMD_ARGV(2)) : 1, true);
		return true;
	}
	// creative inventory screen (client mc_gui.cpp)
	if (!Q_strcmp(cmd, "mc_take"))
	{
		int id = mci::FindItem(CMD_ARGV(1));
		if (id > 0 && pl->IsAlive())
			BuyItem(pl, id, clamp(CMD_ARGC() > 2 ? atoi(CMD_ARGV(2)) : 1, 1, 64), false);
		return true;
	}
	if (!Q_strcmp(cmd, "mc_setslot"))
	{
		int slot = atoi(CMD_ARGV(1)) - 1;
		if (EconomyOn(pl) && mci::FindItem(CMD_ARGV(2)) > 0)
		{
			// paying players buy into the inventory; only creative mode writes a slot directly
			int id = mci::FindItem(CMD_ARGV(2));
			BuyItem(pl, id, clamp(CMD_ARGC() > 3 ? atoi(CMD_ARGV(3)) : 1, 1, 64), false);
			return true;
		}
		if (slot >= 0 && slot < mcp::HOTBAR_SIZE && pl->IsAlive() && !mci::IsCsToken(mp.hotbar[slot].id))
		{
			int id = mci::FindItem(CMD_ARGV(2));
			mci::Stack& st = mp.hotbar[slot];
			if (id > 0)
			{
				const mci::ItemDef& d = mci::Item(id);
				st.id = (uint16_t)id;
				st.count = (uint8_t)clamp(CMD_ARGC() > 3 ? atoi(CMD_ARGV(3)) : 1, 1, max(1, (int)d.maxStack));
				st.damage = 0;
			}
			else
				st = mci::Stack();
			mp.invDirty = true;
			UpdatePlayerFlags(pl);
		}
		return true;
	}
	if (!Q_strcmp(cmd, "mc_kit"))
	{
		int k = atoi(CMD_ARGV(1));
		if (k >= 0 && k <= 4 && pl->IsAlive())
			GiveKit(pl, k);
		return true;
	}
	if (!Q_strcmp(cmd, "buy") && mp.passBuy)
	{
		mp.passBuy = false;
		return false;
	}
	extern bool ClassicMode();
	if (!Q_strcmp(cmd, "mc_menu") ||
		(!Q_strcmp(cmd, "buy") && CMD_ARGC() == 1 && !mp.menu && !ClassicMode() && CVAR_GET_FLOAT("mc_buy_opens_inventory") != 0.0f))
	{
		OpenMenu(pl, 1);
		return true;
	}
	if (!Q_strncmp(cmd, "mc_ench_", 8))
		return EnchantCommand(pl, cmd);
	if (!Q_strcmp(cmd, "mc_brain"))
		return BotTacticsCommand(pl, cmd);
	if (!Q_strcmp(cmd, "mc_click"))
	{
		InventoryClick(pl, atoi(CMD_ARGV(1)), atoi(CMD_ARGV(2)), atoi(CMD_ARGV(3)) != 0);
		return true;
	}
	if (!Q_strcmp(cmd, "mc_invdrop"))
	{
		InventoryDrop(pl, atoi(CMD_ARGV(1)), atoi(CMD_ARGV(2)) != 0);
		return true;
	}
	if (!Q_strcmp(cmd, "mc_invclose"))
	{
		InventoryClose(pl);
		return true;
	}
	if (!Q_strcmp(cmd, "mc_drop") || (!Q_strcmp(cmd, "drop") && mp.mcItemActive))
	{
		const mci::Stack& s = HeldStack(pl);
		if (!s.Empty())
		{
			UTIL_MakeVectors(pl->pev->v_angle);
			Vector org = pl->pev->origin + pl->pev->view_ofs + gpGlobals->v_forward * 16.0f;
			Vector vel = gpGlobals->v_forward * 240.0f + Vector(0, 0, 80);
			SpawnItemEntity(org, s.id, 1, vel);
			ConsumeHeld(pl, 1);
			UpdatePlayerFlags(pl);
		}
		return true;
	}
	if ((!Q_strcmp(cmd, "say") || !Q_strcmp(cmd, "say_team")))
	{
		const char* text = CMD_ARGS();
		if (text && text[0] == '"')
			text++;
		if (text && text[0] == '/')
		{
			char clean[192];
			Q_strlcpy(clean, text);
			size_t len = Q_strlen(clean);
			if (len && clean[len - 1] == '"')
				clean[len - 1] = 0;
			ChatCommand(pl, clean);
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Hookchains

static void H_InternalCommand(IReGameHook_InternalCommand* chain, edict_t* ent, const char* cmd, const char* arg1)
{
	CBasePlayer* pl = (CBasePlayer*)CBaseEntity::Instance(ent);
	if (pl && !pl->IsBot() && cmd && strcmp(cmd, "menuselect"))
		McLog("cmd from %d: '%s' '%s'", pl->entindex(), cmd, arg1 ? arg1 : "");
	if (pl && cmd && !strcmp(cmd, "VModEnable") && !P(pl).clientReady)
	{
		P(pl).clientReady = true; // the client's VGUI viewport exists now
		P(pl).readyAt = gpGlobals->time + 0.5f;
	}
	if (pl && ClientCommand(pl, cmd, arg1))
		return;
	chain->callNext(ent, cmd, arg1);
}

static void H_Spawn(IReGameHook_CBasePlayer_Spawn* chain, CBasePlayer* pl)
{
	chain->callNext(pl);
	McPlayer& mp = P(pl);
	mp.active = true;
	mp.mineProgress = 0.0f;
	mp.deathEffectsDone = false;
	if (mp.foodReset && pl->IsAlive())
	{
		ResetFood(pl); // Minecraft respawns you fed
		mp.foodReset = false;
	}
	mp.lastPosValid = false;
	// ClientPutInServer also runs Spawn on the new (still team-less) player: equipping them there makes CS
	// send InitHUD early, and ClientPutInServer then re-arms it, so the MOTD pops up again on the real spawn
	bool joined = pl->m_iTeam == TERRORIST || pl->m_iTeam == CT;
	if (!pl->IsBot() && pl->IsAlive() && joined)
	{
		if (!pl->HasNamedPlayerItem("weapon_knife"))
			pl->GiveDefaultItems();
		if (!pl->HasNamedPlayerItem("weapon_mcitem"))
			pl->GiveNamedItem("weapon_mcitem");
		mp.invDirty = mp.statDirty = true;
		static bool kitted[MAX_CLIENTS + 1];
		if (cv_autokit.value != 0.0f && !kitted[pl->entindex()])
		{
			kitted[pl->entindex()] = true;
			McLog("autokit for %s", STRING(pl->pev->netname));
			GiveKit(pl, 0, true);
			GiveItem(pl, mci::FindItem("firework_rocket"), 64, false);
			GiveItem(pl, mci::FindItem("cobblestone"), 64, false);
			GiveItem(pl, mci::FindItem("diamond_pickaxe"), 1, false);
		}
		if (ArmorPoints(pl, nullptr) > 0.0f)
		{
			pl->pev->armorvalue = 0;
			pl->m_iKevlar = ARMOR_NONE;
		}
	}
	// bots: some rounds they spend their money on Minecraft gear (mc_botgear.cpp)
	if (pl->IsBot() && pl->IsAlive() && cv_botArmor.value != 0.0f && !IsMobBot(pl))
	{
		extern void BotGearSpawn(CBasePlayer * bot);
		BotGearSpawn(pl);
	}
	if (pl->IsBot() && pl->IsAlive() && !IsMobBot(pl))
		BotTacticsSpawn(pl); // blocks, TNT and a flint and steel for some (mc_bottactics.cpp)
	if (false)
	{
		for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
			mp.armor[i] = mci::Stack();
		if (RANDOM_LONG(0, 1))
		{
			static const char* mats[] = {"leather", "chainmail", "iron", "golden", "diamond"};
			static const char* parts[] = {"helmet", "chestplate", "leggings", "boots"};
			const char* m = mats[RANDOM_LONG(0, 4)];
			for (int i = 0; i < 4; i++)
			{
				if (RANDOM_LONG(0, 3) == 0)
					continue;
				char name[64];
				Q_snprintf(name, sizeof(name), "%s_%s", m, parts[i]);
				int id = mci::FindItem(name);
				if (id > 0)
				{
					mp.armor[i].id = (uint16_t)id;
					mp.armor[i].count = 1;
				}
			}
		}
	}
	CreeperSpawn(pl);
	TeamMobSpawned(pl);
	extern void CharacterSpawn(CBasePlayer * pl);
	CharacterSpawn(pl);
	UpdatePlayerFlags(pl);
}

static bool H_HasRestrictItem(IReGameHook_CBasePlayer_HasRestrictItem* chain, CBasePlayer* pl, ItemID item, ItemRestType type)
{
	if (CreeperRestrictsItem(pl, item) || (IsMobBot(pl) && item != ITEM_KNIFE))
		return true;
	return chain->callNext(pl, item, type);
}

// MineStrike armor: a full set of a material cuts damage by a fixed share (much stronger than Minecraft's
// formula, so gear matters against guns). Each piece counts by its share of a set's armor points.
static float MaterialReduction(const char* name)
{
	if (!strncmp(name, "netherite_", 10))
		return 0.99f;
	if (!strncmp(name, "diamond_", 8))
		return 0.90f;
	if (!strncmp(name, "iron_", 5) || !strcmp(name, "turtle_helmet"))
		return 0.75f;
	if (!strncmp(name, "chainmail_", 10))
		return 0.60f;
	if (!strncmp(name, "golden_", 7))
		return 0.45f;
	if (!strncmp(name, "leather_", 8))
		return 0.30f;
	return 0.0f;
}

float ArmorReduction(CBasePlayer* pl)
{
	static const float kShare[mci::NUM_ARMOR_SLOTS] = {0.15f, 0.40f, 0.30f, 0.15f}; // head, chest, legs, feet
	McPlayer& mp = P(pl);
	float r = 0.0f;
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
		if (!mp.armor[i].Empty() && mci::Item(mp.armor[i].id).type == mci::IT_ARMOR)
			r += kShare[i] * MaterialReduction(mci::Item(mp.armor[i].id).name);
	return r;
}

static BOOL H_TakeDamage(IReGameHook_CBasePlayer_TakeDamage* chain, CBasePlayer* pl, entvars_t* inflictor,
	entvars_t* attacker, float& damage, int bits)
{
	McPlayer& mp = P(pl);
	// creepers don't bite: their knife is only there to make the bot AI close in
	CBaseEntity* att = attacker ? CBaseEntity::Instance(attacker) : nullptr;
	if (att && att->IsPlayer() && IsCreeper((CBasePlayer*)att) && !(bits & DMG_BLAST))
		return FALSE;
	CBaseEntity* infl = inflictor ? CBaseEntity::Instance(inflictor) : nullptr;
	if (TeamMobDamage(pl, infl, att, damage, bits))
		return FALSE;
	if (EndermanDodge(pl, infl, att, bits))
		return FALSE;
	mp.lastInflictor = infl ? infl->entindex() : 0;
	mp.lastBits = bits;
	if (bits & DMG_FALL)
		NoteFallDamage(pl, damage);
	if (att && att->IsPlayer() && att != pl && !((CBasePlayer*)att)->IsBot())
	{
		CBasePlayer* ap = (CBasePlayer*)att;
		if (infl && FClassnameIs(infl->pev, "mc_projectile"))
			Award(ap, ADV_TAKE_AIM);
		else if (infl == att && P(ap).mcItemActive && damage >= 18.0f * mci::HP_PER_MC)
			Award(ap, ADV_OVERKILL); // nine hearts in a single hit
	}
	// Minecraft armor stops bullets (and Counter-Strike's knife, which the game counts as one). Swords, axes,
	// arrows, explosions and fire go through it: Minecraft weapons are the answer to an armored opponent.
	bool bullet = (bits & DMG_BULLET) && !(infl && FClassnameIs(infl->pev, "mc_projectile"));
	if (pl->IsAlive() && damage > 0.0f && bullet)
	{
		float reduction = ArmorReduction(pl);
		if (reduction > 0.0f)
		{
			float mcDamage = damage / mci::HP_PER_MC;
			float before = damage;
			// every hit still costs at least 1 HP (a bullet against full netherite: 1)
			damage = fmaxf(fminf(1.0f, before), before * (1.0f - reduction));
			// armor durability: Minecraft damages each piece by max(1, damage/4), and its half-second
			// invulnerability window lets that happen twice a second at most. Guns land far more hits than
			// that, so wear keeps the same window: one charge per window, topped up by a bigger hit.
			const float kWearWindow = 0.5f;
			int wear = max(1, (int)(mcDamage / 4.0f));
			if (gpGlobals->time >= mp.armorWearUntil || gpGlobals->time < mp.armorWearUntil - kWearWindow)
			{
				mp.armorWearUntil = gpGlobals->time + kWearWindow;
				mp.armorWearDone = 0;
			}
			wear -= mp.armorWearDone;
			if (wear > 0)
			{
				mp.armorWearDone += wear;
				for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
				{
					mci::Stack& s = mp.armor[i];
					const mci::ItemDef& d = mci::Item(s.id);
					if (s.Empty() || d.type != mci::IT_ARMOR || d.durability <= 0 || !EnchantWears(s, true))
						continue;
					s.damage += wear;
					if (s.damage >= d.durability)
					{
						FxSound(mcs::MCS_ITEM_BREAK, pl->pev->origin, 0.8f, 0.8f + RANDOM_FLOAT(0.0f, 0.4f));
						s = mci::Stack();
						mp.statDirty = true;
					}
				}
				mp.invDirty = true;
			}
		}
	}

	// Protection on the armor worn takes its share off any damage: blades and fire too (4% a level)
	if (pl->IsAlive() && damage > 0.0f)
		damage *= 1.0f - EnchantProtection(pl);

	// damage exhausts (Minecraft: 0.1 per hit); absorption hearts soak damage before health
	if (pl->IsAlive() && damage > 0.0f && !pl->IsBot())
	{
		AddExhaustion(pl, 0.1f);
		if (mp.absorption > 0.0f)
		{
			float soak = fminf(damage, mp.absorption);
			mp.absorption -= soak;
			damage -= soak;
			mp.statDirty = true;
		}
	}

	// Totem of Undying: survive a lethal hit
	if (pl->IsAlive() && damage >= pl->pev->health)
	{
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
		{
			if (!mp.hotbar[i].Empty() && !mci::IsCsToken(mp.hotbar[i].id) && mci::Item(mp.hotbar[i].id).type == mci::IT_TOTEM)
			{
				mp.hotbar[i] = mci::Stack();
				mp.invDirty = true;
				pl->pev->health = 1.0f * mci::HP_PER_MC;
				damage = 0.0f;
				FxSound(mcs::MCS_TOTEM_USE, pl->pev->origin, 1.0f, 1.0f, pl->entindex());
				MESSAGE_BEGIN(MSG_BROADCAST, msgFx);
				WRITE_BYTE(mcp::FX_TOTEM);
				WRITE_SHORT(pl->entindex());
				MESSAGE_END();
				Toast(pl, 1, "Totem of Undying!");
				Award(pl, ADV_POSTMORTAL);
				return chain->callNext(pl, inflictor, attacker, damage, bits);
			}
		}
	}

	// a player fighting with a Minecraft weapon keeps a steady aim when hit
	Vector punch = pl->pev->punchangle;
	bool steady = HoldsMcWeapon(pl);
	BOOL r = chain->callNext(pl, inflictor, attacker, damage, bits);
	if (steady)
		pl->pev->punchangle = punch;
	if (HoldsMcMelee(pl))
		pl->m_flVelocityModifier = 1.0f; // and a blade in hand is not slowed by bullets
	if (r && damage > 0.0f)
	{
		FxHurt(pl->entindex());
		if (pl->IsAlive())
			FxSound((bits & DMG_BURN) && MobOf(pl) == MOB_PLAYER ? mcs::MCS_PLAYER_HURT_FIRE : MobHurtSound(pl), pl->pev->origin, 1.0f,
				0.8f + RANDOM_FLOAT(0.0f, 0.4f), pl->entindex());
	}
	return r;
}

// Counter-Strike kicks the view of a player who is hit (TraceAttack). Not while a Minecraft weapon is held.
static void H_TraceAttack(IReGameHook_CBasePlayer_TraceAttack* chain, CBasePlayer* pl, entvars_t* attacker, float damage, Vector& dir,
	TraceResult* tr, int bits)
{
	Vector punch = pl->pev->punchangle;
	bool steady = HoldsMcWeapon(pl);
	if (TeamMobBullet(pl, attacker, damage, tr, bits))
		return; // (an iron golem: the bullet does nothing to it, and comes back)
	if (tr && IsMobBot(pl))
		tr->iHitgroup = HITGROUP_CHEST; // (a mob has no soft spot: a blow at its head is a blow at its body)
	chain->callNext(pl, attacker, damage, dir, tr, bits);
	if (steady)
		pl->pev->punchangle = punch;
}

// the shop screen shows the player's money: resend the stats when it changes
static void H_AddAccount(IReGameHook_CBasePlayer_AddAccount* chain, CBasePlayer* pl, int amount, RewardType type, bool track)
{
	chain->callNext(pl, amount, type, track);
	P(pl).statDirty = true;
}

static void H_Killed(IReGameHook_CBasePlayer_Killed* chain, CBasePlayer* pl, entvars_t* attacker, int gib)
{
	Vector org = pl->pev->origin;
	// (whatever the damage came from, Counter-Strike must have a blast vector it can divide by)
	if (pl->m_vBlastVector.Length() < 1.0f)
		pl->m_vBlastVector = Vector(0, 0, 1);
	chain->callNext(pl, attacker, gib);
	if (!isfinite(pl->pev->velocity.x) || !isfinite(pl->pev->velocity.y) || !isfinite(pl->pev->velocity.z))
	{
		McLog("%s died with a velocity that is not a number; stopped", STRING(pl->pev->netname));
		pl->pev->velocity = g_vecZero;
	}
	McPlayer& mp = P(pl);
	mp.deathTime = gpGlobals->time;
	{
		CBaseEntity* k = attacker ? CBaseEntity::Instance(attacker) : nullptr;
		mp.killer = (k && k->IsPlayer() && k != pl) ? k->entindex() : 0;
	}
	mp.foodReset = true;
	mp.deathEffectsDone = false;
	mp.mineProgress = 0.0f;
	pl->pev->iuser4 &= ~mcp::MCPF_GLIDING;
	{
		edict_t* ie = mp.lastInflictor > 0 ? INDEXENT(mp.lastInflictor) : nullptr;
		entvars_t* infl = (ie && !FNullEnt(ie)) ? VARS(ie) : nullptr;
		DeathMessage(pl, attacker, infl, CreeperExploded(pl) ? DMG_BLAST : mp.lastBits);
		CBaseEntity* att = attacker ? CBaseEntity::Instance(attacker) : nullptr;
		if (att && att->IsPlayer() && att != pl)
		{
			CBasePlayer* ap = (CBasePlayer*)att;
			Award(ap, ADV_MONSTER_HUNTER);
			if (infl && FClassnameIs(infl, "mc_projectile") && (ap->pev->origin - org).Length() >= 50.0f * 40.0f)
				Award(ap, ADV_SNIPER_DUEL);
		}
	}
	if (CreeperExploded(pl))
	{
		// blew itself up: nothing left to animate, drop or collect
		mp.deathEffectsDone = true;
		pl->pev->effects |= EF_NODRAW;
		return;
	}
	FxSound(MobDeathSound(pl), org, 1.0f, 0.8f + RANDOM_FLOAT(0.0f, 0.4f), pl->entindex());
	MobDrops(pl, org);
	DropEverything(pl); // Minecraft: everything you carried lands on the ground
}

static void H_DeathSound(IReGameHook_CBasePlayer_DeathSound* chain, CBasePlayer* pl)
{
	// Minecraft's death sound replaces CS's (played in H_Killed).
}

static void H_Pain(IReGameHook_CBasePlayer_Pain* chain, CBasePlayer* pl, int hitgroup, bool hitkevlar)
{
	// Minecraft hurt sound replaces CS's pain sounds (played in H_TakeDamage).
}

static void H_ShowVGUIMenu(IReGameHook_ShowVGUIMenu* chain, CBasePlayer* pl, int menuType, int bitsSlots, char* oldMenu)
{
	// test convenience: with mc_autojoin the team/class menus never open (we pick for the player)
	if (cv_autojoin.value != 0.0f && pl && !pl->IsBot() && (menuType == VGUI_Menu_Team || menuType == VGUI_Menu_Class_CT || menuType == VGUI_Menu_Class_T))
		return;
	// The team menu goes out on the joining player's first think, before the HL25 client has its VGUI
	// viewport: it gets dropped and the player is left on the intro camera after the MOTD. Hold the first
	// one back until the client says it's ready (VModEnable); it then queues behind the MOTD as intended.
	if (pl && !pl->IsBot() && menuType == VGUI_Menu_Team)
	{
		McPlayer& mp = P(pl);
		if (mp.firstMenuAt <= 0.0f)
			mp.firstMenuAt = gpGlobals->time;
		mp.teamMenuBits = bitsSlots;
		if (!mp.clientReady || gpGlobals->time < mp.readyAt)
		{
			mp.teamMenuPending = true;
			return;
		}
		mp.teamMenuPending = false;
	}
	chain->callNext(pl, menuType, bitsSlots, oldMenu);
}

static void ShowPendingTeamMenus()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* pl = UTIL_PlayerByIndex(i);
		if (!pl || pl->IsBot())
			continue;
		McPlayer& mp = P(i);
		if (!mp.teamMenuPending)
			continue;
		if (pl->m_iJoiningState != PICKINGTEAM || pl->m_iMenu != Menu_ChooseTeam)
		{
			mp.teamMenuPending = false; // they got in some other way
			continue;
		}
		bool timedOut = gpGlobals->time > mp.firstMenuAt + 3.0f; // never heard VModEnable: send it anyway
		if ((mp.clientReady && gpGlobals->time >= mp.readyAt) || timedOut)
		{
			mp.clientReady = true;
			mp.readyAt = 0.0f;
			McLog("showing the held-back team menu for %s", STRING(pl->pev->netname));
			ShowVGUIMenu(pl, VGUI_Menu_Team, mp.teamMenuBits, (char*)(mp.teamMenuBits & MENU_KEY_6 ? "#Team_Select_Spect" : "#Team_Select"));
		}
	}
}

// Removes every Minecraft entity lying in the world (not the players' own state); returns how many.
int CountOrRemoveMcEntities(bool remove)
{
	static const char* kinds[] = {"mc_item", "mc_xp_orb", "mc_tnt", "mc_projectile", "mc_falling_block", "mc_firework"};
	int n = 0;
	for (int i = gpGlobals->maxClients + 1; i < gpGlobals->maxEntities; i++)
	{
		edict_t* ed = INDEXENT(i);
		if (!ed || ed->free || FStringNull(ed->v.classname) || (ed->v.flags & FL_KILLME))
			continue;
		const char* cn = STRING(ed->v.classname);
		if (strncmp(cn, "mc_", 3))
			continue;
		for (const char* k : kinds)
			if (!strcmp(cn, k))
			{
				CBaseEntity* e = CBaseEntity::Instance(ed);
				if (remove && e)
					UTIL_Remove(e);
				n++;
				break;
			}
	}
	return n;
}
static int RemoveMcEntities() { return CountOrRemoveMcEntities(true); }

static void H_RestartRound(IReGameHook_CSGameRules_RestartRound* chain)
{
	TeamMobsRemove(); // (whatever mob is still about: it does not start the next round)
	chain->callNext();
	// The "Game Commencing" restart re-sends InitHUD to everyone, which drops a team menu the client had
	// queued behind the MOTD: offer it again to whoever is still choosing a team.
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* pl = UTIL_PlayerByIndex(i);
		if (pl && !pl->IsBot() && pl->m_iJoiningState == PICKINGTEAM && pl->m_iMenu == Menu_ChooseTeam)
		{
			McPlayer& mp = P(i);
			mp.teamMenuPending = true;
			mp.clientReady = true;
			mp.readyAt = gpGlobals->time + 0.5f;
			if (!mp.teamMenuBits)
				mp.teamMenuBits = MENU_KEY_1 | MENU_KEY_2 | MENU_KEY_5 | MENU_KEY_6;
		}
	}
	extern void ResetWorld();
	if (cv_worldReset.value != 0.0f)
		ResetWorld();
	// Minecraft things lying around (items, XP orbs, primed TNT, arrows, falling blocks) are cleared with
	// the round, like Counter-Strike clears dropped guns. ReGameDLL finds entities by classname through a
	// hash that only map-spawned entities are in, so walk every slot.
	int removed = RemoveMcEntities();
	if (removed)
		McLog("round restart: removed %d Minecraft entities", removed);
	BotTacticsRoundRestart();
}

static void H_PM_Move(IReGameHook_PM_Move* chain, struct playermove_s* ppmove, int server)
{
	MC_WHERE("PM_Move");
	mcm::InstallTraceWrappers(ppmove);
	if (server)
		BotControlMove(ppmove);
	if (mcm::PreMove(ppmove))
		return;
	chain->callNext(ppmove, server);
}

static void H_PreThink(IReGameHook_CBasePlayer_PreThink* chain, CBasePlayer* pl)
{
	chain->callNext(pl);
	MC_WHERE("PreThink");
	McPlayer& mp = P(pl);
	{
		// a sword, an axe or the mace in hand: 5% on top of the speed Counter-Strike allows right now
		bool melee = pl->IsAlive() && HoldsMcMelee(pl);
		if (melee || mp.meleeSpeed)
		{
			pl->ResetMaxSpeed();
			if (melee && pl->pev->maxspeed > 1.0f)
				pl->pev->maxspeed *= 1.05f;
			mp.meleeSpeed = melee;
		}
	}
	if (pl->IsBot())
	{
		bool active = pl->m_pActiveItem && pl->m_pActiveItem->m_iId == WEAPON_GLOCK;
		if (active != mp.mcItemActive)
		{
			mp.mcItemActive = active;
			UpdatePlayerFlags(pl);
		}
		extern void BotGearThink(CBasePlayer * bot);
		if (TeamMobPreThink(pl))
			return; // (a mob: no blocks, no gear)
		BotTacticsThink(pl);
		MC_WHERE("BotGearThink");
		BotGearThink(pl);
		return;
	}
	bool mcActive = pl->m_pActiveItem && pl->m_pActiveItem->m_iId == WEAPON_GLOCK;
	if (mcActive != mp.mcItemActive)
	{
		mp.mcItemActive = mcActive;
		mp.invDirty = true;
	}
	// Minecraft hearts/armor replace CS's health/armor readout; MC crosshair replaces CS's while holding MC items
	pl->m_iHideHUD |= HIDEHUD_HEALTH;
	if (mcActive)
		pl->m_iHideHUD |= HIDEHUD_CROSSHAIR;
	else
		pl->m_iHideHUD &= ~HIDEHUD_CROSSHAIR;
	if (!mcActive)
	{
		// mirror CS weapon slot selection into the hotbar highlight
		if (pl->m_pActiveItem)
		{
			int cs = pl->m_pActiveItem->iItemSlot() - 1; // 0 primary .. 4 C4, same order as the tokens
			for (int i = 0; cs >= 0 && cs < 5 && i < mcp::HOTBAR_SIZE; i++)
				if (mp.hotbar[i].id == mci::CS_TOKEN_BASE + cs && !mp.hotbar[i].Empty() && mp.selected != i)
				{
					mp.selected = i;
					mp.invDirty = true;
				}
		}
	}
	UpdatePlayerFlags(pl);
}

static void H_PostThink(IReGameHook_CBasePlayer_PostThink* chain, CBasePlayer* pl)
{
	chain->callNext(pl);
	if (pl->IsBot())
		return;
	McPlayer& mp = P(pl);
	HungerFrame(pl);
	SyncCsTokens(pl);
	if (mp.invDirty)
		SendInventory(pl);
	if (mp.statDirty)
		SendStats(pl);
}

extern void InstallEngineTraceWrappers();
extern void H_ExplodeHe(IReGameHook_CGrenade_ExplodeHeGrenade* chain, CGrenade* g, TraceResult* tr, int bits);
extern void H_ExplodeBomb(IReGameHook_CGrenade_ExplodeBomb* chain, CGrenade* g, TraceResult* tr, int bits);

void OnGiveFnptrs()
{
	extern void InstallMsgTrace();
	InstallMsgTrace();
	extern void BotGearInit();
	BotGearInit();
	BotTacticsInit();
	TeamMobsInit();
	// register before the command line (+mc_autojoin 1 ...) is executed
	CVAR_REGISTER(&cv_buyInv);
	CVAR_REGISTER(&cv_xpPerKill);
	CVAR_REGISTER(&cv_autokit);
	CVAR_REGISTER(&cv_autojoin);
	CVAR_REGISTER(&cv_worldReset);
	extern cvar_t g_cvBombRadius; // mc_world_srv.cpp
	CVAR_REGISTER(&g_cvBombRadius);
	CVAR_REGISTER(&cv_botArmor);
	extern void RegisterTestCvars();
	RegisterTestCvars();
	HitRigsInit();
	FireInit();
	EconomyInit();
	InstallEngineTraceWrappers();
	mcw::InitBlockRegistry();
	g_ReGameHookchains.m_InternalCommand.registerHook(&H_InternalCommand, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_Spawn.registerHook(&H_Spawn, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_HasRestrictItem.registerHook(&H_HasRestrictItem, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_TakeDamage.registerHook(&H_TakeDamage, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_TraceAttack.registerHook(&H_TraceAttack, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_AddAccount.registerHook(&H_AddAccount, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_Killed.registerHook(&H_Killed, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_DeathSound.registerHook(&H_DeathSound, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_Pain.registerHook(&H_Pain, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_PM_Move.registerHook(&H_PM_Move, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_ShowVGUIMenu.registerHook(&H_ShowVGUIMenu, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CSGameRules_RestartRound.registerHook(&H_RestartRound, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_PreThink.registerHook(&H_PreThink, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_PostThink.registerHook(&H_PostThink, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CGrenade_ExplodeHeGrenade.registerHook(&H_ExplodeHe, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CGrenade_ExplodeBomb.registerHook(&H_ExplodeBomb, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_IsPenetrableEntity.registerHook(&H_IsPenetrable, HC_PRIORITY_DEFAULT);
}


void Precache()
{
	PRECACHE_MODEL("sprites/ledglow.spr");
	// Minecraft step sounds are played through the engine's server-side pmove sound path: precache them
	for (int g = 0; g < 8; g++)
	{
		const mcs::SoundEvent& ev = mcs::g_sounds[mcs::MCS_BLOCK_BASE + g * 4 + 3];
		for (int v = 0; v < ev.numVariants; v++)
			PRECACHE_SOUND(ev.variants[v].file);
	}
	UTIL_PrecacheOtherWeapon("weapon_mcitem");
}

// Minecraft keeps a player's inventory across dimension changes, but the engine disconnects everyone on a
// changelevel: remember the lasting part by name and hand it back when the player comes back in.
struct McSaved
{
	mci::Stack hotbar[mcp::HOTBAR_SIZE];
	mci::Stack armor[mci::NUM_ARMOR_SLOTS];
	mci::Stack inv[27];
	int selected;
	int xpTotal, xpLevel;
	bool creative;
	uint32_t advancements;
};
static std::map<std::string, McSaved> g_saved;

void OnClientPutInServer(edict_t* ent)
{
	int idx = ENTINDEX(ent);
	McPlayer& mp = P(idx);
	// state was reset in OnClientDisconnect; ClientPutInServer already ran Spawn (and maybe an autokit)
	auto it = g_saved.find(STRING(ent->v.netname));
	if (it != g_saved.end() && !(ent->v.flags & FL_FAKECLIENT))
	{
		const McSaved& sv = it->second;
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
			mp.hotbar[i] = sv.hotbar[i];
		for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
			mp.armor[i] = sv.armor[i];
		for (int i = 0; i < 27; i++)
			mp.inv[i] = sv.inv[i];
		mp.selected = sv.selected;
		mp.xpTotal = sv.xpTotal;
		mp.xpLevel = sv.xpLevel;
		mp.creative = sv.creative;
		mp.advancements = sv.advancements;
		g_saved.erase(it);
		McLog("restored inventory for %s", STRING(ent->v.netname));
		CBasePlayer* pl = (CBasePlayer*)CBaseEntity::Instance(ent);
		if (pl)
			UpdatePlayerFlags(pl);
	}
	mp.active = true;
	mp.invDirty = mp.statDirty = true;
	extern void SendWorldToClient(edict_t * ent);
	SendWorldToClient(ent);
	extern void SendCharacters(edict_t * to);
	SendCharacters(ent);
}

void OnClientDisconnect(edict_t* ent)
{
	TeamMobDisconnect(ENTINDEX(ent));
	McPlayer& mp = P(ENTINDEX(ent));
	const char* name = STRING(ent->v.netname);
	if (mp.active && name && name[0] && !(ent->v.flags & FL_FAKECLIENT))
	{
		McSaved& sv = g_saved[name];
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
			sv.hotbar[i] = mp.hotbar[i];
		for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
			sv.armor[i] = mp.armor[i];
		for (int i = 0; i < 27; i++)
			sv.inv[i] = mp.inv[i];
		sv.selected = mp.selected;
		sv.xpTotal = mp.xpTotal;
		sv.xpLevel = mp.xpLevel;
		sv.creative = mp.creative;
		sv.advancements = mp.advancements;
	}
	mp = McPlayer();
}

void OnUpdateClientData(const edict_t* ent, struct clientdata_s* cd)
{
	cd->iuser4 = ent->v.iuser4;
	cd->vuser1[0] = ent->v.vuser1[0];
	cd->vuser1[1] = ent->v.vuser1[1];
	cd->vuser1[2] = ent->v.vuser1[2];
}

extern void WorldStartFrame();

// Test runs: a thread that notices when the server stops finishing frames, and says where it was.
const char* volatile g_mcWhere = "outside the mod";
static volatile unsigned g_frames = 0;
static DWORD WINAPI Watchdog(LPVOID)
{
	unsigned last = 0;
	int still = 0;
	for (;;)
	{
		Sleep(1000);
		unsigned n = g_frames;
		if (n != last || n == 0)
		{
			last = n;
			still = 0;
			continue;
		}
		if (++still == 3)
		{
			char path[512], gd[256];
			GET_GAME_DIR(gd);
			Q_snprintf(path, sizeof(path), "%s/logs/mc_watchdog.log", gd);
			if (FILE* f = fopen(path, "a"))
			{
				fprintf(f, "no frame finished for 3 s after frame %u (game time %.2f): in \"%s\"\n", n, gpGlobals ? gpGlobals->time : 0.0f, g_mcWhere);
				fclose(f);
			}
		}
	}
}

// ... and what a crash leaves behind: the faulting address and the return addresses on the stack that lie in
// this DLL, as offsets to look up in mp.map.
static LONG CALLBACK CrashNote(EXCEPTION_POINTERS* x)
{
	DWORD code = x->ExceptionRecord->ExceptionCode;
	static int notes = 0;
	if ((code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_ILLEGAL_INSTRUCTION &&
			code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_PRIV_INSTRUCTION) ||
		notes >= 4)
		return EXCEPTION_CONTINUE_SEARCH;
	notes++;
	HMODULE self = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&CrashNote, &self);
	HMODULE at = nullptr;
	char module[MAX_PATH] = "?";
	if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)x->ExceptionRecord->ExceptionAddress, &at))
		GetModuleFileNameA(at, module, sizeof(module));
	char path[512], gd[256];
	GET_GAME_DIR(gd);
	Q_snprintf(path, sizeof(path), "%s/logs/mc_watchdog.log", gd);
	FILE* f = fopen(path, "a");
	if (!f)
		return EXCEPTION_CONTINUE_SEARCH;
	fprintf(f, "exception %08lx at %p (%s +%lx), game time %.2f, in \"%s\"\n", code, x->ExceptionRecord->ExceptionAddress, module,
		(unsigned long)((char*)x->ExceptionRecord->ExceptionAddress - (char*)at), gpGlobals ? gpGlobals->time : 0.0f, g_mcWhere);
	if (code == EXCEPTION_ACCESS_VIOLATION && x->ExceptionRecord->NumberParameters >= 2)
		fprintf(f, "  %s address %p\n", x->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading", (void*)x->ExceptionRecord->ExceptionInformation[1]);
	// the stack: every word that points into this DLL's code is (very likely) a return address
	MEMORY_BASIC_INFORMATION mbi;
	const DWORD* sp = (const DWORD*)x->ContextRecord->Esp;
	if (self && code != EXCEPTION_STACK_OVERFLOW && VirtualQuery(sp, &mbi, sizeof(mbi)))
	{
		const DWORD* end = (const DWORD*)((char*)mbi.BaseAddress + mbi.RegionSize);
		const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)((char*)self + ((const IMAGE_DOS_HEADER*)self)->e_lfanew);
		DWORD lo = (DWORD)self, hi = lo + nt->OptionalHeader.SizeOfImage;
		fprintf(f, "  mp.dll offsets on the stack:");
		for (int n = 0; sp < end && n < 40; sp++)
			if (*sp >= lo && *sp < hi)
			{
				fprintf(f, " %lx", (unsigned long)(*sp - lo));
				n++;
			}
		fprintf(f, "\n");
	}
	fclose(f);
	return EXCEPTION_CONTINUE_SEARCH;
}

void StartFrame()
{
	g_frames++;
	static bool watching = false;
	if (!watching && CVAR_GET_STRING("mc_testscript")[0])
	{
		watching = true;
		CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
		AddVectoredExceptionHandler(1, CrashNote);
	}
	MC_WHERE("StartFrame");
	static float nextBeat = 0.0f;
	if (gpGlobals->time >= nextBeat)
	{
		nextBeat = gpGlobals->time + 5.0f;
		CBasePlayer* h = UTIL_PlayerByIndex(1);
		McLog("beat: time %.1f autojoin %.0f quota %.0f p1 %s team %d join %d alive %d", gpGlobals->time, cv_autojoin.value, CVAR_GET_FLOAT("bot_quota"),
			h ? STRING(h->pev->netname) : "-", h ? (int)h->m_iTeam : -1, h ? (int)h->m_iJoiningState : -1, h ? (int)h->IsAlive() : -1);
	}
	{
		MC_WHERE("WorldStartFrame");
		WorldStartFrame();
	}
	extern void TestFrame();
	{
		MC_WHERE("TestFrame");
		TestFrame();
	}
	{
		MC_WHERE("CreeperFrame");
		CreeperFrame();
		MC_WHERE("TeamMobFrame");
		TeamMobFrame();
	}
	{
		MC_WHERE("FireFrame");
		FireFrame();
	}
	{
		MC_WHERE("BotTacticsFrame");
		BotTacticsFrame();
	}
	{
		MC_WHERE("HitRigsFrame");
		HitRigsFrame();
	}
	MC_WHERE("StartFrame: the rest");
	ShowPendingTeamMenus();
	// Minecraft death: the body vanishes in a puff after a moment and drops its XP.
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* pl = UTIL_PlayerByIndex(i);
		if (!pl)
			continue;
		McPlayer& mp = P(i);
		// test convenience: put humans straight onto CT
		if (cv_autojoin.value != 0.0f && !pl->IsBot() && pl->m_iTeam == UNASSIGNED && gpGlobals->time > mp.autojoinAt)
		{
			mp.autojoinAt = gpGlobals->time + 2.0f;
			McLog("autojoin: player %d joining state %d", i, (int)pl->m_iJoiningState);
			pl->m_iMenu = Menu_ChooseTeam;
			if (HandleMenu_ChooseTeam(pl, MENU_SLOT_TEAM_CT))
				HandleMenu_ChooseAppearance(pl, 6);
		}
		if (cv_autojoin.value != 0.0f && !pl->IsBot() && pl->m_iTeam == CT && !pl->IsAlive() && pl->m_iJoiningState != JOINED &&
			gpGlobals->time > mp.autojoinAt - 1.0f)
		{
			mp.autojoinAt = gpGlobals->time + 2.0f;
			pl->m_iJoiningState = JOINED;
			pl->RoundRespawn();
			if (!pl->HasNamedPlayerItem("weapon_knife"))
				pl->GiveDefaultItems();
		}
		if (!pl->IsAlive() && !mp.deathEffectsDone && mp.deathTime > 0.0f && gpGlobals->time - mp.deathTime > 1.0f)
		{
			mp.deathEffectsDone = true;
			Vector org = pl->pev->origin;
			FxDeath(i, org);
			int xp = (int)CVAR_GET_FLOAT("mc_xp_per_kill");
			if (mp.xpLevel > 0)
				xp = max(xp, min(mp.xpLevel * 7, 100)); // Minecraft player XP drop
			// a bot does not go round collecting orbs: its kill pays it the experience directly
			CBasePlayer* killer = mp.killer > 0 ? UTIL_PlayerByIndex(mp.killer) : nullptr;
			if (killer && killer->IsBot() && killer->IsAlive())
				GiveXp(killer, xp + RANDOM_LONG(0, 6));
			else
				DropXp(org + Vector(0, 0, 8), xp + RANDOM_LONG(0, 6));
			pl->pev->effects |= EF_NODRAW; // corpse disappears like a Minecraft mob
		}
	}
}
} // namespace mc

namespace mc
{
int RoomFor(CBasePlayer* pl, int itemId, int damage)
{
	if (!pl || !mci::ValidItem(itemId))
		return 0;
	McPlayer& mp = P(pl);
	int max = mci::Item(itemId).maxStack > 0 ? mci::Item(itemId).maxStack : 1;
	int room = 0;
	auto count = [&](const mci::Stack& s) {
		if (s.Empty())
			room += max;
		else if (s.id == itemId && s.damage == damage)
			room += max - s.count;
	};
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
		count(mp.hotbar[i]);
	for (const auto& s : mp.inv)
		count(s);
	return room;
}
} // namespace mc
