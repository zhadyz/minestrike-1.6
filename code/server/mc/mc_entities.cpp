// Minecraft entities living on the server: XP orbs, dropped items, projectiles, fireworks, primed TNT,
// falling blocks. They run Minecraft's per-tick physics (20 Hz think) with voxel-aware traces and are
// drawn by the client's Minecraft renderer (the engine model is an invisible carrier sprite).
#include "precompiled.h"

#include "mc_server.h"
#include "mc_blocks.h"

namespace mc
{
static const float TICK = 0.05f;
static const float B2U = 40.0f; // blocks -> units

class CMcEntity : public CBaseEntity
{
public:
	Vector m_vel;          // Minecraft velocity in blocks/tick
	int m_age = 0;         // ticks
	bool m_onGround = false;
	float m_halfWidth = 0.125f * B2U;
	float m_height = 0.25f * B2U;

	void SpawnCarrier(int kind, int data)
	{
		pev->solid = SOLID_NOT;
		pev->movetype = MOVETYPE_NONE;
		SET_MODEL(edict(), "sprites/ledglow.spr");
		pev->rendermode = kRenderTransAdd;
		pev->renderamt = 0;
		pev->iuser4 = mcp::MakeEntMarker(kind, data);
		UTIL_SetSize(pev, Vector(0, 0, 0), Vector(0, 0, 0));
		pev->nextthink = gpGlobals->time + TICK;
	}
	void SetMarkerData(int kind, int data) { pev->iuser4 = mcp::MakeEntMarker(kind, data); }

	// Move by m_vel (one tick) with collision against world + voxels. Returns hit fraction (<1 if hit).
	float MoveTick(Vector* hitNormal, edict_t** hitEnt, bool hitEntities)
	{
		if (!isfinite(m_vel.x) || !isfinite(m_vel.y) || !isfinite(m_vel.z))
		{
			McLog("entity %s had a non-finite velocity; stopped it", STRING(pev->classname));
			m_vel = g_vecZero;
		}
		Vector start = pev->origin;
		Vector delta = m_vel * B2U;
		Vector end = start + delta;
		TraceResult tr;
		UTIL_TraceLine(start, end, hitEntities ? dont_ignore_monsters : ignore_monsters, pev->owner, &tr);
		if (tr.fStartSolid && tr.fAllSolid)
		{
			// stuck inside something: nudge up
			UTIL_SetOrigin(pev, start + Vector(0, 0, 4));
			return 0.0f;
		}
		UTIL_SetOrigin(pev, tr.vecEndPos);
		if (hitNormal)
			*hitNormal = tr.vecPlaneNormal;
		if (hitEnt)
			*hitEnt = tr.pHit;
		return tr.flFraction;
	}

	// Simple Minecraft "Entity.move" for resting/sliding things: collide, bounce/stop on ground.
	void PhysicsTick(float gravity, float drag, float groundFriction, float bounce)
	{
		m_vel.z -= gravity;
		Vector n;
		float f = MoveTick(&n, nullptr, false);
		m_onGround = false;
		if (f < 1.0f)
		{
			if (n.z > 0.7f)
			{
				m_onGround = true;
				m_vel.z = (bounce > 0.0f && m_vel.z < -0.1f) ? -m_vel.z * bounce : 0.0f;
			}
			else if (n.z < -0.7f)
				m_vel.z = 0.0f;
			else
			{
				// slide along walls
				float d = DotProduct(m_vel, n);
				m_vel = m_vel - n * d;
			}
		}
		else
		{
			// check if we're resting on something just below
			TraceResult tr;
			UTIL_TraceLine(pev->origin, pev->origin - Vector(0, 0, 1.0f), ignore_monsters, nullptr, &tr);
			m_onGround = tr.flFraction < 1.0f;
		}
		float h = m_onGround ? groundFriction * drag : drag;
		m_vel.x *= h;
		m_vel.y *= h;
		m_vel.z *= 0.98f;
	}

