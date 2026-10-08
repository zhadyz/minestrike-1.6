// GoldSrc BSP v30 reader and the engine's own collision algorithms (hull point contents and
// SV_RecursiveHullCheck), so the mod can collide with a classic map that the engine itself never
// loaded. Shared by client.dll and mp.dll: both sides read the same file and get identical results,
// which keeps Counter-Strike's movement prediction (bunny hops, air strafes, ramps) exact.
//
// Plain C++17, no engine headers.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>

namespace mcb
{
static const int CONT_EMPTY = -1;
static const int CONT_SOLID = -2;
static const int CONT_WATER = -3;
static const int CONT_SKY = -6;

struct Plane
{
	float normal[3];
	float dist;
	int type; // 0..2 axial X/Y/Z, 3..5 non-axial
};

// Hull tree node in clipnode form. children[i] >= 0: node index; < 0: contents.
struct ClipNode
{
	int planenum;
	int children[2];
};

struct Model
{
	float mins[3], maxs[3], origin[3];
	int headnode[4]; // hull 0 (render nodes), hulls 1..3 (clipnodes)
	int visleafs, firstface, numfaces;
};

struct TexInfo
{
	float vecs[2][4];
	int miptex;
	int flags;
};

struct Face
{
	int planenum;
	int side;
	int firstedge, numedges;
	int texinfo;
	uint8_t styles[4];
	int lightofs; // byte offset into lighting, -1 = none
};

struct MipTex
{
	std::string name;
	int width = 0, height = 0;
	int lumpOffset = -1; // offset of the embedded miptex header in the texture lump, -1 = from a WAD
};

// Result of a hull trace, in the hull's own coordinate space.
extern int g_badTraces; // traces refused for non-finite input (each one is a bug upstream)

struct HullTrace
{
	float fraction = 1.0f;
	float endpos[3] = {0, 0, 0};
	float normal[3] = {0, 0, 0};
	float dist = 0.0f;
	bool allsolid = true;
	bool startsolid = false;
	bool inopen = false;
};

struct Map
{
	bool loaded = false;
	std::string path;
	std::vector<Plane> planes;
	std::vector<ClipNode> hull0;     // render nodes converted to clipnode form (Mod_MakeHull0)
	std::vector<ClipNode> clipnodes; // hulls 1..3
	std::vector<Model> models;
	std::vector<float> verts;        // xyz triples
	std::vector<int> edges;          // vertex index pairs
	std::vector<int> surfedges;
	std::vector<Face> faces;
	std::vector<TexInfo> texinfo;
	std::vector<MipTex> miptex;
	std::vector<uint8_t> textureLump;
	std::vector<uint8_t> lighting;
	std::string entities;
	std::vector<int> solidModels;    // brush entities that block movement (func_wall, func_breakable, ...)
	std::vector<int> drawModels;     // brush entities that are drawn (solid ones + func_illusionary)

	bool Load(const char* file);

	// Contents of a point for the given hull of a model (point in model space, hull space).
	int HullPointContents(int hull, int model, const float p[3]) const;
	// SV_RecursiveHullCheck over a model's hull; start/end already offset into hull space.
	void TraceHull(int hull, int model, const float start[3], const float end[3], HullTrace& tr) const;

	// The engine's hull choice for a box (SV_HullForBsp) and the standard hull extents.
	static int HullForBox(const float mins[3], const float maxs[3]);
	static void HullExtents(int hull, float mins[3], float maxs[3]);

	// Face geometry helpers.
	int FaceVertexCount(int f) const { return faces[f].numedges; }
	void FaceVertex(int f, int i, float out[3]) const;
	void FaceNormal(int f, float out[3]) const; // front-facing normal (towards empty space)
	const std::string& FaceTexture(int f) const;

private:
	bool RecursiveHullCheck(const ClipNode* nodes, int num, float p1f, float p2f, const float p1[3], const float p2[3],
		HullTrace& tr, int firstnode) const;
	int PointContentsTree(const ClipNode* nodes, int num, const float p[3]) const;
};

// Helpers for the entity lump.
struct EntityKV
{
	std::vector<std::pair<std::string, std::string>> kv;
	const char* Get(const char* key) const;
};
std::vector<EntityKV> ParseEntities(const std::string& text);
} // namespace mcb
