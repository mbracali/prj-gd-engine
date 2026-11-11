# Kickoff: gd-game-17, Guitar Hero: Warriors of Rock (Xbox 360), wave 01

> **You are starting a new reimplementation.** Read this whole file before
> your first command. It is short on purpose: the full rule set, the mistake
> list and the gate checklist live in
> [GD-GAME-05_KICKOFF.md](GD-GAME-05_KICKOFF.md) sections 2, 3, 4, 7 and 8,
> and they apply here word for word (read them as "gd-game-17 / gd17 /
> GD17_"). This file adds what is different for Warriors of Rock. When it
> conflicts with a tool default, this file wins. When it conflicts with
> `docs/guidelines/`, the guidelines win.

## 1. The target

| | |
|---|---|
| Game | **Guitar Hero: Warriors of Rock** (Neversoft / Activision, September 2010), Xbox 360, USA / Europe. Internal name **GH6** (file names `z_gh6intro`, `selfesteemgh6`) |
| Folder / program | `gd-game-17/` → `gd17`, env prefix `GD17_`, Android `io.github.mbracali.gd17` |
| Disc | `original_isos/Guitar Hero - Warriors of Rock (USA, Europe) (En,Fr,De,Es,It).iso` (7.8 GB, XDVDFS / XGD2) |
| Executable | `default.xex` (10.0 MB, PowerPC Xenon, XEX2), **title ID `41560883`**, image base `0x82000000`, entry `0x822CB0A0` [C, read 11-10] |
| Family | The last of the **GH5 engine** (Neversoft, descended from GH3). Owner's order: Metallica → Van Halen / World Tour → GH5 → Band Hero → **Warriors of Rock** |
| Start from | **gd-game-12 (GH5)** if its engine exists when this run starts; otherwise **gd-game-04 (GH3)**. Today gd-game-12 is an empty skeleton (question 1) |
| Branch | the owner's, named when the run starts. Never create or switch branches yourself |
| Goal | a full decompilation-based reimplementation, 100 % faithful, playable from the Universal Launcher on Linux, Steam Deck, Windows and Android |

### What the disc holds (first look, 2026-11-10)

Read-only with `gd-game-05/tools/xdvdfs.py list` (scratch in `/tmp`, nothing
extracted into the repo):

| Item | Value |
|---|---|
| Game partition | `0xFD90000` (XGD2), 1,375 entries, 6.4 GB of files |
| Top level | `default.xex`, `data/`, `AvatarAwards`, `AvatarAssetPack`, `nxeart`, `$SystemUpdate/` |
| `.pak.xen` | 739: `data/compressed/PAK/` (551: menus, Quest, GH Studio, CAS), `SONGS/` (167), `ZONES/` (20), `JAMS/` (1) |
| `.pab.xen` | 6 (`data/pak/archive/`: `anim_loops`, `cas_globalarchive`, `cas_pieces`) |
| `.fsb.xen` | 543: `data/music/` (542) plus `data/streams/` (`streamall`, `sixchannelfiles` with their `.dat.xen`) |
| `.img.xen` | 19 loose (`UI_IMAGES/LOADING`); most textures are inside the paks |
| `.qb.xen` | 1 loose (`data/scripts/engine/engine_params.qb.xen`); the scripts are inside the paks |
| Other | `FXFILES/MaterialLibrary.bin.xen`, `JAMS/jam1.pak.xen`, `data/anims/standardkey{Q,T}.bin.xen`, `data/memcard/*.png` |
| Video | 31 Bink `.bik` (`data/moviestreams/`: intro, credits, career intro / ending, logos, 21 venue light shows incl. GH4 / GH5 / Band Hero ones) + `data/movies/logo_intro.bik.xen` |
| Songs | 135 audio sets (`<song>_1/_2/_3.fsb.xen` + `_preview`): **93 songs** (Rush's *2112* in 7 parts and a career-only *Sudden Death* included), 33 tutorials (`tut_guitar_*`, `tut_bass_*`, `tut_drums_*`, `tut_vox_*`), 9 zone transitions |
| Venues | `Z_Barbarian`, `Z_Cube`, `Z_Egypt`, `Z_EpicStage`, `Z_Festival`, `Z_FinalBattle`, `Z_GH6Intro`, `Z_HouseBand`, `Z_MansionStudio`, `Z_Modular`, `Z_PunkStage`, `Z_RiotHouse`, `Z_Visualizer`, `Z_credits` + `z_studio`, `z_viewer`, `z_in_game`, `Z_Screenshot`, `global` |
| GH Studio | 58 `musicstudio_*` paks (instrument samples, jam room, mixer) |

These counts are a first look: confirm every number in M0 from the rip and
write them in `gd-game-17/README.md`.

### Same family, one engine generation later (read before writing any code)

Warriors of Rock keeps the Neversoft stack gd-game-04 already reads: PAK / PAB
archives keyed by QB checksums, QB / QScript, FSB4 with XMA, `.img`, scenes,
Havok-era animation. **Assume nothing, confirm everything:** GH5's band,
drums, vocals and the 3-file song audio, plus WoR's own Quest mode, powers,
transformations, the new highway and GH Studio. Every rule, constant and
layout comes from WoR's `default.xex` and disc, never from GH3's or GH5's.
Write each confirmation in `gd-game-17/docs/FINDINGS.md` as **[C] address**;
a copied rule not re-confirmed in WoR's code is **[I]** and blocks "done".

**Copy, never modify.** Copy the starting engine (gd-game-12 or gd-game-04)
into `gd-game-17/engine/`, renamed (gd12/gd04 → gd17, `GD12_`/`GD04_` →
`GD17_`). For drums and vocals copy gd-game-05's parts (drum lanes and
kick, the vocal track, the pitch detector over gd-engine-core's microphone
input GDCORE-011) and adapt them to Neversoft's data. Never edit another
game's folder.

## 2. Knowledge from the community studies (clean-room)

`docs/spikes/colabs01.md`, section "GH: Warriors of Rock (gd-game-17)",
studies three community static recompilations. They run the original code;
we reimplement it, so **no code transfers, only knowledge**, re-found in our
own Ghidra project on our own disc. Saving: about 5-8 % of the port on top
of the starting engine.

**Clean-room rules (non-negotiable):**
- Read their docs, then re-find each fact in Ghidra and write it fresh, in
  our own words, with **our** address and [C] / [I].
- **No code, config, tables or text copied** from any of them. Their
  address dumps never become committed files; leads stay in the git-ignored
  `ghidra/`.
- **One of the three has no licence** (colabs01.md §1 and §5): nothing from
  it, ever, not even as a lead. Don't open it during this run.
- **No names** of the other projects or their maintainers in our code,
  comments, commit messages, `gd-game-17/` docs, CHANGELOG or README. The
  only place they appear is `docs/spikes/colabs01.md`.
- Never take their dependencies (recompiler SDKs, emulator runtimes, DLSS,
  their launchers).

**What we take as Ghidra leads** (each verified on our XEX before use):

| Lead | Use in gd-game-17 |
|---|---|
| The **~1,550 script-function (CFunc) name → address map** | Seed Ghidra labels; order the QScript natives our VM needs (gd-game-04 `script/` model). Name a function only after reading it |
| Engine addresses: the timer, the projection, the highway depth mask, the sustain tails, the D3D device global (colabs01.md §3) | Entry points for the song clock, camera, highway, tails. Their build may differ: find each by its code, not by its number |
| **Clock drift:** the engine assumes a 49.875 MHz timebase, 0.25 % (about 150 ms a minute) | Our song clock follows the audio clock; document the original's quirk in FINDINGS (no option in wave 01) |
| **Frame-counted logic** (vblank clocks, script frame waits, button polls, whammy, the loading star; scripts that time themselves) | A checklist for our fixed tick vs render rate split, so 120 / 240 fps play like 60 |
| **Widescreen layering:** scene Hor+, the 2D layer kept 16:9 centred, clip windows scaled with it, **the highway's depth-only near-plane mask quad** | Renderer design; avoids the black band beside the highway |
| **Screen FX chain:** bloom, depth of field, film grain, colour correction, vignette (grain tied to colour correction) | Our post pass in the original's order; per-effect Enhanced toggles later |
| **Quest mode powers** (each character's power and transformation) | Reimplement from WoR's QB and XEX, one rule per character, with addresses |
| DLC notes (read in place) | Later waves only; never in wave 01 |

Formats (PAK/PAB, QB, FSB4, `.img`, scenes) are **not** documented by any of
them: they come from gd-game-04 / gd-game-12 and our own reverse
engineering, in `gd-game-17/docs/formats.md`.

## 3. Warriors of Rock features to reproduce (verify the full list from the data)

- **Full band, 1-4 local players:** guitar, bass (open notes), drums (kick,
  double kick, accents, cymbals), vocals (pitch, talkies, freeform); every
  difficulty incl. Beginner and Expert+ if the data has it.
- **Band mechanics:** Star Power per instrument and the band's, the band
  multiplier, Band Moments, saving a failed bandmate, failing out, solos,
  extended sustains, sliders / tap notes, HOPOs. Each rule from the XEX.
- **Quest mode:** the story, the eight characters, their **powers**
  (multiplier, Star Power, life and save rules) and **transformations**,
  the stars-to-power progression, the final battle (*2112* and the
  finale). The single most WoR-specific work.
- **Quickplay+** (challenges per song), quickplay, multiplayer (Pro Face-off,
  versus modes, band versus band if present), practice, the tutorials.
- **Character creator (CAS) and unlocks**; **GH Studio** (GHTunes upload is
  online: stub).
- **Venues, crowd, cameras, lighting, light-show movies**, the zone
  transitions between Quest songs.
- **Online, leaderboards, achievements, GHTunes, the Music Store / DLC:**
  stubbed, logged, labelled "not in the port" in the backlog. Song list read
  from the data (mod support prepared, not built).

## 4. How you work

GD-GAME-05_KICKOFF.md section 2 (owner away, DECISIONS / OPEN_QUESTIONS /
PROGRESS, measured estimates, backlog at start and end) and section 3
(ground rules) apply. The differences:

- **Versions (new rule, `general_guidelines.md`):** never bump the launcher
  or any game's version unless the owner asks. gd-game-17 stays **0.0.0**
  in `game-info.yaml`, the engine's `project()` and the CHANGELOG (notes go
  under `## [Unreleased]`) until the owner says otherwise (question 3).
- **Outside `gd-game-17/`**, only, minimally: this game's section of
  `docs/tasks/gd-backlog.md`; `build_module_17` in
  `gd-engine-launcher/scripts/build.sh` and `games.yaml` **only at M4, if
  the owner says yes** (question 4). `gd-engine-core/`: nothing; needs go
  to `OPEN_QUESTIONS.md`.
- **Never modify** `gd-game-01..16`. Copy.
- **Commits** with `git commit -s`, no AI lines; commit and push at each
  gate on the owner's branch; no PR.
- **No emulator comparisons**; no Xenia, no captures.
- **Processes:** a `timeout` on every run, one game process at a time,
  `pgrep -fa gd17` before each gate, kill by PID only. Scratch
  `XDG_DATA_HOME`. The owner's guitar, drum kit and mic ignored in tests
  (`SDL_*_IGNORE_DEVICES`, dummy audio).
- **Ghidra:** the gd-game-04 / gd-game-05 set-up (Ghidra 12.1.4, JDK 21,
  XEXLoaderWV; `ExportAnalysisXex.java`, `FixXexFunctions.java`,
  `DumpAsm.java`, `ReadF.java`, `FindBytes.java`, `FindConst.java`), project in
  `gd-game-17/ghidra/`, exports in `ghidra/analysis/` (git-ignored).

## 5. Mistakes from gd-game-01..05: do NOT repeat them

GD-GAME-05_KICKOFF.md section 4 applies in full (real user path, no stale
binary, the asset audit, read the assembly, clocks, draw order, a
regression check per fix, exact cameras, song start order, menus, feel,
The Glitches, packages, don't claim what you didn't run). WoR adds:

1. **Clocks:** the 0.25 % drift (section 2) is the original's; our highway
   and judging follow the audio clock, and the audio offset moves **both**
   (`drawT = frameTime - audio_offset + video_offset`, GDALL-038).
2. **Frame-rate independence:** every frame-counted item in the lead list
   becomes a fixed-tick rule; `--venuetest` and a song's score must be
   identical at 60 and 240 fps.
3. **Widescreen:** the highway mask and the 16:9 2D layer from day one (no
   black band, no HUD stretched in ultrawide).
4. **Natives to 0:** gd04 shipped with 678 missing natives. Use the CFunc
   leads to drive WoR's count down from M2 on.
5. **Shaders from the disc's own programs** (`MaterialLibrary`, the paks'
   shaders), our own GLSL; gd04's approximated materials were its biggest
   visual gap.

