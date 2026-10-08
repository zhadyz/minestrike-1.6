// Renders the server's Minecraft entities (XP orbs, dropped items, falling blocks, TNT, projectiles,
// fireworks) and per-player extras (hurt flash, elytra wings). Ports of the Java renderers.
#include "mc_blocks.h"
#include "mc_client.h"
#include "mc_draw.h"
#include "mc_state.h"
#include "mc_tex.h"

#include <math.h>
#include <vector>

namespace mc
{
static const float PI = 3.14159265358979f;
static std::vector<cl_entity_t*> g_frameEnts;
static double g_hurtTime[4096];
struct SavedFx
{
	bool active = false;
	int renderfx, renderamt;
	color24 color;
};
static SavedFx g_savedFx[4096];
static double g_spawnTime[4096];
static int g_seenMarker[4096];

bool EntIsMc(cl_entity_t* ent)
{
	if (!ent)
		return false;
	int iu = ent->curstate.iuser4;
	if (ent->player || !mcp::IsMcEnt(iu))
	{
		// red hurt flash on players (LivingEntity hurtTime -> red overlay), as a glow shell. The entity
		// state is only re-sent when it changes, so restore the original values when the flash ends.
		if (ent->index > 0 && ent->index < 4096)
		{
			SavedFx& sf = g_savedFx[ent->index];
			bool hurt = g_hurtTime[ent->index] > 0.0 && g_cl.time - g_hurtTime[ent->index] < 0.4;
			if (hurt && !sf.active)
			{
				sf.active = true;
				sf.renderfx = ent->curstate.renderfx;
				sf.renderamt = ent->curstate.renderamt;
				sf.color = ent->curstate.rendercolor;
			}
			if (hurt)
			{
				ent->curstate.renderfx = kRenderFxGlowShell;
				ent->curstate.rendercolor.r = 255;
				ent->curstate.rendercolor.g = 30;
				ent->curstate.rendercolor.b = 30;
				ent->curstate.renderamt = 8;
			}
			else if (sf.active)
			{
				sf.active = false;
				ent->curstate.renderfx = sf.renderfx;
				ent->curstate.renderamt = sf.renderamt;
				ent->curstate.rendercolor = sf.color;
			}
		}
		return false;
	}
	if (ent->index > 0 && ent->index < 4096 && g_seenMarker[ent->index] != iu)
	{
		if (!g_seenMarker[ent->index] || mcp::EntKindOf(g_seenMarker[ent->index]) != mcp::EntKindOf(iu))
			g_spawnTime[ent->index] = g_cl.time - (ent->index % 97) * 0.37; // per-entity phase
		g_seenMarker[ent->index] = iu;
	}
	g_frameEnts.push_back(ent);
	return true;
}

void EntHurt(int entindex)
{
	if (entindex > 0 && entindex < 4096)
		g_hurtTime[entindex] = g_cl.time;
}

void EntSwing(int) {}

void EntDeath(int entindex, const float* origin)
{
	float o[3] = {origin[0], origin[1], origin[2] + 16.0f};
	ParticlesSpawn(mcp::PK_POOF, o, 20, 0);
}

static float WorldLightAt(const float* p)
{
	const mcw::World* w = WorldGet();
	(void)w;
	return 1.0f;
}

static void Billboard(const char* tex, const float* o, float size, float u0, float v0, float u1, float v1, unsigned rgba)
{
	const mctex::Tex& t = mctex::Get(tex);
	if (!t.id)
		return;
	glBindTexture(GL_TEXTURE_2D, t.id);
	glColor4ub((rgba >> 24) & 0xFF, (rgba >> 16) & 0xFF, (rgba >> 8) & 0xFF, rgba & 0xFF);
	float h = size * 0.5f;
	float r[3], u[3];
	for (int i = 0; i < 3; i++)
	{
		r[i] = g_cl.right[i] * h;
		u[i] = g_cl.up[i] * h;
	}
	glBegin(GL_QUADS);
	glTexCoord2f(u0, v1);
	glVertex3f(o[0] - r[0] - u[0], o[1] - r[1] - u[1], o[2] - r[2] - u[2]);
	glTexCoord2f(u1, v1);
	glVertex3f(o[0] + r[0] - u[0], o[1] + r[1] - u[1], o[2] + r[2] - u[2]);
	glTexCoord2f(u1, v0);
	glVertex3f(o[0] + r[0] + u[0], o[1] + r[1] + u[1], o[2] + r[2] + u[2]);
	glTexCoord2f(u0, v0);
	glVertex3f(o[0] - r[0] + u[0], o[1] - r[1] + u[1], o[2] - r[2] + u[2]);
	glEnd();
}

static void DrawXpOrb(cl_entity_t* e, double age)
{
	int icon = mcp::EntDataOf(e->curstate.iuser4) & 0xF;
	float u0 = (icon % 4) * 16 / 64.0f, v0 = (icon / 4) * 16 / 64.0f;
	// ExperienceOrbRenderer colour pulse
	float t = (float)(age * 20.0) / 2.0f;
	int r = (int)((sinf(t + 0.0f) + 1.0f) * 0.5f * 255.0f);
	int b = (int)((sinf(t + 4.1887903f) + 1.0f) * 0.1f * 255.0f);
	unsigned col = ((unsigned)r << 24) | (255u << 16) | ((unsigned)b << 8) | 0xC0;
	float o[3] = {e->origin[0], e->origin[1], e->origin[2] + 0.1f * 40.0f};
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	Billboard("entity/experience/experience_orb", o, 0.3f * 40.0f, u0, v0, u0 + 0.25f, v0 + 0.25f, col);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

static bool IsBlockItem(int id, int* block)
{
	const mci::ItemDef& d = mci::Item(id);
	if (d.type != mci::IT_BLOCK || d.texture)
		return false;
	int b = mcw::FindBlock(d.blockName);
	if (b <= 0)
		return false;
	mcw::ShapeKind s = mcw::Block((uint16_t)b).shape;
	if (s == mcw::SHAPE_DOOR || s == mcw::SHAPE_CROSS || s == mcw::SHAPE_PANE)
		return false;
	*block = b;
	return true;
}

static void DrawDroppedItem(cl_entity_t* e, double age)
{
	int id = mcp::EntDataOf(e->curstate.iuser4);
	if (!mci::ValidItem(id))
		return;
	float bobOffs = (e->index % 13) * 0.48f;
	float bob = sinf((float)(age * 20.0) / 10.0f + bobOffs) * 0.1f + 0.1f;
	float spin = ((float)(age * 20.0) / 20.0f + bobOffs) * (180.0f / PI);
	int count = e->curstate.skin;
	int copies = count > 48 ? 5 : count > 32 ? 4 : count > 16 ? 3 : count > 1 ? 2 : 1;
	int block = 0;
	bool isBlock = IsBlockItem(id, &block);
	glPushMatrix();
	glTranslatef(e->origin[0], e->origin[1], e->origin[2] + (bob + 0.25f) * 40.0f);
	glRotatef(spin, 0, 0, 1);
	glRotatef(90.0f, 1, 0, 0); // item model y-up -> world z-up
	float s = 40.0f * (isBlock ? 0.25f : 0.5f);
	glScalef(s, s, s);
	for (int k = 0; k < copies; k++)
	{
		glPushMatrix();
		if (k > 0)
		{
			// ItemEntityRenderer jitter for stacks
			float jx = ((k * 7919) % 100 / 100.0f - 0.5f) * 0.3f, jy = ((k * 104729) % 100 / 100.0f - 0.5f) * 0.3f;
			if (isBlock)
				glTranslatef(jx, jy * 0.5f, ((k * 31) % 100 / 100.0f - 0.5f) * 0.3f);
			else
				glTranslatef(jx * 0.5f, jy * 0.5f, -0.0625f * k);
		}
		mcdraw::Item3D(id, WorldLightAt(e->origin));
		glPopMatrix();
	}
	glPopMatrix();
}

static void DrawCubeAt(int blockType, const float* o, float scale, float white)
{
	glPushMatrix();
	glTranslatef(o[0], o[1], o[2] + 20.0f * scale);
	glRotatef(90.0f, 1, 0, 0);
	glScalef(40.0f * scale, 40.0f * scale, 40.0f * scale);
	mcdraw::Cube3D(blockType, 1.0f);
	if (white > 0.0f)
	{
		// TNT flash: additive white overlay
		glDisable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);
		glColor4f(1, 1, 1, white);
		glScalef(1.01f, 1.01f, 1.01f);
		static const float f[6][4][3] = {
			{{-.5f, .5f, .5f}, {.5f, .5f, .5f}, {.5f, .5f, -.5f}, {-.5f, .5f, -.5f}}, {{-.5f, -.5f, -.5f}, {.5f, -.5f, -.5f}, {.5f, -.5f, .5f}, {-.5f, -.5f, .5f}},
			{{-.5f, -.5f, .5f}, {.5f, -.5f, .5f}, {.5f, .5f, .5f}, {-.5f, .5f, .5f}}, {{.5f, -.5f, -.5f}, {-.5f, -.5f, -.5f}, {-.5f, .5f, -.5f}, {.5f, .5f, -.5f}},
			{{.5f, -.5f, .5f}, {.5f, -.5f, -.5f}, {.5f, .5f, -.5f}, {.5f, .5f, .5f}}, {{-.5f, -.5f, -.5f}, {-.5f, -.5f, .5f}, {-.5f, .5f, .5f}, {-.5f, .5f, -.5f}}};
		glBegin(GL_QUADS);
		for (auto& q : f)
			for (auto& v : q)
				glVertex3fv(v);
		glEnd();
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glDisable(GL_BLEND);
		glEnable(GL_TEXTURE_2D);
	}
	glPopMatrix();
}

static void DrawThrown(cl_entity_t* e, const char* itemTex)
{
	float o[3] = {e->origin[0], e->origin[1], e->origin[2]};
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	Billboard(itemTex, o, 0.5f * 40.0f * 0.5f, 0, 0, 1, 1, 0xFFFFFFFF);
}

static void DrawArrow(cl_entity_t* e)
{
	// ArrowRenderer: two crossed quads 16px long with the shaft from arrow.png (rows 0-4)
	glPushMatrix();
	glTranslatef(e->origin[0], e->origin[1], e->origin[2]);
	glRotatef(e->curstate.angles[1], 0, 0, 1);
	glRotatef(-e->curstate.angles[0], 0, 1, 0);
	mctex::Bind("entity/projectiles/arrow");
	glColor4f(1, 1, 1, 1);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glDisable(GL_CULL_FACE);
	float L = 0.5f * 40.0f, H = 0.0625f * 2.5f * 40.0f;
	glBegin(GL_QUADS);
	for (int k = 0; k < 2; k++)
	{
		float a = k * 90.0f * PI / 180.0f;
		float cy = cosf(a) * H, cz = sinf(a) * H;
		glTexCoord2f(0, 0);
		glVertex3f(-L, -cy, -cz);
		glTexCoord2f(0.5f, 0);
		glVertex3f(L, -cy, -cz);
		glTexCoord2f(0.5f, 5.0f / 32.0f);
		glVertex3f(L, cy, cz);
		glTexCoord2f(0, 5.0f / 32.0f);
		glVertex3f(-L, cy, cz);
	}
	glEnd();
	glPopMatrix();
}

// ElytraModel: two 10x20x2 wings behind the chest, folded or spread while gliding.
static void DrawWings(cl_entity_t* e)
{
	bool gliding = (e->curstate.iuser4 & mcp::MCPF_GLIDING) != 0;
	glPushMatrix();
	glTranslatef(e->origin[0], e->origin[1], e->origin[2]);
	glRotatef(e->angles[1], 0, 0, 1);
	if (gliding)
		glRotatef(75.0f, 0, 1, 0);
	// chest back: ~16 units above origin, 5 units behind
	glTranslatef(-5.0f, 0.0f, 22.0f);
	mctex::Bind("entity/equipment/wings/elytra");
	glColor4f(1, 1, 1, 1);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glDisable(GL_CULL_FACE);
	float spread = gliding ? 1.0f : 0.0f;
	for (int side = -1; side <= 1; side += 2)
	{
		glPushMatrix();
		// hinge near the spine, wing hangs down and slightly out; spread rotates it outwards
		glTranslatef(0.0f, side * 2.0f, 0.0f);
		glRotatef(side * (15.0f + spread * 65.0f), 1, 0, 0);
		glRotatef(-15.0f - spread * 20.0f, 0, 1, 0);
		const float W = 10 * 2.5f, H = 20 * 2.5f; // pixels -> units (player 72u ~ 32px)
		float u0 = 22.0f / 64.0f, u1 = 32.0f / 64.0f, v0 = 2.0f / 32.0f, v1 = 22.0f / 32.0f;
		if (side > 0)
		{
			float t = u0;
			u0 = u1;
			u1 = t;
		}
		glBegin(GL_QUADS);
		glTexCoord2f(u0, v0);
		glVertex3f(-1.0f, 0.0f, 4.0f);
		glTexCoord2f(u1, v0);
		glVertex3f(-1.0f, side * W, 4.0f);
		glTexCoord2f(u1, v1);
		glVertex3f(-1.0f, side * W, 4.0f - H);
		glTexCoord2f(u0, v1);
		glVertex3f(-1.0f, 0.0f, 4.0f - H);
		glEnd();
		glPopMatrix();
	}
	glPopMatrix();
}

void EntDrawAll()
{
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	for (cl_entity_t* e : g_frameEnts)
	{
		int iu = e->curstate.iuser4;
		double age = g_cl.time - g_spawnTime[e->index & 4095];
		switch (mcp::EntKindOf(iu))
		{
		case mcp::MCE_XPORB: DrawXpOrb(e, age); break;
		case mcp::MCE_ITEM: DrawDroppedItem(e, age); break;
		case mcp::MCE_FALLING: DrawCubeAt(mcw::CellType((mcw::Cell)mcp::EntDataOf(iu)), e->origin, 1.0f, 0.0f); break;
		case mcp::MCE_TNT:
		{
			int fuse = mcp::EntDataOf(iu);
			float sc = 1.0f;
			if (fuse < 10)
				sc = 1.0f + (1.0f - fuse / 10.0f) * 0.3f;
			float white = ((fuse / 5) % 2 == 0) ? (1.0f - fuse / 100.0f) * 0.8f : 0.0f;
			DrawCubeAt(mcw::FindBlock("tnt"), e->origin, sc, white);
			break;
		}
		case mcp::MCE_ARROW: DrawArrow(e); break;
		case mcp::MCE_PEARL: DrawThrown(e, "item/ender_pearl"); break;
		case mcp::MCE_XPBOTTLE: DrawThrown(e, "item/experience_bottle"); break;
		case mcp::MCE_FIREWORK:
		{
			DrawThrown(e, "item/firework_rocket");
			static double nextTrail[4096];
			if (g_cl.time >= nextTrail[e->index & 4095])
			{
				nextTrail[e->index & 4095] = g_cl.time + 0.033; // ~30 sparks/s like Minecraft's 1 per tick-ish
				float o[3] = {e->origin[0], e->origin[1], e->origin[2] - 6.0f};
				ParticlesSpawn(mcp::PK_FIREWORK_SPARK, o, 1, 0xFFFF);
			}
			break;
		}
		}
	}
	g_frameEnts.clear();

	// elytra on other players (and us in third person)
	cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	for (int i = 1; i <= gEngfuncs.GetMaxClients(); i++)
	{
		cl_entity_t* p = gEngfuncs.GetEntityByIndex(i);
		if (!p || !p->player || !p->model || p->curstate.messagenum < (local ? local->curstate.messagenum : 0))
			continue;
		if (p == local && !gEngfuncs.pDemoAPI->IsPlayingback() && !CL_IsThirdPersonLocal())
			continue;
		extern bool PlayersEnabled();
		if ((p->curstate.iuser4 & mcp::MCPF_ELYTRA) && !PlayersEnabled())
			DrawWings(p);
	}
	// our own firework boost sparks
	static double nextBoostSpark = 0.0;
	if (local && (g_cl.flags & mcp::MCPF_GLIDING) && g_cl.boost > 0.0f && g_cl.time >= nextBoostSpark)
	{
		nextBoostSpark = g_cl.time + 0.033;
		float o[3] = {local->origin[0] - g_cl.forward[0] * 30.0f, local->origin[1] - g_cl.forward[1] * 30.0f, local->origin[2]};
		ParticlesSpawn(mcp::PK_FIREWORK_SPARK, o, 1, 0xFFFF);
	}
}
} // namespace mc
