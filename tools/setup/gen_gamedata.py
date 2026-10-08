"""Create the game files MineStrike needs that are derived from Counter-Strike's own, from YOUR install.

The repository ships only its own files (gamedata/cstrike). Everything based on Valve's data is made
here, from the cstrike folder of your own copy of the game:
  maps/de_dust2_mc.bsp        the sealed shell the engine loads in classic mode (= maps/mc_dust2.bsp,
                              built by tools/voxelizer from your de_dust2.bsp)
  maps/de_dust2_mc.nav        de_dust2's bot navigation mesh, re-stamped for the shell map
  overviews/de_dust2_mc.bmp   de_dust2's overview picture
  delta.lst                   your delta.lst, widened so the mod's player fields reach the client
  listenserver.cfg            your listenserver.cfg, plus "exec csmc.cfg"

usage: python tools/setup/gen_gamedata.py <path to Half-Life/cstrike>
Safe to run again: it always starts from the originals (kept in cstrike/_csmc_originals).
"""
import os
import re
import shutil
import struct
import sys


def original(cstrike, rel):
    """The unmodified game file: backed up once into _csmc_originals before we first change it."""
    keep = os.path.join(cstrike, '_csmc_originals', rel)
    live = os.path.join(cstrike, rel)
    if not os.path.exists(keep):
        os.makedirs(os.path.dirname(keep), exist_ok=True)
        shutil.copyfile(live, keep)
    return keep


def patch_delta(text):
    # player and entity iuser4 carry the mod's flags and held item (32 bits instead of 2)
    text = re.sub(r'(DEFINE_DELTA\(\s*iuser4,\s*DT_INTEGER,\s*)2(\s*,\s*1\.0\s*\))', r'\g<1>32\2', text)
    # elytra boost timers (vuser1) next to iuser4 in the player block
    if 'vuser1[0]' not in text:
        text = re.sub(r'(\tDEFINE_DELTA\( iuser4, DT_INTEGER, 32, 1\.0 \),\n)',
                      r'\1\tDEFINE_DELTA( vuser1[0], DT_FLOAT | DT_SIGNED, 16, 1000.0 ),\n'
                      r'\tDEFINE_DELTA( vuser1[1], DT_FLOAT | DT_SIGNED, 16, 1000.0 ),\n'
                      r'\tDEFINE_DELTA( vuser1[2], DT_FLOAT | DT_SIGNED, 16, 1000.0 ),\n', text, count=1)
    # armor codes for the Minecraft player renderer travel in playerclass
    text = text.replace('\t// DEFINE_DELTA( playerclass, DT_INTEGER, 4, 1.0 )', '\tDEFINE_DELTA( playerclass, DT_INTEGER, 16, 1.0 ),')
    return text


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cs = sys.argv[1]
    maps = os.path.join(cs, 'maps')
    shell = os.path.join(maps, 'mc_dust2.bsp')
    if os.path.exists(shell):
        shutil.copyfile(shell, os.path.join(maps, 'de_dust2_mc.bsp'))
        nav = bytearray(open(os.path.join(maps, 'de_dust2.nav'), 'rb').read())
        magic, version = struct.unpack_from('<II', nav, 0)
        if magic != 0xFEEDFACE or version < 4:
            sys.exit('unexpected de_dust2.nav format')
        struct.pack_into('<I', nav, 8, os.path.getsize(os.path.join(maps, 'de_dust2_mc.bsp')))  # bsp size stamp
        open(os.path.join(maps, 'de_dust2_mc.nav'), 'wb').write(nav)
        print('maps: de_dust2_mc.bsp, de_dust2_mc.nav')
    else:
        print('maps: skipped (build maps/mc_dust2.bsp with tools/voxelizer first)')
    ov = os.path.join(cs, 'overviews')
    if os.path.exists(os.path.join(ov, 'de_dust2.bmp')):
        shutil.copyfile(os.path.join(ov, 'de_dust2.bmp'), os.path.join(ov, 'de_dust2_mc.bmp'))
        print('overview: de_dust2_mc.bmp')
    delta = open(original(cs, 'delta.lst'), encoding='latin-1', newline='').read()
    nl = '\r\n' if '\r\n' in delta else '\n'
    patched = patch_delta(delta.replace('\r\n', '\n')).replace('\n', nl)
    open(os.path.join(cs, 'delta.lst'), 'w', encoding='latin-1', newline='').write(patched)
    print('delta.lst: patched')
    cfg = open(original(cs, 'listenserver.cfg'), encoding='latin-1', newline='').read()
    if 'exec csmc.cfg' not in cfg:
        cfg = cfg.rstrip('\r\n') + '\n\nexec csmc.cfg\n'
    open(os.path.join(cs, 'listenserver.cfg'), 'w', encoding='latin-1', newline='').write(cfg)
    print('listenserver.cfg: exec csmc.cfg')


if __name__ == '__main__':
    main()
