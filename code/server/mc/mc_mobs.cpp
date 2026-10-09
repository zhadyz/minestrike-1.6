// Team mobs: the iron golem. A player builds it the way Minecraft has it (four iron blocks in a T, a carved
// pumpkin set on top last), and it fights for the side of whoever set the pumpkin down.
//
// It is a Counter-Strike bot made in the middle of the round, on that side, with a knife only: so it knows
// the map and the round the way that side's bots do, and what it does there is not a wander-and-attack loop
// but the round's objective, by its side and the state of the bomb:
//   Counter-Terrorist, no bomb down: it holds the bomb site the other side is expected at;
//   Terrorist, no bomb down: it goes with the bomb carrier, and takes the site with him;
//   bomb planted: it goes to the bomb and stays on it, whichever side it is on.
// Whoever of the other side it meets there it fights. Its numbers are Minecraft's: 100 health (500 here),
// 7.5 to 21.5 a blow (38 to 108 here), a blow a second, and a blow throws its victim into the air. It is
// slower than a running player (190 against 250).
// Its quirk: nothing hurts it but a sword. Bullets do nothing to it and come back at whoever fired them,
// with all they had; arrows, knives, TNT, fire and falls do nothing. (Another mob's blow does: two golems
// settle it between them.) So a side that meets one needs swords, or goes round it: it does not plant or
// defuse. Bots know: one without a sword does not shoot at it and keeps away, one with a sword goes in. It does not count as a living member of its side when the round is decided, does not
// take the bomb or defuse, and is gone when the round is over.
#include "precompiled.h"

#include "mc_server.h"
#include "mc_blocks.h"
#include "mc_items.h"
#include "mc_sounds_gen.h"

#include <vector>

namespace mc
{
static cvar_t cv_golemMax = {"mc_golem_max", "1", FCVAR_SERVER, 1.0f, nullptr}; // living golems a side may have at once
// The wither. Four soul sand in a T and three wither skeleton skulls along its top; the last skull set brings
// it, for the side of whoever set it. It rises for eleven seconds where it was built, and nothing touches
// it then; everybody is told where. Then it goes off with a blast of power 7 (the bomb's own is about that),
// which everybody hears, and is loose: Minecraft's 300 health (1500 here), a point of it back every second.
// It plays the round's objective the way the golem does. Whoever of the other side it sees within 1500
// units gets a skull every two seconds: 40 damage and the Wither effect (5 health a second for ten
// seconds) on a hit, a small blast where it lands. A wall of blocks in its way gets a blue skull, whose
// blast takes every block there is but bedrock. A second after it was hurt the blocks around its body
// go, the map's own included. Below half its health arrows do nothing to it and bullets half. A side may
// raise one every mc_wither_rounds rounds.
static cvar_t cv_witherRounds = {"mc_wither_rounds", "8", FCVAR_SERVER, 8.0f, nullptr};
static const float kWitherHealth = 300.0f * mci::HP_PER_MC;
static const float kWitherSpeed = 220.0f;
static const float kWitherCharge = 11.0f;
static int g_roundNo = 0, g_witherRound[4] = {-100, -100, -100, -100};
static int g_golemSeen[4] = {-100, -100, -100, -100}; // the round a side last had the other side's golem in sight
bool GolemSeenLately(int team) { return team >= 0 && team < 4 && g_roundNo - g_golemSeen[team] <= 2; }

static const float kGolemHealth = 100.0f * mci::HP_PER_MC;
static const float kGolemSpeed = 190.0f;
static const float kGolemReach = 110.0f;

struct TeamMob
{
	int kind = TM_NONE;
	int builder = 0; // the player who built it
	float nextHit = 0, nextGoal = 0, swingUntil = 0;
	// the wither
	float chargeUntil = 0, nextSkull = 0, nextRegen = 0, breakAt = 0, nextAmbient = 0;
	bool risen = false;
	// holes (OutOfHoles)
	bool hovering = false;
	float holeSince = 0;
};
// Minecraft's Wither effect on a player: until when, when it next bites, whose it is
static struct
{
	float until = 0, next = 0;
	int by = 0;
} g_withered[MAX_CLIENTS + 1];

bool WitherAllowed(int team)
{
	return team >= 0 && team < 4 && TeamMobsAlive(team, TM_WITHER) < 1 && g_roundNo - g_witherRound[team] >= (int)cv_witherRounds.value;
}
void WitherEffect(CBasePlayer* victim, CBasePlayer* by)
{
	int i = victim ? victim->entindex() : 0;
	if (i < 1 || i > MAX_CLIENTS)
		return;
	g_withered[i].until = gpGlobals->time + 10.0f;
	g_withered[i].next = gpGlobals->time + 1.0f;
	g_withered[i].by = by ? by->entindex() : 0;
}
static TeamMob g_tm[MAX_CLIENTS + 1];

// bullets on their way back (dealt at the start of the next frame: not from inside the shot that made them)
struct Bounce
{
	int shooter, golem;
	float damage;
};
static std::vector<Bounce> g_bounces;
static float g_toldBounce[MAX_CLIENTS + 1], g_keepAway[MAX_CLIENTS + 1];

struct Request
{
	int kind, team;
	Vector feet;
	float yaw;
	int builder;
};
static std::vector<Request> g_requests;
static int g_spawning = 0; // the slot being brought to life now

bool IsMobBot(CBasePlayer* pl)
{
	int i = pl ? pl->entindex() : 0;
	return i >= 1 && i <= MAX_CLIENTS && g_tm[i].kind != TM_NONE;
}
int TeamMobOf(CBasePlayer* pl) { return IsMobBot(pl) ? g_tm[pl->entindex()].kind : TM_NONE; }

int TeamMobsAlive(int team, int kind)
{
	int n = 0;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = g_tm[i].kind == kind ? UTIL_PlayerByIndex(i) : nullptr;
		if (p && p->IsAlive() && p->m_iTeam == team)
			n++;
	}
	for (const Request& r : g_requests)
		n += (r.kind == kind && r.team == team) ? 1 : 0;
	return n;
}
int GolemLimit() { return max(0, (int)cv_golemMax.value); }

