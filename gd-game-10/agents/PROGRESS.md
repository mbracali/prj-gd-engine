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
