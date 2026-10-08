// Hit boxes true to the Minecraft models. The client draws Steve, zombies, creepers and endermen in place
// of Counter-Strike's player models (mc_players.cpp DrawPlayer), but the engine goes on tracing bullets
// against the hidden CS model: a Minecraft head is twice as wide as a CS one, a creeper's is a foot lower
// and an enderman's is a whole head above the CS model's. So for players drawn as Minecraft models the
// engine's hit boxes are switched off during line traces and the trace is finished here, against the same
// cubes the client draws, in the same pose. Which cube was hit decides the hit group (head shots).
#include "precompiled.h"

#include "mc_chars.h"
#include "mc_server.h"

namespace mc
{
extern int CharacterOf(CBasePlayer* pl); // mc_characters.cpp
extern bool ClassicMode();               // mc_world_srv.cpp

static cvar_t cv_hitboxShow = {"mc_hitbox_show", "0", FCVAR_SERVER, 0.0f, nullptr}; // 1: outline the hit boxes of the model you aim at

static const float PI = 3.14159265358979f;
static const float PX = 72.0f / 32.0f; // engine units per model pixel (mc_players.cpp)

enum Rig
{
	RIG_NONE, // Counter-Strike's own model: the engine's hit boxes stand
	RIG_HUMANOID,
	RIG_ZOMBIE, // the humanoid with its arms out when empty-handed
	RIG_CREEPER,
	RIG_ENDERMAN
};

// Team-default players are Minecraft models unless the client's mc_players says otherwise (PlayersEnabled
// in mc_players.cpp; a listen server shares the client's cvars).
static bool DefaultsAreMinecraft()
{
	static cvar_t* cv = nullptr;
	if (!cv)
		cv = CVAR_GET_POINTER("mc_players");
	float v = cv ? cv->value : 1.0f;
	return ClassicMode() ? v >= 2.0f : v != 0.0f;
}

// The model the client draws this player with (PlayerTakeOver + PickSkin in mc_players.cpp)
static Rig RigOf(CBasePlayer* pl)
{
	int c = CharacterOf(pl);
	if (mcp::IsMinecraftCharacter(c))
	{
		const char* skin = mcp::kCharacters[c].skin;
		if (strstr(skin, "creeper"))
			return RIG_CREEPER;
		if (strstr(skin, "enderman"))
			return RIG_ENDERMAN;
		return strstr(skin, "zombie") ? RIG_ZOMBIE : RIG_HUMANOID;
	}
	if (c > 0 || !DefaultsAreMinecraft())
		return RIG_NONE;
	const char* n = STRING(pl->pev->netname);
	if (strstr(n, "Creeper"))
		return RIG_CREEPER;
	if (strstr(n, "Enderman"))
		return RIG_ENDERMAN;
	if (strstr(n, "Zombie") || strstr(n, "Husk") || strstr(n, "Drowned"))
		return RIG_ZOMBIE;
	if (strstr(n, "Steve") || strstr(n, "Alex") || strstr(n, "Herobrine") || strstr(n, "Notch"))
		return RIG_HUMANOID;
	return pl->m_iTeam == TERRORIST ? RIG_ZOMBIE : RIG_HUMANOID;
}

// ---------------------------------------------------------------------------------------------
// The client's matrix stack, in the same order: p' = m.p + t

struct Xf
{
	float m[3][3];
	float t[3];
};

static void XfMul(Xf& a, const float r[3][3])
{
	float o[3][3];
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			o[i][j] = a.m[i][0] * r[0][j] + a.m[i][1] * r[1][j] + a.m[i][2] * r[2][j];
	memcpy(a.m, o, sizeof(o));
}

static void XfTranslate(Xf& a, float x, float y, float z)
{
	for (int i = 0; i < 3; i++)
		a.t[i] += a.m[i][0] * x + a.m[i][1] * y + a.m[i][2] * z;
}

// glRotatef about a principal axis (0 x, 1 y, 2 z)
static void XfRotate(Xf& a, float rad, int axis)
{
	if (rad == 0.0f)
		return;
	float c = cosf(rad), s = sinf(rad);
	const float rx[3][3] = {{1, 0, 0}, {0, c, -s}, {0, s, c}};
	const float ry[3][3] = {{c, 0, s}, {0, 1, 0}, {-s, 0, c}};
	const float rz[3][3] = {{c, -s, 0}, {s, c, 0}, {0, 0, 1}};
	XfMul(a, axis == 0 ? rx : axis == 1 ? ry : rz);
}

struct Part
{
	float px, py, pz; // pivot
	float xr, yr, zr; // radians
};

struct RigBox
{
	Xf xf;           // cube space (model pixels) -> world
	float inv[3][3]; // world -> cube space
	float lo[3], hi[3];
	int group;
};
static const int MAX_RIG_BOXES = 8;

static void AddBox(RigBox* out, int& n, const Xf& base, const Part& p, float x0, float y0, float z0, float sx, float sy, float sz, int group)
{
	RigBox& b = out[n++];
	b.xf = base;
	XfTranslate(b.xf, p.px, p.py, p.pz); // PushPart
	XfRotate(b.xf, p.zr, 2);
	XfRotate(b.xf, p.yr, 1);
	XfRotate(b.xf, p.xr, 0);
	b.lo[0] = x0; b.lo[1] = y0; b.lo[2] = z0;
	b.hi[0] = x0 + sx; b.hi[1] = y0 + sy; b.hi[2] = z0 + sz;
	b.group = group;
	const float (*m)[3] = b.xf.m;
	float det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
				m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
	float id = det != 0.0f ? 1.0f / det : 0.0f;
	b.inv[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) * id;
	b.inv[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) * id;
	b.inv[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) * id;
	b.inv[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) * id;
	b.inv[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) * id;
	b.inv[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) * id;
	b.inv[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) * id;
	b.inv[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) * id;
	b.inv[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) * id;
}

// The Counter-Strike weapon in hand, as the client reads it from the p_ model ("ak47"), or nullptr
static const char* GunOf(CBasePlayer* pl, char* buf, int size)
{
	if (!pl->pev->weaponmodel)
		return nullptr;
	const char* n = strstr(STRING(pl->pev->weaponmodel), "p_");
	if (!n)
		return nullptr;
	Q_strlcpy(buf, n + 2, size);
	if (char* dot = strchr(buf, '.'))
		*dot = 0;
	return buf;
}

// The cubes of a player's model where the client draws them (DrawPlayer in mc_players.cpp, same numbers).
// Not followed: the walk swing of arms and legs (the client animates it on its own clock) and the idle
// bob; limbs are boxed in their rest pose, arms in the pose of what they hold.
static int BuildRig(CBasePlayer* pl, Rig rig, RigBox* out)
{
	entvars_t* pev = pl->pev;
	bool crouch = (pev->flags & FL_DUCKING) != 0;
	float pitch = clamp(-pev->angles.x * 3.0f, -89.0f, 89.0f);
	float k = crouch ? 0.75f : 1.0f;

	Xf base = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {0, 0, 0}};
	XfTranslate(base, pev->origin.x, pev->origin.y, pev->origin.z - (crouch ? 18.0f : 36.0f)); // the feet
	XfRotate(base, pev->angles.y * PI / 180.0f, 2);
	if (pev->iuser4 & mcp::MCPF_GLIDING)
	{
		XfTranslate(base, 0, 0, 36.0f);
		XfRotate(base, (90.0f - pitch) * PI / 180.0f, 1);
		XfTranslate(base, 0, 0, -36.0f);
	}
	const char* name = STRING(pev->netname);
	if (strstr(name, "Dinnerbone") || strstr(name, "Grumm"))
	{
		XfTranslate(base, 0, 0, 72.0f + 4.0f);
		XfRotate(base, PI, 0);
	}
	if (rig == RIG_CREEPER)
	{
		// the fuse inflates the creeper around its feet
		float f = clamp(mcp::SwellOf(pev->iuser4) / 30.0f, 0.0f, 1.0f);
		float f4 = f * f * f * f;
		const float sc[3][3] = {{1.0f + f4 * 0.4f, 0, 0}, {0, 1.0f + f4 * 0.4f, 0}, {0, 0, 1.0f + f4 * 0.1f}};
		XfMul(base, sc);
	}
	// model space (y down, front -z, pixels) -> world, the neck 24 px above the feet
	XfTranslate(base, 0, 0, 24.0f * PX * k);
	float s = PX * k;
	const float toWorld[3][3] = {{0, 0, -s}, {s, 0, 0}, {0, -s, 0}};
	XfMul(base, toWorld);

