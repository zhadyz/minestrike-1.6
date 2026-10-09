#!/usr/bin/env python3
"""navgen - build a Counter-Strike 1.6 / Condition Zero bot navigation mesh (.nav) from an MCW1 voxel world.

Usage (from Z:\\dev\\CSminecraft):
    python tools\\navgen\\navgen.py mc_dust2
    python tools\\navgen\\navgen.py mc_dust2 --max-size 8 --no-precise

Reads   <maps>\\<map>.mcw  (voxel world), <map>.json (spawns/bombsites/landmarks), <map>.bsp (size only)
Writes  <maps>\\<map>.nav  (NAV_VERSION 5, the layout of ReGameDLL game_shared/bot/nav_file.cpp)
        <scratch>\\<map>_nav.png, <map>_nav_attrs.png, <map>_navstats.json, REPORT.md

Pipeline
 1. Classify every half-block (20 units) of every column: AIR / SOLID (standable top) / BLOCK (solid, not
    standable: panes) / STEP (upper half of a normal stair: you stand at 0.5 and step onto it).
    Doors and plants are AIR (CS bots treat doors as walk-through).
 2. Walkable cell = top of a SOLID half-block with >= 36 units (2 half-blocks) of air above.
    >= 72 units (4 half-blocks) -> standing, otherwise NAV_CROUCH.
 3. Transitions between horizontally adjacent cells (4 directions), checked per cell pair:
      the hull must pass at the higher floor: min(ceilings) - higher floor >= 72 (both standing) or >= 36.
      If only the crouched hull passes between two standing cells, the lower cell gets NAV_CROUCH ("lip").
      |dz| <= 58 (JumpCrouchHeight): two-way (walk <= 24, jump above).  58 < drop < 200 (DeathDrop): one-way down.
    A wall between two cells simply means there is no floor in the neighbour column at a usable height,
    so walls are respected per edge cell.
 4. Keep only cells reachable from the spawns that can also get back to a spawn (no trap pits).
 5. Greedy rectangle merge of same-height, same-crouch cells (largest rectangles first, max NxN blocks).
    Areas overlapping a bombsite are written first (bots keep only the first 16 zone areas).
 6. Area connections from the cell transitions; NAV_PRECISE on areas with a step up into a sizeable upper
    region (works around the bot feelers at StepHeight 18 hitting 20-unit slab faces; --no-precise);
    hiding spots at area corners (ComputeHidingSpots rules, cover tested with voxel ray casts);
    Place names from the map metadata landmarks.
 7. Write the .nav, re-read it with the independent reader in navcheck.py, validate (incl. a collision
    check of every cell and link with a port of mcw::ShapeBoxes), render previews, write REPORT.md.

Approach areas, encounter paths and sniper-spot flags are left empty: the loader does not need them and
they are only produced by the in-game analysis (`bot_nav_analyze` with a bot present recomputes all of
them using the engine traces, which see the voxels, and re-saves the file).
"""
from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
import time
from collections import defaultdict, deque

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PROJECT = os.path.abspath(os.path.join(HERE, "..", ".."))  # Z:\dev\CSminecraft
sys.path.insert(0, os.path.join(PROJECT, "tools", "voxelizer"))
sys.path.insert(0, HERE)

from mcw_io import read_mcw  # noqa: E402

DEFAULT_MAPS = os.path.join(PROJECT, "game", "Half-Life", "cstrike", "maps")
DEFAULT_SCRATCH = os.path.join(os.environ.get("MINESTRIKE_SCRATCH") or (r"Z:\dev\scratch\csminecraft" if os.path.isdir(r"Z:\dev\scratch\csminecraft")
                                                                        else os.path.join(PROJECT, "work")), "navgen")
BLOCKS_CPP = os.path.join(PROJECT, "code", "shared", "mc_blocks.cpp")

# ---- game_shared/bot/nav.h -------------------------------------------------------------------------
NAV_MAGIC_NUMBER = 0xFEEDFACE
NAV_VERSION = 5
STEP_HEIGHT = 18.0
JUMP_HEIGHT = 41.8
JUMP_CROUCH_HEIGHT = 58.0
DEATH_DROP = 200.0
HALF_HUMAN_HEIGHT = 36.0
HUMAN_HEIGHT = 72.0
NAV_CROUCH, NAV_JUMP, NAV_PRECISE, NAV_NO_JUMP = 0x01, 0x02, 0x04, 0x08
NORTH, EAST, SOUTH, WEST = 0, 1, 2, 3
DIR_NAMES = ["N", "E", "S", "W"]
DIR_DELTA = {NORTH: (0, -1), EAST: (1, 0), SOUTH: (0, 1), WEST: (-1, 0)}  # NORTH = -Y (AddDirectionVector)
HIDING_IN_COVER = 0x01