// (asked for from inside a player's own frame; made at the start of the next one)
void TeamMobRequest(int kind, int team, const Vector& feet, float yaw, CBasePlayer* by)
{
	Request r = {kind, team, feet, yaw, by ? by->entindex() : 0};
	g_requests.push_back(r);
}

static void Kick(CBasePlayer* pl)
{
	if (pl && !FNullEnt(pl->edict()))
		SERVER_COMMAND(UTIL_VarArgs("kick #%d\n", GETPLAYERUSERID(pl->edict())));
}

static const char* BuilderName(int index)
{
	CBasePlayer* b = (index >= 1 && index <= gpGlobals->maxClients) ? UTIL_PlayerByIndex(index) : nullptr;
	return b ? STRING(b->pev->netname) : "somebody";
}

static CBasePlayer* Create(const Request& r)
{
	if (!TheCSBots() || !TheBotProfiles || (r.team != TERRORIST && r.team != CT))
		return nullptr;
	if (UTIL_ClientsInGame() >= gpGlobals->maxClients)
	{
		McLog("mob: no free player slot for it");
		return nullptr;
	}
	// a bold bot's profile that is not in the game just now (the name it goes by is the mob's own)
	static const char* kProfiles[] = {"Dinnerbone", "Herobrine", "Notch", "Steve", "Alex", "Zombie"};
	const BotProfile* prof = nullptr;
	for (const char* n : kProfiles)
	{
		const BotProfile* p = TheBotProfiles->GetProfile(n, BOT_TEAM_ANY);
		if (!p)
			continue;
		if (!prof)
			prof = p;
		if (!UTIL_IsNameTaken(n, true))
		{
			prof = p;
			break;
		}
	}
	if (!prof)
	{
		McLog("mob: no bot profile to make it from");
		return nullptr;
	}
	edict_t* e = CREATE_FAKE_CLIENT(r.kind == TM_WITHER ? "Wither" : "Iron Golem");
	if (FNullEnt(e))
		return nullptr;
	string_t name = e->v.netname;
	Q_memset(&e->v, 0, sizeof(e->v));
	e->v.netname = name;
	e->v.flags = FL_FAKECLIENT | FL_CLIENT;
	e->v.pContainingEntity = e;
	FREE_PRIVATE(e);
	CCSBot* bot = GetClassPtr<CAPI_CSBot>((CCSBot*)VARS(e));
	bot->Initialize(prof);
	int i = bot->entindex();
	g_tm[i] = TeamMob(); // (a mob before anything counts it)
	g_tm[i].kind = r.kind;
	g_tm[i].builder = r.builder;
	ClientPutInServer(e);
	SET_CLIENT_KEY_VALUE(i, GET_INFO_BUFFER(e), "*bot", "1");
	bot->m_iMenu = Menu_ChooseTeam;
	bot->m_iJoiningState = PICKINGTEAM;
	if (!HandleMenu_ChooseTeam(bot, r.team))
	{
		McLog("mob: could not join side %d", r.team);
		Kick(bot);
		return nullptr;
	}
	HandleMenu_ChooseAppearance(bot, 1);
	g_spawning = i;
	bot->GetIntoGame();
	g_spawning = 0;
	if (!bot->IsAlive())
	{
		McLog("mob: it did not come to life");
		Kick(bot);
		return nullptr;
	}
	UTIL_SetOrigin(bot->pev, r.feet + Vector(0, 0, 37));
	bot->pev->velocity = g_vecZero;
	bot->pev->angles = Vector(0, r.yaw, 0);
	bot->pev->v_angle = bot->pev->angles;
	bot->pev->fixangle = 1;
	bot->Idle();
	if (r.kind == TM_WITHER)
	{
		g_tm[i].chargeUntil = gpGlobals->time + kWitherCharge;
		if (r.team >= 0 && r.team < 4)
			g_witherRound[r.team] = g_roundNo;
	}
	return bot;
}

