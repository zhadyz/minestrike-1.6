// Animation glue between Counter-Strike and Minecraft characters.
//  - Every weapon fire event of the Counter-Strike client (events/<gun>.sc) is noted per player, so a
//    player drawn as a Minecraft character kicks back when they shoot (mc_players.cpp: LastShotTime).
//  - Counter-Strike player models get the held Minecraft item drawn in their right hand: the client's
//    StudioDrawPlayer is wrapped, and after it ran the engine still holds that model's bone matrices.
#include "hlsdk_client.h"
#include "r_studioint.h"
#include "studio.h"
#include "mc_client.h"
#include "mc_draw.h"
#include "mc_gl.h"
#include "mc_items.h"
#include "mc_protocol.h"
#include "mc_state.h"

#include <array>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <utility>

namespace mc
{
float WorldLightAtPos(const float* p);

// ---------------------------------------------------------------------------------------------
// Fire events

typedef void (*EventFn)(struct event_args_s*);
static pfnEngSrc_pfnHookEvent_t g_realHookEvent = nullptr;
struct HookedEvent
{
	EventFn fn;
	char gun[16]; // Counter-Strike weapon name for weapon fire events, else empty
};
static const int MAX_EVENTS = 64;
static HookedEvent g_ev[MAX_EVENTS];
static int g_numEv = 0;
static double g_lastShot[33];
static char g_lastGun[33][16];

static void OnEvent(int i, struct event_args_s* a)
{
	if (!a || !g_ev[i].gun[0] || a->entindex < 1 || a->entindex > 32)
		return;
	g_lastShot[a->entindex] = gEngfuncs.GetClientTime();
	strncpy(g_lastGun[a->entindex], g_ev[i].gun, sizeof(g_lastGun[0]) - 1);
}

template <int I> static void Trampoline(struct event_args_s* a)
{
	OnEvent(I, a);
	if (g_ev[I].fn)
		g_ev[I].fn(a);
}
template <size_t... I> static std::array<EventFn, sizeof...(I)> MakeTrampolines(std::index_sequence<I...>)
{
	return {{&Trampoline<(int)I>...}};
}
static const std::array<EventFn, MAX_EVENTS> kTrampolines = MakeTrampolines(std::make_index_sequence<MAX_EVENTS>{});

static void W_HookEvent(char* name, EventFn fn)
{
	if (!fn || g_numEv >= MAX_EVENTS)
	{
		g_realHookEvent(name, fn);
		return;
	}
	int i = g_numEv++;
	g_ev[i].fn = fn;
	g_ev[i].gun[0] = 0;
	// "events/ak47.sc" -> "ak47" (and elite_left / elite_right -> "elite")
	static const char* kGuns[] = {"ak47", "aug", "awp", "deagle", "elite", "famas", "fiveseven", "g3sg1", "galil", "glock18",
		"m249", "m3", "m4a1", "mac10", "mp5n", "p228", "p90", "scout", "sg550", "sg552", "tmp", "ump45", "usp", "xm1014"};
	const char* base = name ? strrchr(name, '/') : nullptr;
	base = base ? base + 1 : (name ? name : "");
	for (const char* g : kGuns)
	{
		size_t n = strlen(g);
		if (!strncmp(base, g, n) && (base[n] == '.' || (base[n] == '_' && !strcmp(g, "elite"))))
		{
			strncpy(g_ev[i].gun, g, sizeof(g_ev[i].gun) - 1);
			break;
		}
	}
	g_realHookEvent(name, kTrampolines[i]);
}

pfnEngSrc_pfnHookEvent_t InputHookEventHook(pfnEngSrc_pfnHookEvent_t real)
{
	g_realHookEvent = real;
	return &W_HookEvent;
}

double LastShotTime(int index) { return index >= 1 && index <= 32 ? g_lastShot[index] : -100.0; }

// ---------------------------------------------------------------------------------------------
// Minecraft items in the hands of Counter-Strike player models

static r_studio_interface_t g_studioOrig;
static r_studio_interface_t g_studioOurs;
static engine_studio_api_t* g_studio = nullptr;
static cvar_t* g_cvItem[7]; // mc_csitem_ox oy oz rx ry rz scale: placement in the hand bone's frame

static int FindBone(const studiohdr_t* hdr, const char* name)
{
	const mstudiobone_t* bones = (const mstudiobone_t*)((const uint8_t*)hdr + hdr->boneindex);
	for (int i = 0; i < hdr->numbones; i++)
		if (!strcmp(bones[i].name, name))
			return i;
	return -1;
}

// Hands of Counter-Strike models holding a Minecraft item, recorded while the engine draws them (its bone
// matrices are world space) and drawn later in the triangle pass, where the fixed-function matrices hold.
struct CsHands
{
	double time = -100.0;
	int item = 0, flags = 0;
	float origin[3] = {};
	float right[3][4], left[3][4];
};
static CsHands g_hands[33];

static void RecordHands(struct entity_state_s* st)
{
	if (!g_studio || !st || st->number < 1 || st->number > 32)
		return;
	cl_entity_t* ent = gEngfuncs.GetEntityByIndex(st->number);
	int iu = ent ? ent->curstate.iuser4 : 0; // the studio player state passed in has no iuser4
	if (!(iu & mcp::MCPF_MCITEM))
		return;
	int held = mcp::HeldItemOf(iu);
	if (!held || !mci::ValidItem(held))
		return;
	model_t* m = g_studio->SetupPlayerModel(st->number - 1);
	studiohdr_t* hdr = m ? (studiohdr_t*)g_studio->Mod_Extradata(m) : nullptr;
	if (!hdr)
		return;
	static const studiohdr_t* lastHdr = nullptr;
	static int rh = -1, lh = -1;
	if (hdr != lastHdr)
	{
		lastHdr = hdr;
		rh = FindBone(hdr, "Bip01 R Hand");
		lh = FindBone(hdr, "Bip01 L Hand");
		static bool logged = false;
		if (!logged)
		{
			logged = true;
			Log("anim: hand bones R %d L %d in %s", rh, lh, hdr->name);
		}
	}
	float(*bt)[MAXSTUDIOBONES][3][4] = (float(*)[MAXSTUDIOBONES][3][4])g_studio->StudioGetBoneTransform();
	if (rh < 0 || lh < 0 || !bt)
		return;
	CsHands& h = g_hands[st->number];
	memcpy(h.right, (*bt)[rh], sizeof(h.right));
	memcpy(h.left, (*bt)[lh], sizeof(h.left));
	h.time = gEngfuncs.GetClientTime();
	h.item = held;
	h.flags = iu;
	for (int i = 0; i < 3; i++)
		h.origin[i] = st->origin[i];
}

static void MultBone(const float b[3][4])
{
	const GLfloat mat[16] = {b[0][0], b[1][0], b[2][0], 0, b[0][1], b[1][1], b[2][1], 0, b[0][2], b[1][2], b[2][2], 0, b[0][3], b[1][3],
		b[2][3], 1};
	glMultMatrixf(mat);
}

static cvar_t* Cv(const char* name, const char* def) { return gEngfuncs.pfnRegisterVariable((char*)name, (char*)def, 0); }

// Called from the normal-triangles pass: Minecraft items in Counter-Strike models' hands.
void DrawCsModelItems()
{
	// placement in the hand bone's frame (Bip01 * Hand: x runs along the fingers)
	static cvar_t *bx = Cv("mc_csbow_ox", "3"), *by = Cv("mc_csbow_oy", "0"), *bz = Cv("mc_csbow_oz", "0");
	static cvar_t *brx = Cv("mc_csbow_rx", "0"), *bry = Cv("mc_csbow_ry", "0"), *brz = Cv("mc_csbow_rz", "45");
	static cvar_t *ix = Cv("mc_csitem_ox", "4"), *iy = Cv("mc_csitem_oy", "0"), *iz = Cv("mc_csitem_oz", "0");
	static cvar_t *irx = Cv("mc_csitem_rx", "0"), *iry = Cv("mc_csitem_ry", "0"), *irz = Cv("mc_csitem_rz", "-45");
	static cvar_t* sc = Cv("mc_csitem_scale", "22");
	double now = gEngfuncs.GetClientTime();
	cl_entity_t* me = gEngfuncs.GetLocalPlayer();
	for (int i = 1; i <= 32; i++)
	{
		CsHands& h = g_hands[i];
		if (now - h.time > 0.25 || !mci::ValidItem(h.item))
			continue;
		if (me && me->index == i && !CL_IsThirdPersonLocal())
			continue;
		const mci::ItemDef& d = mci::Item(h.item);
		bool bow = d.type == mci::IT_BOW, xbow = d.type == mci::IT_CROSSBOW;
		bool using_ = (h.flags & mcp::MCPF_USING) != 0, loaded = (h.flags & mcp::MCPF_XBOW_LOADED) != 0;
		const char* tex = nullptr;
		if (bow && using_)
			tex = "item/bow_pulling_2";
		else if (xbow)
			tex = loaded ? "item/crossbow_arrow" : using_ ? "item/crossbow_pulling_1" : "item/crossbow_standby";
		float l = 0.35f + 0.65f * WorldLightAtPos(h.origin);

		GLint tex2d = 0;
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex2d);
		glPushAttrib(GL_ALL_ATTRIB_BITS);
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		// bows are held up in the left hand, like a skeleton; everything else in the right
		MultBone(bow ? h.left : h.right);
		if (bow)
		{
			glTranslatef(bx->value, by->value, bz->value);
			glRotatef(brx->value, 1, 0, 0);
			glRotatef(bry->value, 0, 1, 0);
			glRotatef(brz->value, 0, 0, 1);
			static cvar_t* flip = Cv("mc_csbow_flip", "180");
			glRotatef(flip->value, 1, 1, 0); // turn the bow about its long axis (the sprite's diagonal)
		}
		else
		{
			glTranslatef(ix->value, iy->value, iz->value);
			glRotatef(irx->value, 1, 0, 0);
			glRotatef(iry->value, 0, 1, 0);
			glRotatef(irz->value, 0, 0, 1);
		}
		float s = sc->value * (bow || xbow ? 1.3f : 1.0f);
		glScalef(s, s, s);
		glEnable(GL_TEXTURE_2D);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(GL_TRUE);
		glDisable(GL_BLEND);
		glDisable(GL_CULL_FACE);
		glDisable(GL_FOG);
		const GLfloat env[4] = {l, l, l, 1.0f};
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, 0x8570 /* GL_COMBINE */);
		glTexEnvi(GL_TEXTURE_ENV, 0x8571 /* GL_COMBINE_RGB */, GL_MODULATE);
		glTexEnvi(GL_TEXTURE_ENV, 0x8580 /* GL_SOURCE0_RGB */, GL_TEXTURE);
		glTexEnvi(GL_TEXTURE_ENV, 0x8581 /* GL_SOURCE1_RGB */, 0x8576 /* GL_CONSTANT */);
		glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, env);
		// where the bow's string ends are, in world space (the sprite's upper-left and lower-right corners)
		float tips[2][3] = {};
		if (bow)
		{
			GLfloat mv[16];
			glGetFloatv(GL_MODELVIEW_MATRIX, mv);
			(void)mv;
		}
		mcdraw::Item3D(h.item, 1.0f, tex);
		glPopMatrix();
		glPopAttrib();
		glBindTexture(GL_TEXTURE_2D, (GLuint)tex2d);
		(void)tips;

		// drawing a bow: the string pulled back to the right hand with an arrow on it
		if (bow && using_)
		{
			float hand[3] = {h.right[0][3], h.right[1][3], h.right[2][3]};
			float grip[3] = {h.left[0][3], h.left[1][3], h.left[2][3]};
			float up[3] = {h.left[0][2], h.left[1][2], h.left[2][2]}; // bone z: across the hand
			float half = s * 0.45f;
			float top[3], bot[3];
			for (int k = 0; k < 3; k++)
			{
				top[k] = grip[k] + up[k] * half;
				bot[k] = grip[k] - up[k] * half;
			}
			glPushAttrib(GL_ALL_ATTRIB_BITS);
			glDisable(GL_TEXTURE_2D);
			glDisable(GL_CULL_FACE);
			glLineWidth(2.0f);
			glColor3f(0.85f * l, 0.85f * l, 0.85f * l);
			glBegin(GL_LINES);
			glVertex3fv(top); glVertex3fv(hand);
			glVertex3fv(hand); glVertex3fv(bot);
			glEnd();
			// the arrow: from the drawn string past the bow
			float dir[3], len = 0;
			for (int k = 0; k < 3; k++)
			{
				dir[k] = grip[k] - hand[k];
				len += dir[k] * dir[k];
			}
			len = sqrtf(len);
			if (len > 0.1f)
			{
				float tip[3];
				for (int k = 0; k < 3; k++)
					tip[k] = hand[k] + dir[k] / len * (len + 10.0f);
				glLineWidth(3.0f);
				glColor3f(0.45f * l, 0.30f * l, 0.15f * l);
				glBegin(GL_LINES);
				glVertex3fv(hand); glVertex3fv(tip);
				glEnd();
				glPointSize(5.0f);
				glColor3f(0.6f * l, 0.6f * l, 0.65f * l);
				glBegin(GL_POINTS);
				glVertex3fv(tip);
				glEnd();
			}
			glPopAttrib();
		}
	}
}

static int W_StudioDrawPlayer(int flags, struct entity_state_s* pplayer)
{
	int r = g_studioOrig.StudioDrawPlayer(flags, pplayer);
	if (r && (flags & STUDIO_RENDER))
		RecordHands(pplayer);
	return r;
}

int StudioInterfaceHook(int (*orig)(int, struct r_studio_interface_s**, struct engine_studio_api_s*), int version,
	struct r_studio_interface_s** ppinterface, struct engine_studio_api_s* pstudio)
{
	int r = orig ? orig(version, ppinterface, pstudio) : 0;
	if (r && ppinterface && *ppinterface && *ppinterface != &g_studioOurs)
	{
		g_studioOrig = **ppinterface;
		g_studioOurs = g_studioOrig;
		g_studioOurs.StudioDrawPlayer = W_StudioDrawPlayer;
		*ppinterface = &g_studioOurs;
		g_studio = pstudio;
		Log("anim: wrapped StudioDrawPlayer");
	}
	return r;
}
} // namespace mc