## 6. The pattern (gd-game-04 / gd-game-05 are the model)

Folder layout per `docs/platform/adding-a-game.md`. Program `gd17`, save
folder `SDL_GetPrefPath("gd-engine","gd17")`. Dev aids from day one:
`--song <id> --autoplay`, `--difficulty`, `--no-fail`, `--part
guitar|bass|drums|vocals`, `--players N`; `GD17_KEYS`, `GD17_SCREENSHOT` /
`_AT`, `GD17_SCREEN`, `GD17_SKIP_BOOT`, `GD17_SFXLOG`, `GD17_UILOG` (every
unhandled QScript native or unknown class logged once), `GD17_STATS`,
`GD17_PERF`, `GD17_FRAMELOG`, `GD17_GRAPHICS_BACKEND` /
`GD17_AUDIO_BACKEND`, `GD17_VOCALS_FILE=<wav>`.

Headless tests (all take the disc path): `--selftest`, `--scenetest`,
`--scripttest`, `--uitest`, `--sfxtest`, `--videotest`, `--venuetest`,
`--shadertest`, `--audittest`, `--walktest <walk>`
(`SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy`).

Universal parts from the guidelines: the Enhanced menu (universal layout,
Graphics and Audio backend first, Restart Now / Later, backend label), the
GAMEPADS screen (4 players, drums and vocals), mouse, touch, Enter-is-Start,
the help bar, The Glitches, Accessibility (Easy Chords), HDR as the other
games (and the shared GDCORE-HDR path once it lands), the Steam Deck preset.