# Place names the loader can resolve without BotChatter.db (CNavAreaGrid g_pszDefaultPlaceNames).
# Only names from that list may be written: an unknown name is dropped by PlaceDirectory::Load, which
# shifts every later directory entry.
DEFAULT_PLACE_NAMES = [
    "BombsiteA", "BombsiteB", "BombsiteC", "Hostages", "HostageRescueZone", "VipRescueZone", "CTSpawn",
    "TSpawn", "Bridge", "Middle", "House", "Apartment", "Apartments", "Market", "Sewers", "Tunnel", "Ducts",
    "Village", "Roof", "Upstairs", "Downstairs", "Basement", "Crawlspace", "Kitchen", "Inside", "Outside",
    "Tower", "WineCellar", "Garage", "Courtyard", "Water", "FrontDoor", "BackDoor", "SideDoor", "BackWay",
    "FrontYard", "BackYard", "SideYard", "Lobby", "Vault", "Elevator", "DoubleDoors", "SecurityDoors",
    "LongHall", "SideHall", "FrontHall", "BackHall", "MainHall", "FarSide", "Windows", "Window", "Attic",
    "StorageRoom", "ProjectorRoom", "MeetingRoom", "ConferenceRoom", "ComputerRoom", "BigOffice",
    "LittleOffice", "Dumpster", "Airplane", "Underground", "Bunker", "Mines", "Front", "Back", "Rear", "Side",
    "Ramp", "Underpass", "Overpass", "Stairs", "Ladder", "Gate", "GateHouse", "LoadingDock", "GuardHouse",
    "Entrance", "VendingMachines", "Loft", "Balcony", "Alley", "BackAlley", "SideAlley", "FrontRoom",
    "BackRoom", "SideRoom", "Crates", "Truck", "Bedroom", "FamilyRoom", "Bathroom", "LivingRoom", "Den",
    "Office", "Atrium", "Entryway", "Foyer", "Stairwell", "Fence", "Deck", "Porch", "Patio", "Wall",
]
LANDMARK_PLACES = {
    "t_spawn": "TSpawn", "ct_spawn": "CTSpawn", "a_site": "BombsiteA", "b_site": "BombsiteB",
    "mid": "Middle", "mid_doors": "DoubleDoors", "long_doors": "Gate", "outside_long": "Outside",
    "long_a": "LongHall", "short_catwalk": "Stairs", "upper_tunnels": "Tunnel", "lower_tunnels": "Underpass",
    "b_doors": "SideDoor",
}

# ---- block shapes (code/shared/mc_world.h ShapeKind) -------------------------------------------------
AIR, SOLID, BLOCK, STEP = 0, 1, 2, 3
CLASS_NAMES = {AIR: "air", SOLID: "solid", BLOCK: "block", STEP: "step"}


def load_shapes(path=BLOCKS_CPP):
    """block name -> 'SHAPE_*' parsed from the registry table in mc_blocks.cpp (single source of truth)."""
    shapes = {}
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for m in re.finditer(r'\{\s*"([A-Za-z0-9_]+)"\s*,\s*(SHAPE_[A-Z]+)', f.read()):
                shapes[m.group(1)] = m.group(2)
    return shapes


def guess_shape(name):
    if name == "air":
        return "SHAPE_NONE"
    if name.endswith("_slab"):
        return "SHAPE_SLAB"
    if name.endswith("_stairs"):
        return "SHAPE_STAIRS"
    if name.endswith("_door"):
        return "SHAPE_DOOR"
    if name in ("iron_bars",) or name.endswith("_pane"):
        return "SHAPE_PANE"
    if name in ("dead_bush", "cobweb", "grass", "fern", "dandelion", "poppy"):
        return "SHAPE_CROSS"
    return "SHAPE_CUBE"


def build_class_lut(palette, shapes, warnings):
    """Raw uint16 cell -> (class of lower half, class of upper half)."""
    lo = np.zeros(65536, np.uint8)
    hi = np.zeros(65536, np.uint8)
    for p, name in enumerate(palette):
        shape = shapes.get(name)
        if shape is None:
            shape = guess_shape(name)
            if name != "air":
                warnings.append(f"block '{name}' not in mc_blocks.cpp registry; guessed {shape}")
        for s in range(64):
            v = p | (s << 10)
            if shape in ("SHAPE_NONE", "SHAPE_CROSS", "SHAPE_DOOR"):
                pair = (AIR, AIR)  # doors: walk-through columns (bots treat doors as passable)
            elif shape == "SHAPE_CUBE":
                pair = (SOLID, SOLID)
            elif shape == "SHAPE_SLAB":
                pair = (AIR, SOLID) if (s & 1) else (SOLID, AIR)
            elif shape == "SHAPE_STAIRS":
                # normal: full-footprint lower slab + back half on top -> stand at 0.5, step onto the back half.
                # upside down: full-footprint upper slab -> behaves as a cube for walking.
                pair = (SOLID, SOLID) if (s & 4) else (SOLID, STEP)
            elif shape == "SHAPE_PANE":
                pair = (BLOCK, BLOCK)  # thin post: impassable, not a floor
            else:
                pair = (SOLID, SOLID)
            lo[v], hi[v] = pair
    return lo, hi


