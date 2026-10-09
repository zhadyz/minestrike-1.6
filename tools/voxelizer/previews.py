"""Preview renders of the voxel world (top-down colour map, slices, isometric)."""
from __future__ import annotations

import os

import numpy as np
from PIL import Image, ImageDraw

_ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TEXDIR = os.path.join(_ROOT, "assets", "mc", "jar", "assets", "minecraft", "textures", "block")
if not os.path.isdir(TEXDIR):  # (an installed copy has the textures in the game folder only)
    TEXDIR = os.path.join(os.environ.get("MINESTRIKE_GAME") or os.path.join(_ROOT, "game", "Half-Life"), "cstrike", "mc", "textures", "block")


class TexCache:
    def __init__(self, blockdefs: dict):
        self.defs = blockdefs  # name -> (shape, top, side, bottom)
        self.cache = {}

    def tex(self, name):
        if name is None:
            return None
        if name not in self.cache:
            p = os.path.join(TEXDIR, name + ".png")
            im = Image.open(p).convert("RGBA")
            a = np.asarray(im)[:16, :16].copy()
            self.cache[name] = a
        return self.cache[name]

    def avg(self, name, face="top"):
        d = self.defs.get(name)
        if d is None:
            return np.array([255, 0, 255], float)
        t = {"top": d[1], "side": d[2], "bottom": d[3]}[face]
        a = self.tex(t)
        if a is None:
            return np.array([255, 0, 255], float)
        m = a[..., 3] > 128
        if not m.any():
            return np.array([128, 128, 128], float)
        return a[m][:, :3].mean(axis=0)


def palette_colours(tc: TexCache, palette: list[str], face: str):
    cols = np.zeros((len(palette), 3))
    for i, n in enumerate(palette):
        cols[i] = tc.avg(n, face) if i else (255, 255, 255)
    return cols


def topdown(cells, palette, tc: TexCache, path, scale=6, textured_path=None, marks=None, origin=None):
    sz, sy, sx = cells.shape
    pal = cells & 0x3FF
    # top visible (non-air) cell per column
    nonair = pal != 0
    has = nonair.any(axis=0)
    top = np.where(has, sz - 1 - np.argmax(nonair[::-1], axis=0), 0)
    yy, xx = np.mgrid[0:sy, 0:sx]
    tp = pal[top, yy, xx]
    st = cells[top, yy, xx] >> 10
    cols = palette_colours(tc, palette, "top")
    img = cols[tp]
    # height incl. slab half: bottom slab top at +0.5
    shape = np.array([tc.defs.get(n, (0,))[0] for n in palette])
    hgt = top + 1.0
    is_slab = shape[tp] == 2
    hgt = np.where(is_slab & ((st & 1) == 0), top + 0.5, hgt)
    # shading: global height + hillshade from the NW
    h = hgt.astype(float)
    shade = 0.45 + 0.55 * (h - h.min()) / max(1e-6, (h.max() - h.min()))
    gy, gx = np.gradient(h)
    hill = np.clip(1.0 + 0.10 * (-gx + gy), 0.6, 1.3)  # light from -x,+y (north-west, north up)
    img = np.clip(img * (shade * hill)[..., None], 0, 255).astype(np.uint8)
    img = img[::-1]  # north (+y) up
    im = Image.fromarray(img).resize((sx * scale, sy * scale), Image.NEAREST)
    if marks:
        d = ImageDraw.Draw(im)
        for label, (x, y), col in marks:
            px = (x - origin[0]) / 40.0 * scale
            py = (sy - (y - origin[1]) / 40.0) * scale
            d.ellipse([px - 5, py - 5, px + 5, py + 5], outline=col, width=2)
            d.text((px + 7, py - 7), label, fill=col)
    im.save(path)
    if textured_path:
        T = 16
        out = np.zeros((sy * T, sx * T, 3), np.float32)
        for i, n in enumerate(palette):
            if i == 0:
                continue
            m = tp == i
            if not m.any():
                continue
            d = tc.defs.get(n)
            t = tc.tex(d[1]) if d else None
            if t is None:
                continue
            rgb = t[..., :3].astype(np.float32)
            alpha = t[..., 3:4].astype(np.float32) / 255.0
            # plants: draw over sand colour
            if d[0] == 6:
                rgb = rgb * alpha + np.array([219, 207, 163]) * (1 - alpha)
            ys, xs = np.nonzero(m)
            for y, x in zip(ys, xs):
                out[y * T:(y + 1) * T, x * T:(x + 1) * T] = rgb
        sh = np.repeat(np.repeat(shade * hill, T, 0), T, 1)
        out = np.clip(out * sh[..., None], 0, 255).astype(np.uint8)[::-1]
        Image.fromarray(out).save(textured_path)
    return top


