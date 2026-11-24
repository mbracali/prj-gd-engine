# Guitar Dream Engine — Universal Backend

> Purpose: A spec fed to agents building/onboarding games — the universal backend.

## Overview

"Backend" in this engine means a swappable implementation behind one of the
shared `gd-engine-core` interfaces. There are two that every game must reason
about:

- **Graphics backend** — how the game draws. Either the **shared
  `gd-engine-core` render device** (`gd::core::render::Device`, backed by
  OpenGL 3.3/GLES 3.0, SDL3 GPU, or Vulkan), or the game's own historical
  renderer, called **`local`** (gd01/gd04 call GL directly; gd02 goes through
  its own `RenderDevice`). `local` is always present and is the guaranteed
  fallback.
- **Audio backend** — how sound reaches the device. The core offers
  `miniaudio` / `OpenAL` / `FAudio` (plus `local`); the decoding of the
  disc's own song/SFX formats (XMA, FSB, VGS, Vorbis/.mogg, PS-ADPCM) is done
  by **per-game decoders** that feed PCM into whichever audio output backend
  is selected.

A game talks to **one interface per area** (`gd::core::render::Device`,
`gd::core::audio::Output`, `gd::core::image::Decoder`); backends live behind a
factory keyed by lower-case ids saved in settings (`opengl3`, `sdl3`,
`vulkan`, `miniaudio`, `openal`, `faudio`, `stb`, `bcdec`). Backends are
**chosen at start-up** from `enhanced.json`; a change in Enhanced > Performance
applies at the next start (or immediately via the Restart Now pop-up,
`GD_GAME_EXIT_RESTART` = 75). Any core backend that fails to start (missing
driver/library, or the launcher window is wrong for that API) **falls back to
`local` and logs the reason** — a player must never be left with a black
screen or no sound (`engine-backend-guidelines.md` rule 2).

The core knows nothing of any game (plain C++17, no exceptions across the
interface). Games adapt to it; adding a backend never changes a game.

## Graphics backends

The core render device enumerates three compiled backends
(`device.h`, `enum class Backend { OpenGL3, SdlGpu, Vulkan }`), plus each
game's `local` path:

| Id / menu name | Tech | Notes |
|---|---|---|
| `local` | The game's own renderer (gd01/gd04 direct GL, gd02 `RenderDevice`) | Always present; the fallback. |
| `opengl3` / OpenGL3 | OpenGL 3.3 core / GLES 3.0 | Universal fallback. **Not offered on macOS** (Apple GL deprecated). |
| `sdl3` / SDL3 | SDL3 GPU API (Vulkan / D3D12 / Metal under one API) | The intended universal backend. |
| `vulkan` / Vulkan | Raw Vulkan (+ MoltenVK on Apple) | Max control; the only backend with True-HDR swapchains today. |

**Selection.** A game reads `graphics_backend` from settings. If it is not
`local`, the game calls `gd::core::render::parseBackend()` +
`backendCompiled()`, then `createDevice(DeviceCreateInfo{...})`
(`gd-game-01/engine/src/render/renderer.cpp:212`,
`gd-game-01/engine/src/render/core_renderer.cpp`). On any failure it logs
`graphics backend: <id>: <why>; using local` and reverts to `local`. The
window must be created for the chosen API first: `prepareWindow(backend,...)`
sets GL attributes / asks the launcher (via the SDL global property
`gd.render.window_api_request`, `gd_host.h` `GD_PROP_WINDOW_API_*`) for a
window made for that API; `createDevice` **refuses** a backend whose window is
wrong. A launcher module gives the window back in a usable state on return
(`engine-backend-guidelines.md` rule 9).