	CBasePlayer* NearestPlayer(float rangeUnits, float* outDist)
	{
		CBasePlayer* best = nullptr;
		float bestD = rangeUnits;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(i);
			if (!p || !p->IsAlive() || p->pev->iuser1)
				continue;
			float d = (p->pev->origin - pev->origin).Length();
			if (d < bestD)
			{
				bestD = d;
				best = p;
			}
		}
		if (outDist)
			*outDist = bestD;
		return best;
	}

	// player AABB inflated by (1, 0.5, 1) blocks touches us?
	static bool TouchesPlayer(CBasePlayer* p, const Vector& o)
	{
		Vector mins = p->pev->absmin - Vector(B2U * 0.5f, B2U * 0.5f, B2U * 0.25f);
		Vector maxs = p->pev->absmax + Vector(B2U * 0.5f, B2U * 0.5f, B2U * 0.25f);
		return o.x >= mins.x && o.x <= maxs.x && o.y >= mins.y && o.y <= maxs.y && o.z >= mins.z && o.z <= maxs.z;
	}
};

// ---------------------------------------------------------------------------------------------
// Experience orb (ExperienceOrb.tick / playerTouch)

static float g_xpPickupDelay[MAX_CLIENTS + 1];

class CMcXpOrb : public CMcEntity
{
public:
	int m_value = 1;
	void Spawn() override
	{
		pev->classname = MAKE_STRING("mc_xp_orb");
		SpawnCarrier(mcp::MCE_XPORB, 0);
	}
	void SetValue(int v)
	{
		m_value = v;
		// texture icon index by value (ExperienceOrb.getIcon)
		int icon = v >= 2477 ? 10 : v >= 1237 ? 9 : v >= 617 ? 8 : v >= 307 ? 7 : v >= 149 ? 6 : v >= 73 ? 5 : v >= 37 ? 4 : v >= 17 ? 3 : v >= 7 ? 2 : v >= 3 ? 1 : 0;
		SetMarkerData(mcp::MCE_XPORB, icon);
	}
	void Think() override
	{
		pev->nextthink = gpGlobals->time + TICK;
		if (++m_age > 6000)
		{
			UTIL_Remove(this);
			return;
		}
		m_vel.z -= 0.03f;
		float dist;
		CBasePlayer* p = NearestPlayer(8.0f * B2U, &dist);
		if (p)
		{
			Vector target = p->pev->origin + Vector(0, 0, p->pev->view_ofs.z * 0.5f);
			Vector d = (target - pev->origin) / (8.0f * B2U);
			float len2 = d.LengthSquared();
			if (len2 < 1.0f)
			{
				float e = 1.0f - sqrtf(len2);
				Vector n = d.Normalize();
				m_vel = m_vel + n * (e * e * 0.1f);
			}
		}
		Vector n;
		float f = MoveTick(&n, nullptr, false);
		m_onGround = false;
		if (f < 1.0f)
		{
			if (n.z > 0.7f)
			{
				m_onGround = true;
				m_vel.z *= -0.9f;
			}
			else
				m_vel = m_vel - n * DotProduct(m_vel, n);
		}
		float fr = m_onGround ? 0.6f * 0.98f : 0.98f;
		m_vel.x *= fr;
		m_vel.y *= fr;
		m_vel.z *= 0.98f;

		if (p && TouchesPlayer(p, pev->origin) && gpGlobals->time >= g_xpPickupDelay[p->entindex()] && m_age > 2)
		{
			g_xpPickupDelay[p->entindex()] = gpGlobals->time + 2 * TICK;
			float pitch = 0.5f * ((RANDOM_FLOAT(0.0f, 1.0f) - RANDOM_FLOAT(0.0f, 1.0f)) * 0.7f + 1.8f);
			FxSound(mcs::MCS_XP_ORB, p->pev->origin, 0.3f, pitch, p->entindex());
			GiveXp(p, m_value);
			UTIL_Remove(this);
		}
	}
};
LINK_ENTITY_TO_CLASS(mc_xp_orb, CMcXpOrb, CCSEntity)

