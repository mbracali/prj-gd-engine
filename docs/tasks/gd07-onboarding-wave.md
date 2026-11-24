# gd-game-07 (Guitar Hero World Tour) — onboarding-spec wave

> Owner 2026-11-24: run the universal onboarding spec (`docs/` onboarding doc) on
> World Tour as a **test of the spec format**, **aggregating what the module
> already has** with what we discover. Full send to 95%. Branch:
> `feature/20261123-metalicating`. This file is the shared agent context + the
> live status tracker. **Read this before starting any milestone.**

## The module today
- **Mature**: `gd-game-07/game-info.yaml` `completeness: 0.30`, `ready_to_play: true`.
  Boots, renders, plays songs, Explore works, controls Phase A+B done.
- **Engine**: `gd-game-07/engine/src/` (app, audio, data, game, input, render,
  script, ui, video, tests). gh4 engine (shared with gd10 Metallica / World Tour).
- **Ghidra**: `gd-game-07/ghidra/ghwt.gpr` (+ `analysis/` exports). The XEX cannot
  be opened live by REA — use the Ghidra static export + decoded QB
  (`tools/qbscript.py dis`, `tools/qb.py`).
- **Extracted disc**: `gd-game-07/extracted/` (DATA, default.xex, names.tsv, 5.9 GB).
- **Docs**: `gd-game-07/docs/FINDINGS.md`, `FORMATS.md`;
  `docs/tasks/WORLDTOUR_20261117.md`, `docs/spikes/study_ghwt_de.md`.

## Standing rules (every agent, non-negotiable)
1. **Aggregate first.** Before implementing anything, confirm what the module
   ALREADY does (engine code + FINDINGS + dev-hook runs). Only fill real gaps.
   Match the original 100%; this is a lawful clean-room reimplementation.
2. **Ghidra-grounded.** Cite concrete evidence (addresses, QB scripts, disc file
   formats) in FINDINGS for every claim and change. No eyeballing: data-derived
   expected-vs-measured checks.
3. **Branch**: work in your isolated worktree based on the `feature/20261123-metalicating`
   tip. **Commit as `Marcelo Bracali <bracali14@gmail.com>` with NO Claude
   co-author / footer / "Generated with" line** (single-line messages are safest).
   English only. **Do NOT push. Do NOT open PRs. Do NOT bump the launcher version.**
   Do NOT create/switch branches.
4. **Do NOT touch the gameplay judgment layer** (`game/rules*.h`, `player.cpp`
   timing) — it is faithful and pinned by tests.
5. **Build/test cheaply.** gd07 build:
   `cmake -S gd-game-07/engine -B build/<name> -G Ninja -DCMAKE_BUILD_TYPE=Release
   -DGD07_RUNTIME_SPIRV=OFF` then `cmake --build build/<name> -j4`. **Reuse the
   prebuilt ffmpeg** to avoid rebuilding deps:
   `-DGD07_FFMPEG_DIR=/home/marcelobracali/Development/prj-gd-engine/gd-game-10/engine/build/ffmpeg/linux/ffmpeg-prefix`.
   Dev hooks: `--selftest <disc>`, `--drumtest`, env `GD07_AUTOPLAY`,
   `GD07_CHARTEST_*`. Headless: `SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy`.
   **Only one heavy build at a time** (RAM): build once, at the end, at -j4.
6. **Update FINDINGS** with a dated section for your milestone (what the original
   does, what the port does, evidence, what remains honestly). Update the status
   table below (your row) in your commit.
7. **Report back** the commit hash, files touched, objective test evidence, and an
   honest "what remains".

## Milestone status (ratchets UP from 0.30; never reset down)
| Step | Gate % | Status | Owner-agent | Evidence |
|---|---|---|---|---|
| 1 Boot | 8 | ✅ done (pre-existing) | — | ready_to_play, renders, plays |
| 2 Encoders + model explorer | 16 | ✅ | agent (11-24) | `--formattest` all 31,554 files parse; model/song/venue/texture types 100% decoded (.qs/.jam out of scope); `--exploretest` 48 models across Venues/Guitars/Bass/Drums/Mics/Chars, 0 fail; `--selftest` 133 songs; `--chartest` 26 bodies skinned |
| 3 UI elements (bg/font/anim/widget/menu/HUD) | 26 | ✅ | agent (11-24) | UI is data-driven (QScript+descinterface via ui_system.cpp) — faithful by construction; `--uitest` 47 flow states, 912 elements, 0 UNHANDLED UI/HUD, 0 missing fonts; full-song 0 unhandled HUD widgets. 4 follow-ups (fire overlay, anim-out, nxgui desc, 2P band HUD) |
| 4 Animations & physics | 32 | ✅ | agent (11-24) | Every gh4 char-anim class reproduced (`--chartest`: 26/26 bodies animated, 26/26 faces move); venue/ped/stage anims confirmed. Physics: ported gd10 Havok-5.5 `Pack55` capsule/box decode (WT `.rag` shapes were all "Other" → ragdoll collision dormant) + 4 contact passes — now 207 accessory + 607 body collision capsules decode, worst hair/body overlap 0.0002 m. Anim: ported gh4 DrumKit difference-anim (320 fired, was 0). Pinned tests unchanged; 4 venue camera-framing FAILs are the pre-existing Step-2 baseline (Step 5 camera lane). `--drumtest` 20/20 |
| 5 Lights & effects | 40 | ⏳ | — | |
| 6 Gameplay (gtr/bass/drums/vocals + MP) | 50 | ⏳ | — | |
| 7.2 Universal gamepad menu | 55 | ⏳ | — | default: keyboard as X360 guitar, strum |
| 7.3 Quickplay | 65 | ⏳ | — | |
| 7.4 Shop | 68 | ⏳ | — | |
| 7.5 Create-a-rocker / instrument / music | 70 | ⏳ | — | |
| 7.6 Career | 85 | ⏳ | — | |
| 7.7 Tutorials | 90 | ⏳ | — | |
| 7.8 Online disabled | 91 | ⏳ | — | make all online unselectable |
| 7.9 Labels (console trademark scrub → gd-engine) | 92 | ⏳ | — | |
| 7.10 Enhanced menu + cheats + "The Glitches" band | 95 | ⏳ | — | Glitches = last band, all unlocked |

7.1 (recurrent): every mode must use the Ghidra-correct menus/UI/anim; **mouse
works in every menu**; fix the intermittent **left-mode** bug.
