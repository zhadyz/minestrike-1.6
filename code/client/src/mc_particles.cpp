// Minecraft particles: block fragments (TerrainParticle), crits, sweep, poof, explosions, firework
// sparks, totem glitter, hearts. Billboards with the real particle sprites, Minecraft-like motion.
#include "mc_blocks.h"
#include "mc_client.h"
#include "mc_state.h"
#include "mc_move.h"
#include "mc_tex.h"

#include <math.h>
#include <stdio.h>
#include <string>
#include <vector>

namespace mc
{
static const float B = 40.0f; // units per block

struct Particle
{
	float pos[3], vel[3]; // units, units/sec
	float age, life;      // seconds
	float size;           // units
	float gravity;        // units/sec^2
	float drag;           // per-tick velocity multiplier
	float r, g, b, a;
	const char* tex;      // base texture (static string); frames appended as _N when frames > 1
	                      // texBuf (if set) wins: particles move inside the vector, so never point at it
	int frames;
	float u0, v0, u1, v1; // sub-rect (for block fragments)
	bool additive;
	bool collide;
	bool frameByAge;      // animate frames over lifetime (vs random frame)
	int frame;
	char texBuf[48];
};
static std::vector<Particle> g_parts;

static Particle& NewP()
{
	if (g_parts.size() > 4000)
		g_parts.erase(g_parts.begin(), g_parts.begin() + 500);
	g_parts.emplace_back();
	Particle& p = g_parts.back();
	memset(&p, 0, sizeof(p));
	p.u1 = p.v1 = 1.0f;
	p.r = p.g = p.b = p.a = 1.0f;
	p.drag = 0.98f;
	p.frames = 1;
	return p;
}

static void BlockFragment(const float* o, mcw::Cell cell, float spread, bool burst)
{
	uint16_t type = mcw::CellType(cell);
	const mcw::BlockDef& d = mcw::Block(type);
	if (!d.texSide)
		return;
	Particle& p = NewP();
	snprintf(p.texBuf, sizeof(p.texBuf), "block/%s", d.texSide);
	for (int i = 0; i < 3; i++)
		p.pos[i] = o[i] + RandF(-spread, spread);
	if (burst)
	{
		// TerrainParticle from BlockBreak: velocity outward from the block centre
		for (int i = 0; i < 3; i++)
			p.vel[i] = (p.pos[i] - o[i]) / spread * RandF(1.0f, 4.0f) * B * 0.25f;
		p.vel[2] += RandF(1.0f, 3.0f) * B * 0.5f;
	}
	else
	{
		for (int i = 0; i < 3; i++)
			p.vel[i] = RandF(-0.5f, 0.5f) * B;
		p.vel[2] = RandF(0.5f, 1.5f) * B;
	}
	p.gravity = 16.0f * B;
	p.life = 4.0f / (RandF(0.0f, 1.0f) * 0.9f + 0.1f) * 0.05f;
	if (p.life > 1.2f)
		p.life = 1.2f;
	p.size = RandF(0.1f, 0.2f) * B;
	// random 4x4-pixel chunk of the texture
	float cu = floorf(RandF(0.0f, 12.0f)) / 16.0f, cv = floorf(RandF(0.0f, 12.0f)) / 16.0f;
	p.u0 = cu;
	p.v0 = cv;
	p.u1 = cu + 0.25f;
	p.v1 = cv + 0.25f;
	float shade = 0.6f;
	p.r = p.g = p.b = shade;
	p.collide = true;
}

void ParticlesSpawn(int kind, const float* origin, int count, int data)
{
	for (int n = 0; n < count; n++)
	{
		switch (kind)
		{
		case mcp::PK_BLOCK_BREAK:
			BlockFragment(origin, (mcw::Cell)data, B * 0.45f, true);
			break;
		case mcp::PK_BLOCK_HIT:
			BlockFragment(origin, (mcw::Cell)data, B * 0.5f, false);
			break;
		case mcp::PK_CRIT:
		case mcp::PK_ENCHANT_CRIT:
		{
			Particle& p = NewP();
			p.tex = kind == mcp::PK_CRIT ? "particle/critical_hit" : "particle/enchanted_hit";
			for (int i = 0; i < 3; i++)
			{
				p.pos[i] = origin[i] + RandF(-12.0f, 12.0f);
				p.vel[i] = RandF(-1.0f, 1.0f) * B * 4.0f;
			}
			p.vel[2] += 0.2f * B * 20.0f;
			p.drag = 0.7f;
			p.gravity = 0.5f * B * 20.0f;
			p.life = RandF(0.3f, 0.6f);
			p.size = 0.15f * B;
			float c = RandF(0.6f, 1.0f);
			p.r = p.g = p.b = c;
			if (kind == mcp::PK_ENCHANT_CRIT)
				p.r *= 0.3f, p.g *= 0.8f;
			break;
		}
		case mcp::PK_SWEEP:
		{
			Particle& p = NewP();
			p.tex = "particle/sweep";
			p.frames = 8;
			p.frameByAge = true;
			for (int i = 0; i < 3; i++)
				p.pos[i] = origin[i];
			p.life = 0.2f;
			p.size = 1.6f * B;
			p.drag = 1.0f;
			float c = RandF(0.6f, 1.0f);
			p.r = p.g = p.b = c;
			break;
		}
		case mcp::PK_POOF:
		case mcp::PK_SMOKE:
		{
			Particle& p = NewP();
			p.tex = "particle/generic";
			p.frames = 8;
			p.frameByAge = true;
			for (int i = 0; i < 3; i++)
			{
				p.pos[i] = origin[i] + RandF(-16.0f, 16.0f);
				p.vel[i] = RandF(-0.4f, 0.4f) * B;
			}
			p.pos[2] += RandF(0.0f, 40.0f);
			p.vel[2] = RandF(0.2f, 1.0f) * B;
			p.life = RandF(0.4f, 1.0f);
			p.size = RandF(0.25f, 0.5f) * B;
			float c = kind == mcp::PK_POOF ? 1.0f : 0.3f;
			p.r = p.g = p.b = c;
			break;
		}
		case mcp::PK_FIREWORK_SPARK:
		{
			Particle& p = NewP();
			p.tex = "particle/spark";
			p.frames = 8;
			p.frameByAge = true;
			for (int i = 0; i < 3; i++)
			{
				p.pos[i] = origin[i];
				p.vel[i] = RandF(-0.05f, 0.05f) * B * 20.0f;
			}
			p.vel[2] = -0.1f * B * 20.0f;
			p.life = RandF(0.4f, 0.8f);
			p.size = 0.1f * B;
			p.additive = true;
			int c = data;
			p.r = ((c >> 11) & 31) / 31.0f;
			p.g = ((c >> 5) & 63) / 63.0f;
			p.b = (c & 31) / 31.0f;
			break;
		}
		case mcp::PK_HEART:
		{
			Particle& p = NewP();
			p.tex = "particle/heart";
			for (int i = 0; i < 3; i++)
				p.pos[i] = origin[i] + RandF(-16.0f, 16.0f);
			p.vel[2] = 0.4f * B;
			p.drag = 0.86f;
			p.life = 1.0f;
			p.size = 0.2f * B;
			break;
		}
		case mcp::PK_DAMAGE:
		{
			Particle& p = NewP();
			p.tex = "particle/damage";
			for (int i = 0; i < 3; i++)
			{
				p.pos[i] = origin[i] + RandF(-10.0f, 10.0f);
				p.vel[i] = RandF(-0.3f, 0.3f) * B;
			}
			p.vel[2] = RandF(0.5f, 1.0f) * B;
			p.drag = 0.86f;
			p.life = 0.6f;
			p.size = 0.2f * B;
			break;
		}
		case mcp::PK_TOTEM:
		{
			Particle& p = NewP();
			p.tex = "particle/glitter";
			p.frames = 8;
			p.frameByAge = true;
			for (int i = 0; i < 3; i++)
			{
				p.pos[i] = origin[i] + RandF(-8.0f, 8.0f);
				p.vel[i] = RandF(-1.0f, 1.0f) * B * 5.0f;
			}
			p.vel[2] = RandF(0.5f, 3.0f) * B * 5.0f;
			p.drag = 0.6f;
			p.gravity = 1.25f * B * 20.0f * 0.1f;
			p.life = RandF(0.8f, 1.6f);
			p.size = 0.15f * B;
			bool yellow = rand() % 4 == 0;
			p.r = yellow ? 0.9f : 0.1f + RandF(0, 0.2f);
			p.g = yellow ? 0.9f : 0.7f + RandF(0, 0.3f);
			p.b = yellow ? 0.1f : 0.1f;
			p.additive = true;
			break;
		}
		case mcp::PK_PORTAL:
		{
			// PortalParticle: purple specks that drift back in toward where they were spawned around
			Particle& p = NewP();
			snprintf(p.texBuf, sizeof(p.texBuf), "particle/generic_%d", rand() % 8);
			float off[3] = {RandF(-0.5f, 0.5f) * B, RandF(-0.5f, 0.5f) * B, RandF(-1.0f, 1.0f) * B};
			p.life = RandF(1.5f, 2.5f);
			for (int i = 0; i < 3; i++)
			{
				p.pos[i] = origin[i] + off[i] * 2.0f;
				p.vel[i] = -off[i] * 2.0f / p.life;
			}
			p.drag = 1.0f;
			p.size = RandF(0.08f, 0.14f) * B;
			float f = RandF(0.4f, 1.0f);
			p.r = 0.9f * f;
			p.g = 0.3f * f;
			p.b = f;
			break;
		}
		case mcp::PK_XP_SPLASH:
		{
			Particle& p = NewP();
			p.tex = "particle/effect";
			p.frames = 8;
			p.frameByAge = true;
			for (int i = 0; i < 3; i++)
			{
				p.pos[i] = origin[i];
				p.vel[i] = RandF(-1.0f, 1.0f) * B * 4.0f;
			}
			p.vel[2] = RandF(0.2f, 1.5f) * B * 4.0f;
			p.drag = 0.85f;
			p.life = RandF(0.5f, 1.0f);
			p.size = 0.15f * B;
			p.r = 0.4f;
			p.g = 0.6f;
			p.b = 1.0f;
			break;
		}
		}
	}
}

void ParticlesExplosion(const float* o, float power)
{
	// HugeExplosionSeedParticle: a cluster of animated explosion sprites + smoke
	int n = (int)(8 + power * 3);
	for (int k = 0; k < n; k++)
	{
		Particle& p = NewP();
		p.tex = "particle/explosion";
		p.frames = 16;
		p.frameByAge = true;
		float r = power * B * 0.6f;
		for (int i = 0; i < 3; i++)
			p.pos[i] = o[i] + RandF(-r, r);
		p.life = RandF(0.3f, 0.6f);
		p.size = RandF(1.5f, 2.5f) * B * fminf(power / 3.0f, 2.0f);
		float c = RandF(0.6f, 1.0f);
		p.r = p.g = p.b = c;
		p.drag = 1.0f;
	}
	ParticlesSpawn(mcp::PK_SMOKE, o, (int)(10 + power * 4), 0);
}

void ParticlesFirework(const float* o, int shape, int color)
{
	int r = (color >> 16) & 0xFF, g = (color >> 8) & 0xFF, b = color & 0xFF;
	int c565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
	int n = shape == 1 ? 120 : 80;
	for (int k = 0; k < n; k++)
	{
		Particle& p = NewP();
		p.tex = "particle/spark";
		p.frames = 8;
		p.frameByAge = true;
		for (int i = 0; i < 3; i++)
			p.pos[i] = o[i];
		// ball: random direction on a sphere (large ball for shape 1)
		float th = RandF(0.0f, 6.2831853f), ph = acosf(RandF(-1.0f, 1.0f));
		float sp = (shape == 1 ? 0.5f : 0.25f) * B * 20.0f * RandF(0.8f, 1.0f);
		p.vel[0] = sinf(ph) * cosf(th) * sp;
		p.vel[1] = sinf(ph) * sinf(th) * sp;
		p.vel[2] = cosf(ph) * sp;
		p.drag = 0.91f;
		p.gravity = 0.04f * B * 20.0f;
		p.life = RandF(1.0f, 1.6f);
		p.size = 0.15f * B;
		p.additive = true;
		p.r = ((c565 >> 11) & 31) / 31.0f;
		p.g = ((c565 >> 5) & 63) / 63.0f;
		p.b = (c565 & 31) / 31.0f;
	}
}

void ParticlesTotem(int entindex)
{
	cl_entity_t* e = gEngfuncs.GetEntityByIndex(entindex);
	if (!e)
		return;
	float o[3] = {e->origin[0], e->origin[1], e->origin[2] + 20.0f};
	ParticlesSpawn(mcp::PK_TOTEM, o, 80, 0);
}

void ParticlesFrame(float dt)
{
	if (dt <= 0.0f)
		return;
	const mcw::World* w = WorldGet();
	float ticks = dt * 20.0f;
	for (size_t i = 0; i < g_parts.size();)
	{
		Particle& p = g_parts[i];
		p.age += dt;
		if (p.age >= p.life)
		{
			g_parts[i] = g_parts.back();
			g_parts.pop_back();
			continue;
		}
		p.vel[2] -= p.gravity * dt;
		float drag = powf(p.drag, ticks);
		float np[3];
		for (int k = 0; k < 3; k++)
		{
			p.vel[k] *= drag;
			np[k] = p.pos[k] + p.vel[k] * dt;
		}
		if (p.collide && w && mcm::WorldPointSolid(np))
		{
			// stop on the ground like Minecraft particles
			p.vel[0] *= 0.7f;
			p.vel[1] *= 0.7f;
			p.vel[2] = 0.0f;
			np[2] = p.pos[2];
			if (mcm::WorldPointSolid(np))
			{
				np[0] = p.pos[0];
				np[1] = p.pos[1];
			}
		}
		for (int k = 0; k < 3; k++)
			p.pos[k] = np[k];
		i++;
	}
}

void ParticlesDraw()
{
	if (g_parts.empty())
		return;
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glDisable(GL_CULL_FACE);
	glEnable(GL_BLEND);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.05f);
	const char* bound = nullptr;
	bool boundAdd = false;
	char name[64];
	for (Particle& p : g_parts)
	{
		const char* tex = p.texBuf[0] ? p.texBuf : p.tex;
		if (p.frames > 1)
		{
			int f = p.frameByAge ? (int)(p.age / p.life * p.frames) : p.frame;
			if (f >= p.frames)
				f = p.frames - 1;
			// some animated particles count frames down (generic, explosion count up; fine either way)
			snprintf(name, sizeof(name), "%s_%d", tex, f);
			tex = name;
		}
		const mctex::Tex& t = mctex::Get(tex);
		if (!t.id)
			continue;
		glBindTexture(GL_TEXTURE_2D, t.id);
		if (p.additive != boundAdd || !bound)
		{
			glBlendFunc(GL_SRC_ALPHA, p.additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
			boundAdd = p.additive;
		}
		bound = tex;
		float fade = 1.0f;
		if (p.additive && p.age > p.life * 0.7f)
			fade = 1.0f - (p.age - p.life * 0.7f) / (p.life * 0.3f);
		glColor4f(p.r, p.g, p.b, p.a * fade);
		float h = p.size * 0.5f;
		float r[3], u[3];
		for (int i = 0; i < 3; i++)
		{
			r[i] = g_cl.right[i] * h;
			u[i] = g_cl.up[i] * h;
		}
		const float* o = p.pos;
		glBegin(GL_QUADS);
		glTexCoord2f(p.u0, p.v1);
		glVertex3f(o[0] - r[0] - u[0], o[1] - r[1] - u[1], o[2] - r[2] - u[2]);
		glTexCoord2f(p.u1, p.v1);
		glVertex3f(o[0] + r[0] - u[0], o[1] + r[1] - u[1], o[2] + r[2] - u[2]);
		glTexCoord2f(p.u1, p.v0);
		glVertex3f(o[0] + r[0] + u[0], o[1] + r[1] + u[1], o[2] + r[2] + u[2]);
		glTexCoord2f(p.u0, p.v0);
		glVertex3f(o[0] - r[0] + u[0], o[1] - r[1] + u[1], o[2] - r[2] + u[2]);
		glEnd();
	}
	glDepthMask(GL_TRUE);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}
} // namespace mc
