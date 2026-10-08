"""Doors.

de_dust2 has no func_door / func_door_rotating. Its doors are world brushes:
  * open double doors: thin (8u) free-standing leaves, hinged at the door jambs and swung ~25 deg open,
    textured SandWllDoor* (mid doors, B doors, lower-tunnel doors).
  * closed / painted doors: SandWllDoor* textures on ordinary thick walls (decorative).
We carve the thin leaves out of the voxelization and turn every open double-door opening into a
Minecraft door set (spruce_door lower+upper halves, mirrored hinges, placed OPEN like in dust2), with the
opening above two blocks closed by planks. Painted doors just get a wooden wall material (texture map).
"""
from __future__ import annotations

import numpy as np

from bsp30 import Bsp30, CONTENTS_SOLID

DOOR_TEX_PREFIX = "sandwlldoor"


def find_door_leaves(bsp: Bsp30, log=print):
    """Return list of leaves: dict(center, axes(3x3), half(3), hinge(xy), tip(xy), zmin, zmax, normal)."""
    leaves = []
    for f in bsp.model_faces(0):
        name = bsp.face_texture_name(f).lower()
        if not name.startswith(DOOR_TEX_PREFIX):
            continue
        n = bsp.face_normal(f)
        if abs(n[2]) > 0.1:
            continue
        if min(abs(n[0]), abs(n[1])) < 0.2:  # axis aligned -> painted on a wall
            continue
        v = bsp.face_vertices(f)
        t = np.array([-n[1], n[0], 0.0])
        t /= np.linalg.norm(t)
        ts = v @ t
        if ts.max() - ts.min() < 24:  # narrow end face of a leaf
            continue
        # probe thickness behind the face
        c = v.mean(axis=0)
        depth = None
        for d in np.arange(0.5, 24.0, 0.5):
            p = c - n * d
            if bsp.point_contents(p[None])[0] != CONTENTS_SOLID:
                depth = d
                break
        if depth is None or depth > 16:
            continue
        nd = v @ n
        plane = nd.mean()
        zmin, zmax = v[:, 2].min(), v[:, 2].max()
        axes = np.array([n, t, [0.0, 0.0, 1.0]])
        cen_n = plane - depth / 2
        cen_t = (ts.min() + ts.max()) / 2
        cen_z = (zmin + zmax) / 2
        center = n * cen_n + t * cen_t + np.array([0, 0, 1.0]) * cen_z
        center[2] = cen_z
        # recompute exact xy of center: n,t are horizontal so z is independent
        half = np.array([depth / 2 + 1.0, (ts.max() - ts.min()) / 2 + 1.0, (zmax - zmin) / 2])
        # endpoints of the leaf centre line
        e0 = center + t * (ts.min() - cen_t)
        e1 = center + t * (ts.max() - cen_t)
        # hinge = the end that touches solid (the jamb)
        def solid_near(p):
            q = np.array([[p[0], p[1], cen_z]]) + np.array([[t[0] * s, t[1] * s, 0] for s in (-6, 6)])
            return (bsp.point_contents(q) == CONTENTS_SOLID).sum()
        # probe just beyond each end along the leaf direction
        b0 = e0 - t * 6
        b1 = e1 + t * 6
        s0 = bsp.point_contents(np.array([[b0[0], b0[1], cen_z]]))[0] == CONTENTS_SOLID
        s1 = bsp.point_contents(np.array([[b1[0], b1[1], cen_z]]))[0] == CONTENTS_SOLID
        hinge, tip = (e0, e1) if (s0 and not s1) else (e1, e0) if (s1 and not s0) else (None, None)
        leaves.append(dict(face=f, tex=name, center=center, axes=axes, half=half, normal=n,
                           hinge=None if hinge is None else hinge[:2], tip=None if tip is None else tip[:2],
                           zmin=float(zmin), zmax=float(zmax)))
    # merge leaves that are the two faces of one brush (same hinge within 16 units)
    merged = []
    for lf in leaves:
        dup = False
        for m in merged:
            if np.linalg.norm(lf["center"][:2] - m["center"][:2]) < 16 and abs(abs(lf["normal"] @ m["normal"]) - 1) < 0.01:
                dup = True
                if m["hinge"] is None and lf["hinge"] is not None:
                    m["hinge"], m["tip"] = lf["hinge"], lf["tip"]
                break
        if not dup:
            merged.append(lf)
    log(f"  door leaves: {len(merged)} (from {len(leaves)} faces)")
    return leaves, merged