**Which games.** gd-game-01 was the first to expose the menus; gd-game-02..05
now also expose `graphics_backend` / `audio_backend` in Enhanced
(`enhanced_panel.cpp`), and gd-game-05 is designed to run on the core device
(Vulkan) directly. (`engine-backend-guidelines.md` rule 1, which says "only
gd-game-01 can select them", predates this fleet rollout.)

**HDR path.** `DeviceCreateInfo::hdrOutput` requests a True-HDR swapchain;
`Device::hdrSurface()` reports what it got — **0 = SDR, 1 = scRGB (linear,
1.0 = 80 nits), 2 = BT.2020 PQ (HDR10)** — and `hdrReason()` says why it is
SDR. HDR has three user modes (`universal_mechanisms.md` "HDR"): **None**,
**Emulated**, **True HDR**, applied at the next start. None and Emulated are
pixel-identical to the SDR original; True HDR maps the original look to
**paper white** (reference 203 nits; gd01/gd02 store `hdr_white` = 203 and
`hdr_black` = 0) and lets only highlights use the headroom.

True-HDR support by backend (today, `GDCORE-HDR_KICKOFF.md` §2, `device.h`):

| Backend | True HDR |
|---|---|
| Vulkan | **Yes** — `VK_EXT_swapchain_colorspace` HDR10/scRGB swapchain, live rebuild, metadata (GD01-318 / commit #80). The comment in `device.h` notes `hdrOutput` is honoured by Vulkan only. |
| `local` (game's own GL) | Yes via the game's own surface code — gd01: WGL scRGB (Windows), Wayland EGL scRGB/PQ (Linux), Vulkan through #80; gd02's local GL path copied into gd03/04/05. |
| SDL3 GPU | Planned; currently True HDR runs as **Emulated** and logs the reason. |
| OpenGL3 (core) | SDR only (True HDR would be start-up only; planned to move the EGL/WGL code into the core). |

The HDR **study** (`study_hdr.md`) and **kickoff** (`GDCORE-HDR_KICKOFF.md`)
describe one shared core HDR compose pass for every backend/game; the
four-slider menu (Peak / Paper White / SDR Gamma / Highlight Boost) is a
**proposal pending the owner's yes** — the shipped menu today is the single
None/Emulated/True-HDR option plus the paper-white / black-level numbers.
Steam Deck OLED under gamescope uses an Auto preset: **peak 1000 nits, paper
white 203, gamma 2.2**.

## Audio backends

The core output backends are `miniaudio` (default; playback/mix/resample in
one header, every OS), `OpenAL` Soft (3D/HRTF; LGPL, shipped as a separate
shared lib), `FAudio` (XAudio2/XACT reimplementation, for titles that used
XAudio2), plus each game's `local` mixer. Separately, each game **decodes its
disc's own formats** to PCM before the output backend plays them:

| Game (disc) | Song audio | SFX / other | Decoder files |
|---|---|---|---|
| gd-game-01 — Guitar Hero (PS2) | Multitrack **VGS** (PS-ADPCM) | PS2 **SPU PS-ADPCM** sound banks | `audio/{spu,sound_bank,adpcm,synth,mixer}.cpp` |
| gd-game-02 — Guitar Hero II (Xbox 360) | **Ogg Vorbis `.mogg`** via `stb_vorbis` (`third_party/stb_vorbis`) | **XMA2** via FFmpeg; WMV video via FFmpeg | `audio/{vorbis_stream,xma_decoder}.cpp`, `video/wmv_player.cpp` |
| gd-game-03 — Encore: Rocks the 80s (PS2) | Multitrack **VGS** (PS-ADPCM), copied from gd01 | PS2 SPU PS-ADPCM | `audio/{vgs_stream,sample_decode,spu_reverb,adpcm}.cpp` |
| gd-game-04 — Guitar Hero III (Xbox 360) | **FSB** (`.fsb.xen`) three stereo **XMA1** stems via `SongStream` + FFmpeg | **XMA2** via FFmpeg | `audio/{song_stream,xma_stream,xma_decoder}.cpp`, `data/fsb.cpp` |

**FFmpeg's role.** XMA1/XMA2 decode and WMV video go through FFmpeg's LGPL
shared libraries (`libav*`), the same ones the videos load. They are gated by
a build flag (`-DGD04_VIDEO=ON` in gd04; equivalent in gd02). **Without
FFmpeg, `xma::available()` is false and XMA decode fails** — the song will not
play (`gd-game-04/engine/src/audio/xma_decoder.h`). A non-video dev build
therefore has no XMA; use the release-shaped build (video ON) when validating
360 song playback.

**Disc reading (CHD/ISO).** Games do not read raw sectors themselves for
user-chosen files: `gd-engine-core/platform/` reads ISO 9660, UDF and XISO
images and **CHD** (made with `chdman createdvd`, GDUL-030) behind the
launcher's disc handles. Open user paths with `SDL_IOFromFile`, or `_wfopen`
of the UTF-8 path on Windows (each game's `RandomFile::open`); a CHD opens
through the core's `RandomAccessFile::open`, which takes every path kind.
**Never** a bare `fopen` / `std::ifstream` of a UTF-8 string on Windows
(it reads the ANSI code page and fails on accented/non-ASCII paths —
`usual-bugs.md`).

## Default settings per OS

Grounded in the live code (`gd-game-01/engine/src/app.cpp`,
`gd-game-02/engine/src/ui/enhanced_panel.cpp`, wave 20261118 §4, which
supersedes GDALL-050). Defaults are per **fresh** `enhanced.json`
(`settings_version` 3); a player's own saved choices always win. `local` is
always selectable and is the fallback if the chosen backend cannot start.

| OS | Graphics default | Window mode | Max FPS / pacing | HDR default | Internal res / UI | Audio default | Handheld profile |
|---|---|---|---|---|---|---|---|
| **Windows** | `vulkan` | Borderless (full-screen no border) | 120, Vsync present mode | None (True HDR available on Vulkan; D3D12 HDR via SDL3 planned) | 1080p, sharp UI on, bloom off, FPS counter on | `miniaudio` (WASAPI) | n/a |
| **Linux (desktop)** | `vulkan` | Borderless | **60**, Vsync | None (True HDR on Vulkan under a Wayland HDR compositor) | 1080p, sharp UI on, bloom off | `miniaudio` (PipeWire/PulseAudio/ALSA) | n/a |
| **Steam Deck** | `vulkan` | Steam Deck mode → true **fullscreen** 1280×800 | 60, Vsync | None (True HDR on Deck OLED via gamescope Vulkan WSI; Auto preset 1000/203/2.2 nits) | Steam Deck mode → 800p, FXAA, sharp UI | `miniaudio` | **Yes** — `GD_HOST_HANDHELD`, detected by `SteamDeck=1` env or DMI board `Jupiter` (LCD) / `Galileo` (OLED); touch + shake-whammy on, 60 FPS, lighter effects (nothing locked) |
| **macOS** | `sdl3` | Borderless | 120 | None (True HDR via SDL3 GPU currently **Emulated**; local-GL EDR planned) | 1080p, sharp UI on, bloom off | `miniaudio` (CoreAudio) | n/a |
| **Android** | `vulkan` (falls back to `local` GLES 3.0 if core Vulkan can't start) | Full-screen (platform) | 60 | None (**not implemented** on Android) | 1080p requested (panel-capped) | `miniaudio` (AAudio / OpenSL ES) | **Yes** — always handheld: touch + shake-whammy on, 60 FPS |

Shared fleet defaults, every OS (wave 20261118 §4): FPS counter on,
screen mode borderless, internal res 1080p, sharp UI on, bloom off, HOPO
original; touch and shake-whammy off on desktop, on for handhelds. Audio was
`local` before this wave (GDALL-050); it is now `miniaudio`, and an older file
whose backend is `local` is migrated once to the new core default.

**Unknowns / not-yet-real (do not assume):**
- There is **no macOS or Android CI runner proven** yet (`engine-backend-guidelines.md` rule 7 "macOS when its runner is used"; `study_macos_trust.md` "no Mac to test on yet"). macOS/Android defaults are the coded intent, **untested on real hardware**.
- Android Vulkan is unproven on cheap phones; the GLES `local` fallback is why it is kept.
- HDR True on SDL3 GPU and OpenGL3 is **Emulated today** (planned, not shipped); HDR on macOS EDR and Android is **not implemented**.
- iOS, Web (WASM) and consoles are **planned targets with no build** (`engine-backend-guidelines.md` §2 "this table is the plan").
- Some games (gd02/03/04 app.cpp) still initialise the audio variable to `local` before loading settings; the fresh-install default a player actually gets is `miniaudio`.

## Onboarding notes

A new game module must:

1. **Keep `local` working and default-safe.** Ship the game's own renderer and
   mixer; every core backend that fails must fall back to `local` with a log
   line (never a black screen / silence).
2. **Expose the two Enhanced > Performance dropdowns** with the platform's
   allowed ids only: graphics `{vulkan, local, sdl3, opengl3}` (no `opengl3`
   on macOS; macOS leads with `sdl3`), audio `{miniaudio, local, openal,
   faudio}`. Set the per-OS default from the shared helper
   (`defaultGraphicsBackend()` = the platform list's first entry) and migrate a
   pre-wave `local` once (`enhanced_panel.cpp` / `app.cpp`).
3. **Create the window for the chosen API** via `prepareWindow` before
   `SDL_CreateWindow`, read the launcher's window-API properties, and give the
   window back on exit. Read the backend **once at start-up**; a change applies
   next start or via Restart Now (`GD_GAME_EXIT_RESTART`).
4. **Decode the disc's own audio** into PCM for the output backend. If the
   format is XMA, wire FFmpeg behind the video/audio build flag and gate on
   `available()`.
5. **Pass the shared shaders through the build-time pipeline** (GLSL →
   SPIR-V) for SDL3/Vulkan; `local`/OpenGL3 use the GL dialect of the same
   source (`device.h` `ShaderDesc`, `docs/render.md` §3).
6. **Provide a headless self-test** per backend (offscreen / null device); a
   backend is "done" only when the game plays a song on it on Linux and the
   package for every platform contains it.

**Known pitfalls (check before anything ships):**
- **"Too dark" band/menus/venue (recurring, never fix by raising global
  brightness).** The Xbox 360 display-gamma ramp `709(sRGB⁻¹(v))` is validated
  against a real capture. Darkness is always a lighting-wiring or self-shadow
  bug (character shader lighting registers not driven by the venue group
  lights; band receiving its own shadow). Compare mean face/menu luma against
  the reference numbers objectively, never by eye (`usual-bugs.md`).
- **Android `import std` build break.** The Android build fails on
  `clang-scan-deps` of the host GCC `std.cc`; disable `CXX_MODULE_STD` / module
  scanning / `VULKAN_HEADERS_ENABLE_MODULE` in the Android CMake and OpenAL
  args.
- **No-FFmpeg builds stall XMA.** A dev build with video/FFmpeg OFF makes
  `xma::available()` false, so 360 songs (gd02 SFX, gd04 stems) do not decode —
  validate song playback on a release-shaped build.
- **Windows UTF-8 disc paths.** Never `fopen`/`std::ifstream` a UTF-8 string on
  Windows; use `SDL_IOFromFile` / `_wfopen` / the core `RandomAccessFile`.
  Test an ISO **and a CHD** on every platform, including paths with spaces,
  accents and brackets, before release.
- **Black frames between screens.** A render target created mid-frame can
  unbind the frame's target; use the `probe:black` / `check:no_black` walks on
  new transitions.
- **Launcher released without a module.** Every package must end with the
  `verify_bundle.sh` line listing every game module with its libraries.

## Sources

- `docs/guidelines/engine-backend-guidelines.md` — the backend catalogue, per-OS plan, core layout, rules 1–10, Enhanced menu dropdowns.
- `docs/guidelines/usual-bugs.md` — too-dark lighting rule, UTF-8/CHD disc reading, black-frame transitions, missing-module packaging.
- `docs/spikes/study_backends.md` — SDL_GPU vs raw Vulkan vs bgfx; shader pipeline; launcher shared-window API switching.
- `docs/spikes/study_hdr.md`, `docs/spikes/GDCORE-HDR_KICKOFF.md` — HDR surfaces (SDR/scRGB/PQ), paper-white 203, Auto presets, per-backend True-HDR status, the (proposed) slider menu.
- `docs/spikes/study_steam_deck.md` — run-time Deck detection, handheld profile, gamescope HDR WSI.
- `docs/spikes/study_macos_trust.md`, `docs/spikes/study_windows_trust.md` — distribution/signing state; "no Mac to test yet", Windows cross-compile.
- `gd-engine-core/render/include/gd/core/render/device.h` — `Backend` enum/ids, `DeviceCreateInfo::hdrOutput`, `hdrSurface()`/`hdrReason()`, `createDevice`, window-API properties.
- `gd-game-01/engine/src/app.cpp` — fresh-install defaults, `kDefaultGraphicsBackend`, `profileValues` (per-OS FPS/window/handheld), settings migration (wave 20261118 §4).
- `gd-game-01/engine/src/render/renderer.cpp`, `.../render/core_renderer.cpp` — backend selection + `local` fallback; HDR surface handling.
- `gd-game-02/engine/src/ui/enhanced_panel.cpp` — per-OS graphics/audio backend value lists, `defaultGraphicsBackend()`, audio = miniaudio.
- `gd-game-0{1,2,3,4}/engine/src/audio/*` + `gd-game-04/engine/src/data/fsb.cpp`, `.../platform/module_songs.cpp` — per-game decoders (VGS/PS-ADPCM, Vorbis `.mogg`, XMA1/XMA2 via FFmpeg, FSB SongStream).
- `gd-engine-launcher/sdk/gd_host.h` — `GD_HOST_HANDHELD`, `GD_HOST_FULLSCREEN`, `GD_GAME_EXIT_RESTART`, window-API properties.
