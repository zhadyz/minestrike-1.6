#!/usr/bin/env python3
"""Fetch the Minecraft asset cache for the local CS 1.6 mod.

Local use only: the assets stay on this PC. Downloads come exclusively from
Mojang's official hosts (the same ones the launcher uses).

Steps (all resumable, safe to re-run):
  1. version manifest -> latest release (or --version) -> version JSON  -> assets/mc/meta/
  2. client jar (sha1-verified)                                         -> assets/mc/meta/client-<ver>.jar
     extract assets/minecraft/** (no .class)                            -> assets/mc/jar/
  3. asset index -> sound objects (minus music/records), sounds.json,
     lang/en_us.json (sha1-verified, saved by KEY path)                 -> assets/mc/objects/

Usage:  python fetch_mc_assets.py [--root Z:/dev/CSminecraft/assets/mc] [--version 26.3] [--workers 12]
Nothing is written outside --root and the log directory (both on Z:).
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import hashlib
import json
import os
import sys
import time
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

MANIFEST_URL = "https://piston-meta.mojang.com/mc/game/version_manifest_v2.json"
RESOURCES_BASE = "https://resources.download.minecraft.net"
ALLOWED_HOSTS = {
    "piston-meta.mojang.com",
    "piston-data.mojang.com",
    "launchermeta.mojang.com",
    "launcher.mojang.com",
    "resources.download.minecraft.net",
}
DEFAULT_ROOT = Path(r"Z:\dev\CSminecraft\assets\mc")
DEFAULT_LOG_DIR = Path(r"Z:\dev\scratch\csminecraft")
USER_AGENT = "csminecraft-local-asset-cache/1.0"

LOG_FH = None


def log(msg: str) -> None:
    line = f"[{time.strftime('%H:%M:%S')}] {msg}"
    print(line, flush=True)
    if LOG_FH:
        LOG_FH.write(line + "\n")
        LOG_FH.flush()


def check_host(url: str) -> None:
    host = urllib.parse.urlparse(url).hostname
    if host not in ALLOWED_HOSTS:
        raise RuntimeError(f"refusing non-Mojang host {host!r} ({url})")


def sha1_file(path: Path) -> str:
    h = hashlib.sha1()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def file_ok(path: Path, size: int | None, sha1: str | None) -> bool:
    if not path.is_file():
        return False
    if size is not None and path.stat().st_size != size:
        return False
    if sha1 is not None and sha1_file(path) != sha1:
        return False
    return True


def http_get(url: str, retries: int = 6, timeout: int = 60) -> bytes:
    check_host(url)
    delay = 1.0
    last: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=timeout) as resp:
                check_host(resp.geturl())  # no redirects off Mojang hosts
                return resp.read()
        except Exception as exc:  # noqa: BLE001 - network errors of all kinds are retried
            last = exc
            if attempt < retries:
                time.sleep(delay)
                delay = min(delay * 2, 30)
    raise RuntimeError(f"GET failed after {retries} tries: {url}: {last}")


def download_verified(url: str, dest: Path, sha1: str | None, size: int | None,
                      retries: int = 6) -> str:
    """Download to dest atomically with sha1/size verification. Returns status."""
    if file_ok(dest, size, sha1):
        return "skip"
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + ".part")
    delay = 1.0
    last = ""
    for attempt in range(1, retries + 1):
        data = http_get(url, retries=3)
        if size is not None and len(data) != size:
            last = f"size {len(data)} != {size}"
        elif sha1 is not None and hashlib.sha1(data).hexdigest() != sha1:
            last = "sha1 mismatch"
        else:
            tmp.write_bytes(data)
            os.replace(tmp, dest)
            return "ok"
        if attempt < retries:
            time.sleep(delay)
            delay = min(delay * 2, 30)
    raise RuntimeError(f"verification failed for {url}: {last}")


def step_meta(root: Path, version: str | None) -> tuple[str, dict]:
    meta = root / "meta"
    meta.mkdir(parents=True, exist_ok=True)
    manifest_raw = http_get(MANIFEST_URL)
    (meta / "version_manifest_v2.json").write_bytes(manifest_raw)
    manifest = json.loads(manifest_raw)
    ver_id = version or manifest["latest"]["release"]
    entry = next((v for v in manifest["versions"] if v["id"] == ver_id), None)
    if entry is None:
        raise RuntimeError(f"version {ver_id} not in manifest")
    vpath = meta / f"{ver_id}.json"
    download_verified(entry["url"], vpath, entry.get("sha1"), None)
    vjson = json.loads(vpath.read_text(encoding="utf-8"))
    (meta / "VERSION.txt").write_text(ver_id + "\n", encoding="utf-8")
    log(f"version {ver_id} (latest.release={manifest['latest']['release']})")
    return ver_id, vjson


def step_jar(root: Path, ver_id: str, vjson: dict) -> None:
    client = vjson["downloads"]["client"]
    jar = root / "meta" / f"client-{ver_id}.jar"
    st = download_verified(client["url"], jar, client["sha1"], client.get("size"))
    log(f"client jar {st}: {jar} ({jar.stat().st_size} bytes)")

    out = root / "jar"
    n_new = n_skip = 0
    with zipfile.ZipFile(jar) as zf:
        for info in zf.infolist():
            name = info.filename
            if info.is_dir() or not name.startswith("assets/minecraft/"):
                continue
            if name.endswith(".class") or ".." in Path(name).parts:
                continue
            dest = out / Path(name)
            if dest.is_file() and dest.stat().st_size == info.file_size:
                n_skip += 1
                continue
            dest.parent.mkdir(parents=True, exist_ok=True)
            tmp = dest.with_name(dest.name + ".part")
            with zf.open(info) as src, open(tmp, "wb") as dst:
                while chunk := src.read(1 << 20):
                    dst.write(chunk)
            os.replace(tmp, dest)
            n_new += 1
    log(f"jar extract: {n_new} written, {n_skip} already present -> {out}")


def wanted_key(key: str) -> bool:
    if key in ("minecraft/sounds.json", "minecraft/lang/en_us.json"):
        return True
    if not key.startswith("minecraft/sounds/"):
        return False
    return not (key.startswith("minecraft/sounds/music/")
                or key.startswith("minecraft/sounds/records/"))


def step_objects(root: Path, vjson: dict, workers: int) -> int:
    ai = vjson["assetIndex"]
    idx_path = root / "meta" / f"assetindex-{ai['id']}.json"
    download_verified(ai["url"], idx_path, ai.get("sha1"), ai.get("size"))
    index = json.loads(idx_path.read_text(encoding="utf-8"))
    objects = index["objects"]
    todo = sorted((k, v) for k, v in objects.items() if wanted_key(k))
    total_bytes = sum(v["size"] for _, v in todo)
    log(f"asset index {ai['id']}: {len(objects)} objects, {len(todo)} wanted "
        f"({total_bytes / 1e6:.1f} MB)")
    for special in ("minecraft/sounds.json", "minecraft/lang/en_us.json"):
        log(f"  index has {special}: {special in objects}")

    out = root / "objects"

    def job(item: tuple[str, dict]) -> tuple[str, str]:
        key, obj = item
        h = obj["hash"]
        url = f"{RESOURCES_BASE}/{h[:2]}/{h}"
        dest = out / Path(key)
        return key, download_verified(url, dest, h, obj["size"])

    counts = {"ok": 0, "skip": 0, "fail": 0}
    failures: list[str] = []
    done = 0
    with cf.ThreadPoolExecutor(max_workers=workers) as pool:
        futs = {pool.submit(job, it): it[0] for it in todo}
        for fut in cf.as_completed(futs):
            key = futs[fut]
            try:
                _, st = fut.result()
                counts[st] += 1
            except Exception as exc:  # noqa: BLE001
                counts["fail"] += 1
                failures.append(f"{key}: {exc}")
                log(f"FAIL {key}: {exc}")
            done += 1
            if done % 250 == 0 or done == len(todo):
                log(f"  objects {done}/{len(todo)} ok={counts['ok']} "
                    f"skip={counts['skip']} fail={counts['fail']}")
    if failures:
        (root / "meta" / "fetch_failures.txt").write_text("\n".join(failures) + "\n",
                                                           encoding="utf-8")
    log(f"objects done: {counts}")
    return counts["fail"]


def main() -> int:
    global LOG_FH
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    ap.add_argument("--log-dir", type=Path, default=DEFAULT_LOG_DIR)
    ap.add_argument("--version", default=None, help="version id (default: latest.release)")
    ap.add_argument("--workers", type=int, default=12)
    ap.add_argument("--skip-jar", action="store_true")
    ap.add_argument("--skip-objects", action="store_true")
    args = ap.parse_args()

    for p in (args.root, args.log_dir):
        if str(p.resolve()).upper().startswith("C:"):
            raise SystemExit(f"refusing to write to C: ({p})")
    args.root.mkdir(parents=True, exist_ok=True)
    args.log_dir.mkdir(parents=True, exist_ok=True)
    LOG_FH = open(args.log_dir / "fetch_mc_assets.log", "a", encoding="utf-8")
    log(f"=== fetch_mc_assets start root={args.root}")

    ver_id, vjson = step_meta(args.root, args.version)
    if not args.skip_jar:
        step_jar(args.root, ver_id, vjson)
    fails = 0
    if not args.skip_objects:
        fails = step_objects(args.root, vjson, args.workers)
    log(f"=== done (failures={fails})")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
