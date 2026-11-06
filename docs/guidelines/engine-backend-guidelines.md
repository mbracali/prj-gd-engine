# Engine backend guidelines (gd-engine-core)

The owner's 1.1.13 requirements (2026-11-06) for the **universal backends**:
one set of graphics, image, audio, platform, input and UI libraries that every
game module can load, instead of each game carrying its own copy. This file is
the reference for that work; [universal_mechanisms.md](universal_mechanisms.md)
points here for the backend options, and the backlog tracks it under
[gd-engine-core](../tasks/gd-backlog.md#gd-engine-core).

**This is foundational.** The core will serve every decompiled game (PS2 and
Xbox 360 today, other source platforms later), every target we recompile to
(desktop, handheld, mobile, TV, web, consoles), and later our own engine.
Design for extension, never for one game.

## 1. The backends

Every game offers these in its Enhanced menu (section 4). The "In game option
name" column is the exact text shown in the menus and the id saved in the
settings (lower case in files: `sdl3`, `vulkan`, `opengl3`, `stb`, `bcdec`,
`miniaudio`, `openal`, `faudio`).

| Area | Tech | Language / licence | Platforms | Why | In game option name |
|---|---|---|---|---|---|
| **Graphics** | **SDL3 GPU API** | C, zlib | Win, Linux, macOS, iOS, Android, consoles (NDA ports) | One API over Vulkan, Metal and D3D12: the most practical universal backend | SDL3 |
| | **Vulkan** (+ MoltenVK on Apple) | C, Apache 2.0 | Win, Linux, Android, macOS / iOS (MoltenVK) | Maximum control and performance; verbose, so behind our own thin wrapper | Vulkan |
| | **OpenGL 3.3 / GLES 3.0** | C, open standard | Everywhere (deprecated on Apple, still works) | Easiest fit for games written against fixed-function or GL renderers; the fallback | OpenGL3 |
| **Image** | **stb_image / stb_image_write / stb_image_resize2** | C, single header, public domain | Anywhere | PNG, JPG, TGA, BMP, PSD, HDR load / save / resize, no dependencies | STB |
| | **bcdec + KTX-Software (libktx)** | C, MIT / Apache 2.0 | All | Decodes BCn / DXT, ETC, ASTC (ripped textures); KTX2 container for repacking | BCDEC |
| | **SDL3_image** | C, zlib | All SDL targets | Convenience layer (PNG, JPG, WebP, AVIF, QOI...) | SDL3 |
| **Audio** | **miniaudio** | C, single header, public domain / MIT-0 | Win, Linux, macOS, iOS, Android, Web | Playback, capture, mixing, effect graph, resampling, decoding in one header: the default | miniaudio |
| | **OpenAL Soft** | C, LGPL | All major OSes | 3D positional audio with HRTF and EFX; for games that used DirectSound3D / OpenAL | OpenAL |
| | **FAudio** | C, zlib | All SDL targets | XAudio2 / XACT reimplementation: decompiled Windows / Xbox code keeps its audio code | FAudio |

Every game also offers **local**: the game's own current implementation. It is
the default everywhere until the owner changes a default after testing.

## 2. Default per OS (future-proof)

- **Real per-OS split:** desktop GPUs use BCn / DXT (what most ripped PC /
  Xbox textures already are); mobile uses ASTC / ETC2. Either decode BCn to
  RGBA at load time on mobile (simple, more memory) or transcode through
  KTX2 / Basis as an offline asset step.
- **One graphics interface, two main implementations:** SDL_GPU first,
  GLES 3 / GL 3.3 as the fallback. That covers every target below except
  consoles.
- **miniaudio** covers every OS with no extra work; FAudio only per game, when
  the original used XAudio2.
- Not every OS has a build yet; this table is the plan.

| OS | Graphics | Image / textures | Audio | Gotchas |
|---|---|---|---|---|
| **Windows** | SDL_GPU (D3D12 or Vulkan) | stb_image + BCn native | miniaudio (WASAPI); FAudio if the original used XAudio2 | Easiest target; D3D-native paths can stay for debugging against the original |
| **Linux** (incl. Steam Deck) | SDL_GPU (Vulkan) | stb_image + BCn native | miniaudio (PipeWire / PulseAudio / ALSA) | GL 3.3 must keep working as the fallback (old Intel iGPUs, odd drivers) |
| **macOS** | SDL_GPU (Metal) | stb_image + BCn native (Apple Silicon) | miniaudio (CoreAudio) | **No OpenGL** (deprecated, frozen at 4.1); raw Vulkan only through MoltenVK; notarisation / signing to distribute |
| **iOS** | SDL_GPU (Metal) | **ASTC** (no BCn): transcode or ship separately | miniaudio (CoreAudio / AVAudioSession) | Audio session (interruptions, silent switch); App Store rules on bundled game data |
| **Android** | SDL_GPU (Vulkan) + **GLES 3.0 fallback** | **ASTC / ETC2** (BCn rare) | miniaudio (AAudio, OpenSL ES fallback) | Cheap phones' Vulkan drivers are buggy: the GLES fallback matters; audio latency |
| **Web** (WASM) | WebGL2 through the GLES 3 path (or sokol for WebGPU) | stb_image; ETC2 / ASTC / BCn per browser: Basis / KTX2 helps | miniaudio (Web Audio) | No blocking main loop (async loop); audio starts after user input; check SDL_GPU's web backend then |
| **Consoles** | Platform SDK (SDL NDA ports) | Platform-native | Platform SDK / FAudio on Xbox | Devkit and NDA; keep the backend interface clean so they drop in later |

## 3. The core: `gd-engine-core/`

At the repository root. Every game loads common code from here.

| Folder | What it holds |
|---|---|
| `gd-engine-core/platform/` | Shared paths (save / settings folders per OS), file IO, ISO access (ISO 9660, UDF, XISO, the launcher's disc handles), Android data (SAF / content URIs) |
| `gd-engine-core/inputs/` | Controls, bindings, gamepads, guitars, MIDI devices |
| `gd-engine-core/audio/` | The audio backend interface and its backends, the mixer, DSP |
| `gd-engine-core/render/` | The renderer interface and its backends (SDL3 GPU, Vulkan, OpenGL3) |
| `gd-engine-core/image/` | Image and texture wrappers (stb, bcdec, libktx, SDL3_image) |
| `gd-engine-core/ui/` | Shared UI components; the universal menus build on these |
| `gd-engine-core/third_party/` | Vendored single headers with their licences; `cmake/` pins the fetched libraries |
| `gd-engine-core/docs/` | The core's own design notes (decisions, interfaces, how to add a backend) |

### Rules

1. **Not every game moves at once.** The backends are developed to this
   specification, shaped by the four implemented games (gd-game-01..04), and
   adopted game by game. Today only gd-game-01 can select them.
2. **`local` is always there and always the default.** Choosing a core
   backend never removes the game's own path; a backend that fails to start
   (missing driver, missing library) falls back to `local` and says so in the
   log. A player must never be left with a black screen or no sound.
3. **One interface per area, many implementations.** A game talks to an
   interface (`gd::core::render::Device`, `gd::core::audio::Output`,
   `gd::core::image::Decoder`); backends live behind a factory keyed by the
   ids above. Adding a backend never changes a game.
4. **Backends are chosen at start-up** from the settings; a change in the
   Enhanced menu applies on the next start of the game (the menu says so),
   or at once with the Restart Now pop-up ([universal-menus.md](universal-menus.md), "Backends").
5. **Plain C++17, no exceptions across the interface, no game types in the
   core.** The core knows nothing of PS2, Xbox, GH or any one game; games
   adapt to it. C APIs stay behind the backends.
6. **Third-party code:** single headers are vendored in `third_party/` with
   their licence files; larger libraries are fetched by CMake at **pinned
   versions** (exact tags, at least two weeks old) and built for every
   platform. LGPL code (OpenAL Soft) ships only as a separate shared library
   next to the module, loaded at run time, with its licence in the package.
   Never commit build output or binaries.
7. **Every platform builds:** Linux, Steam Deck, Windows, Android (macOS when
   its runner is used). A backend a platform cannot have (OpenGL3 on macOS,
   Vulkan where no loader exists) is simply not offered there.
8. **Shaders:** a backend that needs compiled shaders (Vulkan, SDL3 GPU)
   gets them from the same sources the GL path uses, compiled at build time
   (no shader compiler shipped to players unless a backend truly needs it).
   The exact pipeline is recorded in `gd-engine-core/docs/`.
9. **The launcher's window:** modules draw into the launcher's window
   (`gd-engine-launcher`, the module shims). A module on a non-GL backend
   must get that window in a state its backend can use, and give it back
   when it returns to the launcher.
10. **Tests:** each backend has a headless self-test where one is possible
    (offscreen / dummy audio / null device) and the game's walks run on
    `local` as before. A backend is "done" only when the game plays a song
    on it on Linux and the package for every platform contains it.

## 4. In the games: Enhanced > Performance

Every game's Enhanced menu gets two dropdowns in its performance section:

- **Graphics backend:** `local`, `SDL3`, `Vulkan`, `OpenGL3` (only those the
  platform can run).
- **Audio backend:** `local`, `miniaudio`, `OpenAL`, `FAudio`.

Default `local` in every game. For now they are **selectable on gd-game-01
only**; the other games show them greyed at `local`. The owner tests the
backends on gd-game-01 and then says which defaults change.

## 5. Be aware

- More source platforms are coming, not only PS2 and Xbox 360.
- More target platforms too: keep platform code in `platform/` and behind the
  interfaces.
- These components will be reused across many decompilation projects and in
  our own engine at the end of the project.
