# env-x86.sh - 32-bit (i686) Windows build environment for Git Bash: portable
# LLVM clang-cl + lld-link + CMake + Ninja, against the xwin MSVC CRT + SDK.
#
# Source it:   source /z/dev/CSminecraft/tools/env-x86.sh
#
# Then either
#   * plain compile:  clang-cl-x86 -O2 -MT -EHsc -LD foo.cpp -Fe:foo.dll
#   * CMake:          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
#
# Git Bash gotcha: MSYS rewrites arguments that look like POSIX paths, so a
# bare `clang-cl /MT x.cpp` reaches clang-cl as `C:/Program Files/Git/MT`.
# Use dash-style options (-MT, -O2, -LD) or the clang-cl-x86 function below
# (it disables MSYS argument conversion for that call; pass Windows-style or
# relative paths to it, not /z/... paths).

CSM_TOOLS="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CSM_TOOLS_W="$(cygpath -m "$CSM_TOOLS")"          # Z:/dev/CSminecraft/tools
_csm_xwin="$CSM_TOOLS/xwin"
_csm_msvc_name="$(ls -1 "$_csm_xwin/VC/Tools/MSVC" | sort -V | tail -n 1)"
_csm_sdk_name="$(ls -1 "$_csm_xwin/Windows Kits/10/Include" | sort -V | tail -n 1)"
if [ -z "$_csm_msvc_name" ] || [ -z "$_csm_sdk_name" ]; then
  echo "env-x86.sh: xwin CRT/SDK not found under $_csm_xwin" >&2; return 1 2>/dev/null || exit 1
fi
_csm_msvc="$CSM_TOOLS_W/xwin/VC/Tools/MSVC/$_csm_msvc_name"
_csm_inc="$CSM_TOOLS_W/xwin/Windows Kits/10/Include/$_csm_sdk_name"
_csm_lib="$CSM_TOOLS_W/xwin/Windows Kits/10/Lib/$_csm_sdk_name"

# _MSC_VER to emulate = CRT toolset (14.44.x -> 19.44); default would be 19.33.
CSM_MSC_COMPAT="19.$(echo "$_csm_msvc_name" | cut -d. -f2)"

# PATH: prepend our tools once.
for _d in "$CSM_TOOLS/ninja" "$CSM_TOOLS/cmake/bin" "$CSM_TOOLS/llvm/bin"; do
  case ":$PATH:" in *":$_d:"*) ;; *) PATH="$_d:$PATH" ;; esac
done
export PATH

# x86 CRT + SDK (Windows-style, ';'-separated: read by clang-cl, lld-link and
# llvm-rc; setting them also stops clang-cl/lld-link from auto-detecting the
# VS2019 / Windows Kits installed on C:).
export INCLUDE="$_csm_msvc/include;$_csm_inc/ucrt;$_csm_inc/shared;$_csm_inc/um;$_csm_inc/winrt;$_csm_inc/cppwinrt"
export LIB="$_csm_msvc/lib/x86;$_csm_lib/ucrt/x86;$_csm_lib/um/x86"
unset EXTERNAL_INCLUDE

# Pin _MSC_VER for EVERY clang-cl call in this shell (clang-cl reads %CL%).
# Without it a bare clang-cl reads the version of VS2019's cl.exe on C: and
# reports _MSC_VER 1929 while compiling against the 14.44 STL.
export CL="-fms-compatibility-version=$CSM_MSC_COMPAT"

# Temp files on Z: (clang-cl writes objects to %TMP% for compile+link in one step).
mkdir -p /z/dev/scratch/csminecraft/tmp
export TMP=/z/dev/scratch/csminecraft/tmp TEMP=/z/dev/scratch/csminecraft/tmp TMPDIR=/z/dev/scratch/csminecraft/tmp

# CMake defaults for this shell.
export CMAKE_TOOLCHAIN_FILE="$CSM_TOOLS_W/clang-cl-x86.cmake"
export CMAKE_GENERATOR=Ninja
export CSM_TOOLS CSM_MSC_COMPAT
export CSM_XWIN_MSVC="$_csm_msvc" CSM_XWIN_SDK="$_csm_sdk_name"

# clang-cl preset for i686. -fuse-ld=lld matters: without it clang-cl runs
# link.exe, and in Git Bash that resolves to /usr/bin/link (coreutils).
clang-cl-x86() {
  MSYS2_ARG_CONV_EXCL='*' "$CSM_TOOLS/llvm/bin/clang-cl.exe" --target=i686-pc-windows-msvc -fuse-ld=lld "-fms-compatibility-version=$CSM_MSC_COMPAT" "$@"
}

echo "[env-x86] $(clang-cl --version | head -n 1 | cut -d' ' -f1-3) | CRT $_csm_msvc_name | SDK $_csm_sdk_name | target i686-pc-windows-msvc"
unset _d _csm_xwin _csm_msvc_name _csm_sdk_name _csm_msvc _csm_inc _csm_lib
