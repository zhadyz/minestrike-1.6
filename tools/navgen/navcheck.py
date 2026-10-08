#!/usr/bin/env python3
"""navcheck - read a CS 1.6 / CZ .nav back exactly as ReGameDLL's LoadNavigationMap / CNavArea::Load does,
validate it (ids, links, geometry against the voxel world, connectivity of spawns and bombsites), render
top-down previews and write REPORT.md.

Usage:
    python tools\\navgen\\navcheck.py mc_dust2            (checks <maps>\\mc_dust2.nav)
    python tools\\navgen\\navcheck.py mc_dust2 --nav other.nav
"""
from __future__ import annotations

import argparse
import heapq
import json
import math
import os
import random
import struct
import sys
import time
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import navgen  # noqa: E402  (constants, voxel model)
from navgen import (DEATH_DROP, DEFAULT_PLACE_NAMES, JUMP_CROUCH_HEIGHT, NAV_CROUCH, NAV_JUMP,  # noqa: E402
                    NAV_MAGIC_NUMBER, NAV_PRECISE, NAV_VERSION, NORTH, EAST, SOUTH, WEST)


# ---- reader: mirrors nav_file.cpp LoadNavigationMap / PlaceDirectory::Load / CNavArea::Load ----------------
class Reader:
    def __init__(self, buf):
        self.buf, self.off = buf, 0

    def read(self, fmt):
        size = struct.calcsize(fmt)
        if self.off + size > len(self.buf):
            raise EOFError(f"read past end at offset {self.off} ({fmt})")
        v = struct.unpack_from(fmt, self.buf, self.off)
        self.off += size
        return v

    def u8(self):
        return self.read("<B")[0]

    def u16(self):
        return self.read("<H")[0]

    def u32(self):
        return self.read("<I")[0]

    def f32(self, n=1):
        v = self.read(f"<{n}f")
        return v if n > 1 else v[0]


class NavArea:
    def __init__(self):
        self.id = 0
        self.attr = 0
        self.lo = self.hi = None
        self.neZ = self.swZ = 0.0
        self.conn = [[] for _ in range(4)]
        self.spots = []
        self.approach = []
        self.encounters = []
        self.place_entry = 0

    @property
    def center(self):  # CNavArea::Load: center z = (lo.z + hi.z) / 2
        return ((self.lo[0] + self.hi[0]) / 2, (self.lo[1] + self.hi[1]) / 2, (self.lo[2] + self.hi[2]) / 2)

    def get_z(self, x, y):  # CNavArea::GetZ
        dx, dy = self.hi[0] - self.lo[0], self.hi[1] - self.lo[1]
        if dx == 0 or dy == 0:
            return self.neZ
        u = min(max((x - self.lo[0]) / dx, 0.0), 1.0)
        v = min(max((y - self.lo[1]) / dy, 0.0), 1.0)
        north = self.lo[2] + u * (self.neZ - self.lo[2])
        south = self.swZ + u * (self.hi[2] - self.swZ)
        return north + v * (south - north)

    def overlaps_xy(self, x, y):
        return self.lo[0] <= x <= self.hi[0] and self.lo[1] <= y <= self.hi[1]


def read_nav(path):
    with open(path, "rb") as f:
        buf = f.read()
    r = Reader(buf)
    nav = dict(path=path, size=len(buf))
    magic = r.u32()
    if magic != NAV_MAGIC_NUMBER:
        raise ValueError(f"bad magic {magic:#x}")
    version = r.u32()
    if version > NAV_VERSION:
        raise ValueError(f"unknown version {version}")
    nav["version"] = version
    nav["bsp_size"] = r.u32() if version >= 4 else None
    places = []
    if version >= NAV_VERSION:
        count = r.u16()
        for _ in range(count):
            n = r.u16()
            raw = r.read(f"<{n}s")[0]
            places.append(raw.split(b"\0")[0].decode("latin-1"))
    nav["places"] = places
    count = r.u32()
    if count == 0:
        raise ValueError("zero areas (loader returns NAV_INVALID_FILE)")
    areas = []
    for _ in range(count):
        a = NavArea()
        a.id = r.u32()
        a.attr = r.u8()
        e = r.f32(6)
        a.lo, a.hi = e[0:3], e[3:6]
        a.neZ, a.swZ = r.f32(), r.f32()
        for d in range(4):
            n = r.u32()
            a.conn[d] = list(r.read(f"<{n}I")) if n else []
        hcount = r.u8()
        for _h in range(hcount):
            if version == 1:
                a.spots.append((0, r.f32(3), 0x01))
            else:
                sid = r.u32()
                pos = r.f32(3)
                fl = r.u8()
                a.spots.append((sid, pos, fl))
        acount = r.u8()
        for _a in range(acount):
            here, prev = r.u32(), r.u32()
            t1 = r.u8()
            nxt = r.u32()
            t2 = r.u8()
            a.approach.append((here, prev, t1, nxt, t2))
        ecount = r.u32()
        if version < 3:
            for _e in range(ecount):
                r.u32(); r.u32(); r.f32(3); r.f32(3)
                sc = r.u8()
                for _s in range(sc):
                    r.f32(3); r.f32()
        else:
            for _e in range(ecount):
                frm = r.u32(); fd = r.u8(); to = r.u32(); td = r.u8()
                sc = r.u8()
                sl = [r.read("<IB") for _s in range(sc)]
                a.encounters.append((frm, fd, to, td, sl))
            if version >= NAV_VERSION:
                a.place_entry = r.u16()
        areas.append(a)
    nav["areas"] = areas
    nav["trailing"] = len(buf) - r.off
    return nav


