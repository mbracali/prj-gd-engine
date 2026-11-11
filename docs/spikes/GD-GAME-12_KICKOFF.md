# Kickoff: gd-game-12, Guitar Hero 5 (Xbox 360), wave 01

> **You are starting a new reimplementation.** Read this whole file before
> your first command. It is the short form of `GD-GAME-05_KICKOFF.md`
> (Rock Band), which stays the reference for anything not repeated here:
> its ground rules, the mistakes list, the pattern, the gates. When this
> file conflicts with a tool default, this file wins. When it conflicts with
> `docs/guidelines/`, the guidelines win.

## 1. The target

| | |
|---|---|
| Game | **Guitar Hero 5** (Neversoft / Activision, September 2009), Xbox 360, USA / Europe |
| Folder / program | `gd-game-12/` → `gd12`, env prefix `GD12_`, Android `io.github.mbracali.gd12` |
| Disc | `original_isos/Guitar Hero 5 (USA, Europe) (En,Fr,De,Es,It).iso` (7.8 GB, XDVDFS, game partition at `0xFD90000`) |
| Executable | `default.xex` (14.2 MB, PowerPC Xenon, XEX2), **title ID `41560840`**, internal name `gh5_burn_Final.exe`, CRC32 `25F6E170` [C, read 11-10] |
| Branch | the one the owner names (question 2). Never create or switch branches yourself |
| Goal | a full decompilation-based reimplementation, 100% faithful, playable from the Universal Launcher on Linux, Steam Deck, Windows and Android |

### Disc first look (2026-11-10, read-only)

Read with `gd-game-05/tools/xdvdfs.py list` and a scratch reader in
`/tmp/claude-1000/kick12/` (nothing extracted into the repo).

| What | Found |
|---|---|
| Partition | `0xFD90000`, 1,542 entries, 5.64 GB of files |
| Root | `default.xex`, `data/`, `AvatarAssetPack` (14 MB, Xbox avatars), `nxeart`, `$SystemUpdate/` |
| Archives | 970 `.pak.xen` (793 in `data/compressed/PAK`), 5 `.pab.xen`, `compress.toc.xen` + `compress.toc.lst.xen` |
| Compression | files under `data/compressed/` start with **`CHNK`**: a chunked container, each chunk raw deflate inflating to 512 KB [I, one file probed]. GH3's `pak.py` (whole-file deflate) does **not** read it: new in M1 |
| Songs | 155 `*_song.pak.xen` in `data/compressed/SONGS`: **86 set-list songs**, 9 `_MM` variants (meaning to find), 32 tutorial charts, 28 debug / sync charts |
| Song audio | 472 `.fsb.xen` in `data/music`: 118 songs (set list + tutorials) × `_1`, `_2`, `_3`, `_preview` |
| Venues | 14 zones in `data/compressed/ZONES` (`Z_Cairo` … `Z_Vegas`), plus `Z_Visualizer`, `z_studio`, `z_viewer`, `z_in_game`, `global*` |
| Characters | `data/pak/archive/cas_pieces.pab.xen` (532 MB, the Rockstar Creator parts), `cas_globalarchive`, 165 `L_*` per-player anim paks |
| Other | 58 `musicstudio*` paks (GH Studio / GHMix), `JAMS/`, `FXFILES/MaterialLibrary.bin.xen`, `scripts/engine/engine_params.qb.xen` |
| Video | 39 Bink files (`data/movies/*.bik.xen`, `data/moviestreams/*.bik`) |

**Disc vs the community notes.** Our XEX has the same title ID (`41560840`)
and the same CRC32 (`25F6E170`) as the build described in
`docs/spikes/colabs01.md`, so their addresses should map 1:1. Still verify at
M0: re-read both values from the rip and log them in `docs/FINDINGS.md`.

### Engine lineage: where to start

GH5 is the **Neversoft engine after GH3 and World Tour**: `.pak.xen` /
`.pab.xen` archives keyed by QB checksums, compiled QScript, FSB4 audio,
`.img.xen` textures. Band Hero (gd-game-14) is about 99.3 % the same code and
Warriors of Rock (gd-game-17) builds on it, so **GH5 is the parent of its
family**: design the engine so 14 and 17 can follow as deltas.

