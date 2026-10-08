// Counter-Strike's own weapon models in the hands of Minecraft characters.
//
// A p_<gun>.mdl holds the weapon under the CS skeleton's hand bone ("Bip01 R Hand", the second pistol of
// the Elites under "Bip01 L Hand"): the gun's bones hang off the hand with fixed values, so walking each
// vertex's bone chain up to the hand gives the mesh in the hand's own frame, exactly as CS players hold
// it. In that frame the barrel runs along +x and the top of the gun is +z (-z for the left hand), with
// the grip at the origin. The mesh is drawn with the model's own textures and the light of the Minecraft
// character holding it.
#include "hlsdk_client.h"
#include "studio.h"
#include "mc_client.h"
#include "mc_gl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace mc
{
namespace
{
struct GunVert
{
	float pos[3], uv[2], shade;
};
struct GunPart
{
	std::vector<GunVert> tris[8]; // per texture: three vertices per triangle
	float muzzle[3] = {};
	bool hasMuzzle = false, any = false;
};
struct GunModel
{
	bool tried = false;
	GLuint tex[8] = {};
	int numTex = 0;
	GunPart hand[2]; // 0 right, 1 left
};
std::unordered_map<std::string, GunModel> g_models;

// GoldSrc bone math (AngleQuaternion + QuaternionMatrix, mathlib.cpp), 3x4 row-major
typedef float Mat34[3][4];
void BoneMatrix(const float* value, Mat34 m)
{
	float sr = sinf(value[3] * 0.5f), cr = cosf(value[3] * 0.5f);
	float sp = sinf(value[4] * 0.5f), cp = cosf(value[4] * 0.5f);
	float sy = sinf(value[5] * 0.5f), cy = cosf(value[5] * 0.5f);
	float x = sr * cp * cy - cr * sp * sy, y = cr * sp * cy + sr * cp * sy;
	float z = cr * cp * sy - sr * sp * cy, w = cr * cp * cy + sr * sp * sy;
	m[0][0] = 1 - 2 * y * y - 2 * z * z; m[0][1] = 2 * x * y - 2 * w * z; m[0][2] = 2 * x * z + 2 * w * y; m[0][3] = value[0];
	m[1][0] = 2 * x * y + 2 * w * z; m[1][1] = 1 - 2 * x * x - 2 * z * z; m[1][2] = 2 * y * z - 2 * w * x; m[1][3] = value[1];
	m[2][0] = 2 * x * z - 2 * w * y; m[2][1] = 2 * y * z + 2 * w * x; m[2][2] = 1 - 2 * x * x - 2 * y * y; m[2][3] = value[2];
}
void Concat(const Mat34 a, const Mat34 b, Mat34 out)
{
	Mat34 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 4; j++)
			r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0.0f);
	memcpy(out, r, sizeof(r));
}
void Apply(const Mat34 m, const float* p, float* out, bool point)
{
	for (int i = 0; i < 3; i++)
		out[i] = m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + (point ? m[i][3] : 0.0f);
}

GLuint UploadTexture(const byte* base, const mstudiotexture_t& t)
{
	const byte* pix = base + t.index;
	const byte* pal = pix + t.width * t.height;
	bool masked = (t.flags & 0x40) != 0; // STUDIO_NF_MASKED: palette index 255 is see-through
	std::vector<byte> rgba((size_t)t.width * t.height * 4);
	for (int i = 0; i < t.width * t.height; i++)
	{
		int c = pix[i];
		rgba[i * 4 + 0] = pal[c * 3 + 0];
		rgba[i * 4 + 1] = pal[c * 3 + 1];
		rgba[i * 4 + 2] = pal[c * 3 + 2];
		rgba[i * 4 + 3] = (masked && c == 255) ? 0 : 255;
	}
	GLint prev = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &prev);
	GLuint id = 0;
	glGenTextures(1, &id);
	glBindTexture(GL_TEXTURE_2D, id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, t.width, t.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
	if (mcgl::GenerateMipmap)
	{
		mcgl::GenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	}
	else
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, (GLuint)prev);
	return id;
}

