# gd02 backlog

Everything planned or found so far for the Guitar Hero II (Xbox 360)
reimplementation, done or not. The original's rules are in `FINDINGS.md`,
the formats in `dtb_format.md`, `audio_and_charts.md`,
`milo_and_textures.md`, the songs in `songs.md`, and what comes from
gd-game-01 (and its lessons, L1–L21) in `reuse_and_lessons.md`.
Milestones M0–M11 follow `reuse_and_lessons.md` §4.2.

Status: ✅ done · 🟡 partly done / approximated · ❌ not started

## Done

| Area | Item | Notes |
|---|---|---|
| Research | Disc: Xbox 360 XGD2 image read and extracted | `tools/xdvdfs.py`; game partition at 0xFD90000 |
| Research | Archive: `gen/main.hdr` (Park-Miller XOR, version 3) + `main_0.ark`, 7,912 files | `tools/ark.py`; extraction keeps paths inside the output folder |
| Research | Scripts: all 177 `.dtb` decode (GH1's format once decrypted) | `tools/dtb.py`; `docs/dtb_format.md` |
| Research | Song list: 91 entries (48 career, 26 bonus, 9 tutorial, 8 test) | `docs/songs.md` |
| Research | Song audio: `.mogg` v10 = plain Ogg Vorbis, channel maps | `tools/mogg.py`; `docs/audio_and_charts.md` |
| Research | Charts: GH1 note layout + PART BASS / RHYTHM / GUITAR COOP, EVENTS, TRIGGERS | `tools/midi_chart.py` |
| Research | Scenes: all 503 `.milo_xbox` (directory v25, deflate blocks, inline sub-dirs) | `tools/milo.py`; `docs/milo_and_textures.md` |
| Research | Textures: all 3,533 (DXT1 / DXT5 / BC5, 16-bit swap, no tiling) | `tools/tex.py` |
| Research | Executable in Ghidra: XEXLoaderWV plugin, 24,029 functions | REA cannot open XEX (no PowerPC); project in `ghidra/`, export in `analysis/` (both git-ignored) |
| Research | Gameplay rules from the code: watcher, HOPO, scoring, Star Power, rock meter, practice, two-player modes | `docs/FINDINGS.md` |
| Research | What gd-game-01 gives gd02, and its lessons | `docs/reuse_and_lessons.md` |
| Engine (M1/M3/M4) | gd02 skeleton: disc image or folder, ARK, DTB, song list, charts for every part, `.mogg` via stb_vorbis, mixer with guitar mute | `engine/`; version 0.1.0 |
| Engine (M4) | Debug highway, keyboard input, `--selftest` on every part and difficulty ("all charts fully hit"), `GD02_SCREENSHOT` | |
| Gameplay (G1) | GH2's rules from `FINDINGS.md` replace the GH1 placeholders: chart parser, guitar / joypad watcher, scoring, Star Power, rock meter (constants from `scoring.dta` / `beatmatcher.dta` / `songs.dta`); focused PASS / FAIL rule checks in `--selftest`; `GD02_KEYS` scripted input | Single player; co-op, face-off normalisation and practice are TODOs in `game/player.h` |

## In progress

| # | Area | Item | Status | Notes |
|---|---|---|---|---|
| R1 | Research | Complete Ghidra export: rebuild functions from `.pdata` (the save/restore helpers were taken as non-returning), string references from `lis`/`addi` pairs | 🟡 | ~6,280 functions were cut short in the first export |

## Open

### Milestones

| # | Milestone | Item | Status | Notes |
|---|---|---|---|---|
| M0 | Scaffold | Icon (same GD Engine badge), first screen that picks the `.iso`, `--shadertest` | ❌ | Own builds and packages for gd-game-02 (user decision) |
| M0 | Scaffold | `local_build.sh` + `build_commands/`, build scripts for Linux, Steam Deck, Windows, macOS, Android | ❌ | Mirror gd-game-01; Android `io.github.mbracali.gd02`, own release key |
| M0 | Scaffold | Workflows `gd-game-02-{build,release}.yml` (tags `gd-game-02/v*`), CHANGELOG, licence files in the packages | ❌ | Releases only on `main`, as gd01 |
| M2 | Research | Practice audio: confirm the pitch drops when slowed (voice class of the song stream) | ❌ | FINDINGS open question 1 |
| M2 | Research | Co-op fail (player or band meter), face-off note assignment, `min_length`, co-op star thresholds | ❌ | FINDINGS open questions 2–5, 7 |
| M2 | Research | Lefty Flip, input sampling rate, controller latency (`watcher +0x2c`) on the 360 | ❌ | L3: where rules depend on sampling |
| M4 | Gameplay | Xbox 360 guitar and gamepad input (gd01's `input/`: controller Type, report, touch, dedupe) | ❌ | Keyboard only in the skeleton |
| M4 | Gameplay | Move to gd01's GL renderer (fences, limiter, post-processing, upscalers) | ❌ | The skeleton draws with SDL_Renderer |
| M5 | Scenes | C++ `.milo_xbox` loader and the Rnd object model (Mesh v28, Mat v28, Trans v9, Tex v10, …) | ❌ | Skip the per-object header GH1 doesn't have |
| M5 | Scenes | Our own GLSL material shaders from the research notes on GH2's `.fx` (never ship HLSL) | ❌ | L15, L18 |
| M5 | Scenes | `--view <milo>` scene viewer | ❌ | |
| M6 | Highway | The original track: surfaces, gems, HOPO tops, star gems, tails, smashers, hit effects, Lefty mirror | ❌ | gd01 `track_renderer` as reference |
| M6 | HUD | Score, multiplier, rock meter, Star Power meter from `hud/` | ❌ | |
| M7 | UI | gd01's interpreter on GH2's `ui/*.dtb` panels: boot, main menu, Quickplay, difficulty, loading, pause, results | ❌ | L1: log every unhandled message |
| M7 | UI | Profiles and save; every setter marks `needs_save` | ❌ | L12 |
| M8 | Venues | `world/` venues, venue scripts, lighting (TRIGGERS 48–50/52), camera director, particles, bloom / DOF | ❌ | |
| M9 | Band | Characters, skinning, clips, `BAND *` tracks, hand positions (notes 40–59) | ❌ | |
| M9 | Band | Lip-sync and faces (`.voc` "FACE…Harmonix", `.fac`), crowd | ❌ | |
| M10 | Modes | Co-op (shared Star Power, band streak, score normalisation), face-off, pro face-off | ❌ | FINDINGS "Two players" |
| M10 | Modes | Practice: sections, 100/85/65/50 % (resampled, no pitch correction), click track | ❌ | |
| M10 | Modes | Career with encores, store (26 bonus songs at 550), unlocks, cheats | ❌ | |
| M11 | Polish | Tutorials (9 charts, narration in 5 languages), `.wmv` videos (decoder to choose), credits | ❌ | |
| M11 | Polish | Enhanced Settings (L20), per-build defaults, Steam Deck pass, first release `gd-game-02/v1.0.0` | ❌ | |

### Questions for the owner

| # | Question | Status |
|---|---|---|
| Q1 | Reference captures from the original: Xenia or a real Xbox 360 | Decide later (user) |
| Q2 | `.wmv` video decoder (intro, logos) | ❌ |
| Q3 | Achievements, leaderboards and the DLC sample package (`xboxlive/gh2xp0001`) | ❌ |
