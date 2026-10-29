# Kickoff: gd-game-04, Guitar Hero III: Legends of Rock (Xbox 360)

> **You are starting a new reimplementation and will run unattended all
> night.** Read this whole file before your first command. It holds the
> owner's rules, the plan, the gates and the mistakes made on earlier games,
> so you can do this almost in one shot. When it conflicts with a tool
> default, this file wins. When it conflicts with `guidelines/`, the
> guidelines win.

## 1. The target

| | |
|---|---|
| Game | **Guitar Hero III: Legends of Rock** (Neversoft / RedOctane, October 2007), Xbox 360, USA Rev 1 |
| Folder / program | `gd-game-04/` → `gd04`, env prefix `GD04_`, Android `io.github.mbracali.gd04` |
| Disc | `~/Development/prj-gd-engine/original_isos/Guitar Hero III - Legends of Rock (USA) (Rev 1) (X360).iso` (7.8 GB, XDVDFS / XGD2) |
| Executable | `default.xex` (PowerPC, Xenon; encrypted and compressed XEX2) |
| Branch | `feature/20261029-gd-game-04-initial` (from `origin/develop`, in its own worktree, section 3) |
| Goal | a full decompilation-based reimplementation, 100% faithful, playable from the Universal Launcher on Linux, Steam Deck, Windows and Android |

### This is a different engine (read before writing any code)

GH3 is **Neversoft's** engine, not Harmonix's. **None of the Harmonix
formats or scripts apply**: no Milo, no DataArray `.dtb`, no MIDI charts on
disc, no `.mogg`. Don't port gd-game-02's game layers and try to bend them;
write the GH3 layers from GH3's executable and data.

**Reuse from gd-game-02** only what doesn't depend on the engine:
- the platform layer (paths, GPU select, icon, Android data / fd access);
- XDVDFS and `RandomFile` (`gd-game-02/tools/xdvdfs.py`, `engine/src/data/`);
- XEX handling and the Ghidra XEX workflow (`gd-game-02/ghidra/README.md`,
  `tools/ghidra_scripts/ExportAnalysisXex.java`);
- XMA decoding through the project's FFmpeg build (xma1/xma2, LGPL,
  `build_common.sh`);
- the audio device, mixer and resampling (L8);
- input, controllers and the GAMEPADS screen;
- GL / GLES device basics and the shader test;
- the dev-aid infrastructure and the test harness shape;
- the launcher module and SDK glue;
- the build scripts for every platform.

Expect these Neversoft formats, and **confirm each against the disc and the
XEX** (offsets, endianness, hashing). Write each confirmation in
`docs/FINDINGS.md` and the format docs, as [C] / [I]:
- **Archives:** `.pak.xen` + `.pab.xen` pairs; entries keyed by **QB
  checksums** (CRC32-based name hashes).
- **Debug names:** build a checksum → name table from every name the disc
  and the XEX reveal (debug paks, script strings), so the logs stay
  readable.
- **QB:** compiled QScript (`.qb.xen`): structs, arrays, script bytecode.
  Gameplay flow, menus, career, the store and much tuning live in QB.
  Following lesson L1, **run the original scripts through your own QScript
  interpreter**; don't hand-port them. Log every unhandled script function
  or C-function call once.
- **Charts:** per song in its pak, as QB arrays: notes (time, length, fret
  mask) per difficulty and part (guitar, rhythm / bass, co-op), Star Power
  phrases, battle-star (boss / battle) phrases, face-off sections, fretbars,
  time signatures, markers, lighting and camera events.
- **Audio:** FSB4 banks (`.fsb.xen`), XMA. The song stems (guitar, rhythm,
  song, plus the co-op variants) may be **encrypted or obfuscated**; find
  out in the XEX.
- **Textures and scenes:** `.img.xen` / `.tex.xen` (360-tiled DXT, endian
  swapped), `.scn.xen` / `.mdl.xen` / `.skin.xen` (geometry, materials,
  skinning), `.ska.xen` animations, `.col.xen`.
- **Fonts:** `.fnt.xen`. **Video:** check what's on the disc (Bink or WMV).

You may read publicly documented format notes from the modding community to
speed up, but **don't copy any third-party code**. Write our own decoders,
validated byte-exact (L21), and confirm every rule in the XEX: community
notes describe the PC version and may differ.