## 7. Wave 01: the first milestone (like Rock Band wave 01)

**Target:** boot → our universal menus → **one Quickplay guitar song in a
venue**, playable from the menus, scored by WoR's rules. Rock Band's wave 01
(the same scope) took **about 4.3 h of agent time**, measured; plan
**4.5-5.5 h** here (a new Neversoft generation, but gd-game-04 / -12
already read the containers).

**M0: set-up (≈45 min).**
- Copy the starting engine, tools (`pak.py`, `qb.py`, `qbkey.py`,
  `fsb.py`, `img.py`, `scn.py`, `xdvdfs.py`) and build scaffolding, renamed.
- `agents/CONTEXT.md`, `PROGRESS.md` (estimate at the top),
  `DECISIONS.md`, `OPEN_QUESTIONS.md`.
- Rip the disc into `gd-game-17/extracted/`; Ghidra import, analysis,
  exports; label the CFunc leads that verify.
- README facts from the data (launch date, songs, venues, characters).
- `game-info.yaml` `iso_signatures` (`format: xdvdfs`, `xex_title_id:
  "41560883"`); version unchanged (section 4).
- Backlog: replace GD17-001 "Discovery" with the wave 01 rows.
- Gate: builds with 0 warnings; boots to a log line listing the archives.

**M1: data layer (≈1 h).**
- PAK / PAB, QB, FSB4 / XMA (the 3 song FSBs: which stems in which), `.img`,
  scenes, for WoR's versions. Tools decode byte-exact (L21).
