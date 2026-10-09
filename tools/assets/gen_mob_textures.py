#!/usr/bin/env python3
"""The wither skeleton skull as a block: its faces cut out of Minecraft's own wither skeleton texture (from
your Minecraft assets, like every other texture here) and written beside the block textures of the game
copies. Run after the Minecraft textures are in place; needs Pillow."""
import os
import sys

from PIL import Image

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SRC = os.path.join(ROOT, 'assets', 'mc', 'jar', 'assets', 'minecraft', 'textures', 'entity', 'skeleton', 'wither_skeleton.png')
GAMES = [os.path.join(os.environ['MINESTRIKE_CSTRIKE'], 'mc', 'textures')] if os.environ.get('MINESTRIKE_CSTRIKE') else [
    os.path.join(ROOT, g, 'Half-Life', 'cstrike', 'mc', 'textures') for g in ('game', 'game_test')]
# (name, box in the 64x32 skin: the head's front and its top)
FACES = [('wither_skull_front', (8, 8, 16, 16)), ('wither_skull_top', (8, 0, 16, 8))]


def main():
    made = 0
    for tex in GAMES:
        src = SRC if os.path.exists(SRC) else os.path.join(tex, 'entity', 'skeleton', 'wither_skeleton.png')
        if not os.path.exists(src) or not os.path.isdir(os.path.join(tex, 'block')):
            continue
        skin = Image.open(src).convert('RGBA')
        for name, box in FACES:
            skin.crop(box).resize((16, 16), Image.NEAREST).save(os.path.join(tex, 'block', name + '.png'))
            made += 1
    print('wither skull block textures written:', made)
    return 0 if made else 1


if __name__ == '__main__':
    sys.exit(main())
