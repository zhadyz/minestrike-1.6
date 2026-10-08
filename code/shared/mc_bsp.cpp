#include "mc_bsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mcb
{
static const float DIST_EPSILON = 0.03125f;

namespace
{
struct Lump
{
	int ofs, len;
};

template <typename T> T Rd(const uint8_t* p)
{
	T v;
	memcpy(&v, p, sizeof(T));
	return v;
}
} // namespace

bool Map::Load(const char* file)
{
	*this = Map();
	FILE* f = fopen(file, "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> buf((size_t)size);
	bool ok = size > 124 && fread(buf.data(), 1, (size_t)size, f) == (size_t)size;
	fclose(f);
	if (!ok || Rd<int>(buf.data()) != 30)
		return false;
	Lump L[15];
	for (int i = 0; i < 15; i++)
	{
		L[i].ofs = Rd<int>(&buf[4 + i * 8]);
		L[i].len = Rd<int>(&buf[8 + i * 8]);
		if (L[i].ofs < 0 || L[i].len < 0 || (long)L[i].ofs + L[i].len > size)
			return false;
	}
	const uint8_t* B = buf.data();
	path = file;

	entities.assign((const char*)B + L[0].ofs, strnlen((const char*)B + L[0].ofs, (size_t)L[0].len));

	int np = L[1].len / 20;
	planes.resize(np);
	for (int i = 0; i < np; i++)
	{
		const uint8_t* p = B + L[1].ofs + i * 20;
		for (int k = 0; k < 3; k++)
			planes[i].normal[k] = Rd<float>(p + k * 4);
		planes[i].dist = Rd<float>(p + 12);
		planes[i].type = Rd<int>(p + 16);
	}

	// textures
	textureLump.assign(B + L[2].ofs, B + L[2].ofs + L[2].len);
	if (L[2].len >= 4)
	{
		int n = Rd<int>(B + L[2].ofs);
		miptex.resize(n > 0 ? n : 0);
		for (int i = 0; i < n; i++)
		{
			int off = Rd<int>(B + L[2].ofs + 4 + i * 4);
			if (off < 0 || off + 40 > L[2].len)
				continue;
			const uint8_t* m = B + L[2].ofs + off;
			char name[17] = {};
			memcpy(name, m, 16);
			miptex[i].name = name;
			miptex[i].width = Rd<int>(m + 16);
			miptex[i].height = Rd<int>(m + 20);
			if (Rd<int>(m + 24) != 0)
				miptex[i].lumpOffset = off;
		}
	}

	int nv = L[3].len / 12;
	verts.resize((size_t)nv * 3);
	memcpy(verts.data(), B + L[3].ofs, (size_t)nv * 12);

	// render nodes -> hull 0 in clipnode form; needs leaf contents
	int nl = L[10].len / 28;
	std::vector<int> leafContents(nl);
	for (int i = 0; i < nl; i++)
		leafContents[i] = Rd<int>(B + L[10].ofs + i * 28);
	int nn = L[5].len / 24;
	hull0.resize(nn);
	for (int i = 0; i < nn; i++)
	{
		const uint8_t* p = B + L[5].ofs + i * 24;
		hull0[i].planenum = Rd<int>(p);
		for (int k = 0; k < 2; k++)
		{
			int c = Rd<int16_t>(p + 4 + k * 2);
			if (c >= 0)
				hull0[i].children[k] = c;
			else
			{
				int leaf = -1 - c;
				hull0[i].children[k] = (leaf >= 0 && leaf < nl) ? leafContents[leaf] : CONT_SOLID;
			}
		}
	}

	int nti = L[6].len / 40;
	texinfo.resize(nti);
	for (int i = 0; i < nti; i++)
	{
		const uint8_t* p = B + L[6].ofs + i * 40;
		for (int a = 0; a < 2; a++)
			for (int k = 0; k < 4; k++)
				texinfo[i].vecs[a][k] = Rd<float>(p + (a * 4 + k) * 4);
		texinfo[i].miptex = Rd<int>(p + 32);
		texinfo[i].flags = Rd<int>(p + 36);
	}

	int nf = L[7].len / 20;
	faces.resize(nf);
	for (int i = 0; i < nf; i++)
	{
		const uint8_t* p = B + L[7].ofs + i * 20;
		faces[i].planenum = Rd<uint16_t>(p);
		faces[i].side = Rd<int16_t>(p + 2);
		faces[i].firstedge = Rd<int>(p + 4);
		faces[i].numedges = Rd<int16_t>(p + 8);
		faces[i].texinfo = Rd<int16_t>(p + 10);
		memcpy(faces[i].styles, p + 12, 4);
		faces[i].lightofs = Rd<int>(p + 16);
	}

	lighting.assign(B + L[8].ofs, B + L[8].ofs + L[8].len);

	int nc = L[9].len / 8;
	clipnodes.resize(nc);
	for (int i = 0; i < nc; i++)
	{
		const uint8_t* p = B + L[9].ofs + i * 8;
		clipnodes[i].planenum = Rd<int>(p);
		clipnodes[i].children[0] = Rd<int16_t>(p + 4);
		clipnodes[i].children[1] = Rd<int16_t>(p + 6);
	}

	int ne = L[12].len / 4;
	edges.resize((size_t)ne * 2);
	for (int i = 0; i < ne; i++)
	{
		edges[i * 2] = Rd<uint16_t>(B + L[12].ofs + i * 4);
		edges[i * 2 + 1] = Rd<uint16_t>(B + L[12].ofs + i * 4 + 2);
	}
	int nse = L[13].len / 4;
	surfedges.resize(nse);
	memcpy(surfedges.data(), B + L[13].ofs, (size_t)nse * 4);

	int nm = L[14].len / 64;
	models.resize(nm);
	for (int i = 0; i < nm; i++)
	{
		const uint8_t* p = B + L[14].ofs + i * 64;
		for (int k = 0; k < 3; k++)
		{
			models[i].mins[k] = Rd<float>(p + k * 4);
			models[i].maxs[k] = Rd<float>(p + 12 + k * 4);
			models[i].origin[k] = Rd<float>(p + 24 + k * 4);
		}
		for (int k = 0; k < 4; k++)
			models[i].headnode[k] = Rd<int>(p + 36 + k * 4);
		models[i].visleafs = Rd<int>(p + 52);
		models[i].firstface = Rd<int>(p + 56);
		models[i].numfaces = Rd<int>(p + 60);
	}

	// which brush entities collide / draw
	for (const EntityKV& e : ParseEntities(entities))
	{
		const char* model = e.Get("model");
		const char* cls = e.Get("classname");
		if (!model || model[0] != '*' || !cls)
			continue;
		int m = atoi(model + 1);
		if (m <= 0 || m >= nm)
			continue;
		bool trigger = !strncmp(cls, "trigger_", 8) || !strcmp(cls, "func_buyzone") || !strcmp(cls, "func_bomb_target") ||
			!strcmp(cls, "func_hostage_rescue") || !strcmp(cls, "func_escapezone") || !strcmp(cls, "func_vip_safetyzone") ||
			!strcmp(cls, "func_ladder");
		if (trigger)
			continue;
		drawModels.push_back(m);
		if (strcmp(cls, "func_illusionary") != 0)
			solidModels.push_back(m);
	}
	loaded = true;
	return true;
}

int Map::PointContentsTree(const ClipNode* nodes, int num, const float p[3]) const
{
	while (num >= 0)
	{
		const ClipNode& n = nodes[num];
		const Plane& pl = planes[n.planenum];
		float d = pl.type < 3 ? p[pl.type] - pl.dist : p[0] * pl.normal[0] + p[1] * pl.normal[1] + p[2] * pl.normal[2] - pl.dist;
		num = d < 0.0f ? n.children[1] : n.children[0];
	}
	return num;
}

int Map::HullPointContents(int hull, int model, const float p[3]) const
{
	if (!loaded || model < 0 || model >= (int)models.size())
		return CONT_EMPTY;
	const ClipNode* nodes = hull == 0 ? hull0.data() : clipnodes.data();
	return PointContentsTree(nodes, models[model].headnode[hull], p);
}

// The engine's SV_RecursiveHullCheck (ReHLDS form), including its backing-up when the crossing
// point lands in solid.
bool Map::RecursiveHullCheck(const ClipNode* nodes, int num, float p1f, float p2f, const float p1in[3], const float p2[3],
	HullTrace& tr, int firstnode) const
{
	float p1[3] = {p1in[0], p1in[1], p1in[2]};
	for (;;)
	{
		if (num < 0)
		{
			if (num == CONT_SOLID)
				tr.startsolid = true;
			else
			{
				tr.allsolid = false;
				if (num == CONT_EMPTY)
					tr.inopen = true;
			}
			return true;
		}
		const ClipNode& node = nodes[num];
		const Plane& plane = planes[node.planenum];
		float t1, t2;
		if (plane.type < 3)
		{
			t1 = p1[plane.type] - plane.dist;
			t2 = p2[plane.type] - plane.dist;
		}
		else
		{
			t1 = p1[0] * plane.normal[0] + p1[1] * plane.normal[1] + p1[2] * plane.normal[2] - plane.dist;
			t2 = p2[0] * plane.normal[0] + p2[1] * plane.normal[1] + p2[2] * plane.normal[2] - plane.dist;
		}
		if (t1 >= 0.0f && t2 >= 0.0f)
		{
			num = node.children[0];
			continue;
		}
		if (t1 < 0.0f && t2 < 0.0f)
		{
			num = node.children[1];
			continue;
		}
		float midf = t1 >= 0.0f ? t1 - DIST_EPSILON : t1 + DIST_EPSILON;
		midf = midf / (t1 - t2);
		if (midf < 0.0f)
			midf = 0.0f;
		if (midf > 1.0f)
			midf = 1.0f;
		float pdif = p2f - p1f;
		float frac = pdif * midf + p1f;
		float mid[3];
		for (int i = 0; i < 3; i++)
			mid[i] = p1[i] + (p2[i] - p1[i]) * midf;
		int side = t1 < 0.0f ? 1 : 0;
		if (!RecursiveHullCheck(nodes, node.children[side], p1f, frac, p1, mid, tr, firstnode))
			return false;
		if (PointContentsTree(nodes, node.children[side ^ 1], mid) != CONT_SOLID)
		{
			// go past the node
			num = node.children[side ^ 1];
			p1f = frac;
			for (int i = 0; i < 3; i++)
				p1[i] = mid[i];
			continue;
		}
		if (tr.allsolid)
			return false; // never got out of the solid area
		// the other side of the node is solid: this is the impact point
		for (int i = 0; i < 3; i++)
			tr.normal[i] = side ? -plane.normal[i] : plane.normal[i];
		tr.dist = side ? -plane.dist : plane.dist;
		for (int backup = 0; backup < 12; backup++) // at most 11 steps of 0.1; also ends on NaN input
		{
			if (PointContentsTree(nodes, firstnode, mid) != CONT_SOLID)
			{
				tr.fraction = frac;
				for (int i = 0; i < 3; i++)
					tr.endpos[i] = mid[i];
				return false;
			}
			midf -= 0.1f;
			if (!(midf >= 0.0f))
				break;
			frac = pdif * midf + p1f;
			for (int i = 0; i < 3; i++)
				mid[i] = p1[i] + (p2[i] - p1[i]) * midf;
		}
		tr.fraction = frac;
		for (int i = 0; i < 3; i++)
			tr.endpos[i] = mid[i];
		return false;
	}
}

int g_badTraces = 0;

void Map::TraceHull(int hull, int model, const float start[3], const float end[3], HullTrace& tr) const
{
	tr = HullTrace();
	for (int i = 0; i < 3; i++)
		tr.endpos[i] = end[i];
	if (!loaded || model < 0 || model >= (int)models.size() || hull < 0 || hull > 3)
	{
		tr.allsolid = false;
		return;
	}
	for (int i = 0; i < 3; i++)
		if (!isfinite(start[i]) || !isfinite(end[i]))
		{
			// a NaN/inf position would make the recursion's back-up loop spin forever: refuse it
			g_badTraces++;
			tr.allsolid = false;
			return;
		}
	const ClipNode* nodes = hull == 0 ? hull0.data() : clipnodes.data();
	int head = models[model].headnode[hull];
	RecursiveHullCheck(nodes, head, 0.0f, 1.0f, start, end, tr, head);
	if (tr.allsolid)
		tr.startsolid = true;
}

int Map::HullForBox(const float mins[3], const float maxs[3])
{
	float sx = maxs[0] - mins[0], sz = maxs[2] - mins[2];
	if (sx <= 8.0f)
		return 0;
	if (sx <= 36.0f)
		return sz <= 36.0f ? 3 : 1;
	return 2;
}

void Map::HullExtents(int hull, float mins[3], float maxs[3])
{
	static const float H[4][6] = {
		{0, 0, 0, 0, 0, 0}, {-16, -16, -36, 16, 16, 36}, {-32, -32, -32, 32, 32, 32}, {-16, -16, -18, 16, 16, 18}};
	if (hull < 0 || hull > 3)
		hull = 0;
	for (int i = 0; i < 3; i++)
	{
		mins[i] = H[hull][i];
		maxs[i] = H[hull][3 + i];
	}
}

void Map::FaceVertex(int f, int i, float out[3]) const
{
	int se = surfedges[faces[f].firstedge + i];
	int v = se >= 0 ? edges[se * 2] : edges[-se * 2 + 1];
	out[0] = verts[v * 3];
	out[1] = verts[v * 3 + 1];
	out[2] = verts[v * 3 + 2];
}

void Map::FaceNormal(int f, float out[3]) const
{
	const Plane& p = planes[faces[f].planenum];
	float s = faces[f].side ? -1.0f : 1.0f;
	for (int i = 0; i < 3; i++)
		out[i] = p.normal[i] * s;
}

const std::string& Map::FaceTexture(int f) const
{
	static const std::string none;
	int ti = faces[f].texinfo;
	if (ti < 0 || ti >= (int)texinfo.size())
		return none;
	int m = texinfo[ti].miptex;
	return (m >= 0 && m < (int)miptex.size()) ? miptex[m].name : none;
}

const char* EntityKV::Get(const char* key) const
{
	for (const auto& p : kv)
		if (p.first == key)
			return p.second.c_str();
	return nullptr;
}

std::vector<EntityKV> ParseEntities(const std::string& text)
{
	std::vector<EntityKV> out;
	size_t i = 0, n = text.size();
	auto quoted = [&](std::string& s) -> bool {
		while (i < n && text[i] != '"' && text[i] != '}')
			i++;
		if (i >= n || text[i] != '"')
			return false;
		size_t j = text.find('"', i + 1);
		if (j == std::string::npos)
			return false;
		s = text.substr(i + 1, j - i - 1);
		i = j + 1;
		return true;
	};
	while (i < n)
	{
		size_t open = text.find('{', i);
		if (open == std::string::npos)
			break;
		size_t close = text.find('}', open);
		if (close == std::string::npos)
			break;
		EntityKV e;
		i = open + 1;
		std::string k, v;
		while (i < close)
		{
			size_t save = i;
			if (!quoted(k) || i > close)
				break;
			if (!quoted(v) || i > close + 1)
				break;
			e.kv.emplace_back(k, v);
			if (i <= save)
				break;
		}
		out.push_back(std::move(e));
		i = close + 1;
	}
	return out;
}
} // namespace mcb
