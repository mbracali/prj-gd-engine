# gd-engine-core: design notes

The libraries every Guitar Dream game module can share: platform, inputs,
audio, render, image, ui. The requirements are in
[engine-backend-guidelines.md](../../docs/guidelines/engine-backend-guidelines.md)
(the owner's 1.1.13 specification); this file records how the core meets them
and the decisions taken on the way. The backlog tracks the work as
GDCORE-001..007 ([gd-backlog.md](../../docs/tasks/gd-backlog.md#gd-engine-core)).

## Architecture

```
gd-engine-core/
  CMakeLists.txt     adds every area folder that exists; the INTERFACE target `gdcore`
  cmake/             gdcore_deps.cmake (THE pins), gdcore_helpers.cmake, gdcore_prefetch.cmake
  third_party/       vendored single headers, each with its LICENSE (third_party/README.md)
  platform/          gdcore_platform: folders, file IO, disc images, shared libraries
  inputs/            gdcore_inputs (planned: README.md)
  audio/             gdcore_audio: gd::core::audio::Output and its backends
  render/            gdcore_render: gd::core::render::Device and its backends
  image/             gdcore_image: gd::core::image::Decoder and its backends
  ui/                gdcore_ui (planned: README.md)
  tests/             gdcore_selftest's runner (selftest.h)
  docs/              this file
```

- **One area = one folder = one static library `gdcore_<area>`**, its public
  headers under `<area>/include/gd/core/<area>/`, its namespace
  `gd::core::<area>`. A game includes `"gd/core/<area>/<header>.h"`.
- **One interface per area, many backends** (rule 3). Backends live behind a
  factory keyed by the ids the menus save (`stb`, `bcdec`, `sdl3`, `miniaudio`,
  `openal`, `faudio`, `vulkan`, `opengl3`). The factory returns `nullptr` with
  a reason for an id it does not know or a backend this platform did not get;
  the game then stays on **local** (its own code, always the default) and logs
  why (rule 2). `local` is never a core backend: it is the game's code.
- **Plain C++17, no exceptions across an interface, no game types** (rule 5).
  Failures return `false` / `nullptr` with an error string. Nothing in the core
  knows PS2, Xbox, Guitar Hero or any one game; the organisation name
  `gd-engine` is the only project name it carries (`kPrefOrg`).
- **Static libraries only.** Linking `gdcore` adds no file to a package.
  Unreferenced code never reaches the game binary (the linker takes only the
  archive members a game calls): gd01 links the whole core today and its
  binary has no core symbol in it [C: `nm`], so its behaviour is unchanged.
  Shared libraries exist only where a licence demands it (OpenAL Soft, LGPL);
  see "Shared libraries in the packages".
- **Every build carries every backend a platform can run** (rule 7). An area
  with a backend a platform cannot have compiles it out and leaves its id out
  of the factory's list (`decoderIds()`), so the menus do not offer it.

## How a game links the core

```cmake
# after SDL3 exists as SDL3::SDL3 (find_package, or the SDL3 source added
# with add_subdirectory as the Android builds do)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../gd-engine-core ${CMAKE_CURRENT_BINARY_DIR}/gd-engine-core)
target_link_libraries(<game target> PRIVATE gdcore)
```

That is all gd-game-01 does (`gd-game-01/engine/CMakeLists.txt`), and its
launcher module the same with `set(GDCORE_LAUNCHER_MODULE ON)` first
(`gd-engine-launcher/modules/gd01/CMakeLists.txt`): in a module build the
core's code follows the launcher's conventions (on Android, the nested save
folders of the module shim; on Linux with GCC, `-fno-gnu-unique` as the
module's own code, so the launcher can unload it).

Options: `GDCORE_BUILD_TESTS` (default on only when gd-engine-core is
configured on its own), `GDCORE_LAUNCHER_MODULE`, `GDCORE_DEPS_DIR`, and per
area: `GDCORE_IMAGE_SDL3`, `GDCORE_IMAGE_KTX` (both on).

Standalone build and self-test (Linux):

```sh
cmake -S gd-engine-core -B gd-engine-core/build/dev -G Ninja
cmake --build gd-engine-core/build/dev --target gdcore_selftest
gd-engine-core/build/dev/gdcore_selftest            # all checks
gd-engine-core/build/dev/gdcore_selftest image      # names containing "image"
gd-engine-core/build/dev/gdcore_selftest given -- "original_isos/Guitar Hero (USA) (PS2).iso"  # optional: real discs, read only
```

The self-test is headless (no window, no audio device needed) and never uses
game data: every sample is generated at run time (images drawn in memory and
encoded by one backend, decoded by another; BCn / ASTC blocks and DDS / KTX /
KTX2 files written byte by byte from their specifications; ISO 9660 and XDVDFS
images built in memory). Disc images given after `--` are only opened and
listed.

## Adding an area or a backend

**An area:** create `<area>/CMakeLists.txt` that calls

```cmake
gdcore_area_library(<area> src/a.cpp src/b.cpp)   # target gdcore_<area>, C++17, PIC, SDL3, warnings
gdcore_add_selftest(SOURCES tests/<area>_tests.cpp)
```

The top level picks it up by itself (the area list is
`platform inputs audio render image ui`); `gdcore` links it. Never edit the
top-level CMakeLists.txt for an area.

**A backend:**

1. A class implementing the area's interface in `<area>/src/<id>_backend.cpp`,
   with a `create<Id>()` function declared in the area's private
   `src/backends.h`.
2. One line in the area's factory (and its id list), behind a
   `GDCORE_<AREA>_<ID>` compile definition when a platform may not have it.
3. Third-party code: a single header goes to `third_party/<lib>/` (with its
   LICENSE and a row in `third_party/README.md`), compiled once in an
   `*_impl.cpp` with internal linkage and `-w`; a larger library gets a pin
   in `cmake/gdcore_deps.cmake` and is added with
   `gdcore_fetch(<NAME> dir)` + `add_subdirectory(${dir} ... EXCLUDE_FROM_ALL)`,
   built static, its options set as plain variables in the area's scope,
   `gdcore_third_party_dir_warnings_off(${dir})`, and
   `gdcore_add_license(<file> <name>)` for every licence the package must carry.
4. Checks in the area's self-test file; a backend that needs a device gets a
   headless variant (null / dummy / offscreen) where one exists (rule 10).
5. The id in the games' Enhanced menu list (owned by the menus, GDCORE-006).

A game never changes when a backend is added (rule 3).

## Dependencies

Pinned in [`cmake/gdcore_deps.cmake`](../cmake/gdcore_deps.cmake) (fetched) and
[`third_party/README.md`](../third_party/README.md) (vendored). Every version
was at least two weeks old when pinned.

| Name | Version | How | Licence | Area / backend |
|---|---|---|---|---|
| stb_image, stb_image_write, stb_image_resize2 | 2.30 / 1.16 / 2.18 (nothings/stb `2c980bb`, 2026-08-02) | vendored | public domain or MIT | image `stb` |
| bcdec | 0.985 (iOrange/bcdec `80859ed`, 2026-09-02) | vendored | MIT or Unlicense | image `bcdec` |
| SDL3_image | 3.4.8 (`release-3.4.8`, 2026-10-05) | fetched, static | zlib | image `sdl3` |
| KTX-Software (libktx, `ktx_read`) | 4.4.2 (`v4.4.2`, 2025-10-04) | fetched, static | Apache-2.0; bundles Basis Universal (Apache-2.0), zstd (BSD), astc-encoder (Apache-2.0), dfdutils (Apache-2.0) | image `bcdec` (KTX / KTX2) |
| SDL3 | the game's (3.4.18 in every package today) | the game's | zlib | every area |

The AUDIO and RENDER areas add their rows here and in `gdcore_deps.cmake`.

### Fetching

`gdcore_fetch(<NAME> <out>)` gives the source folder of a pinned library. All
sources live once in **`GDCORE_DEPS_DIR`** (default
`gd-engine-core/build/deps/`, git-ignored; the environment variable of the same
name overrides it), as `<name>-<version>/`, shared by every build folder of the
checkout: gd01's dev build, each launcher module build per platform, the APKs.
The first configure downloads the release archive, checks its **SHA-256**
(a mismatch stops the build), unpacks it and writes a stamp, all under a file
lock (parallel configures, the host and a container, wait for each other).
Later configures reuse it without network.

Why not `FetchContent_MakeAvailable`: it keeps one copy of each source per
build folder (about 300 MB for KTX-Software alone, times the number of build
folders) and cannot add a project `EXCLUDE_FROM_ALL` before CMake 3.28 (the
Steam Runtime SDK has 3.25). The download is CMake's own (`file(DOWNLOAD ...
EXPECTED_HASH)`, what FetchContent runs too); the add is a plain
`add_subdirectory(... EXCLUDE_FROM_ALL)`, so only the targets we link are built.

**Containers:** the Steam Deck build (sniper SDK, podman) and the portable
Linux build (ubuntu:22.04) mount the repository, so they see the same
`build/deps/` folder; both have network access when run by
`gd-engine-launcher/scripts/build.sh` [C: checked 2026-11-06, podman 5.8 on
Fedora 44]. **Offline** (a CI runner without network, a machine behind a
proxy): fetch where there is network and bring the folder along:

```sh
cmake -P gd-engine-core/cmake/gdcore_prefetch.cmake                          # into gd-engine-core/build/deps
cmake -DGDCORE_DEPS_DIR=/cache/gdcore -P gd-engine-core/cmake/gdcore_prefetch.cmake
GDCORE_DEPS_DIR=/cache/gdcore gd-engine-launcher/scripts/build.sh linux       # the builds read the variable
```

The containers do not inherit the host's environment: for them keep the
folder in the repository (the default), or copy the cache into
`gd-engine-core/build/deps/` first.

In GitHub Actions, cache `gd-engine-core/build/deps` keyed on
`hashFiles('gd-engine-core/cmake/gdcore_deps.cmake')` (KTX-Software's release
archive is about 210 MB, mostly its test images).

## Shared libraries in the packages (GDCORE-007)

A backend whose library must stay a separate shared library (OpenAL Soft:
LGPL, rule 6) builds or fetches it and registers it:

```cmake
gdcore_add_runtime_library(OpenAL::OpenAL LICENSE ${openal_src}/COPYING OpenAL-Soft-COPYING.txt)
```

- At generate time the core writes **`<game build dir>/gdcore-runtime.txt`**,
  one tab-separated line per entry: `lib <path>` (the file the loader asks
  for: the soname file on Linux / macOS, the DLL on Windows, the `.so` on
  Android) and `license <path> <name>` (also written for every statically
  linked library, `gdcore_add_license`).
- `gd-engine-launcher/scripts/build.sh` runs **`ship_core_runtime`** for every
  module that links the core (gd01's recipe does): each `lib` is copied next to
  the module (`gd-engine-games/gd-game-NN/`), on Android added to the
  module's `native-libs.txt` (so it goes into the APK's native libraries; the
  name must be `lib*.so`), on macOS given the install name `@loader_path/`;
  each licence goes to the module's `licenses/` (the package's
  `licenses/gd-game-NN-<name>`); the names go to the module's
  **`core-libs.txt`**.
- `gd-engine-launcher/scripts/verify_bundle.sh` checks that every library in a
  module's `core-libs.txt` is in the download (next to the module; in the APK's
  `lib/<abi>/` on Android), so a package can never ship without one. On
  Windows its import check also covers the DLLs' own imports.
- A module reused from the previous build carries them along (its folder is
  copied whole); a change anywhere in gd-engine-core changes the module's
  inputs hash (build.sh `module_inputs_hash`), so the module is rebuilt.
- **At run time** a backend loads it with
  `gd::core::platform::SharedLibrary::open({"libopenal.so.1"})` (Linux),
  `{"OpenAL32.dll", "soft_oal.dll"}` (Windows), `{"libopenal.1.dylib"}` (macOS),
  `{"libopenal.so"}` (Android): it tries the folder of the binary the core is
  linked into first (`moduleDir()`: the module's folder in the launcher, the
  program's folder for a game run on its own), then the system's search.
  A library that does not load means the backend reports failure and the game
  stays on local.

## The platform area

`gd/core/platform/`:

- **`paths.h`**: `prefDir(org, app, layout)` / `gamePrefDir(app)`. The folder
  is exactly what the games compute today, `SDL_GetPrefPath("gd-engine",
  "gdNN")` [C: gd01..04 `platform/paths.cpp`, the launcher's `saveDir()`], so a
  game that moves to this API keeps its saves. On Android a game's own APK gets
  SDL's `files/`; inside the launcher the module shim gives
  `files/gd-engine/<app>/` (the launcher itself `files/gd-engine/gdul/`): that
  is `PrefLayout::Nested`, which `Auto` picks in a module build
  (`GDCORE_LAUNCHER_MODULE`). Game-specific moves (gd01's old `gh1re` folder,
  the shim's gd02 copy) stay in the game / shim. Also `baseDir()`,
  `moduleDir()`, `xdgDataHome()` (desktop entries and icons, as gd01's
  `app_icon.cpp`), path helpers.
- **`file.h`**: `RandomAccessFile` (64-bit offsets, thread-safe reads) for
  plain UTF-8 paths (wide-character open on Windows: the "Windows wide paths"
  usual bug), Android `content://` documents (through SDL) and the launcher's
  `/proc/self/fd/<n>` disc descriptors (pread on a duplicate, as the module
  shim's `fopen`); `readFile`, `writeFileAtomic` (temp file + rename).
- **`disc.h`**: `DiscFileSystem` with `find` / `list` / `readFile` / `read`,
  `openDisc()` trying XDVDFS (Xbox, Xbox 360: the four known partition offsets)
  then ISO 9660 (PS2, PS1, PC), the launcher's probe order. Ported from gd01's
  `iso9660.cpp`, gd02's `xdvdfs.cpp` and the launcher's `iso_probe.cpp`; checked
  on generated images and, read only, on the five original discs in
  `original_isos/` [C: 2026-11-06]. The games keep their own readers until they
  move. Planned: UDF-only images, CHD, GameCube / Wii, PSP: each is another
  `DiscFileSystem` and another try in `openDisc()`.
- **`library.h`**: `SharedLibrary` (above).

## The image area

`gd/core/image/decoder.h`: `Decoder` with `canDecode`, `decode` (a file in
memory to RGBA8, or RGBA32F for HDR sources on request), `decodeBlocks` (raw
BCn), `encode` (PNG, TGA, BMP, JPEG), `resize`; `createDecoder(id)`,
`decoderIds()`.

| Id | Library | Decodes | Encodes | Resize |
|---|---|---|---|---|
| `stb` | stb_image / _write / _resize2 | PNG, JPEG, TGA, BMP, PSD, GIF, HDR, PIC, PNM | PNG, TGA, BMP, JPEG | stb_image_resize2 (default, linear, nearest) |
| `bcdec` | bcdec + libktx | raw BC1-BC7 (BC4 / BC5 as GL's R / RG; signed variants), DDS (legacy fourCC and DX10, mips, arrays, cube faces, uncompressed 24 / 32-bit), KTX 1 and KTX 2 (BCn, Basis Universal ETC1S / UASTC transcoded, ASTC decoded, RGBA / BGRA / RGB) | - | - |
| `sdl3` | SDL3_image | PNG, JPEG, BMP, GIF, TGA, QOI, PCX, PNM, XPM, XCF, XV, LBM, SVG | PNG, TGA, BMP, JPEG | SDL_ScaleSurface (linear, nearest) |

Decisions:
- **SDL3_image without system libraries**: built static with its stb backend
  for PNG / JPEG and its built-in loaders; libpng, libjpeg, libwebp, libavif,
  libtiff and libjxl are off (WebP / AVIF / TIFF / JXL would each need a
  vendored library: add them when a game needs them). `SDLIMAGE_STRICT` makes
  a missing dependency an error, not a silently dropped format.
- **libktx read-only** (`ktx_read`): no tools, tests, GL / Vulkan upload.
  **No ETC1 / ETC2 decoding**: libktx's decoder (`lib/etcdec.cxx`) is under
  Ericsson's licence, not open source [C: KTX-Software LICENSE.md], so
  `KTX_FEATURE_ETC_UNPACK` is off and such a file is refused with that reason.
  If a game needs ETC, add an open decoder (e.g. a single-header one) to the
  bcdec backend.
- **Portable SIMD**: astc-encoder with SSE2 on x86-64 (the default, AVX2, would
  crash on older CPUs), NEON on arm64; Basis Universal's SSE off.
- **Internal linkage for vendored code**: stb and bcdec are compiled with their
  `*_STATIC` switches in `stb_impl.cpp` / `bcdec_impl.cpp`, so they cannot clash
  with the launcher's or SDL3_image's own stb.
- **HDR to 8 bits**: clamp, then gamma 1/2.2 (stb_image's own rule [C]), the
  same for BC6H.
- Not yet checked by a sample: Basis Universal transcoding (writing a Basis
  file needs the encoder, which we do not build). It goes through libktx's
  own, tested path (`ktxTexture2_TranscodeBasis`).

## The planned areas

- `inputs/`: see [inputs/README.md](../inputs/README.md).
- `ui/`: see [ui/README.md](../ui/README.md).
- `audio/`, `render/`: their owners document them here (sections of their own).
