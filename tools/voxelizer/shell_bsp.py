"""Shell BSP for mc_dust2: a sealed sky box around the voxel volume + spawn/trigger entities, compiled with
SDHLT (sdHLCSG/sdHLBSP/sdHLVIS/sdHLRAD x64) and validated by re-parsing."""
from __future__ import annotations

import os
import shutil
import subprocess
import time

import numpy as np

from bsp30 import Bsp30

# SDHLT v1.3.0 (https://github.com/seedee/SDHLT/releases), unpacked under tools/sdhlt/v130; or MINESTRIKE_SDHLT
SDHLT_DIR = os.environ.get("MINESTRIKE_SDHLT") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "sdhlt", "v130",
                                                                "sdhlt-v1.3.0", "tools", "Win64")
WORLD_LIMIT = 4096  # keep every brush inside +-4096 (classic GoldSrc map bounds)


def _face(n, c, tex):
    n = np.array(n, float)
    c = np.array(c, float)
    # tangents u, v with u x v = n (outward normal)
    if abs(n[2]) > 0.5:
        u = np.array([1.0, 0, 0])
        v = np.cross(n, u)
        U, V = (1, 0, 0), (0, -1, 0)
    elif abs(n[0]) > 0.5:
        u = np.array([0, 1.0, 0])
        v = np.cross(n, u)
        U, V = (0, 1, 0), (0, 0, -1)
    else:
        u = np.array([1.0, 0, 0])
        v = np.cross(n, u)
        U, V = (1, 0, 0), (0, 0, -1)
    u, v = u * 64, v * 64
    assert np.allclose(np.cross(u, v) / 4096.0, n)
    p1 = c
    p0 = c + u
    p2 = c + v

    def fmt(p):
        return "( " + " ".join(f"{int(round(x))}" for x in p) + " )"

    return (f"{fmt(p0)} {fmt(p1)} {fmt(p2)} {tex} [ {U[0]} {U[1]} {U[2]} 0 ] [ {V[0]} {V[1]} {V[2]} 0 ] 0 1 1")


def box_brush(mins, maxs, tex):
    mn = np.array(mins, float)
    mx = np.array(maxs, float)
    assert np.all(mx > mn)
    lines = ["{"]
    lines.append(_face((1, 0, 0), (mx[0], mn[1], mn[2]), tex))
    lines.append(_face((-1, 0, 0), (mn[0], mn[1], mn[2]), tex))
    lines.append(_face((0, 1, 0), (mn[0], mx[1], mn[2]), tex))
    lines.append(_face((0, -1, 0), (mn[0], mn[1], mn[2]), tex))
    lines.append(_face((0, 0, 1), (mn[0], mn[1], mx[2]), tex))
    lines.append(_face((0, 0, -1), (mn[0], mn[1], mn[2]), tex))
    lines.append("}")
    return "\n".join(lines)


def ent(kv: dict, brushes=()):
    out = ["{"]
    for k, v in kv.items():
        out.append(f'"{k}" "{v}"')
    out.extend(brushes)
    out.append("}")
    return "\n".join(out)


