# AMD FidelityFX SDK 1.1.4 (MIT): the FSR 3.1 upscaler and its Vulkan backend
# only. The shader permutations in generated/ come from tools/ffx_gen_shaders.py.
add_library(ffx_fsr3 STATIC
  src/shared/ffx_assert.cpp
  src/shared/ffx_message.cpp
  src/shared/ffx_object_management.cpp
  src/shared/ffx_breadcrumbs_list.cpp
  src/backends/shared/ffx_shader_blobs.cpp
  src/backends/shared/blob_accessors/ffx_fsr3upscaler_shaderblobs.cpp
  src/components/fsr3upscaler/ffx_fsr3upscaler.cpp
  ffx_vk_volk.cpp)
target_include_directories(ffx_fsr3 PUBLIC include
  PRIVATE src/shared src/backends/shared src/backends/shared/blob_accessors src/components generated
  ${CMAKE_CURRENT_SOURCE_DIR}/../vulkan ${CMAKE_CURRENT_SOURCE_DIR}/../volk ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ffx_fsr3 PRIVATE FFX_FSR3UPSCALER=1 FFX_GCC=1)
target_link_libraries(ffx_fsr3 PRIVATE volk)
# third-party code: its warnings are not ours
if(MSVC)
  target_compile_options(ffx_fsr3 PRIVATE /w /FI${CMAKE_CURRENT_SOURCE_DIR}/ffx_port.h)
else()
  target_compile_options(ffx_fsr3 PRIVATE -w -include ${CMAKE_CURRENT_SOURCE_DIR}/ffx_port.h)
endif()
set_target_properties(ffx_fsr3 PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(volk STATIC ../volk/volk.c)
target_include_directories(volk PUBLIC ../volk ../vulkan)
target_compile_options(volk PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/w,-w>)
if(WIN32)  # the external-memory / semaphore handles of the GL interop
  target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
