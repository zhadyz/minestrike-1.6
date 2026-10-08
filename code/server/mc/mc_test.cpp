// Scripted test scenarios (mc_testscript <name>) so features can be verified unattended: the server
// drives the local player (view angles, item selection, attacks, teleports) on a timeline while the
// tester captures screenshots / reads logs.
#include "precompiled.h"

#include "mc_server.h"
#include "mc_blocks.h"
#include "mc_move.h"
#include "mc_classic.h"

#include <chrono>

namespace mc
{
extern void McItemFrame(CBasePlayer* pl);
void TestAttackNow(CBasePlayer* pl);

static cvar_t cv_testscript = {"mc_testscript", "", FCVAR_SERVER, 0.0f, nullptr};
static float g_testStart = -1.0f;
static int g_testStep = 0;
static char g_testName[64];

void RegisterBspTest();
void RegisterTestCvars()
{
	CVAR_REGISTER(&cv_testscript);
	RegisterBspTest();
}

static CBasePlayer* Human()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (p && !p->IsBot() && p->IsAlive())
			return p;
	}
	return nullptr;
}

static CBasePlayer* NearestBot(CBasePlayer* to, bool enemy)
{
	CBasePlayer* best = nullptr;
	float bd = 1e9f;
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (!p || !p->IsBot() || !p->IsAlive())
			continue;
		if (enemy && p->m_iTeam == to->m_iTeam)
			continue;
		float d = (p->pev->origin - to->pev->origin).Length();
		if (d < bd)
		{
			bd = d;
			best = p;
		}
	}
	return best;
}

static void Look(CBasePlayer* pl, const Vector& target)
{
	Vector dir = target - (pl->pev->origin + pl->pev->view_ofs);
	Vector ang = UTIL_VecToAngles(dir);
	ang.x = -ang.x;
	pl->pev->angles = ang;
	pl->pev->v_angle = ang;
	pl->pev->fixangle = 1;
}

static void PlaceInFront(CBasePlayer* pl, CBasePlayer* bot, float dist)
{
	UTIL_MakeVectors(Vector(0, pl->pev->v_angle.y, 0));
	Vector pos = pl->pev->origin + gpGlobals->v_forward * dist;
	TraceResult tr;
	UTIL_TraceHull(pl->pev->origin, pos, ignore_monsters, human_hull, pl->edict(), &tr);
	UTIL_SetOrigin(bot->pev, tr.vecEndPos);
	bot->pev->velocity = g_vecZero;
	Vector ang = pl->pev->v_angle;
	ang.y += 180.0f;
	bot->pev->angles = Vector(0, ang.y, 0);
	bot->pev->v_angle = bot->pev->angles;
	bot->pev->fixangle = 1;
}

static float g_prevNow = -1.0f;
static bool Hit(float t)
{
	float now = gpGlobals->time - g_testStart;
	return now >= t && g_prevNow < t;
}

static void ScenarioCombat(CBasePlayer* pl)
{
	static CBasePlayer* bot = nullptr;
	if (Hit(1.0f))
	{
		CVAR_SET_FLOAT("bot_stop", 1.0f);
		GiveItem(pl, mci::FindItem("diamond_sword"), 1, true);
		McPlayer& mp = P(pl);
		for (int i = mcp::FIRST_MC_SLOT; i < mcp::HOTBAR_SIZE; i++)
			if (mp.hotbar[i].id == mci::FindItem("diamond_sword"))
				SelectSlot(pl, i);
		McLog("test combat: sword selected");
	}
	if (Hit(2.0f))
	{
		bot = NearestBot(pl, true);
		if (bot)
		{
			PlaceInFront(pl, bot, 70.0f);
			Look(pl, bot->pev->origin + Vector(0, 0, 10));
			McLog("test combat: bot %s placed, hp %.0f", STRING(bot->pev->netname), bot->pev->health);
		}
	}
	if (Hit(2.6f))
		McLog("SHOT combat_bot");
	if (Hit(3.15f))
		McLog("SHOT combat_hit");
	if (Hit(4.75f))
		McLog("SHOT combat_dying");
	if (Hit(6.5f))
		McLog("SHOT combat_xp");
	if (Hit(9.0f))
		McLog("SHOT end");
	for (int k = 0; k < 12; k++)
	{
		if (Hit(3.0f + k * 0.8f) && bot && bot->IsAlive())
		{
			PlaceInFront(pl, bot, 70.0f);
			Look(pl, bot->pev->origin + Vector(0, 0, 10));
			TestAttackNow(pl);
			McLog("test combat: swing %d, bot hp %.0f", k, bot->pev->health);
		}
	}
	if (Hit(14.0f))
	{
		CVAR_SET_FLOAT("bot_stop", 0.0f);
		McLog("test combat: done, xp level %d total %d", P(pl).xpLevel, P(pl).xpTotal);
	}
}

static void ScenarioElytra(CBasePlayer* pl)
{
	if (Hit(1.0f))
	{
		GiveItem(pl, mci::FindItem("elytra"), 1, true);
		GiveItem(pl, mci::FindItem("firework_rocket"), 64, true);
		McPlayer& mp = P(pl);
		for (int i = mcp::FIRST_MC_SLOT; i < mcp::HOTBAR_SIZE; i++)
			if (mp.hotbar[i].id == mci::FindItem("firework_rocket"))
				SelectSlot(pl, i);
	}
	if (Hit(2.0f))
	{
		// launch from high up above an outdoor spot (T spawn has open sky), looking slightly down
		CBaseEntity* sp = UTIL_FindEntityByClassname(nullptr, "info_player_deathmatch");
		if (sp)
			UTIL_SetOrigin(pl->pev, sp->pev->origin + Vector(0, 0, 8));
		Vector o = pl->pev->origin + Vector(0, 0, 1200);
		TraceResult tr;
		UTIL_TraceHull(pl->pev->origin, o, ignore_monsters, human_hull, pl->edict(), &tr);
		UTIL_SetOrigin(pl->pev, tr.vecEndPos - Vector(0, 0, 8));
		pl->pev->angles = pl->pev->v_angle = Vector(15, pl->pev->v_angle.y, 0);
		pl->pev->fixangle = 1;
		pl->pev->velocity = Vector(0, 0, 0);
		pl->pev->flags &= ~FL_ONGROUND;
		pl->pev->iuser4 |= mcp::MCPF_GLIDING;
		McLog("test elytra: start glide at z %.0f", pl->pev->origin.z);
	}
	if (Hit(3.0f))
	{
		pl->pev->vuser1[0] = 1.4f;
		FxSound(mcs::MCS_FIREWORK_LAUNCH, pl->pev->origin, 3.0f, 1.0f, pl->entindex());
		McLog("test elytra: boost");
	}
	if (Hit(2.4f))
		McLog("SHOT ely_a");
	if (Hit(3.3f))
		McLog("SHOT ely_b");
	if (Hit(3.6f))
		CLIENT_COMMAND(pl->edict(), "thirdperson\n");
	if (Hit(4.3f))
		McLog("SHOT ely_c");
	if (Hit(5.0f))
		McLog("SHOT ely_d");
	if (Hit(5.2f))
		CLIENT_COMMAND(pl->edict(), "firstperson\n");
	for (int k = 0; k < 10; k++)
		if (Hit(2.0f + k * 0.5f))
			McLog("test elytra: t=%.1f pos %.0f %.0f %.0f vel %.0f %.0f %.0f gliding %d", 2.0f + k * 0.5f, pl->pev->origin.x, pl->pev->origin.y,
				pl->pev->origin.z, pl->pev->velocity.x, pl->pev->velocity.y, pl->pev->velocity.z, (pl->pev->iuser4 & mcp::MCPF_GLIDING) ? 1 : 0);
}

extern void MineFrame(CBasePlayer* pl, bool holding);
extern bool UseBlockTarget(CBasePlayer* pl, const mci::Stack& held);
extern void PrimeTnt(const float* origin, int fuse);

