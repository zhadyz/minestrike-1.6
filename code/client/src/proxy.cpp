// Client proxy: the engine loads this client.dll from csmc/cl_dlls. It loads the official
// Counter-Strike client (copied next to it as cs_client.dll), forwards every export to it,
// and wraps the handful of entry points the Minecraft layer needs.
#include "hlsdk_client.h"
#include "mc_client.h"

#include <string.h>

cl_enginefunc_t* g_pEngfuncs = nullptr;
cl_enginefunc_t gEngfuncs;

static HMODULE g_hOrig = nullptr;
static cldll_func_t g_orig;
static bool g_tableLoaded = false;

static HMODULE SelfModule()
{
	HMODULE h = nullptr;
	GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
		(LPCSTR)&SelfModule, &h);
	return h;
}

static bool LoadLib()
{
	if (g_hOrig)
		return true;

	char path[MAX_PATH];
	GetModuleFileNameA(SelfModule(), path, sizeof(path));
	char* slash = strrchr(path, '\\');
	if (!slash)
		return false;
	strcpy(slash + 1, "cs_client.dll");

	g_hOrig = LoadLibraryA(path);
	if (!g_hOrig)
	{
		mc::Log("proxy: failed to load %s (error %lu)", path, GetLastError());
		return false;
	}
	mc::Log("proxy: loaded %s", path);
	return true;
}

// Used only if the engine resolves exports one by one instead of calling F.
static bool LoadOriginal()
{
	if (g_tableLoaded)
		return true;
	if (!LoadLib())
		return false;

#define RESOLVE(field, name) g_orig.field = (decltype(g_orig.field))GetProcAddress(g_hOrig, name)
	RESOLVE(pInitFunc, "Initialize");
	RESOLVE(pHudInitFunc, "HUD_Init");
	RESOLVE(pHudVidInitFunc, "HUD_VidInit");
	RESOLVE(pHudRedrawFunc, "HUD_Redraw");
	RESOLVE(pHudUpdateClientDataFunc, "HUD_UpdateClientData");
	RESOLVE(pHudResetFunc, "HUD_Reset");
	RESOLVE(pClientMove, "HUD_PlayerMove");
	RESOLVE(pClientMoveInit, "HUD_PlayerMoveInit");
	RESOLVE(pClientTextureType, "HUD_PlayerMoveTexture");
	RESOLVE(pIN_ActivateMouse, "IN_ActivateMouse");
	RESOLVE(pIN_DeactivateMouse, "IN_DeactivateMouse");
	RESOLVE(pIN_MouseEvent, "IN_MouseEvent");
	RESOLVE(pIN_ClearStates, "IN_ClearStates");
	RESOLVE(pIN_Accumulate, "IN_Accumulate");
	RESOLVE(pCL_CreateMove, "CL_CreateMove");
	RESOLVE(pCL_IsThirdPerson, "CL_IsThirdPerson");
	RESOLVE(pCL_GetCameraOffsets, "CL_CameraOffset");
	RESOLVE(pFindKey, "KB_Find");
	RESOLVE(pCamThink, "CAM_Think");
	RESOLVE(pCalcRefdef, "V_CalcRefdef");
	RESOLVE(pAddEntity, "HUD_AddEntity");
	RESOLVE(pCreateEntities, "HUD_CreateEntities");
	RESOLVE(pDrawNormalTriangles, "HUD_DrawNormalTriangles");
	RESOLVE(pDrawTransparentTriangles, "HUD_DrawTransparentTriangles");
	RESOLVE(pStudioEvent, "HUD_StudioEvent");
	RESOLVE(pPostRunCmd, "HUD_PostRunCmd");
	RESOLVE(pShutdown, "HUD_Shutdown");
	RESOLVE(pTxferLocalOverrides, "HUD_TxferLocalOverrides");
	RESOLVE(pProcessPlayerState, "HUD_ProcessPlayerState");
	RESOLVE(pTxferPredictionData, "HUD_TxferPredictionData");
	RESOLVE(pReadDemoBuffer, "Demo_ReadBuffer");
	RESOLVE(pConnectionlessPacket, "HUD_ConnectionlessPacket");
	RESOLVE(pGetHullBounds, "HUD_GetHullBounds");
	RESOLVE(pHudFrame, "HUD_Frame");
	RESOLVE(pKeyEvent, "HUD_Key_Event");
	RESOLVE(pTempEntUpdate, "HUD_TempEntUpdate");
	RESOLVE(pGetUserEntity, "HUD_GetUserEntity");
	RESOLVE(pVoiceStatus, "HUD_VoiceStatus");
	RESOLVE(pDirectorMessage, "HUD_DirectorMessage");
	RESOLVE(pStudioInterface, "HUD_GetStudioModelInterface");
	RESOLVE(pChatInputPosition, "HUD_ChatInputPosition");
	RESOLVE(pGetPlayerTeam, "HUD_GetPlayerTeam");
	RESOLVE(pClientFactory, "ClientFactory");
#undef RESOLVE
	g_tableLoaded = true;
	mc::Log("proxy: resolved original exports individually");
	return true;
}