// From the spawn hook: a knife (which only makes the bot's own AI close in), its health, nothing else
void TeamMobSpawned(CBasePlayer* pl)
{
	if (!IsMobBot(pl) || !pl->IsAlive())
		return;
	pl->RemoveAllItems(FALSE);
	pl->GiveNamedItem("weapon_knife");
	pl->pev->health = pl->pev->max_health = TeamMobOf(pl) == TM_WITHER ? kWitherHealth : kGolemHealth;
	pl->pev->armorvalue = 0.0f;
	McPlayer& mp = P(pl);
	for (int s = 0; s < mci::NUM_ARMOR_SLOTS; s++)
		mp.armor[s] = mci::Stack();
	for (int s = 0; s < mcp::HOTBAR_SIZE; s++)
		mp.hotbar[s] = mci::Stack();
	mp.invDirty = mp.statDirty = true;
}

// From PreThink: true for a mob (it has no business with blocks, gear or shopping)
bool TeamMobPreThink(CBasePlayer* pl)
{
	if (!IsMobBot(pl))
		return false;
	float top = TeamMobOf(pl) == TM_WITHER ? kWitherSpeed : kGolemSpeed;
	if (pl->IsAlive() && pl->pev->maxspeed > top)
		pl->pev->maxspeed = top;
	return true;
}

static bool HoldsSword(CBasePlayer* pl)
{
	const mci::Stack& s = HeldStack(pl);
	return !s.Empty() && !mci::IsCsToken(s.id) && mci::Item(s.id).type == mci::IT_SWORD;
}

// From the hit hook (bullets, arrows, blades and knives all come this way). True: it does nothing to it.
bool TeamMobBullet(CBasePlayer* victim, entvars_t* attacker, float damage, TraceResult* tr, int bits)
{
	if (TeamMobOf(victim) != TM_GOLEM)
		return false;
	CBaseEntity* att = attacker ? CBaseEntity::Instance(attacker) : nullptr;
	if (!att || !att->IsPlayer())
		return true;
	CBasePlayer* shooter = static_cast<CBasePlayer*>(att);
	if (IsMobBot(shooter))
		return false;
	if ((bits & DMG_SLASH) && HoldsSword(shooter))
		return false; // the one thing that hurts it
	if (WasBulletOf(attacker) && shooter->IsAlive() && shooter->m_iTeam != victim->m_iTeam)
	{
		// the bullet comes back with all it had
		if (tr)
			UTIL_Sparks(tr->vecEndPos);
		EMIT_SOUND_DYN(victim->edict(), CHAN_BODY, RANDOM_LONG(0, 1) ? "weapons/ric_metal-1.wav" : "weapons/ric_metal-2.wav", 1.0f, ATTN_NORM, 0,
			RANDOM_LONG(95, 110));
		if (g_bounces.size() < 64)
			g_bounces.push_back({shooter->entindex(), victim->entindex(), damage});
	}
	return true;
}