static void SelectItemByName(CBasePlayer* pl, const char* name)
{
	int id = mci::FindItem(name);
	McPlayer& mp = P(pl);
	for (int i = mcp::FIRST_MC_SLOT; i < mcp::HOTBAR_SIZE; i++)
		if (mp.hotbar[i].id == id)
		{
			SelectSlot(pl, i);
			return;
		}
	// not on the hotbar: put a stack straight into the first Minecraft slot (tests need it in hand)
	const mci::ItemDef& d = mci::Item(id);
	if (mci::IsCsToken(mp.hotbar[mcp::FIRST_MC_SLOT].id))
		mp.hotbar[mcp::FIRST_MC_SLOT] = mci::Stack(); // its token comes back in a free slot
	mp.hotbar[mcp::FIRST_MC_SLOT].id = (uint16_t)id;
	mp.hotbar[mcp::FIRST_MC_SLOT].count = (uint8_t)(d.maxStack > 1 ? d.maxStack : 1);
	mp.hotbar[mcp::FIRST_MC_SLOT].damage = 0;
	mp.invDirty = true;
	SelectSlot(pl, mcp::FIRST_MC_SLOT);
}

static void ScenarioMine(CBasePlayer* pl)
{
	float now = gpGlobals->time - g_testStart;
	if (Hit(1.0f))
	{
		SelectItemByName(pl, "diamond_pickaxe");
		pl->pev->angles = pl->pev->v_angle = Vector(50, pl->pev->v_angle.y, 0);
		pl->pev->fixangle = 1;
		McLog("test mine: pickaxe selected");
	}
	extern bool g_testHoldAttack;
	g_testHoldAttack = (now > 2.0f && now < 7.0f);
	if (Hit(2.6f))
		McLog("SHOT mine_a");
	if (Hit(3.5f))
		McLog("SHOT mine_b");
	if (Hit(6.9f))
		McLog("SHOT mine_c");
	if (Hit(9.0f))
		McLog("SHOT mine_d");
	if (Hit(7.5f))
	{
		SelectItemByName(pl, "cobblestone");
		pl->pev->angles = pl->pev->v_angle = Vector(35, pl->pev->v_angle.y, 0);
		pl->pev->fixangle = 1;
	}
	if (Hit(8.0f))
	{
		bool ok = UseBlockTarget(pl, HeldStack(pl));
		McLog("test mine: place cobblestone -> %d", ok);
	}
	if (Hit(8.5f))
		UseBlockTarget(pl, HeldStack(pl));
}

static void ScenarioTnt(CBasePlayer* pl)
{
	if (Hit(1.0f))
	{
		UTIL_MakeVectors(Vector(0, pl->pev->v_angle.y, 0));
		Vector p = pl->pev->origin + gpGlobals->v_forward * 160.0f;
		PrimeTnt(p, 60);
		McLog("test tnt: primed at %.0f %.0f %.0f", p.x, p.y, p.z);
	}
	if (Hit(2.5f))
		McLog("SHOT tnt_a");
	if (Hit(4.05f))
		McLog("SHOT tnt_b");
	if (Hit(5.5f))
		McLog("SHOT tnt_c");
}

extern void ToggleDoor(int x, int y, int z);
extern bool FindDoor(int out[3]);

static void LookAtPoint(CBasePlayer* pl, const Vector& p) { Look(pl, p); }

static void ScenarioTour(CBasePlayer* pl)
{
	static int door[3];
	static bool haveDoor = false;
	if (Hit(1.0f))
	{
		GiveItem(pl, mci::FindItem("diamond_sword"), 1, false);
		GiveItem(pl, mci::FindItem("diamond_helmet"), 1, false);
		GiveItem(pl, mci::FindItem("diamond_chestplate"), 1, false);
		GiveItem(pl, mci::FindItem("diamond_leggings"), 1, false);
		GiveItem(pl, mci::FindItem("diamond_boots"), 1, false);
		SelectItemByName(pl, "diamond_sword");
		pl->pev->angles = pl->pev->v_angle = Vector(0, pl->pev->v_angle.y, 0);
		pl->pev->fixangle = 1;
	}
	if (Hit(2.0f))
		McLog("SHOT tour_hud");
	if (Hit(2.5f))
		OpenMenu(pl, 1);
	if (Hit(3.2f))
		McLog("SHOT tour_menu");
	if (Hit(3.5f))
		OpenMenu(pl, 11); // armor category
	if (Hit(4.2f))
		McLog("SHOT tour_menu_armor");
	if (Hit(4.5f))
	{
		MESSAGE_BEGIN(MSG_ONE, gmsgShowMenu, nullptr, pl->edict());
		WRITE_SHORT(0);
		WRITE_CHAR(0);
		WRITE_BYTE(0);
		WRITE_STRING("");
		MESSAGE_END();
		P(pl).menu = 0;
		haveDoor = FindDoor(door);
		if (haveDoor)
		{
			// stand two blocks in front of the door (try both sides) and look at it
			Vector dc(g_world.origin[0] + (door[0] + 0.5f) * 40.0f, g_world.origin[1] + (door[1] + 0.5f) * 40.0f, g_world.origin[2] + door[2] * 40.0f + 40.0f);
			static const float off[4][2] = {{100, 0}, {-100, 0}, {0, 100}, {0, -100}};
			for (auto& o : off)
			{
				Vector p = dc + Vector(o[0], o[1], 0);
				TraceResult tr;
				UTIL_TraceHull(p, p, ignore_monsters, human_hull, pl->edict(), &tr);
				if (!tr.fStartSolid && !tr.fAllSolid)
				{
					UTIL_SetOrigin(pl->pev, p);
					break;
				}
			}
			LookAtPoint(pl, dc);
			McLog("test tour: door at %d %d %d", door[0], door[1], door[2]);
		}
	}
	if (Hit(5.3f))
		McLog("SHOT tour_door_a");
	if (Hit(5.6f) && haveDoor)
		ToggleDoor(door[0], door[1], door[2]);
	if (Hit(6.3f))
		McLog("SHOT tour_door_b");
	if (Hit(6.6f))
	{
		GiveItem(pl, mci::FindItem("elytra"), 1, false);
		GiveItem(pl, mci::FindItem("firework_rocket"), 64, false);
		SelectItemByName(pl, "firework_rocket");
		// launch over mid from high up
		Vector o(g_world.origin[0] + g_world.sx * 20.0f, g_world.origin[1] + g_world.sy * 20.0f, g_world.origin[2] + g_world.sz * 40.0f + 120.0f);
		UTIL_SetOrigin(pl->pev, o);
		pl->pev->angles = pl->pev->v_angle = Vector(25, 90, 0);
		pl->pev->fixangle = 1;
		pl->pev->velocity = Vector(0, 300, 0);
		pl->pev->flags &= ~FL_ONGROUND;
		pl->pev->iuser4 |= mcp::MCPF_GLIDING;
	}
	if (Hit(7.4f))
		McLog("SHOT tour_fly_a");
	if (Hit(7.6f))
		pl->pev->vuser1[0] = 1.4f;
	if (Hit(8.4f))
		McLog("SHOT tour_fly_b");
	if (Hit(8.6f))
		CLIENT_COMMAND(pl->edict(), "thirdperson\n");
	if (Hit(9.3f))
		McLog("SHOT tour_fly_c");
	if (Hit(9.6f))
		CLIENT_COMMAND(pl->edict(), "firstperson\n");
	if (Hit(10.0f))
		McLog("SHOT end");
}

void TestUse(CBasePlayer* pl, bool pressed, bool held, bool released);
extern bool g_testHoldAttack2;

static void Aim(CBasePlayer* pl, float pitch)
{
	pl->pev->angles = pl->pev->v_angle = Vector(pitch, pl->pev->v_angle.y, 0);
	pl->pev->fixangle = 1;
}

