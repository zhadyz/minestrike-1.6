// Classic mode: a real Counter-Strike map (de_dust2) is the world, and players can dig it.
//
// The engine runs an empty sky-box map; the mod loads the classic BSP itself (mcb::Map) and both DLLs
// collide with it exactly as the engine would (same hulls, same SV_RecursiveHullCheck), so Counter-
// Strike movement is untouched. The voxel grid (mcw::World) overlays it in 40-unit cells:
//   cell 0        - untouched: whatever the classic map has there
//   CARVED type   - dug out: the classic geometry inside the cell is gone, the cell is empty
//   anything else - a placed block, on top of whatever the classic map has there; CARVED_FLAG in the
//                   cell's spare state bit marks a block placed into a dug-out cell
// Traces take the fast engine-exact path unless what they hit lies in a modified cell; then they fall
// back to convex solid pieces (the map's solid space cut into the intact cells) and box-vs-brush sweeps.
#pragma once
#include "mc_bsp.h"
#include "mc_world.h"

#include <unordered_map>
#include <vector>

namespace mcc
{
// Collision result in world space (engine semantics: fraction 1 = no hit).
struct Result
{
	float fraction = 1.0f;
	float endpos[3] = {0, 0, 0};
	float normal[3] = {0, 0, 0};
	bool startsolid = false;
	bool allsolid = false;
	bool hit = false;
	bool fallback = false; // came from the carve-aware path (not the engine-exact hull trace)
};

// A convex piece of the classic map's solid space inside one cell: n.x <= d for every plane.
struct Piece
{
	std::vector<float> planes; // nx, ny, nz, d
	float mins[3], maxs[3];
	bool sky; // came from sky contents (blocks boxes, not points)
};

// Mining material families (what a dug cell drops and how long it takes).
enum Material : uint8_t
{
	MAT_NONE,
	MAT_SAND,
	MAT_SANDSTONE,
	MAT_WOOD, // crates
	MAT_DOOR,
	MAT_GLASS,
	MAT_STONE,
	MAT_SKY,
};

struct CellInfo
{
	bool computed = false;
	bool solid = false;      // has (non-sky) classic solid in it
	bool skyish = false;     // touches sky: not diggable
	int16_t miptex = -1;     // representative texture (the biggest face in or nearest to the cell)
	uint8_t material = MAT_NONE;
	uint8_t ore = 0;         // 0 none, else an ore kind (see OreName)
	bool interior = false;   // no classic faces nearby (buried)
};

static const uint16_t CARVED_FLAG = 0x8000; // state bit 5 of a cell

class Classic
{
public:
	mcb::Map map;
	const mcw::World* grid = nullptr;
	uint16_t carvedType = 0; // block type id of "carved" cells

	bool Load(const char* bspPath);
	bool Active() const { return map.loaded && grid; }

	// Collision (box sweep / point / position test), the classic map minus carved cells. Placed blocks
	// are NOT included here (callers merge mcw::TraceBox for those).
	void Trace(const float start[3], const float end[3], const float mins[3], const float maxs[3], Result& out);
	// Always the carve-aware (pieces) path; for testing it against Trace on intact geometry.
	void TraceSlow(const float start[3], const float end[3], const float mins[3], const float maxs[3], Result& out,
		bool handBack = false);
	int PointContents(const float p[3]);
	bool TestBox(const float origin[3], const float mins[3], const float maxs[3]);

	// Ray pick for mining: nearest classic surface along the ray; cell = the dug cell behind the surface.
	bool PickCell(const float start[3], const float dir[3], float maxDist, int cell[3], int* face, float* dist);

	// Cells
	bool IsCarved(mcw::Cell c) const { return c && (mcw::CellType(c) == carvedType || (c & CARVED_FLAG)); }
	bool Carved(int x, int y, int z) const { return grid && IsCarved(grid->Get(x, y, z)); }
	const CellInfo& Info(int x, int y, int z);
	const std::vector<Piece>& Pieces(int x, int y, int z);
	bool Diggable(int x, int y, int z);
	// Faces whose bounds touch the cell (world + drawn brush entities).
	const std::vector<int>* FacesInCell(int x, int y, int z) const;
	void CellBox(int x, int y, int z, float mins[3], float maxs[3]) const;
	// Door panels are broken as a whole: the connected door cells around (x,y,z).
	void DoorCells(int x, int y, int z, std::vector<int>& out3);
	static Material MaterialOfTexture(const char* name);
	// Texture name of the classic surface a point trace from start to end hits first (what the engine's
	// TraceTexture returns), or nullptr. Cut walls of dug cells report the cell's texture.
	const char* TextureAt(const float start[3], const float end[3]);
	static const char* OreName(int ore);

private:
	std::unordered_map<uint32_t, std::vector<Piece>> m_pieces;
	std::unordered_map<uint32_t, CellInfo> m_info;
	std::unordered_map<uint32_t, std::vector<int>> m_faceCells;
	int m_lowestFloorZ = 0;
	float m_cellMin[3] = {0, 0, 0}, m_cellMax[3] = {0, 0, 0}; // box of the cell being decomposed

	static uint32_t Key(int x, int y, int z) { return (uint32_t)((x + 512) & 1023) | ((uint32_t)((y + 512) & 1023) << 10) | ((uint32_t)((z + 512) & 1023) << 20); }
	void BuildFaceIndex();
	void Decompose(const mcb::ClipNode* nodes, int num, std::vector<float>& planes, const float cmid[3], std::vector<Piece>& out,
		bool pointHull);
	bool AnyModifiedInBox(const float mins[3], const float maxs[3]) const;
	// Does a box at o (hull-space origin, extents hm..hM) overlap classic solid that is still intact?
	bool BoxOverlapsIntact(const float o[3], const float hm[3], const float hM[3], bool point);
	void FallbackTrace(const float o0[3], const float o1[3], const float bmins[3], const float bmaxs[3], float tmin, bool point,
		Result& out, int hull = -1);
	// The engine-exact trace (world + solid brush entities) in hull space.
	void HullTraceAll(int hull, const float s[3], const float e[3], mcb::HullTrace& best);
	// Does the hull contact at (probe) need the carve-aware path? (it touches dug space, not intact solid)
	bool ContactIsCarved(const mcb::HullTrace& t, const float s[3], const float hm[3], const float hM[3], int hull);
};

// The face of a convex polytope given by planes (n.x <= d): returns vertices of plane i's face.
int PolytopeFace(const std::vector<float>& planes, int i, const float center[3], float out[][3], int maxOut);
// Sutherland-Hodgman: clip a polygon to n.x <= d (keeps the inside). Returns new count.
int ClipPolygon(const float in[][3], int n, const float plane[4], float out[][3], int maxOut);
} // namespace mcc

namespace mcc
{
// Classic map settings from maps/<map>.mcc (both DLLs parse the same file).
struct Config
{
	char bsp[128] = "";      // e.g. maps/de_dust2.bsp
	float origin[3] = {0, 0, 0};
	int size[3] = {0, 0, 0};
	char sky[32] = "";
	bool Parse(const char* text);
};

// Registers one dynamic block + block item per classic texture (both DLLs, same order).
// After this, blockOfMiptex[i] / itemOfMiptex[i] give the ids (-1 for skipped textures).
void RegisterClassicBlocks(Classic& c);
extern std::vector<int> blockOfMiptex;
extern std::vector<int> itemOfMiptex;
// Block type a classic cell turns into when mined (its texture's block, or an ore)
int CellBlockType(Classic& c, int x, int y, int z);
} // namespace mcc
