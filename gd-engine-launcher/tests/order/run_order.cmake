# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# cmake -DGDUL=<launcher> -DDIR=<build>/order-test -P run_order.cmake
# The playable games come first (ready_to_play), then the others, each group
# in games.yaml's order (docs/guidelines/universal_launcher.md). games.yaml
# lists a (not ready), b (ready), c (not ready), d (ready): `gdul --list` must
# print b, d, a, c.
cmake_minimum_required(VERSION 3.22)
file(REMOVE_RECURSE ${DIR})
file(MAKE_DIRECTORY ${DIR}/xdg)
set(games "games:\n")
foreach(spec "a;false" "b;true" "c;false" "d;true")
  list(GET spec 0 id)
  list(GET spec 1 ready)
  string(APPEND games "  - gd-order-${id}.yaml\n")
  file(WRITE ${DIR}/gd-order-${id}.yaml
    "id: gd-order-${id}\nname: \"Order ${id}\"\nguitar_dream_version: \"0.0.0\"\nready_to_play: ${ready}\ncompleteness: 0\n")
endforeach()
file(WRITE ${DIR}/games.yaml "${games}")
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy XDG_DATA_HOME=${DIR}/xdg
          GDUL_GAMES_YAML=${DIR}/games.yaml ${GDUL} --list
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
string(REGEX MATCHALL "gd-order-[a-d]" order "${out}")
string(REPLACE ";" " " order "${order}")
message(STATUS "order: ${order}")
if(NOT rc EQUAL 0 OR NOT order STREQUAL "gd-order-b gd-order-d gd-order-a gd-order-c")
  message(FATAL_ERROR "gdul_order: expected gd-order-b gd-order-d gd-order-a gd-order-c, got '${order}' (exit ${rc})\n${out}\n${err}")
endif()
message(STATUS "gdul_order: ok")
