// Minecraft humanoid rendering for players and bots (replaces CS player models on screen; hit boxes
// stay CS's). Port of HumanoidModel/PlayerModel geometry + setupAnim, armor layers, held items, and
// Counter-Strike guns turned into Minecraft-style extruded items built from their HUD icons.
#include "mc_blocks.h"
#include "mc_chars.h"
#include "mc_client.h"
#include "mc_draw.h"
#include "mc_state.h"
#include "mc_tex.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>

namespace mc
{
static const float PI = 3.14159265358979f;
static const float PX = 72.0f / 32.0f; // engine units per model pixel (CS player 72 units = 32 px)
static float g_light = 1.0f;            // world light at the entity being drawn
static float g_flash = 0.0f;
static float g_tint[3] = {1.0f, 1.0f, 1.0f}; // dyed leather            // >0: draw cubes as a flat white overlay with this alpha (creeper fuse)
float WorldLightAtPos(const float* p);

static cvar_t* g_cvSteve = nullptr;

struct PlayerAnim
{
	float lastOrigin[3] = {};
	double lastTime = 0;
	float limbSwing = 0, limbAmount = 0;
	double deathStart = -1;
	bool wasDead = false;
	char skin[96] = "";
	int skinFor = -1; // name hash the skin was chosen for
	float swell = 0;  // creeper fuse (ticks), eased toward the server's value
};
static PlayerAnim g_anim[33];
static std::vector<cl_entity_t*> g_players;

// characters chosen with mc_character (mc_chars.h), per player index, from MCMSG_CHAR
int g_charOf[33];
void SetPlayerCharacter(int index, int c)
{
	if (index >= 1 && index <= 32)
		g_charOf[index] = c;
}

bool PlayersEnabled()
{
	if (!g_cvSteve)
		g_cvSteve = gEngfuncs.pfnRegisterVariable((char*)"mc_players", (char*)"1", FCVAR_ARCHIVE);
	// classic maps keep Counter-Strike's own CT and T models (mc_players 2 forces Minecraft models)
	extern bool WorldIsClassic();
	if (WorldIsClassic() && (!g_cvSteve || g_cvSteve->value < 2.0f))
		return false;
	return g_cvSteve && g_cvSteve->value != 0.0f;
}

// Called from HUD_AddEntity for player entities. Returns true if we take over rendering.
bool PlayerTakeOver(cl_entity_t* ent)
{
	if (!ent || !ent->player || ent->index < 1 || ent->index > 32)
		return false;
	int c = g_charOf[ent->index];
	bool mine = mcp::IsMinecraftCharacter(c);
	bool cs = c > 0 && !mine; // a Counter-Strike model was picked: the engine draws it
	if (!mine && (cs || !PlayersEnabled()))
		return false;
	g_players.push_back(ent);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Skins

static const char* PickSkin(int index)
{
	PlayerAnim& a = g_anim[index];
	hud_player_info_t info = {};
	gEngfuncs.pfnGetPlayerInfo(index, &info);
	const char* name = info.name ? info.name : "";
	int h = 0;
	for (const char* p = name; *p; p++)
		h = h * 31 + *p;
	cl_entity_t* e = gEngfuncs.GetEntityByIndex(index);
	int team = e ? e->curstate.team : 0;
	h ^= team * 7919;
	int ch = g_charOf[index];
	h ^= ch * 104729;
	if (a.skinFor == h && a.skin[0])
		return a.skin;
	a.skinFor = h;
	if (mcp::IsMinecraftCharacter(ch))
	{
		strncpy(a.skin, mcp::kCharacters[ch].skin, sizeof(a.skin) - 1);
		a.skin[sizeof(a.skin) - 1] = 0;
		return a.skin;
	}
	struct Map
	{
		const char* name;
		const char* skin;
	};
	static const Map byName[] = {{"Creeper", "entity/creeper/creeper"}, {"Enderman", "entity/enderman/enderman"}, {"Zombie", "entity/zombie/zombie"}, {"Husk", "entity/zombie/husk"}, {"Drowned", "entity/zombie/drowned"},
		{"Steve", "entity/player/wide/steve"},
		{"Alex", "entity/player/slim/alex"}, {"Herobrine", "entity/player/wide/steve"}, {"Notch", "entity/player/wide/steve"}};
	for (auto& m : byName)
		if (strstr(name, m.name))
		{
			strncpy(a.skin, m.skin, sizeof(a.skin) - 1);
			return a.skin;
		}
	static const char* defaults[] = {"steve", "alex", "ari", "efe", "kai", "makena", "noor", "sunny", "zuri"};
	static const char* tSkins[] = {"entity/zombie/husk", "entity/zombie/zombie", "entity/zombie/drowned", "entity/zombie/husk"};
	// CS model team colour isn't in the entity state reliably; use the scoreboard team instead
	bool terrorist = info.model && (strstr(info.model, "terror") || strstr(info.model, "leet") || strstr(info.model, "arctic") || strstr(info.model, "guerilla"));
	unsigned u = (unsigned)h;
	if (terrorist)
		strncpy(a.skin, tSkins[u % 4], sizeof(a.skin) - 1);
	else
		snprintf(a.skin, sizeof(a.skin), "entity/player/wide/%s", defaults[u % 9]);
	return a.skin;
}

// ---------------------------------------------------------------------------------------------
// Geometry (model space: pixels, y down, front = -z), same UV layout as ModelPart.Cube

static void Cube(float x0, float y0, float z0, float sx, float sy, float sz, float u, float v, float inflate, float tw, float th, bool mirror)
{
	float x1 = x0 + sx, y1 = y0 + sy, z1 = z0 + sz;
	x0 -= inflate; y0 -= inflate; z0 -= inflate;
	x1 += inflate; y1 += inflate; z1 += inflate;
	if (mirror)
	{
		float t = x0;
		x0 = x1;
		x1 = t;
	}
	auto q = [&](float u0, float v0, float u1, float v1, const float* a, const float* b, const float* c, const float* d, float shade) {
		shade *= g_light;
		if (g_flash > 0.0f)
			glColor4f(1.0f, 1.0f, 1.0f, g_flash);
		else
			glColor3f(shade * g_tint[0], shade * g_tint[1], shade * g_tint[2]);
		if (mirror)
		{
			float t = u0;
			u0 = u1;
			u1 = t;
		}
		glTexCoord2f(u0 / tw, v0 / th); glVertex3fv(a);
		glTexCoord2f(u1 / tw, v0 / th); glVertex3fv(b);
		glTexCoord2f(u1 / tw, v1 / th); glVertex3fv(c);
		glTexCoord2f(u0 / tw, v1 / th); glVertex3fv(d);
	};
	float p000[3] = {x0, y0, z0}, p100[3] = {x1, y0, z0}, p110[3] = {x1, y1, z0}, p010[3] = {x0, y1, z0};
	float p001[3] = {x0, y0, z1}, p101[3] = {x1, y0, z1}, p111[3] = {x1, y1, z1}, p011[3] = {x0, y1, z1};
	glBegin(GL_QUADS);
	q(u + sz, v + sz, u + sz + sx, v + sz + sy, p000, p100, p110, p010, 1.0f);                   // front (-z)
	q(u + sz + sx + sz, v + sz, u + sz + sx + sz + sx, v + sz + sy, p101, p001, p011, p111, 0.8f); // back (+z)
	q(u, v + sz, u + sz, v + sz + sy, p001, p000, p010, p011, 0.7f);                             // -x side
	q(u + sz + sx, v + sz, u + sz + sx + sz, v + sz + sy, p100, p101, p111, p110, 0.7f);         // +x side
	q(u + sz, v, u + sz + sx, v + sz, p001, p101, p100, p000, 1.0f);                             // top
	q(u + sz + sx, v, u + sz + sx + sx, v + sz, p010, p110, p111, p011, 0.5f);                   // bottom
	glEnd();
}

struct Part
{
	float px, py, pz;    // pivot
	float xr, yr, zr;    // radians
};

static void PushPart(const Part& p)
{
	glPushMatrix();
	glTranslatef(p.px, p.py, p.pz);
	glRotatef(p.zr * 180.0f / PI, 0, 0, 1);
	glRotatef(p.yr * 180.0f / PI, 0, 1, 0);
	glRotatef(p.xr * 180.0f / PI, 1, 0, 0);
}

// ---------------------------------------------------------------------------------------------
// CS guns as Minecraft items: extrude the weapon's HUD icon silhouette

static const char* WeaponNameFromModel(int modelindex)
{
	if (!modelindex)
		return nullptr;
	model_t* m = gEngfuncs.hudGetModelByIndex(modelindex);
	if (!m || !m->name[0])
		return nullptr;
	const char* n = strstr(m->name, "p_");
	if (!n)
		return nullptr;
	static char buf[64];
	strncpy(buf, n + 2, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	char* dot = strchr(buf, '.');
	if (dot)
		*dot = 0;
	return buf;
}

struct GunMesh
{
	bool tried = false;
	GLuint list = 0;
	float w = 0, h = 0; // pixels
};
static std::unordered_map<std::string, GunMesh> g_guns;

static bool LoadSpriteFrame(const char* path, int& w, int& h, std::vector<uint8_t>& rgba)
{
	int len = 0;
	unsigned char* data = gEngfuncs.COM_LoadFile((char*)path, 5, &len);
	if (!data)
		return false;
	bool ok = false;
	do
	{
		if (len < 60 || memcmp(data, "IDSP", 4))
			break;
		const unsigned char* p = data + 4 + 4 * 9; // header: ident, version, type, texFormat, radius, maxw, maxh, frames, beamlen, synctype
		short palCount = *(short*)p;
		p += 2;
		const unsigned char* pal = p;
		p += palCount * 3;
		int frameType = *(int*)p;
		p += 4;
		if (frameType != 0)
			break;
		p += 8; // origin
		w = *(int*)p;
		h = *(int*)(p + 4);
		p += 8;
		if (p + w * h > data + len)
			break;
		rgba.resize((size_t)w * h * 4);
		for (int i = 0; i < w * h; i++)
		{
			int idx = p[i];
			rgba[i * 4 + 0] = pal[idx * 3 + 0];
			rgba[i * 4 + 1] = pal[idx * 3 + 1];
			rgba[i * 4 + 2] = pal[idx * 3 + 2];
			rgba[i * 4 + 3] = (pal[idx * 3] + pal[idx * 3 + 1] + pal[idx * 3 + 2]) > 60 ? 255 : 0;
		}
		ok = true;
	} while (0);
	gEngfuncs.COM_FreeFile(data);
	return ok;
}

static GunMesh& GetGun(const char* weapon)
{
	GunMesh& g = g_guns[weapon];
	if (g.tried)
		return g;
	g.tried = true;
	char path[128];
	snprintf(path, sizeof(path), "sprites/weapon_%s.txt", weapon);
	int count = 0;
	client_sprite_t* list = gEngfuncs.pfnSPR_GetList(path, &count);
	client_sprite_t* icon = nullptr;
	for (int i = 0; list && i < count; i++)
		if (!strcmp(list[i].szName, "weapon") && list[i].iRes == 640)
			icon = &list[i];
	if (!icon)
		return g;
	char spr[128];
	snprintf(spr, sizeof(spr), "sprites/%s.spr", icon->szSprite);
	int sw, sh;
	std::vector<uint8_t> px;
	if (!LoadSpriteFrame(spr, sw, sh, px))
		return g;
	int x0 = icon->rc.left, y0 = icon->rc.top, rw = icon->rc.right - icon->rc.left, rh = icon->rc.bottom - icon->rc.top;
	// downsample 2x so it reads as chunky Minecraft pixels
	int W = rw / 2, H = rh / 2;
	if (W <= 0 || H <= 0)
		return g;
	std::vector<uint8_t> mask(W * H), bright(W * H);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			int on = 0, b = 0;
			for (int dy = 0; dy < 2; dy++)
				for (int dx = 0; dx < 2; dx++)
				{
					int sx = x0 + x * 2 + dx, sy = y0 + y * 2 + dy;
					if (sx < sw && sy < sh)
					{
						const uint8_t* c = &px[((size_t)sy * sw + sx) * 4];
						on += c[3] ? 1 : 0;
						b += (c[0] + c[1] + c[2]) / 3;
					}
				}
			mask[y * W + x] = on >= 2;
			bright[y * W + x] = (uint8_t)(b / 4);
		}
	g.w = (float)W;
	g.h = (float)H;
	g.list = glGenLists(1);
	glNewList(g.list, GL_COMPILE);
	glDisable(GL_TEXTURE_2D);
	glBegin(GL_QUADS);
	const float d = 0.6f;
	auto on = [&](int x, int y) { return x >= 0 && y >= 0 && x < W && y < H && mask[y * W + x]; };
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			if (!on(x, y))
				continue;
			// gunmetal palette from the icon brightness
			float l = 0.18f + bright[y * W + x] / 255.0f * 0.45f;
			float fx0 = x - W * 0.5f, fx1 = fx0 + 1, fy1 = H * 0.5f - y, fy0 = fy1 - 1;
			glColor3f(l, l, l * 1.05f);
			glVertex3f(fx0, fy0, d); glVertex3f(fx1, fy0, d); glVertex3f(fx1, fy1, d); glVertex3f(fx0, fy1, d);
			glColor3f(l * 0.8f, l * 0.8f, l * 0.85f);
			glVertex3f(fx1, fy0, -d); glVertex3f(fx0, fy0, -d); glVertex3f(fx0, fy1, -d); glVertex3f(fx1, fy1, -d);
			glColor3f(l * 0.7f, l * 0.7f, l * 0.75f);
			if (!on(x, y - 1)) { glVertex3f(fx0, fy1, d); glVertex3f(fx1, fy1, d); glVertex3f(fx1, fy1, -d); glVertex3f(fx0, fy1, -d); }
			if (!on(x, y + 1)) { glVertex3f(fx0, fy0, -d); glVertex3f(fx1, fy0, -d); glVertex3f(fx1, fy0, d); glVertex3f(fx0, fy0, d); }
			if (!on(x - 1, y)) { glVertex3f(fx0, fy0, -d); glVertex3f(fx0, fy0, d); glVertex3f(fx0, fy1, d); glVertex3f(fx0, fy1, -d); }
			if (!on(x + 1, y)) { glVertex3f(fx1, fy0, d); glVertex3f(fx1, fy0, -d); glVertex3f(fx1, fy1, -d); glVertex3f(fx1, fy1, d); }
		}
	glEnd();
	glEnable(GL_TEXTURE_2D);
	glEndList();
	return g;
}

// ---------------------------------------------------------------------------------------------
// Armor (HumanoidArmorLayer): outer layer inflate 1.0, leggings inflate 0.5

static const char* ArmorMaterial(int code)
{
	static const char* m[8] = {nullptr, "leather", "chainmail", "iron", "gold", "diamond", "netherite", "turtle_scute"};
	return m[code & 7];
}

static void DrawBody(const char* skin, bool legacy, const Part& head, const Part& body, const Part& rarm, const Part& larm,
	const Part& rleg, const Part& lleg, int armor, float tint[3])
{
	float th = legacy ? 32.0f : 64.0f;
	mctex::Bind(skin);
	glColor3f(1, 1, 1);
	// base layer
	PushPart(head); Cube(-4, -8, -4, 8, 8, 8, 0, 0, 0, 64, th, false); if (!legacy) Cube(-4, -8, -4, 8, 8, 8, 32, 0, 0.5f, 64, th, false); glPopMatrix();
	PushPart(body); Cube(-4, 0, -2, 8, 12, 4, 16, 16, 0, 64, th, false); if (!legacy) Cube(-4, 0, -2, 8, 12, 4, 16, 32, 0.25f, 64, th, false); glPopMatrix();
	PushPart(rarm); Cube(-3, -2, -2, 4, 12, 4, 40, 16, 0, 64, th, false); if (!legacy) Cube(-3, -2, -2, 4, 12, 4, 40, 32, 0.25f, 64, th, false); glPopMatrix();
	PushPart(larm);
	if (legacy) Cube(-1, -2, -2, 4, 12, 4, 40, 16, 0, 64, th, true);
	else { Cube(-1, -2, -2, 4, 12, 4, 32, 48, 0, 64, th, false); Cube(-1, -2, -2, 4, 12, 4, 48, 48, 0.25f, 64, th, false); }
	glPopMatrix();
	PushPart(rleg); Cube(-2, 0, -2, 4, 12, 4, 0, 16, 0, 64, th, false); if (!legacy) Cube(-2, 0, -2, 4, 12, 4, 0, 32, 0.25f, 64, th, false); glPopMatrix();
	PushPart(lleg);
	if (legacy) Cube(-2, 0, -2, 4, 12, 4, 0, 16, 0, 64, th, true);
	else { Cube(-2, 0, -2, 4, 12, 4, 16, 48, 0, 64, th, false); Cube(-2, 0, -2, 4, 12, 4, 0, 48, 0.25f, 64, th, false); }
	glPopMatrix();

	// armor
	const char* helm = ArmorMaterial(armor);
	const char* chest = ArmorMaterial(armor >> 3);
	const char* legs = ArmorMaterial(armor >> 6);
	const char* boots = ArmorMaterial(armor >> 9);
	char tex[128];
	// one armor piece; undyed leather is the grey layer tinted #A06540 plus an untinted overlay
	auto piece = [&](const char* mat, bool leggings, auto&& cubes) {
		const char* layer = leggings ? "humanoid_leggings" : "humanoid";
		bool leather = !strcmp(mat, "leather");
		snprintf(tex, sizeof(tex), "entity/equipment/%s/%s", layer, mat);
		mctex::Bind(tex);
		if (leather)
		{
			g_tint[0] = 0xA0 / 255.0f;
			g_tint[1] = 0x65 / 255.0f;
			g_tint[2] = 0x40 / 255.0f;
		}
		cubes();
		g_tint[0] = g_tint[1] = g_tint[2] = 1.0f;
		if (leather)
		{
			snprintf(tex, sizeof(tex), "entity/equipment/%s/leather_overlay", layer);
			mctex::Bind(tex);
			cubes();
		}
	};
	if (helm)
		piece(helm, false, [&]() { PushPart(head); Cube(-4, -8, -4, 8, 8, 8, 0, 0, 1.0f, 64, 32, false); glPopMatrix(); });
	if (chest)
		piece(chest, false, [&]() {
			PushPart(body); Cube(-4, 0, -2, 8, 12, 4, 16, 16, 1.0f, 64, 32, false); glPopMatrix();
			PushPart(rarm); Cube(-3, -2, -2, 4, 12, 4, 40, 16, 1.0f, 64, 32, false); glPopMatrix();
			PushPart(larm); Cube(-1, -2, -2, 4, 12, 4, 40, 16, 1.0f, 64, 32, true); glPopMatrix();
		});
	if (legs)
		piece(legs, true, [&]() {
			PushPart(body); Cube(-4, 0, -2, 8, 12, 4, 16, 16, 0.5f, 64, 32, false); glPopMatrix();
			PushPart(rleg); Cube(-2, 0, -2, 4, 12, 4, 0, 16, 0.5f, 64, 32, false); glPopMatrix();
			PushPart(lleg); Cube(-2, 0, -2, 4, 12, 4, 0, 16, 0.5f, 64, 32, true); glPopMatrix();
		});
	if (boots)
		piece(boots, false, [&]() {
			PushPart(rleg); Cube(-2, 0, -2, 4, 12, 4, 0, 16, 1.0f, 64, 32, false); glPopMatrix();
			PushPart(lleg); Cube(-2, 0, -2, 4, 12, 4, 0, 16, 1.0f, 64, 32, true); glPopMatrix();
		});
}

float PerspectiveCameraDistance();

static void DrawPlayer(cl_entity_t* e)
{
	int idx = e->index;
	cl_entity_t* me = gEngfuncs.GetLocalPlayer();
	if (me && e->index == me->index && PerspectiveIsThirdPerson() && PerspectiveCameraDistance() < 36.0f)
		return; // the camera is backed into a wall: we'd only see the inside of our own head
	PlayerAnim& a = g_anim[idx];
	{
		float p[3] = {e->origin[0], e->origin[1], e->origin[2]};
		g_light = 0.35f + 0.65f * WorldLightAtPos(p);
	}
	const char* skin = PickSkin(idx);
	const mctex::Tex& st = mctex::Get(skin);
	if (!st.id)
		return;
	bool legacy = st.h == 32 || strstr(skin, "skeleton") != nullptr;
	bool creeper = strstr(skin, "creeper") != nullptr;
	bool enderman = strstr(skin, "enderman") != nullptr;
	bool zombie = strstr(skin, "zombie") != nullptr;

	// limb swing from horizontal movement (WalkAnimationState)
	double dt = g_cl.time - a.lastTime;
	if (dt > 0.0 && dt < 0.5)
	{
		float dx = e->origin[0] - a.lastOrigin[0], dy = e->origin[1] - a.lastOrigin[1];
		float dist = sqrtf(dx * dx + dy * dy) / 40.0f; // blocks
		float ticks = (float)(dt * 20.0);
		float speed = ticks > 0 ? dist / ticks : 0;
		float target = fminf(1.0f, speed * 4.0f);
		a.limbAmount += (target - a.limbAmount) * fminf(1.0f, 0.4f * ticks);
		a.limbSwing += a.limbAmount * ticks;
	}
	{
		// the server sends whole ticks at 20 Hz; ease toward them so the swell is smooth
		float target = (float)mcp::SwellOf(e->curstate.iuser4);
		float step = (dt > 0.0 && dt < 0.5) ? (float)dt * 20.0f : 1.0f;
		a.swell += fmaxf(-step, fminf(step, target - a.swell));
		if (target == 0.0f && a.swell > 0.0f && a.swell < 1.0f)
			a.swell = 0.0f;
	}
	a.lastTime = g_cl.time;
	for (int i = 0; i < 3; i++)
		a.lastOrigin[i] = e->origin[i];

	bool dead = e->curstate.health <= 0 && e->curstate.solid == SOLID_NOT;
	if (dead && !a.wasDead)
		a.deathStart = g_cl.time;
	a.wasDead = dead;
	if (dead && g_cl.time - a.deathStart > 1.2)
		return; // Minecraft removes the body after the death animation (server hides it too)

	float age = (float)(g_cl.time * 20.0);
	float ls = a.limbSwing, la = a.limbAmount;
	float pitch = -e->angles[0] * 3.0f;
	if (pitch > 89)
		pitch = 89;
	if (pitch < -89)
		pitch = -89;
	bool crouch = e->curstate.usehull == 1;
	bool gliding = (e->curstate.iuser4 & mcp::MCPF_GLIDING) != 0;

	Part head = {0, 0, 0, pitch * PI / 180.0f, 0, 0};
	Part body = {0, 0, 0, 0, 0, 0};
	Part rarm = {-5, 2, 0, cosf(ls * 0.6662f + PI) * 2.0f * la * 0.5f, 0, 0};
	Part larm = {5, 2, 0, cosf(ls * 0.6662f) * 2.0f * la * 0.5f, 0, 0};
	Part rleg = {-1.9f, 12, 0, cosf(ls * 0.6662f) * 1.4f * la, 0, 0};
	Part lleg = {1.9f, 12, 0, cosf(ls * 0.6662f + PI) * 1.4f * la, 0, 0};
	// idle arm bob (AnimationUtils.bobArms)
	rarm.zr += cosf(age * 0.09f) * 0.05f + 0.05f;
	larm.zr -= cosf(age * 0.09f) * 0.05f + 0.05f;
	rarm.xr += sinf(age * 0.067f) * 0.05f;
	larm.xr -= sinf(age * 0.067f) * 0.05f;

	int held = mcp::HeldItemOf(e->curstate.iuser4);
	const char* gun = held ? nullptr : WeaponNameFromModel(e->curstate.weaponmodel);
	bool knife = gun && !strcmp(gun, "knife");
	if (zombie && !gun)
	{
		rarm.xr = larm.xr = -PI / 2.0f;
	}
	else if (gun && !knife && strcmp(gun, "c4") && !strstr(gun, "grenade") && strcmp(gun, "flashbang"))
	{
		// AnimationUtils.animateCrossbowHold: both arms aiming
		rarm.yr = -0.3f + head.yr;
		larm.yr = 0.6f + head.yr;
		rarm.xr = -PI / 2.0f + head.xr + 0.1f;
		larm.xr = -1.5f + head.xr;
	}
	else if (held || gun)
	{
		rarm.xr = rarm.xr * 0.5f - PI / 10.0f; // ArmPose.ITEM
	}
	if (crouch)
	{
		body.xr = 0.5f;
		rarm.xr += 0.4f;
		larm.xr += 0.4f;
		rleg.pz = lleg.pz = 4.0f;
		rleg.py = lleg.py = 12.2f;
		head.py = 4.2f;
		body.py = 3.2f;
		larm.py = rarm.py = 5.2f;
	}

	// armor visuals: Minecraft armor from the server, else CS kevlar shown as iron
	int armor = e->curstate.playerclass & 0xFFF;
	bool elytra = (e->curstate.iuser4 & mcp::MCPF_ELYTRA) != 0;

	glPushMatrix();
	float feet = e->origin[2] - (crouch ? 18.0f : 36.0f);
	glTranslatef(e->origin[0], e->origin[1], feet);
	glRotatef(e->angles[1], 0, 0, 1);
	if (gliding)
	{
		glTranslatef(0, 0, 36.0f);
		glRotatef(90.0f - pitch, 0, 1, 0);
		glTranslatef(0, 0, -36.0f);
	}
	{
		// LivingEntityRenderer.isEntityUpsideDown: "Dinnerbone" and "Grumm" are drawn upside down
		hud_player_info_t pinfo = {};
		gEngfuncs.pfnGetPlayerInfo(idx, &pinfo);
		if (pinfo.name && (strstr(pinfo.name, "Dinnerbone") || strstr(pinfo.name, "Grumm")))
		{
			glTranslatef(0, 0, 72.0f + 4.0f);
			glRotatef(180.0f, 1, 0, 0);
		}
	}
	if (dead)
	{
		// LivingEntityRenderer death: tip over sideways over ~20 ticks
		float f = (float)((g_cl.time - a.deathStart) * 20.0 - 1.0) / 20.0f * 1.6f;
		f = sqrtf(fmaxf(0.0f, f));
		if (f > 1)
			f = 1;
		glRotatef(f * 90.0f, 1, 0, 0);
	}
	// CreeperRenderer.scale: the fuse inflates the creeper around its feet, wobbling as it goes
	float flash = 0.0f;
	if (creeper && a.swell > 0.0f)
	{
		float f = a.swell / 30.0f;
		float wob = 1.0f + sinf(f * 100.0f) * f * 0.01f;
		f = fminf(1.0f, fmaxf(0.0f, f));
		float f4 = f * f * f * f;
		float xz = (1.0f + f4 * 0.4f) * wob;
		float y = (1.0f + f4 * 0.1f) / wob;
		glScalef(xz, xz, y);
		// CreeperRenderer.getWhiteOverlayProgress: blink white, faster and brighter near the end
		int blink = (int)(f * 10.0f);
		flash = (blink % 2 == 0) ? 0.0f : fminf(1.0f, fmaxf(0.5f, f));
	}
	// model space (x right? y down, front -z) -> world (x fwd, y left, z up), neck 24 px above feet
	glTranslatef(0, 0, 24.0f * PX * (crouch ? 0.75f : 1.0f));
	float s = PX * (crouch ? 0.75f : 1.0f);
	float m[16] = {0, s, 0, 0, 0, 0, -s, 0, -s, 0, 0, 0, 0, 0, 0, 1};
	glMultMatrixf(m);

	bool hurt = g_cl.time - 0.0 < 0; // placeholder (hurt tint handled as red glow shell)
	(void)hurt;
	float tint[3] = {1, 1, 1};
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glDisable(GL_CULL_FACE);
	if (dead)
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	if (enderman)
	{
		// EndermanModel: humanoid on 30 px stick limbs, head raised to y=-13 with an inset jaw layer;
		// limb swing is halved and clamped to 0.4 rad
		mctex::Bind(skin);
		auto clampf = [](float v) { return fmaxf(-0.4f, fminf(0.4f, v)); };
		float ra = clampf(cosf(ls * 0.6662f + PI) * la * 0.5f);
		float lar = clampf(cosf(ls * 0.6662f) * la * 0.5f);
		float rl = clampf(cosf(ls * 0.6662f) * 1.4f * la * 0.5f);
		float ll = clampf(cosf(ls * 0.6662f + PI) * 1.4f * la * 0.5f);
		Part eh = {0, -13, 0, pitch * PI / 180.0f, 0, 0};
		auto head = [&]() { PushPart(eh); Cube(-4, -8, -4, 8, 8, 8, 0, 0, 0, 64, 32, false); glPopMatrix(); };
		head();
		PushPart(eh); Cube(-4, -8, -4, 8, 8, 8, 0, 16, -0.5f, 64, 32, false); glPopMatrix();
		Part eb = {0, -14, 0, 0, 0, 0};
		PushPart(eb); Cube(-4, 0, -2, 8, 12, 4, 32, 16, 0, 64, 32, false); glPopMatrix();
		Part limbs[4] = {{-5, -12, 0, ra, 0, 0}, {5, -12, 0, lar, 0, 0}, {-2, -5, 0, rl, 0, 0}, {2, -5, 0, ll, 0, 0}};
		for (int i = 0; i < 4; i++)
		{
			PushPart(limbs[i]);
			Cube(-1, i < 2 ? -2.0f : 0.0f, -1, 2, 30, 2, 56, 0, 0, 64, 32, (i & 1) != 0);
			glPopMatrix();
		}
		// EnderEyesLayer: the eyes glow (additive, full bright)
		glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE);
		glDisable(GL_ALPHA_TEST);
		glDepthMask(GL_FALSE);
		glDepthFunc(GL_LEQUAL);
		float savedLight = g_light;
		g_light = 1.0f;
		mctex::Bind("entity/enderman/enderman_eyes");
		head();
		g_light = savedLight;
		glPopAttrib();
		glPopMatrix();
		return;
	}
	if (creeper)
	{
		// CreeperModel: head + body on a pivot at y=6, four 4x6x4 legs
		// (legs end at y=24, the same ground line as the player rig)
		mctex::Bind(skin);
		float sw = cosf(ls * 0.6662f) * 1.4f * la;
		auto drawCreeper = [&]() {
			Part ch = {0, 6, 0, pitch * PI / 180.0f, 0, 0};
			PushPart(ch); Cube(-4, -8, -4, 8, 8, 8, 0, 0, 0, 64, 32, false); glPopMatrix();
			Part cb = {0, 6, 0, 0, 0, 0};
			PushPart(cb); Cube(-4, 0, -2, 8, 12, 4, 16, 16, 0, 64, 32, false); glPopMatrix();
			Part legs[4] = {{-2, 18, 4, sw, 0, 0}, {2, 18, 4, -sw, 0, 0}, {-2, 18, -4, -sw, 0, 0}, {2, 18, -4, sw, 0, 0}};
			for (auto& lg : legs)
			{
				PushPart(lg);
				Cube(-2, 0, -2, 4, 6, 4, 0, 16, 0, 64, 32, false);
				glPopMatrix();
			}
		};
		drawCreeper();
		if (flash > 0.0f)
		{
			glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_CURRENT_BIT);
			glDisable(GL_TEXTURE_2D);
			glDisable(GL_ALPHA_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDepthFunc(GL_LEQUAL);
			glDepthMask(GL_FALSE);
			g_flash = flash;
			drawCreeper();
			g_flash = 0.0f;
			glPopAttrib();
		}
		glPopMatrix();
		return;
	}
	DrawBody(skin, legacy, head, body, rarm, larm, rleg, lleg, armor, tint);

