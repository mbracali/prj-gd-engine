# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_audio: gd::core::audio::Output and its backends (miniaudio, OpenAL
# Soft, FAudio). See ../docs/audio.md. Added by the top-level
# gd-engine-core/CMakeLists.txt (or any project: add_subdirectory this folder
# and link gdcore_audio). Needs the target SDL3::SDL3 or find_package(SDL3).
#
# Outputs besides the static library (in GDCORE_AUDIO_RUNTIME_DIR, default
# the top build folder, next to the game's executable or module):
#   - the OpenAL Soft shared library (LGPL: built as its own project, never
#     linked; the backend loads it at run time): its file name is the cache
#     entry GDCORE_OPENAL_LIBRARY_NAME, for the packaging scripts;
#   - gdcore-licenses/: the licence texts of the three libraries, to ship.
cmake_minimum_required(VERSION 3.24)

if(NOT CMAKE_C_COMPILER_LOADED)
  enable_language(C)  # miniaudio and FAudio are C
endif()

option(GDCORE_AUDIO_MINIAUDIO "gdcore_audio: the miniaudio backend" ON)
option(GDCORE_AUDIO_OPENAL "gdcore_audio: the OpenAL backend (builds OpenAL Soft as a shared library)" ON)
option(GDCORE_AUDIO_FAUDIO "gdcore_audio: the FAudio backend" ON)
option(GDCORE_AUDIO_SELFTEST "gdcore_audio: build gdcore_audio_selftest" ON)
set(GDCORE_AUDIO_RUNTIME_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Where gdcore_audio puts the OpenAL Soft library and the licences")

# The pinned third-party sources (exact releases, at least two weeks old,
# checked by hash). miniaudio is vendored: ../third_party/miniaudio.
set(GDCORE_OPENAL_SOFT_VERSION 1.25.2)  # 2026-05-12
set(GDCORE_OPENAL_SOFT_URL "https://github.com/kcat/openal-soft/archive/refs/tags/${GDCORE_OPENAL_SOFT_VERSION}.tar.gz")
set(GDCORE_OPENAL_SOFT_SHA256 fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894)
set(GDCORE_FAUDIO_VERSION 26.10)  # 2026-10-01
set(GDCORE_FAUDIO_URL "https://github.com/FNA-XNA/FAudio/archive/refs/tags/${GDCORE_FAUDIO_VERSION}.tar.gz")
set(GDCORE_FAUDIO_SHA256 c508f297ec8d065b456b2d9d74bab89ffc84ac3afbcb428bd54e40f09f8b901e)

if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 REQUIRED CONFIG)
endif()

include(FetchContent)
set(GDCORE_AUDIO_LICENSES "${GDCORE_AUDIO_RUNTIME_DIR}/gdcore-licenses")