def group_doorways(leaves, log=print):
    """Pair leaves into double doors: two leaves whose hinges are 120..260 units apart on one axis line."""
    used = set()
    ways = []
    for i, a in enumerate(leaves):
        if i in used or a["hinge"] is None:
            continue
        best = None
        for j, b in enumerate(leaves):
            if j <= i or j in used or b["hinge"] is None:
                continue
            d = b["hinge"] - a["hinge"]
            dist = np.linalg.norm(d)
            if 100 < dist < 300 and abs(a["zmin"] - b["zmin"]) < 8:
                ax = int(np.argmax(np.abs(d)))
                if abs(d[1 - ax]) < 16:
                    if best is None or dist < best[1]:
                        best = (j, dist, ax)
        if best is None:
            continue
        j, dist, ax = best
        b = leaves[j]
        used |= {i, j}
        h0, h1 = (a, b) if a["hinge"][ax] < b["hinge"][ax] else (b, a)
        plane = (a["hinge"][1 - ax] + b["hinge"][1 - ax]) / 2
        # side the leaves swing to (sign along the perpendicular axis) for each leaf
        sw0 = np.sign(h0["tip"][1 - ax] - h0["hinge"][1 - ax])
        sw1 = np.sign(h1["tip"][1 - ax] - h1["hinge"][1 - ax])
        ways.append(dict(axis=ax, plane=float(plane), lo=float(h0["hinge"][ax]), hi=float(h1["hinge"][ax]),
                         zmin=min(a["zmin"], b["zmin"]), zmax=max(a["zmax"], b["zmax"]),
                         swing=(float(sw0), float(sw1)), textures=(h0["tex"], h1["tex"])))
    for w in ways:
        log(f"  doorway: along {'xy'[w['axis']]} {w['lo']:.0f}..{w['hi']:.0f} plane {'yx'[w['axis']]}={w['plane']:.0f} "
            f"z {w['zmin']:.0f}..{w['zmax']:.0f}")
    return ways


def carve_boxes(face_leaves):
    return [(lf["center"], lf["axes"], lf["half"]) for lf in face_leaves]