	// held item / gun in the right hand (ItemInHandLayer)
	PushPart(rarm);
	glRotatef(-90.0f, 1, 0, 0);
	glRotatef(180.0f, 0, 1, 0);
	glTranslatef(-1.0f, 2.0f, -10.0f); // (-1/16, 0.125, -0.625) blocks in pixels
	if (held)
	{
		const mci::ItemDef& d = mci::Item(held);
		bool isTool = d.type == mci::IT_SWORD || d.type == mci::IT_AXE || d.type == mci::IT_PICKAXE || d.type == mci::IT_SHOVEL || d.type == mci::IT_MACE;
		glScalef(16.0f, 16.0f, 16.0f);
		if (isTool)
		{
			glTranslatef(0, 4.0f / 16.0f, 0.5f / 16.0f);
			glRotatef(-90.0f, 0, 1, 0);
			glRotatef(55.0f, 0, 0, 1);
			glScalef(0.85f, 0.85f, 0.85f);
		}
		else
		{
			glTranslatef(0, 3.0f / 16.0f, 1.0f / 16.0f);
			glScalef(0.55f, 0.55f, 0.55f);
		}
		mcdraw::Item3D(held);
	}
	else if (gun)
	{
		// Counter-Strike weapons shown as their Minecraft counterparts
		const char* tex = "item/crossbow_standby";
		bool tool = false;
		if (knife) { tex = "item/iron_sword"; tool = true; }
		else if (!strcmp(gun, "awp") || !strcmp(gun, "scout") || !strcmp(gun, "sg550") || !strcmp(gun, "g3sg1")) tex = "item/bow";
		else if (!strcmp(gun, "hegrenade")) tex = "item/fire_charge";
		else if (!strcmp(gun, "flashbang")) tex = "item/snowball";
		else if (!strcmp(gun, "smokegrenade")) tex = "item/gunpowder";
		glScalef(16.0f, 16.0f, 16.0f);
		if (!strcmp(gun, "c4"))
		{
			glTranslatef(0, 3.0f / 16.0f, 1.0f / 16.0f);
			glScalef(0.4f, 0.4f, 0.4f);
			mcdraw::Cube3D(mcw::FindBlock("tnt"));
		}
		else
		{
			if (tool)
			{
				glTranslatef(0, 4.0f / 16.0f, 0.5f / 16.0f);
				glRotatef(-90.0f, 0, 1, 0);
				glRotatef(55.0f, 0, 0, 1);
				glScalef(0.85f, 0.85f, 0.85f);
			}
			else
			{
				// crossbow third-person hold: pointing forward
				glTranslatef(0, 3.0f / 16.0f, 1.0f / 16.0f);
				glRotatef(-90.0f, 0, 1, 0);
				glRotatef(-25.0f, 0, 0, 1);
				glScalef(0.75f, 0.75f, 0.75f);
			}
			mcdraw::Texture3D(tex);
		}
	}
	glPopMatrix();