static void ScenarioItems(CBasePlayer* pl)
{
	if (Hit(1.0f)) { SelectItemByName(pl, "bow"); SelectItemByName(pl, "arrow"); SelectItemByName(pl, "bow"); Aim(pl, -5); TestUse(pl, true, true, false); g_testHoldAttack2 = true; }
	if (Hit(2.2f)) { g_testHoldAttack2 = false; TestUse(pl, false, false, true); McLog("test items: bow released"); }
	if (Hit(2.4f)) McLog("SHOT items_bow");
	if (Hit(3.0f)) { SelectItemByName(pl, "ender_pearl"); Aim(pl, -20); TestUse(pl, true, true, false); McLog("test items: pearl thrown from %.0f %.0f %.0f", pl->pev->origin.x, pl->pev->origin.y, pl->pev->origin.z); }
	if (Hit(3.3f)) McLog("SHOT items_pearl");
	if (Hit(4.8f)) McLog("test items: after pearl at %.0f %.0f %.0f hp %.0f", pl->pev->origin.x, pl->pev->origin.y, pl->pev->origin.z, pl->pev->health);
	if (Hit(5.0f)) { SelectItemByName(pl, "experience_bottle"); Aim(pl, 35); TestUse(pl, true, true, false); }
	if (Hit(5.6f)) McLog("SHOT items_xpbottle");
	if (Hit(6.5f)) { SelectItemByName(pl, "firework_rocket"); Aim(pl, -60); TestUse(pl, true, true, false); }
	if (Hit(7.3f)) McLog("SHOT items_firework_up");
	if (Hit(8.0f)) McLog("SHOT items_firework_boom");
	if (Hit(8.5f)) { pl->pev->health = 40; SelectItemByName(pl, "golden_apple"); Aim(pl, 0); TestUse(pl, true, true, false); g_testHoldAttack2 = true; }
	if (Hit(9.0f)) McLog("SHOT items_eating");
	if (Hit(10.3f)) { g_testHoldAttack2 = false; McLog("test items: after apple hp %.0f", pl->pev->health); McLog("SHOT items_ate"); }
	if (Hit(11.0f)) { GiveItem(pl, mci::FindItem("totem_of_undying"), 1, false); pl->TakeDamage(pl->pev, pl->pev, 500.0f, DMG_GENERIC); McLog("test items: after lethal hit alive %d hp %.0f", pl->IsAlive(), pl->pev->health); }
	if (Hit(11.2f)) McLog("SHOT items_totem");
	if (Hit(12.0f)) McLog("SHOT end");
}

static float g_modelYaw = 0.0f;
static void ScenarioModel(CBasePlayer* pl)
{
	// the local player's own model through Minecraft's F5 views: behind, then in front looking back
	auto cl = [&](const char* c) { CLIENT_COMMAND(pl->edict(), (char*)c); };
	if (Hit(1.0f))
	{
		SelectItemByName(pl, "diamond_sword");
		GiveItem(pl, mci::FindItem("diamond_chestplate"), 1, false);
		GiveItem(pl, mci::FindItem("diamond_helmet"), 1, false);
		// face the direction with the most room both ahead and behind (the camera needs 4 blocks)
		float bestRoom = -1.0f;
		for (int k = 0; k < 8; k++)
		{
			float yaw = k * 45.0f;
			UTIL_MakeVectors(Vector(0, yaw, 0));
			float room = 1e9f;
			for (int sgn = -1; sgn <= 1; sgn += 2)
			{
				TraceResult tr;
				Vector eye = pl->pev->origin + pl->pev->view_ofs;
				UTIL_TraceLine(eye, eye + gpGlobals->v_forward * (sgn * 200.0f), ignore_monsters, pl->edict(), &tr);
				room = fminf(room, tr.flFraction);
			}
			if (room > bestRoom)
			{
				bestRoom = room;
				g_modelYaw = yaw;
			}
		}
		pl->pev->angles = pl->pev->v_angle = Vector(10, g_modelYaw, 0);
		pl->pev->fixangle = 1;
		cl("thirdperson\n");
	}
	if (Hit(1.8f))
		McLog("SHOT model_back");
	if (Hit(2.8f))
		cl("mc_perspective\n");
	if (Hit(3.6f))
		McLog("SHOT model_front");
	if (Hit(4.6f))
	{
		cl("thirdperson\n");
		pl->pev->angles = pl->pev->v_angle = Vector(25, g_modelYaw + 45.0f, 0);
		pl->pev->fixangle = 1;
	}
	if (Hit(5.4f))
		McLog("SHOT model_side");
	if (Hit(6.4f))
	{
		cl("firstperson\n");
		McLog("SHOT end");
	}
}

static CBasePlayer* BotNamed(const char* part)
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CBasePlayer* p = UTIL_PlayerByIndex(i);
		if (p && p->IsBot() && p->IsAlive() && strstr(STRING(p->pev->netname), part))
			return p;
	}
	return nullptr;
}

static void ScenarioCreeper(CBasePlayer* pl)
{
	// a creeper 5 blocks away (outside its 3-block trigger), then 2 blocks away: it hisses, swells, blows up
	static CBasePlayer* bot = nullptr;
	float now = gpGlobals->time - g_testStart;
	if (Hit(0.2f) && !BotNamed("Creeper"))
		SERVER_COMMAND("bot_add Creeper\n");
	if (Hit(2.0f))
	{
		CVAR_SET_FLOAT("bot_stop", 1.0f);
		bot = BotNamed("Creeper");
		McLog("test creeper: bot %s, hp %.0f", bot ? STRING(bot->pev->netname) : "(none)", pl->pev->health);
	}
	if (bot && bot->IsAlive() && now > 2.0f && now < 5.6f)
	{
		PlaceInFront(pl, bot, now < 4.0f ? 200.0f : 80.0f);
		Look(pl, bot->pev->origin - Vector(0, 0, 10));
	}
	if (Hit(3.0f))
		McLog("SHOT creeper_model");
	if (Hit(4.45f))
		McLog("SHOT creeper_swell_a");
	if (Hit(4.75f))
		McLog("SHOT creeper_swell_b");
	if (Hit(5.4f))
		McLog("SHOT creeper_boom");
	if (Hit(6.5f))
		McLog("test creeper: after blast hp %.0f, creeper alive %d", pl->pev->health, bot ? (int)bot->IsAlive() : -1);
	if (Hit(7.0f))
	{
		UTIL_MakeVectors(Vector(0, pl->pev->v_angle.y, 0));
		Look(pl, pl->pev->origin + gpGlobals->v_forward * 100.0f - Vector(0, 0, 70));
	}
	if (Hit(7.5f))
		McLog("SHOT creeper_crater");
	if (Hit(8.5f))
	{
		CVAR_SET_FLOAT("bot_stop", 0.0f);
		McLog("SHOT end");
	}
}

static void ScenarioCreeperWatch(CBasePlayer* pl)
{
	// let a creeper bot loose in a normal round and log what it does (the human is invulnerable)
	float now = gpGlobals->time - g_testStart;
	pl->pev->takedamage = DAMAGE_NO;
	if (Hit(0.2f) && !BotNamed("Creeper"))
		SERVER_COMMAND("bot_add_t Creeper\n");
	static float nextLog = 0.0f;
	if (now < 0.5f)
		nextLog = 0.0f;
	if (now > 1.0f && now >= nextLog)
	{
		nextLog = now + 2.0f;
		CBasePlayer* c = BotNamed("Creeper");
		if (c)
		{
			float best = 1e9f;
			const char* who = "-";
			for (int j = 1; j <= gpGlobals->maxClients; j++)
			{
				CBasePlayer* o = UTIL_PlayerByIndex(j);
				if (o && o != c && o->IsAlive() && o->m_iTeam != c->m_iTeam)
				{
					float d = (o->pev->origin - c->pev->origin).Length() / 40.0f;
					if (d < best)
					{
						best = d;
						who = STRING(o->pev->netname);
					}
				}
			}
			McLog("watch: creeper at %.0f %.0f %.0f weapon %s swell %d, nearest enemy %s %.1f blocks", c->pev->origin.x, c->pev->origin.y,
				c->pev->origin.z, c->m_pActiveItem ? STRING(c->m_pActiveItem->pev->classname) : "none", mcp::SwellOf(c->pev->iuser4), who, best);
		}
	}
	if (Hit(150.0f))
		McLog("SHOT end");
}

static int g_mapsVisited = 0; // survives map changes
static void ScenarioChangelevel(CBasePlayer* pl)
{
	// de_dust2 -> mc_dust2 -> de_dust2: the voxel world must load, unload and reload cleanly
	const char* map = STRING(gpGlobals->mapname);
	bool voxel = strncmp(map, "mc_", 3) == 0;
	if (Hit(1.5f))
		McLog("test changelevel: on %s (visit %d), hp %.0f, sv_stepsize %.0f", map, g_mapsVisited, pl->pev->health, CVAR_GET_FLOAT("sv_stepsize"));
	if (Hit(2.5f))
		McLog("SHOT changelevel_%d_%s", g_mapsVisited, map);
	if (Hit(4.0f))
	{
		if (++g_mapsVisited <= 2)
			SERVER_COMMAND(voxel ? (char*)"changelevel de_dust2\n" : (char*)"changelevel mc_dust2\n");
		else
			McLog("SHOT end");
	}
}

