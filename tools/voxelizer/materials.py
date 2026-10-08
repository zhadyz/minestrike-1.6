"""Surface materials (nearest BSP face texture), subsoil, deep terrain with ores, decorations."""
from __future__ import annotations

import collections
import json
import re

import numpy as np
from scipy import ndimage

from bsp30 import Bsp30

DIRS = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]  # x,y,z unit vectors
UP, DOWN = 4, 5


def load_registry(path: str) -> list[str]:
    src = open(path, encoding="utf-8").read()
    names = re.findall(r'\{\s*"([a-z_0-9]+)",\s*SHAPE_\w+', src)
    return names


def load_texture_map(path: str) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


class FaceSet:
    """Polygons of the world + solid brush entity faces, with texture ids."""

    def __init__(self, bsp: Bsp30, models: list[int], skip: set[str]):
        self.polys = []
        self.normals = []
        self.dists = []
        self.tex = []
        self.texnames = []
        tid = {}
        for m in models:
            for f in bsp.model_faces(m):
                name = bsp.face_texture_name(f).lower()
                if name in skip:
                    continue
                v = bsp.face_vertices(f)
                n = bsp.face_normal(f)
                if name not in tid:
                    tid[name] = len(self.texnames)
                    self.texnames.append(name)
                self.polys.append(v)
                self.normals.append(n)
                self.dists.append(float(v.mean(axis=0) @ n))
                self.tex.append(tid[name])
        self.mins = np.array([p.min(axis=0) for p in self.polys])
        self.maxs = np.array([p.max(axis=0) for p in self.polys])


def point_poly_dist(Q: np.ndarray, V: np.ndarray, n: np.ndarray, d0: float) -> np.ndarray:
    dp = Q @ n - d0
    Qp = Q - dp[:, None] * n[None, :]
    k = len(V)
    inside = np.ones(len(Q), bool)
    seg = np.full(len(Q), np.inf)
    c = V.mean(axis=0)
    for i in range(k):
        a = V[i]
        b = V[(i + 1) % k]
        e = b - a
        en = np.cross(e, n)
        if (c - a) @ en > 0:
            en = -en
        inside &= ((Qp - a) @ en) <= 1e-6
        L2 = e @ e
        if L2 < 1e-9:
            continue
        t = np.clip(((Qp - a) @ e) / L2, 0, 1)
        proj = a[None, :] + t[:, None] * e[None, :]
        seg = np.minimum(seg, np.linalg.norm(Qp - proj, axis=1))
    plane_d = np.abs(dp)
    return np.where(inside, plane_d, np.sqrt(plane_d ** 2 + seg ** 2))


def match_surface_textures(faces: FaceSet, qpts: np.ndarray, qdir: np.ndarray, origin, maxdist=44.0):
    """For query points (world) with an outward direction index (0..5), find the nearest face whose normal
    points along that direction (dot >= 0.5). Returns (tex_id or -1, dist)."""
    nq = len(qpts)
    best_d = np.full(nq, np.inf)
    best_t = np.full(nq, -1, np.int64)
    dirv = np.array(DIRS, float)
    # bucket queries by 80-unit cells
    B = 80.0
    keys = np.floor(qpts / B).astype(np.int64)
    buckets = collections.defaultdict(list)
    for i, k in enumerate(map(tuple, keys)):
        buckets[k].append(i)
    buckets = {k: np.array(v) for k, v in buckets.items()}
    for fi, V in enumerate(faces.polys):
        n = faces.normals[fi]
        lo = np.floor((faces.mins[fi] - maxdist) / B).astype(int)
        hi = np.floor((faces.maxs[fi] + maxdist) / B).astype(int)
        cand = []
        for x in range(lo[0], hi[0] + 1):
            for y in range(lo[1], hi[1] + 1):
                for z in range(lo[2], hi[2] + 1):
                    b = buckets.get((x, y, z))
                    if b is not None:
                        cand.append(b)
        if not cand:
            continue
        idx = np.concatenate(cand)
        ok = (dirv[qdir[idx]] @ n) >= 0.5
        idx = idx[ok]
        if len(idx) == 0:
            continue
        Q = qpts[idx]
        inb = np.all((Q >= faces.mins[fi] - maxdist) & (Q <= faces.maxs[fi] + maxdist), axis=1)
        idx = idx[inb]
        if len(idx) == 0:
            continue
        d = point_poly_dist(qpts[idx], V, n, faces.dists[fi])
        better = (d < best_d[idx]) & (d <= maxdist)
        best_d[idx[better]] = d[better]
        best_t[idx[better]] = faces.tex[fi]
    return best_t, best_d


