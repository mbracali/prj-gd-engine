# gd-game-02: what to reuse from gd-game-01, and what we learned there

gd-game-02 (program `gd02`) reimplements Guitar Hero II (Xbox 360). It is a
**fresh engine**, written for its own game. This document lists what
gd-game-01 (`gd01`, Guitar Hero PS2, released up to 1.1.3) can give it: code
we can copy (and later move into `gd-core/`, see `docs/adding-a-game.md`,
"rule of two"), the build and release pipeline, and above all the lessons
that cost time in gd-game-01.

Paths starting `gd-game-01/` are in this repo. "GH1" = Guitar Hero (PS2),
"GH2" = Guitar Hero II (Xbox 360). Anything about GH2 that is not checked yet
is marked **(Q)**. GH2 facts marked **(seen)** were checked on the extracted
disc (`gd-game-02/extracted/`, local only).

---------------------------------------------------------------------------

## 1. Module inventory of `gd-game-01/engine/src`

Verdicts:

- **reuse as is**: game-independent. Copy it, rename `gd01`/`GD01_` to
  `gd02`/`GD02_`, and it works. First candidates for `gd-core/`.
- **adapt**: the same idea and often the same structure, but GH2's formats or
  rules differ. Use the gd01 code as a reference and a skeleton, and verify
  every format and constant against GH2.
- **GH1 only**: PS2-specific. Leave it behind.

The engine is about 26,800 lines of C++20 (`wc -l` of the `.cpp` files). The
largest files are `ui/ui_system.cpp` (2813), `app.cpp` (2462),
`ui/select_panels.cpp` (1565), `scene/character.cpp` (1205),
`render/post_fx.cpp` (1097), `render/renderer.cpp` (1069) and
`input/input.cpp` (1049).

### 1.1 `platform/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `platform/paths.{h,cpp}` | `prefDir()`: the save folder (`SDL_GetPrefPath("gd-engine","gd01")`). Also `aliasLegacyEnv()` (GH1RE_* → GD01_*) and the move from the old `gh1re` folder. | **reuse as is**. Set it to `gd-engine/gd02`. Drop the gh1re migration and the env alias: gd02 has no legacy names. |
| `platform/gpu_select.{h,cpp}` | Puts the game on the dedicated GPU on hybrid laptops. Windows: exports `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance`. Linux: PRIME offload / `DRI_PRIME`. If graphics fail to start, it restarts on the system GPU and remembers that in `gpu_system`. `GD01_GPU=system/testfail`. | **reuse as is** |
| `platform/app_icon.{h,cpp}` | The window icon, from the PNG built into the program (`cmake/embed_file.cmake`); also the logo on the first screen. | **reuse as is**, with new symbols (`kGd02IconPng`). |
| `platform/android_data.{h,cpp}` | Android: picks the `.iso` with the system file picker and copies it into `Android/data/<pkg>/files/`, because `content://` URIs cannot be read as plain files. | **reuse as is**, with the package renamed. Note: a GH2 360 image is far larger than GH1's 2.2 GB **(Q: check the size; the copy may need a progress screen and a free-space check)**. |

### 1.2 `data/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `data/bytes.h` | `ByteReader`: a bounds-checked reader with **both little- and big-endian** reads. | **reuse as is**. Xbox 360 data is mostly big-endian **(Q, per format)**. |
| `data/inflate.{h,cpp}` | Our own raw DEFLATE decoder (RFC 1951). | **reuse as is**. GH2's `main.hdr` entries have an "inflated size" field (see `gd-game-02/tools/ark.py`), and Milo files are block-compressed **(Q: zlib or raw deflate)**. |
| `data/json.{h,cpp}` | A small JSON value with parse and write, used for the saves, `enhanced.json` and `controllers.json`. | **reuse as is** |
| `data/midi.{h,cpp}` | Standard MIDI file parser (events, meta texts, tempo map input). | **reuse as is**. GH2 charts are type-1 MIDI at 480 ticks/beat (seen). |
| `data/iso9660.{h,cpp}` | `RandomFile` (64-bit random reads) and an ISO 9660 reader. | **adapt**. Keep `RandomFile`. Replace ISO 9660 with XDVDFS, ported from `gd-game-02/tools/xdvdfs.py` (360 discs: XGD2, game partition offset, "MICROSOFT*XBOX*MEDIA"). |
| `data/game_data.{h,cpp}` | `GameData`: opens a `.iso` or an extracted folder and reads files out of `GEN/MAIN.HDR` + `MAIN_n.ARK`. | **adapt**. GH2's `gen/main.hdr` is Park-Miller encrypted, has an inflated-size field per entry, and its paths include `../../system/run/...` (see `gd-game-02/tools/ark.py`). Keep the class's interface (`exists`, `read`, `description`). |
| `data/dta.{h,cpp}` | Decodes a serialised DataArray (`.dtb`) into `DataNode` trees, with GH1's `rand()`-LCG decryption. | **adapt**. The node model (Int, Float, Var, Symbol, Array, Command, Property, Define, Include, Merge, IfDef...) is Harmonix's and should carry over. **(Q: GH2's `.dtb` encryption and endianness; GH1 used the C `rand()` LCG, GH2's hdr uses Park-Miller.)** |
| `data/vgs.{h,cpp}` | Streaming decoder for `.vgs` (PS-ADPCM with the channel id in byte 1). | **GH1 only**. GH2 uses `.mogg` (seen). |
| `data/bitmap.{h,cpp}` | `Image` (RGBA8) and the decoder for `.bmp_ps2` / `.png_ps2` (PS2 palettes, alpha 0..0x80, CSM1 order, RGBA5551). | **GH1 only** for the decoder. Keep the `Image` struct. GH2 needs `.bmp_xbox` / `.png_xbox` (2468 + 1065 files, seen): **(Q: DXT1/DXT5 with Xbox 360 tiling and 16-bit endian swap, the usual 360 layout)**. |

