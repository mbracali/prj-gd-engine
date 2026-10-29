# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
cmake_minimum_required(VERSION 3.24)
project(gd03 VERSION 0.0.1 LANGUAGES C CXX)

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
  # as libSDL3.so; the game is libgd03.so, loaded by the Java activity
  # (android/app/src/main/java/io/github/mbracali/gd03/GD03Activity.java).
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  add_subdirectory(${GD03_SDL3_SOURCE} SDL3 EXCLUDE_FROM_ALL)
  add_library(gd03 SHARED)
else()
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(gd03 WIN32 MACOSX_BUNDLE)
endif()

target_sources(gd03 PRIVATE
  src/main.cpp
  src/app.cpp
  src/audio/audio_device.cpp
  src/audio/crowd_audio.cpp
  src/audio/mixer.cpp
  src/audio/sfx_dsp.cpp
  src/audio/sfx_player.cpp
  src/audio/synth.cpp
  src/audio/vgs_stream.cpp
  src/audio/adpcm.cpp
  src/audio/sample_decode.cpp
  src/audio/xaudio_reverb.cpp
  src/char/char_objects.cpp
  src/char/facefx.cpp
  src/char/midi_parser.cpp
  src/char/char_driver.cpp
  src/char/clip.cpp
  src/char/skeleton.cpp
  src/char/rig.cpp
  src/char/character.cpp
  src/char/char_walk.cpp
  src/data/crypt.cpp
  src/data/dta.cpp
  src/data/game_data.cpp
  src/data/json.cpp
  src/data/midi.cpp
  src/data/iso9660.cpp
  src/data/random_file.cpp
  src/devaids/audit.cpp
  src/devaids/menu_keys.cpp
  src/devaids/screenshot.cpp
  src/game/camera_director.cpp
  src/game/band.cpp
  src/game/band_performer.cpp
  src/game/chart.cpp
  src/game/crowd.cpp
  src/game/player.cpp
  src/game/light_presets.cpp
  src/game/rules.cpp
  src/game/song_db.cpp
  src/game/song_setup.cpp
  src/game/venue.cpp
  src/game/world.cpp
  src/game/world_driver.cpp
  src/input/input.cpp
  src/input/input_selftest.cpp
  src/platform/app_icon.cpp
  src/platform/disc_picker.cpp
  src/platform/paths.cpp
  src/render/band_view.cpp
  src/render/canvas.cpp
  src/render/char_view.cpp
  src/render/crowd_view.cpp
  src/render/gl_api.cpp
  src/render/gl_device.cpp
  src/render/highway.cpp
  src/render/hud_view.cpp
  src/render/material.cpp
  src/render/particles.cpp
  src/render/post_fx.cpp
  src/render/renderer.cpp
  src/render/scene_gpu.cpp
  src/render/scene_view.cpp
  src/render/shadertest.cpp
  src/render/spotlight_view.cpp
  src/render/standard_shader.cpp
  src/render/texture_codec.cpp
  src/render/text_layout.cpp
  src/render/track_view.cpp
  src/render/ui_view.cpp
  src/render/venue_view.cpp
  src/scene/anim.cpp
  src/scene/bitmap.cpp
  src/scene/inflate.cpp
  src/scene/milo.cpp
  src/scene/objects.cpp
  src/scene/scenetest.cpp
  src/scene/synth_objects.cpp
  src/script/script.cpp
  src/script/script_builtins.cpp
  src/script/tasks.cpp
  src/scene/ui_objects.cpp
  src/scene/ui_styles.cpp
  src/scene/world_objects.cpp
  src/tests/scripttest.cpp
  src/ui/career.cpp
  src/ui/campaign.cpp
  src/ui/store.cpp
  src/ui/char_select.cpp
  src/ui/multi_select.cpp
  src/ui/gamepad_panel.cpp
  src/ui/port_draw.cpp
  src/ui/enhanced_panel.cpp
  src/ui/natives_game.cpp
  src/ui/natives_meta.cpp
  src/ui/panels.cpp
  src/ui/port_screens.cpp
  src/ui/screen_text.cpp
  src/ui/script_classes.cpp
  src/ui/ui_manager.cpp
  src/tests/selftest.cpp
  src/tests/sfxtest.cpp
  src/tests/facefxtest.cpp
  src/tests/uitest.cpp
  src/tests/walktest.cpp
  src/tests/venuetest.cpp
  src/tests/director_golden.cpp
  src/tests/venuetest_crowd.cpp
  src/tests/venuetest_camera.cpp
  src/tests/chartest.cpp
  src/tests/chartest_band.cpp
  src/tests/chartest_crowd.cpp
  src/tests/miditest.cpp
  src/video/movie_player.cpp
  src/video/mpeg2.cpp
  src/video/pss.cpp
)
target_include_directories(gd03 PRIVATE src)
target_link_libraries(gd03 PRIVATE SDL3::SDL3)

if(NOT WIN32 AND NOT APPLE)
  target_link_libraries(gd03 PRIVATE m)
endif()

find_package(Threads REQUIRED)
target_link_libraries(gd03 PRIVATE Threads::Threads)

target_compile_definitions(gd03 PRIVATE GD03_VERSION="${PROJECT_VERSION}")

# No FFmpeg: the PSS movies are decoded by our own MPEG-2 decoder (from
# gd-game-01) and the sound effects are SPU ADPCM (agents/DECISIONS.md D3).
target_compile_definitions(gd03 PRIVATE GD03_VIDEO=0)

