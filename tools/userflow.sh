#!/bin/bash
# Play the mod the way a person would: PLAY_*.bat settings (MOTD, team menu), real key presses and mouse.
# usage: userflow.sh <map> "<step>" ...   each step: "keys...|shotname" (keys passed to sendkeys.ps1)
MAP=$1; shift
LOG=/z/dev/CSminecraft/game_test/Half-Life/cstrike/logs/mc_server.log
OUT=Z:/dev/scratch/csminecraft/shots
powershell -File /z/dev/CSminecraft/tools/stop.ps1 >/dev/null
for i in $(seq 1 40); do rm -f $LOG 2>/dev/null && [ ! -f $LOG ] && break; sleep 0.25; done
powershell -Command "& Z:\dev\CSminecraft\tools\launch.ps1 -Map $MAP -User -Test '$UFTEST'" >/dev/null
t0=$(date +%s)
until grep -q "beat:" $LOG 2>/dev/null || [ $(( $(date +%s) - t0 )) -gt 90 ]; do sleep 0.5; done
sleep 4
for step in "$@"; do
  keys=${step%%|*}; shot=${step##*|}
  if [ -n "$keys" ]; then powershell -File /z/dev/CSminecraft/tools/sendkeys.ps1 $keys >/dev/null; fi
  if [ -n "$shot" ] && [ "$shot" != "$step" ]; then
    powershell -File /z/dev/CSminecraft/tools/shot.ps1 -Out "$OUT/$shot.png" -Width 800 >/dev/null; echo "captured $shot"
  fi
done