# ---- graph helpers ---------------------------------------------------------------------------------------
def scc(nodes, succ):
    """Kosaraju, iterative. Returns list of components (lists), largest first."""
    order, seen = [], set()
    for s in nodes:
        if s in seen:
            continue
        stack = [(s, iter(succ[s]))]
        seen.add(s)
        while stack:
            v, it = stack[-1]
            nxt = next(it, None)
            if nxt is None:
                stack.pop()
                order.append(v)
            elif nxt not in seen:
                seen.add(nxt)
                stack.append((nxt, iter(succ[nxt])))
    pred = defaultdict(list)
    for v in nodes:
        for w in succ[v]:
            pred[w].append(v)
    comp, comps = {}, []
    for s in reversed(order):
        if s in comp:
            continue
        cur = [s]
        comp[s] = len(comps)
        i = 0
        while i < len(cur):
            v = cur[i]
            i += 1
            for w in pred[v]:
                if w not in comp:
                    comp[w] = len(comps)
                    cur.append(w)
        comps.append(cur)
    comps.sort(key=len, reverse=True)
    return comps


def astar(by_id, start, goal):
    """NavAreaBuildPath with ShortestPathCost-like cost (center distance). Returns list of ids or None."""
    if start == goal:
        return [start]
    gz = by_id[goal].center
    openh = [(0.0, 0.0, start)]
    came, cost = {start: None}, {start: 0.0}
    while openh:
        _, g, a = heapq.heappop(openh)
        if a == goal:
            path = []
            while a is not None:
                path.append(a)
                a = came[a]
            return path[::-1]
        if g > cost[a]:
            continue
        ca = by_id[a].center
        for d in range(4):
            for b in by_id[a].conn[d]:
                ng = g + math.dist(ca, by_id[b].center)
                if ng < cost.get(b, 1e18):
                    cost[b] = ng
                    came[b] = a
                    heapq.heappush(openh, (ng + math.dist(by_id[b].center, gz), ng, b))
    return None


def get_nav_area(areas, pos, beneath=120.0):  # CNavAreaGrid::GetNavArea
    best, bz = None, -1e18
    tz = pos[2] + 5.0
    for a in areas:
        if a.overlaps_xy(pos[0], pos[1]):
            z = a.get_z(pos[0], pos[1])
            if z > tz or z < pos[2] - beneath:
                continue
            if z > bz:
                best, bz = a, z
    return best


def collect_zone(areas, mins, maxs, fudge=50.0, cap=16):  # CollectOverlappingAreas, MAX_ZONE_NAV_AREAS 16
    out = []
    lo = (mins[0], mins[1], mins[2] - fudge)
    hi = (maxs[0], maxs[1], maxs[2] + fudge)
    for a in areas:
        if (a.hi[0] >= lo[0] and a.lo[0] <= hi[0] and a.hi[1] >= lo[1] and a.lo[1] <= hi[1]
                and a.hi[2] >= lo[2] and a.lo[2] <= hi[2]):
            out.append(a)
            if len(out) == cap:
                break
    return out


# ---- independent collision check (port of mc_world.cpp ShapeBoxes + TestBox penetration rule) -------------
DOOR_T = 3.0 / 16.0


def shape_boxes(shape, state):
    """Local [0,1] boxes for a shape+state, exactly as mcw::ShapeBoxes."""
    if shape == "SHAPE_CUBE":
        return [(0, 0, 0, 1, 1, 1)]
    if shape == "SHAPE_SLAB":
        return [(0, 0, 0.5, 1, 1, 1)] if state & 1 else [(0, 0, 0, 1, 1, 0.5)]
    if shape == "SHAPE_STAIRS":
        facing, ud = state & 3, bool(state & 4)
        sz0, bz0 = (0.5, 0.0) if ud else (0.0, 0.5)
        back = {0: (0.5, 0, bz0, 1, 1, bz0 + 0.5), 1: (0, 0.5, bz0, 1, 1, bz0 + 0.5),
                2: (0, 0, bz0, 0.5, 1, bz0 + 0.5), 3: (0, 0, bz0, 1, 0.5, bz0 + 0.5)}[facing]
        return [(0, 0, sz0, 1, 1, sz0 + 0.5), back]
    if shape == "SHAPE_DOOR":
        facing, opened, hinge_r = state & 3, bool(state & 4), bool(state & 16)
        side = ((facing + (3 if hinge_r else 1)) & 3) if opened else facing
        b = [0, 0, 0, 1, 1, 1]
        if side == 0:
            b[0] = 1 - DOOR_T
        elif side == 1:
            b[1] = 1 - DOOR_T
        elif side == 2:
            b[3] = DOOR_T
        else:
            b[4] = DOOR_T
        return [tuple(b)]
    if shape == "SHAPE_PANE":
        return [(7 / 16, 7 / 16, 0, 9 / 16, 9 / 16, 1)]
    return []


class Collider:
    TOL = 1.0 / 1024.0

    def __init__(self, model):
        self.m = model
        w = model.world
        shapes = navgen.load_shapes()
        self.shape_of = [shapes.get(n) or navgen.guess_shape(n) for n in w["palette"]]
        self.cells = w["cells"]

    def boxes_in(self, lo, hi):
        m = self.m
        ox, oy, oz = m.origin
        bs = m.bs
        r = []
        for a, (l, h, n, o) in enumerate(zip(lo, hi, (m.sx, m.sy, m.sz), (ox, oy, oz))):
            c0 = max(int(math.floor((l - o) / bs)), 0)
            c1 = min(int(math.ceil((h - o) / bs)) - 1, n - 1)
            r.append((c0, c1))
        out = []
        for bz in range(r[2][0], r[2][1] + 1):
            for by in range(r[1][0], r[1][1] + 1):
                for bx in range(r[0][0], r[0][1] + 1):
                    c = int(self.cells[bz, by, bx])
                    if not c:
                        continue
                    for b in shape_boxes(self.shape_of[c & 0x3FF], c >> 10):
                        out.append((ox + (bx + b[0]) * bs, oy + (by + b[1]) * bs, oz + (bz + b[2]) * bs,
                                    ox + (bx + b[3]) * bs, oy + (by + b[4]) * bs, oz + (bz + b[5]) * bs))
        return out

    def hull_free(self, x, y, feet, height, half=16.0):
        lo = (x - half, y - half, feet)
        hi = (x + half, y + half, feet + height)
        for b in self.boxes_in(lo, hi):
            if all(hi[i] - b[i] > self.TOL and b[i + 3] - lo[i] > self.TOL for i in range(3)):
                return False
        return True


