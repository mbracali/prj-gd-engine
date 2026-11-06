# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_render: gd::core::render, the graphics interface and its backends
# (docs/render.md). OpenGL3 (OpenGL 3.3 core / OpenGL ES 3.0) and SDL3 GPU
# (SDL_GPU with the build-time SPIR-V: its Vulkan driver) are built
# everywhere; Vulkan (GDCORE_RENDER_VULKAN) everywhere but Apple and the web
# for now. Needs SDL3::SDL3 (find_package, or the game's add_subdirectory of
# SDL's source on Android).
#
# Also defines gdcore_spirv_shaders() (cmake/gdcore_shaders.cmake): the
# build-time SPIR-V of a game's shaders, with the host tool gdcore_shaderc.
cmake_minimum_required(VERSION 3.24)
project(gdcore_render LANGUAGES C CXX)

if(NOT TARGET SDL3::SDL3)
  find_package(SDL3 REQUIRED CONFIG)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/gdcore_shaders.cmake)

add_library(gdcore_render STATIC
  src/device.cpp
  src/common/spirv_reflect.cpp
  src/gl/gl_api.cpp
  src/gl/gl_device.cpp
  src/sdlgpu/sdlgpu_device.cpp
)
target_include_directories(gdcore_render PUBLIC include PRIVATE src)
target_compile_features(gdcore_render PUBLIC cxx_std_17)
target_compile_definitions(gdcore_render PRIVATE GDCORE_RENDER_OPENGL3=1 GDCORE_RENDER_SDLGPU=1)
target_link_libraries(gdcore_render PRIVATE SDL3::SDL3)

# The Vulkan backend (docs/render.md 4.3): Vulkan-Headers, volk and VMA,
# pinned in cmake/gdcore_deps.cmake. No Vulkan library is linked: volk takes
# the loader SDL opens at run time. volk is compiled in the C++ namespace
# `volk` (VOLK_NAMESPACE) so it never clashes with gd-game-01's own volk.
# Apple: off until MoltenVK / KosmicKrisp ships with the packages.
if(APPLE OR EMSCRIPTEN)
  set(gdcore_render_vulkan_default OFF)
else()
  set(gdcore_render_vulkan_default ON)
endif()
option(GDCORE_RENDER_VULKAN "Build the vulkan backend of gdcore_render" ${gdcore_render_vulkan_default})
if(GDCORE_RENDER_VULKAN)
  if(NOT COMMAND gdcore_fetch)  # render/ configured on its own
    include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/gdcore_deps.cmake)
  endif()
  gdcore_fetch(VULKAN_HEADERS gdcore_vulkan_headers_src)
  gdcore_fetch(VOLK gdcore_volk_src)
  gdcore_fetch(VMA gdcore_vma_src)
  target_sources(gdcore_render PRIVATE src/vulkan/vk_device.cpp src/vulkan/vk_loader.cpp)
  target_include_directories(gdcore_render SYSTEM PRIVATE
    ${gdcore_vulkan_headers_src}/include ${gdcore_volk_src} ${gdcore_vma_src}/include)
  target_compile_definitions(gdcore_render PRIVATE GDCORE_RENDER_VULKAN=1 VOLK_NAMESPACE=1)
  # Upstream code, unchanged: no warnings from it.
  set_source_files_properties(src/vulkan/vk_loader.cpp PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")
  # The Vulkan idiom `VkFooInfo info{VK_STRUCTURE_TYPE_FOO_INFO};` zero-fills the rest.
  if(NOT MSVC)
    set_source_files_properties(src/vulkan/vk_device.cpp PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
  endif()
  target_link_libraries(gdcore_render PRIVATE ${CMAKE_DL_LIBS})
  if(COMMAND gdcore_add_license)  # the packages carry the licences (top-level build)
    gdcore_add_license(${gdcore_volk_src}/LICENSE.md volk-LICENSE.md)
    gdcore_add_license(${gdcore_vma_src}/LICENSE.txt VulkanMemoryAllocator-LICENSE.txt)
    gdcore_add_license(${gdcore_vulkan_headers_src}/LICENSE.md Vulkan-Headers-LICENSE.md)
    gdcore_add_license(${gdcore_vulkan_headers_src}/LICENSES/Apache-2.0.txt Vulkan-Headers-Apache-2.0.txt)
    gdcore_add_license(${gdcore_vulkan_headers_src}/LICENSES/MIT.txt Vulkan-Headers-MIT.txt)
  endif()
else()
  target_compile_definitions(gdcore_render PRIVATE GDCORE_RENDER_VULKAN=0)
endif()
# Linked into game modules (shared libraries).
set_target_properties(gdcore_render PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(gdcore_render PRIVATE /W4)
else()
  target_compile_options(gdcore_render PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The headless self-test (docs/render.md section 7): SDL_VIDEO_DRIVER=offscreen
# gdcore_render_selftest; exit status 0 = every check passed.
if(PROJECT_IS_TOP_LEVEL OR GDCORE_BUILD_TESTS)
  set(gdcore_render_tests_default ON)
else()
  set(gdcore_render_tests_default OFF)
endif()
option(GDCORE_RENDER_TESTS "Build gdcore_render_selftest" ${gdcore_render_tests_default})
if(PROJECT_IS_TOP_LEVEL)
  enable_testing()
endif()
if(GDCORE_RENDER_TESTS AND NOT ANDROID)
  add_executable(gdcore_render_selftest tests/render_selftest.cpp)
  target_link_libraries(gdcore_render_selftest PRIVATE gdcore_render SDL3::SDL3)
  # Its shaders as SPIR-V too (render_selftest_spirv.h): checks that the
  # dialect compiles for SDL_GPU / Vulkan, and gives those backends the blobs.
  target_include_directories(gdcore_render_selftest PRIVATE src)
  gdcore_spirv_shaders(gdcore_render_selftest OUTPUT render_selftest_spirv.h SOURCES tests/render_selftest.cpp)
  if(NOT MSVC)
    target_compile_options(gdcore_render_selftest PRIVATE -Wall -Wextra)
  endif()
  if(COMMAND add_test)
    add_test(NAME gdcore_render_selftest COMMAND gdcore_render_selftest)
    set_tests_properties(gdcore_render_selftest PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen")
    if(GDCORE_RENDER_VULKAN)
      # Off screen (no swapchain: the offscreen driver's surface cannot
      # present); exit 77 = skipped (no Vulkan loader or device here).
      add_test(NAME gdcore_render_selftest_vulkan COMMAND gdcore_render_selftest --backend vulkan)
      set_tests_properties(gdcore_render_selftest_vulkan PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=offscreen" SKIP_RETURN_CODE 77)
    endif()
    # SDL3 GPU needs a window it can present to: with the environment's video
    # driver (x11 / wayland); skipped (77) where SDL_GPU cannot claim the window.
    add_test(NAME gdcore_render_selftest_sdl3 COMMAND gdcore_render_selftest --backend sdl3)
    set_tests_properties(gdcore_render_selftest_sdl3 PROPERTIES SKIP_RETURN_CODE 77)
  endif()
endif()
