# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# cmake -DGDUL=<launcher> -DDIR=<build>/handover-test -P run_handover.cmake
# Plays gd-test-handover in the launcher (SDL offscreen video, dummy audio, an
# empty save folder), then lets the launcher draw its list and saves a frame:
# passes when the module's checks all passed, the launcher made its OpenGL
# window again, and that frame is not black.
cmake_minimum_required(VERSION 3.22)
file(REMOVE_RECURSE ${DIR}/xdg ${DIR}/after.bmp)
file(MAKE_DIRECTORY ${DIR}/xdg)
execute_process(
  COMMAND ${CMAKE_COMMAND} -E env SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy XDG_DATA_HOME=${DIR}/xdg
          GDUL_GAMES_YAML=${DIR}/games.yaml "GDUL_SCRIPT=8000:shot=${DIR}/after.bmp;8300:quit"
          SDL_GAMECONTROLLER_IGNORE_DEVICES=0x1bad/0x0002 SDL_JOYSTICK_IGNORE_DEVICES=0x1bad/0x0002
          ${GDUL} --play gd-test-handover --iso ${DIR}/disc.iso
  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
file(WRITE ${DIR}/run.log "${out}\n${err}")
set(all "${out}\n${err}")
string(REGEX MATCHALL "handover: [A-Z]+ [^\n]*" lines "${all}")
foreach(l ${lines})
  message(STATUS "${l}")
endforeach()
set(problems "")
if(NOT rc EQUAL 0)
  list(APPEND problems "the launcher exited with ${rc}")
endif()
if(NOT all MATCHES "handover: 0 failed")
  list(APPEND problems "the module's checks did not all pass")
endif()
if(NOT all MATCHES "back from gd-test-handover: the OpenGL window again")
  list(APPEND problems "the launcher did not make its OpenGL window again")
endif()
if(NOT EXISTS ${DIR}/after.bmp)
  list(APPEND problems "no frame of the launcher after the game")
else()
  file(READ ${DIR}/after.bmp px OFFSET 4096 LIMIT 65536 HEX)
  if(px MATCHES "^0*$")
    list(APPEND problems "the launcher's frame after the game is black")
  endif()
endif()
if(problems)
  message(FATAL_ERROR "gdul_handover: ${problems} (log: ${DIR}/run.log)")
endif()
message(STATUS "gdul_handover: passed (log: ${DIR}/run.log)")
