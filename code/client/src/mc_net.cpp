// User message handlers (server -> client) for the Minecraft layer.
#include "mc_chars.h"
#include "mc_client.h"
#include "mc_sounds_gen.h"
#include "mc_state.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace mc
{
struct Reader
{
	const unsigned char* p;
	int left;
	bool bad = false;
	Reader(void* buf, int size) : p((const unsigned char*)buf), left(size) {}
	int Byte()
	{
		if (left < 1) { bad = true; return 0; }
		left--;
		return *p++;
	}
	int Char() { return (signed char)Byte(); }
	int Short()
	{
		if (left < 2) { bad = true; return 0; }
		short v = (short)(p[0] | (p[1] << 8));
		p += 2;
		left -= 2;
		return v;
	}
	int Long()
	{
		if (left < 4) { bad = true; return 0; }
		int v = p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
		p += 4;
		left -= 4;
		return v;
	}
	float Coord() { return Short() * (1.0f / 8.0f); }
	const char* String()
	{
		static char buf[512];
		int n = 0;
		while (left > 0 && *p && n < (int)sizeof(buf) - 1)
		{
			buf[n++] = (char)*p++;
			left--;
		}
		if (left > 0)
		{
			p++;
			left--;
		}
		buf[n] = 0;
		return buf;
	}
};

static int MsgInv(const char*, int size, void* buf)
{
	Reader r(buf, size);
	int sel = r.Byte();
	int flags = r.Byte();
	int oldHeld = HeldItemId();
	int oldSel = g_cl.selected;
	g_cl.selected = sel;
	g_cl.mcItemActive = (flags & 1) != 0;
	g_cl.creative = (flags & 2) != 0;
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
	{
		g_cl.hotbarId[i] = r.Short();
		g_cl.hotbarCount[i] = r.Byte();
		g_cl.hotbarDamage[i] = r.Short();
	}
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
	{
		g_cl.armorId[i] = r.Short();
		g_cl.armorDamage[i] = r.Short();
	}
	g_cl.haveInv = true;
	Log("inv: sel %d mc %d slots %d %d %d %d %d | %d %d %d %d armor %d %d %d %d", sel, g_cl.mcItemActive, g_cl.hotbarId[0], g_cl.hotbarId[1],
		g_cl.hotbarId[2], g_cl.hotbarId[3], g_cl.hotbarId[4], g_cl.hotbarId[5], g_cl.hotbarId[6], g_cl.hotbarId[7], g_cl.hotbarId[8],
		g_cl.armorId[0], g_cl.armorId[1], g_cl.armorId[2], g_cl.armorId[3]);
	int held = HeldItemId();
	if (held != oldHeld || sel != oldSel)
	{
		g_cl.equipTime = g_cl.time;
		if (held)
			g_cl.heldNameTime = g_cl.time;
		g_cl.lastSwing = g_cl.time; // Minecraft resets attack strength on switch
	}
	return 1;
}

static int MsgInvMain(const char*, int size, void* buf)
{
	Reader r(buf, size);
	for (int i = 0; i < 27; i++)
	{
		g_cl.invId[i] = r.Short();
		g_cl.invCount[i] = r.Byte();
		g_cl.invDamage[i] = r.Short();
	}
	for (int i = 0; i < 4; i++)
	{
		g_cl.craftId[i] = r.Short();
		g_cl.craftCount[i] = r.Byte();
		g_cl.craftDamage[i] = r.Short();
	}
	g_cl.craftResId = r.Short();
	g_cl.craftResCount = r.Byte();
	g_cl.cursorId = r.Short();
	g_cl.cursorCount = r.Byte();
	g_cl.cursorDamage = r.Short();
	return 1;
}

static int MsgStat(const char*, int size, void* buf)
{
	Reader r(buf, size);
	g_cl.xpLevel = r.Short();
	g_cl.xpInto = r.Short();
	g_cl.xpNeed = r.Short();
	g_cl.armorPoints = r.Byte();
	r.Byte();
	g_cl.food = r.Byte();
	g_cl.saturation = r.Byte();
	g_cl.absorption = r.Byte();
	if (r.bad)
		g_cl.food = 20;
	return 1;
}

static int MsgFx(const char*, int size, void* buf)
{
	Reader r(buf, size);
	int type = r.Byte();
	switch (type)
	{
	case mcp::FX_SOUND:
	{
		int snd = r.Short();
		float o[3] = {r.Coord(), r.Coord(), r.Coord()};
		float vol = r.Byte() / 200.0f;
		float pitch = r.Byte() / 100.0f;
		int ent = r.Short();
		if (!r.bad)
			SoundPlay(snd, o, vol, pitch, ent);
		break;
	}
	case mcp::FX_PARTICLES:
	{
		int kind = r.Byte();
		float o[3] = {r.Coord(), r.Coord(), r.Coord()};
		int count = r.Byte();
		int data = r.Short() & 0xFFFF;
		if (!r.bad)
			ParticlesSpawn(kind, o, count, data);
		break;
	}
	case mcp::FX_HURT:
		EntHurt(r.Short());
		break;
	case mcp::FX_DEATH:
	{
		int ent = r.Short();
		float o[3] = {r.Coord(), r.Coord(), r.Coord()};
		EntDeath(ent, o);
		break;
	}
	case mcp::FX_EXPLOSION:
	{
		float o[3] = {r.Coord(), r.Coord(), r.Coord()};
		float power = r.Byte() / 10.0f;
		ParticlesExplosion(o, power);
		break;
	}
	case mcp::FX_FIREWORK:
	{
		float o[3] = {r.Coord(), r.Coord(), r.Coord()};
		int shape = r.Byte();
		int color = r.Long();
		ParticlesFirework(o, shape, color);
		break;
	}
	case mcp::FX_TOTEM:
		ParticlesTotem(r.Short());
		break;
	case mcp::FX_SWING:
	{
		int ent = r.Short();
		cl_entity_t* local = gEngfuncs.GetLocalPlayer();
		if (local && ent == local->index)
			HandOnSwing();
		else
			EntSwing(ent);
		break;
	}
	}
	return 1;
}

static int MsgVox(const char*, int size, void* buf)
{
	Reader r(buf, size);
	int n = r.Byte();
	for (int i = 0; i < n && !r.bad; i++)
	{
		int x = r.Short(), y = r.Short(), z = r.Short();
		int c = r.Short() & 0xFFFF;
		if (!r.bad)
			WorldApplyChange(x, y, z, (mcw::Cell)c);
	}
	return 1;
}

static int MsgBreak(const char*, int size, void* buf)
{
	Reader r(buf, size);
	int who = r.Byte();
	int x = r.Short(), y = r.Short(), z = r.Short();
	int stage = r.Byte();
	if (!r.bad)
		WorldSetBreak(who, x, y, z, stage == 255 ? -1 : stage);
	return 1;
}

static int MsgHello(const char*, int size, void* buf)
{
	Reader r(buf, size);
	int enabled = r.Byte();
	int crc = r.Long();
	const char* map = r.String();
	Log("hello: world %d crc %08x map %s", enabled, crc, map);
	g_cl.worldEnabled = (enabled & 1) != 0;
	if (enabled & 2)
	{
		// new round: the server restored the map; reload our copy from disk
		char name[128];
		strncpy(name, map, sizeof(name) - 1);
		name[sizeof(name) - 1] = 0;
		extern bool WorldIsClassic();
		extern void WorldResetCells();
		if (WorldIsClassic())
			WorldResetCells(); // classic: just forget the digging, keep textures and meshes
		else
		{
			WorldUnload();
			WorldLoadForMap(name);
		}
	}
	return 1;
}

// someone picked a character (mc_chars.h): byte player index, byte character
static int MsgChar(const char*, int size, void* buf)
{
	Reader r(buf, size);
	int index = r.Byte();
	int c = r.Byte();
	extern void SetPlayerCharacter(int index, int c);
	if (!r.bad)
		SetPlayerCharacter(index, c);
	return 1;
}

static int MsgToast(const char*, int size, void* buf)
{
	Reader r(buf, size);
	int kind = r.Byte();
	const char* text = r.String();
	HudToast(kind, text);
	return 1;
}

void NetInit()
{
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_INV, MsgInv);
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_STAT, MsgStat);
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_FX, MsgFx);
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_VOX, MsgVox);
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_BREAK, MsgBreak);
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_HELLO, MsgHello);
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_TOAST, MsgToast);
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_INVMAIN, MsgInvMain);
	gEngfuncs.pfnHookUserMsg((char*)MCMSG_CHAR, MsgChar);
}

