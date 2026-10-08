// Minecraft advancements and death messages.
// Players earn a handful of real Minecraft advancements (the ones this world can produce): the client
// slides in the "Advancement Made!" toast and everyone reads "X has made the advancement [Y]" in chat.
// Deaths are announced with Minecraft's death messages (death.attack.* in the language file).
#include "precompiled.h"

#include "mc_server.h"

namespace mc
{
struct AdvDef
{
	const char* title;
	const char* icon;    // item name
	int frame;           // 0 task, 1 goal, 2 challenge (AdvancementType)
};
static const AdvDef kAdv[ADV_COUNT] = {
	{"Monster Hunter", "iron_sword", 0},
	{"Stone Age", "wooden_pickaxe", 0},
	{"Diamonds!", "diamond", 0},
	{"Suit Up", "iron_chestplate", 0},
	{"Cover Me with Diamonds", "diamond_chestplate", 0},
	{"Cover Me in Debris", "netherite_chestplate", 2},
	{"Sky's the Limit", "elytra", 1},
	{"Postmortal", "totem_of_undying", 1},
	{"Take Aim", "arrow", 0},
	{"Sniper Duel", "arrow", 2},
	{"Overkill", "mace", 2},
};

void SendAdvancementToast(CBasePlayer* pl, int adv); // mc_main.cpp (McToast kind 2)

void Award(CBasePlayer* pl, Adv adv)
{
	if (!pl || pl->IsBot() || adv < 0 || adv >= ADV_COUNT)
		return;
	McPlayer& mp = P(pl);
	if (mp.advancements & (1u << adv))
		return;
	mp.advancements |= 1u << adv;
	const AdvDef& d = kAdv[adv];
	McLog("advancement %s: %s", STRING(pl->pev->netname), d.title);
	SendAdvancementToast(pl, adv);
	// chat.type.advancement.task / .goal / .challenge
	static const char* verbs[] = {"has made the advancement", "has reached the goal", "has completed the challenge"};
	ToastAll(0, "%s %s %s[%s]", STRING(pl->pev->netname), verbs[d.frame], d.frame == 2 ? "\\d" : "\\g", d.title);
}

const char* AdvancementTitle(int adv) { return adv >= 0 && adv < ADV_COUNT ? kAdv[adv].title : ""; }
const char* AdvancementIcon(int adv) { return adv >= 0 && adv < ADV_COUNT ? kAdv[adv].icon : ""; }
int AdvancementFrame(int adv) { return adv >= 0 && adv < ADV_COUNT ? kAdv[adv].frame : 0; }

void CheckArmorAdvancements(CBasePlayer* pl)
{
	McPlayer& mp = P(pl);
	int diamond = 0, netherite = 0;
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
	{
		const mci::Stack& s = mp.armor[i];
		if (s.Empty())
			continue;
		const char* n = mci::Item(s.id).name;
		if (!strncmp(n, "iron_", 5))
			Award(pl, ADV_SUIT_UP);
		if (!strncmp(n, "diamond_", 8))
			diamond++;
		if (!strncmp(n, "netherite_", 10))
			netherite++;
		if (mci::Item(s.id).type == mci::IT_ELYTRA)
			Award(pl, ADV_SKYS_LIMIT);
	}
	if (diamond == 4)
		Award(pl, ADV_COVER_DIAMONDS);
	if (netherite == 4)
		Award(pl, ADV_COVER_DEBRIS);
}

// ---------------------------------------------------------------------------------------------
// Death messages

static float g_lastFallDamage[MAX_CLIENTS + 1];

void NoteFallDamage(CBasePlayer* pl, float damage)
{
	g_lastFallDamage[pl->entindex()] = damage;
}

void DeathMessage(CBasePlayer* victim, entvars_t* attackerVars, entvars_t* inflictorVars, int bits)
{
	const char* v = STRING(victim->pev->netname);
	CBaseEntity* attacker = attackerVars ? CBaseEntity::Instance(attackerVars) : nullptr;
	CBaseEntity* inflictor = inflictorVars ? CBaseEntity::Instance(inflictorVars) : nullptr;
	const char* a = (attacker && attacker->IsPlayer() && attacker != victim) ? STRING(attacker->pev->netname) : nullptr;
	if (bits & DMG_FALL)
	{
		// death.fell.accident.generic vs death.attack.fall
		if (g_lastFallDamage[victim->entindex()] >= 40.0f)
			ToastAll(0, "%s fell from a high place", v);
		else
			ToastAll(0, "%s hit the ground too hard", v);
		return;
	}
	if (bits & DMG_BLAST)
	{
		if (a)
			ToastAll(0, "%s was blown up by %s", v, a); // death.attack.explosion.player
		else
			ToastAll(0, "%s blew up", v); // death.attack.explosion
		return;
	}
	if (bits & DMG_BURN)
	{
		if (a)
			ToastAll(0, "%s was burned to a crisp while fighting %s", v, a); // death.attack.onFire.player
		else
			ToastAll(0, "%s went up in flames", v); // death.attack.inFire
		return;
	}
	if (inflictor && inflictor != attacker && FClassnameIs(inflictor->pev, "mc_arrow"))
	{
		ToastAll(0, a ? "%s was shot by %s" : "%s was shot", v, a); // death.attack.arrow
		return;
	}
	if (a && attacker->IsPlayer())
	{
		CBasePlayer* ap = (CBasePlayer*)attacker;
		bool melee = P(ap).mcItemActive || (ap->m_pActiveItem && ap->m_pActiveItem->m_iId == WEAPON_KNIFE);
		if (melee)
			ToastAll(0, "%s was slain by %s", v, a); // death.attack.player
		else
			ToastAll(0, "%s was shot by %s", v, a); // death.attack.arrow (guns are crossbows here)
		return;
	}
	if (attacker == victim)
		ToastAll(0, "%s died", v); // death.attack.generic
	else
		ToastAll(0, "%s died", v);
}
} // namespace mc
