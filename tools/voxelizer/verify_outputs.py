"""Independent consistency check of the produced files (mc_dust2.mcw / .json / .bsp).

    python tools\\voxelizer\\verify_outputs.py
Exit code 0 = all checks passed.
"""
from __future__ import annotations

import json
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import numpy as np

from bsp30 import Bsp30
from mcw_io import read_mcw
import walkability as walk

_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MAPS = os.path.join(os.environ.get("MINESTRIKE_GAME") or os.path.join(_ROOT, "game", "Half-Life"), "cstrike", "maps")
REGISTRY = os.path.join(_ROOT, "code", "shared", "mc_blocks.cpp")
SHAPES = {"SHAPE_NONE": 0, "SHAPE_CUBE": 1, "SHAPE_SLAB": 2, "SHAPE_STAIRS": 3, "SHAPE_DOOR": 4, "SHAPE_PANE": 5, "SHAPE_CROSS": 6,
          "SHAPE_DUST": 7, "SHAPE_TORCH": 8, "SHAPE_LEVER": 9, "SHAPE_BUTTON": 10, "SHAPE_PLATE": 11, "SHAPE_REPEATER": 12,
          "SHAPE_FIRE": 13, "SHAPE_TABLE": 14}


def main(log=print):
    problems = []
    src = open(REGISTRY, encoding="utf-8").read()
    reg = {n: SHAPES.get(s, 7) for n, s in re.findall(r'\{\s*"([a-z_0-9]+)",\s*(SHAPE_\w+)', src)}
    W = read_mcw(os.path.join(MAPS, "mc_dust2.mcw"))
    M = json.load(open(os.path.join(MAPS, "mc_dust2.json")))
    sx, sy, sz = W["size"]
    cells = W["cells"]
    pal = W["palette"]
    if pal[0] != "air":
        problems.append("palette[0] != air")
    for n in pal:
        if n not in reg:
            problems.append(f"palette name {n!r} not in mc_blocks.cpp")
    if list(W["size"]) != M["size"]:
        problems.append(f"size mismatch mcw {W['size']} json {M['size']}")
    if not np.allclose(W["origin"], M["origin"]):
        problems.append(f"origin mismatch {W['origin']} vs {M['origin']}")
    if W["block_size"] != 40.0 or M["blockSize"] != 40:
        problems.append("block size != 40")
    shape = np.array([reg.get(n, 0) for n in pal])
    p = cells & 0x3FF
    st = cells >> 10
    sh = shape[p]
    bad_state = (st != 0) & ~np.isin(sh, [2, 3, 4])
    if bad_state.any():
        problems.append(f"{int(bad_state.sum())} cells carry state bits on stateless shapes")
    if ((sh == 2) & (st > 1)).any():
        problems.append("slab with state bits other than bit0")
    if ((sh == 0) & (p != 0)).any():
        problems.append("SHAPE_NONE block other than air present")
    # bottom layer must be bedrock (solid floor of the world)
    bed = pal.index("bedrock") if "bedrock" in pal else -1
    if not np.all(p[0] == bed):
        problems.append("bottom layer is not entirely bedrock")
    # doors
    door_ids = [i for i, n in enumerate(pal) if reg.get(n) == 4]
    n_door_cells = int(np.isin(p, door_ids).sum())
    if n_door_cells != 2 * len(M["doors"]):
        problems.append(f"{n_door_cells} door cells but {len(M['doors'])} doors in metadata (expected 2 cells each)")
    for d in M["doors"]:
        bx, by, bz = d["lower"]
        lo = int(cells[bz, by, bx])
        up = int(cells[bz + 1, by, bx])
        if pal[lo & 0x3FF] != d["block"] or pal[up & 0x3FF] != d["block"]:
            problems.append(f"door at {d['lower']}: cells are {pal[lo & 0x3FF]}/{pal[up & 0x3FF]}")
            continue
        sl, su = lo >> 10, up >> 10
        want = d["facing"] | (4 if d.get("open") else 0) | (16 if d["hinge_right"] else 0)
        if sl != want or su != (want | 8):
            problems.append(f"door at {d['lower']}: states {sl}/{su}, expected {want}/{want | 8}")
        below = int(cells[bz - 1, by, bx])
        if shape[below & 0x3FF] not in (1,) and not (shape[below & 0x3FF] == 2 and (below >> 10) & 1):
            problems.append(f"door at {d['lower']} not standing on a full block / top slab")
    # spawns + surface samples: hull free and supported
    origin = W["origin"]
    shape_of_pal = shape
    mins = np.array([-16.0, -16.0, -36.0])
    maxs = np.array([16.0, 16.0, 36.0])
    pts = [("ct", s) for s in M["spawns"]["ct"]] + [("t", s) for s in M["spawns"]["t"]]
    pts += [("sample:" + k, v + [0]) for k, v in M["surfaceSamples"].items()]
    for name, s in pts:
        o = np.array(s[:3], float)
        if walk.box_hits_world(cells, shape_of_pal, origin, o + mins, o + maxs):
            problems.append(f"{name} {s[:3]} intersects voxels")
        o2 = o - [0, 0, 2]
        if not walk.box_hits_world(cells, shape_of_pal, origin, o2 + mins, o2 + maxs):
            problems.append(f"{name} {s[:3]} is not standing on voxels (falls)")
    vmin = np.array(origin)
    vmax = vmin + np.array([sx, sy, sz]) * 40.0
    for b in M["bombsites"] + M["buyzones"]:
        if np.any(np.array(b["mins"]) < vmin) or np.any(np.array(b["maxs"]) > vmax):
            problems.append(f"trigger volume {b} outside the voxel volume")
    # shell BSP
    bsp_path = os.path.join(MAPS, "mc_dust2.bsp")
    if os.path.exists(bsp_path):
        b = Bsp30(bsp_path)
        ents = b.entities
        ws = ents[0]
        if ws.get("classname") != "worldspawn" or not ws.get("skyname") or not ws.get("wad"):
            problems.append("shell worldspawn missing skyname/wad")
        ct = [e for e in ents if e.get("classname") == "info_player_start"]
        tt = [e for e in ents if e.get("classname") == "info_player_deathmatch"]
        for lst, ms, nm in ((ct, M["spawns"]["ct"], "ct"), (tt, M["spawns"]["t"], "t")):
            if len(lst) != len(ms):
                problems.append(f"shell {nm} spawn count {len(lst)} != metadata {len(ms)}")
                continue
            for e, s in zip(lst, ms):
                xyz = [float(v) for v in e["origin"].split()]
                yaw = float(e["angles"].split()[1])
                if not np.allclose(xyz, s[:3]) or abs(yaw - s[3]) > 1e-3:
                    problems.append(f"shell {nm} spawn {xyz}/{yaw} != metadata {s}")
        trig = [e for e in ents if e.get("classname") in ("func_bomb_target", "func_buyzone")]
        if len(trig) != 4:
            problems.append(f"shell has {len(trig)} trigger entities, expected 4")
        vols = [(bb["mins"], bb["maxs"]) for bb in M["bombsites"]] + [(bb["mins"], bb["maxs"]) for bb in M["buyzones"]]
        for e in trig:
            mi = int(e["model"][1:])
            mn = b.models["mins"][mi].tolist()
            mx = b.models["maxs"][mi].tolist()
            if not any(np.allclose(mn, v[0], atol=1.01) and np.allclose(mx, v[1], atol=1.01) for v in vols):
                problems.append(f"shell trigger {e['classname']} bounds {mn}..{mx} match no metadata volume")
            c = b.point_contents(np.array([(np.array(mn) + np.array(mx)) / 2]), mi, 0)[0]
            if c != -2:
                problems.append(f"shell trigger {e['classname']} model hull 0 not solid at its centre ({c})")
        wmin = b.models["mins"][0]
        wmax = b.models["maxs"][0]
        if np.any(wmin > vmin) or np.any(wmax < vmax):
            problems.append("shell world does not enclose the voxel volume")
        P = np.array([s[:3] for s in M["spawns"]["ct"] + M["spawns"]["t"]], float)
        for hull in (0, 1, 3):
            if np.any(b.point_contents(P, 0, hull) != -1):
                problems.append(f"a spawn is not in empty space of shell hull {hull}")
    else:
        problems.append("mc_dust2.bsp missing")
    for pr in problems:
        log("  PROBLEM: " + pr)
    log(f"  verify_outputs: {'ALL CHECKS PASSED' if not problems else str(len(problems)) + ' problem(s)'} "
        f"({len(M['doors'])} doors, {len(pts)} standing points, palette {len(pal)})")
    return problems


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
