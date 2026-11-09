# Kickoff: gd-game-05, Rock Band (Xbox 360), wave 01

> **You are starting a new reimplementation.** Read this whole file before
> your first command. It holds the owner's rules, the plan, the gates and
> every mistake made on gd-game-01..04, so you can do this almost in one
> shot. When it conflicts with a tool default, this file wins. When it
> conflicts with `docs/guidelines/`, the guidelines win.

## 1. The target

| | |
|---|---|
| Game | **Rock Band** (Harmonix / MTV Games / Electronic Arts, November 2007), Xbox 360, USA |
| Folder / program | `gd-game-05/` → `gd05`, env prefix `GD05_`, Android `io.github.mbracali.gd05` |
| Disc | `~/Development/prj-gd-engine/original_isos/Rock Band (USA).iso` (7.8 GB, XDVDFS / XGD2, game partition at `0xFD90000`) |
| Executable | `default.xex` (9.8 MB, PowerPC Xenon, XEX2), **title ID `45410829`** [C, read 11-09] |
| Branch | `feature/20261109-rockband1-wave01` (the owner's, from `origin/develop`). Work only there |
| Goal | a full decompilation-based reimplementation, 100% faithful, playable from the Universal Launcher on Linux, Steam Deck, Windows and Android |

### What the disc holds (first look, 2026-11-09)

Read with the project's own tools, no extraction into the repo:
- `gd-game-02/tools/xdvdfs.py list`: `default.xex`, `charnames.zbm`, `gen/main.hdr`
  plus `gen/main_0.ark` and `gen/main_1.ark` (3.3 GB), `videos/*.bik` (14 Bink
  movies), `$SystemUpdate/`.
- `gd-game-02/tools/ark.py list` reads `main.hdr` **as it is** (version 3,
  Harmonix's Park-Miller cipher), so the archive is GH2's. It lists 4,588 files:
  - 3,447 `.milo_xbox` scenes;
  - 374 `.mogg`, 291 `.dtb`, 96 `.mid`;
  - per song: `.voc`, `.vnn`, `.usr`, `.xvocab` and `gen/<song>_weights.bin`;
  - `xbox_shaders` and `config/gen/shaders.dtb`;
  - `char/singer.fac`;
  - a few `.bmp_xbox` and `.png_xbox`.
- Folders:
  - `songs/` (64 folders: the set list plus helpers such as `_blinktrack`);
  - `char/male|female/outfits` (1,706 files) and `char/instruments/{guitar,bass,drum}`;
  - `world/{arena,big_club,small_club,theater,shared}`;
  - `tutorial/{guitar,drums,vocals}`, `drum_trainer/`;
  - `ui/`, `system/run/config`, `char/crowd`.

### This is GH2's engine, one generation later (read before writing any code)

Rock Band runs on the **same Harmonix engine family as Guitar Hero II**:
ARK archives, DataArray scripts (`.dtb`), the Milo scene graph, `.mogg` multitrack
Vorbis, MIDI charts, XMA sound effects. So:

**Start from gd-game-02's engine, copied into `gd-game-05/engine/`**:
- `data/`: XDVDFS, `RandomFile` with the Windows `_wfopen` path, crypt, DTA, MIDI, mogg, JSON;
- `script/` (the DataArray interpreter, L1) and `scene/` (Milo);
- `render/`, including the gd-engine-core adapter `core_device` and `core_backend`, runtime SPIR-V;
- `audio/`, `char/`, `ui/`, `input/`, `devaids/`, `tests/`, `platform/` (module entry, explorer, sounds);
- the build scripts for every platform.

Rename gd02→gd05 and GD02_→GD05_. This is the gd-game-03 path (Encore copied gd02 and reached 99%), not the gd-game-04 path (GH3 is Neversoft and had to be written fresh).

**Assume nothing, confirm everything.** One generation later means:
- Milo object versions and **new classes**: the band, the four tracks, vocals, the crowd, the character creator, Overdrive;
- new DataArray handlers and natives;
- a new song format (vocals files, more stems in each `.mogg`, drum and vocal MIDI tracks);
- new UI screens and a new save format.

Every rule, constant and layout comes from `default.xex` and the disc, never
from GH2's. Write each confirmation in `gd-game-05/docs/FINDINGS.md` as
**[C] address**. Where Rock Band differs from GH2, Rock Band's executable
wins, every time. A copied gd02 rule that is not re-confirmed in RB's code is
**[I]** and blocks "done" (L2).

**Formats to confirm or decode** (each in `docs/formats.md`, decoded
byte-exact by a Python tool in `tools/`, L21):
- **Milo (`.milo_xbox`):** versions, compression, every class the disc uses.
  `--scenetest` must read all 3,447 to their end markers.
- **Songs:**
  - the song list and metadata (which `.dtb`);
  - `.mid` tracks per instrument and difficulty: guitar, bass, drums, vocals, venue, events, beat;
  - Overdrive phrases, solos, drum fills, Big Rock Endings, practice sections.
- **Vocals:** `.voc`, `.vnn`, `.usr`, `.xvocab`, `_weights.bin`, `char/singer.fac`. Find which is lip sync, which is the vocal pitch data and which is the vocabulary or phoneme table.
- **Audio:** `.mogg` (more stems than GH2's: drums may be split into kick / snare / kit, plus vocals; the tracks map is in the song's data), XMA banks, the crowd.
- **Shaders:** `xbox_shaders` plus `config/gen/shaders.dtb`. These are original code: read them for research only and write our own GLSL in our own words (L15).
- **Video:** **Bink** (`.bik`), new for this project: GH2 used WMV and gd03 uses the PS2 module.
  - Add the decoders (`bink`, `binkaudio_rdft` / `binkaudio_dct`) to the project's FFmpeg build (LGPL, pinned, `build_common.sh`).
  - Confirm their output frame by frame against the file's own frame count.
- **Textures:** `.bmp_xbox` / `.png_xbox` and the textures inside Milo (gd02's `tex.py` as the start).
- **Saves:** RB's profile and band data, re-expressed as versioned JSON (L12).

You may read publicly documented format notes from the modding community to
speed up, but **don't copy any third-party code**. Write our own decoders and
confirm every rule in the XEX.

### Rock Band features to reproduce (verify the full list from the data)

- **Four instruments, four local players:**
  - guitar, bass, drums and vocals, each with its own highway (vocals as the lyric and pitch track on top);
  - every difficulty;
  - lefty and no-kick options, if the original has them.
- **Band mechanics** (each rule from the XEX, with addresses):
  - Overdrive: phrases, the meter, whammy on Overdrive sustains, deploying it on each instrument (guitar and bass tilt and Select; drum fills ended on the cymbal; vocals in free sections);
  - the band multiplier and the Unison bonus;
  - saving a failed bandmate, and failing out;
  - Big Rock Endings and guitar solos (the solo frets, the solo bonus);
  - drum fills and freestyle;
  - vocal pitch tolerance per difficulty, talkies, percussion sections;
  - stars and score.
- **Modes:**
  - Solo Tour per instrument;
  - Band World Tour (cities, venues, fans, unlocks, cash);
  - quickplay (solo and band);
  - multiplayer: Tug of War, Score Duel;
  - practice, the tutorials (guitar, drums, vocals), the Drum Trainer.
- **Rock Shop and the character creator:**
  - creating characters (body, face, hair, clothes, tattoos and make-up, instruments);
  - buying with tour cash;
  - bands and band logos, as far as the disc has them;
  - The Glitches with everything unlocked and bought.
- **Venues, crowd, cameras, lighting:** the RB director (camera cuts from the venue MIDI track), lighting cues and post effects.
- **Online, leaderboards, achievements, the Music Store / DLC:** stub them, logged, and label them in the backlog as "not in the port". Songs come only from the player's disc in wave 01; keep the song library open for added songs later (mod support, see section 8).

### Ghidra on this machine

- Ghidra 12.1.4 at `~/.local/opt/ghidra_12.1.4_PUBLIC`, JDK 21 at `~/.local/opt/jdk-21.0.12.1+1` (`export JAVA_HOME=...`).
- The **XEXLoaderWV** extension is installed: import `default.xex` with it into `gd-game-05/ghidra/` (git-ignored).
- Copy gd-game-04's `tools/ghidra_scripts/` on day one:
  - `ExportAnalysisXex.java`, `FixXexFunctions.java`;
  - `DumpAsm.java`, `ReadF.java`, `FindBytes.java`, `FindConst.java`.
- Export functions, strings, refs and pseudocode into `ghidra/analysis/`.
- Name and comment functions as you confirm them. The DataArray handler and native tables are the first big map: message name → native address.
- Run `analyzeHeadless <abs project dir> <name> -process -readOnly -noanalysis -postScript ...`. The project path must be absolute, with no `.` element.

## 2. How you work

- **The owner may be away.** Do not stop to ask questions and do not wait for answers.
  - When the owner is present, ask with the interactive question tool, always with suggestions.
  - When something would normally be the owner's choice, pick the original game's behaviour. When the original is unknown, pick the gd-game-02 pattern.
  - Write the choice in `gd-game-05/agents/DECISIONS.md`: what, why, how to undo it.
  - Questions go in `gd-game-05/agents/OPEN_QUESTIONS.md`. Never block on either.
- **Progress log.** After every milestone, and at least every 2 hours, append to `gd-game-05/agents/PROGRESS.md`:
  - time;
  - milestone and what is done;
  - test results, with the exact commands and the pass/fail lines;
  - what's next;
  - the time left.
  The owner reads it; it's your only voice.
- **Time estimate, measured.** Use the agent time measured from git history, not guesses:
  - gd-game-03 (gd02 copied) reached its final report in about 4 h;
  - gd-game-04 (a new engine) reached `ready_to_play` (M0..M9) in about 6.5 h (10-28 22:00 → 10-29 04:26);
  - gd-game-04 then needed about a week of owner waves (100+ fixes) to reach 96%.

  Rock Band reuses like gd-game-03 but is much wider (four instruments, four players, vocals, Bink, the character creator). Plan **8 to 12 h for M0..M9**, then parity waves.
  - Write the estimate at the top of `PROGRESS.md` and revise it at each gate.
- **Commit and push at every milestone gate** (section 7) on `feature/20261109-rockband1-wave01`. **No PR**: the owner opens PR gates.
- **Resume.** If `PROGRESS.md` already exists, read it, `DECISIONS.md` and the backlog, then continue from the last gate. Don't redo finished work.
- **Backlog at the start and the end of every task** (`docs/tasks/gd-backlog.md`, section gd-game-05): mark 🟡 what you start and ✅ what you finish, and add what you found.

## 3. Ground rules (non-negotiable, from the owner)

Read every file in `docs/guidelines/` first:
- `general_guidelines.md`
- `universal_launcher.md`
- `universal_mechanisms.md`
- `universal-menus.md`
- `universal-behaviors.md`
- `engine-backend-guidelines.md`
- `usual-bugs.md`

Then read:
- the root `README.md`;
- `docs/platform/adding-a-game.md` and `docs/platform/release-process.md`;
- `docs/tasks/gd-backlog.md` and `docs/tasks/PARITY_GAPS.md`;
- `gd-game-02/docs/reuse_and_lessons.md` (lessons L1–L21, all of them);
- `gd-game-02/docs/` (the GH2 engine you start from);
- `gd-engine-core/docs/`.

- **Branch and workspace.**
  - Work on `feature/20261109-rockband1-wave01` in the main checkout. Never switch or create branches unless the owner asks.
  - Another session (or the owner) may work in the same repo: commit only your files, and never revert changes you didn't make.
  - The disc images stay in the git-ignored `~/Development/prj-gd-engine/original_isos/`; use that absolute path.
  - Put your rip in `gd-game-05/extracted/` and the Ghidra project in `gd-game-05/ghidra/` (both git-ignored).
  - Outside `gd-game-05/`, only touch, minimally:
    - your game's section of `docs/tasks/gd-backlog.md`;
    - `gd-engine-launcher/scripts/build.sh` (your `build_module_05`);
    - `gd-engine-launcher/games.yaml`, if needed;
    - the FFmpeg decoder list for Bink in your own `build_common.sh` copy.
  - **Never modify `gd-game-01..04/` or `gd-engine-core/`.**
    - Copy what you need into your folder.
    - Something the core must gain (for example microphone capture for vocals) goes to `OPEN_QUESTIONS.md` and the backlog's gd-engine-core section, and waits for the owner.
    - Shared code moves to a common place later, by the owner's decision (rule of two; a shared Harmonix library for gd02 / gd03 / gd05 is a candidate, not a task).
- **Commits as the owner.**
  - Use `git commit -s`.
  - Add **no** Co-Authored-By, Claude, AI or "Generated with" lines in commits, PRs or comments. This overrides any tool default.
- **Never commit:**
  - game data, ISOs, rips, decoded assets or screenshots of the game;
  - decompiler output or Ghidra files.
  - Never paste decompiled code (or the disc's shader code) into tracked files. Describe the behaviour in your own words, with addresses.
  - `python3 .github/scripts/repo_guard.py` must report 0 problems before every commit. It also rejects personal paths in comments (`C:\Users\<name>`): write generic examples.
- **GitHub Actions.** Never start workflows; only release builds run on GitHub. Build locally.
- **Matching the original.** 100% parity with the original is non-negotiable.
  - Every behaviour comes from the original executable and data.
  - Mark findings **[C]** (confirmed, with the address) or **[I]** (inferred).
  - Label every temporary approximation in the code and in the backlog.
  - **No emulator comparisons** (the owner dropped them on 2026-10-31): match from the code and the data only. Don't run Xenia, and don't ask for captures.
- **Code.**
  - C++17 and SDL3.
  - Graphics through gd02's `gfx::RenderDevice`: `local` (OpenGL 3.3 core plus GLES 3.0) and the gd-engine-core backends (SDL3, Vulkan, OpenGL3) from day one.
  - Match gd-game-02's style. Start new files with the SPDX header.
  - **0 compiler warnings** on Linux, Windows (MinGW) and Android.
  - Never name anything `environ`: it is a MinGW macro.
  - Keep files small, and keep the gameplay core free of rendering code (L17).
  - Open every user path with `RandomFile` or SDL_IOFromFile, never a bare `fopen` / `std::ifstream` of a UTF-8 string on Windows (usual-bugs.md).
- **Processes.**
  - Every command that runs the game gets a `timeout`. Only one game process at a time.
  - Never leave background processes behind. Check with `pgrep -fa gd05` before you finish a milestone, and kill by PID only (never `pkill -f`).
  - With a real Rock Band guitar plugged in, run the tests with `SDL_GAMECONTROLLER_IGNORE_DEVICES=0x1bad/0x0002 SDL_JOYSTICK_IGNORE_DEVICES=0x1bad/0x0002`.
  - Tests use a scratch `XDG_DATA_HOME`: never touch `~/.local/share/gd-engine`.
  - Don't work around a blocked command: find an allowed way, or note it and move on.
- **Subagents.**
  - Use them only for independent research or parallel verification, at most about 4 at once, with the owner's OK for long runs.
  - Give each a precise brief, and **verify their claims yourself**: an agent's "done" isn't done until your own test passes.
  - Subagents clean up after themselves.

## 4. The mistakes made on gd-game-01..04 — do NOT repeat them

Each one cost the owner rounds of back and forth. Each one is now a rule.

1. **Test the real user path, not only the dev shortcut.** The venue intro camera worked with `--song` but not from the menus (gd02).
   - Verify every feature through **the menus** (`--walktest`), through the launcher (`--play gd05`) and on Windows under Wine.
   - `--song` is only a shortcut while you develop.
2. **Never test a stale binary.**
   - Rebuild, then test, and check the binary is newer than your edits.
   - Write the exact test invocations in `agents/CONTEXT.md`. Tests take the disc path; a test that prints its usage text **has not run**.
3. **Audit the data for things we never implemented** (GH2's Star Power lightning was never drawn).
   - Build the asset/reference audit early (M3): every object, view, anim, sound, config key, DataArray handler and native that the data and scripts reference, against what the engine resolves.
   - Every gap becomes a backlog row. Re-run the audit at every gate.
   - Count the **missing natives** and drive the count to 0: gd04 still has 678.
4. **The decompiler drops arguments: read the assembly** (`DumpAsm.java`) whenever a parameter matters.
5. **Know your clocks.**
   - Track, audio, sfx log and screenshot clocks differ; gd02's track ran 180 ms ahead.
   - Rock Band adds the **vocal pitch clock** and the input latency calibration (audio and video, with the original's calibration screen). Document every offset.
   - The audio offset moves the highway **and** the judging, as gd-game-01 does (`drawT = frameTime - audio_offset + video_offset`); gd02..04 moved only the judging (GitHub #69, GDALL-038).
6. **Draw order and blending are behaviour.**
   - Port the original's traversal order (L14).
   - Venue transparency and smoke must never blend over the band (gd04 GD04-068).
   - Give every visual fix a counter or a check in a walk.
7. **Every fix gets a regression check** in `--selftest`, `--walktest`, `--venuetest` or a dedicated test, as headless as possible. Visual features get a counter the walk can assert.
8. **Camera and animation transforms must be exact.**
   - Look-at, roll, splines, scale keys and parent order.
   - Check every shot of every venue headlessly: no NaN, not inside geometry, target in front.
   - Hands on the necks must follow the original's IK: gd04's fret hands were twisted and off the neck (GD04-066).
   - Everything runs on the original's fixed step at any frame rate.
9. **Song start order.**
   - Venue first, then the intro shot (moving from the first frame), then the highways' intro with its sounds, then the music.
   - **Nothing counts before the highway is live**: no strum, hit, miss or fail sound (gd01 B86, gd04 GD04-112).
10. **Menus.**
    - Menu sounds must never go silent: whoever lowers a volume restores it.
    - Typed names grow left to right; **Enter is Start and accepts typed names** (GD04-080).
    - Back plays the backward transition.
    - Screens must not darken when you go back and forth: release shared light state (GD02-077).
    - Locked items show as locked.
    - The mouse works everywhere, and every help-bar pill is clickable.
    - Held directions repeat with the original's hold and repeat times (GD04-114).
    - Layouts come from the original's scripts and data to the pixel, checked with `check:pos` / `check:centrex` walk acts. gd04's pause, Cheats and Store were 4–20 px off (usual-bugs.md).
    - **No black frames** between screens (intro to menu, press start, song end): use gd04's `probe:black` / `check:no_black` acts (usual-bugs.md).
11. **Gameplay feel is behaviour.**
    - Overstrumming during a held sustain kills it (GD04-108).
    - Star Power / Overdrive audio effects exactly as the original's DSP (GD04-098).
    - Practice keeps the music, highway and SFX in sync at every speed (GD04-100).
    - Quickplay picks characters and venues by the original's rules, not at random (GD04-071 / -091).
12. **The Glitches** has everything unlocked and bought in every build. A new band starts with nothing bought (GD04-096).
13. **Packages.**
    - A launcher build missing a module, or a launcher that can't open ISOs (Android content URIs, Windows wide paths, Windows DLL dependencies), must never happen again.
    - `verify_bundle.sh` must report your module in **every** platform's bundle.
    - The Windows release container needs a host C++ compiler for gd-engine-core's shader tool (1.1.13).
14. **Don't claim what you didn't run.**
    - In `PROGRESS.md`, separate "verified" (command plus output) from "built only" and "not tested".
    - When you aren't sure what the original does, say so and add it to `OPEN_QUESTIONS.md`.

## 5. The pattern to follow (gd-game-02 is the model)

Folder layout as in `docs/platform/adding-a-game.md`:
- `README.md`, `agent-instructions.md`, `CHANGELOG.md`, `game-info.yaml`, `local_build.sh`;
- `build_commands/`, `docs/`, `engine/`, `tools/`, `agents/`;
- the git-ignored `extracted/`, `ghidra/` and `local_builds/`.

Engine:
- program `gd05`, save folder `SDL_GetPrefPath("gd-engine","gd05")`;
- Android package `io.github.mbracali.gd05`;
- env prefix `GD05_`.

**Dev aids from day one (L6).** Copy gd02's set, renamed:
- `--song <id> --autoplay`, `--difficulty`, `--no-fail`, plus **`--part guitar|bass|drums|vocals`** and **`--players N`**;
- `GD05_KEYS` scripted input (menu keys and song keys, per player);
- `GD05_SCREENSHOT` / `GD05_SCREENSHOT_AT` (wall-clock ms, or `s<ms>` for song ms);
- `GD05_SCREEN`, `GD05_SKIP_BOOT`;
- `GD05_SFXLOG`, `GD05_UILOG` (every unhandled script message, stub or unknown class is logged once, L1);
- `GD05_STATS`, `GD05_PERF`, `GD05_FRAMELOG`;
- `GD05_GRAPHICS_BACKEND` / `GD05_AUDIO_BACKEND`;
- **`GD05_VOCALS_FILE=<wav>`**: a recorded or synthesised voice fed to the pitch detector, so vocals can be tested headless.

**Headless tests (all of them take the disc path):**
- `--selftest`: data checks, plus every chart auto-played on **every part** (guitar, bass, drums with and without kick, vocals) at every difficulty: "all charts fully hit". Band runs check Overdrive, Unison, saves and Big Rock Endings.
- `--scenetest`: every Milo, texture and model parses exactly, to its end marker.
- `--scripttest`: every `.dtb` loads; the boot scripts run against stubs.
- `--uitest`: every screen and panel loads.
- `--sfxtest`: every sound and every `.mogg` decodes.
- `--videotest`: every `.bik` decodes to its last frame.
- `--chartest`: every character, outfit, instrument, rig and clip decodes; the creator's combinations build.
- `--venuetest`: every venue loads; every camera shot is checked; the result is the same at 60 and 240 fps.
- `--shadertest`: every shader compiles on GL, on GLES (`GD05_GLES=1`) and to SPIR-V for SDL3 / Vulkan.
- `--walktest <walk>` (`SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy`), with the walks:
  - boot, quickplay (each instrument), band_quickplay (4 players), lose, save_bandmate, pause, attract, back;
  - solo_tour, band_tour, tour_song, practice, tutorials, drum_trainer;
  - tug_of_war, score_duel;
  - rock_shop, character_creator, glitches;
  - enhanced, enhanced_backends (with its Restart Now step, `enhanced_backends_restarted` as gd02), cheats, mouse, gamepads, gamepads_drums, gamepads_vocals, stickyaxis.

**Launcher module (`docs/platform/adding-a-game.md`, "The launcher module"):**
- `gd_game_run`, `gd_game_explore` (characters in their rest pose, a "Show instrument" toggle, drum kits too) and `gd_game_ui_sounds`.
- The `GD05_MODULE` CMake option.
- `build_module_05` in `gd-engine-launcher/scripts/build.sh`, with the gd-engine-core libraries and OpenAL Soft's licence as the other modules have.
- `iso_signatures` in `game-info.yaml`: `format: xdvdfs`, `xex_title_id: "45410829"`. Check that the launcher recognises the disc in `gdul.log` and `--list`.

**Port extras (`docs/guidelines/`):**
- the Enhanced menu in the universal layout (`universal-menus.md` 1.2.2), with **Graphics backend** and **Audio backend** as the first rows of Performance & Graphics, the Restart Now / Later pop-up, and the backend label above the FPS counter;
- the GAMEPADS screen (`universal-menus.md` 1.2.1):
  - **the first game with four players and drums and vocals**, so players 3 and 4 are available;
  - the Drums and Vocals instrument types and the Rock Band sections (small frets, effect selector) are shown;
  - keyboard and gamepad defaults as `universal_mechanisms.md` 1.1.4;
- mouse, touch and Enter-is-Start;
- the help bar's access buttons. Rock Band's screens vary, so propose a placement to the owner with suggestions (`universal_mechanisms.md` 1.1.3);
- The Glitches; cheats (the port's, then the original's with their names and codes);
- the Steam Deck preset, windowed by default, Enhanced defaults off (the original look);
- HDR as the other games (`universal_mechanisms.md` "HDR").

**Platforms:**
- Linux (portable and Steam Deck);
- Windows (MinGW, tested under Proton's wine with its own `WINEPREFIX`: `"$HOME/.local/share/Steam/steamapps/common/Proton - Experimental/files/bin/wine"`);
- Android arm64. The 7.8 GB disc is read **in place** through the picker's fd, never copied.
- macOS builds only if a toolchain exists; never claim it was tested.

**Docs (`gd-game-05/docs/`):** `FINDINGS.md` (gameplay rules, with addresses), `formats.md`, `audit.md`, `parity.md` (section 8), plus one file per subsystem:
- renderer;
- highways and HUD;
- vocals;
- drums;
- UI;
- venues;
- audio and charts;
- band and characters;
- tour and Rock Shop.

Keep them current: they are the memory between sessions.

## 6. Milestones (in order; each one ends at a gate, section 7)

Estimates are for planning; write yours in `PROGRESS.md` and measure them.

**M0: set-up (≈45 min).**
- Copy gd-game-02's engine, tools and build scaffolding into `gd-game-05/`, renamed (gd02→gd05, GD02_→GD05_).
  - Keep the Harmonix layers.
  - Remove GH2-only game code that RB replaces only when its replacement lands.
- Write `agents/CONTEXT.md` (build, test, run; the clocks), and add `PROGRESS.md`, `DECISIONS.md` and `OPEN_QUESTIONS.md`.
- Fill `gd-game-05/README.md` (launch date, songs, venues, characters from the data), point `agent-instructions.md` at this kickoff (its "plan it with the owner" step is this file), and start `CHANGELOG.md` at 0.0.1.
- Rip the disc into `extracted/`.
- Import `default.xex` into Ghidra, analyse it, export `ghidra/analysis/`, and add the Ghidra scripts.
- `game-info.yaml`: `iso_signatures` (title ID `45410829`), version 0.0.1; the engine's `project(gd05 VERSION 0.0.1)`.
- Backlog section "gd-game-05": replace "Discovery" (GD05-001) with the milestone rows.
- Gate: it builds with 0 warnings and boots to a log line that lists the archive.

**M1: data layer (≈1.5 h; most of it is gd02's).**
- ARK, DTA and the cipher, confirmed for RB.
- Milo for RB's versions and every class on the disc; textures; `.mogg` with RB's track maps; MIDI with every RB track; the vocal files; Bink.
- Python tools byte-exact: `ark.py`, `dtb.py`, `milo.py`, `tex.py`, `mogg.py`, `midi_chart.py`, plus `voc.py` / `vocals.py`.
- Gate:
  - `--scenetest` parses **every** Milo to its end, with no unknown classes;
  - every texture decodes, every `.dtb` loads;
  - `--videotest` decodes every `.bik`.

**M2: songs, audio, gameplay core (≈2.5 h).**
- The song list and metadata, charts for every part and difficulty, the `.mogg` stem mix (and each part's miss mute), sound effects.
- The rules from the XEX, rendering-free (L17):
  - hit windows, HOPOs, sustains and whammy for guitar and bass;
  - solo frets and solos;
  - drums (pads, kick, fills, freestyle);
  - the vocal pitch detector and its scoring per difficulty, talkies and percussion;
  - Overdrive per instrument, the band multiplier, Unison, saves, failing out, Big Rock Endings, stars.
- Gate:
  - `--selftest` "all charts fully hit" for every song, part and difficulty, plus the band checks;
  - `--sfxtest` decodes every bank and `.mogg`;
  - the rules are in `FINDINGS.md` with addresses.

**M3: script interpreter, UI, the asset audit (≈2 h).**
- gd02's DataArray interpreter running RB's own scripts (L1), with every RB native and handler implemented, each logged once when unhandled.
- Menus as the original builds them: screens, panels, text, fonts, transitions forward and back, sounds (never silenced).
- The **asset audit** (`--audittest`): every name referenced in the scripts and Milos against what the engine resolves. Write the gaps and the missing-native count to `docs/audit.md` and the backlog.
- Gate:
  - `--scripttest` and `--uitest` pass;
  - `--walktest quickplay` goes from boot (the Bink logos skippable) through the menus into a song and on to the results screen.

**M4: highways and HUD, exact (≈2 h).**
- Guitar and bass highways, the drum highway (the kick line, fills, Overdrive deploy zones), the vocal track (the pitch arrow, lyrics, phrase markers, talkies, percussion);
- gems, sustains, solos, Big Rock Endings, Overdrive (the glow, the meter, deploy);
- the band meter, multipliers, score, the fail and save states;
- one to four players laid out as the original.
- Song start order (section 4 rule 9).
- Gate: walk counters for every effect; screenshots at known song times look right for 1, 2 and 4 players.

**M5: venues, crowd, cameras, lighting (≈2 h).**
- Every venue (arena, big club, small club, theater, the shared sets) with its animated objects, lights, effects (post effects with an "off" fallback, L5), pyro and the crowd.
- The camera director driven by the venue track, with exact transforms and the fixed step; the intro moves from the first frame **through the menus**.
- Gate: `--venuetest` loads every venue, checks every camera of every venue, and gives identical results at 60 and 240 fps.

**M6: the band and the character creator (≈2 h).**
- Characters as the creator builds them (body, face, hair, outfits, tattoos and make-up, instruments, the drum kit), skinning, the instruments' IK (fret hands on the neck, sticks on the pads).
- Animations driven as the original (song events, per-part anim sets), lip sync from the vocal files.
- Gate: `--chartest` decodes every character part and animation and checks golden poses; no band member vanishes from any shot.

**M7: modes, tour, Rock Shop, saves (≈2 h).**
- Solo Tour, Band World Tour, quickplay, practice, the tutorials, the Drum Trainer, Tug of War, Score Duel.
- The Rock Shop and unlocks, the original's cheats.
- Saves as versioned JSON (L12).
- **The Glitches** with everything unlocked and bought.
- Gate: the walks in section 5 for every mode.

**M8: the port's extras (≈1.5 h).**
- Enhanced (backends first), GAMEPADS with four players and drums and vocals, mouse and touch, cheats, the Steam Deck preset, the windowed default, HDR, calibration. All exactly per `docs/guidelines/`.
- Gate: walks `enhanced`, `enhanced_backends` (Restart Now included), `cheats`, `mouse`, `gamepads`, `gamepads_drums`, `gamepads_vocals`, `stickyaxis`; every backend pair plays a song (local / local, OpenGL3 / miniaudio, SDL3 / OpenAL, Vulkan / FAudio).

**M9: launcher module and packages (≈1.5 h).**
- `gd_game_run`, `gd_game_explore`, `gd_game_ui_sounds`; `GD05_MODULE`.
- `build_module_05`; `games.yaml`; `ready_to_play` only when it plays from the launcher.
- Windows DLL dependencies bundled (FFmpeg with Bink, libwinpthread, OpenAL Soft).
- Android reads the disc in place. The microphone needs the `RECORD_AUDIO` permission in the launcher's APK (`gd-engine-launcher/android`): outside your folder, so ask the owner (section 9).
- Gate:
  - section 7's end-of-run checks;
  - the launcher recognises the disc;
  - the Linux launcher and Wine run the game 25 s and exit 0;
  - **Restart Now** inside the launcher switches backends;
  - a disc under a path with spaces, accents and brackets loads (`--iso`, drop and save, the remembered disc).

**M10: close the gaps (all remaining time).**
- Re-run the audit and work down its rows, then the parity ledger, then the remaining "Original" rows, highest player-visible impact first.
- Write the final report.

## 7. Gates: what "done" means for each milestone

A milestone is done only when:
1. `cmake --build` and the full `./local_build.sh` are clean (0 warnings) on every platform;
2. every test of the milestone, and every earlier test, passes **on a fresh binary**, with the output pasted into `PROGRESS.md`;
3. the asset audit has no new unexplained gaps, and the missing-native count did not grow;
4. `repo_guard.py` reports 0 problems;
5. the backlog is updated (rows done, rows found);
6. it is committed with `-s` and pushed;
7. no game processes are left running.

At the very end, also:
- rebuild all the local packages (`gd-engine-launcher/scripts/build.sh all --force`). `verify_bundle` must list your module in linux, steamdeck, windows and android-arm64 (usual-bugs.md, never again);
- play your game from the Linux launcher (`--play gd05 --exit-after` with `GDUL_GAME_QUIT_AFTER=25000`) and under Wine;
- write the final report at the top of `PROGRESS.md`:
  - what works;
  - what's approximate;
  - what's missing;
  - a confidence level per OS, each with 🟢 / 🟡 / 🔴.
- Set `game-info.yaml` `completeness` and `ready_to_play` honestly.

## 8. Reaching 100%: what the earlier games taught

gd-game-02 and gd-game-03 reached 99% only after several owner waves that
closed what the first run left open. gd-game-04 sits at 96%, with gaps in
`docs/tasks/PARITY_GAPS.md`. Do the following from day one, so that Rock
Band's waves are short:

- **A parity ledger, `gd-game-05/docs/parity.md`.** One row per area:
  - boot and menus, highways, each instrument's rules, vocals, band mechanics;
  - venues, crowd, lighting, cameras, the band, the creator;
  - each mode, saves, audio mix, video.
  
  Each row says [C] (matched in code, with addresses), [I] (inferred), or missing, with its backlog ID. "Done" means no [I] in gameplay rules (L2) and no missing rows. This is the file the owner reads to judge 100%.
- **Natives and handlers to 0.** The audit counts every DataArray handler and native the scripts call that the engine lacks. gd04's 678 missing natives were the root of most of its menu gaps.
- **Shaders from the disc's own programs.** One approximated shader for every venue or character (gd04 GD04-007 / -008) was the biggest visual gap. Port each material's maths from `xbox_shaders` / `shaders.dtb`, in our own GLSL (L15).
- **Walk every screen and mode, not only the main path.** gd03 closed its last gaps by walking the unvisited screens: the final-song choice, the stats screens, the options extras, the controller pause, the store. List every screen in the scripts and give each a walk act.
- **The owner's first checks.** These came back on every game: check them yourself before each gate.
  - Fret hands on the neck.
  - Hair and lighting (no white hair, no washed-out characters).
  - Sustains that look like the original.
  - Note-pop and highway SFX.
  - Menus to the pixel.
  - Black flashes.
  - Store state per band.
  - The Star Power / Overdrive sound.
  - Practice sync.
  - Overstrum on a sustain.
  - Strums before the highway.
  - Held-key repeat.
  - Enter as Start.
- **Calibration as GH1 (GDALL-038).** Audio and video offsets move both the drawing and the judging. With vocals, the mic latency too.
- **Mod support, prepared, not built.** The song library reads its list from the data (not a fixed table), so added songs can come in later (the owner's 1.1.13 "Preparation for mod support"). Don't load outside content in wave 01.

## 9. Questions for the owner (ask before or early in the run; record the answers here)

1. **Vocals on PC:** microphone capture (SDL3 or a gd-engine-core capture interface) is required for the original. Also offer keyboard or gamepad "vocals" as an Enhanced option, or leave vocals mic-only?
2. **Microphone capture in gd-engine-core:** add an audio *input* interface to the core (shared later by every Rock Band game), or keep it inside gd-game-05 for now?
3. **The help bar's access buttons** on Rock Band's varied screens: where do they go? (Propose two placements with screenshots.)
4. **Character creator scope in wave 01:** the full creator, or the premade characters first and the creator in wave 02?
5. **Hardware for testing:** the owner has a Rock Band guitar (`1bad:0002`). Is there a Rock Band drum kit and a USB microphone to test with, or do drums and vocals stay "built, tested headless only" in wave 01?
6. **Android microphone:** may the launcher's APK ask for `RECORD_AUDIO` (only when a game opens the mic)?
7. **A shared Harmonix library** (ARK, DTA, Milo, mogg, MIDI) for gd02 / gd03 / gd05: now, or after Rock Band reaches 99%?

## Owner's answers
- (none yet)
