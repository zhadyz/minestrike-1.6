"""Minecraft-style item icons for Counter-Strike's weapons (hotbar / inventory slots 1-5).

Source pictures are Counter-Strike's own buy-menu renders (cstrike/gfx/vgui/<weapon>.tga). Each gun is
cut out (largest connected shape, so a loose silencer is dropped), tilted diagonally like Minecraft's
tools, and reduced to 32x32 pixel art with hard alpha. The knife and C4 have no buy-menu picture: their
HUD silhouettes (sprites/640hud*.spr via sprites/weapon_<name>.txt) are shaded instead.

    python tools/assets/gun_icons.py
Writes cstrike/mc/textures/item/cs_<weapon>.png in both game copies.
"""
import os
import struct

import numpy as np
from PIL import Image
from scipy import ndimage

# the game copies beside the repository (wherever it lies); MINESTRIKE_CSTRIKE names one cstrike folder instead
ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')).replace(os.sep, '/')
GAMES = [os.environ['MINESTRIKE_CSTRIKE'].replace(os.sep, '/')] if os.environ.get('MINESTRIKE_CSTRIKE') else [
    f'{ROOT}/game/Half-Life/cstrike', f'{ROOT}/game_test/Half-Life/cstrike']
SRC = GAMES[0]
SIZE = 32

# CS weapon name (as in the client's kCsWeaponNames) -> vgui picture
VGUI = {
    'p228': 'p228', 'scout': 'scout', 'hegrenade': 'hegrenade', 'xm1014': 'xm1014', 'mac10': 'mac10', 'aug': 'aug',
    'smokegrenade': 'smokegrenade', 'elite': 'elites', 'fiveseven': 'fiveseven', 'ump45': 'ump45', 'sg550': 'sg550',
    'galil': 'galil', 'famas': 'famas', 'usp': 'usp45', 'glock18': 'glock18', 'awp': 'awp', 'mp5navy': 'mp5', 'm249': 'm249',
    'm3': 'm3', 'm4a1': 'm4a1', 'tmp': 'tmp', 'g3sg1': 'g3sg1', 'flashbang': 'flashbang', 'deagle': 'deserteagle',
    'sg552': 'sg552', 'ak47': 'ak47', 'p90': 'p90',
}
SILHOUETTE = {'knife': (215, 215, 225), 'c4': (190, 200, 170)}


def largest_component(rgba):
    mask = rgba[..., 3] > 40
    lab, n = ndimage.label(mask)
    if n <= 1:
        return rgba
    sizes = ndimage.sum(mask, lab, range(1, n + 1))
    keep = 1 + int(np.argmax(sizes))
    out = rgba.copy()
    out[lab != keep, 3] = 0
    return out


def crop(img):
    a = np.asarray(img)
    ys, xs = np.nonzero(a[..., 3] > 40)
    if len(xs) == 0:
        return img
    return img.crop((xs.min(), ys.min(), xs.max() + 1, ys.max() + 1))


def pixel_art(img, angle, flip=False):
    """Tilt, fit into SIZE x SIZE, hard alpha: Minecraft item look."""
    if flip:
        img = img.transpose(Image.FLIP_LEFT_RIGHT)
    big = img.rotate(angle, resample=Image.BICUBIC, expand=True)
    big = crop(big)
    w, h = big.size
    s = (SIZE - 2) / max(w, h)
    small = big.resize((max(1, round(w * s)), max(1, round(h * s))), Image.LANCZOS)
    a = np.asarray(small).astype(np.float32)
    # un-premultiply-ish: pixels at the edges keep their colour, alpha becomes binary
    alpha = a[..., 3]
    rgb = a[..., :3]
    keep = alpha > 110
    out = np.zeros((SIZE, SIZE, 4), np.uint8)
    oy = (SIZE - small.size[1]) // 2
    ox = (SIZE - small.size[0]) // 2
    sub = out[oy:oy + small.size[1], ox:ox + small.size[0]]
    col = np.clip(rgb * (255.0 / np.maximum(alpha[..., None], 1.0)), 0, 255)
    col = np.where(keep[..., None], np.minimum(col, 255), 0)
    sub[..., :3] = col.astype(np.uint8)
    sub[..., 3] = np.where(keep, 255, 0)
    # Minecraft items have a darker rim: darken pixels that touch transparency
    solid = out[..., 3] > 0
    edge = solid & ~ndimage.binary_erosion(solid)
    out[edge, :3] = (out[edge, :3] * 0.62).astype(np.uint8)
    return Image.fromarray(out)


