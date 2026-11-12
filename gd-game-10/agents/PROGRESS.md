# gd-game-10 progress log

Guitar Hero: Metallica (Xbox 360) -> `gd10`. Kickoff:
`docs/spikes/GD-GAME-10_KICKOFF.md` (wave 01, branch `feature/spike-metal`).
Newest entries at the bottom. "Verified" lines quote the command and its
output; "built only" and "not tested" are said explicitly.

## Final report, proof of concept (2026-11-11, about 3 h of agent time, 23:39 -> 02:40)

Scope (defaults chosen, owner to confirm: kickoff "Owner's answers"): boot
first, then one Quickplay song on guitar. Version stays 0.0.0;
ready_to_play false; not in the launcher's packages.

**Milestones.** M0 done. M1 done for what the boot and a song need (paks,
QB, scripts, fonts, UI images, QS strings, song audio, Bink), not for
scenes / anims / skins / .wav. M2: charts, tuning and the three song banks
in; the judging rules are still GH3's [I]. M3: gh4's UI state machine and
descs run the original's own boot to the main menu. M4 in part: Quickplay
from the main menu to a song, YOU ROCK!, the results, cash rewards and back
to the set list, but on the labelled debug highway and with no venue drawn.

**What boots (verified, fresh `engine/build/devv/gd10`, headless):**
- `--walktest boot`: passed (legal, the four boot movies skipped with A,
  press start, sign-in, memcard, drum tip pop-up, main menu); black frames 0.
- `--walktest quickplay`: passed (main menu > QUICKPLAY > game mode >
  guitar > set list (+ first-time pop-ups) > Albatross > Start > Expert >
  song in z_Forum on autoplay > YOU ROCK! > song breakdown > cash rewards >
  autosave > set list).
- `--scripttest`: "every script tokenizes; boot ran (71 natives to write)".
- `pak.py` / `qb.py` / `qbscript.py check`: 0 problems over the whole disc.
- `--selftest`: 688 charts autoplayed; 66 failures (4 Death Magnetic DLC
  songs have no audio on the disc, bass open notes in the tapping check, 2
  near misses, 5 virtual-controller checks inherited from gd04).
- `--songaudiotest entersandman`: the three banks decode (9 stereo pairs).
- Build: 0 warnings (Linux GCC, dev builds).

**Screenshots checked** (agents/scratch/, git-ignored): press start
(correct), main menu (correct layout and art), drum tip and double-bass
pop-ups (correct), difficulty pop-up (text clipped), set list (song names
empty, tabs overlap), career / difficulty menus (skull art over the menu),
the song (debug highway), YOU ROCK!, song breakdown (score 000,000, text
clipped).

**Built only / not tested:** MinGW, Android and Steam Deck builds, the
launcher module, Wine, real controllers, audio output (dummy driver).

**Approximations (all in DECISIONS):** D4 state call order, D6 sprite dims,
D8 stem routing, D9 note flags, D10 Glitches stand-in, D11 switch
fallthrough, D12 IsSongAvailable, D13 debug highway.

**Next (in order):** gh4 scenes (venues, the 3D gems of Gem_Set_01, the
highway skins), the score into the results (gh4 reads player info, not
GH3's playerN_status fields), the set list's song names and the text
fitting (fit_width / fit_height), the remaining 71 natives (--scripttest),
the judging rules from Metallica's XEX (hit window, HOPO, Star Power,
whammy), streamall's XMA (sound effects), song previews' key, then M5+.

Estimate (kickoff 2): 4-6 h for the proof of concept (M0..M4 on guitar);
stop at about 6 h whatever the milestone.

## 2026-11-10 23:40 M0 set-up

- gd04's engine, tools and build scripts copied, renamed gd10 (D1). Version
  0.0.0. Built: `ninja -C gd-game-10/engine/build/dev`: 0 warnings (one
  GCC 16 false positive in `port_gamepads.cpp` silenced by masking the index).
- Rip: `xdvdfs.py extract` -> "extracted 1470 files, 7.22 GB".
- Ghidra: import + `FixXexFunctions` + `ExportAnalysisXex` -> "Exported 30337
  functions, 30337 decompiled, 94894 data refs, 9246 strings".
- Verified: `pak.py check gd-game-10/extracted` -> "paks: 1060, files:
  33406, with problems: 0" (after the CHNK reader and the padded-pak rule).
- Verified: `qb.py check gd-game-10/extracted` -> "qb files: 1544, sections:
  38466, scripts: 8853, with problems: 0" (after the gh4 struct items and
  type 0x1C).
- Verified: headless run on the ISO boots, loads the disc and logs what
  fails: "Guitar Hero: Metallica disc: 0 songs", fonts "glyph table
  offset", "streamall.fsb.xen: not an FSB3 bank", "guitar_startup did not
  start". `--formattest`: 18,202 failures by reason (formats.md "Not
  decoded yet").

## 2026-11-11 02:25 M1-M3 (boot) and M4 (Quickplay song) reached in part

Time so far: about 2 h 45 min of agent time (23:39 -> 02:25).

Verified (fresh binary `engine/build/devv/gd10`, with FFmpeg from gd04's
local prefix copied under engine/build/ffmpeg/linux):
- `--walktest boot` -> "walktest boot: passed (UIstate_boot_movie_atvi >
  ... > uistate_boot_iis > uistate_optimal_drum > main_menu_fs)", "black
  frames: 0".
