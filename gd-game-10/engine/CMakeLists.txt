# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
cmake_minimum_required(VERSION 3.24)
project(gd10 VERSION 0.0.0 LANGUAGES C CXX)

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
  # as libSDL3.so; the game is libgd10.so, loaded by the Java activity
  # (android/app/src/main/java/io/github/mbracali/gd10/GD10Activity.java).
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  add_subdirectory(${GD10_SDL3_SOURCE} SDL3 EXCLUDE_FROM_ALL)
  add_library(gd10 SHARED)
else()
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(gd10 WIN32 MACOSX_BUNDLE)
endif()

target_sources(gd10 PRIVATE
  src/main.cpp
  src/app.cpp
  src/audio/audio_device.cpp
  src/audio/mixer.cpp
  src/audio/fast_pitch.cpp
  src/audio/guitar_fx.cpp
  src/audio/sfx_dsp.cpp
  src/audio/sfx.cpp
  src/audio/sound_buss.cpp
  src/audio/song_stream.cpp
  src/audio/xma_decoder.cpp
  src/audio/xma_stream.cpp
  src/data/anim.cpp
  src/data/band.cpp
  src/data/disc.cpp
  src/data/font.cpp
  src/data/fsb.cpp
  src/data/inflate.cpp
  src/data/json.cpp
  src/data/matlib.cpp
  src/data/pak.cpp
  src/data/qb.cpp
  src/data/qs.cpp
  src/data/qbkey.cpp
  src/data/random_file.cpp
  src/data/havok.cpp
  src/data/scene.cpp
  src/data/texture.cpp
  src/data/venue.cpp
  src/data/xdvdfs.cpp
  src/devaids/menu_keys.cpp
  src/devaids/screenshot.cpp
  src/input/input.cpp
  src/input/midi_in.cpp
  src/input/input_selftest.cpp
  src/platform/app_icon.cpp
  src/platform/disc_picker.cpp
  src/platform/paths.cpp
  src/render/band.cpp
  src/render/band_anim.cpp
  src/render/band_ragdoll.cpp
  src/render/band_view.cpp
  src/render/canvas.cpp
  src/render/core_backend.cpp
  src/render/core_device.cpp
  src/render/crowd.cpp
  src/render/gl_api.cpp
  src/render/gh3_shader.cpp
  src/render/gl_device.cpp
  src/render/highway2d.cpp
  src/render/hud2d.cpp
  src/render/hud2p.cpp
  src/render/particles.cpp
  src/render/renderer.cpp
  src/render/screen_fx.cpp
  src/render/enhanced_fx.cpp
  src/render/enhanced_lights.cpp
  src/render/image_enhance.cpp
  src/render/stage_peds.cpp
  src/render/shadertest.cpp
  src/render/spirv_compile.cpp
  src/render/texture_cache.cpp
  src/render/texture_codec.cpp
  src/render/venue_camera.cpp
  src/render/venue_lights.cpp
  src/render/venue_stage.cpp
  src/render/venue_view.cpp
  src/render/xenos_glsl.cpp
  src/game/chart.cpp
  src/game/note_track.cpp
  src/game/player.cpp
  src/game/match.cpp
  src/game/save.cpp
  src/game/song_db.cpp
  src/game/tuning.cpp
  src/script/bytecode.cpp
  src/script/globals.cpp
  src/script/pc_strings.cpp
  src/script/natives_core.cpp
  src/script/atoms.cpp
  src/script/natives_misc.cpp
  src/script/vm.cpp
  src/tests/audittest.cpp
  src/tests/chartest.cpp
  src/tests/format_checks.cpp
  src/tests/scripttest.cpp
  src/tests/selftest.cpp
  src/tests/walk.cpp
  src/tests/selftest_play.cpp
  src/tests/sfxtest.cpp
  src/tests/venuetest.cpp
  src/ui/song_screen.cpp
  src/ui/song_hooks.cpp
  src/ui/song_preview.cpp
  src/ui/fx2d.cpp
  src/ui/front_end.cpp
  src/ui/glitches.cpp
  src/ui/menu_objects.cpp
  src/ui/menu_scene.cpp
  src/ui/ui_movies.cpp
  src/ui/natives_flow.cpp
  src/ui/natives_game.cpp
  src/ui/natives_sound.cpp
  src/ui/ui_system.cpp
  src/ui/port_draw.cpp
  src/ui/port_enhanced.cpp
  src/ui/port_gamepads.cpp
  src/ui/port_screens.cpp
  src/ui/port_settings.cpp
  src/video/movie_player.cpp
  src/tests/formattest.cpp
)
target_include_directories(gd10 PRIVATE src)
target_link_libraries(gd10 PRIVATE SDL3::SDL3)

