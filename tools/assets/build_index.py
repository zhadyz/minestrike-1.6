#!/usr/bin/env python3
"""Write assets/mc/INDEX.md and assets/mc/sound_events.json from the local cache.

Run after fetch_mc_assets.py and convert_sounds.py. Reads only; writes the two files.
Usage: python build_index.py [--root Z:/dev/CSminecraft/assets/mc]
"""
from __future__ import annotations

import argparse
import json
import time
from pathlib import Path

DEFAULT_ROOT = Path(r"Z:\dev\CSminecraft\assets\mc")

TEXTURE_CHECKS = [
    "item/diamond_sword.png", "item/diamond_axe.png", "item/diamond_helmet.png",
    "item/diamond_chestplate.png", "item/diamond_leggings.png", "item/diamond_boots.png",
    "item/elytra.png", "item/firework_rocket.png",
    "entity/experience_orb.png", "entity/elytra.png",
    "entity/equipment/humanoid/diamond.png", "entity/equipment/humanoid_leggings/diamond.png",
    "gui/sprites/hud/heart/full.png", "gui/sprites/hud/armor_full.png",
    "gui/sprites/hud/experience_bar_progress.png", "gui/sprites/hud/hotbar.png",
    "gui/sprites/hud/hotbar_selection.png", "entity/player/wide/steve.png",
    "block/oak_door_top.png", "block/sandstone.png",
]
# Where 26.x actually keeps the textures the original checklist expected elsewhere.
TEXTURE_ALTERNATIVES = {
    "entity/experience_orb.png": ["entity/experience/experience_orb.png"],
    "entity/elytra.png": ["entity/equipment/wings/elytra.png"],
}
SOUND_CHECKS = [
    "entity.experience_orb.pickup", "entity.player.levelup", "entity.player.attack.strong",
    "entity.player.attack.sweep", "entity.player.attack.crit",
    "entity.player.attack.knockback", "entity.player.attack.weak", "entity.player.hurt",
    "entity.player.death", "entity.generic.death", "item.armor.equip_diamond",
    "item.armor.equip_elytra", "item.elytra.flying", "entity.firework_rocket.launch",
    "entity.firework_rocket.blast", "entity.firework_rocket.large_blast",
    "entity.firework_rocket.twinkle", "entity.item.pickup", "block.wooden_door.break",
    "block.wood.break", "block.stone.break", "block.sand.break", "block.gravel.break",
    "block.wood.hit", "block.stone.hit", "entity.zombie.death", "entity.zombie.hurt",
]
SOUND_ALTERNATIVES = {
    # No door-specific break event exists: a wooden door breaks with the wood sound type.
    "block.wooden_door.break": ["block.wood.break", "block.wooden_door.open",
                                "block.wooden_door.close", "entity.zombie.break_wooden_door"],
}
OPTIONAL_FIELDS = ("volume", "pitch", "weight", "stream", "attenuation_distance", "preload")


def strip_ns(name: str) -> str:
    return name.split(":", 1)[1] if ":" in name else name


def resolve_event(events: dict, name: str, root: Path, chain: tuple = ()) -> list[dict]:
    out: list[dict] = []
    for entry in events.get(name, {}).get("sounds", []):
        e = {"name": entry} if isinstance(entry, str) else dict(entry)
        target = strip_ns(e["name"])
        if e.get("type") == "event":
            if target in chain or target == name:
                continue  # cycle guard
            for sub in resolve_event(events, target, root, chain + (name,)):
                sub = dict(sub)
                sub.setdefault("via", [])
                sub["via"] = [target] + sub["via"]
                for f in ("volume", "pitch"):
                    if f in e:
                        sub[f] = round(sub.get(f, 1.0) * e[f], 6)
                out.append(sub)
            continue
        ogg_rel = f"minecraft/sounds/{target}.ogg"
        wav_rel = f"minecraft/sounds/{target}.wav"
        item = {
            "wav": wav_rel,
            "ogg": ogg_rel,
            "exists": (root / "wav" / wav_rel).is_file(),
        }
        for f in OPTIONAL_FIELDS:
            if f in e:
                item[f] = e[f]
        out.append(item)
    return out