- `--walktest qp4` (Quickplay, being built) reaches the main menu, game
  mode, instrument, set list, picks a song (Albatross), difficulty Expert,
  the song starts in z_Forum ("song Albatross (Albatross, Expert): 951
  notes"), autoplays (streak 54, x4 at 70 s) and the original's YOU ROCK!
  shows; the walk then waits on gameplay (results not reached).
- `--selftest`: charts parse and autoplay (688 charts, 497,010 notes); 66
  failures: 4 DLC songs without audio on the disc (expected), bass open
  notes in the tapping check, 2 near misses, 5 virtual-controller input
  checks (gd04 GH2/GH3 types, not looked at).
- `--songaudiotest entersandman` decodes the three banks (9 stereo pairs).
- `pak.py check` / `qb.py check` / `qbscript.py check`: 0 problems.
- Screenshots looked at (git-ignored, agents/scratch/): press start,
  main menu, drum tip pop-up, double-bass pop-up, difficulty pop-up, set
  list (partial), the song (debug highway), YOU ROCK!.

Not done / approximate: the highway (debug highway, D13), the venue is not
drawn (scenes: "material name area not zero"), menu layouts off in places
(skull art over the career / difficulty menus, set list song names empty),
results screen, sound effects partly (XMA1 errors on streamall).

## 2026-11-11 09:10 Sound (GD10-030) and the launcher (GD10-031)

Causes of "no sound": (1) gh4's FSB4 samples (streamall, song banks) are
XMA2 packets (6-bit frame count), decoded as XMA1 -> FFmpeg "overflow in
spectral RLE" / "would have to skip N bits"; (2) gh4's .wav (global_sfx,
1,017) have no "fmt " chunk but a big-endian "XMA2" chunk after the data
(docs/formats.md) -> every menu sound effect failed; (3) the boot movies
are texture-slot movies (PlayMovieAndWait noblack), played without sound;
(4) gh4 sound-event containers keep `sounds` as an array (vol dB, pitch
semitones) and the rules in the container, and some events are scripts
(Menu_Music_SE, Menu_Guitar_Lick_SFX: the menu music never started); (5)
PlaySound's gh4 units (vol dB / vol_percent, pitch / pitch_percent,
num_loops). The busses were not at 0.

- Verified: `--sfxtest` -> "sound paks: 1017 .wav keys, 0 failed; banks: 1,
  samples decoded: 2629, 3.6 hours of audio; every sound decodes" (fails on
  a silent sample now).
- Verified: `GD10_AUDIODUMP` + `--walktest quickplay` (dummy driver): sfx
  device peaks 0.35 (licks, menu music Menu_Music_NoLeaf at -12.5 dB buss),
  0.07 menus with music, 0.6-0.9 in the song (crowd); song device silent in
  the menus, 0.55-0.69 during Albatross; no XMA errors in the log; 225 sounds
  played, 0 lost. App run 40 s: boot movies' sound peaks 0.10-0.57.
- Verified: `--songaudiotest entersandman` -> decoded (9 pairs as D8).
- Launcher: build_module_10 (gd04's recipe), module_systems_10 every system,
  verify_bundle checks gd10's FFmpeg; gd10 ready_to_play, completeness 0.1,
  version unchanged. `build.sh all`: Linux, AppImage, Steam Deck, Windows,
  Android each "complete (6 game module(s))". `--play gd10 --exit-after`
  (GDUL_GAME_QUIT_AFTER=25000): Linux package exit 0 after 25.1 s (movie
  sound dumped non-silent); Windows package under Proton wine, disc in
  "Jogos Música": exit 0 after 25.1 s. Not tested: a Deck, an Android device,
  macOS (not built).


## 2026-11-11 08:00 -> 10:00 In-song visuals and judging (GD10-010)

About 2 h of agent time. A Quickplay song is now drawn and played as gh4
draws it: the venue, gh4's highway (skin, gems, now-bar buttons, tails),
Star Power, the HUD (GH3's widgets with gh4's images).

Causes of what was broken:
- **No venue**: gh4 scenes changed three things (0xD0-byte materials,
  0x90-byte mesh records, triangle lists instead of strips) and use 8 /
  1_5_5_5 / 5_6_5 textures; a pak whose one scene failed lost all its
  textures (global.pak: -1). docs/formats.md "Scenes", "Textures".
- **Debug highway (D13)**: gh4's gems, buttons and tails are not sprites
  but script-built materials (guitar_material.qb: grey textures of
  z_in_game's basic_gems.tex tinted by the gem set's colours), the buttons
  from button_up_models, the skin from the character's CAS highway; z_in_game
  (linked by every venue) was never loaded. D15, D16.
- **Judging**: the chart's 0x40 is "this note is a hammer-on" (the port
  XORed it with its own natural rule, flipping nearly every HOPO); an open
  note left no last-hit value, so a fretted HOPO after it could not be
  tapped; two entries at one time are one chord (Master of Puppets hard).
  D9 [C 0x8214EE70].
- **Colours**: material words map 1:1 to GH3's (blend, alpha cut-off,
  flags) [C data]; vertex colours span 0..255 (no 2x overbright); the
  original shaders (MATL v3, now read) draw black with GH3's constants
  (their lights moved to c176 / c192 / c248..), so the approximation draws.

Judging rules from the XEX [C]: GuitarInputLogicInit 0x82141338 reads the
same globals as GH3 (hammer_time_early / late 0.1, strum_before_forming,
form_before_strumming, split_late_percent, star_power_sequence_bonus,
ignore_strums_after_hammers, disallow_tolerance_strums); check_time_early
0.116 / late 0.1 (guitar_tweaks.qb, per player through playerinfo);
fretbar_update_hammer_on_tolerance_CFunc 0x8218D6B0 = GH3's formula;
ProcessHammerOns 0x8214EE70 (jam songs) = the chart's HOPO rule. Still
GH3's [I]: the multiplier steps, Star Power fill / drain, whammy.

Verified (engine/build/gfx/gd10, headless, scratch XDG):
- `--formattest`: .scn 46 / 46, .tex 2064 / 2069, .mdl 11 / 14 decode.
- `--venuetest` (z_forum, entersandman): 394 meshes, 147 textures, 0 missing,
  60 / 240 fps identical, shots looked at (arena, crowd, stage lit).
