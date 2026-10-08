// Client-side Minecraft layer: routes the proxy's hooks to the modules.
#include "mc_client.h"
#include "mc_blocks.h"
#include "mc_gl.h"
#include "mc_move.h"
#include "mc_state.h"

#include <math.h>
#include <stdlib.h>

namespace mc
{
ClientState g_cl;
static char g_lastMap[128] = "";

float RandF(float lo, float hi) { return lo + (hi - lo) * (rand() / (float)RAND_MAX); }

void AngleVectors3(const float angles[3], float f[3], float r[3], float u[3])
{
	const float d2r = 3.14159265358979f / 180.0f;
	float sy = sinf(angles[1] * d2r), cy = cosf(angles[1] * d2r);
	float sp = sinf(angles[0] * d2r), cp = cosf(angles[0] * d2r);
	float sr = sinf(angles[2] * d2r), cr = cosf(angles[2] * d2r);
	if (f)
	{
		f[0] = cp * cy;
		f[1] = cp * sy;
		f[2] = -sp;
	}
	if (r)
	{
		r[0] = -sr * sp * cy + cr * sy;
		r[1] = -sr * sp * sy - cr * cy;
		r[2] = -sr * cp;
	}
	if (u)
	{
		u[0] = cr * sp * cy + sr * sy;
		u[1] = cr * sp * sy - sr * cy;
		u[2] = cr * cp;
	}
}

int HeldItemId()
{
	if (!g_cl.mcItemActive || g_cl.selected < 0 || g_cl.selected >= mcp::HOTBAR_SIZE)
		return 0;
	int id = g_cl.hotbarId[g_cl.selected];
	return id > 0 ? id : 0;
}

const mci::ItemDef* HeldItem()
{
	int id = HeldItemId();
	return id ? &mci::Item(id) : nullptr;
}

void OnInitialize()
{
	Log("OnInitialize: game dir '%s'", gEngfuncs.pfnGetGameDirectory ? gEngfuncs.pfnGetGameDirectory() : "?");
	mcw::InitBlockRegistry();
	InputInit();
}

void OnHudInit()
{
	extern void GuiInit();
	GuiInit();
	Log("OnHudInit");
	NetInit();
	HudInit();
	WorldInit();
	extern void MusicInit();
	MusicInit();
	// Minecraft hotbar replaces CS's weapon selection popup
	gEngfuncs.Cvar_SetValue((char*)"hud_fastswitch", 1.0f);
}

void OnVidInit()
{
	Log("OnVidInit");
	HudVidInit();
	SCREENINFO si;
	si.iSize = sizeof(si);
	gEngfuncs.pfnGetScreenInfo(&si);
	g_cl.screenW = si.iWidth;
	g_cl.screenH = si.iHeight;
}

static void CheckMap()
{
	const char* lvl = gEngfuncs.pfnGetLevelName ? gEngfuncs.pfnGetLevelName() : "";
	if (!lvl)
		lvl = "";
	if (strcmp(lvl, g_lastMap))
	{
		strncpy(g_lastMap, lvl, sizeof(g_lastMap) - 1);
		WorldUnload();
		if (lvl[0])
		{
			char name[128];
			const char* base = strrchr(lvl, '/');
			base = base ? base + 1 : lvl;
			strncpy(name, base, sizeof(name) - 1);
			name[sizeof(name) - 1] = 0;
			char* dot = strrchr(name, '.');
			if (dot)
				*dot = 0;
			WorldLoadForMap(name);
		}
	}
}

void OnRedraw(float time, int intermission)
{
	if (!mcgl::Init())
		return;
	SCREENINFO si;
	si.iSize = sizeof(si);
	gEngfuncs.pfnGetScreenInfo(&si);
	g_cl.screenW = si.iWidth;
	g_cl.screenH = si.iHeight;
	mcgl::StateGuard guard;
	extern void GuiDraw();
	extern void GuiHide();
	if (!intermission)
	{
		HandDraw();
		HudDraw();
		if (g_cl.health <= 0)
			GuiHide();
		GuiDraw();
	}
	else
		GuiHide();
}

void DrawCsModelItems(); // mc_anim_cl.cpp
void OnDrawNormalTriangles()
{
	if (!mcgl::Init())
		return;
	CheckMap();
	mcgl::StateGuard guard;
	WorldDraw();
	EntDrawAll();
	extern void PlayersDrawAll();
	PlayersDrawAll();
	DrawCsModelItems(); // Minecraft items in Counter-Strike models' hands
}

void OnDrawTransparentTriangles()
{
	if (!mcgl::Init())
		return;
	mcgl::StateGuard guard;
	extern void WorldDrawClouds();
	WorldDrawClouds();
	ParticlesDraw();
}

void OnPlayerMoveInit(playermove_t* pm)
{
	Log("OnPlayerMoveInit");
	mcm::InstallTraceWrappers(pm);
}

void OnPrePlayerMove(playermove_t* pm, int server)
{
	mcm::InstallTraceWrappers(pm);
}

void OnPostPlayerMove(playermove_t* pm, int server)
{
	g_cl.flags = pm->iuser4;
	g_cl.boost = pm->vuser1[0];
	g_cl.onGround = pm->onground != -1;
}

// Elytra: the engine calls HUD_PlayerMove for prediction; when gliding we must skip CS's own
// movement entirely (see proxy). Exposed so the proxy can ask.
bool MovementOverride(playermove_t* pm)
{
	mcm::InstallTraceWrappers(pm);
	return mcm::PreMove(pm);
}

void OnFrame(double frametime)
{
	// HUD_Frame is handed the frame's duration, not a timestamp: the clock comes from the engine
	double now = gEngfuncs.GetClientTime();
	float dt = (float)(now - g_cl.time);
	if (dt < 0.0f || dt > 0.5f)
		dt = (float)frametime;
	if (dt < 0.0f || dt > 0.5f)
		dt = 0.0f;
	g_cl.time = now;
	g_cl.frametime = dt;
	ParticlesFrame(dt);
	SoundFrame();
	extern void MusicFrame();
	MusicFrame();
}

bool GuiKey(int down, int keynum, const char* binding);
bool OnKeyEvent(int down, int keynum, const char* binding)
{
	if (!GuiKey(down, keynum, binding))
		return false;
	return InputKey(down, keynum, binding);
}

// Minecraft's camera perspectives (F5 cycles): 0 first person, 1 third person behind, 2 third person in
// front looking back. The CS client's own "thirdperson" is refused in this build, so the camera is ours.
static int g_perspective = 0;
static float g_camDist = 0.0f; // how far the F5 camera ended up from the eye (blocks pull it in)
bool PerspectiveIsThirdPerson() { return g_perspective != 0; }
float PerspectiveCameraDistance() { return g_perspective ? g_camDist : 0.0f; }
void PerspectiveSet(int v)
{
	g_perspective = (v % 3 + 3) % 3;
	Log("camera: perspective %d", g_perspective);
}
void PerspectiveCycle() { PerspectiveSet(g_perspective + 1); }

void OnCalcRefdef(ref_params_t* p)
{
	// the player's own eye and look direction (block picking, hand) whatever the camera does next
	for (int i = 0; i < 3; i++)
	{
		g_cl.vieworg[i] = p->vieworg[i];
		g_cl.viewangles[i] = p->viewangles[i];
		g_cl.forward[i] = p->forward[i];
		g_cl.right[i] = p->right[i];
		g_cl.up[i] = p->up[i];
	}
	cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	if (g_perspective && local && !local->curstate.iuser1 && !p->intermission)
	{
		// Camera.setup: 4 blocks out along the view (behind, or in front facing back), stopped by blocks
		float eye[3] = {p->vieworg[0], p->vieworg[1], p->vieworg[2]};
		float sign = g_perspective == 1 ? -1.0f : 1.0f;
		float cam[3];
		for (int i = 0; i < 3; i++)
			cam[i] = eye[i] + p->forward[i] * 160.0f * sign;
		const mcw::World* w = WorldGet();
		if (w)
		{
			static const float mn[3] = {-4, -4, -4}, mx[3] = {4, 4, 4};
			mcw::Trace tr;
			mcm::WorldTrace(eye, cam, mn, mx, tr);
			if (tr.hit || tr.startsolid)
				for (int i = 0; i < 3; i++)
					cam[i] = tr.startsolid ? eye[i] : tr.endpos[i];
		}
		float d2 = 0.0f;
		for (int i = 0; i < 3; i++)
		{
			d2 += (cam[i] - eye[i]) * (cam[i] - eye[i]);
			p->vieworg[i] = cam[i];
		}
		g_camDist = sqrtf(d2);
		if (g_perspective == 2)
		{
			p->viewangles[0] = -p->viewangles[0];
			p->viewangles[1] += 180.0f;
		}
		gEngfuncs.pfnAngleVectors(p->viewangles, p->forward, p->right, p->up);
	}
	else if (local && CL_IsThirdPersonLocal())
	{
		// CS's own third-person camera only collides with the (empty) BSP shell: pull it in front of voxels
		const mcw::World* w = WorldGet();
		float eye[3] = {local->origin[0], local->origin[1], local->origin[2] + 17.0f};
		static const float mn[3] = {-4, -4, -4}, mx[3] = {4, 4, 4};
		mcw::Trace tr;
		if (w)
		{
			mcm::WorldTrace(eye, p->vieworg, mn, mx, tr);
			if (tr.hit || tr.startsolid)
				for (int i = 0; i < 3; i++)
					p->vieworg[i] = tr.startsolid ? eye[i] : tr.endpos[i];
		}
	}
	// Minecraft damage tilt (GameRenderer.bobHurt): sin(f^4 * pi) * 14 degrees of roll
	static int lastHealth = 100;
	static double hurtAt = -100.0;
	int hp = (int)p->health;
	if (hp < lastHealth && hp > 0)
		hurtAt = g_cl.time;
	lastHealth = hp;
	double ht = g_cl.time - hurtAt;
	if (ht >= 0 && ht < 0.5 && !p->intermission)
	{
		float f = (float)(1.0 - ht / 0.5);
		f = sinf(f * f * f * f * 3.14159265f);
		p->viewangles[2] -= f * 14.0f;
	}
	if (hp <= 0 && g_cl.health > 0)
		g_cl.deathTime = g_cl.time;
	g_cl.health = hp;
}

bool OnAddEntity(int type, cl_entity_t* ent, const char* modelname)
{
	if (EntIsMc(ent))
		return false; // drawn by our renderer
	extern bool PlayerTakeOver(cl_entity_t * ent);
	if (type == ET_PLAYER && PlayerTakeOver(ent))
	{
		static cvar_t* cv = gEngfuncs.pfnGetCvarPointer("mc_players");
		return cv && cv->value == 2.0f; // 2 = debug: draw the CS model too
	}
	return true;
}

void GuiPreCreateMove();
void GuiCreateMove(usercmd_t* cmd);
void OnPreCreateMove() { GuiPreCreateMove(); }
void OnMouseDeactivate()
{
	// Esc never reaches HUD_Key_Event: the engine opens its menu and releases the mouse instead
	extern void GuiHide();
	GuiHide();
}
void OnCreateMove(float frametime, usercmd_t* cmd, int active)
{
	GuiCreateMove(cmd);
	InputCreateMove(frametime, cmd, active);
	// item use (right mouse: +attack2, or +alt1 in HL25's default binds) drives the bow/eat animations
	bool use = (cmd->buttons & ((1 << 11) | (1 << 14))) != 0 && g_cl.mcItemActive && g_cl.health > 0;
	if (use && !g_cl.useHeld)
		g_cl.useStart = g_cl.time;
	g_cl.useHeld = use;
}

void OnUpdateClientData(client_data_t* cdata, float time)
{
	if (cdata)
		g_cl.fov = cdata->fov > 0 ? cdata->fov : 90.0f;
}

void OnShutdown() { Log("OnShutdown"); }
} // namespace mc