static void ScenarioGui(CBasePlayer* pl)
{
	// the creative inventory screen, driven by real clicks (runtest performs the KEYS lines)
	auto cl = [&](const char* c) { char b[96]; Q_snprintf(b, sizeof(b), "%s\n", c); CLIENT_COMMAND(pl->edict(), b); };
	// cursor moves arrive instantly, the KEYS run ~0.5-1 s later: leave room on both sides
	if (Hit(1.0f)) cl("buy");
	if (Hit(1.5f)) McLog("SHOT gui_open");
	if (Hit(2.0f)) cl("mc_guicursor grid 4 0");
	if (Hit(2.6f)) McLog("SHOT gui_tooltip");
	if (Hit(3.5f)) cl("mc_guicursor hotbar 8");
	if (Hit(3.6f)) McLog("KEYS rclick");
	if (Hit(5.5f)) cl("mc_guicursor grid 4 0");
	if (Hit(5.6f)) McLog("KEYS click");
	if (Hit(7.2f)) McLog("SHOT gui_taken");
	if (Hit(7.5f)) cl("mc_guicursor tab 5");
	if (Hit(7.6f)) McLog("KEYS click");
	if (Hit(9.5f)) cl("mc_guicursor grid 1 2");
	if (Hit(9.6f)) McLog("KEYS click");
	if (Hit(11.2f)) McLog("SHOT gui_kits");
	if (Hit(11.5f)) cl("mc_guicursor tab 0");
	if (Hit(11.6f)) McLog("KEYS click");
	if (Hit(13.5f)) cl("mc_guicursor grid 2 1");
	if (Hit(13.6f)) McLog("KEYS 7");
	if (Hit(15.2f)) McLog("SHOT gui_blocks");
	if (Hit(15.5f)) McLog("KEYS esc");
	if (Hit(17.0f)) { McLog("test gui: slot7 %s slot9 %s chest %s", mci::Item(P(pl).hotbar[6].id).name, mci::Item(P(pl).hotbar[8].id).name, mci::Item(P(pl).armor[1].id).name); McLog("SHOT gui_closed"); }
	if (Hit(18.0f)) McLog("SHOT end");
}

static void ScenarioEnderman(CBasePlayer* pl)
{
	static CBasePlayer* ender = nullptr;
	static CBasePlayer* dinner = nullptr;
	float now = gpGlobals->time - g_testStart;
	if (Hit(0.2f) && !BotNamed("Enderman"))
		SERVER_COMMAND("bot_add Enderman\n");
	if (Hit(0.4f) && !BotNamed("Dinnerbone"))
		SERVER_COMMAND("bot_add Dinnerbone\n");
	if (Hit(2.0f))
	{
		CVAR_SET_FLOAT("bot_stop", 1.0f);
		ender = BotNamed("Enderman");
		dinner = BotNamed("Dinnerbone");
		SelectItemByName(pl, "diamond_sword");
		McLog("test enderman: %s / %s", ender ? "enderman" : "-", dinner ? "dinnerbone" : "-");
	}
	static Vector startAngles;
	if (Hit(2.05f))
		startAngles = pl->pev->v_angle;
	if (Hit(4.4f) || Hit(5.9f))
	{
		// face the original direction again (the shot turned us toward wherever it teleported)
		pl->pev->angles = pl->pev->v_angle = startAngles;
		pl->pev->fixangle = 1;
	}
	if (ender && ender->IsAlive() && now > 2.0f && now < 3.3f)
	{
		PlaceInFront(pl, ender, 130.0f);
		Look(pl, ender->pev->origin + Vector(0, 0, 20));
	}
	if (Hit(2.8f))
		McLog("SHOT ender_model");
	if (Hit(3.4f) && ender)
	{
		float hp = ender->pev->health;
		ender->TakeDamage(pl->pev, pl->pev, 30.0f, DMG_BULLET);
		McLog("test enderman: shot it, hp %.0f -> %.0f, now %.0f units away", hp, ender->pev->health, (ender->pev->origin - pl->pev->origin).Length());
	}
	if (Hit(3.6f))
		McLog("SHOT ender_teleport");
	if (dinner && dinner->IsAlive() && now > 4.5f && now < 5.6f)
	{
		PlaceInFront(pl, dinner, 120.0f);
		Look(pl, dinner->pev->origin + Vector(0, 0, 10));
	}
	if (Hit(5.2f))
		McLog("SHOT ender_dinnerbone");
	for (int k = 0; k < 10; k++)
	{
		if (Hit(6.0f + k * 0.7f) && ender && ender->IsAlive())
		{
			PlaceInFront(pl, ender, 70.0f);
			Look(pl, ender->pev->origin + Vector(0, 0, 10));
			TestAttackNow(pl);
			McLog("test enderman: swing %d hp %.0f", k, ender->pev->health);
		}
	}
	if (Hit(13.5f))
	{
		McLog("test enderman: alive %d", ender ? (int)ender->IsAlive() : -1);
		CVAR_SET_FLOAT("bot_stop", 0.0f);
		McLog("SHOT end");
	}
}

static void ScenarioFlight(CBasePlayer* pl)
{
	// a sightseeing glide over mc_dust2: T spawn -> mid -> CT spawn -> A site -> long A, steering by yaw and
	// holding height with pitch (looking down speeds a glide up, looking up bleeds speed), rockets now and then
	static const Vector wp[] = {Vector(-420, 200, 0), Vector(-380, 1400, 0), Vector(300, 2250, 0), Vector(1150, 2450, 0),
		Vector(1450, 1300, 0), Vector(900, 300, 0), Vector(-300, -300, 0)};
	static const int NW = sizeof(wp) / sizeof(wp[0]);
	static int cur = 0;
	float now = gpGlobals->time - g_testStart;
	if (Hit(0.3f))
	{
		GiveItem(pl, mci::FindItem("elytra"), 1, false);
		SelectItemByName(pl, "firework_rocket");
	}
	if (Hit(1.0f))
	{
		cur = 0;
		Vector start(-760, -760, 1500);
		UTIL_SetOrigin(pl->pev, start);
		Vector to = wp[0] - start;
		float yaw = atan2f(to.y, to.x) * 180.0f / M_PI;
		pl->pev->angles = pl->pev->v_angle = Vector(15, yaw, 0);
		pl->pev->fixangle = 1;
		UTIL_MakeVectors(Vector(0, yaw, 0));
		pl->pev->velocity = gpGlobals->v_forward * 500.0f;
		pl->pev->flags &= ~FL_ONGROUND;
		pl->pev->iuser4 |= mcp::MCPF_GLIDING;
	}
	if (now > 1.0f && now < 17.0f && (pl->pev->iuser4 & mcp::MCPF_GLIDING))
	{
		Vector to = wp[cur] - pl->pev->origin;
		to.z = 0;
		if (to.Length() < 380.0f && cur < NW - 1)
			cur++;
		float want = atan2f(to.y, to.x) * 180.0f / M_PI;
		float yaw = pl->pev->v_angle.y;
		float d = want - yaw;
		while (d > 180.0f) d -= 360.0f;
		while (d < -180.0f) d += 360.0f;
		float turn = 70.0f * gpGlobals->frametime;
		yaw += d > turn ? turn : d < -turn ? -turn : d;
		// nose down the whole way (the camera should see the map), steeper while there's height to spare
		float pitch = 10.0f + (pl->pev->origin.z - 1150.0f) / 20.0f;
		pitch = pitch < 4.0f ? 4.0f : pitch > 30.0f ? 30.0f : pitch;
		pl->pev->angles = pl->pev->v_angle = Vector(pitch, yaw, 0);
		pl->pev->fixangle = 1;
	}
	if (Hit(1.4f) || Hit(4.6f))
	{
		pl->pev->vuser1[0] = 1.4f; // firework boost
		FxSound(mcs::MCS_FIREWORK_LAUNCH, pl->pev->origin, 3.0f, 1.0f, pl->entindex());
	}
	// Minecraft's F5 camera for the middle of the flight
	if (Hit(3.2f))
		CLIENT_COMMAND(pl->edict(), "thirdperson\n");
	if (Hit(6.4f))
		CLIENT_COMMAND(pl->edict(), "firstperson\n");
	static const float shots[] = {1.5f, 2.5f, 3.8f, 5.0f, 6.0f, 7.5f};
	for (int i = 0; i < 6; i++)
		if (Hit(shots[i]))
			McLog("SHOT flight_%d", i);
	for (int k = 0; k < 16; k++)
		if (Hit(1.0f + k))
			McLog("test flight: t=%d wp %d pos %.0f %.0f %.0f speed %.0f gliding %d v_angle %.1f %.1f angles %.1f", k, cur, pl->pev->origin.x, pl->pev->origin.y, pl->pev->origin.z,
				pl->pev->velocity.Length(), (pl->pev->iuser4 & mcp::MCPF_GLIDING) ? 1 : 0, pl->pev->v_angle.x, pl->pev->v_angle.y, pl->pev->angles.x);
	if (Hit(10.0f))
		McLog("SHOT end");
}

