# gd01 backlog

Everything raised so far about the Guitar Hero (PS2) reimplementation, done or
not. Details of the original behaviour are in `FINDINGS.md`.

Status: ✅ done · 🟡 partly done / approximated · ❌ not started

## Done

| Area | Item | Notes |
|---|---|---|
| Reverse engineering | Ghidra headless analysis of SLUS_212.24 (R5900) | REA's Ghidra adapter rejects R5900; exports in `analysis/` |
| Data | ARK archive, encrypted DTB scripts, MIDI charts, VGS audio, textures, scene files | Python tools in `tools/`, all validated byte-exact |
| Gameplay | Chart building (chords, sustains, HOPOs), hit window, strum/HOPO judgment | Port of the game's watcher; `--selftest` hits 100% of all charts |
| Gameplay | Scoring, multiplier, Star Power, rock meter / failing | From the decompiled Player / StarPower / crowd code |
| Audio | VGS playback, guitar track muting on misses | |
| Audio | Deadlock at song start (stream lock vs mixer mutex) | Fixed: consistent lock order |
| Input | Keyboard, gamepad (joypad mode), USB guitars | |
| Platforms | Linux and Windows builds (`engine/build.sh`), macOS via GitHub Actions | Windows not yet run on Windows; macOS not yet built |
| Highway | Original 3D highway: surface, rails, lane lines, beat lines, gems, HOPO tops, star gems, sustains, smashers, hit flames, Star Power lightning | From `track/gen/track.rnd_ps2` |
| Highway | Fretboard artwork scrolling backwards | Fixed: the surface's V runs from 1 (strike line) down to −0.3 (horizon) |
| Rendering | OpenGL 3.3 renderer: materials, blend/z modes, textures, UV transforms | |
| Rendering | Per-vertex lighting exactly as the VU1 microcode: program choice (point light replaces directionals, ≤3 directionals), `use_environ` / `vert_amb` / `vert_dyn`, clamp to 1 per vertex, Multiply2 stages ×2, light along +Y | From the `.vutext` microcode (0x6ec / 0x7c5 / 0x436) |
| Rendering | Fog (view-depth linear, environ fog colour) | VU 0x2b8/0x2b9, GS FOGCOL |
| Rendering | Transform owners: an object's `parent` field names the transform it is drawn with | Fixed stage floors hanging over the crowd in every venue |
| Rendering | GPU skinning (4 bones per mesh) | |
| Animation | TransAnim, MatAnim, LightAnim, EnvAnim, CamAnim; filters; child anims | |
| Venues | All six venues load with their lighting sections | |
| Venues | Venue scripts: `switch_anim` slots, funcs, handlers, `delay_task`, `anim_task`, `set_showing`, `set_frame`, `remove_anim`, `unhook_anim_parents`, `add_trans`, `set_singer_env` | |
| Venues | Lighting states from crowd excitement × verse/chorus/solo, switched on downbeats | |
| Camera | Shot director: intro shot, weighted pools, solo shots, section changes; path / offset / framing / fov / ease math | |
| Characters | Skeleton, skinning, `.acp` clip playback | |
| Characters | AnimDriver chain: frames phase-locked to the beat every tick, `.acg` graph transitions (First / Longest / Now), S-curve blends, least-recently-used clip choice, kAnimGraphLoop | `FUN_0018a4b0`, `FUN_0017f1d0` |
| Characters | Band moods from `upcoming_<part>_on/off` one beat early, drummer patterns, 20% downbeat change-ups, frame scale on half/double tempo | `theband.dta` |
| Characters | Guitarist states (idle / playing / wail / solo, Bad when doing badly), clips blended in by the event beat, intro | `GuitarCharacter` `FUN_002856a0` |
| Characters | Fore-arm and upper-arm twist servos; `.rotx` / `.roty` channels | `charbase.dta` / `bandbase.dta` servos |
| Characters | Guitarist with the song's character and guitar on the walk spot | |
| Characters | Band (singer, bass, drummer) on the stage spots, driven by the EVENTS track | |
| Characters | Lit by the venue's band environment | |
| Crowd | Flat crowd: Crowd01..06 instances as live impostors, re-rendered each frame from the main camera's rotation (`FUN_00172350`), quads billboarded to the camera (FastBillboardXYZ), first N instances shown by excitement, the six `crowd instance` archetype/head pairs | 128×256 render target per block |
| UI | Animated screen transitions (common.gh `ui.enm` ambient fade, `meta_cam_*` camera turns, forward / back), gotos queued during transitions | |
| UI | Script `if` returns kDataUnhandled when false (FUN_002313a0), so handlers ending in a false `if` pass messages on | |
| Gameplay | Star rating (`num_stars`: score vs 2×/3× the base max score), `percent_complete`, game over → guitarist win/lose clip → `game_outro` → post_show / lose screen | docs/end_of_song.md |
| UI | `game` / PlayerConfig natives (character, guitar, skin, descriptions, `get_player_config`, `set_quickplay`), `autosave_goto`, `animate_forever_30fps`, `get_bank_loader` | |
| Audio | Song preview on the song list (preview window, looped, 1 s fades) and menu music (`meta_loop1a..g` at 0.6) | Second mixer voice |

## Reported bugs (2026-10-21)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B1 | Character proportions look wrong | ✅ | ~85% of the arm skin hangs off twist bones only the twist servos move; plus the hands now reach the neck / strum targets through IK (docs/guitarist_hands.md) |
| B2 | Theatre (Cochise): the floor shows above the crowd | ✅ | Not the crowd: `stage.1.mesh` etc. are drawn with their transform owner's matrix (`stage.mesh`); we drew them with their own stale identity |
| B3 | Weird glitches in the venues | ❌ | Needs specifics (venue / moment); candidates in "Venue artifacts" below |
| B4 | Characters over-bright | ✅ | The light direction sign was backwards (light travels along +Y): the 7.0 rim light flooded camera-facing surfaces. Formula now matches the VU1 microcode |
| B5 | Band not in sync with the music | ✅ | Frames now snap to the beat phase every tick (A = play_flags & 0xf) and moods change one beat early so blends finish on the event |
| B6 | Crowd sprites look like flat paper | ✅ | Quads billboard to the camera and the impostor is rendered from the main camera's angle every frame |

## Reported bugs (2026-10-21, second round)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B7 | Pause menu: only about a quarter of the background is drawn (text and behaviour fine) | ✅ | Texgen per stage was wrong: texgen 1 rotates/scales about the texture centre after the translation, texgen 4 without centring, texgen 0 ignores the transform (docs/render_gems_dof.md §1) |
| B8 | Characters stay on their starting spots; they never walk around the venue | ✅ | Walking reimplemented: walk/turn/stop clips, `bone_facing` root motion, hub spot, walk_delays by excitement, pre-solo blocks, solo walk point, lose-walk. Start spot fixed to walk_spot_01. Layout simplified (whole walk cycles) |
| B9 | Gems look off: the original has a black round top; Star Power gems look almost fully transparent; whammy on a sustain shows no animation | ✅ | Black tops (coloured only on HOPOs), star_base disc, bonus gems during Star Power, coloured star tails, grey tails once missed/released, whammy width wave on held sustains. gem2 / star2 / gem_bonus2 highlights, glow lines and burn particles now in (twelfth round H1) |
| B10 | Character select: not every character's picture shows | ✅ | PictureEx/PictureToggle meshes drawn (with the undo of their stale world transform) |
| B11 | The help bar (button legend) stays on screen during transitions; in the original it disappears when something is selected and reappears on the new screen | ✅ | Drawn only while the UI is idle |
| B12 | Star Power: the meter gauge is wrong, there is no Star Power animation on the highway, and the guitarist's Star Power move plays only about half the time | 🟡 | Guitarist move via `start_starpower`; star tube MeshAnim; lightning bolts timed from the deploy; bonus gems; rails glow (red warning, blue SP ready). Missing: deploy sparkle burst, cyan burn, track shake check. Please re-check the meter |
| B13 | Hands move correctly but the fingers do not | ✅ | The finger mapping entries are flat symbol/value pairs; they were parsed as sublists so every note fell back to finger_open |
| B14 | The close-up camera on the guitarist's hands never appears | ✅ | They are the SOLO_NEAR shots (solos only). Native picker ported (side/distance rules, fresh shots, walk/Star Power validity, no switching in Star Power); focus now follows the guitarist's head; depth of field and hide_crowd per shot. Shaky cam still missing |
| B15 | No bokeh (depth of field) | ✅ | PS2 pass: everything at or beyond 2·|subject − camera| gets a 4-tap blur, on shots with `enable_dof` and `arena set_depth_of_field` |
| B16 | Some fonts look odd, especially the status on the career screen | 🟡 | LabelEx fit now follows the decomp (FUN_0011f868: width/height fit as a scale). Also seen: on Select Career the "NOVICE STATUS (1 OF 25 SONGS)" line overlaps EASY — check that label's position/fit |
| B17 | Freeze: Iron Man with Johnny Napalm in the Basement | ✅ | Most likely the same cause as B25 (song row lost after changing difficulty); the song itself plays fine |
| B18 | The selected character is not carried over anywhere (neither quickplay nor career) | ✅ | `game set_quickplay` with no arguments now copies the song's setup and the game always starts from the config (character, guitar, venue) |
| B19 | The shop (Unlock Shop) is not available | ✅ | Disabled on Easy in the original too; on Medium and up it works (store natives done) |

