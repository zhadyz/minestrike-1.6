"""Half-cell solid/air classification from the fine contents grid, incl. the open-sky rule."""
from __future__ import annotations

import numpy as np
from scipy import ndimage

from finegrid import EMPTY, FX, FY, FZ, SKY, SOLID


def sky_start(G: np.ndarray, min_prop_level: int):
    """Per fine column: index of the lowest sky sample that has no empty sample above it (the open sky
    ceiling). Columns without such sky get the value of the nearest column whose sky ceiling index is
    >= min_prop_level (so low sky pits at the map edge do not spread). Returns (start, has_sky)."""
    nz = G.shape[0]
    emp = G == EMPTY
    sky = G == SKY
    has_e = emp.any(axis=0)
    top_e = np.where(has_e, nz - 1 - np.argmax(emp[::-1], axis=0), -1)
    zi = np.arange(nz)[:, None, None]
    sky_above = sky & (zi > top_e[None])
    has_s = sky_above.any(axis=0)
    start = np.where(has_s, np.argmax(sky_above, axis=0), nz).astype(np.int32)
    src = has_s & (start >= min_prop_level)
    if (~has_s).any():
        _, (iy, ix) = ndimage.distance_transform_edt(~src, return_indices=True)
        prop = start[iy, ix]
        start = np.where(has_s, start, prop)
    return start, has_s


def classify_half(G: np.ndarray, start: np.ndarray, thresh: float):
    """Return (solid_half bool (nzh, ny, nx), frac_solid, above_sky_frac)."""
    nz = G.shape[0]
    zi = np.arange(nz)[:, None, None]
    above = zi >= start[None]
    solid = (G == SOLID) & ~above
    nzh, ny, nx = nz // FZ, G.shape[1] // FY, G.shape[2] // FX
    n = FX * FY * FZ
    fs = solid.reshape(nzh, FZ, ny, FY, nx, FX).sum(axis=(1, 3, 5)).astype(np.float32) / n
    fa = above.reshape(nzh, FZ, ny, FY, nx, FX).sum(axis=(1, 3, 5)).astype(np.float32) / n
    return fs >= thresh, fs, fa, solid