# ---- world model ---------------------------------------------------------------------------------------
class VoxelModel:
    """Half-block column classification, walkable cells and their transitions."""

    def __init__(self, world, shapes, warnings):
        self.world = world
        self.sx, self.sy, self.sz = world["size"]
        self.origin = tuple(float(v) for v in world["origin"])
        self.bs = float(world["block_size"])
        self.half = self.bs / 2.0
        self.H = 2 * self.sz
        lut_lo, lut_hi = build_class_lut(world["palette"], shapes, warnings)
        cells = world["cells"]  # (sz, sy, sx)
        cls = np.empty((self.H, self.sy, self.sx), np.uint8)
        cls[0::2] = lut_lo[cells]
        cls[1::2] = lut_hi[cells]
        self.cls = cls
        # thresholds in half-blocks
        self.stand_need = int(math.ceil(HUMAN_HEIGHT / self.half - 1e-9))       # 4  (72 -> 80)
        self.crouch_need = int(math.ceil(HALF_HUMAN_HEIGHT / self.half - 1e-9))  # 2  (36 -> 40)
        self.max_up = int(math.floor(JUMP_CROUCH_HEIGHT / self.half + 1e-9))     # 2  (40 <= 58 < 60)
        self.max_drop = int(math.ceil(DEATH_DROP / self.half - 1e-9)) - 1        # 9  (180 < 200)
        self.walk_up = int(math.floor(24.0 / self.half + 1e-9))                  # 1  (sv_stepsize 24)

    # half-block boundary index f -> world z of that floor
    def fz(self, f):
        return self.origin[2] + f * self.half

    def find_cells(self):
        H, cls = self.H, self.cls
        solid = cls == SOLID
        blocker = (cls == SOLID) | (cls == BLOCK)
        open_at = np.ones((H + 1, self.sy, self.sx), bool)  # can stand with feet at boundary f?
        open_at[:H] = (cls == AIR) | (cls == STEP)
        floor = np.zeros((H + 1, self.sy, self.sx), bool)
        floor[1:] = solid & open_at[1:]
        BIG = 1 << 20
        nb = np.full((H + 1, self.sy, self.sx), BIG, np.int32)  # index of first blocker half-block >= h
        for h in range(H - 1, -1, -1):
            nb[h] = np.where(blocker[h], h, nb[h + 1])
        f, y, x = np.nonzero(floor)
        ceil = nb[f, y, x]
        step = np.zeros(len(f), bool)
        m = f < H
        step[m] = cls[f[m], y[m], x[m]] == STEP
        clear = np.minimum(ceil - f - step.astype(np.int32), 10 ** 4)
        ok = clear >= self.crouch_need
        self.n_floor_candidates = int(len(f))
        self.n_too_low = int((~ok).sum())
        self.cf = f[ok].astype(np.int32)
        self.cy = y[ok].astype(np.int32)
        self.cx = x[ok].astype(np.int32)
        self.cceil = ceil[ok].astype(np.int32)
        self.cclear = clear[ok].astype(np.int32)
        self.cstand = self.cclear >= self.stand_need
        self.ncell = len(self.cf)
        self.cell_id = np.full((H + 1, self.sy, self.sx), -1, np.int32)
        self.cell_id[self.cf, self.cy, self.cx] = np.arange(self.ncell, dtype=np.int32)

    def find_transitions(self):
        """Directed cell->cell transitions. Arrays: src, dst, dir, k (=dst floor - src floor), lip (crouch-only)."""
        src, dst, dirs, ks, lips = [], [], [], [], []
        H = self.H
        for d, (dx, dy) in DIR_DELTA.items():
            nx = self.cx + dx
            ny = self.cy + dy
            inb = (nx >= 0) & (nx < self.sx) & (ny >= 0) & (ny < self.sy)
            idx = np.nonzero(inb)[0]
            for k in range(-self.max_drop, self.max_up + 1):
                f2 = self.cf[idx] + k
                okf = (f2 >= 1) & (f2 <= H)
                ii = idx[okf]
                j = self.cell_id[f2[okf], ny[ii], nx[ii]]
                has = j >= 0
                ii, j = ii[has], j[has]
                if len(ii) == 0:
                    continue
                f_hi = np.maximum(self.cf[ii], self.cf[j])
                gap = np.minimum(self.cceil[ii], self.cceil[j]) - f_hi
                both_stand = self.cstand[ii] & self.cstand[j]
                need = np.where(both_stand, self.stand_need, self.crouch_need)
                full = gap >= need
                crouch_ok = gap >= self.crouch_need
                conn = crouch_ok
                lip = conn & ~full
                src.append(ii[conn]); dst.append(j[conn]); ks.append(np.full(conn.sum(), k, np.int32))
                dirs.append(np.full(conn.sum(), d, np.int8)); lips.append(lip[conn])
        self.t_src = np.concatenate(src).astype(np.int32)
        self.t_dst = np.concatenate(dst).astype(np.int32)
        self.t_dir = np.concatenate(dirs)
        self.t_k = np.concatenate(ks)
        self.t_lip = np.concatenate(lips)

    def csr(self, keep_mask=None, reverse=False):
        a, b = (self.t_dst, self.t_src) if reverse else (self.t_src, self.t_dst)
        if keep_mask is not None:
            a, b = a[keep_mask], b[keep_mask]
        order = np.argsort(a, kind="stable")
        a, b = a[order], b[order]
        indptr = np.zeros(self.ncell + 1, np.int64)
        np.add.at(indptr, a + 1, 1)
        indptr = np.cumsum(indptr)
        return indptr, b

    def bfs(self, seeds, reverse=False):
        indptr, nbr = self.csr(reverse=reverse)
        seen = np.zeros(self.ncell, bool)
        q = deque()
        for s in seeds:
            if not seen[s]:
                seen[s] = True
                q.append(s)
        while q:
            c = q.popleft()
            for n in nbr[indptr[c]:indptr[c + 1]]:
                if not seen[n]:
                    seen[n] = True
                    q.append(n)
        return seen

    def cell_at(self, x, y, z_feet, tol_half=1.6, search=1):
        """Walkable cell in the column under (x, y) whose floor is closest to z_feet (within tol)."""
        ox, oy, oz = self.origin
        bx = int(math.floor((x - ox) / self.bs))
        by = int(math.floor((y - oy) / self.bs))
        ft = (z_feet - oz) / self.half
        best, bestd = -1, 1e9
        for r in range(0, search + 1):
            for yy in range(by - r, by + r + 1):
                for xx in range(bx - r, bx + r + 1):
                    if not (0 <= xx < self.sx and 0 <= yy < self.sy):
                        continue
                    if max(abs(xx - bx), abs(yy - by)) != r:
                        continue
                    ids = self.cell_id[:, yy, xx]
                    for f in np.nonzero(ids >= 0)[0]:
                        dd = abs(f - ft) + 0.25 * r
                        if abs(f - ft) <= tol_half and dd < bestd:
                            best, bestd = int(ids[f]), dd
            if best >= 0:
                return best
        return best

    # occupancy for line-of-sight style traces (anything with collision)
    def occupancy(self):
        return self.cls != AIR

    def trace_hits(self, starts, ends, step=4.0):
        """Vectorized point traces through the voxel grid: True where the segment hits collision."""
        occ = self.occupancy()
        starts = np.asarray(starts, np.float64)
        ends = np.asarray(ends, np.float64)
        d = ends - starts
        L = np.linalg.norm(d, axis=1)
        n = int(math.ceil(max(L.max(), 1.0) / step)) + 1
        t = np.linspace(0.0, 1.0, n)[None, :, None]
        pts = starts[:, None, :] + d[:, None, :] * t  # (N, n, 3)
        ox, oy, oz = self.origin
        ix = np.floor((pts[..., 0] - ox) / self.bs).astype(np.int64)
        iy = np.floor((pts[..., 1] - oy) / self.bs).astype(np.int64)
        iz = np.floor((pts[..., 2] - oz) / self.half).astype(np.int64)
        inb = (ix >= 0) & (ix < self.sx) & (iy >= 0) & (iy < self.sy) & (iz >= 0) & (iz < self.H)
        hit = np.zeros(pts.shape[:2], bool)
        hit[inb] = occ[iz[inb], iy[inb], ix[inb]]
        hit[:, 0] = False
        return hit.any(axis=1)