## Reported bugs (2026-10-21, third round)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B20 | Entering a menu (Career or any other), the text shows in a wrong part of the screen for a second or two, then jumps to the right place | ✅ | Three causes: (1) the GPU fell seconds behind (no limit on queued frames), so the first texture upload of a new screen stalled ~2 s on that frame; now at most 2 frames in flight. (2) Lit fonts ignored the transition fade (`ui.enm` ambient), so text showed before its poster. (3) The exiting screen wasn't drawn during its out-animation (black) |
| B21 | The help bar buttons are gone completely; they should show, hide when something is selected, and come back on the new screen | ✅ | A `helpbar set_display` from the memory-card dialogs stayed in effect on every later screen (empty list). Now reset on each screen change |
| B22 | No loading screen when a song starts; it should stay at least 2 s like the save screens | ✅ | The `loading` screen holds 2 s; the loading tips (`tips random_tip`, config/tips.dta, by difficulty) now show |
| B23 | In game, the ROCK meter shows at the start, loaded but sideways; it should be hidden like the score until it slides in | ✅ | Its pre-slide pose sits just outside the PS2's 4:3 screen; our 16:9 view showed it. The HUD is now clipped to 4:3 |
| B24 | In game, every fret button looks pressed all the time | ✅ | `foreach_player_matcher` handed the script a stub, so `refresh_track_buttons` never reached the track |
| B25 | Freeze in Quick Play: Sharp Dressed Man on Expert | ✅ | Not a freeze: the song never started. The song list is rebuilt when the difficulty changes, so the stored list row pointed at a header after picking a non-default difficulty (no song → black screen; the song's quickplay character/venue were lost too). The game now remembers the chosen song itself |
| B26 | Characters still never leave their spot; the start-of-song animations that walk don't move them | 🟡 | Root motion was applied only to walk clips; the original applies every guitarist clip's `bone_facing` deltas (the intro walks around). Random walks still need Okay/Great excitement and 20–55 s |

## Reported bugs (2026-10-21, fourth round)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B27 | Select Career: the status line ("NOVICE STATUS (1 OF 25 SONGS)") overlaps the difficulty name | 🟡 | With B35 the buttons are box-wide and the line now sits between EASY and MEDIUM; please re-check against the original. Checked against the data and the LabelEx/ButtonEx code: the status label is top-right aligned at the button's centre line (both views share z), serif.font at size 30 with the label's own (0.8, 1, 0.4) scale. Rendering matches that; need a screenshot of the original to find what differs |
| B28 | Some labels look stretched / "exploded": venue select, "34 WINSHIP ST" (the "BASEMENT" label next to it is fine) | ✅ | LabelEx fit *replaces* the label's scale (FUN_0011f868 → FUN_001da700 sets the local rotation; label and text share one transform). The saved local already holds the editor's fit (0.69 × 2.67), and we applied the fit again on top (~2.7× too tall) |
| B29 | Help bar: the strum (UP/DOWN) icon is cut in half, the entries are spaced differently, and the original has semi-transparent black round boxes behind the icons | ✅ | Ported the HBElement draws (FUN_00149d88, ButtonHBElement_v5, LabelHBElement_v5): rounded box from resources help_box_left/mid/right (mid stretched to the element), icons on help_bar.mesh / help_bar_strum.mesh (the strum one is 2× wide), the strum entry starts at its own fixed spot (help_bar_strumbar.mesh), label text at help_bar.txt's 0.8 scale |
| B30 | The ROCK meter starts the song on red (it should start in the middle); hitting notes moves it to yellow | ✅ | The needle value started at 0.5 but the view was only posed once the value moved, so it stayed on frame 0 (red). Now posed every frame |
| B31 | The Unlock Shop is still not accessible | ✅ | Same as the original: career.dta disables `cm_store.btn` on Easy (no cash on Easy). The tested save only has an Easy career; on Medium the shop opens |
| B32 | Career song list (after choosing the difficulty): the text layout differs from the original; it should be spread more evenly | ✅ | Two causes. (1) The song provider started in quickplay mode (the original ctor FUN_00118400 sets 0), so the career list was the quickplay list until sel_song was left once: no scores, stars, checkmarks or career footer. (2) The layout was inferred; now ported (docs/song_list.md): one element per song, a tier header is column 3 of its first song, which sits 2 slots lower; score "%i" on every career row, stars, checkmark on beaten songs, footer at the blurb offset, highlight on name/score/stars, `spaced_selected_pos` = sel + 2·headers |
| B33 | Menu music sometimes stops after a while or after some actions | ✅ | The original (FUN_0020dcd8/e588) picks one meta_loop when the meta panel loads and loops it; the meta panel's Exit stops it. We chained loops and never restarted after a song (sel_song clears metamusic_on_exit). Now: one looping track, restarted whenever the menus come back |
| B34 | Select Career: the font of the status label under each difficulty doesn't match the original | 🟡 | Style label_serif_black → label_serif_black.txt → serif.font (no font override). Same as B27: needs a reference screenshot |

## Reported bugs (2026-10-21, fifth round)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B35 | Menu text is centred at its natural width; in the original it stretches horizontally to fill its box | ✅ | RndButtonEx FUN_0012c408: with fit (+0x134) the button's text gets x scale box_width / widest line (stretch or squeeze, no height fit), centred. We had read the fit byte as caps and dropped the real caps byte (+0x12c, caps_mode 2/0). docs/button_text_fit.md |
| B36 | Hitting notes shows a simpler effect than the original | ✅ | The original plays particle views on every fret of the chord (docs/hit_fx.md): smash_normal / smash_star / smash_bonus (flame blobs, sparks, embers, spinning flare), the phrase-capture burst + 12°-fanned lightning, and the sustain burn (normal / whammy / Star Power colours). Added ParticleSys + ParticleSysAnim (load, simulation per PsParticleSys, camera-facing quads) and drove the views from the hit / miss / sustain-end events like FUN_00164988. Not yet: tail_bonus.part (star-tail whammy particles), venue particles |
| B37 | The bokeh (depth of field) renders glitched | ✅ | The PS2's 4 point taps 3 px apart (on 448 lines, softened by the TV) were scaled to ~5 px at 720p and showed as a doubled, grainy image; now the same spread as a 5×5 tent. The lookup also ignored the viewport origin (shifted blur in offset viewports) |

## Reported bugs (2026-10-21, sixth round)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B38 | Star Power gems are all the same colour and don't spin | ✅ | docs/star_gems.md. (1) The star meshes carry red vertex colours that the PS2 drops (star.mat: no use_environ, no vert_amb → material colour), but our unlit path multiplied them in, tinting every star; the rule now applies to all unlit meshes. (2) Nothing drove star.tnm: now at now·0.03 (loop 0..100, a turn per 3.3 s) before the gems. (3) star2.mesh highlight drawn; tops drawn under the star; the gem that breaks a phrase keeps the star look with star_miss |
| B39 | Every settings menu shows a strange icon (looks like a woman, like Judy Nails) | ✅ | The PictureToggle checkboxes: `set_state` / `get_state` / `toggle` are on/off and show the style's tex_on / tex_off (checkbox_checked / checkbox_empty); we treated set_state as the focus state and drew the mesh's own placeholder picture. `options get_stereo` / `get_lefty` were also unanswered (wrong names), so the boxes started unticked |
| B40 | The settings menus look broken | ✅ | The volume sliders were not implemented. RndSlider ported (docs/slider.md): track tinted by state (green while editing), pod at 31 + 102·pos/(steps−1), label; Select toggles editing, Up/Down ±1 with SCROLL_START_MSG, Back cancels (GH_CANCEL_MSG); SliderPanel routes Up/Down/Back to the slider being edited and answers `slider_selected` |
| B41 | Pause menu: no sounds, and moving through the options doesn't work | ✅ | Pausing froze the whole SPU, so the pause menu's own button sounds were silent too. The original (pause_all_sfx, IOP FUN_000041dc) freezes only the voices playing at that moment; now per voice. Keyboard navigation of the pause menu was checked working |

## Reported bugs (2026-10-21, seventh round)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B42 | After the song ends, the guitarist's hand stays frozen in place while the rest of the body moves | ✅ | At game over we stopped running the hand logic, so `bone_fret` (the fret-hand IK target, detached from the skeleton) froze in world space while the body moved on. GuitarCharacter_v10 runs every frame; now so does ours |
| B43 | The newspaper at the end of the song shows a different guitarist than the one played | ✅ | FUN_00104fd0 loads `ghui/image/photo_<character><0..3>.bmp` (random) into `winner_photo0.tex`; we never did, so the panel's own photo showed |

## Reported bugs (2026-10-21, eighth round)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B44 | Pause menu: still no sound when moving through the options | ✅ | Pausing paused the whole audio device (SDL), so no sound could play at all. Now only the song stops (with its own frame clock, so it resumes in sync); the SPU keeps running for the menu sounds |
| B45 | End-of-song screens: no sound when moving through the options | ✅ | Same cause as B44 (post_show sets the game paused) |
| B46 | Sustain tails are white; they should have the note's colour | ✅ | tail_<c>.mat are vert_amb mats (vertex colour, no material colour); the original's tail builder (FUN_001633d8) writes the mat colour into those vertices. Our tails now carry it as their colour |
| B47 | HUD: the 1x/2x/3x/4x multiplier looks over-bright | ✅ | See B52: drawn after the frame, the additive ×2 glow saturated the window to white. It is now drawn before the frame (a coloured halo around the box) [I] |
| B48 | Deploying Star Power: a glitchy artifact appears on top of the notes | ✅ | The deploy sparkle's MatAnims animate the UV rotation and scale (starts ×5, i.e. a small star); we only applied UV translation, so each 6-unit quad showed a magnified clamped texture (white slabs). MatAnim stage rotation / scale keys implemented (also fixes venue materials that use them) and the UVs re-bake when they change |

## Reported bugs (2026-10-21, ninth round)

| # | Bug | Status | First suspicion |
|---|---|---|---|
| B49 | The guitarist drifts off his spot over a song and ends up facing a wall | ✅ | The stand / play clips are not perfectly still in `bone_facing` (measured: about 2.6 units and 10° per 100 s even without walking), and walks only steered the heading, so the stop clip's overshoot added up walk after walk. Between walks the guitarist now eases back (position and facing, ~1 beat) to the spot he walked to (start spot, walk target, or the nearest one after a cancel / lose teleport). In tests he stays within 0.13 units of the spot (autoplay keeps the crowd at Peak, which never walks, so a walk itself was not exercised headless) |
| B50 | Sustain whammy does not look like the original | ✅ | Tails were stacked copies of tail02.mesh scaled per 4-unit block (stepped edges). Now the original strip (FUN_001633d8): tail02's 6-vertex cross-section swept in 30 rows anchored to the far end with a 1-unit pointed tip, each row widened by 1 + h from a 100-sample history pushed at a fixed 60 Hz (h = 3 × bar, 0..3), frozen when released, zeroed per tail. The bar is now analog (guitar whammy axis); a keyboard / digital whammy wiggles it. "Whammy active" follows whammy_speed / whammy_timeout (also for Star Power gain). The glow line width eases toward (max s − 1)/2 + 1 |
| B51 | Whammying a Star Power sustain should glow blue | ✅ | Ramp g (±0.1 per 60 Hz poll) on a held star-phrase sustain while whammying: the tail colour lerps to tail_star's blue (.08,.81,1), the glow line from tail_glow_star to tail_glow_whammy, and tail_bonus.part emits along the tail |
| B52 | Score HUD colours: the streak bulbs are blue and the multiplier is over-bright | ✅ | Bulbs: score_streak_N.mat carry a cyan material colour, but stage-0 blend "Src" selects TFX DECAL (texture colour only), which we never applied to the first pass. DECAL is now honoured for every stage-0 "Src" material (also rock meter LED, fireworks flare). Multiplier: see B47 |
| B53 | The main menu opens on Tutorials instead of Career | ✅ | main.dta focuses Tutorials on the first visit when a guitar is used and no tutorial is done (original behaviour); the port ignores that so the menu always opens on Career (user request) |
| B54 | Microstutters: 60 FPS must be guaranteed | 🟡 | Measured headless: ~370 FPS uncapped, worst frame 4–10 ms, so it is pacing, not load. Fixed: (1) the song clock used for drawing advances with the wall clock and eases toward the audio position (the audio clock re-anchors on every callback, which jittered scrolling); (2) flare occlusion reads depth through pixel-pack buffers 3 frames late instead of a synchronous glReadPixels that stalled the GPU every frame; (3) per-frame tail / line geometry uses a pool of buffers instead of rewriting one buffer many times a frame; (4) vsync at the 60 Hz multiple of the display (interval 2 at 120 Hz), and a precise 60 FPS limiter when vsync is refused. Still possible: first-use texture / mesh builds mid-song (a few ms). Needs confirming on a real display (headless has no vsync) — the new FPS counter helps |
| X1a | Enhanced Settings screen + FPS counter (first enhanced option) | ✅ | Options gets "Enhanced Settings" between Game Settings and Data Settings (a copy of the Data Settings button; the entries below move one step down). The screen is defined in the port's own DTA (parsed from text) and uses the game's meta panel (menu music), the original guitar_display panel (a random guitar spinning on the right, with some ambient added) and the help bar; a native panel draws the black page and the list in the game's fonts. Every planned enhancement is listed as "Coming soon"; FPS Counter (default off) shows the frame rate in green, bottom left. Saved in enhanced.json next to career.json |

## Parity round (2026-10-22): crowd audio, career, tutorials, multiplayer, characters, camera

Specs from the research pass: docs/crowd_audio_plan.md, career_e2e_plan.md, tutorials_plan.md, multiplayer_plan.md, character_polish_plan.md, camera_director_plan.md.

| # | Item | Status | Notes |
|---|---|---|---|
| P1 | Crowd audio | ✅ | The crowd loops (22050 Hz) played at double speed: the mixer now resamples every stream to the device rate (also the tutorial VO). The sound-effects volume option now reaches the crowd; the screen fade fades it; set_excitement always restarts the file; Boot→Okay plays the falling reaction (venue.dta); `wingame` starts on a career final win; set_volume / set_enabled / enabled. Claps were already right (Star Power only) |
| P2 | Career end to end | ✅ | Status newspaper page (its scene name is a script expression, now evaluated on every entry; character picture; band / character headlines); bonus videos play (video_provider get_video_file from store.dta); career song list follows the selected venue; guitar lists drop guitar_red_octane, and quickplay / multiplayer count unlocks at any difficulty; FINAL_WIN only above Easy. Encores do not exist in GH1. Credits panel: done in the tenth round (B58) |
| P3 | Tutorials | 🟡 | Playable: the tutorials menu (button kept enabled on PC), the `tutorial` object (VO player with drop / round-robin, the tuning trainer, the hammer-on / pull-off trainer), ThreadTask scripts (wait / wait_timeout / sleep / exit), set_local_pos / scale / stage_tex, the tutorial game mode (basement, no band, TUTORIAL camera, tutorial bank, songs loaded and started by the lessons, time pause, no game over), the player setters and the new callbacks (pass, hopo, swingAtHopo, held_note_released, whammy_start / end, star_gem_hit). Basic Lessons verified end to end headless; multiplayer results (multi_compete) verified; Star Power and Advanced lessons not yet walked through. Open: set_wrap_width, arena_panel set_paused, the lesson box backgrounds to check against the original |
| P4 | Multiplayer face-off | 🟡 | Two players: pad slots (keyboard fills a free slot; `--kb2p` puts P2 on H J K L ; / N M / B / O), per-player menus (character, guitar), section ownership from the chart, the opponent's gems neither hittable nor missable, P2 score × B0/B1, no fail, two highways (dual perspective 0.76 in each half, 20 tail rows, own notes only), the multiplayer HUD (two score meters, two star meters, the lead meter), small_club_multi with two guitarists on walk_spot_02 / 01, no walks, lights pinned to Great, the MULTIPLAYER camera switching on active_players, winner / loser poses, player1::player stats and both newspaper photos. Open: the guitar-stem mix (UnmuteBeatMatchAudio not decoded; interim rule), per-player track shake / bonus look routing, multi_compete not yet seen end to end |
| P5 | Character polish | ✅ | Native CharWalk (turn by predicted pose, 2-beat layout with speed correction, chain queueing, 1-beat-ahead steering, pose hold); random walks no longer lose half their picks; drummer / keyboard realign and the beat delta after a tempo change; crowd impostors face the blocks' average; the crowd draws after the band and not under hide_crowd. [I] the stop clip is chosen by its measured turn (the shipped stop clips do not turn the way their names say) and the hold eases to the spot itself. The strum hand already matched (the player's strum never drives it in normal play) |
| P6 | Camera director | ✅ | Shot durations [lo, hi) bars; beat → downbeat → one_bar_to order; the bar count keeps running in Star Power; excitement re-picks on the first beat with ≤ 1 beat left; only picker shots count as current (intro_camera_flags for the first); the pool refresh rules; low_excitement_ok means excitement ≠ Great. Open: reduced_crowd_shots, the crowd-update counter, the original RNG |