def write_map(path, vox_min, vox_max, spawns_ct, spawns_t, bombsites, buyzones, wad, skyname, extra_ents=(),
              margin=256, top_margin=1024, floor_gap=16, wall=32, log=print):
    vmin = np.array(vox_min, float)
    vmax = np.array(vox_max, float)
    inner_min = vmin - margin
    inner_max = vmax + margin
    inner_min[2] = vmin[2] - floor_gap
    inner_max[2] = vmax[2] + top_margin
    clamped = []
    for i in range(3):
        if inner_min[i] - wall < -WORLD_LIMIT:
            inner_min[i] = -WORLD_LIMIT + wall
            clamped.append(f"{'xyz'[i]}min")
        if inner_max[i] + wall > WORLD_LIMIT:
            inner_max[i] = WORLD_LIMIT - wall
            clamped.append(f"{'xyz'[i]}max")
    inner_min = np.floor(inner_min)
    inner_max = np.ceil(inner_max)
    lo, hi = inner_min, inner_max
    W = wall
    brushes = []
    # floor: normal solid brush
    brushes.append(box_brush((lo[0] - W, lo[1] - W, lo[2] - W), (hi[0] + W, hi[1] + W, lo[2]), "out_dirt1"))
    # sky walls + ceiling
    brushes.append(box_brush((lo[0] - W, lo[1] - W, hi[2]), (hi[0] + W, hi[1] + W, hi[2] + W), "sky"))
    brushes.append(box_brush((lo[0] - W, lo[1] - W, lo[2]), (lo[0], hi[1] + W, hi[2]), "sky"))
    brushes.append(box_brush((hi[0], lo[1] - W, lo[2]), (hi[0] + W, hi[1] + W, hi[2]), "sky"))
    brushes.append(box_brush((lo[0], lo[1] - W, lo[2]), (hi[0], lo[1], hi[2]), "sky"))
    brushes.append(box_brush((lo[0], hi[1], lo[2]), (hi[0], hi[1] + W, hi[2]), "sky"))
    world = ent({"classname": "worldspawn", "mapversion": "220", "wad": wad, "skyname": skyname,
                 "MaxRange": "8192", "message": "mc_dust2 (voxel de_dust2 shell)"}, brushes)
    parts = [world]
    parts.append(ent({"classname": "light_environment", "origin": f"{int((lo[0]+hi[0])/2)} {int((lo[1]+hi[1])/2)} {int(hi[2]-64)}",
                      "angles": "0 43 0", "pitch": "-60", "_light": "255 245 220 300", "_diffuse_light": "190 210 255 110"}))
    for (x, y, z, yaw) in spawns_ct:
        parts.append(ent({"classname": "info_player_start", "origin": f"{x:g} {y:g} {z:g}", "angles": f"0 {yaw:g} 0"}))
    for (x, y, z, yaw) in spawns_t:
        parts.append(ent({"classname": "info_player_deathmatch", "origin": f"{x:g} {y:g} {z:g}", "angles": f"0 {yaw:g} 0"}))
    for b in bombsites:
        parts.append(ent({"classname": "func_bomb_target"}, [box_brush(b["mins"], b["maxs"], "AAATRIGGER")]))
    for b in buyzones:
        team = 2 if b["team"] == "ct" else 1
        parts.append(ent({"classname": "func_buyzone", "team": str(team)}, [box_brush(b["mins"], b["maxs"], "AAATRIGGER")]))
    for e in extra_ents:
        parts.append(ent(e))
    with open(path, "w", newline="\n") as f:
        f.write("// mc_dust2 shell map, generated by tools/voxelizer/shell_bsp.py\n")
        f.write("// Format: Valve220\n")
        f.write("\n".join(parts) + "\n")
    log(f"  map written: box inner {lo.tolist()} .. {hi.tolist()} (clamped: {clamped or 'none'})")
    return lo, hi, clamped


def compile_map(map_path, log=print):
    d = os.path.dirname(map_path)
    base = os.path.splitext(map_path)[0]
    steps = [
        ("sdHLCSG_x64.exe", []),
        ("sdHLBSP_x64.exe", []),
        ("sdHLVIS_x64.exe", ["-fast"]),
        ("sdHLRAD_x64.exe", ["-fast", "-bounce", "1"]),
    ]
    results = []
    for exe, args in steps:
        t0 = time.time()
        cmd = [os.path.join(SDHLT_DIR, exe)] + args + [base]
        p = subprocess.run(cmd, cwd=d, capture_output=True, text=True, errors="replace",
                           env={**os.environ, "TEMP": d, "TMP": d})
        out = (p.stdout or "") + (p.stderr or "")
        with open(base + f".{exe.split('_')[0]}.out.txt", "w", encoding="utf-8") as f:
            f.write(out)
        warn = [l for l in out.splitlines() if "warning" in l.lower() or "error" in l.lower()]
        results.append(dict(tool=exe, args=args, rc=p.returncode, seconds=round(time.time() - t0, 1), warnings=warn[:20]))
        log(f"  {exe} {' '.join(args)}: rc={p.returncode} in {time.time()-t0:.1f}s, {len(warn)} warning/error lines")
        for w in warn[:8]:
            log(f"      {w.strip()}")
        if p.returncode != 0:
            raise RuntimeError(f"{exe} failed (rc={p.returncode}); see {base}.{exe}.out.txt")
    return results