// From the damage hook: true when there is no damage to take
bool TeamMobDamage(CBasePlayer* victim, CBaseEntity* inflictor, CBaseEntity* attacker, float& damage, int bits)
{
	if (TeamMobOf(victim) == TM_WITHER)
	{
		TeamMob& m = g_tm[victim->entindex()];
		if (gpGlobals->time < m.chargeUntil || (bits & DMG_FALL))
			return true; // rising, nothing touches it; and it does not fall
		if (attacker && attacker->IsPlayer() && TeamMobOf(static_cast<CBasePlayer*>(attacker)) == TM_WITHER)
			return true; // a wither's blasts (its own, another's) do nothing to a wither
		if (victim->pev->health < kWitherHealth * 0.5f)
		{
			// its armor, below half its health: arrows do nothing, bullets half
			if (inflictor && FClassnameIs(inflictor->pev, "mc_projectile"))
				return true;
			if (bits & DMG_BULLET)
				damage *= 0.5f;
		}
		if (m.breakAt <= 0.0f)
			m.breakAt = gpGlobals->time + 1.0f; // (the blocks around it go a second after it was hurt)
	}
	if (TeamMobOf(victim) == TM_GOLEM)
	{
		// nothing but a sword, and another mob
		CBasePlayer* by = (attacker && attacker->IsPlayer()) ? static_cast<CBasePlayer*>(attacker) : nullptr;
		bool sword = by && !IsMobBot(by) && (bits & DMG_SLASH) && HoldsSword(by);
		if (!sword && !(by && IsMobBot(by)))
			return true;
	}
	// its knife is only there to make the bot close in: the blow is the mob's own (DMG_CLUB)
	if (attacker && attacker->IsPlayer() && IsMobBot(static_cast<CBasePlayer*>(attacker)) && !(bits & (DMG_CLUB | DMG_BLAST)))
		return true;
	return false;
}

static void Objective(CBasePlayer* pl, TeamMob& m, CGrenade* bomb, float now, bool waitForFight);

// A mob in a hole. The ground a bot's own AI knows is the map's; blasts dig below it, and a mob cannot build
// its way out as a bot with blocks does: it would stand in a crater for the rest of the round (a wither in
// the one its own rising makes). A wither hangs in the air anyway: over a hole it keeps the height of the
// ground that was there and drifts on the way its AI wants to go. A golem heaves itself out with a leap,
// when it has stood in a hole for a second.
static void OutOfHoles(CBasePlayer* pl, TeamMob& m, float now)
{
	Vector up = pl->pev->origin + Vector(0, 0, 160);
	CNavArea* area = TheNavAreaGrid.GetNavArea(&up, 400.0f);
	if (!area)
	{
		m.hovering = false;
		m.holeSince = 0.0f;
		return;
	}
	float feet = pl->pev->origin.z + pl->pev->mins.z, below = area->GetZ(&pl->pev->origin) - feet;
	Vector wish = BotWish(pl);
	wish.z = 0.0f;
	Vector dir = wish.Length() > 10.0f ? wish.Normalize() : Vector(0, 0, 0);
	if (m.kind == TM_WITHER)
	{
		bool air = !(pl->pev->flags & FL_ONGROUND);
		if (below > 20.0f || (m.hovering && air && below > -10.0f))
		{
			m.hovering = true;
			float vz = (below + 6.0f) * 6.0f;
			vz = vz > 260.0f ? 260.0f : vz < -120.0f ? -120.0f : vz;
			pl->pev->velocity = Vector(dir.x * kWitherSpeed * 0.8f, dir.y * kWitherSpeed * 0.8f, vz);
			pl->pev->flags &= ~FL_ONGROUND;
		}
		else
			m.hovering = false;
		return;
	}
	if (below > 30.0f && (pl->pev->flags & FL_ONGROUND))
	{
		if (m.holeSince <= 0.0f)
			m.holeSince = now;
		else if (now - m.holeSince > 1.0f)
		{
			m.holeSince = 0.0f;
			pl->pev->velocity = Vector(dir.x * 170.0f, dir.y * 170.0f, sqrtf(2.0f * 800.0f * (below + 30.0f)));
			pl->pev->flags &= ~FL_ONGROUND;
			McLog("mob: %s heaves itself out of a hole %.0f deep", STRING(pl->pev->netname), below);
		}
	}
	else if (below <= 30.0f)
		m.holeSince = 0.0f;
}

static void SetSwell(CBasePlayer* pl, int v)
{
	pl->pev->iuser4 = (pl->pev->iuser4 & ~mcp::MCPF_SWELL_MASK) | ((v < 0 ? 0 : v > 30 ? 30 : v) << mcp::MCPF_SWELL_SHIFT);
}

static void SoundToAll(int sound)
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* h = UTIL_PlayerByIndex(i);
		if (h && !h->IsBot())
			FxSound(sound, h->pev->origin, 1.0f, 1.0f, 0, h->edict());
	}
}

