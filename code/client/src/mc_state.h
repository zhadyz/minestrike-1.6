// Client-side Minecraft state shared between the client modules.
#pragma once
#include "hlsdk_client.h"
#include "mc_items.h"
#include "mc_protocol.h"
#include "mc_world.h"

namespace mc
{
struct ClientState
{
	// from MCMSG_INV
	int selected = 2;
	bool mcItemActive = false;
	bool creative = false;
	int hotbarId[mcp::HOTBAR_SIZE] = {};   // >0 MC item id, <0 CS weapon id (negated), 0 empty
	int hotbarCount[mcp::HOTBAR_SIZE] = {};
	int hotbarDamage[mcp::HOTBAR_SIZE] = {};
	int armorId[mci::NUM_ARMOR_SLOTS] = {};
	int armorDamage[mci::NUM_ARMOR_SLOTS] = {};
	bool haveInv = false;
	// from MCMSG_INVMAIN: survival inventory
	int invId[27] = {}, invCount[27] = {}, invDamage[27] = {};
	int craftId[4] = {}, craftCount[4] = {}, craftDamage[4] = {};
	int craftResId = 0, craftResCount = 0;
	int cursorId = 0, cursorCount = 0, cursorDamage = 0;
	// from MCMSG_STAT
	int xpLevel = 0, xpInto = 0, xpNeed = 7, armorPoints = 0;
	int food = 20, saturation = 5, absorption = 0; // hunger bar, absorption hearts (Minecraft health points)
	// from prediction (local player)
	int flags = 0;           // MCPF_*
	float boost = 0.0f;
	bool onGround = true;
	// timing
	double time = 0.0;       // client time (HUD_Frame)
	float frametime = 0.0f;
	// view
	float vieworg[3] = {}, viewangles[3] = {}, forward[3] = {}, right[3] = {}, up[3] = {};
	float fov = 90.0f;
	int screenW = 1280, screenH = 720;
	// attack cooldown (client prediction of the server's lastSwing)
	double lastSwing = -100.0;
	double swingStart = -100.0; // arm swing animation
	double equipTime = -100.0;  // item switch animation
	int lastHeldId = 0;
	bool attackHeld = false;
	// using the held item (bow draw, eating): right mouse held since useStart
	bool useHeld = false;
	double useStart = -100.0;
	// health
	int health = 100;
	double deathTime = -100.0;
	bool alive = true;
	// world
	bool worldEnabled = false;
	// held item name popup
	double heldNameTime = -100.0;
	// mining crack for local display (from MCMSG_BREAK): per breaker
};

extern ClientState g_cl;

const mci::ItemDef* HeldItem(); // nullptr when holding nothing / a CS weapon
int HeldItemId();

// modules
void NetInit();
void SoundPlay(int sound, const float* origin, float volume, float pitch, int entindex);
void SoundFrame();

void HudInit();
void HudVidInit();
void HudDraw();
void HudToast(int kind, const char* text);

void HandDraw();        // first-person hand/item (call from HUD_Redraw before the 2D HUD)
void HandOnSwing();

void WorldInit();
void WorldLoadForMap(const char* mapname);
void WorldUnload();
void WorldApplyChange(int x, int y, int z, mcw::Cell c);
void WorldSetBreak(int breaker, int x, int y, int z, int stage);
void WorldDraw();        // opaque + alphatest voxels, block outline, cracks
const mcw::World* WorldGet();

void EntDrawAll();       // Minecraft entities (orbs, items, ...)
bool EntIsMc(cl_entity_t* ent);
void EntHurt(int entindex);
void EntSwing(int entindex);
void EntDeath(int entindex, const float* origin);

void ParticlesSpawn(int kind, const float* origin, int count, int data);
void ParticlesExplosion(const float* origin, float power);
void ParticlesFirework(const float* origin, int shape, int color);
void ParticlesTotem(int entindex);
void ParticlesFrame(float dt);
void ParticlesDraw();

void InputInit();        // command interception (slots, inventory)
bool InputKey(int down, int keynum, const char* binding);
void InputCreateMove(float frametime, usercmd_t* cmd, int active);

// helpers
float RandF(float lo, float hi);
void AngleVectors3(const float angles[3], float f[3], float r[3], float u[3]);
} // namespace mc