def validate_bsp(path, expect: dict, log=print):
    b = Bsp30(path)
    problems = []
    ents = b.entities
    cls = [e.get("classname") for e in ents]
    if cls[0] != "worldspawn":
        problems.append("first entity is not worldspawn")
    ws = ents[0]
    for k in ("skyname", "wad"):
        if k not in ws:
            problems.append(f"worldspawn missing {k}")
    n_ct = cls.count("info_player_start")
    n_t = cls.count("info_player_deathmatch")
    if n_ct != expect["ct"] or n_t != expect["t"]:
        problems.append(f"spawn counts ct={n_ct} t={n_t}, expected {expect['ct']}/{expect['t']}")
    if cls.count("func_bomb_target") != 2 or cls.count("func_buyzone") != 2:
        problems.append("expected 2 func_bomb_target + 2 func_buyzone")
    if cls.count("light_environment") != 1:
        problems.append("expected 1 light_environment")
    nmodels = len(b.models)
    if nmodels != 1 + 4:
        problems.append(f"model count {nmodels}, expected 5 (world + 4 triggers)")
    for e in ents:
        if e.get("classname") in ("func_bomb_target", "func_buyzone"):
            m = e.get("model", "")
            if not m.startswith("*") or int(m[1:]) >= nmodels:
                problems.append(f"{e['classname']} has bad model {m!r}")
    wmin = b.models["mins"][0]
    wmax = b.models["maxs"][0]
    vmin = np.array(expect["vox_min"])
    vmax = np.array(expect["vox_max"])
    if np.any(wmin > vmin) or np.any(wmax < vmax):
        problems.append(f"world bounds {wmin}..{wmax} do not enclose the voxel volume {vmin}..{vmax}")
    # point contents: inside empty, spawns empty in all hulls, sky above, solid below floor
    probe = []
    for (x, y, z, _) in expect["spawns"]:
        probe.append((x, y, z))
    P = np.array(probe, float)
    for hull in (0, 1):
        c = b.point_contents(P, 0, hull)
        if np.any(c != -1):
            problems.append(f"some spawn points are not empty in hull {hull}: {np.unique(c)}")
    centre = (vmin + vmax) / 2
    c_mid = b.point_contents(np.array([centre]), 0, 0)[0]
    c_floor = b.point_contents(np.array([[centre[0], centre[1], vmin[2] - 24]]), 0, 0)[0]
    if c_mid != -1:
        problems.append(f"centre of voxel volume not empty ({c_mid})")
    if c_floor != -2:
        problems.append(f"floor brush below voxel volume not solid ({c_floor})")
    leaf_c = set(b.leaf_contents.tolist())
    if -6 not in leaf_c:
        problems.append("no sky leafs")
    texnames = sorted({mt.name.lower() for mt in b.miptex})
    lighting = b.lumps["lighting"][1]
    info = dict(models=nmodels, entities=len(ents), world_mins=wmin.tolist(), world_maxs=wmax.tolist(),
                leafs=len(b.leaf_contents), nodes=len(b.node_plane), clipnodes=len(b.clip_plane),
                faces=len(b.face_plane), textures=texnames, lighting_bytes=lighting,
                classnames={c: cls.count(c) for c in sorted(set(cls))}, problems=problems,
                size_bytes=os.path.getsize(path))
    log(f"  shell BSP validation: {'OK' if not problems else 'PROBLEMS: ' + '; '.join(problems)}")
    return info