static void WitherThink(CBasePlayer* pl, TeamMob& m, CGrenade* bomb, float now)
{
	CCSBot* ai = static_cast<CCSBot*>(pl);
	Vector org = pl->pev->origin;
	if (now < m.chargeUntil)
	{
		// rising: it stands where it was built, and fills with health
		float left = (m.chargeUntil - now) / kWitherCharge;
		pl->pev->flags |= FL_FROZEN;
		pl->pev->takedamage = DAMAGE_NO;
		pl->pev->velocity = g_vecZero;
		pl->pev->health = max(1.0f, kWitherHealth * (1.0f - left));
		SetSwell(pl, 1 + (int)(left * 29.0f));
		return;
	}
	if (!m.risen)
	{
		m.risen = true;
		pl->pev->flags &= ~FL_FROZEN;
		pl->pev->takedamage = DAMAGE_AIM;
		pl->pev->health = kWitherHealth;
		SetSwell(pl, 0);
		float o[3] = {org.x, org.y, org.z};
		Explode(o, 7.0f, nullptr, pl, true);
		SoundToAll(mcs::MCS_WITHER_SPAWN);
		McLog("mob: the wither is loose (side %d)", pl->m_iTeam);
		ai->Idle();
	}
	pl->m_flVelocityModifier = 1.0f;
	OutOfHoles(pl, m, now);
	if (now >= m.nextRegen)
	{
		m.nextRegen = now + 1.0f;
		if (pl->pev->health < kWitherHealth)
			pl->pev->health = min(kWitherHealth, pl->pev->health + mci::HP_PER_MC);
	}
	if (m.breakAt > 0.0f && now >= m.breakAt)
	{
		m.breakAt = 0.0f;
		int n = WitherBreaks(org, pl);
		if (n)
		{
			FxSound(mcs::MCS_WITHER_BREAK_BLOCK, org, 1.0f, 1.0f);
			McLog("mob: the wither breaks %d blocks around itself", n);
		}
	}
	if (now >= m.nextAmbient)
	{
		m.nextAmbient = now + RANDOM_FLOAT(5.0f, 9.0f);
		FxSound(mcs::MCS_WITHER_AMBIENT, org, 1.0f, 1.0f);
	}
	// its skulls: at whoever of the other side it sees; a blue one at a wall of blocks in its way
	if (now >= m.nextSkull)
	{
		Vector head = org + Vector(0, 0, 70);
		CBasePlayer* target = nullptr;
		float best = 1500.0f;
		for (int k = 1; k <= gpGlobals->maxClients; k++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(k);
			if (!p || p == pl || !p->IsAlive() || p->m_iTeam == pl->m_iTeam || (p->m_iTeam != CT && p->m_iTeam != TERRORIST))
				continue;
			float d = (p->pev->origin - org).Length();
			if (d >= best || !pl->FVisible(p))
				continue;
			best = d;
			target = p;
		}
		if (target)
		{
			m.nextSkull = now + 2.0f;
			Vector lead = target->pev->origin + target->pev->velocity * (best / 880.0f) * 0.5f;
			SpawnSkull(pl, head, (lead - head).Normalize(), false);
			FxSound(mcs::MCS_WITHER_SHOOT, org, 1.0f, 1.0f);
		}
		else
		{
			m.nextSkull = now + 0.5f;
			Vector wish = pl->pev->velocity;
			wish.z = 0.0f;
			UTIL_MakeVectors(Vector(0, pl->pev->v_angle.y, 0));
			Vector dir = wish.Length() > 20.0f ? wish.Normalize() : gpGlobals->v_forward;
			float s[3] = {org.x, org.y, org.z}, e[3] = {org.x + dir.x * 120.0f, org.y + dir.y * 120.0f, org.z};
			int c[3];
			float q[3] = {org.x + dir.x * 60.0f, org.y + dir.y * 60.0f, org.z};
			g_world.ToBlock(q, c);
			(void)s;
			(void)e;
			bool wall = g_worldLoaded && (IsPlacedBlock(c[0], c[1], c[2]) || IsPlacedBlock(c[0], c[1], c[2] - 1)) && pl->pev->velocity.Length2D() < 60.0f;
			if (wall)
			{
				m.nextSkull = now + 3.0f;
				SpawnSkull(pl, head, (CellCenter(c[0], c[1], c[2]) - head).Normalize(), true);
				FxSound(mcs::MCS_WITHER_SHOOT, org, 1.0f, 0.8f);
				McLog("mob: the wither fires a blue skull at the blocks in its way");
			}
		}
	}
	Objective(pl, m, bomb, now, false);
}