	float headXr = pitch * PI / 180.0f;
	int n = 0;
	if (rig == RIG_CREEPER)
	{
		Part ch = {0, 6, 0, headXr, 0, 0}, cb = {0, 6, 0, 0, 0, 0};
		AddBox(out, n, base, ch, -4, -8, -4, 8, 8, 8, HITGROUP_HEAD);
		AddBox(out, n, base, cb, -4, 0, -2, 8, 7, 4, HITGROUP_CHEST);
		AddBox(out, n, base, cb, -4, 7, -2, 8, 5, 4, HITGROUP_STOMACH);
		const Part legs[4] = {{-2, 18, 4, 0, 0, 0}, {2, 18, 4, 0, 0, 0}, {-2, 18, -4, 0, 0, 0}, {2, 18, -4, 0, 0, 0}};
		for (int i = 0; i < 4; i++)
			AddBox(out, n, base, legs[i], -2, 0, -2, 4, 6, 4, (i & 1) ? HITGROUP_LEFTLEG : HITGROUP_RIGHTLEG);
		return n;
	}

	// arms: the pose of what is held (the player rig's; the enderman borrows it)
	Part rarm = {-5, 2, 0, 0, 0, 0}, larm = {5, 2, 0, 0, 0, 0};
	int held = mcp::HeldItemOf(pev->iuser4);
	char gunBuf[64];
	const char* gun = held ? nullptr : GunOf(pl, gunBuf, sizeof(gunBuf));
	bool knife = gun && !strcmp(gun, "knife");
	bool throwable = gun && (!strcmp(gun, "c4") || strstr(gun, "grenade") || !strcmp(gun, "flashbang"));
	bool dual = gun && !strcmp(gun, "elite");
	bool pistol = gun && (!strcmp(gun, "usp") || !strcmp(gun, "glock18") || !strcmp(gun, "p228") || !strcmp(gun, "deagle") || !strcmp(gun, "fiveseven"));
	bool firearm = gun && !knife && !throwable;
	bool using_ = (pev->iuser4 & mcp::MCPF_USING) != 0;
	bool loaded = (pev->iuser4 & mcp::MCPF_XBOW_LOADED) != 0;
	int heldType = held ? mci::Item(held).type : -1;
	if (rig == RIG_ZOMBIE && !gun && !held)
	{
		rarm.xr = larm.xr = -PI / 2.0f;
	}
	else if (heldType == mci::IT_BOW && using_)
	{
		rarm.yr = -0.1f;
		larm.yr = 0.5f;
		rarm.xr = larm.xr = -PI / 2.0f + headXr;
	}
	else if (heldType == mci::IT_CROSSBOW && using_)
	{
		rarm.yr = -0.8f;
		rarm.xr = -0.97079635f;
		larm.yr = 0.4f + (0.85f - 0.4f) * 0.6f;
		larm.xr = rarm.xr + (-PI / 2.0f - rarm.xr) * 0.6f;
	}
	else if ((heldType == mci::IT_CROSSBOW && loaded) || (firearm && !pistol))
	{
		rarm.yr = -0.3f;
		larm.yr = 0.6f;
		rarm.xr = -PI / 2.0f + headXr + 0.1f;
		larm.xr = -1.5f + headXr;
	}
	else if (pistol)
	{
		rarm.yr = -0.1f;
		rarm.xr = -PI / 2.0f + headXr;
	}
	else if (held || gun)
	{
		rarm.xr = -PI / 10.0f;
	}
	if (dual)
	{
		rarm.yr = -0.15f;
		larm.yr = 0.15f;
		rarm.xr = larm.xr = -PI / 2.0f + headXr;
	}
	if (crouch)
	{
		rarm.xr += 0.4f;
		larm.xr += 0.4f;
	}