void DropXp(const float* origin, int amount)
{
	Vector org(origin[0], origin[1], origin[2]);
	while (amount > 0)
	{
		int v = mci::XpOrbValue(amount);
		amount -= v;
		CMcXpOrb* orb = GetClassPtr<CCSEntity>((CMcXpOrb*)nullptr);
		orb->Spawn();
		UTIL_SetOrigin(orb->pev, org);
		orb->SetValue(v);
		// ExperienceOrb constructor: random velocity
		orb->m_vel = Vector((RANDOM_FLOAT(0, 1) * 0.2f - 0.1f) * 2.0f, (RANDOM_FLOAT(0, 1) * 0.2f - 0.1f) * 2.0f,
			RANDOM_FLOAT(0, 1) * 0.2f * 2.0f);
	}
}

// ---------------------------------------------------------------------------------------------
// Dropped item (ItemEntity)

class CMcItemDrop : public CMcEntity
{
public:
	int m_item = 0;
	int m_count = 1;
	int m_pickupDelay = 10;
	int m_damage = 0;
	int m_ench = 0;
	void Spawn() override
	{
		pev->classname = MAKE_STRING("mc_item");
		SpawnCarrier(mcp::MCE_ITEM, 0);
	}
	void Think() override
	{
		pev->nextthink = gpGlobals->time + TICK;
		if (++m_age > 6000)
		{
			UTIL_Remove(this);
			return;
		}
		if (m_pickupDelay > 0)
			m_pickupDelay--;
		PhysicsTick(0.04f, 0.98f, 0.6f, 0.0f);
		if (m_pickupDelay > 0)
			return;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(i);
			if (!p || !p->IsAlive() || p->IsBot() || !TouchesPlayer(p, pev->origin))
				continue;
			// pick up as much as fits; the rest stays on the ground (Minecraft)
			int take = min(m_count, RoomFor(p, m_item, m_damage));
			if (take > 0 && take < m_count && GiveItem(p, m_item, take, false, true, m_damage, m_ench))
			{
				m_count -= take;
				float pitch = ((RANDOM_FLOAT(0, 1) - RANDOM_FLOAT(0, 1)) * 0.7f + 1.0f) * 2.0f;
				FxSound(mcs::MCS_ITEM_PICKUP, p->pev->origin, 0.4f, pitch, p->entindex());
				return;
			}
			if (take == m_count && GiveItem(p, m_item, m_count, false, true, m_damage, m_ench))
			{
				float pitch = ((RANDOM_FLOAT(0, 1) - RANDOM_FLOAT(0, 1)) * 0.7f + 1.0f) * 2.0f;
				FxSound(mcs::MCS_ITEM_PICKUP, p->pev->origin, 0.4f, pitch, p->entindex());
				UTIL_Remove(this);
				return;
			}
		}
	}
};
LINK_ENTITY_TO_CLASS(mc_item, CMcItemDrop, CCSEntity)

CBaseEntity* SpawnItemEntity(const float* origin, int itemId, int count, const float* velocity)
{
	if (itemId <= 0)
		return nullptr;
	CMcItemDrop* it = GetClassPtr<CCSEntity>((CMcItemDrop*)nullptr);
	it->Spawn();
	UTIL_SetOrigin(it->pev, Vector(origin[0], origin[1], origin[2]));
	it->m_item = itemId;
	it->m_count = count;
	it->pev->skin = count;
	it->SetMarkerData(mcp::MCE_ITEM, itemId);
	if (velocity)
	{
		it->m_vel = Vector(velocity[0], velocity[1], velocity[2]) / (B2U * 20.0f);
		it->m_pickupDelay = 40;
	}
	else
		it->m_vel = Vector(RANDOM_FLOAT(-0.1f, 0.1f), RANDOM_FLOAT(-0.1f, 0.1f), 0.2f);
	return it;
}

// ---------------------------------------------------------------------------------------------
// Thrown projectiles: ender pearl, bottle o' enchanting, arrow