def read_spr(path):
    d = open(path, 'rb').read()
    assert d[:4] == b'IDSP'
    # IDSP, version, type, texFormat, boundingradius, maxwidth, maxheight, numframes, beamlen, synctype
    (_, ver, typ, fmt, rad, mw, mh, nf, bl, sync) = struct.unpack_from('<4siiifiiifi', d, 0)
    off = 40
    ncol = struct.unpack_from('<h', d, off)[0]
    off += 2
    pal = np.frombuffer(d, np.uint8, ncol * 3, off).reshape(ncol, 3)
    off += ncol * 3
    ftype, ox, oy, w, h = struct.unpack_from('<iiiii', d, off)
    off += 20
    pix = np.frombuffer(d, np.uint8, w * h, off).reshape(h, w)
    return pal, pix, fmt


def hud_rect(name):
    txt = open(f'{SRC}/sprites/weapon_{name}.txt').read().split('\n')
    for line in txt:
        p = line.split()
        if len(p) >= 7 and p[0] == 'weapon' and p[1] == '640':
            return p[2], [int(v) for v in p[3:7]]
    return None


def silhouette(name, colour):
    """The HUD weapon picture: a grey render on a dark panel with a label. Keep the largest bright shape."""
    spr, (x, y, w, h) = hud_rect(name)
    pal, pix, fmt = read_spr(f'{SRC}/sprites/{spr}.spr')
    rgb = pal[pix[y:y + h, x:x + w]].astype(np.float32)
    lum = rgb.mean(axis=2)
    mask = lum > 70
    mask = ndimage.binary_fill_holes(ndimage.binary_closing(mask, iterations=1))
    lab, n = ndimage.label(mask)
    if n > 1:
        sizes = ndimage.sum(mask, lab, range(1, n + 1))
        mask = lab == (1 + int(np.argmax(sizes)))
    rgba = np.zeros((h, w, 4), np.uint8)
    for k in range(3):
        rgba[..., k] = np.clip(rgb[..., k] * colour[k] / 200.0, 0, 255)
    rgba[..., 3] = np.where(mask, 255, 0)
    return Image.fromarray(rgba)


def main():
    outs = {}
    for name, pic in VGUI.items():
        img = Image.open(f'{SRC}/gfx/vgui/{pic}.tga').convert('RGBA')
        img = Image.fromarray(largest_component(np.asarray(img)))
        img = crop(img)
        angle = 0 if name in ('hegrenade', 'flashbang', 'smokegrenade') else 35
        outs[name] = pixel_art(img, angle)
    for name, colour in SILHOUETTE.items():
        outs[name] = pixel_art(crop(silhouette(name, colour)), 35 if name == 'knife' else 0)
    for g in GAMES:
        d = f'{g}/mc/textures/item'
        if not os.path.isdir(d):
            continue
        for name, im in outs.items():
            im.save(f'{d}/cs_{name}.png')
    # contact sheet for review
    sheet = Image.new('RGBA', (SIZE * 8 * 3, SIZE * 4 * 3), (139, 139, 139, 255))
    for i, (name, im) in enumerate(sorted(outs.items())):
        big = im.resize((SIZE * 3, SIZE * 3), Image.NEAREST)
        sheet.alpha_composite(big, ((i % 8) * SIZE * 3, (i // 8) * SIZE * 3))
    if os.path.isdir('Z:/dev/scratch/csminecraft'):  # (the contact sheet: for whoever works on the icons)
        sheet.save('Z:/dev/scratch/csminecraft/gun_icons.png')
    print(f'{len(outs)} icons')


if __name__ == '__main__':
    main()