# gd-engine-core (GDCORE-010, ../../gd-engine-core/docs/README.md): the shared
# render / audio / platform libraries, static, as gd-game-01 and gd-game-02
# link them. Linking it does not change the game: gd10 runs its own code
# ("local") unless a core backend is chosen (Enhanced > Performance, or the
# development aids GD10_GRAPHICS_BACKEND / GD10_AUDIO_BACKEND). The launcher
# module (GD10_MODULE, below) takes it through gd10's LINK_LIBRARIES, built in
# the launcher's layout (GDCORE_LAUNCHER_MODULE); the core lists the shared
# libraries and licences the packages must carry in <build>/gdcore-runtime.txt
# (gd-engine-launcher/scripts/build.sh ship_core_runtime).
option(GD10_MODULE "Also build gd10 as a Guitar Dream Universal Launcher module" OFF)
set(GDCORE_LAUNCHER_MODULE ${GD10_MODULE})
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../gd-engine-core ${CMAKE_CURRENT_BINARY_DIR}/gd-engine-core)
target_link_libraries(gd10 PRIVATE gdcore)
# The core's sdl3 and vulkan backends take SPIR-V. gd01 compiles its shaders
# at build time (gdcore_spirv_shaders); GH3 draws with the disc's own Xenos
# shaders, translated to GLSL when a material is loaded (src/render/
# xenos_glsl.cpp), so gd10 compiles them when a program is made
# (src/render/spirv_compile.cpp) with glslang, the release gd-engine-core pins
# for its gdcore_shaderc (its GLSL front end and SPIR-V back end only: no
# HLSL, no optimiser), as gd02 does. Without it (GD10_RUNTIME_SPIRV OFF) sdl3
# and vulkan fall back to local.
option(GD10_RUNTIME_SPIRV "Compile gd10's shaders to SPIR-V at run time (the sdl3 / vulkan graphics backends)" ON)
if(GD10_RUNTIME_SPIRV)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../../gd-engine-core/cmake/gdcore_deps.cmake)  # the pins, in this scope
  gdcore_fetch(GLSLANG gd10_glslang_src)
  set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
  set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
  set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
  set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
  set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
  set(ENABLE_CTEST OFF CACHE BOOL "" FORCE)
  set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
  set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
  add_subdirectory(${gd10_glslang_src} glslang EXCLUDE_FROM_ALL)
  foreach(t glslang MachineIndependent GenericCodeGen OSDependent SPIRV glslang-default-resource-limits)
    if(TARGET ${t})  # upstream code, unchanged: no warnings from it
      target_compile_options(${t} PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>)
    endif()
  endforeach()
  target_link_libraries(gd10 PRIVATE glslang::glslang glslang::glslang-default-resource-limits)
  if(TARGET glslang::SPIRV)
    target_link_libraries(gd10 PRIVATE glslang::SPIRV)
  endif()
  target_compile_definitions(gd10 PRIVATE GD10_RUNTIME_SPIRV=1)
  # Its licence, for the packages (the launcher's build.sh).
  configure_file(${gd10_glslang_src}/LICENSE.txt ${CMAKE_BINARY_DIR}/glslang-LICENSE.txt COPYONLY)
else()
  target_compile_definitions(gd10 PRIVATE GD10_RUNTIME_SPIRV=0)
endif()

if(NOT WIN32 AND NOT APPLE)
  target_link_libraries(gd10 PRIVATE m)
endif()

find_package(Threads REQUIRED)
target_link_libraries(gd10 PRIVATE Threads::Threads)

# Generic MIDI input (src/input/midi_in.cpp): RtMidi (third_party/rtmidi), desktop only.
if(NOT ANDROID)
  add_subdirectory(third_party/rtmidi)
  target_link_libraries(gd10 PRIVATE gd_rtmidi)
endif()

target_compile_definitions(gd10 PRIVATE GD10_VERSION="${PROJECT_VERSION}")

