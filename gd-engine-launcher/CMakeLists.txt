# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# The Guitar Dream Universal Launcher (gdul): C++17, SDL3, OpenGL 3.3. The
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

find_package(SDL3 REQUIRED CONFIG)
add_executable(gdul WIN32
  src/main.cpp
  src/launcher.cpp
  src/gfx.cpp
  src/catalog.cpp
  src/iso_probe.cpp
  src/module_host.cpp
  src/action_log.cpp
  src/platform.cpp
  src/yaml.cpp
)
target_include_directories(gdul PRIVATE src sdk)
target_link_libraries(gdul PRIVATE SDL3::SDL3 ${CMAKE_DL_LIBS})
target_compile_definitions(gdul PRIVATE GDUL_VERSION="${PROJECT_VERSION}")
set_target_properties(gdul PROPERTIES OUTPUT_NAME "Guitar Dream Universal Launcher")

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
elseif(NOT APPLE)
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