void Load(const char* gun, GunModel& gm)
{
	char path[96];
	snprintf(path, sizeof(path), "models/p_%s.mdl", gun);
	int len = 0;
	byte* data = gEngfuncs.COM_LoadFile(path, 5, &len);
	if (!data)
		return;
	const studiohdr_t* hdr = (const studiohdr_t*)data;
	if (len < (int)sizeof(studiohdr_t) || hdr->version != 10 || hdr->numbones > MAXSTUDIOBONES)
	{
		gEngfuncs.COM_FreeFile(data);
		return;
	}
	// textures, embedded or in the companion <name>T.mdl
	byte* tdata = data;
	const studiohdr_t* thdr = hdr;
	if (hdr->numtextures == 0)
	{
		char tpath[100];
		snprintf(tpath, sizeof(tpath), "models/p_%sT.mdl", gun);
		int tlen = 0;
		tdata = gEngfuncs.COM_LoadFile(tpath, 5, &tlen);
		thdr = tdata ? (const studiohdr_t*)tdata : nullptr;
	}
	if (thdr)
	{
		const mstudiotexture_t* tex = (const mstudiotexture_t*)(tdata + thdr->textureindex);
		gm.numTex = thdr->numtextures < 8 ? thdr->numtextures : 8;
		for (int i = 0; i < gm.numTex; i++)
			gm.tex[i] = UploadTexture(tdata, tex[i]);
	}
	const short* skins = thdr ? (const short*)(tdata + thdr->skinindex) : nullptr;
	int texW[8] = {}, texH[8] = {};
	if (thdr)
	{
		const mstudiotexture_t* tex = (const mstudiotexture_t*)(tdata + thdr->textureindex);
		for (int i = 0; i < gm.numTex; i++)
		{
			texW[i] = tex[i].width;
			texH[i] = tex[i].height;
		}
	}

	// each bone's transform relative to the hand it hangs from
	const mstudiobone_t* bones = (const mstudiobone_t*)(data + hdr->boneindex);
	int handBone[2] = {-1, -1};
	for (int i = 0; i < hdr->numbones; i++)
	{
		if (!strcmp(bones[i].name, "Bip01 R Hand"))
			handBone[0] = i;
		else if (!strcmp(bones[i].name, "Bip01 L Hand"))
			handBone[1] = i;
	}
	static Mat34 rel[MAXSTUDIOBONES];
	int side[MAXSTUDIOBONES];
	for (int i = 0; i < hdr->numbones; i++)
	{
		side[i] = -1;
		Mat34 m = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}};
		int b = i;
		while (b >= 0 && b != handBone[0] && b != handBone[1])
		{
			Mat34 bm;
			BoneMatrix(bones[b].value, bm);
			Concat(bm, m, m);
			b = bones[b].parent;
		}
		// the hands themselves only carry stub meshes; the weapon hangs below them
		if (b >= 0 && i != b)
			side[i] = b == handBone[0] ? 0 : 1;
		memcpy(rel[i], m, sizeof(Mat34));
		const char* n = bones[i].name;
		if (side[i] >= 0 && (!strcmp(n, "flash") || !strcmp(n, "rflash") || !strcmp(n, "lflash")))
		{
			GunPart& part = gm.hand[side[i]];
			for (int k = 0; k < 3; k++)
				part.muzzle[k] = m[k][3];
			part.hasMuzzle = true;
		}
	}

	const mstudiobodyparts_t* bp = (const mstudiobodyparts_t*)(data + hdr->bodypartindex);
	for (int b = 0; b < hdr->numbodyparts; b++)
	{
		const mstudiomodel_t* mdl = (const mstudiomodel_t*)(data + bp[b].modelindex); // the first (default) submodel
		const vec3_t* verts = (const vec3_t*)(data + mdl->vertindex);
		const byte* vbone = data + mdl->vertinfoindex;
		const vec3_t* norms = (const vec3_t*)(data + mdl->normindex);
		const byte* nbone = data + mdl->norminfoindex;
		const mstudiomesh_t* meshes = (const mstudiomesh_t*)(data + mdl->meshindex);
		for (int mi = 0; mi < mdl->nummesh; mi++)
		{
			int t = skins ? skins[meshes[mi].skinref] : 0;
			if (t < 0 || t >= gm.numTex)
				t = 0;
			float su = texW[t] ? 1.0f / texW[t] : 0.0f, sv = texH[t] ? 1.0f / texH[t] : 0.0f;
			const short* cmd = (const short*)(data + meshes[mi].triindex);
			int count;
			while ((count = *cmd++) != 0)
			{
				bool fan = count < 0;
				if (fan)
					count = -count;
				GunVert strip[256];
				int hs[256];
				int n = 0;
				for (int k = 0; k < count; k++, cmd += 4)
				{
					if (n >= 256)
						continue;
					int vi = cmd[0], ni = cmd[1];
					int bone = vbone[vi];
					GunVert& v = strip[n];
					hs[n] = side[bone];
					Apply(rel[bone], verts[vi], v.pos, true);
					float nn[3];
					Apply(rel[nbone[ni]], norms[ni], nn, false);
					// soft light from above the gun (+z in the hand frame; the left gun is upside down)
					float up = hs[n] == 1 ? -nn[2] : nn[2];
					v.shade = 0.62f + 0.38f * (up > 0.0f ? up : 0.0f) + 0.12f * fabsf(nn[1]);
					if (v.shade > 1.0f)
						v.shade = 1.0f;
					v.uv[0] = cmd[2] * su;
					v.uv[1] = cmd[3] * sv;
					n++;
				}
				for (int k = 2; k < n; k++)
				{
					int a, b2, c;
					if (fan)
						a = 0, b2 = k - 1, c = k;
					else if (k % 2 == 0)
						a = k - 2, b2 = k - 1, c = k;
					else
						a = k - 1, b2 = k - 2, c = k;
					int s = hs[a];
					if (s < 0 || hs[b2] != s || hs[c] != s)
						continue;
					GunPart& part = gm.hand[s];
					part.tris[t].push_back(strip[a]);
					part.tris[t].push_back(strip[b2]);
					part.tris[t].push_back(strip[c]);
					part.any = true;
				}
			}
		}
	}
	if (tdata != data && tdata)
		gEngfuncs.COM_FreeFile(tdata);
	gEngfuncs.COM_FreeFile(data);
	Log("csguns: p_%s loaded (%d textures, right %s, left %s)", gun, gm.numTex, gm.hand[0].any ? "yes" : "no",
		gm.hand[1].any ? "yes" : "no");
}

