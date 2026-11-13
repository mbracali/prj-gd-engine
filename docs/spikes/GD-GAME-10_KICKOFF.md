# Kickoff: gd-game-10, Guitar Hero: Metallica (Xbox 360), wave 01

> **You are starting a new reimplementation.** Read this whole file before
> your first command. It holds the owner's rules, the plan, the gates and
> every mistake made on gd-game-01..05, so you can do this almost in one
> shot. When it conflicts with a tool default, this file wins. When it
> conflicts with `docs/guidelines/`, the guidelines win.

## 1. The target

| | |
|---|---|
| Game | **Guitar Hero: Metallica** (Neversoft / Activision, March 2009), Xbox 360, USA / Europe |
| Folder / program | `gd-game-10/` → `gd10`, env prefix `GD10_`, Android `io.github.mbracali.gd10` |
| Disc | `original_isos/Guitar Hero - Metallica (USA, Europe) (En,Fr,De,Es,It).iso` (7.8 GB, XDVDFS / XGD2) |
| Executable | `default.xex` (10.4 MB, PowerPC Xenon, XEX2), **title ID `41560830`** [C, read 11-10] |
| Branch | the owner's branch for this run, named in the run prompt. Never create or switch branches |
| Goal | a full decompilation-based reimplementation, 100% faithful, playable from the Universal Launcher on Linux, Steam Deck, Windows and Android |
| Role | **proof of concept of the "World Tour engine" family** (Neversoft "gh4", the step after GH3): Metallica first, then Van Halen (gd-game-15, a delta on this folder) and later World Tour (gd-game-07) |

### What the disc holds (first look, 2026-11-10)

Read-only, with the project's own tools (`gd-game-05/tools/xdvdfs.py list`,
`gd-game-04/tools/pak.py list` on two files copied to a scratch folder
outside the repo). Nothing extracted into the repo.

| Item | Found |
|---|---|
| Title ID / media ID | `41560830` / `2E0C8E5F` (XEX execution info) |
| Game partition | `0xFD90000` (XGD2), 1,493 entries, 7.2 GB of files |
| `.pak.xen` / `.pab.xen` | 1,060 / 3 (910 in `DATA/COMPRESSED/PAK`, 101 in `DATA/COMPRESSED/SONGS`, 27 in `DATA/PAK/CAS`, 19 zones) |
| `.fsb.xen` | 342 (341 in `DATA/MUSIC`: per song `_1`, `_2`, `_3` and `_preview`; plus `DATA/STREAMS/streamall.fsb.xen`, 160 MB) |
| `.bik.xen` | 37 Bink movies (logos, intro / outro, story movies, venue movies, loading and instructions) |
| `.img.xen` | 13 (loose; most textures are inside paks) |
| Other | `compress.toc.xen` + `.lst`, `pak_sizes.txt.xen`, `MaterialLibrary.bin.xen` (two copies), `engine_params.qb.xen`, `standardkey{Q,T}.bin.xen` |
| Songs | 59 `_preview` streams; 101 song paks including tutorials and debug / sync-test charts. The exact set list comes from `songlist.qb` at M2 |
| Venues (zones) | donnington, forum, icecave, justice, meadowlands, mop, soundcheck, soundcheck2, stone, studio, studio2, tushino (+ frontend, in_game, viewers) |
| Characters | `DATA/PAK/CAS/` (create-a-rocker: bodies, legs, shoes, accessories, bass / drum kits and finishes) |
| GH3 tools on gh4 data | `pak.py list` reads `qb.pak.xen` **as is** (955 entries, every name resolved: same QB key hash). `pak.py extract` / `qb.py dump` do **not**: 693 of 955 `.qb` come out empty or short, so the pak + pab data layout changed. First M1 task |

### This is GH3's engine, one generation later (read before writing any code)

Metallica runs on Neversoft's gh4 engine, the successor of GH3's (gd-game-04):
`.pak.xen` / `.pab.xen` archives keyed by QB checksums, compiled QScript,
FSB audio, `.img.xen` textures, the same disc layout (`DATA/COMPRESSED`,
`compress.toc`). So:

