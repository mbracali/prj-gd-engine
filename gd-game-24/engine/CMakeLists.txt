# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
cmake_minimum_required(VERSION 3.24)
project(gd24 VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# gd24: Rock Band Unplugged (PSP). A desktop program (Linux, Windows with
# MinGW-w64, macOS) and, with GD24_MODULE, the launcher's module. Not ready
# to play (game-info.yaml): the launcher does not package it yet.
find_package(SDL3 REQUIRED CONFIG)
add_executable(gd24 WIN32 MACOSX_BUNDLE)
target_sources(gd24 PRIVATE
  src/main.cpp
  src/app.cpp
  src/audio/at3_preview.cpp
  src/audio/audio_out.cpp
  src/data/param_sfo.cpp
  src/data/song_list.cpp
  src/data/str_file.cpp
  src/data/text_table.cpp
  src/data/umd.cpp
  src/devaids/dev_aids.cpp
  src/platform/paths.cpp
  src/platform/settings.cpp
  src/render/canvas.cpp
  src/tests/audiotest.cpp
)
target_include_directories(gd24 PRIVATE src)
target_compile_definitions(gd24 PRIVATE GD24_VERSION="${PROJECT_VERSION}")
target_link_libraries(gd24 PRIVATE SDL3::SDL3)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(gd24 PRIVATE -Wall -Wextra)
endif()

# FFmpeg (the song previews' ATRAC3plus, src/audio/at3_preview.cpp): gd24's
# own minimal LGPL build (build_common.sh, build_ffmpeg.sh) in GD24_FFMPEG_DIR,
# shared libraries next to the program / module, as gd-game-10's. Without it
# the previews are silent (a log line).
set(GD24_FFMPEG_DIR "" CACHE PATH "FFmpeg install prefix (include/, lib/): build_ffmpeg.sh's build/ffmpeg/linux/ffmpeg-prefix")
set(gd24_ffmpeg_libs "")
if(GD24_FFMPEG_DIR)
  target_include_directories(gd24 SYSTEM PRIVATE "${GD24_FFMPEG_DIR}/include")
  foreach(lib avcodec avutil)
    find_library(GD24_FFMPEG_${lib} NAMES ${lib} PATHS "${GD24_FFMPEG_DIR}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
    if(NOT GD24_FFMPEG_${lib})
      message(FATAL_ERROR "GD24_FFMPEG_DIR: no ${lib} library in ${GD24_FFMPEG_DIR}/lib")
    endif()
    target_link_libraries(gd24 PRIVATE ${GD24_FFMPEG_${lib}})
  endforeach()
  target_compile_definitions(gd24 PRIVATE GD24_FFMPEG=1)
  message(STATUS "gd24: song previews with FFmpeg (${GD24_FFMPEG_DIR})")
else()
  message(STATUS "gd24: no FFmpeg (GD24_FFMPEG_DIR empty): the song previews are silent")
endif()

# gd-engine-core (../../gd-engine-core/docs/README.md): the disc image reader
# (.iso and .chd, platform/file.h), later the render / audio / input backends.
option(GD24_MODULE "Also build gd24 as a Guitar Dream Universal Launcher module" OFF)
set(GDCORE_LAUNCHER_MODULE ${GD24_MODULE})
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../gd-engine-core ${CMAKE_CURRENT_BINARY_DIR}/gd-engine-core)
target_link_libraries(gd24 PRIVATE gdcore)

# The launcher module: the same sources and settings plus the entry point and
# the launcher's SDL shims (gd_module_shim.h, force-included), as gd-game-05.
if(GD24_MODULE)
  set(GD24_HOST_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../gd-engine-launcher/sdk" CACHE PATH
      "The launcher's module SDK (gd_host.h, gd_module_shim.h/.cpp)")
  get_target_property(gd24_srcs gd24 SOURCES)
  add_library(gd24_module SHARED ${gd24_srcs} src/platform/module_entry.cpp ${GD24_HOST_SDK}/gd_module_shim.cpp)
  foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(value gd24 ${prop})
    if(value)
      set_property(TARGET gd24_module PROPERTY ${prop} ${value})
    endif()
  endforeach()
  target_include_directories(gd24_module PRIVATE ${GD24_HOST_SDK})
  target_compile_definitions(gd24_module PRIVATE GD24_MODULE=1)
  if(MSVC)
    target_compile_options(gd24_module PRIVATE "/FI${GD24_HOST_SDK}/gd_module_shim.h")
  else()
    target_compile_options(gd24_module PRIVATE "SHELL:-include \"${GD24_HOST_SDK}/gd_module_shim.h\"")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(gd24_module PRIVATE -fno-gnu-unique)
  endif()
  set_target_properties(gd24_module PROPERTIES OUTPUT_NAME gd-game-24 PREFIX ""
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  if(NOT WIN32 AND NOT APPLE)
    set_target_properties(gd24_module PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    target_link_options(gd24_module PRIVATE -Wl,--exclude-libs,ALL)
  endif()
endif()