static void ScenarioViews(CBasePlayer* pl)
{
	// the same 8 views on any map (A/B the classic renderer against the engine on de_dust2)
	static const float spots[2][3] = {{352.0f, 2464.0f, -60.0f}, {-736.0f, -800.0f, 200.0f}};
	pl->pev->takedamage = DAMAGE_NO;
	for (int k = 0; k < 8; k++)
	{
		if (Hit(1.0f + k * 1.2f))
		{
			const float* sp = spots[k / 4];
			Vector o(sp[0], sp[1], sp[2]);
			TraceResult tr;
			UTIL_TraceHull(o, o - Vector(0, 0, 400), ignore_monsters, human_hull, pl->edict(), &tr);
			UTIL_SetOrigin(pl->pev, tr.vecEndPos);
			pl->pev->velocity = g_vecZero;
			pl->pev->angles = pl->pev->v_angle = Vector(5.0f, (float)(k % 4) * 90.0f, 0.0f);
			pl->pev->fixangle = 1;
		}
		if (Hit(1.8f + k * 1.2f))
			McLog("SHOT view_%d", k);
	}
	if (Hit(11.0f))
		McLog("SHOT end");
}

static void ScenarioHand(CBasePlayer* pl)
{
	// what the first-person hand looks like holding a sword, a pickaxe, a block, an apple
	static const char* items[] = {"diamond_sword", "diamond_pickaxe", "cobblestone", "golden_apple", "bow"};
	pl->pev->takedamage = DAMAGE_NO;
	if (Hit(0.5f))
	{
		pl->pev->angles = pl->pev->v_angle = Vector(0, pl->pev->v_angle.y, 0);
		pl->pev->fixangle = 1;
	}
	for (int k = 0; k < 5; k++)
	{
		if (Hit(1.0f + k * 1.0f))
			SelectItemByName(pl, items[k]);
		if (Hit(1.7f + k * 1.0f))
			McLog("SHOT hand_%s", items[k]);
	}
	if (Hit(6.5f))
		McLog("SHOT end");
}

extern void CarveCell(int x, int y, int z, CBasePlayer* by, bool drop);
static void ScenarioPit(CBasePlayer* pl)
{
	// dig a 3x3 pit two cells deep right in front of the player, then walk into it, along its edge
	// and back out with real key presses; positions are logged to spot sticking
	static Vector startPos;
	pl->pev->takedamage = DAMAGE_NO;
	if (Hit(0.5f))
	{
		pl->pev->angles = pl->pev->v_angle = Vector(0, 0, 0);
		pl->pev->fixangle = 1;
		startPos = pl->pev->origin;
		int b[3];
		float feet[3] = {pl->pev->origin.x + 100.0f, pl->pev->origin.y, pl->pev->origin.z - 40.0f};
		mcm::GetWorld()->ToBlock(feet, b);
		for (int dz = 0; dz > -2; dz--)
			for (int dy = -1; dy <= 1; dy++)
				for (int dx = -1; dx <= 1; dx++)
					CarveCell(b[0] + dx, b[1] + dy, b[2] + dz, nullptr, false);
		McLog("test pit: dug around %d %d %d from %.0f %.0f %.0f", b[0], b[1], b[2], startPos.x, startPos.y, startPos.z);
	}
	if (Hit(1.5f))
		McLog("SHOT pit_before");
	if (Hit(2.0f))
		McLog("KEYS kdown:w wait:1800 kup:w");
	if (Hit(4.5f))
		McLog("SHOT pit_in");
	if (Hit(5.0f))
		McLog("KEYS kdown:d wait:1200 kup:d kdown:a wait:1200 kup:a");
	if (Hit(8.0f))
		McLog("KEYS kdown:s kdown:space wait:300 kup:space wait:1500 kup:s");
	if (Hit(10.5f))
		McLog("SHOT pit_out");
	for (int k = 0; k < 22; k++)
		if (Hit(1.0f + k * 0.5f))
			McLog("test pit: t=%.1f pos %.1f %.1f %.1f vel %.0f %.0f %.0f ground %d", 1.0f + k * 0.5f, pl->pev->origin.x - startPos.x,
				pl->pev->origin.y - startPos.y, pl->pev->origin.z - startPos.z, pl->pev->velocity.x, pl->pev->velocity.y, pl->pev->velocity.z,
				(pl->pev->flags & FL_ONGROUND) ? 1 : 0);
	if (Hit(12.0f))
		McLog("SHOT end");
}

static void ScenarioPlace(CBasePlayer* pl)
{
	// place cobblestone on the floor and against a wall of the classic map
	pl->pev->takedamage = DAMAGE_NO;
	static const float pitches[] = {70.0f, 45.0f, 20.0f, 0.0f};
	if (Hit(0.5f))
		SelectItemByName(pl, "cobblestone");
	for (int k = 0; k < 8; k++)
	{
		if (Hit(1.0f + k * 1.2f))
		{
			pl->pev->angles = pl->pev->v_angle = Vector(pitches[k % 4], (k / 4) * 180.0f, 0);
			pl->pev->fixangle = 1;
		}
		if (Hit(1.3f + k * 1.2f))
			McLog("KEYS rclick"); // a real right click, like a player
	}
	if (Hit(11.0f))
	{
		pl->pev->angles = pl->pev->v_angle = Vector(30, 90, 0);
		pl->pev->fixangle = 1;
	}
	if (Hit(11.6f))
		McLog("SHOT place_result");
	if (Hit(12.4f))
		McLog("SHOT end");
}

static void ScenarioInv(CBasePlayer* pl)
{
	// fill the inventory, open it, move a stack, put a helmet on, trash something - no real input needed
	pl->pev->takedamage = DAMAGE_NO;
	auto cl = [&](const char* c) { CLIENT_COMMAND(pl->edict(), (char*)c); };
	if (Hit(0.5f))
	{
		static const char* items[] = {"golden_apple", "tnt", "cobblestone", "ender_pearl", "arrow", "diamond", "gunpowder", "oak_planks"};
		for (const char* n : items)
			GiveItem(pl, mci::FindItem(n), 16, false);
		McPlayer& mp = P(pl);
		mp.armor[mci::SLOT_HEAD] = mci::Stack();
		mp.inv[20].id = (uint16_t)mci::FindItem("netherite_helmet");
		mp.inv[20].count = 1;
		mp.invDirty = true;
		McLog("test inv: filled");
	}
	if (Hit(1.0f))
		cl("mc_inventory\n");
	if (Hit(1.6f))
		McLog("SHOT inv_open");
	// storage slot 9 -> slot 35 (pick up, put down)
	if (Hit(2.0f)) cl("mc_guicursor invslot 9\n");
	if (Hit(2.2f)) cl("mc_guicursor click 0 0\n");
	if (Hit(2.4f)) cl("mc_guicursor invslot 35\n");
	if (Hit(2.6f)) McLog("SHOT inv_carry");
	if (Hit(3.0f)) cl("mc_guicursor click 0 0\n");
	// shift-click the helmet (storage 29) into the head slot
	if (Hit(3.4f)) cl("mc_guicursor invslot 29\n");
	if (Hit(3.6f)) cl("mc_guicursor click 0 1\n");
	// take the cobblestone stack and trash it
	if (Hit(4.0f)) cl("mc_guicursor invslot 11\n");
	if (Hit(4.2f)) cl("mc_guicursor click 0 0\n");
	if (Hit(4.4f)) cl("mc_guicursor invslot 46\n");
	if (Hit(4.6f)) cl("mc_guicursor click 0 0\n");
	if (Hit(5.2f))
	{
		McPlayer& mp = P(pl);
		McLog("test inv: slot35 %s, head %s, slot11 %s", mci::Item(mp.inv[26].id).name, mci::Item(mp.armor[mci::SLOT_HEAD].id).name,
			mci::Item(mp.inv[2].id).name);
		McLog("SHOT inv_after");
	}
	if (Hit(6.0f)) cl("mc_inventory\n");
	if (Hit(6.6f)) McLog("SHOT end");
}

