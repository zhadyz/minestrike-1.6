// Bots and Minecraft gear. At each spawn a bot decides how to spend its money, like a player in the
// buy menu: some rounds it buys Minecraft gear (armor up to iron, plus a bow,
// crossbow or sword), and Counter-Strike's own buy AI spends whatever is left on guns. In a fight, a
// bot with Minecraft gear takes over from the CS AI's trigger finger: swords up close, bows and
// crossbows at range (aimed high enough for the arrow's drop).
#include "precompiled.h"

#include "mc_server.h"

namespace mc
{
bool CrossbowLoaded(CBasePlayer* pl); // mc_weapon.cpp

struct BotGear
{
	int weapon = 0;      // Minecraft weapon item bought this life (0 = plays Counter-Strike only)
	float drawStart = 0; // bow/crossbow: when the current draw started (0 = not drawing)
	float nextShot = 0;
};
static BotGear g_gear[MAX_CLIENTS + 1];

static cvar_t cv_botMc = {"mc_bot_mcgear", "40", FCVAR_SERVER, 40.0f, nullptr}; // % of rounds a bot buys Minecraft gear

struct Offer
{
	const char* item;
	int price;
};
// prices in Counter-Strike dollars (a full armor set is bought at once)
static const Offer kWeapons[] = {{"iron_sword", 400}, {"diamond_sword", 700}, {"bow", 500}, {"crossbow", 900}};
static const Offer kArmor[] = {{"leather", 400}, {"golden", 650}, {"chainmail", 900}, {"iron", 1300}}; // never above iron

void BotGearInit() { CVAR_REGISTER(&cv_botMc); }

void BotGearSpawn(CBasePlayer* bot)
{
	BotGear& g = g_gear[bot->entindex()];
	g = BotGear();
	McPlayer& mp = P(bot);
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
		mp.armor[i] = mci::Stack();
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
		mp.hotbar[i] = mci::Stack();
	Mob mob = MobOf(bot);
	if (mob == MOB_CREEPER || mob == MOB_ENDERMAN)
		return;
	if (RANDOM_LONG(1, 100) > (int)cv_botMc.value)
		return; // a Counter-Strike round for this bot
	int money = bot->m_iAccount;
	// a weapon first (whatever it can afford), then the best armor set left in the budget
	int choices[4], n = 0;
	for (int i = 0; i < 4; i++)
		if (kWeapons[i].price <= money)
			choices[n++] = i;
	if (!n)
		return;
	const Offer& w = kWeapons[choices[RANDOM_LONG(0, n - 1)]];
	int spent = w.price;
	int wid = mci::FindItem(w.item);
	mp.hotbar[mcp::FIRST_MC_SLOT].id = (uint16_t)wid;
	mp.hotbar[mcp::FIRST_MC_SLOT].count = 1;
	if (!strcmp(w.item, "bow") || !strcmp(w.item, "crossbow"))
	{
		mp.hotbar[mcp::FIRST_MC_SLOT + 1].id = (uint16_t)mci::FindItem("arrow");
		mp.hotbar[mcp::FIRST_MC_SLOT + 1].count = 64;
	}
	g.weapon = wid;
	const char* set = nullptr;
	for (int i = 3; i >= 0 && !set; i--)
		if (kArmor[i].price <= money - spent && RANDOM_LONG(0, 3))
		{
			set = kArmor[i].item;
			spent += kArmor[i].price;
		}
	if (set)
	{
		static const char* parts[4] = {"helmet", "chestplate", "leggings", "boots"};
		for (int i = 0; i < 4; i++)
		{
			char name[48];
			Q_snprintf(name, sizeof(name), "%s_%s", set, parts[i]);
			mp.armor[i].id = (uint16_t)mci::FindItem(name);
			mp.armor[i].count = 1;
		}
		bot->pev->armorvalue = 0; // Minecraft armor replaces kevlar
		bot->m_iKevlar = ARMOR_NONE;
	}
	bot->AddAccount(-spent, RT_PLAYER_BOUGHT_SOMETHING);
	if (!bot->HasNamedPlayerItem("weapon_mcitem"))
		bot->GiveNamedItem("weapon_mcitem");
	mp.invDirty = mp.statDirty = true;
	McLog("bot %s bought %s%s%s for $%d", STRING(bot->pev->netname), w.item, set ? " and " : "", set ? set : "", spent);
}

// Called from PreThink after the engine worked out this frame's buttons.
void BotGearThink(CBasePlayer* bot)
{
	BotGear& g = g_gear[bot->entindex()];
	if (!g.weapon || !bot->IsAlive())
		return;
	CCSBot* ai = static_cast<CCSBot*>(bot);
	CBasePlayer* enemy = ai->GetEnemy();
	if (!enemy || !enemy->IsAlive() || !ai->IsEnemyVisible())
	{
		g.drawStart = 0.0f;
		return;
	}
	const mci::ItemDef& d = mci::Item(g.weapon);
	bool ranged = d.type == mci::IT_BOW || d.type == mci::IT_CROSSBOW;
	Vector target = enemy->pev->origin + Vector(0, 0, 12); // chest
	Vector eye = bot->pev->origin + bot->pev->view_ofs;
	float dist = (target - eye).Length();
	if (!ranged && dist > 150.0f)
	{
		g.drawStart = 0.0f;
		return; // too far for the sword: let Counter-Strike's AI shoot
	}
	McPlayer& mp = P(bot);
	// hold the Minecraft weapon
	if (mp.selected != mcp::FIRST_MC_SLOT || !bot->m_pActiveItem || bot->m_pActiveItem->m_iId != WEAPON_GLOCK)
	{
		SelectSlot(bot, mcp::FIRST_MC_SLOT);
		g.drawStart = 0.0f;
		return;
	}
	// aim: arrows fall (gravity 0.05 blocks/tick^2 at ~3 blocks/tick): lift the aim by the drop
	Vector dir = target - eye;
	if (ranged)
	{
		float speed = (d.type == mci::IT_CROSSBOW ? 3.15f : 3.0f) * 40.0f * 20.0f; // units/s
		float t = dist / speed;
		dir.z += 0.5f * 0.05f * 40.0f * 400.0f * t * t;
	}
	Vector ang = UTIL_VecToAngles(dir);
	ang.x = -ang.x;
	bot->pev->v_angle = ang;
	bot->pev->angles = Vector(-ang.x / 3.0f, ang.y, 0);

	// the CS AI thinks it holds a pistol and taps attack: the Minecraft weapon decides instead
	bot->pev->button &= ~(IN_ATTACK | IN_ATTACK2);
	bot->m_afButtonPressed &= ~(IN_ATTACK | IN_ATTACK2);
	bot->m_afButtonReleased &= ~(IN_ATTACK | IN_ATTACK2);
	float now = gpGlobals->time;
	if (!ranged)
	{
		float cooldown = d.attackSpeed > 0.0f ? 1.0f / d.attackSpeed : 0.6f;
		if (dist < 120.0f && now - mp.lastSwing >= cooldown)
		{
			bot->pev->button |= IN_ATTACK;
			bot->m_afButtonPressed |= IN_ATTACK;
		}
		return;
	}
	if (now < g.nextShot)
		return;
	bool crossbow = d.type == mci::IT_CROSSBOW;
	if (crossbow && CrossbowLoaded(bot))
	{
		// loaded: one click fires
		bot->pev->button |= IN_ATTACK2;
		bot->m_afButtonPressed |= IN_ATTACK2;
		g.nextShot = now + 0.4f;
		return;
	}
	float drawTime = crossbow ? 1.3f : 1.0f;
	if (g.drawStart <= 0.0f)
	{
		g.drawStart = now;
		bot->pev->button |= IN_ATTACK2;
		bot->m_afButtonPressed |= IN_ATTACK2;
	}
	else if (now - g.drawStart < drawTime)
		bot->pev->button |= IN_ATTACK2; // keep drawing
	else
	{
		bot->m_afButtonReleased |= IN_ATTACK2; // let go: the arrow flies (or the crossbow is loaded)
		g.drawStart = 0.0f;
		g.nextShot = now + (crossbow ? 0.15f : 0.35f);
	}
}
} // namespace mc
