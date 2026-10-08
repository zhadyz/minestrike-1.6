#include "mc_classic.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

namespace mcc
{
static const float DIST_EPSILON = 0.03125f;
static const float CS = MC_BLOCK_SIZE; // cell size

// ---------------------------------------------------------------------------------------------
// Polygon helpers

int ClipPolygon(const float in[][3], int n, const float plane[4], float out[][3], int maxOut)
{
	int m = 0;
	if (n < 3)
		return 0;
	for (int i = 0; i < n; i++)
	{
		const float* a = in[i];
		const float* b = in[(i + 1) % n];
		float da = a[0] * plane[0] + a[1] * plane[1] + a[2] * plane[2] - plane[3];
		float db = b[0] * plane[0] + b[1] * plane[1] + b[2] * plane[2] - plane[3];
		bool ain = da <= 0.0f, bin = db <= 0.0f;
		if (ain && m < maxOut)
		{
			out[m][0] = a[0];
			out[m][1] = a[1];
			out[m][2] = a[2];
			m++;
		}
		if (ain != bin && m < maxOut)
		{
			float t = da / (da - db);
			for (int k = 0; k < 3; k++)
				out[m][k] = a[k] + (b[k] - a[k]) * t;
			m++;
		}
	}
	return m;
}

int PolytopeFace(const std::vector<float>& planes, int i, const float center[3], float out[][3], int maxOut)
{
	const float* p = &planes[i * 4];
	float n[3] = {p[0], p[1], p[2]};
	float dc = n[0] * center[0] + n[1] * center[1] + n[2] * center[2] - p[3];
	float c[3] = {center[0] - n[0] * dc, center[1] - n[1] * dc, center[2] - n[2] * dc};
	// two axes in the plane
	float a[3];
	if (fabsf(n[2]) < 0.9f)
	{
		a[0] = -n[1];
		a[1] = n[0];
		a[2] = 0;
	}
	else
	{
		a[0] = 0;
		a[1] = -n[2];
		a[2] = n[1];
	}
	float al = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
	for (int k = 0; k < 3; k++)
		a[k] /= al;
	float b[3] = {n[1] * a[2] - n[2] * a[1], n[2] * a[0] - n[0] * a[2], n[0] * a[1] - n[1] * a[0]};
	const float R = 256.0f;
	static float bufA[64][3], bufB[64][3];
	int cnt = 4;
	for (int k = 0; k < 3; k++)
	{
		bufA[0][k] = c[k] - a[k] * R - b[k] * R;
		bufA[1][k] = c[k] + a[k] * R - b[k] * R;
		bufA[2][k] = c[k] + a[k] * R + b[k] * R;
		bufA[3][k] = c[k] - a[k] * R + b[k] * R;
	}
	// the quad's winding faces along +n: clipping keeps it consistent
	float(*src)[3] = bufA;
	float(*dst)[3] = bufB;
	int np = (int)planes.size() / 4;
	for (int j = 0; j < np && cnt >= 3; j++)
	{
		if (j == i)
			continue;
		float pl[4] = {planes[j * 4], planes[j * 4 + 1], planes[j * 4 + 2], planes[j * 4 + 3] + 0.001f};
		cnt = ClipPolygon(src, cnt, pl, dst, 64);
		float(*t)[3] = src;
		src = dst;
		dst = t;
	}
	if (cnt < 3)
		return 0;
	int m = cnt < maxOut ? cnt : maxOut;
	for (int v = 0; v < m; v++)
		for (int k = 0; k < 3; k++)
			out[v][k] = src[v][k];
	return m;
}

static float PolyArea(const float p[][3], int n)
{
	float s[3] = {0, 0, 0};
	for (int i = 1; i + 1 < n; i++)
	{
		float u[3] = {p[i][0] - p[0][0], p[i][1] - p[0][1], p[i][2] - p[0][2]};
		float v[3] = {p[i + 1][0] - p[0][0], p[i + 1][1] - p[0][1], p[i + 1][2] - p[0][2]};
		s[0] += u[1] * v[2] - u[2] * v[1];
		s[1] += u[2] * v[0] - u[0] * v[2];
		s[2] += u[0] * v[1] - u[1] * v[0];
	}
	return 0.5f * sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
}

// ---------------------------------------------------------------------------------------------
// Loading and indexes

bool Classic::Load(const char* bspPath)
{
	m_pieces.clear();
	m_info.clear();
	m_faceCells.clear();
	if (!map.Load(bspPath))
		return false;
	if (grid)
		BuildFaceIndex();
	return true;
}

void Classic::CellBox(int x, int y, int z, float mins[3], float maxs[3]) const
{
	int b[3] = {x, y, z};
	for (int i = 0; i < 3; i++)
	{
		mins[i] = grid->origin[i] + b[i] * CS;
		maxs[i] = mins[i] + CS;
	}
}

void Classic::BuildFaceIndex()
{
	m_faceCells.clear();
	std::vector<int> faceList;
	const mcb::Model& w = map.models[0];
	for (int f = w.firstface; f < w.firstface + w.numfaces; f++)
		faceList.push_back(f);
	for (int m : map.drawModels)
		for (int f = map.models[m].firstface; f < map.models[m].firstface + map.models[m].numfaces; f++)
			faceList.push_back(f);
	for (int f : faceList)
	{
		const std::string& tex = map.FaceTexture(f);
		if (!strcasecmp(tex.c_str(), "aaatrigger") || !strcasecmp(tex.c_str(), "clip") || !strcasecmp(tex.c_str(), "origin"))
			continue;
		float mn[3] = {1e9f, 1e9f, 1e9f}, mx[3] = {-1e9f, -1e9f, -1e9f};
		for (int i = 0; i < map.FaceVertexCount(f); i++)
		{
			float v[3];
			map.FaceVertex(f, i, v);
			for (int k = 0; k < 3; k++)
			{
				mn[k] = fminf(mn[k], v[k]);
				mx[k] = fmaxf(mx[k], v[k]);
			}
		}
		int b0[3], b1[3];
		for (int k = 0; k < 3; k++)
		{
			b0[k] = (int)floorf((mn[k] - 0.5f - grid->origin[k]) / CS);
			b1[k] = (int)floorf((mx[k] + 0.5f - grid->origin[k]) / CS);
		}
		for (int z = b0[2]; z <= b1[2]; z++)
			for (int y = b0[1]; y <= b1[1]; y++)
				for (int x = b0[0]; x <= b1[0]; x++)
					m_faceCells[Key(x, y, z)].push_back(f);
	}
}

const char* Classic::TextureAt(const float start[3], const float end[3])
{
	if (!Active())
		return nullptr;
	static const float zero[3] = {0, 0, 0};
	Result r;
	Trace(start, end, zero, zero, r);
	if (!r.hit || r.startsolid)
		return nullptr;
	const float* p = r.endpos;
	const float* n = r.normal;
	// the face lying on the hit point: same plane, facing the same way, point inside its polygon
	int best = -1;
	float bestD = 4.0f;
	for (int side = 0; side < 2; side++)
	{
		float q[3] = {p[0] - n[0] * (side ? -0.5f : 0.5f), p[1] - n[1] * (side ? -0.5f : 0.5f), p[2] - n[2] * (side ? -0.5f : 0.5f)};
		int c[3];
		grid->ToBlock(q, c);
		const std::vector<int>* fl = FacesInCell(c[0], c[1], c[2]);
		if (!fl)
			continue;
		for (int f : *fl)
		{
			float fn[3];
			map.FaceNormal(f, fn);
			if (fn[0] * n[0] + fn[1] * n[1] + fn[2] * n[2] < 0.7f)
				continue;
			float v0[3];
			map.FaceVertex(f, 0, v0);
			float d = fabsf((p[0] - v0[0]) * fn[0] + (p[1] - v0[1]) * fn[1] + (p[2] - v0[2]) * fn[2]);
			if (d > bestD)
				continue;
			// inside the (convex) polygon: on the same side of every edge (either winding)
			int nv = map.FaceVertexCount(f);
			int pos = 0, neg = 0;
			for (int i = 0; i < nv; i++)
			{
				float a[3], b[3];
				map.FaceVertex(f, i, a);
				map.FaceVertex(f, (i + 1) % nv, b);
				float e[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
				float w[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
				float cr[3] = {e[1] * w[2] - e[2] * w[1], e[2] * w[0] - e[0] * w[2], e[0] * w[1] - e[1] * w[0]};
				float s = cr[0] * fn[0] + cr[1] * fn[1] + cr[2] * fn[2];
				if (s > 0.5f)
					pos++;
				else if (s < -0.5f)
					neg++;
			}
			bool inside = !(pos && neg);
			if (inside)
			{
				best = f;
				bestD = d;
			}
		}
	}
	if (best >= 0)
		return map.FaceTexture(best).c_str();
	// a cut wall: the dug cell's representative texture
	float q[3] = {p[0] - n[0] * 0.5f, p[1] - n[1] * 0.5f, p[2] - n[2] * 0.5f};
	int c[3];
	grid->ToBlock(q, c);
	const CellInfo& ci = Info(c[0], c[1], c[2]);
	if (ci.miptex >= 0 && ci.miptex < (int)map.miptex.size())
		return map.miptex[ci.miptex].name.c_str();
	return nullptr;
}

const std::vector<int>* Classic::FacesInCell(int x, int y, int z) const
{
	auto it = m_faceCells.find(Key(x, y, z));
	return it == m_faceCells.end() ? nullptr : &it->second;
}

// ---------------------------------------------------------------------------------------------
// Solid space of a cell as convex pieces

void Classic::Decompose(const mcb::ClipNode* nodes, int num, std::vector<float>& planes, const float cmid[3],
	std::vector<Piece>& out, bool)
{
	// find the next node that actually splits the current polytope
	static float face[64][3];
	for (;;)
	{
		if (num < 0)
		{
			if (num != mcb::CONT_SOLID && num != mcb::CONT_SKY)
				return;
			Piece pc;
			pc.sky = num == mcb::CONT_SKY;
			for (int k = 0; k < 3; k++)
			{
				pc.mins[k] = 1e9f;
				pc.maxs[k] = -1e9f;
			}
			int np = (int)planes.size() / 4;
			std::vector<float> fv;        // all face polygons, flattened
			std::vector<int> fcount;      // vertex count per polygon
			for (int i = 0; i < np; i++)
			{
				int n = PolytopeFace(planes, i, cmid, face, 64);
				if (n < 3)
					continue;
				for (int k = 0; k < 4; k++)
					pc.planes.push_back(planes[i * 4 + k]);
				fcount.push_back(n);
				for (int v = 0; v < n; v++)
					for (int k = 0; k < 3; k++)
					{
						fv.push_back(face[v][k]);
						pc.mins[k] = fminf(pc.mins[k], face[v][k]);
						pc.maxs[k] = fmaxf(pc.maxs[k], face[v][k]);
					}
			}
			// Quake 3 edge bevels: for every edge and axis, the plane through the edge parallel to that
			// axis, when the whole piece is behind it. Without them a box sweeps into "fat" corners where
			// two slanted faces meet.
			size_t nfv = fv.size() / 3;
			auto addPlane = [&](const float n[3], float d) {
				for (size_t q = 0; q + 3 < pc.planes.size(); q += 4)
					if (n[0] * pc.planes[q] + n[1] * pc.planes[q + 1] + n[2] * pc.planes[q + 2] > 0.999f && fabsf(d - pc.planes[q + 3]) < 0.02f)
						return;
				for (size_t v = 0; v < nfv; v++)
					if (fv[v * 3] * n[0] + fv[v * 3 + 1] * n[1] + fv[v * 3 + 2] * n[2] > d + 0.02f)
						return;
				pc.planes.push_back(n[0]);
				pc.planes.push_back(n[1]);
				pc.planes.push_back(n[2]);
				pc.planes.push_back(d);
			};
			size_t base = 0;
			for (int c : fcount)
			{
				for (int a = 0; a < c; a++)
				{
					const float* v0 = &fv[(base + a) * 3];
					const float* v1 = &fv[(base + (a + 1) % c) * 3];
					float e[3] = {v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2]};
					float el = sqrtf(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
					if (el < 0.5f)
						continue;
					for (int k = 0; k < 3; k++)
						e[k] /= el;
					for (int ax = 0; ax < 3; ax++)
					{
						if (fabsf(e[ax]) > 0.999f)
							continue; // the edge is along this axis
						for (int sg = -1; sg <= 1; sg += 2)
						{
							float axv[3] = {0, 0, 0};
							axv[ax] = (float)sg;
							float n[3] = {e[1] * axv[2] - e[2] * axv[1], e[2] * axv[0] - e[0] * axv[2], e[0] * axv[1] - e[1] * axv[0]};
							float nl = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
							if (nl < 0.05f)
								continue;
							for (int k = 0; k < 3; k++)
								n[k] /= nl;
							addPlane(n, n[0] * v0[0] + n[1] * v0[1] + n[2] * v0[2]);
						}
					}
				}
				base += c;
			}
			if (pc.planes.size() >= 16 && pc.maxs[0] - pc.mins[0] > 0.01f && pc.maxs[1] - pc.mins[1] > 0.01f &&
				pc.maxs[2] - pc.mins[2] > 0.01f)
				out.push_back(std::move(pc));
			return;
		}
		const mcb::ClipNode& node = nodes[num];
		const mcb::Plane& pl = map.planes[node.planenum];
		{
			// quick reject with the cell's box (the polytope is always inside it)
			float bmin = 0, bmax = 0;
			for (int k = 0; k < 3; k++)
			{
				float a = pl.normal[k] * m_cellMin[k], b = pl.normal[k] * m_cellMax[k];
				bmin += a < b ? a : b;
				bmax += a < b ? b : a;
			}
			bmin -= pl.dist;
			bmax -= pl.dist;
			if (bmin >= -0.01f)
			{
				num = node.children[0];
				continue;
			}
			if (bmax <= 0.01f)
			{
				num = node.children[1];
				continue;
			}
		}
		// classify the polytope's vertices against the plane
		float dmin = 1e9f, dmax = -1e9f;
		int np = (int)planes.size() / 4;
		int nverts = 0;
		for (int i = 0; i < np; i++)
		{
			int n = PolytopeFace(planes, i, cmid, face, 64);
			for (int v = 0; v < n; v++)
			{
				float d = face[v][0] * pl.normal[0] + face[v][1] * pl.normal[1] + face[v][2] * pl.normal[2] - pl.dist;
				dmin = fminf(dmin, d);
				dmax = fmaxf(dmax, d);
				nverts++;
			}
		}
		if (nverts < 4)
			return; // empty polytope
		const float eps = 0.01f;
		if (dmin >= -eps)
		{
			num = node.children[0];
			continue;
		}
		if (dmax <= eps)
		{
			num = node.children[1];
			continue;
		}
		size_t base = planes.size();
		// front: n.x >= dist  ->  -n.x <= -dist
		planes.push_back(-pl.normal[0]);
		planes.push_back(-pl.normal[1]);
		planes.push_back(-pl.normal[2]);
		planes.push_back(-pl.dist);
		Decompose(nodes, node.children[0], planes, cmid, out, false);
		planes[base] = pl.normal[0];
		planes[base + 1] = pl.normal[1];
		planes[base + 2] = pl.normal[2];
		planes[base + 3] = pl.dist;
		Decompose(nodes, node.children[1], planes, cmid, out, false);
		planes.resize(base);
		return;
	}
}

const std::vector<Piece>& Classic::Pieces(int x, int y, int z)
{
	uint32_t k = Key(x, y, z);
	auto it = m_pieces.find(k);
	if (it != m_pieces.end())
		return it->second;
	std::vector<Piece>& out = m_pieces[k];
	if (!map.loaded || !grid)
		return out;
	float mn[3], mx[3];
	CellBox(x, y, z, mn, mx);
	float cmid[3] = {(mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f};
	for (int k = 0; k < 3; k++)
	{
		m_cellMin[k] = mn[k];
		m_cellMax[k] = mx[k];
	}
	std::vector<float> planes = {1, 0, 0, mx[0], -1, 0, 0, -mn[0], 0, 1, 0, mx[1], 0, -1, 0, -mn[1], 0, 0, 1, mx[2], 0, 0, -1, -mn[2]};
	Decompose(map.hull0.data(), map.models[0].headnode[0], planes, cmid, out, false);
	for (int m : map.solidModels)
	{
		const mcb::Model& md = map.models[m];
		bool overlap = true;
		for (int i = 0; i < 3; i++)
			if (md.maxs[i] < mn[i] || md.mins[i] > mx[i])
				overlap = false;
		if (!overlap)
			continue;
		std::vector<float> p2(planes.begin(), planes.begin() + 24);
		std::vector<Piece> sub;
		Decompose(map.hull0.data(), md.headnode[0], p2, cmid, sub, false);
		for (Piece& pc : sub)
		{
			pc.sky = false;
			out.push_back(std::move(pc));
		}
	}
	return out;
}

// ---------------------------------------------------------------------------------------------
// Box vs convex piece (the Quake 3 brush trace, with the engine's epsilon)

static void BoxSupport(const float n[3], const float bmins[3], const float bmaxs[3], float& s)
{
	s = 0;
	for (int k = 0; k < 3; k++)
		s += n[k] * (n[k] > 0 ? bmins[k] : bmaxs[k]);
}

// Returns true and updates best when the piece is entered earlier than best.
static bool TracePiece(const Piece& pc, const float s[3], const float e[3], const float bmins[3], const float bmaxs[3],
	float& best, float bestNormal[3], bool& startsolid, bool& allsolid)
{
	float enterFrac = -1.0f, leaveFrac = 1.0f;
	const float* clip = nullptr;
	bool getout = false, startout = false;
	int np = (int)pc.planes.size() / 4;
	// the piece's own planes plus its bounding box as axial bevels
	float bevel[6][4] = {{1, 0, 0, pc.maxs[0]}, {-1, 0, 0, -pc.mins[0]}, {0, 1, 0, pc.maxs[1]}, {0, -1, 0, -pc.mins[1]},
		{0, 0, 1, pc.maxs[2]}, {0, 0, -1, -pc.mins[2]}};
	for (int i = 0; i < np + 6; i++)
	{
		const float* p = i < np ? &pc.planes[i * 4] : bevel[i - np];
		float sup;
		BoxSupport(p, bmins, bmaxs, sup);
		float d = p[3] - sup;
		float d1 = s[0] * p[0] + s[1] * p[1] + s[2] * p[2] - d;
		float d2 = e[0] * p[0] + e[1] * p[1] + e[2] * p[2] - d;
		if (d2 >= 0.0f)
			getout = true;
		if (d1 >= 0.0f)
			startout = true;
		if (d1 > 0.0f && (d2 >= DIST_EPSILON || d2 >= d1))
			return false; // completely in front of this face
		if (d1 <= 0.0f && d2 <= 0.0f)
			continue;
		if (d1 > d2)
		{
			float f = (d1 - DIST_EPSILON) / (d1 - d2);
			if (f < 0)
				f = 0;
			if (f > enterFrac)
			{
				enterFrac = f;
				clip = p;
			}
		}
		else
		{
			float f = (d1 + DIST_EPSILON) / (d1 - d2);
			if (f > 1)
				f = 1;
			if (f < leaveFrac)
				leaveFrac = f;
		}
	}
	if (!startout)
	{
		startsolid = true;
		if (!getout)
		{
			allsolid = true;
			best = 0.0f;
		}
		return false;
	}
	if (enterFrac < leaveFrac && enterFrac > -1.0f && enterFrac < best && clip)
	{
		best = enterFrac < 0 ? 0 : enterFrac;
		bestNormal[0] = clip[0];
		bestNormal[1] = clip[1];
		bestNormal[2] = clip[2];
		return true;
	}
	return false;
}

static bool InsidePiece(const Piece& pc, const float o[3], const float bmins[3], const float bmaxs[3])
{
	int np = (int)pc.planes.size() / 4;
	for (int i = 0; i < np; i++)
	{
		const float* p = &pc.planes[i * 4];
		float sup;
		BoxSupport(p, bmins, bmaxs, sup);
		if (o[0] * p[0] + o[1] * p[1] + o[2] * p[2] - (p[3] - sup) > -0.001f)
			return false;
	}
	for (int k = 0; k < 3; k++)
		if (o[k] + bmaxs[k] <= pc.mins[k] + 0.001f || o[k] + bmins[k] >= pc.maxs[k] - 0.001f)
			return false;
	return true;
}

bool Classic::BoxOverlapsIntact(const float o[3], const float hm[3], const float hM[3], bool point)
{
	int b0[3], b1[3];
	for (int k = 0; k < 3; k++)
	{
		b0[k] = (int)floorf((o[k] + hm[k] - 0.5f - grid->origin[k]) / CS);
		b1[k] = (int)floorf((o[k] + hM[k] + 0.5f - grid->origin[k]) / CS);
	}
	for (int z = b0[2]; z <= b1[2]; z++)
		for (int y = b0[1]; y <= b1[1]; y++)
			for (int x = b0[0]; x <= b1[0]; x++)
			{
				if (IsCarved(grid->Get(x, y, z)))
					continue;
				for (const Piece& pc : Pieces(x, y, z))
				{
					if (point && pc.sky)
						continue;
					if (InsidePiece(pc, o, hm, hM))
						return true;
				}
			}
	return false;
}

bool Classic::AnyModifiedInBox(const float mins[3], const float maxs[3]) const
{
	if (!grid)
		return false;
	int b0[3], b1[3];
	for (int k = 0; k < 3; k++)
	{
		b0[k] = (int)floorf((mins[k] - grid->origin[k]) / CS);
		b1[k] = (int)floorf((maxs[k] - grid->origin[k]) / CS);
	}
	for (int z = b0[2]; z <= b1[2]; z++)
		for (int y = b0[1]; y <= b1[1]; y++)
			for (int x = b0[0]; x <= b1[0]; x++)
				if (IsCarved(grid->Get(x, y, z)))
					return true;
	return false;
}

void Classic::FallbackTrace(const float o0[3], const float o1[3], const float bmins[3], const float bmaxs[3], float tmin,
	bool point, Result& out, int hull)
{
	float d[3] = {o1[0] - o0[0], o1[1] - o0[1], o1[2] - o0[2]};
	float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	float best = 1.0f, nrm[3] = {0, 0, 0};
	bool startsolid = false, allsolid = false;
	const float step = CS * 0.5f;
	int steps = len > 0.0f ? (int)ceilf(len * (1.0f - tmin) / step) : 0;
	// Cells are swept once: step boxes move monotonically along each axis, so a cell seen in any earlier
	// step that is in this step's box was also in the previous step's box.
	int pb0[3] = {1, 1, 1}, pb1[3] = {0, 0, 0}; // previous box (empty)
	int quiet = 0;                               // consecutive steps with no dug cell in reach
	for (int s = 0; s <= steps; s++)
	{
		float t = len > 0.0f ? tmin + (s * step) / len : 0.0f;
		if (t > 1.0f)
			t = 1.0f;
		if (t - step / (len > 0 ? len : 1) > best)
			break; // passed the nearest hit
		float p[3] = {o0[0] + d[0] * t, o0[1] + d[1] * t, o0[2] + d[2] * t};
		int b0[3], b1[3];
		for (int k = 0; k < 3; k++)
		{
			b0[k] = (int)floorf((p[k] + bmins[k] - step - 1.0f - grid->origin[k]) / CS);
			b1[k] = (int)floorf((p[k] + bmaxs[k] + step + 1.0f - grid->origin[k]) / CS);
		}
		bool anyCarved = false;
		for (int z = b0[2]; z <= b1[2]; z++)
			for (int y = b0[1]; y <= b1[1]; y++)
				for (int x = b0[0]; x <= b1[0]; x++)
				{
					bool carved = IsCarved(grid->Get(x, y, z));
					anyCarved = anyCarved || carved;
					if (x >= pb0[0] && x <= pb1[0] && y >= pb0[1] && y <= pb1[1] && z >= pb0[2] && z <= pb1[2])
						continue; // swept in the previous step
					if (carved)
						continue; // dug out
					for (const Piece& pc : Pieces(x, y, z))
					{
						if (point && pc.sky)
							continue;
						TracePiece(pc, o0, o1, bmins, bmaxs, best, nrm, startsolid, allsolid);
					}
				}
		for (int k = 0; k < 3; k++)
		{
			pb0[k] = b0[k];
			pb1[k] = b1[k];
		}
		if (len <= 0.0f)
			break;
		quiet = anyCarved ? 0 : quiet + 1;
		// clear of dug cells with nothing hit yet: the rest of the map is original, so the engine-exact
		// trace takes over from here (and the march resumes only if that runs into dug space again)
		if (hull >= 0 && quiet >= 2 && best >= 1.0f && !startsolid && t < 1.0f)
		{
			mcb::HullTrace ht;
			HullTraceAll(hull, p, o1, ht);
			if (ht.startsolid)
			{
				quiet = 0;
				continue;
			}
			if (ht.fraction >= 1.0f)
				break; // nothing more in the way
			float hitT = t + (1.0f - t) * ht.fraction;
			if (!ContactIsCarved(ht, p, bmins, bmaxs, hull))
			{
				best = hitT;
				for (int k = 0; k < 3; k++)
					nrm[k] = ht.normal[k];
				break;
			}
			// it ran into dug space: march on from just before that contact
			int target = (int)floorf(((hitT - tmin) * len - 4.0f) / step);
			if (target > s)
			{
				s = target - 1;
				for (int k = 0; k < 3; k++)
				{
					pb0[k] = 1;
					pb1[k] = 0;
				}
			}
			quiet = 0;
		}
	}
	out.startsolid = startsolid;
	out.allsolid = allsolid;
	out.fraction = allsolid ? 0.0f : best;
	out.hit = out.fraction < 1.0f;
	for (int k = 0; k < 3; k++)
		out.normal[k] = out.hit ? nrm[k] : 0.0f;
}

// ---------------------------------------------------------------------------------------------
// Public collision

void Classic::HullTraceAll(int hull, const float s[3], const float e[3], mcb::HullTrace& best)
{
	map.TraceHull(hull, 0, s, e, best);
	for (int m : map.solidModels)
	{
		mcb::HullTrace t;
		map.TraceHull(hull, m, s, e, t);
		if (t.startsolid || t.allsolid || t.fraction < best.fraction)
		{
			bool ss = best.startsolid;
			best = t;
			if (ss)
				best.startsolid = true;
		}
	}
}

bool Classic::ContactIsCarved(const mcb::HullTrace& t, const float s[3], const float hm[3], const float hM[3], int hull)
{
	if (!grid || !(t.fraction < 1.0f || t.startsolid))
		return false;
	float probe[3], cm[3], cM[3];
	for (int k = 0; k < 3; k++)
	{
		probe[k] = t.startsolid ? s[k] : t.endpos[k] - t.normal[k] * 0.5f;
		cm[k] = probe[k] + hm[k] - 1.0f;
		cM[k] = probe[k] + hM[k] + 1.0f;
	}
	return AnyModifiedInBox(cm, cM) && !BoxOverlapsIntact(probe, hm, hM, hull == 0);
}

void Classic::Trace(const float start[3], const float end[3], const float mins[3], const float maxs[3], Result& out)
{
	out = Result();
	for (int k = 0; k < 3; k++)
		out.endpos[k] = end[k];
	if (!map.loaded)
		return;
	int hull = mcb::Map::HullForBox(mins, maxs);
	float hm[3], hM[3], off[3];
	mcb::Map::HullExtents(hull, hm, hM);
	for (int k = 0; k < 3; k++)
		off[k] = hull == 0 ? 0.0f : hm[k] - mins[k];
	float s[3], e[3];
	for (int k = 0; k < 3; k++)
	{
		s[k] = start[k] - off[k];
		e[k] = end[k] - off[k];
	}
	mcb::HullTrace best;
	map.TraceHull(hull, 0, s, e, best);
	for (int m : map.solidModels)
	{
		const mcb::Model& md = map.models[m];
		mcb::HullTrace t;
		map.TraceHull(hull, m, s, e, t);
		(void)md;
		if (t.startsolid || t.allsolid || t.fraction < best.fraction)
		{
			bool ss = best.startsolid;
			best = t;
			if (ss)
				best.startsolid = true;
		}
	}
	// The engine-exact result stands unless what it ran into has been dug out. Probe just past the
	// contact (or the start, when the hull says we begin in solid): if the box there overlaps classic
	// solid that is still intact, the contact is real and the hull trace is right.
	bool fallback = false;
	if (grid && (best.fraction < 1.0f || best.startsolid))
	{
		float probe[3];
		for (int k = 0; k < 3; k++)
			probe[k] = best.startsolid ? s[k] : best.endpos[k] - best.normal[k] * 0.5f;
		float cm[3], cM[3];
		for (int k = 0; k < 3; k++)
		{
			cm[k] = probe[k] + hm[k] - 1.0f;
			cM[k] = probe[k] + hM[k] + 1.0f;
		}
		if (AnyModifiedInBox(cm, cM))
			fallback = !BoxOverlapsIntact(probe, hm, hM, hull == 0);
	}
	if (fallback)
	{
		float len = sqrtf((e[0] - s[0]) * (e[0] - s[0]) + (e[1] - s[1]) * (e[1] - s[1]) + (e[2] - s[2]) * (e[2] - s[2]));
		float tmin = 0.0f;
		if (!best.startsolid && len > 0.0f)
		{
			tmin = best.fraction - 4.0f / len;
			if (tmin < 0.0f)
				tmin = 0.0f;
		}
		FallbackTrace(s, e, hm, hM, tmin, hull == 0, out, hull);
		out.fallback = true;
		for (int k = 0; k < 3; k++)
			out.endpos[k] = start[k] + (end[k] - start[k]) * out.fraction;
		return;
	}
	out.fraction = best.fraction;
	out.startsolid = best.startsolid;
	out.allsolid = best.allsolid;
	out.hit = best.fraction < 1.0f;
	for (int k = 0; k < 3; k++)
	{
		out.endpos[k] = best.fraction < 1.0f ? best.endpos[k] + off[k] : end[k];
		out.normal[k] = out.hit ? best.normal[k] : 0.0f;
	}
}

void Classic::TraceSlow(const float start[3], const float end[3], const float mins[3], const float maxs[3], Result& out,
	bool handBack)
{
	out = Result();
	int hull = mcb::Map::HullForBox(mins, maxs);
	float hm[3], hM[3], s[3], e[3];
	mcb::Map::HullExtents(hull, hm, hM);
	for (int k = 0; k < 3; k++)
	{
		float off = hull == 0 ? 0.0f : hm[k] - mins[k];
		s[k] = start[k] - off;
		e[k] = end[k] - off;
	}
	FallbackTrace(s, e, hm, hM, 0.0f, hull == 0, out, handBack ? hull : -1);
	for (int k = 0; k < 3; k++)
		out.endpos[k] = start[k] + (end[k] - start[k]) * out.fraction;
}

int Classic::PointContents(const float p[3])
{
	if (!map.loaded)
		return mcb::CONT_EMPTY;
	if (grid)
	{
		int b[3];
		grid->ToBlock(p, b);
		if (IsCarved(grid->Get(b[0], b[1], b[2])))
			return mcb::CONT_EMPTY;
	}
	int c = map.HullPointContents(0, 0, p);
	if (c != mcb::CONT_SOLID)
		for (int m : map.solidModels)
			if (map.HullPointContents(0, m, p) == mcb::CONT_SOLID)
				return mcb::CONT_SOLID;
	return c;
}

bool Classic::TestBox(const float origin[3], const float mins[3], const float maxs[3])
{
	if (!map.loaded)
		return false;
	int hull = mcb::Map::HullForBox(mins, maxs);
	float hm[3], hM[3], o[3];
	mcb::Map::HullExtents(hull, hm, hM);
	for (int k = 0; k < 3; k++)
		o[k] = origin[k] - (hull == 0 ? 0.0f : hm[k] - mins[k]);
	bool solid = map.HullPointContents(hull, 0, o) == mcb::CONT_SOLID;
	if (!solid)
		for (int m : map.solidModels)
			if (map.HullPointContents(hull, m, o) == mcb::CONT_SOLID)
			{
				solid = true;
				break;
			}
	if (!solid)
		return false;
	float cm[3], cM[3];
	for (int k = 0; k < 3; k++)
	{
		cm[k] = o[k] + hm[k] - 1.0f;
		cM[k] = o[k] + hM[k] + 1.0f;
	}
	if (!AnyModifiedInBox(cm, cM))
		return true;
	int b0[3], b1[3];
	for (int k = 0; k < 3; k++)
	{
		b0[k] = (int)floorf((cm[k] - grid->origin[k]) / CS);
		b1[k] = (int)floorf((cM[k] - grid->origin[k]) / CS);
	}
	for (int z = b0[2]; z <= b1[2]; z++)
		for (int y = b0[1]; y <= b1[1]; y++)
			for (int x = b0[0]; x <= b1[0]; x++)
			{
				if (IsCarved(grid->Get(x, y, z)))
					continue;
				for (const Piece& pc : Pieces(x, y, z))
				{
					if (hull == 0 && pc.sky)
						continue;
					if (InsidePiece(pc, o, hm, hM))
						return true;
				}
			}
	return false;
}

bool Classic::PickCell(const float start[3], const float dir[3], float maxDist, int cell[3], int* face, float* dist)
{
	if (!Active())
		return false;
	float end[3] = {start[0] + dir[0] * maxDist, start[1] + dir[1] * maxDist, start[2] + dir[2] * maxDist};
	float zero[3] = {0, 0, 0};
	Result r;
	Trace(start, end, zero, zero, r);
	if (!r.hit || r.startsolid)
		return false;
	float p[3];
	for (int k = 0; k < 3; k++)
		p[k] = r.endpos[k] - r.normal[k] * 0.5f;
	grid->ToBlock(p, cell);
	int ax = 0;
	for (int k = 1; k < 3; k++)
		if (fabsf(r.normal[k]) > fabsf(r.normal[ax]))
			ax = k;
	if (face)
		*face = ax * 2 + (r.normal[ax] > 0 ? 0 : 1);
	if (dist)
		*dist = r.fraction * maxDist;
	return true;
}

// ---------------------------------------------------------------------------------------------
// Cell info: material, ores, diggability

Material Classic::MaterialOfTexture(const char* name)
{
	char low[32];
	size_t i = 0;
	for (; name[i] && i < sizeof(low) - 1; i++)
		low[i] = (char)tolower((unsigned char)name[i]);
	low[i] = 0;
	if (strstr(low, "sky"))
		return MAT_SKY;
	if (strstr(low, "door"))
		return MAT_DOOR;
	if (strstr(low, "crt") || strstr(low, "crate"))
		return MAT_WOOD;
	if (strstr(low, "wndw") || strstr(low, "glass") || strstr(low, "lgt"))
		return MAT_GLASS;
	if (strstr(low, "wll") || strstr(low, "wall") || strstr(low, "ccrete") || strstr(low, "trim") || strstr(low, "rock") ||
		strstr(low, "credit"))
		return MAT_SANDSTONE;
	if (strstr(low, "sand"))
		return MAT_SAND;
	return MAT_STONE;
}

const char* Classic::OreName(int ore)
{
	static const char* names[] = {nullptr, "coal_ore", "iron_ore", "gold_ore", "redstone_ore", "diamond_ore", "lapis_ore"};
	return ore > 0 && ore < 7 ? names[ore] : nullptr;
}

const CellInfo& Classic::Info(int x, int y, int z)
{
	uint32_t key = Key(x, y, z);
	auto it = m_info.find(key);
	if (it != m_info.end() && it->second.computed)
		return it->second;
	CellInfo ci;
	ci.computed = true;
	if (!Active())
		return m_info[key] = ci;
	for (const Piece& pc : Pieces(x, y, z))
	{
		if (pc.sky)
			ci.skyish = true;
		else
			ci.solid = true;
	}
	// the biggest classic face inside the cell whose solid side is in the cell
	auto direct = [&](int cx, int cy, int cz, bool& sky) -> int {
		sky = false;
		const std::vector<int>* fl = FacesInCell(cx, cy, cz);
		if (!fl)
			return -1;
		float mn[3], mx[3];
		CellBox(cx, cy, cz, mn, mx);
		float planes[6][4] = {{1, 0, 0, mx[0]}, {-1, 0, 0, -mn[0]}, {0, 1, 0, mx[1]}, {0, -1, 0, -mn[1]}, {0, 0, 1, mx[2]}, {0, 0, -1, -mn[2]}};
		int bestTex = -1;
		float bestArea = 0.5f;
		static float a[64][3], b[64][3];
		for (int f : *fl)
		{
			int n = map.FaceVertexCount(f);
			if (n > 64)
				n = 64;
			for (int v = 0; v < n; v++)
				map.FaceVertex(f, v, a[v]);
			for (int p = 0; p < 6 && n >= 3; p++)
			{
				n = ClipPolygon(a, n, planes[p], b, 64);
				memcpy(a, b, sizeof(float) * 3 * n);
			}
			if (n < 3)
				continue;
			float area = PolyArea(a, n);
			const std::string& tex = map.FaceTexture(f);
			if (MaterialOfTexture(tex.c_str()) == MAT_SKY)
			{
				if (area > 1.0f)
					sky = true;
				continue;
			}
			if (area > bestArea)
			{
				bestArea = area;
				bestTex = map.texinfo[map.faces[f].texinfo].miptex;
			}
		}
		return bestTex;
	};
	bool sky = false;
	int tex = direct(x, y, z, sky);
	if (sky)
		ci.skyish = true;
	if (tex < 0)
	{
		ci.interior = true;
		// buried: take the material of the nearest surface, looking up first (the floor above us)
		static const int order[][3] = {{0, 0, 1}, {0, 0, 2}, {0, 0, 3}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, -1}, {2, 0, 0},
			{-2, 0, 0}, {0, 2, 0}, {0, -2, 0}, {0, 0, 4}, {0, 0, 5}, {0, 0, 6}, {3, 0, 0}, {-3, 0, 0}, {0, 3, 0}, {0, -3, 0}};
		for (const auto& o : order)
		{
			bool s2;
			int t = direct(x + o[0], y + o[1], z + o[2], s2);
			if (t >= 0)
			{
				tex = t;
				break;
			}
		}
		if (tex < 0)
		{
			for (size_t i = 0; i < map.miptex.size(); i++)
				if (MaterialOfTexture(map.miptex[i].name.c_str()) == MAT_SAND)
				{
					tex = (int)i;
					break;
				}
		}
	}
	ci.miptex = (int16_t)tex;
	ci.material = tex >= 0 ? MaterialOfTexture(map.miptex[tex].name.c_str()) : MAT_STONE;
	if (ci.material == MAT_DOOR && ci.interior)
		ci.material = MAT_SANDSTONE; // the wall behind a painted door
	// Minecraft ores in the buried rock below the map (deterministic, same on both sides)
	if (ci.interior && ci.solid && z < 10)
	{
		uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)z * 83492791u;
		h ^= h >> 13;
		h *= 0x5bd1e995u;
		h ^= h >> 15;
		int r = (int)(h % 1000);
		if (z < 4 && r < 5)
			ci.ore = 5; // diamond
		else if (z < 6 && r < 15)
			ci.ore = 4; // redstone
		else if (z < 6 && r < 22)
			ci.ore = 3; // gold
		else if (z < 8 && r < 26)
			ci.ore = 6; // lapis
		else if (r < 45)
			ci.ore = 2; // iron
		else if (r < 75)
			ci.ore = 1; // coal
	}
	return m_info[key] = ci;
}

bool Classic::Diggable(int x, int y, int z)
{
	if (!Active() || !grid->InBounds(x, y, z) || z <= 0 || IsCarved(grid->Get(x, y, z)))
		return false;
	const CellInfo& ci = Info(x, y, z);
	return ci.solid && !ci.skyish;
}

void Classic::DoorCells(int x, int y, int z, std::vector<int>& out3)
{
	out3.clear();
	std::vector<int> stack = {x, y, z};
	std::vector<uint32_t> seen = {Key(x, y, z)};
	while (!stack.empty() && out3.size() < 48 * 3)
	{
		int cz = stack.back();
		stack.pop_back();
		int cy = stack.back();
		stack.pop_back();
		int cx = stack.back();
		stack.pop_back();
		if (!Diggable(cx, cy, cz) || Info(cx, cy, cz).material != MAT_DOOR)
			continue;
		out3.push_back(cx);
		out3.push_back(cy);
		out3.push_back(cz);
		static const int nb[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
		for (const auto& n : nb)
		{
			int nx = cx + n[0], ny = cy + n[1], nz = cz + n[2];
			if (abs(nx - x) > 3 || abs(ny - y) > 3 || abs(nz - z) > 4)
				continue;
			uint32_t k = Key(nx, ny, nz);
			bool s = false;
			for (uint32_t v : seen)
				if (v == k)
					s = true;
			if (s)
				continue;
			seen.push_back(k);
			stack.push_back(nx);
			stack.push_back(ny);
			stack.push_back(nz);
		}
	}
}
} // namespace mcc

// ---------------------------------------------------------------------------------------------
// Config and the classic texture blocks

#include "mc_blocks.h"
#include "mc_items.h"

#include <stdio.h>

namespace mcc
{
std::vector<int> blockOfMiptex;
std::vector<int> itemOfMiptex;

bool Config::Parse(const char* text)
{
	const char* p = text;
	while (p && *p)
	{
		const char* nl = strchr(p, '\n');
		char line[256];
		size_t n = nl ? (size_t)(nl - p) : strlen(p);
		if (n >= sizeof(line))
			n = sizeof(line) - 1;
		memcpy(line, p, n);
		line[n] = 0;
		if (!strncmp(line, "bsp ", 4))
			sscanf(line + 4, "%127s", bsp);
		else if (!strncmp(line, "origin ", 7))
			sscanf(line + 7, "%f %f %f", &origin[0], &origin[1], &origin[2]);
		else if (!strncmp(line, "size ", 5))
			sscanf(line + 5, "%d %d %d", &size[0], &size[1], &size[2]);
		else if (!strncmp(line, "sky ", 4))
			sscanf(line + 4, "%31s", sky);
		p = nl ? nl + 1 : nullptr;
	}
	return bsp[0] && size[0] > 0 && size[1] > 0 && size[2] > 0;
}

void RegisterClassicBlocks(Classic& c)
{
	mcw::ClearDynamicBlocks();
	mci::ClearDynamicItems();
	blockOfMiptex.assign(c.map.miptex.size(), -1);
	itemOfMiptex.assign(c.map.miptex.size(), -1);
	for (size_t i = 0; i < c.map.miptex.size(); i++)
	{
		const std::string& tn = c.map.miptex[i].name;
		Material mat = Classic::MaterialOfTexture(tn.c_str());
		if (tn.empty() || mat == MAT_SKY || !strcasecmp(tn.c_str(), "aaatrigger") || !strcasecmp(tn.c_str(), "clip") ||
			!strcasecmp(tn.c_str(), "origin") || !strcasecmp(tn.c_str(), "null") || !strcasecmp(tn.c_str(), "black"))
			continue;
		char name[64], display[96], tex[16];
		int k = snprintf(name, sizeof(name), "d2_");
		for (size_t j = 0; j < tn.size() && k < (int)sizeof(name) - 1; j++)
			name[k++] = isalnum((unsigned char)tn[j]) ? (char)tolower((unsigned char)tn[j]) : '_';
		name[k] = 0;
		static const char* fam[] = {"Block", "Sand", "Sandstone", "Crate", "Door", "Window", "Stone", "Sky"};
		snprintf(display, sizeof(display), "Dust II %s (%s)", fam[mat], tn.c_str());
		snprintf(tex, sizeof(tex), "#bsp:%d", (int)i);
		mcw::BlockDef bd = {};
		bd.name = name;
		bd.shape = mcw::SHAPE_CUBE;
		bd.texTop = bd.texSide = bd.texBottom = tex;
		bd.drop = nullptr;
		switch (mat)
		{
		case MAT_SAND: bd.hardness = 0.5f; bd.tool = mcw::TOOL_SHOVEL; bd.sound = mcw::SOUND_SAND; break;
		case MAT_SANDSTONE: bd.hardness = 0.8f; bd.tool = mcw::TOOL_PICKAXE; bd.sound = mcw::SOUND_STONE; break;
		case MAT_WOOD: bd.hardness = 2.0f; bd.tool = mcw::TOOL_AXE; bd.sound = mcw::SOUND_WOOD; break;
		case MAT_DOOR: bd.hardness = 3.0f; bd.tool = mcw::TOOL_AXE; bd.sound = mcw::SOUND_WOOD; break;
		case MAT_GLASS: bd.hardness = 0.3f; bd.tool = mcw::TOOL_NONE; bd.sound = mcw::SOUND_GLASS; break;
		default: bd.hardness = 1.5f; bd.tool = mcw::TOOL_PICKAXE; bd.sound = mcw::SOUND_STONE; break;
		}
		int type = mcw::RegisterDynamicBlock(bd);
		if (type < 0)
			continue;
		blockOfMiptex[i] = type;
		mci::ItemDef id = {};
		id.name = name;
		id.display = display;
		id.type = mci::IT_BLOCK;
		id.texture = nullptr;
		id.maxStack = 64;
		id.attackDamage = 1.0f;
		id.attackSpeed = 4.0f;
		id.miningSpeed = 1.0f;
		id.blockName = name;
		itemOfMiptex[i] = mci::RegisterDynamicItem(id);
	}
	c.carvedType = (uint16_t)mcw::FindBlock("carved");
}

int CellBlockType(Classic& c, int x, int y, int z)
{
	const CellInfo& ci = c.Info(x, y, z);
	if (ci.ore)
	{
		int t = mcw::FindBlock(Classic::OreName(ci.ore));
		if (t >= 0)
			return t;
	}
	if (ci.miptex >= 0 && ci.miptex < (int)blockOfMiptex.size() && blockOfMiptex[ci.miptex] >= 0)
		return blockOfMiptex[ci.miptex];
	return mcw::FindBlock("sandstone");
}
} // namespace mcc