def slice_image(cells, palette, tc: TexCache, bz, path, scale=6, title=None):
    sz, sy, sx = cells.shape
    pal = cells[bz] & 0x3FF
    st = cells[bz] >> 10
    cols = palette_colours(tc, palette, "side")
    img = cols[pal]
    shape = np.array([tc.defs.get(n, (0,))[0] for n in palette])
    sh = shape[pal]
    img[pal == 0] = (250, 248, 240)
    # slabs: lighter tint, doors: dark outline colour
    img = np.where((sh == 2)[..., None], img * 0.75 + np.array([255, 255, 255]) * 0.25, img)
    img = np.where((sh == 4)[..., None], np.array([200, 40, 40]), img)
    big = np.repeat(np.repeat(img[::-1], scale, 0), scale, 1)
    # slab marker: diagonal hatch for half blocks
    m = np.repeat(np.repeat((sh == 2)[::-1], scale, 0), scale, 1)
    yy, xx = np.mgrid[0:big.shape[0], 0:big.shape[1]]
    hatch = m & (((xx + yy) % 6) == 0)
    big[hatch] = big[hatch] * 0.6
    im = Image.fromarray(np.clip(big, 0, 255).astype(np.uint8))
    if title:
        d = ImageDraw.Draw(im)
        d.rectangle([0, 0, 8 * len(title) + 8, 16], fill=(0, 0, 0))
        d.text((4, 2), title, fill=(255, 255, 255))
    im.save(path)


def isometric(cells, palette, tc: TexCache, path, a=5, clip_fn=None):
    """Simple painter's-algorithm isometric render, viewer south-east and above (at +x, -y, +z):
    screen X = (x + y) * a, screen Y = (x - y) * a/2 - z*a; visible faces +z (top), -y (left), +x (right).
    North (+y) points to the upper right."""
    sz, sy, sx = cells.shape
    pal = cells & 0x3FF
    st = cells >> 10
    shape = np.array([tc.defs.get(n, (0,))[0] for n in palette])
    sh = shape[pal]
    solid = (pal != 0) & (sh != 6) & (sh != 4)
    if clip_fn is not None:
        solid &= clip_fn
    full = solid & (sh != 2)
    # visible if +z, +x or -y neighbour is not full
    nzp = np.zeros_like(full); nzp[:-1] = full[1:]
    nxp = np.zeros_like(full); nxp[:, :, :-1] = full[:, :, 1:]
    nym = np.zeros_like(full); nym[:, 1:, :] = full[:, :-1, :]
    vis = solid & ~(nzp & nxp & nym)
    zs, ys, xs = np.nonzero(vis)
    # screen coords (viewer looking from south-east)
    X = (xs + ys) * a
    Y = (xs - ys) * a * 0.5 - zs * a * 1.0
    W = int(X.max() + 3 * a)
    off_y = -Y.min() + 2 * a
    H = int(Y.max() + off_y + 3 * a)
    img = np.full((H, W, 3), (200, 220, 245), np.uint8)
    # order: far to near = increasing x - (sy - y)... viewer at +x,-y: nearer = larger x, smaller y, larger z
    order = np.lexsort((zs, xs - ys))
    ctop = palette_colours(tc, palette, "top")
    cside = palette_colours(tc, palette, "side")
    # precompute face masks for a cube sprite of size (2a wide)
    w = 2 * a
    hgt = int(2 * a)
    # sprite geometry in local coords: top rhombus, left (-y face) and right (+x face) parallelograms
    yy, xx = np.mgrid[0:hgt + a + 1, 0:w + 1]
    cx = a
    top_m = (np.abs(xx - cx) / a + np.abs(yy - a / 2) / (a / 2)) <= 1.0
    left_m = (xx <= cx) & (yy >= a / 2 + xx * 0.5 - 0.0) & (yy <= a / 2 + xx * 0.5 + a) & ~top_m
    right_m = (xx >= cx) & (yy >= a / 2 + (w - xx) * 0.5) & (yy <= a / 2 + (w - xx) * 0.5 + a) & ~top_m
    SH, SW = top_m.shape
    for i in order:
        x, y, z = xs[i], ys[i], zs[i]
        p = pal[z, y, x]
        sx0 = int(X[i] - a)
        sy0 = int(Y[i] + off_y - a / 2)
        if sy0 < 0 or sx0 < 0 or sy0 + SH > H or sx0 + SW > W:
            continue
        reg = img[sy0:sy0 + SH, sx0:sx0 + SW]
        ct = ctop[p]
        cs = cside[p]
        reg[top_m] = np.clip(ct * 1.0, 0, 255)
        reg[left_m] = np.clip(cs * 0.72, 0, 255)
        reg[right_m] = np.clip(cs * 0.86, 0, 255)
    Image.fromarray(img).save(path)