# ---- rectangle merge -----------------------------------------------------------------------------------
def merge_rectangles(mask, max_w, max_h):
    """Cover a 2D boolean mask[y, x] with axis-aligned rectangles, largest (then squarest) first."""
    sizes = sorted(((w, h) for w in range(1, max_w + 1) for h in range(1, max_h + 1)),
                   key=lambda s: (-s[0] * s[1], abs(s[0] - s[1]), -s[0]))
    avail = mask.copy()
    rects = []
    ny, nx = avail.shape
    remaining = int(avail.sum())
    for (w, h) in sizes:
        if remaining == 0:
            break
        if w > nx or h > ny:
            continue
        sat = np.zeros((ny + 1, nx + 1), np.int32)
        sat[1:, 1:] = np.cumsum(np.cumsum(avail, axis=0), axis=1)
        full = sat[h:, w:] - sat[:-h, w:] - sat[h:, :-w] + sat[:-h, :-w]
        ys, xs = np.nonzero(full == w * h)
        for y, x in zip(ys.tolist(), xs.tolist()):
            if avail[y:y + h, x:x + w].all():
                avail[y:y + h, x:x + w] = False
                rects.append((x, y, w, h))
                remaining -= w * h
    assert remaining == 0
    return rects


# ---- areas ----------------------------------------------------------------------------------------------
class Area:
    __slots__ = ("id", "x0", "y0", "w", "h", "f", "attr", "z", "lo", "hi", "conn", "spots", "place", "cells")

    def __init__(self, aid, x0, y0, w, h, f, attr, model):
        self.id = aid
        self.x0, self.y0, self.w, self.h, self.f, self.attr = x0, y0, w, h, f, attr
        ox, oy, _ = model.origin
        self.z = model.fz(f)
        self.lo = (ox + x0 * model.bs, oy + y0 * model.bs)
        self.hi = (ox + (x0 + w) * model.bs, oy + (y0 + h) * model.bs)
        self.conn = [[] for _ in range(4)]
        self.spots = []
        self.place = 0
        self.cells = None

    @property
    def center(self):
        return ((self.lo[0] + self.hi[0]) / 2.0, (self.lo[1] + self.hi[1]) / 2.0, self.z)


def build_areas(model, keep, attr, max_size):
    layers = defaultdict(list)
    ids = np.nonzero(keep)[0]
    for c in ids.tolist():
        layers[(int(model.cf[c]), int(attr[c]))].append(c)
    areas = []
    area_of = np.full(model.ncell, -1, np.int32)
    for (f, a) in sorted(layers):
        cl = np.array(layers[(f, a)], np.int64)
        xs, ys = model.cx[cl], model.cy[cl]
        x0, y0 = int(xs.min()), int(ys.min())
        mask = np.zeros((int(ys.max()) - y0 + 1, int(xs.max()) - x0 + 1), bool)
        mask[ys - y0, xs - x0] = True
        for (rx, ry, w, h) in merge_rectangles(mask, max_size, max_size):
            ar = Area(len(areas) + 1, x0 + rx, y0 + ry, w, h, f, a, model)
            cid = model.cell_id[f, ar.y0:ar.y0 + h, ar.x0:ar.x0 + w].ravel()
            assert (cid >= 0).all()
            ar.cells = cid
            area_of[cid] = len(areas)
            areas.append(ar)
    return areas, area_of