- `--selftest`: 31 failures = 10 Death Magnetic DLC songs without audio
  (x3) + jamsession; every disc chart autoplays (strum and tap passes).
- `--song entersandman --autoplay` shots at 10 / 25 / 50 / 79 / 82 / 90 s
  looked at: venue, highway skin, gems, stars, tails, Star Power (cyan
  gems, lightning, x8), multiplier, rock meter.
- `GD10_KEYBOT=1 --song entersandman` (real key events through the
  keyboard bindings, Down strums): 98 of the first 100 notes hit.
- `--walktest quickplay` passes; `GD10_FRAMELOG=1`: no frame over 25 ms
  in the song; under play_song one 335 ms frame ("menu scene: z_tushino",
  the menu scene's synchronous zone load) and one 92 ms (the venue's
  upload on the main thread).

Not done: the band, crowd and stage characters (gh4 .ske / skinned .skin /
.ska not decoded: "ske: bone count"); gh4's own HUD (desc hud_1g +
AttachHudWidget natives, see OPEN_QUESTIONS 4); the original shaders'
light constants; drums (0x2000, Expert+ double kick, OPEN_QUESTIONS 5);
the stage floor near the camera draws white under the approximation in
z_Meadowlands; gamepads / guitars verified only by the input selftest.

## 2026-11-11 10:10 Front end (menus agent, GD10-020)

Time: about 2 h 15 min of agent time (07:55 -> 10:10), own build dir
`engine/build/ui`. Audit: `docs/frontend.md`.

Fixed (cause in brackets):
- Text clipped ("E / H / EXPER", "HIGHES / NO"): gh4 fonts store bearing,
  tracking and glyph offsets as f32 at other offsets [C 0x824D11B0, 0x82454C78].
- Menus in the wrong font: ui_shared.pak's fonts were never loaded.
- Skull art over the career / difficulty menus, black screens: a
  descinterface's own props were applied to the desc's root (D14 rewritten:
  the root is the element's child 0, as setup_band_hud needs).
- Pop-ups showing OVERWRITTEN / MESSAGE: setprops did not reach desc props.
- Highlights at the top-left corner: `summed` / `{absolute}` positions.
- Help bar: menus' fit_major / fit_minor, pos_anchor'ed children, se_getprops
  of desc props, meta_button_map by name, desc z on its parent's.
- Set list empty / one row: gh4 `scrollingmenu`.
- Results score 000,000: player statuses lacked is_local_client, so
  Progression_UpdateDetailedStatsForGig kept nothing.
- Music Studio fell to the boot drum tip: menustack_getstackcontents without
  stack_size; isRBDrum.
- Extras / Cheats / Band never opened: included structs in script params
  only flattened one level.
- No pause menu: Start now goes through handle_pause_event.
- Xbox LIVE greyed (no online; the sign-in had no way back).

Verified (build/ui/gd10, headless, scratch XDG_DATA_HOME):
- `--walktest menus` -> "walktest menus: passed"
- `--walktest quickplay` -> "walktest quickplay: passed"
- `--walktest qp_pause` -> "walktest qp_pause: passed" ("song: resumed (UnPauseGame)")
- `--walktest fuzz` (GD10_SEED 7, 99) -> "walk: fuzz: 300 presses, the menus kept the focus", passed
- `--scripttest` -> "every script tokenizes; boot ran (71 natives to write)"
- Screenshots looked at for every screen in the audit.

Not done: sub-screens below the first level (Extras, credits, calibration,
audio, save/load, jam), career flow, text fit rules beyond the descs'
values ([I]), cash rewards' Rock Rank title, sound cues per screen (logged
only).

## 2026-11-11 Original shaders on by default (rendering agent, GD10-010)

About 4 h of agent time, own build dir `engine/build/gfx2`. Doc:
`docs/render.md`; decisions D17..D19.

Causes and fixes:
- **Black venue with the original shaders**: gh4 moved the engine constants
  [C data: the constant tables of all 4,981 shader records]: ambient c160,
  key light c161..c163, specular slots c164..c169, SH c172..c175, new vertex
  point / spot lights c176 / c192 with attenuations c248..c253, filter
  passes c64..c99, g_TextureProjection0 c124. The port wrote GH3's c176..:
  the pixel shaders read zeros. Registers remapped (xenos::objectSlot,
  Globals c64..c255); the light set fills gh4's slots (D17 [I]); the
  library is on by default (GD10_GH4_SHADERS=0 turns it off).
- **Grey haze / white blobs** (with the approximation too): gh4 particle
  quick materials carry `lighting`, `softedge` and `max`; the port drew
  them unlit at full white with no cap. Lit, soft-edged, capped (D19 [I]).
- **Screen FX**: gh4's profiles hold bloom2 / bright_sat / dof3 / noise2 /
  uberscreenfx; only bright_sat was read and the passes' constants sat at
  GH3's registers. bloom2 and dof3 now mapped on GH3's passes (D18 [I]);
  noise2 / uberscreenfx not drawn.
- **Meadowlands white floor**: gone with the original shaders (the floor is
  lit by the stage lights).
- Programs (receiver variants, FX passes) are compiled with the venue.

Verified (engine/build/gfx2/gd10, headless, scratch XDG):
- `--venuetest` z_forum (entersandman): 394 meshes with the original
  shaders, 0 approximated, 60/240 fps identical; z_meadowlands, z_tushino,
  z_justice, z_donnington shots looked at: lit, coloured, not black.
- `--song entersandman --autoplay` shots at 10 / 25 / 50 / 61 / 79 / 85 /
  90 s looked at, Star Power (cyan gems, lightning, x8) included; no frame
  over 25 ms in the song (one at song time 0 in the direct `--song` mode).
