#!/bin/bash
# Run a scripted scenario in the real game, on a Windows desktop of its own: no window on the screen, no
# focus taken, no mouse or keyboard touched. The game writes its own picture for every "SHOT name" the
# scenario logs; they end up as PNGs in $SHOTDIR. For what has to be looked at. (Scenarios that need real
# key presses, "KEYS ...", cannot run here: there is no input on that desktop.)
# usage: runtest_hidden.sh <map> <scenario> [bots] [timeout_s]
MAP=$1; SC=$2; BOTS=${3:-0}; TO=${4:-60}
G=/z/dev/CSminecraft/game_test/Half-Life/cstrike
LOG=$G/logs/mc_server.log
OUT=${SHOTDIR:-Z:/dev/scratch/csminecraft/shots}
powershell -File /z/dev/CSminecraft/tools/stop.ps1 >/dev/null
for i in $(seq 1 40); do rm -f $LOG 2>/dev/null && [ ! -f $LOG ] && break; sleep 0.25; done
[ -f $LOG ] && { echo "the log is held by a running game from the test copy: not started"; exit 1; }
powershell -File /z/dev/CSminecraft/tools/stop_headless.ps1 >/dev/null 2>&1
PIDF=/z/dev/scratch/csminecraft/hl.pid
# The game now and then comes up without loading the map when another copy has only just closed: it is
# given 25 s to write its first log line, and started once more if it does not.
for try in 1 2 3; do
  rm -f $PIDF
  powershell -Command "& Z:\dev\CSminecraft\tools\launch_hidden.ps1 -Map $MAP -Bots $BOTS -Test 'mc_autokit 1;${TESTCMDS:-};mc_testscript $SC'" | tail -1
  [ -f $PIDF ] || exit 1   # it did not start
  for i in $(seq 1 50); do [ -f $LOG ] && break; sleep 0.5; done
  [ -f $LOG ] && break
  echo "no sign of life from the game: starting it again"
  powershell -File /z/dev/CSminecraft/tools/stop.ps1 >/dev/null
  sleep 2
done
t0=$(date +%s); rc=1
onscreen() { powershell -Command "\$p = Get-Process -Id (Get-Content Z:\dev\scratch\csminecraft\hl.pid) -ErrorAction SilentlyContinue; if (\$p -and \$p.MainWindowHandle -ne 0) { 'yes' }" 2>/dev/null | grep -q yes; }
while [ $(( $(date +%s) - t0 )) -lt $TO ]; do
  # the guard: a window of the game that this desktop can see is on the screen of whoever sits there
  if [ -f $PIDF ] && onscreen; then
    powershell -File /z/dev/CSminecraft/tools/stop.ps1 >/dev/null
    echo "the game's window came up on the real desktop: stopped at once"; exit 4
  fi
  [ -f $LOG ] && grep -aqE 'test done|SHOT end([^a-z0-9_]|$)' $LOG && { rc=0; break; }
  sleep 0.4
done
[ $rc -eq 0 ] && sleep 2   # the last picture is taken a second after its line
powershell -File /z/dev/CSminecraft/tools/stop.ps1 >/dev/null
[ $rc -eq 0 ] && echo "scenario $SC finished in $(( $(date +%s) - t0 )) s" || echo "scenario $SC did not finish in $TO s"
/c/Python314/python - "$G/mc_shots" "$OUT" "${SHOTW:-1280}" <<'EOF'
import glob, os, sys
from PIL import Image
src, out, width = sys.argv[1], sys.argv[2], int(sys.argv[3])
os.makedirs(out, exist_ok=True)
for f in sorted(glob.glob(os.path.join(src, '*.bmp'))):
    im = Image.open(f).convert('RGB')
    if im.width > width:
        im = im.resize((width, round(im.height * width / im.width)), Image.LANCZOS)
    name = os.path.splitext(os.path.basename(f))[0] + '.png'
    im.save(os.path.join(out, name))
    lo, hi = im.convert('L').getextrema()
    print('picture', name, im.size, 'black' if hi < 8 else 'ok')
EOF
exit $rc
