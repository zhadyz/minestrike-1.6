#!/usr/bin/env python3
"""Convert the cached Minecraft .ogg sounds to GoldSrc-compatible WAVs.

Input : <root>/objects/**.ogg   (from fetch_mc_assets.py)
Output: <root>/wav/**.wav       (same tree, .ogg -> .wav)
Format: RIFF WAVE, PCM (fmt tag 1, 16-byte fmt chunk), mono, 16-bit signed, 22050 Hz,
        only 'fmt ' + 'data' chunks (no LIST/INFO metadata).

Resumable: a WAV is skipped when it is newer than its source and its header validates.
Runs ~8 ffmpeg processes concurrently. Local use only.

Usage: python convert_sounds.py [--root Z:/dev/CSminecraft/assets/mc] [--jobs 8] [--force]
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_ROOT = Path(r"Z:\dev\CSminecraft\assets\mc")
DEFAULT_LOG_DIR = Path(r"Z:\dev\scratch\csminecraft")
FFMPEG_ARGS = ["-ac", "1", "-ar", "22050", "-c:a", "pcm_s16le", "-map_metadata", "-1",
               "-fflags", "+bitexact", "-flags:a", "+bitexact", "-f", "wav"]


def wav_valid(path: Path) -> tuple[bool, str]:
    """True when the file is a plain PCM16 mono 22050 Hz RIFF with only fmt+data."""
    try:
        data = path.read_bytes()
    except OSError as exc:
        return False, str(exc)
    if len(data) < 44 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        return False, "not RIFF/WAVE"
    if struct.unpack_from("<I", data, 4)[0] != len(data) - 8:
        return False, "RIFF size mismatch"
    pos, chunks = 12, []
    while pos + 8 <= len(data):
        cid = data[pos:pos + 4]
        size = struct.unpack_from("<I", data, pos + 4)[0]
        chunks.append((cid, pos + 8, size))
        pos += 8 + size + (size & 1)
    ids = [c[0] for c in chunks]
    if ids != [b"fmt ", b"data"]:
        return False, f"chunks {ids}"
    _, fpos, fsize = chunks[0]
    if fsize != 16:
        return False, f"fmt size {fsize}"
    tag, ch, rate, _, align, bits = struct.unpack_from("<HHIIHH", data, fpos)
    if (tag, ch, rate, align, bits) != (1, 1, 22050, 2, 16):
        return False, f"fmt {(tag, ch, rate, align, bits)}"
    _, dpos, dsize = chunks[1]
    if dpos + dsize > len(data):
        return False, "data truncated"
    return True, "ok"


def convert(ffmpeg: str, src: Path, dst: Path, force: bool) -> tuple[str, str]:
    if (not force and dst.is_file() and dst.stat().st_mtime >= src.stat().st_mtime
            and wav_valid(dst)[0]):
        return "skip", ""
    dst.parent.mkdir(parents=True, exist_ok=True)
    tmp = dst.with_name(dst.name + ".part")
    cmd = [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-y", "-i", str(src),
           *FFMPEG_ARGS, str(tmp)]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        tmp.unlink(missing_ok=True)
        return "fail", proc.stderr.strip()[-400:]
    ok, why = wav_valid(tmp)
    if not ok:
        tmp.unlink(missing_ok=True)
        return "fail", f"invalid output: {why}"
    os.replace(tmp, dst)
    return "ok", ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    ap.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--force", action="store_true")
    args = ap.parse_args()

    for p in (args.root, args.log_dir):
        if str(p.resolve()).upper().startswith("C:"):
            raise SystemExit(f"refusing to write to C: ({p})")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise SystemExit("ffmpeg not on PATH")
    args.log_dir.mkdir(parents=True, exist_ok=True)
    logf = open(args.log_dir / "convert_sounds.log", "a", encoding="utf-8")

    def log(msg: str) -> None:
        line = f"[{time.strftime('%H:%M:%S')}] {msg}"
        print(line, flush=True)
        logf.write(line + "\n")
        logf.flush()

    src_root, dst_root = args.root / "objects", args.root / "wav"
    sources = sorted(src_root.rglob("*.ogg"))
    log(f"=== convert_sounds: {len(sources)} ogg under {src_root} -> {dst_root} "
        f"(jobs={args.jobs})")
    counts = {"ok": 0, "skip": 0, "fail": 0}
    failures: list[str] = []
    with cf.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futs = {}
        for src in sources:
            dst = dst_root / src.relative_to(src_root).with_suffix(".wav")
            futs[pool.submit(convert, ffmpeg, src, dst, args.force)] = src
        for i, fut in enumerate(cf.as_completed(futs), 1):
            src = futs[fut]
            try:
                st, err = fut.result()
            except Exception as exc:  # noqa: BLE001
                st, err = "fail", repr(exc)
            counts[st] += 1
            if st == "fail":
                failures.append(f"{src.relative_to(src_root)}: {err}")
                log(f"FAIL {src}: {err}")
            if i % 500 == 0 or i == len(sources):
                log(f"  {i}/{len(sources)} {counts}")
    fail_file = args.root / "meta" / "convert_failures.txt"
    if failures:
        fail_file.write_text("\n".join(failures) + "\n", encoding="utf-8")
    elif fail_file.exists():
        fail_file.unlink()
    log(f"=== done {counts}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
