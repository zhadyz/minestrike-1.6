"""Grid-origin search: choose (ox, oy, oz) offsets (mod 40) that best align dust2 to the 40-unit block grid.

XY: horizontal slices of point contents at 2-unit spacing at player heights over the main floors; for each
even offset the block cells are classified by majority and we score the fraction of samples whose cell
class agrees with their own contents (higher = walls fall on block boundaries).
Z: vertical profiles at random columns (2-unit spacing) scored the same way on 20-unit half cells, plus a
floor term: area-weighted error of every flat floor height vs the nearest half-cell boundary, with a bonus
for floors on full block tops.
"""
from __future__ import annotations

import collections
import time

import numpy as np

from bsp30 import Bsp30


def floor_histogram(bsp: Bsp30, models=(0,)) -> dict:
    """Area of flat upward-facing (walkable) faces per height."""
    hist = collections.Counter()
    for m in models:
        for f in bsp.model_faces(m):
            if bsp.face_texture_name(f).lower() in ("sky", "aaatrigger", "clip"):
                continue
            n = bsp.face_normal(f)
            if n[2] < 0.999:
                continue
            v = bsp.face_vertices(f)
            c = np.zeros(3)
            for i in range(1, len(v) - 1):
                c += np.cross(v[i] - v[0], v[i + 1] - v[0])
            hist[int(round(v[:, 2].mean()))] += float(np.linalg.norm(c) / 2)
    return dict(hist)


def floor_error(h: float, oz: int, thresh: float = 0.5) -> float:
    """Signed error (voxel floor - true floor) for a floor at height h with half-cell boundaries at
    oz + 20k, when a half cell becomes solid if its solid fraction >= thresh."""
    rel = (h - oz) / 20.0
    k = np.floor(rel)
    frac = rel - k
    vox = (k + 1) if frac >= thresh else k
    if frac == 0:
        vox = k
    return float(vox * 20.0 - (h - oz))


def search(bsp: Bsp30, contents_fn, log=print):
    t0 = time.time()
    wmins = bsp.models["mins"][0].astype(float)
    wmaxs = bsp.models["maxs"][0].astype(float)
    # ---------------- XY ----------------
    floors = floor_histogram(bsp)
    top = sorted(floors.items(), key=lambda kv: -kv[1])[:6]
    zs = sorted({h + 36 for h, _ in top} | {h + 96 for h, _ in top[:2]})
    xs = np.arange(wmins[0] + 1, wmaxs[0], 2.0)
    ys = np.arange(wmins[1] + 1, wmaxs[1], 2.0)
    X, Y = np.meshgrid(xs, ys)  # (ny, nx)
    slices = []
    for z in zs:
        P = np.stack([X.ravel(), Y.ravel(), np.full(X.size, float(z))], 1)
        c = contents_fn(P).reshape(X.shape)
        slices.append(c == -2)
    log(f"  xy slices at z={zs} ({len(xs)}x{len(ys)} samples each) in {time.time()-t0:.1f}s")
    x0 = int(wmins[0])
    y0 = int(wmins[1])
    xy_scores = np.zeros((20, 20))
    for jy in range(20):
        oy = jy * 2
        sy = ((oy - y0) % 40) // 2
        for jx in range(20):
            ox = jx * 2
            sx = ((ox - x0) % 40) // 2
            agree = 0
            total = 0
            for S in slices:
                ny = (S.shape[0] - sy) // 20
                nx = (S.shape[1] - sx) // 20
                sub = S[sy:sy + ny * 20, sx:sx + nx * 20]
                cnt = sub.reshape(ny, 20, nx, 20).sum(axis=(1, 3))
                agree += np.maximum(cnt, 400 - cnt).sum()
                total += ny * nx * 400
            xy_scores[jy, jx] = agree / total
    by, bx = np.unravel_index(np.argmax(xy_scores), xy_scores.shape)
    best_xy = (int(bx * 2), int(by * 2))
    log(f"  best xy offset {best_xy} agreement {xy_scores[by, bx]:.4f} (worst {xy_scores.min():.4f}, "
        f"median {np.median(xy_scores):.4f})")

    # ---------------- Z ----------------
    rng = np.random.default_rng(1234)
    ncol = 20000
    cx = rng.uniform(wmins[0], wmaxs[0], ncol)
    cy = rng.uniform(wmins[1], wmaxs[1], ncol)
    zz = np.arange(wmins[2] + 1, wmaxs[2], 2.0)
    P = np.stack([np.repeat(cx, len(zz)), np.repeat(cy, len(zz)), np.tile(zz, ncol)], 1)
    C = contents_fn(P).reshape(ncol, len(zz))
    S = C == -2
    # keep only columns that contain some empty space (inside the map)
    inside = (C == -1).any(axis=1)
    S = S[inside]
    z0 = int(wmins[2])
    z_agree = np.zeros(20)
    for j in range(20):
        oz = j * 2
        s = ((oz - z0) % 20) // 2
        n = (S.shape[1] - s) // 10
        cnt = S[:, s:s + n * 10].reshape(S.shape[0], n, 10).sum(axis=2)
        z_agree[j] = np.maximum(cnt, 10 - cnt).sum() / (S.shape[0] * n * 10)
    tot_area = sum(floors.values())
    z_floor = np.zeros(20)
    z_block = np.zeros(20)
    rows = []
    for j in range(20):
        oz = j * 2
        err = sum(a * abs(floor_error(h, oz)) for h, a in floors.items()) / tot_area
        blk = sum(a for h, a in floors.items() if (h - oz) % 40 == 0) / tot_area
        slab = sum(a for h, a in floors.items() if (h - oz) % 20 == 0) / tot_area
        z_floor[j] = err
        z_block[j] = blk
        rows.append((oz, z_agree[j], err, slab, blk))
    # combined: agreement (0..1) - mean floor error/20 + small block-top bonus
    z_score = z_agree - z_floor / 20.0 + 0.05 * z_block
    jbest = int(np.argmax(z_score))
    best_z = jbest * 2
    log("  z offset table: oz, agreement, mean|floor err|, frac floors on 20-grid, frac on block tops, score")
    for (oz, ag, er, sl, bk), sc in zip(rows, z_score):
        log(f"    {oz:3d}  {ag:.4f}  {er:5.2f}  {sl:.3f}  {bk:.3f}  {sc:.4f}{'  <== best' if oz == best_z else ''}")
    log(f"  origin search done in {time.time()-t0:.1f}s")
    return {
        "xy_offset": best_xy, "z_offset": best_z,
        "xy_agreement": float(xy_scores[by, bx]), "xy_agreement_worst": float(xy_scores.min()),
        "z_agreement": float(z_agree[jbest]), "z_floor_err": float(z_floor[jbest]),
        "z_block_frac": float(z_block[jbest]), "z_score": float(z_score[jbest]),
        "xy_scores": xy_scores.tolist(), "z_table": [list(map(float, r)) for r in rows],
        "floors": {str(k): v for k, v in sorted(floors.items(), key=lambda kv: -kv[1])},
    }