# The Steam Deck download (build_linux_steamdeck.sh): the Linux program with
# the handheld settings as its defaults (gd01's GD01_STEAM_DECK).
option(GD03_STEAM_DECK "Steam Deck build: handheld default settings" OFF)
if(GD03_STEAM_DECK)
  target_compile_definitions(gd03 PRIVATE GD03_STEAM_DECK=1)
endif()

if(MSVC)
  target_compile_options(gd03 PRIVATE /W4 /permissive-)
else()
  target_compile_options(gd03 PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  # GCC 12 (the Linux download's Ubuntu 22.04 compiler) warns wrongly about
  # `"text" + std::string` (GCC bug 105329); GCC 13 and later are fine.
  target_compile_options(gd03 PRIVATE -Wno-restrict)
endif()

if(MINGW)
  # One less set of DLLs to ship next to the executable.
  target_link_options(gd03 PRIVATE -static-libgcc -static-libstdc++ -static $<$<CONFIG:Release>:-s>)
endif()

if(ANDROID)
  target_link_libraries(gd03 PRIVATE android log)
endif()

# The icon (res/, made by ../tools/make_icons.py from the repo's res/icon_full.jpg):
# the PNG built into the program (window icon, first screen), the .exe's icon
# on Windows, the .app's on macOS. Android's launcher icons are in android/.
set(GD03_ICON_FILES gd03_icon gd03_icon_small)
set(GD03_ICON_SYMBOLS kGd03IconPng kGd03IconSmallPng)
foreach(file symbol IN ZIP_LISTS GD03_ICON_FILES GD03_ICON_SYMBOLS)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png -DOUT=${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
            -DNAME=${symbol} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    VERBATIM)
  target_sources(gd03 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp)
endforeach()
# The walk test's STUB whitelist (tests/ui_stubs.txt), built in.
add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/ui_stubs_data.cpp
  COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_stubs.txt -DOUT=${CMAKE_CURRENT_BINARY_DIR}/ui_stubs_data.cpp
          -DNAME=kUiStubsTxt -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/ui_stubs.txt ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
  VERBATIM)
target_sources(gd03 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/ui_stubs_data.cpp)
if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gd03.rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/res/gd03.ico\"\n")
  target_sources(gd03 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gd03.rc)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gd03.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/gd03.ico)
endif()
if(APPLE)
  target_sources(gd03 PRIVATE res/gd03.icns)
  set_source_files_properties(res/gd03.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

set_target_properties(gd03 PROPERTIES
  MACOSX_BUNDLE_ICON_FILE gd03.icns
  MACOSX_BUNDLE_BUNDLE_NAME "GD Engine 03"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.mbracali.gd03"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# The launcher module (GD03_MODULE=ON; gd-engine-launcher/README.md): gd03 also
# as a shared library, gd-game-03.so / gd-game-03.dll, that the Guitar Dream
# Universal Launcher loads into its own window. The same sources and settings
# as the program above, plus its entry points (src/platform/module_entry.cpp,
# module_explore.cpp: the launcher's model explorer) and the launcher's SDL
# shims (gd_module_shim.h, force-included). The
# program itself is unchanged. On Android the module is libgd-game-03.so in the
# launcher's APK (gd-engine-launcher/scripts/build.sh android); it uses the
# launcher's libSDL3.so, built from the same SDL3 source.
option(GD03_MODULE "Also build gd03 as a Guitar Dream Universal Launcher module" OFF)
if(GD03_MODULE)
  set(GD03_HOST_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../gd-engine-launcher/sdk" CACHE PATH
      "The launcher's module SDK (gd_host.h, gd_module_shim.h/.cpp)")
  get_target_property(gd03_srcs gd03 SOURCES)
  list(FILTER gd03_srcs EXCLUDE REGEX "\\.(rc|icns)$")
  add_library(gd03_module SHARED ${gd03_srcs} src/platform/module_entry.cpp src/platform/module_explore.cpp
              ${GD03_HOST_SDK}/gd_module_shim.cpp)
  foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(value gd03 ${prop})
    if(value)
      set_property(TARGET gd03_module PROPERTY ${prop} ${value})
    endif()
  endforeach()
  target_include_directories(gd03_module PRIVATE ${GD03_HOST_SDK})
  target_compile_definitions(gd03_module PRIVATE GD03_MODULE=1)
  if(MSVC)
    target_compile_options(gd03_module PRIVATE "/FI${GD03_HOST_SDK}/gd_module_shim.h")
  else()
    target_compile_options(gd03_module PRIVATE "SHELL:-include \"${GD03_HOST_SDK}/gd_module_shim.h\"")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Unique symbols would keep the library loaded after the launcher unloads
    # it, and with it the game's globals of the last run.
    target_compile_options(gd03_module PRIVATE -fno-gnu-unique)
  endif()
  set_target_properties(gd03_module PROPERTIES OUTPUT_NAME gd-game-03 PREFIX ""
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  if(ANDROID)
    set_target_properties(gd03_module PROPERTIES PREFIX "lib")  # an APK packs lib*.so only
  endif()
  if(NOT WIN32 AND NOT APPLE)
    # Only the module's entry points exported.
    set_target_properties(gd03_module PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    target_link_options(gd03_module PRIVATE -Wl,--exclude-libs,ALL)
  endif()
endif()