	if (rig == RIG_ENDERMAN)
	{
		// a humanoid on 30 px stick limbs, the head raised to y = -13
		Part eh = {0, -13, 0, headXr, 0, 0}, eb = {0, -14, 0, 0, 0, 0};
		AddBox(out, n, base, eh, -4, -8, -4, 8, 8, 8, HITGROUP_HEAD);
		AddBox(out, n, base, eb, -4, 0, -2, 8, 7, 4, HITGROUP_CHEST);
		AddBox(out, n, base, eb, -4, 7, -2, 8, 5, 4, HITGROUP_STOMACH);
		Part limbs[4] = {{-5, -12, 0, 0, 0, 0}, {5, -12, 0, 0, 0, 0}, {-2, -5, 0, 0, 0, 0}, {2, -5, 0, 0, 0, 0}};
		if (gun || held)
		{
			limbs[0].xr = rarm.xr; limbs[0].yr = rarm.yr * 0.36f;
			limbs[1].xr = larm.xr; limbs[1].yr = larm.yr * 0.36f;
		}
		static const int groups[4] = {HITGROUP_RIGHTARM, HITGROUP_LEFTARM, HITGROUP_RIGHTLEG, HITGROUP_LEFTLEG};
		for (int i = 0; i < 4; i++)
			AddBox(out, n, base, limbs[i], -1, i < 2 ? -2.0f : 0.0f, -1, 2, 30, 2, groups[i]);
		return n;
	}