**GH3 features to reproduce** (verify the full list from the data):
- Career: single player with tiers, encores, **boss battles** (Tom Morello,
  Slash, Lou) and cash.
- **Co-op career**, quickplay, practice (sections, speed).
- Multiplayer: face-off, **pro face-off**, **battle mode** (battle
  power-ups thrown with Star Power: Amp Overload, Broken String, Difficulty
  Up, Double Notes, Lefty Flip, Whammy, Steal, Death Drain), **co-op
  quickplay**.
- Tilt / Select Star Power, whammy, HOPOs with GH3's own tolerance rules,
  sustains, the rock meter.
- The store (the Vault: songs, guitars, finishes, characters, outfits,
  videos), unlocks, cheats (the original's codes).
- Online, leaderboards and achievements: stub them, logged, and label them
  in the backlog as "not in the port".

Known Ghidra setup on this machine (from gd-game-02):
- Ghidra 12.1.4 at `~/.local/opt/ghidra_12.1.4_PUBLIC`, JDK 21 at
  `~/.local/opt/jdk-21.0.12.1+1` (`export JAVA_HOME=...`).
- The **XEXLoaderWV** extension is installed in Ghidra's Extensions:
  import `default.xex` with it.
- Add `DumpAsm.java` / `ReadF.java` (section 4, rule 4).
- Export the functions, strings, refs and pseudocode into `ghidra/analysis/`
  as gd-game-02 did. Name and comment functions as you confirm them (the
  QB C-function table is the first big map: script names → native
  addresses).

## 2. How you work tonight (autonomous mode)

The owner is asleep. **Do not stop to ask questions and do not wait for
answers.** Work until every milestone in section 6 is done, or until you're
truly blocked on all of them.

- **Decisions.** When something would normally be the owner's choice, pick the
  original game's behaviour. When the original is unknown, pick the gd-game-02
  pattern. Write the choice in `gd-game-04/agents/DECISIONS.md`: what was chosen,
  why, and how to undo it. Write questions for the morning in
  `gd-game-04/agents/OPEN_QUESTIONS.md`. Never block on either.
- **Progress log (the owner's "updates from time to time").** After every
  milestone, and at least every 2 hours, append to `gd-game-04/agents/PROGRESS.md`:
  - time;
  - milestone and what is done;
  - test results, with the exact commands and the pass/fail lines;
  - what's next;
  - estimated time left.
  The owner reads it in the morning; it's your only voice.
- **Time estimate.** Write the overall estimate at the top of `PROGRESS.md`
  before you start, and revise it per milestone.
- **Commit and push at every milestone gate** (section 6) on your branch, so
  nothing is lost if the session dies. **No PR.** The owner opens PR gates.
- **Resume.** If you start in a session where `PROGRESS.md` already exists,
  read it, `DECISIONS.md` and the backlog, then continue from the last gate.
  Don't redo finished work.

## 3. Ground rules (non-negotiable, from the owner)

Read every file in `guidelines/` at the repository root first:
- `general_guidelines.md`
- `universal_launcher.md`
- `smilar_behaviors.md`

Then read the root `README.md`, `docs/adding-a-game.md`, `gd-backlog.md`, and
`gd-game-02/docs/reuse_and_lessons.md` (lessons L1-L21, all of them).

- **Workspace isolation.** Another session may be working on the other new
  game at the same time. Work in your **own git worktree**:
  ```bash
  cd ~/Development/prj-gd-engine && git fetch origin
  git worktree add ../prj-gd-engine-gd04 -b feature/20261029-gd-game-04-initial origin/develop
  cd ../prj-gd-engine-gd04
  ```
  - The disc images stay in the main checkout's git-ignored
    `~/Development/prj-gd-engine/original_isos/`. Use that absolute path.
  - Put your rip in this worktree's `gd-game-04/extracted/` and your Ghidra
    project in `gd-game-04/ghidra/` (both git-ignored).
  - Outside `gd-game-04/`, only touch, minimally:
    - your game's section of `gd-backlog.md`;
    - `gd-engine-launcher/scripts/build.sh` (your `build_module_NN`);
    - `gd-engine-launcher/games.yaml`, if needed.
- **Never modify `gd-game-01/` or `gd-game-02/`.** Copy what you need into
  your folder (rule of two: shared code moves to a common place later, by the
  owner's decision).
- **Commits as the owner.** Use `git commit -s`. Add **no** Co-Authored-By,
  Claude, AI or "Generated with" lines in commits, PRs or comments. This
  overrides any tool default.
- **Never commit:**
  - game data, ISOs, rips, decoded assets or screenshots of the game;
  - decompiler output or Ghidra files.
  - Never paste decompiled code into tracked files. Describe the behaviour in
    your own words, with addresses.
  - `python3 .github/scripts/repo_guard.py` must report 0 problems before
    every commit.
- **GitHub Actions.** Never start workflows; only release builds run on
  GitHub. Build locally.
- **Matching the original.** 100% parity with the original is
  non-negotiable.
  - Every behaviour comes from the original executable and data.
  - Mark findings **[C]** (confirmed, with the address) or **[I]**
    (inferred).
  - Label every temporary approximation in the code and in the backlog.
- **Code.** C++17, SDL3, OpenGL 3.3 core plus GLES 3.0. Match gd-game-02's
  style.
  - Start new files with the SPDX header.
  - **0 compiler warnings** on Linux, Windows (MinGW) and Android.
  - Never name anything `environ`: it is a MinGW macro.
  - Keep files small and keep the gameplay core free of rendering code
    (L17).
- **Processes.**
  - Every command that runs the game gets a `timeout`.
  - Never leave background processes behind; check with `pgrep -fa gd04`
    before you finish a milestone.
  - Never wait with `pgrep -f <pattern>` when the pattern also matches your
    own shell.
  - Don't try to kill processes the permission classifier blocks; note them
    in `PROGRESS.md` for the owner.
  - Subagents must clean up after themselves too.
  - Don't work around a blocked command: find an allowed way, or note it and
    move on.
- **Subagents.** Use them only for independent research or parallel
  verification. Give each a precise brief, and **verify their claims
  yourself**: an agent's "done" isn't done until your own test passes. Keep
  at most about 4 running at once.

## 4. The mistakes made on gd-game-02 — do NOT repeat them

These cost the owner days of back and forth. Each one is now a rule.

1. **Test the real user path, not only the dev shortcut.**
   - What went wrong: the venue intro camera worked with `--song` but stood
     still when the song was started from the menus. The world clock was set
     after the intro shot was picked. It was reported three times.
   - The rule: every feature is verified through **the menus**
     (`--walktest`), through the launcher (`--play gd04`), and on Windows
     under Wine. `--song` is only a shortcut while you develop.
2. **Never test a stale binary.**
   - Rebuild, then test. Check the binary's timestamp is newer than your
     edits.
   - Know the exact test invocation, and write it in
     `agents/CONTEXT.md` the first time it works. Tests take the disc path
     as an argument; a test that prints its usage text **has not run**.
3. **Audit the data for things we never implemented.**
   - What went wrong: GH2's Star Power phrase lightning (`combo1` / `combo2`
     in `track_graphics.dtb`) was never drawn; nobody noticed until the
     owner did.
   - The rule: build an **asset/reference audit** early (milestone M3).
     - List every object, view, anim, sound and config key the game's data
       and scripts reference.
     - List which ones our engine never resolves, plays or draws.
     - Every unreferenced item becomes a backlog row (implement it, or
       prove the original never uses it). Re-run the audit at every gate.
4. **The decompiler drops arguments: read the assembly.** Ghidra's output
   lost the argument that carried the "phrase captured" state. Save a
   headless asm dumper and a float/data reader as Ghidra scripts in
   `gd-game-04/tools/ghidra_scripts/` on day one:
   - `DumpAsm.java` (address:count) and `ReadF.java`.
   - Run them with `analyzeHeadless <abs project dir> <name> -process
     -readOnly -noanalysis -postScript ...`. The project path must be
     absolute, with no `.` element.
   - Read call sites in assembly whenever a parameter matters.
5. **Know your clocks.**
   - The track clock, the audio clock, the sfx log and the screenshot
     timestamps are different domains: gd02's track ran 180 ms ahead of the
     sfx log, which hid a working effect.
   - Document each clock's offset in `docs/` and in the dev-aid help text.
6. **Draw order and blending are behaviour.** The Star Power glow
   disappeared because a new solid tail was drawn after it. Port the
   original's traversal order (L14), and give every visual fix a counter or
   a check in a walk, so it can't silently regress.
7. **Every fix gets a regression check.**
   - It goes into `--selftest`, `--walktest`, `--venuetest` or a dedicated
     test, as headless as possible.
   - Visual features get a counter the walk can assert, for example
     "effect X started N times".
8. **Camera and animation transforms must be exact.**
   - Look-at, roll, path splines and scale keys, and the parent order of
     transforms. Getting these wrong gave upside-down and under-the-floor
     shots.
   - Check every camera shot of every venue headlessly: no NaN, not inside
     geometry, target in front.
   - Everything runs on the original's fixed step at any frame rate.
9. **Song start order.** Venue first, then the intro shot (moving from the
   first frame), then the highway intro with its sounds, then the music.
   Nothing counts before the highway is live.
10. **Menus.**
    - Menu sounds must never go silent: whoever lowers a volume restores
      it.
    - Typed names grow left to right.
    - Back plays the backward transition.
    - Screens must not darken when you go back and forth.
    - Locked items show as locked.
    - Mouse works everywhere; Enter is Start.
    - GAMEPADS and Enhanced menus follow `guidelines/universal_launcher.md`.
11. **The Glitches** band has **everything** unlocked in every build.
12. **Packages.** A launcher build missing a module, or a launcher that
    can't open ISOs (Android 15 content URIs, Windows DLL dependencies such
    as `libwinpthread-1.dll`), must never happen again. `verify_bundle.sh`
    must report your module in **every** platform's bundle.
13. **Don't claim what you didn't run.**
    - In `PROGRESS.md`, separate "verified" (command plus output) from
      "built only" and "not tested".
    - When you aren't sure what the original does, say so and add it to
      `OPEN_QUESTIONS.md`.

## 5. The pattern to follow (gd-game-02 is the model)

Folder layout as in `docs/adding-a-game.md`:
- `README.md`, `CHANGELOG.md`, `game-info.yaml`, `local_build.sh`;
- `build_commands/`, `docs/`, `engine/`, `tools/`, `agents/`;
- the git-ignored `extracted/`, `ghidra/` and `local_builds/`.

Engine:
- program `gd04`, save folder `SDL_GetPrefPath("gd-engine","gd04")`;
- Android package `io.github.mbracali.gd04`;
- env prefix `GD04_`.

**Dev aids from day one (L6).** Copy gd02's set, renamed:
- `--song <id> --autoplay`, `--difficulty`, `--no-fail`;
- `GD04_KEYS` scripted input (menu keys and song keys);
- `GD04_SCREENSHOT` / `GD04_SCREENSHOT_AT` (wall-clock ms, or `s<ms>`
  for song ms);
- `GD04_SCREEN`, `GD04_SKIP_BOOT`;
- `GD04_SFXLOG`, `GD04_UILOG` (every unhandled script message, stub or
  unknown class is logged once, L1);
- `GD04_STATS`, `GD04_PERF`.

**Headless tests (all of them take the disc path):**
- `--selftest`: data checks, plus every chart auto-played at every
  difficulty ("all charts fully hit").
- `--scenetest` / format tests: every scene, texture and model file parses
  exactly, to its end marker.
- `--scripttest`: every script loads; the boot scripts run against stubs.
- `--uitest`: every screen and panel loads.
- `--sfxtest`: every sound decodes.
- `--chartest`: every character, rig and clip decodes.
- `--venuetest`: every venue loads; every camera shot is checked; the
  result is the same at 60 and 240 fps.
- `--shadertest`: every shader compiles on GL and on GLES
  (`GD04_GLES=1`).
- `--walktest <walk>` (`SDL_VIDEO_DRIVER=offscreen
  SDL_AUDIO_DRIVER=dummy`), with the walks:
  - quickplay, lose, pause, attract, back;
  - career, career_full, career_song;
  - enhanced, cheats, mouse, gamepads, stickyaxis;
  - every multiplayer mode the original has.

**Launcher module (`docs/adding-a-game.md`, "The launcher module"):**
- `gd_game_run`, `gd_game_explore` (Explore models: characters in their
  rest pose, a "Show instrument" toggle) and `gd_game_ui_sounds`.
- The `GD04_MODULE` CMake option.
- `build_module_NN` in `gd-engine-launcher/scripts/build.sh`.
- `iso_signatures` in `game-info.yaml`. Read the real values from the disc
  and check that the launcher recognises it in `gdul.log`.

**Port extras (`guidelines/`):**
- the Enhanced menu, in gd-game-01's layout (options left, band close-up
  right; dropdowns for multiple choice, checkboxes for on/off);