add_library(gdcore_audio STATIC src/output.cpp src/input.cpp src/sdl_input.cpp)
target_include_directories(gdcore_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(gdcore_audio PUBLIC cxx_std_17)
target_compile_definitions(gdcore_audio PUBLIC GDCORE_HAS_AUDIO=1)
target_link_libraries(gdcore_audio PRIVATE SDL3::SDL3 ${CMAKE_DL_LIBS})
set_target_properties(gdcore_audio PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(gdcore_audio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4>)
else()
  target_compile_options(gdcore_audio PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic>)
endif()
find_package(Threads REQUIRED)
target_link_libraries(gdcore_audio PRIVATE Threads::Threads)

# ---- miniaudio (vendored single header, public domain / MIT-0) ----
if(GDCORE_AUDIO_MINIAUDIO)
  set(GDCORE_MINIAUDIO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/miniaudio)
  target_sources(gdcore_audio PRIVATE src/miniaudio_output.cpp src/miniaudio_input.cpp src/miniaudio_impl.c)
  target_include_directories(gdcore_audio PRIVATE ${GDCORE_MINIAUDIO_DIR})
  # Device IO and the low-level API only (decoders, node graph and engine
  # off until a game needs them).
  target_compile_definitions(gdcore_audio PRIVATE GDCORE_AUDIO_HAS_MINIAUDIO=1 MA_NO_DECODING MA_NO_ENCODING
    MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE MA_NO_GENERATION)
  if(UNIX AND NOT APPLE)
    target_link_libraries(gdcore_audio PRIVATE m)
  endif()
  if(APPLE)
    target_link_libraries(gdcore_audio PRIVATE "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
  endif()
  configure_file(${GDCORE_MINIAUDIO_DIR}/LICENSE ${GDCORE_AUDIO_LICENSES}/miniaudio-LICENSE.txt COPYONLY)
endif()

# ---- FAudio (zlib): fetched, built here as a static library on SDL3 ----
if(GDCORE_AUDIO_FAUDIO)
  # SOURCE_SUBDIR with no CMakeLists.txt: only download; FAudio's own
  # CMakeLists.txt (shared by default, install rules) is not used.
  FetchContent_Declare(gdcore_faudio_src URL ${GDCORE_FAUDIO_URL} URL_HASH SHA256=${GDCORE_FAUDIO_SHA256}
    SOURCE_SUBDIR gdcore-download-only)
  FetchContent_MakeAvailable(gdcore_faudio_src)
  set(fa ${gdcore_faudio_src_SOURCE_DIR})
  add_library(gdcore_faudio STATIC
    ${fa}/src/F3DAudio.c ${fa}/src/FACT3D.c ${fa}/src/FACT.c ${fa}/src/FACT_internal.c ${fa}/src/FAPOBase.c
    ${fa}/src/FAPOFX.c ${fa}/src/FAPOFX_echo.c ${fa}/src/FAPOFX_eq.c ${fa}/src/FAPOFX_masteringlimiter.c
    ${fa}/src/FAPOFX_reverb.c ${fa}/src/FAudio.c ${fa}/src/FAudioFX_reverb.c ${fa}/src/FAudioFX_collector.c
    ${fa}/src/FAudioFX_volumemeter.c ${fa}/src/FAudio_internal.c ${fa}/src/FAudio_internal_simd.c
    ${fa}/src/FAudio_operationset.c ${fa}/src/FAudio_platform_sdl3.c)
  target_include_directories(gdcore_faudio SYSTEM PUBLIC ${fa}/include PRIVATE ${fa}/src)
  target_compile_definitions(gdcore_faudio PUBLIC FAUDIO_SDL3_PLATFORM PRIVATE DISABLE_XNASONG
    $<$<NOT:$<CONFIG:Debug>>:FAUDIO_DISABLE_DEBUGCONFIGURATION>)
  target_link_libraries(gdcore_faudio PUBLIC SDL3::SDL3)
  set_target_properties(gdcore_faudio PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
  if(NOT MSVC)
    target_compile_options(gdcore_faudio PRIVATE -w)  # third-party code: its warnings are not ours
  endif()
  target_sources(gdcore_audio PRIVATE src/faudio_output.cpp)
  target_compile_definitions(gdcore_audio PRIVATE GDCORE_AUDIO_HAS_FAUDIO=1)
  target_link_libraries(gdcore_audio PRIVATE gdcore_faudio)
  configure_file(${fa}/LICENSE ${GDCORE_AUDIO_LICENSES}/FAudio-LICENSE.txt COPYONLY)
endif()

# ---- OpenAL Soft (LGPL-2.1+): its own shared library, built as a separate
# CMake project with this build's toolchain, copied next to the game. The core
# only loads it at run time (src/openal_output.cpp). ----
if(GDCORE_AUDIO_OPENAL)
  target_sources(gdcore_audio PRIVATE src/openal_output.cpp)
  target_compile_definitions(gdcore_audio PRIVATE GDCORE_AUDIO_HAS_OPENAL=1)

  FetchContent_Declare(gdcore_openal_src URL ${GDCORE_OPENAL_SOFT_URL} URL_HASH SHA256=${GDCORE_OPENAL_SOFT_SHA256}
    SOURCE_SUBDIR gdcore-download-only)
  FetchContent_MakeAvailable(gdcore_openal_src)
  set(oal ${gdcore_openal_src_SOURCE_DIR})

  # The file the backend looks for (src/openal_output.cpp kLibNames) and the
  # one OpenAL Soft's build writes (the real file behind its symlinks).
  if(WIN32)
    set(GDCORE_OPENAL_LIBRARY_NAME OpenAL32.dll)
    set(oal_built OpenAL32.dll)
  elseif(APPLE)
    set(GDCORE_OPENAL_LIBRARY_NAME libopenal.1.dylib)
    set(oal_built libopenal.${GDCORE_OPENAL_SOFT_VERSION}.dylib)
  elseif(ANDROID)
    set(GDCORE_OPENAL_LIBRARY_NAME libopenal.so)
    set(oal_built libopenal.so)
  else()
    set(GDCORE_OPENAL_LIBRARY_NAME libopenal.so.1)
    set(oal_built libopenal.so.${GDCORE_OPENAL_SOFT_VERSION})
  endif()
  set(GDCORE_OPENAL_LIBRARY_NAME ${GDCORE_OPENAL_LIBRARY_NAME} CACHE INTERNAL "OpenAL Soft's file in GDCORE_AUDIO_RUNTIME_DIR")

  set(oal_args
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DLIBTYPE=SHARED
    -DALSOFT_UTILS=OFF -DALSOFT_NO_CONFIG_UTIL=ON -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF
    -DALSOFT_INSTALL=OFF -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=OFF
    -DALSOFT_INSTALL_AMBDEC_PRESETS=OFF -DALSOFT_INSTALL_EXAMPLES=OFF -DALSOFT_INSTALL_UTILS=OFF
    -DALSOFT_UPDATE_BUILD_VERSION=OFF -DALSOFT_ENABLE_MODULES=OFF
    # Its own output drivers only (never SDL: the game's SDL is not its to load).
    -DALSOFT_BACKEND_SDL2=OFF -DALSOFT_BACKEND_SDL3=OFF)
  if(NOT MSVC)
    list(APPEND oal_args -DCMAKE_C_FLAGS=-w -DCMAKE_CXX_FLAGS=-w)  # third-party code: its warnings are not ours
  endif()
  # The GCC runtimes inside the library where the toolchain has them static
  # (MinGW, the Steam Runtime; Fedora's GCC has no static libstdc++: the
  # library then needs the system's, as the game module does).
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    include(CheckLinkerFlag)
    check_linker_flag(CXX -static-libstdc++ GDCORE_HAVE_STATIC_LIBSTDCXX)
    if(GDCORE_HAVE_STATIC_LIBSTDCXX)
      list(APPEND oal_args -DALSOFT_STATIC_LIBGCC=ON -DALSOFT_STATIC_STDCXX=ON)
    endif()
  endif()
  if(MINGW)
    list(APPEND oal_args -DALSOFT_STATIC_WINPTHREAD=ON)
  endif()
  if(CMAKE_TOOLCHAIN_FILE)
    list(APPEND oal_args -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE})
  else()
    list(APPEND oal_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
  endif()
  if(ANDROID)
    # The STL statically inside the library (as the game modules), and the
    # 16 KB pages Android 15 asks for.
    list(APPEND oal_args -DANDROID_ABI=${ANDROID_ABI} -DANDROID_PLATFORM=${ANDROID_PLATFORM} -DANDROID_STL=c++_static
      "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,-z,max-page-size=16384")
  endif()
  if(APPLE)
    list(APPEND oal_args "-DCMAKE_OSX_ARCHITECTURES=${CMAKE_OSX_ARCHITECTURES}"
      -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
  endif()

  include(ExternalProject)
  set(oal_bin ${CMAKE_CURRENT_BINARY_DIR}/openal-soft-build)
  ExternalProject_Add(gdcore_openal_build
    SOURCE_DIR ${oal}
    BINARY_DIR ${oal_bin}
    CMAKE_ARGS ${oal_args}
    BUILD_COMMAND ${CMAKE_COMMAND} --build ${oal_bin} --config Release --target OpenAL
    BUILD_BYPRODUCTS ${oal_bin}/${oal_built}
    INSTALL_COMMAND ""
    UPDATE_COMMAND ""
    DOWNLOAD_COMMAND "")
  add_custom_command(OUTPUT ${GDCORE_AUDIO_RUNTIME_DIR}/${GDCORE_OPENAL_LIBRARY_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy ${oal_bin}/${oal_built} ${GDCORE_AUDIO_RUNTIME_DIR}/${GDCORE_OPENAL_LIBRARY_NAME}
    DEPENDS gdcore_openal_build ${oal_bin}/${oal_built}
    COMMENT "gdcore_audio: OpenAL Soft ${GDCORE_OPENAL_SOFT_VERSION} -> ${GDCORE_OPENAL_LIBRARY_NAME}"
    VERBATIM)
  add_custom_target(gdcore_openal ALL DEPENDS ${GDCORE_AUDIO_RUNTIME_DIR}/${GDCORE_OPENAL_LIBRARY_NAME})
  # Whatever links gdcore_audio gets the library built and in place first.
  add_dependencies(gdcore_audio gdcore_openal)

  configure_file(${oal}/COPYING ${GDCORE_AUDIO_LICENSES}/OpenAL-Soft-COPYING.txt COPYONLY)
  configure_file(${oal}/BSD-3Clause ${GDCORE_AUDIO_LICENSES}/OpenAL-Soft-BSD-3Clause.txt COPYONLY)
  configure_file(${oal}/LICENSE-pffft ${GDCORE_AUDIO_LICENSES}/OpenAL-Soft-LICENSE-pffft.txt COPYONLY)
  file(GLOB oal_fmt_dir LIST_DIRECTORIES true ${oal}/fmt-*)
  if(oal_fmt_dir)
    configure_file(${oal_fmt_dir}/LICENSE ${GDCORE_AUDIO_LICENSES}/OpenAL-Soft-fmt-LICENSE.txt COPYONLY)
  endif()
  file(WRITE ${GDCORE_AUDIO_LICENSES}/OpenAL-Soft-NOTICE.txt
"${GDCORE_OPENAL_LIBRARY_NAME} is OpenAL Soft ${GDCORE_OPENAL_SOFT_VERSION} (https://openal-soft.org), unmodified,
licensed under the GNU Lesser General Public License version 2 or later (OpenAL-Soft-COPYING.txt;
parts under OpenAL-Soft-BSD-3Clause.txt, OpenAL-Soft-LICENSE-pffft.txt, OpenAL-Soft-fmt-LICENSE.txt).
Source: ${GDCORE_OPENAL_SOFT_URL}
(SHA-256 ${GDCORE_OPENAL_SOFT_SHA256}).
The game loads it at run time only: you may replace this file with your own build of OpenAL Soft
(the same file name, AL_SOFT_callback_buffer needed).
")
endif()

# ---- packaging: the core's runtime manifest (gdcore_helpers.cmake; build.sh's
# ship_core_runtime copies each library next to the module and each licence to
# licenses/, verify_bundle.sh checks them) ----
if(COMMAND gdcore_add_runtime_library)
  if(GDCORE_AUDIO_OPENAL AND GDCORE_OPENAL_LIBRARY_NAME)
    gdcore_add_runtime_library(${GDCORE_AUDIO_RUNTIME_DIR}/${GDCORE_OPENAL_LIBRARY_NAME})
  endif()
  file(GLOB gdcore_audio_license_files ${GDCORE_AUDIO_LICENSES}/*)
  foreach(f ${gdcore_audio_license_files})
    get_filename_component(n ${f} NAME)
    gdcore_add_license(${f} ${n})
  endforeach()
endif()

# ---- the headless self-test (every backend on its null / dummy device) ----
if(GDCORE_AUDIO_SELFTEST AND NOT ANDROID)
  add_executable(gdcore_audio_selftest tests/audio_selftest.cpp)
  target_link_libraries(gdcore_audio_selftest PRIVATE gdcore_audio SDL3::SDL3)
  set_target_properties(gdcore_audio_selftest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${GDCORE_AUDIO_RUNTIME_DIR})
  set_target_properties(gdcore_audio_selftest PROPERTIES EXCLUDE_FROM_ALL ON)
  # The capture backends (gd/core/audio/input.h) on their null / dummy devices.
  add_executable(gdcore_audio_input_selftest tests/input_selftest.cpp)
  target_link_libraries(gdcore_audio_input_selftest PRIVATE gdcore_audio SDL3::SDL3)
  set_target_properties(gdcore_audio_input_selftest PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${GDCORE_AUDIO_RUNTIME_DIR})
  set_target_properties(gdcore_audio_input_selftest PROPERTIES EXCLUDE_FROM_ALL ON)
endif()
