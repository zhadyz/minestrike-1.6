#!/usr/bin/env python3
"""Fetch Minecraft's music for the mod and convert it to MP3 for the engine's MP3 player.

Local use only, like fetch_mc_assets.py: files come from Mojang's asset host for your own install and
stay on this PC. Tracks are the ones sounds.json lists for these music events:
  music.overworld.desert  (Dust II is a desert map; the default pool)
  music.game              (the general overworld pool)
  music.creative          (played in creative mode)

Output: assets/mc/music/<track>.mp3 plus tracks.txt ("<pool> <track> <weight> <seconds>" per line),
installed into cstrike/mc/music/ of both game copies.
Usage:  python tools/assets/fetch_music.py [--workers 6]
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import json
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from fetch_mc_assets import RESOURCES_BASE, download_verified  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets" / "mc"
OUT = ASSETS / "music"
GAMES = [ROOT / "game_test" / "Half-Life" / "cstrike", ROOT / "game" / "Half-Life" / "cstrike"]
POOLS = {"music.overworld.desert": "desert", "music.game": "game", "music.creative": "creative"}


def pool_tracks(sounds: dict, event: str, seen: set[str] | None = None) -> list[tuple[str, int]]:
    """(sound path, weight) for an event, following nested event references (music.creative includes
    music.game)."""
    seen = seen or set()
    if event in seen or event not in sounds:
        return []
    seen.add(event)
    out = []
    for s in sounds[event]["sounds"]:
        name = s if isinstance(s, str) else s["name"]
        weight = 1 if isinstance(s, str) else int(s.get("weight", 1))
        if isinstance(s, dict) and s.get("type") == "event":
            out += pool_tracks(sounds, name, seen)
        elif name in sounds:  # a bare event name (music.creative lists "music.game")
            out += pool_tracks(sounds, name, seen)
        else:
            out.append((name, weight))
    return out


def duration(path: Path) -> float:
    r = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0",
                        str(path)], capture_output=True, text=True, check=True)
    return float(r.stdout.strip())


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--workers", type=int, default=6)
    args = ap.parse_args()
    sounds = json.loads((ASSETS / "objects" / "minecraft" / "sounds.json").read_text(encoding="utf-8"))
    index_file = sorted((ASSETS / "meta").glob("assetindex-*.json"))[-1]
    objects = json.loads(index_file.read_text(encoding="utf-8"))["objects"]

    pools: dict[str, list[tuple[str, int]]] = {}
    for event, pool in POOLS.items():
        tracks = pool_tracks(sounds, event)
        if pool == "creative":  # music.game is already its own pool; keep only the creative-only tracks
            tracks = [t for t in tracks if "/creative/" in t[0]]
        pools[pool] = tracks
    wanted = sorted({name for tracks in pools.values() for name, _ in tracks})
    print(f"{len(wanted)} tracks")

    def fetch(name: str) -> tuple[str, float]:
        key = f"minecraft/sounds/{name}.ogg"
        obj = objects[key]
        h = obj["hash"]
        ogg = ASSETS / "objects" / key
        download_verified(f"{RESOURCES_BASE}/{h[:2]}/{h}", ogg, h, obj["size"])
        mp3 = OUT / (Path(name).name + ".mp3")
        if not mp3.exists() or mp3.stat().st_mtime < ogg.stat().st_mtime:
            OUT.mkdir(parents=True, exist_ok=True)
            tmp = mp3.with_suffix(".part.mp3")
            subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(ogg), "-ac", "2", "-ar", "44100",
                            "-c:a", "libmp3lame", "-b:a", "160k", str(tmp)], check=True)
            tmp.replace(mp3)
        return name, duration(mp3)

    lengths: dict[str, float] = {}
    with cf.ThreadPoolExecutor(args.workers) as ex:
        for name, secs in ex.map(fetch, wanted):
            lengths[name] = secs
            print(f"  {Path(name).name}: {secs:.0f} s")

    lines = [f"{pool} {Path(name).name} {weight} {lengths[name]:.1f}"
             for pool, tracks in pools.items() for name, weight in tracks]
    (OUT / "tracks.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    for g in GAMES:
        if not g.is_dir():
            continue
        dst = g / "mc" / "music"
        dst.mkdir(parents=True, exist_ok=True)
        for f in OUT.iterdir():
            if f.suffix in (".mp3", ".txt") and ".part" not in f.name:
                t = dst / f.name
                if not t.exists() or t.stat().st_size != f.stat().st_size or f.name == "tracks.txt":
                    shutil.copy2(f, t)
        print("installed", dst)
    return 0


if __name__ == "__main__":
    sys.exit(main())