def assign_surface(solid_full: np.ndarray, kind: np.ndarray, faces: FaceSet, tmap: dict, origin, log=print):
    """solid_full: (sz, sy, sx) bool for cells holding a solid block (full or slab).
    kind: (sz,sy,sx) int8: 0 air, 1 full, 2 bottom slab, 3 top slab.
    Returns surface block name per exposed cell: dict index->(name), plus exposed mask."""
    sz, sy, sx = solid_full.shape
    ox, oy, oz = origin
    full = kind == 1
    # a face of a solid cell is exposed if the neighbour is not a full block
    exposed_dir = np.zeros((6,) + solid_full.shape, bool)
    for di, (dx, dy, dz) in enumerate(DIRS):
        nb = np.zeros_like(full)
        src = [slice(None)] * 3
        dst = [slice(None)] * 3
        for axis, d in ((2, dx), (1, dy), (0, dz)):
            if d == 1:
                dst[axis] = slice(0, -1)
                src[axis] = slice(1, None)
            elif d == -1:
                dst[axis] = slice(1, None)
                src[axis] = slice(0, -1)
        nb[tuple(dst)] = full[tuple(src)]
        # outside the grid: above = air (exposed), sides/below = solid terrain (not exposed)
        if dz == 1:
            nb[-1] = False
        elif dz == -1:
            nb[0] = True
        elif dx == 1:
            nb[:, :, -1] = True
        elif dx == -1:
            nb[:, :, 0] = True
        elif dy == 1:
            nb[:, -1, :] = True
        elif dy == -1:
            nb[:, 0, :] = True
        e = solid_full & ~nb
        # slabs: the slab's own open half faces up (bottom slab) / down (top slab)
        if dz == 1:
            e |= kind == 2
        if dz == -1:
            e |= kind == 3
        exposed_dir[di] = e
    exposed = exposed_dir.any(axis=0)
    # query points: centre of the exposed face (slab: centre of its solid half's open face)
    qs = []
    qd = []
    qc = []
    for di, (dx, dy, dz) in enumerate(DIRS):
        z, y, x = np.nonzero(exposed_dir[di])
        if len(z) == 0:
            continue
        cx = ox + (x + 0.5) * 40 + dx * 20
        cy = oy + (y + 0.5) * 40 + dy * 20
        k = kind[z, y, x]
        zc = oz + (z + 0.5) * 40
        zlo = np.where(k == 3, oz + z * 40 + 20, oz + z * 40)
        zhi = np.where(k == 2, oz + z * 40 + 20, oz + z * 40 + 40)
        if dz == 1:
            cz = zhi
        elif dz == -1:
            cz = zlo
        else:
            cz = (zlo + zhi) / 2
        qs.append(np.stack([cx, cy, cz], 1))
        qd.append(np.full(len(z), di))
        qc.append(np.stack([z, y, x], 1))
    Q = np.concatenate(qs)
    QD = np.concatenate(qd)
    QC = np.concatenate(qc)
    log(f"  surface queries: {len(Q)} exposed faces on {exposed.sum()} cells")
    tid, dist = match_surface_textures(faces, Q, QD, origin)
    log(f"  matched {np.mean(tid >= 0)*100:.1f}% of exposed faces to a BSP face")
    # per cell decision
    tex_block = {}
    for name, t in tmap["textures"].items():
        if not t.get("skip"):
            tex_block[name] = t["block"]
    cell_votes = collections.defaultdict(lambda: [None, collections.Counter(), None])
    for (z, y, x), d, t in zip(QC.tolist(), QD.tolist(), tid.tolist()):
        if t < 0:
            continue
        name = faces.texnames[t]
        v = cell_votes[(z, y, x)]
        if d == UP:
            v[0] = name
        elif d == DOWN:
            v[2] = name
        else:
            v[1][name] += 1
    surf_tex = {}
    for key, (up, sides, down) in cell_votes.items():
        if up is not None:
            surf_tex[key] = up
        elif sides:
            surf_tex[key] = sides.most_common(1)[0][0]
        elif down is not None:
            surf_tex[key] = down
    return exposed, exposed_dir, surf_tex, tex_block


def cell_shell_distance(solid: np.ndarray):
    """Chessboard-ish (6-connected BFS) distance in blocks from air, inside solid cells (air = 0)."""
    return ndimage.distance_transform_cdt(solid, metric="taxicab")