**Start from a copy of gd-game-04's engine in `gd-game-10/engine/`**, renamed
gd04→gd10 and GD04_→GD10_, as gd-game-05 started from gd-game-02:
- `data/` (XDVDFS, `RandomFile` with the Windows `_wfopen` path, pak, QB, FSB, images);
- `script/` (the QScript interpreter, L1), `scene/`, `render/` with the gd-engine-core adapter, runtime SPIR-V;
- `audio/`, `ui/`, `input/`, `devaids/`, `tests/`, `platform/`, `video/`;
- the tools (`pak.py`, `qb.py`, `qbscript.py`, `qbkey.py`, `img.py`, `fsb.py`, `scn.py`, `anim.py`, `havok.py`, `matlib.py`, `vmx128.py`, the Ghidra scripts) and the build scripts for every platform.

**Never modify gd-game-04** (or any other game): copy, then change the copy.

**Assume nothing, confirm everything.** One generation later means:
- the pak + pab data layout (above), QB item types and new script functions;
- a **full band**: guitar, bass, drums, vocals, 1–4 players (GH3 had guitar and bass only);
- a new song format (three FSB banks per song plus a preview; drum and vocal charts; vocal phrases and lyrics);
- create-a-rocker characters (`CAS`), new venues, new UI screens, a new save format.

Every rule, constant and layout comes from Metallica's `default.xex` and its
disc, never from GH3's. Write each confirmation in `gd-game-10/docs/FINDINGS.md`
as **[C] address**. A copied gd04 rule not re-confirmed in Metallica's code is
**[I]** and blocks "done" (L2).

**Formats to confirm or decode** (each in `docs/formats.md`, decoded
byte-exact by a Python tool in `tools/`, L21):
- **Archives:** pak + pab (the new layout), `compress.toc`, zones.
- **QB:** every `.qb` decodes to its end; the script function and C-function tables (`DumpCFuncs.java`).
- **Songs:** `songlist.qb` (metadata), per-song charts for every part and difficulty, Star Power, drum fills / accents, vocal phrases, lyrics, talkies, fretbars, camera and lighting events.
- **Audio:** the three FSB banks per song (which stems sit in which bank; encryption or obfuscation from the XEX), `streamall`, sound effects, XMA through the project's FFmpeg.
- **Video:** Bink (`.bik.xen`): copy gd-game-05's FFmpeg Bink decoders into your own `build_common.sh` copy.
- **Textures, scenes, characters:** `.img.xen`, scenes, skinning, the CAS parts, `MaterialLibrary.bin.xen` (shaders are original code: research only, our own GLSL, L15).
- **Saves:** profile, band, career, as versioned JSON (L12).

You may read publicly documented format notes from the modding community to
speed up, but **don't copy any third-party code**.

### Knowledge from the community recompilation (clean room)

`docs/spikes/colabs01.md` section A studied a community static recompilation
of this game. What it gives us is **knowledge, never code**:
- **Addresses as Ghidra leads only.** The list in colabs01 (timer, frustum,
  sprite / tail / highway-board draws, controller port, vblank) seeds names in
  our Ghidra project; each one is confirmed on **our** disc's XEX before use.
- **The original's bugs**, to find again in our XEX and reproduce faithfully,
  each with an Enhanced toggle to fix it:
  - the clock: a 49.875 MHz timebase correction drifts 0.25 % (~150 ms a minute);
  - crowd claps wait a fixed 0.18 s and ignore calibration;
  - timers that count frames (boot star, calibration beeps, fret spring-back): do them time-based on the original's fixed step.
- **Ultrawide** (1280×720 canvas, Hor+ through the frustum, 2D kept 16:9), the
  **screen effects** list (bloom, depth of field, grain, colour correction,
  vignette, ink blotches) with a switch each, **whammy / tilt** axis rest and
  remap, **DLC** read in place (STFS packages; out of wave 01).

**Clean-room rule:** no code copied; **no names of other projects or their
maintainers** in our code, in `gd-game-10/docs/` or in commits (refer to it
neutrally only in `docs/spikes/colabs01.md`). Every fact is re-found on our
disc and written in our own words.

### Full band: reuse gd-game-05 as reference

Rock Band (gd-game-05) already built a full band: the microphone through
gd-engine-core's audio input (`gd::core::audio::Input`), the pitch detector
and `GD05_VOCALS_FILE`, drum judging patterns, four players, GAMEPADS with
drums and vocals. **Read it as a reference and copy what fits into
`gd-game-10/`; never modify gd-game-05.** Harmonix and Neversoft rules
differ: every rule comes from Metallica's XEX.

### Ghidra on this machine

