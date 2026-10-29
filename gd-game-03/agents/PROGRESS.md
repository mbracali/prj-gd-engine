# gd-game-03 progress log

Guitar Hero Encore: Rocks the 80s (PS2) -> `gd03`. Kickoff:
`GD-GAME-03_KICKOFF.md` (repository root). Newest entries at the bottom.
"Verified" lines quote the command and its output; "built only" and "not
tested" are said explicitly.

## Overall estimate

Written at the start (2026-10-28 22:45): the disc is the PS2 build of Guitar
Hero II's engine (volume id `GH2`, directory version 24, GH2's script set), so
the plan is to start from gd-game-02's whole engine (GH2 Xbox 360) and swap the
platform layers for gd-game-01's PS2 ones (ISO 9660, ARK v3, the dtb cipher,
PS2 textures, VGS, SPU ADPCM, PSS video), then walk the older class versions.

| Milestone | Estimate |
|---|---|
| M0 set-up | 1 h |
| M1 data layer (dir v24, ~30 older class versions, PS2 textures) | 4 h |
| M2 songs, VGS audio, gameplay core | 2 h |
| M3 scripts, UI, asset audit | 3 h |
| M4-M6 highway, venues, band (mostly gd02's, re-verified) | 8 h |
| M7-M9 modes, extras, launcher module, packages | 6 h |
| M10 gaps | rest |

Total to a playable, packaged module: about 24 hours of agent time; 100 %
parity (every finding re-confirmed in SLUS_215.86) is longer.

## 2026-10-28 22:45, M0 set-up

Done:
- Disc ripped (`7z x` -> `extracted/iso`), ARK unpacked (1761 files, no
  compressed entry), all 187 .dtb decoded to text with the GH1 cipher.
- REA tried on SLUS_215.86: opens it, no analysis provider for MIPS
  (CONTEXT.md). Ghidra headless import + export: 11480 functions, 54180 data
  refs, 57278 strings. `ExportAnalysisElf.java`, `DumpAsm.java`, `ReadF.java`.
- dtb cipher confirmed at 0x002d95b0 (rand LCG table, indices 0 / 0x67).
- Engine: gd-game-02's at 0d04efe, renamed; Xbox layers replaced (ISO 9660,
  plain ARK header, dtb cipher, VGS streams, ADPCM samples; FFmpeg, stb_vorbis,
  XMA, XDVDFS removed; movies skipped until GD03-008).

Verified:
- `ninja -C build/linux` -> builds, 0 warnings (grep for warning/error empty).
- `build/linux/gd03 "$ISO" --scenetest` runs on the disc: `0 milo files
  parsed, 430 failed` (every one "directory version 24, expected 25"): the M1
  work.
- `python3 .github/scripts/repo_guard.py` -> `0 problem(s)`.

Not yet: the full `./build.sh` (Windows, Android) still carries gd02's FFmpeg
steps; adapted in M9.

Next: M1 (directory v24 and the older class versions). Estimate unchanged.

## 2026-10-28 23:05, M1 data layer (gate passed), M2 started

Done:
- Directory v24; ObjectDir 16, RndDir 8, WorldDir 11, BandStarMeterDir 4, Mat 27,
  Mesh 28 + the PS2 per-group block, Light 6, ParticleSys 27 (me); Character
  family (subagent); CamShot, Spotlight, LightPreset, EventTrigger, Sfx
  (subagent). Layouts: docs/formats.md. PS2 textures (4/8-bit palettes, CSM1,
  alpha x2, 16/24/32-bit) to RGBA8. Records with embedded marker bytes retried.