// ---------------------------------------------------------------------------------------------
// Wrapped entry points

namespace mc
{
pfnEngSrc_pfnAddCommand_t InputAddCommandHook();
pfnEngSrc_pfnHookUserMsg_t InputHookUserMsgHook(pfnEngSrc_pfnHookUserMsg_t real);
void InstallEventApi(cl_enginefunc_t* e);
}

static int Hook_Initialize(cl_enginefunc_t* pEnginefuncs, int iVersion)
{
	g_pEngfuncs = pEnginefuncs;
	memcpy(&gEngfuncs, pEnginefuncs, sizeof(cl_enginefunc_t));
	mc::OnInitialize();
	// The official client copies the engine table during Initialize and registers its commands later
	// (HUD_Init) through that copy: route its pfnAddCommand through our hotbar interception.
	pfnEngSrc_pfnAddCommand_t real = pEnginefuncs->pfnAddCommand;
	pfnEngSrc_pfnHookUserMsg_t realHook = pEnginefuncs->pfnHookUserMsg;
	pEnginefuncs->pfnAddCommand = mc::InputAddCommandHook();
	pEnginefuncs->pfnHookUserMsg = mc::InputHookUserMsgHook(realHook);
	// the client keeps the event API pointer: its bullet traces then see the mod world
	mc::InstallEventApi(pEnginefuncs);
	int r = g_orig.pInitFunc(pEnginefuncs, iVersion);
	pEnginefuncs->pfnAddCommand = real;
	pEnginefuncs->pfnHookUserMsg = realHook;
	return r;
}

// Third person is CS's camera or our own Minecraft perspective (F5); the engine asks this to decide
// whether to draw the local player.
int CL_IsThirdPersonLocal() { return mc::PerspectiveIsThirdPerson() || (g_orig.pCL_IsThirdPerson && g_orig.pCL_IsThirdPerson()); }
static int Hook_CL_IsThirdPerson() { return CL_IsThirdPersonLocal(); }

static void Hook_HUD_Init()
{
	g_orig.pHudInitFunc();
	mc::OnHudInit();
}

static int Hook_HUD_VidInit()
{
	int r = g_orig.pHudVidInitFunc();
	mc::OnVidInit();
	return r;
}

static int Hook_HUD_Redraw(float time, int intermission)
{
	int r = g_orig.pHudRedrawFunc(time, intermission);
	mc::OnRedraw(time, intermission);
	return r;
}

static int Hook_HUD_UpdateClientData(client_data_t* cdata, float time)
{
	int r = g_orig.pHudUpdateClientDataFunc(cdata, time);
	mc::OnUpdateClientData(cdata, time);
	return r;
}

static void Hook_HUD_PlayerMove(struct playermove_s* ppmove, int server)
{
	// Minecraft movement (elytra) replaces CS movement entirely for this command when active
	if (!mc::MovementOverride(ppmove))
		g_orig.pClientMove(ppmove, server);
	mc::OnPostPlayerMove(ppmove, server);
}

static void Hook_HUD_PlayerMoveInit(struct playermove_s* ppmove)
{
	g_orig.pClientMoveInit(ppmove);
	mc::OnPlayerMoveInit(ppmove);
}

static void Hook_CL_CreateMove(float frametime, struct usercmd_s* cmd, int active)
{
	mc::OnPreCreateMove();
	g_orig.pCL_CreateMove(frametime, cmd, active);
	mc::OnCreateMove(frametime, cmd, active);
}

static void Hook_V_CalcRefdef(struct ref_params_s* pparams)
{
	g_orig.pCalcRefdef(pparams);
	mc::OnCalcRefdef(pparams);
}

static int Hook_HUD_AddEntity(int type, struct cl_entity_s* ent, const char* modelname)
{
	int r = g_orig.pAddEntity(type, ent, modelname);
	if (!mc::OnAddEntity(type, ent, modelname))
		return 0;
	return r;
}