	Part head = {0, 0, 0, headXr, 0, 0}, body = {0, 0, 0, 0, 0, 0};
	Part rleg = {-1.9f, 12, 0, 0, 0, 0}, lleg = {1.9f, 12, 0, 0, 0, 0};
	if (crouch)
	{
		body.xr = 0.5f;
		rleg.pz = lleg.pz = 4.0f;
		rleg.py = lleg.py = 12.2f;
		head.py = 4.2f;
		body.py = 3.2f;
		larm.py = rarm.py = 5.2f;
	}
	AddBox(out, n, base, head, -4, -8, -4, 8, 8, 8, HITGROUP_HEAD);
	AddBox(out, n, base, body, -4, 0, -2, 8, 7, 4, HITGROUP_CHEST);
	AddBox(out, n, base, body, -4, 7, -2, 8, 5, 4, HITGROUP_STOMACH);
	AddBox(out, n, base, rarm, -3, -2, -2, 4, 12, 4, HITGROUP_RIGHTARM);
	AddBox(out, n, base, larm, -1, -2, -2, 4, 12, 4, HITGROUP_LEFTARM);
	AddBox(out, n, base, rleg, -2, 0, -2, 4, 12, 4, HITGROUP_RIGHTLEG);
	AddBox(out, n, base, lleg, -2, 0, -2, 4, 12, 4, HITGROUP_LEFTLEG);
	return n;
}