- VGS songs, menu loops (synth.dta's list), crowd streams; ADPCM sound effects
  trimmed to their sample count. Venue list from config/gh2.dta.
- 52 ms strum filter confirmed at 0x002356f8; selftest's strum-only bot
  allows the misses that filter forces.

Verified (fresh build, `ninja -C build/linux`: no warning or error lines):
- `build/linux/gd03 "$ISO" --scenetest` -> `430 milo files parsed, 0 failed`,
  `scene test passed: 0 typed objects failed, 0 files failed`
- `build/linux/gd03 "$ISO" --sfxtest` -> `274 decoded to exactly their sample count`, `sfxtest: OK`
- `build/linux/gd03 "$ISO" --selftest` -> exit 0; `187 .dtb scripts decoded, 0 failed`,
  `PART GUITAR 156 charts, 67007 gems, 0 not fully hit`, `PART BASS 120 charts, 73749 gems, 0 not fully hit`,
  `all input checks passed`
- `repo_guard.py` -> 0 problems.

Next: M2 rule re-confirmation in SLUS_215.86 (Star Power, multiplier, rock
meter), then M3 (scripts, UI, audit, walk).

## 2026-10-28 23:12, M3 in progress (scripts, UI, boot flow)

Done:
- `memcard` object as the PS2's (0x0014c078): get_info, load_data /
  save_data / format on career.json, space_needed 0x3ac00, localize
  `<token>_ps2` with {SLOT} / {REQ_SPACE}. Boot flow is the PS2's:
  bootup_load -> bootup_create_save_check -> bootup_create_save ->
  cut_scene_screen -> splash_screen -> main_screen.
- song_provider set_quickplay / get_quickplay; is_missing_multi_controller (D4).
- uitest's campaign checks rewritten for this disc's campaign.dta (6 tiers,
  Easy 5 via 0x00130c58, required songs 0x001313d8).

Verified (fresh build):
- `build/linux/gd03 "$ISO" --scripttest` -> `scripttest: OK`
- `SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy build/linux/gd03 "$ISO" --uitest` -> `uitest: campaign: 26 checks, 0 failed`, `uitest: OK`
- `--walktest quickplay` -> FAILED: boots through the menus into ballstothewall
  and plays, but the PS2 track (one PanelDir `track/gen/track.milo`, no
  GHTrackDir / track_panel_1p) is not loaded, so the debug highway draws and
  the Star Power glow check fails. That is the M4 work, done next.

## 2026-10-28 23:45, M3 done; M4 track and M5 venue checks in

Done:
- The PS2 highway (subagent): track/track.milo drawn as TrackPanel does
  (addresses in render/track_view.cpp). Screenshot checked by eye: gems,
  tails, smashers, beat lines, HUD over the venue.
- Walks use the PS2 screens (autosave, boot flow). Asset audit:
  `--audittest <walk>` writes docs/audit.md.
- Venue / char tests carry this disc's expectations (unused milos [C data],
  fest's trigger-controlled hides, waypoint radius bound, golden baselines).
- The 60 / 240 fps camera check skips a shot's first sample (paths that jump
  at their first key).

Verified (fresh `ninja -C build/linux`, no warning lines; agents/scratch/alltests.sh):
- scenetest rc=0 :: scene test passed: 0 typed objects failed, 0 files failed
- selftest rc=0 :: all rule checks passed (0 charts not fully hit)
- sfxtest rc=0 :: sfxtest: OK
- scripttest rc=0 :: scripttest: OK
- uitest rc=0 :: uitest: OK
- venuetest rc=0 :: venue test passed: 0 failures, 44 world milos loaded, 8 unused
- chartest rc=0 :: chartest: 0 failures
- shadertest rc=0 :: all 587 shader programs compiled (GL and GD03_GLES=1)
- `--walktest quickplay` -> walktest quickplay: OK (boot -> song -> results -> back)
- facefxtest rc=1 (GH2 goldens; GD03-014).
- `./build_windows.sh` -> 0 warnings in build/windows/compile.log (agent's report; rerun at the next gate).

Next: the other walks (lose, pause, attract, back, career*, coop, faceoff,
enhanced, cheats, mouse, gamepads, stickyaxis), facefxtest, M6-M9.
