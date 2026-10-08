"""Generate a Minecraft-style daytime skybox for GoldSrc (gfx/env/mcsky{rt,bk,lf,ft,up,dn}.tga).

Minecraft's overworld day sky: zenith ~#78A7FF fading to the fog colour ~#C0D8FF at the horizon, with the
square sun from textures/environment/sun.png (additive) high in the sky.
"""
import os

import numpy as np
from PIL import Image

ROOT = 'Z:/dev/CSminecraft'
OUT = f'{ROOT}/game/Half-Life/cstrike/gfx/env'
SUN = f'{ROOT}/assets/mc/jar/assets/minecraft/textures/environment/celestial/sun.png'
SUN_ALT = f'{ROOT}/assets/mc/jar/assets/minecraft/textures/environment/sun.png'
N = 256

ZENITH = np.array([120, 167, 255], np.float32)
HORIZON = np.array([192, 216, 255], np.float32)
BELOW = np.array([150, 180, 230], np.float32)


def sky_color(dirs):
    """dirs: (...,3) unit vectors with z up -> RGB."""
    z = dirs[..., 2:3]
    t = np.clip(z, 0.0, 1.0) ** 0.55  # quick falloff above the horizon like MC's fog band
    col = HORIZON * (1 - t) + ZENITH * t
    below = np.clip(-z * 4.0, 0.0, 1.0)
    col = col * (1 - below) + BELOW * below
    return col


def face_dirs(face):
    # GoldSrc skybox faces in a Z-up world: rt=+Y? The engine's convention (Quake): rt=+X? We use
    # the standard Quake2/HL mapping: ft = +X, bk = -X, lf = +Y, rt = -Y, up = +Z, dn = -Z.
    u = (np.arange(N) + 0.5) / N * 2 - 1
    uu, vv = np.meshgrid(u, -u)  # vv: +1 at top row
    one = np.ones_like(uu)
    if face == 'ft':
        d = np.stack([one, -uu, vv], -1)
    elif face == 'bk':
        d = np.stack([-one, uu, vv], -1)
    elif face == 'lf':
        d = np.stack([uu, one, vv], -1)
    elif face == 'rt':
        d = np.stack([-uu, -one, vv], -1)
    elif face == 'up':
        d = np.stack([-vv, -uu, one], -1)
    else:  # dn
        d = np.stack([vv, -uu, -one], -1)
    return d / np.linalg.norm(d, axis=-1, keepdims=True)


def write_tga(path, rgb):
    # classic uncompressed 24-bit TGA, bottom-left origin, BGR rows bottom-up (what GoldSrc's loader expects)
    h, w, _ = rgb.shape
    header = bytes([0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, w & 255, w >> 8, h & 255, h >> 8, 24, 0])
    data = rgb[::-1, :, ::-1].tobytes()
    with open(path, 'wb') as f:
        f.write(header)
        f.write(data)


def main():
    os.makedirs(OUT, exist_ok=True)
    sun_path = SUN if os.path.exists(SUN) else SUN_ALT
    if os.path.exists(sun_path):
        sun = np.asarray(Image.open(sun_path).convert('RGB'), np.float32)
    else:
        # procedural Minecraft sun: bright square with a soft halo, 32x32
        sun = np.zeros((32, 32, 3), np.float32)
        yy, xx = np.mgrid[0:32, 0:32]
        core = (abs(xx - 15.5) < 8) & (abs(yy - 15.5) < 8)
        halo = np.clip(1.0 - np.maximum(abs(xx - 15.5), abs(yy - 15.5)) / 16.0, 0, 1) ** 2
        sun[..., 0] = halo * 120
        sun[..., 1] = halo * 110
        sun[..., 2] = halo * 60
        sun[core] = [255, 255, 210]
    sun_dir = np.array([0.55, 0.25, 0.8])
    sun_dir /= np.linalg.norm(sun_dir)
    for face in ['rt', 'bk', 'lf', 'ft', 'up', 'dn']:
        d = face_dirs(face)
        col = sky_color(d)
        if sun is not None:
            # project onto the sun's tangent plane; the sun is a square ~10 degrees wide
            cosang = d @ sun_dir
            mask = cosang > 0.9
            if mask.any():
                ref = np.array([0, 0, 1.0])
                tu = np.cross(sun_dir, ref)
                tu /= np.linalg.norm(tu)
                tv = np.cross(tu, sun_dir)
                pu = (d @ tu) / np.maximum(cosang, 1e-3)
                pv = (d @ tv) / np.maximum(cosang, 1e-3)
                size = 0.09
                su = ((pu / size) * 0.5 + 0.5) * sun.shape[1]
                sv = ((-pv / size) * 0.5 + 0.5) * sun.shape[0]
                inside = mask & (su >= 0) & (su < sun.shape[1]) & (sv >= 0) & (sv < sun.shape[0])
                ys, xs = np.nonzero(inside)
                col[ys, xs] = np.clip(col[ys, xs] + sun[sv[ys, xs].astype(int), su[ys, xs].astype(int)], 0, 255)
        write_tga(f'{OUT}/mcsky{face}.tga', np.clip(col, 0, 255).astype(np.uint8))
    print('skybox written to', OUT)


if __name__ == '__main__':
    main()
