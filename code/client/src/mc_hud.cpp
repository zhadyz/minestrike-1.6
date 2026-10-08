// Minecraft HUD: hotbar, hearts, armor, XP bar + level, held item name, crosshair + attack indicator,
// chat/action-bar toasts. Layout and sprites follow Minecraft's Gui class (gui scale = screenH/240).
#include "mc_client.h"
#include "mc_draw.h"
#include "mc_state.h"
#include "mc_sounds_gen.h"
#include "mc_tex.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mc
{
struct Toast
{
	char text[200];
	double time;
};
static Toast g_chat[8];
static int g_chatHead = 0;
static char g_actionBar[200];
static double g_actionTime = -100.0;

// Minecraft's advancement toast (AdvancementToast in ToastComponent): slides in at the top right
struct AdvToast
{
	int frame;
	char icon[48];
	char title[64];
	double start = -100.0;
};
static AdvToast g_adv[5];
static int g_advCount = 0;

static void AdvToastPush(const char* spec)
{
	AdvToast t;
	t.frame = atoi(spec);
	const char* a = strchr(spec, '|');
	const char* b = a ? strchr(a + 1, '|') : nullptr;
	if (!a || !b)
		return;
	snprintf(t.icon, sizeof(t.icon), "%.*s", (int)(b - a - 1), a + 1);
	snprintf(t.title, sizeof(t.title), "%s", b + 1);
	if (g_advCount == 5)
	{
		for (int i = 1; i < 5; i++)
			g_adv[i - 1] = g_adv[i];
		g_advCount = 4;
	}
	g_adv[g_advCount++] = t;
}

static void DrawAdvToasts(float s)
{
	const double SHOW = 5.0, ANIM = 0.6;
	int slot = 0;
	for (int i = 0; i < g_advCount; i++)
	{
		AdvToast& t = g_adv[i];
		if (t.start < 0.0)
		{
			if (slot >= 5)
				break;
			t.start = g_cl.time; // becomes visible now
			SoundPlay(t.frame == 2 ? mcs::MCS_TOAST_CHALLENGE : mcs::MCS_TOAST_IN, nullptr, 1.0f, 1.0f, 0);
		}
		double age = g_cl.time - t.start;
		if (age > SHOW + ANIM)
		{
			for (int k = i + 1; k < g_advCount; k++)
				g_adv[k - 1] = g_adv[k];
			g_advCount--;
			i--;
			continue;
		}
		// slide in, hold, slide out (ToastInstance.getVisibility)
		float vis = age < ANIM ? (float)(age / ANIM) : age > SHOW ? 1.0f - (float)((age - SHOW) / ANIM) : 1.0f;
		vis = vis * vis * (3.0f - 2.0f * vis);
		float x = g_cl.screenW - 160 * s * vis, y = slot * 32 * s;
		mcdraw::BlitFull("gui/sprites/toast/advancement", x, y, 160 * s, 32 * s);
		static const char* kinds[] = {"Advancement Made!", "Goal Reached!", "Challenge Complete!"};
		unsigned col = t.frame == 2 ? 0xFF88FFFF : 0xFFFF00FF;
		mcdraw::Text(kinds[t.frame >= 0 && t.frame <= 2 ? t.frame : 0], x + 30 * s, y + 7 * s, s, col, false);
		mcdraw::Text(t.title, x + 30 * s, y + 18 * s, s, 0xFFFFFFFF, false);
		int icon = mci::FindItem(t.icon);
		if (icon > 0)
			mcdraw::ItemIcon(icon, x + 8 * s, y + 8 * s, 16 * s);
		slot++;
	}
}

void HudToast(int kind, const char* text)
{
	if (kind == 2)
	{
		AdvToastPush(text);
		return;
	}
	if (kind == 1)
	{
		strncpy(g_actionBar, text, sizeof(g_actionBar) - 1);
		g_actionBar[sizeof(g_actionBar) - 1] = 0;
		g_actionTime = g_cl.time;
		return;
	}
	Toast& t = g_chat[g_chatHead];
	strncpy(t.text, text, sizeof(t.text) - 1);
	t.text[sizeof(t.text) - 1] = 0;
	t.time = g_cl.time;
	g_chatHead = (g_chatHead + 1) % 8;
	// console copy without the colour shortcuts (\r \y \g \w \d \aq)
	char plain[256];
	int n = 0;
	for (const char* p = text; *p && n < (int)sizeof(plain) - 1; p++)
	{
		if (*p == '\\' && p[1])
		{
			p += (p[1] == 'a' && p[2] == 'q') ? 2 : 1;
			continue;
		}
		plain[n++] = *p;
	}
	plain[n] = 0;
	gEngfuncs.Con_Printf((char*)"[MC] %s\n", plain);
}

// ---------------------------------------------------------------------------------------------
// CS weapon icons for hotbar slots 1-5 (drawn from the game's own HUD sprites)

static const char* kCsWeaponNames[32] = {nullptr, "p228", nullptr, "scout", "hegrenade", "xm1014", "c4", "mac10", "aug",
	"smokegrenade", "elite", "fiveseven", "ump45", "sg550", "galil", "famas", "usp", "glock18", "awp", "mp5navy", "m249",
	"m3", "m4a1", "tmp", "g3sg1", "flashbang", "deagle", "sg552", "ak47", "knife", "p90", nullptr};

struct WeaponIcon
{
	bool tried = false;
	HSPRITE spr = 0;
	wrect_t rc = {};
};
static WeaponIcon g_icons[32];

static WeaponIcon* GetWeaponIcon(int id)
{
	if (id <= 0 || id >= 32 || !kCsWeaponNames[id])
		return nullptr;
	WeaponIcon& wi = g_icons[id];
	if (!wi.tried)
	{
		wi.tried = true;
		char path[128];
		snprintf(path, sizeof(path), "sprites/weapon_%s.txt", kCsWeaponNames[id]);
		int count = 0;
		client_sprite_t* list = gEngfuncs.pfnSPR_GetList(path, &count);
		for (int i = 0; list && i < count; i++)
		{
			if (!strcmp(list[i].szName, "weapon") && list[i].iRes == 640)
			{
				char spr[128];
				snprintf(spr, sizeof(spr), "sprites/%s.spr", list[i].szSprite);
				wi.spr = gEngfuncs.pfnSPR_Load(spr);
				wi.rc = list[i].rc;
				break;
			}
		}
	}
	return wi.spr ? &wi : nullptr;
}

static void DrawCsWeapon(int id, float x, float y, float size, bool selected)
{
	// Minecraft-style item icon made from Counter-Strike's own weapon picture (tools/assets/gun_icons.py)
	if (id > 0 && id < 32 && kCsWeaponNames[id])
	{
		char rel[48];
		snprintf(rel, sizeof(rel), "item/cs_%s", kCsWeaponNames[id]);
		if (mctex::Get(rel).id)
		{
			glEnable(GL_TEXTURE_2D);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			mcdraw::BlitFull(rel, x, y, size, size);
			return;
		}
	}
	WeaponIcon* wi = GetWeaponIcon(id);
	if (!wi)
	{
		if (id > 0 && id < 32 && kCsWeaponNames[id])
		{
			char buf[8];
			strncpy(buf, kCsWeaponNames[id], 4);
			buf[4] = 0;
			mcdraw::TextCentered(buf, x + size * 0.5f, y + size * 0.35f, size / 24.0f, 0xFFD080FF);
		}
		return;
	}
	const model_s* m = gEngfuncs.GetSpritePointer(wi->spr);
	if (!m)
		return;
	int fw = gEngfuncs.pfnSPR_Width(wi->spr, 0), fh = gEngfuncs.pfnSPR_Height(wi->spr, 0);
	gEngfuncs.pTriAPI->SpriteTexture((struct model_s*)m, 0);
	// the icon is a wide rectangle; fit it into the slot keeping aspect
	float rw = (float)(wi->rc.right - wi->rc.left), rh = (float)(wi->rc.bottom - wi->rc.top);
	float s = size * 1.25f / rw;
	float w = rw * s, h = rh * s;
	float cx = x + size * 0.5f, cy = y + size * 0.5f;
	glEnable(GL_TEXTURE_2D);
	glBlendFunc(GL_ONE, GL_ONE);
	if (selected)
		glColor4f(1.0f, 0.75f, 0.3f, 1.0f);
	else
		glColor4f(0.8f, 0.55f, 0.2f, 1.0f);
	float u0 = wi->rc.left / (float)fw, u1 = wi->rc.right / (float)fw, v0 = wi->rc.top / (float)fh, v1 = wi->rc.bottom / (float)fh;
	glBegin(GL_QUADS);
	glTexCoord2f(u0, v0);
	glVertex2f(cx - w * 0.5f, cy - h * 0.5f);
	glTexCoord2f(u1, v0);
	glVertex2f(cx + w * 0.5f, cy - h * 0.5f);
	glTexCoord2f(u1, v1);
	glVertex2f(cx + w * 0.5f, cy + h * 0.5f);
	glTexCoord2f(u0, v1);
	glVertex2f(cx - w * 0.5f, cy + h * 0.5f);
	glEnd();
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void HudInit() {}
void HudVidInit()
{
	for (auto& i : g_icons)
		i = WeaponIcon();
}

float GuiScale()
{
	int s = g_cl.screenH / 240;
	if (s < 1)
		s = 1;
	if (s > 4)
		s = 4;
	return (float)s;
}

static void DrawDurability(int itemId, int damage, float x, float y, float s)
{
	const mci::ItemDef& d = mci::Item(itemId);
	if (d.durability <= 0 || damage <= 0)
		return;
	float frac = 1.0f - damage / (float)d.durability;
	if (frac < 0)
		frac = 0;
	int w = (int)roundf(13.0f * frac);
	float hue = frac / 3.0f; // green -> red
	float r = 0, g = 0, b = 0;
	{
		float h6 = hue * 6.0f;
		float c = 1.0f, xx = c * (1.0f - fabsf(fmodf(h6, 2.0f) - 1.0f));
		if (h6 < 1) { r = c; g = xx; }
		else { r = xx; g = c; }
	}
	mcdraw::Rect(x + 2 * s, y + 13 * s, 13 * s, 2 * s, 0x000000FF);
	unsigned col = ((unsigned)(r * 255) << 24) | ((unsigned)(g * 255) << 16) | ((unsigned)(b * 255) << 8) | 0xFF;
	mcdraw::Rect(x + 2 * s, y + 13 * s, w * s, 1 * s, col);
}

// CS weapons as inventory entries (negative ids in the inventory messages)
void DrawCsWeaponIcon(int id, float x, float y, float size) { DrawCsWeapon(id, x, y, size, false); }
const char* CsWeaponDisplayName(int id)
{
	static const struct
	{
		const char* name;
		const char* display;
	} kNames[] = {{"p228", "P228"}, {"scout", "Scout"}, {"hegrenade", "HE Grenade"}, {"xm1014", "XM1014"}, {"c4", "C4"},
		{"mac10", "MAC-10"}, {"aug", "AUG"}, {"smokegrenade", "Smoke Grenade"}, {"elite", "Dual Elites"}, {"fiveseven", "Five-SeveN"},
		{"ump45", "UMP45"}, {"sg550", "SG 550"}, {"galil", "Galil"}, {"famas", "FAMAS"}, {"usp", "USP"}, {"glock18", "Glock-18"},
		{"awp", "AWP"}, {"mp5navy", "MP5"}, {"m249", "M249"}, {"m3", "M3"}, {"m4a1", "M4A1"}, {"tmp", "TMP"}, {"g3sg1", "G3SG1"},
		{"flashbang", "Flashbang"}, {"deagle", "Desert Eagle"}, {"sg552", "SG 552"}, {"ak47", "AK-47"}, {"knife", "Knife"},
		{"p90", "P90"}};
	if (id <= 0 || id >= 32 || !kCsWeaponNames[id])
		return "Counter-Strike weapon";
	for (const auto& n : kNames)
		if (!strcmp(n.name, kCsWeaponNames[id]))
			return n.display;
	return kCsWeaponNames[id];
}

void DrawSlotItem(int slot, float x, float y, float s)
{
	int id = g_cl.hotbarId[slot];
	if (id > 0)
	{
		mcdraw::ItemIcon(id, x, y, 16 * s);
		int count = g_cl.hotbarCount[slot];
		if (count > 1)
		{
			char buf[8];
			snprintf(buf, sizeof(buf), "%d", count);
			float tw = mcdraw::TextWidth(buf) * s;
			mcdraw::Text(buf, x + 17 * s - tw, y + 9 * s, s, 0xFFFFFFFF);
		}
		DrawDurability(id, g_cl.hotbarDamage[slot], x, y, s);
	}
	else if (id < 0)
	{
		DrawCsWeapon(-id, x, y, 16 * s, slot == g_cl.selected);
	}
}

static void DrawHearts(float x, float y, float s)
{
	int hp = (g_cl.health + 4) / 5; // Minecraft health points (half hearts)
	if (hp < 0)
		hp = 0;
	if (hp > 20)
		hp = 20;
	bool low = hp <= 4;
	for (int i = 0; i < 10; i++)
	{
		float hx = x + i * 8 * s;
		float hy = y;
		if (low)
			hy += (float)((rand() % 2)) * s;
		mcdraw::BlitFull("gui/sprites/hud/heart/container", hx, hy, 9 * s, 9 * s);
		if (i * 2 + 1 < hp)
			mcdraw::BlitFull("gui/sprites/hud/heart/full", hx, hy, 9 * s, 9 * s);
		else if (i * 2 + 1 == hp)
			mcdraw::BlitFull("gui/sprites/hud/heart/half", hx, hy, 9 * s, 9 * s);
	}
}

// Gui.renderFood: drumsticks right-aligned over the hotbar; they jitter when saturation is gone
static void DrawFood(float right, float y, float s)
{
	int f = g_cl.food;
	for (int i = 0; i < 10; i++)
	{
		float fx = right - i * 8 * s - 9 * s;
		float fy = y;
		if (g_cl.saturation <= 0 && (rand() % (f * 3 + 1)) == 0)
			fy += (float)(rand() % 3 - 1) * s;
		mcdraw::BlitFull("gui/sprites/hud/food_empty", fx, fy, 9 * s, 9 * s);
		if (i * 2 + 1 < f)
			mcdraw::BlitFull("gui/sprites/hud/food_full", fx, fy, 9 * s, 9 * s);
		else if (i * 2 + 1 == f)
			mcdraw::BlitFull("gui/sprites/hud/food_half", fx, fy, 9 * s, 9 * s);
	}
}

// golden absorption hearts (golden apples), on the row above the health hearts
static void DrawAbsorption(float x, float y, float s)
{
	int a = g_cl.absorption;
	for (int i = 0; i * 2 < a && i < 10; i++)
	{
		float hx = x + i * 8 * s;
		mcdraw::BlitFull("gui/sprites/hud/heart/container", hx, y, 9 * s, 9 * s);
		mcdraw::BlitFull(i * 2 + 1 < a ? "gui/sprites/hud/heart/absorbing_full" : "gui/sprites/hud/heart/absorbing_half", hx, y, 9 * s, 9 * s);
	}
}

static void DrawArmor(float x, float y, float s)
{
	int a = g_cl.armorPoints;
	if (a <= 0)
		return;
	for (int i = 0; i < 10; i++)
	{
		float ax = x + i * 8 * s;
		const char* t = (i * 2 + 1 < a) ? "gui/sprites/hud/armor_full" : (i * 2 + 1 == a) ? "gui/sprites/hud/armor_half" : "gui/sprites/hud/armor_empty";
		mcdraw::BlitFull(t, ax, y, 9 * s, 9 * s);
	}
}

static void DrawXp(float x, float y, float s)
{
	mcdraw::BlitFull("gui/sprites/hud/experience_bar_background", x, y, 182 * s, 5 * s);
	float frac = g_cl.xpNeed > 0 ? g_cl.xpInto / (float)g_cl.xpNeed : 0.0f;
	if (frac > 1)
		frac = 1;
	int w = (int)(frac * 183.0f);
	if (w > 0)
		mcdraw::Blit("gui/sprites/hud/experience_bar_progress", x, y, w * s, 5 * s, 0, 0, (float)w, 5);
	if (g_cl.xpLevel > 0)
	{
		char buf[16];
		snprintf(buf, sizeof(buf), "%d", g_cl.xpLevel);
		float cx = x + 91 * s;
		float ty = y - 6 * s;
		// black outline then lime text (Gui.renderExperienceLevel)
		static const float off[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
		for (auto& o : off)
			mcdraw::TextCentered(buf, cx + o[0] * s, ty + o[1] * s, s, 0x000000FF, false);
		mcdraw::TextCentered(buf, cx, ty, s, 0x80FF20FF, false);
	}
}

static void DrawCrosshair(float s)
{
	float cx = g_cl.screenW * 0.5f, cy = g_cl.screenH * 0.5f;
	// inverted-colour crosshair like Minecraft
	glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR);
	mcdraw::BlitFull("gui/sprites/hud/crosshair", cx - 7.5f * s, cy - 7.5f * s, 15 * s, 15 * s);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	// attack indicator
	const mci::ItemDef* d = HeldItem();
	float speed = d ? d->attackSpeed : 4.0f;
	float strength = (float)((g_cl.time - g_cl.lastSwing + 0.025) * speed);
	if (strength < 1.0f)
	{
		float ix = cx - 8 * s, iy = cy + 9 * s;
		mcdraw::BlitFull("gui/sprites/hud/crosshair_attack_indicator_background", ix, iy, 16 * s, 4 * s);
		int w = (int)(strength * 17.0f);
		if (w > 0)
			mcdraw::Blit("gui/sprites/hud/crosshair_attack_indicator_progress", ix, iy, w * s, 4 * s, 0, 0, (float)w, 4);
	}
}

static void DrawToasts(float s, float hotbarY)
{
	float y = hotbarY - 48 * s;
	int shown = 0;
	for (int k = 0; k < 8; k++)
	{
		int idx = (g_chatHead - 1 - k + 16) % 8;
		Toast& t = g_chat[idx];
		if (!t.text[0])
			continue;
		double age = g_cl.time - t.time;
		if (age > 10.0 || age < 0)
			continue;
		float a = age > 9.0 ? (float)(10.0 - age) : 1.0f;
		float w = (mcdraw::TextWidth(t.text) + 4) * s;
		mcdraw::Rect(2 * s, y - 1 * s, w, 9 * s, (unsigned)(a * 128) & 0xFF);
		mcdraw::Text(t.text, 4 * s, y, s, 0xFFFFFF00 | (unsigned)(a * 255));
		y -= 9 * s;
		if (++shown >= 6)
			break;
	}
	double aage = g_cl.time - g_actionTime;
	if (aage >= 0 && aage < 3.0)
	{
		float a = aage > 2.0 ? (float)(3.0 - aage) : 1.0f;
		mcdraw::TextCentered(g_actionBar, g_cl.screenW * 0.5f, hotbarY - 46 * s, s, 0xFFFFFF00 | (unsigned)(a * 255));
	}
}

void HudDraw()
{
	if (!g_cl.haveInv)
		return;
	cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	// spectators, and players still on the intro camera choosing a team (the server keeps them EF_NODRAW)
	bool spectating = !local || local->curstate.iuser1 != 0 || (local->curstate.effects & EF_NODRAW) != 0;
	float s = GuiScale();
	float W = (float)g_cl.screenW, H = (float)g_cl.screenH;
	mcdraw::Begin2D(g_cl.screenW, g_cl.screenH);
	float hbW = 182 * s, hbH = 22 * s;
	float hx = (W - hbW) * 0.5f, hy = H - hbH;

	if (!spectating && g_cl.health > 0)
	{
		if (g_cl.mcItemActive && !CL_IsThirdPersonLocal())
			DrawCrosshair(s); // Minecraft has no crosshair in third person
		mcdraw::BlitFull("gui/sprites/hud/hotbar", hx, hy, hbW, hbH);
		mcdraw::BlitFull("gui/sprites/hud/hotbar_selection", hx - 1 * s + g_cl.selected * 20 * s, hy - 1 * s, 24 * s, 23 * s);
		for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
			DrawSlotItem(i, hx + (3 + i * 20) * s, hy + 3 * s, s);
		float statY = H - 39 * s;
		DrawHearts(hx, statY, s);
		DrawFood(hx + 182 * s, statY, s);
		if (g_cl.absorption > 0)
		{
			DrawAbsorption(hx, statY - 10 * s, s);
			statY -= 10 * s; // the armor bar moves up a row, like Minecraft's
		}
		DrawArmor(hx, statY - 10 * s, s);
		DrawXp(hx, H - 29 * s, s);
		// held item name (fades after 2 s)
		int held = HeldItemId();
		double nage = g_cl.time - g_cl.heldNameTime;
		if (held && nage >= 0 && nage < 2.5)
		{
			const mci::ItemDef& d = mci::Item(held);
			float a = nage > 2.0 ? (float)((2.5 - nage) / 0.5) : 1.0f;
			unsigned col = (mcdraw::RarityColor(d.rarity) & 0xFFFFFF00) | (unsigned)(a * 255);
			mcdraw::TextCentered(d.display, W * 0.5f, H - 59 * s, s, col);
		}
	}
	// Minecraft death screen (DeathScreen): red gradient, "You died!", score
	double dage = g_cl.time - g_cl.deathTime;
	if (g_cl.health <= 0 && dage >= 0 && dage < 5.0)
	{
		float a = dage > 4.0 ? (float)(5.0 - dage) : 1.0f;
		glDisable(GL_TEXTURE_2D);
		glBegin(GL_QUADS);
		glColor4f(0.31f, 0.0f, 0.0f, 0.38f * a);
		glVertex2f(0, 0);
		glVertex2f(W, 0);
		glColor4f(0.63f, 0.18f, 0.18f, 0.63f * a);
		glVertex2f(W, H);
		glVertex2f(0, H);
		glEnd();
		glEnable(GL_TEXTURE_2D);
		unsigned al = (unsigned)(a * 255);
		mcdraw::TextCentered("You died!", W * 0.5f, H * 0.3f, s * 2.0f, 0xFFFFFF00 | al);
		char score[64];
		snprintf(score, sizeof(score), "Score: \\y%d", g_cl.xpLevel * 7 + g_cl.xpInto);
		mcdraw::TextCentered(score, W * 0.5f, H * 0.3f + 30 * s, s, 0xFFFFFF00 | al);
	}
	DrawToasts(s, hy);
	DrawAdvToasts(s);
	mcdraw::End2D();
}
} // namespace mc
