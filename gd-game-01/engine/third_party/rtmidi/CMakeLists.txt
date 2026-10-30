# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
# RtMidi 6.0.0 (https://github.com/thestk/rtmidi, MIT, LICENSE here), unchanged.
# Generic MIDI input for the Gamepad menu (src/input/midi_in.cpp): ALSA on
# Linux, WinMM on Windows, CoreMIDI on macOS. Not built on Android (MIDI shows
# as unavailable there). Without the ALSA headers (Fedora: alsa-lib-devel,
# Debian / Ubuntu: libasound2-dev) Linux builds the dummy API and MIDI shows as
# unavailable too.
add_library(gd_rtmidi STATIC RtMidi.cpp)
target_include_directories(gd_rtmidi SYSTEM INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(gd_rtmidi PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(gd_rtmidi PRIVATE /w)
else()
  target_compile_options(gd_rtmidi PRIVATE -w)
endif()
set(GD_RTMIDI_API "dummy")
if(WIN32)
  target_compile_definitions(gd_rtmidi PUBLIC __WINDOWS_MM__)
  target_link_libraries(gd_rtmidi PUBLIC winmm)
  set(GD_RTMIDI_API "WinMM")
elseif(APPLE)
  target_compile_definitions(gd_rtmidi PUBLIC __MACOSX_CORE__)
  target_link_libraries(gd_rtmidi PUBLIC "-framework CoreMIDI" "-framework CoreAudio" "-framework CoreFoundation")
  set(GD_RTMIDI_API "CoreMIDI")
else()
  find_package(ALSA QUIET)
  find_package(Threads REQUIRED)
  if(ALSA_FOUND)
    target_compile_definitions(gd_rtmidi PUBLIC __LINUX_ALSA__)
    target_include_directories(gd_rtmidi SYSTEM PRIVATE ${ALSA_INCLUDE_DIRS})
    target_link_libraries(gd_rtmidi PUBLIC ${ALSA_LIBRARIES} Threads::Threads)
    set(GD_RTMIDI_API "ALSA")
  else()
    message(STATUS "RtMidi: no ALSA headers (alsa-lib-devel / libasound2-dev): MIDI input will show as unavailable")
    target_compile_definitions(gd_rtmidi PUBLIC __RTMIDI_DUMMY__)
  endif()
endif()
target_compile_definitions(gd_rtmidi PUBLIC GD_RTMIDI_API="${GD_RTMIDI_API}")