static void GolemThink(CBasePlayer* pl, TeamMob& m, CGrenade* bomb, float now)
{
	CCSBot* ai = static_cast<CCSBot*>(pl);
	Vector org = pl->pev->origin;
	pl->m_flVelocityModifier = 1.0f; // (a hit does not slow it)
	OutOfHoles(pl, m, now);
	// its fists
	if (now >= m.nextHit)
	{
		CBasePlayer* victim = nullptr;
		float best = kGolemReach;
		for (int k = 1; k <= gpGlobals->maxClients; k++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(k);
			if (!p || p == pl || !p->IsAlive() || p->m_iTeam == pl->m_iTeam || (p->m_iTeam != CT && p->m_iTeam != TERRORIST))
				continue;
			Vector d = p->pev->origin - org;
			if (fabsf(d.z) > 90.0f || d.Length2D() >= best || !pl->FVisible(p))
				continue;
			best = d.Length2D();
			victim = p;
		}
		if (victim)
		{
			m.nextHit = now + 1.0f;
			m.swingUntil = now + 0.5f;
			FxSound(mcs::MCS_IRON_GOLEM_ATTACK, org, 1.0f, 1.0f);
			Vector push = victim->pev->origin - org;
			push.z = 0.0f;
			push = push.Normalize();
			victim->TakeDamage(pl->pev, pl->pev, RANDOM_FLOAT(7.5f, 21.5f) * mci::HP_PER_MC, DMG_CLUB);
			// thrown into the air (Minecraft: 0.4 of a block a tick upward)
			victim->pev->velocity = victim->pev->velocity + push * 120.0f;
			victim->pev->velocity.z = 300.0f;
			victim->pev->flags &= ~FL_ONGROUND;
			McLog("mob: %s strikes %s (%.0f hp left)", STRING(pl->pev->netname), STRING(victim->pev->netname), victim->IsAlive() ? victim->pev->health : 0.0f);
			if (!victim->IsAlive())
				StoryTell(5.0f, "%s's iron golem killed %s", BuilderName(m.builder), STRING(victim->pev->netname));
		}
	}
	Objective(pl, m, bomb, now, true);
}

// Its place in the round, by its side and the state of the bomb
static void Objective(CBasePlayer* pl, TeamMob& m, CGrenade* bomb, float now, bool waitForFight)
{
	CCSBot* ai = static_cast<CCSBot*>(pl);
	Vector org = pl->pev->origin;
	if (now < m.nextGoal || (waitForFight && ai->IsAttacking()))
		return;
	m.nextGoal = now + 2.5f;
	Vector goal = org;
	float leash = 0.0f;
	if (bomb)
	{
		goal = bomb->pev->origin; // on the bomb: to guard it, or to clear the way to it
		leash = pl->m_iTeam == CT ? 220.0f : 420.0f;
	}
	else if (pl->m_iTeam == TERRORIST)
	{
		for (int k = 1; k <= gpGlobals->maxClients; k++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(k);
			if (p && p->IsAlive() && p->m_iTeam == TERRORIST && p->m_bHasC4)
			{
				goal = p->pev->origin; // with the bomb carrier
				leash = 320.0f;
			}
		}
	}
	else if (TheCSBots() && TheCSBots()->GetZoneCount() > 0)
	{
		int site = TacticsExpectedSite();
		const CCSBotManager::Zone* zone = TheCSBots()->GetZone(0);
		for (int z = 0; z < TheCSBots()->GetZoneCount(); z++)
			if (TheCSBots()->GetZone(z)->m_index == site)
				zone = TheCSBots()->GetZone(z);
		goal = zone->m_center; // the site the other side is expected at
		leash = 450.0f;
	}
	if (leash > 0.0f && (goal - org).Length2D() > leash)
		ai->MoveTo(&goal, FASTEST_ROUTE);
}

