# SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
# Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#
# THE place where gd-engine-core's fetched libraries are pinned. Every larger
# third-party library is fetched at configure time, at an exact release (at
# least two weeks old when pinned), checked against its SHA-256, and built
# from source for every platform. Single headers are vendored in
# ../third_party/ instead (see third_party/README.md).
#
# | Name       | Version (tag)          | Licence                        | Used by                                  |
# |------------|------------------------|--------------------------------|------------------------------------------|
# | SDL3_image | 3.4.8 (release-3.4.8)  | zlib                           | image: the "sdl3" backend                |
# | KTX        | 4.4.2 (v4.4.2)         | Apache-2.0 (+ bundled: see doc)| image: the "bcdec" backend's KTX / KTX2  |
#
# New pins go in this file (a row above, a block below, the name in
# GDCORE_DEPS). An area's CMakeLists.txt may call gdcore_pin() itself while
# its work is on a branch of its own; the pin moves here when it is merged.
#
# How sources are kept: one shared folder (GDCORE_DEPS_DIR, default
# <gd-engine-core>/build/deps, git-ignored; the environment variable of the
# same name overrides it) holds every source once, as <name>-<version>/, for
# every build folder and container of this checkout (the Steam Deck and
# portable-Linux containers mount the repository, so they see it too). The
# first configure downloads and checks it under a file lock; later ones reuse
# it. Offline machines: run `cmake -P gd-engine-core/cmake/gdcore_prefetch.cmake`
# where there is network (or copy the folder), then build. A download is
# never trusted without its SHA-256.
#
# Why not FetchContent_MakeAvailable: it keeps a copy of every source per
# build folder (gd01 has a dev build, a module build per platform, an APK
# build...) and cannot add a project EXCLUDE_FROM_ALL before CMake 3.28 (the
# Steam Runtime has 3.25). The download itself is CMake's (file(DOWNLOAD)
# with EXPECTED_HASH), the same thing FetchContent runs.

# ---- the pins ----
# gdcore_dep_<name>: URL, SHA-256, version, the folder inside the archive.
set(GDCORE_DEP_SDL3_IMAGE_VERSION 3.4.8)
set(GDCORE_DEP_SDL3_IMAGE_URL "https://github.com/libsdl-org/SDL_image/releases/download/release-3.4.8/SDL3_image-3.4.8.tar.gz")
set(GDCORE_DEP_SDL3_IMAGE_SHA256 e8223b424bc7541cf84ffff5cf7e4f24ec3711c60462cb1bd6dc1d7bd7f25477)
set(GDCORE_DEP_SDL3_IMAGE_TOPDIR SDL3_image-3.4.8)

set(GDCORE_DEP_KTX_VERSION 4.4.2)
set(GDCORE_DEP_KTX_URL "https://github.com/KhronosGroup/KTX-Software/archive/refs/tags/v4.4.2.tar.gz")
set(GDCORE_DEP_KTX_SHA256 9412cb45045a503005acd47d98f9e8b47154634a50b4df21e17a1dfa8971d323)
set(GDCORE_DEP_KTX_TOPDIR KTX-Software-4.4.2)

set(GDCORE_DEPS SDL3_IMAGE KTX)

# gdcore_pin(<NAME> <version> <url> <sha256> <folder inside the archive>)
function(gdcore_pin name ver url sha top)
  set(GDCORE_DEP_${name}_VERSION ${ver} PARENT_SCOPE)
  set(GDCORE_DEP_${name}_URL ${url} PARENT_SCOPE)
  set(GDCORE_DEP_${name}_SHA256 ${sha} PARENT_SCOPE)
  set(GDCORE_DEP_${name}_TOPDIR ${top} PARENT_SCOPE)
endfunction()

# ---- the shared source folder ----
if(NOT GDCORE_DEPS_DIR)
  if(DEFINED ENV{GDCORE_DEPS_DIR} AND NOT "$ENV{GDCORE_DEPS_DIR}" STREQUAL "")
    set(GDCORE_DEPS_DIR "$ENV{GDCORE_DEPS_DIR}")
  else()
    get_filename_component(GDCORE_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../build/deps" ABSOLUTE)
  endif()
endif()
set(GDCORE_DEPS_DIR "${GDCORE_DEPS_DIR}" CACHE PATH "gd-engine-core: where fetched third-party sources are kept")

# gdcore_fetch(<NAME> <out var>): the source folder of a pinned library,
# downloaded, checked and unpacked first if it is not there yet.
function(gdcore_fetch name out)
  set(ver ${GDCORE_DEP_${name}_VERSION})
  set(url ${GDCORE_DEP_${name}_URL})
  set(sha ${GDCORE_DEP_${name}_SHA256})
  set(top ${GDCORE_DEP_${name}_TOPDIR})
  if(NOT ver OR NOT url OR NOT sha OR NOT top)
    message(FATAL_ERROR "gdcore_fetch: ${name} is not pinned in gdcore_deps.cmake")
  endif()
  string(TOLOWER ${name} lname)
  set(dir "${GDCORE_DEPS_DIR}/${lname}-${ver}")
  set(stamp "${dir}/.gdcore-${sha}")
  if(NOT EXISTS "${stamp}")
    file(MAKE_DIRECTORY "${GDCORE_DEPS_DIR}")
    # Several configures at once (parallel builds, a container and the host):
    # one downloads, the others wait.
    file(LOCK "${GDCORE_DEPS_DIR}/.${lname}.lock" GUARD FUNCTION TIMEOUT 1800)
    if(NOT EXISTS "${stamp}")
      set(archive "${GDCORE_DEPS_DIR}/${lname}-${ver}.download")
      set(have_archive OFF)
      if(EXISTS "${archive}")
        file(SHA256 "${archive}" got)
        if(got STREQUAL sha)
          set(have_archive ON)
        endif()
      endif()
      if(NOT have_archive)
        message(STATUS "gd-engine-core: downloading ${name} ${ver} (${url})")
        file(DOWNLOAD "${url}" "${archive}" EXPECTED_HASH SHA256=${sha} STATUS st TLS_VERIFY ON)
        list(GET st 0 code)
        if(NOT code EQUAL 0)
          file(REMOVE "${archive}")
          list(GET st 1 why)
          message(FATAL_ERROR "gd-engine-core: ${name} ${ver}: download failed (${why}). Without network, run "
                              "`cmake -P gd-engine-core/cmake/gdcore_prefetch.cmake` where there is one, "
                              "or copy ${GDCORE_DEPS_DIR} from another checkout (docs/README.md).")
        endif()
      endif()
      set(tmp "${GDCORE_DEPS_DIR}/.${lname}-${ver}.unpack")
      file(REMOVE_RECURSE "${tmp}" "${dir}")
      file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${tmp}")
      if(NOT IS_DIRECTORY "${tmp}/${top}")
        message(FATAL_ERROR "gd-engine-core: ${name} ${ver}: ${top}/ not found in the archive")
      endif()
      file(RENAME "${tmp}/${top}" "${dir}")
      file(REMOVE_RECURSE "${tmp}")
      file(REMOVE "${archive}")
      file(WRITE "${stamp}" "${url}\n")
    endif()
  endif()
  set(${out} "${dir}" PARENT_SCOPE)
endfunction()