class CMcThrown : public CMcEntity
{
public:
	int m_kind = mcp::MCE_PEARL;
	float m_gravity = 0.03f;
	bool m_stuck = false;
	int m_stuckTicks = 0;
	void Spawn() override
	{
		pev->classname = MAKE_STRING("mc_projectile");
		SpawnCarrier(m_kind, 0);
	}
	void Think() override
	{
		pev->nextthink = gpGlobals->time + TICK;
		m_age++;
		if (m_stuck)
		{
			if (++m_stuckTicks > 1200)
				UTIL_Remove(this);
			return;
		}
		if (m_age > 400 || pev->origin.z < -8192)
		{
			UTIL_Remove(this);
			return;
		}
		Vector n;
		edict_t* hit = nullptr;
		float f = MoveTick(&n, &hit, true);
		// orientation for rendering (arrows point along velocity)
		Vector ang = UTIL_VecToAngles(m_vel);
		pev->angles = ang;
		if (f < 1.0f)
		{
			OnImpact(hit, n);
			return;
		}
		m_vel = m_vel * 0.99f;
		m_vel.z -= m_gravity;
	}

	void OnImpact(edict_t* hit, const Vector& normal)
	{
		CBaseEntity* owner = pev->owner ? CBaseEntity::Instance(pev->owner) : nullptr;
		CBaseEntity* victim = (hit && !FNullEnt(hit) && hit != ENT(0)) ? CBaseEntity::Instance(hit) : nullptr;
		if (m_kind == mcp::MCE_SKULL)
		{
			CBasePlayer* by = (owner && owner->IsPlayer()) ? static_cast<CBasePlayer*>(owner) : nullptr;
			if (victim && victim->IsPlayer() && victim != owner && by && static_cast<CBasePlayer*>(victim)->m_iTeam != by->m_iTeam)
			{
				victim->TakeDamage(by->pev, by->pev, 8.0f * mci::HP_PER_MC, DMG_CLUB);
				WitherEffect(static_cast<CBasePlayer*>(victim), by);
			}
			float o[3] = {pev->origin.x, pev->origin.y, pev->origin.z};
			if (pev->fuser1 > 0.5f)
				ExplodeThroughAll(o, 1.0f, by);
			else
				Explode(o, 1.0f, nullptr, by, true);
			UTIL_Remove(this);
			return;
		}
		switch (m_kind)
		{
		case mcp::MCE_PEARL:
		{
			FxParticles(mcp::PK_POOF, pev->origin, 24);
			if (owner && owner->IsPlayer() && owner->IsAlive())
			{
				// ThrownEnderpearl.onHit: teleport the thrower to the impact point (find a spot the hull fits)
				Vector base = pev->origin + normal * 20.0f;
				static const Vector tries[] = {Vector(0, 0, 0), Vector(0, 0, -36), Vector(0, 0, 36), Vector(0, 0, -60), Vector(20, 0, 0),
					Vector(-20, 0, 0), Vector(0, 20, 0), Vector(0, -20, 0), Vector(0, 0, -90)};
				for (const Vector& t : tries)
				{
					Vector dest = base + normal * 16.0f + t;
					TraceResult tr;
					UTIL_TraceHull(dest, dest, ignore_monsters, human_hull, owner->edict(), &tr);
					if (!tr.fStartSolid && !tr.fAllSolid)
					{
						UTIL_SetOrigin(owner->pev, dest);
						owner->pev->velocity = g_vecZero;
						owner->TakeDamage(pev, owner->pev, 5.0f * mci::HP_PER_MC, DMG_FALL);
						break;
					}
				}
			}
			UTIL_Remove(this);
			break;
		}
		case mcp::MCE_XPBOTTLE:
		{
			FxParticles(mcp::PK_XP_SPLASH, pev->origin, 30);
			FxSound(mcs::MCS_ITEM_BREAK, pev->origin, 0.8f, 0.9f + RANDOM_FLOAT(0.0f, 0.1f));
			DropXp(pev->origin + Vector(0, 0, 8), 3 + RANDOM_LONG(0, 4) + RANDOM_LONG(0, 4));
			UTIL_Remove(this);
			break;
		}
		case mcp::MCE_ARROW:
		{
			float speed = m_vel.Length();
			if (victim && victim->pev->takedamage != DAMAGE_NO && victim != owner)
			{
				// AbstractArrow.onHitEntity: ceil(speed * baseDamage(2)), crit adds rand(dmg/2+2)
				float base = 2.0f + (pev->fuser2 > 0.0f ? 0.5f * pev->fuser2 + 0.5f : 0.0f); // Power
				int dmg = (int)ceilf(speed * base);
				if (pev->fuser1 > 0.0f)
					dmg += RANDOM_LONG(0, dmg / 2 + 1);
				Vector dir = m_vel.Normalize();
				TraceResult tr;
				UTIL_TraceLine(pev->origin - dir * 8.0f, pev->origin + dir * 16.0f, dont_ignore_monsters, nullptr, &tr);
				ClearMultiDamage();
				victim->TraceAttack(owner ? owner->pev : pev, dmg * mci::HP_PER_MC * McWeaponScale(), dir, &tr, DMG_BULLET | DMG_NEVERGIB);
				ApplyMultiDamage(pev, owner ? owner->pev : pev);
				if (pev->fuser3 > 0.0f)
					EnchantIgnite(victim, (owner && owner->IsPlayer()) ? (CBasePlayer*)owner : nullptr, 5.0f); // Flame
				if (pev->fuser4 > 0.0f && victim->IsPlayer())
					victim->pev->velocity = victim->pev->velocity + Vector(dir.x, dir.y, 0) * (220.0f * pev->fuser4) + Vector(0, 0, 60); // Punch
				FxSound(mcs::MCS_ARROW_HIT, pev->origin, 1.0f, 1.2f / (RANDOM_FLOAT(0.0f, 0.2f) + 0.9f));
				if (owner && owner->IsPlayer() && victim->IsPlayer())
					FxSound(mcs::MCS_ARROW_HIT_PLAYER, owner->pev->origin, 0.18f, 0.45f, owner->entindex(), owner->edict());
				UTIL_Remove(this);
			}
			else
			{
				FxSound(mcs::MCS_ARROW_HIT, pev->origin, 1.0f, 1.2f / (RANDOM_FLOAT(0.0f, 0.2f) + 0.9f));
				m_stuck = true;
				m_vel = g_vecZero;
			}
			break;
		}
		default:
			UTIL_Remove(this);
		}
	}
};
LINK_ENTITY_TO_CLASS(mc_projectile, CMcThrown, CCSEntity)