- the GAMEPADS screen (the yellow fret on the main menu opens it);
- mouse, touch and Enter-is-Start;
- The Glitches;
- cheats (the port's, then the original's);
- Steam Deck preset, windowed by default.

**Platforms:**
- Linux (portable and Steam Deck), Windows (MinGW, tested under Proton:
  `"$HOME/.local/share/Steam/steamapps/common/Proton - Experimental/files/bin/wine"`
  with its own `WINEPREFIX`), Android arm64.
- macOS builds only if a toolchain exists; never claim it was tested.

**Docs (`gd-game-04/docs/`):** `FINDINGS.md` (gameplay rules, with addresses),
plus one file per subsystem (formats, renderer, highway and HUD, UI, venues,
audio and charts, band, career). Keep them current: they are the memory
between sessions.

## 6. Milestones (in order; each one ends at a gate, section 7)

Estimates are for planning; write yours in `PROGRESS.md`.

**M0: set-up (≈45 min).**
- Create the worktree and branch.
- Fill the `gd-game-04/` layout from gd-game-02's skeleton (README,
  local_build.sh, build_commands, the engine's platform, build and test
  scaffolding), renamed (gd02→gd04, GD02_→GD04_). **No Harmonix game
  code.**
- Write `agents/CONTEXT.md` (build, test, run; the clocks).
- Add the `PROGRESS.md` / `DECISIONS.md` / `OPEN_QUESTIONS.md` files.
- Rip the disc into `extracted/` (XDVDFS files).
- Import `default.xex` with XEXLoaderWV, analyse it, export
  `ghidra/analysis/`, and add the DumpAsm / ReadF scripts.
