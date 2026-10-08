"""Convert Counter-Strike de_dust2 into a Minecraft voxel world (mc_dust2.mcw + mc_dust2.json) and build the
minimal shell BSP (mc_dust2.bsp), with previews + REPORT.md in Z:\\dev\\scratch\\csminecraft\\voxelizer.

    python tools\\voxelizer\\build_world.py              # regenerate everything
    python tools\\voxelizer\\build_world.py --reuse-origin --no-shell   # faster iteration
"""
from __future__ import annotations

import argparse
import collections
import json
import os
import shutil
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np
from scipy import ndimage

import classify
import doors as doormod
import finegrid
import materials as matmod
import mcw_io
import origin_search
import previews
import shell_bsp
import walkability as walk
import watertight
from bsp30 import Bsp30, load_wads

GAME = r"Z:\dev\CSminecraft\game\Half-Life"
BSP_IN = os.path.join(GAME, "cstrike", "maps", "de_dust2.bsp")
OUT_DIR = os.path.join(GAME, "cstrike", "maps")
OUT_MCW = os.path.join(OUT_DIR, "mc_dust2.mcw")
OUT_JSON = os.path.join(OUT_DIR, "mc_dust2.json")
OUT_BSP = os.path.join(OUT_DIR, "mc_dust2.bsp")
REGISTRY = r"Z:\dev\CSminecraft\code\shared\mc_blocks.cpp"
TEXMAP = os.path.join(HERE, "texture_map.json")
SCRATCH = r"Z:\dev\scratch\csminecraft\voxelizer"

BLOCK = 40.0
THRESH = 0.5            # half cell solid if >= this fraction of its samples is solid (+ watertight pass)
DEPTH_BELOW = 10        # blocks of terrain below the lowest dust2 floor
HEADROOM = 8            # blocks of air above the highest sky ceiling
SKY_PROP_MIN_Z = 300.0  # only the main sky ceilings (352/384/512) spread to sky-less columns (flatter plateau)


class Log:
    def __init__(self, path):
        self.f = open(path, "w", encoding="utf-8")
        self.lines = []

    def __call__(self, msg=""):
        print(msg, flush=True)
        self.f.write(msg + "\n")
        self.f.flush()
        self.lines.append(msg)


def parse_registry_full(path):
    import re
    src = open(path, encoding="utf-8").read()
    rows = re.findall(r'\{\s*"([a-z_0-9]+)",\s*(SHAPE_\w+),\s*(nullptr|"[a-z_0-9]+"),\s*(nullptr|"[a-z_0-9]+"),\s*(nullptr|"[a-z_0-9]+")', src)
    shapes = {"SHAPE_NONE": 0, "SHAPE_CUBE": 1, "SHAPE_SLAB": 2, "SHAPE_STAIRS": 3, "SHAPE_DOOR": 4, "SHAPE_PANE": 5, "SHAPE_CROSS": 6}
    defs = {}
    for n, s, t, si, b in rows:
        un = lambda x: None if x == "nullptr" else x.strip('"')
        defs[n] = (shapes[s], un(t), un(si), un(b))
    return defs