As in gd-game-04 / 05 (Ghidra 12.1.4, JDK 21, **XEXLoaderWV**). Import
`default.xex` into `gd-game-10/ghidra/` (git-ignored); copy gd-game-04's
`tools/ghidra_scripts/` (`ExportAnalysisXex`, `FixXexFunctions`, `DumpAsm`,
`DumpCFuncs`, `ReadF`, `FindBytes`, `FindConst`); export into
`ghidra/analysis/`. `analyzeHeadless` needs an absolute project path.
Build a **function matcher** against gd-game-04's GH3 project early: names
confirmed in GH3 transfer as leads.

## 2. How you work

- **The owner may be away.** Don't stop to ask and don't wait.
  - When present, ask with the interactive question tool, with suggestions.
  - An owner's choice you must make: pick the original's behaviour; if unknown, the gd-game-04 pattern. Write it in `agents/DECISIONS.md` (what, why, how to undo). Questions go in `agents/OPEN_QUESTIONS.md`.
- **Progress log** in `agents/PROGRESS.md` after every milestone and at least every 2 h: time, milestone, test commands with pass / fail lines, what's next, time left.
- **Time estimate, measured** (agent time from git history):
  - gd-game-04 (new engine) reached `ready_to_play` (M0..M9) in about 6.5 h;
  - gd-game-05 wave 01 (copied engine, full band) reached a menus-to-song Quickplay in **about 4.3 h**, waves 02–05 took 5.2 / 2.9 / 2.3 / 4.5 h.

  Metallica copies a working Neversoft engine but adds the band, a new pak
  layout and CAS characters. Plan **4–6 h for the proof of concept (M0..M4
  on guitar)** and **11–14 h for M0..M9**, then owner waves. Write the
  estimate at the top of `PROGRESS.md` and revise it at each gate.
- **Commit and push at every gate** on the owner's branch. **No PR**.
- **Resume:** if `PROGRESS.md` exists, read it, `DECISIONS.md` and the backlog, continue from the last gate.
- **Backlog at the start and the end of every task** (`docs/tasks/gd-backlog.md`, section gd-game-10).

## 3. Ground rules (non-negotiable, from the owner)

Read every file in `docs/guidelines/` first, then the root `README.md`,
`docs/platform/adding-a-game.md`, `docs/platform/release-process.md`,
`docs/legal.md`, `docs/tasks/gd-backlog.md`, `docs/tasks/PARITY_GAPS.md`,
`gd-game-02/docs/reuse_and_lessons.md` (L1–L21), `gd-game-04/docs/` (the
engine you start from), `gd-game-05/agents/PROGRESS.md` (what waves 01–05
taught) and `gd-engine-core/docs/`.

- **Branch and workspace.** Work on the owner's branch in the main checkout; never switch or create branches. Commit only your files; never revert others' changes. The disc stays in the git-ignored `original_isos/`. Rip into `gd-game-10/extracted/`, Ghidra into `gd-game-10/ghidra/` (both git-ignored). Outside `gd-game-10/`, only touch, minimally: your section of the backlog, `build_module_10` in `gd-engine-launcher/scripts/build.sh`, `gd-engine-launcher/games.yaml` if needed.
- **Never modify `gd-game-01..05/` or `gd-engine-core/`.** Core needs (per-effect switches, DLC reader, guitar axis mapping for every game) go to `OPEN_QUESTIONS.md` for the owner.
- **Versions: never bump.** Don't change `launcher_version`, and don't bump `guitar_dream_version`, the engine's version or the CHANGELOG version unless the owner asks (`general_guidelines.md`). The skeleton stays `0.0.0` until the owner says otherwise (question 2).
- **Commits as the owner:** `git commit -s`; **no** Co-Authored-By, Claude, AI or "Generated with" lines.
- **Never commit** game data, rips, decoded assets, screenshots of the game, decompiler output or Ghidra files; never paste decompiled code or the disc's shader code. `python3 .github/scripts/repo_guard.py` reports 0 problems before every commit.
- **GitHub Actions:** never start workflows. Build locally.
- **Matching the original:** 100% parity; every behaviour from the XEX and data; **[C]** / **[I]**; label approximations. **No emulator comparisons.**
- **Code:** C++17, SDL3, gd04's render device with `local` and the gd-engine-core backends from day one, gd04's style, SPDX headers, **0 warnings** (Linux, MinGW, Android), no `environ`, small files, gameplay core free of rendering (L17), user paths only through `RandomFile` / `SDL_IOFromFile`.
- **Processes:** every game run under `timeout`, one at a time; check `pgrep -fa gd10` and kill by PID only. Ignore plugged guitars, drum kits and mics in self-tests (`SDL_*_IGNORE_DEVICES`, dummy audio). Scratch `XDG_DATA_HOME`. Real-device checks go in `PROGRESS.md` "for the owner to play".
- **Subagents:** only for independent research or verification, at most about 4, with the owner's OK for long runs; verify their claims yourself.