// An iron golem of the other side in sight: a bot with a sword goes in with it, one without keeps away (it
// does not shoot: BotIgnoresThreat) and says why.
static void BotsAndGolems(float now)
{
	for (int gi = 1; gi <= gpGlobals->maxClients; gi++)
	{
		CBasePlayer* golem = g_tm[gi].kind == TM_GOLEM ? UTIL_PlayerByIndex(gi) : nullptr;
		if (!golem || !golem->IsAlive())
			continue;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* b = UTIL_PlayerByIndex(i);
			if (!b || !b->IsBot() || !b->IsAlive() || IsMobBot(b) || b->m_iTeam == golem->m_iTeam || (b->m_iTeam != CT && b->m_iTeam != TERRORIST))
				continue;
			float d = (b->pev->origin - golem->pev->origin).Length();
			if (d > 520.0f || !b->FVisible(golem))
				continue;
			CCSBot* ai = static_cast<CCSBot*>(b);
			g_golemSeen[b->m_iTeam] = g_roundNo; // (next round its side thinks of swords: mc_botgear.cpp)
			if (BotHasSword(b))
			{
				if (ai->GetEnemy() != golem)
					ai->Attack(golem); // (mc_botgear.cpp draws the sword and runs in)
				continue;
			}
			if (d > 380.0f || (now < g_keepAway[i] && g_keepAway[i] < now + 5.0f))
				continue;
			g_keepAway[i] = now + 2.0f;
			if (!ai->TryToRetreat())
			{
				Vector away = b->pev->origin - golem->pev->origin;
				away.z = 0.0f;
				Vector goal = b->pev->origin + away.Normalize() * 500.0f;
				ai->MoveTo(&goal, FASTEST_ROUTE);
			}
			if (now - g_toldBounce[i] > 20.0f || g_toldBounce[i] > now)
			{
				g_toldBounce[i] = now;
				McLog("mob: %s has no sword for the iron golem and keeps away from it", STRING(b->pev->netname));
				for (int h = 1; h <= gpGlobals->maxClients; h++)
				{
					CBasePlayer* hp = UTIL_PlayerByIndex(h);
					if (hp && !hp->IsBot() && hp->m_iTeam == b->m_iTeam)
						Toast(hp, 0, "<%s> %s", STRING(b->pev->netname), RANDOM_LONG(0, 1) ? "Golem! Bullets bounce off it. Keep away." : "Iron golem here. Swords only.");
				}
			}
		}
	}
}

bool BotIgnoresThreat(CBasePlayer* bot, CBasePlayer* other)
{
	return bot && other && TeamMobOf(other) == TM_GOLEM && other->m_iTeam != bot->m_iTeam && !BotHasSword(bot);
}

void TeamMobFrame()
{
	if (!g_bounces.empty())
	{
		std::vector<Bounce> bs;
		bs.swap(g_bounces);
		for (const Bounce& b : bs)
		{
			CBasePlayer* shooter = UTIL_PlayerByIndex(b.shooter);
			CBasePlayer* golem = UTIL_PlayerByIndex(b.golem);
			if (!shooter || !golem || !shooter->IsAlive())
				continue;
			shooter->TakeDamage(golem->pev, golem->pev, b.damage, DMG_BULLET | DMG_NEVERGIB | DMG_CLUB); // (DMG_CLUB: the mob's own doing, see TeamMobDamage)
			float now = gpGlobals->time;
			if (!shooter->IsBot() && (now - g_toldBounce[b.shooter] > 8.0f || g_toldBounce[b.shooter] > now))
			{
				g_toldBounce[b.shooter] = now;
				Toast(shooter, 1, "Bullets bounce off an iron golem. Only a sword hurts it.");
			}
			if (!shooter->IsAlive())
				StoryTell(4.0f, "%s shot at the iron golem and died of the bullet that came back", STRING(shooter->pev->netname));
		}
	}
	{
		static float nextBots = 0.0f;
		float now = gpGlobals->time;
		if (now >= nextBots || nextBots > now + 2.0f)
		{
			nextBots = now + 0.25f;
			BotsAndGolems(now);
		}
	}
	if (!g_requests.empty())
	{
		std::vector<Request> rs;
		rs.swap(g_requests);
		for (const Request& r : rs)
		{
			CBasePlayer* m = Create(r);
			const char* what = r.kind == TM_WITHER ? "a wither" : "an iron golem";
			McLog("mob: %s built %s for side %d at (%.0f %.0f %.0f): %s", BuilderName(r.builder), what, r.team, r.feet.x, r.feet.y, r.feet.z,
				m ? "it stands" : "it did not come to life");
			if (m)
				StoryTell(r.kind == TM_WITHER ? 7.0f : 5.0f, "%s raised %s", BuilderName(r.builder), what);
			if (m && r.kind == TM_WITHER)
			{
				// everybody is told: eleven seconds to get ready for it, or away from it
				ToastAll(0, "A wither is rising for the %s!", r.team == CT ? "Counter-Terrorists" : "Terrorists");
				SoundToAll(mcs::MCS_WITHER_AMBIENT);
			}
		}
	}
	static float next = 0.0f;
	float now = gpGlobals->time;
	if (now < next && next < now + 1.0f)
		return;
	next = now + 0.05f;
	CGrenade* bomb = PlantedBombEnt();
	// the Wither effect: a point of health (5 here) a second, and it can kill
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		if (g_withered[i].until <= 0.0f)
			continue;
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (!p || !p->IsAlive() || now >= g_withered[i].until)
		{
			g_withered[i].until = 0.0f;
			continue;
		}
		if (now < g_withered[i].next)
			continue;
		g_withered[i].next = now + 1.0f;
		CBasePlayer* by = UTIL_PlayerByIndex(g_withered[i].by);
		entvars_t* from = (by && IsMobBot(by)) ? by->pev : VARS(INDEXENT(0));
		p->TakeDamage(from, from, mci::HP_PER_MC, DMG_CLUB);
	}
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		if (g_tm[i].kind == TM_NONE)
			continue;
		CBasePlayer* pl = UTIL_PlayerByIndex(i);
		if (!pl || !pl->IsBot())
		{
			g_tm[i] = TeamMob();
			continue;
		}
		if (pl->IsAlive() && g_tm[i].kind == TM_GOLEM)
			GolemThink(pl, g_tm[i], bomb, now);
		if (pl->IsAlive() && g_tm[i].kind == TM_WITHER)
			WitherThink(pl, g_tm[i], bomb, now);
		// (its arms: up for half a second after a blow)
		int swing = (pl->IsAlive() && now < g_tm[i].swingUntil) ? mcp::MCPF_MOB_SWING : 0;
		if ((pl->pev->iuser4 & mcp::MCPF_MOB_SWING) != swing)
			pl->pev->iuser4 = (pl->pev->iuser4 & ~mcp::MCPF_MOB_SWING) | swing;
	}
}

