// Mobs among the bots.
// A bot whose name contains "Creeper" plays the mob: it only ever carries a knife (so the CS bot AI rushes
// its target), its melee does no damage, and it runs Minecraft's SwellGoal: within 3 blocks of a visible
// enemy it hisses and swells for 30 ticks (1.5 s) standing still, backing off past 7 blocks (or breaking
// line of sight) lets the fuse fall back, and a full fuse is a power-3 explosion that removes the creeper.
// Zombie-skinned bots get zombie/husk/drowned voices and drops; explosions use Minecraft's entity damage.
#include "precompiled.h"

#include "mc_server.h"

#include <vector>

namespace mc
{
static const float B2U = 40.0f;
static const int MAX_SWELL = 30;

static int g_swell[MAX_CLIENTS + 1];
static int g_swellDir[MAX_CLIENTS + 1];
static bool g_exploded[MAX_CLIENTS + 1];
static bool g_froze[MAX_CLIENTS + 1];
static float g_nextHunt[MAX_CLIENTS + 1];
static float g_nextTick = 0.0f;

bool IsCreeper(CBasePlayer* pl)
{
	return pl && pl->IsBot() && strstr(STRING(pl->pev->netname), "Creeper") != nullptr;
}

bool CreeperExploded(CBasePlayer* pl)
{
	return g_exploded[pl->entindex()];
}

// The same name/team hash the client uses to pick a bot's skin (mc_players.cpp PickSkin), so the voice
// matches the model.
Mob MobOf(CBasePlayer* pl)
{
	if (IsMobBot(pl))
		return TeamMobOf(pl) == TM_WITHER ? MOB_WITHER : MOB_GOLEM;
	const char* name = STRING(pl->pev->netname);
	if (strstr(name, "Creeper"))
		return MOB_CREEPER;
	if (strstr(name, "Enderman"))
		return MOB_ENDERMAN;
	if (strstr(name, "Zombie"))
		return MOB_ZOMBIE;
	if (strstr(name, "Husk"))
		return MOB_HUSK;
	if (strstr(name, "Drowned"))
		return MOB_DROWNED;
	static const char* players[] = {"Steve", "Alex", "Herobrine", "Notch"};
	for (const char* n : players)
		if (strstr(name, n))
			return MOB_PLAYER;
	if (pl->m_iTeam != TERRORIST)
		return MOB_PLAYER;
	int h = 0;
	for (const char* p = name; *p; p++)
		h = h * 31 + *p;
	h ^= pl->pev->team * 7919;
	static const Mob tMobs[] = {MOB_HUSK, MOB_ZOMBIE, MOB_DROWNED, MOB_HUSK};
	return tMobs[(unsigned)h % 4];
}

int MobHurtSound(CBasePlayer* pl)
{
	switch (MobOf(pl))
	{
	case MOB_CREEPER: return mcs::MCS_CREEPER_HURT;
	case MOB_ENDERMAN: return mcs::MCS_ENDERMAN_HURT;
	case MOB_ZOMBIE: return mcs::MCS_ZOMBIE_HURT;
	case MOB_HUSK: return mcs::MCS_HUSK_HURT;
	case MOB_DROWNED: return mcs::MCS_DROWNED_HURT;
	case MOB_GOLEM: return mcs::MCS_IRON_GOLEM_HURT;
	case MOB_WITHER: return mcs::MCS_WITHER_HURT;
	default: return mcs::MCS_PLAYER_HURT;
	}
}

int MobDeathSound(CBasePlayer* pl)
{
	switch (MobOf(pl))
	{
	case MOB_CREEPER: return mcs::MCS_CREEPER_DEATH;
	case MOB_ENDERMAN: return mcs::MCS_ENDERMAN_DEATH;
	case MOB_ZOMBIE: return mcs::MCS_ZOMBIE_DEATH;
	case MOB_HUSK: return mcs::MCS_HUSK_DEATH;
	case MOB_DROWNED: return mcs::MCS_DROWNED_DEATH;
	case MOB_GOLEM: return mcs::MCS_IRON_GOLEM_DEATH;
	case MOB_WITHER: return mcs::MCS_WITHER_DEATH;
	default: return mcs::MCS_PLAYER_DEATH;
	}
}

// Loot dropped where a mob dies (0-2 gunpowder for creepers, 0-2 rotten flesh for zombies)
void MobDrops(CBasePlayer* pl, const Vector& org)
{
	const char* item = nullptr;
	switch (MobOf(pl))
	{
	case MOB_CREEPER: item = "gunpowder"; break;
	case MOB_ENDERMAN: item = "ender_pearl"; break;
	case MOB_ZOMBIE:
	case MOB_HUSK:
	case MOB_DROWNED: item = "rotten_flesh"; break;
	default: return;
	}
	int n = RANDOM_LONG(0, MobOf(pl) == MOB_ENDERMAN ? 1 : 2);
	int id = mci::FindItem(item);
	if (n > 0 && id > 0)
	{
		float o[3] = {org.x, org.y, org.z};
		SpawnItemEntity(o, id, n, nullptr);
	}
}

static void SetSwellBits(CBasePlayer* pl, int swell)
{
	pl->pev->iuser4 = (pl->pev->iuser4 & ~mcp::MCPF_SWELL_MASK) | (swell << mcp::MCPF_SWELL_SHIFT);
}

static void Unfreeze(int i, CBasePlayer* pl)
{
	if (g_froze[i])
	{
		pl->pev->flags &= ~FL_FROZEN;
		g_froze[i] = false;
	}
}

void CreeperSpawn(CBasePlayer* pl)
{
	int i = pl->entindex();
	g_swell[i] = 0;
	g_swellDir[i] = -1;
	g_exploded[i] = false;
	g_froze[i] = false;
	SetSwellBits(pl, 0);
	if (!IsCreeper(pl) || !pl->IsAlive())
		return;
	// no guns, no armor: the bot AI's knife rush is exactly a creeper's approach
	pl->RemoveAllItems(FALSE);
	pl->GiveNamedItem("weapon_knife");
	McPlayer& mp = P(pl);
	for (int s = 0; s < mci::NUM_ARMOR_SLOTS; s++)
		mp.armor[s] = mci::Stack();
}

// Creepers can't buy, pick up or be handed anything but their knife.
bool CreeperRestrictsItem(CBasePlayer* pl, int item)
{
	return IsCreeper(pl) && item != ITEM_KNIFE;
}

static void CreeperExplode(CBasePlayer* pl)
{
	int i = pl->entindex();
	g_exploded[i] = true;
	g_swell[i] = 0;
	SetSwellBits(pl, 0);
	Unfreeze(i, pl);
	Vector org = pl->pev->origin;
	McLog("creeper %s exploded", STRING(pl->pev->netname));
	pl->pev->effects |= EF_NODRAW; // Creeper.explodeCreeper discards the mob: no body, no death animation
	float o[3] = {org.x, org.y, org.z};
	Explode(o, 3.0f, nullptr, pl);
	if (pl->IsAlive())
	{
		pl->pev->health = 0.0f;
		pl->Killed(pl->pev, GIB_NEVER);
	}
}

void CreeperFrame()
{
	if (g_nextTick - gpGlobals->time > 1.0f)
		g_nextTick = gpGlobals->time; // new map: the clock restarted
	if (gpGlobals->time < g_nextTick)
		return;
	g_nextTick = gpGlobals->time + 0.05f; // Minecraft ticks at 20 Hz
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* pl = UTIL_PlayerByIndex(i);
		if (!IsCreeper(pl))
			continue;
		if (!pl->IsAlive())
		{
			g_swell[i] = 0;
			Unfreeze(i, pl);
			continue;
		}
		// SwellGoal: the target is the nearest living enemy
		CBasePlayer* target = nullptr;
		float best = 1e9f;
		for (int j = 1; j <= gpGlobals->maxClients; j++)
		{
			CBasePlayer* o = UTIL_PlayerByIndex(j);
			if (!o || o == pl || !o->IsAlive() || o->m_iTeam == pl->m_iTeam || (o->m_iTeam != TERRORIST && o->m_iTeam != CT))
				continue;
			float d = (o->pev->origin - pl->pev->origin).Length() / B2U;
			if (d < best)
			{
				best = d;
				target = o;
			}
		}
		bool active = target && (best < 3.0f || g_swellDir[i] > 0);
		int dir = (active && best <= 7.0f && pl->FVisible(target)) ? 1 : -1;
		if (dir > 0 && g_swell[i] == 0)
			FxSound(mcs::MCS_CREEPER_PRIMED, pl->pev->origin, 1.0f, 0.5f, i);
		g_swellDir[i] = dir;
		g_swell[i] = max(0, min(MAX_SWELL, g_swell[i] + dir));
		// the goal stops the creeper's navigation while it swells
		if (dir > 0 && !(pl->pev->flags & FL_FROZEN))
		{
			pl->pev->flags |= FL_FROZEN;
			g_froze[i] = true;
		}
		else if (dir < 0)
			Unfreeze(i, pl);
		SetSwellBits(pl, g_swell[i]);
		if (g_swell[i] >= MAX_SWELL)
		{
			CreeperExplode(pl);
			continue;
		}
		if (dir < 0 && target)
		{
			// NearestAttackableTargetGoal: lock on to a visible enemy inside the 16-block follow range and
			// close in (the bot's knife attack state runs straight at its victim); with nobody in sight,
			// stalk toward the nearest enemy so the creeper keeps finding the fight
			CCSBot* bot = static_cast<CCSBot*>(pl);
			if (best <= 16.0f && pl->FVisible(target))
			{
				if (!bot->IsAttacking() || bot->GetEnemy() != target)
					bot->Attack(target);
			}
			else if (gpGlobals->time >= g_nextHunt[i] && !bot->IsAttacking())
			{
				g_nextHunt[i] = gpGlobals->time + 3.0f;
				Vector goal = target->pev->origin;
				bot->MoveTo(&goal, FASTEST_ROUTE);
			}
		}
	}
}

