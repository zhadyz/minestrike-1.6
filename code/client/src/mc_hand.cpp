// First-person Minecraft hand + held item, ported from ItemInHandRenderer (Java 1.21): same pose
// stack transforms, swing curves and equip animation, drawn with its own 70-degree projection over
// the world like Minecraft's hand pass.
#include "mc_blocks.h"
#include "mc_client.h"
#include "mc_draw.h"
#include "mc_state.h"
#include "mc_tex.h"

#include <math.h>

namespace mc
{
static const float PI = 3.14159265358979f;
static const double SWING_TIME = 0.30; // 6 ticks

void HandOnSwing()
{
	double t = g_cl.time - g_cl.swingStart;
	// Minecraft restarts the swing only if the previous one is past half-way
	if (t < 0 || t >= SWING_TIME * 0.5)
		g_cl.swingStart = g_cl.time;
}

static float SwingProgress()
{
	double t = g_cl.time - g_cl.swingStart;
	if (t < 0 || t >= SWING_TIME)
		return 0.0f;
	return (float)(t / SWING_TIME);
}

static float EquipProgress()
{
	double t = g_cl.time - g_cl.equipTime;
	if (t < 0 || t > 0.35)
		return 0.0f;
	if (t < 0.1)
		return (float)(t / 0.1);
	return (float)(1.0 - (t - 0.1) / 0.25);
}

// Steve's right arm box (-3,-2,-2) size (4,12,4) in pixel units with the skin's UV layout.
static void ArmBox(float x0, float y0, float z0, float sx, float sy, float sz, float u, float v, float inflate)
{
	float x1 = x0 + sx, y1 = y0 + sy, z1 = z0 + sz;
	x0 -= inflate; y0 -= inflate; z0 -= inflate;
	x1 += inflate; y1 += inflate; z1 += inflate;
	const float W = 64.0f, Hh = 64.0f;
	auto quad = [&](float u0, float v0, float u1, float v1, const float a[3], const float b[3], const float c[3], const float d[3]) {
		glTexCoord2f(u0 / W, v0 / Hh);
		glVertex3fv(a);
		glTexCoord2f(u1 / W, v0 / Hh);
		glVertex3fv(b);
		glTexCoord2f(u1 / W, v1 / Hh);
		glVertex3fv(c);
		glTexCoord2f(u0 / W, v1 / Hh);
		glVertex3fv(d);
	};
	// model space: y down. Faces per ModelPart.Cube UV layout.
	float p000[3] = {x0, y0, z0}, p100[3] = {x1, y0, z0}, p110[3] = {x1, y1, z0}, p010[3] = {x0, y1, z0};
	float p001[3] = {x0, y0, z1}, p101[3] = {x1, y0, z1}, p111[3] = {x1, y1, z1}, p011[3] = {x0, y1, z1};
	glBegin(GL_QUADS);
	glColor3f(0.8f, 0.8f, 0.8f);
	quad(u + sz, v + sz, u + sz + sx, v + sz + sy, p000, p100, p110, p010);                   // north (-z) front
	quad(u + sz + sx + sz, v + sz, u + sz + sx + sz + sx, v + sz + sy, p101, p001, p011, p111); // south (+z) back
	glColor3f(0.6f, 0.6f, 0.6f);
	quad(u, v + sz, u + sz, v + sz + sy, p001, p000, p010, p011);                             // west (-x)
	quad(u + sz + sx, v + sz, u + sz + sx + sz, v + sz + sy, p100, p101, p111, p110);         // east (+x)
	glColor3f(1.0f, 1.0f, 1.0f);
	quad(u + sz, v, u + sz + sx, v + sz, p001, p101, p100, p000);                             // up (-y in model = top)
	glColor3f(0.5f, 0.5f, 0.5f);
	quad(u + sz + sx, v, u + sz + sx + sx, v + sz, p010, p110, p111, p011);                   // down
	glEnd();
}

static void DrawArmModel()
{
	mctex::Bind("entity/player/wide/steve");
	glDisable(GL_CULL_FACE);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.1f);
	glPushMatrix();
	glScalef(1.0f / 16.0f, 1.0f / 16.0f, 1.0f / 16.0f);
	glTranslatef(-5.0f, 2.0f, 0.0f); // PartPose offset of right_arm
	ArmBox(-3, -2, -2, 4, 12, 4, 40, 16, 0.0f);   // arm
	ArmBox(-3, -2, -2, 4, 12, 4, 40, 32, 0.25f);  // sleeve overlay
	glPopMatrix();
}

static void RenderEmptyArm(float equip, float swing)
{
	// ItemInHandRenderer.renderPlayerArm, right side (f = 1)
	const float side = 1.0f;
	float f1 = sqrtf(swing);
	float f2 = -0.3f * sinf(f1 * PI);
	float f3 = 0.4f * sinf(f1 * (PI * 2.0f));
	float f4 = -0.4f * sinf(swing * PI);
	glTranslatef(side * (f2 + 0.64000005f), f3 + -0.6f + equip * -0.6f, f4 + -0.71999997f);
	glRotatef(side * 45.0f, 0, 1, 0);
	float f5 = sinf(swing * swing * PI);
	float f6 = sinf(f1 * PI);
	glRotatef(side * f6 * 70.0f, 0, 1, 0);
	glRotatef(side * f5 * -20.0f, 0, 0, 1);
	glTranslatef(side * -1.0f, 3.6f, 3.5f);
	glRotatef(side * 120.0f, 0, 0, 1);
	glRotatef(200.0f, 1, 0, 0);
	glRotatef(side * -135.0f, 0, 1, 0);
	glTranslatef(side * 5.6f, 0.0f, 0.0f);
	DrawArmModel();
}