static void ScenarioFood(CBasePlayer* pl)
{
	// hunt -> eat bread -> regenerate; golden apple -> absorption + regeneration; inventory preview
	extern bool g_testHoldAttack2;
	float now = gpGlobals->time - g_testStart;
	McPlayer& mp = P(pl);
	auto cl = [&](const char* c) { CLIENT_COMMAND(pl->edict(), (char*)c); };
	if (Hit(0.5f))
	{
		pl->TakeDamage(VARS(INDEXENT(0)), VARS(INDEXENT(0)), 50.0f, DMG_GENERIC);
		mp.food = 14;
		mp.saturation = 0.0f;
		mp.statDirty = true;
		SelectItemByName(pl, "bread");
		McLog("test food: hurt to %.0f, food %d", pl->pev->health, mp.food);
	}
	if (Hit(1.2f))
		McLog("SHOT food_hungry");
	g_testHoldAttack2 = (now > 1.5f && now < 3.4f) || (now > 8.5f && now < 10.4f);
	if (Hit(3.6f))
		McLog("test food: after bread food %d sat %.1f hp %.0f", mp.food, mp.saturation, pl->pev->health);
	for (int k = 0; k < 8; k++)
		if (Hit(4.0f + k * 0.5f))
			McLog("test food: t=%.1f hp %.0f food %d sat %.1f", 4.0f + k * 0.5f, pl->pev->health, mp.food, mp.saturation);
	if (Hit(8.0f))
		SelectItemByName(pl, "golden_apple");
	if (Hit(10.6f))
	{
		McLog("test food: after golden apple hp %.0f absorption %.0f food %d", pl->pev->health, mp.absorption, mp.food);
		McLog("SHOT food_apple");
	}
	if (Hit(11.2f))
		cl("mc_inventory\n");
	if (Hit(12.0f))
		McLog("SHOT food_inventory");
	if (Hit(12.5f))
		cl("mc_inventory\n");
	if (Hit(13.0f))
		McLog("SHOT end");
}

// Redstone on the real Dust II floor: lever -> 3 dust -> lamp -> TNT, built in front of the player.
// A real right-click flips the lever; the dust lights up, the lamp turns on, the TNT ignites and blows a
// crater. Also logs the dust2 texture under the player (CS footsteps) .
static int g_rsCells[6][3];
static float g_rsFloor[6];
static bool g_rsBuilt = false;
static void AimAt(CBasePlayer* pl, const Vector& target)
{
	Vector eye = pl->pev->origin + pl->pev->view_ofs;
	Vector d = target - eye;
	float yaw = atan2f(d.y, d.x) * 180.0f / (float)M_PI;
	float down = atan2f(-d.z, d.Length2D()) * 180.0f / (float)M_PI; // positive looks down (as in ScenarioPlace)
	pl->pev->angles = pl->pev->v_angle = Vector(down, yaw, 0);
	pl->pev->fixangle = 1;
}
static Vector RsCenter(int k, float up)
{
	return Vector(g_world.origin[0] + (g_rsCells[k][0] + 0.5f) * 40.0f, g_world.origin[1] + (g_rsCells[k][1] + 0.5f) * 40.0f,
		g_rsFloor[k] + up); // on the real floor
}
static void ScenarioRedstone(CBasePlayer* pl)
{
	pl->pev->takedamage = DAMAGE_NO;
	if (Hit(0.5f))
	{
		Vector o = pl->pev->origin;
		float s[3] = {o.x, o.y, o.z}, e[3] = {o.x, o.y, o.z - 64.0f};
		const char* tex = mcm::WorldTraceTexture(s, e);
		McLog("test redstone: texture under the player: %s", tex ? tex : "(none)");
		// a row of open floor cells starting 2 cells ahead, in the first direction that has one
		g_rsBuilt = false;
		int placed = 0;
		static const float dirs[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
		for (int di = 0; di < 4 && placed < 6; di++)
		{
			placed = 0;
			float mn[3] = {0, 0, 0};
			for (int k = 0; k < 6; k++)
			{
				float px = o.x + dirs[di][0] * (80.0f + k * 40.0f), py = o.y + dirs[di][1] * (80.0f + k * 40.0f);
				float a[3] = {px, py, o.z}, b[3] = {px, py, o.z - 100.0f}, s0[3] = {o.x, o.y, o.z};
				mcw::Trace path, tr;
				mcm::WorldTrace(s0, a, mn, mn, path);
				if (path.hit || mcm::WorldPointSolid(a))
					break;
				mcm::WorldTrace(a, b, mn, mn, tr);
				if (!tr.hit || tr.startsolid || tr.normal[2] < 0.7f)
					break;
				float q[3] = {tr.endpos[0], tr.endpos[1], tr.endpos[2] + 1.0f};
				g_world.ToBlock(q, g_rsCells[k]);
				g_rsFloor[k] = tr.endpos[2];
				placed++;
			}
		}
		if (placed == 6)
		{
			static const char* parts[6] = {"lever", "redstone_wire", "redstone_wire", "redstone_wire", "redstone_lamp", "tnt"};
			for (int k = 0; k < 6; k++)
			{
				int t = mcw::FindBlock(parts[k]);
				uint16_t st = (k == 0) ? 0 : 0; // lever on the floor, off
				SetBlock(g_rsCells[k][0], g_rsCells[k][1], g_rsCells[k][2], mcw::MakeCell((uint16_t)t, st));
			}
			g_rsBuilt = true;
			McLog("test redstone: built at %d %d %d .. %d %d %d", g_rsCells[0][0], g_rsCells[0][1], g_rsCells[0][2], g_rsCells[5][0],
				g_rsCells[5][1], g_rsCells[5][2]);
		}
		else
			McLog("test redstone: no floor ahead (%d cells)", placed);
	}
	if (!g_rsBuilt)
	{
		if (Hit(2.0f))
			McLog("SHOT end");
		return;
	}
	if (Hit(1.0f))
		AimAt(pl, RsCenter(3, 0.0f));
	if (Hit(1.6f))
		McLog("SHOT rs_off");
	if (Hit(2.0f))
		AimAt(pl, RsCenter(0, 4.0f));
	if (Hit(2.4f))
		McLog("KEYS rclick"); // flip the lever like a player
	if (Hit(2.9f) && !(mcw::CellState(g_world.Get(g_rsCells[0][0], g_rsCells[0][1], g_rsCells[0][2])) & 8))
	{
		McLog("test redstone: the real click did not reach the game (not in front), flipping the lever directly");
		RedstoneUse(pl, g_rsCells[0][0], g_rsCells[0][1], g_rsCells[0][2]);
	}
	if (Hit(3.2f))
	{
		auto st = [&](int k) { return (int)mcw::CellState(g_world.Get(g_rsCells[k][0], g_rsCells[k][1], g_rsCells[k][2])); };
		auto ty = [&](int k) { return mcw::Block(mcw::CellType(g_world.Get(g_rsCells[k][0], g_rsCells[k][1], g_rsCells[k][2]))).name; };
		McLog("test redstone: lever %s st %d, dust %d %d %d, lamp %s st %d, tnt cell %s", ty(0), st(0), st(1) & 15, st(2) & 15,
			st(3) & 15, ty(4), st(4), ty(5));
		AimAt(pl, RsCenter(3, 0.0f));
	}
	if (Hit(3.8f))
		McLog("SHOT rs_on");
	if (Hit(8.0f))
		McLog("SHOT rs_boom");
	if (Hit(8.4f))
	{
		// bullet-length traces from the player through the crater and on across the map
		Vector eye = pl->pev->origin + pl->pev->view_ofs;
		Vector mid = (RsCenter(2, 0.0f) + RsCenter(4, 0.0f)) * 0.5f;
		auto t0 = std::chrono::steady_clock::now();
		int hits = 0, fb = 0, n = 200;
		float mn[3] = {0, 0, 0};
		for (int i = 0; i < n; i++)
		{
			Vector tgt = mid + Vector(RANDOM_FLOAT(-60, 60), RANDOM_FLOAT(-60, 60), RANDOM_FLOAT(-60, 10));
			Vector dir = (tgt - eye).Normalize();
			Vector end = eye + dir * 8192.0f;
			float s[3] = {eye.x, eye.y, eye.z}, e[3] = {end.x, end.y, end.z};
			mcc::Result r;
			mcm::GetClassic()->Trace(s, e, mn, mn, r);
			hits += r.hit ? 1 : 0;
			fb += r.fallback ? 1 : 0;
		}
		double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / n;
		McLog("test redstone: %d traces through the crater, %d hit, %d took the carve-aware path, %.1f us each", n, hits, fb, us);
		// the old behaviour for comparison: the carve-aware march over the whole segment, no hand-back
		t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < 20; i++)
		{
			Vector tgt = mid + Vector(RANDOM_FLOAT(-60, 60), RANDOM_FLOAT(-60, 60), RANDOM_FLOAT(-60, 10));
			Vector end = eye + (tgt - eye).Normalize() * 8192.0f;
			float s[3] = {eye.x, eye.y, eye.z}, e[3] = {end.x, end.y, end.z};
			mcc::Result r;
			mcm::GetClassic()->TraceSlow(s, e, mn, mn, r);
		}
		McLog("test redstone: full-segment march (old way) %.1f us each",
			std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 20);
		// worst case: long eye-level traces across the map, carve-aware all the way vs handed back
		for (int mode = 0; mode < 2; mode++)
		{
			float hm2[3] = {-16, -16, -18}, hM2[3] = {16, 16, 18};
			double total = 0.0, worst = 0.0;
			int agree = 0;
			for (int i = 0; i < 16; i++)
			{
				float a = i * 22.5f * (float)M_PI / 180.0f;
				float s[3] = {eye.x, eye.y, eye.z}, e[3] = {eye.x + cosf(a) * 8192.0f, eye.y + sinf(a) * 8192.0f, eye.z};
				for (int h = 0; h < 2; h++)
				{
					auto q0 = std::chrono::steady_clock::now();
					mcc::Result r1, r2;
					mcm::GetClassic()->TraceSlow(s, e, h ? hm2 : mn, h ? hM2 : mn, r1, mode == 1);
					double us1 = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - q0).count();
					total += us1;
					worst = us1 > worst ? us1 : worst;
					if (mode == 1)
					{
						mcm::GetClassic()->TraceSlow(s, e, h ? hm2 : mn, h ? hM2 : mn, r2, false);
						agree += fabsf(r1.fraction - r2.fraction) * 8192.0f < 1.0f ? 1 : 0;
					}
				}
			}
			McLog("test redstone: 32 long traces, %s: %.0f us avg, worst %.0f us%s", mode ? "handed back (new)" : "carve-aware all the way (old)",
				total / 32, worst, mode ? (agree == 32 ? ", same hits as the old way" : ", DIFFERENT hits") : "");
			if (mode == 1 && agree != 32)
				McLog("test redstone: %d of 32 agree", agree);
		}
	}
	// with bots around: the game must keep running smoothly next to the crater
	for (int k = 0; k < 8; k++)
		if (Hit(9.0f + k * 2.0f))
			McLog("test redstone: t=%.0f frametime %.4f", 9.0f + k * 2.0f, gpGlobals->frametime);
	if (Hit(25.0f))
		McLog("SHOT end");
}