def dir_stats(path: Path, pattern: str = "*") -> tuple[int, int]:
    n = size = 0
    for p in path.rglob(pattern):
        if p.is_file():
            n += 1
            size += p.stat().st_size
    return n, size


def mb(n: int) -> str:
    return f"{n / (1024 * 1024):.1f} MiB"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    root = ap.parse_args().root

    version = (root / "meta" / "VERSION.txt").read_text(encoding="utf-8").strip()
    mc_jar = root / "jar" / "assets" / "minecraft"
    tex = mc_jar / "textures"
    events = json.loads((root / "objects" / "minecraft" / "sounds.json")
                        .read_text(encoding="utf-8"))

    # sound_events.json
    sound_events = {}
    for ev in sorted(events):
        sound_events[ev] = {
            "subtitle": events[ev].get("subtitle"),
            "replace": events[ev].get("replace", False),
            "sounds": resolve_event(events, ev, root),
        }
    payload = {
        "version": version,
        "wav_root": "assets/mc/wav",
        "note": ("wav paths are relative to assets/mc/wav/; exists=false means the source "
                 "was deliberately not fetched (music/ and records/ are excluded). "
                 "Fields volume/pitch/weight/stream/attenuation_distance/preload are copied "
                 "from sounds.json only when present (Minecraft defaults: volume 1, pitch 1, "
                 "weight 1, attenuation_distance 16). For entries reached through a "
                 "type=event reference, 'via' lists the referenced events and volume/pitch "
                 "are multiplied through."),
        "events": sound_events,
    }
    (root / "sound_events.json").write_text(json.dumps(payload, indent=1), encoding="utf-8")
    n_events = len(sound_events)
    n_events_playable = sum(1 for v in sound_events.values()
                            if any(s["exists"] for s in v["sounds"]))
    n_missing_refs = sum(1 for v in sound_events.values() for s in v["sounds"]
                         if not s["exists"])

    # counts
    n_png, s_png = dir_stats(tex, "*.png")
    n_models, s_models = dir_stats(mc_jar / "models", "*.json")
    n_bs, _ = dir_stats(mc_jar / "blockstates", "*.json")
    n_items_def, _ = dir_stats(mc_jar / "items", "*.json")
    n_jar, s_jar = dir_stats(root / "jar")
    n_ogg, s_ogg = dir_stats(root / "objects", "*.ogg")
    n_obj, s_obj = dir_stats(root / "objects")
    n_wav, s_wav = dir_stats(root / "wav", "*.wav")
    n_meta, s_meta = dir_stats(root / "meta")
    lang_jar = (mc_jar / "lang" / "en_us.json").is_file()

    L: list[str] = []
    w = L.append
    w("# Minecraft asset cache (local only, never distributed)")
    w("")
    w(f"- Minecraft version: **{version}** (latest.release on "
      f"{time.strftime('%Y-%m-%d')})")
    w("- Source: Mojang official hosts only (piston-meta / piston-data / "
      "resources.download.minecraft.net), sha1-verified.")
    w("- Rebuild: `python Z:\\dev\\CSminecraft\\tools\\assets\\fetch_mc_assets.py` then "
      "`convert_sounds.py` then `build_index.py` (all resumable).")
    w("")
    w("## Layout")
    w("")
    w("| Path | Contents |")
    w("|---|---|")
    w(f"| `meta/` | version manifest, `{version}.json`, asset index, `client-{version}.jar` |")
    w("| `jar/assets/minecraft/` | everything under `assets/minecraft/` in the client jar "
      "(textures, models, blockstates, items, lang, atlases, font, shaders, ...) |")
    w("| `objects/minecraft/` | asset-index objects by key path: `sounds.json` + "
      "`sounds/**.ogg` (music/ and records/ excluded) |")
    w("| `wav/minecraft/sounds/` | GoldSrc WAVs mirroring `objects/`: mono, PCM s16le, "
      "22050 Hz, only fmt+data chunks |")
    w("| `sound_events.json` | every sounds.json event -> resolved wav list "
      "(+ volume/pitch/weight) |")
    w("")
    w("## Counts and sizes")
    w("")
    w("| Item | Count | Size |")
    w("|---|---:|---:|")
    w(f"| Textures (`jar/.../textures/**.png`) | {n_png} | {mb(s_png)} |")
    w(f"| Models (`jar/.../models/**.json`) | {n_models} | {mb(s_models)} |")
    w(f"| Blockstates | {n_bs} | |")
    w(f"| Item definitions (`items/`) | {n_items_def} | |")
    w(f"| All files extracted from jar | {n_jar} | {mb(s_jar)} |")
    w(f"| Sounds .ogg (`objects/`) | {n_ogg} | {mb(s_ogg)} |")
    w(f"| All asset-index objects fetched | {n_obj} | {mb(s_obj)} |")
    w(f"| Sounds .wav (`wav/`) | {n_wav} | {mb(s_wav)} |")
    w(f"| `meta/` (incl. client jar) | {n_meta} | {mb(s_meta)} |")
    w(f"| sounds.json events | {n_events} ({n_events_playable} with at least one local "
      f"wav) | |")
    w("")
    w(f"`lang/en_us.json`: in the jar at `jar/assets/minecraft/lang/en_us.json` "
      f"({'yes' if lang_jar else 'NO'}); it is not in the asset index.")
    w(f"Unresolved sound references (all music/records, excluded on purpose): "
      f"{n_missing_refs}.")
    w("")
    w("## Texture checklist")
    w("")
    w("Paths are relative to `jar/assets/minecraft/textures/`.")
    w("")
    w("| Texture | Exists | Note |")
    w("|---|---|---|")
    for t in TEXTURE_CHECKS:
        ok = (tex / t).is_file()
        note = ""
        if not ok:
            alts = [a for a in TEXTURE_ALTERNATIVES.get(t, []) if (tex / a).is_file()]
            note = ("use `" + "`, `".join(alts) + "`") if alts else "no equivalent found"
        elif t.startswith("entity/equipment/"):
            note = ("26.x armor layer: humanoid = helmet/chest/boots, "
                    "humanoid_leggings = leggings")
        w(f"| `{t}` | {'yes' if ok else 'no'} | {note} |")
    w("")
    w("## Sound checklist")
    w("")
    w("Event keys from `objects/minecraft/sounds.json`. Files are relative to "
      "`objects/` (ogg) and `wav/` (wav); both trees use the same path with "
      "a different extension.")
    w("")
    for ev in SOUND_CHECKS:
        if ev not in sound_events:
            alts = [a for a in SOUND_ALTERNATIVES.get(ev, []) if a in sound_events]
            w(f"### `{ev}`: no such event")
            w("")
            if alts:
                w("Closest real events: " + ", ".join(f"`{a}`" for a in alts) +
                  ". A wooden door's break sound is `block.wood.break` (wood sound type).")
            w("")
            continue
        snds = sound_events[ev]["sounds"]
        all_ok = all(s["exists"] and (root / "objects" / s["ogg"]).is_file() for s in snds)
        w(f"### `{ev}`: yes ({len(snds)} file{'s' if len(snds) != 1 else ''}, "
          f"ogg+wav {'all present' if all_ok else 'SOME MISSING'})")
        w("")
        for s in snds:
            stem = s["wav"][:-4]
            extras = ", ".join(f"{f}={s[f]}" for f in OPTIONAL_FIELDS if f in s)
            ogg_ok = (root / "objects" / s["ogg"]).is_file()
            mark = "ogg+wav" if (ogg_ok and s["exists"]) else (
                f"ogg={'yes' if ogg_ok else 'no'} wav={'yes' if s['exists'] else 'no'}")
            w(f"- `{stem}.ogg|.wav` ({mark}{'; ' + extras if extras else ''})")
        w("")
    (root / "INDEX.md").write_text("\n".join(L) + "\n", encoding="utf-8")
    print(f"wrote {root / 'INDEX.md'} and {root / 'sound_events.json'} "
          f"({n_events} events)")


if __name__ == "__main__":
    main()