static void Hook_HUD_DrawNormalTriangles()
{
	g_orig.pDrawNormalTriangles();
	mc::OnDrawNormalTriangles();
}

static void Hook_HUD_DrawTransparentTriangles()
{
	g_orig.pDrawTransparentTriangles();
	mc::OnDrawTransparentTriangles();
}

static void Hook_HUD_Shutdown()
{
	mc::OnShutdown();
	g_orig.pShutdown();
}

static void Hook_HUD_Frame(double time)
{
	g_orig.pHudFrame(time);
	mc::OnFrame(time);
}

static int Hook_HUD_Key_Event(int down, int keynum, const char* binding)
{
	if (!mc::OnKeyEvent(down, keynum, binding))
		return 0;
	return g_orig.pKeyEvent(down, keynum, binding);
}

static void Hook_IN_DeactivateMouse()
{
	mc::OnMouseDeactivate(); // the engine menu or console took the mouse
	g_orig.pIN_DeactivateMouse();
}

static void FillTable(cldll_func_t* t)
{
	*t = g_orig;
	t->pInitFunc = Hook_Initialize;
	t->pHudInitFunc = Hook_HUD_Init;
	t->pHudVidInitFunc = Hook_HUD_VidInit;
	t->pHudRedrawFunc = Hook_HUD_Redraw;
	t->pHudUpdateClientDataFunc = Hook_HUD_UpdateClientData;
	t->pClientMove = Hook_HUD_PlayerMove;
	t->pClientMoveInit = Hook_HUD_PlayerMoveInit;
	t->pCL_CreateMove = Hook_CL_CreateMove;
	t->pCalcRefdef = Hook_V_CalcRefdef;
	t->pAddEntity = Hook_HUD_AddEntity;
	t->pDrawNormalTriangles = Hook_HUD_DrawNormalTriangles;
	t->pDrawTransparentTriangles = Hook_HUD_DrawTransparentTriangles;
	t->pShutdown = Hook_HUD_Shutdown;
	t->pHudFrame = Hook_HUD_Frame;
	t->pKeyEvent = Hook_HUD_Key_Event;
	t->pIN_DeactivateMouse = Hook_IN_DeactivateMouse;
	t->pCL_IsThirdPerson = Hook_CL_IsThirdPerson;
}

static cldll_func_t g_hooked;
static cldll_func_t* Table()
{
	if (!LoadOriginal())
		return nullptr;
	static bool filled = false;
	if (!filled)
	{
		FillTable(&g_hooked);
		filled = true;
	}
	return &g_hooked;
}

// ---------------------------------------------------------------------------------------------
// Exports

// The engine pre-fills some slots of the table it passes to F (the destination-address and
// module-function hacks from APIProxy.h), and the official client reads them before writing its
// exports. So the original F must see the engine's own buffer; we patch our hooks in afterwards.
extern "C" __declspec(dllexport) void F(void* pv)
{
	if (!LoadLib())
		return;
	typedef void (*F_t)(void*);
	F_t origF = (F_t)GetProcAddress(g_hOrig, "F");
	if (!origF)
	{
		mc::Log("proxy: original client has no F export");
		return;
	}
	origF(pv);
	g_orig = *(cldll_func_t*)pv;
	g_tableLoaded = true;
	FillTable((cldll_func_t*)pv);
	mc::Log("proxy: F hooked");
}

extern "C" __declspec(dllexport) void* CreateInterface(const char* name, int* ret)
{
	if (!LoadLib())
		return nullptr;
	typedef void* (*CI_t)(const char*, int*);
	CI_t ci = (CI_t)GetProcAddress(g_hOrig, "CreateInterface");
	return ci ? ci(name, ret) : nullptr;
}

extern "C" __declspec(dllexport) void* ClientFactory()
{
	if (!LoadLib())
		return nullptr;
	return (void*)GetProcAddress(g_hOrig, "CreateInterface");
}

// Individual exports, for engines that resolve the client API one symbol at a time.
#define FWD(ret, name, field, params, args) \
	extern "C" __declspec(dllexport) ret name params { return Table()->field args; }

