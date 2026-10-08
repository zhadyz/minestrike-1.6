"""Watertight pass: never let the voxel world connect empty regions that a thin BSP sheet separates.

Works on the fine grid (5-unit samples). E = passable samples (empty, sky, void above the sky ceiling).
  * pair check: for every pair of face-adjacent voxel-air half cells (A, B) where at least one is not fully
    empty, label the passable samples inside A u B only; if no component reaches both A and B, the BSP has
    a sheet between them -> the one with the larger solid fraction becomes solid.
  * split check: inside a single voxel-air half cell, if two or more distinct passable components touch
    faces whose neighbour is voxel-air, the cell would short-circuit a sheet -> it becomes solid.
Repeated until nothing changes.
"""
from __future__ import annotations

import numpy as np
from scipy import ndimage

from finegrid import FX, FY, FZ

STRUCT3 = ndimage.generate_binary_structure(3, 1)


def _blocks(E6, kz, ky, kx):
    # (n, FZ, FY, FX)
    return E6[kz, :, ky, :, kx, :]


def pair_check(E: np.ndarray, air: np.ndarray, mixed: np.ndarray, axis: int):
    """Return (cells_a, cells_b) index tuples of disconnected air pairs along axis (0=z,1=y,2=x)."""
    nzh, ny, nx = air.shape
    E6 = E.reshape(nzh, FZ, ny, FY, nx, FX)
    sl_a = [slice(None)] * 3
    sl_b = [slice(None)] * 3
    sl_a[axis] = slice(0, -1)
    sl_b[axis] = slice(1, None)
    cand = air[tuple(sl_a)] & air[tuple(sl_b)] & (mixed[tuple(sl_a)] | mixed[tuple(sl_b)])
    kz, ky, kx = np.nonzero(cand)
    if len(kz) == 0:
        return (kz, ky, kx), (kz, ky, kx)
    off = [0, 0, 0]
    off[axis] = 1
    bz, by, bx = kz + off[0], ky + off[1], kx + off[2]
    A = _blocks(E6, kz, ky, kx)
    B = _blocks(E6, bz, by, bx)
    cat_axis = {0: 1, 1: 2, 2: 3}[axis]
    U = np.concatenate([A, B], axis=cat_axis)  # (n, z, y, x)
    n = U.shape[0]
    P = np.zeros((n, U.shape[1] + 1, U.shape[2] + 1, U.shape[3] + 1), bool)
    P[:, :-1, :-1, :-1] = U
    L, nl = ndimage.label(P.reshape(n * P.shape[1], P.shape[2], P.shape[3]), structure=STRUCT3)
    L = L.reshape(P.shape)[:, :-1, :-1, :-1]
    half = U.shape[cat_axis] // 2
    idx_a = [slice(None)] * 4
    idx_b = [slice(None)] * 4
    idx_a[cat_axis] = slice(0, half)
    idx_b[cat_axis] = slice(half, None)
    in_a = np.zeros(nl + 1, bool)
    in_b = np.zeros(nl + 1, bool)
    in_a[L[tuple(idx_a)].ravel()] = True
    in_b[L[tuple(idx_b)].ravel()] = True
    both = in_a & in_b
    both[0] = False
    conn = both[L].reshape(n, -1).any(axis=1)
    dis = ~conn
    return (kz[dis], ky[dis], kx[dis]), (bz[dis], by[dis], bx[dis])


def split_check(E: np.ndarray, air: np.ndarray, mixed: np.ndarray, min_size: int = 2):
    nzh, ny, nx = air.shape
    E6 = E.reshape(nzh, FZ, ny, FY, nx, FX)
    kz, ky, kx = np.nonzero(air & mixed)
    if len(kz) == 0:
        return (kz, ky, kx)
    C = _blocks(E6, kz, ky, kx)  # (n, FZ, FY, FX)
    n = C.shape[0]
    P = np.zeros((n, FZ + 1, FY + 1, FX + 1), bool)
    P[:, :-1, :-1, :-1] = C
    L, nl = ndimage.label(P.reshape(n * (FZ + 1), FY + 1, FX + 1), structure=STRUCT3)
    L = L.reshape(P.shape)[:, :-1, :-1, :-1]
    sizes = np.bincount(L.ravel(), minlength=nl + 1)
    big = sizes >= min_size
    big[0] = False

    def nb_air(dz, dy, dx):
        z2, y2, x2 = kz + dz, ky + dy, kx + dx
        ok = (z2 >= 0) & (z2 < nzh) & (y2 >= 0) & (y2 < ny) & (x2 >= 0) & (x2 < nx)
        res = np.zeros(n, bool)
        res[ok] = air[z2[ok], y2[ok], x2[ok]]
        # outside the grid counts as air above the top (open sky), solid elsewhere
        if dz == 1:
            res[~ok] = True
        return res

    faces = [
        ((-1, 0, 0), L[:, 0, :, :]), ((1, 0, 0), L[:, -1, :, :]),
        ((0, -1, 0), L[:, :, 0, :]), ((0, 1, 0), L[:, :, -1, :]),
        ((0, 0, -1), L[:, :, :, 0]), ((0, 0, 1), L[:, :, :, -1]),
    ]
    touch_lists = []
    for (dz, dy, dx), plane in faces:
        m = nb_air(dz, dy, dx)
        lab = plane.reshape(n, -1).copy()
        lab[~m] = 0
        touch_lists.append(lab)
    T = np.concatenate(touch_lists, axis=1)  # (n, k) labels touching air faces
    T = np.where(big[T], T, 0)
    # count distinct non-zero labels per row
    Ts = np.sort(T, axis=1)
    distinct = (np.diff(Ts, axis=1) != 0) & (Ts[:, 1:] != 0)
    cnt = distinct.sum(axis=1) + (Ts[:, 0] != 0)
    sp = cnt >= 2
    return (kz[sp], ky[sp], kx[sp])


