// Minecraft hunger and natural regeneration (FoodData, Java 1.21), on Counter-Strike's 100 HP scale
// (1 Minecraft health point = 5 HP). Food level 0..20 and saturation drive healing; exhaustion from
// running, jumping, attacking, being hit and healing burns saturation, then food. Eating restores both.
// Also the golden apple effects: Regeneration II for 5 s and 2 absorption hearts.
#include "precompiled.h"

#include "mc_server.h"

namespace mc
{
static const float TICK = 0.05f;

struct FoodDef
{
	const char* item;
	int nutrition;
	float saturation; // saturation points restored (nutrition * modifier * 2)
	bool always;      // edible with a full food bar (golden apples)
};
static const FoodDef kFoods[] = {
	{"golden_apple", 4, 9.6f, true},
	{"enchanted_golden_apple", 4, 9.6f, true},
	{"cooked_beef", 8, 12.8f, false},
	{"cooked_porkchop", 8, 12.8f, false},
	{"cooked_chicken", 6, 7.2f, false},
	{"bread", 5, 6.0f, false},
	{"baked_potato", 5, 6.0f, false},
	{"apple", 4, 2.4f, false},
	{"carrot", 3, 3.6f, false},
	{"golden_carrot", 6, 14.4f, false},
	{"cookie", 2, 0.4f, false},
	{"rotten_flesh", 4, 0.8f, false},
};

static const FoodDef* FoodOf(int itemId)
{
	const char* n = mci::Item(itemId).name;
	for (const FoodDef& f : kFoods)
		if (!strcmp(f.item, n))
			return &f;
	return nullptr;
}

bool CanEat(CBasePlayer* pl, int itemId)
{
	const FoodDef* f = FoodOf(itemId);
	if (!f)
		return mci::Item(itemId).type == mci::IT_FOOD;
	return f->always || P(pl).food < 20 || P(pl).creative;
}

void AddExhaustion(CBasePlayer* pl, float amount)
{
	McPlayer& mp = P(pl);
	if (mp.creative)
		return;
	mp.exhaustion = fminf(mp.exhaustion + amount, 40.0f);
}

// FoodData.eat + the item's effects
void EatFood(CBasePlayer* pl, int itemId)
{
	McPlayer& mp = P(pl);
	const FoodDef* f = FoodOf(itemId);
	int nutrition = f ? f->nutrition : 4;
	float sat = f ? f->saturation : 2.4f;
	mp.food = min(mp.food + nutrition, 20);
	mp.saturation = fminf(mp.saturation + sat, (float)mp.food);
	const char* n = mci::Item(itemId).name;
	if (!strcmp(n, "golden_apple"))
	{
		mp.regenUntil = gpGlobals->time + 5.0f; // Regeneration II, 5 s
		mp.regenLevel = 2;
		mp.absorption = fmaxf(mp.absorption, 4.0f * mci::HP_PER_MC); // Absorption I: 2 hearts
	}
	else if (!strcmp(n, "enchanted_golden_apple"))
	{
		mp.regenUntil = gpGlobals->time + 20.0f; // Regeneration II, 20 s
		mp.regenLevel = 2;
		mp.absorption = fmaxf(mp.absorption, 16.0f * mci::HP_PER_MC); // Absorption IV: 8 hearts
	}
	mp.statDirty = true;
}

void ResetFood(CBasePlayer* pl)
{
	McPlayer& mp = P(pl);
	mp.food = 20;
	mp.saturation = 5.0f;
	mp.exhaustion = 0.0f;
	mp.foodTimer = 0;
	mp.absorption = 0.0f;
	mp.regenUntil = 0.0f;
	mp.statDirty = true;
}

static void Heal(CBasePlayer* pl, float mcPoints)
{
	if (pl->pev->health >= pl->pev->max_health)
		return;
	pl->TakeHealth(mcPoints * mci::HP_PER_MC, DMG_GENERIC);
}

// Called every server frame per living human player; runs Minecraft's 20 Hz tick.
void HungerFrame(CBasePlayer* pl)
{
	McPlayer& mp = P(pl);
	if (!pl->IsAlive())
		return;
	// movement exhaustion, measured continuously (sprinting: 0.1 per block; CS's run speed is a sprint)
	Vector pos = pl->pev->origin;
	if (mp.lastPosValid && (pl->pev->flags & FL_ONGROUND))
	{
		float dx = pos.x - mp.lastPos.x, dy = pos.y - mp.lastPos.y;
		float d = sqrtf(dx * dx + dy * dy);
		if (d < 64.0f)
		{
			float speed = pl->pev->velocity.Length2D();
			AddExhaustion(pl, d / 40.0f * (speed > 200.0f ? 0.1f : 0.0f));
		}
	}
	bool onGround = (pl->pev->flags & FL_ONGROUND) != 0;
	if (mp.lastPosValid && mp.wasOnGround && !onGround && pl->pev->velocity.z > 100.0f)
		AddExhaustion(pl, pl->pev->velocity.Length2D() > 200.0f ? 0.2f : 0.05f); // (sprint) jump
	mp.wasOnGround = onGround;
	mp.lastPos = pos;
	mp.lastPosValid = true;

	if (gpGlobals->time < mp.nextFoodTick)
		return;
	mp.nextFoodTick = gpGlobals->time + TICK;
	bool hurt = pl->pev->health < pl->pev->max_health;
	// Regeneration effect: level II heals every 25 ticks
	if (mp.regenUntil > gpGlobals->time && hurt)
	{
		int interval = 50 >> (mp.regenLevel - 1);
		if (++mp.regenTimer >= interval)
		{
			Heal(pl, 1.0f);
			mp.regenTimer = 0;
		}
	}
	if (mp.creative)
		return;
	if (mp.exhaustion > 4.0f)
	{
		mp.exhaustion -= 4.0f;
		if (mp.saturation > 0.0f)
			mp.saturation = fmaxf(mp.saturation - 1.0f, 0.0f);
		else
			mp.food = max(mp.food - 1, 0);
		mp.statDirty = true;
	}
	if (mp.saturation > 0.0f && hurt && mp.food >= 20)
	{
		if (++mp.foodTimer >= 10)
		{
			float f = fminf(mp.saturation, 6.0f);
			Heal(pl, f / 6.0f);
			AddExhaustion(pl, f);
			mp.foodTimer = 0;
		}
	}
	else if (mp.food >= 18 && hurt)
	{
		if (++mp.foodTimer >= 80)
		{
			Heal(pl, 1.0f);
			AddExhaustion(pl, 6.0f);
			mp.foodTimer = 0;
		}
	}
	else if (mp.food <= 0)
	{
		// starving (normal difficulty): down to half a heart, never to death
		if (++mp.foodTimer >= 80)
		{
			if (pl->pev->health > 1.0f * mci::HP_PER_MC)
				pl->TakeDamage(VARS(INDEXENT(0)), VARS(INDEXENT(0)), 1.0f * mci::HP_PER_MC, DMG_GENERIC);
			mp.foodTimer = 0;
		}
	}
	else
		mp.foodTimer = 0;
}
} // namespace mc