// Where the segment v1 -> v2 enters the box, as a fraction of the segment (a start inside the box is no
// hit, like the engine's: a bullet that went through one cube must not hit it again on its way out).
static bool SegmentBox(const RigBox& b, const float* v1, const float* v2, float& frac, float normal[3])
{
	float p[3], d[3];
	for (int i = 0; i < 3; i++)
	{
		float a0 = v1[0] - b.xf.t[0], a1 = v1[1] - b.xf.t[1], a2 = v1[2] - b.xf.t[2];
		p[i] = b.inv[i][0] * a0 + b.inv[i][1] * a1 + b.inv[i][2] * a2;
		d[i] = b.inv[i][0] * (v2[0] - v1[0]) + b.inv[i][1] * (v2[1] - v1[1]) + b.inv[i][2] * (v2[2] - v1[2]);
	}
	float tIn = -1.0f, tOut = 1.0f;
	int axis = -1;
	float sign = 0.0f;
	for (int i = 0; i < 3; i++)
	{
		if (fabsf(d[i]) < 1e-6f)
		{
			if (p[i] < b.lo[i] || p[i] > b.hi[i])
				return false;
			continue;
		}
		float t0 = (b.lo[i] - p[i]) / d[i], t1 = (b.hi[i] - p[i]) / d[i];
		float sg = -1.0f; // entering through the low face
		if (t0 > t1)
		{
			float t = t0;
			t0 = t1;
			t1 = t;
			sg = 1.0f;
		}
		if (t0 > tIn)
		{
			tIn = t0;
			axis = i;
			sign = sg;
		}
		if (t1 < tOut)
			tOut = t1;
		if (tIn > tOut)
			return false;
	}
	if (axis < 0 || tIn < 0.0f)
		return false;
	frac = tIn;
	// the face's normal in world space (inverse transpose)
	float len = 0.0f;
	for (int i = 0; i < 3; i++)
	{
		normal[i] = b.inv[axis][i] * sign;
		len += normal[i] * normal[i];
	}
	len = sqrtf(len);
	for (int i = 0; i < 3; i++)
		normal[i] = len > 0.0f ? normal[i] / len : 0.0f;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Line traces (W_TraceLine in mc_world_srv.cpp)

struct Hidden
{
	CBasePlayer* pl;
	Rig rig;
	int solid;
};
static Hidden g_hidden[MAX_CLIENTS];
static int g_numHidden = 0;

// The engine's trace is about to run: take the players drawn as Minecraft models out of it. Returns
// whether there are any (then HitRigsRestore and HitRigsTrace must follow).
bool HitRigsHide(edict_t* skip)
{
	// who is drawn how changes rarely: once a frame is plenty
	static Rig rigs[MAX_CLIENTS + 1];
	static float rigsTime = -1.0f;
	if (rigsTime != gpGlobals->time)
	{
		rigsTime = gpGlobals->time;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(i);
			rigs[i] = (p && p->IsAlive()) ? RigOf(p) : RIG_NONE;
		}
	}
	g_numHidden = 0;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		if (rigs[i] == RIG_NONE)
			continue;
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (!p || p->pev->solid == SOLID_NOT || p->pev->deadflag != DEAD_NO)
			continue;
		edict_t* e = p->edict();
		if (skip && (e == skip || skip->v.owner == e))
			continue; // the engine's rule: never the tracer itself, nor the owner of a tracing projectile
		g_hidden[g_numHidden++] = {p, rigs[i], p->pev->solid};
		p->pev->solid = SOLID_NOT; // SV_ClipToLinks reads this per trace; no relink needed
	}
	return g_numHidden > 0;
}

void HitRigsRestore()
{
	for (int i = 0; i < g_numHidden; i++)
		g_hidden[i].pl->pev->solid = g_hidden[i].solid;
}

// The nearest Minecraft model the line hits before whatever the engine found
void HitRigsTrace(const float* v1, const float* v2, TraceResult* ptr)
{
	if (ptr->fAllSolid)
		return;
	float d[3] = {v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2]};
	float len2 = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
	float best = ptr->flFraction, bestN[3] = {0, 0, 0};
	CBasePlayer* hit = nullptr;
	int group = 0;
	for (int h = 0; h < g_numHidden; h++)
	{
		CBasePlayer* p = g_hidden[h].pl;
		// far from the line: no need to pose the model (the tallest, an enderman aiming, fits in 96 units)
		float c[3] = {p->pev->origin.x - v1[0], p->pev->origin.y - v1[1], p->pev->origin.z + 14.0f - v1[2]};
		float t = len2 > 0.0f ? clamp((c[0] * d[0] + c[1] * d[1] + c[2] * d[2]) / len2, 0.0f, 1.0f) : 0.0f;
		float q[3] = {c[0] - d[0] * t, c[1] - d[1] * t, c[2] - d[2] * t};
		if (q[0] * q[0] + q[1] * q[1] + q[2] * q[2] > 96.0f * 96.0f)
			continue;
		RigBox boxes[MAX_RIG_BOXES];
		int n = BuildRig(p, g_hidden[h].rig, boxes);
		for (int i = 0; i < n; i++)
		{
			float f, nrm[3];
			if (SegmentBox(boxes[i], v1, v2, f, nrm) && f < best)
			{
				best = f;
				hit = p;
				group = boxes[i].group;
				memcpy(bestN, nrm, sizeof(bestN));
			}
		}
	}
	if (!hit)
		return;
	ptr->flFraction = best;
	ptr->vecEndPos = Vector(v1[0] + d[0] * best, v1[1] + d[1] * best, v1[2] + d[2] * best);
	ptr->vecPlaneNormal = Vector(bestN[0], bestN[1], bestN[2]);
	ptr->flPlaneDist = DotProduct(ptr->vecEndPos, ptr->vecPlaneNormal);
	ptr->pHit = hit->edict();
	ptr->iHitgroup = group;
	ptr->fInOpen = TRUE;
	gpGlobals->trace_fraction = best;
	gpGlobals->trace_endpos = ptr->vecEndPos;
	gpGlobals->trace_plane_normal = ptr->vecPlaneNormal;
	gpGlobals->trace_plane_dist = ptr->flPlaneDist;
	gpGlobals->trace_ent = ptr->pHit;
	gpGlobals->trace_hitgroup = group;
}

