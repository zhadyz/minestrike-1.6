#!/bin/bash
# Copy freshly built DLLs into the Z: game copy's cstrike folder (our private copy; originals in cstrike/_csmc_originals).
# Two copies: game_test (where tools/runtest.sh etc. run; must succeed) and game (the one the PLAY_*.bat
# launchers start; skipped with a warning while someone is playing it).
B=/z/dev/scratch/csminecraft/build
copy_retry() {
  for i in $(seq 1 30); do cp "$1" "$2" 2>/dev/null && return 0; sleep 0.5; done
  echo "deploy: could not copy $1 (file locked)"; return 1
}
copy_once() { cp "$1" "$2" 2>/dev/null || { echo "deploy: $2 is in use (game running), not updated"; return 1; }; }
rc=0
deploy_to() {
G=$1; COPY=$2
[ -f $B/client/client.dll ] && { $COPY $B/client/client.dll $G/cl_dlls/client.dll || return 1; }
[ -f $B/server/mp.dll ] && { $COPY $B/server/mp.dll $G/dlls/mp.dll || return 1; }
# game-side data kept in the repo (configs, MOTD, bot names, map briefing/overview, delta.lst)
D=/z/dev/CSminecraft/gamedata/cstrike
(cd $D && find . -type f) | while read f; do
  cmp -s "$D/$f" "$G/$f" || { mkdir -p "$(dirname "$G/$f")"; copy_retry "$D/$f" "$G/$f"; }
done
return 0
}
deploy_to /z/dev/CSminecraft/game_test/Half-Life/cstrike copy_retry || rc=1
deploy_to /z/dev/CSminecraft/game/Half-Life/cstrike copy_once || echo "deploy: play copy not updated (in use)"
echo "deployed: $(date +%T) rc=$rc"
exit $rc