### 1.3 `audio/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `audio/mixer.{h,cpp}` | Plays a song's multitrack audio and owns the **song clock**. Per-track volume and pan. Resamples every stream to the device rate. Has a second voice for previews and menu music, the "slip voice" (a whammy pitch delay line and the miss mute), and face-off guitar copies. | **reuse, after one change**. The logic is game-independent, but `mixer.h` includes `data/vgs.h` and `audio/spu.h`. Put a stream-source interface between them (a `.mogg` Vorbis source for GH2) and the SFX output, then copy it. |
| `audio/adpcm.{h,cpp}` | SPU2 PS-ADPCM sample decoder. | **GH1 only** |
| `audio/spu.{h,cpp}` | A software SPU2: 48 voices, ADSR, 4.12 pitch, pan law, echo and reverb buses. | **GH1 only**. The voice-mixing idea is generic, but GH2's sound effects come from Xbox 360 Synth objects. **(Q: the sfx format in `sfx/` and in `.milo_xbox`; possibly XMA or PCM.)** |
| `audio/sound_bank.{h,cpp}` | `.bnk` / `.nse` / `.seq` banks. | **GH1 only**. GH2 has no `.bnk` files (seen). |
| `audio/synth.{h,cpp}` | The `synth` object: bank slots, `play_sfx` with every layer of a name, sequences. | **adapt**. The script-facing behaviour (`play_sfx`, `play_sequence`, the volumes) is the same idea; the data behind it is different. |
| `audio/crowd_audio.{h,cpp}` | Venue crowd loops by excitement (`arena/venues.dta`), plus the intro and wingame reactions. | **adapt** **(Q: GH2's crowd stream files and their config)**. |

### 1.4 `game/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `game/chart.{h,cpp}` | Builds gems from `T1 GEMS`: chords within 10 ticks, no sustain under 161 ticks, the HOPO rule (single fret, no fret shared with the previous gem, ≤169 ticks), Star Power phrases, player sections. | **adapt**. Seen in GH2's `freebird.mid`: tracks `PART GUITAR`, `PART GUITAR COOP`, `PART RHYTHM` (other songs: `PART BASS` **(Q)**), `BAND BASS/DRUMS/SINGER/KEYS`, `EVENTS`, `TRIGGERS`. The notes are 60–64, 72–76, 84–88, 96–100 (frets per difficulty) and 67/79/91/103 (Star Power), the same layout as GH1, plus 41–59 (left-hand positions) **(Q)** and 110 **(Q)**. The HOPO threshold and chord rules must be re-verified in GH2's code. |
| `game/player.{h,cpp}` | Port of the "watcher": the hit window (slop), strum grace, HOPO window −40..+160 ms, joypad mode, scoring, multiplier, Star Power, rock meter, sustain accrual, whammy, and the events used by tutorials and the venue. | **adapt**. Harmonix's beatmatch code is likely close, but every constant and branch must come from GH2's executable and `config/gen/beatmatcher.dtb` / `scoring.dtb` / `coop_max_scores.dtb` (seen). Keep the gd01 port's shape: one `Player`, events out, no rendering inside. |
| `game/rules.{h,cpp}` | Reads `scoring.dta` (points, streaks, Star Power, crowd ranges per difficulty). | **adapt** (new fields for bass, co-op and practice **(Q)**). |
| `game/autoplay.h` | Perfect input for a chart, with HOPOs. Used by `--autoplay` and `--selftest`. | **reuse as is**. It needs only `Player`'s API. |
| `game/song_db.{h,cpp}` | `songs.dta` → `SongInfo` (name, artist, audio path, pans, band lineup). | **adapt**. GH2's `songs.dtb` (175 KB, seen) describes a `.mogg` with track→channel maps, pans and volumes, a `_coop.mogg` (seen) and the parts. |
| `game/campaign.{h,cpp}` | Career: 8 profiles, per-difficulty careers, tiers, unlocks, cash, store, high scores, JSON save. | **adapt**. GH2 adds encores, co-op career and different tiers and store contents **(Q)**. Keep the JSON save design. |
| `game/venue_script.{h,cpp}` | Runs the venue script, the lighting by excitement × section (switched on downbeats), `switch_anim` slots and the script natives. | **adapt** (GH2 `world/<venue>/` milo files and their dta **(Q)**). |
| `game/venue_camera.{h,cpp}` | The shot director: intro shot, weighted pools, solo shots, durations in bars, shaky cam, `post_switch_cam`. | **adapt** **(Q: GH2's camera director and shot data format)**. |
| `game/guitar_anim.{h,cpp}` | Guitarist hands: `bone_fret` on the neck, finger and strum clips from the gems. | **adapt**. The GH2 chart's notes 41–59 look like hand-position notes (seen), which may drive this more directly **(Q)**. |
| `game/guitar_walk.{h,cpp}` | The guitarist's walks between spots, root motion and a standing guard. | **adapt or drop** **(Q: whether and how GH2 characters walk)**. |

### 1.5 `input/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `input/input.{h,cpp}` | Every controller through SDL3. Table-driven bindings for 16 controls, per-device overrides and players in `controllers.json`. Controller types (gamepad / PS2 guitar adapter / Xbox guitar / Rock Band guitar). Analog whammy measured from its rest value. Touch (lanes, chords within 30 ms, swipes, on-screen buttons). Lefty flip. Hold/repeat. The same press from two devices is counted once (`kSameNs` 80 ms). `controllers-report.txt`. | **reuse as is**. Only the defaults change: the Xbox 360 guitar becomes the primary controller, and the GH1 joypad fret order (L2 L1 R1 R2 Cross) becomes whatever GH2 360 uses on a pad **(Q, from GH2's `joypad.dtb` / `beatmatch_controller.dtb`)**. |

### 1.6 `render/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `render/gl.{h,cpp}` | Loads the OpenGL 3.3 / GLES 3.0 entry points through SDL (no loader generator). | **reuse as is** |
| `render/math.h` | `Mat4`, vectors. | **reuse as is** |
| `render/renderer.{h,cpp}` | GL state, blend modes, textures and meshes, the off-screen scene target at the internal resolution, ultrawide (`beginWide`), vsync and the frame limiter, **at most two frames in flight (fence ring)**, the GLES path (`GD01_GLES=1`), and the fallback when post-processing fails to compile. | **reuse the core**. The PS2 VU1 lighting shader inside it is **GH1 only**: GH2 shades per pixel with HLSL (`system/run/shaders/lighting.fx`, seen). Write our own GLSL for that. |
| `render/post_fx.{h,cpp}` | The port's post chain: dual-Kawase bloom, fog / Super Fog, colour grading, film grain, chromatic aberration, light streaks, AO, sharpening, FXAA/SSAA/TAA, Star Power blur, bokeh. | **reuse as is** (enhanced, opt-in). GH2 has its own bloom and depth of field (`postprocess.fx`, `bloom.fx`, seen), which should become the *original* look, with ours on top. |
| `render/vk_upscaler.{h,cpp}`, `vk_upscaler_stub.cpp` | FSR 3.1 and DLSS 4 in Vulkan beside GL (memory-object and semaphore interop, motion vectors from depth). The stub is for Android. | **reuse as is** |
| `render/image_enhance.{h,cpp}` | "Sharper Text & Sprites" (Catmull-Rom ×2/×4, alpha re-tightened). | **reuse as is**. Probably less needed: 360 textures are already HD. |
| `render/highway.{h,cpp}` | A simple 2D placeholder highway and HUD (no game art). | **reuse as is** as the milestone-3 debug highway (see §4). |
| `render/text_layout.{h,cpp}` | RndFont glyph metrics measured from the sheet's alpha, RndText layout. | **adapt** (the GH2 font object in the milo files **(Q)**). |
| `render/scene_renderer.{h,cpp}` | Uploads a Scene and draws Views and Meshes in draw-list order, with transform owners, texgen, multi-stage materials and billboards. | **adapt** (to GH2's object classes and materials) |
| `render/track_renderer.{h,cpp}` | The original 3D highway from `track/gen/track.rnd_ps2`: gems, tails, smashers, the fade at the far end, lefty mirror. | **adapt**. GH2 has `track/` (textures and surfaces, seen). The lefty mirror and the per-frame tail geometry pool carry over. |
| `render/venue_renderer.{h,cpp}` | Puts the venue, band, crowd, camera, lights and particles together each frame. | **adapt** |
| `render/crowd_renderer.{h,cpp}` | The PS2 crowd: impostors rendered every frame, and 3D members near the stage. | **adapt / likely GH1 only** **(Q: GH2's `char/crowd` and how it is drawn)** |

### 1.7 `scene/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `scene/scene.{h,cpp}` | Decoders for `.rnd_ps2` object directories: Xfm, Mesh, Mat, Tex, View, Cam, Environ, Light, anims, UI classes. | **adapt**. The object model is Harmonix's Rnd, and GH2 keeps it. The container is new: `.milo_xbox` (503 files, seen), block-compressed and big-endian, with newer class revisions **(Q)**. Write a new loader. Keep the in-memory model where it fits. |
| `scene/anim.{h,cpp}` | `AnimPlayer`: RndAnimatable `SetFrame`, filters, child anims, Trans/Mat/Light/Env/Cam anims. | **adapt** (key formats per revision) |
| `scene/character.{h,cpp}` | Skeleton, GPU skinning (4 bones), clip blending, the AnimDriver graph, servos, face morphs, IK. | **adapt** **(Q: GH2's Character / CharClip classes; `.fac` and `.voc` files, seen, look like face and lip-sync data: `.voc` starts with "FACE" … "Harmonix")** |
| `scene/clip.{h,cpp}` | `.acp` clip samples. | **GH1 only** as a format (GH2's clips live in `char/*/anims/*.milo_xbox`, seen); the sampling idea carries over. |
| `scene/particles.{h,cpp}` | PsParticleSys simulation. | **adapt** |

### 1.8 `script/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `script/script.{h,cpp}` | `ScriptLoader` (decrypt, `#define`/`#include`/`#merge`/`#ifdef`, macro splicing) and `Interp` (variables, control flow, funcs, messages, handlers with parameter lists, `kDataUnhandled`, `switch`/`cond` defaults). | **reuse the interpreter, adapt the loader**. This is the most valuable piece of gd01: GH2's menus, venues and config are DTA scripts too (177 `.dtb`, seen, including `ui/gen/practice.dtb`). Only the decryption, the endianness and the new built-in verbs change. |

### 1.9 `ui/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `ui/ui_system.{h,cpp}` | The original menus driven by the original scripts: screens, panels, focus, navigators, message routing, transitions, help bar, native objects (`game`, `campaign`, `meta`, `song_provider`…). | **adapt**. The design carries over directly, and the natives differ. GH2's UI scenes are `ui/gen/*.milo_xbox` (seen) instead of `ghui/*.gh`. |
| `ui/panel.{h,cpp}` | A panel's scene: meshes, textures, LabelEx / ButtonEx / Text in the game's fonts. | **adapt** (milo panels) |
| `ui/ui_resources.{h,cpp}` | Fonts, text templates, styles (`config.dta`) and locale strings. | **adapt**. GH2 ships `ui/{eng,fre,deu,esl,…}/gen/locale.dtb` (seen). |
| `ui/select_panels.{h,cpp}`, `ui/game_panels.cpp`, `ui/tutorial_panel.cpp` | Native panel classes: character / guitar select, store, status, slider, credits, HUD, tutorials. | **adapt** (each one is GH1's class; GH2's are cousins) |
| `ui/career_objects.{h,cpp}` | Script natives for career and options (`needs_save`, setters). | **adapt** |
| `ui/gamepad_panel.{h,cpp}` | The port's own GAMEPADS screen. | **reuse as is** (depends on `Input` and the `SelectHost` interface) |
| `ui/pc_strings.{h,cpp}` | The port's wording over the console texts (memory card → PC saves, extra tips). | **adapt** (GH2's console texts: storage device, Xbox LIVE, achievements, gamer profiles **(Q)**) |

### 1.10 `video/`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `video/mpeg2.{h,cpp}`, `video/pss.{h,cpp}` | Our own MPEG-2 decoder and Sony PSS demuxer (intro and bonus videos). | **GH1 only**. GH2's videos are `videos/{hmx,intro,wg}.wmv` (seen): Windows Media, VC-1/WMV9 **(Q)**. Needs a decoder, or skipping them. |

### 1.11 `app.cpp`, `main.cpp`

| File | What it does | Verdict for GH2 |
|---|---|---|
| `main.cpp` | CLI: `--song`, `--difficulty`, `--av-offset`, `--no-fail`, `--autoplay`, `--kb2p`, `--no-venue`, `--selftest`, `--scenetest`, `--shadertest`, `--view/--panel/--character/--at/--guitar/--root/--cam`, `--skip-boot`. | **reuse the pattern** (adapt the options) |
| `app.cpp` | Everything else: window, first screen (pick the `.iso`), per-platform default settings (`profileValues`, the Steam Deck build), the Enhanced Settings actions, the song loop, the dev aids (`GD01_KEYS`, `GD01_SCREENSHOT*`), `runSelfTest`, `runShaderTest`, the Linux desktop entry, Steam virtual pads. | **adapt, and split**. At 2462 lines it holds too much. Start gd02 with separate files: `app` (loop and screens), `settings` (profiles and enhanced options), `devaids` (key injection, screenshots, logs), `selftest`, `shadertest`, `platform/desktop_entry`. |

### 1.12 Dependencies: what has to come together

From the `#include` graph:

```
platform/{paths,gpu_select,app_icon}  ← standalone (app_icon needs data/bitmap's Image)
data/bytes ← data/{dta,midi,inflate,bitmap,vgs,game_data(+iso9660)}
script/script ← data/dta, data/game_data
game/chart ← data/midi, game/rules ;  game/player ← chart, rules ;  game/autoplay ← player
input/input ← game/player (ControllerMode), data/json, platform/paths
audio/mixer ← audio/spu, data/vgs      (decouple before reuse: §1.3)
render/renderer ← render/gl, render/post_fx, data/bitmap ;  post_fx ← vk_upscaler
render/highway ← renderer, game/chart, game/player      (placeholder highway)
scene/scene ← data/bitmap, data/inflate ;  scene/anim ← scene, particles
render/scene_renderer ← renderer, scene ;  scene/character ← scene_renderer, clip, dta
render/track_renderer ← scene_renderer, scene/anim, chart, player
game/venue_script ← script ;  game/venue_camera ← scene, math
render/venue_renderer ← venue_script, venue_camera, guitar_anim, guitar_walk, crowd_renderer, character
ui/* ← script, panel(scene_renderer, anim, ui_resources(text_layout)), campaign, synth, crowd_audio, song_db
app ← everything
```

Groups that must move together:

1. **Platform group**: `platform/*` + `render/gl` + `render/math` + `render/renderer` (core) + `render/post_fx` + `render/vk_upscaler*` + `render/image_enhance` + `data/bitmap`'s `Image`. This is the `gd-core` candidate.
2. **Gameplay group**: `data/midi` + `game/{rules,chart,player,autoplay}` + `input/input` (which uses `ControllerMode` from `player.h`; move that enum to `input/` so input stops depending on game rules).
3. **Script group**: `data/dta` + `script/script`. It is needed by song_db, rules (through DTA), venues and UI, so it comes early.
4. **Scene group**: `scene/*` + `render/scene_renderer` + `render/text_layout`. It needs the GH2 milo loader first.
5. **UI group**: `ui/*`. It needs the script and scene groups, plus `campaign`, `synth` and `song_db`.

---------------------------------------------------------------------------

## 2. Build, release and packaging: what gd-game-02 mirrors

### 2.1 Pieces to copy

| Piece (gd-game-01) | What it does | For gd02 |
|---|---|---|
| `gd-game-01/local_build.sh` | Builds exactly what a release builds, into `local_builds/` (git-ignored). Targets: `linux`, `steamdeck`, `windows`, `macos`, `android`, `all`, plus `--native`, `--no-dlss`, `--force`. Runs the repo guard first. Skips builds that are up to date. Prints a summary per build with a log in `local_builds/logs/`. Runs `--shadertest` on the macOS build. | Copy as `gd-game-02/local_build.sh`. Change the `project(gd01 VERSION` regex, the `gd01-` names and the paths. |
| `gd-game-01/build_commands/*` | Double-click wrappers (`build-linux.sh`, `build-windows.bat`, `build-macos.command`, `build-android.sh`, `build-all.sh`, `_run.sh`). | Copy and rename |
| `engine/build_common.sh` | `say`, `configure`, and `compile` (a Ninja dry run counts the steps; on screen only errors, everything else in a log). | **Copy as is** |
| `engine/build.sh` | Developer build on Fedora: Linux (system SDL3), Windows (MinGW), Android if an SDK is found, into `dist/`. | Copy and rename |
| `engine/build_linux_portable.sh` | Ubuntu 22.04 container, SDL3 3.4.18 from source next to the program, RPATH `$ORIGIN`. Runs on any distro from 2022 on. | Copy and rename |
| `engine/build_linux_steamdeck.sh` | Steam Runtime 3 "sniper" SDK container, gcc-14, static libstdc++, `-DGD01_STEAM_DECK=ON` (handheld defaults), `STEAM-DECK.txt`. | Copy and rename (`GD02_STEAM_DECK`) |
| `engine/build_windows.sh` | MinGW-w64, cross-compiled from Fedora or native in MSYS2 UCRT64. Static libgcc/libstdc++. | Copy and rename |
| `engine/build_macos.sh` | Homebrew, `dylibbundler`, ad-hoc `codesign`. | Copy and rename |
| `engine/build_android.sh` + `engine/android/` | Gradle project, SDL3 built from source, `libgd01.so` loaded by `GD01Activity.java`. `versionCode = X*10000 + Y*100 + Z` (`android/app/build.gradle`). Debug key, or the release key from `GD01_RELEASE_KEYSTORE*`. | Copy. Rename the package to `io.github.mbracali.gd02`, the activity to `GD02Activity.java`, the app name to "GD Engine 02" and `libgd02.so`. |
| `engine/package.sh` | `dist/` → `gd01-<v>-<platform>.<zip|tar.gz|apk>`. | Copy and rename |
| `engine/CMakeLists.txt` | The `project(gd01 VERSION X.Y.Z)` version source of truth, `GD01_VERSION`, Android shared lib vs desktop exe, FidelityFX / volk / DLSS optional, warnings, MinGW static, embedded icons, `.rc`, `.icns`, macOS bundle ids. | Copy the skeleton. Rename the target and defines to `gd02` / `GD02_*` and the bundle id to `io.github.mbracali.gd02`. |
| `engine/cmake/embed_file.cmake` | Builds a file into the program as a byte array. | **Copy as is** |
| `engine/third_party/{fidelityfx,volk,vulkan}` + `third_party/dlss/README.md` | Vendored SDKs with their licences. DLSS is fetched, never committed. | Move to `gd-core/third_party` when gd02 needs them (rule of two). Don't copy them twice. |
| `gd-game-01/tools/fetch_dlss.sh` | Downloads the DLSS SDK 310.9.16 (pinned tag, SHA-256 checked). | Share it, or copy it into `gd-game-02/tools/` |
| `gd-game-01/tools/make_icons.py` | From `res/icon_full.jpg`: cuts the round badge out and writes `engine/res/gd01_icon*.png`, `gd01.ico` (classic bitmaps), `gd01.icns` and the Android mipmaps (adaptive, no black ring; small sizes show the badge's centre). | Copy and add a `gd02` parameter. Decide whether gd02 gets its own badge (**Q for the owner**). |
| `gd-game-01/tools/ffx_gen_shaders.py` | Pre-generates the FidelityFX shaders. | Shared (gd-core) |
| `.github/workflows/gd-game-01-build.yml` | Reusable (`workflow_call`) and manual (`workflow_dispatch`) build: windows (fedora:44 container), linux (ubuntu:22.04), steamdeck (sniper SDK), macos (macos-latest), android (secrets `ANDROID_KEYSTORE_B64`, `_PASSWORD`, `ANDROID_KEY_ALIAS`, `ANDROID_KEY_PASSWORD`). | Copy as `gd-game-02-build.yml` |
| `.github/workflows/gd-game-01-release.yml` | On a push to `main` touching `gd-game-01/**`: is the version new (no `gd-game-01/vX.Y.Z` release) and does the CHANGELOG have `## [X.Y.Z]`? Then repo guard + secret scan, every build, release notes from the CHANGELOG section plus a how-to-play table, `SHA256SUMS.txt`, and `-alpha/-beta/-rc` versions as pre-releases. | Copy as `gd-game-02-release.yml`: paths `gd-game-02/**`, tag `gd-game-02/vX.Y.Z`, title "gd02 X.Y.Z: Guitar Hero II (Xbox 360) reimplementation", "pick your `.iso`" texts. |
| `.github/scripts/repo_guard.py` | Rejects game data extensions, RE output folders, files over 20 MB, personal paths, and Ghidra names (`FUN_`, `DAT_`, `undefinedN`) in code outside comments. | Shared. Already extended on this branch with `original_iso/`. Consider adding `milo_xbox`/`_xbox` texture extensions, `.voc`, `.fac`, `.wmv` and `.xex` (`.xex`, `.mogg` and `.milo` are already covered). |
| `.github/scripts/check_action_updates.py` + workflow | Manual check for newer action majors. No Dependabot. | Shared, unchanged |
| `gd-game-01/CHANGELOG.md` format | `## [Unreleased]`, `## [X.Y.Z] - date`, a bold one-line summary, Fixed / Added / Changed / Removed. Written for players. | Same format in `gd-game-02/CHANGELOG.md` |
| Licence in every download | `LICENSE`, `LICENSE-ADDITIONAL-TERMS.md`, `licenses/` (FidelityFX, volk, Vulkan-Headers, SDL3, NVIDIA RTX SDK when DLSS is in). | Same |

### 2.2 What must change for gd02 (checklist)

- Program, CMake target and project: `gd02`; `project(gd02 VERSION 0.1.0 ...)`; define `GD02_VERSION`.
- Window title "Guitar Dream Engine: Game 02"; app name "GD Engine 02".
- App id / bundle id / Android `applicationId` and `namespace`: `io.github.mbracali.gd02`. `SDL_SetAppMetadata("GD Engine 02", GD02_VERSION, "io.github.mbracali.gd02")`. Linux desktop entry `io.github.mbracali.gd02.desktop`.
- Save folder: `SDL_GetPrefPath("gd-engine", "gd02")` → `gd-engine/gd02/`.
- Developer variables: `GD02_*` (no legacy alias).
- Download names: `gd02-<v>-{windows-x64.zip, linux-x86_64.tar.gz, steamdeck.tar.gz, macos.zip, android.apk}`.
- Tags: `gd-game-02/vX.Y.Z`; workflows `gd-game-02-{build,release}.yml`; concurrency group `gd-game-02-release`; label `gd-game-02`.
- Icons: `gd02_icon*.png`, `gd02.ico`, `gd02.icns`, `kGd02IconPng`.
- Android release key: **decide** whether gd02 uses its own keystore (`gd02-release.jks`), which keeps the apps independent, or shares gd01's. It must never change afterwards (`docs/release-process.md`, "Android release key").
- First screen and docs: "pick your Guitar Hero II (Xbox 360) `.iso`". The image is XDVDFS (XGD2) **(Q: also accept an extracted folder containing `gen/main.hdr`, like gd01)**.
- `docs/adding-a-game.md` still mentions `gd-game-01-ci.yml`, which was removed in commit c586410. Fix that doc when gd02's workflows are added.

---------------------------------------------------------------------------

## 3. Lessons learned (turned into rules for gd-game-02)

Each lesson gives what happened in gd-game-01, where it is recorded, and the
rule for gd-game-02.

### L1. Run the original's scripts through our own interpreter; don't hand-port them
- **What happened.** GH1's menus, HUD wiring, venue logic, sounds and tutorials are DTA scripts that talk to native objects. Once the interpreter was complete, whole features started working by construction: pause, lose, post_show and the end screens (backlog "UI plan" 4–8), venue scripts "on the full interpreter" (backlog "Venue effects"), crowd sounds from `arena_game.dta` / `beatmatch.dta` / `player.dta`, and the original track_panel script driving the smasher intro. Most bugs then came from **stubs and missing natives**, not from the scripts. B24: `foreach_player_matcher` handed the script a stub, so every fret looked pressed. B67: `campaign get_random_song` was looked up too early. B68: views did not report their `frame`. B39: `options get_lefty`/`get_stereo` had the wrong names, so the checkboxes started unticked.
- **Interpreter details that mattered.** A false `if` returns `kDataUnhandled` so the message passes on (backlog "UI" row). `switch`/`cond` run every remaining non-list command as the default (FINDINGS.md "Script interpreter notes"). A screen passes a handler it lacks on to its focus panel (commit 5df1fe4, `ui/ui_system.cpp`).
- **Rule.** Build the DTA loader and interpreter (`script/`) in the first milestones. Implement natives on demand. **Log every unhandled message and every stubbed native** (gd01: `GD01_UILOG`). A native that returns a fake value must say so loudly in the log. Keep the port's own screens in the port's own DTA (gd01's Enhanced Settings were "defined in the port's own DTA", X1a).

### L2. Verify rules against the disassembly before trusting assumptions (the HOPO saga)
- **What happened.** HOPOs were "done" several times. First the rule was ported (chart.cpp, ≤169 ticks). Then the look was wrong: the coloured top only from the third gem on, and only when the previous gem was the player's own (G1). Then players said hammer-ons failed in real play (B84). The window was re-checked **instruction by instruction**: watcher +0x74 is a constant 60.0 set by the constructor, giving −40..+160 ms (FINDINGS.md "Hit detection", "HOPO re-check" at 0x263a58..0x263a8c). The real cause was a PC difference (L3). Other assumptions that cost rounds:
  - B4: the light direction sign was backwards, so characters were over-bright (`docs/lighting.md`).
  - B28: LabelEx fit was applied twice.
  - B35: the button-fit byte was read as caps.
  - B38: PS2 star meshes drop their vertex colours.
  - B52: DECAL on stage-0 "Src".
  - B13: finger-map entries are flat pairs, not sublists.
- **Rule.** Every constant and branch in gameplay, rendering and UI code cites where it came from, with **[C]** (read in code or data) or **[I]** (inferred), as gd01 does (`docs/contributing.md` "Code style"; e.g. `docs/end_of_song.md` line 3). An [I] in a gameplay rule blocks "done". When a player reports "feels wrong", re-read the instructions, not our notes. Keep a FINDINGS.md section per system with the address list.

### L3. Emulate the console's input sampling where rules depend on it
- **What happened.** B84 / FINDINGS.md "HOPO on PC": the PS2 reads the pad once a frame, so moving from one fret to the next never showed "no fret held". Event-driven PC input saw a split-second gap and broke the HOPO chain. Fixed with a 1/59.94 s grace (`game/player.cpp`). The original's key repeat (hold_ms / repeat_ms, no acceleration, B80) mattered as well.
- **Rule.** Write down early how the Xbox 360 game samples input (per frame? XInput polling rate?) **(Q)**. Keep rules that depend on "held" state at pad-frame granularity. Offer any "forgiving" variant as an opt-in Enhanced option (gd01's "Hammer-ons & Pull-offs: Forgiving"), never as the default.

### L4. A headless `--selftest` that plays every chart is the gameplay gate
- **What happened.** `runSelfTest` (`gd-game-01/engine/src/app.cpp` ~line 2351) loads every song and builds every difficulty. It plays each chart twice: strummed only, and with every legal HOPO (`game/autoplay.h`). It must print `all charts fully hit`; it reported 12,708 HOPO landings. It is the PR checklist item for gameplay changes (`docs/contributing.md`). CI cannot run it because there is no game data on GitHub.
- **Rule.** `gd02 --selftest <iso>` exists from the first gameplay milestone. It covers every part (lead, co-op lead, bass/rhythm), every difficulty, both input styles, and **(Q)** practice speeds. Print the totals so score changes show up in diffs. Add `--scenetest`-style loaders for every milo and every script (load-all-and-report). They catch decoder regressions without a screen.

### L5. `--shadertest`, and never let one effect black out the game
- **What happened.** On macOS, songs, venues and Enhanced Settings were black (1.1.2, commit 80d04a4). The "super fog" shader declared `noise3`, a name GLSL reserves. Linux and Windows drivers accepted it; Apple's compiler did not. `PostFx::init` was all-or-nothing, so the scene went into a target nothing showed. The fix added `--shadertest`, which compiles every GL program with no game data (exit 1 on failure, 2 without a context). `local_build.sh` runs it on macOS before packaging. `Renderer::beginScene` now draws straight to the frame when effects fail.
- **Rule.** Every shader goes through `--shadertest` and through the GLES path (`GD02_GLES=1`, Android). Avoid GLSL built-in names (`noise1..4`, `input`, `output`, `sample`, `filter`…). Every optional effect degrades to "off" on failure and logs once. The macOS build is otherwise untested by us (backlog "Platforms"), so the shadertest is our only macOS check: keep it in `local_build.sh`.

### L6. Build the developer aids on day one
- **What gd01 has** (grep `GD01_` in `engine/src`):
  - Input injection: `GD01_KEYS="ms:key,+key,-key,p2key,s<song ms>:key,hoverX_Y,clickX_Y,touchX_Y"` (app.cpp `injectDebugKeys`).
  - Screenshots: `GD01_SCREENSHOT=<file>` with `GD01_SCREENSHOT_AT=<song ms>` / `GD01_SCREENSHOT_WALL=<s,...>`.
  - Navigation: `GD01_SKIP_BOOT`, `GD01_SCREEN=<screen>`, `GD01_SHOT=<camera shot>`, `GD01_BAND=...`, `GD01_TIP`, `GD01_SLOT`, `GD01_LOAD`, `GD01_DIALOG=<path>` (answers file dialogs), `GD01_VIRTUAL_PAD=<vid>:<pid>`.
  - Logs: `GD01_UILOG`, `GD01_HOPOLOG`, `GD01_SFXLOG`, `GD01_FACELOG`, `GD01_ROOTLOG`, `GD01_GPULOG`, `GD01_FRAMELOG`, `GD01_TUTLOG`, `GD01_LOG_SHOTS`, `GD01_NGXLOG`, `GD01_AUDIO_DUMP`.
  - Toggles: `GD01_FORCE_WALKS`, `GD01_NOTRACK`, `GD01_WINDOW`, `GD01_GUITARIST_CAM`.
  - CLI: `--view/--panel/--character/--at/--cam` to look at a single scene.
- **Why they paid off.** Many bugs were reproduced and measured headless:
  - B57: the first transition frames were found by screenshots at set times.
  - B49 / B60: the guitarist's drift was measured in units and degrees per 100 s.
  - B70: the master volume was checked after the demo.
  - B84: a fret change was injected at a song time to confirm the `hopo` event.
- **Rule.** Add a `devaids` module with key injection, timed screenshots, screen and shot jump and per-system logs before the systems that need them. Every bug fix that can be reproduced headless gets its reproduction line in the commit message or the backlog.

### L7. Frame pacing: limit frames in flight, smooth the song clock, never stall on the GPU
- **What happened.**
  - B20: text appeared in the wrong place for 1–2 s on entering a menu. The GPU had fallen seconds behind (no limit on queued frames), so the first texture upload stalled. Fixed with **at most two frames in flight** (a GL fence ring, `render/renderer.cpp` ~line 464).
  - B54 (microstutter): the drawn song clock now advances with the wall clock and **eases toward the audio position**, because the audio clock re-anchors on every callback. Occlusion reads use **pixel-pack buffers 3 frames late** instead of `glReadPixels`. Per-frame geometry uses a **buffer pool**.
  - B65 / B69: a swap interval of 2 is ignored by many drivers and compositors. The fix keeps vsync at 1 and paces in the loop whenever the cap is below the refresh rate.
  - B57: textures upload when a panel loads, not on first draw.
  - B78: FSR 3.1 needs a 2-frame fence ring and FP16 shaders, and relaunches on the dedicated GPU.
- **Rule.** Copy `Renderer`'s pacing, fences and limiter as they are. Never read back synchronously. Upload a screen's resources at load. Keep separate audio and draw clocks with easing. Add an FPS counter and `GD02_FRAMELOG` from the start.

### L8. Audio: one lock order, resample everything, pause the song and not the device
- **What happened.**
  - A deadlock at song start: the stream lock against the mixer mutex. Fixed with a consistent lock order (backlog "Done").
  - P1: 22,050 Hz crowd loops played at double speed until the mixer resampled every stream to the device rate.
  - B41 / B44 / B45: pausing the SPU or the whole SDL device silenced the pause and end-screen menu sounds. Now only the song stops, with its own frame clock.
  - B70: a fade panel left the SFX master at 0 after the demo.
  - B33: the menu music must be one looping track, restarted when the menus come back.
- **Rule.** The mixer has one documented lock order. Every source goes through the resampler. Pause is per voice or per stream. Every volume a screen changes is restored by the same object that changed it. For `.mogg`, decode on a worker thread with a ring buffer; Vorbis is far heavier than ADPCM.

### L9. Steam, Steam Deck and duplicate devices
- **What happened.**
  - Started from Steam, Steam hides the real controllers (`SDL_GAMECONTROLLER_IGNORE_DEVICES`) and offers its virtual pads, which SDL skips. gd01 sets `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1` (app.cpp ~line 2204; 1.1.1 "Fixed").
  - One press counted two or three times in the menus: the same controller seen twice (device plus Steam's virtual copy), or Steam's desktop layout typing Enter for A. Now a press from another device within 80 ms is the same press (`kSameNs`, `input/input.cpp` lines 86 and 217).
  - The Steam Deck build runs in Valve's Steam Runtime "sniper" SDK. It starts with handheld defaults (60 FPS, 800p, full screen, lighter effects), finds an `.iso` copied next to it, and ships `STEAM-DECK.txt`.
  - "Steam Deck Mode" first locked every option. That was wrong: it became a one-shot "Apply Steam Deck Settings" (1.1.0).
- **Rule.** Copy this behaviour with `input/`. Test from Steam Game Mode on a Deck before each release. Default settings per platform live in one table (`profileValues` in gd01's app.cpp). Settings presets apply once and lock nothing.

### L10. Controllers: identify by type and USB id, measure at connect, let the player choose
- **What happened.**
  - B75: a Rock Band guitar for Xbox 360 was reported as an Xbox 360 gamepad named "...Guitar". Its whammy rests at −32768, which read as fully pressed. Now axes are measured from their rest value at connect, with hysteresis.
  - 1.1.3: PS2 guitars on adapters that report a PS3 pad (054c:0268) played as gamepads. Each controller now gets a **Type** (Auto, Gamepad, PS2 guitar (adapter), Xbox guitar, Rock Band guitar), remembered per model.
  - Controllers SDL had no mapping for get a generic one (1.1.1).
  - One device per player; devices without a player drive the menus (B77).
  - Guitars have no Select/Back (B76).
  - `controllers-report.txt` collects controller data from players for bug reports.
- **Rule.** For GH2 the **Xbox 360 guitar (X-plorer, Les Paul) is the reference controller**: its layout comes from GH2's own `joypad.dtb` / `beatmatch_controller.dtb`. Copy `input/` with all of the above, and ask testers for the controller report.

### L11. Touch
- **What happened** (X42, 1.1.1, `input/input.cpp`):
  - A tap clicks when the finger lifts, and only if it did not slide more than 22 menu units.
  - Fingers within 30 ms strum one chord.
  - Two-finger swipes move through the menus (in a song: Star Power up, pause down, both configurable).
  - On-screen Up / Down / START buttons appear on touch and hide when a pad is used.
  - SDL touch-as-mouse is off while touch is on.
  - Touch is always on on Android.
  - Lefty mirrors the touch lanes.
  - Not yet tried on a real phone (backlog N1).
- **Rule.** Copy it. Get a real-device test before calling Android done.

### L12. Saves: every setter marks `needs_save`, and test "change, restart, check"
- **What happened.** Lefty Flip and the Game Settings volumes were lost on restart (commit 5df1fe4). The Options setters never set `needsSave`, so `autosave_goto` did not write the profile (`ui/career_objects.cpp` line 199). The volume sliders also needed the screen-to-focus-panel handler fallback (L1).
- **Rule.** Any native setter that changes a saved value sets `needs_save` (in one helper, not per field). Add a self-check that changes every option, saves, reloads and compares. Keep saves as versioned JSON (`gd02-career`), and import/export them as gd01 does (X33, `GD01-user-progress.json`) with type and range checks on load.

### L13. Before "fixing" a report, check what the original does
- **What happened.**
  - B19 / B31: the shop was "not available". It is disabled on Easy in the original.
  - B53: the main menu opens on Tutorials in the original; changing it was a deliberate deviation.
  - B17 / B25: a "freeze" was a lost song-list row after a difficulty change.
  - B72 / B74: Garden beams and Toxic mountains matched the code and needed a capture of the original. B82 then became a marked deviation [I].
  - B16 / B27 / B34 stayed 🟡 for want of a reference screenshot.
- **Rule.** Triage every report as one of: original behaviour, port bug, or needs a capture (`needs-capture` label, `docs/contributing.md`). Collect reference captures of GH2 360 early (**Q: Xenia captures or real hardware?**). Deliberate deviations are marked [I] in code and backlog, and are kept to options where possible.

### L14. Draw the scene graph exactly as the original traverses it
- **What happened.**
  - B2: stage floors hung over the crowd; meshes are drawn with their transform *owner's* matrix.
  - B81: about 200 meshes were missing because meshes have their own draw lists.
  - B10: PictureEx meshes needed the undo of a stale world transform.
  - B23: the HUD's pre-slide pose sits just outside the PS2's 4:3 screen and showed in 16:9. It is now clipped to 4:3.
  - B7: texgen modes; B48: MatAnim UV rotation and scale.
- **Rule.** Port the original's Draw / DrawShowing traversal and transform-owner rules before tuning any single venue. For GH2: the 360 frame is natively 16:9 (1280×720) **(Q)**, so the 4:3 clip probably goes away. Check the HUD's off-screen poses anyway.

### L15. Shading: port the maths, don't eyeball it
- **What happened.** The PS2 VU1 microcode was disassembled to get the lighting right (`docs/lighting.md`, B4, B38, B52, `docs/material_stages.md`).
- **Rule for GH2.** The shaders are HLSL on the disc (`system/run/shaders/*.fx`, seen). They are **original code**: read them for research only, never copy them into the repo, and write our own GLSL from notes "in our own words" (`docs/legal.md`). The repo guard does not catch pasted HLSL, so review that by hand.

### L16. Name it once, at the start
- **What happened.** Renaming gh1re → gd01 (commit 5eb18e9) touched the program, CMake, the Android package and activity, the macOS bundle, the window title, the defines, the env variables (with a legacy alias), the save folder (with a migration) and the save format id. Texts were renamed again in 1.1.1.
- **Rule.** gd02 starts as `gd02` everywhere. Keep the names in one header (`app_ids.h`: display name, app id, pref folder, env prefix), so a later rename is a one-file change.

### L17. Keep files small and keep the gameplay core rendering-free
- **What happened.** `app.cpp` (2462 lines) and `ui/ui_system.cpp` (2813) grew into catch-alls. `input.h` includes `game/player.h` only for `ControllerMode`.
- **Rule.** Split by concern from day one (§1.11). Gameplay (`chart`, `player`, `rules`, `autoplay`) has no SDL or GL includes, so the self-test and unit tests link without a window.

### L18. Licensing, attribution and the repo guard
- **Rules already in force** (`docs/legal.md`, `docs/contributing.md`, `.github/scripts/repo_guard.py`):
  - Never commit game data, extracted files, disc images, `analysis/`, `ghidra/`, `decomp/`, or Ghidra names in code outside comments.
  - DLSS is fetched at build time (`tools/fetch_dlss.sh`, commit 520915d).
  - Signing keys live only in Actions secrets.
  - Every download carries `LICENSE`, `LICENSE-ADDITIONAL-TERMS.md` and `licenses/`.
  - Every source file starts with the SPDX header (`LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms`) and the Guitar Dream Engine line, as the gd-game-02 tools already do.
  - The credits start with the Guitar Dream Engine attribution, then the project's developers, the open-source thanks, the testers, and the original game's credits (X36, 1.1.1).
  - Commits are signed off (`git commit -s`, DCO) with scope prefixes (`gd-game-02: ...`).
  - The splash says "free and source-available", because the licence is not an open-source one (1.1.1).
- **For GH2.** gd-game-02 now holds `original_iso/` and `extracted/` locally. Both are git-ignored and in the guard. Run `python3 .github/scripts/repo_guard.py` before every commit. Research output from the Xbox 360 executable (`default.xex`) stays outside the repo, like gd01's `analysis/`.

### L19. GitHub Actions budget: releases only; build locally
- **What happened.** Commit c586410 removed CI on PRs and branches and removed Dependabot. A release runs only when `main` gets a new version, and `local_build.sh` reproduces the release exactly. A manual "build" workflow exists for macOS. Smaller fixes: the Android SDK packages had to be installed through setup-android (04d19a7), and the release had to upload only the builds' files (a620540).
- **Rule.** gd02 follows the same flow, with no automatic CI. The PR checklist is: `local_build.sh` passes, `--selftest` passes, `--shadertest` passes, CHANGELOG updated.

### L20. Enhanced features: opt-in, labelled, default to the original
- **What happened.** Everything beyond the original (Enhanced Settings, AI upscaling, post FX, cheats as switches, calibration, mouse, touch) is marked [I] and lives in its own screen, with defaults per platform. Two items turned out not to be feasible: FSR 4 needs DirectX 12 and RDNA 4 (X56), and Replace Textures was dropped (1.1.3). Saying so early saved work.
- **Rule.** Port the whole Enhanced Settings stack as one unit after parity of the core loop. Check feasibility for new options (API, GPU, licence) before listing them.

### L21. Research tooling: own the decoders, validate byte-exact
- **What happened.** Every GH1 format got a Python decoder first (`gd-game-01/tools/{ark,dtb,rnd,tex,vgs,bnk,char_anim,ui_panels}.py`), "validated byte-exact", and the C++ followed. REA could not open the PS2 ELF, so Ghidra ran headless (FINDINGS.md "Tooling"). The MPEG-2 decoder was checked against ffmpeg (61.9 dB on every frame).
- **Rule.** Same order for GH2: a Python tool, a round-trip or count check against the whole archive, then C++. `gd-game-02/tools/{xdvdfs,ark}.py` are already the first two. Next come `dtb.py`, `milo.py`, `tex.py` (360 untiling) and `mogg.py`. For the `default.xex` (PowerPC, big-endian, XEX2-packaged), first establish the toolchain: XEX2 unpacking plus Ghidra PowerPC (Xenon/VMX128) through a XEX loader. **(Q: check whether REA can open it; record the setup like `rea-ghidra-ps2-setup`.)**

---------------------------------------------------------------------------

## 4. Proposed architecture and milestone plan for gd02

### 4.1 Starting layout

```
gd-game-02/
  engine/
    CMakeLists.txt            project(gd02 VERSION 0.1.0); same options as gd01
    src/
      main.cpp, app.cpp       small; CLI + loop only
      app_ids.h               names, ids, pref folder, env prefix (L16)
      devaids/                key injection, screenshots, logs (L6)
      tests/                  selftest, shadertest, scenetest (L4, L5)
      platform/               from gd01 as is (paths, gpu_select, app_icon, android_data)
      data/                   bytes, inflate, json, midi (as is); xdvdfs, ark (new);
                              dtb (adapt); milo (new); texture_xbox (new); mogg (new)
      script/                 gd01 interpreter + GH2 loader
      audio/                  mixer (decoupled), vorbis source, synth (GH2), crowd
      game/                   rules, chart, player, autoplay, song_db, campaign (GH2)
      input/                  from gd01 as is (+ GH2 defaults)
      render/                 gl, math, renderer core, post_fx, vk_upscaler, image_enhance (as is);
                              materials (new GLSL for GH2), scene_renderer, track, venue, hud
      scene/                  Rnd object model for milo, anim, character, particles
      ui/                     ui_system (adapted), panels, natives, gamepad_panel (as is)
      video/                  (later: wmv, Q)
    third_party/              stb_vorbis or libvorbis (Q: licence); shared SDKs via gd-core
    android/, res/, cmake/
  tools/                      xdvdfs.py, ark.py (exist); dtb.py, milo.py, tex.py, mogg.py, ...
  docs/                       FINDINGS.md, backlog.md, one spec per system (this file first)
  CHANGELOG.md, README.md, local_build.sh, build_commands/
```

When gd02 needs a piece of gd01 unchanged (the platform group, `render/{gl,math,renderer core,post_fx,vk_upscaler}`, `input/`, `data/{bytes,inflate,json,midi}`), move it into `gd-core/` instead of keeping two copies (`docs/adding-a-game.md`). This is a decision for the owner, given the "fresh engine" wish **(Q: copy first and share later, or share from the start?)**.

### 4.2 Milestones (ordered)

**M0: scaffold (no game data needed).**
- CMake project `gd02`, the platform group copied, a window with the icon, a first screen that picks the `.iso`.
- `--shadertest`, `local_build.sh` and the build scripts, workflows `gd-game-02-{build,release}.yml`, CHANGELOG, README, licence files in the packages.
- Done when `local_build.sh all` produces runnable empty shells on every platform.

**M1: data access.**
- XDVDFS reader (port of `tools/xdvdfs.py`), `main.hdr` decrypt plus ARK with inflate (port of `tools/ark.py`).
- DTB decoding: Python first (`tools/dtb.py` for GH2), then C++.
- MIDI loads.
- Done when a `--scenetest`-like listing reads every file, and every `.dtb` decodes, both reported in counts.

**M2: research bootstrap (in parallel with M1).**
- XEX2 unpacking and a Ghidra PPC project (outside the repo).
- Locate the beatmatch watcher, scoring, Star Power and crowd meter by their strings (`beatmatcher`, `slop`, `scoring`), as in gd01.
- Start `docs/FINDINGS.md`.

**M3: song audio.**
- `.mogg` → Ogg Vorbis (header seen: `0x0a` = unencrypted version, Ogg offset, seek-table count, chunk size **(Q)**).
- `songs.dtb` track maps, pans and volumes.
- The gd01 mixer decoupled from VGS/SPU.
- Song clock with easing (L7). Guitar-track mute on a miss.
- Done when `gd02 --song freebird` plays the song with all stems.

**M4: "a song plays on a highway with GH2's timing and scoring" (first real goal).**
- `chart` from `PART GUITAR` (and `PART GUITAR COOP`, `PART BASS`/`PART RHYTHM`).
- `player` with GH2's watcher constants and rules ([C] only, L2), `rules` from `scoring.dtb`, `autoplay`.
- `--selftest` "all charts fully hit" on every part and difficulty (L4).
- Keyboard and Xbox 360 guitar input (gd01 `input/`).
- Drawn with gd01's placeholder `render/highway.cpp` and a text HUD.
- Dev aids `GD02_KEYS` / `GD02_SCREENSHOT` / `GD02_HOPOLOG` (L6).

**M5: Milo and textures.**
- `.milo_xbox` loader (Python first), Rnd object model, `.bmp_xbox`/`.png_xbox` (DXT plus 360 untiling **(Q)**).
- `--view <milo>` scene viewer.
- Our own GLSL material shaders, written from research notes on `lighting.fx` (L15).

**M6: the original highway and HUD.**
- `track/` surfaces and gems (gd01's `track_renderer` as reference: tails, fade, lefty mirror), `hud/` (score, multiplier, rock meter, Star Power).

**M7: script-driven UI and Quickplay.**
- The gd01 interpreter on GH2's `ui/*.dtb` and `ui/gen/*.milo_xbox` panels.
- Natives on demand with an unhandled-message log (L1).
- Boot, main menu, Quickplay song select, difficulty, loading, pause, results.
- Profile and save (L12).

**M8: venues.**
- `world/{arena,battle,big,fest,small1,small2,stone,theatre}` (seen).
- Venue scripts, lighting states, the camera director, particles, the GH2 post look (bloom, DOF).

**M9: characters and band.**
- `char/*` skeletons, skinning and clips, the band from `BAND *` tracks.
- Lip-sync and faces (`.voc`, `.fac` **(Q)**), crowd (`char/crowd` **(Q)**).

**M10: GH2's new modes.**
- Co-op (lead plus bass/rhythm, career and quickplay), face-off and pro face-off (two highways, gd01 face-off as reference).
- Practice mode (sections, slow-down speeds, `ui/gen/practice.dtb` seen; time-stretched audio **(Q)**).
- Career with encores, store and unlocks, cheats (`config/gen/cheat_init.dtb` seen).

**M11: tutorials, videos, credits, polish.**
- `tutorial/` (625 streams, seen), `.wmv` intro and logos **(Q: decoder)**, credits.
- Enhanced Settings stack ported (L20), platform defaults, Steam Deck pass (L9), first release `gd-game-02/v1.0.0`.

### 4.3 What GH2 (Xbox 360) has that GH1 (PS2) did not

| Area | GH1 (gd01) | GH2 Xbox 360 | Status |
|---|---|---|---|
| Disc | ISO 9660 | XDVDFS (XGD2), `default.xex` | seen; reader in `tools/xdvdfs.py` |
| Archive | `MAIN.HDR` plain + `MAIN_0.ARK` | `gen/main.hdr` Park-Miller encrypted, `main_0.ark`, inflated-size field, `../../system/run/` paths | seen; `tools/ark.py` |
| Executable | MIPS R5900 ELF, little-endian | PowerPC Xenon, big-endian, XEX2 | **Q**: toolchain |
| Scripts | `.dtb` with the C `rand()` LCG | 177 `.dtb` | **Q**: encryption / endianness |
| Scenes | `.rnd_ps2`, `.gh` | `.milo_xbox` (503) | **Q**: container and class revisions |
| Textures | `.bmp_ps2` (palettes, PS2 alpha) | `.bmp_xbox` / `.png_xbox` (3533) | **Q**: DXT and tiling |
| Shaders | VU1 microcode | HLSL `.fx` on disc plus compiled `.fx_xbox` (2564) | seen; research only (L15) |
| Song audio | `.vgs` PS-ADPCM, 4 channels | `.mogg` (Ogg Vorbis multitrack) plus `_coop.mogg` | seen (version 0x0a) |
| SFX | `.bnk/.nse/.seq` on a software SPU2 | no `.bnk`; Synth objects in milo / `sfx/` | **Q** |
| Videos | `.pss` MPEG-2 | `.wmv` (hmx, intro, wg) | **Q**: codec, decoder licence |
| Chart tracks | `T1 GEMS`, `EVENTS`, `ANIM`, `TRIGGERS` | `PART GUITAR`, `PART GUITAR COOP`, `PART RHYTHM` / `PART BASS`, `BAND BASS/DRUMS/SINGER/KEYS`, `EVENTS`, `TRIGGERS` | seen in freebird.mid |
| Instruments | lead guitar | lead, bass or rhythm (co-op), co-op lead chart | seen (tracks); **Q**: the rules per part (bass multiplier up to 6×?) |
| Multiplayer | face-off | co-op (career and quickplay), face-off, pro face-off | **Q**: pro face-off rules |
| Practice mode | none | sections, slow-down | seen (`practice.dtb`); **Q**: how the audio slows |
| Career | 6 tiers, store | tiers with encores, store, co-op career | **Q** |
| HOPO / timing | −40..+160 ms, ≤169 ticks, 100 ms slop | likely revised | **Q**: verify in the xex (L2) |
| Lip-sync / faces | GEMS note 108, face morphs | `.voc` ("FACE"…"Harmonix"), `.fac` | seen; **Q**: format |
| Screen | 4:3, 448 lines | 16:9 720p | **Q**: HUD layout and safe areas |
| Platform extras | memory card texts | achievements, Xbox LIVE leaderboards, storage device, DLC | **Q**: what to keep (local achievements? DLC folders as mods?) |
| Locales | eng | eng, fre, deu, esl, … | seen |
| Release date | 2005 | the 360 version is from April 2007 (PS2 GH2: Nov 2006) | **Q**: `gd-game-02/README.md` says Nov/2006 |

### 4.4 Open questions for the owner

1. **Sharing.** Copy gd01's game-independent modules into gd02 now and move them into `gd-core/` later, or create `gd-core/` immediately (`docs/adding-a-game.md` prefers "rule of two", which gd02 now meets)?
2. **Icon and Android key.** Does gd02 get its own badge and its own release keystore?
3. **Third-party code.** Vorbis decoding (stb_vorbis, public domain, or libvorbis, BSD) and WMV decoding (FFmpeg LGPL as a dynamic library, or skip the videos) are both permitted under `docs/legal.md`'s third-party rule. Which do we want?
4. **References.** Capture the original on real hardware or in Xenia (L13)?
5. **Scope.** Achievements, leaderboards, DLC: drop, or re-imagine locally (achievements as a local list, DLC folders as the mod path)?
