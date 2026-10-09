// Bots and Minecraft gear. At each spawn a bot decides how to spend its money, like a player in the buy
// menu: some rounds it shops for Minecraft gear at the price list's prices (mci::Price), one item at a time
// while its budget lasts (a bow or crossbow with arrows, a sword, a golden apple, armor pieces up to iron),
// and Counter-Strike's own buy AI spends whatever is left on guns. What it still carries from a round it
// lived through it keeps, like a player.
// Guns stay a bot's first choice. Its Minecraft weapons take over from the CS AI's trigger finger where
// they are the better tool: the sword when the enemy is on top of it, and against an opponent in Minecraft
// armor (which stops bullets but not blades or arrows) the bow or crossbow from afar, aimed high enough
// for the arrow's drop, and the sword from further out. A bot with only a pistol uses its bow at range.
// Between fights it eats its golden apple when it is hurt, and with experience levels to spend it sets
// down an enchanting table and enchants its gear (mc_enchant_srv.cpp).
#include "precompiled.h"

#include "mc_server.h"

namespace mc
{
bool CrossbowLoaded(CBasePlayer* pl); // mc_weapon.cpp
float ArmorReduction(CBasePlayer* pl); // mc_main.cpp

struct BotGear
{
	int ranged = 0;      // bow or crossbow owned (0 = none); its arrows are in the next slot
	int melee = 0;       // sword owned (0 = none)
	float drawStart = 0; // bow/crossbow: when the current draw started (0 = not drawing)
	float nextShot = 0;
	float nextChase = 0; // sword: when to plan the next run at the enemy
	float nextEat = 0;
	bool table = false;  // an enchanting table bought and not yet set down
	float nextEnchant = 0;
};
static BotGear g_gear[MAX_CLIENTS + 1];

// % of rounds a bot shops for Minecraft gear. Guns stay the main thing: a little above the original 40.
static cvar_t cv_botMc = {"mc_bot_mcgear", "45", FCVAR_SERVER, 45.0f, nullptr};

static const int SLOT_RANGED = mcp::FIRST_MC_SLOT, SLOT_ARROWS = mcp::FIRST_MC_SLOT + 1, SLOT_MELEE = mcp::FIRST_MC_SLOT + 2,
				 SLOT_FOOD = mcp::FIRST_MC_SLOT + 3;
static const char* kArmor[] = {"iron", "chainmail", "golden", "leather"}; // best first; never above iron
static const int BOT_ARROWS = 16;

void BotGearInit() { CVAR_REGISTER(&cv_botMc); }

static void Put(McPlayer& mp, int slot, int id, int count)
{
	mp.hotbar[slot] = mci::Stack();
	mp.hotbar[slot].id = (uint16_t)id;
	mp.hotbar[slot].count = (uint8_t)count;
}

void BotGearSpawn(CBasePlayer* bot)
{
	BotGear& g = g_gear[bot->entindex()];
	McPlayer& mp = P(bot);
	g.drawStart = g.nextShot = g.nextChase = g.nextEat = 0.0f;
	Mob mob = MobOf(bot);
	if (mob == MOB_CREEPER || mob == MOB_ENDERMAN)
	{
		// these two carry nothing
		g = BotGear();
		for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
			mp.armor[i] = mci::Stack();
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
			mp.hotbar[i] = mci::Stack();
		return;
	}
	// what it carried through the last round is still its own (a death drops everything)
	if (!g.ranged || mp.hotbar[SLOT_RANGED].id != g.ranged || mp.hotbar[SLOT_ARROWS].Empty())
		g.ranged = 0;
	if (!g.melee || mp.hotbar[SLOT_MELEE].id != g.melee)
		g.melee = 0;
	// The other side has had an iron golem about: nothing hurts one but a sword, so a sword it is this round,
	// the best this bot can pay for and still have a gun.
	if (!g.melee && GolemSeenLately(bot->m_iTeam))
	{
		static const char* kSwords[3] = {"diamond_sword", "iron_sword", "wooden_sword"};
		static const int kKeep[3] = {3000, 1500, 0};
		for (int k = 0; k < 3; k++)
		{
			int id = mci::FindItem(kSwords[k]), cost = mci::Price(id);
			if (id <= 0 || cost <= 0 || bot->m_iAccount < cost + kKeep[k])
				continue;
			bot->AddAccount(-cost, RT_PLAYER_BOUGHT_SOMETHING);
			Put(mp, SLOT_MELEE, id, 1);
			g.melee = id;
			if (!bot->HasNamedPlayerItem("weapon_mcitem"))
				bot->GiveNamedItem("weapon_mcitem");
			mp.invDirty = true;
			McLog("bot %s buys a %s: the other side has an iron golem", STRING(bot->pev->netname), kSwords[k]);
			break;
		}
	}
	if (RANDOM_LONG(1, 100) > (int)cv_botMc.value)
		return; // a Counter-Strike round for this bot

	// the budget: a third to two thirds of its money; the rest is for guns
	int budget = (int)(bot->m_iAccount * RANDOM_FLOAT(0.33f, 0.66f)), spent = 0;
	auto afford = [&](int cost) { return cost > 0 && spent + cost <= budget; };
	int arrow = mci::FindItem("arrow");
	// a bow or crossbow for range and a sword for close quarters, in the order this bot cares about them
	bool rangedFirst = RANDOM_LONG(0, 1) != 0;
	for (int pass = 0; pass < 2; pass++)
	{
		if ((pass == 0) == rangedFirst)
		{
			if (g.ranged)
				continue;
			const char* pick[2] = {RANDOM_LONG(0, 2) ? "bow" : "crossbow", "bow"};
			for (const char* w : pick)
			{
				int id = mci::FindItem(w), cost = mci::Price(id) + BOT_ARROWS * mci::Price(arrow);
				if (!afford(cost))
					continue;
				spent += cost;
				Put(mp, SLOT_RANGED, id, 1);
				Put(mp, SLOT_ARROWS, arrow, BOT_ARROWS);
				g.ranged = id;
				break;
			}
		}
		else if (!g.melee)
		{
			const char* pick[2] = {RANDOM_LONG(0, 1) ? "diamond_sword" : "iron_sword", "iron_sword"};
			for (const char* w : pick)
			{
				int id = mci::FindItem(w);
				if (!afford(mci::Price(id)))
					continue;
				spent += mci::Price(id);
				Put(mp, SLOT_MELEE, id, 1);
				g.melee = id;
				break;
			}
		}
	}
	// a golden apple for later, now and then
	int apple = mci::FindItem("golden_apple");
	if (mp.hotbar[SLOT_FOOD].Empty() && RANDOM_LONG(0, 2) == 0 && afford(mci::Price(apple)))
	{
		spent += mci::Price(apple);
		Put(mp, SLOT_FOOD, apple, 1);
	}
	// an enchanting table, when it has levels to spend there
	int tableItem = mci::FindItem("enchanting_table");
	if (!g.table && mp.xpLevel >= 2 && RANDOM_LONG(0, 1) && afford(mci::Price(tableItem)))
	{
		spent += mci::Price(tableItem);
		g.table = true;
	}
	// armor one piece at a time, the best it can still pay for: chest, legs, head, feet
	static const int order[4] = {mci::SLOT_CHEST, mci::SLOT_LEGS, mci::SLOT_HEAD, mci::SLOT_FEET};
	static const char* parts[4] = {"helmet", "chestplate", "leggings", "boots"};
	bool armored = false;
	for (int slot : order)
	{
		if (!mp.armor[slot].Empty() || RANDOM_LONG(0, 3) == 0)
			continue;
		for (const char* mat : kArmor)
		{
			char name[48];
			Q_snprintf(name, sizeof(name), "%s_%s", mat, parts[slot]);
			int id = mci::FindItem(name);
			if (!afford(mci::Price(id)))
				continue;
			spent += mci::Price(id);
			mp.armor[slot] = mci::Stack();
			mp.armor[slot].id = (uint16_t)id;
			mp.armor[slot].count = 1;
			armored = true;
			break;
		}
	}
	if (armored)
	{
		bot->pev->armorvalue = 0; // Minecraft armor replaces kevlar
		bot->m_iKevlar = ARMOR_NONE;
	}
	if (!spent)
		return;
	bot->AddAccount(-spent, RT_PLAYER_BOUGHT_SOMETHING);
	if ((g.ranged || g.melee) && !bot->HasNamedPlayerItem("weapon_mcitem"))
		bot->GiveNamedItem("weapon_mcitem");
	mp.invDirty = mp.statDirty = true;
	McLog("bot %s spent $%d on Minecraft gear (%s, %s%s), $%d left", STRING(bot->pev->netname), spent, g.ranged ? mci::Item(g.ranged).name : "no bow",
		g.melee ? mci::Item(g.melee).name : "no sword", armored ? ", armor" : "", bot->m_iAccount);
}

// For the test scenarios: a bow with arrows and a diamond sword, paid for by nobody
void BotGearTestEquip(CBasePlayer* bot)
{
	BotGear& g = g_gear[bot->entindex()];
	McPlayer& mp = P(bot);
	g = BotGear();
	g.ranged = mci::FindItem("bow");
	g.melee = mci::FindItem("diamond_sword");
	Put(mp, SLOT_RANGED, g.ranged, 1);
	Put(mp, SLOT_ARROWS, mci::FindItem("arrow"), 64);
	Put(mp, SLOT_MELEE, g.melee, 1);
	if (!bot->HasNamedPlayerItem("weapon_mcitem"))
		bot->GiveNamedItem("weapon_mcitem");
	mp.invDirty = true;
}

// Called from PreThink after the engine worked out this frame's buttons.
bool BotHasSword(CBasePlayer* bot)
{
	int i = bot ? bot->entindex() : 0;
	if (i < 1 || i > MAX_CLIENTS || !bot->IsBot())
		return false;
	return g_gear[i].melee && P(bot).hotbar[SLOT_MELEE].id == g_gear[i].melee && mci::Item(g_gear[i].melee).type == mci::IT_SWORD;
}

void BotGearThink(CBasePlayer* bot)
{
	BotGear& g = g_gear[bot->entindex()];
	if (!bot->IsAlive())
		return;
	McPlayer& mp = P(bot);
	CCSBot* ai = static_cast<CCSBot*>(bot);
	CBasePlayer* enemy = ai->GetEnemy();
	float now = gpGlobals->time;
	if (!enemy || !enemy->IsAlive() || !ai->IsEnemyVisible())
	{
		g.drawStart = 0.0f;
		// a quiet moment: hurt, and a golden apple in the pack
		if (bot->pev->health < 60.0f && (now >= g.nextEat || g.nextEat > now + 30.0f) && !mp.hotbar[SLOT_FOOD].Empty() &&
			mci::Item(mp.hotbar[SLOT_FOOD].id).type == mci::IT_FOOD)
		{
			g.nextEat = now + 8.0f;
			McLog("bot %s eats its %s at %.0f hp", STRING(bot->pev->netname), mci::Item(mp.hotbar[SLOT_FOOD].id).name, bot->pev->health);
			EatFood(bot, mp.hotbar[SLOT_FOOD].id);
			FxSound(mcs::MCS_PLAYER_BURP, bot->pev->origin, 0.5f, 0.9f + RANDOM_FLOAT(0.0f, 0.1f), bot->entindex());
			if (--mp.hotbar[SLOT_FOOD].count == 0)
				mp.hotbar[SLOT_FOOD] = mci::Stack();
			mp.invDirty = true;
		}
		// and a table in the pack with levels to spend: set it down and enchant something
		if (g.table && bot->pev->velocity.Length() < 60.0f && (now >= g.nextEnchant || g.nextEnchant > now + 30.0f))
		{
			g.nextEnchant = now + 4.0f;
			mci::Stack* gear[6] = {g.melee ? &mp.hotbar[SLOT_MELEE] : nullptr, g.ranged ? &mp.hotbar[SLOT_RANGED] : nullptr, &mp.armor[mci::SLOT_CHEST],
				&mp.armor[mci::SLOT_LEGS], &mp.armor[mci::SLOT_HEAD], &mp.armor[mci::SLOT_FEET]};
			if (BotEnchant(bot, gear, 6))
				g.table = false;
		}
		return;
	}
	if (g.ranged && (mp.hotbar[SLOT_RANGED].id != g.ranged || mp.hotbar[SLOT_ARROWS].Empty()))
		g.ranged = 0; // out of arrows
	if (g.melee && mp.hotbar[SLOT_MELEE].id != g.melee)
		g.melee = 0; // the sword broke
	if (!g.ranged && !g.melee)
		return;

	Vector target = enemy->pev->origin + Vector(0, 0, 12); // chest
	Vector eye = bot->pev->origin + bot->pev->view_ofs;
	float dist = (target - eye).Length();
	// Guns first: a bot with a rifle or SMG shoots it, and draws the sword only when the enemy is on top of
	// it. The Minecraft weapons come out where they are the better tool: against an opponent in Minecraft
	// armor (bullets do little to it, blades and arrows ignore it: the bow from afar, and the sword is
	// worth a longer run), and for a bot that has nothing better than a pistol.
	// And an iron golem: bullets and arrows do nothing to it and the bullets come back, so it is the sword
	// from any distance, or nothing.
	bool armored = ArmorReduction(enemy) >= 0.4f;
	bool golem = TeamMobOf(enemy) == TM_GOLEM;
	bool hasGun = bot->m_rgpPlayerItems[PRIMARY_WEAPON_SLOT] != nullptr;
	float swordRange = golem ? 6000.0f : armored ? 320.0f : hasGun ? 130.0f : 180.0f;
	int slot = -1;
	if (g.melee && dist <= swordRange)
		slot = SLOT_MELEE;
	else if (g.ranged && !golem && (armored || !hasGun) && (dist > 140.0f || !g.melee))
		slot = SLOT_RANGED;
	if (slot < 0)
	{
		g.drawStart = 0.0f;
		return;
	}
	bool ranged = slot == SLOT_RANGED;
	const mci::ItemDef& d = mci::Item(ranged ? g.ranged : g.melee);
	// hold the Minecraft weapon
	if (mp.selected != slot || !bot->m_pActiveItem || bot->m_pActiveItem->m_iId != WEAPON_GLOCK)
	{
		SelectSlot(bot, slot);
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
	if (ang.x < -180.0f)
		ang.x += 360.0f;
	// through the move command (BotControlMove): the engine takes a bot's view from there, and a view set
	// here alone is gone again before the weapon fires
	BotLook(bot, ang);

	// the CS AI thinks it holds a pistol and taps attack: the Minecraft weapon decides instead
	bot->pev->button &= ~(IN_ATTACK | IN_ATTACK2);
	bot->m_afButtonPressed &= ~(IN_ATTACK | IN_ATTACK2);
	bot->m_afButtonReleased &= ~(IN_ATTACK | IN_ATTACK2);
	if (!ranged)
	{
		// run at the enemy until the sword reaches
		if (dist > 100.0f && (now >= g.nextChase || g.nextChase > now + 5.0f))
		{
			g.nextChase = now + 0.5f;
			Vector goal = enemy->pev->origin;
			ai->MoveTo(&goal, FASTEST_ROUTE);
		}
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
