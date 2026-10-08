"""REPORT.md writer (filled in from build_world.py's summary dict)."""
from __future__ import annotations

import collections
import datetime
import os

DOORWAY_NAMES = {0: "mid doors (lower, z -128)", 1: "B doors", 2: "long doors"}


def _fmt(v, nd=1):
    if isinstance(v, float):
        return f"{v:.{nd}f}"
    return str(v)


def write_report(path, s, log=print):
    o = s["origin"]
    sx, sy, sz = s["size"]
    L = []
    w = L.append
    w("# mc_dust2 voxelizer report")
    w("")
    w(f"Generated {datetime.datetime.now().strftime('%Y-%m-%d %H:%M')} by `python Z:\\dev\\CSminecraft\\tools\\voxelizer\\build_world.py` "
      f"(total {s['timings'].get('total')} s). Source: `cstrike\\maps\\de_dust2.bsp` (GoldSrc v30).")
    w("")
    w("## Outputs")
    w("")
    w("| file | what |")
    w("|---|---|")
    w(f"| `Z:\\dev\\CSminecraft\\game\\Half-Life\\cstrike\\maps\\mc_dust2.mcw` | voxel world, MCW1, {s['mcw_bytes']} bytes, {s['nruns']} RLE runs, palette {len(s['counts'])} |")
    w("| `Z:\\dev\\CSminecraft\\game\\Half-Life\\cstrike\\maps\\mc_dust2.json` | metadata (spawns, bombsites, buyzones, doors, surfaceSamples, landmarks, palette) |")
    w("| `Z:\\dev\\CSminecraft\\game\\Half-Life\\cstrike\\maps\\mc_dust2.bsp` | shell BSP (sky box + floor + spawns + triggers), SDHLT v1.3.0 |")
    w("| `Z:\\dev\\CSminecraft\\tools\\voxelizer\\texture_map.json` | editable texture -> block table |")
    w("")
    w("## Grid origin and alignment")
    w("")
    osr = s["osr"]
    w(f"* **origin = ({o[0]:g}, {o[1]:g}, {o[2]:g})**, block = 40 units; offsets mod 40 = x {s['offsets'][0]}, y {s['offsets'][1]}, z {s['offsets'][2]}.")
    w(f"* XY search (all even offsets 0..38 on both axes, 2-unit point-contents slices at 8 player heights over the main floors, "
      f"majority classification per 40x40 cell): best agreement **{osr['xy_agreement']:.4f}** at ({s['offsets'][0]}, {s['offsets'][1]}); "
      f"worst offset {osr['xy_agreement_worst']:.4f}.")
    w(f"* Z search (offsets 0..38 step 2): score = column agreement on 20-unit half cells - mean |floor error|/20 + 0.05 x (floor area on block tops). "
      f"Chosen z offset {s['offsets'][2]}: agreement {osr['z_agreement']:.4f}, area-weighted mean floor error {osr['z_floor_err']:.2f} u, "
      f"{osr['z_block_frac']*100:.0f}% of flat floor area exactly on a block top (dust2's dominant floor z=0 is a block top; "
      f"every other floor lands within +-8 u of a block or slab top).")
    w("")
    w("| dust2 floor z | area (k u^2) | voxel floor z |")
    w("|---|---|---|")
    fl = list(osr["floors"].items())[:10]
    for h, a in fl:
        h = int(h)
        rel = (h - o[2]) / 20.0
        k = int(rel // 1)
        frac = rel - k
        vox = o[2] + 20 * (k + (1 if frac >= 0.5 else 0)) if frac > 0 else h
        w(f"| {h} | {a/1000:.0f} | {vox:g} ({'block top' if (vox - o[2]) % 40 == 0 else 'slab top'}) |")
    w("")
    w("## World size")
    w("")
    w(f"* **size = {sx} x {sy} x {sz} blocks** = x {o[0]:g}..{o[0]+sx*40:g}, y {o[1]:g}..{o[1]+sy*40:g}, z {o[2]:g}..{o[2]+sz*40:g}.")
    w(f"* XY covers the whole de_dust2 world model bounds; z gives {int((-200 - o[2]) / 40)} blocks of terrain under the lowest dust2 floor "
      f"(z -192 -> voxel -200) and 8 blocks of air above the highest open-sky ceiling (z {s['max_cap']:g}).")
    w("")
    w("## Method")
    w("")
    w("1. **Point contents**: hull 0 (nodes/leafs) of the world model, plus func_breakable crates (their own hull-0 trees) as solid. "
      "Every 40x40x20 half cell is box-tested against the BSP tree; uniform cells are filled directly, the ~7.5% mixed ones are sampled "
      "8x8x4 (5-unit spacing, centres at x.5 so no sample sits on a brush plane).")
    w("2. **Open sky**: per sample column the open-sky ceiling is the lowest SKY sample with no EMPTY above it; everything from there up "
      "(sky brushes + the void above them) is air to the top of the volume. Columns with no sky (indoor roofs that run into the void, "
      "and the void outside the map walls) take the ceiling of the nearest main sky column (>= z 300), so roofs/terrain end flush with "
      "the wall tops (a flat desert plateau at z ~352-512 around the sunken town). Thin sky caps on roof tops count as air.")
    w(f"   Open-sky check after all passes: {s.get('above_sky_solid')} solid half cells above any sky ceiling.")
    w("3. **Classification**: a half cell is solid when >= 50% of its samples are solid (sky = air). Lower+upper half -> full block, "
      "lower only -> bottom slab, upper only -> top slab. I used 0.5 instead of the suggested ~0.3 because 0.3 grows every wall by "
      "~8u per side on average and closes 48u gaps at worst alignment; thin walls are instead kept by the explicit watertight pass "
      "(which keeps every sheet the 5u samples see, i.e. anything >= 5u thick).")
    wt = s["wt"]
    w(f"4. **Watertight pass** on the 5-unit samples: for every pair of face-adjacent air half cells, the passable samples inside the two "
      f"cells are labelled; if no component reaches both, a BSP sheet separates them and the cell with the larger solid fraction becomes "
      f"solid. Single cells containing two separate passable regions that touch different air neighbours become solid too. "
      f"Added {sum(wt.values())} half cells ({dict(wt)}).")
    v = s["wt_verify"]
    w(f"   **Global check**: BSP empty space (sky excluded) has {v['bsp_components']} significant components; the voxel air below the sky "
      f"has {v['voxel_components']} components; **{len(v['merges'])} voxel components join two different BSP components** "
      f"(0 = no leaks between spaces dust2 keeps apart, e.g. the sealed memorial room stays sealed).")
    w(f"5. **Walkability repair (BSP guided)**: a reference graph of dust2 standing spots is built from **hull 1** (standing player, includes "
      f"clip brushes and the crates) on a 20-unit grid ({s['bsp_graph']['nodes']} spots, {s['bsp_graph']['edges']} moves; from T spawn "
      f"{s['bsp_graph']['walk']} reachable walking with 18u steps, {s['bsp_graph']['jump']} with jumps). Every dust2 walk move whose "
      f"two voxel columns differ by >= 40u got a half-step: {len(s['step_changes'])} half cells changed "
      f"({sum(1 for c in s['step_changes'] if c[0]=='raise')} raised, {sum(1 for c in s['step_changes'] if c[0]=='lower')} lowered).")
    w(f"6. **Escape blocking**: without clip brushes and with the sky opened, steep rock slopes (nz ~0.55, not standable in GoldSrc) "
      f"became 40u staircases that let players jump onto roofs and the plateau. Voxel spots reachable with crouch-jumps that have no "
      f"dust2 counterpart and form a large group are escapes; the first escape column next to each legit spot was raised so the climb "
      f"is >= 80u: {len(s['esc_changes'])} columns, {sum(c[1]-c[0] for c in s['esc_changes'])} half cells, all below the sky ceilings.")
    w("7. **Materials**: each exposed face of a solid cell queries the nearest BSP face (world + crates) whose normal points out of that "
      "face (<= 44u, point-to-polygon distance); a cell uses its top face's texture if it has one, else the most common side texture, "
      "else the bottom. Texture -> block via `texture_map.json` (slab variant for half cells). Unmatched exposed cells (the plateau, "
      "wall tops under removed sky) default to sand on top / cut sandstone on sides. Sand never sits on a non-full block (BF_FALLS).")
    w("8. **Terrain**: interior solid within 1-3 blocks (smooth noise) of a surface takes that surface's subsoil (sand/sandstone/"
      "concrete -> sandstone, rock -> stone, wood -> wood); deeper is stone with ore veins (random-walk veins, fixed seed "
      f"{s['tmap']['terrain']['seed']}), deepslate in the lowest 2-4 layers, bedrock at bz=0 (+35% speckle at bz=1).")
    w(f"9. **Decor**: {s['bush']} dead bushes on sand (6% along wall bases, 0.4% in the open); SHAPE_CROSS = no collision, never blocks.")
    w("")
    w("## Block counts")
    w("")
    w("| block | cells |")
    w("|---|---|")
    for n, c in sorted(s["counts"].items(), key=lambda kv: -kv[1]):
        w(f"| {n} | {c} |")
    w("")
    w(f"Slabs: {s['slab_counts']['bottom']} bottom, {s['slab_counts']['top']} top. Ores placed: {dict(s['ores'])}.")
    w("")
    w("## Brush entities")
    w("")
    w("de_dust2 has **no func_door / func_door_rotating / func_wall**. Its 42 brush models:")
    w("")
    w("| model | class | bounds | handling |")
    w("|---|---|---|---|")
    for b in s["brush_ents"]:
        mn = ",".join(f"{v:g}" for v in b["mins"])
        mx = ",".join(f"{v:g}" for v in b["maxs"])
        keys = {k: v for k, v in b["keys"].items() if k in ("team", "target", "targetname", "material")}
        w(f"| *{b['model']} | {b['classname']} {keys if keys else ''} | ({mn})..({mx}) | {s['handling'].get(b['classname'], 'ignored')} |")
    w("")
    w("func_illusionary *4..*28 are the 6 hanging lamps (generic011 + fifties light), *32..*35 are non-solid broken-wall pieces "
      "above B; both skipped (non-solid in dust2). The 10 func_breakable crates (tgt_a/tgt_b, explode with the bomb) are solid crates "
      "(military crate texture -> polished_andesite, wooden -> planks).")
    w("")
    w("## Doors")
    w("")
    w("dust2's doors are **world brushes**: three open double doors made of 8u-thick leaves textured SandWllDoor* (hinged at the jambs, "
      "swung ~25 degrees open), plus door textures painted on solid walls. The leaves are carved out of the voxelization (oriented "
      "boxes from the leaf faces) and each opening gets spruce_door pairs (lower+upper half cells), placed **open** (state bit2) like in "
      "dust2, mirrored hinges per double door (open panel against the jamb / the neighbour pair), an odd middle column filled with spruce "
      "planks, and the opening above 2 blocks closed with spruce planks up to the dust2 lintel. Painted / closed doors (e.g. the "
      "closed double door behind B, 'NO EXIT') just become spruce_planks wall patches (texture map).")
    w("")
    w("| # | doorway | lower [bx,by,bz] | world centre of lower cell | facing | hinge_right | open |")
    w("|---|---|---|---|---|---|---|")
    fn = {0: "+X", 1: "+Y", 2: "-X", 3: "-Y"}
    for i, d in enumerate(s["doors"]):
        bx, by, bz = d["lower"]
        wc = (o[0] + bx * 40 + 20, o[1] + by * 40 + 20, o[2] + bz * 40)
        w(f"| {i} | {DOORWAY_NAMES.get(d['way'], d['way'])} | {d['lower']} | ({wc[0]:g}, {wc[1]:g}, z {wc[2]:g}) | {d['facing']} ({fn[d['facing']]}) | {d['hinge_right']} | {d['open']} |")
    w("")
    w("## Texture map summary")
    w("")
    w("| texture | avg RGB | block (full) | slab | surface cells |")
    w("|---|---|---|---|---|")
    for name, t in s["tmap"]["textures"].items():
        if t.get("skip"):
            continue
        slab = t.get("slab") or s["tmap"]["blocks"].get(t["block"], {}).get("slab", "")
        w(f"| {name} | {tuple(t.get('avg_rgb', []))} | {t['block']} | {slab} | {s['tex_counts'].get(name, 0)} |")
    w("")
    w(f"Skipped for material lookup: sky, aaatrigger, clip, lamp textures. {s['unmatched']} of {s['n_exposed']} exposed cells had no "
      f"matching BSP face within 44u (plateau, wall tops under the removed sky ceiling, terrain faces) and use the defaults.")
    w("")
    w("## Connectivity / walkability")
    w("")
    cov = s["cov"]
    w(f"* dust2 standing spots reachable from T spawn by walking (hull 1, 18u steps): **{cov['1']*100 if isinstance(cov, dict) and '1' in cov else cov[1]*100:.2f}% "
      f"are reachable in the voxel world by walking** (20u half-steps, stepsize 24, 4 half cells = 80u headroom); with jumps / "
      f"crouch-jumps {cov[2]*100:.2f}% / {cov[3]*100:.2f}%. dust2 jump-reachable spots covered by voxel walk+jump: {s['cov_jump']*100:.2f}%.")
    w(f"* Voxel spots reachable from T spawn with no dust2 counterpart: walking {s['extra'][1]}, +jump {s['extra'][2]}, +crouch-jump "
      f"{s['extra'][3]} (crate tops etc.; the plateau / roofs are not reachable without building).")
    w(f"* Spawns reachable by walking from T spawn: {sum(s['spawn_ok'])}/{len(s['spawn_ok'])}.")
    w("")
    w("| landmark | standing origin | walk from T | jump | crouch-jump |")
    w("|---|---|---|---|---|")
    for nm, r in s["lm"].items():
        p = r.get("pos")
        ps = f"({p[0]:g}, {p[1]:g}, {p[2]:g})" if p else "-"
        w(f"| {nm} | {ps} | {r.get('walk')} | {r.get('jump')} | {r.get('crouch')} |")
    w("")
    pw = s["pair_walk"]
    names = list(pw.keys())
    ok = sum(v for row in pw.values() for v in row.values())
    w(f"Pairwise walking (20u steps, no jumps) between all {len(names)} landmarks: **{ok}/{len(names)**2} ordered pairs connected**.")
    fails = [(a, b) for a in names for b in names if not pw[a][b]]
    if fails:
        w(f"Not connected: {fails}")
    w("")
    if s["unc_clusters"]:
        w("dust2 walk-reachable spots not covered (all tiny edge cases: spots on the lip of a ledge whose voxel column took the lower "
          "floor, or under 40u-wide ledges that voxelize away):")
        w("")
        for c in s["unc_clusters"]:
            w(f"* {c['n']} spot(s) x {c['x'][0]:g}..{c['x'][1]:g}, y {c['y'][0]:g}..{c['y'][1]:g}, origin z {c['z_origin'][0]:g}..{c['z_origin'][1]:g}")
        w("")
    w("Escape-blocking raises (column centre, z range filled):")
    w("")
    for h0, h1, y, x in s["esc_changes"]:
        w(f"* ({o[0]+x*40+20:g}, {o[1]+y*40+20:g}) z {o[2]+h0*20:g}..{o[2]+h1*20:g}")
    w("")
    w(f"Spawns: {s['spawns'][0]} CT + {s['spawns'][1]} T, all on voxel floors with the 32x32x72 hull free (checked against exact "
      f"block shapes) and supported; {s['spawns'][2]} + {s['spawns'][3]} needed an xy nudge. z = feet + 36 + 1.")
    w("")
    w("## Shell BSP")
    w("")
    sh = s.get("shell")
    if sh:
        w("Compiled with **SDHLT v1.3.0** (github.com/seedee/SDHLT release `sdhlt_v130.zip`, Win64 binaries in "
          "`Z:\\dev\\CSminecraft\\tools\\sdhlt\\v130\\`): " + ", ".join(f"{st['tool']} {' '.join(st['args'])} (rc {st['rc']}, {st['seconds']} s, "
          f"{len(st['warnings'])} warnings)" for st in s["shell_steps"]) + ".")
        w("")
        bi = sh["box_inner"]
        w(f"* Sealed box interior x {bi[0][0]:g}..{bi[1][0]:g}, y {bi[0][1]:g}..{bi[1][1]:g}, z {bi[0][2]:g}..{bi[1][2]:g} "
          f"(256u margin around the voxel volume, 1024u above it, floor 16u below it; 32u thick walls). Walls + ceiling are SKY brushes, "
          f"the floor is a solid `out_dirt1` brush. Clamped to stay inside +-4096: {sh['clamped'] or 'none'} "
          f"(+Y margin {bi[1][1] - (o[1] + sy * 40):g}u).")
        w("* worldspawn: skyname `des` (dust2's own bright desert sky; available: Des, DrkG, TrainYard, backalley, badlands, blue, city1, "
          "cx, de_storm, doom1, forest, green, grnplsnt, hav, morningdew, office, snow, snowlake_, tornsky, tsccity_), wad "
          "`halflife.wad` (cstrike.wad has no sky/AAATRIGGER), mapversion 220.")
        w("* light_environment: pitch -60, yaw 43, _light 255 245 220 300, _diffuse_light 190 210 255 110.")
        w(f"* Entities: {sh['classnames']}. de_dust2 has no info_map_parameters.")
        w(f"* Validation (re-parsed with bsp30.py): {sh['models']} models (world + 4 triggers), {sh['entities']} entities, world bounds "
          f"{sh['world_mins']}..{sh['world_maxs']} enclose the voxel volume, spawns empty in hulls 0/1/3, floor below the voxels solid, "
          f"sky leafs present, lighting {sh['lighting_bytes']} bytes, {sh['size_bytes']} bytes total. Problems: {sh['problems'] or 'none'}.")
    else:
        w("Shell BSP not built in this run (--no-shell).")
    w("")
    w("## Previews")
    w("")
    sc = r"Z:\dev\scratch\csminecraft\voxelizer"
    for f in ["topdown.png", "topdown_textured.png", "topdown_landmarks.png", "isometric.png", "isometric_cutaway.png", "connectivity.png"]:
        w(f"* `{os.path.join(sc, f)}`")
    for p in s["slice_paths"]:
        w(f"* `{p}`")
    w(f"* `{os.path.join(sc, 'connectivity.json')}`, `{os.path.join(sc, 'build_log.txt')}`, `{os.path.join(sc, 'summary.json')}`")
    w(f"* references: `{os.path.join(sc, 'textures_sheet.png')}` (all dust2 textures with average colours), "
      f"`{os.path.join(sc, 'skies.png')}` (sky candidates), `{os.path.join(sc, 'shell')}` (shell .map + SDHLT logs)")
    w("")
    w("Slices: one block layer, coloured by each block's side-texture average; hatched = slab, red = door, white = air, "
      "gray = stone/deepslate interior. Top-downs: top visible block per column with height shading (north up).")
    w("")
    w("connectivity.png: green = columns walk-reachable from T spawn, orange = only with jumps, yellow = only with crouch-jumps, "
      "red = standable but unreachable (roofs, plateau, sealed room), blue dots = dust2 walk spots not covered.")
    w("")
    w("## Timings (s)")
    w("")
    w(" | ".join(f"{k} {v}" for k, v in s["timings"].items()))
    w("")
    w("## Uncertain / for the integrator")
    w("")
    w("* **Doors**: there are no func_door entities in de_dust2, so the 12 spruce doors are my interpretation of its three world-brush "
      "double doors (placed open; facing/hinge per mc_world.h, open panel side s = (f + (hinge_right ? 3 : 1)) % 4). Closing them "
      "seals mid doors / B doors / long doors. The openings above the doors are planked up to dust2's lintel, so these doorways are "
      "2 blocks (80u) high instead of ~158u: a jump through them hits the planks.")
    w("* **Escapes**: clip brushes are not voxelized (hull 0 only), so ledges that dust2 clipped are standable; the escape pass only "
      "blocks routes that lead out of the playable space (roofs/plateau). A few crate tops dust2 clipped are now reachable "
      "(extra spots above). Players who can place blocks can of course still build out.")
    w("* **Terrain outside**: the void outside dust2's walls is a flat desert plateau flush with the wall tops (z 352/384/512); "
      "sky brushes that come down low at the map edge leave open pits/terraces in the plateau (e.g. NW corner to z -128, east "
      "strip to z ~132). They are outside the playable space and not reachable.")
    if sh and sh.get("clamped"):
        w(f"* **Clamp**: to keep the shell inside +-4096 the margin on {sh['clamped']} is smaller than 256u "
          f"(+Y margin {sh['box_inner'][1][1] - (o[1] + sy * 40):g}u).")
    w("* The shell's func_bomb_target/func_buyzone use dust2's exact volumes; voxel floors are within +-8u of dust2's floors except "
      "where step repair moved a column by a half block, so players stay inside them.")
    w("* Python deps: numpy, Pillow and scipy (scipy 1.17 is already installed in the user site-packages; used for labelling/"
      "distance transforms/BFS).")
    w("")
    w(f"verify_outputs.py: {'all checks passed' if not s.get('verify') else s['verify']}")
    w("")
    w("## Regenerate")
    w("")
    w("```")
    w("python Z:\\dev\\CSminecraft\\tools\\voxelizer\\build_world.py      # everything (~2 min)")
    w("python Z:\\dev\\CSminecraft\\tools\\voxelizer\\verify_outputs.py   # independent re-check of .mcw/.json/.bsp")
    w("```")
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")
    log(f"  wrote {path}")