def collision_checks(nav, model):
    """Every area cell must hold a player hull (72, or 36 if NAV_CROUCH) somewhere in its column, and every link
    must let that hull straddle the shared edge at the higher floor in at least one edge column. Lateral offsets
    up to 4 units are tried (open door panels leave 32.5 of 40 units)."""
    col = Collider(model)
    areas = nav["areas"]
    by_id = {a.id: a for a in areas}
    offs = (0.0, -4.0, 4.0, -2.0, 2.0)
    bs = model.bs
    bad_cells, bad_links, nlinks, ncells = [], [], 0, 0

    def hull_h(a, b=None):
        crouch = (a.attr & NAV_CROUCH) or (b is not None and b.attr & NAV_CROUCH)
        return navgen.HALF_HUMAN_HEIGHT if crouch else navgen.HUMAN_HEIGHT

    def straddle(a, b, d):
        """Hull crosses the shared edge at the higher floor and, when b is lower, can fall down to b's floor
        just past the edge (otherwise the player lands on something else, e.g. a slab bridge above b)."""
        feet = max(a.lo[2], b.lo[2]) + 0.01
        hh = hull_h(a, b)
        ddx, ddy = navgen.DIR_DELTA[d]
        if d in (NORTH, SOUTH):
            yb = a.lo[1] if d == NORTH else a.hi[1]
            x0, x1 = max(a.lo[0], b.lo[0]), min(a.hi[0], b.hi[0])
            pts = [(x0 + (k + 0.5) * bs + o, yb) for k in range(int(round((x1 - x0) / bs))) for o in offs]
        else:
            xb = a.lo[0] if d == WEST else a.hi[0]
            y0, y1 = max(a.lo[1], b.lo[1]), min(a.hi[1], b.hi[1])
            pts = [(xb, y0 + (k + 0.5) * bs + o) for k in range(int(round((y1 - y0) / bs))) for o in offs]
        for (px_, py_) in pts:
            if not col.hull_free(px_, py_, feet, hh):
                continue
            if b.lo[2] >= a.lo[2]:
                return True
            qx, qy = px_ + ddx * 16.5, py_ + ddy * 16.5  # hull fully on b's side of the edge
            z = a.lo[2] + 0.01
            fall_ok = True
            while z > b.lo[2] + 0.01:
                if not col.hull_free(qx, qy, z, hh):
                    fall_ok = False
                    break
                z -= 10.0
            if fall_ok and col.hull_free(qx, qy, b.lo[2] + 0.01, hh):
                return True
        return False

    for a in areas:
        h = hull_h(a)
        nx = int(round((a.hi[0] - a.lo[0]) / bs))
        ny = int(round((a.hi[1] - a.lo[1]) / bs))
        for j in range(ny):
            for i in range(nx):
                ncells += 1
                cx, cy = a.lo[0] + (i + 0.5) * bs, a.lo[1] + (j + 0.5) * bs
                if not any(col.hull_free(cx + dx, cy + dy, a.lo[2] + 0.01, h) for dx in offs for dy in offs):
                    bad_cells.append((a.id, cx, cy, a.lo[2]))
        for d in range(4):
            for bid in a.conn[d]:
                b = by_id.get(bid)
                if b is None:
                    continue
                nlinks += 1
                if not straddle(a, b, d):
                    bad_links.append((a.id, d, bid))

    # inverse test: touching pairs (shared edge) within link range that are NOT linked should be blocked
    missed, tested = [], 0
    for a in areas:
        for b in areas:
            if a is b:
                continue
            dz = b.lo[2] - a.lo[2]
            if dz > navgen.JUMP_CROUCH_HEIGHT or -dz >= navgen.DEATH_DROP:
                continue
            for d in range(4):
                if d == NORTH:
                    touch = abs(a.lo[1] - b.hi[1]) < 0.01 and min(a.hi[0], b.hi[0]) - max(a.lo[0], b.lo[0]) > 0
                elif d == SOUTH:
                    touch = abs(a.hi[1] - b.lo[1]) < 0.01 and min(a.hi[0], b.hi[0]) - max(a.lo[0], b.lo[0]) > 0
                elif d == EAST:
                    touch = abs(a.hi[0] - b.lo[0]) < 0.01 and min(a.hi[1], b.hi[1]) - max(a.lo[1], b.lo[1]) > 0
                else:
                    touch = abs(a.lo[0] - b.hi[0]) < 0.01 and min(a.hi[1], b.hi[1]) - max(a.lo[1], b.lo[1]) > 0
                if not touch or b.id in a.conn[d]:
                    continue
                tested += 1
                if straddle(a, b, d):
                    missed.append((a.id, d, b.id, round(dz)))
    return dict(cells=ncells, links=nlinks, bad_cells=bad_cells, bad_links=bad_links,
                unlinked_tested=tested, missed=missed)


