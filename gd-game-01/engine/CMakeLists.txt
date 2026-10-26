cmake_minimum_required(VERSION 3.24)
project(gd01 VERSION 1.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(ANDROID)
  # Android (engine/build_android.sh): SDL3 built from source next to the game
  # as libSDL3.so; the game is libgd01.so, loaded by the Java activity
  # (android/app/src/main/java/io/github/mbracali/gd01/GD01Activity.java).
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  add_subdirectory(${GD01_SDL3_SOURCE} SDL3 EXCLUDE_FROM_ALL)
  add_library(gd01 SHARED)
else()
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(gd01 WIN32 MACOSX_BUNDLE)
endif()

target_sources(gd01 PRIVATE
  src/main.cpp
  src/app.cpp
  src/audio/mixer.cpp
  src/audio/adpcm.cpp
  src/audio/spu.cpp
  src/audio/sound_bank.cpp
  src/audio/synth.cpp
  src/audio/crowd_audio.cpp
  src/data/bitmap.cpp
  src/data/dta.cpp
  src/data/game_data.cpp
  src/data/inflate.cpp
  src/data/iso9660.cpp
  src/data/midi.cpp
  src/data/vgs.cpp
  src/game/chart.cpp
  src/game/player.cpp
  src/game/rules.cpp
  src/game/song_db.cpp
  src/input/input.cpp
  src/render/crowd_renderer.cpp
  src/render/gl.cpp
  src/render/highway.cpp
  src/render/post_fx.cpp
  src/render/vk_upscaler.cpp
  src/render/renderer.cpp
  src/render/scene_renderer.cpp
  src/render/image_enhance.cpp
  src/render/text_layout.cpp
  src/render/track_renderer.cpp
  src/render/venue_renderer.cpp
  src/game/venue_camera.cpp
  src/game/venue_script.cpp
  src/script/script.cpp
  src/scene/anim.cpp
  src/scene/particles.cpp
  src/video/mpeg2.cpp
  src/video/pss.cpp
  src/ui/panel.cpp
  src/ui/select_panels.cpp
  src/ui/tutorial_panel.cpp
  src/ui/game_panels.cpp
  src/ui/career_objects.cpp
  src/game/guitar_anim.cpp
  src/game/guitar_walk.cpp
  src/game/campaign.cpp
  src/data/json.cpp
  src/ui/ui_resources.cpp
  src/ui/pc_strings.cpp
  src/ui/gamepad_panel.cpp
  src/ui/ui_system.cpp
  src/platform/gpu_select.cpp
  src/platform/paths.cpp
  src/platform/app_icon.cpp
  src/scene/character.cpp
  src/scene/clip.cpp
  src/scene/scene.cpp
)
target_include_directories(gd01 PRIVATE src)
target_link_libraries(gd01 PRIVATE SDL3::SDL3)

# AI upscaling (render/vk_upscaler.cpp): Vulkan loaded at run time through
# volk, AMD FidelityFX FSR 3.1 (third_party/fidelityfx) and, on Linux x86-64,
# NVIDIA DLSS (third_party/dlss: the NGX static library and the DLSS model
# library, copied next to the executable).
if(ANDROID)
  # No Vulkan upscalers on Android (FSR 3.1 / DLSS 4 need the desktop GL
  # interop): the stub reports them unavailable.
  get_target_property(GD01_SRC gd01 SOURCES)
  list(REMOVE_ITEM GD01_SRC src/render/vk_upscaler.cpp)
  set_target_properties(gd01 PROPERTIES SOURCES "${GD01_SRC}")
  target_sources(gd01 PRIVATE src/render/vk_upscaler_stub.cpp src/platform/android_data.cpp)
  target_compile_definitions(gd01 PRIVATE GD01_DLSS=0)
  target_link_libraries(gd01 PRIVATE android log)
else()
add_subdirectory(third_party/fidelityfx)
target_include_directories(gd01 SYSTEM PRIVATE third_party/vulkan third_party/volk third_party/fidelityfx/include)
target_link_libraries(gd01 PRIVATE ffx_fsr3 volk ${CMAKE_DL_LIBS})
set(GD01_DLSS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/dlss)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND EXISTS ${GD01_DLSS_DIR}/lib/Linux_x86_64/libnvsdk_ngx.a)
  target_compile_definitions(gd01 PRIVATE GD01_DLSS=1)
  target_include_directories(gd01 SYSTEM PRIVATE ${GD01_DLSS_DIR}/include)
  target_link_libraries(gd01 PRIVATE ${GD01_DLSS_DIR}/lib/Linux_x86_64/libnvsdk_ngx.a)
  file(GLOB GD01_DLSS_MODEL ${GD01_DLSS_DIR}/lib/Linux_x86_64/libnvidia-ngx-dlss.so.*)
  add_custom_command(TARGET gd01 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GD01_DLSS_MODEL} $<TARGET_FILE_DIR:gd01>)
