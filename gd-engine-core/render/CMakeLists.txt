# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_render: gd::core::render, the graphics interface and its backends
# (docs/render.md). OpenGL3 (OpenGL 3.3 core / OpenGL ES 3.0) is built
# everywhere; SDL3 GPU and Vulkan come in later phases. Needs SDL3::SDL3
# (find_package, or the game's add_subdirectory of SDL's source on Android).
cmake_minimum_required(VERSION 3.24)
project(gdcore_render LANGUAGES C CXX)

if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 REQUIRED CONFIG)
endif()

add_library(gdcore_render STATIC
  src/device.cpp
  src/gl/gl_api.cpp
  src/gl/gl_device.cpp
)
target_include_directories(gdcore_render PUBLIC include PRIVATE src)
target_compile_features(gdcore_render PUBLIC cxx_std_17)
target_compile_definitions(gdcore_render PRIVATE GDCORE_RENDER_OPENGL3=1)
target_link_libraries(gdcore_render PRIVATE SDL3::SDL3)
# Linked into game modules (shared libraries).
set_target_properties(gdcore_render PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(gdcore_render PRIVATE /W4)
else()
  target_compile_options(gdcore_render PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The headless self-test (docs/render.md section 7): SDL_VIDEO_DRIVER=offscreen
# gdcore_render_selftest; exit status 0 = every check passed.
option(GDCORE_RENDER_TESTS "Build gdcore_render_selftest" ${PROJECT_IS_TOP_LEVEL})
if(PROJECT_IS_TOP_LEVEL)
  enable_testing()
endif()
if(GDCORE_RENDER_TESTS AND NOT ANDROID)
  add_executable(gdcore_render_selftest tests/render_selftest.cpp)
  target_link_libraries(gdcore_render_selftest PRIVATE gdcore_render SDL3::SDL3)
  if(NOT MSVC)
    target_compile_options(gdcore_render_selftest PRIVATE -Wall -Wextra)
  endif()
  if(COMMAND add_test)
    add_test(NAME gdcore_render_selftest COMMAND gdcore_render_selftest)
    set_tests_properties(gdcore_render_selftest PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
  endif()
endif()