- Read `default.xex`'s **title ID** for `game-info.yaml` `iso_signatures`
  (`format: xdvdfs`, `xex_title_id`).
- Backlog section "gd-game-04": replace "Discovery" with the milestone rows.
- Version 0.0.1.

**M1: data layer (≈3 h).**
- PAK/PAB reader, QB checksum hashing and the debug-name table, the QB
  decoder (every node type), textures, scenes, models, skins, animations,
  collision, fonts.
- Python tools in `tools/` decode **byte-exact** (L21): `pak.py`, `qb.py`,
  `img.py`, `scn.py`, `fsb.py`.
- Gate: `--scenetest` (or `--formattest`) parses **every** file of every
  type to its end with no unknown chunks; every texture decodes; the name
  table resolves the checksums the scripts use.

**M2: songs, audio, gameplay core (≈3 h).**
- Song list and metadata (QB), charts from the song paks, FSB4/XMA streams
  with decryption if any, the stem mixing (guitar / rhythm / song / co-op,
  the miss mute), sound effects.
- Gameplay rules from the XEX and QB tuning, rendering-free (L17):
  - hit window, HOPO tolerance, sustains and whammy;
  - Star Power (tilt and Select);
  - multiplier, rock meter, score;
  - battle and boss-battle power-ups, face-off and pro face-off;
  - co-op rules.