// ---------------------------------------------------------------------------------------------
// Bots (cs_bot_update.cpp): Counter-Strike's bots aim at where a CS model's head and chest are, which on
// an enderman is the gap between its legs. Aim at the drawn head (or chest) instead. The bot turns from
// its origin but fires from its eyes, so the spot is lowered by its eye height, like CS's own.
bool BotAimAtRig(CBasePlayer* bot, CBasePlayer* enemy, bool head, Vector& aimSpot)
{
	if (!enemy)
		return false;
	Rig rig = RigOf(enemy);
	if (rig == RIG_NONE)
		return false;
	RigBox boxes[MAX_RIG_BOXES];
	BuildRig(enemy, rig, boxes);
	const RigBox& b = boxes[head ? 0 : 1]; // every rig lists its head first, then its chest
	float mid[3] = {(b.lo[0] + b.hi[0]) * 0.5f, (b.lo[1] + b.hi[1]) * 0.5f, (b.lo[2] + b.hi[2]) * 0.5f};
	Vector at;
	for (int i = 0; i < 3; i++)
		at[i] = b.xf.m[i][0] * mid[0] + b.xf.m[i][1] * mid[1] + b.xf.m[i][2] * mid[2] + b.xf.t[i];
	// aimSpot is the enemy's origin plus the bot's lead: keep the lead
	aimSpot = aimSpot + (at - enemy->pev->origin);
	aimSpot.z -= bot->pev->view_ofs.z;
	return true;
}

// ---------------------------------------------------------------------------------------------
// mc_hitbox_show 1: the hit boxes of the model nearest your crosshair, outlined (hit groups by colour)

static void ShowRig(CBasePlayer* viewer, CBasePlayer* target, Rig rig)
{
	static const byte rgb[8][3] = {{255, 255, 255}, {255, 60, 60}, {255, 220, 60}, {255, 150, 40}, {80, 220, 255}, {80, 220, 255}, {90, 255, 110}, {90, 255, 110}};
	RigBox boxes[MAX_RIG_BOXES];
	int n = BuildRig(target, rig, boxes);
	for (int i = 0; i < n; i++)
	{
		const RigBox& b = boxes[i];
		Vector c[8];
		for (int k = 0; k < 8; k++)
		{
			float p[3] = {(k & 1) ? b.hi[0] : b.lo[0], (k & 2) ? b.hi[1] : b.lo[1], (k & 4) ? b.hi[2] : b.lo[2]};
			for (int a = 0; a < 3; a++)
				c[k][a] = b.xf.m[a][0] * p[0] + b.xf.m[a][1] * p[1] + b.xf.m[a][2] * p[2] + b.xf.t[a];
		}
		for (int k = 0; k < 8; k++)
			for (int a = 0; a < 3; a++)
			{
				if (k & (1 << a))
					continue;
				const Vector& from = c[k];
				const Vector& to = c[k | (1 << a)];
				MESSAGE_BEGIN(MSG_ONE_UNRELIABLE, SVC_TEMPENTITY, nullptr, viewer->edict());
				WRITE_BYTE(TE_LINE);
				WRITE_COORD(from.x); WRITE_COORD(from.y); WRITE_COORD(from.z);
				WRITE_COORD(to.x); WRITE_COORD(to.y); WRITE_COORD(to.z);
				WRITE_SHORT(3); // life, tenths of a second
				WRITE_BYTE(rgb[b.group & 7][0]); WRITE_BYTE(rgb[b.group & 7][1]); WRITE_BYTE(rgb[b.group & 7][2]);
				MESSAGE_END();
			}
	}
}