def write_texture_map(path, tmap):
    """Pretty JSON with one line per texture / block / ore entry (stays hand-editable)."""
    lines = ["{"]
    keys = list(tmap.keys())
    for i, k in enumerate(keys):
        v = tmap[k]
        comma = "," if i < len(keys) - 1 else ""
        if k in ("textures", "blocks", "terrain", "decor") and isinstance(v, dict):
            lines.append(f"  {json.dumps(k)}: {{")
            sub = list(v.items())
            w = max(len(json.dumps(n)) for n, _ in sub) + 1
            for j, (n, e) in enumerate(sub):
                c2 = "," if j < len(sub) - 1 else ""
                if isinstance(e, list) and e and isinstance(e[0], dict):
                    lines.append(f"    {json.dumps(n)}: [")
                    for q, o in enumerate(e):
                        lines.append(f"      {json.dumps(o)}{',' if q < len(e) - 1 else ''}")
                    lines.append(f"    ]{c2}")
                else:
                    lines.append(f"    {(json.dumps(n) + ':').ljust(w)} {json.dumps(e)}{c2}")
            lines.append(f"  }}{comma}")
        elif k == "_comment" and isinstance(v, list):
            lines.append(f"  {json.dumps(k)}: [")
            for q, c in enumerate(v):
                lines.append(f"    {json.dumps(c)}{',' if q < len(v) - 1 else ''}")
            lines.append(f"  ]{comma}")
        else:
            lines.append(f"  {json.dumps(k)}: {json.dumps(v)}{comma}")
    lines.append("}")
    text = "\n".join(lines) + "\n"
    json.loads(text)  # sanity
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def value_noise2d(sy, sx, cell, rng):
    gy = sy // cell + 2
    gx = sx // cell + 2
    g = rng.random((gy, gx))
    yy, xx = np.mgrid[0:sy, 0:sx] / float(cell)
    y0 = np.floor(yy).astype(int)
    x0 = np.floor(xx).astype(int)
    fy = yy - y0
    fx = xx - x0
    fy = fy * fy * (3 - 2 * fy)
    fx = fx * fx * (3 - 2 * fx)
    a = g[y0, x0] * (1 - fx) + g[y0, x0 + 1] * fx
    b = g[y0 + 1, x0] * (1 - fx) + g[y0 + 1, x0 + 1] * fx
    return a * (1 - fy) + b * fy


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--reuse-origin", action="store_true", help="reuse scratch origin_search.json")
    ap.add_argument("--no-shell", action="store_true", help="skip the shell BSP compile")
    args = ap.parse_args()
    os.makedirs(SCRATCH, exist_ok=True)
    log = Log(os.path.join(SCRATCH, "build_log.txt"))
    T = collections.OrderedDict()
    t_all = time.time()

    def stage(name, t0):
        T[name] = round(time.time() - t0, 1)

    # ------------------------------------------------------------------ inputs
    t0 = time.time()
    bsp = Bsp30(BSP_IN)
    blockdefs = parse_registry_full(REGISTRY)
    registry = list(blockdefs.keys())
    tmap = matmod.load_texture_map(TEXMAP)
    used_names = set()
    for t in tmap["textures"].values():
        if not t.get("skip"):
            used_names.add(t["block"])
            if t.get("slab"):
                used_names.add(t["slab"])
    for b, d in tmap["blocks"].items():
        used_names |= {b, d["slab"], d["subsoil"]}
    used_names |= {tmap["default_top"], tmap["default_side"], tmap["default_bottom"], tmap["door_block"],
                   tmap["door_frame_fill"], tmap["unsupported_sand_replacement"], "dead_bush"}
    tr = tmap["terrain"]
    used_names |= {tr["deep"], tr["deepslate"], tr["bedrock"]}
    for o in tr["ores"]:
        used_names.add(o["block"])
        if o.get("deepslate_variant"):
            used_names.add(o["deepslate_variant"])
    bad = sorted(n for n in used_names if n not in blockdefs)
    if bad:
        raise SystemExit(f"texture_map.json uses blocks not in mc_blocks.cpp: {bad}")
    log(f"de_dust2: {len(bsp.models)} models, {len(bsp.entities)} entities, {len(bsp.face_plane)} faces; "
        f"registry {len(registry)} blocks")
    # texture average colours -> json (informational)
    wadtex = load_wads(bsp, [os.path.join(GAME, "valve"), os.path.join(GAME, "cstrike")])
    for name, t in tmap["textures"].items():
        mt = wadtex.get(name)
        if mt is not None:
            t["avg_rgb"] = [int(v) for v in mt.average_colour()]
    write_texture_map(TEXMAP, tmap)

    # ------------------------------------------------------------------ brush entities
    brush_ents = []
    for e in bsp.entities:
        m = e.get("model", "")
        if m.startswith("*"):
            mi = int(m[1:])
            brush_ents.append(dict(classname=e["classname"], model=mi, mins=bsp.models["mins"][mi].tolist(),
                                   maxs=bsp.models["maxs"][mi].tolist(), keys={k: v for k, v in e.items() if k not in ("model", "classname")}))
    SOLID_CLASSES = {"func_breakable", "func_wall", "func_door", "func_door_rotating", "func_pushable"}
    solid_models = [b["model"] for b in brush_ents if b["classname"] in SOLID_CLASSES]
    handling = {
        "func_breakable": "solid crate (voxelized with the world; texture -> block via texture_map)",
        "func_illusionary": "skipped (non-solid lamps / decorative wall pieces)",
        "func_bomb_target": "metadata bombsite + shell BSP trigger",
        "func_buyzone": "metadata buyzone + shell BSP trigger",
        "func_wall": "solid", "func_door": "Minecraft door", "func_door_rotating": "Minecraft door",
    }
    log(f"brush entities: {collections.Counter(b['classname'] for b in brush_ents)}")

    # ------------------------------------------------------------------ doors (world-brush leaves)
    face_leaves, leaves = doormod.find_door_leaves(bsp, log)
    ways = doormod.group_doorways(leaves, log)
    carve = doormod.carve_boxes(face_leaves)
    model = finegrid.ContentsModel(bsp, solid_models, carve)
    stage("load", t0)

    # ------------------------------------------------------------------ origin search
    t0 = time.time()
    cache = os.path.join(SCRATCH, "origin_search.json")
    if args.reuse_origin and os.path.exists(cache):
        osr = json.load(open(cache))
        log(f"origin search: reused {cache}")
    else:
        log("origin search:")
        osr = origin_search.search(bsp, lambda P: np.where(model.codes(P) == 1, -2, -1), log)
        json.dump(osr, open(cache, "w"), indent=1)
    xoff, yoff = osr["xy_offset"]
    zoff = osr["z_offset"]
    stage("origin_search", t0)

    wmin = bsp.models["mins"][0].astype(float)
    wmax = bsp.models["maxs"][0].astype(float)
    ox = np.floor((wmin[0] - xoff) / BLOCK) * BLOCK + xoff
    oy = np.floor((wmin[1] - yoff) / BLOCK) * BLOCK + yoff
    sx = int(np.ceil((wmax[0] - ox) / BLOCK))
    sy = int(np.ceil((wmax[1] - oy) / BLOCK))
    floors = {int(k): v for k, v in osr["floors"].items()}
    lowest_floor = min(h for h, a in floors.items() if a > 1000)
    oz = np.floor((lowest_floor - 20 - DEPTH_BELOW * BLOCK - zoff) / BLOCK) * BLOCK + zoff
    origin = (float(ox), float(oy), float(oz))
    log(f"grid origin {origin}, offsets mod 40 = ({xoff}, {yoff}, {zoff}); sx={sx} sy={sy}; lowest floor {lowest_floor}")
    spawn_ct_src = [e for e in bsp.entities if e.get("classname") == "info_player_start"]
    spawn_t_src = [e for e in bsp.entities if e.get("classname") == "info_player_deathmatch"]
    t_spawn_pts = [tuple(map(float, e["origin"].split())) for e in spawn_t_src]

    # ------------------------------------------------------------------ BSP reference walk graph (hull 1)
    t0 = time.time()
    BG = walk.bsp_reach_graph(bsp, ox, oy, sx * 2, sy * 2, 20.0, wmin[2] - 10, wmax[2] + 10, t_spawn_pts, log,
                              solid_models=solid_models)
    stage("bsp_graph", t0)

    # ------------------------------------------------------------------ fine grid
    t0 = time.time()
    kz0 = int(np.floor((wmin[2] - oz) / 20.0))
    kz1 = int(np.ceil((wmax[2] - oz) / 20.0))
    G, mixed_box = finegrid.build_fine_grid(model, origin, sx, sy, kz0, kz1, log)
    stage("fine_grid", t0)

    # ------------------------------------------------------------------ classification + sky
    t0 = time.time()
    zf0 = oz + kz0 * 20.0  # world z of fine grid bottom
    min_prop = int((SKY_PROP_MIN_Z - zf0) / 5.0)
    start, has_sky = classify.sky_start(G, min_prop)
    cap_world = zf0 + start * 5.0
    max_cap = float(cap_world[has_sky].max())
    top = np.ceil((max_cap - oz) / BLOCK) * BLOCK + oz + HEADROOM * BLOCK
    sz = int(round((top - oz) / BLOCK))
    log(f"sky ceilings: {np.unique(np.round(cap_world[has_sky]/4)*4, return_counts=True)[0][:12]}..., max {max_cap}; "
        f"volume top z={top}; sz={sz}")
    solid_half, fs, fa, S_fine = classify.classify_half(G, start, THRESH)
    nzh_f = solid_half.shape[0]
    E = ~S_fine
    mixedE = ~E.reshape(nzh_f, finegrid.FZ, sy, finegrid.FY, sx, finegrid.FX).all(axis=(1, 3, 5))
    air_f = ~solid_half
    n_before = int(air_f.sum())
    stage("classify", t0)

    t0 = time.time()
    log("watertight pass:")
    air_f, wt_added = watertight.make_watertight(lambda: E, air_f, fs, mixedE, log)
    stage("watertight", t0)
    log(f"  watertight added {len(wt_added)} solid half cells: {collections.Counter(a[3] for a in wt_added)}")

    # global half-cell grid
    nzh = 2 * sz
    H = np.zeros((nzh, sy, sx), bool)
    H[:kz0] = True
    k1 = min(kz1, nzh)
    H[kz0:k1] = ~air_f[: k1 - kz0]
    # sky rule above the fine grid top: nothing (air)

    # ------------------------------------------------------------------ walkability repairs (BSP guided)
    t0 = time.time()

    def t_seeds(Hx):
        Sx = walk.standable(Hx)
        return [n for n in (walk.node_at(Sx, origin, x, y, z - 36) for (x, y, z) in t_spawn_pts) if n]

    log("step repair (every dust2 walk connection must be walkable with 20u steps):")
    step_changes = walk.step_repair(H, BG, origin, log)
    legit = walk.legit_mask(BG, H.shape, origin, "crouch")
    log("escape blocking (no jumping / crouch-jumping out of dust2's playable space):")
    esc_changes = walk.block_escapes(H, t_seeds, legit, log)
    stage("walk_repairs", t0)
    # ------------------------------------------------------------------ global watertight verification
    t0 = time.time()
    zi = np.arange(G.shape[0])[:, None, None]
    E_empty = (G == finegrid.EMPTY) & (zi < start[None])
    n_bspc, n_voxc, merges = watertight.verify_no_merges(E_empty[: (k1 - kz0) * finegrid.FZ], ~H[kz0:k1])
    del E_empty
    log(f"  watertight verification: {n_bspc} BSP empty components, {n_voxc} voxel air components (below the sky), "
        f"{len(merges)} merges {merges[:5]}")
    # open-sky check: no solid half cell may lie entirely at/above the sky ceiling of all its sample columns
    cap_half = np.ceil((cap_world - oz) / 20.0).astype(int)  # per fine column: first half cell starting at/above the cap
    cap_min = cap_half.reshape(sy, finegrid.FY, sx, finegrid.FX).max(axis=(1, 3))  # half cell index fully above every column's cap
    hh = np.arange(nzh)[:, None, None]
    above_sky_solid = int((H & (hh >= cap_min[None])).sum())
    log(f"  open-sky check: {above_sky_solid} solid half cells above the sky ceiling (must be 0)")
    if above_sky_solid:
        raise SystemExit("open-sky rule violated")
    stage("verify_watertight", t0)

    # ------------------------------------------------------------------ blocks / slabs
    lower = H[0::2]
    upper = H[1::2]
    kind = np.zeros((sz, sy, sx), np.int8)
    kind[lower & upper] = 1
    kind[lower & ~upper] = 2
    kind[~lower & upper] = 3

    # ------------------------------------------------------------------ doors
    t0 = time.time()
    door_cells = {}

    def air_block(bx, by, bz):
        return 0 <= bx < sx and 0 <= by < sy and 0 <= bz < sz and kind[bz, by, bx] == 0 and (bx, by, bz) not in door_cells

    def not_full(bx, by, bz):
        return 0 <= bx < sx and 0 <= by < sy and 0 <= bz < sz and kind[bz, by, bx] != 1 and (bx, by, bz) not in door_cells

    doors, fills = doormod.place_doors(ways, air_block, not_full, origin, log)
    for d in doors:
        bx, by, bz = d["lower"]
        st = d["facing"] | (4 if d["open"] else 0) | (16 if d["hinge_right"] else 0)
        door_cells[(bx, by, bz)] = st
        door_cells[(bx, by, bz + 1)] = st | 8
        d["block"] = tmap["door_block"]
    fill_set = set(map(tuple, fills))
    for (bx, by, bz) in fill_set:
        kind[bz, by, bx] = 1
    stage("doors", t0)

    # ------------------------------------------------------------------ materials
    t0 = time.time()
    skip = {n for n, t in tmap["textures"].items() if t.get("skip")}
    faces = matmod.FaceSet(bsp, [0] + solid_models, skip)
    exposed, exposed_dir, surf_tex, tex_block = matmod.assign_surface(kind > 0, kind, faces, tmap, origin, log)
    palette = ["air"]
    pidx = {"air": 0}

    def P(name):
        if name not in pidx:
            pidx[name] = len(palette)
            palette.append(name)
        return pidx[name]

    mat = np.zeros((sz, sy, sx), np.int32)  # palette index
    tex_counts = collections.Counter()
    unmatched = 0
    ez, ey, ex = np.nonzero(exposed)
    for z, y, x in zip(ez.tolist(), ey.tolist(), ex.tolist()):
        t = surf_tex.get((z, y, x))
        if t is not None and t in tex_block:
            b = tex_block[t]
            tex_counts[t] += 1
        else:
            unmatched += 1
            if exposed_dir[matmod.UP, z, y, x]:
                b = tmap["default_top"]
            elif exposed_dir[matmod.DOWN, z, y, x] and not exposed_dir[:4, z, y, x].any():
                b = tmap["default_bottom"]
            else:
                b = tmap["default_side"]
        k = kind[z, y, x]
        if k >= 2:
            tslab = tmap["textures"].get(t, {}).get("slab") if t else None
            b = tslab or tmap["blocks"].get(b, {}).get("slab") or "smooth_sandstone_slab"
        mat[z, y, x] = P(b)
    log(f"  exposed cells {len(ez)}, {unmatched} without a matching BSP face (defaults used)")
    # door frames
    for (bx, by, bz) in fill_set:
        mat[bz, by, bx] = P(tmap["door_frame_fill"])
    # sand needs support
    sand_i = pidx.get("sand")
    if sand_i is not None:
        below_full = np.zeros_like(kind, dtype=bool)
        below_full[1:] = kind[:-1] == 1
        below_full[0] = True
        uns = (mat == sand_i) & ~below_full
        mat[uns] = P(tmap["unsupported_sand_replacement"])
        log(f"  unsupported sand -> {tmap['unsupported_sand_replacement']}: {int(uns.sum())}")
    # subsoil + deep terrain
    rng = np.random.default_rng(tr["seed"])
    solid = kind > 0
    interior = solid & ~exposed
    surf_mask = solid & exposed
    dist, inds = ndimage.distance_transform_edt(~surf_mask, return_indices=True)
    noise = value_noise2d(sy, sx, 6, rng)
    smin, smax = tr["subsoil_depth_min"], tr["subsoil_depth_max"]
    sdepth = np.round(smin + noise * (smax - smin)).astype(int)  # (sy, sx)
    near_surf_mat = mat[inds[0], inds[1], inds[2]]
    sub_of = np.zeros(len(palette) + 64, np.int32)
    for name, i in list(pidx.items()):
        bdef = tmap["blocks"].get(name)
        if bdef is None:
            # slabs -> subsoil of their family
            for full, d in tmap["blocks"].items():
                if d["slab"] == name:
                    bdef = d
                    break
        sub_of[i] = P(bdef["subsoil"]) if bdef else P(tr["deep"])
    is_sub = interior & (dist <= sdepth[None, :, :])
    mat[is_sub] = sub_of[near_surf_mat[is_sub]]
    deep = interior & ~is_sub
    zz = np.arange(sz)[:, None, None]
    dnoise = value_noise2d(sy, sx, 5, rng)
    ds_top = tr["deepslate_layers"] + np.round(dnoise * 1.4 - 0.7).astype(int)  # 2..4
    is_ds = deep & (zz <= ds_top[None])
    mat[deep] = P(tr["deep"])
    mat[is_ds] = P(tr["deepslate"])
    # bedrock
    bed = P(tr["bedrock"])
    mat[0][solid[0]] = bed
    speck = (rng.random((sy, sx)) < 0.35) & solid[1] & ~exposed[1]
    mat[1][speck] = bed
    # ores
    stone_i = P(tr["deep"])
    ds_i = P(tr["deepslate"])
    deep_cells = np.argwhere((mat == stone_i) | (mat == ds_i))
    n_deep = len(deep_cells)
    ore_counts = collections.Counter()
    nbrs = np.array([(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)])
    for o in tr["ores"]:
        nveins = int(round(o["veins_per_10k"] * n_deep / 10000.0))
        if "zmax_layers" in o:
            zmax = o["zmax_layers"]
        else:
            zmax = int(o["zmax_frac"] * sz)
        cand = deep_cells[deep_cells[:, 0] <= zmax]
        if len(cand) == 0:
            continue
        oi = P(o["block"])
        odi = P(o["deepslate_variant"]) if o.get("deepslate_variant") else oi
        for _ in range(nveins):
            c = cand[rng.integers(len(cand))].copy()
            size = int(rng.integers(o["size"][0], o["size"][1] + 1))
            for _k in range(size):
                z, y, x = c
                if 0 <= z < sz and 0 <= y < sy and 0 <= x < sx:
                    if mat[z, y, x] == stone_i:
                        mat[z, y, x] = oi
                        ore_counts[o["block"]] += 1
                    elif mat[z, y, x] == ds_i:
                        if odi != oi or o["block"] != "diamond_ore":
                            mat[z, y, x] = odi
                            ore_counts[palette[odi]] += 1
                c = c + nbrs[rng.integers(6)]
    log(f"  deep cells {n_deep}; ores placed {dict(ore_counts)}")
    mat[~solid] = 0
    # sanity: every solid cell has a material
    assert np.all(mat[solid] > 0)
    stage("materials", t0)

    # ------------------------------------------------------------------ assemble cells (state bits)
    cells = mat.astype(np.uint16)
    cells[kind == 3] |= (1 << 10)  # top slab
    door_i = P(tmap["door_block"])
    for (bx, by, bz), st in door_cells.items():
        cells[bz, by, bx] = door_i | (st << 10)

    # decorations: dead bushes on sand (never in doorways / on spawns)
    t0 = time.time()
    bush_i = P("dead_bush")
    sand_i = pidx.get("sand")
    n_bush = 0
    if sand_i is not None:
        dc = tmap["decor"]["dead_bush"]
        sandtop = np.zeros_like(kind, bool)
        sandtop[:-1] = (mat[:-1] == sand_i) & (kind[:-1] == 1) & (cells[1:] == 0)
        near_wall = np.zeros_like(sandtop)
        full = kind == 1
        for dy, dx in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            sh = np.zeros_like(full)
            sh[:, max(0, dy):sy + min(0, dy), max(0, dx):sx + min(0, dx)] = full[:, max(0, -dy):sy - max(0, dy), max(0, -dx):sx - max(0, dx)]
            near_wall |= sh
        cz, cy, cx = np.nonzero(sandtop)
        r = rng.random(len(cz))
        above_near = near_wall[np.minimum(cz + 1, sz - 1), cy, cx]
        chance = np.where(above_near, dc["wall_base_chance"], dc["open_chance"])
        for z, y, x, rr, ch in zip(cz, cy, cx, r, chance):
            if rr < ch and (x, y, z + 1) not in door_cells:
                cells[z + 1, y, x] = bush_i
                n_bush += 1
    log(f"  dead bushes: {n_bush}")
    stage("decor", t0)

    shape_of_pal = np.array([blockdefs[n][0] for n in palette], np.int32)

    # ------------------------------------------------------------------ collision half grid + walkability
    t0 = time.time()
    pal = cells & 0x3FF
    shp = shape_of_pal[pal]
    st = cells >> 10
    full_c = (shp == 1)
    slab_b = (shp == 2) & ((st & 1) == 0)
    slab_t = (shp == 2) & ((st & 1) == 1)
    Hc = np.zeros((nzh, sy, sx), bool)
    Hc[0::2] = full_c | slab_b
    Hc[1::2] = full_c | slab_t
    S_nodes = walk.standable(Hc)

    def feet_node(x, y, zfeet, mask, tol=3):
        return walk.node_at(mask, origin, x, y, zfeet, tol)

    t_seed = t_seeds(Hc)
    reach = {}
    for up in (1, 2, 3):
        reach[up] = walk.reachability(Hc, t_seed, max_up=up)
        log(f"  reachable nodes from T spawn (max rise {up*20}u): {int(reach[up].sum())} of {int(S_nodes.sum())} standable")
    stage("walk_graph", t0)
    np.savez_compressed(os.path.join(SCRATCH, "debug_state.npz"), Hc=Hc, S=S_nodes, r1=reach[1], r2=reach[2],
                        r3=reach[3], cells=cells, kind=kind, origin=np.array(origin))
    # coverage of dust2's reachable standing spots + extra reach
    t0 = time.time()
    cov = {}
    unc = {}
    for up in (1, 2, 3):
        c, okm = walk.coverage(BG, reach[up], origin, "walk")
        cov[up] = c
        m = BG["reach"]["walk"] & ~okm
        unc[up] = np.stack([BG["x"][m], BG["y"][m], BG["z"][m]], 1)
    cov_jump, _ = walk.coverage(BG, reach[2], origin, "jump")
    extra = {up: int((reach[up] & ~legit).sum()) for up in (1, 2, 3)}
    log(f"  dust2 (hull 1) walk-reachable spots covered by voxel reach: walk {cov[1]*100:.2f}%, +jump {cov[2]*100:.2f}%, "
        f"+crouch-jump {cov[3]*100:.2f}%; dust2 jump-reachable covered by voxel walk+jump {cov_jump*100:.2f}%")
    log(f"  voxel nodes reachable without a dust2 counterpart: walk {extra[1]}, jump {extra[2]}, crouch-jump {extra[3]}")
    unc_clusters = []
    if len(unc[1]):
        g = np.zeros((sy * 2, sx * 2), bool)
        gi = ((unc[1][:, 0] - ox) // 20).astype(int)
        gj = ((unc[1][:, 1] - oy) // 20).astype(int)
        g[gj, gi] = True
        lab, nl = ndimage.label(g, structure=np.ones((3, 3)))
        for k in range(1, nl + 1):
            mm = lab[gj, gi] == k
            pts = unc[1][mm]
            unc_clusters.append(dict(n=int(mm.sum()), x=[float(pts[:, 0].min()), float(pts[:, 0].max())],
                                     y=[float(pts[:, 1].min()), float(pts[:, 1].max())],
                                     z_origin=[float(pts[:, 2].min()), float(pts[:, 2].max())]))
        for c in unc_clusters:
            log(f"    uncovered: {c['n']} spots x {c['x']} y {c['y']} origin z {c['z_origin']}")
    stage("bsp_coverage", t0)

    # ------------------------------------------------------------------ spawns + landmarks
    t0 = time.time()

    def place_standing(x, y, z_hint, prefer=None, search=48):
        """Standing origin (feet+36+1) on the voxel floor near (x,y), hull must not intersect voxels."""
        mins = np.array([-16.0, -16.0, -36.0])
        maxs = np.array([16.0, 16.0, 36.0])
        masks = [prefer, S_nodes] if prefer is not None else [S_nodes]
        for mask in masks:
            for r in range(0, search + 1, 4):
                cand = [(dx, dy) for dx in range(-r, r + 1, 4) for dy in range(-r, r + 1, 4) if max(abs(dx), abs(dy)) == r]
                cand.sort(key=lambda d: d[0] ** 2 + d[1] ** 2)
                for dx, dy in cand:
                    px, py = x + dx, y + dy
                    nd = walk.node_at(mask, origin, px, py, z_hint - 36, 4)
                    if nd is None:
                        continue
                    h, by, bx = nd
                    zfeet = oz + h * 20.0
                    zo = zfeet + 36 + 1
                    o = np.array([px, py, zo])
                    if walk.box_hits_world(cells, shape_of_pal, origin, o + mins, o + maxs):
                        continue
                    # must be supported within 2 units below (not hanging over a hole)
                    o2 = o - [0, 0, 2]
                    if not walk.box_hits_world(cells, shape_of_pal, origin, o2 + mins, o2 + maxs):
                        continue
                    return [float(px), float(py), float(zo)], (dx, dy)
        return None, None

    def spawns_from(src):
        out = []
        moved = 0
        for e in src:
            x, y, z = map(float, e["origin"].split())
            yaw = float(e.get("angles", "0 0 0").split()[1])
            p, d = place_standing(x, y, z, prefer=reach[1])
            if p is None:
                log(f"  WARNING: no voxel standing spot for spawn at {x},{y},{z}")
                continue
            if d != (0, 0):
                moved += 1
            out.append([p[0], p[1], p[2], yaw])
        return out, moved

    spawns_ct, moved_ct = spawns_from(spawn_ct_src)
    spawns_t, moved_t = spawns_from(spawn_t_src)
    log(f"  spawns: ct {len(spawns_ct)} ({moved_ct} nudged in xy), t {len(spawns_t)} ({moved_t} nudged)")
    # duplicates check (two spawns on the same spot)
    def bbox_of(model_i):
        return bsp.models["mins"][model_i].tolist(), bsp.models["maxs"][model_i].tolist()

    bombsites = []
    for b in brush_ents:
        if b["classname"] == "func_bomb_target":
            # name by proximity: dust2 tgt_a / tgt_b targets
            name = "A" if b["keys"].get("target", "").lower().endswith("a") else "B"
            bombsites.append(dict(name=name, mins=b["mins"], maxs=b["maxs"]))
    bombsites.sort(key=lambda b: b["name"])
    buyzones = []
    for b in brush_ents:
        if b["classname"] == "func_buyzone":
            team = "ct" if b["keys"].get("team") == "2" else "t"
            buyzones.append(dict(team=team, mins=b["mins"], maxs=b["maxs"]))
    buyzones.sort(key=lambda b: b["team"])

    def centre(b):
        return [(b["mins"][i] + b["maxs"][i]) / 2 for i in range(3)]

    A = next(b for b in bombsites if b["name"] == "A")
    B = next(b for b in bombsites if b["name"] == "B")
    ct_c = np.mean([[float(v) for v in e["origin"].split()] for e in spawn_ct_src], axis=0)
    t_c = np.mean([[float(v) for v in e["origin"].split()] for e in spawn_t_src], axis=0)
    LANDMARKS = collections.OrderedDict([
        ("t_spawn", (t_c[0], t_c[1], t_c[2])),
        ("ct_spawn", (ct_c[0], ct_c[1], ct_c[2])),
        ("a_site", (centre(A)[0], centre(A)[1], A["mins"][2] + 36)),
        ("b_site", (centre(B)[0], centre(B)[1], B["mins"][2] + 36)),
        ("mid", (-336.0, 1000.0, 0.0)),
        ("mid_doors", (-384.0, 1560.0, -92.0)),
        ("long_doors", (640.0, 200.0, 36.0)),
        ("outside_long", (700.0, -300.0, 36.0)),
        ("long_a", (1460.0, 1100.0, 36.0)),
        ("short_catwalk", (300.0, 1850.0, 132.0)),
        ("upper_tunnels", (-1900.0, 1200.0, 70.0)),
        ("lower_tunnels", (-900.0, 1400.0, -90.0)),
        ("b_doors", (-1230.0, 2176.0, 36.0)),
    ])
    lm_results = collections.OrderedDict()
    surface = {}
    for nm, (x, y, z) in LANDMARKS.items():
        p, d = place_standing(x, y, z, prefer=reach[1], search=120)
        if p is None:
            lm_results[nm] = dict(pos=None)
            continue
        nd = walk.node_at(S_nodes, origin, p[0], p[1], p[2] - 37, 0)
        lm_results[nm] = dict(pos=p, walk=bool(nd and reach[1][nd]), jump=bool(nd and reach[2][nd]),
                              crouch=bool(nd and reach[3][nd]))
    for nm, (x, y, z) in [("a_site", LANDMARKS["a_site"]), ("b_site", LANDMARKS["b_site"]), ("mid", LANDMARKS["mid"]),
                          ("ct_spawn", LANDMARKS["ct_spawn"]), ("t_spawn", LANDMARKS["t_spawn"])]:
        surface[nm] = lm_results[nm]["pos"]
    for nm, r in lm_results.items():
        log(f"  landmark {nm:14s} {r['pos']}  walk={r.get('walk')} jump={r.get('jump')} crouchjump={r.get('crouch')}")
    # pairwise walk connectivity (20u steps only, no jumps) between all landmarks
    lm_nodes = {nm: walk.node_at(S_nodes, origin, r["pos"][0], r["pos"][1], r["pos"][2] - 37, 0)
                for nm, r in lm_results.items() if r["pos"]}
    pair_walk = {}
    for a, na in lm_nodes.items():
        ra = walk.reachability(Hc, [na], max_up=1) if na else None
        pair_walk[a] = {b: bool(ra is not None and nb is not None and ra[nb]) for b, nb in lm_nodes.items()}
    n_pairs_ok = sum(v for row in pair_walk.values() for v in row.values())
    log(f"  pairwise landmark walk connectivity: {n_pairs_ok}/{len(lm_nodes)**2} ordered pairs connected")
    # spawn walk connectivity: every spawn reaches every landmark?
    spawn_ok = []
    for (x, y, z, _) in spawns_ct + spawns_t:
        nd = walk.node_at(S_nodes, origin, x, y, z - 37, 0)
        spawn_ok.append(bool(nd and reach[1][nd]))
    log(f"  spawns reachable from T spawn by walking: {sum(spawn_ok)}/{len(spawn_ok)}")
    with open(os.path.join(SCRATCH, "connectivity.json"), "w") as f:
        json.dump(dict(landmarks={k: v for k, v in lm_results.items()}, pairwise_walk=pair_walk,
                       spawns_walk_reachable=spawn_ok, coverage_walk=cov[1], coverage_jump=cov[2],
                       coverage_crouch=cov[3], dust2_jump_covered=cov_jump, extra_nodes=extra,
                       uncovered_clusters=unc_clusters, bsp_graph=dict(nodes=int(len(BG["x"])),
                       walk=int(BG["reach"]["walk"].sum()), jump=int(BG["reach"]["jump"].sum()))), f, indent=1)
    stage("spawns_landmarks", t0)

    # ------------------------------------------------------------------ write outputs
    t0 = time.time()
    nruns = mcw_io.write_mcw(OUT_MCW, cells, palette, origin, BLOCK)
    rd = mcw_io.read_mcw(OUT_MCW)
    rt_ok = (rd["palette"] == palette and tuple(rd["size"]) == (sx, sy, sz) and np.allclose(rd["origin"], origin)
             and np.array_equal(rd["cells"], cells) and rd["block_size"] == BLOCK)
    log(f"  wrote {OUT_MCW}: {os.path.getsize(OUT_MCW)} bytes, {nruns} runs, palette {len(palette)}; round trip "
        f"{'OK' if rt_ok else 'FAILED'}")
    if not rt_ok:
        raise SystemExit("MCW round trip failed")
    meta = collections.OrderedDict()
    meta["map"] = "mc_dust2"
    meta["source"] = "de_dust2"
    meta["blockSize"] = int(BLOCK)
    meta["origin"] = list(origin)
    meta["size"] = [sx, sy, sz]
    meta["spawns"] = {"ct": spawns_ct, "t": spawns_t}
    meta["bombsites"] = bombsites
    meta["buyzones"] = buyzones
    meta["doors"] = [dict(lower=d["lower"], facing=d["facing"], hinge_right=d["hinge_right"], block=d["block"],
                          open=d["open"]) for d in doors]
    meta["surfaceSamples"] = surface
    meta["landmarks"] = {k: v["pos"] for k, v in lm_results.items()}
    meta["palette"] = palette
    meta["notes"] = ("Spawn/sample z = player origin (feet + 36 + 1). Doors are placed open (bit2) like dust2's "
                     "ajar double doors; facing = side of the closed panel (0=+X,1=+Y,2=-X,3=-Y).")
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(meta, f, indent=1)
    log(f"  wrote {OUT_JSON}")
    stage("write", t0)

    # ------------------------------------------------------------------ previews
    t0 = time.time()
    tc = previews.TexCache(blockdefs)
    marks = []
    for nm, r in lm_results.items():
        if r["pos"]:
            col = (0, 160, 0) if r.get("walk") else ((230, 140, 0) if r.get("jump") else (220, 0, 0))
            marks.append((nm, (r["pos"][0], r["pos"][1]), col))
    previews.topdown(cells, palette, tc, os.path.join(SCRATCH, "topdown.png"), scale=6,
                     textured_path=os.path.join(SCRATCH, "topdown_textured.png"))
    previews.topdown(cells, palette, tc, os.path.join(SCRATCH, "topdown_landmarks.png"), scale=6, marks=marks, origin=origin)
    slice_specs = [(-120, "lower tunnels (z -120..-80)"), (-40, "z -40..0 (below main floor)"), (0, "main street level (z 0..40)"),
                   (40, "z 40..80"), (120, "A site / catwalk level (z 120..160)"), (160, "T spawn level (z 160..200)"),
                   (240, "z 240..280"), (360, "z 360..400 (near sky ceiling)")]
    slice_paths = []
    for z, title in slice_specs:
        bz = int((z - oz) // BLOCK)
        p = os.path.join(SCRATCH, f"slice_z{z}.png")
        previews.slice_image(cells, palette, tc, bz, p, scale=6, title=f"bz={bz} {title}")
        slice_paths.append(p)
    # isometric: full, and a cut-away with terrain above the street clipped outside the map area
    previews.isometric(cells, palette, tc, os.path.join(SCRATCH, "isometric.png"), a=5)
    cut_bz = int((260 - oz) // BLOCK)
    clip = np.zeros(cells.shape, bool)
    clip[:cut_bz] = True
    previews.isometric(cells, palette, tc, os.path.join(SCRATCH, "isometric_cutaway.png"), a=6, clip_fn=clip)
    # connectivity map
    reach_img = np.zeros((sy, sx, 3), np.uint8)
    col_w = reach[1].any(axis=0)
    col_j = reach[2].any(axis=0) & ~col_w
    col_c = reach[3].any(axis=0) & ~col_w & ~col_j
    col_s = S_nodes.any(axis=0) & ~(col_w | col_j | col_c)
    reach_img[:] = (60, 50, 40)
    reach_img[col_s] = (200, 60, 60)
    reach_img[col_c] = (240, 200, 0)
    reach_img[col_j] = (255, 140, 0)
    reach_img[col_w] = (90, 200, 90)
    from PIL import Image, ImageDraw
    im = Image.fromarray(reach_img[::-1]).resize((sx * 6, sy * 6), Image.NEAREST)
    dr = ImageDraw.Draw(im)
    for (x, y, zo) in unc[1]:
        px = (x - ox) / 40 * 6
        py = (sy - (y - oy) / 40) * 6
        dr.rectangle([px - 1, py - 1, px + 1, py + 1], fill=(40, 40, 255))
    for nm, (x, y), col in marks:
        px = (x - ox) / 40 * 6
        py = (sy - (y - oy) / 40) * 6
        dr.ellipse([px - 4, py - 4, px + 4, py + 4], outline=(0, 0, 0), width=2)
        dr.text((px + 6, py - 6), nm, fill=(255, 255, 255))
    im.save(os.path.join(SCRATCH, "connectivity.png"))
    stage("previews", t0)

    # ------------------------------------------------------------------ shell BSP
    shell_info = None
    shell_steps = None
    if not args.no_shell:
        t0 = time.time()
        sdir = os.path.join(SCRATCH, "shell")
        os.makedirs(sdir, exist_ok=True)
        map_path = os.path.join(sdir, "mc_dust2.map")
        vox_min = [ox, oy, oz]
        vox_max = [ox + sx * BLOCK, oy + sy * BLOCK, oz + sz * BLOCK]
        wad = os.path.join(GAME, "valve", "halflife.wad")
        lo, hi, clamped = shell_bsp.write_map(map_path, vox_min, vox_max, spawns_ct, spawns_t, bombsites, buyzones,
                                              wad, "mcsky", log=log)
        shell_steps = shell_bsp.compile_map(map_path, log)
        built = os.path.join(sdir, "mc_dust2.bsp")
        shell_info = shell_bsp.validate_bsp(built, dict(ct=len(spawns_ct), t=len(spawns_t), vox_min=vox_min,
                                                        vox_max=vox_max, spawns=spawns_ct + spawns_t), log)
        shell_info["box_inner"] = [lo.tolist(), hi.tolist()]
        shell_info["clamped"] = clamped
        if not shell_info["problems"]:
            shutil.copyfile(built, OUT_BSP)
            log(f"  copied shell BSP -> {OUT_BSP}")
        stage("shell_bsp", t0)
    # ------------------------------------------------------------------ independent output verification
    import verify_outputs
    verify_problems = verify_outputs.main(log) if not args.no_shell else ["shell not built (--no-shell)"]
    T["total"] = round(time.time() - t_all, 1)

    # ------------------------------------------------------------------ report
    counts = collections.Counter()
    pv, pc = np.unique(cells & 0x3FF, return_counts=True)
    for v, c in zip(pv, pc):
        counts[palette[v]] += int(c)
    slab_counts = {"bottom": int(((shp == 2) & ((st & 1) == 0)).sum()), "top": int(((shp == 2) & ((st & 1) == 1)).sum())}
    summary = dict(origin=origin, size=[sx, sy, sz], offsets=[xoff, yoff, zoff], osr=osr, counts=counts,
                   slab_counts=slab_counts, doors=doors, ways=ways, brush_ents=brush_ents, handling=handling,
                   tex_counts=tex_counts, wt=collections.Counter(a[3] for a in wt_added), cov=cov, cov_jump=cov_jump, extra=extra,
                   unc_clusters=unc_clusters, step_changes=step_changes, esc_changes=esc_changes,
                   wt_verify=dict(bsp_components=n_bspc, voxel_components=n_voxc, merges=merges),
                   pair_walk=pair_walk, spawn_ok=spawn_ok, verify=verify_problems, above_sky_solid=above_sky_solid,
                   bsp_graph=dict(nodes=int(len(BG['x'])), edges=int(len(BG['e1'])), walk=int(BG['reach']['walk'].sum()), jump=int(BG['reach']['jump'].sum()), crouch=int(BG['reach']['crouch'].sum())),
                   unc={k: v.tolist() for k, v in unc.items()}, lm=lm_results, spawns=(len(spawns_ct), len(spawns_t), moved_ct, moved_t),
                   shell=shell_info, shell_steps=shell_steps, timings=T, nruns=nruns, mcw_bytes=os.path.getsize(OUT_MCW),
                   bush=n_bush, ores=ore_counts, unmatched=unmatched, n_exposed=len(ez), max_cap=max_cap,
                   slice_paths=slice_paths, tmap=tmap, wadtex={k: [int(c) for c in v.average_colour()] for k, v in wadtex.items()})
    with open(os.path.join(SCRATCH, "summary.json"), "w") as f:
        json.dump(summary, f, indent=1, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o))
    import report
    report.write_report(os.path.join(SCRATCH, "REPORT.md"), summary, log)
    log(f"done in {T['total']}s: timings {dict(T)}")


if __name__ == "__main__":
    main()