GunModel* Get(const char* gun)
{
	GunModel& gm = g_models[gun];
	if (!gm.tried)
	{
		gm.tried = true;
		Load(gun, gm);
	}
	return &gm;
}
} // namespace

bool CsGunHas(const char* gun, int hand)
{
	if (!gun || !gun[0] || hand < 0 || hand > 1)
		return false;
	return Get(gun)->hand[hand].any;
}

bool CsGunMuzzle(const char* gun, int hand, float* out)
{
	if (!CsGunHas(gun, hand))
		return false;
	const GunPart& p = Get(gun)->hand[hand];
	for (int k = 0; k < 3; k++)
		out[k] = p.muzzle[k];
	return p.hasMuzzle;
}

// Draws the gun in the current GL frame = the CS hand bone's frame, in world units.
void CsGunDraw(const char* gun, int hand, float light)
{
	if (!CsGunHas(gun, hand))
		return;
	GunModel& gm = *Get(gun);
	const GunPart& part = gm.hand[hand];
	glPushAttrib(GL_ENABLE_BIT | GL_COLOR_BUFFER_BIT | GL_CURRENT_BIT | GL_TEXTURE_BIT);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.5f);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	for (int t = 0; t < 8; t++)
	{
		if (part.tris[t].empty())
			continue;
		glBindTexture(GL_TEXTURE_2D, gm.tex[t]);
		glBegin(GL_TRIANGLES);
		for (const GunVert& v : part.tris[t])
		{
			float c = v.shade * light;
			glColor3f(c, c, c);
			glTexCoord2fv(v.uv);
			glVertex3fv(v.pos);
		}
		glEnd();
	}
	glPopAttrib();
}
} // namespace mc