def order_areas(areas, meta, model):
    """File order matters to the bots: CollectOverlappingAreas keeps only the first 16 areas overlapping a
    bombsite (MAX_ZONE_NAV_AREAS) and uses zone.m_area[0] for travel distances. Put the areas with the largest
    overlap with each bombsite first, then the rest in generation order; ids follow file order."""
    def overlap(A, site, fudge=50.0):
        mins, maxs = site["mins"], site["maxs"]
        if not (mins[2] - fudge <= A.z <= maxs[2] + fudge):
            return 0.0
        ox_ = min(A.hi[0], maxs[0]) - max(A.lo[0], mins[0])
        oy_ = min(A.hi[1], maxs[1]) - max(A.lo[1], mins[1])
        return ox_ * oy_ if ox_ > 0 and oy_ > 0 else 0.0

    first = []
    for site in meta.get("bombsites", []):
        ranked = sorted((A for A in areas if overlap(A, site) > 0), key=lambda A: -overlap(A, site))
        first += [A for A in ranked if A not in first]
    rest = [A for A in areas if A not in first]
    ordered = first + rest
    area_of = np.full(model.ncell, -1, np.int32)
    for i, A in enumerate(ordered):
        A.id = i + 1
        area_of[A.cells] = i
    return ordered, area_of


def connect_areas(model, areas, area_of, live):
    """Area links from cell transitions. A link A->B in direction d needs the source cell on A's d edge and the
    target cell on B's opposite edge (otherwise the bot's portal computation would point at the wrong place)."""
    s, t, d = model.t_src[live], model.t_dst[live], model.t_dir[live]
    a, b = area_of[s], area_of[t]
    cross = a != b
    s, t, d, a, b = s[cross], t[cross], d[cross], a[cross], b[cross]
    links = defaultdict(int)
    dropped = 0
    for si, ti, di, ai, bi in zip(s.tolist(), t.tolist(), d.tolist(), a.tolist(), b.tolist()):
        A, B = areas[ai], areas[bi]
        x, y = int(model.cx[si]), int(model.cy[si])
        tx, ty = int(model.cx[ti]), int(model.cy[ti])
        on_a = ((di == NORTH and y == A.y0) or (di == SOUTH and y == A.y0 + A.h - 1)
                or (di == WEST and x == A.x0) or (di == EAST and x == A.x0 + A.w - 1))
        on_b = ((di == NORTH and ty == B.y0 + B.h - 1) or (di == SOUTH and ty == B.y0)
                or (di == WEST and tx == B.x0 + B.w - 1) or (di == EAST and tx == B.x0))
        if not (on_a and on_b):
            dropped += 1
            continue
        links[(ai, di, bi)] += 1
    partial = []
    for (ai, di, bi), n in links.items():
        A, B = areas[ai], areas[bi]
        areas[ai].conn[di].append(B.id)
        if di in (NORTH, SOUTH):
            overlap = min(A.x0 + A.w, B.x0 + B.w) - max(A.x0, B.x0)
        else:
            overlap = min(A.y0 + A.h, B.y0 + B.h) - max(A.y0, B.y0)
        if n < overlap:
            partial.append((A.id, DIR_NAMES[di], B.id, n, overlap))
    for ar in areas:
        for di in range(4):
            ar.conn[di].sort()
    return links, dropped, partial


def mark_precise(areas, min_region_cells=12):
    """NAV_PRECISE (bot feelers off) on areas with a step up (two-way link to a higher area) that leads into a
    sizeable upper region. The feelers sit StepHeight+0.1 = 18.1 above the feet and hit 20-unit slab faces,
    which makes bots veer when they approach a step they must climb. Steps onto small ledges / daises (the
    region reachable from the step without going below its height has < min_region_cells cells) are ignored:
    there the feeler just keeps the bot off the ledge like off a wall, and bots do not route through them."""
    by_id = {a.id: a for a in areas}
    region_cache = {}

    def region_cells(b):
        if b in region_cache:
            return region_cache[b]
        z0 = by_id[b].z
        seen, stack, cells = {b}, [b], 0
        while stack and cells < min_region_cells:
            v = stack.pop()
            cells += by_id[v].w * by_id[v].h
            for d in range(4):
                for w_ in by_id[v].conn[d]:
                    if w_ not in seen and by_id[w_].z >= z0:
                        seen.add(w_)
                        stack.append(w_)
        region_cache[b] = cells
        return cells

    for a in areas:
        for d in range(4):
            if any(by_id[b].z > a.z and a.id in sum(by_id[b].conn, []) and region_cells(b) >= min_region_cells
                   for b in a.conn[d]):
                a.attr |= NAV_PRECISE
                break