// A wither's skull: straight from its head at whoever it has picked out. Minecraft's numbers: 8 damage to
// whoever it hits (40 here) and the Wither effect on him, and a blast of power 1 where it lands, which takes
// what is as soft as planks. A blue one flies slower, and its blast takes every block but bedrock.
CBaseEntity* SpawnSkull(CBasePlayer* owner, const Vector& from, const Vector& dir, bool blue)
{
	CMcThrown* t = GetClassPtr<CCSEntity>((CMcThrown*)nullptr);
	t->m_kind = mcp::MCE_SKULL;
	t->m_gravity = 0.0f;
	t->Spawn();
	t->SetMarkerData(mcp::MCE_SKULL, blue ? 1 : 0);
	UTIL_SetOrigin(t->pev, from);
	t->pev->owner = owner ? owner->edict() : nullptr;
	t->pev->fuser1 = blue ? 1.0f : 0.0f;
	t->m_vel = dir * (blue ? 0.6f : 1.1f);
	return t;
}

CBaseEntity* SpawnThrown(CBasePlayer* pl, int kind, float speed)
{
	CMcThrown* t = GetClassPtr<CCSEntity>((CMcThrown*)nullptr);
	t->m_kind = kind;
	t->m_gravity = kind == mcp::MCE_ARROW ? 0.05f : 0.03f;
	t->Spawn();
	UTIL_MakeVectors(pl->pev->v_angle);
	Vector org = pl->GetGunPosition() + gpGlobals->v_forward * 8.0f - Vector(0, 0, 4);
	UTIL_SetOrigin(t->pev, org);
	t->pev->owner = pl->edict();
	// shootFromRotation: velocity = look * speed (+ shooter's own motion)
	Vector plv = pl->pev->velocity / (B2U * 20.0f);
	t->m_vel = gpGlobals->v_forward * speed + Vector(plv.x, plv.y, (pl->pev->flags & FL_ONGROUND) ? 0.0f : plv.z);
	if (kind != mcp::MCE_ARROW)
		FxSound(mcs::MCS_ARROW_SHOOT, pl->pev->origin, 0.5f, 0.4f / (RANDOM_FLOAT(0.0f, 0.4f) + 0.8f));
	return t;
}

