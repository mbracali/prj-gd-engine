# gd02 handoff: context for Cursor

> Snapshot written before the M9 commit (663fa8d); the "State" section below is
> history now. Read `../README.md`, `../CHANGELOG.md` and `../docs/` for the current state.

## Project
- Repo `~/Development/prj-gd-engine`, public. Branch `feature/gd-game-02-initial`.
  - Pushed through M8 (`416a9b1`).
  - No PR yet.
  - main and develop are protected.
- `gd-game-02/` holds **gd02**, a C++17 / SDL3 / OpenGL 3.3 + GLES3 reimplementation of **Guitar Hero II (Xbox 360)**.
  - It loads the user's own disc image.
  - `gd-game-01/` (gd01, GH1 PS2) is finished. **Never modify it**; it is only a reference for code to port.
- Goal: a **100 % match with the original game**. Behaviour comes from GH2's code (Ghidra exports in `gd-game-02/ghidra/analysis/`, git-ignored). Mark findings **[C]** (confirmed, with address) or **[I]** (inferred) in the docs.

## Rules
- Commits: `git commit -s`, written as the user. **No AI co-author or footer** lines in commits, PRs or comments.
- Never commit game data, decoded assets, or decompiler output. Run `python3 .github/scripts/repo_guard.py` (it must report 0 problems).
- CI: only release builds run automatically. No PR CI, no Dependabot. Releases are tags `gd-game-02/vX.Y.Z` on main.
- Code style: match the surrounding code. New files start with the SPDX header. Log every unhandled script message (lesson L1).
- MinGW defines `environ` as a macro, so **never** name anything `environ`. Use `lightEnv` / `environName`.

## Build and test
Run from `gd-game-02/engine`.
- Quick build: `cmake --build build/linux`
- Full build: `./build.sh` (Linux, Windows via MinGW, Android). Must have 0 compiler warnings.
- Tests take the ISO as their first argument: `build/linux/gd02 "../../original_isos/Guitar Hero II (USA) (Rev1) (X360).iso" --<test>` (disc images live in the repo root `original_isos/`, git-ignored)
  - `--selftest`, `--scenetest`
  - `--shadertest`, also with `GD02_GLES=1`
  - `--scripttest`, `--uitest`
  - `--walktest quickplay|lose|pause|attract`
  - `--venuetest` (includes a golden camera/lighting log)
  - `--sfxtest`, `--facefxtest`, `--chartest` (the last one is new and may be incomplete)
- Dev env vars are listed in `docs/*.md`. Common ones: `GD02_VENUE`, `GD02_SKIP_BOOT`, `GD02_SCREENSHOT_AT`, `GD02_KEYS`, `GD02_UILOG`, `GD02_WORLDLOG`.

## Docs (read first)
- Formats and code: `gd-game-02/docs/`
  - `FINDINGS.md` (gameplay rules)
  - `renderer.md`, `milo_and_textures.md`, `highway_and_hud.md`
  - `ui_and_frontend.md`, `venues.md`, `audio_and_charts.md` (section 4 is the sound effects)
  - `facefx.md`, `reuse_and_lessons.md` (the backlog is `gd-backlog.md` at the repository root)
- M9 plan and reference parsers, in `agents/scratch/` (git-ignored, local only):
  - `M9_PLAN.md`: class table, addresses, and steps M9.0–M9.10.
  - `M9_runtime_notes.md`
  - `f1/clips.py` (clips, `--pose`), `f2/rig.py` (rig and venue slots), `f3/facefx.py`.
  - Every instance on the disc parses exactly. Use these to check the C++ against.

## State (uncommitted in the working tree)
1. **XMA sound effects: done.**
   - `scene/synth_objects.*`
   - `audio/{xma_decoder,sfx_player,synth,sfx_dsp,audio_device,xaudio_reverb}.*`
   - `tests/sfxtest.*`
   - FFmpeg is built with the xma1/xma2 decoders (LGPL, `build_common.sh`).
   - Still not exact: the 5.1→stereo fold (ITU used), the console's resampler (linear used), and the master fade to −96 dB in 0x822D50E0.
2. **FaceFX lip sync: done**, standalone in `char/facefx.*` and `tests/facefxtest.*`.
   - It is not attached to the characters yet. The API is at the end of `docs/facefx.md`.
3. **M9 band core: partial**, from an agent stopped while writing the character viewer.
   - Files: `src/char/*`, `game/band.*`, `render/band_view.*`, `render/char_view.*`, plus edits to `objects.*`, `app.cpp`, `world.*` and `camera_director.*`.
   - **Known breakage to fix first:**
     - **Windows build:** members named `environ` in `char/character.h:101`, `render/band_view.cpp:44`, `render/char_view.cpp:121` and `game/band.cpp:482`. Rename them.
     - **Walktest crash:** `--walktest quickplay|pause` gets SIGSEGV after the song (at saving_screen). `Interp::send` delivers `do_pick_expression` to a freed object. Kill that object's tasks on unload (`TaskMgr::killFor`) and unregister it.
     - **Stubs:** `--walktest attract|lose` fail on `guitarist0::hand_flames_L find` and `lip.servo do_pick_expression`. Implement both; the servo should use the FaceFX `ExpressionScript` / `LipSync`.
     - **Golden log:** `--venuetest` director_golden gives 326 lines instead of 328 and a different hash. If the real band (or the camCheckShot fix below) changes the shot picks for a good reason, verify that and regenerate the golden.
   - **Still to check:**
     - Whether M9.0–M9.5 are complete: the `--chartest` counts, CharDriver, the pose pipeline, MidiParser and hands, GuitarCharacter and the band, and the golden clip log.
     - The camCheckShot fix: `0xc0` is the waypoint flag mask (kWalkSpot | kSoloWalkSpot), not a radius (Waypoint::Nearest 0x82195548).

## What's left after that
- Verify everything, then commit and push. M9 core, sound effects and FaceFX can go in one commit or several.
- **M9.6** walks and interacts.
- **M9.7** the camera director using the real band: bone targets, clip flags 0x80000 / 0x4000, nearest waypoint.
- **M9.8** faces on the band (FaceFX API).
- **M9.9** GH2's crowd cards: render-to-texture cards plus 3D near the camera, and crowd clips. The owner chose to match the original here.
- **M9.10** character, guitar and skin choice (the profile field is not traced yet); endgame photos from `ui/image/og/photo_<char><0..3>_keep.bmp`.
- Then:
  - Update `docs/band.md` (new), `gd-backlog.md` (root) and `CHANGELOG.md`.
  - M10: modes (co-op, face-off, practice, career and encores, store).
  - M11: polish and the first release.
- Other open items:
  - macOS has never been built.
  - The Windows build and APK have never been run on real hardware.
  - The workflows have never been run.
  - FFmpeg libraries are required at startup; better to load them only when a video plays.
  - GH2's beam shader and depth fog are not reproduced.