// EnderMan.hurt: projectiles never land (an enderman teleports out of their way), and anything that isn't
// a living attacker makes it teleport 9 times out of 10. Counter-Strike's bullets count as projectiles, so
// only melee (a sword, an axe, the mace, a knife) or a blast can bring one down.
static bool TeleportRandomly(CBasePlayer* pl)
{
	// EnderMan.teleport: a random spot within 32 blocks; we pick from the bots' nav mesh so it is walkable
	std::vector<CNavArea*> nearby;
	for (CNavArea* a : TheNavAreaList)
	{
		Vector c = *a->GetCenter();
		float d = (c - pl->pev->origin).Length2D();
		if (d > 4.0f * B2U && d < 32.0f * B2U && fabsf(c.z - pl->pev->origin.z) < 32.0f * B2U)
			nearby.push_back(a);
	}
	for (int attempt = 0; attempt < 16 && !nearby.empty(); attempt++)
	{
		CNavArea* a = nearby[RANDOM_LONG(0, (int)nearby.size() - 1)];
		Vector dest = *a->GetCenter() + Vector(0, 0, 37);
		TraceResult tr;
		UTIL_TraceHull(dest, dest, ignore_monsters, human_hull, pl->edict(), &tr);
		if (tr.fStartSolid || tr.fAllSolid)
			continue;
		Vector from = pl->pev->origin;
		float f[3] = {from.x, from.y, from.z}, t[3] = {dest.x, dest.y, dest.z};
		FxParticles(mcp::PK_PORTAL, f, 64);
		FxSound(mcs::MCS_ENDERMAN_TELEPORT, from, 1.0f, 1.0f);
		UTIL_SetOrigin(pl->pev, dest);
		pl->pev->velocity = g_vecZero;
		FxParticles(mcp::PK_PORTAL, t, 64);
		FxSound(mcs::MCS_ENDERMAN_TELEPORT, dest, 1.0f, 1.0f, pl->entindex());
		McLog("enderman %s teleported %.0f blocks", STRING(pl->pev->netname), (dest - from).Length() / B2U);
		return true;
	}
	return false;
}

