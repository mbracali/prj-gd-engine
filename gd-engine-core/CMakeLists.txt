# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gd-engine-core: the libraries every game module can share (platform, inputs,
# audio, render, image, ui). See docs/README.md.
#
# A game adds this folder and links ONE target, `gdcore`:
#   add_subdirectory(<repo>/gd-engine-core ${CMAKE_CURRENT_BINARY_DIR}/gd-engine-core)
#   target_link_libraries(<game> PRIVATE gdcore)
# after SDL3 is available as the target SDL3::SDL3 (find_package or the SDL3
# source added with add_subdirectory, as the Android builds do).
#
# Each area is a folder with its own CMakeLists.txt defining `gdcore_<area>`;
# an area that is not there yet is simply skipped. Area owners write only
# their own folder (docs/README.md, "Adding an area or a backend").
#
# Options:
#   GDCORE_BUILD_TESTS       gdcore_selftest (default: ON when this folder is
#                            the top-level project, OFF inside a game)
#   GDCORE_LAUNCHER_MODULE   this copy is built into a launcher module
#                            (Android: the save folder follows the launcher's
#                            layout, as its module shim does)
#   GDCORE_DEPS_DIR          where the fetched third-party sources are kept
#                            (cmake/gdcore_deps.cmake)
cmake_minimum_required(VERSION 3.22)
project(gdcore VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(GDCORE_TOP_LEVEL ON)
else()
  set(GDCORE_TOP_LEVEL OFF)
endif()
option(GDCORE_BUILD_TESTS "Build gdcore_selftest" ${GDCORE_TOP_LEVEL})
option(GDCORE_LAUNCHER_MODULE "Built into a Guitar Dream Universal Launcher module" OFF)

if(GDCORE_TOP_LEVEL AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()
# Every core library is static and ends up in a game executable or a game
# module (a shared library): position-independent code everywhere, including
# the fetched libraries built below this folder.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(GDCORE_LAUNCHER_MODULE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # As the launcher's modules are compiled: a GNU unique symbol would keep the
  # module loaded after the launcher unloads it (modules/gd01/CMakeLists.txt).
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fno-gnu-unique>)
endif()

set(GDCORE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(gdcore_helpers)
include(gdcore_deps)

if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 REQUIRED CONFIG)
endif()

# The areas (the contract with the area owners: each defines gdcore_<area>).
set(GDCORE_AREAS platform inputs audio render image ui)
foreach(a ${GDCORE_AREAS})
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${a}/CMakeLists.txt)
    add_subdirectory(${a})
  endif()
endforeach()

# `gdcore`: what a game links; every area target that exists.
add_library(gdcore INTERFACE)
set(GDCORE_LINKED_AREAS "")
foreach(a ${GDCORE_AREAS})
  if(TARGET gdcore_${a})
    target_link_libraries(gdcore INTERFACE gdcore_${a})
    list(APPEND GDCORE_LINKED_AREAS ${a})
  endif()
endforeach()
message(STATUS "gd-engine-core ${PROJECT_VERSION}: areas ${GDCORE_LINKED_AREAS}")

# What the packages must carry for the core, listed for the packaging scripts
# in <build>/gdcore-runtime.txt: the shared libraries the backends load at
# run time (gdcore_add_runtime_library; none yet) and the licences of the
# third-party code (gdcore_add_license).
gdcore_write_runtime_manifest(${CMAKE_BINARY_DIR}/gdcore-runtime.txt)

if(GDCORE_BUILD_TESTS)
  gdcore_finish_selftest()
endif()