## Reported bugs and enhancements (2026-10-22, tenth round)

| # | Item | Status | Notes |
|---|---|---|---|
| B55 | The whammy does not change the sound of held notes | ✅ | docs/whammy_audio_plan.md. The original's "slip voice": the guitar channels play through a delay line whose speed drops to 2^(-1/12) (pitch_bend_range 1, ramped over ms_to_full_pitch_bend 1000 ms from the hit), down only, armed once the bar rests after the hit, lag capped at 0.9 × max_slip (270 ms), resynced at each hit and gem time. Keyboard whammy wiggles at half depth |
| B56 | Notes pop in at the far end of the highway; the original fades them | ✅ | docs/track_fade_plan.md. The original copies a 128×32 strip of the venue before drawing the highway and lays it back over the far end with alpha 1→0 (track_fade.mat) plus +0.02 (track_add.mat); surface, lines, tails and gems fade, smashers / hit effects / lightning draw after. Face-off: each highway its own strip |
| B57 | Options: UI elements in the wrong place for 1–2 s on entry | 🟡 | Not reproducible headless (the entry is the original slide-in). Same family as B20: panel textures were uploaded on first draw, stalling the first frames; they now upload when the panel loads. Please re-check on your machine |
| B58 | Credits do nothing | ✅ | docs/credits_plan.md. CreditsPanel + RndCreditList: config/credits.dta (907 rows), title / heading / name / centred templates in the font material colours, 15 rows 25 apart, 0.018 row a poll at 60 Hz with the top and bottom two rows fading, credits.vgs music (1 s fade in to 0.8, 1.25 s out), go_back to Options at the end (~14 min), Back any time |
| B59 | The company logos' black backgrounds stand out against the screen | ✅ | The logo screens are cleared to black over the whole window |
| B60 | Guitarists (Pandora and the others) slowly turn to face the back of the stage and clip into objects | ✅ | Measured headless: the stand / extreme / Star Power special clips move the root through their bone_facing deltas, and a clip cut short by the next one leaves part of its swing behind (special_01 → stand_medium_04 turned Pandora 108° and moved her 21 units in two beats). The original's pose hold only corrects while a clip waits to start, so recovery was luck. [I] The port adds a standing guard for every guitarist (and both face-off guitarists): off a walk the root eases back to its spot every frame and can never be more than 60° or 15 units away; off during game over (the win / lose clips move on purpose). Tested with all 7 rockers, forced walks: worst standing error 44° (root), 46° (pelvis incl. sway), 15 units. Dev aid: GD01_FACELOG=1 |
| X1b | Options: Enhanced Settings overlapped the poster art | ✅ | Moved left (x −80) |
| X1c | Enhanced Settings page restyled | ✅ | A random guitar (facing the player, still) or a random rocker frozen in a win-celebration pose, guitar in hand, centred in the free half of the page under a top-left key light with a visible beam and floor pool; cool rim light; no movement |
| X20 | Mouse support (Enhanced option, on by default) | ✅ | Every menu text records its screen rectangle as it is drawn: hovering a navigable entry or list row focuses it (with the original focus / scroll sounds), a click selects; the help bar's CONTINUE / BACK (and Start) are clickable; the Enhanced Settings list too |
| X21 | Esc three times in a row on the main menu quits | ✅ | |
| X22 | Main menu help bar: GAMEPADS (blue) and MODS (orange) | ✅ | Placeholders for the gamepad configurator and the mod tools; the icons are the disc's green help icon recoloured |
| X23 | Sharper Text & Sprites (Enhanced option) | ✅ | Menu and font textures upscaled ×4 (×2 above 256 px) with Catmull-Rom on premultiplied colour; hard-edged alpha (glyphs, cut-outs) re-tightened. Applies to textures created after it is switched on (new screens) |

