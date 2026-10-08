// Pulls in the Half-Life SDK client-side engine interface headers in the right order.
#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "wrect.h"
#include "cl_dll.h"
#include "APIProxy.h"
#include "cl_entity.h"
#include "com_model.h"
#include "triangleapi.h"
#include "event_api.h"
#include "pm_defs.h"
#include "pmtrace.h"
#include "ref_params.h"
#include "usercmd.h"
#include "entity_types.h"
#include "cvardef.h"
#include "r_efx.h"
#include "in_buttons.h"
#include "const.h"
#include "demo_api.h"
#include "pm_movevars.h"

int CL_IsThirdPersonLocal();

// The engine function table as handed to us (live engine memory) and our copy of it.
extern cl_enginefunc_t* g_pEngfuncs;
extern cl_enginefunc_t gEngfuncs;