- Backends: local, Vulkan (RADV) and OpenGL3 (gd-engine-core) draw the same
  frame; SDL3 GPU cannot present offscreen and fell back to local.
- Gate: walks boot, quickplay, menus, qp_pause, fuzz passed; `--selftest`
  31 failures (the expected DLC x3 + jamsession); `--scripttest`,
  `--sfxtest` pass.

Not done: the 90 s shot of Enter Sandman in Meadowlands is still veiled by
a ring of scripted spark glows near the camera; the XEX's light set and
bloom2 / dof3 / noise2 passes (VMX128 code); loading-screen frames of
250-500 ms under play_song (the menu scene's synchronous load and the
venue's shader compiles; not in the song); the highway / gems / HUD on the
library's own templates (D15).

## 2026-11-11 12:00 Front end alignment (layout agent, GD10-020)

Time: about 1 h 15 min of agent time (10:45 -> 12:00), own build dir
`engine/build/ui2`. Audit: `docs/frontend.md` (now 17 match, 2 differ, 2 not
reached).

Misaligned, and why:
- Set list titles ran 30 px past their 630 and into the skulls: the text
  measure left out each glyph's advance adjust and x offset, and took
  font_spacing -1 as "given" [C 0x824D11B0].
- Set list skulls / double-kick icons stuck at the row's edge: the scripts
  place them from the title's line child (a text block's lines are its
  children) and the title's `summed` position, which is (0, 0) for an
  element not laid out yet [C 0x823F5AA8 / 0x82375E08; I].
- Focused song had no info panel: `se_setprops desc = ...` was ignored.
- Hard dark band over the set list's bottom, mirrored halves of the inner
  shadow and highlights not mirrored: a desc's `flip_h = true` /
  `flip_v = true` were ignored (the band is a subtract gradient, flipped).
- Black pill without circles at the set list's corner: CreateScreenElement
  did not merge an included struct (type, parent, just).
- Results player row and set list stars empty: descinterfaces inside a
  desc were never built; cash reward rows shifted right and clipped:
  `autosizedims` (rect size, rect corner on the element's top-left) [I].
- Cheats: the bone stayed over the list: a prop name on two elements set
  only the first; its highlight sat on another row (line children).
- Results' highest multiplier 1x: highest_multiplier / max_notes were never
  synced into the player status.
- Pause spacing is the original's (selection_height 50, spacing -15): kept.

Verified (build/ui2/gd10, headless, scratch XDG_DATA_HOME):
- walks: boot, quickplay, menus, qp_pause passed; fuzz seeds 7 and 99
  passed ("300 presses, the menus kept the focus"; the fuzz no longer
  starts a jam session, which has no Back, like a song).
- `--selftest`: 31 FAILED (10 Death Magnetic DLC songs x3 + jamsession).
- `--scripttest`: "every script tokenizes; boot ran (71 natives to write)";
  `--sfxtest`: "every sound decodes".
- Shots looked at: 1280x720 (all walk screens), 1920x1080, 1024x768,
  2560x1080 and 3440x1440 (set list, results, cash rewards, cheats, main
  menu): the same placement, 16:9 letterboxed / pillarboxed.

Not done: results name placeholder (no gamertag), notes N/A / streak 0
(detailed stats), black sides behind the results (venue); "Rock Rank"
wraps and shrinks as measured (unconfirmed); credits, Extras sub-screens,
calibration, audio, save/load, jam, career flow not audited.

## 2026-11-11 13:40 Fuzz walk: menus that lost the focus (fuzz agent, GD10-020)

Time: about 1 h 35 min of agent time (12:05 -> 13:40). Built from a clean
export of HEAD plus these changes (the shared tree had another agent's
render work in progress).

Causes found (the fuzz walk, `GD10_SEED`), each fixed as the original:
- Tutorials (game mode > TRAINING) unfocused: SetButtonEventMappings and
  SetSpawnInstanceLimits were no-ops, so a second A during TRAINING's
  loading chose it again; select_tutorial was pushed twice and the second
  choose unfocused its list. Now block_menu_input / unblock_menu_input stop
  the pads' menu events and the spawn limits hold (D20).
- Music Studio > GHTUNES > Sign In: an empty screen for good, waiting for
  the LIVE blade to close (no blade in the port): it now closes at once and
  the warning comes back (D21).
- Practice's section list: Back never went back (GetContentFolderState
  unhandled; D22).
- Extras > lyrics: Back did nothing: IsObjectScript was unhandled (the
  preview monitor never started) and BroadcastEvent did not end a Block, so
  ui_song_extras_load never saw song_started.
- HEAD's 7c6c66d (zone QB files read on a worker) also took the screens'
  ui_paks off the frame: ui_create_<state> ran before its desc was in, so
  Settings, Backstage and Head to Head came up empty and unfocused (menus
  walk failed on pure HEAD). A screen's ui_paks are read at once again
  (ui/natives_game.cpp setCurrent); zones stay on the worker.
- Walk only (the original has no Back there): cheat entry leaves with
  Start; the LIVE warning and set list quit warning with their second item;
  the set and double bass pop-ups with A. The fuzz no longer starts a
  practice section or makes a character (CAS builds are not in the port:
  cas_queue never empties and the CAS screens' block_menu_input waits on
  it; open in GD10-020).

Verified (clean build, headless, scratch XDG_DATA_HOME each):
- fuzz: default seed and the odd seeds 1..79 (41 walks; n and n|1 are the
  same walk): 41 passed. Before: the default seed failed now and then at
  uistate_select_tutorial, and 20 of seeds 1..40 failed (tutorials,
  sign-in, CAS save dialog, lyrics, bonus videos, cheats, practice).
- `gd-game-10/tools/fuzz_seeds.sh <gd10> <iso>` runs the regression seeds
  (default 7 99 1 3 5 11 17 23 27 31 39 73), all in the 41 above.