// ---------------------------------------------------------------------------------------------
// Firework rocket launched from the ground (FireworkRocketEntity)

class CMcFirework : public CMcEntity
{
public:
	int m_life = 30;
	int m_color = 0xFF4040;
	void Spawn() override
	{
		pev->classname = MAKE_STRING("mc_firework");
		SpawnCarrier(mcp::MCE_FIREWORK, 0);
	}
	void Think() override
	{
		pev->nextthink = gpGlobals->time + TICK;
		m_vel.x *= 1.15f;
		m_vel.y *= 1.15f;
		m_vel.z += 0.04f;
		Vector n;
		MoveTick(&n, nullptr, false);
		if (++m_age >= m_life)
		{
			FxFirework(pev->origin, RANDOM_LONG(0, 4), m_color);
			FxSound(RANDOM_LONG(0, 3) == 0 ? mcs::MCS_FIREWORK_LARGE_BLAST : mcs::MCS_FIREWORK_BLAST, pev->origin, 4.0f, 0.95f + RANDOM_FLOAT(0.0f, 0.1f));
			if (RANDOM_LONG(0, 1))
				FxSound(mcs::MCS_FIREWORK_TWINKLE, pev->origin, 2.0f, 0.9f + RANDOM_FLOAT(0.0f, 0.15f));
			UTIL_Remove(this);
		}
	}
};
LINK_ENTITY_TO_CLASS(mc_firework, CMcFirework, CCSEntity)

void LaunchFirework(CBasePlayer* pl)
{
	CMcFirework* f = GetClassPtr<CCSEntity>((CMcFirework*)nullptr);
	f->Spawn();
	UTIL_MakeVectors(pl->pev->v_angle);
	Vector org = pl->GetGunPosition() + gpGlobals->v_forward * 24.0f;
	UTIL_SetOrigin(f->pev, org);
	// random sideways jitter like Minecraft: x/z gaussian * 0.001, y 0.05
	f->m_vel = Vector(RANDOM_FLOAT(-0.002f, 0.002f), RANDOM_FLOAT(-0.002f, 0.002f), 0.05f);
	f->m_life = 10 * 2 + RANDOM_LONG(0, 5) + RANDOM_LONG(0, 6);
	static const int colors[] = {0xFF3030, 0x30FF60, 0x3080FF, 0xFFD020, 0xFF40FF, 0x40FFFF, 0xFFFFFF, 0xFF8020};
	f->m_color = colors[RANDOM_LONG(0, 7)];
	FxSound(mcs::MCS_FIREWORK_LAUNCH, org, 3.0f, 1.0f);
}

// ---------------------------------------------------------------------------------------------
// Primed TNT (PrimedTnt)

class CMcTnt : public CMcEntity
{
public:
	int m_fuse = 80;
	CBaseEntity* m_source = nullptr;
	int m_owner = 0; // player index of who lit it (0: nobody: a chain reaction, fire, redstone)
	void Spawn() override
	{
		pev->classname = MAKE_STRING("mc_tnt");
		SpawnCarrier(mcp::MCE_TNT, 80);
	}
	void Think() override
	{
		pev->nextthink = gpGlobals->time + TICK;
		PhysicsTick(0.04f, 0.98f, 0.7f, 0.0f);
		SetMarkerData(mcp::MCE_TNT, m_fuse);
		if (--m_fuse <= 0)
		{
			Vector o = pev->origin + Vector(0, 0, 0.0625f * B2U);
			// the blast is the lighter's, like a grenade is its thrower's (kills count, mp_friendlyfire applies)
			CBasePlayer* by = m_owner ? UTIL_PlayerByIndex(m_owner) : nullptr;
			UTIL_Remove(this);
			Explode(o, 4.0f, nullptr, by, true);
		}
	}
};
LINK_ENTITY_TO_CLASS(mc_tnt, CMcTnt, CCSEntity)