- Gate: `--selftest` "all charts fully hit" for every song, part and
  difficulty; `--sfxtest` decodes every bank; the rules are in
  `FINDINGS.md` with addresses.

**M3: QScript interpreter, UI, the asset audit (≈4 h).**
- The QScript VM running the game's own scripts (L1): structs, arrays,
  script calls, spawned scripts, waits, events, the C-function bindings
  implemented natively (each logged once when unhandled).
- Menus as the original builds them: element tree, screen elements, text,
  fonts, transitions forward and back, sounds (never silenced).
- The **asset audit tool** (`--audittest`, section 4 rule 3): every name
  referenced in QB, paks and scenes against what the engine resolves. Write
  the gaps to `docs/audit.md` and the backlog.
- Gate: `--scripttest`, `--uitest`; `--walktest quickplay` goes from boot
  (splashes and movies skippable) through the menus into a song and to the
  results screen.

**M4: highway and HUD, exact (≈2.5 h).**
- Fretboard, gems, HOPO gems, star gems, sustains, whammy, the hit and
  flame effects, the Star Power look (blue highway, lightning) and its
  activation, the rock meter, the star meter, multiplier and streak, score.
- The battle-mode HUD and attack effects; the boss-battle HUD.
- Song start order (section 4 rule 9).
- Gate: walk counters for every effect, and screenshots at known song times
  look right.