- Gate: `--scenetest` reads every pak and scene to its end, no unknown
  classes; every `.img` decodes; `--sfxtest` decodes every FSB.

**M2: one song's rules and the QScript boot (≈1.5 h).**
- The song list and one guitar chart (notes, HOPOs, sustains, extended
  sustains, sliders, Star Power, solos), hit windows, scoring, the
  multiplier, Star Power, failing, rendering-free (L17), each with its address.
- The QScript VM runs WoR's boot and front-end scripts against our natives;
  the missing-native count goes into `docs/audit.md`.
- Gate: `--selftest` "all charts fully hit" for guitar on every song and
  difficulty; `--scripttest` loads every QB.

**M3: menus, highway, venue (≈1.5 h).**
- Boot (Bink logos skippable), the front end into Quickplay, our universal
  menus (Enhanced, GAMEPADS) one frame after selection.
- The guitar highway and HUD with the 16:9 2D layer and the depth mask; one
  venue with its cameras on the fixed step; song start order (venue, intro
  shot, highway intro, music; nothing counts before the highway is live).
- Gate: `--walktest quickplay` from boot to the results screen;
  `--walktest enhanced` and `gamepads`; walk counters for gems, sustains, Star
  Power; `check:no_black` between screens.

**M4: launcher and packages (≈45 min, only with the owner's yes).**
- `gd_game_run`, `GD17_MODULE`, `build_module_17`; the launcher recognises
  the disc; Linux and Wine run 25 s and exit 0; `verify_bundle.sh` lists the
  module in every platform's bundle. `ready_to_play` stays false.

**Wave 01 report** (top of `PROGRESS.md`): what works, what's approximate,
what's missing, verified vs built-only, 🟢 / 🟡 / 🔴 per OS.

## 8. Later waves (outline, each planned with the owner)

| Wave | Scope |
|---|---|
| 02 | Bass, drums, vocals; 2-4 players; band mechanics; every venue and camera |
| 03 | **Quest mode**: story, characters, powers, transformations, transitions, the finale |
| 04 | Quickplay+ challenges, versus modes, practice, tutorials, CAS, unlocks, saves (versioned JSON) |
| 05 | GH Studio, the asset audit and parity ledger to 100 %, shaders from the disc |

A parity ledger `gd-game-17/docs/parity.md` (as gd-game-05) starts in wave
01 and is the measure of completeness.

## 9. Gates

GD-GAME-05_KICKOFF.md section 7, unchanged, except: **no version bump** at
any gate, and the package steps run only once M4 is approved. 0 warnings on
Linux, Windows (MinGW) and Android; every earlier test on a fresh binary;
`repo_guard.py` 0 problems; backlog updated; committed `-s` and pushed; no
`gd17` process left.

## 10. Questions for the owner

1. **Starting engine:** gd-game-12 (GH5) is still empty. Wait for it, or
   start now from gd-game-04 (GH3) and re-base later?
2. **Branch:** which branch does this run use (you create it)?
3. **Version:** keep gd-game-17 at 0.0.0 through wave 01, or may it go to
   0.0.1 when it first boots?
4. **Launcher:** wire `build_module_17` and `games.yaml` in wave 01 (M4,
   hidden as not ready), or keep the game out of the launcher until a later wave?
5. **Wave 01 song and venue:** any preference (a short Quickplay song in
   a simple zone), or pick the first song of the Quickplay list?
6. **Quest powers:** reproduce them exactly as the original in wave 03, or
   also offer them in Quickplay as an Enhanced option later?
7. **Clock drift:** match the audio clock only (our choice), or also keep a
   "console-accurate" 0.25 % option for purists?
8. **DLC and GH5 / Band Hero export songs:** out of scope until which wave?

## Owner's answers

