# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# Helpers for the areas' CMakeLists.txt (included by the top-level one, so
# every area can call them):
#
#   gdcore_area_library(<area> <sources...>)
#       the static library gdcore_<area>: C++17, PIC, the area's include/
#       folder public, our warning level, SDL3 linked.
#   gdcore_add_selftest(SOURCES <files...> [LIBRARIES <targets...>])
#       adds test files to gdcore_selftest (tests/selftest.h: GDCORE_TEST).
#   gdcore_add_runtime_library(<imported or built target | file> [LICENSE <file> <name>])
#       a shared library a backend loads at run time (OpenAL Soft, ...), and
#       its licence: the packaging scripts copy them next to the game module
#       (docs/README.md, "Shared libraries in the packages").
#   gdcore_third_party_warnings_off(<target>)
#   gdcore_third_party_dir_warnings_off(<added source folder>)
#       no warnings from fetched / vendored code we do not change.

function(gdcore_area_library area)
  set(t gdcore_${area})
  add_library(${t} STATIC ${ARGN})
  target_compile_features(${t} PUBLIC cxx_std_17)
  set_target_properties(${t} PROPERTIES POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden C_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(${t} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  endif()
  target_link_libraries(${t} PUBLIC SDL3::SDL3)
  if(MSVC)
    target_compile_options(${t} PRIVATE /W4 /permissive-)
  else()
    target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endfunction()

function(gdcore_third_party_warnings_off target)
  if(NOT TARGET ${target})
    return()
  endif()
  get_target_property(aliased ${target} ALIASED_TARGET)
  if(aliased)
    set(target ${aliased})
  endif()
  get_target_property(type ${target} TYPE)
  if(NOT type MATCHES "^(STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
    return()
  endif()
  get_target_property(imported ${target} IMPORTED)
  if(imported)
    return()
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /w)
  else()
    target_compile_options(${target} PRIVATE -w)
  endif()
endfunction()

# Every target of an added third-party folder and its subfolders.
function(gdcore_third_party_dir_warnings_off dir)
  get_property(targets DIRECTORY ${dir} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(t ${targets})
    gdcore_third_party_warnings_off(${t})
  endforeach()
  get_property(subs DIRECTORY ${dir} PROPERTY SUBDIRECTORIES)
  foreach(s ${subs})
    gdcore_third_party_dir_warnings_off(${s})
  endforeach()
endfunction()

# ---- gdcore_selftest ----
function(gdcore_add_selftest)
  cmake_parse_arguments(T "" "" "SOURCES;LIBRARIES" ${ARGN})
  foreach(s ${T_SOURCES})
    get_filename_component(abs ${s} ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(GLOBAL APPEND PROPERTY GDCORE_SELFTEST_SOURCES ${abs})
  endforeach()
  set_property(GLOBAL APPEND PROPERTY GDCORE_SELFTEST_LIBRARIES ${T_LIBRARIES})
endfunction()

function(gdcore_finish_selftest)
  get_property(srcs GLOBAL PROPERTY GDCORE_SELFTEST_SOURCES)
  get_property(libs GLOBAL PROPERTY GDCORE_SELFTEST_LIBRARIES)
  add_executable(gdcore_selftest ${GDCORE_ROOT}/tests/selftest_main.cpp ${srcs})
  target_include_directories(gdcore_selftest PRIVATE ${GDCORE_ROOT}/tests)
  target_compile_features(gdcore_selftest PRIVATE cxx_std_17)
  target_link_libraries(gdcore_selftest PRIVATE gdcore SDL3::SDL3 ${libs})
  if(MSVC)
    target_compile_options(gdcore_selftest PRIVATE /W4 /permissive-)
  else()
    target_compile_options(gdcore_selftest PRIVATE -Wall -Wextra -Wpedantic)
  endif()
  if(GDCORE_TOP_LEVEL)
    enable_testing()
    add_test(NAME gdcore_selftest COMMAND gdcore_selftest)
  endif()
endfunction()

# ---- shared libraries for the packages ----
function(gdcore_add_runtime_library what)
  cmake_parse_arguments(R "" "" "LICENSE" ${ARGN})
  if(TARGET ${what})
    set(path "$<TARGET_FILE:${what}>")
  else()
    get_filename_component(path ${what} ABSOLUTE)
  endif()
  set_property(GLOBAL APPEND PROPERTY GDCORE_RUNTIME_LINES "lib|${path}")
  if(R_LICENSE)
    list(LENGTH R_LICENSE n)
    if(NOT n EQUAL 2)
      message(FATAL_ERROR "gdcore_add_runtime_library(${what}): LICENSE <file> <name in licenses/>")
    endif()
    list(GET R_LICENSE 0 file)
    list(GET R_LICENSE 1 name)
    get_filename_component(file ${file} ABSOLUTE)
    set_property(GLOBAL APPEND PROPERTY GDCORE_RUNTIME_LINES "license|${file}|${name}")
  endif()
endfunction()

# <file>: one line per entry, fields separated by TABs, written at generate
# time (target paths resolved):
#   lib<TAB><absolute path of the built shared library>
#   license<TAB><absolute path of the licence file><TAB><its name in licenses/>
# An empty file when the core needs no shared library (today).
function(gdcore_write_runtime_manifest file)
  get_property(lines GLOBAL PROPERTY GDCORE_RUNTIME_LINES)
  set(content "")
  foreach(l ${lines})
    string(REPLACE "|" "\t" l "${l}")
    string(APPEND content "${l}\n")
  endforeach()
  file(GENERATE OUTPUT ${file} CONTENT "${content}")
endfunction()
