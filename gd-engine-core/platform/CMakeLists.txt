# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# gdcore_platform: folders, file IO, disc images, shared libraries
# (include/gd/core/platform/*.h). Depends on SDL3 only.
gdcore_area_library(platform
  src/paths.cpp
  src/file.cpp
  src/disc.cpp
  src/library.cpp)
target_include_directories(gdcore_platform PRIVATE src)
target_link_libraries(gdcore_platform PUBLIC ${CMAKE_DL_LIBS})  # dladdr (glibc before 2.34: libdl)
if(GDCORE_LAUNCHER_MODULE)
  target_compile_definitions(gdcore_platform PRIVATE GDCORE_LAUNCHER_MODULE=1)
endif()

gdcore_add_selftest(SOURCES tests/platform_tests.cpp)