elseif(MSVC AND EXISTS ${GD01_DLSS_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib)
  # Windows: NVIDIA ships the NGX library for MSVC only (not in the MinGW
  # build); add lib/Windows_x86_64 from the DLSS SDK to third_party/dlss.
  target_compile_definitions(gd01 PRIVATE GD01_DLSS=1)
  target_include_directories(gd01 SYSTEM PRIVATE ${GD01_DLSS_DIR}/include)
  target_link_libraries(gd01 PRIVATE ${GD01_DLSS_DIR}/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib)
  add_custom_command(TARGET gd01 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${GD01_DLSS_DIR}/lib/Windows_x86_64/rel/nvngx_dlss.dll $<TARGET_FILE_DIR:gd01>)
else()
  target_compile_definitions(gd01 PRIVATE GD01_DLSS=0)
endif()
endif()  # NOT ANDROID

target_compile_definitions(gd01 PRIVATE GD01_VERSION="${PROJECT_VERSION}")

# The Steam Deck download (build_linux_steamdeck.sh): the Linux program with
# the handheld settings as its defaults (app.cpp profileValues).
option(GD01_STEAM_DECK "Steam Deck build: handheld default settings" OFF)
if(GD01_STEAM_DECK)
  target_compile_definitions(gd01 PRIVATE GD01_STEAM_DECK=1)
endif()

if(MSVC)
  target_compile_options(gd01 PRIVATE /W4 /permissive-)
else()
  target_compile_options(gd01 PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(MINGW)
  # One less set of DLLs to ship next to the executable.
  target_link_options(gd01 PRIVATE -static-libgcc -static-libstdc++ -static $<$<CONFIG:Release>:-s>)
endif()

# The icon (res/, made by tools/make_icons.py from the repo's res/icon_full.jpg):
# the PNG built into the program (window icon, first screen), the .exe's icon
# on Windows, the .app's on macOS. Android's launcher icons are in android/.
set(GD01_ICON_FILES gd01_icon gd01_icon_small)
set(GD01_ICON_SYMBOLS kGd01IconPng kGd01IconSmallPng)
foreach(file symbol IN ZIP_LISTS GD01_ICON_FILES GD01_ICON_SYMBOLS)
  add_custom_command(OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png -DOUT=${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp
            -DNAME=${symbol} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/${file}.png ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    VERBATIM)
  target_sources(gd01 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${file}_data.cpp)
endforeach()
if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gd01.rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/res/gd01.ico\"\n")
  target_sources(gd01 PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gd01.rc)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gd01.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/gd01.ico)
endif()
if(APPLE)
  target_sources(gd01 PRIVATE res/gd01.icns)
  set_source_files_properties(res/gd01.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
endif()

set_target_properties(gd01 PROPERTIES
  MACOSX_BUNDLE_ICON_FILE gd01.icns
  MACOSX_BUNDLE_BUNDLE_NAME "GD Engine 01"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.mbracali.gd01"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
