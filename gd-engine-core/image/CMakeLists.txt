# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_image: gd::core::image::Decoder (include/gd/core/image/decoder.h) and
# its backends "stb", "bcdec" (+ libktx), "sdl3" (SDL3_image). Everything is
# linked statically into the game: no extra shared library to ship.
option(GDCORE_IMAGE_SDL3 "Image backend sdl3 (SDL3_image, fetched)" ON)
option(GDCORE_IMAGE_KTX "KTX / KTX2 files in the bcdec backend (KTX-Software, fetched)" ON)

gdcore_area_library(image
  src/decoder.cpp
  src/stb_backend.cpp
  src/stb_impl.cpp
  src/bcdec_backend.cpp
  src/bcdec_impl.cpp)
target_include_directories(gdcore_image PRIVATE src ${GDCORE_ROOT}/third_party/stb ${GDCORE_ROOT}/third_party/bcdec)
# The vendored single headers' implementation files: their code, their warnings.
if(MSVC)
  set_source_files_properties(src/stb_impl.cpp src/bcdec_impl.cpp PROPERTIES COMPILE_OPTIONS /w)
else()
  set_source_files_properties(src/stb_impl.cpp src/bcdec_impl.cpp PROPERTIES COMPILE_OPTIONS -w)
endif()

# Fetched libraries: built static, with only what we use, inside this
# folder's scope (the options below do not leak into the game's build).
set(BUILD_SHARED_LIBS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)  # option() honours the variables set here

if(GDCORE_IMAGE_SDL3)
  gdcore_fetch(SDL3_IMAGE sdl3_image_src)
  # SDL3_image with its own stb_image for PNG / JPEG and its built-in
  # loaders: no libpng / libjpeg / libwebp / libavif / libtiff / libjxl, no
  # vendored submodules, nothing loaded at run time.
  set(SDLIMAGE_VENDORED OFF)
  set(SDLIMAGE_DEPS_SHARED OFF)
  set(SDLIMAGE_BACKEND_STB ON)
  set(SDLIMAGE_BACKEND_WIC OFF)
  set(SDLIMAGE_BACKEND_IMAGEIO OFF)  # the same decoders on macOS as elsewhere
  set(SDLIMAGE_PNG_LIBPNG OFF)
  set(SDLIMAGE_AVIF OFF)
  set(SDLIMAGE_JXL OFF)
  set(SDLIMAGE_TIF OFF)
  set(SDLIMAGE_WEBP OFF)
  set(SDLIMAGE_SAMPLES OFF)
  set(SDLIMAGE_TESTS OFF)
  set(SDLIMAGE_INSTALL OFF)
  set(SDLIMAGE_STRICT ON)  # fail instead of quietly dropping a format
  add_subdirectory(${sdl3_image_src} ${CMAKE_CURRENT_BINARY_DIR}/sdl3_image EXCLUDE_FROM_ALL)
  get_target_property(t SDL3_image::SDL3_image ALIASED_TARGET)
  get_target_property(type ${t} TYPE)
  if(NOT type STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "gd-engine-core: SDL3_image must be a static library here (got ${type})")
  endif()
  gdcore_third_party_dir_warnings_off(${sdl3_image_src})
  target_link_libraries(gdcore_image PRIVATE SDL3_image::SDL3_image)
  target_sources(gdcore_image PRIVATE src/sdl3_backend.cpp)
  target_compile_definitions(gdcore_image PRIVATE GDCORE_IMAGE_SDL3=1)
else()
  target_compile_definitions(gdcore_image PRIVATE GDCORE_IMAGE_SDL3=0)
endif()

if(GDCORE_IMAGE_KTX)
  gdcore_fetch(KTX ktx_src)
  # The read-only libktx (ktx_read): no tools, tests, GL / Vulkan upload or
  # bindings. ETC_UNPACK off: lib/etcdec.cxx is under Ericsson's non-open
  # licence. SIMD: SSE2 only on x86-64 (runs on every x86-64 CPU; the default
  # would be AVX2), NEON on arm64 (automatic).
  set(KTX_FEATURE_TESTS OFF)
  set(KTX_FEATURE_TOOLS OFF)
  set(KTX_FEATURE_TOOLS_CTS OFF)
  set(KTX_FEATURE_DOC OFF)
  set(KTX_FEATURE_JNI OFF)
  set(KTX_FEATURE_PY OFF)
  set(KTX_FEATURE_ETC_UNPACK OFF)
  set(KTX_FEATURE_GL_UPLOAD OFF)
  set(KTX_FEATURE_VK_UPLOAD OFF)
  set(KTX_FEATURE_LOADTEST_APPS OFF)
  set(KTX_FEATURE_KTX1 ON)
  set(KTX_FEATURE_KTX2 ON)
  set(KTX_WERROR OFF)
  set(BASISU_SUPPORT_SSE OFF)
  set(BASISU_SUPPORT_OPENCL OFF)
  set(ASTCENC_ISA_SSE2 ON)
  # The release tarball has no git history: give KTX its version.
  set(KTX_GIT_VERSION_FULL v${GDCORE_DEP_KTX_VERSION})
  add_subdirectory(${ktx_src} ${CMAKE_CURRENT_BINARY_DIR}/ktx EXCLUDE_FROM_ALL)
  gdcore_third_party_dir_warnings_off(${ktx_src})
  target_link_libraries(gdcore_image PRIVATE ktx_read)
  target_compile_definitions(gdcore_image PRIVATE GDCORE_IMAGE_KTX=1)
else()
  target_compile_definitions(gdcore_image PRIVATE GDCORE_IMAGE_KTX=0)
endif()

gdcore_add_selftest(SOURCES tests/image_tests.cpp)
