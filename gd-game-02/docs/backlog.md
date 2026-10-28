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
| Research | Executable in Ghidra: XEXLoaderWV plugin | REA cannot open XEX (no PowerPC); project in `ghidra/`, export in `ghidra/analysis/` (both git-ignored) |
| Research (R1) | Complete export: functions rebuilt from `.pdata` (save/restore helpers inlined and returning), 35,365 `lis`/lo references, 28,394 functions, 5,732 strings with their users, `refs.tsv` | `tools/ghidra_scripts/` (FixXexFunctions, ExportAnalysisXex, README); VMX128 opcodes stay undecoded (a Ghidra PowerPC gap) |
| Research | Gameplay rules from the code: watcher, HOPO, scoring, Star Power, rock meter, practice, two-player modes | `docs/FINDINGS.md` |
| Research | What gd-game-01 gives gd02, and its lessons | `docs/reuse_and_lessons.md` |
| Engine (M1/M3/M4) | gd02 skeleton: disc image or folder, ARK, DTB, song list, charts for every part, `.mogg` via stb_vorbis, mixer with guitar mute | `engine/`; version 0.1.0 |
| Engine (M4) | Debug highway, keyboard input, `--selftest` on every part and difficulty ("all charts fully hit"), `GD02_SCREENSHOT` | |
| Gameplay (G1) | GH2's rules from `FINDINGS.md` replace the GH1 placeholders: chart parser, guitar / joypad watcher, scoring, Star Power, rock meter (constants from `scoring.dta` / `beatmatcher.dta` / `songs.dta`); focused PASS / FAIL rule checks in `--selftest`; `GD02_KEYS` scripted input | Single player; co-op, face-off normalisation and practice are TODOs in `game/player.h` |
| M0 | Icon (the GD Engine badge), first screen that picks the disc image (dialog, drop, `.iso` next to the program, remembered) | `tools/make_icons.py`, `platform/app_icon`, `platform/disc_picker` |
| M0 | `local_build.sh` + `build_commands/`; Linux, Steam Deck, Windows, macOS, Android builds; `package.sh`; Android `io.github.mbracali.gd02` with its own release key | Linux, Steam Deck, Windows, Android built here; macOS untested (no Mac); Windows not run, APK not run on a device |
| M0 | Workflows `gd-game-02-{build,release}.yml` (tags `gd-game-02/v*`, `ANDROID_GD02_*` secrets), CHANGELOG | Not run yet (branch local) |
| M4 | Input: gd01's layer (bindings, Type, report, touch, dedupe, settle, Steam virtual pads) with GH2's controller types (`hx_guitar_xbox_new`, `joypad_guitar`, lefty, PS2 guitar) | 22 input checks in `--selftest`; `GD02_VIRTUAL_PAD` |
| M4/M5 | New renderer from GH2's own: GL 3.3 / GLES 3 device (Vulkan-ready interface), GH2's shader keys, Mat rules, lighting, fence pacing, `--shadertest` (546 programs) | `docs/renderer.md` |
| M5 | C++ `.milo_xbox` loader: every typed object of all 503 milos decodes (`--scenetest`); `--view <milo>` | `docs/milo_and_textures.md` |
| M6 | GH2's original highway from `track_new` (surface, gems, HOPO / star / bonus, built tails, smashers, flames, beat lines, Star Power) and the `hud_sp` HUD (score, multiplier, streak, star meter, rock meter, messages) | `docs/highway_and_hud.md`; `GD02_DEBUG_HIGHWAY=1` for the old one |
| M6 | Particles (generic, `render/particles`), sparks, tail and star-meter particles, intro sequence, win explode + "You Rock!", fail wind-down, Lefty, two-player tracks and the vs / co-op / practice HUDs (dev flag `GD02_MODE`) | `docs/highway_and_hud.md`; the 360's peak flames draw nothing in the original |
| M7 | gd01's interpreter on GH2's 177 `.dtb` (`#merge`, `new`, `handle`, task units, L1 logging); `--scripttest` | `docs/ui_and_frontend.md` |
| M7 | UI object decoders, text chains, styles; GH2's front end: boot, title / attract, main, Quick play, difficulty, loading, game, results, high scores, pause, lose, options, credits, bonus videos | `--uitest`, `--walktest`; debug song list behind `GD02_DEBUG_MENU=1` |
| M7 | Save (`career.json`, GH2's version 17 data, atomic + `.bak`, every setter marks it dirty: L12); GAMEPADS screen | |
| M8 | All 8 venues (sp / _mp / _coop chains, proxies): decoders for every world class, world clock, anims, EventTriggers, WorldDir scripts as `world`; `--venuetest`, `--venue` / `GD02_VENUE` | `docs/venues.md` |
| M8 | Camera director (CamShot playback, pick_shot, script schedule), lighting preset manager, spotlights / floor spots / flares, bloom + DOF post, crowd (fullness, crowd3d, instanced), crowd audio loops, encore fx and venue extras | Golden director log in `--venuetest` |
| M7 | Controls and menus bug-fix wave: frets as kPad buttons for every controller (gd01's `uiButton`; no more joypad A = select + L1), Back pauses in a song, Esc ×3 quits, splashes / videos skip like gd01, key repeat reset in transitions; the shared metacam (camera off-centre after going back); the GAMEPADS help icon | `docs/ui_and_frontend.md` "Controls in the menus", "Metacam"; `--walktest back`; `GD02_KEYS` `k` / `j` items |
| M7 / Q2 | `.wmv` videos: minimal LGPL FFmpeg 7.1.5 (ASF, WMV3, WMA v2), shared libraries + licence notice in every download | `--playmovie` |
| Bug-fix 3 | Options: GH2's six entries in GH2's order plus the port's Enhanced Options after Video Settings (the column re-spaced, a little smaller); no GAMEPADS in Options or on the main menu's help bar; Audio Settings shows its help bar (the panel's FOCUS_MSG on enter) and its poster's top. Enhanced Options: gd01's list in GH2's font over a live shot of the second venue (small1) with the band; Screen Mode, In-Game Max FPS, Show FPS Counter, Internal Resolution (448p (ps2), 720p (x360)...) work and are saved in `enhanced.json`; Gamepads opens the GAMEPADS screen; the rest is listed "not available yet". The Glitches in every save. GH2's long cheats (button codes at the main menu, CheatsManager 0x8235B5B0) and the CHEATS section with GH2's names | `docs/ui_and_frontend.md` "Enhanced Options", "Cheats", "Save"; `--walktest enhanced`, `--walktest cheats`, The Glitches check in `--walktest quickplay` / `career` |
| Bug-fix 2 | Note hits checked against GH2's watcher (anchoring on single notes and HOPOs allowed, chords exact, sustain release rules: unchanged and right); exact details fixed: +0x94 set on hits, swingAtHopo on a fixed HOPO-able strum, overstrum ends the sustain before the 50 ms gap, missed-gem pick. Faces: kTaskSeconds now follows the song with the menus (the lip sync / expressions ran on the menus' clock and the singer stopped posing) | `docs/FINDINGS.md` "Fret matching", `docs/facefx.md`; new rule checks in `--selftest`, task-clock check in `--walktest quickplay`, `GD02_FACE_CAM` |
| Bug-fix 4 | Owner's 2026-10-28 list. Camera: the director's filter and shake ran once a frame instead of once a 1/60 s (2.4x at 144 Hz, 4x uncapped) and the shake drew from the shot picks' RNG, so the frame rate changed the shots; now GH2's 60 Hz steps at any rate (`--venuetest` camera rate check, 60 vs 240 fps, every venue). Band: a lower LOD (band_POV's `set_min_lod 1`, distance) drew nothing (the world pose's saved `lod1.grp` flag won over the LOD switch); outfits' LOD / shadow groups merged by object as 0x82158F30 (same-named groups were missed; the guitar's `shadow_guitar.mesh` drew, black and depth-writing, over the neck); `--chartest` draw-list check per LOD. Menus: GH2's `RndAnimFilter` reversed ranges (metacam's backward groups now play backwards: going back no longer replays the forward entrance), `ui_enter_back` / `_forward` events, shared subdirectories (meta_bricks' light) posed in every view (`--walktest back` bricks check). Shutdown: a null outfit slot (Band destructor). Windows: `gd02.log` + crash address in the save folder; Salvation and every song start on Linux and under Wine (not reproduced). macOS: unsynchronized buffer streaming, `GD02_PERF=1` | `docs/venues.md`, `docs/band.md`, `docs/ui_and_frontend.md`, `docs/renderer.md` |

## In progress

Nothing right now.

## Open

### Milestones

| # | Milestone | Item | Status | Notes |
|---|---|---|---|---|
| M2 | Research | Practice audio: confirm the pitch drops when slowed (voice class of the song stream) | ❌ | FINDINGS open question 1 |
| M2 | Research | Co-op fail (player or band meter), face-off note assignment, `min_length`, co-op star thresholds, timing class | ✅ | M10.2: FINDINGS open questions 2–5, 7, 8 answered; `docs/multiplayer.md` |
| M2 | Research | Watcher bass-cue path (OnHit / OnMiss ask `+0x10` vtable `+0x28` by tick, then `bass_%d.cue`): which object and chart data | ❌ | FINDINGS open question 9 |
| M2 | Research | Lefty Flip, input sampling rate, controller latency (`watcher +0x2c`) on the 360 | ❌ | L3: where rules depend on sampling |
| M10 | Modes | Practice track surface (rosewood) and practice UI pieces | ❌ | Two-player rules: see below |
| M7 | UI | Touch lanes for the new renderer | ❌ | Input API ready |
| M7 | UI | gd01's mouse in the menus (hover focuses, click selects, wheel) | ❌ | gd01 `UiSystem::mouse` |
| M7 | UI | Main menu words stacked tightly: matches BandButton's fit code (re-checked in bug-fix 3: rows 14 units apart, `fit_text` 1 into 9 units against size 30 × leading 0.7, 0x8214C7B0 / 0x821CFF08), compare with a capture of the original | 🟡 | Q1; `docs/ui_and_frontend.md` "Not done" |
| M7 | UI | Enhanced Options not built yet: Steam Deck settings, mouse, touch, Rock Band guitars, XInput, calibration, sharper text, anti-aliasing, AI upscaling, bokeh, particles, bloom, cinematic colour, colour grading, film grain, chromatic aberration, light streaks, ambient occlusion, sharpening, Star Power blur, band shadows, more lights, ground fog, shaky camera, user files export / load, cut content, no fail, autoplay, forgiving HOPOs | ❌ | Listed greyed in Enhanced Options (gd01's list); L20 |
| M9 | Cheats | Flaming Head: the code works (its message, `$cheat_enabled_flaming_head`) but the band's particle systems (`cheat_headflames start`, hand flames) are STUBs, so nothing burns | ❌ | Listed "not available yet" in Enhanced Options |
| M9 | Cheats | Horse Head: GH2 has its name (`cheat_horse_head_mode`) but no code in `long_cheats.dta` (cut from the game) | — | Not listed |
| M7 | Cheats | `quick_cheats` (`config/cheats.dta`, the debug keyboard's `set_key_cheats_enabled`): not wired (development cheats, not the players') | ❌ | |
| M7 | UI | Endgame receipt lighting (`turn_off_ambient`), live volume sliders, help bar boxes, progress export / import | ❌ | `docs/ui_and_frontend.md` "Not done" |
| M7 / M8 | Audio | Sound effects: SynthSample v5 / SampleData v11 = plain XMA2 (FFmpeg xma2), Sfx and sequences, the UI's and the venues' cues, menu background sfx | ✅ | `docs/audio_and_charts.md` section 4; `--sfxtest` |
| M7 / M8 | Audio | Sound effect voices as the 360 plays them: envelope (attack / release), 5.1 pan + stereo fold, FxSendReverb sends with XAudio's reverb (bit-exact port), master limiter, `set_master_vol` / `toggle_limiter`, fx volume per GH2's callers, RandomGroupSeq rule (the PS2 fx cores do nothing on the 360) | ✅ | `docs/audio_and_charts.md` section 4; `--sfxtest` |
| M8 | Venues | GH2's beam shader; free camera | ❌ | `docs/venues.md` open items; band-targeted shots wait for M9 |
| M9 | Band | Characters, outfits, LODs, skinning, clips (CharDriver), the pose (servo, IK, twists, hair, look-ats), MidiParser (hands, fret position 40–59, `BAND *` tracks), GuitarCharacter, the band on stage, drum kit hits, director hooks | 🟡 | Phase 1 done: `docs/band.md`, `--chartest`, `--view char/<c>`; the shadow pass, hair collision open |
| M9 | Band | Walks and interacts (M9.6): CharWalk planner and steering, the GuitarCharacter timeline (walks, holds, Star Power, interacts), the walk schedule to solo / win spots, walk_delays / interact_probs by excitement, regulate / regulate_override, lose_teleport, waypoint natives | 🟡 | `docs/band.md` "Walks"; open: the waypoint list's load order, CharWalk / lip servo PollDeps, the interact bone offset, arrivals 10–40 units off before the servo regulates |
| M9 | Band | Faces (M9.8): FaceFX lip servo on the band (blink track, expressions, the singer's `.voc`, eye entries, viseme poses) | ✅ | `docs/facefx.md`, `--chartest` faces |
| M9 | Band | Crowd (M9.9): one Character per archetype (the `<v>_chars` proxy) on the crowd clip sets, crowd.dta's mix per excitement, hands and lighters from the clips, the card textures rendered per frame (0x82247310) and drawn on the placements, the shot's 3D placements | ✅ | `docs/venues.md` "Crowd", `--chartest` crowd golden; open: how GH2's card texture gets its alpha, the lighter flame's show / hide (`$cheat_crowd_heads` works: bug-fix 3) |
| M9 | Band | Character / guitar choice (M9.10): PlayerConfig look, character_provider, CharsysPanel (menu characters, the door), GuitarSelectPanel / GuitarDisplayPanel, the profile's look per difficulty in `career.json`, the band from the configs; endgame photos (`photo_<outfit><0..3>`) | ✅ | `docs/band.md` "Characters, guitars and photos", `--walktest career`; GH2's unlock table: M10.1; the multiplayer screens: M10.2 |
| M10 | Modes | Co-op (band performer: streak, score, the band's meter that fails the song, shared Star Power both must engage), face-off (section ownership, active_players_changed), pro face-off; normalisation; the multiplayer menus, two-player HUDs, results and the band on stage | ✅ | M10.2: `docs/multiplayer.md`, `--selftest` two-player rule checks, `--walktest coop|faceoff|proface`; open: face-off HOPOs per player's gem list, the other player's face-off gems on a track, the per-player guitar mute [I] |
| M10 | Modes | Practice: sections, 100/85/65/50 % (resampled, no pitch correction), click track | ❌ | |
| M10 | Modes | Career with encores, store (26 bonus songs at 550), unlocks | ✅ | M10.1: `docs/career_and_store.md`, `--walktest career_full`, uitest's campaign checks; co-op's records and bass unlocks: M10.2; open: win_game_screen and unlock_guitar_screen not walked, the stone ending's stream |
| M11 | Polish | Tutorials (9 charts, narration in 5 languages) | ❌ | |
| M11 | Polish | Enhanced Settings (L20): the rest of the list, per-build defaults, Steam Deck pass, first release `gd-game-02/v1.0.0` | 🟡 | Enhanced Options screen and four settings done (bug-fix 3) |

### From bug-fix 4 (2026-10-28)

| Area | Item | Status | Notes |
|---|---|---|---|
| Windows | "Salvation does not start in quick play": not reproduced (Linux, Wine/Proton with the Windows build: `--song`, the quick play walk and real-time menus; all 83 songs start). Needs the owner's `gd02.log` (save folder) | 🟡 | The log now records a crash's address |
| Camera | "Goes under the venue" (mostly Windows): not reproduced; the frame-rate dependence is fixed and checked per venue | 🟡 | Screenshots or the shot name (`GD02_WORLDLOG=1`) would pin it |
| macOS | ~20 fps on an M4 Pro: to measure with `GD02_PERF=1` (and `GD02_GL_STREAM=subdata` against the default `map`) | 🟡 | `docs/renderer.md` "Performance" |
| Band | sheraton lists `shadow_guitar.mesh` in its own LOD groups (drawn as GH2's data says) | — | |

### Questions for the owner

| # | Question | Status |
|---|---|---|
| Q1 | Reference captures from the original: Xenia or a real Xbox 360 | Decide later (user) |
| Q2 | `.wmv` video decoder (intro, logos) | ✅ minimal LGPL FFmpeg |
| Q3 | Achievements, leaderboards and the DLC sample package (`xboxlive/gh2xp0001`) | ❌ |
