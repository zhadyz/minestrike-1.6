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
	bool meleeSpeed = false; // the 5% speed of a melee Minecraft weapon is applied
	int killer = 0;          // player index of who killed this player last (0: nobody), for the experience
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
bool GiveItem(CBasePlayer* pl, int itemId, int count, bool announce, bool pickup = false, int damage = 0, int ench = 0);
// survival inventory (mc_inventory.cpp)
void InventoryClick(CBasePlayer* pl, int slot, int button, bool shift);
void InventoryDrop(CBasePlayer* pl, int slot, bool all);
void InventoryClose(CBasePlayer* pl);
void DropEverything(CBasePlayer* pl);
void ThrowStack(CBasePlayer* pl, const mci::Stack& s, bool scatter);
void SetItemDamage(CBaseEntity* item, int damage);
void SetItemEnchant(CBaseEntity* item, int ench);
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
// spareMates: the blast is the attacker's the way a grenade is its thrower's: with mp_friendlyfire off it
// does nothing to the attacker's team (TNT a player lit)
void Explode(const float* origin, float power, CBaseEntity* source, CBaseEntity* attacker = nullptr, bool spareMates = false);
void ExplosionHurt(const float* origin, float power, CBaseEntity* attacker, bool spareMates = false);
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
	MOB_ENDERMAN,
	MOB_GOLEM, // the team mobs (mc_mobs.cpp)
	MOB_WITHER
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

// the economy and the standing of Minecraft weapons (mc_economy.cpp)
void EconomyInit();
bool EconomyOn(CBasePlayer* pl);                                      // does this player pay for items?
int BuyItem(CBasePlayer* pl, int itemId, int count, bool announce);  // how many the player got
bool BuyKit(CBasePlayer* pl, const char* const* items, const int* counts, int num, const char* name, bool free = false);
float McWeaponScale();
bool HoldsMcWeapon(CBasePlayer* pl);
bool HoldsMcMelee(CBasePlayer* pl);

// enchanting (mc_enchant_srv.cpp)
void EnchantInit();
void SendEnchants(CBasePlayer* pl);
void EnchantOpen(CBasePlayer* pl, int x, int y, int z); // right-click on an enchanting table
bool EnchantMenuSelect(CBasePlayer* pl, int menu, int key);
bool EnchantCommand(CBasePlayer* pl, const char* cmd); // the enchanting screen's clicks (mc_ench_*)
int BookshelvesAround(int x, int y, int z);
float EnchantMeleeBonus(const mci::Stack& held);       // Sharpness, in Minecraft damage points
float EnchantProtection(CBasePlayer* pl);              // Protection worn: the share taken off any damage
bool EnchantWears(const mci::Stack& s, bool armor);    // Unbreaking: does this use cost durability?
void EnchantIgnite(CBaseEntity* victim, CBasePlayer* by, float seconds); // Fire Aspect, Flame
bool BotEnchant(CBasePlayer* bot, mci::Stack* gear[], int numGear);

