# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# The build-time shader step (docs/render.md section 3.3). Included by
# render/CMakeLists.txt, so every game that adds gd-engine-core can call:
#
#   gdcore_spirv_shaders(<target> OUTPUT <header.h> SOURCES <file.cpp>...
#                        [NAMESPACE <ns>] [DEFINES NAME[=VALUE]...])
#
# which compiles every k<Name>Vs / k<Name>Fs raw string (the gd shader
# dialect) of the sources to SPIR-V with gdcore_shaderc at build time, into
# <build>/gdcore_shaders/<target>/<header.h> (on the target's include path):
# `inline constexpr uint32_t k<Name>VsSpirv[]`, for ShaderDesc::spirv(). A
# shader that does not compile, is not valid SPIR-V (spirv-val) or breaks the
# dialect's binding layout fails the build.
#
# gdcore_shaderc is a HOST program: built once per build machine environment
# (the host, each container) in GDCORE_DEPS_DIR/host-tools/, also for the
# Windows and Android cross builds, from the glslang / SPIRV-Tools /
# SPIRV-Headers pinned in cmake/gdcore_deps.cmake. The first configure on a
# machine builds it (a few minutes); later configures and builds reuse it.
#
# Options (cache variables, or the environment variables of the same name):
#   GDCORE_SHADERC        a gdcore_shaderc built elsewhere (skips building it)
#   GDCORE_HOST_CC / GDCORE_HOST_CXX   the host compilers, when CC / CXX of
#                         the environment are not the build machine's
#   GDCORE_SPIRV_VALIDATE validate with SPIRV-Tools (default ON; needs Python 3
#                         to build SPIRV-Tools; turned off with a warning when
#                         there is none)

include_guard(GLOBAL)
if(NOT COMMAND gdcore_fetch)  # render/ configured on its own
  include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/gdcore_deps.cmake)
endif()

foreach(v GDCORE_SHADERC GDCORE_HOST_CC GDCORE_HOST_CXX)
  if(NOT ${v} AND DEFINED ENV{${v}} AND NOT "$ENV{${v}}" STREQUAL "")
    set(${v} "$ENV{${v}}")
  endif()
endforeach()
set(GDCORE_SHADERC "${GDCORE_SHADERC}" CACHE FILEPATH "gd-engine-core: a prebuilt gdcore_shaderc (empty: build it for the host)")
set(GDCORE_HOST_CC "${GDCORE_HOST_CC}" CACHE STRING "gd-engine-core: the host C compiler for gdcore_shaderc (empty: CC / the default)")
set(GDCORE_HOST_CXX "${GDCORE_HOST_CXX}" CACHE STRING "gd-engine-core: the host C++ compiler for gdcore_shaderc (empty: CXX / the default)")
option(GDCORE_SPIRV_VALIDATE "gd-engine-core: validate the build-time SPIR-V with SPIRV-Tools" ON)
# gdcore_spirv_shaders is called from the games' folders, where the core's
# variables (the pins, the folders) are not visible: kept as global properties.
set_property(GLOBAL PROPERTY GDCORE_RENDER_SHADERS_DIR ${CMAKE_CURRENT_LIST_DIR})
set_property(GLOBAL PROPERTY GDCORE_SHADERC_DEPS_DIR "${GDCORE_DEPS_DIR}")
foreach(dep GLSLANG SPIRV_TOOLS SPIRV_HEADERS)
  foreach(f VERSION URL SHA256 TOPDIR)
    set_property(GLOBAL PROPERTY GDCORE_SHADERC_DEP_${dep}_${f} "${GDCORE_DEP_${dep}_${f}}")
  endforeach()
endforeach()