	// elytra wings on the back (ElytraModel), drawn in model space
	if (elytra)
	{
		mctex::Bind("entity/equipment/wings/elytra");
		glColor3f(1, 1, 1);
		float spread = gliding ? 1.0f : 0.0f;
		for (int side = -1; side <= 1; side += 2)
		{
			// ElytraModel: left wing (model +x) pivots at x=5 with its box reaching back toward the spine
			// (-10..0), the right wing is its mirror; both inflated by 1. Fall-flying spreads them to
			// xRot 20 deg / zRot -90 deg (WingsLayer pushes the pair 2 px off the back).
			float xr = 0.2617994f + spread * (0.34906584f - 0.2617994f);
			float zr = -0.2617994f + spread * (-PI / 2.0f + 0.2617994f);
			Part w = {side * 5.0f, 0, 2.0f, xr, 0, side * zr};
			PushPart(w);
			Cube(side > 0 ? -10.0f : 0.0f, 0, 0, 10, 20, 2, 22, 0, 1.0f, 64, 32, side < 0);
			glPopMatrix();
		}
	}
	glPopMatrix();
}

void PlayersDrawAll()
{
	if (g_players.empty())
		return;
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	for (cl_entity_t* e : g_players)
		DrawPlayer(e);
	g_players.clear();
}