def compute_hiding_spots(model, areas):
    """CNavArea::ComputeHidingSpots corner rules + IsHidingSpotInCover via voxel traces."""
    by_id = {a.id: a for a in areas}
    corner_size = 20.0
    offset = 12.5
    cand = []  # (area, pos)
    for A in areas:
        if A.attr & NAV_JUMP:
            continue
        cc = [0, 0, 0, 0]  # NW, NE, SE, SW
        for d in range(4):
            lo_e, hi_e = 999999.9, -999999.9
            horiz = d in (NORTH, SOUTH)
            for bid in A.conn[d]:
                B = by_id[bid]
                opp = (d + 2) % 4
                if A.id not in B.conn[opp]:
                    continue  # one-way (drop) link: a discontinuity that may mean cover
                if B.attr & NAV_JUMP:
                    continue
                if horiz:
                    lo_e, hi_e = min(lo_e, B.lo[0]), max(hi_e, B.hi[0])
                else:
                    lo_e, hi_e = min(lo_e, B.lo[1]), max(hi_e, B.hi[1])
            if d == NORTH:
                if lo_e - A.lo[0] >= corner_size: cc[0] += 1
                if A.hi[0] - hi_e >= corner_size: cc[1] += 1
            elif d == SOUTH:
                if lo_e - A.lo[0] >= corner_size: cc[3] += 1
                if A.hi[0] - hi_e >= corner_size: cc[2] += 1
            elif d == EAST:
                if lo_e - A.lo[1] >= corner_size: cc[1] += 1
                if A.hi[1] - hi_e >= corner_size: cc[2] += 1
            else:
                if lo_e - A.lo[1] >= corner_size: cc[0] += 1
                if A.hi[1] - hi_e >= corner_size: cc[3] += 1
        z = A.z
        corners = [((A.lo[0] + offset, A.lo[1] + offset, z), cc[0] == 2),
                   ((A.hi[0] - offset, A.lo[1] + offset, z), cc[1] == 2),
                   ((A.lo[0] + offset, A.hi[1] - offset, z), cc[3] == 2),
                   ((A.hi[0] - offset, A.hi[1] - offset, z), cc[2] == 2)]
        placed = []
        for i, (pos, ok) in enumerate(corners):
            if not ok:
                continue
            if i > 0 and any(math.dist(pos, p) < 30.0 for p in placed):
                continue  # IsHidingSpotCollision (not applied to the first corner, as in the game)
            placed.append(pos)
            cand.append((A, pos))
    if not cand:
        return 0, 0
    pos = np.array([p for _, p in cand], np.float64)
    eye = pos + np.array([0, 0, HALF_HUMAN_HEIGHT])
    up = model.trace_hits(eye, eye + np.array([0, 0, 20.0]))
    ang = np.arange(16) * (math.pi / 8.0)
    starts = np.repeat(eye, 16, axis=0)
    offs = np.stack([100.0 * np.cos(ang), 100.0 * np.sin(ang), np.full(16, HALF_HUMAN_HEIGHT)], axis=1)
    ends = starts + np.tile(offs, (len(eye), 1))
    hits = model.trace_hits(starts, ends).reshape(len(eye), 16).sum(axis=1)
    cover = up | (hits >= 8)
    sid = 0
    for (A, p), cv in zip(cand, cover.tolist()):
        sid += 1
        A.spots.append((sid, p, HIDING_IN_COVER if cv else 0))
    return sid, int(cover.sum())


def area_at(areas, x, y, z_origin, beneath=120.0):
    """CNavAreaGrid::GetNavArea equivalent for an origin position."""
    best, best_z = None, -1e18
    tz = z_origin + 5.0
    for A in areas:
        if A.lo[0] <= x <= A.hi[0] and A.lo[1] <= y <= A.hi[1]:
            if A.z > tz or A.z < z_origin - beneath:
                continue
            if A.z > best_z:
                best, best_z = A, A.z
    return best


def assign_places(areas, meta):
    """Geodesic nearest-landmark Place names, overridden inside bombsites / buyzones."""
    import heapq
    by_id = {a.id: a for a in areas}
    adj = defaultdict(set)
    for A in areas:
        for d in range(4):
            for b in A.conn[d]:
                adj[A.id].add(b)
                adj[b].add(A.id)
    marks = dict(meta.get("landmarks") or {})
    for k, v in (meta.get("surfaceSamples") or {}).items():
        marks.setdefault(k, v)
    dist, label = {}, {}
    heap = []
    for name, p in marks.items():
        place = LANDMARK_PLACES.get(name)
        if not place:
            continue
        A = area_at(areas, p[0], p[1], p[2])
        if A is None:
            continue
        dist[A.id] = 0.0
        label[A.id] = place
        heapq.heappush(heap, (0.0, A.id, place))
    while heap:
        dd, a, place = heapq.heappop(heap)
        if dd > dist.get(a, 1e18):
            continue
        ca = by_id[a].center
        for b in adj[a]:
            nd = dd + math.dist(ca, by_id[b].center)
            if nd < dist.get(b, 1e18):
                dist[b] = nd
                label[b] = place
                heapq.heappush(heap, (nd, b, place))
    for A in areas:
        A.place = label.get(A.id, "")

    def strictly_inside(A, mins, maxs, fudge=50.0):
        return (A.hi[0] > mins[0] and A.lo[0] < maxs[0] and A.hi[1] > mins[1] and A.lo[1] < maxs[1]
                and mins[2] - fudge <= A.z <= maxs[2] + fudge)

    for bz in meta.get("buyzones", []):
        name = "CTSpawn" if str(bz.get("team", "")).lower() == "ct" else "TSpawn"
        for A in areas:
            if strictly_inside(A, bz["mins"], bz["maxs"]):
                A.place = name
    for site in meta.get("bombsites", []):
        name = "Bombsite" + str(site.get("name", "A")).upper()
        if name not in DEFAULT_PLACE_NAMES:
            continue
        for A in areas:
            if strictly_inside(A, site["mins"], site["maxs"]):
                A.place = name
    used = []
    for A in areas:
        if A.place and A.place not in used:
            assert A.place in DEFAULT_PLACE_NAMES, A.place
            used.append(A.place)
    return used


