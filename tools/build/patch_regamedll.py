"""Insert the csmc hooks into ReGameDLL sources (idempotent: each insertion is tagged // [csmc])."""
import os
import re
import sys

RG = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'ReGameDLL_CS', 'regamedll')).replace(os.sep, '/')
TAG = '// [csmc]'


def patch(path, edits):
    p = f'{RG}/{path}'
    s = open(p, encoding='utf-8', errors='surrogateescape').read()
    orig = s
    for name, find, repl in edits:
        if f'{TAG} {name}' in s:
            continue
        if find not in s:
            sys.exit(f'{path}: anchor for {name} not found')
        s = s.replace(find, repl.replace('@TAG', f'{TAG} {name}'), 1)
    if s != orig:
        open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
        print('patched', path)


def body_end(src, signature):
    """Return index of the closing brace of the function starting at signature."""
    i = src.index(signature)
    j = src.index('{', i)
    depth = 0
    for k in range(j, len(src)):
        if src[k] == '{':
            depth += 1
        elif src[k] == '}':
            depth -= 1
            if depth == 0:
                return k
    raise ValueError(signature)


def insert_before_end(path, name, signature, code):
    p = f'{RG}/{path}'
    s = open(p, encoding='utf-8', errors='surrogateescape').read()
    if f'{TAG} {name}' in s:
        return
    k = body_end(s, signature)
    # keep the function's last line ending
    s = s[:k] + f'\t{code} {TAG} {name}\n' + s[k:]
    open(p, 'w', encoding='utf-8', errors='surrogateescape', newline='').write(s)
    print('patched', path, name)


# h_export.cpp: wrap engine functions + register hookchains right after the engine hands us its table
patch('dlls/h_export.cpp', [
    ('include', '#include "precompiled.h"', '#include "precompiled.h"\n#include "mc_server.h" @TAG'),
    ('fnptrs', '\tgpGlobals = pGlobals;', '\tgpGlobals = pGlobals;\n\tmc::OnGiveFnptrs(); @TAG'),
])

patch('dlls/client.cpp', [
    ('include', '#include "precompiled.h"', '#include "precompiled.h"\n#include "mc_server.h" @TAG'),
    ('startframe', '\tif (TheTutor) {\n\t\tTheTutor->StartFrame(gpGlobals->time);\n\t}\n',
     '\tif (TheTutor) {\n\t\tTheTutor->StartFrame(gpGlobals->time);\n\t}\n\n\tmc::StartFrame(); @TAG\n'),
    ('deactivate', '\tg_bServerActive = false;\n', '\tg_bServerActive = false;\n\tmc::OnServerDeactivate(); @TAG\n'),
])
insert_before_end('dlls/client.cpp', 'msgs', 'void LinkUserMessages()', 'mc::RegisterMessages();')
insert_before_end('dlls/client.cpp', 'precache', 'void ClientPrecache()', 'mc::Precache();')
insert_before_end('dlls/client.cpp', 'activate', 'void EXT_FUNC ServerActivate(edict_t *pEdictList, int edictCount, int clientMax)', 'mc::OnServerActivate();')
insert_before_end('dlls/client.cpp', 'putinserver', 'void EXT_FUNC ClientPutInServer(edict_t *pEntity)', 'mc::OnClientPutInServer(pEntity);')
insert_before_end('dlls/client.cpp', 'disconnect', 'void EXT_FUNC ClientDisconnect(edict_t *pEntity)', 'mc::OnClientDisconnect(pEntity);')
insert_before_end('dlls/client.cpp', 'clientdata', 'void EXT_FUNC UpdateClientData(const edict_t *ent, int sendweapons, struct clientdata_s *cd)', 'mc::OnUpdateClientData(ent, cd);')

# player.cpp: CS's train flag must not wipe the Minecraft bits we keep in iuser4
patch('dlls/player.cpp', [
    ('train1', '\t\tpev->iuser4 = 1;\n', '\t\tpev->iuser4 |= 1; @TAG\n'),
    ('train0', '\t\tpev->iuser4 = 0;\n', '\t\tpev->iuser4 &= ~1; @TAG\n'),
])
patch('dlls/client.cpp', [
    ('playerclass', '\t\tstate->health = int(ent->v.health);\n', '\t\tstate->health = int(ent->v.health);\n\t\tstate->playerclass = ent->v.playerclass; @TAG\n'),
])
patch('game_shared/bot/nav.h', [
    ('stepheight', 'const float StepHeight          = 18.0f; // if delta Z is greater than this, we have to jump to get up',
     'extern float StepHeight;                // if delta Z is greater than this, we have to jump to get up (18, or 24 on Minecraft voxel maps) @TAG'),
])
# cs_bot_update.cpp: bots aim at the drawn Minecraft model's head or chest (mc_hitbox.cpp), not a CS model's
patch('dlls/bot/cs_bot_update.cpp', [
    ('include', '#include "precompiled.h"', '#include "precompiled.h"\n#include "mc_server.h" @TAG'),
    ('botaim', '\t\t\t\tif (aimBlocked)\n\t\t\t\t\tm_aimSpot.z -= feetOffset * 0.25f;\n',
     '\t\t\t\tif (mc::BotAimAtRig(this, m_enemy, !aimBlocked && IsEnemyPartVisible(HEAD), m_aimSpot)) @TAG\n\t\t\t\t\t;\n'
     '\t\t\t\telse if (aimBlocked)\n\t\t\t\t\tm_aimSpot.z -= feetOffset * 0.25f;\n'),
])
# cs_bot.h: a bot planning a route knows that a walled way in costs time (mc_bottactics.cpp)
patch('dlls/bot/cs_bot.h', [
    ('wallcost_decl', 'class PathCost\n{\npublic:\n\tPathCost(CCSBot *pBot, RouteType route = SAFEST_ROUTE)',
     'namespace mc { float BotWallCost(CBasePlayer *bot, CNavArea *area, CNavArea *from); } @TAG\n\n'
     'class PathCost\n{\npublic:\n\tPathCost(CCSBot *pBot, RouteType route = SAFEST_ROUTE)'),
    ('pathwall', '\t\t\t// zombies ignore all path penalties\n',
     '\t\t\tcost += mc::BotWallCost(m_bot, area, fromArea); @TAG\n\n\t\t\t// zombies ignore all path penalties\n'),
])
print('done')