# ---- validation ------------------------------------------------------------------------------------------
def validate(nav, maps_dir, map_name, model=None):
    errors, warns, info = [], [], {}
    areas = nav["areas"]
    by_id = {}
    for a in areas:
        if a.id == 0:
            errors.append("area id 0")
        if a.id in by_id:
            errors.append(f"duplicate area id {a.id}")
        by_id[a.id] = a
        if a.lo[0] >= a.hi[0] or a.lo[1] >= a.hi[1]:
            errors.append(f"degenerate area #{a.id}")
    if nav["trailing"]:
        errors.append(f"{nav['trailing']} trailing bytes after the last area")
    bsp = os.path.join(maps_dir, map_name + ".bsp")
    bsp_size = os.path.getsize(bsp) if os.path.exists(bsp) else None
    info["bsp_size_file"] = bsp_size
    if nav["version"] >= 4 and bsp_size is not None and nav["bsp_size"] != bsp_size:
        warns.append(f"header bsp size {nav['bsp_size']} != current {bsp} size {bsp_size}: the game will show "
                     "'AI navigation data is from a different version of this map' (re-run navgen)")
    for i, name in enumerate(nav["places"]):
        if name not in DEFAULT_PLACE_NAMES:
            warns.append(f"place '{name}' unknown to the fallback place list (would shift later entries)")
    # links
    nlinks = one_way = 0
    spot_ids = set()
    for a in areas:
        for d in range(4):
            if len(set(a.conn[d])) != len(a.conn[d]):
                errors.append(f"area #{a.id} duplicate link in dir {d}")
            for b in a.conn[d]:
                nlinks += 1
                if b == a.id:
                    errors.append(f"area #{a.id} links to itself")
                    continue
                if b not in by_id:
                    errors.append(f"area #{a.id} links to missing id {b} (loader: NAV_CORRUPT_DATA)")
                    continue
                B = by_id[b]
                back = any(a.id in B.conn[dd] for dd in range(4))
                if not back:
                    one_way += 1
                # geometry: areas must touch along the edge in direction d
                if d == NORTH:
                    touch, ov = abs(a.lo[1] - B.hi[1]) < 0.01, min(a.hi[0], B.hi[0]) - max(a.lo[0], B.lo[0])
                elif d == SOUTH:
                    touch, ov = abs(a.hi[1] - B.lo[1]) < 0.01, min(a.hi[0], B.hi[0]) - max(a.lo[0], B.lo[0])
                elif d == EAST:
                    touch, ov = abs(a.hi[0] - B.lo[0]) < 0.01, min(a.hi[1], B.hi[1]) - max(a.lo[1], B.lo[1])
                else:
                    touch, ov = abs(a.lo[0] - B.hi[0]) < 0.01, min(a.hi[1], B.hi[1]) - max(a.lo[1], B.lo[1])
                if not touch or ov <= 0:
                    errors.append(f"link #{a.id}->{b} dir {d}: areas do not share an edge")
                dz = B.lo[2] - a.lo[2]
                if dz > JUMP_CROUCH_HEIGHT + 0.01:
                    errors.append(f"link #{a.id}->{b}: climb {dz:.0f} > JumpCrouchHeight")
                if -dz >= DEATH_DROP:
                    errors.append(f"link #{a.id}->{b}: drop {-dz:.0f} >= DeathDrop")
                if -dz > JUMP_CROUCH_HEIGHT + 0.01 and back:
                    errors.append(f"link #{a.id}<->{b}: drop {-dz:.0f} but linked both ways")
        for sid, pos, fl in a.spots:
            if sid == 0 or sid in spot_ids:
                errors.append(f"bad/duplicate hiding spot id {sid}")
            spot_ids.add(sid)
            if not a.overlaps_xy(pos[0], pos[1]):
                warns.append(f"hiding spot {sid} outside its area #{a.id}")
        for ap in a.approach:
            for ref in (ap[0], ap[1], ap[3]):
                if ref and ref not in by_id:
                    errors.append(f"area #{a.id} approach refers to missing area {ref}")
        for enc in a.encounters:
            if enc[0] not in by_id or enc[2] not in by_id:
                errors.append(f"area #{a.id} encounter refers to missing area")
        if a.place_entry > len(nav["places"]):
            errors.append(f"area #{a.id} place entry {a.place_entry} > directory size")
    info.update(areas=len(areas), links=nlinks, one_way_links=one_way, hiding_spots=len(spot_ids),
                approach=sum(len(a.approach) for a in areas), encounters=sum(len(a.encounters) for a in areas),
                crouch_areas=sum(1 for a in areas if a.attr & NAV_CROUCH),
                precise_areas=sum(1 for a in areas if a.attr & NAV_PRECISE),
                jump_areas=sum(1 for a in areas if a.attr & NAV_JUMP),
                placed_areas=sum(1 for a in areas if a.place_entry))
    sizes = [((a.hi[0] - a.lo[0]) * (a.hi[1] - a.lo[1])) for a in areas]
    info["area_size_units2"] = dict(min=min(sizes), max=max(sizes), mean=round(sum(sizes) / len(sizes), 1))

    # geometry against the voxel world
    if model is not None:
        bad_floor = bad_crouch = cells = 0
        for a in areas:
            x0 = int(round((a.lo[0] - model.origin[0]) / model.bs))
            x1 = int(round((a.hi[0] - model.origin[0]) / model.bs))
            y0 = int(round((a.lo[1] - model.origin[1]) / model.bs))
            y1 = int(round((a.hi[1] - model.origin[1]) / model.bs))
            f = int(round((a.lo[2] - model.origin[2]) / model.half))
            for y in range(y0, y1):
                for x in range(x0, x1):
                    cells += 1
                    cid = model.cell_id[f, y, x] if (0 <= f <= model.H) else -1
                    if cid < 0:
                        bad_floor += 1
                    elif not model.cstand[cid] and not (a.attr & NAV_CROUCH):
                        bad_crouch += 1
        info["area_cells_checked"] = cells
        if bad_floor:
            errors.append(f"{bad_floor} area cells are not standable voxel floors at the area height")
        if bad_crouch:
            errors.append(f"{bad_crouch} area cells have < 72 units headroom but the area is not NAV_CROUCH")
        cc = collision_checks(nav, model)
        info["collision"] = dict(cells=cc["cells"], links=cc["links"], bad_cells=len(cc["bad_cells"]),
                                 bad_links=len(cc["bad_links"]), unlinked_tested=cc["unlinked_tested"],
                                 missed=len(cc["missed"]), missed_examples=cc["missed"][:20])
        if cc["missed"]:
            warns.append(f"{len(cc['missed'])} touching unlinked area pairs where the hull could cross the shared "
                         f"edge (possible missing links), e.g. {cc['missed'][:8]}")
        for c in cc["bad_cells"][:20]:
            errors.append(f"hull does not fit in area #{c[0]} cell at ({c[1]:.0f},{c[2]:.0f},{c[3]:.0f})")
        for l in cc["bad_links"][:20]:
            errors.append(f"link #{l[0]}->{l[2]} dir {l[1]}: no edge column where the hull can cross")
        if len(cc["bad_cells"]) > 20 or len(cc["bad_links"]) > 20:
            errors.append(f"... {len(cc['bad_cells'])} bad cells, {len(cc['bad_links'])} bad links in total")

    # connectivity
    succ = {a.id: [b for d in range(4) for b in a.conn[d] if b in by_id] for a in areas}
    comps = scc([a.id for a in areas], succ)
    comp_of = {v: i for i, c in enumerate(comps) for v in c}
    und = defaultdict(set)
    for v, ws in succ.items():
        for w in ws:
            und[v].add(w)
            und[w].add(v)
    weak, seen = [], set()
    for a in areas:
        if a.id in seen:
            continue
        st, cur = [a.id], []
        seen.add(a.id)
        while st:
            v = st.pop()
            cur.append(v)
            for w in und[v]:
                if w not in seen:
                    seen.add(w)
                    st.append(w)
        weak.append(cur)
    weak.sort(key=len, reverse=True)
    info["scc_count"] = len(comps)
    info["scc_sizes"] = [len(c) for c in comps[:10]]
    info["weak_components"] = len(weak)
    info["weak_sizes"] = [len(c) for c in weak[:10]]
    main = set(comps[0])
    sinks = [a.id for a in areas if not succ[a.id]]
    leaves = [a.id for a in areas if len(und[a.id]) == 1]
    info["sinks"] = sinks
    info["dead_end_leaves"] = len(leaves)
    info["not_in_main_scc"] = len(areas) - len(main)

    meta_path = os.path.join(maps_dir, map_name + ".json")
    keypoints = {}
    if os.path.exists(meta_path):
        with open(meta_path, encoding="utf-8") as f:
            meta = json.load(f)
        info["meta"] = dict(bombsites=meta.get("bombsites", []), spawns=meta.get("spawns", {}),
                            landmarks=meta.get("landmarks") or meta.get("surfaceSamples") or {})
        spawn_res = {}
        for team in ("ct", "t"):
            ok = total = 0
            for s in meta.get("spawns", {}).get(team, []):
                total += 1
                a = get_nav_area(areas, s)
                if a is not None and a.id in main:
                    ok += 1
                    keypoints.setdefault(f"{team}_spawn", a.id)
            spawn_res[team] = (ok, total)
            if total and ok == 0:
                errors.append(f"no {team.upper()} spawn lies on an area of the main component")
            elif ok < total:
                warns.append(f"{total - ok}/{total} {team.upper()} spawns not on a main-component area")
        info["spawns_on_main"] = spawn_res
        site_res = {}
        for site in meta.get("bombsites", []):
            zone = collect_zone(areas, site["mins"], site["maxs"])
            inmain = [a.id for a in zone if a.id in main]
            # areas really inside the site footprint (not just touching its boundary)
            inner = [a for a in zone if a.hi[0] > site["mins"][0] and a.lo[0] < site["maxs"][0]
                     and a.hi[1] > site["mins"][1] and a.lo[1] < site["maxs"][1]]
            site_res[site["name"]] = dict(zone_areas=len(zone), in_main=len(inmain),
                                          inside_footprint=len(inner))
            if not inmain:
                errors.append(f"bombsite {site['name']}: no zone area in the main component")
            else:
                c = ((site["mins"][0] + site["maxs"][0]) / 2, (site["mins"][1] + site["maxs"][1]) / 2)
                best = min((a for a in inner if a.id in main) or [by_id[i] for i in inmain],
                           key=lambda a: math.dist(c, a.center[:2]))
                keypoints[f"site_{site['name']}"] = best.id
        info["bombsites"] = site_res
        lm_res = {}
        for name, p in (info["meta"]["landmarks"] or {}).items():
            a = get_nav_area(areas, p)
            lm_res[name] = (a.id if a else None, bool(a and a.id in main))
            if a and a.id in main:
                keypoints.setdefault(name, a.id)
            else:
                warns.append(f"landmark {name} {p} is not on a main-component area")
        info["landmarks"] = lm_res
    # routes
    routes = {}
    for s in ("t_spawn", "ct_spawn"):
        for g in [k for k in keypoints if k.startswith("site_")] + (["ct_spawn"] if s == "t_spawn" else []):
            if s not in keypoints or g not in keypoints:
                continue
            p = astar(by_id, keypoints[s], keypoints[g])
            if p is None:
                errors.append(f"no path {s} -> {g}")
                routes[f"{s}->{g}"] = None
            else:
                L = sum(math.dist(by_id[p[i]].center, by_id[p[i + 1]].center) for i in range(len(p) - 1))
                routes[f"{s}->{g}"] = dict(areas=len(p), length=round(L))
    info["routes"] = routes
    info["keypoints"] = keypoints
    info["comp_of"] = comp_of
    info["main"] = main
    info["sink_set"] = set(sinks)
    info["leaf_set"] = set(leaves)
    return errors, warns, info