## Reported bugs and enhancements (2026-10-22, eleventh round)

| # | Item | Status | Notes |
|---|---|---|---|
| B57 | Options > Data Settings / Game Settings: checkbox and sliders drawn in the wrong place for a moment | ✅ | Reproduced headless: on the first frames of the transition (screen still black) the toggle and slider meshes were drawn at full brightness, ignoring the panel's ui.env ambient (the fade). Lit resource meshes now take the ambient like the fonts; the shared slider / toggle textures upload at start, and toggle pictures are cached |
| B60b | Pandora still turned to face the back | ✅ [I] | Found two holes: venues without walks (basement, small club) never set the hold, so the guard never ran (69° there); and arena walks between the back corners go through the downstage hub, so the upstage leg faces 107–131° away (original path, kept). Added: hold set in every venue; a camera-cut snap (at every cut, a standing guitarist is put exactly back on his spot, hidden by the cut; also whenever he is out of shot); a walk watchdog (80 beats). Worst standing error after: 13° root / 36° pelvis incl. sway. Dev aid GD01_ROOTLOG |
| B61 | Whammy on a plain sustain turned the burn blue | ✅ | The burn's whammy (cyan) state is the star whammy: a star-phrase sustain, or any sustain during Star Power |
| B62 | Mouse hover scrolled the song list | ✅ | Lists keep the selection in a fixed slot, so a hover scrolled them: lists now take clicks only; the mouse wheel scrolls (Up / Down); hover only while the window has focus |
| B63 | Company logos not skippable | ✅ | Enter / Esc / Start skip all the logos (and the intro movie) |
| B64 | Name Your Band: the name sat off to the left | ✅ [I] | The entry's anchor is the original's left edge (it lays the name out left-aligned, growing right; FUN_00129948 align 0x21), but the template centred it on that anchor. The port centres the band name on the page (x 0, the labels' centre line), so it grows from the middle; the high-score name entry is unchanged |
| B65 | Max FPS followed the display's refresh rate instead of capping | ✅ | Vsync only when the refresh rate is the cap or a multiple of it; otherwise vsync off and the frame loop paces to the cap (measured 60 / 120 / 240 exactly) |
| B66 | Every character used the same highway | ✅ | characters.dta track_surface: each guitarist's own surface on track_surface.tex (both players in a face-off), as FUN_0015c540 |
| B67 | Idle on the title screen: black screen, then no menu music | ✅ | The attract mode's `campaign get_random_song` was looked up only in the song list (not built yet): song -1. Now found in the engine's songs; player_matcher set_auto_play plays it, track_panel set_showing FALSE hides the highway, any button leaves, 120 s fade back to the title; the menu music returns |
| B68 | Venue select scrolled down, then up to the new venue | ✅ | The map scroll starts at `{sv_map.view frame}`, which returned nothing (0, below the basement): views now report their frame |
| X24 | The Glitches | ✅ | A ninth band at the bottom of Choose Band / Load Band (not saved): everything unlocked and bought, every song beaten with 5 stars, $9,999,999, all tutorials done, at every difficulty |
| X22b | Main menu help bar spacing | ✅ | With GAMEPADS / MODS the strum bar follows the row; every gap is twice text_spacing |
| X1d | Enhanced Settings: the screen transition | ✅ | The page slides and fades with the screen transition like the posters |
| X1e | Enhanced Settings rework | ✅ | Collapsible sections (Steam Deck collapsed), separators, scrolling list, checkboxes, dropdowns (Enter opens, Left / Right cycle), ? help pop-up over the model (yellow fret / ? icon / `/` key), warning signs on options not built yet, mouse and wheel, every change applied at once |
| X25 | Steam Deck Mode | ✅ | Locks every other option at its default; 60 FPS, 800p internal, borderless full screen, original sprites |
| X26 | Screen mode (windowed / full screen / borderless) | ✅ | SDL fullscreen modes, applied live |
| X27 | In-game max FPS 60 / 120 / 240 (default 120) | ✅ | vsync interval at refresh multiples, else vsync + pacing; headless unpaced |
| X28 | Internal resolution PS2 448p … 4K (default 1080p) | ✅ | Frames render into an off-screen colour + depth target at that height (window aspect) and are scaled to the window |
| X23b | Sharper Text & Sprites default on | ✅ | |
| X29 | Stronger Bokeh | ✅ | Focus at 1.3× the subject distance (original 2×), blur taps 4.8 px / 448 lines (original 3) |
| X30 | No Fail Mode, Autoplay (cheats) | ✅ | As --no-fail / --autoplay, from the next song |
| X31 | Touch controls, Rock Band guitars, XInput, Anti-aliasing, FSR 3, shadows for all, more lights, replace textures, cut content | 🟡 | All built in the thirteenth round except Replace Textures (dropped from the roadmap in 1.1.3) and Cut Content |

## Reported bugs and enhancements (2026-10-22, twelfth round)

| # | Item | Status | Notes |
|---|---|---|---|
| B69 | Max FPS 60 still showed 120 on a 120 Hz display | ✅ | The 60 cap used vsync every 2nd refresh (swap interval 2), which many drivers / compositors ignore. Now vsync stays at interval 1 and the frame loop also paces to the cap whenever the cap is below the refresh rate |
| B70 | After the demo (attract) mode the menu sound effects were silent until a song was played | ✅ | The demo leaves through finish_fade, whose fade panel turns the sound-effect master volume to 0; the panel stayed up into finish_fade, so nothing gave it back (a song's fade panel did). The fade panel now restores the volume when it closes (verified: master 1.00 on every menu sound after the demo) |
| B71 | Songs with a female vocalist must show the female singer | ✅ | songs.dta gives some songs their own (band ...) lineup, which the port ignored: female singer on iloverockandroll, takeitoff, farewellmyth, getready2rokk; a keyboard player on frankenstein; no singer on spanishcastlemagic, breakingwheel. SongInfo::band → VenueRenderer::Band::lineup → loadBand (charsys.dta's default band otherwise). Dev aid GD01_BAND="SINGER_FEMALE_METAL BASS_METAL DRUMMER_METAL" |
| B72 | The Garden: ceiling lights flash far too fast | 🟡 | The ceiling rig (anim_beam01–08, orange / pink / red spots) loops 100 frames on the beat clock (480 per beat), a fifth of a beat; the floor beams loop 1000 frames. The port follows the original code path for these anims; no script slows them. Needs a capture of the original (e.g. PCSX2) to confirm a different rate |
| B73 | The Garden: one wide shot has a far too bright light from stage left | ✅ | The original sends the venue post_switch_cam after every camera cut (VenueCam switch_cam FUN_0016f3b8); the port never did, so the arena's stadium_spotlight.mesh cone stayed up in flr_far_lft03 / flr_far_rt03. Now sent at every shot change, which also turns on the other venues' per-shot hides (fest solo beams, big_club smoke and beams, basement, small_club, theatre crowd). Dev aid GD01_SHOT=<shot> holds a shot |
| B74 | Toxic Summer Fest: parts of the mountains are see-through | 🟡 | The mountain meshes fade by design: vertex alpha 0 on their two lowest rows, rising to 1 at the middle row; the material passes vertex alpha through as the original's (docs/lighting.md). cloud_MASK.mesh is not involved. Needs a capture of the original's opening shot to compare |
| X40 | Disable Shaky Camera | ✅ | The original shaky cam ((shaky 1) shots follow shaky_cam1.tnm, ±0.2 units) was already in; VenueRenderer::setShakyCamera, off with Enhanced Settings > Disable Shaky Camera |
| X41 | Enhanced Settings backdrop | ✅ [I] | The basement with its default band, posed for one second and frozen, filmed by a still three-quarter close-up on the singer's microphone (VenueRenderer::setStillView, singerMicPos / singerBonePos), placed upper right; the old spotlight beam and a warm glow on the microphone; the explanation box sits bottom right. Loaded on entering the screen, freed on leaving. Particles and venue lights are frozen as they are |
| H1 | Gem highlight, miss pop, hit rules (highway round) | ✅ | gem2.mesh highlight on normal / missed gems, gem_bonus2.mesh streak in Star Power, layer order top → body → highlight; a miss with a fret pops that smasher (PlayerMatcher_v4 → FUN_0015e2f8 → FUN_00164ae8); a pending wrong strum that expires reports its gem and fret (could never break a Star Power phrase before); sustain points by the original ms-share formula, finishing when all points are earned. Open questions in FINDINGS.md answered (HOPO +0x74 = 60 ms constant, phrase loss matches, no SOLO / PITCH tracks on the disc) |
| X32 | Enhanced Settings overhaul | ✅ [I] | 3D model and ? icon removed; the focused (or hovered) option's explanation shows in a box on the right; the options sit on a see-through black box; Disable Shaky Camera, Audio / Video Calibration, Export User Files, Load User Settings, Load User Progress, the 7 original cheats |
| X33 | Export / load user files | ✅ [I] | Export: a folder dialog, writes GD01-user-settings.json (every Enhanced Setting) and GD01-user-progress.json (the career save). Load settings / progress: a file dialog; applied and saved at once; every property is checked (type, allowed values, ranges, known characters / guitars / skins) and anything odd keeps its default. Dev aid GD01_DIALOG=<path> answers the dialog |
| X34 | Cheats in Enhanced Settings | ✅ | The original long_cheats.dta cheats as on/off switches, setting the same script variables as the button codes: Air Guitar ($cheat_guitar 21: no guitar), Guitar Hero Controller Guitar (22: guitar_red_octane), Unlock Everything (campaign all_access, never saved), Locked Full Rock Meter ($cheat_meter: arena init sets the crowd rating to 1, inactive), No Venue (arena set_showing 0, now honoured: venue, band and crowd not drawn), Monkey / Skull Head Crowd ($cheat_crowd 1 / 2). Air / GH guitar and monkey / skull are exclusive pairs |
| X35 | Audio / Video Calibration | ✅ [I] | The original has none (no strings, screens or scripts). Two steps of 8 presses on a 750 ms beat (3 lead-in beats): ticks you hear, then a ring that flashes silently; the median offsets are saved as audio_offset (moves the judging, added to --av-offset) and video_offset (draws the highway that far ahead); input delay cancels out. Kept in Steam Deck mode |
| X36 | New credits | ✅ [I] | GD01 credits (project, main developers, open source thanks, beta testers, original heroes), a spacer, then the original credits |
| X37 | Title screen legal text | ✅ [I] | The GD01 notice over the five legal labels (two were hidden in the original), one line each at the original size |
| L1 | Memory card texts | ✅ [I] | ui/pc_strings.cpp: the user's From → To table (docs/texts_loading_and_save.md) over the locale, both `tok` and `tok_ps2`. save_saving used "Saving data..." (the table repeated the loading text) |
| X38 | GD01 loading tips 30–35 | ✅ [I] | Added to the tip pool on every difficulty; a tip longer than 6 lines is laid out wider and scaled down to fit the poster. Dev aid GD01_TIP=<token> |
| X39 | Dev aid GD01_SCREEN=<screen> | ✅ | With GD01_SKIP_BOOT, opens that menu screen (e.g. credits, enhanced_settings) |

## Enhancements (2026-10-22, thirteenth round)

| # | Item | Status | Notes |
|---|---|---|---|
| X42 | Touch controls | ✅ [I] | In a song a finger on a lane at the strike line = fret + strum (hold = sustain, fingers within 30 ms = one chord, both highways in a face-off); Star Power: tap the meter or a second finger off the highway; top-right corner pauses; menus: tap = click. SDL touch-as-mouse off while on. Dev aid GD01_KEYS touchX_Y / +touchX_Y / -touchX_Y. Not tried on a real touch screen |
| X43 | Rock Band guitars | ✅ [I] | Xbox 360 RB guitars, PS3 / Wii (added SDL mappings, layout not checked on hardware), RB4 by USB id; frets (solo frets too), d-pad strum, right-stick whammy, tilt / Select Star Power |
| X44 | XInput controllers | ✅ [I] | On: the original gamepad mode (L2 L1 R1 R2 A frets, each press strums) plus stick whammy; off: gamepads only drive the menus |
| X45 | Anti-aliasing | ✅ [I] | FXAA 3.11, SSAA (2x, 4-tap downsample), TAA (Halton jitter, depth reprojection, YCoCg variance clip, reset at cuts); on the venue frame only (highway / HUD / menus stay crisp) |
| X46 | AI Upscaling | 🟡 [I] | FSR 1 (EASU + RCAS) with Performance / Balanced / Quality (0.5 / 0.59 / 0.67). FSR 3.1, FSR 4, DLSS 4, DLSS 5 listed as not available: they need DX12 / Vulkan vendor SDKs, motion vectors and specific GPUs; not possible in the OpenGL 3.3 renderer |
| X47 | Bokeh Effect None / Regular / Stronger (default Stronger) | ✅ | Replaces Stronger Bokeh |
| X48 | Enable Particles FX | ✅ | Global switch (scene/particles): venue particle systems and the highway hit / burn / tail particles |
| X49 | All band mates cast shadows | ✅ [I] | A light above each member with its own floor glow (original ellipse math) and a flattened depth-only silhouette cutting it (band models have no shadow mesh) |
| X50 | More Lights on Venues Off / Subtle / Strong + Dust in Light Beams | ✅ [I] | Additive cones / glows ×1.2 / ×1.45, band fill light, floor pools per member, soft beams to each member, dust motes; colours follow the stage spotlight |
| X51 | Bloom, Cinematic Color | ✅ [I] | Dual-Kawase bloom (Low / High); filmic curve, warm / teal grade, vignette |
| X52 | Add Fog to the Ground (Experimental) None / Low / Full | ✅ [I] | One fullscreen pass: world height from the venue depth, exponential height fog above the stage floor, drifting noise, coloured by the blurred frame; never over the highway / HUD |
| P7 | Guitar track mutes on a miss | ✅ | Decoded UnmuteBeatMatchAudio handlers (docs/guitar_audio.md): the slip voice stops on a miss / own gem passed / opponent gem / early sustain release and resumes in sync on the next hit |
| P8 | Face-off guitar mix | ✅ | One guitar copy per player, panned left / right, −1.5 dB, −1.5 dB more in shared sections; replaces the interim rule. Autoplay now holds sustains to their end (self-test scores slightly higher) |
| P9 | Crowd | ✅ | Monkey / skull head cheats (heads 6 / 7) on 3D members and impostors; the original crowd animation (excitement split, shared LRU clips, random phases), crowd tempo events. The 3D members near the stage were already in |
| B72b, B74b | Garden ceiling lights, Toxic mountains | 🟡 | Re-checked in the decomp: the anims are on beat ticks and the vertex alpha fade is as the original code; still need a capture of the original |

## Enhancements (2026-10-22, fourteenth round)

| # | Item | Status | Notes |
|---|---|---|---|
| X53 | Enhanced Settings backdrop shows every option | ✅ | The backdrop goes through the same post chain as a song (AA, upscaling, bloom, cinematic colour, fog), the bokeh focuses as the shots do (2x / 1.3x the microphone distance), venue lights / shadows / particles applied live; camera pulled back (fov 0.7) |
| B75 | Rock Band guitar for Xbox 360 had its buttons mixed up | ✅ | SDL reports it as an Xbox 360 gamepad (not a guitar) named "...Guitar", so it fell into the PS2-adapter layout; and its whammy rests at -32768, read as fully pressed. Xbox-style guitars (type GUITAR, or vendor 1bad / 1430 on an Xbox gamepad type) use the Xbox guitar layout; axes are measured from their rest value at connect, with hysteresis. Detected on the real guitar; buttons not yet pressed by a person |
| X54 | GAMEPADS screen | ✅ [I] | Blue fret / click on the main menu's GAMEPADS. Controllers (and the keyboard) on the left with their player (1 / 2), live input, and on the right the 16 controls with their bindings, lit while held; select a control and press a button / move an axis to remap; Reset to Defaults. Table-driven bindings (input/input.cpp) with per-device overrides and players in controllers.json |
| X55 | AI Upscaling: FSR 3.1, DLSS 4 | ✅ [I] | A Vulkan side next to GL (render/vk_upscaler.*, volk), sharing colour / depth / motion vectors / output through GL_EXT_memory_object + semaphores; motion vectors from depth and the camera; FidelityFX SDK 1.1.4 (MIT, shaders pre-generated with tools/ffx_gen_shaders.py) and DLSS SDK 310.9.16 (preset K transformer). FSR 3.1 on any Vulkan GPU, Linux + Windows; DLSS 4 on NVIDIA RTX, Linux only (NGX ships MSVC-only libraries); with DLSS selected the game starts itself on the NVIDIA GPU of hybrid laptops. Status line under AI Upscaling. Characters have no motion vectors of their own |
| X56 | FSR 4, DLSS 5 | ❌ | FSR 4 removed from the list (DirectX 12 + RDNA 4 only; a saved "fsr4" loads as None). DLSS 5: later |

## Fifteenth round (2026-10-23)

| # | Item | Status | Notes |
|---|---|---|---|
| G1 | Hammer-ons / pull-offs | ✅ | Rules already matched the original (single fret, no shared fret with the previous note, ≤169 ticks, previous note hit; window -40/+160 ms). Fixed the gem look (coloured top ring only from the third gem on, previous gem the player's own). Autoplay hammers / pulls; selftest runs strum-only and HOPO passes (FINDINGS.md "HOPO re-check") |
| B76 | Controller defaults | ✅ | Guitar and keyboard defaults = the user's controllers.json; guitars have no Select / Back (green / red only), duplicate Select/Back-on-fret bindings dropped; Xbox guitar Star Power on Back (no tilt) |
| X57 | Unbound controls | ✅ | Delete while capturing, or the row's "x"; empty bindings persist |
| X58 | Keyboard "Frets also strum" | ✅ | Checkbox on the keyboard's GAMEPADS page (controllers.json frets_strum) |
| B77 | GAMEPADS screen | ✅ | P1 / P2 / -- labels inside the box; one device per player (swap on conflict); devices without a player drive menus as P1; keyboard Esc / Enter / arrows and mouse always work in menus. Player swap not confirmed on screen; buttons of the real guitar not pressed by a person |
| B78 | AI upscaling tanked FPS | ✅ | FSR 3.1 cost 12-15 ms on the AMD iGPU: FSR 3.1 now also relaunches on the NVIDIA GPU; upscaler outputs at screen resolution (scene at /1.5, /1.7, /2); 2-frame fence ring (no CPU stall); real FP16 FSR shaders; status shows resolutions and ms. GD01_GPULOG=1 |
| B79 | Quick Play list flashed career columns on exit | ✅ | The setlist exit script sets the provider back to career; the list now copies its rows on set_provider / refresh like the original |
| B80 | Held directions repeat | ✅ | Original: hold_ms then repeat_ms (1000/80 default, setlist 500/80, per-panel joypad override); keyboard, d-pad, strum. No acceleration in the original |
| B81 | Garden floor / Toxic cliffs / missing pieces in every venue | ✅ | Meshes have their own draw lists (e.g. arrow03.mesh = Garden stage top, cliff*.mesh = Toxic rock cliffs); scene_renderer now draws them (~200 meshes), skipping children a view already lists |
| B82 | Garden ceiling beams too fast | ✅ [I] | Driven by lighting.view on song ticks with the default 100-frame loop (~10 sweeps/s, also in the original code); slowed to 0.05 (one sweep per bar). A deliberate deviation; not frame-rate related |
| B83 | Toxic overbright in close-ups | ✅ | Our Bloom thresholded each channel at 0.55/0.45, so Toxic's teal floor (0.39,0.62,0.59) glowed when it filled the frame. Bloom now keys on luminance with a soft knee (0.62-0.9 / 0.55-0.85), keeping the light's colour |
| X59 | Fog: better Low / Full, new Super Fog | ✅ | Super Fog: volumetric height fog with shafts from each member's stage light, haze, glare |
| X60 | Color Grading, Film Grain, Chromatic Aberration, Light Streaks, Ambient Occlusion, Sharpening | ✅ | All off by default, on the backdrop too |
| X61 | Blur on Star Power (None / Light / Medium / High) | ✅ | Venue + band only, fades in 0.25 s / out 0.5 s. GD01_STARBLUR=1 |
| P10 | Android APK | ✅ [I] | See P1 |
| X62 | Always start on the dedicated GPU (Windows + Linux) | ✅ [I] | platform/gpu_select.*. Windows: exported NvOptimusEnablement / AmdPowerXpressRequestHighPerformance (the drivers fall back themselves). Linux: dedicated card from /sys/class/drm (NVIDIA, or AMD with >2 GB VRAM, not the boot GPU) → PRIME offload variables (NVIDIA driver) or DRI_PRIME=pci-... (Mesa). Fallback: graphics fail or land on llvmpipe → restart on the system GPU; a launch that died while starting graphics is caught next time; the failure is remembered in gpu_system until the driver or kernel changes. GD01_GPU=system / testfail. Replaces the upscaler-only switch |
| B84 | Hammer-ons failing in real play | ✅ | Rules re-verified against the raw instructions (window −40..+160 ms, needs a strum and a fret held since). PC difference fixed: the PS2 reads the pad once a frame, so moving from one fret to the next never showed "nothing held"; per-event input did, ending the chain. Now a gap shorter than a pad frame (16.7 ms) keeps it. New Cheats option "Hammer-ons & Pull-offs": Original / Forgiving (−100 ms early, no strum or held fret needed, wrong fret not locked out). GD01_HOPOLOG=1 logs each try; GD01_KEYS "s<song ms>:key" injects guitar input at song times |
| N1 | Next | ❌ | FSR 1 cost on the iGPU; character motion vectors; Android on a real phone (speed, START on touch); clang -Wunused warnings seen in the Android build |

## Labels to change (PC instead of PS2)

| # | Item | Status | Notes |
|---|---|---|---|
| L1 | Memory card texts ("memory card (8MB) for PlayStation 2 in MEMORY CARD slot 1", "Do not remove the memory card, reset, or switch off the console", free-space wording) | ✅ | Done in the twelfth round (ui/pc_strings.cpp) |
| L2 | Other console-specific labels (controller ports, "PlayStation 2" mentions, button names) | ❌ | Inventory the locale tokens that mention PS2 hardware |

## Missing from the original

| # | Item | Status | Notes |
|---|---|---|---|
| M1 | Boot / logo screens and the intro video (`ghintro.pss`) | ✅ | RedOctane / MTV / Harmonix logos, then the 20 s intro movie |
| M2 | The screen that shows how to use the guitar | ✅ | `guitar_help` in the first-boot flow (after the splash) |
| M3 | Saving and loading screens (memory card screens in `mem_card.dta`) | ✅ | The original mem_card.dta screens run on a native `memcard` mapped to `career.json`; each phase lasts at least 2 s |
| M4 | Profile screens (band name entry, profile select / delete) | ✅ | First boot: no save → create → splash → guitar help → name entry → autosave → main. Later boots: load → splash → band select |
| M5 | Lighting looks better in the original | 🟡 | Light cones/glows billboard to the camera (~280 meshes), transparent views drawn after the band, unlit materials use the raw vertex colour (fest grass), depth of field. Missing: extra material stages (glow/env layers), spotlight + floor glow, flares, particles |
| M6 | The crowd looks different: fewer people and no reaction to how well you play | 🟡 | The crowd's ok/great/clap/cheer clips were being dropped (their flags overlap the walk bits): now they react. Size starts full and changes on camera cuts like the original. Missing: 3D members near the stage, outfit swaps |

## Project questions

The explanation of the approach (reimplementation vs re-engineering, how close it can get), the end-product
layout for Windows / macOS / Linux, and three suggested git structures are in `docs/project_overview.md`.

## Extended features (after 100% parity)

| # | Feature | Status |
|---|---|---|
| X1 | An "extended settings" screen next to (or inside) Settings for everything below — Enhanced Settings exists (ninth round, X1a); FPS Counter is the first option | 🟡 |
| X2 | Several XInput and other USB guitar controllers | ❌ |
| X3 | Gamepads with the native Guitar Hero 1 inputs (no strum) | ❌ |
| X4 | Touch controls | ❌ |
| X5 | Controller configuration screen for 2 players | ❌ |
| X6 | 4K upscaling and native high-resolution rendering | ❌ |
| X7 | DLSS and FSR | ❌ |
| X8 | Advanced shaders (replacing the PS2-style ones) | ❌ |
| X9 | Replacement textures | ➖ | Dropped from the roadmap (1.1.3): the Enhanced Settings entry is removed |
| X10 | Modding, including new songs | ❌ |
| X11 | Ultra-wide monitors | ❌ |
| X12 | Higher frame rates | ❌ |
| X13 | Optimised settings for Steam Deck and other handhelds | ❌ |
| X14 | A custom character select screen | ❌ |
| X15 | Modding: new characters | ❌ |
| X16 | Modding: new guitars | ❌ |
| X17 | Modding: new songs | ❌ |
| X18 | "Master logger" (details to come from the user) | ❌ |
| X19 | Graphics quality options: anti-aliasing (MSAA, FXAA/SMAA), texture filtering (bilinear/trilinear/anisotropic) | ❌ |
| X20 | Mouse support (menus and settings) | ✅ |

## Future platforms

| # | Platform | Status | Notes |
|---|---|---|---|
| P1 | Android | ✅ [I] | Fifteenth round (P10): engine/android + engine/build_android.sh, APK in dist/android (arm64-v8a, x86_64, Android 7+); GLES 3.0 through gl.cpp; ISO picked with the system file picker and copied to app storage. Tested in an x86_64 emulator only |
| P2 | iOS | ❌ | SDL3 supports it; same needs plus Metal or GLES via ANGLE, and App Store rules on user-supplied content |

## UI plan (next)

The original menus are script-driven: `ghui/*.dta` define panels
(`new_gh_panel main (navigator ...) (focus ...) (enter ...) (SELECT_MSG ...)`)
that talk to native objects (`ui`, `game`, `campaign`, `synth`, `beatmatch`),
and each panel's art is a `ghui/<panel>.gh` scene (meshes, external
`ghui/image/gen/*.bmp_ps2` textures, LabelEx / ButtonEx / PictureEx / List
components, Text with fonts from `ghui/gen/resources.rnd_ps2`).

| Step | Item | Status |
|---|---|---|
| 1 | Load `.gh` panels in C++: external textures, UI classes (LabelEx, ButtonEx, PictureEx, List, Text, Font) | ✅ |
| 2 | Fonts: glyph table measured from the sheet's alpha, kerning, RndText layout / alignment / wrap / markup (`docs/fonts_and_text.md`) | ✅ |
| 3 | Styles from `ghui/config.dta` (text templates, fonts, per-state colours) and `ghui/eng/locale.dta` strings | ✅ |
| 4 | DTA script interpreter: preprocessor (#define / #include / #ifdef, macro splicing), variables, control flow, funcs, messages, handlers with parameter lists, `kDataUnhandled` | ✅ |
| 5 | UI framework: screens, panels (shared panels stay up), focus, navigators, routing order, auto-back, `goto_screen` / push / pop, helpbar, animated transitions | ✅ |
| 6 | Native objects (`game`, `campaign`, `meta`, `ui`, `song_provider`, stubs for `synth` / `memcard` / ...) | 🟡 | Enough for quickplay; career and profiles stubbed |
| 7 | Screens: main menu, options, quickplay song select (tiers, scrolling setlist), difficulty, loading → game | ✅ |
| 7b | Pause (resume / restart / settings / quit), lose (retry / new song / quit) through the original scripts; song end → `post_show` | ✅ |
| 7b2 | End screens: post_show → endgame (newspaper, stars, streak) → status / cash award / highscore (name entry) → complete | ✅ | |
| 7c | Song preview audio on the song list, menu music | ✅ |
| 7c2 | Sound effects: `.bnk`/`.nse`/`.seq` banks per context, software SPU2 (ADSR, pitch, pan law, 48 voices, echo / large-studio reverb), sequences, `play_sfx` / `synth play_sequence` / `play_game_*`, UI sounds | ✅ | docs/audio_sfx.md |
| 7f | Character / guitar select panels: CharsysPanel (6-model cache, menu idle clip, placer, view camera/env), GuitarDisplayPanel (spin, skin), GuitarSelectPanel, MultiCharSelPanel (docs/select_panels.md) | ✅ |
| 7g | Check the hero-select layout against the original (3D character overlaps the blurb column; follows the placer/camera data) | ❌ | Needs a reference capture |
| 7h | Menu character shadows (`*_shadow.lit`, `<c>_shadow.rnd`), the menu characters' default guitar (SG inferred) | ❌ | |
| 7i | Campaign unlocks (guitars, skins, characters per difficulty), store purchases | ❌ | Everything reports unlocked for now |
| 7d | PictureEx / PictureToggle / Slider / TextEntry components (character select, game settings sliders, profile names) | ❌ |
| 7e | Splash, boot, intro video, profile screens (skipped: the game starts at the main menu) | ❌ |
| 8 | Original in-game HUD (hud.rnd: score digits, multiplier, streak bulbs, rock meter, Star Power tube, messages, You Rock!), slide-in, song-title overlay timing, highway roll-out (extend_track.tnm), 10 s intro | ✅ | docs/endgame_and_hud.md |
| 9 | Career mode: campaign (tiers, unlocks, status, cash awards, per-difficulty careers), career setlist, store prices, high scores, JSON save (`career.json`) | ✅ | docs/career.md |
| 9b | Profile screens (splash, band name entry, profile select / delete); a first profile "Rocker" is created for now | ❌ | TextEntry is ready |
| 9c | Check by playing: status screen (tier complete), win_easy / win_game + Battle Axe, store purchases, multiplayer screens | 🟡 | Implemented from the scripts; not yet walked through |
| 9d | Tutorials, multiplayer face-off | ❌ | |

## Open

| Area | Item | Status | Notes |
|---|---|---|---|
| **UI** | Original menus (`ghui/*.gh` panels): career, store, multiplayer screens | ❌ | Main, options, quickplay done (see UI plan) |
| **UI** | Original in-game HUD: score, multiplier, rock meter, Star Power meter, streak messages | ❌ | Current HUD is a placeholder 2D overlay |
| **UI** | Results screen, pause menu, loading tips, career progression, saves | ❌ | |
| Crowd | Full 3D crowd members inside the `crowd_limitsNN` regions (CrowdChar) | ✅ | docs/crowd3d.md: the crowd instance pool (10/12/17 chars, head + outfit swap), regions by the game's inside test, switch_region per shot (crowd_region or auto score), set_sizes with the block lists, sphere cull; eyes look at a random member |
| Crowd | Head / outfit variety per instance, striped quad, `crowd_walk_offset`, facing | 🟡 | One head per archetype; same move for a whole block (as the original impostor) |
| Crowd | `hide_crowd` / `crowd_region` per shot | ✅ | |
| Crowd | Crowd audio (`crowd_v1_*`, cheering by excitement) | ✅ | Parity round P1 |
| Venue artifacts | User reported odd-looking things in some stages; find each root cause | 🟡 | Likely causes fixed: light cones were not billboarded (seen edge-on), beams/glows drawn under the band, fest grass black. Please re-check |
| Rendering | Vertex alpha now honoured (menus' vignettes, highway rails); window alpha masked so the window stays opaque | ✅ | Re-check venues for changes this caused |
| Venue artifacts | Environment-map stages (texgen 5), multi-stage materials | ❌ | Only stage 0 is drawn |
| Venue artifacts | Blend "Add" alpha handling, vertex alpha ignored | 🟡 | Approximations |
| Venue artifacts | MatAnim stage scale/rotation keys | ✅ | rows = scale · [[cos, sin], [−sin, cos]] (B48) |
| Venue artifacts | One big-club shot showed a flat brown frame | 🟡 | Probably a crowd-facing shot; recheck after the crowd |
| Camera | Shaky cam (`shaky` shots, `shaky_cam1.tnm`), `eyes` (look at camera), `crowd_region` | ✅ | docs/camera_endgame.md: shaky_cam1.tnm from arena fx.rnd moves the camera in path space and drifts the focus; `eyes` modes drive the guitarist's eyes. `crowd_region` waits for the 3D crowd members |
| Rendering | Extra material stages (39 materials: neon glows, reflection maps; texgen 5 env maps), gem2/star2 specular | ✅ | docs/material_stages.md: every stage with a blend is its own pass (DECAL unless MultipassLit), per-pass blend / depth / fog; texgen 5 reflection map in the vertex shader; Sphere (2) approximated [I] |
| Rendering | VenueSpotLight (spotlight tracks the guitarist) and floor glow | ✅ | docs/spotlight.md: spotlight01.lit aimed at the guitarist's feet every frame, floorspot_char.mesh ellipse with the venue's floorspot_glow.mat after the band. The shadow silhouette that cuts it is still missing |
| Walking | Native layout (2-beat simulation, speed correction, backward-integrated target, end hold on position) | ✅ | Parity round P5 |
| Highway | Deploy sparkle burst, cyan burn on held sustains, glow lines on tails, burn particles | ✅ | Glow lines: RndLine at z 0.16, the tight cyan line on unplayed star tails, 1.5× wide fret / star / bonus glow while sustaining |
| Display | Ultrawide: menus and HUD stay 16:9 centred; the venue fills the window up to 21:9 | ✅ | `Renderer::beginWide` |
| Highway | Intro: fret buttons pop and light up green → orange, then show the held frets (`pop_smasher`, `set_smasher_glowing`, `refresh_track_buttons`); hit pops; rims always coloured | ✅ | Through the original track_panel script |
| Highway | Win explosion (`track_panel explode`): the track breaks into pieces and splinters | ✅ | The explosion's particle systems now run and draw |
| UI | Walkthrough: first boot, profile, quickplay to the newspaper results, career (difficulty → character → guitar → status → autosave → venue) | ✅ | Store purchase and the final Battle Axe win not walked through yet (need cash / a finished career) |
| Venue effects | Particles (ParticleSys, ParticleSysAnim), dry ice, sparks | ✅ | docs/venue_fx.md: simulated per PsParticleSys; preserved static particles (basement / fest light strings); ambient tint, no fog. Script-switched emitters now run (venue scripts on the full interpreter) |
| Venue effects | Flares (Flare class), `chorus` flare sequence | ✅ | Z readback a frame late, brightness count stepped at 60 Hz, distance fade, s0→s1 size, `set_steps` pop and fade |
| Venue effects | In-venue video screens (`.ipu` movies) | ✅ | Not shown in the shipped game: every venue has use_video 0 and the karaoke_logo.mat they need exists in no scene (docs/video.md) |
| Venue effects | TRIGGERS track (`kick_drum`, `bass_hit`, speaker pumps) | ✅ | Notes 60 / 61 sent 90 / 50 ms early as arena messages |
| Venue effects | `hit_gem` reactions (`hit_guitar_amp`), funcs with arguments, `if` / `foreach` / variables in scripts | ✅ | Venue scripts run on the full interpreter (arena / game / char_sys natives, scene objects as script objects); `$slot` = the hit gem's lowest fret |
| Camera | Shaky cam, depth of field, native shot picker (`cam change_to_regular_shot`) details | ✅ | Parity round P6 |
| Camera | Win / lose / game-over shots and sequences | ✅ | game_over: FINAL_WIN at once (career final song), WIN after 1750 ms, LOSE (lose01, with lose_teleport as its pre-switch) at once; the director stops picking. Guitarist win waits for a transition, lose cuts in; WinFinal / LoseWalk; Star Power pose not while walking |
| Characters | Hand IK (two-bone, `frac` from the IK clip flags, align_quat, stretch), hand drivers (finger / strum clips from the gems via hand_finger_mapping / hand_strum_mapping, HandMap_/StrumMap_ events), `bone_fret` gliding between `spot_neck_fret<N>` from the ANIM track | ✅ | docs/guitarist_hands.md |
| Characters | Player-strum `strum_pluck_down` (PlayerMatcher swing, flag not traced) | ❌ | |
| Characters | Drummer / keyboard `realign`; root motion (`use_delta`) for walking | ✅ | Parity round P5 |
| Crowd | Impostor archetype facing (average block facing) and lighting environment (`DAT_003637e4`) | 🟡 | Identity facing, `crowd.env` |
| Characters | Face morphs (expressions, blinking), eyes, lip-sync | ✅ | docs/faces_shadows.md: hero poses follow the crowd excitement (one face changes at a time), singer mouth on T1 GEMS note 108 (257 ms early), eyes look-at with the 22° cone. The original never blinks |
| Characters | Walking between spots, win/lose animations, Star Power poses | ❌ | |
| Characters | Shadows (`*_shadow.rnd`), LOD 1 | ✅ | The shadow file skinned by the guitarist's bones, flattened along the spotlight onto root.z + 3 and drawn depth-only: it cuts the silhouette out of the floor glow. The crowd has no LODs |
| Gameplay | Multiplayer face-off / co-op | ❌ | |
| Gameplay | Practice mode, tutorials | ❌ | Tutorials hidden from the song list |
| Gameplay | Open RE questions (watcher +0x74 HOPO offset, phrase-miss rules, SOLO/PITCH tracks) | ✅ | Answered in FINDINGS.md (twelfth round H1); still open: whether the guitar audio mutes on a miss (BeatMatchAudio) |
| Audio | Crowd streams by excitement, crowd one-shots, claps in Star Power, miss / Star Power sounds via the original handler scripts | ✅ | arena_game.dta, beatmatch.dta, player.dta run in the UI interpreter |
| Audio | SPU2 reverb presets (approximated with a Freeverb-style room and a feedback delay), mix levels | 🟡 | Tune by ear against the PS2 |
| Audio | A/V calibration screen | ✅ | The original has none; the port's Audio / Video Calibration (X35) |
| Audio | Music slow-down to 0 over 2 s on a fail (`beatmatch set_music_speed`) | ✅ | |
| UI | The PS2 picture is 4:3: things placed just off-screen (meters before slide-in) show at the edges of our 16:9 window | ❌ | Decision: menus stay 4:3 and centred (pillarboxed on wide screens); gameplay may extend the scene wider, but only as far as it doesn't break or look off |
| Highway | Smasher pops / held glows (`pop_smasher`, `set_smasher_glowing`), win explosion (`explode`), rails glow | ✅ | docs/endgame_and_hud.md B2; miss pop added in the twelfth round |
| Video | Intro video (`ghintro.pss`, MPEG-2) and the bonus videos | ✅ | Own MPEG-2 decoder + PSS demuxer (engine/src/video, no third-party code; every frame of all four movies within 61.9 dB of ffmpeg). `meta play_movie` plays videos/<file> pillarboxed 4:3 with its 48 kHz PCM, the UI frozen, Green / X / Start skip |
| Platforms | Run-test the Windows build on Windows; build and test macOS | ❌ | |
| Quality | Playtest feedback on real hardware (timing feel, controllers) | ❌ | |