# ---- .nav writer (CNavArea::Save / SaveNavigationMap layout, little endian, 32-bit size_t) ---------------
def write_nav(path, bsp_size, places, areas):
    out = bytearray()
    out += struct.pack("<III", NAV_MAGIC_NUMBER, NAV_VERSION, bsp_size)
    out += struct.pack("<H", len(places))  # PlaceDirectory::EntryType = unsigned short
    for name in places:
        b = name.encode("ascii") + b"\0"
        out += struct.pack("<H", len(b)) + b
    out += struct.pack("<I", len(areas))
    place_entry = {name: i + 1 for i, name in enumerate(places)}
    for A in areas:
        out += struct.pack("<IB", A.id, A.attr)
        out += struct.pack("<6f", A.lo[0], A.lo[1], A.z, A.hi[0], A.hi[1], A.z)  # m_extent lo / hi
        out += struct.pack("<2f", A.z, A.z)  # m_neZ, m_swZ (flat)
        for d in range(4):  # NORTH, EAST, SOUTH, WEST
            ids = A.conn[d]
            out += struct.pack("<I", len(ids))
            if ids:
                out += struct.pack(f"<{len(ids)}I", *ids)
        spots = A.spots[:255]
        out += struct.pack("<B", len(spots))
        for sid, p, flags in spots:
            out += struct.pack("<I3fB", sid, p[0], p[1], p[2], flags)
        out += struct.pack("<B", 0)  # m_approachCount
        out += struct.pack("<I", 0)  # encounter paths
        out += struct.pack("<H", place_entry.get(A.place, 0))
    tmp = path + ".navgen.tmp"
    with open(tmp, "wb") as f:
        f.write(out)
    os.replace(tmp, path)
    return len(out)


# ---- main -------------------------------------------------------------------------------------------------
def spawn_list(meta):
    sp = meta.get("spawns", {})
    return [("ct", s) for s in sp.get("ct", [])] + [("t", s) for s in sp.get("t", [])]


