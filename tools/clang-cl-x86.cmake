# clang-cl-x86.cmake
# CMake toolchain: 32-bit (i686) Windows, MSVC ABI, built with the portable
# LLVM clang-cl + lld-link in this folder against the xwin-splatted MSVC CRT +
# Windows SDK. Use for GoldSrc mods (ReGameDLL_CS mp.dll, client.dll, ReHLDS).
#
#   cmake -S <src> -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_TOOLCHAIN_FILE=Z:/dev/CSminecraft/tools/clang-cl-x86.cmake
#   cmake --build <build>
#
# Notes
# * Hermetic on purpose: this PC has VS2019 BuildTools + Windows Kits on C:.
#   clang-cl and lld-link silently auto-detect those when INCLUDE / LIB are not
#   set, so compile uses /X (ignore %INCLUDE% and auto-detection) + explicit
#   /imsvc dirs, and link uses /winsysroot (pins lld-link's auto-detection to
#   xwin) + explicit /LIBPATH dirs. Nothing from C: can leak in.
# * The CRT was splatted without debug libs: /MTd and /MDd do not link. The
#   default runtime here is /MT (static, GoldSrc convention) for every config.
#   For a Debug build keep /MT (set CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded).

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR x86)

get_filename_component(CSM_TOOLS_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)
set(CSM_LLVM_BIN "${CSM_TOOLS_DIR}/llvm/bin")
set(CSM_XWIN_ROOT "${CSM_TOOLS_DIR}/xwin")

# Pick the newest MSVC CRT and Windows SDK present in the xwin splat.
file(GLOB _csm_msvc LIST_DIRECTORIES true "${CSM_XWIN_ROOT}/VC/Tools/MSVC/*")
file(GLOB _csm_sdk  LIST_DIRECTORIES true "${CSM_XWIN_ROOT}/Windows Kits/10/Include/*")
if(NOT _csm_msvc OR NOT _csm_sdk)
  message(FATAL_ERROR "clang-cl-x86.cmake: xwin CRT/SDK not found under ${CSM_XWIN_ROOT}")
endif()
list(SORT _csm_msvc COMPARE NATURAL)
list(SORT _csm_sdk COMPARE NATURAL)
list(GET _csm_msvc -1 CSM_MSVC_DIR)
list(GET _csm_sdk -1 _csm_sdk_inc)
get_filename_component(CSM_SDK_VERSION "${_csm_sdk_inc}" NAME)
set(CSM_SDK_INC "${CSM_XWIN_ROOT}/Windows Kits/10/Include/${CSM_SDK_VERSION}")
set(CSM_SDK_LIB "${CSM_XWIN_ROOT}/Windows Kits/10/Lib/${CSM_SDK_VERSION}")

# _MSC_VER to emulate = the CRT's toolset (14.44.x -> 19.44). Without this
# clang-cl takes the version from VS2019's cl.exe on C: (1929), or 1933 when
# no VS is installed (xwin ships no cl.exe to read a version from).
get_filename_component(_csm_msvc_ver "${CSM_MSVC_DIR}" NAME)
string(REGEX MATCH "^14\\.([0-9]+)" _csm_m "${_csm_msvc_ver}")
set(CSM_MSC_COMPAT "19.${CMAKE_MATCH_1}")

set(CSM_X86_INCLUDE_DIRS
  "${CSM_MSVC_DIR}/include"
  "${CSM_SDK_INC}/ucrt"
  "${CSM_SDK_INC}/shared"
  "${CSM_SDK_INC}/um"
  "${CSM_SDK_INC}/winrt"
  "${CSM_SDK_INC}/cppwinrt")
set(CSM_X86_LIB_DIRS
  "${CSM_MSVC_DIR}/lib/x86"
  "${CSM_SDK_LIB}/ucrt/x86"
  "${CSM_SDK_LIB}/um/x86")

# Tools.
set(CMAKE_C_COMPILER   "${CSM_LLVM_BIN}/clang-cl.exe")
set(CMAKE_CXX_COMPILER "${CSM_LLVM_BIN}/clang-cl.exe")
set(CMAKE_C_COMPILER_TARGET   i686-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET i686-pc-windows-msvc)
set(CMAKE_LINKER  "${CSM_LLVM_BIN}/lld-link.exe")
set(CMAKE_AR      "${CSM_LLVM_BIN}/llvm-lib.exe")
set(CMAKE_RC_COMPILER "${CSM_LLVM_BIN}/llvm-rc.exe")
set(CMAKE_MT      "${CSM_LLVM_BIN}/llvm-mt.exe")

# Compile flags (C and C++): hermetic system includes.
set(_csm_cflags "/X -fms-compatibility-version=${CSM_MSC_COMPAT}")
set(_csm_rcflags "/X")
foreach(_d IN LISTS CSM_X86_INCLUDE_DIRS)
  string(APPEND _csm_cflags " \"/imsvc${_d}\"")
  string(APPEND _csm_rcflags " /I \"${_d}\"")
endforeach()
set(CMAKE_C_FLAGS_INIT   "${_csm_cflags}")
set(CMAKE_CXX_FLAGS_INIT "${_csm_cflags}")
set(CMAKE_RC_FLAGS_INIT  "${_csm_rcflags}")

# Link flags (exe, dll, module): hermetic x86 library search path.
set(_csm_ldflags "\"/winsysroot:${CSM_XWIN_ROOT}\"")
foreach(_d IN LISTS CSM_X86_LIB_DIRS)
  string(APPEND _csm_ldflags " \"/LIBPATH:${_d}\"")
endforeach()
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_csm_ldflags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_csm_ldflags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_csm_ldflags}")

# Static CRT by default (projects may override after project()).
# Needs policy CMP0091 NEW (cmake_minimum_required >= 3.15).
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