// ---------------------------------------------------------------------------------------------
// Sounds

void SoundPlay(int sound, const float* origin, float volume, float pitch, int entindex)
{
	if (sound < 0 || sound >= mcs::MCS_COUNT)
		return;
	const mcs::SoundEvent& ev = mcs::g_sounds[sound];
	int total = 0;
	for (int i = 0; i < ev.numVariants; i++)
		total += ev.variants[i].weight;
	int pick = total > 0 ? rand() % total : 0;
	const mcs::SoundVariant* v = &ev.variants[0];
	for (int i = 0; i < ev.numVariants; i++)
	{
		pick -= ev.variants[i].weight;
		if (pick < 0)
		{
			v = &ev.variants[i];
			break;
		}
	}
	float vol = volume * v->volume;
	float p = pitch * v->pitch;
	// Minecraft volume > 1 extends the audible range instead of getting louder
	float atten = vol > 1.0f ? 0.8f / vol : 0.8f;
	if (vol > 1.0f)
		vol = 1.0f;
	int ipitch = (int)(p * 100.0f);
	if (ipitch < 1)
		ipitch = 1;
	if (ipitch > 255)
		ipitch = 255;
	cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	if (!origin)
	{
		// interface sounds (no origin): on ourselves
		origin = g_cl.vieworg;
		if (local)
			entindex = local->index;
	}
	float org[3] = {origin[0], origin[1], origin[2]};
	if (local && entindex == local->index)
	{
		// sounds on ourselves play at full volume without spatialisation
		gEngfuncs.pEventAPI->EV_PlaySound(local->index, org, CHAN_AUTO, v->file, vol, 0.0f, 0, ipitch);
		return;
	}
	gEngfuncs.pEventAPI->EV_PlaySound(0, org, CHAN_AUTO, v->file, vol, atten, 0, ipitch);
}

void SoundFrame()
{
	// Elytra wind (ElytraOnPlayerSoundInstance): a 10 s loop while gliding, stopped on landing.
	static bool playing = false;
	static double started = 0.0;
	cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	if (!local)
		return;
	const char* file = mcs::g_sounds[mcs::MCS_ELYTRA_FLYING].variants[0].file;
	bool gliding = (g_cl.flags & mcp::MCPF_GLIDING) != 0;
	if (gliding && (!playing || g_cl.time - started > 10.0))
	{
		float o[3] = {local->origin[0], local->origin[1], local->origin[2]};
		gEngfuncs.pEventAPI->EV_PlaySound(local->index, o, CHAN_STATIC, file, 0.7f, 0.0f, 0, 100);
		playing = true;
		started = g_cl.time;
	}
	else if (!gliding && playing)
	{
		gEngfuncs.pEventAPI->EV_StopSound(local->index, CHAN_STATIC, file);
		playing = false;
	}
}
} // namespace mc