def generate(map_name, maps_dir, out_path, scratch, max_size=8, precise=True, verbose=True):
    t0 = time.time()
    log = []

    def say(msg):
        log.append(msg)
        if verbose:
            print(msg, flush=True)

    warnings = []
    mcw_path = os.path.join(maps_dir, map_name + ".mcw")
    meta_path = os.path.join(maps_dir, map_name + ".json")
    bsp_path = os.path.join(maps_dir, map_name + ".bsp")
    # the voxelizer may be rewriting the world/metadata while we start: retry a few times on a torn read
    for attempt in range(6):
        try:
            world = read_mcw(mcw_path)
            with open(meta_path, encoding="utf-8") as f:
                meta = json.load(f)
            break
        except (ValueError, struct.error, OSError) as e:
            if attempt == 5:
                raise
            say(f"read failed ({e}); retrying in 2 s")
            time.sleep(2.0)
    bsp_size = os.path.getsize(bsp_path) if os.path.exists(bsp_path) else 0
    if not bsp_size:
        warnings.append(f"{bsp_path} missing: bsp size 0 in header (loader will warn 'different version')")
    for k, v in (("origin", world["origin"]), ("size", world["size"])):
        mv = meta.get(k)
        if mv is not None and [float(a) for a in mv] != [float(a) for a in v]:
            warnings.append(f"metadata {k} {mv} != world {list(v)} (world file wins)")
    say(f"world {world['size']} origin {world['origin']} block {world['block_size']} palette {len(world['palette'])}")
    shapes = load_shapes()
    model = VoxelModel(world, shapes, warnings)
    model.find_cells()
    model.find_transitions()
    say(f"floor candidates {model.n_floor_candidates}, too low (<36u) {model.n_too_low}, walkable cells {model.ncell}"
        f" (standing {int(model.cstand.sum())}, crouch {int((~model.cstand).sum())}); transitions {len(model.t_src)}")

    # seeds: spawn points (origin z = feet + 37)
    seeds, seed_info = [], []
    for team, s in spawn_list(meta):
        c = model.cell_at(s[0], s[1], s[2] - 37.0)
        seed_info.append((team, s, c))
        if c >= 0:
            seeds.append(c)
        else:
            warnings.append(f"{team} spawn {s} has no walkable cell under it")
    if not seeds:
        raise SystemExit("no spawn resolves to a walkable cell; cannot seed reachability")
    fwd = model.bfs(seeds)
    back = model.bfs(seeds, reverse=True)
    keep = fwd & back
    say(f"reachable from spawns {int(fwd.sum())}, can return to a spawn {int(back.sum())}, kept {int(keep.sum())}"
        f" (trap cells dropped {int((fwd & ~back).sum())})")

    live = keep[model.t_src] & keep[model.t_dst]
    attr = np.zeros(model.ncell, np.uint8)
    attr[~model.cstand] |= NAV_CROUCH
    lip_cells = np.zeros(model.ncell, bool)
    lm = live & model.t_lip
    lower = np.where(model.t_k[lm] > 0, model.t_src[lm], model.t_dst[lm])
    lip_cells[lower] = True
    lip_only = lip_cells & model.cstand & keep
    attr[lip_cells] |= NAV_CROUCH
    precise_cells = np.zeros(model.ncell, bool)
    if precise:
        up = live & (model.t_k >= 1) & (model.t_k <= model.max_up)
        precise_cells[model.t_src[up]] = True
    say(f"crouch cells {int(((attr & NAV_CROUCH) > 0)[keep].sum())} (lip-only {int(lip_only.sum())}),"
        f" cells in front of a step up {int(precise_cells[keep].sum())}")

    # merge on (height, crouch) only; NAV_PRECISE is applied per area afterwards (marking cells would
    # fragment the tiling into many 1x1 areas along diagonal steps)
    areas, area_of = build_areas(model, keep, attr, max_size)
    areas, area_of = order_areas(areas, meta, model)
    links, dropped, partial = connect_areas(model, areas, area_of, live)
    if precise:
        mark_precise(areas)
    nlinks = sum(len(a.conn[d]) for a in areas for d in range(4))
    say(f"areas {len(areas)}, directed links {nlinks}, cross-level transitions not representable {dropped},"
        f" partial portals {len(partial)}")
    nspots, ncover = compute_hiding_spots(model, areas)
    places = assign_places(areas, meta)
    say(f"hiding spots {nspots} ({ncover} in cover); places {places}")

    nbytes = write_nav(out_path, bsp_size, places, areas)
    say(f"wrote {out_path} ({nbytes} bytes, version {NAV_VERSION}, bsp size {bsp_size})")

    gen = dict(
        map=map_name, mcw=mcw_path, mcw_mtime=os.path.getmtime(mcw_path), bsp_size=bsp_size,
        world_size=list(world["size"]), origin=list(world["origin"]), max_size=max_size, precise=precise,
        floor_candidates=model.n_floor_candidates, too_low=model.n_too_low, cells=model.ncell,
        cells_standing=int(model.cstand.sum()), transitions=int(len(model.t_src)),
        reachable=int(fwd.sum()), returnable=int(back.sum()), kept=int(keep.sum()),
        trap_cells=int((fwd & ~back).sum()), unreachable_cells=int((~fwd).sum()),
        crouch_cells=int(((attr & NAV_CROUCH) > 0)[keep].sum()), lip_crouch_cells=int(lip_only.sum()),
        precise_cells=int(precise_cells[keep].sum()), precise_areas=sum(1 for A in areas if A.attr & NAV_PRECISE),
        areas=len(areas), links=nlinks,
        cross_level_dropped=dropped, partial_portals=len(partial), partial_examples=partial[:20],
        hiding_spots=nspots, hiding_in_cover=ncover, places=places,
        seeds=[(t, s, int(c)) for t, s, c in seed_info], warnings=warnings,
        transitions_by_k={int(k): int(n) for k, n in zip(*np.unique(model.t_k[live], return_counts=True))},
        area_cells_histogram={int(k): int(n) for k, n in
                              zip(*np.unique([A.w * A.h for A in areas], return_counts=True))},
        place_counts={p: sum(1 for A in areas if A.place == p) for p in places},
        seconds=round(time.time() - t0, 2),
    )
    # diagnostic: how much more would be reachable if 60-unit (1.5 block) climbs were linked? Humans can
    # crouch-jump ~63 units, the bot code stops at JumpCrouchHeight 58, so these stay unlinked.
    saved = model.max_up
    model.max_up = saved + 1
    model.find_transitions()
    gen["cells_gained_with_60_climbs"] = int((model.bfs(seeds) & ~fwd).sum())
    model.max_up = saved
    model.find_transitions()
    for w in warnings:
        say("WARNING: " + w)
    return gen, model, areas


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("map", nargs="?", default="mc_dust2", help="map name (default mc_dust2)")
    ap.add_argument("--maps-dir", default=DEFAULT_MAPS)
    ap.add_argument("--out", default=None, help="output .nav (default <maps-dir>/<map>.nav)")
    ap.add_argument("--scratch", default=DEFAULT_SCRATCH, help="previews / report directory")
    ap.add_argument("--max-size", type=int, default=8, help="max area size in blocks (default 8)")
    ap.add_argument("--no-precise", action="store_true",
                    help="do not mark the cells in front of a step-up NAV_PRECISE (see REPORT.md)")
    ap.add_argument("--no-check", action="store_true", help="skip the read-back validation / previews / report")
    args = ap.parse_args(argv)
    out = args.out or os.path.join(args.maps_dir, args.map + ".nav")
    os.makedirs(args.scratch, exist_ok=True)
    gen, model, areas = generate(args.map, args.maps_dir, out, args.scratch, args.max_size, not args.no_precise)
    with open(os.path.join(args.scratch, args.map + "_navgen.json"), "w", encoding="utf-8") as f:
        json.dump(gen, f, indent=1)
    if args.no_check:
        return 0
    import navcheck
    ok = navcheck.run(out, args.maps_dir, args.map, args.scratch, gen=gen, model=model)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
