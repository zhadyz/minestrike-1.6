// weapon_mcitem: the single CS "weapon" that represents whatever Minecraft item (or the bare hand)
// is in the selected hotbar slot. Left click = attack/mine, right click = use/place.
#include "precompiled.h"

#include "mc_server.h"
#include "mc_blocks.h"

namespace mc
{
extern void MineFrame(CBasePlayer* pl, bool holding);
extern bool UseBlockTarget(CBasePlayer* pl, const mci::Stack& held);
extern CBaseEntity* SpawnThrown(CBasePlayer* pl, int kind, float speed);
extern void LaunchFirework(CBasePlayer* pl);
void UpdatePlayerFlagsPublic(CBasePlayer* pl);
} // namespace mc

class CMcItem : public CBasePlayerWeapon
{
public:
	void Spawn() override
	{
		Precache();
		m_iId = WEAPON_GLOCK;
		SET_MODEL(edict(), "models/w_knife.mdl");
		m_iClip = WEAPON_NOCLIP;
		FallInit();
		CBasePlayerWeapon::Spawn();
	}
	void Precache() override { PRECACHE_MODEL("models/w_knife.mdl"); }
	int GetItemInfo(ItemInfo* p) override
	{
		p->pszName = STRING(pev->classname);
		p->pszAmmo1 = nullptr;
		p->iMaxAmmo1 = -1;
		p->pszAmmo2 = nullptr;
		p->iMaxAmmo2 = -1;
		p->iMaxClip = WEAPON_NOCLIP;
		p->iSlot = 2;
		p->iPosition = 2;
		p->iId = WEAPON_GLOCK;
		p->iFlags = 0;
		p->iWeight = -1;
		return 1;
	}
	BOOL CanDrop() override { return FALSE; }
	int iItemSlot() override { return KNIFE_SLOT; }
	float GetMaxSpeed() override { return 250.0f; }
	BOOL Deploy() override
	{
		BOOL ok = DefaultDeploy("", "", 0, "knife", FALSE);
		m_pPlayer->pev->viewmodel = 0;
		m_pPlayer->pev->weaponmodel = 0;
		m_pPlayer->m_flNextAttack = 0.0f;
		mc::P(m_pPlayer).lastSwing = gpGlobals->time; // Minecraft resets attack strength on item switch
		return ok;
	}
	void Holster(int skiplocal) override
	{
		mc::McPlayer& mp = mc::P(m_pPlayer);
		mp.mineProgress = 0.0f;
		CBasePlayerWeapon::Holster(skiplocal);
	}
	void ItemPostFrame() override { mc::McItemFrame(m_pPlayer); }
	void PrimaryAttack() override {}
	void SecondaryAttack() override {}
	void WeaponIdle() override {}
	BOOL UseDecrement() override { return FALSE; }
};

LINK_ENTITY_TO_CLASS(weapon_mcitem, CMcItem, CCSPlayerWeapon)