The owner's overall order: Metallica (gd-game-10, the World Tour engine) →
Van Halen / World Tour → **GH5** → Band Hero → Warriors of Rock.
- **Ideal start:** after gd-game-10 exists. Copy its engine (World Tour era:
  drums, vocals, band, the creator) into `gd-game-12/engine/`.
- **Sooner, if the owner wants (question 1):** copy gd-game-04's engine (GH3,
  0.96): XDVDFS, pak / QB / FSB / img layers, the QScript VM, highway, venues,
  the universal menus. Then take band, drums and vocals from gd-game-05 as
  reference (section 3).

Either way: rename gd04→gd12 (or gd10→gd12), GD04_→GD12_. **Assume nothing,
confirm everything.** Two generations after GH3 means new QB versions and
script functions, the `CHNK` compression, new chart tracks (drums, vocals,
open bass notes, slider / tap notes), a new song-audio layout (three FSB
banks per song), Party Play and a new save format. Every rule, constant and
layout comes from GH5's `default.xex` and disc, written in `docs/FINDINGS.md`
as **[C] address**. A copied GH3 rule not re-confirmed in GH5's code is
**[I]** and blocks "done" (L2).

### Knowledge from the community recompilation (clean-room)

`docs/spikes/colabs01.md` studied a community static recompilation of GH5.
We take **knowledge, never code**:
- **Addresses and script ids are Ghidra leads only.** The ~60 named
  addresses (frame loop, timer, vblank, skinning, composite, shadow filter,
  the element table) and the QB checksums (main menu, Party Play, the dialog
  box, star-power drain, the drummer) go into Ghidra as labels **after** you
  verify each on our XEX. Log each in `FINDINGS.md` as [C] (verified, our
  address) or drop it.