// bots with blocks and TNT, and what they learn (mc_bottactics.cpp)
void BotTacticsInit();
void BotTacticsSpawn(CBasePlayer* bot);
void BotTacticsThink(CBasePlayer* bot);       // from PreThink
void BotTacticsFrame();                       // from StartFrame
void BotTacticsRoundRestart();
void BotTacticsMapEnd();
// Team mobs (mc_mobs.cpp): the iron golem, built by a player, fighting for his side
enum TeamMobKind
{
	TM_NONE,
	TM_GOLEM,
	TM_WITHER
};
bool IsMobBot(CBasePlayer* pl);
int TeamMobOf(CBasePlayer* pl);
void TeamMobsInit();
void TeamMobRequest(int kind, int team, const Vector& feet, float yaw, CBasePlayer* by); // made at the start of the next frame
void TeamMobSpawned(CBasePlayer* pl);
bool TeamMobPreThink(CBasePlayer* pl);
bool TeamMobDamage(CBasePlayer* victim, CBaseEntity* inflictor, CBaseEntity* attacker, float& damage, int bits);
bool TeamMobSwings(CBasePlayer* pl);
bool TeamMobBullet(CBasePlayer* victim, entvars_t* attacker, float damage, TraceResult* tr, int bits); // true: it does nothing to it
bool BotIgnoresThreat(CBasePlayer* bot, CBasePlayer* other); // an iron golem, to a bot without a sword
bool BotHasSword(CBasePlayer* bot);                          // mc_botgear.cpp
bool WasBulletOf(entvars_t* shooter);                        // mc_world_srv.cpp: the hit being dealt now is that player's bullet
void TeamMobFrame();
void TeamMobDisconnect(int index);
void TeamMobsRemove();
int TeamMobsAlive(int team, int kind);
int GolemLimit();
Vector BotWish(CBasePlayer* bot);                                   // mc_bottactics.cpp: where its own AI wants to go (units a second)
bool GolemSeenLately(int team);                                    // the other side had an iron golem about, this round or the two before
bool WitherAllowed(int team);                                      // none standing, and its side's wait since the last one is over
void WitherEffect(CBasePlayer* victim, CBasePlayer* by);          // Minecraft's Wither effect: it eats health for ten seconds
CBaseEntity* SpawnSkull(CBasePlayer* owner, const Vector& from, const Vector& dir, bool blue); // mc_entities.cpp
int WitherBreaks(const Vector& origin, CBasePlayer* by);          // mc_world_srv.cpp: the blocks around a hurt wither go
void ExplodeThroughAll(const float* origin, float power, CBaseEntity* attacker); // mc_world_srv.cpp: a blue skull's blast
bool TryBuildMob(int x, int y, int z, CBasePlayer* pl); // mc_world_srv.cpp: the block just set down completes a mob
CGrenade* PlantedBombEnt();                              // mc_world_srv.cpp: the planted bomb, if there is one
int TacticsExpectedSite();                               // mc_bottactics.cpp: the bomb site the Terrorists are expected at (-1: no idea)
void StoryTell(float weight, const char* fmt, ...); // something worth telling when the round is over (mc_bottactics.cpp)
bool BotTacticsCommand(CBasePlayer* pl, const char* cmd); // mc_brain
void BotControlMove(struct playermove_s* pm); // before a bot's move is run
void BotLook(CBasePlayer* bot, const Vector& viewAngles); // where a bot looks this frame (pitch down positive)
// ... and the world's side of it (mc_world_srv.cpp, mc_entities.cpp)
bool IsPlacedBlock(int x, int y, int z);      // a block somebody set down (not the map)
int BlockOwner(int x, int y, int z);          // the side of whoever set it down (0: nobody's)
bool BombKeepsFree(int x, int y, int z);      // a cell beside a planted bomb: no block may go there
void BombClearsSpace(CGrenade* bomb);         // the bomb has just been planted: what stands against it comes down
int BlockBulletClass(const char* blockName);  // 0 stops every bullet, 1 the guns that go through walls pass, 2 stops none
bool H_IsPenetrable(IReGameHook_IsPenetrableEntity* chain, Vector& src, Vector& end, entvars_t* attacker, edict_t* hit);
bool CellTakesBlock(int x, int y, int z);     // could a block be set down in this cell?
Vector CellCenter(int x, int y, int z);
bool BotPlaceBlock(CBasePlayer* bot, int x, int y, int z); // the block in its hand
bool BotLightTnt(CBasePlayer* bot, int x, int y, int z);   // with the flint and steel in its hand
void MineFrame(CBasePlayer* pl, bool holding, const int* forced = nullptr, bool mapToo = false);
bool BotPlacePlate(CBasePlayer* bot, int x, int y, int z); // a pressure plate from its hand onto the block below
CBasePlayer* BlockPlacer(int x, int y, int z);              // who set the block down (nullptr: gone, or nobody)
void PrimeTntBy(const float* origin, int fuse, CBasePlayer* by);

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

// Where the mod's code is right now, for the watchdog of test runs (mc_main.cpp): when a frame does not end,
// the innermost of these names is what hangs.
extern const char* volatile g_mcWhere;
struct McWhere
{
	const char* prev;
	McWhere(const char* w) : prev(g_mcWhere) { g_mcWhere = w; }
	~McWhere() { g_mcWhere = prev; }
};
#define MC_WHERE_CAT2(a, b) a##b
#define MC_WHERE_CAT(a, b) MC_WHERE_CAT2(a, b)
#define MC_WHERE(name) mc::McWhere MC_WHERE_CAT(mcWhere_, __LINE__)(name)
} // namespace mc