**M5: venues, cameras, lighting (≈3.5 h).**
- Every venue: scenes, animated objects, lights, effects (post effects
  with an "off" fallback, L5), crowd.
- The camera system driven by the charts' camera events, and its
  scripts: exact transforms, fixed step, the intro moving from the first
  frame **through the menus**.
- Gate: `--venuetest` loads every venue, checks every camera of every
  venue, and gives identical results at 60 and 240 fps.

**M6: the band (≈3 h).**
- Characters (including the boss and unlockable characters), outfits,
  instruments, skinning.
- Animations driven as the original (song events, the per-character anim
  sets); faces and lip sync if the original has them.
- Gate: `--chartest` decodes every character and animation and checks
  golden poses; no band member vanishes from any shot.

**M7: modes, career, saves (≈3.5 h).**
- Career and co-op career with tiers, encores, the bosses and cash;
  quickplay, practice, face-off, pro face-off, battle.
- The Vault (store) and unlocks; the original's cheats.
- Saves as versioned JSON (L12).
- **The Glitches** with everything unlocked and bought.
- Gate: walks `career`, `career_full`, `career_song`, `coop`, `faceoff`,
  `proface`, `battle`, `boss`, `lose`, `pause`, `back`, `attract`.

**M8: the port's extras (≈2 h).**
- Enhanced menu, GAMEPADS, mouse and touch, cheats, Steam Deck preset, the
  windowed default: exactly per `guidelines/universal_launcher.md`,
  modelled on gd-game-01.
- Gate: walks `enhanced`, `cheats`, `mouse`, `gamepads`, `stickyaxis`.

**M9: launcher module and packages (≈1.5 h).**
- `gd_game_run`, `gd_game_explore`, `gd_game_ui_sounds`; `GD04_MODULE`.
- `build_module_04` in the launcher's `build.sh`; `games.yaml`.
- Android: a 7.8 GB disc is read **in place** through the picker's fd,
  never copied.
- Windows DLL dependencies bundled.
- Gate:
  - section 7's end-of-run checks;
  - the launcher recognises the disc in `gdul.log`;
  - both the Linux launcher and Wine run the game 25 s and exit 0.

**M10: close the gaps (all remaining time).**
- Re-run the asset audit and work down its rows, then the backlog's
  remaining "Original re-implementation" rows, the highest player-visible
  impact first.
- Write the final report.

## 7. Gates: what "done" means for each milestone

A milestone is done only when:
1. `ninja`/`cmake --build` and the full `./build.sh` are clean (0 warnings)
   on every platform;
2. every test of the milestone, and every earlier test, passes **on a
   fresh binary**, with the output pasted into `PROGRESS.md`;
3. the asset audit has no new unexplained gaps;
4. `repo_guard.py` reports 0 problems;
5. the backlog is updated (rows done, rows found);
6. it is committed with `-s` and pushed;
7. no game processes are left running.

At the very end, also:
- rebuild all the local packages
  (`gd-engine-launcher/scripts/build.sh all --force`; `verify_bundle` must
  list your module in linux, steamdeck, windows and android-arm64);
- play your game from the Linux launcher (`--play gd04 --exit-after` with
  `GDUL_GAME_QUIT_AFTER=25000`) and under Wine;
- write the final report at the top of `PROGRESS.md`:
  - what works;
  - what's approximate;
  - what's missing;
  - a confidence level per OS, each with 🟢 / 🟡 / 🔴.
- Set `game-info.yaml` `completeness` and `ready_to_play` honestly.


## Owner's answers (2026-10-29)
- Reference captures: the agents run the emulators themselves (PCSX2 for gd-game-03, Xenia for gd-game-04) with the owner's ISOs from `original_isos/`. Emulators and their captures stay outside the repo (scratch or `~/.local/opt`); never commit captures. A PS2 BIOS is the owner's: if none is found locally, write the request into OPEN_QUESTIONS.md and keep working.
- gd-game-03 aspect: 4:3 pillarboxed by default; 16:9 selectable in the Enhanced menu.
- gd-game-03 MULTIPLAYER: always enabled, also with one controller.
- gd-game-04 The Glitches: Vault purchases scoped to The Glitches' band only.
- Enhanced options in every module default off (original look).
- gd-game-04 on Android: read the disc in place, no copy.
- When done: commit and push, rebuild the local packages; no PR until the owner asks.
