# gd05 on gd-engine-core (GD05-074)

The owner's G06 section 7 (agents/WAVE_G06_REQUIREMENTS.md): Rock Band is the
first game that runs only on the shared core (`gd-engine-core/`), Vulkan by
default, without changing any other game. Decision: DECISIONS.md D71. The
core's own notes: `gd-engine-core/docs/render.md` section 6e,
`gd-engine-core/docs/audio.md`.

## Inventory (2026-11-12, before the move)

What gd05 had of its own beside the core, and what bypassed the core's
interfaces. "Core" is the gd-engine-core area that covers it.

| Area | gd05's own code | Core | State |
|---|---|---|---|
| Graphics | `render/gl_device.cpp` ("local": gd05's OpenGL 3.3 / ES 3 device, the default), `render/gl_api.{h,cpp}` (its GL loader), `gfx::prepareWindow()` (GL attributes) | `gd::core::render::Device` (vulkan, sdl3, opengl3) under `render/core_device.cpp` | **Moved**: core only, Vulkan default, then SDL3, then OpenGL3; local deleted |
| True HDR | `render/hdr_output.{h,cpp}` (an EGL / WGL RGBA16F surface probed with a GL context) and GlDevice's conversion pass | `DeviceCreateInfo::hdrOutput` (Vulkan's HDR swapchain: PQ, scRGB) | **Moved**: CoreDevice asks for the HDR swapchain and runs the same conversion shader; sdl3 / opengl3 say "not on this backend yet" and run Emulated |
| Audio output | `audio/audio_device.cpp`'s SDL device path ("local", the default) | `gd::core::audio::Output` (miniaudio, openal, faudio) | **Moved**: core only, miniaudio default, then FAudio / OpenAL; the SDL device path deleted. The streams, the sum and GH2's master limiter stay gd05's (game mixing, not output) |
| Microphone | `audio/vocal_input.cpp` | `gd::core::audio::Input` | Already on the core (miniaudio or sdl3 capture) |
| Disc image | `data/xdvdfs.{h,cpp}` (XDVDFS reader) | `gd::core::platform::openXdvdfs` / `DiscFileSystem` | **Moved**, deleted |
| Random-access files | `data/random_file.{h,cpp}` (wide-path fopen on Windows) | `gd::core::platform::RandomAccessFile` (also Android `content://` and the launcher's descriptors) | **Moved**, deleted |
| Pref folder | `platform/paths.cpp` (SDL_GetPrefPath) | `gd::core::platform::gamePrefDir` (the same folder; the shim's nested one on Android modules) | **Moved** (prefDir() is now a one-line wrapper) |
| BCn decoding (CPU) | `render/texture_codec.cpp`'s BC1 / BC3 / BC5 decoder (devices without BCn, the image enhancer) | `gd::core::image` `bcdec` `decodeBlocks` | **Moved** (levelSize / swapBytes16, the 360's layout helpers, stay) |
| Development tools' windows | `char_view`, `scene_view`, `venue_view`, `shadertest`, `wmv_player`, the launcher's model explorer (`module_explore.cpp`) made GL windows for local | the core's OpenGL3 backend | **Moved** (`prepareWindowFor(rendererBackend())`, OpenGL3 for the tools and the explorer, whose host is OpenGL-only) |
| Input (keyboard, gamepads, guitars, drums, virtual pads) | `input/input.cpp`, `input/input_selftest.cpp`, controllers.json | `gd-engine-core/inputs` is **planned only** (README, no code) | Remains gd05's: nothing in the core to move onto |
| MIDI input | `input/midi_in.cpp` + vendored RtMidi | planned in `inputs/` | Remains |
| UI | `ui/*` (Rock Band's own menus on GH2's UI engine) | `gd-engine-core/ui` is **planned only** | Remains (game UI; the universal menus' shared parts would move when the core has them) |
| Settings / save file IO | `std::ifstream` / `std::ofstream` + rename for enhanced.json, the career save, controllers.json, Export / Load User Files | `gd::core::platform::readFile` / `writeFileAtomic` / `openFile` | **Not moved yet** (works on Linux; on Windows a non-ASCII user folder is the "usual bug" risk the core's functions avoid) |
| Desktop entry / icon | `platform/app_icon.cpp` (its own XDG_DATA_HOME / HOME logic) | `gd::core::platform::xdgDataHome()` | Not moved yet (same result on Linux) |
| Disc picker | `platform/disc_picker.cpp` (SDL dialog, Android content URI copy) | none (the launcher's Load ISO is the launcher's) | Remains |
| Audio decoding | `audio/vorbis_stream.cpp` (stb_vorbis), `audio/xma_decoder.cpp` (FFmpeg), `audio/mogg.cpp` | none (codecs are not a core area) | Remains (game formats) |
| Launcher hooks | `platform/module_entry.cpp` (gd_game_run), `module_explore.cpp` | the window hand-over through `core::prepareWindow` / the shim's `request_window` | Uses the core's hand-over (unchanged) |

Nothing gd05 does now bypasses the core's render or audio interfaces: every
draw goes through `gfx::RenderDevice` -> `CoreDevice` ->
`gd::core::render::Device`, every sound through `audiodev` ->
`gd::core::audio::Output`.

## How the backend is chosen

- `graphics_backend` (Enhanced > Performance): `vulkan` (default), `sdl3`,
  `opengl3` (not on macOS). `GD05_GRAPHICS_BACKEND=<id>` overrides it.
  `render/core_backend.cpp backendsToTry`: the chosen one, then the others in
  the order Vulkan, SDL3, OpenGL3 (those this build has). `App::run` makes the
  window for each in turn (in the launcher the shim asks the launcher for a
  window made for that API) until a device starts; the log says
  `graphics backend: vulkan did not start (...)` and
  `making the window again for sdl3`.
- `audio_backend`: `miniaudio` (default), `openal`, `faudio`.
  `GD05_AUDIO_BACKEND=<id>` overrides it. The chosen one, then miniaudio,
  then the core's other backends; when none starts the game stops at
  start-up with "cannot open the audio device" (as before with SDL's).
- A saved `local` (or any id this build does not have) is dropped when
  enhanced.json is read: the default takes its place.
- The launcher passes no backend: the module reads its own enhanced.json.

## Checks

- Pixels: `agents/scratch/be6/shots.sh` (git-ignored) takes song shots with a
  fixed clock (a test-only LD_PRELOAD: SDL's clock advances 1/60 s per
  drained event queue; no audio pulled, so the song clock follows it). Two
  runs of one binary give 0 differing pixels. PROGRESS.md has the results.
- Walks: the gate (gate1.sh's walks + rb_highway, rb_crowd and the
  self-tests) on the default backends.