def verify_no_merges(E_empty: np.ndarray, air: np.ndarray, min_bsp=64, min_overlap=4):
    """Independent global check. E_empty: fine samples that are CONTENTS_EMPTY below the open-sky ceiling
    (sky excluded, so separate rooms are not joined through the sky). Label the BSP's empty space and the
    voxel air cells whose samples are mostly such empty space; a voxel component containing samples of two
    different significant BSP components means the voxelization joined spaces that dust2 keeps apart.
    Returns (n_bsp_components, n_voxel_components, list of merges)."""
    nzh, ny, nx = air.shape
    Lb, nb = ndimage.label(E_empty, structure=STRUCT3)
    bsizes = np.bincount(Lb.ravel(), minlength=nb + 1)
    E6 = E_empty.reshape(nzh, FZ, ny, FY, nx, FX)
    emp_frac = E6.mean(axis=(1, 3, 5))
    vox = air & (emp_frac >= 0.5)
    Lv, nv = ndimage.label(vox, structure=STRUCT3)
    # per fine sample: voxel component of its half cell
    Lv_f = np.repeat(np.repeat(np.repeat(Lv, FZ, 0), FY, 1), FX, 2)
    m = (Lv_f > 0) & (Lb > 0)
    pairs = np.stack([Lv_f[m], Lb[m]], 1)
    up, cnt = np.unique(pairs, axis=0, return_counts=True)
    merges = []
    by_v = {}
    for (v, b), c in zip(up.tolist(), cnt.tolist()):
        if bsizes[b] >= min_bsp and c >= min_overlap:
            by_v.setdefault(v, []).append((b, c))
    for v, lst in by_v.items():
        if len(lst) > 1:
            merges.append(dict(voxel_component=int(v), bsp_components=[(int(b), int(c), int(bsizes[b])) for b, c in lst]))
    return int((bsizes[1:] >= min_bsp).sum()), int(nv), merges


def make_watertight(E_fn, air: np.ndarray, fs: np.ndarray, mixed: np.ndarray, log=print, max_iter=20):
    """E_fn(air) -> fine passable mask given current voxel air (the fine grid itself does not change, but
    we keep the signature general). Mutates and returns air, plus list of cells made solid."""
    E = E_fn()
    added = []
    for it in range(max_iter):
        changed = 0
        sz, sy, sx = split_check(E, air, mixed)
        if len(sz):
            air[sz, sy, sx] = False
            added += list(zip(sz.tolist(), sy.tolist(), sx.tolist(), ["split"] * len(sz)))
            changed += len(sz)
        for axis in (0, 1, 2):
            (az, ay, ax), (bz, by, bx) = pair_check(E, air, mixed, axis)
            if len(az) == 0:
                continue
            fa = fs[az, ay, ax]
            fb = fs[bz, by, bx]
            pick_a = fa >= fb
            cz = np.where(pick_a, az, bz)
            cy = np.where(pick_a, ay, by)
            cx = np.where(pick_a, ax, bx)
            still = air[cz, cy, cx] & air[np.where(pick_a, bz, az), np.where(pick_a, by, ay), np.where(pick_a, bx, ax)]
            cz, cy, cx = cz[still], cy[still], cx[still]
            air[cz, cy, cx] = False
            added += list(zip(cz.tolist(), cy.tolist(), cx.tolist(), [f"pair{axis}"] * len(cz)))
            changed += len(cz)
        log(f"    watertight iter {it}: {changed} cells made solid")
        if changed == 0:
            break
    return air, added
