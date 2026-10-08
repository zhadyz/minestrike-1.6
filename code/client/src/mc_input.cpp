// Input: the 9-slot Minecraft hotbar. Slots 1-5 keep Counter-Strike's weapon categories, 6-9 are
// Minecraft items. We intercept the official client's slotN/invnext/invprev commands at registration
// time (pfnAddCommand) so number keys and the mouse wheel drive the hotbar.
#include "mc_client.h"
#include "mc_state.h"
#include "mc_move.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mc
{
typedef void (*CmdFn)(void);
static CmdFn g_origSlot[11];
static CmdFn g_origInvNext, g_origInvPrev;
static pfnEngSrc_pfnAddCommand_t g_realAddCommand;

// Text menus (CS radio/buy menus, our creative inventory) take number keys: track whether one is up.
static pfnUserMsgHook g_origShowMenu = nullptr;
static bool g_menuOpen = false;

static int Hook_ShowMenu(const char* name, int size, void* buf)
{
	if (size >= 2)
	{
		const unsigned char* p = (const unsigned char*)buf;
		int keys = p[0] | (p[1] << 8);
		g_menuOpen = keys != 0;
	}
	return g_origShowMenu ? g_origShowMenu(name, size, buf) : 1;
}

static pfnEngSrc_pfnHookUserMsg_t g_realHookUserMsg;
static int Hook_HookUserMsg(char* name, pfnUserMsgHook fn)
{
	if (!strcmp(name, "ShowMenu"))
	{
		g_origShowMenu = fn;
		return g_realHookUserMsg(name, Hook_ShowMenu);
	}
	return g_realHookUserMsg(name, fn);
}

pfnEngSrc_pfnHookUserMsg_t InputHookUserMsgHook(pfnEngSrc_pfnHookUserMsg_t real)
{
	g_realHookUserMsg = real;
	return &Hook_HookUserMsg;
}

static void SelectHotbar(int slot1based)
{
	int idx = slot1based - 1;
	if (g_menuOpen)
	{
		// let the official client turn the key into "menuselect N"
		g_menuOpen = false;
		if (g_origSlot[slot1based])
			g_origSlot[slot1based]();
		return;
	}
	// every hotbar slot is selected by the server, whatever it holds (a gun, an item, nothing)
	char cmd[32];
	snprintf(cmd, sizeof(cmd), "mc_select %d", slot1based);
	gEngfuncs.pfnServerCmd(cmd);
	g_cl.selected = idx; // predicted; the server confirms with MCMSG_INV
}

#define SLOTFN(n) static void Cmd_Slot##n() { SelectHotbar(n); }
SLOTFN(1) SLOTFN(2) SLOTFN(3) SLOTFN(4) SLOTFN(5) SLOTFN(6) SLOTFN(7) SLOTFN(8) SLOTFN(9)
static void Cmd_Slot10()
{
	g_menuOpen = false; // "0" closes/answers any open menu
	if (g_origSlot[10])
		g_origSlot[10]();
}
static const CmdFn kSlotFns[11] = {nullptr, Cmd_Slot1, Cmd_Slot2, Cmd_Slot3, Cmd_Slot4, Cmd_Slot5, Cmd_Slot6, Cmd_Slot7, Cmd_Slot8, Cmd_Slot9, Cmd_Slot10};

static bool SlotSelectable(int idx)
{
	(void)idx;
	return true; // Minecraft lets you hold an empty slot
}

static void Cycle(int dir)
{
	if (!g_cl.haveInv)
	{
		CmdFn f = dir > 0 ? g_origInvNext : g_origInvPrev;
		if (f)
			f();
		return;
	}
	int idx = g_cl.selected;
	for (int k = 0; k < mcp::HOTBAR_SIZE; k++)
	{
		idx = (idx + dir + mcp::HOTBAR_SIZE) % mcp::HOTBAR_SIZE;
		if (SlotSelectable(idx))
			break;
	}
	SelectHotbar(idx + 1);
}
static void Cmd_InvNext() { Cycle(+1); }
static void Cmd_InvPrev() { Cycle(-1); }

static CmdFn g_origBuy = nullptr;
static cvar_t* g_cvBuyInv = nullptr;
static void Cmd_Buy()
{
	// classic maps play like Counter-Strike: B is the buy menu (Minecraft gear is a category in it)
	extern bool WorldIsClassic();
	if (WorldIsClassic() && !g_cl.creative)
	{
		if (g_origBuy)
			g_origBuy();
		return;
	}
	// B opens the Minecraft creative inventory (server text menu); mc_buy_inventory 0 restores CS's
	if (!g_cvBuyInv)
		g_cvBuyInv = gEngfuncs.pfnGetCvarPointer("mc_buy_inventory");
	if (g_cvBuyInv && g_cvBuyInv->value == 0.0f)
	{
		if (g_origBuy)
			g_origBuy();
		return;
	}
	// the Minecraft creative inventory screen (mc_gui 0: the server's text menu instead)
	static cvar_t* gui = nullptr;
	if (!gui)
		gui = gEngfuncs.pfnGetCvarPointer("mc_gui");
	extern bool GuiOpen();
	extern void GuiShow();
	extern void GuiHide();
	if (gui && gui->value != 0.0f && g_cl.health > 0)
	{
		if (GuiOpen())
			GuiHide();
		else
			GuiShow();
		return;
	}
	gEngfuncs.pfnServerCmd((char*)"mc_menu");
}
static void Cmd_CsBuy()
{
	if (g_origBuy)
		g_origBuy();
}

// Minecraft's F5 camera replaces CS's thirdperson/firstperson (which this client build refuses)
void PerspectiveSet(int v);
void PerspectiveCycle();
static void Cmd_ThirdPerson() { PerspectiveSet(1); }
static void Cmd_FirstPerson() { PerspectiveSet(0); }
static void Cmd_Perspective() { PerspectiveCycle(); }

static int Hook_AddCommand(char* name, void (*fn)(void))
{
	if (!strcmp(name, "thirdperson"))
	{
		g_realAddCommand((char*)"mc_perspective", Cmd_Perspective);
		return g_realAddCommand(name, Cmd_ThirdPerson);
	}
	if (!strcmp(name, "firstperson"))
		return g_realAddCommand(name, Cmd_FirstPerson);
	if (!strcmp(name, "buy"))
	{
		g_origBuy = fn;
		int r = g_realAddCommand(name, Cmd_Buy);
		g_realAddCommand((char*)"mc_csbuy", Cmd_CsBuy);
		gEngfuncs.pfnRegisterVariable((char*)"mc_buy_inventory", (char*)"1", FCVAR_ARCHIVE);
		gEngfuncs.pfnRegisterVariable((char*)"mc_gui", (char*)"1", FCVAR_ARCHIVE);
		return r;
	}
	if (!strncmp(name, "slot", 4))
	{
		int n = atoi(name + 4);
		if (n >= 1 && n <= 10)
		{
			g_origSlot[n] = fn;
			return g_realAddCommand(name, kSlotFns[n]);
		}
	}
	if (!strcmp(name, "invnext"))
	{
		g_origInvNext = fn;
		return g_realAddCommand(name, Cmd_InvNext);
	}
	if (!strcmp(name, "invprev"))
	{
		g_origInvPrev = fn;
		return g_realAddCommand(name, Cmd_InvPrev);
	}
	return g_realAddCommand(name, fn);
}

// Called before the official client's Initialize: route its command registration through us.
void InputInit()
{
	if (!g_pEngfuncs || g_realAddCommand)
		return;
	g_realAddCommand = g_pEngfuncs->pfnAddCommand;
}

pfnEngSrc_pfnAddCommand_t InputAddCommandHook() { return &Hook_AddCommand; }

bool InputKey(int down, int keynum, const char* binding)
{
	// F5: Minecraft's perspective toggle
	const int K_F5 = 139;
	if (keynum == K_F5)
	{
		if (down)
			PerspectiveCycle();
		return false;
	}
	return true;
}

void InputCreateMove(float frametime, usercmd_t* cmd, int active)
{
	bool attack = (cmd->buttons & IN_ATTACK) != 0;
	if (g_cl.mcItemActive)
	{
		if (attack && !g_cl.attackHeld)
		{
			g_cl.lastSwing = g_cl.time;
			HandOnSwing();
		}
		else if (attack)
		{
			// mining keeps the arm swinging while a block is targeted
			const mcw::World* w = WorldGet();
			if (w)
			{
				int b[3], face;
				float dist;
				bool cl = false;
				if (mcm::WorldPick(g_cl.vieworg, g_cl.forward, mci::BLOCK_REACH, b, &face, &dist, &cl))
					HandOnSwing();
			}
		}
	}
	g_cl.attackHeld = attack;
}
} // namespace mc
