#!/usr/bin/env python3
"""What MineStrike needs of Minecraft, fetched for the game copy in one go (the installer runs this).

From Mojang's own public servers, for use with the Minecraft you own, into your game copy only:
  - the textures (out of the client jar) into <cstrike>/mc/textures
  - the sounds the mod plays (the ones named in code/shared/mc_sounds_gen.cpp, about 340 of the 4,800),
    decoded to the mono 22 kHz WAV files the engine plays, into <cstrike>/sound/mc
Then the pictures made from them and from Counter-Strike's own files: the gun icons, the Minecraft sky, the
wither skull block.

The Minecraft version is pinned to the one the prebuilt libraries were made against (their sound table
names files of that version).

Needs: miniaudio (decodes the .ogg files), Pillow, numpy, scipy. The installer runs it through uv, which
brings those along:  uv run --with miniaudio --with pillow --with numpy --with scipy tools/install/mc_setup.py --cstrike <path>
"""
import argparse
import concurrent.futures
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import urllib.request
import wave
import zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MANIFEST = 'https://piston-meta.mojang.com/mc/game/version_manifest_v2.json'
RESOURCES = 'https://resources.download.minecraft.net'
VERSION = '26.3'


def get(url, tries=4):
    last = None
    for _ in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': 'MineStrike-setup'}), timeout=60) as r:
                return r.read()
        except Exception as e:  # a dropped connection: again
            last = e
    raise SystemExit(f'could not download {url}: {last}')


def cached(path, url, sha1=None):
    if os.path.exists(path) and (sha1 is None or hashlib.sha1(open(path, 'rb').read()).hexdigest() == sha1):
        return open(path, 'rb').read()
    data = get(url)
    if sha1 and hashlib.sha1(data).hexdigest() != sha1:
        raise SystemExit(f'{url}: the download does not match its checksum')
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(data)
    return data


def needed_sounds():
    src = open(os.path.join(ROOT, 'code', 'shared', 'mc_sounds_gen.cpp'), encoding='utf-8').read()
    return sorted(set(re.findall(r'"mc/([^"]+)\.wav"', src)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--cstrike', required=True, help='the cstrike folder of your game copy')
    ap.add_argument('--work', default=os.path.join(ROOT, 'work', 'minecraft'), help='where downloads are kept between runs')
    ap.add_argument('--version', default=VERSION)
    args = ap.parse_args()
    cs = os.path.abspath(args.cstrike)
    if not os.path.isdir(cs):
        raise SystemExit(f'{cs} is not a folder')
    os.makedirs(args.work, exist_ok=True)

    print(f'Minecraft {args.version}: looking it up at Mojang', flush=True)
    manifest = json.loads(get(MANIFEST))
    entry = next((v for v in manifest['versions'] if v['id'] == args.version), None)
    if not entry:
        raise SystemExit(f'Mojang lists no Minecraft {args.version}')
    meta = json.loads(cached(os.path.join(args.work, f'{args.version}.json'), entry['url']))

    # 1. textures, out of the client jar
    tex = os.path.join(cs, 'mc', 'textures')
    marker = os.path.join(tex, f'.minecraft-{args.version}')
    if os.path.exists(marker):
        print('textures: already there', flush=True)
    else:
        client = meta['downloads']['client']
        print(f'textures: downloading the client ({client["size"] // 1048576} MB)', flush=True)
        jar = cached(os.path.join(args.work, f'client-{args.version}.jar'), client['url'], client['sha1'])
        n = 0
        with zipfile.ZipFile(io.BytesIO(jar)) as z:
            for name in z.namelist():
                if not name.startswith('assets/minecraft/textures/') or name.endswith('/'):
                    continue
                dst = os.path.join(tex, *name[len('assets/minecraft/textures/'):].split('/'))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                open(dst, 'wb').write(z.read(name))
                n += 1
        open(marker, 'w').write('')
        print(f'textures: {n} files', flush=True)

    # 2. the sounds the mod plays
    import miniaudio
    index = json.loads(cached(os.path.join(args.work, f'assets-{args.version}.json'), meta['assetIndex']['url'], meta['assetIndex'].get('sha1')))['objects']
    wanted = needed_sounds()
    out = os.path.join(cs, 'sound', 'mc')

    def one(rel):
        dst = os.path.join(out, *rel.split('/')) + '.wav'
        if os.path.exists(dst) and os.path.getsize(dst) > 44:
            return 'had'
        obj = index.get(f'minecraft/sounds/{rel}.ogg')
        if not obj:
            return f'missing {rel}'
        h = obj['hash']
        ogg = cached(os.path.join(args.work, 'objects', h), f'{RESOURCES}/{h[:2]}/{h}', h)
        pcm = miniaudio.decode(ogg, output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=22050)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with wave.open(dst, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(22050)
            w.writeframes(pcm.samples.tobytes())
        return 'made'

    print(f'sounds: {len(wanted)} wanted', flush=True)
    with concurrent.futures.ThreadPoolExecutor(8) as ex:
        results = list(ex.map(one, wanted))
    missing = [r for r in results if r.startswith('missing')]
    print(f'sounds: {results.count("made")} made, {results.count("had")} already there, {len(missing)} missing', flush=True)
    if missing:
        print('  ' + ', '.join(m[8:] for m in missing[:8]), flush=True)
        if len(missing) > len(wanted) // 10:
            raise SystemExit('too many sounds are missing: is this the Minecraft version the libraries were built for?')

    # 3. what is made from them, and from Counter-Strike's own files
    env = dict(os.environ, MINESTRIKE_CSTRIKE=cs)
    for script in ('gun_icons.py', 'make_skybox.py', 'gen_mob_textures.py'):
        r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'assets', script)], env=env, capture_output=True, text=True)
        tail = (r.stdout.strip().splitlines() or r.stderr.strip().splitlines() or [''])[-1]
        print(f'{script}: {tail if r.returncode == 0 else "FAILED: " + tail}', flush=True)
        if r.returncode != 0:
            print(r.stderr[-800:], flush=True)
            raise SystemExit(1)
    print('Minecraft assets: done', flush=True)


if __name__ == '__main__':
    main()
