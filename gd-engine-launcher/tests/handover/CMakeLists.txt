# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# The window hand-over test (GDUL_BUILD_TESTS=ON; desktop only; never in a
# package): the module gd-test-handover (handover_module.cpp) in
# <build>/gd-engine-games/gd-test-handover/, a games.yaml listing it
# (<build>/handover-test/), and run_handover.cmake, which plays it in the
# launcher under SDL's offscreen driver and checks the log and the launcher's
# frame after it (ctest -R gdul_handover).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../gd-engine-core ${CMAKE_BINARY_DIR}/gd-engine-core EXCLUDE_FROM_ALL)

add_library(gd-test-handover MODULE handover_module.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/gd_module_shim.cpp)
set_target_properties(gd-test-handover PROPERTIES PREFIX "" CXX_VISIBILITY_PRESET hidden
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/gd-engine-games/gd-test-handover)
target_include_directories(gd-test-handover PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../gd-game-01/engine/third_party/vulkan)
target_link_libraries(gd-test-handover PRIVATE gdcore_render SDL3::SDL3)
if(MSVC)
  target_compile_options(gd-test-handover PRIVATE "/FI${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/gd_module_shim.h")
else()
  target_compile_options(gd-test-handover PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/../../sdk/gd_module_shim.h)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(gd-test-handover PRIVATE -fno-gnu-unique)
  endif()
endif()
add_dependencies(gdul gd-test-handover)

set(t ${CMAKE_BINARY_DIR}/handover-test)
file(WRITE ${t}/games.yaml "games:\n  - gd-test-handover.yaml\n")
file(WRITE ${t}/gd-test-handover.yaml
  "id: gd-test-handover\nname: \"Window hand-over test\"\nguitar_dream_version: \"0.0.0\"\nready_to_play: true\ncompleteness: 0\n")
file(WRITE ${t}/disc.iso "not a disc: the test module reads nothing\n")

enable_testing()
add_test(NAME gdul_handover
  COMMAND ${CMAKE_COMMAND} -DGDUL=$<TARGET_FILE:gdul> -DDIR=${t} -P ${CMAKE_CURRENT_SOURCE_DIR}/run_handover.cmake)