# GH3's videos (Bink, the video player comes with M3) and sounds (XMA,
# src/audio/xma_decoder.cpp): FFmpeg's libavformat, libavcodec
# and libavutil as shared libraries, the minimal LGPL build of
# build_common.sh's build_ffmpeg, installed in GD10_FFMPEG_DIR (on Android one
# build per ABI: <dir>/<ABI>/ffmpeg-prefix), or else the system's (pkg-config). The build
# copies the libraries next to the program (Linux: found through RPATH
# $ORIGIN; Windows: the DLLs beside the .exe); build_macos.sh's dylibbundler
# puts them in the .app, and the Android Gradle plugin into the APK. Without
# FFmpeg (GD10_VIDEO OFF) the videos are skipped with a log line and the
# sound effects are silent.
set(GD10_FFMPEG_DIR "" CACHE PATH "FFmpeg install prefix (include/, lib/) for the video player")
if(GD10_FFMPEG_DIR)
  option(GD10_VIDEO "Play GH3's videos and sounds with FFmpeg" ON)
else()
  option(GD10_VIDEO "Play GH3's videos and sounds with FFmpeg" OFF)
endif()
if(GD10_VIDEO)
  set(ffmpeg_dir "${GD10_FFMPEG_DIR}")
  if(ANDROID AND ffmpeg_dir)
    set(ffmpeg_dir "${ffmpeg_dir}/${ANDROID_ABI}/ffmpeg-prefix")
  endif()
  if(ffmpeg_dir)
    target_include_directories(gd10 SYSTEM PRIVATE "${ffmpeg_dir}/include")
    foreach(lib avformat avcodec avutil)
      find_library(GD10_FFMPEG_${lib} NAMES ${lib} PATHS "${ffmpeg_dir}/lib" NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
      if(NOT GD10_FFMPEG_${lib})
        message(FATAL_ERROR "GD10_FFMPEG_DIR: no ${lib} library in ${ffmpeg_dir}/lib")
      endif()
      add_library(gd10_ffmpeg_${lib} SHARED IMPORTED)
      if(WIN32)
        file(GLOB dll "${ffmpeg_dir}/bin/${lib}-*.dll")
        set_target_properties(gd10_ffmpeg_${lib} PROPERTIES IMPORTED_IMPLIB "${GD10_FFMPEG_${lib}}" IMPORTED_LOCATION "${dll}")
        add_custom_command(TARGET gd10 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${dll}" "$<TARGET_FILE_DIR:gd10>")
      else()
        set_target_properties(gd10_ffmpeg_${lib} PROPERTIES IMPORTED_LOCATION "${GD10_FFMPEG_${lib}}")
        if(NOT APPLE AND NOT ANDROID)
          # The file named by the library's SONAME (libavcodec.so.61), copied next to the program.
          file(GLOB sonames "${ffmpeg_dir}/lib/lib${lib}.so.[0-9]*")
          list(FILTER sonames INCLUDE REGEX "\\.so\\.[0-9]+$")
          add_custom_command(TARGET gd10 POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different ${sonames} "$<TARGET_FILE_DIR:gd10>")
        endif()
      endif()
      target_link_libraries(gd10 PRIVATE gd10_ffmpeg_${lib})
    endforeach()
    if(NOT APPLE AND NOT ANDROID AND NOT WIN32)
      set_target_properties(gd10 PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    endif()
  else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GD10_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil)
    target_link_libraries(gd10 PRIVATE PkgConfig::GD10_FFMPEG)
  endif()
  target_compile_definitions(gd10 PRIVATE GD10_VIDEO=1)
  message(STATUS "gd10: videos with FFmpeg (${ffmpeg_dir})")
else()
  target_compile_definitions(gd10 PRIVATE GD10_VIDEO=0)
  message(STATUS "gd10: no FFmpeg (GD10_VIDEO OFF): videos are skipped")
endif()

# The Steam Deck download (build_linux_steamdeck.sh): the Linux program with
# the handheld settings as its defaults (gd01's GD01_STEAM_DECK).
option(GD10_STEAM_DECK "Steam Deck build: handheld default settings" OFF)
if(GD10_STEAM_DECK)
  target_compile_definitions(gd10 PRIVATE GD10_STEAM_DECK=1)
endif()

if(MSVC)
  target_compile_options(gd10 PRIVATE /W4 /permissive-)
else()
  target_compile_options(gd10 PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 13)
  # GCC 12 (the Linux download's Ubuntu 22.04 compiler) warns wrongly about
  # `"text" + std::string` (GCC bug 105329); GCC 13 and later are fine.
  target_compile_options(gd10 PRIVATE -Wno-restrict)
endif()

if(MINGW)
  # One less set of DLLs to ship next to the executable.
  target_link_options(gd10 PRIVATE -static-libgcc -static-libstdc++ -static $<$<CONFIG:Release>:-s>)
endif()

if(ANDROID)
  target_link_libraries(gd10 PRIVATE android log)
endif()

# The icon (res/, made by ../tools/make_icons.py from the repo's res/icon_full.jpg):
# the PNG built into the program (window icon, first screen), the .exe's icon
# on Windows, the .app's on macOS. Android's launcher icons are in android/.
set(GD10_ICON_FILES gd10_icon gd10_icon_small)
set(GD10_ICON_SYMBOLS kGd04IconPng kGd04IconSmallPng)
foreach(file symbol IN ZIP_LISTS GD10_ICON_FILES GD10_ICON_SYMBOLS)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png -DOUT=${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
            -DNAME=${symbol} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    VERBATIM)
  target_sources(gd10 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp)
endforeach()
if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gd10.rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/res/gd10.ico\"\n")
  target_sources(gd10 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gd10.rc)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gd10.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/gd10.ico)