# The host tool: its executable path in GDCORE_SHADERC_EXE, and a target
# (gdcore_shaderc_tool) that keeps it up to date before every build.
function(gdcore_setup_shaderc)
  if(TARGET gdcore_shaderc_tool)
    return()
  endif()
  if(GDCORE_SHADERC)
    if(NOT EXISTS "${GDCORE_SHADERC}")
      message(FATAL_ERROR "GDCORE_SHADERC=${GDCORE_SHADERC} does not exist")
    endif()
    set_property(GLOBAL PROPERTY GDCORE_SHADERC_EXE "${GDCORE_SHADERC}")
    add_custom_target(gdcore_shaderc_tool)
    return()
  endif()
  get_property(GDCORE_RENDER_SHADERS_DIR GLOBAL PROPERTY GDCORE_RENDER_SHADERS_DIR)
  get_property(GDCORE_DEPS_DIR GLOBAL PROPERTY GDCORE_SHADERC_DEPS_DIR)
  foreach(dep GLSLANG SPIRV_TOOLS SPIRV_HEADERS)
    foreach(f VERSION URL SHA256 TOPDIR)
      get_property(GDCORE_DEP_${dep}_${f} GLOBAL PROPERTY GDCORE_SHADERC_DEP_${dep}_${f})
    endforeach()
  endforeach()
  gdcore_fetch(GLSLANG glslang_dir)
  gdcore_fetch(SPIRV_TOOLS spirv_tools_dir)
  gdcore_fetch(SPIRV_HEADERS spirv_headers_dir)
  set(validate ${GDCORE_SPIRV_VALIDATE})
  if(validate)
    find_program(GDCORE_HOST_PYTHON3 NAMES python3 python NO_CMAKE_FIND_ROOT_PATH)
    if(NOT GDCORE_HOST_PYTHON3)
      message(WARNING "gd-engine-core: no Python 3 on this machine: the SPIR-V is compiled without SPIRV-Tools' validation")
      set(validate OFF)
    endif()
  endif()
  # One build per host environment: the system (and its release: a container
  # has other libraries than its host), the compilers, the pins, the folder.
  set(env "${CMAKE_HOST_SYSTEM_NAME}|${CMAKE_HOST_SYSTEM_PROCESSOR}|$ENV{CC}|$ENV{CXX}|${GDCORE_HOST_CC}|${GDCORE_HOST_CXX}")
  if(EXISTS /etc/os-release)
    file(STRINGS /etc/os-release osr REGEX "^(ID|VERSION_ID|BUILD_ID)=")
    string(APPEND env "|${osr}")
  endif()
  string(APPEND env "|${GDCORE_DEP_GLSLANG_SHA256}|${GDCORE_DEP_SPIRV_TOOLS_SHA256}|${validate}|${GDCORE_DEPS_DIR}")
  string(MD5 key "${env}")
  string(SUBSTRING ${key} 0 12 key)
  string(TOLOWER "${CMAKE_HOST_SYSTEM_NAME}" sys)
  set(bin "${GDCORE_DEPS_DIR}/host-tools/gdcore_shaderc-${sys}-${key}")
  if(CMAKE_GENERATOR MATCHES "Ninja")
    set(gen "Ninja")
  else()
    set(gen "${CMAKE_GENERATOR}")
  endif()
  set(make "")
  if(gen STREQUAL "Ninja" AND CMAKE_MAKE_PROGRAM)
    set(make "${CMAKE_MAKE_PROGRAM}")
  endif()
  set(cmd ${CMAKE_COMMAND} -DSRC=${GDCORE_RENDER_SHADERS_DIR}/../tools/shaderc -DBIN=${bin}
      -DRENDER=${GDCORE_RENDER_SHADERS_DIR}/.. -DGLSLANG=${glslang_dir} -DSPIRV_TOOLS=${spirv_tools_dir}
      -DSPIRV_HEADERS=${spirv_headers_dir} -DVALIDATE=${validate} -DGENERATOR=${gen} -DMAKE_PROGRAM=${make}
      -DHOST_CC=${GDCORE_HOST_CC} -DHOST_CXX=${GDCORE_HOST_CXX} -P ${GDCORE_RENDER_SHADERS_DIR}/gdcore_shaderc_host.cmake)
  if(NOT EXISTS "${bin}/CMakeCache.txt")
    message(STATUS "gd-engine-core: building gdcore_shaderc for this machine (glslang ${GDCORE_DEP_GLSLANG_VERSION}, "
                   "SPIRV-Tools validation ${validate}; once per machine, a few minutes): ${bin}")
  endif()
  execute_process(COMMAND ${cmd} RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "gd-engine-core: gdcore_shaderc could not be built (above). It is a program for the build "
                        "machine: set GDCORE_HOST_CC / GDCORE_HOST_CXX if CC / CXX are a cross compiler, or "
                        "GDCORE_SHADERC to one built elsewhere.")
  endif()
  set(suffix "")
  if(CMAKE_HOST_WIN32)
    set(suffix ".exe")
  endif()
  set(exe "${bin}/gdcore_shaderc${suffix}")
  if(NOT EXISTS "${exe}" AND EXISTS "${bin}/Release/gdcore_shaderc${suffix}")
    set(exe "${bin}/Release/gdcore_shaderc${suffix}")
  endif()
  if(NOT EXISTS "${exe}")
    message(FATAL_ERROR "gd-engine-core: gdcore_shaderc was built but ${exe} is not there")
  endif()
  set_property(GLOBAL PROPERTY GDCORE_SHADERC_EXE "${exe}")
  set_property(GLOBAL PROPERTY GDCORE_SHADERC_VALIDATES ${validate})
  # Before each build: rebuilt if its sources changed (a no-op otherwise).
  add_custom_target(gdcore_shaderc_tool COMMAND ${cmd} BYPRODUCTS ${exe} COMMENT "gdcore_shaderc (host tool)" VERBATIM)
endfunction()

function(gdcore_spirv_shaders target)
  cmake_parse_arguments(S "" "OUTPUT;NAMESPACE" "SOURCES;DEFINES" ${ARGN})
  if(NOT S_OUTPUT OR NOT S_SOURCES)
    message(FATAL_ERROR "gdcore_spirv_shaders(${target} OUTPUT <header.h> SOURCES <files...>)")
  endif()
  gdcore_setup_shaderc()
  get_property(exe GLOBAL PROPERTY GDCORE_SHADERC_EXE)
  set(dir ${CMAKE_BINARY_DIR}/gdcore_shaders/${target})
  set(out ${dir}/${S_OUTPUT})
  set(srcs "")
  foreach(s ${S_SOURCES})
    get_filename_component(abs ${s} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    list(APPEND srcs ${abs})
  endforeach()
  set(args --quiet --out ${out})
  if(S_NAMESPACE)
    list(APPEND args --namespace ${S_NAMESPACE})
  endif()
  foreach(d ${S_DEFINES})
    list(APPEND args --define ${d})
  endforeach()
  file(MAKE_DIRECTORY ${dir})
  add_custom_command(OUTPUT ${out}
    COMMAND ${exe} ${args} ${srcs}
    DEPENDS ${srcs} ${exe} gdcore_shaderc_tool
    COMMENT "gdcore_shaderc: ${S_OUTPUT} (SPIR-V of ${target}'s shaders)" VERBATIM)
  target_sources(${target} PRIVATE ${out})
  target_include_directories(${target} PRIVATE ${dir})
endfunction()