## 4. The mistakes made on gd-game-01..05 — do NOT repeat them

1. **Test the real user path:** menus (`--walktest`), the launcher (`--play gd10`) and Wine. `--song` is a dev shortcut.
2. **Never test a stale binary.** Write the exact test invocations in `agents/CONTEXT.md`; a test that prints its usage text has not run.
3. **Audit the data early (M3)** for everything referenced and not resolved; drive the **missing script / C-function count to 0** (gd04 still has 678).
4. **The decompiler drops arguments:** read the assembly (`DumpAsm.java`).
5. **Know your clocks:** track, audio, sfx log, screenshot, vocal pitch, calibration. The audio offset moves the highway **and** the judging (GDALL-038). The original's 0.25 % drift is documented, reproduced and toggleable (section 1).
6. **Draw order and blending are behaviour:** the original's traversal; venue smoke never over the band (GD04-068).
7. **Every fix gets a regression check**, as headless as possible, with a counter the walk can assert.
8. **Camera and animation transforms exact:** every shot of every venue headless (no NaN, not inside geometry); fret hands on the neck by the original's IK (GD04-066); **bit masks from this XEX** (gd05's drummer lost every clip through GH2's tempo mask, GD05-041); fixed step at any frame rate.
9. **Song start order:** venue, intro shot, highways' intro with sounds, music. **Nothing counts before the highway is live** (GD04-112).
10. **Menus:** sounds never silenced; Enter is Start; Back plays the backward transition; no darkening back and forth; locked items locked; mouse and help-bar pills everywhere; held-key repeat (GD04-114); layouts to the pixel (`check:pos` / `check:centrex`); **no black frames** (`probe:black`). From gd05 waves 03–05:
    - **colours and gamma:** find the original's display gamma ramp and UI lighting (gd05's menus were washed out until RB's gamma 0.85 ramp and UI environ were found, GD05-030 / -049); prelit fonts keep their colour;
    - **panels re-enter fresh:** a panel left by Back must not keep its exit pose (GD05-052, found by a seeded `fuzz` walk: add one from M3);
    - **back only where the screen's script says** (GD05-024).
11. **Gameplay feel:** overstrum kills a held sustain (GD04-108); Star Power audio as the original's DSP (GD04-098); practice in sync at every speed (GD04-100); Quickplay picks venues and characters by the original's rules (GD04-071).
12. **Keyboard play:** with a guitar plugged in, a one-player song still takes the keyboard as player 1 and its Start pauses (GD05-050).
13. **No stutter:** load on a loader thread, upload a bounded amount per frame, start on the loading screen (usual-bugs.md, GD05-034). Measure with `GD10_HITCHLOG`.
14. **The Glitches** has everything unlocked and bought; a new band starts with nothing.
15. **Packages:** `verify_bundle.sh` lists your module in **every** platform; ISOs load on Android and Windows (wide paths, accents).
16. **Don't claim what you didn't run:** "verified" (command plus output) vs "built only" vs "not tested".

## 5. The pattern to follow

Folder layout as `docs/platform/adding-a-game.md` (already in the skeleton).
Program `gd10`, save folder `SDL_GetPrefPath("gd-engine","gd10")`.

**Dev aids from day one (L6),** gd04's set renamed, plus gd05's band ones:
`--song <id> --autoplay`, `--difficulty`, `--no-fail`, `--part guitar|bass|drums|vocals`,
`--players N`; `GD10_KEYS`, `GD10_SCREENSHOT` / `_AT`, `GD10_SCREEN`,
`GD10_SKIP_BOOT`, `GD10_SFXLOG`, `GD10_UILOG` (every unhandled script call
logged once), `GD10_STATS`, `GD10_PERF`, `GD10_FRAMELOG`, `GD10_HITCHLOG`,
`GD10_GRAPHICS_BACKEND` / `GD10_AUDIO_BACKEND`, `GD10_VOCALS_FILE=<wav>`.