void PrimeTntBy(const float* origin, int fuse, CBasePlayer* by)
{
	CMcTnt* t = GetClassPtr<CCSEntity>((CMcTnt*)nullptr);
	t->Spawn();
	UTIL_SetOrigin(t->pev, Vector(origin[0], origin[1], origin[2]));
	t->m_fuse = fuse;
	t->m_owner = by ? by->entindex() : 0;
	float a = RANDOM_FLOAT(0.0f, 6.2831853f);
	t->m_vel = Vector(-sinf(a) * 0.02f, cosf(a) * 0.02f, 0.2f);
	FxSound(mcs::MCS_TNT_PRIMED, t->pev->origin, 1.0f, 1.0f);
}

void PrimeTnt(const float* origin, int fuse) { PrimeTntBy(origin, fuse, nullptr); }

// ---------------------------------------------------------------------------------------------
// Falling block (sand/gravel with nothing below)

class CMcFalling : public CMcEntity
{
public:
	mcw::Cell m_cell = 0;
	void Spawn() override
	{
		pev->classname = MAKE_STRING("mc_falling_block");
		SpawnCarrier(mcp::MCE_FALLING, 0);
	}
	void Think() override
	{
		pev->nextthink = gpGlobals->time + TICK;
		m_vel.z -= 0.04f;
		Vector n;
		float f = MoveTick(&n, nullptr, false);
		m_vel = m_vel * 0.98f;
		if ((f < 1.0f && n.z > 0.7f) || ++m_age > 600)
		{
			if (g_worldLoaded)
			{
				float p[3] = {pev->origin.x, pev->origin.y, pev->origin.z + 2.0f};
				int b[3];
				g_world.ToBlock(p, b);
				if (g_world.Get(b[0], b[1], b[2]) != 0)
					b[2]++;
				if (BombKeepsFree(b[0], b[1], b[2]))
				{
					// nothing comes to rest against a planted bomb: it lies there as an item
					int item = mci::FindItem(mcw::Block(mcw::CellType(m_cell)).name);
					if (item > 0)
						SpawnItemEntity(pev->origin, item, 1, nullptr);
				}
				else if (g_world.Get(b[0], b[1], b[2]) == 0)
					SetBlock(b[0], b[1], b[2], m_cell);
			}
			UTIL_Remove(this);
		}
	}
};
LINK_ENTITY_TO_CLASS(mc_falling_block, CMcFalling, CCSEntity)

void SpawnFallingBlock(int x, int y, int z, mcw::Cell cell)
{
	CMcFalling* f = GetClassPtr<CCSEntity>((CMcFalling*)nullptr);
	f->Spawn();
	f->m_cell = cell;
	f->SetMarkerData(mcp::MCE_FALLING, cell);
	Vector o(g_world.origin[0] + (x + 0.5f) * B2U, g_world.origin[1] + (y + 0.5f) * B2U, g_world.origin[2] + z * B2U + 1.0f);
	UTIL_SetOrigin(f->pev, o);
	f->m_vel = g_vecZero;
}
} // namespace mc

namespace mc
{
void SetItemDamage(CBaseEntity* item, int damage)
{
	if (item && FClassnameIs(item->pev, "mc_item"))
		static_cast<CMcItemDrop*>(item)->m_damage = damage;
}
void SetItemEnchant(CBaseEntity* item, int ench)
{
	if (item && FClassnameIs(item->pev, "mc_item"))
		static_cast<CMcItemDrop*>(item)->m_ench = ench;
}
} // namespace mc
