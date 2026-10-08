"""Fine sampling of BSP contents on a regular grid aligned to the voxel half-cells.

Each half cell (40 x 40 x 20 units) holds FX x FY x FZ = 8 x 8 x 4 samples (5-unit spacing, sample centres
at 2.5, 7.5, ... so they never sit on the integer-aligned brush planes). Half cells whose box is uniform in
the BSP (box-vs-tree test) are filled without sampling.

Codes: 0 = empty (passable), 1 = solid, 2 = sky.
"""
from __future__ import annotations

import time

import numpy as np

from bsp30 import Bsp30, CONTENTS_SKY, CONTENTS_SOLID

FX, FY, FZ = 8, 8, 4
EMPTY, SOLID, SKY = 0, 1, 2


def contents_code(c: np.ndarray) -> np.ndarray:
    out = np.zeros(c.shape, np.uint8)
    out[c == CONTENTS_SOLID] = SOLID
    out[c == CONTENTS_SKY] = SKY
    return out


class ContentsModel:
    """World hull 0 + solid brush entities (func_breakable / func_wall) - optional empty overrides
    (oriented boxes carved out, used for door leaves that we replace with Minecraft doors)."""

    def __init__(self, bsp: Bsp30, solid_models: list[int], carve_boxes: list | None = None):
        self.bsp = bsp
        self.solid_models = solid_models
        self.carve = carve_boxes or []  # list of (center(3), axes(3x3 rows), half(3))

    def codes(self, P: np.ndarray) -> np.ndarray:
        b = self.bsp
        code = contents_code(b.point_contents(P, 0, 0))
        for m in self.solid_models:
            mn = b.models["mins"][m] - 1
            mx = b.models["maxs"][m] + 1
            inb = np.all((P >= mn) & (P <= mx), axis=1) & (code != SOLID)
            if inb.any():
                c = b.point_contents(P[inb], m, 0)
                sub = code[inb]
                sub[c == CONTENTS_SOLID] = SOLID
                code[inb] = sub
        for cen, axes, half in self.carve:
            d = (P - cen) @ np.asarray(axes).T
            inside = np.all(np.abs(d) <= half, axis=1)
            code[inside & (code == SOLID)] = EMPTY
        return code

    def box_mask(self, mins: np.ndarray, maxs: np.ndarray) -> np.ndarray:
        """bitmask per box as in Bsp30.box_contents_mask, with entity/carve overlaps forced 'mixed'."""
        b = self.bsp
        mask = b.box_contents_mask(mins, maxs, 0)
        force = np.zeros(len(mins), bool)
        for m in self.solid_models:
            mn = b.models["mins"][m]
            mx = b.models["maxs"][m]
            force |= np.all((maxs > mn) & (mins < mx), axis=1)
        for cen, axes, half in self.carve:
            r = np.abs(np.asarray(axes)).T @ np.asarray(half)  # world AABB half extents of the OBB
            force |= np.all((maxs > cen - r) & (mins < cen + r), axis=1)
        mask[force] |= 0x80
        return mask


def build_fine_grid(model: ContentsModel, origin, nx: int, ny: int, kz0: int, kz1: int, log=print):
    """Sample half cells kz0..kz1-1 (z index in half cells), all x/y. Returns G (nzh*FZ, ny*FY, nx*FX) uint8
    and per-half-cell 'mixed' flag (nzh, ny, nx)."""
    t0 = time.time()
    ox, oy, oz = origin
    nzh = kz1 - kz0
    kz, ky, kx = np.meshgrid(np.arange(kz0, kz1), np.arange(ny), np.arange(nx), indexing="ij")
    kz = kz.ravel()
    ky = ky.ravel()
    kx = kx.ravel()
    eps = 0.01
    mins = np.stack([ox + kx * 40.0, oy + ky * 40.0, oz + kz * 20.0], 1) + eps
    maxs = mins + np.array([40.0, 40.0, 20.0]) - 2 * eps
    mask = model.box_mask(mins, maxs)
    single = np.isin(mask, [1, 2, 4])
    G6 = np.zeros((nzh, FZ, ny, FY, nx, FX), np.uint8)
    uni_code = np.where(mask == 2, SOLID, np.where(mask == 4, SKY, EMPTY)).astype(np.uint8)
    # fill uniform cells
    ui = np.nonzero(single)[0]
    G6[kz[ui] - kz0, :, ky[ui], :, kx[ui], :] = uni_code[ui][:, None, None, None]
    mi = np.nonzero(~single)[0]
    log(f"  box test: {len(mask)} half cells, {len(mi)} mixed ({100*len(mi)/len(mask):.1f}%) in {time.time()-t0:.1f}s")
    # sample offsets within a half cell
    fz, fy, fx = np.meshgrid((np.arange(FZ) + 0.5) * (20.0 / FZ), (np.arange(FY) + 0.5) * (40.0 / FY),
                             (np.arange(FX) + 0.5) * (40.0 / FX), indexing="ij")
    offs = np.stack([fx.ravel(), fy.ravel(), fz.ravel()], 1)  # (256,3) order z,y,x
    per = FX * FY * FZ
    batch = max(1, (1 << 22) // per)
    for s in range(0, len(mi), batch):
        idx = mi[s:s + batch]
        base = np.stack([ox + kx[idx] * 40.0, oy + ky[idx] * 40.0, oz + kz[idx] * 20.0], 1)
        P = (base[:, None, :] + offs[None, :, :]).reshape(-1, 3)
        c = model.codes(P).reshape(len(idx), FZ, FY, FX)
        G6[kz[idx] - kz0, :, ky[idx], :, kx[idx], :] = c
    log(f"  fine grid sampled in {time.time()-t0:.1f}s")
    mixed = (~single).reshape(nzh, ny, nx)
    return G6.reshape(nzh * FZ, ny * FY, nx * FX), mixed


def half_fractions(G: np.ndarray):
    nzf, nyf, nxf = G.shape
    G6 = G.reshape(nzf // FZ, FZ, nyf // FY, FY, nxf // FX, FX)
    n = FX * FY * FZ
    fs = (G6 == SOLID).sum(axis=(1, 3, 5)) / n
    fk = (G6 == SKY).sum(axis=(1, 3, 5)) / n
    return fs.astype(np.float32), fk.astype(np.float32)