bool EndermanDodge(CBasePlayer* pl, CBaseEntity* inflictor, CBaseEntity* attacker, int bits)
{
	if (MobOf(pl) != MOB_ENDERMAN || !pl->IsAlive())
		return false;
	static float nextTeleport[MAX_CLIENTS + 1];
	bool projectile = (bits & DMG_BULLET) || (inflictor && FClassnameIs(inflictor->pev, "mc_projectile"));
	if (projectile)
	{
		// a full magazine shouldn't fling it across the map 30 times a second
		if (gpGlobals->time >= nextTeleport[pl->entindex()] && TeleportRandomly(pl))
			nextTeleport[pl->entindex()] = gpGlobals->time + 0.5f;
		return true; // immune either way
	}
	bool livingAttacker = attacker && attacker->IsPlayer();
	if (!livingAttacker && !(bits & DMG_FALL) && RANDOM_LONG(0, 9) != 0)
		TeleportRandomly(pl);
	return false;
}

// Explosion.getSeenPercent: the fraction of sample points on the target's box with a clear line to the blast
static float SeenPercent(const Vector& c, CBasePlayer* p)
{
	Vector mn = p->pev->origin + p->pev->mins, mx = p->pev->origin + p->pev->maxs;
	Vector size = mx - mn;
	float sw = size.x / B2U, sh = size.z / B2U;
	float dx = 1.0f / (sw * 2.0f + 1.0f), dz = 1.0f / (sh * 2.0f + 1.0f);
	float ox = (1.0f - floorf(1.0f / dx) * dx) / 2.0f;
	int seen = 0, total = 0;
	for (float fx = 0.0f; fx <= 1.0f; fx += dx)
		for (float fy = 0.0f; fy <= 1.0f; fy += dx)
			for (float fz = 0.0f; fz <= 1.0f; fz += dz)
			{
				Vector s(mn.x + size.x * fx + ox * B2U, mn.y + size.y * fy + ox * B2U, mn.z + size.z * fz);
				TraceResult tr;
				UTIL_TraceLine(s, c, ignore_monsters, p->edict(), &tr);
				if (tr.flFraction >= 1.0f)
					seen++;
				total++;
			}
	return total ? (float)seen / total : 0.0f;
}