# ---- previews --------------------------------------------------------------------------------------------
def render(nav, info, model, out_png, out_png_attr, scale=8):
    from PIL import Image, ImageDraw
    import numpy as np
    areas = nav["areas"]
    ox, oy, oz = model.origin
    W, Hh = model.sx * scale, model.sy * scale

    def px(x, y):
        return ((x - ox) / model.bs * scale, (model.sy - (y - oy) / model.bs) * scale)

    # background: height of the highest walkable surface per column, grey shaded
    top = np.full((model.sy, model.sx), np.nan)
    for c in range(model.ncell):
        y, x = model.cy[c], model.cx[c]
        z = model.cf[c]
        if np.isnan(top[y, x]) or z > top[y, x]:
            top[y, x] = z
    zmin, zmax = np.nanmin(top), np.nanmax(top)
    g = np.where(np.isnan(top), 20, 40 + 90 * (top - zmin) / max(zmax - zmin, 1)).astype(np.uint8)
    g = g[::-1, :]
    bg = Image.fromarray(np.ascontiguousarray(g)).resize((W, Hh), Image.NEAREST).convert("RGBA")

    def base_layer(color_fn, title):
        img = bg.copy()
        ov = Image.new("RGBA", img.size, (0, 0, 0, 0))
        dr = ImageDraw.Draw(ov)
        for a in sorted(areas, key=lambda a: a.lo[2]):
            x0, y1 = px(a.lo[0], a.lo[1])
            x1, y0 = px(a.hi[0], a.hi[1])
            col = color_fn(a)
            dr.rectangle([x0 + 1, y0 + 1, x1 - 1, y1 - 1], fill=col, outline=(0, 0, 0, 220))
        img = Image.alpha_composite(img, ov)
        return img

    rng = random.Random(12345)
    colors = {a.id: (rng.randint(60, 255), rng.randint(60, 255), rng.randint(60, 255), 200) for a in areas}
    img = base_layer(lambda a: colors[a.id], "areas")
    dr = ImageDraw.Draw(img)
    by_id = {a.id: a for a in areas}
    for a in areas:
        ca = px(*a.center[:2])
        for d in range(4):
            for b in a.conn[d]:
                B = by_id[b]
                cb = px(*B.center[:2])
                back = any(a.id in B.conn[dd] for dd in range(4))
                if back:
                    if a.id < b:
                        dr.line([ca, cb], fill=(255, 255, 255, 170), width=1)
                else:
                    dr.line([ca, cb], fill=(255, 30, 30, 255), width=2)
                    dr.ellipse([cb[0] - 2.5, cb[1] - 2.5, cb[0] + 2.5, cb[1] + 2.5], fill=(255, 30, 30, 255))
    meta = info.get("meta", {})
    for site in meta.get("bombsites", []):
        x0, y1 = px(site["mins"][0], site["mins"][1])
        x1, y0 = px(site["maxs"][0], site["maxs"][1])
        dr.rectangle([x0, y0, x1, y1], outline=(255, 230, 0, 255), width=3)
        dr.text((x0 + 4, y0 + 2), "SITE " + site["name"], fill=(255, 230, 0, 255))
    for team, col in (("ct", (60, 140, 255, 255)), ("t", (255, 120, 30, 255))):
        for s in meta.get("spawns", {}).get(team, []):
            p = px(s[0], s[1])
            dr.ellipse([p[0] - 4, p[1] - 4, p[0] + 4, p[1] + 4], fill=col, outline=(0, 0, 0, 255))
    for name, p in (meta.get("landmarks") or {}).items():
        q = px(p[0], p[1])
        dr.line([q[0] - 4, q[1], q[0] + 4, q[1]], fill=(255, 255, 255, 255), width=2)
        dr.line([q[0], q[1] - 4, q[0], q[1] + 4], fill=(255, 255, 255, 255), width=2)
        dr.text((q[0] + 5, q[1] - 6), name, fill=(255, 255, 255, 255))
    dr.text((6, 6), f"{os.path.basename(nav['path'])}: {len(areas)} areas, {info['links']} links "
                    f"({info['one_way_links']} one-way drops in red). +Y up. CT blue, T orange.",
            fill=(255, 255, 255, 255))
    img.convert("RGB").save(out_png)

    main = info["main"]

    def attr_color(a):
        if a.id not in main:
            return (255, 0, 0, 230)
        if a.attr & NAV_CROUCH:
            return (255, 150, 0, 220)
        if a.attr & NAV_PRECISE:
            return (0, 190, 220, 200)
        return (120, 200, 120, 190)

    img2 = base_layer(attr_color, "attrs")
    dr2 = ImageDraw.Draw(img2)
    for a in areas:
        c = px(*a.center[:2])
        if a.id in info["sink_set"]:
            dr2.line([c[0] - 4, c[1] - 4, c[0] + 4, c[1] + 4], fill=(255, 0, 0, 255), width=2)
            dr2.line([c[0] - 4, c[1] + 4, c[0] + 4, c[1] - 4], fill=(255, 0, 0, 255), width=2)
        elif a.id in info["leaf_set"]:
            dr2.ellipse([c[0] - 2, c[1] - 2, c[0] + 2, c[1] + 2], fill=(255, 0, 255, 255))
        for sid, pos, fl in a.spots:
            q = px(pos[0], pos[1])
            dr2.rectangle([q[0] - 1, q[1] - 1, q[0] + 1, q[1] + 1],
                          fill=(255, 255, 255, 255) if fl & 1 else (90, 90, 90, 255))
    dr2.text((6, 6), "green plain, cyan PRECISE, orange CROUCH, red = not in main component; "
                     "magenta dot = dead-end leaf, X = sink; white squares = hiding spots (grey = no cover)",
             fill=(255, 255, 255, 255))
    img2.convert("RGB").save(out_png_attr)