static void RotXYZ(float rx, float ry, float rz)
{
	// Quaternionf.rotationXYZ: X, then Y, then Z applied in that order to the pose
	glRotatef(rx, 1, 0, 0);
	glRotatef(ry, 0, 1, 0);
	glRotatef(rz, 0, 0, 1);
}

static bool HasArrows()
{
	if (g_cl.creative)
		return true;
	int arrow = mci::FindItem("arrow");
	for (int i = 0; i < mcp::HOTBAR_SIZE; i++)
		if (g_cl.hotbarId[i] == arrow)
			return true;
	for (int i = 0; i < 27; i++)
		if (g_cl.invId[i] == arrow)
			return true;
	return false;
}

// ItemInHandRenderer, case BOW while the player is using it: the bow is raised to the middle and pulled.
static void RenderBowDraw(int itemId, float equip)
{
	const float i = 1.0f; // right arm
	glTranslatef(i * 0.56f, -0.52f + equip * -0.6f, -0.72f); // applyItemArmTransform
	glTranslatef(i * -0.2785682f, 0.18344387f, 0.15731531f);
	glRotatef(-13.935f, 1, 0, 0);
	glRotatef(i * 35.3f, 0, 1, 0);
	glRotatef(i * -9.785f, 0, 0, 1);
	float used = (float)((g_cl.time - g_cl.useStart) * 20.0); // ticks
	float pull = used / 20.0f;
	pull = (pull * pull + pull * 2.0f) / 3.0f;
	if (pull > 1.0f)
		pull = 1.0f;
	if (pull > 0.1f)
	{
		float shake = sinf((used - 0.1f) * 1.3f) * (pull - 0.1f);
		glTranslatef(0.0f, shake * 0.004f, 0.0f);
	}
	glTranslatef(0.0f, 0.0f, pull * 0.04f);
	glScalef(1.0f, 1.0f, 1.0f + pull * 0.2f);
	glRotatef(-i * 45.0f, 0, 1, 0);
	// item/generated first-person transform, then the pulling texture (bow.json predicates)
	glTranslatef(1.13f / 16.0f, 3.2f / 16.0f, 1.13f / 16.0f);
	RotXYZ(0, -90, 25);
	glScalef(0.68f, 0.68f, 0.68f);
	const char* tex = pull >= 0.9f ? "item/bow_pulling_2" : pull >= 0.65f ? "item/bow_pulling_1" : "item/bow_pulling_0";
	glDisable(GL_CULL_FACE);
	mcdraw::Item3D(itemId, 1.0f, tex);
}

// ItemInHandRenderer.applyEatTransform: the food comes up to the mouth and bobs (32-tick use).
static void ApplyEat()
{
	const float i = 1.0f, duration = 32.0f;
	float remaining = duration - (float)((g_cl.time - g_cl.useStart) * 20.0);
	if (remaining < 0.0f)
		remaining = 0.0f;
	float f1 = remaining / duration;
	if (f1 < 0.8f)
		glTranslatef(0.0f, fabsf(cosf(remaining / 4.0f * PI) * 0.1f), 0.0f);
	float f3 = 1.0f - powf(f1, 27.0f);
	glTranslatef(f3 * 0.6f * i, f3 * -0.5f, 0.0f);
	glRotatef(i * f3 * 90.0f, 0, 1, 0);
	glRotatef(f3 * 10.0f, 1, 0, 0);
	glRotatef(i * f3 * 30.0f, 0, 0, 1);
}