FWD(int, Initialize, pInitFunc, (cl_enginefunc_t* e, int v), (e, v))
FWD(void, HUD_Init, pHudInitFunc, (void), ())
FWD(int, HUD_VidInit, pHudVidInitFunc, (void), ())
FWD(int, HUD_Redraw, pHudRedrawFunc, (float t, int i), (t, i))
FWD(int, HUD_UpdateClientData, pHudUpdateClientDataFunc, (client_data_t* c, float t), (c, t))
FWD(void, HUD_Reset, pHudResetFunc, (void), ())
FWD(void, HUD_PlayerMove, pClientMove, (struct playermove_s* p, int s), (p, s))
FWD(void, HUD_PlayerMoveInit, pClientMoveInit, (struct playermove_s* p), (p))
FWD(char, HUD_PlayerMoveTexture, pClientTextureType, (char* n), (n))
FWD(void, IN_ActivateMouse, pIN_ActivateMouse, (void), ())
FWD(void, IN_DeactivateMouse, pIN_DeactivateMouse, (void), ())
FWD(void, IN_MouseEvent, pIN_MouseEvent, (int m), (m))
FWD(void, IN_ClearStates, pIN_ClearStates, (void), ())
FWD(void, IN_Accumulate, pIN_Accumulate, (void), ())
FWD(void, CL_CreateMove, pCL_CreateMove, (float f, struct usercmd_s* c, int a), (f, c, a))
FWD(int, CL_IsThirdPerson, pCL_IsThirdPerson, (void), ())
FWD(void, CL_CameraOffset, pCL_GetCameraOffsets, (float* o), (o))
FWD(struct kbutton_s*, KB_Find, pFindKey, (const char* n), (n))
FWD(void, CAM_Think, pCamThink, (void), ())
FWD(void, V_CalcRefdef, pCalcRefdef, (struct ref_params_s* p), (p))
FWD(int, HUD_AddEntity, pAddEntity, (int t, struct cl_entity_s* e, const char* m), (t, e, m))
FWD(void, HUD_CreateEntities, pCreateEntities, (void), ())
FWD(void, HUD_DrawNormalTriangles, pDrawNormalTriangles, (void), ())
FWD(void, HUD_DrawTransparentTriangles, pDrawTransparentTriangles, (void), ())
FWD(void, HUD_StudioEvent, pStudioEvent, (const struct mstudioevent_s* e, const struct cl_entity_s* ent), (e, ent))
FWD(void, HUD_PostRunCmd, pPostRunCmd,
	(struct local_state_s* f, struct local_state_s* t, struct usercmd_s* c, int r, double tm, unsigned int s),
	(f, t, c, r, tm, s))
FWD(void, HUD_Shutdown, pShutdown, (void), ())
FWD(void, HUD_TxferLocalOverrides, pTxferLocalOverrides, (struct entity_state_s* s, const struct clientdata_s* c), (s, c))
FWD(void, HUD_ProcessPlayerState, pProcessPlayerState, (struct entity_state_s* d, const struct entity_state_s* s), (d, s))
FWD(void, HUD_TxferPredictionData,
	pTxferPredictionData,
	(struct entity_state_s* ps, const struct entity_state_s* pps, struct clientdata_s* pcd,
		const struct clientdata_s* ppcd, struct weapon_data_s* wd, const struct weapon_data_s* pwd),
	(ps, pps, pcd, ppcd, wd, pwd))
FWD(void, Demo_ReadBuffer, pReadDemoBuffer, (int s, unsigned char* b), (s, b))
FWD(int, HUD_ConnectionlessPacket, pConnectionlessPacket,
	(const struct netadr_s* n, const char* a, char* r, int* s), (n, a, r, s))
FWD(int, HUD_GetHullBounds, pGetHullBounds, (int h, float* mn, float* mx), (h, mn, mx))
FWD(void, HUD_Frame, pHudFrame, (double t), (t))
FWD(int, HUD_Key_Event, pKeyEvent, (int d, int k, const char* b), (d, k, b))
FWD(void, HUD_TempEntUpdate, pTempEntUpdate,
	(double ft, double ct, double g, struct tempent_s** f, struct tempent_s** a,
		int (*add)(struct cl_entity_s*), void (*snd)(struct tempent_s*, float)),
	(ft, ct, g, f, a, add, snd))
FWD(struct cl_entity_s*, HUD_GetUserEntity, pGetUserEntity, (int i), (i))
FWD(void, HUD_VoiceStatus, pVoiceStatus, (int e, qboolean t), (e, t))
FWD(void, HUD_DirectorMessage, pDirectorMessage, (int s, void* b), (s, b))
FWD(int, HUD_GetStudioModelInterface, pStudioInterface,
	(int v, struct r_studio_interface_s** p, struct engine_studio_api_s* s), (v, p, s))
FWD(void, HUD_ChatInputPosition, pChatInputPosition, (int* x, int* y), (x, y))
FWD(int, HUD_GetPlayerTeam, pGetPlayerTeam, (int p), (p))