# Team mobs (mc_mobs.cpp: the iron golem, the wither) are bots made in the middle of a round. They stand
# outside bot_quota, are never the bot kicked to make room, do not count as living or dead members of
# their side when a round is decided, do not go for the bomb.
MOBDECL = 'namespace mc { bool IsMobBot(CBasePlayer *pl); } @TAG'
patch('game_shared/bot/bot_util.cpp', [
    ('mobdecl', '#include "precompiled.h"', '#include "precompiled.h"\n' + MOBDECL),
    ('mobquota', '\t\tif (!pPlayer->IsBot())\n\t\t\tcontinue;\n\n\t\tiCount++;',
     '\t\tif (!pPlayer->IsBot())\n\t\t\tcontinue;\n\n\t\tif (mc::IsMobBot(pPlayer)) @TAG\n\t\t\tcontinue;\n\n\t\tiCount++;'),
    ('mobkick1', '\t\tif (!pPlayer->IsBot())\n\t\t\tcontinue;\n\n\t\tif (!pPlayer->IsAlive() && pPlayer->m_iTeam == kickTeam)',
     '\t\tif (!pPlayer->IsBot() || mc::IsMobBot(pPlayer)) @TAG\n\t\t\tcontinue;\n\n\t\tif (!pPlayer->IsAlive() && pPlayer->m_iTeam == kickTeam)'),
    ('mobkick2', '\t\tif (!pPlayer->IsBot())\n\t\t\tcontinue;\n\n\t\tif (pPlayer->m_iTeam == kickTeam)',
     '\t\tif (!pPlayer->IsBot() || mc::IsMobBot(pPlayer)) @TAG\n\t\t\tcontinue;\n\n\t\tif (pPlayer->m_iTeam == kickTeam)'),
])
patch('dlls/multiplay_gamerules.cpp', [
    ('mobdecl', '#include "precompiled.h"', '#include "precompiled.h"\n' + MOBDECL),
    ('mobcount', '\t\tCBasePlayer *pPlayer = GetClassPtr<CCSPlayer>((CBasePlayer *)pEntity->pev);\n\t\tswitch (pPlayer->m_iTeam)\n',
     '\t\tCBasePlayer *pPlayer = GetClassPtr<CCSPlayer>((CBasePlayer *)pEntity->pev);\n\t\tif (mc::IsMobBot(pPlayer)) @TAG\n\t\t\tcontinue;\n\t\tswitch (pPlayer->m_iTeam)\n'),
])
patch('dlls/client.cpp', [
    ('mobteam', '\t\tif (pPlayer->m_iTeam == iTeam)\n', '\t\tif (pPlayer->m_iTeam == iTeam && !mc::IsMobBot(pPlayer)) @TAG\n'),
])
patch('dlls/bot/cs_bot.cpp', [
    ('mobdecl', '#include "precompiled.h"', '#include "precompiled.h"\n' + MOBDECL),
    ('mobscenario', 'bool CCSBot::IsDoingScenario() const\n{\n',
     'bool CCSBot::IsDoingScenario() const\n{\n\tif (mc::IsMobBot(const_cast<CCSBot *>(this))) return false; @TAG\n'),
    ('mobloosebomb', 'bool CCSBot::NoticeLooseBomb() const\n{\n',
     'bool CCSBot::NoticeLooseBomb() const\n{\n\tif (mc::IsMobBot(const_cast<CCSBot *>(this))) return false; @TAG\n'),
])

# cs_bot_vision.cpp: a bot does not take for its target what it cannot hurt (an iron golem, to a bot
# without a sword: mc_mobs.cpp)
patch('dlls/bot/cs_bot_vision.cpp', [
    ('mobdecl2', '#include "precompiled.h"', '#include "precompiled.h"\nnamespace mc { bool BotIgnoresThreat(CBasePlayer *bot, CBasePlayer *other); } @TAG'),
    ('mobthreat', '\tint i;\n\n\t{\n\t\tfor (i = 1; i <= gpGlobals->maxClients; i++)\n\t\t{\n\t\t\tCBasePlayer *pPlayer = UTIL_PlayerByIndex(i);\n',
     '\tint i;\n\n\t{\n\t\tfor (i = 1; i <= gpGlobals->maxClients; i++)\n\t\t{\n\t\t\tCBasePlayer *pPlayer = UTIL_PlayerByIndex(i);\n'
     '\t\t\tif (mc::BotIgnoresThreat(this, pPlayer)) @TAG\n\t\t\t\tcontinue;\n'),
])
