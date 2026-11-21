# gd-game-07 progress log

## 2026-11-17: GD07-001 discovery (done)

- Disc extracted into `gd-game-07/extracted/` (6,635 files, 6.30 GB, 13 s)
  with gd10's `xdvdfs.py`; name table `extracted/names.tsv` (126,468 names).
- `default.xex` imported into `gd-game-07/ghidra/ghwt.gpr` exactly as gd10
  (XEXLoaderWV, FixXexFunctions, DumpCFuncs, ExportAnalysisXex; 6 GB heap,
  7 minutes): 30,030 functions, all decompiled, 9,394 strings, 94,578 data
  refs, 1,773 C-function table entries in `ghidra/analysis/`.
- Compared with Metallica (gd10's rip, analysis and C++ decoders): paks, QB
  and QScript parse with 0 problems; gd10's `--formattest` decodes most of
  the disc and fails on 10,656 files in 8 groups (docs/FORMATS.md), mainly
  the compressed `.ska` keys, an earlier scene header and older Havok
  packfiles. 1,692 of 1,708 C functions are shared with Metallica (16 new,
  agents/CONTEXT.md "The executable").
- Recommendation: copy gd10's engine (DECISIONS D2); owner questions in
  OPEN_QUESTIONS.md.

Agent time: about 30 minutes (extraction, Ghidra, comparison, docs).

## 2026-11-17: GD07-002 M1 data layer (done)

- gd10's engine copied as gd07 (DECISIONS D5); builds with no warnings
  (Release, Ninja, -j4, 2 m 50 s from clean).
- `gd07 --formattest <World Tour disc>`: "all files parse" (from 10,656
  failures with gd10): .ska 5,707, .skin 1,696, .scn / .mdl, Havok .hkc / .rag,
  .ske, .wav 3,106, .table, .img / .tex. `.jam` (19) and `.qs` (8,495) have no
  decoder, as in gd10.
- Boot smoke (1,500 frames, offscreen): disc (133 songs) and scripts load,
  front end starts, the screen is black at uistate_null; Metallica's resident
  `axel_1.pak` is looked for. That is M2 (GD07-003).

Agent time: about 25 minutes (14:26 -> 14:51).

## 2026-11-17: GD07-003 M2 boot (in progress: reaches the main menu)

- `--walktest boot` passes (28.9 s): legal -> 4 movies -> press any button ->
  band name / logo -> controller tip -> main menu (DECISIONS D6). Screenshots
  in agents/scratch (s13500.png press start, bnl.png band name, mm.png main
  menu; git-ignored).
- Note: GD07_QUIT_AFTER / GD07_SCREENSHOT_AT are milliseconds, not frames.
- Left for M2: descinterface layout too large / off-centre (main menu, band
  name), z_frontend backdrop missing (a grain texture shows), the controller
  tip panel, axel_1.pak; walk names for World Tour's casing.

Agent time: about 20 minutes (14:52 -> 15:14).

## 2026-11-17: GD07-004 M3 one Quickplay song (done)

- `--walktest quickplay` passes: boot -> main menu -> Quickplay -> guitar ->
  set list -> Aggro (Expert, 785 notes, z_Frathouse) to the end -> breakdown
  -> cash rewards -> set list. `--walktest boot` passes; `--formattest` all parse.
- Fixes: DECISIONS D7. Screenshots: agents/scratch/shots (hud_uistate_gameplay,
  21 breakdown, 22 cash rewards).
- Next: the menus on the sound stage (viewport UI texture), band quickplay,
  the 23 venues (M4), CAS.

Agent time: about 50 minutes (15:15 -> 16:05).

## 2026-11-17: World Tour's menus (GD07-003 done)

- The menus now match World Tour's data: main menu on the board of the
  sound stage (GH World Tour banner, speaker walls, the rockers from behind),
  game mode / instrument on the rockers' jacket patches with the embroidered
  titles, the set list without the stray tab overlays (DECISIONS D8).
- Walks: boot and quickplay pass; formattest all parse. Screenshots:
  agents/scratch/mm5.png (main menu), shots4/m.png (game mode, instrument),
  shots5/hud.png (song).
- Song venues: LevelObject props now in place (Frat House neon sign, fan).

Agent time: about 1 h (16:05 -> 17:06).

## 2026-11-17: the darker scenes and the band (GD07-005 in progress)

- Light show snapshots now play, gh4 vertex lights lit, shared textures in
  (DECISIONS D9). The band is on stage. `--venuetest` with aggro: 18 venues,
  1 failure (z_Tool camera 60 / 240 fps). Shots: agents/scratch/venues_sheet.png,
  sF/hud.png (band in the Bayou).
- quickplay walk passes.

Agent time: about 1 h 20 (17:10 -> 18:30).

## 2026-11-17: owner's bug pass (GD07-009)

- Band from the game's own band builder (random rockers, clothed), VM fixes
  for the randomizer, the player's highway (DECISIONS D10). Walks: boot,
  qp_pause, quickplay, menus pass. Shots: agents/scratch/sM, sN (band and
  highway in the House of Blues), menus/sheet1-2.png.
- Next: GD07-010..012.

Agent time: about 1 h 30 (18:35 -> 20:03).

## 2026-11-17: owner's second bug list (GD07-013)

- Fixed: band name screen + logo, menu greying / focus colours / Options,
  Cathedral black band (shadow level registers), music studio room, mouse on
  board menus, menu stage node scripts (DECISIONS D11).
- Dev aids: walk `custom` (GD07_WALK_SCRIPT, GD07_WALK_P2, ops F0..F4,
  2C/2D, globalkey:, hover:at:/click:at:), GD07_BANDLIGHTLOG,
  GD07_SCENELOG, GD07_SHADOW_NOCASTERS, GD07_BANDSHADOW_WHITE.
- Open: GD07-014 (recording session), GD07-015 (main menu background).

## Plan (milestones; estimates are agent time)

Measured precedents: gd-game-10 went from a copy of gd04's engine to a
Quickplay song played to the results in about 2.7 h (11-10 23:50 -> 11-11
02:32) on a more different engine than World Tour is from Metallica; GH2
0.1.12 took about 2 h. World Tour adds more content than Metallica (23
venues, full create-a-rocker, the music studio), which carries most of the
work.

| Milestone | What | Gate | Estimate |
| --- | --- | --- | --- |
| M1 data layer | Copy gd10's engine / tools / build files as gd07 (D2), names and title ID, the compressed `.ska` keys, the earlier scene header, the older Havok packfiles, `.wav` chunks, the small leftovers | `gd07 --formattest <disc>`: all files parse (`.jam` / `.qs` aside) | 1.5-2.5 h |
| M2 boot to main menu | Boot movies and legal screens, World Tour's main menu, the 16 new C functions, sign-in natives offline | `--walktest boot` reaches the main menu, screenshot; no unhandled native in the log | 1-2 h |
| M3 one Quickplay song | Quickplay (one player, then band), set list, difficulty, a song on the highway to the results (audio: gd10's reader, D3) | `--walktest quickplay` plays one song to the results and back | 1-2 h |
| M4 venues / band / CAS | The 23 venues (scenes, lights, cameras, crowd, particles), the default rockers, create-a-rocker and instrument creation | `--venuetest` over every venue; a created rocker in a song | 6-10 h |
| M5 career and modes | Solo careers per instrument, band career, gigs / tiers, boss battles, face-off / pro face-off / battle, store, cheats, saves | a career walk through the first tier; a battle walk | 3-5 h |
| M6 music studio | GHTunes / recording studio: `.jam` files, z_studio zones, drum-kit / instrument sample banks, the editor screens | load and play `jam1.pak`'s songs; record a short take | 3-6 h |
| M7 module / release | Launcher module (build_module_07, games.yaml, iso_signatures with 4156081A), packages on every system, bundle checks, ready_to_play (version only when the owner asks) | every bundle verified; the launcher runs gd07 on Linux | 1-2 h |

Total: about 17-29 h of agent time, over several runs. Builds at -j4 (-j2
while another build-heavy lane shares the machine), one at a time.