**Headless tests (all take the disc path):** `--selftest` (every chart
auto-played on every part and difficulty: "all charts fully hit"), `--paktest`
(every pak and QB to its end), `--scripttest`, `--uitest`, `--sfxtest`,
`--videotest` (every Bink to its last frame), `--chartest` (CAS parts and
combinations), `--venuetest` (every shot, same at 60 and 240 fps),
`--shadertest`, `--audittest`, `--walktest <walk>`: boot, quickplay (each
part), band_quickplay, lose, pause, attract, back, fuzz, keys, career,
practice, tutorials, faceoff / battle (if on the disc), store, creator,
glitches, enhanced, enhanced_backends, cheats, mouse, gamepads,
gamepads_drums, gamepads_vocals, stickyaxis.

**Launcher module:** `gd_game_run`, `gd_game_explore` (characters and
instruments), `gd_game_ui_sounds`; `GD10_MODULE`; `build_module_10`;
`iso_signatures` with `format: xdvdfs`, `xex_title_id: "41560830"`.

**Port extras (`docs/guidelines/`):** the Enhanced menu (backends first, the
Restart pop-up, the per-effect switches and the original-bug toggles),
GAMEPADS with four players, drums and vocals, mouse / touch / Enter-is-Start,
the help bar, cheats, The Glitches, Steam Deck preset, windowed default,
HDR, accessibility, ultrawide.

**Docs (`gd-game-10/docs/`):** `FINDINGS.md`, `formats.md`, `audit.md`,
`parity.md` (from day one, section 8), `gh4-vs-gh3.md` (what changed from
gd04: the base for Van Halen and World Tour), plus one file per subsystem
(renderer, highways, vocals, drums, UI, venues, audio and charts, band and
characters, modes).

## 6. Milestones (each ends at a gate, section 7)

**M0: set-up (≈45 min).** Copy gd-game-04's engine, tools and build
scaffolding, renamed. `agents/CONTEXT.md`, `PROGRESS.md`, `DECISIONS.md`,
`OPEN_QUESTIONS.md`. Rip the disc, import the XEX, export the analysis, seed
the colabs01 leads and the GH3 matcher names (as leads). `game-info.yaml`
`iso_signatures` (version untouched, rule above). Backlog: replace GD10-001
with the milestone rows. Gate: 0 warnings; boots to a log line listing the
paks.

**M1: data layer (≈1.5 h).** The new pak + pab layout, QB to its end, FSB
banks and stems, images, scenes, Bink. Python tools byte-exact. Gate:
`--paktest`, every QB decoded, `--videotest`, `--sfxtest` on the menu banks.

**M2: songs and the guitar core (≈1.5 h).** `songlist.qb`, charts for every
part, the song's three banks mixed, the guitar and bass rules from the XEX
(hit window, HOPO, sustains, whammy, Star Power, rock meter, multiplier).
Gate: `--selftest` guitar and bass "all charts fully hit".

**M3: QScript, menus, audit (≈1.5 h).** Metallica's own scripts through the
copied interpreter (L1), every new C-function, the boot to the main menu
with the universal menus; `--audittest`. Gate: `--scripttest`, `--uitest`,
`--walktest boot`.

**M4: proof of concept (≈1 h).** Quickplay from the main menu, one song on
**guitar** in a venue, the highway and HUD exact for one player, song start
order, results, back. Gate: `--walktest quickplay` from boot to results;
screenshots at song times. **Stop here, write the PoC report and wait for
the owner's go (question 1)** before M5.

