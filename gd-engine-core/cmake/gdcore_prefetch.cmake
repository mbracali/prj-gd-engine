# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# Downloads every pinned library of gdcore_deps.cmake into GDCORE_DEPS_DIR
# (default gd-engine-core/build/deps), for a machine or container without
# network:   cmake -P gd-engine-core/cmake/gdcore_prefetch.cmake
#            cmake -DGDCORE_DEPS_DIR=/some/cache -P gd-engine-core/cmake/gdcore_prefetch.cmake
cmake_minimum_required(VERSION 3.22)
include(${CMAKE_CURRENT_LIST_DIR}/gdcore_deps.cmake)
foreach(name ${GDCORE_DEPS})
  gdcore_fetch(${name} dir)
  message(STATUS "${name} ${GDCORE_DEP_${name}_VERSION}: ${dir}")
endforeach()
