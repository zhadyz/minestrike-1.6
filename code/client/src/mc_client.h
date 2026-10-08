// Minecraft layer hooks called by the client proxy around the official CS client.
#pragma once
#include "hlsdk_client.h"

namespace mc
{
void Log(const char* fmt, ...);

void OnInitialize();
void OnHudInit();
void OnVidInit();
void OnRedraw(float time, int intermission);
void OnDrawNormalTriangles();
void OnDrawTransparentTriangles();
void OnPlayerMoveInit(playermove_t* ppmove);
void OnPrePlayerMove(playermove_t* ppmove, int server);
void OnPostPlayerMove(playermove_t* ppmove, int server);
bool MovementOverride(playermove_t* ppmove);
void OnFrame(double time);
// Return false to swallow the key (the official client never sees it).
bool OnKeyEvent(int down, int keynum, const char* binding);
void OnCalcRefdef(ref_params_t* pparams);
// Return false to hide the entity from rendering.
bool OnAddEntity(int type, cl_entity_t* ent, const char* modelname);
void OnPreCreateMove(); // before the official client builds the usercmd
void OnCreateMove(float frametime, usercmd_t* cmd, int active);
void OnUpdateClientData(client_data_t* cdata, float time);
void OnShutdown();
void OnMouseDeactivate();
bool PerspectiveIsThirdPerson(); // Minecraft F5 camera (mc_client.cpp)
} // namespace mc
