# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
cmake_minimum_required(VERSION 3.24)
project(gd02 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Same shape as gd01's CMakeLists.txt: an Android shared library or a desktop
# executable (Linux, Windows with MinGW-w64, a macOS .app). The builds:
# build_*.sh, package.sh and ../local_build.sh.
if(ANDROID)
  # Android (engine/build_android.sh): SDL3 built from source next to the game
  # as libSDL3.so; the game is libgd02.so, loaded by the Java activity
  # (android/app/src/main/java/io/github/mbracali/gd02/GD02Activity.java).
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
  src/data/json.cpp
  src/data/midi.cpp
  src/data/mogg.cpp
  src/data/random_file.cpp
  src/data/xdvdfs.cpp
  src/devaids/menu_keys.cpp
  src/devaids/screenshot.cpp
  src/game/chart.cpp
  src/game/player.cpp
  src/game/rules.cpp
  src/game/song_db.cpp
  src/game/song_setup.cpp
  src/input/input.cpp
  src/input/input_selftest.cpp
  src/platform/app_icon.cpp
  src/platform/disc_picker.cpp
  src/platform/paths.cpp
  src/render/canvas.cpp
  src/render/gl_api.cpp
  src/render/gl_device.cpp
  src/render/highway.cpp
  src/render/hud_view.cpp
  src/render/material.cpp
  src/render/particles.cpp
  src/render/renderer.cpp
  src/render/scene_gpu.cpp
  src/render/scene_view.cpp
  src/render/shadertest.cpp
  src/render/standard_shader.cpp
  src/render/texture_codec.cpp
  src/render/text_layout.cpp
  src/render/track_view.cpp
  src/render/ui_view.cpp
  src/scene/anim.cpp
  src/scene/bitmap.cpp
  src/scene/inflate.cpp
  src/scene/milo.cpp
  src/scene/objects.cpp
  src/scene/scenetest.cpp
  src/script/script.cpp
  src/script/script_builtins.cpp
  src/script/tasks.cpp
  src/scene/ui_objects.cpp
  src/scene/ui_styles.cpp
  src/tests/scripttest.cpp
  src/ui/career.cpp
  src/ui/gamepad_panel.cpp
  src/ui/natives_game.cpp
  src/ui/natives_meta.cpp
  src/ui/panels.cpp
  src/ui/port_screens.cpp
  src/ui/screen_text.cpp
  src/ui/script_classes.cpp
  src/ui/ui_manager.cpp
  src/tests/selftest.cpp
  src/tests/uitest.cpp
  src/tests/walktest.cpp
  src/video/wmv_player.cpp
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

# GH2's videos (src/video/wmv_player.cpp): FFmpeg's libavformat, libavcodec
# and libavutil as shared libraries, the minimal LGPL build of
# build_common.sh's build_ffmpeg, installed in GD02_FFMPEG_DIR (on Android one
# build per ABI: <dir>/<ABI>/ffmpeg-prefix), or else the system's (pkg-config). The build
# copies the libraries next to the program (Linux: found through RPATH
# $ORIGIN; Windows: the DLLs beside the .exe); build_macos.sh's dylibbundler
# puts them in the .app, and the Android Gradle plugin into the APK. Without
# FFmpeg (GD02_VIDEO OFF) the videos are skipped with a log line.
set(GD02_FFMPEG_DIR "" CACHE PATH "FFmpeg install prefix (include/, lib/) for the video player")
if(GD02_FFMPEG_DIR)
  option(GD02_VIDEO "Play GH2's videos with FFmpeg" ON)
else()
  option(GD02_VIDEO "Play GH2's videos with FFmpeg" OFF)
endif()
if(GD02_VIDEO)
  set(ffmpeg_dir "${GD02_FFMPEG_DIR}")
  if(ANDROID AND ffmpeg_dir)
    set(ffmpeg_dir "${ffmpeg_dir}/${ANDROID_ABI}/ffmpeg-prefix")
  endif()
  if(ffmpeg_dir)
    target_include_directories(gd02 SYSTEM PRIVATE "${ffmpeg_dir}/include")
    foreach(lib avformat avcodec avutil)
      find_library(GD02_FFMPEG_${lib} NAMES ${lib} PATHS "${ffmpeg_dir}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
      if(NOT GD02_FFMPEG_${lib})
        message(FATAL_ERROR "GD02_FFMPEG_DIR: no ${lib} library in ${ffmpeg_dir}/lib")
      endif()
      add_library(gd02_ffmpeg_${lib} SHARED IMPORTED)
      if(WIN32)
        file(GLOB dll "${ffmpeg_dir}/bin/${lib}-*.dll")
        set_target_properties(gd02_ffmpeg_${lib} PROPERTIES IMPORTED_IMPLIB "${GD02_FFMPEG_${lib}}" IMPORTED_LOCATION "${dll}")
        add_custom_command(TARGET gd02 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:gd02>")
      else()
        set_target_properties(gd02_ffmpeg_${lib} PROPERTIES IMPORTED_LOCATION "${GD02_FFMPEG_${lib}}")
        if(NOT APPLE AND NOT ANDROID)
          # The file named by the library's SONAME (libavcodec.so.61), copied next to the program.
          file(GLOB sonames "${ffmpeg_dir}/lib/lib${lib}.so.[0-9]*")
          list(FILTER sonames INCLUDE REGEX "\\.so\\.[0-9]+$")
          add_custom_command(TARGET gd02 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${sonames} "$<TARGET_FILE_DIR:gd02>")
        endif()
      endif()
      target_link_libraries(gd02 PRIVATE gd02_ffmpeg_${lib})
    endforeach()
    if(NOT APPLE AND NOT ANDROID AND NOT WIN32)
      set_target_properties(gd02 PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    endif()
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GD02_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
    target_link_libraries(gd02 PRIVATE PkgConfig::GD02_FFMPEG)
  endif()
  target_compile_definitions(gd02 PRIVATE GD02_VIDEO=1)
  message(STATUS "gd02: videos with FFmpeg (${ffmpeg_dir})")
else()
  target_compile_definitions(gd02 PRIVATE GD02_VIDEO=0)
  message(STATUS "gd02: no FFmpeg (GD02_VIDEO OFF): videos are skipped")
endif()

# The Steam Deck download (build_linux_steamdeck.sh): the Linux program with
# the handheld settings as its defaults (gd01's GD01_STEAM_DECK).
option(GD02_STEAM_DECK "Steam Deck build: handheld default settings" OFF)
if(GD02_STEAM_DECK)
  target_compile_definitions(gd02 PRIVATE GD02_STEAM_DECK=1)
endif()

if(MSVC)
  target_compile_options(gd02 PRIVATE /W4 /permissive-)
else()
  target_compile_options(gd02 PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  # GCC 12 (the Linux download's Ubuntu 22.04 compiler) warns wrongly about
  # `"text" + std::string` (GCC bug 105329); GCC 13 and later are fine.
  target_compile_options(gd02 PRIVATE -Wno-restrict)
endif()

if(MINGW)
  # One less set of DLLs to ship next to the executable.
  target_link_options(gd02 PRIVATE -static-libgcc -static-libstdc++ -static $<$<CONFIG:Release>:-s>)
endif()

if(ANDROID)
  target_link_libraries(gd02 PRIVATE android log)
endif()

# The icon (res/, made by ../tools/make_icons.py from the repo's res/icon_full.jpg):
# the PNG built into the program (window icon, first screen), the .exe's icon
# on Windows, the .app's on macOS. Android's launcher icons are in android/.
set(GD02_ICON_FILES gd02_icon gd02_icon_small)
set(GD02_ICON_SYMBOLS kGd02IconPng kGd02IconSmallPng)
foreach(file symbol IN ZIP_LISTS GD02_ICON_FILES GD02_ICON_SYMBOLS)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png -DOUT=${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
            -DNAME=${symbol} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    VERBATIM)
  target_sources(gd02 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp)
endforeach()
# The walk test's STUB whitelist (tests/ui_stubs.txt), built in.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui_stubs_data.cpp
  COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_stubs.txt -DOUT=${CMAKE_CURRENT_BINARY_DIR}/ui_stubs_data.cpp
          -DNAME=kUiStubsTxt -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_stubs.txt ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
  VERBATIM)
target_sources(gd02 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ui_stubs_data.cpp)
if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gd02.rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/res/gd02.ico\"\n")
  target_sources(gd02 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gd02.rc)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gd02.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/gd02.ico)
endif()
if(APPLE)
  target_sources(gd02 PRIVATE res/gd02.icns)
  set_source_files_properties(res/gd02.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

set_target_properties(gd02 PROPERTIES
  MACOSX_BUNDLE_ICON_FILE gd02.icns
  MACOSX_BUNDLE_BUNDLE_NAME "GD Engine 02"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.mbracali.gd02"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