static void RenderItemInHand(int itemId, float equip, float swing)
{
	const float side = 1.0f;
	const mci::ItemDef& held = mci::Item(itemId);
	if (g_cl.useHeld && held.type == mci::IT_BOW && HasArrows())
	{
		RenderBowDraw(itemId, equip);
		return;
	}
	if (held.type == mci::IT_CROSSBOW)
	{
		// crossbow.json predicates: charged -> crossbow_arrow; charging -> pulling_0/1/2 by charge (25 ticks)
		cl_entity_t* me = gEngfuncs.GetLocalPlayer();
		bool loaded = me && (me->curstate.iuser4 & mcp::MCPF_XBOW_LOADED);
		const char* tex = "item/crossbow_standby";
		if (loaded)
			tex = "item/crossbow_arrow";
		else if (g_cl.useHeld)
		{
			float pull = (float)((g_cl.time - g_cl.useStart) * 20.0) / 25.0f;
			tex = pull >= 1.0f ? "item/crossbow_pulling_2" : pull >= 0.58f ? "item/crossbow_pulling_1" : "item/crossbow_pulling_0";
		}
		glTranslatef(side * 0.56f, -0.52f + equip * -0.6f, -0.72f);
		glTranslatef(1.13f / 16.0f, 3.2f / 16.0f, 1.13f / 16.0f);
		RotXYZ(0, -90, 25);
		glScalef(0.68f, 0.68f, 0.68f);
		glDisable(GL_CULL_FACE);
		mcdraw::Item3D(itemId, 1.0f, tex);
		return;
	}
	if (g_cl.useHeld && held.type == mci::IT_FOOD)
	{
		ApplyEat();
		glTranslatef(side * 0.56f, -0.52f + equip * -0.6f, -0.72f);
		glTranslatef(1.13f / 16.0f, 3.2f / 16.0f, 1.13f / 16.0f);
		RotXYZ(0, -90, 25);
		glScalef(0.68f, 0.68f, 0.68f);
		glDisable(GL_CULL_FACE);
		mcdraw::Item3D(itemId);
		return;
	}
	float sq = sqrtf(swing);
	float f5 = -0.4f * sinf(sq * PI);
	float f6 = 0.2f * sinf(sq * (PI * 2.0f));
	float f10 = -0.2f * sinf(swing * PI);
	glTranslatef(side * f5, f6, f10);
	// applyItemArmTransform
	glTranslatef(side * 0.56f, -0.52f + equip * -0.6f, -0.72f);
	// applyItemArmAttackTransform
	float f = sinf(swing * swing * PI);
	glRotatef(side * (45.0f + f * -20.0f), 0, 1, 0);
	float f1 = sinf(sq * PI);
	glRotatef(side * f1 * -20.0f, 0, 0, 1);
	glRotatef(f1 * -80.0f, 1, 0, 0);
	glRotatef(side * -45.0f, 0, 1, 0);

	const mci::ItemDef& d = mci::Item(itemId);
	bool cube = false;
	if (d.type == mci::IT_BLOCK)
	{
		int b = mcw::FindBlock(d.blockName);
		mcw::ShapeKind sh = b > 0 ? mcw::Block((uint16_t)b).shape : mcw::SHAPE_NONE;
		cube = sh == mcw::SHAPE_CUBE || sh == mcw::SHAPE_SLAB || sh == mcw::SHAPE_STAIRS;
	}
	if (cube)
	{
		// block/block display: firstperson_righthand rotation [0,45,0], scale 0.4
		RotXYZ(0, 45, 0);
		glScalef(0.4f, 0.4f, 0.4f);
	}
	else
	{
		// item/generated & item/handheld: rotation [0,-90,25], translation [1.13,3.2,1.13]/16, scale 0.68
		glTranslatef(1.13f / 16.0f, 3.2f / 16.0f, 1.13f / 16.0f);
		RotXYZ(0, -90, 25);
		glScalef(0.68f, 0.68f, 0.68f);
	}
	glDisable(GL_CULL_FACE);
	mcdraw::Item3D(itemId);
}

void HandDraw()
{
	if (!g_cl.mcItemActive || g_cl.health <= 0)
		return;
	cl_entity_t* local = gEngfuncs.GetLocalPlayer();
	if (!local || local->curstate.iuser1 || CL_IsThirdPersonLocal())
		return;
	int held = HeldItemId();
	if (held != g_cl.lastHeldId)
	{
		g_cl.lastHeldId = held;
		g_cl.equipTime = g_cl.time;
	}

	float aspect = g_cl.screenW / (float)(g_cl.screenH > 0 ? g_cl.screenH : 1);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	{
		// Minecraft renders the hand with fov 70 regardless of the world fov
		float fovy = 70.0f * PI / 180.0f;
		float n = 0.05f, f = 100.0f;
		float top = n * tanf(fovy * 0.5f), right = top * aspect;
		glFrustum(-right, right, -top, top, n, f);
	}
	glMatrixMode(GL_MODELVIEW);
	glPushMatrix();
	glLoadIdentity();

	glClear(GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glDisable(GL_FOG);
	glDisable(GL_LIGHTING);
	glShadeModel(GL_SMOOTH);

	// gentle view bob from walking speed (Minecraft's bobView applied to the hand)
	float speed = sqrtf(local->curstate.velocity[0] * local->curstate.velocity[0] + local->curstate.velocity[1] * local->curstate.velocity[1]);
	float bobAmt = fminf(speed / 250.0f, 1.0f) * (g_cl.onGround ? 1.0f : 0.0f);
	static float walkDist = 0.0f;
	walkDist += speed * g_cl.frametime / 40.0f;
	float wd = walkDist * 0.6f;
	glTranslatef(sinf(wd * PI) * bobAmt * 0.05f, -fabsf(cosf(wd * PI) * bobAmt) * 0.08f, 0.0f);
	glRotatef(sinf(wd * PI) * bobAmt * 3.0f, 0, 0, 1);

	float swing = SwingProgress();
	float equip = EquipProgress();
	if (held)
		RenderItemInHand(held, equip, swing);
	else
		RenderEmptyArm(equip, swing);

	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glClear(GL_DEPTH_BUFFER_BIT);
}
} // namespace mc