// CS bullets against the classic map: real left mouse on an AK, impacts (puffs, sparks, impact sound)
// must appear on the dust2 wall, not pass through it.
static void ScenarioShoot(CBasePlayer* pl)
{
	pl->pev->takedamage = DAMAGE_NO;
	if (Hit(0.5f))
	{
		pl->GiveNamedItem("weapon_ak47");
		pl->GiveAmmo(90, "762Nato", 90);
		// the nearest wall in one of 8 directions, 100..400 units away
		Vector eye = pl->pev->origin + pl->pev->view_ofs;
		float best = 1e9f, bestYaw = 0.0f;
		for (int k = 0; k < 8; k++)
		{
			float yaw = k * 45.0f * (float)M_PI / 180.0f;
			float s[3] = {eye.x, eye.y, eye.z}, e[3] = {eye.x + cosf(yaw) * 400.0f, eye.y + sinf(yaw) * 400.0f, eye.z}, mn[3] = {0, 0, 0};
			mcw::Trace tr;
			mcm::WorldTrace(s, e, mn, mn, tr);
			float d = tr.fraction * 400.0f;
			if (tr.hit && d > 100.0f && d < best)
			{
				best = d;
				bestYaw = k * 45.0f;
			}
		}
		pl->pev->angles = pl->pev->v_angle = Vector(5.0f, bestYaw, 0);
		pl->pev->fixangle = 1;
		float s[3] = {eye.x, eye.y, eye.z}, e[3] = {eye.x + cosf(bestYaw * (float)M_PI / 180.0f) * 400.0f, eye.y + sinf(bestYaw * (float)M_PI / 180.0f) * 400.0f, eye.z - 35.0f};
		const char* tex = mcm::WorldTraceTexture(s, e);
		McLog("test shoot: wall %.0f units away at yaw %.0f, texture %s", best, bestYaw, tex ? tex : "(none)");
	}
	if (Hit(1.0f))
		CLIENT_COMMAND(pl->edict(), (char*)"weapon_ak47\n");
	if (Hit(1.8f))
		McLog("KEYS hold:700");
	if (Hit(2.1f))
		McLog("SHOT shoot_a");
	if (Hit(2.5f))
		McLog("SHOT shoot_b");
	if (Hit(2.8f))
	{
		CBasePlayerWeapon* w = (CBasePlayerWeapon*)(CBasePlayerItem*)pl->m_pActiveItem;
		McLog("test shoot: active %s clip %d", w ? STRING(w->pev->classname) : "none", w ? w->m_iClip : -1);
	}
	if (Hit(3.2f))
		McLog("SHOT end");
}

// The hotbar holds anything: move the pistol to storage and a sword into slot 2, press the real "2"
// key (selects the sword), then Shift+B opens the Minecraft item menu.
static void LogHotbar(CBasePlayer* pl, const char* when)
{
	McPlayer& mp = P(pl);
	char buf[256] = "";
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
	{
		const mci::Stack& s = mp.hotbar[i];
		char one[40];
		if (s.Empty())
			snprintf(one, sizeof(one), "%d:- ", i + 1);
		else if (mci::IsCsToken(s.id))
			snprintf(one, sizeof(one), "%d:gun%d ", i + 1, s.id - mci::CS_TOKEN_BASE);
		else
			snprintf(one, sizeof(one), "%d:%s ", i + 1, mci::Item(s.id).name);
		strncat(buf, one, sizeof(buf) - strlen(buf) - 1);
	}
	CBasePlayerItem* a = pl->m_pActiveItem;
	McLog("test hotbar %s: %s| selected %d, active %s, mc %d", when, buf, mp.selected + 1, a ? STRING(a->pev->classname) : "-",
		(int)mp.mcItemActive);
}
static void ScenarioHotbar(CBasePlayer* pl)
{
	pl->pev->takedamage = DAMAGE_NO;
	auto cl = [&](const char* c) { CLIENT_COMMAND(pl->edict(), (char*)c); };
	if (Hit(0.5f))
	{
		GiveItem(pl, mci::FindItem("diamond_sword"), 1, false);
		LogHotbar(pl, "start");
	}
	if (Hit(1.0f))
		cl("mc_inventory\n");
	// pistol (hotbar 2 = slot 37) -> storage slot 9
	if (Hit(1.4f)) cl("mc_guicursor invslot 37\n");
	if (Hit(1.6f)) cl("mc_guicursor click 0 0\n");
	if (Hit(1.8f)) cl("mc_guicursor invslot 9\n");
	if (Hit(2.0f)) cl("mc_guicursor click 0 0\n");
	if (Hit(2.4f))
	{
		// the sword, wherever it is, -> hotbar 2
		McPlayer& mp = P(pl);
		int from = -1;
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
			if (mp.hotbar[i].id == mci::FindItem("diamond_sword"))
				from = 36 + i;
		for (int i = 0; i < 27 && from < 0; i++)
			if (mp.inv[i].id == mci::FindItem("diamond_sword"))
				from = 9 + i;
		char c[48];
		snprintf(c, sizeof(c), "mc_guicursor invslot %d\n", from);
		cl(c);
	}
	if (Hit(2.6f)) cl("mc_guicursor click 0 0\n");
	if (Hit(2.8f)) cl("mc_guicursor invslot 37\n");
	if (Hit(3.0f)) cl("mc_guicursor click 0 0\n");
	if (Hit(3.4f))
	{
		LogHotbar(pl, "rearranged");
		McLog("SHOT hotbar_inv");
	}
	if (Hit(4.0f))
		cl("mc_inventory\n");
	if (Hit(4.6f))
		cl("slot2\n"); // what the 2 key runs
	if (Hit(5.4f))
	{
		LogHotbar(pl, "after key 2");
		McLog("SHOT hotbar_sword");
	}
	if (Hit(5.8f))
		cl("slot3\n");
	if (Hit(6.6f))
		LogHotbar(pl, "after key 3");
	if (Hit(7.0f))
		cl("mc_itemmenu\n"); // what Shift+B runs
	if (Hit(7.8f))
		McLog("SHOT hotbar_shiftb");
	if (Hit(8.4f))
		cl("mc_itemmenu\n");
	if (Hit(9.0f))
		McLog("SHOT end");
}

