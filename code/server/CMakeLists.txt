# Windows build of ReGameDLL_CS (+ the Minecraft layer) with clang-cl, mirroring msvc/ReGameDLL.vcxproj Release|Win32.
cmake_minimum_required(VERSION 3.20)
project(csmc_server C CXX)

set(RG "${CMAKE_CURRENT_SOURCE_DIR}/../../src/ReGameDLL_CS/regamedll")
set(SHARED "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/regamedll_sources.txt" RG_REL_SRCS)
set(RG_SRCS "")
foreach(s ${RG_REL_SRCS})
	list(APPEND RG_SRCS "${RG}/msvc/${s}")
endforeach()
file(GLOB MC_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mc/*.cpp")
file(GLOB SHARED_SRCS CONFIGURE_DEPENDS "${SHARED}/*.cpp")

add_library(mp SHARED ${RG_SRCS} ${MC_SRCS} ${SHARED_SRCS} "${CMAKE_CURRENT_SOURCE_DIR}/mp.def")
set_target_properties(mp PROPERTIES OUTPUT_NAME mp PREFIX "")
target_include_directories(mp PRIVATE
	"${RG}" "${RG}/hookers" "${RG}/common" "${RG}/dlls" "${RG}/engine" "${RG}/public"
	"${RG}/public/regamedll" "${RG}/pm_shared" "${RG}/regamedll" "${RG}/game_shared" "${SHARED}" "${CMAKE_CURRENT_SOURCE_DIR}/mc")
target_compile_definitions(mp PRIVATE
	REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES
	BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG MC_SERVER=1)
target_compile_options(mp PRIVATE /O2 /fp:precise /Zc:threadSafeInit- /EHsc /W1
	-Wno-everything)
target_precompile_headers(mp PRIVATE "${RG}/regamedll/precompiled.h")
set_source_files_properties(${SHARED_SRCS} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
target_link_libraries(mp PRIVATE "${RG}/lib/libacof32.lib" user32 ws2_32)
target_link_options(mp PRIVATE /SAFESEH:NO)
