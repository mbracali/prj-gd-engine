# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
cmake_minimum_required(VERSION 3.24)
project(gd04 VERSION 0.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)  # as gd-game-02 (agents/DECISIONS.md D2)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Same shape as gd01's CMakeLists.txt: an Android shared library or a desktop
# executable (Linux, Windows with MinGW-w64, a macOS .app). The builds:
# build_*.sh, package.sh and ../local_build.sh.
if(ANDROID)
  # Android (engine/build_android.sh): SDL3 built from source next to the game
  # as libSDL3.so; the game is libgd04.so, loaded by the Java activity
  # (android/app/src/main/java/io/github/mbracali/gd04/GD04Activity.java).
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  add_subdirectory(${GD04_SDL3_SOURCE} SDL3 EXCLUDE_FROM_ALL)
  add_library(gd04 SHARED)
else()
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(gd04 WIN32 MACOSX_BUNDLE)
endif()

target_sources(gd04 PRIVATE
  src/main.cpp
  src/app.cpp
  src/audio/audio_device.cpp
  src/audio/mixer.cpp
  src/audio/sfx_dsp.cpp
  src/audio/song_stream.cpp
  src/audio/xma_decoder.cpp
  src/audio/xma_stream.cpp
  src/data/anim.cpp
  src/data/disc.cpp
  src/data/font.cpp
  src/data/fsb.cpp
  src/data/inflate.cpp
  src/data/json.cpp
  src/data/pak.cpp
  src/data/qb.cpp
  src/data/qbkey.cpp
  src/data/random_file.cpp
  src/data/scene.cpp
  src/data/texture.cpp
  src/data/xdvdfs.cpp
  src/devaids/menu_keys.cpp
  src/devaids/screenshot.cpp
  src/input/input.cpp
  src/input/input_selftest.cpp
  src/platform/app_icon.cpp
  src/platform/disc_picker.cpp
  src/platform/paths.cpp
  src/render/canvas.cpp
  src/render/gl_api.cpp
  src/render/gl_device.cpp
  src/render/highway2d.cpp
  src/render/renderer.cpp
  src/render/shadertest.cpp
  src/render/texture_cache.cpp
  src/render/texture_codec.cpp
  src/game/chart.cpp
  src/game/note_track.cpp
  src/game/player.cpp
  src/game/song_db.cpp
  src/game/tuning.cpp
  src/script/bytecode.cpp
  src/script/globals.cpp
  src/script/natives_core.cpp
  src/script/vm.cpp
  src/tests/audittest.cpp
  src/tests/format_checks.cpp
  src/tests/scripttest.cpp
  src/tests/selftest.cpp
  src/tests/selftest_play.cpp
  src/tests/sfxtest.cpp
  src/ui/song_screen.cpp
  src/ui/front_end.cpp
  src/ui/natives_game.cpp
  src/ui/ui_system.cpp
  src/video/movie_player.cpp
  src/tests/formattest.cpp
)
target_include_directories(gd04 PRIVATE src)
target_link_libraries(gd04 PRIVATE SDL3::SDL3)

if(NOT WIN32 AND NOT APPLE)
  target_link_libraries(gd04 PRIVATE m)
endif()

find_package(Threads REQUIRED)
target_link_libraries(gd04 PRIVATE Threads::Threads)

target_compile_definitions(gd04 PRIVATE GD04_VERSION="${PROJECT_VERSION}")

# GH3's videos (Bink, the video player comes with M3) and sounds (XMA,
# src/audio/xma_decoder.cpp): FFmpeg's libavformat, libavcodec
# and libavutil as shared libraries, the minimal LGPL build of
# build_common.sh's build_ffmpeg, installed in GD04_FFMPEG_DIR (on Android one
# build per ABI: <dir>/<ABI>/ffmpeg-prefix), or else the system's (pkg-config). The build
# copies the libraries next to the program (Linux: found through RPATH
# $ORIGIN; Windows: the DLLs beside the .exe); build_macos.sh's dylibbundler
# puts them in the .app, and the Android Gradle plugin into the APK. Without
# FFmpeg (GD04_VIDEO OFF) the videos are skipped with a log line and the
# sound effects are silent.
set(GD04_FFMPEG_DIR "" CACHE PATH "FFmpeg install prefix (include/, lib/) for the video player")
if(GD04_FFMPEG_DIR)
  option(GD04_VIDEO "Play GH3's videos and sounds with FFmpeg" ON)
