#!/bin/bash
# Run a scripted scenario on the dedicated server: no window, nothing on the screen, checked by the log.
# For scenarios about game logic (bots, economy, world rules); what has to be looked at still needs
# tools/runtest.sh and the game window. A bot stands in for "the player" (mc_test.cpp).
# usage: runtest_headless.sh <map> <scenario> [bots] [timeout_s]     (TESTCMDS='cvar value;...' for more settings)
# One server at a time: starting a run stops the server of the run before. A run only ever stops the
# server it started itself (by its process id), so a runner that outlives its server harms nobody, and it
# ends as soon as its server is gone.
MAP=$1; SC=$2; BOTS=${3:-8}; TO=${4:-90}
G=/z/dev/CSminecraft/game_test/Half-Life/cstrike
LOG=$G/logs/mc_server.log
powershell -File /z/dev/CSminecraft/tools/stop_headless.ps1 >/dev/null
for i in $(seq 1 40); do rm -f $LOG 2>/dev/null && [ ! -f $LOG ] && break; sleep 0.25; done
[ -f $LOG ] && { echo "the log is held by a running game from the test copy: not started"; exit 1; }
rm -f $G/logs/mc_watchdog.log
SRV=$(powershell -Command "& Z:\dev\CSminecraft\tools\launch_headless.ps1 -Map $MAP -Bots $BOTS -Test '${TESTCMDS:-};mc_testscript $SC'" | grep -ao 'pid [0-9]*' | awk '{print $2}')
[ -z "$SRV" ] && { echo "the server did not start"; exit 1; }
alive() { tasklist //FI "PID eq $SRV" 2>/dev/null | grep -q hlds; }
t0=$(date +%s); rc=1; why="did not finish in $TO s"
while [ $(( $(date +%s) - t0 )) -lt $TO ]; do
  [ -f $LOG ] && grep -aqE 'test done|SHOT end([^a-z0-9_]|$)' $LOG && { rc=0; break; }
  [ -f $G/logs/mc_watchdog.log ] && { why="stopped: see logs/mc_watchdog.log"; rc=2; break; }
  alive || { why="lost its server after $(( $(date +%s) - t0 )) s (stopped from outside, or it died)"; rc=3; break; }
  sleep 0.5
done
powershell -File /z/dev/CSminecraft/tools/stop_headless.ps1 -ProcId $SRV >/dev/null
[ $rc -eq 0 ] && echo "scenario $SC finished in $(( $(date +%s) - t0 )) s" || echo "scenario $SC $why"
[ -f $G/logs/mc_watchdog.log ] && cat $G/logs/mc_watchdog.log
exit $rc
