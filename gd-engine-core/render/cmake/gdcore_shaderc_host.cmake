# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# Configures (the first time) and builds the host tool gdcore_shaderc, under
# a file lock (several build folders, a container and the host may share the
# folder). Run by gdcore_shaders.cmake at configure time and before every
# build (a no-op when it is up to date):
#
#   cmake -DSRC=<render/tools/shaderc> -DBIN=<host build folder> -DRENDER=<render>
#         -DGLSLANG=<dir> -DSPIRV_TOOLS=<dir> -DSPIRV_HEADERS=<dir> -DVALIDATE=ON|OFF
#         -DGENERATOR=<cmake generator> [-DMAKE_PROGRAM=<ninja>] [-DHOST_CC=..] [-DHOST_CXX=..]
#         -P gdcore_shaderc_host.cmake
cmake_minimum_required(VERSION 3.22)
file(MAKE_DIRECTORY "${BIN}")
file(LOCK "${BIN}.lock" TIMEOUT 3600 RESULT_VARIABLE locked)
if(NOT locked EQUAL 0)
  message(FATAL_ERROR "gdcore_shaderc: cannot lock ${BIN}.lock: ${locked}")
endif()
if(NOT EXISTS "${BIN}/CMakeCache.txt")
  set(args -S "${SRC}" -B "${BIN}" -G "${GENERATOR}" -DCMAKE_BUILD_TYPE=Release
      -DGLSLANG_SOURCE=${GLSLANG} -DSPIRV_TOOLS_SOURCE=${SPIRV_TOOLS} -DSPIRV_HEADERS_SOURCE=${SPIRV_HEADERS}
      -DGDCORE_RENDER_DIR=${RENDER} -DGDCORE_SHADERC_VALIDATE=${VALIDATE} -Wno-dev)
  if(MAKE_PROGRAM)
    list(APPEND args -DCMAKE_MAKE_PROGRAM=${MAKE_PROGRAM})
  endif()
  if(HOST_CC)
    list(APPEND args -DCMAKE_C_COMPILER=${HOST_CC})
  endif()
  if(HOST_CXX)
    list(APPEND args -DCMAKE_CXX_COMPILER=${HOST_CXX})
  endif()
  # The game's cross toolchain must not leak in (CMake reads these from the
  # environment): this is a program for the build machine.
  execute_process(COMMAND ${CMAKE_COMMAND} -E env --unset=CMAKE_TOOLCHAIN_FILE --unset=CMAKE_GENERATOR
                          --unset=CMAKE_BUILD_TYPE --unset=CMAKE_PREFIX_PATH --unset=CMAKE_OSX_ARCHITECTURES
                          ${CMAKE_COMMAND} ${args}
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    file(REMOVE "${BIN}/CMakeCache.txt")
    message(FATAL_ERROR "gdcore_shaderc: configuring the host tool failed (${BIN}):\n${out}\n${err}")
  endif()
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E env --unset=CMAKE_TOOLCHAIN_FILE ${CMAKE_COMMAND} --build "${BIN}" --target gdcore_shaderc --config Release
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "gdcore_shaderc: building the host tool failed (${BIN}):\n${out}\n${err}")
endif()
file(LOCK "${BIN}.lock" RELEASE)