namespace mc
{
static bool OnLadder(CBasePlayer* pl) { return pl->pev->movetype == MOVETYPE_FLY; }

static CBaseEntity* FindMeleeTarget(CBasePlayer* pl, float reach, Vector& hitPos)
{
	UTIL_MakeVectors(pl->pev->v_angle);
	Vector src = pl->GetGunPosition();
	Vector end = src + gpGlobals->v_forward * reach;
	TraceResult tr;
	UTIL_TraceLine(src, end, dont_ignore_monsters, pl->edict(), &tr);
	if (tr.flFraction >= 1.0f)
	{
		// Minecraft's crosshair target uses entity bounding boxes, which are fatter than a ray:
		// retry with a small hull so near-misses on a player's edge still connect.
		UTIL_TraceHull(src, end, dont_ignore_monsters, head_hull, pl->edict(), &tr);
	}
	hitPos = tr.vecEndPos;
	if (tr.pHit && tr.flFraction < 1.0f)
	{
		CBaseEntity* e = CBaseEntity::Instance(tr.pHit);
		if (e && e != pl && e->pev->takedamage != DAMAGE_NO)
			return e;
	}
	return nullptr;
}

static void Knockback(CBaseEntity* target, CBasePlayer* attacker, float strength)
{
	if (!target->IsPlayer())
		return;
	// LivingEntity.knockback(strength, sin(yaw), -cos(yaw))
	Vector dir = target->pev->origin - attacker->pev->origin;
	dir.z = 0;
	float len = dir.Length();
	if (len < 0.01f)
		return;
	dir = dir / len;
	Vector& v = target->pev->velocity;
	const float toUnits = 800.0f * 0.6f; // tuned down: CS players have far less ground friction than MC mobs
	v.x = v.x * 0.5f + dir.x * strength * toUnits;
	v.y = v.y * 0.5f + dir.y * strength * toUnits;
	if (target->pev->flags & FL_ONGROUND)
		v.z = min(0.4f * 800.0f * 0.5f, v.z * 0.5f + strength * 800.0f * 0.5f);
	target->pev->flags &= ~FL_ONGROUND;
}

static void Attack(CBasePlayer* pl)
{
	AddExhaustion(pl, 0.1f); // Minecraft: every attack swing
	McPlayer& mp = P(pl);
	const mci::Stack& held = HeldStack(pl);
	const mci::ItemDef& def = held.Empty() ? mci::g_items[0] : mci::Item(held.id);
	float baseDamage = held.Empty() ? 1.0f : def.attackDamage;
	float speed = held.Empty() ? 4.0f : def.attackSpeed;

	float cooldown = 1.0f / speed;
	float strength = clamp((gpGlobals->time - mp.lastSwing + 0.025f) / cooldown, 0.0f, 1.0f);
	mp.lastSwing = gpGlobals->time;
	FxSwing(pl->entindex());

	Vector hitPos;
	CBaseEntity* target = FindMeleeTarget(pl, mci::ATTACK_REACH, hitPos);
	if (!target)
		return; // swinging at air is silent in Minecraft

	float dmg = baseDamage * (0.2f + strength * strength * 0.8f);
	bool strong = strength > 0.9f;
	bool onGround = (pl->pev->flags & FL_ONGROUND) != 0;
	bool crit = strong && !onGround && pl->pev->velocity.z < 0.0f && !OnLadder(pl) && pl->pev->waterlevel < 2;
	if (crit)
		dmg *= 1.5f;
	float hspeed = pl->pev->velocity.Length2D();
	bool sprinting = hspeed > 200.0f; // CS has no sprint key; full-speed running counts
	bool knock = strong && sprinting;
	bool sweep = strong && !crit && !knock && onGround && def.type == mci::IT_SWORD && hspeed < 250.0f;

	// Mace smash: bonus damage from fall distance (Java 1.21 MaceItem.getAttackDamageBonus)
	bool smash = false;
	if (def.type == mci::IT_MACE)
	{
		float fallBlocks = (pl->m_flFallVelocity * pl->m_flFallVelocity) / (2.0f * 800.0f) / 40.0f;
		if (pl->pev->iuser4 & mcp::MCPF_GLIDING)
			fallBlocks = max(fallBlocks, -pl->pev->velocity.z * -pl->pev->velocity.z / 1600.0f / 40.0f);
		if (fallBlocks > 1.5f)
		{
			float bonus = 0.0f, f = fallBlocks;
			bonus += min(f, 3.0f) * 4.0f;
			f -= 3.0f;
			if (f > 0)
				bonus += min(f, 5.0f) * 2.0f;
			f -= 5.0f;
			if (f > 0)
				bonus += f;
			dmg += bonus;
			smash = true;
			pl->m_flFallVelocity = 0.0f; // the smash cancels fall damage
			pl->pev->velocity.z = 0.0f;
		}
	}

	Vector dir = (target->Center() - pl->GetGunPosition()).Normalize();
	ClearMultiDamage();
	TraceResult tr;
	UTIL_TraceLine(pl->GetGunPosition(), target->Center(), dont_ignore_monsters, pl->edict(), &tr);
	target->TraceAttack(pl->pev, dmg * mci::HP_PER_MC, dir, &tr, DMG_SLASH | DMG_NEVERGIB);
	ApplyMultiDamage(pl->pev, pl->pev);

	Knockback(target, pl, knock ? 0.9f : 0.4f);

	if (sweep)
	{
		// sweep hits everything else within 1 block of the target
		CBaseEntity* other = nullptr;
		while ((other = UTIL_FindEntityInSphere(other, target->pev->origin, 1.5f * 40.0f)))
		{
			if (other == pl || other == target || !other->IsPlayer() || !other->IsAlive())
				continue;
			if ((other->pev->origin - pl->pev->origin).Length() > mci::ATTACK_REACH + 40.0f)
				continue;
			other->TakeDamage(pl->pev, pl->pev, 1.0f * mci::HP_PER_MC, DMG_SLASH);
			Knockback(other, pl, 0.4f);
		}
		Vector p = pl->GetGunPosition() + gpGlobals->v_forward * 40.0f;
		FxParticles(mcp::PK_SWEEP, p, 1);
		FxSound(mcs::MCS_ATTACK_SWEEP, pl->pev->origin);
	}
	if (crit)
	{
		FxParticles(mcp::PK_CRIT, target->Center(), 16);
		FxSound(mcs::MCS_ATTACK_CRIT, pl->pev->origin);
	}
	if (smash)
	{
		FxParticles(mcp::PK_BLOCK_BREAK, target->pev->origin - Vector(0, 0, 30), 40, mcw::FindBlock("sandstone"));
		FxSound(mcs::MCS_ANVIL_LAND, target->pev->origin, 0.7f, 0.8f);
	}
	if (knock)
		FxSound(mcs::MCS_ATTACK_KNOCKBACK, pl->pev->origin);
	if (!crit && !sweep && !knock)
		FxSound(strong ? mcs::MCS_ATTACK_STRONG : mcs::MCS_ATTACK_WEAK, pl->pev->origin);
	FxParticles(mcp::PK_DAMAGE, target->Center() + Vector(0, 0, 16), max(1, (int)(dmg / 2.0f)));

	if (!held.Empty())
		DamageHeld(pl, (def.type == mci::IT_SWORD || def.type == mci::IT_MACE) ? 1 : 2);
}

static void Eat(CBasePlayer* pl, const mci::Stack& held)
{
	EatFood(pl, held.id); // food + saturation (+ golden apple effects); healing comes from regeneration
	FxSound(mcs::MCS_PLAYER_BURP, pl->pev->origin, 0.5f, 0.9f + RANDOM_FLOAT(0.0f, 0.1f));
	FxParticles(mcp::PK_HEART, pl->pev->origin + Vector(0, 0, 40), 3);
	ConsumeHeld(pl, 1);
}

static float g_eatStart[MAX_CLIENTS + 1];
static float g_eatSound[MAX_CLIENTS + 1];
static float g_bowStart[MAX_CLIENTS + 1];
static float g_xbowStart[MAX_CLIENTS + 1];
static bool g_xbowLoaded[MAX_CLIENTS + 1];
static bool g_xbowLoadingSound[MAX_CLIENTS + 1];
bool CrossbowLoaded(CBasePlayer* pl) { return g_xbowLoaded[pl->entindex()]; }
bool UsingItem(CBasePlayer* pl)
{
	int i = pl->entindex();
	return g_bowStart[i] > 0.0f || g_xbowStart[i] > 0.0f || g_eatStart[i] > 0.0f;
}

static void UseItem(CBasePlayer* pl, bool pressed, bool held, bool released)
{
	const mci::Stack& stack = HeldStack(pl);
	int idx = pl->entindex();
	const mci::ItemDef& d = stack.Empty() ? mci::g_items[0] : mci::Item(stack.id);

	// Doors, TNT ignition, block placement act on the targeted block first.
	if (pressed && UseBlockTarget(pl, stack))
		return;
	if (stack.Empty())
		return;

	switch (d.type)
	{
	case mci::IT_ARMOR:
	case mci::IT_ELYTRA:
		if (pressed)
		{
			int id = stack.id;
			ConsumeHeld(pl, 1);
			EquipArmor(pl, id);
		}
		break;
	case mci::IT_FIREWORK:
		if (pressed)
		{
			if (pl->pev->iuser4 & mcp::MCPF_GLIDING)
			{
				// boost lasts the rocket's lifetime: 10*(flight+1) + rand(6) + rand(7) ticks, flight = 1
				float ticks = 20.0f + RANDOM_LONG(0, 5) + RANDOM_LONG(0, 6);
				pl->pev->vuser1[0] = ticks * mci::TICK;
				FxSound(mcs::MCS_FIREWORK_LAUNCH, pl->pev->origin, 3.0f, 1.0f, pl->entindex());
				ConsumeHeld(pl, 1);
			}
			else
			{
				LaunchFirework(pl);
				ConsumeHeld(pl, 1);
			}
		}
		break;
	case mci::IT_FOOD:
		if (pressed)
			McLog("eat: %s pressed, can eat %d", d.name, (int)CanEat(pl, stack.id));
		if (pressed && CanEat(pl, stack.id))
			g_eatStart[idx] = gpGlobals->time;
		if (held && g_eatStart[idx] > 0.0f)
		{
			if (gpGlobals->time > g_eatSound[idx])
			{
				FxSound(mcs::MCS_GENERIC_EAT, pl->pev->origin, 0.5f, 0.8f + RANDOM_FLOAT(0.0f, 0.4f));
				FxParticles(mcp::PK_BLOCK_HIT, pl->GetGunPosition() + gpGlobals->v_forward * 12.0f, 4, 0);
				g_eatSound[idx] = gpGlobals->time + 0.2f;
			}
			if (gpGlobals->time - g_eatStart[idx] >= 1.6f)
			{
				Eat(pl, stack);
				g_eatStart[idx] = 0.0f;
			}
		}
		if (released)
			g_eatStart[idx] = 0.0f;
		break;
	case mci::IT_PEARL:
		if (pressed)
		{
			SpawnThrown(pl, mcp::MCE_PEARL, 1.5f);
			ConsumeHeld(pl, 1);
		}
		break;
	case mci::IT_XP_BOTTLE:
		if (pressed)
		{
			SpawnThrown(pl, mcp::MCE_XPBOTTLE, 0.7f);
			ConsumeHeld(pl, 1);
		}
		break;
	case mci::IT_CROSSBOW:
		// CrossbowItem: hold to charge (25 ticks), let go loaded; the next click fires (3.15 blocks/tick)
		if (g_xbowLoaded[idx])
		{
			if (pressed)
			{
				CBaseEntity* bolt = SpawnThrown(pl, mcp::MCE_ARROW, 3.15f);
				if (bolt)
					bolt->pev->fuser1 = 1.0f;
				g_xbowLoaded[idx] = false;
				FxSound(mcs::MCS_CROSSBOW_SHOOT, pl->pev->origin, 1.0f, 1.0f / (RANDOM_FLOAT(0.0f, 0.4f) + 1.2f) + 0.5f);
				DamageHeld(pl, 1);
				UpdatePlayerFlagsPublic(pl);
			}
			break;
		}
		if (pressed)
		{
			g_xbowStart[idx] = gpGlobals->time;
			g_xbowLoadingSound[idx] = false;
			FxSound(mcs::MCS_CROSSBOW_LOAD_START, pl->pev->origin, 0.5f, 1.0f);
			UpdatePlayerFlagsPublic(pl);
		}
		if (held && g_xbowStart[idx] > 0.0f && !g_xbowLoadingSound[idx] && gpGlobals->time - g_xbowStart[idx] >= 1.25f)
		{
			g_xbowLoadingSound[idx] = true;
			FxSound(mcs::MCS_CROSSBOW_LOAD_END, pl->pev->origin, 0.5f, 1.0f);
		}
		if (released && g_xbowStart[idx] > 0.0f)
		{
			g_xbowLoaded[idx] = gpGlobals->time - g_xbowStart[idx] >= 1.25f;
			g_xbowStart[idx] = 0.0f;
			UpdatePlayerFlagsPublic(pl);
		}
		break;
	case mci::IT_BOW:
		if (pressed)
		{
			g_bowStart[idx] = gpGlobals->time;
			UpdatePlayerFlagsPublic(pl);
		}
		if (released && g_bowStart[idx] > 0.0f)
		{
			// BowItem.getPowerForTime: f = t/20 ticks; power = (f*f + f*2)/3, capped at 1
			float f = (gpGlobals->time - g_bowStart[idx]) / 1.0f;
			float power = min(1.0f, (f * f + f * 2.0f) / 3.0f);
			g_bowStart[idx] = 0.0f;
			UpdatePlayerFlagsPublic(pl);
			if (power >= 0.1f)
			{
				CBaseEntity* arrow = SpawnThrown(pl, mcp::MCE_ARROW, power * 3.0f);
				if (arrow && power >= 1.0f)
					arrow->pev->fuser1 = 1.0f; // critical arrow
				FxSound(mcs::MCS_ARROW_SHOOT, pl->pev->origin, 1.0f, 1.0f / (RANDOM_FLOAT(0.0f, 0.4f) + 1.2f) + power * 0.5f);
				DamageHeld(pl, 1);
			}
		}
		break;
	default:
		break;
	}
}

void TestAttackNow(CBasePlayer* pl)
{
	P(pl).lastSwing = gpGlobals->time - 10.0f; // full strength
	Attack(pl);
}

bool g_testHoldAttack = false;
bool g_testHoldAttack2 = false;
void TestUse(CBasePlayer* pl, bool pressed, bool held, bool released) { UseItem(pl, pressed, held, released); }

void McItemFrame(CBasePlayer* pl)
{
	if (!pl->IsAlive())
		return;
	int pressed = pl->m_afButtonPressed;
	int released = pl->m_afButtonReleased;
	int buttons = pl->pev->button;
	if (g_testHoldAttack && !pl->IsBot())
		buttons |= IN_ATTACK;
	if (g_testHoldAttack2 && !pl->IsBot())
		buttons |= IN_ATTACK2;
	{
		// the test hook's held button also produces press/release edges, like a real one
		static bool prev[MAX_CLIENTS + 1];
		int i = pl->entindex();
		bool now = g_testHoldAttack2 && !pl->IsBot();
		if (now && !prev[i])
			pressed |= IN_ATTACK2;
		if (!now && prev[i])
			released |= IN_ATTACK2;
		prev[i] = now;
	}

	if (pressed & IN_ATTACK)
		Attack(pl);
	MineFrame(pl, (buttons & IN_ATTACK) != 0);

	// right mouse is +attack2 in a stock config, but HL25 configs bind it to +alt1 (IN_ALT1): both use
	const int USE = IN_ATTACK2 | IN_ALT1;
	UseItem(pl, (pressed & USE) != 0, (buttons & USE) != 0, (released & USE) != 0);
}
} // namespace mc