else()
  option(GD04_VIDEO "Play GH3's videos and sounds with FFmpeg" OFF)
endif()
if(GD04_VIDEO)
  set(ffmpeg_dir "${GD04_FFMPEG_DIR}")
  if(ANDROID AND ffmpeg_dir)
    set(ffmpeg_dir "${ffmpeg_dir}/${ANDROID_ABI}/ffmpeg-prefix")
  endif()
  if(ffmpeg_dir)
    target_include_directories(gd04 SYSTEM PRIVATE "${ffmpeg_dir}/include")
    foreach(lib avformat avcodec avutil)
      find_library(GD04_FFMPEG_${lib} NAMES ${lib} PATHS "${ffmpeg_dir}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
      if(NOT GD04_FFMPEG_${lib})
        message(FATAL_ERROR "GD04_FFMPEG_DIR: no ${lib} library in ${ffmpeg_dir}/lib")
      endif()
      add_library(gd04_ffmpeg_${lib} SHARED IMPORTED)
      if(WIN32)
        file(GLOB dll "${ffmpeg_dir}/bin/${lib}-*.dll")
        set_target_properties(gd04_ffmpeg_${lib} PROPERTIES IMPORTED_IMPLIB "${GD04_FFMPEG_${lib}}" IMPORTED_LOCATION "${dll}")
        add_custom_command(TARGET gd04 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:gd04>")
      else()
        set_target_properties(gd04_ffmpeg_${lib} PROPERTIES IMPORTED_LOCATION "${GD04_FFMPEG_${lib}}")
        if(NOT APPLE AND NOT ANDROID)
          # The file named by the library's SONAME (libavcodec.so.61), copied next to the program.
          file(GLOB sonames "${ffmpeg_dir}/lib/lib${lib}.so.[0-9]*")
          list(FILTER sonames INCLUDE REGEX "\\.so\\.[0-9]+$")
          add_custom_command(TARGET gd04 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${sonames} "$<TARGET_FILE_DIR:gd04>")
        endif()
      endif()
      target_link_libraries(gd04 PRIVATE gd04_ffmpeg_${lib})
    endforeach()
    if(NOT APPLE AND NOT ANDROID AND NOT WIN32)
      set_target_properties(gd04 PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    endif()
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GD04_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
    target_link_libraries(gd04 PRIVATE PkgConfig::GD04_FFMPEG)
  endif()
  target_compile_definitions(gd04 PRIVATE GD04_VIDEO=1)
  message(STATUS "gd04: videos with FFmpeg (${ffmpeg_dir})")
else()
  target_compile_definitions(gd04 PRIVATE GD04_VIDEO=0)
  message(STATUS "gd04: no FFmpeg (GD04_VIDEO OFF): videos are skipped")
endif()

# The Steam Deck download (build_linux_steamdeck.sh): the Linux program with
# the handheld settings as its defaults (gd01's GD01_STEAM_DECK).
option(GD04_STEAM_DECK "Steam Deck build: handheld default settings" OFF)
if(GD04_STEAM_DECK)
  target_compile_definitions(gd04 PRIVATE GD04_STEAM_DECK=1)
endif()

if(MSVC)
  target_compile_options(gd04 PRIVATE /W4 /permissive-)
else()
  target_compile_options(gd04 PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  # GCC 12 (the Linux download's Ubuntu 22.04 compiler) warns wrongly about
  # `"text" + std::string` (GCC bug 105329); GCC 13 and later are fine.
  target_compile_options(gd04 PRIVATE -Wno-restrict)
endif()

if(MINGW)
  # One less set of DLLs to ship next to the executable.
  target_link_options(gd04 PRIVATE -static-libgcc -static-libstdc++ -static $<$<CONFIG:Release>:-s>)
endif()

if(ANDROID)
  target_link_libraries(gd04 PRIVATE android log)
endif()

# The icon (res/, made by ../tools/make_icons.py from the repo's res/icon_full.jpg):
# the PNG built into the program (window icon, first screen), the .exe's icon
# on Windows, the .app's on macOS. Android's launcher icons are in android/.
set(GD04_ICON_FILES gd04_icon gd04_icon_small)
set(GD04_ICON_SYMBOLS kGd04IconPng kGd04IconSmallPng)
foreach(file symbol IN ZIP_LISTS GD04_ICON_FILES GD04_ICON_SYMBOLS)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png -DOUT=${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
            -DNAME=${symbol} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    VERBATIM)
  target_sources(gd04 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp)
endforeach()
if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gd04.rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/res/gd04.ico\"\n")
  target_sources(gd04 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gd04.rc)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gd04.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/gd04.ico)
endif()
if(APPLE)
  target_sources(gd04 PRIVATE res/gd04.icns)
  set_source_files_properties(res/gd04.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

set_target_properties(gd04 PROPERTIES
  MACOSX_BUNDLE_ICON_FILE gd04.icns
  MACOSX_BUNDLE_BUNDLE_NAME "GD Engine 02"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.mbracali.gd04"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# The launcher module (GD04_MODULE=ON; gd-engine-launcher/README.md): gd04 also
# as a shared library, gd-game-04.so / gd-game-04.dll, that the Guitar Dream
# Universal Launcher loads into its own window. The same sources and settings
# as the program above, plus its entry points (src/platform/module_entry.cpp,
# module_explore.cpp: the launcher's model explorer) and the launcher's SDL
# shims (gd_module_shim.h, force-included). The
# program itself is unchanged. On Android the module is libgd-game-04.so in the
# launcher's APK (gd-engine-launcher/scripts/build.sh android); it uses the
# launcher's libSDL3.so, built from the same SDL3 source.
option(GD04_MODULE "Also build gd04 as a Guitar Dream Universal Launcher module" OFF)
if(GD04_MODULE)
  set(GD04_HOST_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../gd-engine-launcher/sdk" CACHE PATH
      "The launcher's module SDK (gd_host.h, gd_module_shim.h/.cpp)")
  get_target_property(gd04_srcs gd04 SOURCES)
  list(FILTER gd04_srcs EXCLUDE REGEX "\\.(rc|icns)$")
  add_library(gd04_module SHARED ${gd04_srcs} src/platform/module_entry.cpp
              ${GD04_HOST_SDK}/gd_module_shim.cpp)
  foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(value gd04 ${prop})
    if(value)
      set_property(TARGET gd04_module PROPERTY ${prop} ${value})
    endif()
  endforeach()
  if(GD04_VIDEO AND ffmpeg_dir)
    target_include_directories(gd04_module SYSTEM PRIVATE "${ffmpeg_dir}/include")
  endif()
  target_include_directories(gd04_module PRIVATE ${GD04_HOST_SDK})
  target_compile_definitions(gd04_module PRIVATE GD04_MODULE=1)
  if(MSVC)
    target_compile_options(gd04_module PRIVATE "/FI${GD04_HOST_SDK}/gd_module_shim.h")
  else()
    target_compile_options(gd04_module PRIVATE "SHELL:-include \"${GD04_HOST_SDK}/gd_module_shim.h\"")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Unique symbols would keep the library loaded after the launcher unloads
    # it, and with it the game's globals of the last run.
    target_compile_options(gd04_module PRIVATE -fno-gnu-unique)
  endif()
  set_target_properties(gd04_module PROPERTIES OUTPUT_NAME gd-game-04 PREFIX ""
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  if(ANDROID)
    set_target_properties(gd04_module PROPERTIES PREFIX "lib")  # an APK packs lib*.so only
  endif()
  if(NOT WIN32 AND NOT APPLE)
    # FFmpeg next to the module; only the gd_game_* entry points exported.
    set_target_properties(gd04_module PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    target_link_options(gd04_module PRIVATE -Wl,--exclude-libs,ALL)
  endif()
endif()
