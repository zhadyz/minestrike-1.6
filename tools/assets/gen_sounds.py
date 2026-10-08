"""Copy the Minecraft sound events the mod uses into cstrike/sound/mc/ and generate the shared C++ table.

Each event becomes an enum entry (MCS_<NAME>) with its list of wav variants (Minecraft picks one at random,
weighted) plus base volume/pitch. Paths are relative to the game's sound/ folder, e.g. "mc/random/orb.wav".
"""
import json
import os
import shutil

ROOT = 'Z:/dev/CSminecraft'
EVENTS_JSON = f'{ROOT}/assets/mc/sound_events.json'
WAV_ROOT = f'{ROOT}/assets/mc/wav'
GAME_SOUNDS = [f'{ROOT}/game/Half-Life/cstrike/sound', f'{ROOT}/game_test/Half-Life/cstrike/sound']
OUT_H = f'{ROOT}/code/shared/mc_sounds_gen.h'
OUT_CPP = f'{ROOT}/code/shared/mc_sounds_gen.cpp'

# enum name -> Minecraft sound event
EVENTS = [
    ('XP_ORB', 'entity.experience_orb.pickup'),
    ('LEVELUP', 'entity.player.levelup'),
    ('ATTACK_STRONG', 'entity.player.attack.strong'),
    ('ATTACK_SWEEP', 'entity.player.attack.sweep'),
    ('ATTACK_CRIT', 'entity.player.attack.crit'),
    ('ATTACK_KNOCKBACK', 'entity.player.attack.knockback'),
    ('ATTACK_WEAK', 'entity.player.attack.weak'),
    ('ATTACK_NODAMAGE', 'entity.player.attack.nodamage'),
    ('PLAYER_HURT', 'entity.player.hurt'),
    ('PLAYER_DEATH', 'entity.player.death'),
    ('PLAYER_SMALL_FALL', 'entity.player.small_fall'),
    ('PLAYER_BIG_FALL', 'entity.player.big_fall'),
    ('PLAYER_BURP', 'entity.player.burp'),
    ('GENERIC_EAT', 'entity.generic.eat'),
    ('EQUIP_DIAMOND', 'item.armor.equip_diamond'),
    ('EQUIP_ELYTRA', 'item.armor.equip_elytra'),
    ('EQUIP_GENERIC', 'item.armor.equip_generic'),
    ('ELYTRA_FLYING', 'item.elytra.flying'),
    ('FIREWORK_LAUNCH', 'entity.firework_rocket.launch'),
    ('FIREWORK_BLAST', 'entity.firework_rocket.blast'),
    ('FIREWORK_LARGE_BLAST', 'entity.firework_rocket.large_blast'),
    ('FIREWORK_TWINKLE', 'entity.firework_rocket.twinkle'),
    ('ITEM_PICKUP', 'entity.item.pickup'),
    ('ITEM_BREAK', 'entity.item.break'),
    ('TOTEM_USE', 'item.totem.use'),
    ('ARROW_SHOOT', 'entity.arrow.shoot'),
    ('ARROW_HIT', 'entity.arrow.hit'),
    ('ARROW_HIT_PLAYER', 'entity.arrow.hit_player'),
    ('BOW_PULL', 'item.crossbow.loading_middle'),
    ('EXPLODE', 'entity.generic.explode'),
    ('TNT_PRIMED', 'entity.tnt.primed'),
    ('ZOMBIE_HURT', 'entity.zombie.hurt'),
    ('ZOMBIE_DEATH', 'entity.zombie.death'),
    ('UI_CLICK', 'ui.button.click'),
    ('THUNDER', 'entity.lightning_bolt.thunder'),
    ('DOOR_WOOD_OPEN', 'block.wooden_door.open'),
    ('DOOR_WOOD_CLOSE', 'block.wooden_door.close'),
    ('DOOR_IRON_OPEN', 'block.iron_door.open'),
    ('DOOR_IRON_CLOSE', 'block.iron_door.close'),
    ('CHEST_OPEN', 'block.chest.open'),
    ('NOTE_PLING', 'block.note_block.pling'),
    ('ANVIL_LAND', 'block.anvil.land'),
    ('CREEPER_PRIMED', 'entity.creeper.primed'),
    ('CREEPER_HURT', 'entity.creeper.hurt'),
    ('CREEPER_DEATH', 'entity.creeper.death'),
    ('HUSK_HURT', 'entity.husk.hurt'),
    ('HUSK_DEATH', 'entity.husk.death'),
    ('DROWNED_HURT', 'entity.drowned.hurt'),
    ('DROWNED_DEATH', 'entity.drowned.death'),
    ('TOAST_IN', 'ui.toast.in'),
    ('TOAST_CHALLENGE', 'ui.toast.challenge_complete'),
    ('ENDERMAN_HURT', 'entity.enderman.hurt'),
    ('ENDERMAN_DEATH', 'entity.enderman.death'),
    ('ENDERMAN_TELEPORT', 'entity.enderman.teleport'),
    ('ENDERMAN_STARE', 'entity.enderman.stare'),
    ('LEVER_CLICK', 'block.lever.click'),
    ('STONE_BUTTON_ON', 'block.stone_button.click_on'),
    ('STONE_BUTTON_OFF', 'block.stone_button.click_off'),
    ('WOOD_BUTTON_ON', 'block.wooden_button.click_on'),
    ('WOOD_BUTTON_OFF', 'block.wooden_button.click_off'),
    ('STONE_PLATE_ON', 'block.stone_pressure_plate.click_on'),
    ('STONE_PLATE_OFF', 'block.stone_pressure_plate.click_off'),
    ('WOOD_PLATE_ON', 'block.wooden_pressure_plate.click_on'),
    ('WOOD_PLATE_OFF', 'block.wooden_pressure_plate.click_off'),
    ('CROSSBOW_LOAD_START', 'item.crossbow.loading_start'),
    ('CROSSBOW_LOAD_END', 'item.crossbow.loading_end'),
    ('CROSSBOW_SHOOT', 'item.crossbow.shoot'),
    ('FLINT_USE', 'item.flintandsteel.use'),
    ('FIRE_AMBIENT', 'block.fire.ambient'),
    ('PLAYER_HURT_FIRE', 'entity.player.hurt_on_fire'),
]
# Block sound groups, in the order of mcw::SOUND_* in mc_blocks.h
GROUPS = ['stone', 'wood', 'sand', 'gravel', 'grass', 'glass', 'metal', 'wool']
for g in GROUPS:
    for kind in ('break', 'hit', 'place', 'step'):
        EVENTS.append((f'{g.upper()}_{kind.upper()}', f'block.{g}.{kind}'))