- walks boot, quickplay, menus, qp_pause: passed.
- `--selftest`: 31 FAILED (10 Death Magnetic DLC songs x3 + jamsession);
  `--scripttest`: "every script tokenizes; boot ran (72 natives to
  write)" (SetButtonEventMappings is now a flow native, not core);
  `--sfxtest`: "every sound decodes".
- Re-run on 22cdcbd (the stutter agent's commits) plus these changes
  (engine/build/fz): walks boot, quickplay, menus, qp_pause passed;
  selftest 31 FAILED (as expected), scripttest, sfxtest pass; fuzz 41 seeds:
  40 passed, seed 17 ended on the band "two controllers" alert (its Back is
  nullscript, the original's): the walk now leaves it with A; the 13
  regression seeds then all passed.

## 2026-11-11 14:30 No stutter on loads (render agent, GD10-032)

Measured with the new `GD10_HITCHLOG=<ms>` (frames over it, with the
scopes that took them) on the quickplay walk, local backend, NVIDIA.

Before (idle machine; under the other agent's tests the same loads took
2-10x): boot_iis 163 ms (zone textures uploaded at once, 1.3 s loaded),
main menu 94 ms (menu scene view: textures, 30 program compiles, mesh,
screen FX), play_song 411 ms (the menu scene's synchronous z_tushino) +
260 ms (SetPakManCurrent's QB read) + 180 ms (the venue's upload) + 214 ms
(particles' textures), back on the set list 77 ms (z_soundcheck again).

Fixed: VenueView prepares on a worker and uploads / compiles / builds a
slice a frame; particles likewise; the menu scene loads its zone and view on
workers; TextureCache::poll in slices; zone QBs read on a worker while the
script waits; programs shared per device (compiled once a run), begun in
the driver's threads on local OpenGL (KHR_parallel_shader_compile), made a
slice a frame on the core backends; one 8 ms load budget a frame
(`hitch::slice`). docs/render.md "Loading without a stutter".

After (gate run, 7 tests at once): the load window (song request to 5 s
in) worst 14.3 ms of work; no load frame over 33 ms on the path; left over
33: boot_iis 47 ms and set list 41 ms of ui work (script + layout at the
screen's start, not a load), the song's first frame ~40 ms of ui
(destroy_loading_screen and the HUD's elements) under load.

Check: every walk fails when a frame from a song's request to 5 s into it
takes more than `GD10_HITCH_BUDGET` ms of work (default 50; present not
counted). Not done: cold driver caches still cost one program per frame
(core OpenGL 3: up to ~100 ms each, gd10 cannot make it asynchronous);
Vulkan pipelines are made by the core at first draw (one 66 ms frame on a
cold RADV cache).

## 2026-11-11 15:50 Owner's play, wave 1 (lead agent: GD10-033..038)

Load times (GD10-034). Every run now logs `load: <phase> at <wall> ms
(cpu <ms>)` for the start (SDL, window, disc and scripts, resident
textures, front end, first frame), each flow state, and `load: song request
to the song's start` (devaids/load_times.h). Measured (offscreen, NVIDIA,
the machine shared with two other agents' builds; idle runs):
- Start to the first frame: 1.47 s -> 1.25 s (global_sfx.pak, 0.44 s of
  inflate, now read on a worker while the scripts load). On a busy machine
  the same took 6-9 s of wall time for 2.4 s of CPU (disc reads waiting).
- A song's request (its loading screen) to its start: at least 2 s (the
  port's 2 s hold, GD10-040) -> 0.8-1.4 s (D23: gh4 has no minimum).
- Boot to the main menu is the original's own waits: the legal screen 6 s
  (ui_boot_legal_wait), the movies (skippable), the save check's 3 s + 1 s
  (memcard_wait_for_timer, OPEN_QUESTIONS 6), the download scan's 1 s, the
  main menu intro. The way back to the set list: the autosave's 3 s + 1 s.
- Not found: no load done twice on the path (Pak::shared caches; zones,
  venue, particles already on workers since GD10-032).

PC wording (GD10-035, D24): every localized string (qs.pak, 10,847) is
rewritten as it loads; `--scripttest` fails on any naming the console.
Seen on the Save / Load screens ("Please don't turn off the system.").

Universal menus (GD10-037): the Enhanced menu and GAMEPADS were already
there from gd04's copy (Settings' ENHANCED pill, the main menu's GAMEPADS
pill); their cheats were GH3's: now gh4's guitar_hero_cheats (D25). New
`--walktest enhanced`: fresh save defaults, rows change values, export /
load settings, two cheats; `--walktest gamepads` passes;
enhanced_backends now reaches SETTINGS by keys. The mouse walk is GH3's
(main_menu_options_text does not exist in gh4) and fails as before.

Menu overlaps (GD10-038): the menus walk now shoots the main menu after
each of the seven sub-menus: all eight shots are the same (no image left
over). Not reproduced at the first level.

Fixed on the way: `--song <id>` spun for ever in VenueStage::attach
(hitch::slice refused every piece after the frame's first; hitch::AllAtOnce).

Characters (GD10-033, characters agent, D30..D33): gh4 .ske (225 of 251:
24 LZSS ragdoll skeletons, 2 mirror-name), .skin (2,166) and .ska (8,255)
decoded (docs/formats.md); the band drawn and animated in songs (mocap clips
from <song>_performance, loops, strums, frets, the drummer's kit),
instruments from song_specific_instrument_data; band and crowd attached a
member / ped a frame. CAS: GetLogicFrame, CASBlockForLoading and the other
queue natives added (the queue should empty), no CAS screen walked and
build_band_member_from_appearance unwritten: no CAS character in the menus.
Not done: face / body deform, CAS texture compositing, ragdolls, the mocap
cameras, Band_WalkToNode, the crowd checked only in passing.

Screen effects (GD10-036, screen FX agent, D40..D42): gh4's uber pass
(bloom2, dof3 near and far, noise2, bright_sat after the bloom, vignette,
inkblotch) and the camera notes' profiles (One: Default, NoisyDefault,
BW_Grainy, BW_Grungy). Before: GH3's chain, no grain, vignette or ink.
CPU 0.11 ms a frame. Not done: godray (Metal_HotBlue only), camera notes
101..108 (blur, flashes, fades), the CPU constants (VMX128), bloom2's
equation.

Checked by the lead (entersandman 30 s / 60 s, the characters agent's
shots): the band is drawn (Rob's close-up with his bass, the drummer's kit),
but many camera shots look at the stage floor or the members' feet and the
venue reads very dark: the gh4 camera targets (mocap cameras, the shots'
look-at nodes) are the next thing to check (GD10-033).

Gate (16:11 tree, engine/build/lead, headless, scratch XDG): walks boot,
quickplay (load window worst 26.5 ms, 0 over 50), menus, qp_pause,
enhanced, gamepads passed; `--scripttest`, `--sfxtest`, `--venuetest`
(entersandman, 10 venues) pass; `--selftest` 31 FAILED (as expected);
`tools/fuzz_seeds.sh`: 13 / 13 passed.

## 2026-11-11 20:00 Owner's wave 2: camera aim, venue lighting, characters, crowd and stage (lead + 4 agents, GD10-039..042)

Camera aim (GD10-039, lead, D43..D47). Cause of the shots at the stage
floor / the members' feet: gh4's mocap and moment cameras (`lockto =
<camera lock target> locktobone = bone_camera`, pos / quat 0) had no lock
target, so they sat at the world origin (in Enter Sandman every other cut:
cameras_moments, drummer_mocap01). Now the camera lock targets play their
camera anims on the GH4_Camera skeleton: Band_PlayClip's clip `cameras`
(moment_cam_lock_target_N at <zone>_<node>) and `drummercameras`,
start_drummer_cameras (Drum_Andy_StandardLP_C01 / C02), the members' loop
cameras (<loop>_c01 / _c02 on <member>_mocap_lock_target_01 / 02), the
intro / encore transitions' SpecialCamera_PlayAnim (z_forum's
ven_forum_camera_intro). A lock target playing nothing at a cut is not
picked [I]. Also: CrowdOff on a camera entry hides the crowd for the shot
(StructureContains: entry level only [C 0x822B4EA0]; close-ups were shot
through the crowd cards), distfromlookat keeps a close-up's distance to the
bone (mocap moves the members), CameraCuts_SpecialStepMove (audience
shots). Conventions checked against the data: screenoffset x right / y
down, FOV horizontal. Looked at: z_forum (intro, moments, Kirk / Rob / Lars
close-ups, guitarist mocap, audience, long shots, Star Power), z_meadowlands,
z_tushino, z_justice, z_donnington at -8, 14.5, 30, 45, 60, 75 s: every
shot frames what its data frames; no floor / feet shots left. Not done: FOV
pulses, hand-cam amplitudes, per-shot shadow casters (render.md "Open").

Venue lighting (GD10-040, lighting agents, D50..D52):
The light set is Function_824D7220 [C: the ranking's 0.299 / 0.587 / 0.114
and 50000 live at 0x82031428..34, read only there; its VMX128 read with
tools/vmx128.py]. What changed:
- Light slots (D50): a point or spot light takes a vertex shader slot
  (g_PointLight / g_SpotLight) only when its node has `vertexlight` (record
  flag 0x10). A point light without it is lit as GH3's: into the SH (x
  attenuation) and a directional slot (it can be the key light); a spot
  light without it goes into the SH only. `noSH` (flag 0x20) keeps a light
  out of the SH. Before: every point / spot light took a vertex slot.
- Light groups (D51): an object has `lightgroup`, then `lightgroup2`,
  `lightgroup3`, ... (823C9FD8 formats "lightgroup%d"); a light serves it
  through any of them. Before: lightgroup only (crowd billboards, z_justice's
  statue, z_mop's seats / stage, z_stone's drum riser missed lights).
- Checked, no change: candidates ranked by luminance x intensity x the
  group's intensity x the master intensity (SetLightMasterIntensity,
  0x82848338, 1.0 and never set by a script) x attenuation, x 50000 for
  `shadow` lights; directional lights: the first four take the slots, the
  rest into the SH; ambient into c160 and SH0; prt coeff0..3 x intensity
  into the SH only for objects with the SH bit; spot cones from hotspot /
  field as full angles (8240AF18: angle x pi / 360) [all C].
- Display gamma (D52): gh4's ramp (824558E8) is i x 256 x (0.6 x 0.5 +
  0.7) = identity, clamped to 65280; the per-channel 1.8 values
  (0x82849290) and 1.3 (0x82849284) are only cached, never applied [C].
- No texture on z_forum carries a fetch-constant exp_adjust [C data]: no
  hidden 2x on light maps.

Looked at (venuetest shots, entersandman, offscreen): z_forum, z_meadowlands,
z_tushino, z_justice, z_donnington; in-song z_forum 20..95 s and
z_meadowlands 88..92 s. Close shots of the band and stage got brighter
(z_forum mean luminance per shot e.g. 0.154 -> 0.200, 0.081 -> 0.125);
wide shots stay much darker than the venue preview movies (e.g. z_forum's
arena wide shot 0.012; the movies 0.29..0.43 on z_forum, 0.08..0.21 on
z_meadowlands / z_tushino). Enter Sandman's camera notes pick
Metal_BlueTint (note 114 CoolBlue): the blue cast is the original's
profile, its contrast 1.106 about 0.5 crushes the dark frame further.

Not found yet (GD10-040 stays open): why the scene is that dark. Ruled
out: the gamma ramp, texture exp_adjust, the light set's slot rules, a
master / group intensity. Left to check: the light boxes for ambient
lights (Z_Forum_LFX_L_Forum_Room01: intensity 1.5 but attenend 12.25, so
GH3's box rule gives it to almost nothing), SUB_824d7180's SH weights (no
4/17 or 8/17 constant in the XEX), the light record's box (8240F560 calls
822FAC18), the light volumes / smoke the movies show.

Meadowlands 90 s spark ring: not seen with the current cameras (the 90 s
shot is now James's close-up); the ring systems are at y = 11.06 m in the
rig, 0.2..4 m add glows.

Second pass on the darkness (no code): render.md "Venue brightness": every
term multiplies at its data value; z_forum's arena 0.013 vs 0.28..0.35 in
venue_forum.bik; the cause is still open (leads: the 3D pass's render target
/ resolve, a CPU constant scaling lightmaps / vertex colours, gamma).

Character details (GD10-041, characters agent, D60..D66): the talents now
stand on their own skeletons (global.pak keeps `<skeleton>_original` as the
entry's full name, `<skeleton>` as its short name; the band looked them up by
the short name only, so every talent fell back to GH_Rocker_Male_original:
James' and Rob's hands stretched into loops, the faces torn open by the
facial clips: fangs, the goatee's inner shell under the chin, the zombies'
stitches flying). Faces: facial_anim_loop reads the anim struct member's
facial_anims (the song's `<anim_struct>_<song>[_alt]`, which lives in the song
pak: Kirk, Lars, James, Rob get facial_anims_male_<name>), Band_ChangeFacialAnims
(fa_type, ff / mf anims), Band_PlayFacialAnim (the singer's lip sync
GHM_Singer_Male_<Song>_N). Band_PlayClip clip structs are read from the song
pak too (before: no mocap clip ever played in a song). The drummer loops
initial_drummer_anim (Drum_Lars_StandardLP_NoTempo; use_drummer_events 0)
instead of retrying a stance loop each frame. Missing textures (Kirk's eyes,
skin detail, environment maps: 368 over the talents) come from
PAK/global_model_tex. The accessory ragdolls (hair, braids, dreads) run from
PAK/global_rag_skel's .rag (LZSS + Havok 5.5.0, no reflection: read at fixed
offsets). Face_deform / GH_Male_Deform parse (one wrong mirror index).
--chartest now loads every locked talent on each allowed part (24), shoots
face close-ups (faces_NN.bmp) and checks the face bones move; default song
entersandman (slowride is not on the disc).

Looked at: chartest face close-ups of James (3 outfits, vocals / guitar,
idle face and lip sync), Kirk (3), Rob (3, bass / guitar), Lars (3), Lemmy,
King Diamond; entersandman z_forum in-song shots 60..96 s. Before: stretched
hands (James, Rob, zombies), mouths torn with fang teeth and a dark shell
under James' chin, Kirk's eyes white, no idle facial anims, no mocap. After:
faces as authored (closed / talking mouths, teeth, goatee), Kirk's brown eyes,
hands normal, hair and braids swing.

Not done: CAS characters (face / body deform records of the .skin trailer,
Face_deform / GH_Male_Deform sliders: talents have no deform records),
facialhacksource's exact meaning (clips applied as rotations absolute,
translations offsets), hkUFloat8 max velocities (default 200), eye blinks
are part of the facial clips (no separate blink driver found in scripts).

Crowd and pyro (GD10-042, crowd agent, D70..D73):
- The crowd cards are quad lists: gh4 mesh +0x84 is the primitive type, 4
  (triangle list) or 13 (D3DPT_QUADLIST); the 15 crowd card meshes of the
  zones are 13, all a multiple of 4 indices (most not of 3). Read as
  triangle lists they drew as shards across the crowd areas (z_tushino /
  z_justice's "sticks", z_forum's black floor pieces). Now (a, b, c),
  (a, c, d) per quad.
- The crowd's peds render into one atlas (crowd_base_viewport ->
  crowd_atlas0.dds, 1024 x 1024 for six peds or fewer on Xenon), each ped in
  its cell (HRViewportParams / ViewportParams rect, 4 x 2 cells of
  256 x 512); every crowd model shares the textureasset, the card quads' uv0
  pick a cell. Before: one 256 x 512 target per ped and only crowd1's
  shown, stretched over the whole atlas (z_forum's big orange hands).
- The peds face 180 + roty (obj_setorientation y), hfov crowd_ped_camera_fov
  (21); before roty alone and 20.
- Every zone has <zone>_crowd_models (guitar_crowd_data.qb: z_meadowlands,
  z_Stone, z_Tushino, z_Justice, z_MoP, z_Donnington, Z_Forum; not
  z_icecave), not only z_meadowlands.
- Pyro: gh4's LightShow_NoteMapping notes 37 (generic), 35..20
  (front / top / mid / back _1.._4) run the mapping's pyro_scripts.<type>
  (Enter Sandman on z_meadowlands: 19). Before: none ran (GH3's note 56 /
  pyro_script only).
- SafeCreate / SafeKill work: IsInNodeArray / IsCreated read the node name
  as the first unnamed item (Vm::positional skipped it as a flag key), so
  every SafeCreate was a no-op.
- A particleobject with a lifetime (z_meadowlands' Topsparks /
  Topsparks_Flashes, 0.5 s) emits that long per Create and is gone after;
  created at start, its emission is over under the loading screen. Before:
  the 34 nodes emitted for ever (the ring of 17 4 m glows at y = 11.06 m
  veiling shots); now they fire with Pyro_Moments_Top_1 (Enter Sandman
  27.4 s, 42.1 s, ...), 0.1 s apart round the ring.

Checked (entersandman, shots s-4000 / s14500 / s35500, all eight venues;
z_forum s-4000 and AUDIENCE_R 35.5 s at 1280 x 720; z_tushino s-4000 with
the atlas filled red to see the card shapes; z_meadowlands 27.5 / 28 s).
Not done: the crowd's lighting (the peds read very dark: lightgroup crowd),
update_crowd_model_cam's turn sign and apply_correction (AddCrowdModelCam,
a native), Cheat_SnobCrowd, the ped camera's pitch (SetViewportCameraOrient
takes the venue camera's full at / up), the base viewport's mipmaps.

Gate (HEAD 0f3dbcd, engine/build/m10, headless, scratch XDG): walks boot,
quickplay (load window worst 26.1 ms, 0 over 50), menus, qp_pause, enhanced,
gamepads passed; `--selftest` 31 FAILED (as expected); `--scripttest`,
`--sfxtest`, `--venuetest` (entersandman, 10 venues) pass;
`tools/fuzz_seeds.sh` 13 / 13 passed.

## 2026-11-12 09:30 Owner's menus: help bar glyph, song previews, difficulty focus (GD10-050..052)

- Help bar: every pill showed A + the green fret ("\b3\b4") because
  RemoveParameter was a stub: user_control_helper_get_buttonchar removes
  all_buttons on the Xbox 360 and then picks \m0 (the green fret, \b4);
  now only the fret glyphs show, every screen (D80).
- Set list previews: the previews' key is the song banks' generator on
  "<song>_preview" (the "other key" was the name); the natives
  SongPlayPreview / SongStopPreview / SongIsPreviewPlaying and
  SongCheckIfDownloaded were missing (the monitor took the download path
  and gave up). Now the focused song's preview plays on music_setlist, the
  next song's after a move, none after Back (D81).
- Difficulty pop-up: the focus is the desc's scribble highlight
  (alias_highlight moved and stretched to the item by ui_setlist_diff_focus,
  its three scribbles cycled by ui_setlist_scribbles); AddParam was a stub,
  so every scribble stayed at alpha 0. Now the red scribble marks the
  focused difficulty (D80). The set list's own focus bar was already right.

Checked: walk setlist_preview (new): preview playing for the first song
(aceofspades), then albatross after Down, still 8 s later, off after Back;
GD10_AUDIODUMP song mix peak 0.13..0.27 per second while a song row is
focused, 0.000 before the set list and after Back. Screenshots: main menu
help bar (green fret + SELECT, yellow + GAMEPADS), the difficulty pop-up
with the scribble on BEGINNER / EASY / HARD after 0 / 1 / 3 Downs.

Gate (engine/build/pfx, headless, scratch XDG): walks boot, quickplay,
menus, qp_pause, enhanced, gamepads, setlist_preview passed;
`tools/fuzz_seeds.sh` 13 / 13 passed; `--selftest` 31 FAILED (as
expected); `--scripttest`, `--sfxtest` pass.

## 2026-11-12 A song's lifecycle: quit, restart, end; the crowd (GD10-060, D90)

Owner: a quit song kept playing, the crowd screamed through every song and
for ever after it, quitting made the game "do a lot of wrong stuff".
Causes (logged with GD10_SFXLOG=1, GD10_SPAWNLOG=all, now logging every
spawn and KillSpawnedScript):
- gh4's kill_gem_scroller calls `SongUnLoad` (GH3:
  SongUnLoadFSBIfDownloaded); unhandled, the quit song played on under the
  set list, ui_deinit_gameplay's UnpauseGame resumed it, and its end's
  GuitarEvent_SongWon_Spawned pulled the menus back to gameplay.