# ---- report ----------------------------------------------------------------------------------------------
def write_report(path, nav, errors, warns, info, gen, pngs):
    L = []
    w = L.append
    m = gen.get("map", "?") if gen else "?"
    w(f"# navgen report: {m}.nav\n")
    w(f"Generated {time.strftime('%Y-%m-%d %H:%M:%S')} by `tools\\navgen\\navgen.py`; read back and validated by "
      f"`tools\\navgen\\navcheck.py` (byte layout of ReGameDLL `nav_file.cpp`).\n")
    w("## Output\n")
    w(f"- File: `{nav['path']}` ({nav['size']} bytes)")
    w(f"- Format: magic 0xFEEDFACE, **version {nav['version']}** (NAV_VERSION, newest the loader accepts), "
      f"bsp size field {nav['bsp_size']} (current .bsp: {info.get('bsp_size_file')}), "
      f"{len(nav['places'])} place names: {', '.join(nav['places'])}")
    w("- Per area: id, attributes, extent, NE/SW corner z, links N/E/S/W, hiding spots, approach areas (0), "
      "encounter paths (0), place entry. No ladders (none in the voxel world; the loader builds ladders from "
      "func_ladder entities, not from the file).")
    w(f"- Previews: {', '.join('`' + p + '`' for p in pngs)}\n")
    w("## Stats\n")
    if gen:
        w(f"- Voxel world {gen['world_size']} blocks, origin {gen['origin']}; mcw mtime "
          f"{time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(gen['mcw_mtime']))}")
        w(f"- Floor candidates {gen['floor_candidates']} (of which {gen['too_low']} have < 36 units headroom); "
          f"walkable cells {gen['cells']} ({gen['cells_standing']} standing)")
        w(f"- Reachable from the spawns: {gen['reachable']} cells; kept {gen['kept']} "
          f"(dropped {gen['trap_cells']} trap cells you can fall into but not leave; "
          f"{gen['unreachable_cells']} cells unreachable: roofs, wall tops, sealed space)")
        w(f"- Cell transitions used, by height change in 20-unit steps (k<0 = drop): {gen['transitions_by_k']}")
        w(f"- Crouch cells {gen['crouch_cells']} (lip-only {gen['lip_crouch_cells']}); cells in front of a step up "
          f"{gen['precise_cells']} -> {gen.get('precise_areas', '?')} areas marked NAV_PRECISE "
          f"(option {'on' if gen['precise'] else 'off'})")
        w(f"- Max area size {gen['max_size']}x{gen['max_size']} blocks; cross-level transitions that a "
          f"rectangle edge cannot represent (dropped): {gen['cross_level_dropped']}; partial portals: "
          f"{gen['partial_portals']}")
        if "area_cells_histogram" in gen:
            w(f"- Areas by size in cells (cells: count): {gen['area_cells_histogram']}")
        if "cells_gained_with_60_climbs" in gen:
            w(f"- Cells that would become reachable if 60-unit (1.5-block) climbs were linked (humans can "
              f"crouch-jump ~63, bots stop at 58): {gen['cells_gained_with_60_climbs']}")
        if "place_counts" in gen:
            w(f"- Areas per place: {gen['place_counts']}")
        w(f"- Generation time {gen['seconds']} s")
    w(f"- **Areas {info['areas']}**, **directed links {info['links']}** ({info['one_way_links']} one-way drop links), "
      f"area size (units^2) {info['area_size_units2']}")
    w(f"- Attributes: CROUCH {info['crouch_areas']}, PRECISE {info['precise_areas']}, JUMP {info['jump_areas']}")
    w(f"- Hiding spots {info['hiding_spots']}; approach entries {info['approach']}; encounter paths "
      f"{info['encounters']}; areas with a place {info['placed_areas']}\n")
    w("## Connectivity\n")
    w(f"- Strongly connected components: {info['scc_count']} (largest sizes {info['scc_sizes']}); weakly connected: "
      f"{info['weak_components']} (sizes {info['weak_sizes']})")
    w(f"- Areas outside the main SCC: {info['not_in_main_scc']}; sinks (no outgoing link): {len(info['sinks'])}; "
      f"dead-end leaves (one neighbour): {info['dead_end_leaves']}")
    if "spawns_on_main" in info:
        sp = info["spawns_on_main"]
        w(f"- Spawns on main-component areas: CT {sp['ct'][0]}/{sp['ct'][1]}, T {sp['t'][0]}/{sp['t'][1]}")
        for name, r in info["bombsites"].items():
            w(f"- Bombsite {name}: {r['zone_areas']} zone areas (bot CollectOverlappingAreas keeps the first 16 "
              f"in file order; navgen writes the most-overlapping areas first), {r['in_main']} in main component, "
              f"{r['inside_footprint']} inside the footprint")
        w("- Landmarks: " + ", ".join(f"{k} {'OK' if v[1] else 'MISSING'} (#{v[0]})"
                                     for k, v in info["landmarks"].items()))
    w("- Routes (A* over area centers): " + "; ".join(
        f"{k}: {v['length']} u / {v['areas']} areas" if v else f"{k}: NONE" for k, v in info["routes"].items()))
    both = (not errors) and info.get("spawns_on_main", {}).get("ct", (0,))[0] and \
        info.get("spawns_on_main", {}).get("t", (0,))[0] and all(r["in_main"] for r in info.get("bombsites", {}).values())
    w(f"\n**Both team spawns and both bombsites in one strongly connected component: {'YES' if both else 'NO'}**\n")
    w("## Validation\n")
    w(f"- Errors: {len(errors)}" + ("" if not errors else "\n" + "\n".join("  - " + e for e in errors[:50])))
    w(f"- Warnings: {len(warns)}" + ("" if not warns else "\n" + "\n".join("  - " + x for x in warns[:50])))
    if gen and gen.get("warnings"):
        w("- Generator warnings:\n" + "\n".join("  - " + x for x in gen["warnings"]))
    w(f"- Checked {info.get('area_cells_checked', 0)} area cells against the voxel world (standable floor at the area "
      "height, NAV_CROUCH wherever headroom < 72).")
    if "collision" in info:
        c = info["collision"]
        w(f"- Independent collision check (Python port of `mcw::ShapeBoxes` incl. real door panels, engine "
          f"penetration tolerance): {c['cells']} area cells hold the player hull (72, or 36 for CROUCH): "
          f"{c['cells'] - c['bad_cells']} OK / {c['bad_cells']} fail; {c['links']} links where the hull can straddle "
          f"the shared edge at the higher floor: {c['links'] - c['bad_links']} OK / {c['bad_links']} fail. "
          f"Inverse test: {c['unlinked_tested']} touching but unlinked directed pairs within link height range, "
          f"{c['missed']} of them passable by the hull (possible missing links).")
    w("")
    w(CONCERNS)
    w("## Regenerate\n")
    w("```\ncd Z:\\dev\\CSminecraft\npython tools\\navgen\\navgen.py mc_dust2\n```\n"
      "Re-run after the .mcw **or the .bsp** changes (the header stores the BSP size). "
      "`python tools\\navgen\\navcheck.py mc_dust2` re-validates an existing .nav (also one re-saved by the game).")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")


