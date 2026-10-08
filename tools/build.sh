#!/bin/bash
# Build client proxy + server DLL with the portable clang-cl toolchain, then deploy.
source /z/dev/CSminecraft/tools/env-x86.sh >/dev/null 2>&1
B=/z/dev/scratch/csminecraft/build
T=Z:/dev/CSminecraft/tools/clang-cl-x86.cmake
what=${1:-all}
rc=0
if [ "$what" = all ] || [ "$what" = client ]; then
  [ -f $B/client/build.ninja ] || cmake -S /z/dev/CSminecraft/code/client -B $B/client -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$T >/dev/null
  cmake --build $B/client 2>&1 | grep -E "error|FAILED" ; [ ${PIPESTATUS[0]} -ne 0 ] && rc=1
fi
if [ "$what" = all ] || [ "$what" = server ]; then
  [ -f $B/server/build.ninja ] || cmake -S /z/dev/CSminecraft/code/server -B $B/server -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=$T >/dev/null
  cmake --build $B/server 2>&1 | grep -E "error|FAILED" ; [ ${PIPESTATUS[0]} -ne 0 ] && rc=1
fi
if [ $rc -eq 0 ]; then powershell -File /z/dev/CSminecraft/tools/stop.ps1 >/dev/null; /z/dev/CSminecraft/tools/deploy.sh; else echo "BUILD FAILED"; fi
exit $rc
