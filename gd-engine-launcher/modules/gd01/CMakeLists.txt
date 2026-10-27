# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gd01 (gd-game-01) as a Guitar Dream Universal Launcher module:
# gd-game-01.so / gd-game-01.dll. gd-game-01 is finished and frozen, so this
# builds its sources as they are, from here:
#   - the source list is read from gd-game-01/engine/CMakeLists.txt;
#   - src/main.cpp's main() is compiled as gd01_main (a definition for that file);
#   - ../../sdk/gd_module_shim.h is force-included in every C++ file: SDL_Init,
#     SDL_Quit, SDL_CreateWindow, SDL_DestroyWindow and the GL context calls go
#     through the launcher's shims (the launcher's window, its SDL);
#   - gd01_module_entry.cpp is the module's gd_game_run, gd01_module_explore.cpp
#     its gd_game_explore (the launcher's model explorer, with gd01's loaders).
# Used by ../../scripts/build.sh.
cmake_minimum_required(VERSION 3.24)
project(gd01_module LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(GD01_ENGINE "${CMAKE_CURRENT_SOURCE_DIR}/../../../gd-game-01/engine" CACHE PATH "gd-game-01/engine")
set(GDUL_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../sdk")
get_filename_component(GD01_ENGINE "${GD01_ENGINE}" ABSOLUTE)

# gd01's version and source list, from its own CMakeLists.txt.
file(READ "${GD01_ENGINE}/CMakeLists.txt" gd01_cmake)
string(REGEX MATCH "project\\(gd01 VERSION ([0-9.]+)" _ "${gd01_cmake}")
set(GD01_VERSION "${CMAKE_MATCH_1}")
string(REGEX MATCH "target_sources\\(gd01 PRIVATE([^)]*)\\)" _ "${gd01_cmake}")
string(REGEX REPLACE "[ \t\r\n]+" ";" gd01_srcs "${CMAKE_MATCH_1}")
list(FILTER gd01_srcs INCLUDE REGEX "\\.cpp$")
list(TRANSFORM gd01_srcs PREPEND "${GD01_ENGINE}/")
if(NOT GD01_VERSION OR NOT gd01_srcs)
  message(FATAL_ERROR "gd01 module: cannot read the version or sources from ${GD01_ENGINE}/CMakeLists.txt")
endif()
message(STATUS "gd01 module: gd01 ${GD01_VERSION}")

find_package(SDL3 REQUIRED CONFIG)
add_library(gd01_module SHARED ${gd01_srcs} gd01_module_entry.cpp gd01_module_explore.cpp ${GDUL_SDK}/gd_module_shim.cpp)
target_include_directories(gd01_module PRIVATE ${GD01_ENGINE}/src ${GDUL_SDK})
target_link_libraries(gd01_module PRIVATE SDL3::SDL3)
target_compile_definitions(gd01_module PRIVATE GD01_VERSION="${GD01_VERSION}")
set_source_files_properties(${GD01_ENGINE}/src/main.cpp PROPERTIES COMPILE_DEFINITIONS "main=gd01_main")

# The AI upscalers, as gd01 builds them: FSR 3.1 and volk always, DLSS where
# its SDK is present (Linux x86-64; NGX finds its model library next to the
# launcher, where scripts/build.sh puts it).
add_subdirectory(${GD01_ENGINE}/third_party/fidelityfx fidelityfx)
target_include_directories(gd01_module SYSTEM PRIVATE ${GD01_ENGINE}/third_party/vulkan ${GD01_ENGINE}/third_party/volk
                           ${GD01_ENGINE}/third_party/fidelityfx/include)
target_link_libraries(gd01_module PRIVATE ffx_fsr3 volk ${CMAKE_DL_LIBS})
set(GD01_DLSS_DIR ${GD01_ENGINE}/third_party/dlss)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND EXISTS ${GD01_DLSS_DIR}/lib/Linux_x86_64/libnvsdk_ngx.a)
  target_compile_definitions(gd01_module PRIVATE GD01_DLSS=1)
  target_include_directories(gd01_module SYSTEM PRIVATE ${GD01_DLSS_DIR}/include)
  target_link_libraries(gd01_module PRIVATE ${GD01_DLSS_DIR}/lib/Linux_x86_64/libnvsdk_ngx.a)
else()
  target_compile_definitions(gd01_module PRIVATE GD01_DLSS=0)
endif()

option(GD01_STEAM_DECK "Steam Deck build: handheld default settings" OFF)
if(GD01_STEAM_DECK)
  target_compile_definitions(gd01_module PRIVATE GD01_STEAM_DECK=1)
endif()

# gd01's own warning level; the shims are force-included.
if(MSVC)
  target_compile_options(gd01_module PRIVATE /W4 /permissive- "/FI${GDUL_SDK}/gd_module_shim.h")
else()
  target_compile_options(gd01_module PRIVATE -Wall -Wextra -Wpedantic "SHELL:-include \"${GDUL_SDK}/gd_module_shim.h\"")
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # Unique symbols would keep the library loaded after the launcher unloads it.
  target_compile_options(gd01_module PRIVATE -fno-gnu-unique)
endif()
if(MINGW)
  target_link_options(gd01_module PRIVATE -static-libgcc -static-libstdc++ -static $<$<CONFIG:Release>:-s>)
elseif(NOT APPLE)
  target_link_options(gd01_module PRIVATE -Wl,--exclude-libs,ALL)
endif()

# The icons built into gd01 (window icon, first screen).
set(GD01_ICON_FILES gd01_icon gd01_icon_small)
set(GD01_ICON_SYMBOLS kGd01IconPng kGd01IconSmallPng)
foreach(file symbol IN ZIP_LISTS GD01_ICON_FILES GD01_ICON_SYMBOLS)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
    COMMAND ${CMAKE_COMMAND} -DIN=${GD01_ENGINE}/res/${file}.png -DOUT=${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
            -DNAME=${symbol} -P ${GD01_ENGINE}/cmake/embed_file.cmake
    DEPENDS ${GD01_ENGINE}/res/${file}.png ${GD01_ENGINE}/cmake/embed_file.cmake
    VERBATIM)
  target_sources(gd01_module PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp)
endforeach()

set_target_properties(gd01_module PROPERTIES OUTPUT_NAME gd-game-01 PREFIX ""
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