CONCERNS = """## Semantics and concerns

- **Heights.** One walkable cell per block column per surface layer, at the top of the highest solid
  half-block (slab tops are half-height floors; stairs count as their 0.5 slab with the back half as a
  20-unit step). Standing needs >= 72 units of air (4 half-blocks), 36..71 -> NAV_CROUCH, < 36 -> no cell.
  Areas are flat (all four corner z equal) and exactly cover whole 40x40 columns, so neighbours share edges.
- **Links (checked per edge cell, so walls and overhangs are respected).** Rise <= 58 (JumpCrouchHeight)
  -> two-way link (20 = slab step, walkable with sv_stepsize 24; 40 = full block, bots jump via
  DiscontinuityJump; 60 = 1.5 blocks is not linked upward). Drop 60..180 (< DeathDrop 200) -> one-way
  link from high to low only. This matches what the CS bot code expects: `ComputePathPositions` treats a
  link with no link back as a "jump down" (pushes the goal 25 units over the ledge and adds a landing
  node) and `PathCost` charges estimated fall damage for it. Ledge connections are NOT marked NAV_JUMP:
  in the original generator NAV_JUMP marks steep sloped strip areas, and bots refuse to walk jump->jump
  (PathCost returns -1), so marking large flat areas would break routes. Jumps up are triggered by the
  bots' ground probe (`DiscontinuityJump`), as on normal maps.
- **Bot StepHeight is 18 in nav.h while the server runs sv_stepsize 24.** Consequences on voxel maps:
  bots jump at every 20-unit slab step (dz > StepHeight) and their ankle feelers (StepHeight + 0.1 above
  the feet) hit slab-step faces, which makes them veer 300 units sideways when approaching a step at an
  angle. Mitigation in this file: every area containing a cell with a step up (20 or 40) out of it is
  NAV_PRECISE, which disables the feelers while the bot is in it (`--no-precise` turns this off; side
  effects: feelers also off near walls in those areas, and REGAMEDLL_ADD random-spawn generation skips
  areas with any attribute). Recommended real fix in the ReGameDLL build: set `StepHeight` to 24 (or 21)
  for voxel maps, then regenerate with `--no-precise`; the jump-at-each-step also goes away.
- **Crouch lips.** Where two standing cells differ in height and the gap above the higher floor is
  < 72 but >= 36 units (e.g. a doorway with a 2-block lintel next to a slab step), only a crouched
  player passes; the lower cell is marked NAV_CROUCH so bots crouch through it.
- **Doors** are treated as passable columns (CS bots treat doors as walk-through). The voxel doors are
  placed open, the panel leaves 32.5 of 40 units, so the 32-unit hull fits but bots may brush the panel.
- **Not computed here:** approach areas, encounter paths and sniper-spot flags (expensive visibility
  analysis; the loader does not require them). Hiding spots use the game's corner rule with cover tested
  by voxel ray casts. For full bot "map knowledge", run once in game with a bot present:
  `bot_nav_analyze` (recomputes hiding/sniper spots, approach areas, encounter paths with the engine
  traces, which see the voxels, and saves the .nav). If the game later saves the file, navcheck.py
  still reads it.
- **Places** are assigned by geodesic nearest landmark from the map metadata (names limited to the
  fallback list the loader knows without BotChatter.db) and overridden inside bombsites and buyzones.
- **Trap regions** (cells reachable only by a drop with no way back) are removed so bots never pick
  goals there; a bot that falls into one has no mesh under it.
- The **bsp size** in the header must equal the size of maps\\mc_dust2.bsp at load time, otherwise the
  game prints the "different version of this map" warning (harmless but noisy). The BSP was being
  regenerated by another agent during this work, so re-run navgen after the final BSP build.
"""


