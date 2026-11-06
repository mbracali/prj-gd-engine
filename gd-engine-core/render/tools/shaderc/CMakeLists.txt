# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_shaderc, the build-time shader compiler (gdcore_shaderc.cpp), as its
# own project: it is always built for the HOST (the machine that runs the
# build), also when the game is cross-compiled for Windows or Android.
# render/cmake/gdcore_shaders.cmake configures and builds it, once per host
# environment, in GDCORE_DEPS_DIR/host-tools/<key>/ (docs/render.md 3.3).
#
#   -DGLSLANG_SOURCE=<dir> -DSPIRV_TOOLS_SOURCE=<dir> -DSPIRV_HEADERS_SOURCE=<dir>
#   -DGDCORE_RENDER_DIR=<gd-engine-core/render>
#   -DGDCORE_SHADERC_VALIDATE=ON|OFF   SPIRV-Tools' validator (needs Python 3 to build)
cmake_minimum_required(VERSION 3.22)
project(gdcore_shaderc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
option(GDCORE_SHADERC_VALIDATE "Validate the SPIR-V with SPIRV-Tools" ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(GDCORE_SHADERC_VALIDATE)
  set(SPIRV-Headers_SOURCE_DIR ${SPIRV_HEADERS_SOURCE})
  set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
  set(SPIRV_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
  set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
  set(SPIRV_TOOLS_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(SKIP_SPIRV_TOOLS_INSTALL ON CACHE BOOL "" FORCE)
  add_subdirectory(${SPIRV_TOOLS_SOURCE} spirv-tools EXCLUDE_FROM_ALL)
endif()

# glslang: the GLSL front end and its SPIR-V back end only (no HLSL, no
# optimiser, no programs, no tests).
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_PCH OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${GLSLANG_SOURCE} glslang EXCLUDE_FROM_ALL)

add_executable(gdcore_shaderc gdcore_shaderc.cpp ${GDCORE_RENDER_DIR}/src/common/spirv_reflect.cpp)
target_include_directories(gdcore_shaderc PRIVATE ${GDCORE_RENDER_DIR}/src ${GDCORE_RENDER_DIR}/include)
target_link_libraries(gdcore_shaderc PRIVATE glslang::glslang glslang::glslang-default-resource-limits)
if(TARGET glslang::SPIRV)
  target_link_libraries(gdcore_shaderc PRIVATE glslang::SPIRV)
endif()
if(GDCORE_SHADERC_VALIDATE)
  target_compile_definitions(gdcore_shaderc PRIVATE GDCORE_SHADERC_VALIDATE=1)
  target_link_libraries(gdcore_shaderc PRIVATE SPIRV-Tools-static)
endif()