// Explosion.explode, entity part: damage = (int)((i*i + i) / 2 * 7 * 2r + 1), i = (1 - d/2r) * exposure,
// and a push of i blocks/tick away from the blast.
void ExplosionHurt(const float* origin, float power, CBaseEntity* attacker, bool spareMates)
{
	Vector c(origin[0], origin[1], origin[2]);
	if (POINT_CONTENTS(c) == CONTENTS_SOLID)
		c.z += 4.0f;
	float r2 = power * 2.0f;
	entvars_t* att = attacker ? attacker->pev : VARS(INDEXENT(0));
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (!p || !p->IsAlive() || p->pev->deadflag != DEAD_NO)
			continue;
		if (spareMates && attacker && attacker->IsPlayer() && !g_pGameRules->FPlayerCanTakeDamage(p, attacker))
			continue;
		Vector feet = p->pev->origin;
		feet.z += p->pev->mins.z;
		float d = (feet - c).Length() / B2U / r2;
		if (d > 1.0f)
			continue;
		Vector dir = p->pev->origin + p->pev->view_ofs - c;
		float len = dir.Length();
		if (len < 0.01f)
			continue;
		dir = dir / len;
		float impact = (1.0f - d) * SeenPercent(c, p);
		float mcDamage = (float)(int)((impact * impact + impact) / 2.0f * 7.0f * r2 + 1.0f);
		p->pev->velocity = p->pev->velocity + dir * (impact * B2U * 20.0f);
		McLog("explosion hurts %s: dist %.2f impact %.2f damage %.0f", STRING(p->pev->netname), d * r2, impact, mcDamage);
		// Counter-Strike flings a body killed by a blast along m_vBlastVector, which it takes from the
		// inflictor and divides by its length. The inflictor here is no grenade: the vector is set from the
		// middle of the blast, and the world is passed as inflictor so that it is left alone (with the
		// attacker as inflictor a creeper, or somebody in their own TNT, got a zero vector and a body with
		// a velocity that is not a number, which hangs the engine).
		Vector blast = p->pev->origin - c;
		if (blast.Length() < 1.0f)
			blast = Vector(0, 0, 1);
		p->m_vBlastVector = blast;
		p->TakeDamage(VARS(INDEXENT(0)), att, mcDamage * mci::HP_PER_MC, DMG_BLAST);
		if (!p->IsAlive() && attacker && attacker->IsPlayer() && attacker != p)
			StoryTell(6.0f, "%s blew %s up", STRING(attacker->pev->netname), STRING(p->pev->netname));
	}
}
} // namespace mc