// Is its arm up for a blow just now? (for the model's pose)
bool TeamMobSwings(CBasePlayer* pl) { return IsMobBot(pl) && gpGlobals->time < g_tm[pl->entindex()].swingUntil; }

void TeamMobDisconnect(int index)
{
	if (index >= 1 && index <= MAX_CLIENTS)
		g_tm[index] = TeamMob();
}

// The round is over: they go
void TeamMobsRemove()
{
	g_requests.clear();
	for (auto& w : g_withered)
		w.until = 0.0f;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
		if (g_tm[i].kind != TM_NONE)
			Kick(UTIL_PlayerByIndex(i));
}

static BOOL H_CanRespawn(IReGameHook_CSGameRules_FPlayerCanRespawn* chain, CBasePlayer* pl)
{
	if (pl && g_spawning && pl->entindex() == g_spawning)
		return TRUE; // a mob comes to life where and when it is built
	return chain->callNext(pl);
}
static bool H_MakeBomber(IReGameHook_CBasePlayer_MakeBomber* chain, CBasePlayer* pl)
{
	if (IsMobBot(pl))
		return false;
	return chain->callNext(pl);
}
static void H_DefuseStart(IReGameHook_CGrenade_DefuseBombStart* chain, CGrenade* bomb, CBasePlayer* pl)
{
	if (IsMobBot(pl))
		return;
	chain->callNext(bomb, pl);
}
static bool H_MobRoundEnd(IReGameHook_RoundEnd* chain, int winStatus, ScenarioEventEndRound event, float tmDelay)
{
	bool ok = chain->callNext(winStatus, event, tmDelay);
	if (ok)
	{
		TeamMobsRemove();
		g_roundNo++;
	}
	return ok;
}

void TeamMobsInit()
{
	CVAR_REGISTER(&cv_golemMax);
	CVAR_REGISTER(&cv_witherRounds);
	g_ReGameHookchains.m_CSGameRules_FPlayerCanRespawn.registerHook(&H_CanRespawn, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CBasePlayer_MakeBomber.registerHook(&H_MakeBomber, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_CGrenade_DefuseBombStart.registerHook(&H_DefuseStart, HC_PRIORITY_DEFAULT);
	g_ReGameHookchains.m_RoundEnd.registerHook(&H_MobRoundEnd, HC_PRIORITY_DEFAULT);
}
} // namespace mc
