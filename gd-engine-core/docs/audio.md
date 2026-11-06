# gd-engine-core audio

The audio area of the core (GDCORE-002): one interface, `gd::core::audio::Output`,
and three backends behind a factory. Spec:
[engine-backend-guidelines.md](../../docs/guidelines/engine-backend-guidelines.md).

## The interface

`audio/include/gd/core/audio/output.h`:

- `Output::open(Spec, PullCallback, error)`: `Spec` is sample rate, channels,
  a frames-per-pull hint and `nullDevice` (headless). The callback fills
  interleaved **float** frames on the backend's audio thread. The device opens
  paused; nothing is pulled before `start()`.
- `start()`, `pause()` (returns once the callback is out), `close()` (safe
  twice; no pull after it returns), `spec()` (what the backend really runs),
  `latencyMs()`, `id()`, `name()` (for logs).
- `create(id, error)`, `backends()`, `available()` (ids that can run here:
  compiled in and, for OpenAL, its library present), `registerBackend()` for a
  game's or platform's own backend.
- No exceptions cross it: every failure is `false` / `nullptr` plus an error
  string, so the game logs it and falls back to its `local` path (rule 2).

**Decision: the game keeps its mixer.** The core output only moves frames from
the game's mix to a device. A game that already mixes (gd01's `Mixer`, its SPU
voices, the slip voices) keeps that code byte for byte; the backend changes only
who pulls it. Format conversion stays on the game side (gd01 uses an unbound
`SDL_AudioStream`, see below), so a backend does not need a resampler of its own.

| id | Library (pinned) | Licence | Linked | How it plays |
|---|---|---|---|---|
| `miniaudio` | miniaudio 0.11.25, vendored single header (`third_party/miniaudio/`) | Public domain / MIT-0 | Static (one C file) | `ma_device` playback, f32; miniaudio's data callback calls the pull |
| `openal` | OpenAL Soft 1.25.2 (FetchContent URL + SHA-256) | LGPL-2.1+ | **Never**: its own shared library, `SDL_LoadObject` at run time | One device, context and source per output; a callback buffer (`AL_SOFT_callback_buffer`) pulled by OpenAL Soft's mixer thread; direct channels, no HRTF, no limiter |
| `faudio` | FAudio 26.10 (FetchContent URL + SHA-256), built here on its SDL3 platform | zlib | Static | `FAudioCreate`, a mastering voice at the asked format, a float source voice fed from a ring of 3 buffers refilled in `OnBufferEnd` |

Pins live in `audio/CMakeLists.txt` (`GDCORE_OPENAL_SOFT_*`, `GDCORE_FAUDIO_*`),
miniaudio's in `third_party/miniaudio/README.md`. Options:
`GDCORE_AUDIO_MINIAUDIO`, `GDCORE_AUDIO_OPENAL`, `GDCORE_AUDIO_FAUDIO`,
`GDCORE_AUDIO_SELFTEST` (all ON). `gdcore_audio` defines `GDCORE_HAS_AUDIO=1`
for whoever links it.

## OpenAL Soft: LGPL handling and packaging

- Downloaded only (`SOURCE_SUBDIR` with no CMakeLists, so its CMake never runs
  inside ours), then built by `ExternalProject_Add` as its own CMake project with
  the same toolchain (MinGW / NDK / compilers, `ANDROID_ABI`, `ANDROID_PLATFORM`,
  `c++_static`, 16 KB pages; GCC runtimes static where the toolchain has them:
  MinGW, the Steam Runtime; not Fedora, whose GCC has no static libstdc++).
  Utilities, examples, tests, install rules, SDL backends and C++20 modules off.
- The backend loads it **only from the module's own folder** (`dladdr` /
  `GetModuleHandleEx` on an address inside the core; the executable's folder
  for a plain program; by name on Android, the APK's library folder). Never from
  the system path, so every player gets the tested build. `GDCORE_OPENAL_LIBRARY=<file>`
  overrides it (development aid). It stays loaded for the process (its threads are
  not unloaded under a running game).
- Without the file the backend's probe fails ("not installed next to the game"),
  `available()` leaves it out and the game falls back to local.
- An OpenAL library without `AL_SOFT_callback_buffer` / `AL_EXT_FLOAT32` (not
  OpenAL Soft 1.21+) fails at open, same fallback.

**What must ship, per package** (in GDCORE_AUDIO_RUNTIME_DIR, default the top
build folder; the cache entry `GDCORE_OPENAL_LIBRARY_NAME` names the file):