endif()
if(APPLE)
  target_sources(gd10 PRIVATE res/gd10.icns)
  set_source_files_properties(res/gd10.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

set_target_properties(gd10 PROPERTIES
  MACOSX_BUNDLE_ICON_FILE gd10.icns
  MACOSX_BUNDLE_BUNDLE_NAME "GD Engine 02"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.mbracali.gd10"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")

# The launcher module (GD10_MODULE=ON; gd-engine-launcher/README.md): gd10 also
# as a shared library, gd-game-10.so / gd-game-10.dll, that the Guitar Dream
# Universal Launcher loads into its own window. The same sources and settings
# as the program above, plus its entry points (src/platform/module_entry.cpp,
# module_explore.cpp: the launcher's model explorer) and the launcher's SDL
# shims (gd_module_shim.h, force-included). The
# program itself is unchanged. On Android the module is libgd-game-10.so in the
# launcher's APK (gd-engine-launcher/scripts/build.sh android); it uses the
# launcher's libSDL3.so, built from the same SDL3 source.
if(GD10_MODULE)
  set(GD10_HOST_SDK "${CMAKE_CURRENT_SOURCE_DIR}/../../gd-engine-launcher/sdk" CACHE PATH
      "The launcher's module SDK (gd_host.h, gd_module_shim.h/.cpp)")
  get_target_property(gd10_srcs gd10 SOURCES)
  list(FILTER gd10_srcs EXCLUDE REGEX "\\.(rc|icns)$")
  add_library(gd10_module SHARED ${gd10_srcs} src/platform/module_entry.cpp src/platform/module_sounds.cpp
              ${GD10_HOST_SDK}/gd_module_shim.cpp)
  foreach(prop INCLUDE_DIRECTORIES COMPILE_DEFINITIONS COMPILE_OPTIONS LINK_LIBRARIES LINK_OPTIONS)
    get_target_property(value gd10 ${prop})
    if(value)
      set_property(TARGET gd10_module PROPERTY ${prop} ${value})
    endif()
  endforeach()
  if(GD10_VIDEO AND ffmpeg_dir)
    target_include_directories(gd10_module SYSTEM PRIVATE "${ffmpeg_dir}/include")
  endif()
  target_include_directories(gd10_module PRIVATE ${GD10_HOST_SDK})
  target_compile_definitions(gd10_module PRIVATE GD10_MODULE=1)
  if(MSVC)
    target_compile_options(gd10_module PRIVATE "/FI${GD10_HOST_SDK}/gd_module_shim.h")
  else()
    target_compile_options(gd10_module PRIVATE "SHELL:-include \"${GD10_HOST_SDK}/gd_module_shim.h\"")
  endif()
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Unique symbols would keep the library loaded after the launcher unloads
    # it, and with it the game's globals of the last run.
    target_compile_options(gd10_module PRIVATE -fno-gnu-unique)
  endif()
  set_target_properties(gd10_module PROPERTIES OUTPUT_NAME gd-game-10 PREFIX ""
    CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  if(ANDROID)
    set_target_properties(gd10_module PROPERTIES PREFIX "lib")  # an APK packs lib*.so only
  endif()
  if(NOT WIN32 AND NOT APPLE)
    # FFmpeg next to the module; only the gd_game_* entry points exported.
    set_target_properties(gd10_module PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
    target_link_options(gd10_module PRIVATE -Wl,--exclude-libs,ALL)
  endif()
endif()