// The inventory screen's player preview (InventoryScreen.renderEntityInInventoryFollowsMouse): the local
// player's Minecraft model with its armor, turned towards the mouse.
void PlayerPreview(float x0, float y0, float x1, float y1, float lookX, float lookY)
{
	cl_entity_t* me = gEngfuncs.GetLocalPlayer();
	if (!me)
		return;
	const char* skin = PickSkin(me->index);
	const mctex::Tex& st = mctex::Get(skin);
	if (!st.id)
		return;
	bool legacy = st.h == 32 || strstr(skin, "skeleton") != nullptr;
	static const char* mats[] = {"", "leather", "chainmail", "iron", "golden", "diamond", "netherite", "turtle"};
	int armor = 0;
	for (int i = 0; i < mci::NUM_ARMOR_SLOTS; i++)
	{
		int id = g_cl.armorId[i];
		if (id <= 0 || mci::Item(id).type != mci::IT_ARMOR)
			continue;
		const char* n = mci::Item(id).name;
		for (int m = 1; m < 8; m++)
			if (!strncmp(n, mats[m], strlen(mats[m])))
			{
				armor |= m << (i * 3);
				break;
			}
	}
	float yawT = atanf(lookX / 40.0f), pitchT = atanf(lookY / 40.0f);
	Part head = {0, 0, 0, -pitchT * 20.0f * PI / 180.0f, yawT * 20.0f * PI / 180.0f, 0};
	Part body = {0, 0, 0, 0, 0, 0};
	Part rarm = {-5, 2, 0, 0, 0, 0.05f};
	Part larm = {5, 2, 0, 0, 0, -0.05f};
	Part rleg = {-1.9f, 12, 0, 0, 0, 0};
	Part lleg = {1.9f, 12, 0, 0, 0, 0};
	float tint[3] = {1, 1, 1};
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glOrtho(0, g_cl.screenW, g_cl.screenH, 0, -2000, 2000);
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();
	float scale = (y1 - y0) / 36.0f;
	glTranslatef((x0 + x1) * 0.5f, y0 + (y1 - y0) * 0.5f - 8.0f * scale + 2.0f * scale, 0.0f);
	glScalef(scale, scale, -scale);
	glRotatef(yawT * 20.0f, 0, 1, 0); // faces the viewer (the z flip above already turns the model round)
	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	float savedLight = g_light;
	g_light = 1.0f;
	DrawBody(skin, legacy, head, body, rarm, larm, rleg, lleg, armor, tint);
	g_light = savedLight;
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_ALPHA_TEST);
	glPopMatrix();
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
}
} // namespace mc