- **The original's quirks to confirm and decide:**
  - a **49.875 MHz timebase** in the timer: ≈0.25 % (≈150 ms a minute) of
    drift. Our song time follows the audio clock; never port the constant;
  - the scene **drawn over two frames** (stage and crowd, then band and
    effects). We draw one; record what the split changes on screen first;
  - **per-effect flags** in screen FX (bloom, DOF from a 320×180 depth
    target, grain, colour correction, vignette); **shadows** from a 400×400
    map, 9-tap filter; CPU skinning from a 128-matrix palette;
  - **above 60 FPS**, all that counts frames (vblank clocks, `Wait N
    gameframes`, whammy, strings, loading anims, fret fall-back, cloth) stays
    at the original's rate on our fixed step;
  - **ultrawide:** Hor+ 3D, 2D and HUD 16:9 in the centre, side bars outside
    songs, the rock meter border full width, the highway's depth mask;
  - the **DLC manifest** (`bmanifest_*.pak.xen` in the player's packages):
    spec only in wave 01; fast character switching cancels model loads (a
    loader test case).
- **Clean-room rule (non-negotiable).** No code copied or adapted. No names
  of other projects or their maintainers in our code, in `gd-game-12/` docs or
  in commits; refer to them neutrally only through `docs/spikes/colabs01.md`.
  Every fact is re-found on our disc and written in our own words. If any code
  is ever adapted, stop and ask the owner (its notice would go to `licenses/`
  only).

### GH5 features to reproduce (verify the full list from the data)

- **Full band, 1–4 local players:** guitar, bass, drums (five pads, two
  cymbals, kick), vocals (microphone); any combination, including several of
  one instrument; **Party Play** and drop-in / drop-out; every difficulty
  including Beginner and Expert+ if the data has it.
- **Mechanics** (each rule from the XEX, with addresses): HOPOs, sustains,
  whammy, open bass notes, slider / tap notes, Star Power per player and the
  **band** Star Power, Band Moments, multipliers, solos, the rock meter, saves
  and failing out, stars and score.
- **Modes:** Career (gigs, per-song challenges, stars, unlocks), Quickplay+,
  RockFest competitive modes, Practice, the tutorials (32 charts on the disc),
  GH Studio / GHMix (question 5).
- **Rockstar Creator** (`cas_pieces`), the premade and guest characters, Xbox
  avatars (`AvatarAssetPack`, question 4), the store / unlocks.
- **Venues, crowd, cameras, lighting,** the screen effects above.
- Online, leaderboards, achievements, GHTunes, song import from other discs:
  stub them, logged, labelled "not in the port" in the backlog.

## 2. How you work

As GD-GAME-05 section 2: the owner may be away, never block; choices in
`gd-game-12/agents/DECISIONS.md`, questions in `OPEN_QUESTIONS.md` (ask live
with the interactive question tool, with suggestions, when he is present);
`PROGRESS.md` after every milestone and at least every 2 hours; commit and push
at every gate (`git commit -s`); no PR; resume from `PROGRESS.md`; backlog
(`docs/tasks/gd-backlog.md`, section gd-game-12) at the start and the end of
every task.

**Time estimate, measured (agent time from git history):**
- gd-game-03 (gd02 copied): ~4 h to its final report;
- gd-game-04 (new Neversoft engine): M0..M9 in ~6.5 h, then a week of owner
  waves to 96 %;
- gd-game-05 (gd02 copied, much wider): wave 01 (boot → Quickplay song in a
  venue) **~4.3 h**; waves 02–05: 5.2, 2.9, 2.3 and 4.5 h (21 % → 61 %).

| Plan for GH5 | From gd-game-10 | From gd-game-04 |
|---|---|---|
| First playable (section 6, M0–M3) | 3.5–4.5 h | 4.5–6 h |
| M0..M9 | 10–13 h | 13–16 h |
| To ~60 % by the ledger | +2–3 waves | +3–4 waves |

Write yours at the top of `PROGRESS.md` and revise it at each gate.

## 3. Ground rules (non-negotiable, from the owner)

Read every file in `docs/guidelines/` first (`general_guidelines.md`,
`universal_launcher.md`, `universal_mechanisms.md`, `universal-menus.md`,
`universal-behaviors.md`, `engine-backend-guidelines.md`, `usual-bugs.md`),
then the root `README.md`, `docs/platform/adding-a-game.md`, `docs/legal.md`,
`docs/tasks/gd-backlog.md`, `docs/tasks/PARITY_GAPS.md`,
`gd-game-02/docs/reuse_and_lessons.md` (L1–L21), `gd-game-04/docs/` (the
Neversoft formats, QScript, highway, venues), `gd-game-05/docs/` and the top of
`gd-game-05/agents/PROGRESS.md` (waves 01–05), `gd-engine-core/docs/` and
`docs/spikes/colabs01.md` (the GH5 section).

- **Versions (new rule).** Never bump `launcher_version` or any game's
  version (`game-info.yaml`, the engine's `project()`, the CHANGELOG version)
  unless the owner asks. Leave gd-game-12 at `0.0.0` until he does
  (question 7). "Commit and push" is not a request to bump.
- **Workspace.** Only `gd-game-12/` plus, minimally: your backlog section;
  `build_module_12` in `gd-engine-launcher/scripts/build.sh`;
  `gd-engine-launcher/games.yaml` if needed. Rip into `gd-game-12/extracted/`,
  Ghidra into `gd-game-12/ghidra/` (both git-ignored); the disc stays in
  `original_isos/`.
- **Never modify other games** (`gd-game-01..05`, `-10`, `-14`, …): copy what
  you need into your folder. **`gd-engine-core/`: no changes**; use its audio
  input (the microphone, added by gd-game-05) as it is. Other core needs go to
  `OPEN_QUESTIONS.md`.
- **Band reference from Rock Band.** gd-game-05 solved drums (pads, kick,
  cymbals, the drum GAMEPADS rows), vocals (the mic through gd-engine-core,
  the pitch detector, `GD05_VOCALS_FILE` for headless tests) and 2–4 players.
  Copy the engine-independent parts into gd-game-12; every GH5 rule
  (windows, pitch tolerance, Star Power) still comes from GH5's XEX.
- **As GD-GAME-05 section 3, unchanged:** commits `git commit -s` with no
  Co-Authored-By / AI lines; never commit game data, rips, screenshots,
  decompiler output or Ghidra files, nor paste decompiled code or disc shaders
  (`repo_guard.py` 0 before every commit); no workflows; 100 % parity with
  [C] / [I] and **no emulator comparisons**; C++17 / SDL3, `local` plus the
  gd-engine-core backends from day one, 0 warnings, no `environ`, rendering
  out of the gameplay core (L17), `RandomFile` for user paths; `timeout` on
  every run, one process, kill by PID (`pgrep -fa gd12`), scratch
  `XDG_DATA_HOME`, the owner's controllers and mic ignored in tests;
  subagents only with the owner's OK and a cost estimate, claims re-verified.

## 4. Mistakes made on gd-game-01..05 — do NOT repeat them

Rules 1–14 are in GD-GAME-05 section 4; read them. In one line each: (1) test
the real user path, menus / launcher / Wine; (2) never test a stale binary;
(3) audit the data early and drive missing QB C-functions to 0 (gd04: 678);
(4) read the assembly when arguments matter; (5) know your clocks, the audio
offset moves highway **and** judging; (6) draw order is behaviour; (7) every
fix gets a headless check; (8) cameras, animation and IK exact; (9) song start
order, nothing counts before the highway; (10) menus: sounds, Enter is Start,
backward transitions, pixel layouts, held repeat, **no black frames**; (11)
gameplay feel: overstrum, Star Power DSP, practice sync, Quickplay's picks;
(12) The Glitches has everything; (13) never a package without a module;
(14) don't claim what you didn't run.

**Rock Band waves 01–05 added:**
15. **Display gamma and colour are behaviour.** RB set a gamma ramp composed
    with the XDK's TV table; the port first showed the frame linear, then too
    bright. Find GH5's gamma / colour-correction path in the XEX and measure
    menus against reference values, box by box.
16. **Menu lighting:** panels with no environment of their own use the UI
    manager's default (RB: ambient 1); hidden meshes stay hidden.
17. **A fuzz walk finds what scripted walks miss** (`rb_fuzz`: random keys
    through every screen, 2000 steps, checks that panels keep their buttons).
    Add `gh5_fuzz` from M3.
18. **Keyboard play with a controller plugged in:** one-player songs take
    every device as player 1; Start pauses from any of them.
19. **Drums and vocals need real-device checks by the owner** (list them in
    `PROGRESS.md`); headless, feed `GD12_VOCALS_FILE` and scripted pads.
20. **Load stutter:** load on a thread behind the loading screen, bounded
    uploads per frame (`usual-bugs.md`); measure with a hitch log.
21. **Tempo-masked animation bits** differ between generations (RB's drummer
    lost every clip): confirm each anim flag in the new XEX.

## 5. The pattern (gd-game-04 / gd-game-05 are the models)

Folder layout per `docs/platform/adding-a-game.md`. Program `gd12`, save
folder `SDL_GetPrefPath("gd-engine","gd12")`.

**Dev aids from day one (L6):** `--song <id> --autoplay`, `--difficulty`,
`--no-fail`, `--part guitar|bass|drums|vocals`, `--players N`; `GD12_KEYS`,
`GD12_SCREENSHOT(_AT)`, `GD12_SCREEN`, `GD12_SKIP_BOOT`, `GD12_SFXLOG`,
`GD12_UILOG` (every unhandled script function logged once), `GD12_STATS`,
`GD12_PERF`, `GD12_FRAMELOG`, `GD12_HITCHLOG`, `GD12_GRAPHICS_BACKEND` /
`GD12_AUDIO_BACKEND`, `GD12_VOCALS_FILE`.

**Headless tests (all take the disc path):** `--selftest` (every chart
auto-played on every part and difficulty: "all charts fully hit", plus band
Star Power and saves), `--scenetest`, `--scripttest`, `--uitest`, `--sfxtest`,
`--videotest`, `--chartest`, `--venuetest` (60 and 240 fps identical),
`--shadertest`, `--audittest`, `--walktest <walk>` (boot, quickplay per
instrument, party_play 4 players, lose, pause, career, rockfest, practice,
tutorials, creator, studio, glitches, enhanced, enhanced_backends, cheats,
mouse, gamepads, gamepads_drums, gamepads_vocals, stickyaxis, gh5_fuzz).

**Launcher module:** `gd_game_run`, `gd_game_explore`, `gd_game_ui_sounds`;
`GD12_MODULE`; `build_module_12`; `iso_signatures`: `format: xdvdfs`,
`xex_title_id: "41560840"`. Check `gdul.log` and `--list` recognise the disc.

**Port extras:** the Enhanced menu (backends first, Restart Now / Later),
GAMEPADS with four players and the drum and vocal rows, mouse / touch /
Enter-is-Start, the help bar, The Glitches, cheats, Steam Deck preset, windowed
default, Enhanced defaults off, HDR as the other games. Candidates from the
study, as Enhanced options (question 8): one switch per screen effect, Hor+
ultrawide, a quit prompt on the main menu.

**Docs (`gd-game-12/docs/`):** `FINDINGS.md`, `formats.md`, `audit.md`,
`parity.md`, `frontend.md`, and one file per subsystem, kept current.

## 6. Milestones

**First playable = the wave 01 target (M0–M3):** boot (logos skippable), GH5's
main menu with our universal pieces (Enhanced, GAMEPADS, help bar, mouse,
Enter-is-Start), **one Quickplay song on guitar in a venue**, the results
screen, back to the song list. Estimate 3.5–6 h (section 2).

**M0: set-up (≈45 min).** Copy the chosen engine, tools
(`xdvdfs.py`, `pak.py`, `qb.py`, `qbkey.py`, `fsb.py`, `img.py`, …) and Ghidra
scripts (`gd-game-04/tools/ghidra_scripts/`) into `gd-game-12/`, renamed.
`agents/CONTEXT.md`, `PROGRESS.md`, `DECISIONS.md`, `OPEN_QUESTIONS.md`. Fill
`README.md` from the data (songs, venues, characters), point
`agent-instructions.md` here. Rip the disc; import `default.xex` with
XEXLoaderWV, export `ghidra/analysis/`; **verify title ID and CRC32 against
section 1**; load the study's addresses as unverified bookmarks. Backlog:
replace GD12-001 with the milestone rows. `iso_signatures` in `game-info.yaml`
(version unchanged). Gate: 0 warnings, boots to a log line listing the
archives.

**M1: data layer (≈1.5–2 h).** The `CHNK` container, `compress.toc`, pak /
pab, QB (GH5's version), FSB (three banks per song plus preview; encryption
check), `.img.xen`, scenes, `MaterialLibrary.bin.xen`, Bink. Python tools
byte-exact (L21). Gate: every pak parses to its last entry, every QB loads,
every texture and FSB decodes, `--videotest` passes.

**M2: songs and gameplay core (≈2 h).** The song list from QB (not a fixed
table: mod support later), charts for every part and difficulty, the stem mix
and each part's miss mute. Rules from the XEX, rendering-free: windows,
HOPOs, sustains, open notes, slider notes, drums, the vocal detector, Star
Power per player and band, multipliers, rock meter, saves, stars. Gate:
`--selftest` all charts fully hit; `--sfxtest`.

**M3: QScript, UI, audit → first playable (≈2 h).** GH5's own scripts through
our QScript VM (L1), every C-function implemented or logged once; menus as the
original builds them; the asset audit (`docs/audit.md`). Gate:
`--scripttest`, `--uitest`, `--walktest quickplay` from boot to results;
`gh5_fuzz` passes; screenshots of the main menu and a song.

After the first playable:
- **M4 highways and HUD (≈2 h):** guitar, bass, drums, vocals; 1–4 players
  laid out as the original; Party Play drop-in; song start order. Gate: walk
  counters per effect; shots for 1, 2 and 4 players.
- **M5 venues (≈2 h):** the 14 zones, camera director, screen-FX passes
  with their flags, shadows, the gamma / colour path (rule 15). Gate:
  `--venuetest`, identical at 60 and 240 fps.
- **M6 band and Rockstar Creator (≈2 h):** `cas_pieces`, skinning, IK on neck
  and pads, per-player anim paks, lip sync. Gate: `--chartest`; no member
  vanishes from any shot.
- **M7 modes, unlocks, saves (≈2 h):** Career and challenges, Quickplay+,
  RockFest, Practice, tutorials, GH Studio as scoped (question 5), the store,
  the original's cheats, versioned JSON saves (L12), The Glitches. Gate: the
  section 5 walks.
- **M8 port extras (≈1.5 h):** per `docs/guidelines/`; every backend pair
  plays a song.
- **M9 launcher module and packages (≈1.5 h):** `build_module_12`,
  `games.yaml`, `ready_to_play` only when it plays from the launcher; Windows
  DLLs; Android reads the disc in place (mic permission as gd-game-05). Gate:
  section 7; a disc under a path with spaces, accents and brackets loads.
- **M10 close the gaps:** audit rows, then the ledger, highest player-visible
  impact first; final report.

## 7. Gates

As GD-GAME-05 section 7: clean builds with 0 warnings on every platform;
every test of this and earlier milestones passes on a fresh binary (output in
`PROGRESS.md`); no new unexplained audit gaps, missing C-functions not
growing; `repo_guard.py` 0; backlog updated; committed with `-s` and pushed;
no game processes left. At the end: `gd-engine-launcher/scripts/build.sh all
--force` with `verify_bundle` listing gd12 in linux, steamdeck, windows and
android-arm64; `--play gd12 --exit-after` on Linux and under Wine; the final
report (works / approximate / missing / confidence per OS 🟢 🟡 🔴);
`completeness` and `ready_to_play` honest (versions only if the owner asks).

## 8. Reaching 100 %

- **`docs/parity.md` from day one** with fixed weights, computed by a copy of
  `gd-game-05/tools/parity.py`. "Done" = no [I] in gameplay rules, no missing
  rows.
- **C-functions to 0.** The audit counts every script function the scripts
  call that we lack.
- **Shaders from the disc's own programs** (`MaterialLibrary`, the FX
  passes), in our own GLSL (L15).
- **`docs/frontend.md`:** every screen in the scripts' graph, each with a walk
  act and a match / differs row (gd05 wave 03's method).
- **The owner's first checks** before every gate: fret hands, hair and
  lighting, sustains, note-pop SFX, menus to the pixel, black flashes, gamma,
  store state, Star Power sound, practice sync, overstrum, strums before the
  highway, held repeat, Enter as Start, the keyboard with a guitar plugged in.
- **Calibration as GH1 (GDALL-038)**, mic latency included. **Mod support
  prepared, not built**; the DLC manifest documented only.
- **Prepare gd-game-14:** per-game data (song lists, characters, venues,
  struct offsets) in tables, not in code, so Band Hero is a delta.

## 9. Questions for the owner

1. **Start point:** wait for the World Tour engine (gd-game-10) and copy it,
   or start now from gd-game-04 (GH3) with gd-game-05 as the band reference?
2. **Branch:** which branch does wave 01 run on?
3. **Engine sharing with Band Hero:** build gd-game-12 with per-game tables
   so gd-game-14 copies it (rule of two), or plan a shared GH5-family engine
   folder now?
4. **Xbox avatars** (`AvatarAssetPack`): reproduce them as playable
   characters, or premade and created rockers only?
5. **GH Studio / GHMix** (58 `musicstudio` paks): in wave 01's scope, or a
   later wave?
6. **Hardware:** which GH5-era controllers do you have to test (guitar with
   the slider, five-pad kit with cymbals, microphone)?
7. **First version:** may M0 set gd-game-12 to 0.0.1, or stay at 0.0.0 until
   you ask?
8. **Enhanced options from the study:** add per-effect switches (bloom, DOF,
   grain, colour correction, vignette), Hor+ ultrawide and a main-menu quit
   prompt?
9. **Ambition for wave 01:** stop at the first playable (M0–M3), or run on to
   M9?

## Owner's answers