data = json.load(open(EVENTS_JSON))['events']
entries = []
copied = 0
for enum_name, event in EVENTS:
    ev = data.get(event)
    if not ev:
        raise SystemExit(f'missing event {event}')
    variants = []
    for s in ev['sounds']:
        if not s.get('exists'):
            continue
        rel = s['wav'].replace('minecraft/sounds/', '')
        dst_rel = 'mc/' + rel
        if len(dst_rel) > 60:
            raise SystemExit(f'path too long for GoldSrc: {dst_rel}')
        src = os.path.join(WAV_ROOT, s['wav'])
        for game_sound in GAME_SOUNDS:
            dst = os.path.join(game_sound, dst_rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if not os.path.exists(dst) or os.path.getsize(dst) != os.path.getsize(src):
                shutil.copyfile(src, dst)
                copied += 1
        variants.append((dst_rel, float(s.get('volume', 1.0)), float(s.get('pitch', 1.0)), int(s.get('weight', 1))))
    if not variants:
        raise SystemExit(f'no files for {event}')
    entries.append((enum_name, event, variants))

with open(OUT_H, 'w', newline='\n') as h:
    h.write('// Generated by tools/assets/gen_sounds.py - do not edit.\n#pragma once\n\nnamespace mcs\n{\n')
    h.write('enum Sound : int\n{\n')
    for name, ev, _ in entries:
        h.write(f'\tMCS_{name}, // {ev}\n')
    h.write('\tMCS_COUNT\n};\n\n')
    h.write('struct SoundVariant\n{\n\tconst char* file; // relative to sound/\n\tfloat volume;\n\tfloat pitch;\n\tint weight;\n};\n\n')
    h.write('struct SoundEvent\n{\n\tconst char* event;\n\tint numVariants;\n\tconst SoundVariant* variants;\n};\n\n')
    h.write('extern const SoundEvent g_sounds[MCS_COUNT];\n')
    h.write('// First block sound group entry; group g kind k (0 break,1 hit,2 place,3 step) = base + g*4 + k\n')
    h.write('static const int MCS_BLOCK_BASE = MCS_STONE_BREAK;\n')
    h.write('} // namespace mcs\n')

with open(OUT_CPP, 'w', newline='\n') as c:
    c.write('// Generated by tools/assets/gen_sounds.py - do not edit.\n#include "mc_sounds_gen.h"\n\nnamespace mcs\n{\n')
    for name, ev, variants in entries:
        c.write(f'static const SoundVariant v_{name}[] = {{')
        c.write(', '.join(f'{{"{f}", {v:.3f}f, {p:.3f}f, {w}}}' for f, v, p, w in variants))
        c.write('};\n')
    c.write('\nconst SoundEvent g_sounds[MCS_COUNT] = {\n')
    for name, ev, variants in entries:
        c.write(f'\t{{"{ev}", {len(variants)}, v_{name}}},\n')
    c.write('};\n} // namespace mcs\n')

print(f'{len(entries)} events, {sum(len(v) for _, _, v in entries)} variants, copied {copied} wav files')