def run(nav_path, maps_dir, map_name, scratch, gen=None, model=None):
    nav = read_nav(nav_path)
    if model is None:
        from mcw_io import read_mcw
        world = read_mcw(os.path.join(maps_dir, map_name + ".mcw"))
        model = navgen.VoxelModel(world, navgen.load_shapes(), [])
        model.find_cells()
    errors, warns, info = validate(nav, maps_dir, map_name, model)
    os.makedirs(scratch, exist_ok=True)
    png = os.path.join(scratch, f"{map_name}_nav.png")
    png2 = os.path.join(scratch, f"{map_name}_nav_attrs.png")
    render(nav, info, model, png, png2)
    if gen is None:
        gp = os.path.join(scratch, map_name + "_navgen.json")
        if os.path.exists(gp):
            with open(gp, encoding="utf-8") as f:
                gen = json.load(f)
    report = os.path.join(scratch, "REPORT.md")
    write_report(report, nav, errors, warns, info, gen, [png, png2])
    summary = {k: v for k, v in info.items() if k not in ("comp_of", "main", "sink_set", "leaf_set", "meta")}
    summary["errors"] = errors
    summary["warnings"] = warns
    with open(os.path.join(scratch, f"{map_name}_navcheck.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=1, default=str)
    print(f"navcheck: version {nav['version']}, {info['areas']} areas, {info['links']} links "
          f"({info['one_way_links']} one-way), SCCs {info['scc_count']} {info['scc_sizes'][:5]}, "
          f"sinks {len(info['sinks'])}, leaves {info['dead_end_leaves']}")
    if "spawns_on_main" in info:
        print(f"navcheck: spawns on main CT {info['spawns_on_main']['ct']} T {info['spawns_on_main']['t']}; "
              f"bombsites {info['bombsites']}")
    print(f"navcheck: routes {info['routes']}")
    for e in errors[:30]:
        print("ERROR:", e)
    for x in warns[:30]:
        print("WARN:", x)
    print(f"navcheck: report {report}")
    return not errors


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("map", nargs="?", default="mc_dust2")
    ap.add_argument("--maps-dir", default=navgen.DEFAULT_MAPS)
    ap.add_argument("--nav", default=None)
    ap.add_argument("--scratch", default=navgen.DEFAULT_SCRATCH)
    args = ap.parse_args(argv)
    nav = args.nav or os.path.join(args.maps_dir, args.map + ".nav")
    return 0 if run(nav, args.maps_dir, args.map, args.scratch) else 1


if __name__ == "__main__":
    sys.exit(main())