- `menu_soundevent_in` (play_song) spawns Loading_Screen_Crowd_Swell, a
  loop with no end (Crowd_Beds +12 dB, swells every 0.5..1 s), killed by
  the transition's WarmUp entry; the port ran it for Intro only, so
  Quickplay's FastIntro (GH_SFX_Intro_WarmUp_Fast) never killed it: 4.31
  crowd sounds / s in the song, on in the menus. Now 0.26 / s.
- start_gem_scroller's crowd_reset never ran: the venue's bed and the
  one-shots (every 4.5..8 s) did not start; the menus' 10% bed went on
  until the meter crossed a threshold. SongHooks::begin runs it now.
- A voice stopped while the pause menu had it paused stayed paused for
  ever (IsSoundPlaying true, never freed): the stops now drop the pause.
The quiet crowd under the menus is the original's: FrontEnd_Menu_Music =
EXT_MD_Crowd_Good_Loop at 10% [C data]. docs/ui.md "A song's lifecycle".

Walks: qp_quit (Quickplay -> song -> pause -> Quit -> Quit -> New Song,
three times), qp_restart (pause -> Restart -> Restart, then Quit),
qp_finish (the song's end, results, the set list): in each song
`check:crowd_rate<=1.0` (crowd voices under User_Crowd per second; fails
while Loading_Screen_Crowd_Swell runs), after each `check:song_quiet` (no
song, no crowd voice but the menus' bed, no paused voice, no swell /
whistle / song script; the scripts still running listed: the same four
after each of the three quits). Before: qp_quit failed (4.31 / s, then the
song alive, 8 voices and GuitarEvent_SongWon_Spawned after the quit).

Gate (engine/build/life, headless, scratch XDG): walks boot, quickplay
(load window worst 32.3 ms, 0 over 50), menus, qp_pause, enhanced,
gamepads, qp_quit, qp_restart, qp_finish passed; `tools/fuzz_seeds.sh`
13 / 13; `--scripttest`, `--sfxtest` pass; `--selftest` 31 FAILED (as
expected).
Not done: the WarmUp runs at the song's start, not the table's 100 ms;
<Current_Crowd_Applause_SoundEvent_L> is a local the WarmUp scripts never
get (the first song's applause is silent); in-venue loading (career's
Loading transition) and Lou's devil_finish_restart not walked.
