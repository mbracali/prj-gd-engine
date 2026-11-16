# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_chd: CHD disc images (MAME's Compressed Hunks of Data: `chdman
# createdvd` / `createcd`) read as the plain image they hold (GDUL-030).
# libchdr (BSD-3, pinned in gdcore_deps.cmake) with the decoders it bundles
# (lzma, miniz, zstd, dr_flac), compiled as one translation unit whose every
# global symbol carries a gdchd_ prefix (platform/src/chd_libchdr_names.h):
# KTX links its own zstd into the same programs, and two copies of ZSTD_*
# would not link.
#
# gdcore_platform links it (RandomAccessFile::open reads a .chd as its
# image), and so does the launcher, which has no gdcore (its disc probe):
#   include(<repo>/gd-engine-core/cmake/gdcore_chd.cmake)
#   target_link_libraries(<target> PRIVATE gdcore_chd)
# The interface is gd/core/platform/chd.h.
include_guard(GLOBAL)
get_filename_component(GDCORE_CHD_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT COMMAND gdcore_fetch)
  include(${CMAKE_CURRENT_LIST_DIR}/gdcore_deps.cmake)
endif()
gdcore_fetch(LIBCHDR gdcore_libchdr_src)

add_library(gdcore_chd STATIC
  ${GDCORE_CHD_ROOT}/platform/src/chd_libchdr.c
  ${GDCORE_CHD_ROOT}/platform/src/chd_image.cpp)
target_compile_features(gdcore_chd PUBLIC cxx_std_17)
set_target_properties(gdcore_chd PROPERTIES POSITION_INDEPENDENT_CODE ON
  CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(gdcore_chd PUBLIC ${GDCORE_CHD_ROOT}/platform/include)
target_include_directories(gdcore_chd PRIVATE ${GDCORE_CHD_ROOT}/platform/src)
target_include_directories(gdcore_chd SYSTEM PRIVATE ${gdcore_libchdr_src}/include)
target_compile_definitions(gdcore_chd PRIVATE
  GDCORE_LIBCHDR_DIR=1
  # Disc data only: the sectors' user data, no subcode; each hunk's CRC is
  # checked (a damaged CHD fails a read instead of giving wrong bytes).
  WANT_RAW_DATA_SECTOR=1 WANT_SUBCODE=0 VERIFY_BLOCK_CRC=1 LOWRAM_TARGET=0)
# The unity file includes libchdr's sources by path.
set_source_files_properties(${GDCORE_CHD_ROOT}/platform/src/chd_libchdr.c PROPERTIES
  COMPILE_OPTIONS "$<IF:$<C_COMPILER_ID:MSVC>,/w,-w>"
  INCLUDE_DIRECTORIES "${gdcore_libchdr_src};${gdcore_libchdr_src}/deps/lzma-26.02/include;${gdcore_libchdr_src}/deps/zstd-1.5.7;${gdcore_libchdr_src}/deps/miniz-3.1.2")
if(NOT MSVC)
  set_source_files_properties(${GDCORE_CHD_ROOT}/platform/src/chd_image.cpp PROPERTIES
    COMPILE_OPTIONS "-Wall;-Wextra;-Wpedantic")
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ANDROID)
  find_package(Threads)
  if(Threads_FOUND)
    target_link_libraries(gdcore_chd PUBLIC Threads::Threads)
  endif()
endif()
if(COMMAND gdcore_add_license)
  gdcore_add_license(${gdcore_libchdr_src}/LICENSE.txt libchdr-LICENSE.txt)
  gdcore_add_license(${GDCORE_CHD_ROOT}/third_party/libchdr/zstd-LICENSE.txt libchdr-zstd-LICENSE.txt)
endif()
