# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# The Guitar Dream Universal Launcher (gdul): C++17, SDL3, OpenGL 3.3 (OpenGL
# ES 3.0 on Android). Windows, Linux, a macOS .app, Android's libmain.so. The
# game modules are built separately (scripts/build.sh): gd-game-02 by its own
# CMakeLists.txt (GD02_MODULE=ON), gd-game-01 by modules/gd01/.
cmake_minimum_required(VERSION 3.24)

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/launcher-info.yaml" gdul_version_line REGEX "^launcher_version:")
string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" GDUL_VERSION "${gdul_version_line}")
project(gdul VERSION ${GDUL_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(ANDROID)
  # Android (scripts/build.sh android, android/): SDL3 built from source next
  # to the launcher as libSDL3.so; the launcher is libmain.so, loaded by the
  # Java activity (android/app/src/main/java/io/github/mbracali/gdul/GdulActivity.java).
  set(GDUL_SDL3_SOURCE "" CACHE PATH "SDL3 source tree (Android)")
  set(SDL_SHARED ON CACHE BOOL "" FORCE)
  set(SDL_STATIC OFF CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  add_subdirectory(${GDUL_SDL3_SOURCE} SDL3 EXCLUDE_FROM_ALL)
  add_library(gdul SHARED)
else()
  find_package(SDL3 REQUIRED CONFIG)
  add_executable(gdul WIN32 MACOSX_BUNDLE)
endif()
target_sources(gdul PRIVATE
  src/main.cpp
  src/launcher.cpp
  src/gfx.cpp
  src/catalog.cpp
  src/iso_probe.cpp
  src/module_host.cpp
  src/explorer.cpp
  src/action_log.cpp
  src/platform.cpp
  src/ui_sounds.cpp
  src/yaml.cpp
)
target_include_directories(gdul PRIVATE src sdk)
target_link_libraries(gdul PRIVATE SDL3::SDL3 ${CMAKE_DL_LIBS})
target_compile_definitions(gdul PRIVATE GDUL_VERSION="${PROJECT_VERSION}")
if(ANDROID)
  set_target_properties(gdul PROPERTIES OUTPUT_NAME main)
  target_link_libraries(gdul PRIVATE android log)
else()
  set_target_properties(gdul PROPERTIES OUTPUT_NAME "Guitar Dream Universal Launcher")
endif()

# stb_image and stb_truetype (third_party/stb), upstream code: its warnings are not ours.
add_library(gdul_stb STATIC src/stb_impl.c)
target_include_directories(gdul_stb SYSTEM PUBLIC third_party/stb)
target_compile_options(gdul_stb PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
target_link_libraries(gdul PRIVATE gdul_stb)
if(NOT WIN32)
  target_link_libraries(gdul PRIVATE m)
endif()

if(MSVC)
  target_compile_options(gdul PRIVATE /W4 /permissive-)
else()
  target_compile_options(gdul PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()
if(MINGW)
  target_link_options(gdul PRIVATE -static-libgcc -static-libstdc++ -static $<$<CONFIG:Release>:-s>)
elseif(APPLE)
  # "Guitar Dream Universal Launcher.app": SDL3 in Contents/Frameworks (put
  # there by scripts/build.sh macos), the game modules in
  # Contents/Resources/gd-engine-games/ (SDL_GetBasePath is Contents/Resources/).
  target_sources(gdul PRIVATE res/gdul.icns)
  set_source_files_properties(res/gdul.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
  set_target_properties(gdul PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Guitar Dream Universal Launcher"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.mbracali.gdul"
    MACOSX_BUNDLE_ICON_FILE gdul.icns
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "@executable_path/../Frameworks")
elseif(NOT ANDROID)
  # libSDL3.so.0 next to the program.
  set_target_properties(gdul PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN")
endif()

# Built-in resources (res/): the icon, the backdrop and the fonts.
function(gdul_embed file symbol)
  get_filename_component(name "${file}" NAME_WE)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/embed_${symbol}.cpp)
  add_custom_command(OUTPUT ${out}
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/${file} -DOUT=${out} -DNAME=${symbol}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${file} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed_file.cmake
    VERBATIM)
  target_sources(gdul PRIVATE ${out})
endfunction()
gdul_embed(res/gdul_icon.png kGdulIconPng)
gdul_embed(res/gdul_backdrop.jpg kGdulBackdropJpg)
gdul_embed(res/fonts/MetalMania-Regular.ttf kFontMetalMania)
gdul_embed(res/fonts/BarlowCondensed-Regular.ttf kFontBarlowRegular)
gdul_embed(res/fonts/BarlowCondensed-SemiBold.ttf kFontBarlowSemiBold)

if(WIN32)
  enable_language(RC)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/gdul.rc "1 ICON \"${CMAKE_CURRENT_SOURCE_DIR}/res/gdul.ico\"\n")
  target_sources(gdul PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/gdul.rc)
  set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/gdul.rc PROPERTIES OBJECT_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/res/gdul.ico)
endif()