**M5: full band (≈2.5 h).** Drums (pads, kick, accents, fills), vocals (the
core's mic input, pitch detector, phrases, talkies), four players, band
Star Power, band meter and failing, every highway. Gate: `--selftest` every
part, `band_quickplay`, `gamepads_drums`, `gamepads_vocals`.

**M6: venues, crowd, cameras, lighting, the band (≈2 h).** Every zone, the
camera director, lights, effects (each with its switch), crowd and claps,
characters from CAS with the instruments' IK and lip sync. Gate:
`--venuetest`, `--chartest`.

**M7: modes, store, saves (≈1.5 h).** Career, Quickplay, practice,
tutorials, multiplayer modes on the disc, the store and unlocks, original
cheats, saves as JSON, The Glitches. Gate: the walks of section 5.

**M8: port extras (≈1 h).** Section 5 extras exactly per the guidelines.
Gate: every backend pair plays a song.

**M9: launcher module and packages (≈1 h).** Gate: section 7's end checks,
the launcher recognises the disc, Linux and Wine run 25 s and exit 0,
Restart Now switches backends, a disc under a path with spaces, accents and
brackets loads.

**M10: close the gaps.** Audit rows, then the parity ledger, highest
player-visible impact first. Write `gh4-vs-gh3.md` complete for gd-game-15.

## 7. Gates: what "done" means

1. `cmake --build` and `./local_build.sh` clean (0 warnings) on every platform;
2. every test of the milestone and every earlier one passes **on a fresh binary**, output in `PROGRESS.md`;
3. the audit has no new unexplained gaps and the missing-call count did not grow;
4. `repo_guard.py` 0 problems; the backlog updated;
5. committed with `-s` and pushed; no game processes left.

At the end: `gd-engine-launcher/scripts/build.sh all --force` with
`verify_bundle` listing gd10 on linux, steamdeck, windows and android-arm64;
play from the Linux launcher and under Wine; the final report at the top of
`PROGRESS.md` (works / approximate / missing / confidence per OS 🟢 🟡 🔴);
`completeness` and `ready_to_play` honest (values the owner confirms).

## 8. Reaching 100%

- **Parity ledger `docs/parity.md` from day one,** weighted, computed by a copy of gd05's `parity.py`. Rows: boot and menus, highways, each instrument's rules, vocals, band, venues, crowd, lighting, cameras, characters and CAS, each mode, saves, audio mix, video. "Done" = no [I] in gameplay rules, no missing rows.
- **Script and C-functions to 0** (the audit).
- **Shaders from the disc's materials** (`MaterialLibrary.bin.xen`), in our own GLSL.
- **Walk every screen** listed in the scripts, not only the main path; a seeded fuzz walk at every gate.
- **The owner's first checks,** every gate: fret hands, hair and lighting, menu colours and gamma, sustains, note-pop SFX, menus to the pixel, black flashes, Star Power sound, practice sync, overstrum, strums before the highway, held-key repeat, Enter as Start, keyboard with a guitar plugged in, no stutter.
- **Calibration as GH1 (GDALL-038),** with the mic latency; claps follow it only with the toggle.
- **Mod support prepared, not built;** DLC read in place later (question 5).
- **Make it reusable:** everything gh4-generic stays apart from Metallica's content, so gd-game-15 and gd-game-07 copy it with few changes.

## 9. Questions for the owner

1. **Proof-of-concept stop:** stop after M4 (one Quickplay song on guitar in a venue, ≈4–6 h) and wait for your go, or continue straight into M5..M9 in the same run?
2. **Version:** may M0 set gd10 to `0.0.1` (game-info, engine, CHANGELOG), or stay at `0.0.0` until you ask for a bump?
3. **Branch:** which branch does wave 01 run on (from `develop`)?
4. **Original bugs** (clock drift, claps ignoring calibration, frame-counted timers): toggles default to the original (faithful), or to the fix?
5. **DLC** (the game's downloadable songs read in place from your own packages): in a later wave, or never?
6. **gd-engine-core:** may the per-effect switches, the guitar axis remap and a DLC reader go into the core for every game, or stay in gd-game-10 for now?
7. **Hardware:** which controllers will you test with (GH guitar, World Tour drums, a USB mic)? Until then drums and vocals are "tested headless only".
8. **Shared Neversoft library** (pak, QB, FSB, images) for gd04 / gd10 / gd15 / gd07: now, or after Metallica reaches its gates?

## Owner's answers

Defaults chosen for the proof of concept, **owner to confirm** (run of 2026-11-10):

1. Proof of concept: boot first (M0-M3: disc, formats incl. the new pak / pab layout, scripts, intro → title → main menu with the universal menus), then M4 (one Quickplay song on guitar in a venue) if time allows. Stop there and report; no M5..M9.
2. Version: stays `0.0.0` (game-info, engine, CHANGELOG); no bump without the owner.
3. Branch: `feature/spike-metal`.
4. Original bugs: faithful by default; fixes later, as Enhanced toggles (off).
5. DLC: a later wave.
6. Everything stays in `gd-game-10/`; the core's existing backends and audio are used as gd04 does.
7. Hardware: tested headless; the owner checks devices later.
8. Shared Neversoft library: after Metallica reaches its gates.

