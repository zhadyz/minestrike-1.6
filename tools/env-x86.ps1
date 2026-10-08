# env-x86.ps1 - 32-bit (i686) Windows build environment: portable LLVM clang-cl
# + lld-link + CMake + Ninja, against the xwin MSVC CRT + Windows SDK.
#
# Dot-source it (Windows PowerShell 5.1 or pwsh):
#     . Z:\dev\CSminecraft\tools\env-x86.ps1
#
# Then either
#   * plain compile:  clang-cl-x86 /O2 /MT /EHsc /LD foo.cpp /Fe:foo.dll
#     (= clang-cl --target=i686-pc-windows-msvc -fuse-ld=lld ... ; INCLUDE/LIB
#      below supply the xwin x86 CRT/SDK), or
#   * CMake:          cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#                     cmake --build build
#     (CMAKE_TOOLCHAIN_FILE and CMAKE_GENERATOR=Ninja are set below).
#
# Sets PATH, INCLUDE, LIB, TMP/TEMP (Z: scratch - the C: drive is not used),
# CMAKE_TOOLCHAIN_FILE, CMAKE_GENERATOR. Safe to dot-source more than once.

$CsmTools   = $PSScriptRoot
$CsmLlvmBin = Join-Path $CsmTools 'llvm\bin'
$CsmCMake   = Join-Path $CsmTools 'cmake\bin'
$CsmNinja   = Join-Path $CsmTools 'ninja'
$CsmXwin    = Join-Path $CsmTools 'xwin'
$CsmScratch = 'Z:\dev\scratch\csminecraft\tmp'

$msvcDir = Get-ChildItem -Directory (Join-Path $CsmXwin 'VC\Tools\MSVC') |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1
$sdkVer = Get-ChildItem -Directory (Join-Path $CsmXwin 'Windows Kits\10\Include') |
    Sort-Object { [version]$_.Name } | Select-Object -Last 1
if (-not $msvcDir -or -not $sdkVer) { throw "env-x86.ps1: xwin CRT/SDK not found under $CsmXwin" }

$msvc   = $msvcDir.FullName
$sdkInc = Join-Path $CsmXwin ('Windows Kits\10\Include\' + $sdkVer.Name)
$sdkLib = Join-Path $CsmXwin ('Windows Kits\10\Lib\' + $sdkVer.Name)

# _MSC_VER to emulate = CRT toolset (14.44.x -> 19.44); default would be 19.33.
$global:CsmMscCompat = '19.' + $msvcDir.Name.Split('.')[1]

# PATH: prepend our tools once.
$prepend = @($CsmLlvmBin, $CsmCMake, $CsmNinja)
$rest = $env:PATH -split ';' | Where-Object { $_ -and ($prepend -notcontains $_) }
$env:PATH = ($prepend + $rest) -join ';'

# x86 CRT + SDK (consumed by clang-cl, lld-link and llvm-rc; setting them also
# stops clang-cl/lld-link from auto-detecting the VS2019 / Windows Kits on C:).
$env:INCLUDE = @(
    "$msvc\include",
    "$sdkInc\ucrt", "$sdkInc\shared", "$sdkInc\um", "$sdkInc\winrt", "$sdkInc\cppwinrt"
) -join ';'
$env:LIB = @("$msvc\lib\x86", "$sdkLib\ucrt\x86", "$sdkLib\um\x86") -join ';'
$env:EXTERNAL_INCLUDE = $null

# Pin _MSC_VER for EVERY clang-cl call in this shell (clang-cl reads %CL%).
# Without it a bare clang-cl reads the version of VS2019's cl.exe on C: and
# reports _MSC_VER 1929 while compiling against the 14.44 STL.
$env:CL = "-fms-compatibility-version=$global:CsmMscCompat"

# Temp files (clang-cl writes objects there for compile+link in one step).
if (-not (Test-Path $CsmScratch)) { New-Item -ItemType Directory -Force $CsmScratch | Out-Null }
$env:TMP  = $CsmScratch
$env:TEMP = $CsmScratch

# CMake defaults for this shell.
$env:CMAKE_TOOLCHAIN_FILE = (Join-Path $CsmTools 'clang-cl-x86.cmake') -replace '\\', '/'
$env:CMAKE_GENERATOR = 'Ninja'

$env:CSM_TOOLS = $CsmTools
$env:CSM_XWIN_MSVC = $msvc
$env:CSM_XWIN_SDK = $sdkVer.Name

# clang-cl preset for i686. -fuse-ld=lld matters: without it clang-cl runs
# link.exe (absent here; in Git Bash /usr/bin/link is coreutils' link).
function global:clang-cl-x86 {
    & (Join-Path $env:CSM_TOOLS 'llvm\bin\clang-cl.exe') --target=i686-pc-windows-msvc -fuse-ld=lld "-fms-compatibility-version=$global:CsmMscCompat" @args
}

Write-Host ("[env-x86] clang-cl {0} | CRT {1} | SDK {2} | target i686-pc-windows-msvc" -f `
    ((& (Join-Path $CsmLlvmBin 'clang-cl.exe') --version | Select-Object -First 1) -replace '^clang version ([^ ]+).*', '$1'),
    $msvcDir.Name, $sdkVer.Name)