| Package | File next to the game module | Notes |
|---|---|---|
| Linux, Steam Deck | `libopenal.so.1` (the real file, not a symlink) | needs `libatomic.so.1` and, on a Fedora-built one, `libstdc++.so.6` from the system (as the module does) |
| Windows | `OpenAL32.dll` | imports only system DLLs (static libgcc / libstdc++ / winpthread) |
| Android | `libopenal.so`, also listed in the module's `native-libs.txt` | goes into the APK's `lib/<abi>/` |
| macOS | `libopenal.1.dylib` | not built locally yet |

Plus the licences from `gdcore-licenses/` into the package's `licenses/`:
`OpenAL-Soft-COPYING.txt` (LGPL), `OpenAL-Soft-BSD-3Clause.txt`,
`OpenAL-Soft-LICENSE-pffft.txt`, `OpenAL-Soft-fmt-LICENSE.txt`,
`OpenAL-Soft-NOTICE.txt` (version, source URL and hash, "replace this file with
your own build"), `FAudio-LICENSE.txt`, `miniaudio-LICENSE.txt`.

`gd-engine-launcher/scripts/build.sh` `build_module_01` does this for gd01: when
the module's `CMakeCache.txt` has `GDCORE_OPENAL_LIBRARY_NAME`, it copies that
file next to the module (a missing file fails the build) and adds it to
`native-libs.txt` on Android, and copies `gdcore-licenses/*` to `licenses/`.
Another game adopting the core copies the same two things.

## Headless behaviour (tests, walks)

| Backend | Null device | Real time? |
|---|---|---|
| miniaudio | `Spec::nullDevice`: a context with only `ma_backend_null` | yes: its thread consumes one period per period |
| openal | `Spec::nullDevice`: sets `ALSOFT_DRIVERS=null` (unless set) before OpenAL Soft's first ALC call; OpenAL Soft reads it once per process, so the first output decides | yes: the null backend mixes on its own thread and drops the result |
| faudio | SDL's `dummy` audio driver (FAudio plays through SDL); `nullDevice` sets the hint only when SDL audio is not yet started | yes: SDL's dummy driver waits a buffer's time |

gd01 sets `nullDevice` when SDL's current audio driver is `dummy`, so
`SDL_AUDIO_DRIVER=dummy` (what its self-tests and walks use) is headless on every backend.

## Tests

- `gdcore_audio_selftest` (built with `--target gdcore_audio_selftest`, next to
  the OpenAL library): every available backend on its null device at 44100 Hz:
  no pull before start, a quarter second pulled in real time, nothing while
  paused, resume, nothing after close, close twice, an unknown id fails with an
  error. `--device` uses the real default devices; ids on the command line
  limit it (and make an unavailable one fail).
- gd01: `--selftest` and `tools/walks.sh` with `SDL_AUDIO_DRIVER=dummy`, on
  `local` and with `GD01_AUDIO_BACKEND=<id>` (gd01's development override of the
  Enhanced setting) for each backend.

## gd01 (the first game on it)

- `audio_backend` (Enhanced > Performance > Audio backend) is latched at
  start-up (`startupAudioBackend()`, app.h); `Mixer::open(error, backend)` gets
  it. `local` (or anything that fails) is the old code path, unchanged: the same
  `SDL_OpenAudioDeviceStream` and calls.
- Another id: `AudioRoute` (audio/mixer.h) opens the core output at the song's
  rate, then an **unbound** `SDL_AudioStream` from the mixer's format to the
  output's, with the mixer's existing `Mixer::callback` as its get callback. The
  output's pull calls `SDL_GetAudioStreamData`, which runs that callback exactly
  as SDL's device thread does, so the song clock code (queued frames, the device
  latency held back) is the same; the latency comes from `Output::latencyMs()`.
  Underruns play silence, as SDL does.
- The PSS movies' audio opens a second `AudioRoute` on the same backend (its own
  output; the picture follows the wall clock as on local), or SDL's device when
  the song mix is on local or the route fails.
- Logs: `audio: backend <id>: <name> (<ms> held back from the song clock)`, or
  `audio: backend <id> failed (<why>); using local`.

## Adding a backend

1. `audio/src/<name>_output.cpp`: a class implementing `Output` and a
   `Backend <name>Backend()` (id, menu text, probe, maker); declare it in
   `src/backends.h` under `GDCORE_AUDIO_HAS_<NAME>` and list it in
   `output.cpp`'s registry.
2. `audio/CMakeLists.txt`: an option, the sources, the pinned library.
3. Its null device in `gdcore_audio_selftest`'s run, this file's tables, and
   the Enhanced menu's id list (universal-menus.md). Games need no change.
   Out-of-tree backends can use `registerBackend()` instead.
