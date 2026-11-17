# gd-game-24 progress log

Rock Band Unplugged (PSP) -> `gd24`. Newest entries at the bottom.

## GD24-001 discovery (2026-11-17, about 1.5 h of agent time)

Done: rip (444 files), EBOOT decrypted, Ghidra project with Allegrex,
named imports and an export (28,942 functions), the formats in
`../docs/FORMATS.md`. Not done: any engine code (owner approves the plan
first).

What the game is: Harmonix's Rock Band beatmatch code ported to the PSP by
Backbone (sources `src/rock/beatmatch`), Frequency / Amplitude play: one
player, four instrument tracks, four lanes each, switching tracks. 41 songs +
14 tutorial tracks, 24 tour venues, 5 languages.

What makes it different from our other games: almost all data are **heap
images** (raw RAM with absolute pointers, objects identified by vtable) and
the song audio is **encrypted** 5-stem ATRAC3plus. Both need the EBOOT's
code first; everything else (PNG, AT3 previews, PMF, TEXT, SBlk ADPCM) is
standard.

### Plan (owner to approve; agent time, from git history of gd10 / gd02)

| Milestone | Content | Estimate |
|---|---|---|
| M0 set-up | Folder from gd-game-02's layout (engine/, build_commands/, CMake, `gd24`, FFmpeg with atrac3p + h264 + mpegps), disc reader (ISO 9660 + UMD paths) | 1-2 h |
| M1 data layer | STR container + heap-image reader (chunk bases from the loader, vtable -> class table); preview AT3 -> FFmpeg atrac3plus; TEXT; SBlk/ADPCM (gd01's decoder); PMF through FFmpeg; `--formattest` | 4-6 h |
| M2 first boot (GD24-002) | One song: the four tracks' gems for one difficulty, tempo map, sections from BEATMATCH; the 5 stems in sync; 4-lane highway (debug look unless Q2 says otherwise), autoplay and real input, one track at a time | 4-6 h |
| M3 gameplay rules | Track switching, phrase capture and the auto-play "deploy" of captured phrases, overdrive (energy), multiplier, scoring, fail, stem muting, results; from BeatMatcher / PhraseDB / TrackWatcher | 6-10 h |
| M4 menus | UI heap images (UIList / UIElement / UIAnim / UITextBox), fonts, quickplay flow, settings, GAMEPADS / Enhanced menu per the guidelines | 10-15 h |
| M5 venues and band | GE meshes / swizzled paletted textures / materials / cameras from the venue images, the 3D highway, characters + ANIMS clips | 15-25 h |
| M6 career and the rest | World tour / world map, band creation (ROCKERMAKER), presets, saves, jukebox, tutorials (BNK voice-overs), movies, launcher module + packages | 10-15 h |

Total about 50-80 h of agent time; the first boot (M0-M2) about 9-14 h.

### Reuse

- gd-engine-core: platform, inputs, audio, render, image backends.
- gd-game-05 (Rock Band 1): the same Harmonix beatmatch classes (GameGem
  record, SongData, track watchers, scoring constants) as a reference for
  the layouts and rules; its chart / audio / highway code as a model.
- gd-game-04 / gd-game-10: the FFmpeg build (add `atrac3p`, `h264`,
  `mpegps`/PSMF) and the video player.
- gd-game-01: PS-ADPCM decoder for the SBlk banks; gd-game-02: folder
  layout, launcher module and packaging.

### Risks

- Heap images: every needed class layout must be read from the EBOOT
  (vtable map); cost grows with each feature (UI and venues the most).
- The song audio: the `.ATM` stems are encrypted and out of scope (D6);
  the previews play.
- The function export splits some functions (FindPspFunctions); the
  decompiled C needs a check against the assembly in tricky places.
- PSP rendering data (GE display lists, swizzled 4/8-bit textures, VFPU
  math) for M5.

## GD24-002 start (2026-11-17): owner's answers in

Owner approved the plan (OPEN_QUESTIONS 1-5, DECISIONS D5): the first boot
shows the **original** highway (no debug highway), the launcher recognises
PSP discs (.iso, .chd) with it, PSP mapping + GAMEPADS + guitars.

Re-estimate of the first boot with the original highway: the highway is an
object graph in a heap image (meshes, materials, swizzled PSP textures,
the gem models), so M2 also needs the heap-image reader for those classes
and a small GE-format mesh / texture renderer: +6-10 h. M0-M2 now about
15-25 h of agent time.

Song audio: the `.ATM` files are encrypted. Owner 11-17: no protection is
ever broken, so the full 5-track audio is out of scope and the key work was
dropped (D6); songs play their unencrypted preview AT3 from the disc.

## GD24-002 lane B, 11-17 (about 3.5 h of agent time)

Done, each committed:

1. **The window**: `gd24 <disc>` and `gd-game-24.so` open an SDL window
   (gd-engine-core's OpenGL 3 device presenting a 960x544 CPU canvas drawn
   with SDL's software renderer and 8x8 font). Our own status screen: the
   title from PARAM.SFO ("Rock Band Unplugged™", ULUS-10418 v1.00), the 41
   songs + 14 tutorial tracks named from STR_<L>_MUSIC.TXT (title / artist
   / album triples in folder order); ENHANCED basics (screen mode, with
   GD24_FULLSCREEN as Big Picture; volume; song-name language) saved in
   enhanced.txt; mouse. Runs in the dev launcher through
   `tools/try_in_launcher.sh` (the launcher refuses ready_to_play: false
   games; the script passes a git-ignored games.yaml marking it ready).
   Screenshots looked at: the canvas and the real window (spectacle),
   standalone and inside the launcher.
2. **Previews**: `<SONG>_PREVIEW.AT3` through FFmpeg's atrac3p (gd24's own
   FFmpeg config, gd10's pattern + atrac3p only), delay cut, fact length,
   looped on a gd-engine-core audio output (miniaudio). `--audiotest`:
   41/41, 0 decode errors, every length = the fact chunk (decoder gives
   +368 samples past it for every song), RMS -13.9..-17.7 dB, 1,216 s in
   2.5 s; played through the real device.
3. **Controls**: `src/input` on gd10's model (PSP buttons from keyboard and
   gamepads, 2 bindings per control, merged actions, gd10's guitar
   detection, controllers.json); guitars play lanes 1-4 in a song and
   confirm / go back in menus; the GAMEPADS screen (universal layout).
   `--inputtest` with SDL virtual pads: 33/33. The PSP lane mapping is
   [I] (FORMATS.md "Controls"): the defaults are not in a data table and
   the tip glyphs are not decoded yet.
4. **Chart reader** (once): `src/data/beatmatch` + `--charttest`: base
   0x096E9A80 (FORMATS' 0x096E9800 estimate corrected), the four PART
   tracks and their four difficulty vectors: 41/41 songs, 334,338 gems, one
   time per tick in every list.

Not done: the original highway (GD24-002's main part: the highway heap
image, GE meshes / textures), playing a chart (autoplay, hit detection),
tempo map / phrases / Overdrive objects, the controls' real defaults, the
tutorials' charts, packaging (`build.sh` module recipe; not ready_to_play).

## GD24-002 highway, first part (11-17, about 2 h)

Small fixes: the status screen's album line ("Unknown" not shown), the
GAMEPADS dropdowns fitted with "...".

Done: the highway's data decode (`src/data/psp_gfx`, `--highwaytest`): the
heap-image textures (swizzled 4/8 bpp + palette) and GE display-list meshes
of COMMON.STR; every highway piece found (FORMATS.md "Graphics in the heap
images"); the four gem textures measured red / yellow / green / blue.

Next (the rest of the first boot), in order:
1. The placement: the scene-graph objects around the mesh objects (node
   transforms, the mesh scale from its bounds), starting from the track
   plane object (five material / block pairs) and the plate meshes; the
   resource directory lists (FUN_088dc2d4) to walk them properly; the code
   that spawns gems on lanes (search the EBOOT for the float constants of
   the lane x positions, or trace the users of the gem list objects, the
   GameGemList asserts at 0x08b40c37 / 0x08b4195c) and the camera.
2. A small renderer on gd-engine-core's device for these meshes / textures
   (the canvas becomes a 2D overlay on it).
3. The chart scrolling with the preview (the preview covers ~30 s of the
   song: its offset into the song is not known yet, [I]), input judged
   against the gems (hit / miss), track switching.
Estimate: 6-10 h of agent time, most of it step 1.

## GD24-002 first boot, the highway and the chart (11-17, about 3.5 h)

Done, committed (c31989e, fb25e1a and the docs):

1. **Placement** (FORMATS.md "Mesh objects, materials, placement"): mesh
   objects -> materials -> textures in COMMON.STR, the scale (the box's
   largest |coordinate|), blend modes, skinned display lists, GE vertex /
   material colours; the lanes and the strike line from the track plane's
   own lane marks (x -1.100 -0.362 0.379 1.127, y -4.488) [C]. The speed
   from the disc's DTA (`y_per_second` 7.5..11.5 per difficulty) and the
   hit window (`slop` 100 ms) [C data, whether the code reads them: I].
   Not found: the camera and the tracks' spacing [I, chosen to frame the
   track; GD24_CAMERA overrides].
2. **Renderer**: `src/render/highway_renderer` on gd-engine-core's device
   (1920x1088 colour + depth target, alpha / additive pipelines, the 2D
   canvas on top, screenshots read back).
3. **Playing**: Confirm on the status screen (Triangle: difficulty, Square:
   preview) or `--song <FOLDER> [--difficulty 0..3] [--track 0..3]`; four
   tracks side by side with their icons, the active one lit, beat lines
   from the gems' ticks (a bar line every 4 beats [I]), plates, gems,
   crash pieces on hits; lanes judged (hit / miss / wrong press, chords),
   Left / Right switch tracks (camera eases over). The preview plays from
   the song's `(preview start ...)` plus a measured onset lag
   (`--synctest`: 18/41 exactly at start, the rest within +-200 ms).
   Checks: `--highwaytest` 0 failures (models + lane marks);
   GD24_AUTOPLAY=1: 14/14 hits, 0 misses, 0 wrong on ABC Expert guitar in
   6 s; four lane keys pressed off the notes on WOULD Easy: 4 wrong, 13
   misses; screenshots looked at (ABC Expert guitar / bass, track switch).

Next: the camera and the tracks' spacing from the code (the highway
draw code is not found yet), Overdrive / phrases / scoring (GD24-003), the
PSP controls' real defaults (the DTA's slots are the PS2 legacy), Easy vs
Medium lists (same counts on several songs: which vector is which [I]),
the venue behind the see-through tracks (GD24-005).

