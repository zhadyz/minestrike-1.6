#!/bin/bash
# Run a scripted scenario on a map and capture a screenshot whenever the server logs "SHOT <name>".
# usage: runtest.sh <map> <scenario> [bots] [timeout_s]
MAP=$1; SC=$2; BOTS=${3:-0}; TO=${4:-60}
LOG=/z/dev/CSminecraft/game_test/Half-Life/cstrike/logs/mc_server.log
powershell -File /z/dev/CSminecraft/tools/stop.ps1 >/dev/null
# the old game can hold the log for a moment after it exits
for i in $(seq 1 40); do rm -f $LOG 2>/dev/null && [ ! -f $LOG ] && break; sleep 0.25; done
powershell -Command "& Z:\dev\CSminecraft\tools\launch.ps1 -Map $MAP -Bots $BOTS -Test 'mc_autokit 1;${TESTCMDS:-};mc_testscript $SC'" >/dev/null
seen=0; t0=$(date +%s); recpid=
FFMPEG=${FFMPEG:-ffmpeg}
while [ $(( $(date +%s) - t0 )) -lt $TO ]; do
  if [ -f $LOG ]; then
    # REC=<file.mp4> [REC_T=seconds]: record the game window from the moment the scenario starts
    if [ -n "$REC" ] && [ -z "$recpid" ] && grep -aq "test: running" $LOG; then
      "$FFMPEG" -y -loglevel error -f gdigrab -framerate 30 -i title="Counter-Strike" -t ${REC_T:-15}         -vf "scale=1280:-2" -c:v libx264 -preset veryfast -crf 26 -pix_fmt yuv420p -movflags +faststart "$REC" </dev/null &
      recpid=$!
      echo "recording $REC"
    fi
    mapfile -t shots < <(awk '/test: starting/ && !n {n=NR} {l[NR]=$0} END{for(i=n;i<=NR;i++) print l[i]}' $LOG | grep -ao "SHOT [a-z0-9_]*" | awk '{print $2}')
    while [ $seen -lt ${#shots[@]} ]; do
      n=${shots[$seen]}
      powershell -File /z/dev/CSminecraft/tools/shot.ps1 -Out "${SHOTDIR:-Z:/dev/scratch/csminecraft/shots}/${n}.png" -Width ${SHOTW:-640} >/dev/null
      echo "captured $n"
      seen=$((seen+1))
    done
    # scenarios can ask for real input: "KEYS <sendkeys args>"
    mapfile -t keys < <(awk '/test: starting/ && !n {n=NR} {l[NR]=$0} END{for(i=n;i<=NR;i++) print l[i]}' $LOG | grep -ao "KEYS .*" | cut -c6-)
    while [ ${kseen:-0} -lt ${#keys[@]} ]; do
      powershell -File /z/dev/CSminecraft/tools/sendkeys.ps1 ${keys[${kseen:-0}]} | grep -a skipped
      echo "keys ${keys[${kseen:-0}]}"
      kseen=$(( ${kseen:-0} + 1 ))
    done
    grep -aqE 'test done|SHOT end([^a-z0-9_]|$)' $LOG && break
  fi
  sleep 0.1
done
[ -n "$recpid" ] && wait $recpid && echo "recorded $REC"
