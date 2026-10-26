# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
cmake_minimum_required(VERSION 3.24)
project(gd02 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Same shape as gd01's CMakeLists.txt: an Android shared library (SDL3 built
# from source next to it) or a desktop executable. Only the Linux build is
# set up and tested so far; Windows (MinGW), macOS and Android follow gd01's
# build scripts later.
if(ANDROID)
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  add_subdirectory(${GD02_SDL3_SOURCE} SDL3 EXCLUDE_FROM_ALL)
  add_library(gd02 SHARED)
else()
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(gd02 WIN32 MACOSX_BUNDLE)
endif()

target_sources(gd02 PRIVATE
  src/main.cpp
  src/app.cpp
  src/audio/mixer.cpp
  src/audio/vorbis_stream.cpp
  src/data/crypt.cpp
  src/data/dta.cpp
  src/data/game_data.cpp
  src/data/midi.cpp
  src/data/mogg.cpp
  src/data/random_file.cpp
  src/data/xdvdfs.cpp
  src/devaids/screenshot.cpp
  src/game/chart.cpp
  src/game/player.cpp
  src/game/rules.cpp
  src/game/song_db.cpp
  src/game/song_setup.cpp
  src/input/keyboard.cpp
  src/platform/paths.cpp
  src/render/canvas.cpp
  src/render/highway.cpp
  src/tests/selftest.cpp
)
target_include_directories(gd02 PRIVATE src)
target_link_libraries(gd02 PRIVATE SDL3::SDL3)

# stb_vorbis (third_party/stb_vorbis, pinned; see third_party/README.md):
# the Ogg Vorbis decoder for the .mogg song audio. Built as C, with its own
# warnings silenced (it is upstream code, kept unmodified).
add_library(stb_vorbis STATIC third_party/stb_vorbis/stb_vorbis.c)
target_compile_definitions(stb_vorbis PUBLIC STB_VORBIS_NO_STDIO)
target_include_directories(stb_vorbis SYSTEM PUBLIC third_party/stb_vorbis)
if(MSVC)
  target_compile_options(stb_vorbis PRIVATE /w)
else()
  target_compile_options(stb_vorbis PRIVATE -w)
endif()
set_target_properties(stb_vorbis PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(gd02 PRIVATE stb_vorbis)
if(NOT WIN32 AND NOT APPLE)
  target_link_libraries(gd02 PRIVATE m)
endif()

find_package(Threads REQUIRED)
target_link_libraries(gd02 PRIVATE Threads::Threads)

target_compile_definitions(gd02 PRIVATE GD02_VERSION="${PROJECT_VERSION}")

if(MSVC)
  target_compile_options(gd02 PRIVATE /W4 /permissive-)
else()
  target_compile_options(gd02 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
  # One less set of DLLs to ship next to the executable.
  target_link_options(gd02 PRIVATE -static-libgcc -static-libstdc++ -static $<$<CONFIG:Release>:-s>)
endif()

if(ANDROID)
  target_link_libraries(gd02 PRIVATE android log)
endif()

set_target_properties(gd02 PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "GD Engine 02"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.mbracali.gd02"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
