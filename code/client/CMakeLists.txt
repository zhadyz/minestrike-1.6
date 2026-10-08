cmake_minimum_required(VERSION 3.20)
project(csmc_client C CXX)

set(HLSDK "${CMAKE_CURRENT_SOURCE_DIR}/../../src/halflife")
set(SHARED "${CMAKE_CURRENT_SOURCE_DIR}/../shared")

file(GLOB CLIENT_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c")
file(GLOB SHARED_SRC CONFIGURE_DEPENDS "${SHARED}/*.cpp")

add_library(client SHARED ${CLIENT_SRC} ${SHARED_SRC})
set_target_properties(client PROPERTIES OUTPUT_NAME client PREFIX "")
target_include_directories(client PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${SHARED}"
	"${CMAKE_CURRENT_SOURCE_DIR}/../third_party"
	"${HLSDK}/cl_dll"
	"${HLSDK}/common"
	"${HLSDK}/engine"
	"${HLSDK}/public"
	"${HLSDK}/pm_shared"
	"${HLSDK}/dlls"
	"${HLSDK}/game_shared")
target_compile_definitions(client PRIVATE CLIENT_DLL _CRT_SECURE_NO_WARNINGS MC_CLIENT=1)
target_compile_options(client PRIVATE /O2 /EHsc /W3 -Wno-microsoft-cast -Wno-writable-strings -Wno-unused-value
	-Wno-deprecated-declarations -Wno-nonportable-include-path -Wno-ignored-attributes -Wno-invalid-noreturn
	-Wno-microsoft-enum-forward-reference -Wno-dangling-else -Wno-null-dereference)
target_link_libraries(client PRIVATE opengl32 user32 gdi32)
