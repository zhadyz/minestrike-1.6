// Server-side Minecraft layer (lives inside the ReGameDLL game DLL).
#pragma once

#include "mc_items.h"
#include "mc_protocol.h"
#include "mc_sounds_gen.h"
#include "mc_world.h"

#include <vector>

class CBasePlayer;
class CBaseEntity;

namespace mc
{
// ---------------------------------------------------------------------------------------------
// Per-player state

struct McPlayer
{
	bool active = false;
	mci::Stack hotbar[mcp::HOTBAR_SIZE]; // Minecraft items and CS weapon tokens (mci::IsCsToken), any slot
	int csPref[5] = {0, 1, 2, 3, 4};      // where each CS weapon slot's token goes back to (0..8 hotbar, 9..35 storage)
	mci::Stack armor[mci::NUM_ARMOR_SLOTS];
	mci::Stack inv[27];   // survival inventory storage (Minecraft slots 9-35)
	mci::Stack craft[4];  // 2x2 crafting grid
	mci::Stack cursor;    // the stack held on the mouse in the inventory screen
	// hunger (mc_hunger.cpp, Minecraft FoodData)
	int food = 20;
	float saturation = 5.0f;
	float exhaustion = 0.0f;
	int foodTimer = 0;
	float nextFoodTick = 0.0f;
	float regenUntil = 0.0f;
	int regenLevel = 0;
	int regenTimer = 0;
	float absorption = 0.0f; // extra HP that soaks damage first (golden apples)
	// on fire (mc_fire.cpp): alight until fireUntil, hurt again at fireNext, the kill goes to fireOwner
	float fireUntil = 0.0f, fireNext = 0.0f, fireFlames = 0.0f;
	int fireOwner = 0;
	// armor wear is charged once per Minecraft invulnerability window, not once per bullet
	float armorWearUntil = 0.0f;
	int armorWearDone = 0; // wear already charged in the open window
	bool foodReset = true;   // set on death: food refills on the next spawn
	Vector lastPos;
	bool lastPosValid = false;
	bool wasOnGround = true;
	int selected = 2;          // hotbar slot shown as selected (0..8)
	bool mcItemActive = false; // holding weapon_mcitem
	int xpTotal = 0;           // total experience points
	int xpLevel = 0;
	float lastSwing = -100.0f; // gpGlobals->time of last attack swing (attack cooldown)
	float lastLevelSound = -100.0f;
	bool creative = false;
	bool invDirty = true;
	bool statDirty = true;
	// mining
	int mineBlock[3] = {-1, -1, -1};
	float mineProgress = 0.0f; // 0..1
	float mineLastHitSound = 0.0f;
	int mineStageSent = -1;
	// menu
	int menu = 0;
	int menuPage = 0;
	bool passBuy = false;
	// death/respawn bookkeeping
	float deathTime = 0.0f;
	bool deathEffectsDone = false;
	// elytra
	float nextFlySound = 0.0f;
	// totem / regen
	float nextRegen = 0.0f;
	float autojoinAt = 1.0f;
	// first team menu, held back until the client's VGUI is up (see H_ShowVGUIMenu)
	bool clientReady = false;
	float readyAt = 0.0f;
	float firstMenuAt = 0.0f;
	bool teamMenuPending = false;
	int teamMenuBits = 0;
	uint32_t advancements = 0; // bit per Adv
	// last damage taken (for death messages)
	int lastInflictor = 0;
	int lastBits = 0;
};

McPlayer& P(int index);         // 1..32
McPlayer& P(CBasePlayer* pl);

// ---------------------------------------------------------------------------------------------
// Entry points called from ReGameDLL (see the small hooks added to its sources)
void OnGiveFnptrs();            // h_export.cpp: wrap engine traces + register hookchains
void RegisterMessages();        // client.cpp LinkUserMessages
void Precache();                // client.cpp ClientPrecache
void OnServerActivate();        // client.cpp ServerActivate (world load)
void OnServerDeactivate();
void OnClientPutInServer(edict_t* ent);
void OnClientDisconnect(edict_t* ent);
void StartFrame();              // client.cpp StartFrame
void OnUpdateClientData(const edict_t* ent, struct clientdata_s* cd);

// ---------------------------------------------------------------------------------------------
// Inventory / items
bool GiveItem(CBasePlayer* pl, int itemId, int count, bool announce, bool pickup = false, int damage = 0);
// survival inventory (mc_inventory.cpp)
void InventoryClick(CBasePlayer* pl, int slot, int button, bool shift);
void InventoryDrop(CBasePlayer* pl, int slot, bool all);
void InventoryClose(CBasePlayer* pl);
void DropEverything(CBasePlayer* pl);
void ThrowStack(CBasePlayer* pl, const mci::Stack& s, bool scatter);
void SetItemDamage(CBaseEntity* item, int damage);
// hunger (mc_hunger.cpp)
bool CanEat(CBasePlayer* pl, int itemId);
void EatFood(CBasePlayer* pl, int itemId);
void AddExhaustion(CBasePlayer* pl, float amount);
void ResetFood(CBasePlayer* pl);
void HungerFrame(CBasePlayer* pl);
int RoomFor(CBasePlayer* pl, int itemId, int damage); // how many of an item still fit in the inventory
void SelectSlot(CBasePlayer* pl, int slot);
void SyncCsTokens(CBasePlayer* pl);             // tokens appear/disappear with the CS weapons owned
int CsWeaponInSlot(CBasePlayer* pl, int csSlot); // CS weapon id in a CS slot (0 none)
bool DropCsWeapon(CBasePlayer* pl, int csSlot);  // throw the weapon of a CS slot like CS's drop
void SendInventory(CBasePlayer* pl);
void SendStats(CBasePlayer* pl);
void EquipArmor(CBasePlayer* pl, int itemId);
float ArmorPoints(CBasePlayer* pl, float* toughness);
const mci::Stack& HeldStack(CBasePlayer* pl); // empty stack when not holding an MC item
void ConsumeHeld(CBasePlayer* pl, int n);
void DamageHeld(CBasePlayer* pl, int amount);

// ---------------------------------------------------------------------------------------------
// Effects (client-side rendering/sound via MCMSG_FX)
void FxSound(int sound, const float* origin, float volume = 1.0f, float pitch = 1.0f, int entindex = 0, edict_t* only = nullptr);
void FxParticles(int kind, const float* origin, int count, int data = 0);
void FxHurt(int entindex);
void FxDeath(int entindex, const float* origin);
void FxExplosion(const float* origin, float power);
void FxFirework(const float* origin, int shape, int color);
void FxSwing(int entindex);
void Toast(CBasePlayer* pl, int kind, const char* fmt, ...); // kind 0 = chat line, 1 = action bar
void ToastAll(int kind, const char* fmt, ...);

// ---------------------------------------------------------------------------------------------
// XP
void GiveXp(CBasePlayer* pl, int amount);
void DropXp(const float* origin, int amount);

// ---------------------------------------------------------------------------------------------
// World (voxels)
extern mcw::World g_world;
extern bool g_worldLoaded;
void SetBlock(int x, int y, int z, mcw::Cell c, bool broadcast = true);
void BreakBlock(int x, int y, int z, CBasePlayer* by, bool drop);
void Explode(const float* origin, float power, CBaseEntity* source, CBaseEntity* attacker = nullptr);
void ExplosionHurt(const float* origin, float power, CBaseEntity* attacker);
// redstone (mc_redstone.cpp)
void RedstoneRescan();
void RedstoneCellChanged(int x, int y, int z, mcw::Cell c);
void RedstoneFrame();
bool RedstoneUse(CBasePlayer* pl, int x, int y, int z);
bool PartSupported(int x, int y, int z); // mc_world_srv.cpp
// fire (mc_fire.cpp)
bool FireLight(int x, int y, int z, CBasePlayer* by); // flint and steel; false: no fire can be lit there
void FireInit();
void FireFrame();
void FireReset();
int FireCount();

// advancements + death messages (mc_advance.cpp)
enum Adv
{
	ADV_MONSTER_HUNTER,
	ADV_STONE_AGE,
	ADV_DIAMONDS,
	ADV_SUIT_UP,
	ADV_COVER_DIAMONDS,
	ADV_COVER_DEBRIS,
	ADV_SKYS_LIMIT,
	ADV_POSTMORTAL,
	ADV_TAKE_AIM,
	ADV_SNIPER_DUEL,
	ADV_OVERKILL,
	ADV_COUNT
};
void Award(CBasePlayer* pl, Adv adv);
const char* AdvancementTitle(int adv);
const char* AdvancementIcon(int adv);
int AdvancementFrame(int adv);
void CheckArmorAdvancements(CBasePlayer* pl);
void NoteFallDamage(CBasePlayer* pl, float damage);
void DeathMessage(CBasePlayer* victim, entvars_t* attacker, entvars_t* inflictor, int bits);

// mobs (mc_creeper.cpp)
enum Mob
{
	MOB_PLAYER,
	MOB_ZOMBIE,
	MOB_HUSK,
	MOB_DROWNED,
	MOB_CREEPER,
	MOB_ENDERMAN
};
Mob MobOf(CBasePlayer* pl);
int MobHurtSound(CBasePlayer* pl);
int MobDeathSound(CBasePlayer* pl);
void MobDrops(CBasePlayer* pl, const Vector& org);
bool IsCreeper(CBasePlayer* pl);
bool CreeperExploded(CBasePlayer* pl);
void CreeperSpawn(CBasePlayer* pl);
bool CreeperRestrictsItem(CBasePlayer* pl, int item);
void CreeperFrame();
bool EndermanDodge(CBasePlayer* pl, CBaseEntity* inflictor, CBaseEntity* attacker, int bits);

// hit boxes of players drawn as Minecraft models (mc_hitbox.cpp)
void HitRigsInit();
bool HitRigsHide(edict_t* skip); // before the engine's line trace; true: restore and trace must follow
void HitRigsRestore();
void HitRigsTrace(const float* v1, const float* v2, TraceResult* ptr);
void HitRigsFrame();
void HitRigsLogMap(CBasePlayer* shooter, CBasePlayer* target, float yawOff);
bool BotAimAtRig(CBasePlayer* bot, CBasePlayer* enemy, bool head, Vector& aimSpot); // cs_bot_update.cpp

// Items in the world
CBaseEntity* SpawnItemEntity(const float* origin, int itemId, int count, const float* velocity);

// Commands
bool ClientCommand(CBasePlayer* pl, const char* cmd, const char* args);
void OpenMenu(CBasePlayer* pl, int menu, int page = 0);

// Weapon (weapon_mcitem) behaviour, implemented in mc_weapon.cpp
void McItemFrame(CBasePlayer* pl);

// Logging to <cstrike>/logs/mc_server.log
void McLog(const char* fmt, ...);
} // namespace mc