// TNT going off among bots (the case that froze a real game): two bots are put next to primed TNT in front
// of the player; the blast kills them. The game must keep running.
static void ScenarioTntBots(CBasePlayer* pl)
{
	pl->pev->takedamage = DAMAGE_NO;
	static Vector spot;
	if (Hit(0.5f))
	{
		UTIL_MakeVectors(Vector(0, pl->pev->angles.y, 0));
		spot = pl->pev->origin;
		// a floor spot ~120 units ahead in open space
		for (int k = 0; k < 8; k++)
		{
			float a = k * 45.0f * (float)M_PI / 180.0f;
			float s[3] = {pl->pev->origin.x, pl->pev->origin.y, pl->pev->origin.z};
			float e[3] = {s[0] + cosf(a) * 160.0f, s[1] + sinf(a) * 160.0f, s[2]}, mn[3] = {-16, -16, -18}, mx[3] = {16, 16, 18};
			mcw::Trace tr;
			mcm::WorldTrace(s, e, mn, mx, tr);
			if (!tr.hit)
			{
				spot = Vector(s[0] + cosf(a) * 140.0f, s[1] + sinf(a) * 140.0f, s[2]);
				break;
			}
		}
		int moved = 0;
		for (int i = 1; i <= gpGlobals->maxClients && moved < 2; i++)
		{
			CBasePlayer* b = UTIL_PlayerByIndex(i);
			if (!b || b == pl || !b->IsAlive() || !b->IsBot())
				continue;
			UTIL_SetOrigin(b->pev, spot + Vector(moved ? 30.0f : -30.0f, 0, 0));
			b->pev->velocity = g_vecZero;
			McLog("test tntbots: moved %s next to the TNT", STRING(b->pev->netname));
			moved++;
		}
		float o[3] = {spot.x, spot.y, spot.z - 30.0f};
		PrimeTnt(o, 30);
		// full netherite: an AK bullet (36 damage) does 1
		McPlayer& mp = P(pl);
		static const char* parts[4] = {"netherite_helmet", "netherite_chestplate", "netherite_leggings", "netherite_boots"};
		for (int i = 0; i < 4; i++)
		{
			mp.armor[i].id = (uint16_t)mci::FindItem(parts[i]);
			mp.armor[i].count = 1;
			mp.armor[i].damage = 0;
		}
		pl->pev->takedamage = DAMAGE_YES;
		static const char* sets[] = {"netherite", "diamond", "iron", "leather"};
		for (const char* m : sets)
		{
			static const char* slot[4] = {"helmet", "chestplate", "leggings", "boots"};
			for (int i = 0; i < 4; i++)
			{
				char nm[48];
				Q_snprintf(nm, sizeof(nm), "%s_%s", m, slot[i]);
				mp.armor[i].id = (uint16_t)mci::FindItem(nm);
				mp.armor[i].count = 1;
				mp.armor[i].damage = 0;
			}
			pl->pev->health = 100.0f;
			pl->TakeDamage(VARS(INDEXENT(0)), VARS(INDEXENT(0)), 36.0f, DMG_BULLET);
			float afterBullet = pl->pev->health;
			pl->pev->health = 100.0f;
			pl->TakeDamage(VARS(INDEXENT(0)), VARS(INDEXENT(0)), 80.0f, DMG_BLAST);
			McLog("test tntbots: full %s: 36-damage bullet -> %.2f HP left, 80-damage blast -> %.2f HP left", m, afterBullet, pl->pev->health);
		}
		pl->pev->health = 100.0f;
		pl->pev->takedamage = DAMAGE_NO;
	}
	for (int k = 0; k < 10; k++)
		if (Hit(2.0f + k * 1.5f))
		{
			int alive = 0;
			for (int i = 1; i <= gpGlobals->maxClients; i++)
			{
				CBasePlayer* b = UTIL_PlayerByIndex(i);
				alive += (b && b->IsAlive() && b->IsBot()) ? 1 : 0;
			}
			McLog("test tntbots: t=%.1f bots alive %d frametime %.4f", 2.0f + k * 1.5f, alive, gpGlobals->frametime);
		}
	if (Hit(3.0f))
		McLog("SHOT tntbots_after");
	if (Hit(17.5f))
		McLog("SHOT end");
}

void TestMapChanged()
{
	g_testStart = -1.0f;
}

void TestFrame()
{
	const char* name = cv_testscript.string;
	if (!name || !name[0])
	{
		g_testStart = -1.0f;
		return;
	}
	CBasePlayer* pl = Human();
	if (!pl)
		return;
	if (g_testStart < 0.0f || strcmp(g_testName, name))
	{
		Q_strlcpy(g_testName, name);
		g_testStart = gpGlobals->time + 1.0f;
		if (g_testStart < 7.0f)
			g_testStart = 7.0f; // after CS's "Game Commencing" restart (respawns everyone)
		g_prevNow = -1.0f;
		McLog("test: starting scenario '%s'", name);
	}
	if (gpGlobals->time >= g_testStart && g_prevNow < 0.0f)
		McLog("test: running"); // the timeline starts now (recorders key off this)
	if (!strcmp(name, "combat"))
		ScenarioCombat(pl);
	else if (!strcmp(name, "elytra"))
		ScenarioElytra(pl);
	else if (!strcmp(name, "mine"))
		ScenarioMine(pl);
	else if (!strcmp(name, "tnt"))
		ScenarioTnt(pl);
	else if (!strcmp(name, "tour"))
		ScenarioTour(pl);
	else if (!strcmp(name, "model"))
		ScenarioModel(pl);
	else if (!strcmp(name, "items"))
		ScenarioItems(pl);
	else if (!strcmp(name, "creeper"))
		ScenarioCreeper(pl);
	else if (!strcmp(name, "creeperwatch"))
		ScenarioCreeperWatch(pl);
	else if (!strcmp(name, "changelevel"))
		ScenarioChangelevel(pl);
	else if (!strcmp(name, "gui"))
		ScenarioGui(pl);
	else if (!strcmp(name, "enderman"))
		ScenarioEnderman(pl);
	else if (!strcmp(name, "flight"))
		ScenarioFlight(pl);
	else if (!strcmp(name, "views"))
		ScenarioViews(pl);
	else if (!strcmp(name, "hand"))
		ScenarioHand(pl);
	else if (!strcmp(name, "pit"))
		ScenarioPit(pl);
	else if (!strcmp(name, "place"))
		ScenarioPlace(pl);
	else if (!strcmp(name, "inv"))
		ScenarioInv(pl);
	else if (!strcmp(name, "food"))
		ScenarioFood(pl);
	else if (!strcmp(name, "redstone"))
		ScenarioRedstone(pl);
	else if (!strcmp(name, "shoot"))
		ScenarioShoot(pl);
	else if (!strcmp(name, "hotbar"))
		ScenarioHotbar(pl);
	else if (!strcmp(name, "tntbots"))
		ScenarioTntBots(pl);
	g_prevNow = gpGlobals->time - g_testStart;
}
} // namespace mc