## GD24-003 gameplay rules (11-17, about 4 h)

Done (commits d0e7afb and the docs):

- Small fix: the song screen's bottom bar (score / streak) no longer runs
  into the help line (help at the top, smaller).
- Time signatures read from the BEATMATCH images [C data, all 55 files];
  bars from them.
- The rules from the code (FORMATS.md "Gameplay rules", DECISIONS D7-D9):
  phrases (2-bar windows [I]) and the Phrase Indicator, capture (the track
  plays on its own, released after 8 bars / 11 at x2+ [C]), multiplier int +
  1 up to 5, reset by a phrase miss after the capture timer [C], gem /
  capture points x multiplier [C, values I], Overdrive doubling (5 -> 11)
  [C], Overdrive runs fill the Energy meter, deploy at half (Space / Cross),
  failed tracks rescued [C text], per-track health, crowd meter, failed
  tracks, song failure [C structure, values I]; Easy / Medium protection [C].
- Song screen: our HUD (score, multiplier, streak, Energy, crowd, healths),
  the indicator frame, track tints, the results panel at the preview's end
  (or at a failure); GD24_AUTOPLAY now changes tracks and deploys Overdrive.
- `gd24 --rulestest "$ISO" [song|all] [--difficulty n]` (headless, < 1 s
  for 41 songs): perfect autoplay passes every song and its score replays
  from the event log by the rule alone; an idle player fails at the time
  predicted from the chart alone (ABC Expert 41,133 ms vs 41,120 predicted;
  all 41 songs within one frame); a gem left out after five captures sets
  the multiplier back to 1. 41/41 songs pass on Easy, Medium, Hard, Expert.
  ABC Expert perfect: 174,675 (gems 78,175 + captures 96,500), 35 / 35
  phrases, x5 at the 4th capture, x11 in Overdrive, 2 Overdrives.
- Screenshots looked at: ABC Expert autoplay mid-song (HUD, captured
  tracks), the indicator frame on Vocals, the results panel.

Not found: the tuning block's source (0x08b5a440: no code writes it, no
disc file holds the DTA's values as floats), so all point / health values
are [I]; the DTA's `max_points` is ~11x our perfect scores.

Left: the real numbers; solo sections (0x08000000 gems); sustains (found
after the commit: gem flag bit 0x02 clear + the length at +0x0A, FORMATS.md;
not judged / drawn yet: the tail texture is probably `susTttt`); stars (min / max points); the original's HUD,
indicator and results UI (GD24-004); stem muting (needs the song audio);
the capture queue's finer rules; bouncing arrows; Quick Switch / Track
Wrapping options.