void HitRigsFrame()
{
	static float next = 0.0f;
	if (cv_hitboxShow.value == 0.0f || (gpGlobals->time >= next - 1.0f && gpGlobals->time < next))
		return;
	next = gpGlobals->time + 0.25f;
	for (int v = 1; v <= gpGlobals->maxClients; v++)
	{
		CBasePlayer* viewer = UTIL_PlayerByIndex(v);
		if (!viewer || viewer->IsBot() || viewer->IsDormant())
			continue;
		UTIL_MakeVectors(viewer->pev->v_angle);
		Vector eye = viewer->pev->origin + viewer->pev->view_ofs, fwd = gpGlobals->v_forward;
		CBasePlayer* pick = nullptr;
		Rig pickRig = RIG_NONE;
		float bestDot = 0.9f;
		for (int i = 1; i <= gpGlobals->maxClients; i++)
		{
			CBasePlayer* p = UTIL_PlayerByIndex(i);
			if (!p || p == viewer || !p->IsAlive())
				continue;
			Rig rig = RigOf(p);
			Vector to = p->pev->origin - eye;
			float dist = to.Length();
			if (rig == RIG_NONE || dist > 1200.0f || dist < 1.0f)
				continue;
			float dot = DotProduct(to, fwd) / dist;
			if (dot > bestDot)
			{
				bestDot = dot;
				pick = p;
				pickRig = rig;
			}
		}
		if (pick)
			ShowRig(viewer, pick, pickRig);
	}
}

void HitRigsInit() { CVAR_REGISTER(&cv_hitboxShow); }

// The hit map of a player's model seen level from `yaw` degrees off its front, one character per model
// pixel (H head, C chest, S stomach, a arms, l legs), written to the log by the test scenarios.
void HitRigsLogMap(CBasePlayer* shooter, CBasePlayer* target, float yawOff)
{
	static const char sym[9] = {'?', 'H', 'C', 'S', 'a', 'a', 'l', 'l', '#'};
	float yaw = (target->pev->angles.y + yawOff) * PI / 180.0f;
	Vector fwd(cosf(yaw), sinf(yaw), 0), side(-sinf(yaw), cosf(yaw), 0);
	Vector feet = target->pev->origin;
	feet.z -= (target->pev->flags & FL_DUCKING) ? 18.0f : 36.0f;
	McLog("hitmap %s (%s, seen %.0f deg off its front), rows = model pixels above the feet:", STRING(target->pev->netname),
		mcp::kCharacters[CharacterOf(target)].name, yawOff);
	for (int row = 47; row >= 0; row--)
	{
		char line[40];
		int w = 0;
		for (int col = -14; col <= 14; col++)
		{
			Vector at = feet + side * ((col + 0.0f) * PX) + Vector(0, 0, (row + 0.5f) * PX);
			TraceResult tr;
			UTIL_TraceLine(at + fwd * 150.0f, at - fwd * 150.0f, dont_ignore_monsters, shooter->edict(), &tr);
			line[w++] = (tr.flFraction < 1.0f && tr.pHit == target->edict()) ? sym[tr.iHitgroup & 7] : (tr.flFraction < 1.0f ? '+' : '.');
		}
		line[w] = 0;
		McLog("hitmap %2d %s", row, line);
	}
}
} // namespace mc