def place_doors(ways, air_blocks, not_full, origin, log=print):
    """Decide door cells for each doorway.

    air_blocks(bx,by,bz)->bool tells whether a block cell is air (both halves); not_full(bx,by,bz) whether it
    is not a full solid block (air or slab) - the opening above the doors is closed up to the doorway top.
    Returns (doors, fills): doors = list of dict(lower=[bx,by,bz], facing, hinge_right, open, way);
    fills = list of (bx,by,bz) cells to set to planks (doorway above the doors / beside them).
    """
    ox, oy, oz = origin
    doors = []
    fills = []
    for wi, w in enumerate(ways):
        ax = w["axis"]  # doorway runs along this axis (0=x, 1=y)
        perp = 1 - ax
        o_ax = (ox, oy)[ax]
        o_pp = (ox, oy)[perp]
        # door row: the cell row (perpendicular index) containing the doorway plane
        p_rel = (w["plane"] - o_pp) / 40.0
        prow = int(np.floor(p_rel))
        frac = p_rel - prow
        # closed panel side: towards the plane inside the cell
        # facing codes: 0=+X,1=+Y,2=-X,3=-Y
        pos_side = 0 if perp == 0 else 1
        neg_side = 2 if perp == 0 else 3
        if frac < 0.02:  # plane on a boundary: use the cell on the + side, panel at its - side
            facing = neg_side
        elif frac > 0.98:
            facing = pos_side
        else:
            facing = pos_side if frac >= 0.5 else neg_side
        bz = int(np.floor((w["zmin"] + 1 - oz) / 40.0))
        if (w["zmin"] - oz) / 40.0 - bz > 0.5:
            bz += 1
        # floor of the doorway in voxel terms: first air block at/above the door floor
        lo = int(np.floor((w["lo"] - o_ax) / 40.0))
        hi = int(np.floor((w["hi"] - o_ax) / 40.0))

        def cell(a, p, z):
            return (a, p, z) if ax == 0 else (p, a, z)

        # find floor level: lowest z near bz where the middle of the opening is air
        mid = (lo + hi) // 2
        zf = None
        for dz in (0, 1, -1, 2):
            if air_blocks(*cell(mid, prow, bz + dz)) and not air_blocks(*cell(mid, prow, bz + dz - 1)):
                zf = bz + dz
                break
        if zf is None:
            zf = bz
        # opening: contiguous air cells (both door levels) along the axis around mid
        cells = [a for a in range(lo - 1, hi + 2) if air_blocks(*cell(a, prow, zf)) and air_blocks(*cell(a, prow, zf + 1))]
        # keep the contiguous run containing mid
        run = []
        for a in range(mid, lo - 3, -1):
            if a in cells:
                run.insert(0, a)
            else:
                break
        for a in range(mid + 1, hi + 3):
            if a in cells:
                run.append(a)
            else:
                break
        if not run:
            log(f"  doorway {wi}: no opening found")
            continue
        n = len(run)
        # door positions: double doors from both ends inward, an odd middle cell becomes filler
        door_cells = []
        k = 0
        left = run[:]
        while len(left) >= 2:
            door_cells.append((left[0], "lo"))
            door_cells.append((left[-1], "hi"))
            left = left[1:-1]
            k += 1
            if len(left) >= 2:
                continue
        filler = left[:]  # 0 or 1 cell
        # hinge rule: door at the low end of a pair is hinged on its low side (-axis), the high end on +axis
        lo_dir = 2 if ax == 0 else 3  # -X or -Y
        hi_dir = 0 if ax == 0 else 1  # +X or +Y
        # pairs: consecutive in the sorted list form double doors meeting in the middle
        door_cells.sort()
        m = len(door_cells)
        for idx, (a, _) in enumerate(door_cells):
            # within each adjacent pair, first hinges low, second hinges high
            pair_first = (idx % 2 == 0)
            hinge_dir = lo_dir if pair_first else hi_dir
            # (f + (hr ? 3 : 1)) % 4 == hinge_dir
            hr = ((facing + 3) % 4) == hinge_dir
            if not hr:
                assert ((facing + 1) % 4) == hinge_dir
            bx, by, _ = cell(a, prow, zf)
            doors.append(dict(lower=[int(bx), int(by), int(zf)], facing=int(facing), hinge_right=bool(hr),
                              open=True, way=wi))
        for a in filler:
            fills.append(cell(a, prow, zf))
            fills.append(cell(a, prow, zf + 1))
        # close the opening above the doors up to the doorway top
        ztop = int(np.ceil((w["zmax"] - oz) / 40.0 - 0.25))
        for a in run:
            for z in range(zf + 2, ztop):
                if not_full(*cell(a, prow, z)):
                    fills.append(cell(a, prow, z))
        log(f"  doorway {wi}: row {'yx'[ax]}={prow} cells {run[0]}..{run[-1]} floor bz={zf} -> "
            f"{len(door_cells)} doors, {len(filler)} filler column(s), lintel up to bz<{ztop}")
    return doors, fills
