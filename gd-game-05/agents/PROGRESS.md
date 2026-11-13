# gd-game-05 progress log

Rock Band (Xbox 360) -> `gd05`. Kickoff: `docs/spikes/GD-GAME-05_KICKOFF.md`
(wave 01, branch `feature/20261109-rockband1-wave01`). Newest entries at the
bottom. "Verified" lines quote the command and its output; "built only" and
"not tested" are said explicitly.

## 2026-11-12: the band as Rock Band moves it (wave G06 1, GD05-066/067/068)

- **Causes (measured, GD05_BANDCHECK=1; GD05_BAND_LEGACY=1 runs the band
  without the fixes):** none of RB's band drivers ran: RB's parsers are
  script objects ({new MidiParser guitar_fret ...}) the GH2 parser set did
  not read, so no fret / strum / drum hit clip ever played and the fret
  hand never moved along the neck; the CharWeightSetters used GH2's
  formula (RB's is offset + scale x flag weight: the hand IK was on when
  the main clip wanted the arm free); GH2's multi-target IK pulled the
  hands between hip and mouth (RB weighs targets by 144 / squared offset);
  the strum IK ran before the right hand's hip IK (RB's hand_iks order);
  draw_only ignored (the singer held mic, cowbell and tambourine); the
  face graph was looked up as char/male/singer.fac and RB's visemes are
  separate clips relative to Base.
- **Fixes:** D51 (RbMidiParser, the members' sinks, play modes, the main
  clip by flags, the drummer's main_add.drv), D52 (rig), D53 (face).
- **Verified** (`GD05_BANDCHECK=1 GD05_END_AT=60000 gd05 "$ISO" --song
  inbloom --autoplay --part guitar --difficulty 3`), before (legacy) ->
  after: guitarist arm bones 2.98 -> 1.25 x bind (11 of 11938 frames past
  1.2), bassist 2.77 -> 1.39 (89 frames); fret finger and pick to their
  targets p95 0.00 with the IK on (7178 / 10092 frames); drummer moves on
  150 -> 235 of 252 notes; singer's jaw moving on 0 -> 5155 of 11820
  frames; hands.grp draws every member -> mic.grp, mic 4.97 from the hand.
  GD05_FPS=60 vs 240: the same numbers within 1-5 frames, but the cap
  does not throttle the offscreen driver (about 200 fps both), so the
  frame-rate check is not conclusive; the drummer's worst stretch varies
  between runs (1.22 / 7.92). Shots (GD05_FACE_CAM per member, GD05_POST=0)
  looked at: guitarist fretting and strumming, singer singing into the mic.
- **Later the same day:** the drum hand clip sets key a different target
  bone per clip; the driver scaled down only the newest clip's channels, so
  the older ones summed for ever (targets 1e4 .. 1e5 units away). Older
  clips' channels now scale too (ClipDriver::poll, Bones::scaleDownOthers)
  [I]: drummer worst 7.92 -> 1.208 x bind (the ankle, 3849 of 14095 frames).
- **Left:** the drummer's ankle just past 1.2 (FAIL),
  guitar / bass hands past 1.2 x bind on 0.1% / 0.7% of frames (FAIL;
  RB's height / weight deform and CharIKScale are not applied),
  expression.drv faces, the guitarists' singalong, Overdrive / crowd
  energy reactions beyond the play modes, fail / win groups (win / lose
  played at the end as before).

## 2026-11-12: the guitar inside the launcher (GD05-061)

- **Owner:** "the strum is not working on the songs, there is no pause in
  the song." His 21:40 report (launcher 1.1.14, gd05 0.0.6, Vulkan): the
  guitar listed, background events on, keyboard focus on, and not one
  joystick / gamepad event in 53 s; only keys. Guitar Start never came
  either, hence no pause (his keys were 1..4 and the arrows, no Enter / Esc).
- **Ruled out, by tests through his launcher build (local_builds
  gdul-1.1.14, its libSDL3, the 0.0.6 module he ran, his enhanced.json /
  controllers.json):** one SDL in the process (the module has no SDL of its
  own: nm -D, ldd); the launcher closes its pads before the game and owns
  nothing while it runs; the window made again for Vulkan. Events written
  into the real guitar's own node /dev/input/event9 (evdev passes them to
  every reader as the device's) reach gd05 through the launcher: menus
  launched from the launcher's own UI with the guitar, then 120 / 120
  events (controllers-report: `4: 120 / 120`); a uinput guitar with his
  ids and GUID: 120 / 120; --inputcapture: 80 / 80. Not reproduced: no
  test fails on the current code. What is left unseen is below the
  kernel's input layer (the USB stream from the guitar / xpad while gd05
  runs): nothing here can strum the real guitar.
- **Test (new):** walk device mode `GD05_WALK_EVDEV=/dev/input/eventN`
  (written into that device) or `=uinput` (a kernel device that is his
  guitar), tools/uinput_guitar.c. Verified in the launcher (Vulkan, real
  node event9, his GUID seen by SDL): `GDUL_GAME_ARGS="--walktest
  rb_guitar_play"`: menus with the guitar, 87 / 87 hits, Start -> pause,
  green -> resume, sustain + whammy, Overdrive by tilt, a miss: OK;
  rb_guitar_none (his assignment, keyboard P1, guitar None): 87 / 87,
  pause / resume on the guitar: OK; rb_keys_play (Enter pause): OK.
  The uinput mode inside a walk is not tested here (the real guitar takes
  player 1 beside it); uinput devices are tested with the tool.
  rb_owner_guitar fails in the launcher at dev_unfocus (a launcher window
  cannot be hidden on Wayland: a test limit, not the game).
- **Diagnosis for his next run:** the controller report reads each pad's
  kernel device itself beside SDL: `Kernel device /dev/input/eventN: N
  events from the controller, SDL joystick events M`, and says which side
  lost them. Verified: idle guitar `0 events ... sent nothing to the
  system`; written events `80 events from the controller, SDL joystick
  events 80`. Owner steps: agents/OWNER_CHECK.md.

## Wave 06b (owner's retest, 2026-11-11 evening)

- **Real guitar still silent (GD05-058).** His 17:35 session (launcher,
  window made again for Vulkan): the report listed his guitar but no raw
  event at all, and 0 of 326 notes. Cause [C SDL 3.4,
  SDL_PrivateJoystickShouldIgnoreEvent]: with windows and no keyboard focus
  SDL drops every joystick / gamepad event. Proven: a window made again
  without focus got 0 events, 2 with SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS.
  Fix: the hint (D41). Walk rb_owner_guitar (his GUID via
  GD05_TEST_PAD_GUID, his controllers.json imported, the window without
  focus): 29 of 30 hit, 28 of 29 hat strums reach the song as Strum; with
  SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS=0 it fails 0 of 30 (his bug).
  The report and GD05_INPUTLOG=1 log every raw event and the game's
  decision; `--inputcapture` (also through the launcher: --play gd05
  --exit-after -- --inputcapture) writes input-capture.txt. His bindings
  already list Orange=LB, Whammy=Right Stick X, Gyro=Right Stick Y/Back.
- **Title round trip (GD05-059).** The port switched screens at once and
  faded after (the new screen flashed at full, then dimmed); going back the
  main menu vanished at once. RB exits the old panels and loads the new
  ones as the transition starts while the old screen shows; main_panel's
  load / exit fade the fx panel to black (D41). Walk rb_title_back: shots
  at 33..2500 ms both ways: the buttons slide out (button_exit.trg) under
  the fade, PRESS START flashes and fades, the new screen fades in.
- **Text colours (GD05-054).** Walk rb_text_colors: per text element the
  expected glyph-core colour from the data (preset / material colour,
  lights, ambient, font texel, D25 curve) vs the drawn pixels, fail > 8%
  darker. Worst before: tour_band_welcome title 22 vs 180 (a blackmask drawn
  over it: RB sorts draws, D43), now 180. Options audio 190/190, gameplay
  104/107, Create a Band 236/236 and 127/127, pause 217/217 and 153/162,
  help bar 215/218. Second gamma, sRGB, premultiplied alpha, double
  colour and mips ruled out by measurement. LOADING bright at once (owner
  decision D44, 28 -> 253).
- **In-song HUD (GD05-060).** trackpanel.milo's own elements replace the
  stand-in (D45): drawn boxes vs milo boxes within 0-4 px (solo: Overdrive
  meter 3, score 0, multiplier 2, rock meter 1; band: 13 boxes within 2),
  values = the Player's. Not done: the vocal HUD, head-to-head's panel,
  side angles, streak pop-ups, gamertags.
- **rb_keys_rebind**: a load flake (26 of 30 hits with eight walks at
  once); hit floor 80% as the device walks; walk save folders unique.

## 2026-11-12: sub-menu letters "grey" (GD05-062)

- Measured, not changed (D46). The owner's capture of RB's main menu
  (SOLO's sub-menu, QUICKPLAY focused) vs the port, 99th percentile luma
  per word box at 1200 x 675: QUICKPLAY focused RB 188 / port 183, SOLO
  TOUR normal RB 102 / port 97. Same through his launcher build (Vulkan,
  his enhanced.json / controllers.json / profile): 191 / 98. The labels'
  data colour (custom_colors 0x707070 / 0xd7d7d7), environ, light falloff
  (RB 0x827D6F00) and per-pixel shader match; every sub-menu (multi,
  training, options) also matches its data (rb_text_colors: 90..104
  normal, 179..198 focused). Walk rb_text_colors now also checks the sub-menu
  labels against RB's capture (refcolors:solo, refcolors:solo_2nd, 10%) and
  prints the main menu's modelled words: COMMUNITY 224 vs RB 191 (RB's word
  darker on its left), OPTIONS 226 / 215; others within 2%. Open: what the
  owner sees as grey needs a screenshot of his screen.

## 2026-11-12: the now bar under the HUD, LOADING graded as the icons (GD05-064 / 065)

- **Highway (GD05-064).** Causes [C data]: (1) the gem tracks never took
  trackpanel.milo's track_configuration (track_paneldir.dta update ->
  gem_track.dta set_pitch / set_fov / set_cam_pos / set_fade): the camera
  stayed tracksystem's own (0 -57.6 17.6, 27 degrees), so the HUD's
  Overdrive meter and multiplier sat across the smashers; (2) rig.grp's
  rig_movement.tnm (repeat_trans) stopped at its last key, y 100, a second
  in: the smashers and camera stayed 100 units ahead of the gems' now, so
  gems crossed the smashers 1.2 s before they were struck. Fix: 1_player_wide
  applied (pitch -44.5, camera 0 -81 18.7, fov 21, fade 60 + 40, top_y 105,
  y_per_second = top_y / view_time, the panel's 2.4 .. 1.2 s), the rig kept
  at its origin, TransAnim repeat_trans (D50). Check (quickplay,
  rb_guitar_play, rb_pad_play, band_quickplay): a gem in each lane at the
  now bar, then the HUD drawn over it: 39% of the gems' pixels covered
  before, 0% now (`rb now bar` log, counters rb_nowbar_*). Shots at
  20 / 30 / 45 s for guitar and drums: gems pass the smashers, the meter
  under them. Open: the surface's track_reflect scroll (it rode the rig).
- **LOADING (GD05-065).** D44's flat white replaced (owner: graded like the
  icons, D49): the word at its focus colour lit by instruments.lit as the
  four icons take it, plus loading.tnm's sweep light. rb_bright: word luma
  194, blue/red 0.69; icons 172 / 0.68 (D44: 232 white, 1.00); shots at
  1.2 / 1.6 / 2.0 s show the sweep crossing the word.
- Gate (own build, the w6 gate1 list): 29 walks OK, selftest, scenetest,
  scripttest, videotest, sfxtest OK.

## Wave 06 (owner's 0.0.6 report, 2026-11-11, 14:50 -> ~18:30, me + 4 subagents)

Each item: cause, fix, how verified.
- **Inputs (GD05-056).** Cause [C, our code + his controllers.json]: his
  guitar (1bad:0002) was saved at GAMEPADS None beside the keyboard as
  player 1; Input::actionsOf drops every in-song control of a device
  without a player but Start. Fix: a one-player song takes every device
  (D35, D34 extended). Also: a pad's / guitar's Start on the title went
  nowhere (splash_panel's {$this button_down kPad_X}: the panel's native
  ButtonDown was missing); a gamepad after the keyboard in the menus hit
  nothing (the watcher was fixed by the last device at song start; it now
  follows the device that plays, D38). Verified: walks rb_guitar_play (a
  virtual joystick that is his device: name, 1bad:0002, 11 buttons /
  6 axes / hat, his SDL mapping; menus with the guitar, 86/87 hits, pause /
  resume with Start / green, a sustain with the whammy, Overdrive by tilt,
  a miss), rb_guitar_none (his exact assignment: keyboard P1, guitar None),
  rb_pad_play (generic Xbox pad), rb_keys_play.
- **Sound (GD05-057).** Misses were silent: RB has no PlayerMatcher
  handlers; config/player.dta's player handler (miss $player_num $slot)
  plays miss_gtr.cue / miss_bass / drum misses [C data] (D38). The guitar
  stem mutes on a miss and comes back on hits (checked in rb_guitar_play).
- **Missing shaders at band start (GD05-055).** $world was never set:
  band_objects.dta start_shot could not find ProFilm_a.pp, so every shot
  from the band's first drew with no post (D37); pp_blend_task added;
  RB bloom gated off until ported (interim). The highway now rises at
  intro.trig. Audit: no failed program / pipeline / variant on local GL,
  core OpenGL 3, Vulkan (AMD, NVIDIA). Walk rb_band_start (fails with
  missing_shaders 3 without the fix).
- **Fret board (GD05-045).** tracksystem.milo's EventTriggers: hit flames,
  smasher glow, burn, miss, phrase captured, Overdrive deploy (D40);
  ParticleSys rev 36 grow before shrink. rb_fx_* counters in the device
  walks (86 hits, 0 without a flame; 1 deploy with its visual).
- **Dark items (GD05-054).** Band icons on the loading screen lit by their
  directory's own environ (D36, p97 44 -> 215); audio sliders drawn
  edge-on (slider.grp's turn, D39, p97 13 -> 150+); Calibrate opens.
  LOADING: RB's draw path matched in the XEX (D39): the word is dark grey
  until loading.tnm's 3 fps glint reaches it ~4.5 s in; the port's ~1 s
  loads never show it. Owner's choice. Walk rb_bright: luma floors on main,
  options, audio, solo, song list, difficulty, loading (icons, LOADING),
  pause; fails without the fixes.
- Left: RB bloom port, band overbright (GD05-042), RB's own HUD meters,
  solo / fill / BRE lanes, the `options` walk (four downs reach COMMUNITY,
  not in the gate), LOADING timing (owner).

## Final report, wave 05 (2026-11-10, about 4.5 h of agent time, 08:50 -> 13:20)

The owner's wave 05 list (agents/WAVE05_REQUIREMENTS.md). Ledger
(`python3 tools/parity.py docs/parity.md`): **61.00%** (unchanged: no
parity row is complete yet). gd05 **0.0.5** (CHANGELOG).

Each requirement: cause, fix, how verified.
- **2.1 Colour grading (GD05-030, partly).** Cause found [C]: RB sets a
  display gamma ramp from rnd (gamma 0.85) (config/default.dta; RB
  0x8246BD90 builds pow(i/256, gamma)); the port showed the frame linear.
  Fix: every frame through pow(c, 0.85) (setOutputGamma, local + core
  backends, screenshots ramped; D25). Second shared cause: prelit fonts
  ignored their text colour (port titles white, state colours lost) (D27).
  Verified: main menu label 214 -> 219, menus / venue brighter in shots;
  core opengl3 renders. Open: the main menu's focused orange still looks
  gold; the main menu labels are lit by main_buttons.env (lights 0.38 + 1.0).
- **2.2 Create a Band background (GD05-031): not fixed.** Shots added to
  band_tour (shot_band_create / shot_band_naming): the room from a camera
  looking up. OPEN_QUESTIONS wave 05 #1.
- **2.3 Menus disappearing (GD05-032): not reproduced** (rb_mouse goes
  back and forth through four sub-menus, all draw). Needs the owner's steps.
- **2.4 Esc x3 (GD05-033): fixed.** RB's main menu goes back to the title,
  which reset the count. Verified: new walk rb_esc3 (the game quits).
- **2.5 Stutter (GD05-034): measured, not fixed.** GD05_HITCHLOG=<ms>:
  a 2.4 s frame as game_screen starts (venue 0.5 s, track 0.1 s, band
  ~1.8 s), smaller ones on the song list (0.25 s). Rule added to
  docs/guidelines/usual-bugs.md (loader thread, bounded uploads, start on
  the loading screen). The loader thread is the next step.
- **3.1 Enhanced menu (GD05-035): fixed.** Orange titles / gold names (prelit
  colour), Kabel Bold as the bold menu font, RB's Back / Select help bar.
  Verified: screenshot next to gd02's Enhanced Options; layout and space
  unchanged.
- **3.2 GAMEPADS (GD05-036): done.** A pill button bottom left of the main
  menu (D26); the old help bar patch is gone (RB's main menu has none).
  Verified: rb_mouse checks it twice and clicks it (gamepads_screen); shot.
- **3.3 Mouse (GD05-037): fixed.** A click on a main menu button beside an
  open sub-menu now selects it (non-focus panel). Verified: rb_mouse (SOLO,
  MULTIPLAYER, TRAINING from the sub-menu, OPTIONS, Enhanced, GAMEPADS).
- **3.4 Instrument / difficulty paper (GD05-038): texture fixed.** Cause:
  the proxied seldiffpart_player.milo's own subdirectory
  (seldiffpart_common.milo, border.tex) was not loaded (GD05_TEXLOG showed
  "border.tex of border.mat: not found"). Verified: shot, the ornate dark
  paper with its silver frame; the wave is wave 04's cloth. Label colours
  follow color_presets.dta select_difficulty.
- **3.5 Loading icons / 3.6 fail screen (GD05-039 / 040): partly.** Both
  brighter through the gamma ramp; the fail screen's labels are still grey
  (lit by its panel lights); not compared item by item.
- **4 Drummer (GD05-041): fixed.** Cause [C data]: GH2's tempo mask 3 met
  RB's kLeftArm / kRightArm bits, deleting every drummer clip in slow songs
  (bind pose, collapsed). RB's bits 16/32/64, mask 112; sit fallback (D28).
  Verified: Creep (slow) logs no "no clip in group" (was 46k lines).
- **4 Overbright (GD05-042) / filters (GD05-043): not fixed.** RB's shaders
  read (Xenos disassembly, a read-only helper): lights uploaded unscaled,
  same lighting maths; RB adds the environ ColorXfm, a bloom mask in alpha
  and a final post sat(ColorXfm . (frame + bloom * intensity)) with no
  luminance map. A light now takes its colour owner's colour [C]. The white
  wash remains even with GD05_POST=0 (OPEN_QUESTIONS wave 05 #2).
- **4 Hands (GD05-044) / fret board (GD05-045): not started.** Plan: RB's
  MidiParser format ({new MidiParser ...}: guitar_fret, guitar_strum,
  guitar_fret_pos, drums_*), CompositeCharacter types guitar / drum /
  vocal so RB's enter scripts run (every member logs "no Character type
  'rb_*'"), then the dmidi / ikmidi sinks.

Verified by running (Linux, the final binary, one process at a time,
scratch XDG per run, dummy audio, the RB guitar ignored):
- Walk gate, all 21 OK: rb_boot, rb_owner_bugs, rb_pause_quit, rb_esc3
  (new), rb_mouse (new), quickplay, keys, quickplay_drums,
  quickplay_vocals, band_quickplay, solo_tour, score_duel, tug_of_war,
  rock_shop, creator, credits, rb_lose, rb_lose_quit, tutorials,
  rb_practice; band_tour OK (with the naming shots).
- Shots looked at: main menu (gamma, GAMEPADS button), Enhanced Options
  next to gd02's, the difficulty card, the fail screen, band naming,
  Creep / Tom Sawyer highway (drummer clips, the white wash still there).
- Packages: `gd-engine-launcher/scripts/build.sh all` exit 0, 0 compiler
  warnings; linux, steamdeck, windows, android each `complete (5 game
  module(s) with their libraries)`; local_builds keeps 1.1.12 and 1.1.13.
- Linux launcher `--play gd05 --exit-after`: `gd05 0.0.5 started`, exit 0.
- Wine (Proton Experimental, own prefix), Windows launcher, `--iso` from
  `.../Ação Música [RB]/Rock Bánd (USA).iso`, GDUL_GAME_QUIT_AFTER=25000:
  `gd05 0.0.5: 99 songs from disc image ...Ação Música [RB]/Rock Bánd
  (USA).iso`, `back from gd-game-05: exit code 0 after 25.1 s`.
- The core opengl3 backend renders with the gamma ramp (a song shot).

Built only: the vulkan / sdl3 backends' gamma pass, Steam Deck, Android.

What is left (next wave): the venue white wash (shader ColorXfm, bloom
mask, RB's final post, then the fog / additive layers; GD05-042 / 043);
the loader thread for the load stutter (GD05-034); RB's MidiParser and
character types for the hands (GD05-044); hit flames / Overdrive / solo /
fill / BRE (GD05-045); Create a Band's camera (GD05-031); the menus
disappearing (GD05-032, needs the steps); the main menu's focused orange
and the fail screen's grey labels (GD05-030 / 040).

Owner's checklist:
- Main menu: whites brighter? GAMEPADS bottom left, click it; Esc three
  times quits.
- Click SOLO, then MULTIPLAYER while SOLO's sub-menu is open.
- Options > Enhanced Options: orange titles, bold font, Back / Select.
- Quickplay > difficulty card: the ornate paper.
- A slow song (Creep, Dani California) on drums: the drummer at his kit.
- Tell us the steps for "menus disappearing" and how Create a Band's
  naming background looks on the original.

## Final report, wave 04 (2026-11-10, about 2.3 h of agent time, 05:55 -> 08:15)

The owner's goal: the menus even better (colours, delays, animations, the
cloth, the loading screen's band, the menus' strange behaviour) and a first
look at the fret board, all as the original. Ledger (`python3 tools/parity.py
docs/parity.md`): **61.00%** (unchanged: every row touched still misses a
part, below). gd05 **0.0.4** (CHANGELOG). Front end audit
(`docs/frontend.md`): **14 of 17 rows match** (was 13), 2 differ, 1 out of
scope. Fret board audit (`docs/highway.md`, new): **8 of 12 elements
match**, 1 differs, 3 missing.

The owner's points (what was wrong, the fix, how verified):
- **The cloth is missing in the song list, instrument and difficulty
  selection (GD05-022).** Wrong: nothing polled the RenderDirs' own
  CharTransCopy / CharHair, so the cloth never moved and its mesh was drawn
  unskinned; the song list was shown as a flat texture; the RenderDir's own
  transform was ignored (the song list's cloth sat too close, its first row
  under the title bar; the tour hub's menu ran off the screen). Fix: each
  RenderDir polls its CharTransCopy (RB 0x8233F6F8) and hair once a frame,
  skins its cloth (render/ui_cloth.cpp), takes its own RndTransformable
  from its record; the song list is drawn through cloth.cam (D20).
  Verified: walk `rb_owner_bugs` check `song_list_layout` (on the cloth,
  287 polls, 7 rows inside it); screenshots at several times (the cloth
  sways; Random Song right under the title bar; the difficulty card; the
  tour hub's menu cloth on screen).
- **The band members' icons on the loading screen are all wrong
  (GD05-026).** Wrong: the four icons (load_icon_*.milo, whose root is
  "loading") stood on one spot and never moved. Fix: a proxy object
  parented to its file's root hangs from its holder; a RndDir used as an
  anim sets its MatAnims (the texture swaps) (D22). Verified: walk check
  `loading_icons` (x 97 / 134 / 178 / 217); shots (the singer and the
  drummer swap frames; the guitarist's filter plays 0..0 and holds, as the
  data says).
- **Colours, delays, animations (GD05-027).** Wrong: every trigger anim
  ran from 0 to its end in end/30 s (no start, scale, delay or loop);
  scripts' (loop) / (delay) were ignored; buttons had no selecting
  animation. Fix: UITrigger anims as RndAnimatable::Animate (0x8221BE38),
  script animate as 0x8221C040, BandButton selecting_anim over
  select_frames (0x822D4848, 0x82270768) (D21). Verified: the walks; shots
  of the main menu during a selection (the button moves) and of the
  looping menu lights and loading animations.
- **Menu screens behave strangely (GD05-028).** Wrong: every screen change
  waited a fixed 0.5 s out + 0.5 s in (GH2's metacam) with the input
  dropped and nothing moving. RB waits only while a panel's
  block_transition trigger runs (UITrigger 0x82262B28 / 0x82262918,
  PanelDir 0x8225FEE0 / 0x8225FF60) (D21): most RB screens change at once,
  the dialogs wait for their exit. Also: the results' stars use
  scoring.dta's symbols (`*` .. `*****`, gold `66666`; it wrote `***__`,
  and `_` is not in the stars font) [I: the index rule]. New walk
  `rb_pause_quit` (pause QUIT, confirm with no.btn, yes, main menu, the song
  loaded once); gd04's black-frame probe (`probe_black` / `no_black`) in
  `rb_boot`; `--uitest` resolves a panel's names through its screens'
  panels.
- **The fret board (GD05-029).** Wrong: the strike line's smashers were not
  drawn (inline RndDirs in a group); the gems' materials were swapped for
  "slot" materials RB does not use (the green gems took prism_green.tex);
  the tails were flat strips sampling the dark part of their texture
  (invisible); the drum pads sat at -6 -2 2 6. Fix (D23): the smashers in
  their slot colours (gem_smasher.dta set_color through the PropAnims
  particle_slot_colors / smasher_glow); the gems' own materials; RB's tail
  builder (0x8281A8C8 / 0x8281C928 / 0x8281C4A0 / 0x8281BC48: 4.444-unit
  segments, the whammy wobble, alpha, end fade, the template's uv); the drum
  pads where the drum smashers stand (-7.5 -2.5 2.5 7.5). Verified:
  screenshots of guitar, bass and drums at several song times (Gimme
  Shelter, Tom Sawyer, (Don't Fear) The Reaper, Maps); tails with
  GD05_TAILTEST=1.

Verified by running (Linux, the final binary, one process at a time,
scratch XDG per run, dummy audio, the RB guitar ignored):
- Walk gate, all 19 OK: rb_boot (0 black frames; RB's own fx fade through
  black not counted), rb_owner_bugs (song_list_layout on the cloth,
  loading_icons), rb_pause_quit (new), quickplay, keys, quickplay_drums,
  quickplay_vocals, band_quickplay, solo_tour, band_tour, score_duel,
  tug_of_war, rock_shop, creator, credits, rb_lose, rb_lose_quit,
  tutorials, rb_practice. (score_duel / tug_of_war first failed on
  natives the faster transitions now reach: band_to_slot, get_result,
  max_delta_points, player_config score; added [I], then OK.)
- `--scenetest`, `--scripttest`, `--videotest` (14 videos, 44189 frames),
  `--sfxtest`, `--selftest` (all charts fully hit): exit 0. `--uitest`
  still FAILED on script names other panels own (68 lines; 8 more now
  found through the screens' panels) as in wave 03.
- Packages: `gd-engine-launcher/scripts/build.sh all --force` exit 0, every
  module 0 compiler warnings on every platform; every bundle `complete (5
  game module(s) with their libraries)`: linux, steamdeck, windows,
  android-arm64.
- Linux launcher: `--play gd05 --iso ... --exit-after`,
  GDUL_GAME_QUIT_AFTER=25000 -> `gd05 0.0.4 started`, `back from
  gd-game-05: exit code 0 after 25.1 s`.
- Wine (Proton Experimental, own prefix), the Windows launcher, same
  command -> `gd05 0.0.4 started`, `back from gd-game-05: exit code 0 after
  25.1 s`.

Built only (not run): Steam Deck, Android.

What is left:
- Fret board: the tails' glow line; hit flames, sparks and the smasher
  press (the smashers' EventTriggers, particles, BandMeshLaunchers);
  Overdrive / solo / fill / BRE highway visuals; the stand-in HUD's
  "OVERDRIVE READY" over the strike line.
- Front end: the audio sliders' pods; the confirm dialogs' dim YES / NO
  (lit by splash.grp's environ [I]); the creator's 3D preview; the credits
  and the band lobby screen by screen; `--uitest` still lists script names
  of shared handlers (rank0.lbl ...) other panels own.

## For the owner to play (wave 04)
- Solo > Quickplay: the song list hangs on its cloth, turned a little, and
  sways; Random Song is the first row, right under the title bar; the
  difficulty card sways too.
- Pick a song: on the loading screen the band (guitarist, singer, drummer,
  bassist) stands side by side at the bottom right; the singer and the
  drummer move.
- Move around the menus: screens change at once (no second of waiting);
  choose a main menu button and watch it move; the pause menu's buttons
  come in one after another.
- In a song: five coloured smashers on the strike line (drums: red,
  yellow, blue, green pads and the orange kick); sustains are thin glowing
  lines; hold one and move the whammy: it wobbles and brightens.
- Finish a song with stars: the results show them (gold for five on
  Expert as the font's gold star).
- Esc > QUIT > Yes: back to the main menu.
- Solo > Tour: the hub's menu cloth is fully on screen.

## Final report, wave 03 (2026-11-10, about 2.9 h of agent time, 00:26 -> 03:20)

The owner's goal: the start of the game and the menus as the original.
The screen-by-screen audit is `docs/frontend.md` (made with the new
`tools/frontend.py`, the scripts' screen graph): of its 17 rows (the flow
from boot through Quickplay, a song, the results and back, plus the options
screens) **13 match, 3 differ, 1 is out of scope** (game_screen's HUD),
none missing. Ledger (`python3 tools/parity.py docs/parity.md`):
**61.00%** (was 60; Front end 9.0 / 14). gd05 **0.0.3** (CHANGELOG),
game-info.yaml completeness 0.61.

The owner's bugs (cause, fix, how verified):
- **People screaming at the start (GD05-023).** Cause: the port's Enhanced
  Options backdrop, a venue posed at boot before the intro movie, sent its
  crowd an excitement change, which played the venue's crowd_upto_good
  cheer. Fix: the backdrop's world is silent (D16). RB's own menu ambience
  (meta.dta: the shell music and shell_crowd.cue, muffled on the song list)
  plays as the scripts say. Verified: GD05_SFXLOG=1 boot logs (no
  crowd_upto_good, the shell crowd only); walks `rb_boot` and
  `rb_owner_bugs` check `counter_max:menu_venue_sounds:0`.
- **Key 2 in Quickplay loads another song (GD05-024).** Key 2 is the red
  fret, which the menus read as back (kPad_Tri). Cause: the port kept
  GH2's "back to the screen we came from", so on game_screen, before the
  first note, red went back to the loading screens, which loaded the song
  again. Fix: RB's UIScreen (0x8225AEE8) goes back only to a screen's own
  `(back ...)`; `goto_screen`'s third argument plays the backward
  transition (D15). Verified: walk `rb_owner_bugs` presses keyboard 2 eight
  times as the song starts, `counter_max:song_loads:1`. Keys audit in
  docs/frontend.md (developer keys need an environment variable or the
  Enhanced developer switch, off by default).

Also changed (each verified by screenshots I looked at, and the walks):
- The menus' background concert video (movie_panel, rb_video*.bik,
  MoviePanel's pick rule 0x828557E8); the intro at its own 16:9.
- Main menu: the metal words lit by their group's environ (no dark first
  letters), the golden focused pill, the lower third's skull wave.
- RenderDir panels drawn as RB draws them (TexRenderers into textures,
  their own camera): the difficulty screen (koi banner, the player card:
  guitar / bass, then the difficulty, READY), the shop and tour rooms. The
  song list's texture is shown flat under the title bar [I] (D17).
- UI inline proxies use their files and draw under their holders (the
  slots, the results' player panel); slots' scripts in their own
  directory; text colours from color_presets.dta; RB labels' own text
  fields, markup and fit types.
- The help bar shows (actions, fret icons), takes clicks, and carries
  GAMEPADS on the main menu (D19); loading tips as RB's RandomContext;
  the lose screen's % complete; dialogs' messages; attract mode.

Verified by running (Linux, this build, one process at a time, scratch
XDG, dummy audio, the RB guitar ignored):
- Gate, every walk OK: rb_boot, rb_owner_bugs, quickplay, keys,
  quickplay_drums, quickplay_vocals, band_quickplay, solo_tour, band_tour,
  score_duel, tug_of_war, rock_shop, creator, credits, rb_lose,
  rb_lose_quit, tutorials, rb_practice.
- `--selftest` (all charts fully hit), `--scenetest`, `--scripttest`,
  `--videotest`, `--sfxtest`: exit 0. `--uitest` still lists the 42
  component names of wave 01 (names other panels of the same screen own;
  docs/frontend.md).
- Attract mode: a 75 s idle run (movies skipped) goes title > attract >
  title > attract (log).
- Packages: `gd-engine-launcher/scripts/build.sh all --force` exit 0, every
  module 0 compiler warnings; every bundle `complete (5 game module(s) with
  their libraries)`: linux, steamdeck, windows, android-arm64.
- Linux launcher: `--play gd05 --iso ... --exit-after`,
  GDUL_GAME_QUIT_AFTER=25000 -> `gd05 0.0.3 started`, `back from
  gd-game-05: exit code 0 after 25.1 s`.

Built only (not run): Windows (no Wine run this wave), Steam Deck, Android.

What is left (docs/frontend.md):
- The song list on its swaying cloth through its camera (GD05-022;
  GD05_SONGLIST_3D=1 shows the camera's framing, which hides the top rows).
- The results' stars label, the audio options' slider pods.
- QUIT from the pause screen, the credits (behind Community), the tours'
  screens and the band lobby (D10) screen by screen; the in-game HUD.

## For the owner to play (wave 03)
- Start the game: the intro fills the screen; on the title and the menus a
  concert video plays behind; you hear the menu music and a calm crowd, no
  cheering.
- Wait 30 s on the title: the attract videos play, then the title again.
- Main menu: whole metal words, the gold focused pill; moving plays a
  click; GAMEPADS at the bottom right (yellow fret or a click).
- Solo > Quickplay: the list on its cloth, headings in gold, the yellow
  highlight; the help bar (Back / Change Sorting / Choose Song) takes
  clicks; Back returns to the Solo menu.
- Pick a song: the difficulty screen's koi banner and card, GUITAR / BASS
  first, then the difficulty, READY; the loading tip is a real sentence.
- In the song, press 2 (red) right at the start: the song keeps going.
- Esc: the pause screen (Return to Game / Select). Fail a song: the %
  complete; QUIT: the confirm dialog's message is readable.
- The results: your panel (difficulty, % hit, streak); then NEW SONG /
  REPLAY / EXIT.

## Final report, wave 02 (2026-11-09, about 5.2 h of agent time, 18:05 -> 23:15)

Completeness from the ledger (docs/parity.md, `python3 tools/parity.py
docs/parity.md`): **60.00%** (wave start 21.0%). By area:
- Front end 8.0 / 14
- Port 1.0 / 2
- Gameplay 16.25 / 22
- Band 9.0 / 10
- HUD 4.25 / 10
- Stage 7.0 / 14
- Modes 9.0 / 18
- Custom 3.0 / 6
- Saves 1.5 / 3
- Video 1.0 / 1

game-info.yaml completeness 0.6, gd05 0.0.2 (CHANGELOG).

Verified by running (Linux, this build; one process at a time, scratch
XDG, dummy audio, RB guitar ignored):
- The gate: every walk OK.
  - quickplay, quickplay_drums, quickplay_vocals, band_quickplay
    (rerun OK after the venue band-name label fix);
  - solo_tour, band_tour, score_duel, tug_of_war;
  - rock_shop, creator, credits;
  - rb_lose (retry), rb_lose_quit (QUIT to the main menu);
  - tutorials (guitar; drums and vocals with GD05_INSTRUMENT: voice-overs play);
  - rb_practice (section 3..4 at 90%: the clock starts a measure before).
- `--selftest` (all charts fully hit), `--scenetest`, `--scripttest`,
  `--videotest`, `--sfxtest`: all exit 0.
- Overdrive's deploy and depleted cues play (quickplay with
  GD05_AUTODEPLOY, logged).
- The camera picks follow the RB rules (GD05_SHOTLOG). The shots' band
  targets pose and place the band (screenshots in small_club_09). The
  crowd shows in arena_07's far shot. The created character is on stage
  in its gigs (log).
- Packages: `gd-engine-launcher/scripts/build.sh all` exit 0. Every bundle
  is `complete (5 game module(s) with their libraries)`: linux, steamdeck,
  windows, android-arm64.
- Linux launcher: `--play gd05 --iso ... --exit-after` with
  GDUL_GAME_QUIT_AFTER=25000 -> `gd05 0.0.2 started`,
  `back from gd-game-05: exit code 0 after 25.1 s`.
- Wine (Proton Experimental, own prefix), same command -> `back from
  gd-game-05: exit code 0 after 25.1 s`, 99 songs.

Built only (not run by me):
- The real microphone path (the null device under the dummy driver
  stands in), Android's RECORD_AUDIO prompt, the RB drum kit as a device,
  2 to 4 real pads.
- Steam Deck and Android packages.

Approximate:
- RB's HUD is the port's simple one (stand-in).
- The help bar does not show (D12).
- Some venues are too bright.
- Small clubs' crowd cards lie flat.
- Main menu: the first letter of each button word is dark.
- The song list cloth is flat (GD05-022) and its rows crowd together.
- The difficulty screen's labels overlap.
- Creator: no 3D preview, physique or colours.
- The band lobby is a stand-in (D10).
- Tutorials: finishing a lesson by playing is unchecked.
- Camera: no intro chains or BFTB shots.

Missing:
- The Drum Trainer (not reached).
- A/V calibration's own screens wired to the judge.
- Leaderboards, online and DLC (GD05-013, out of scope).

## For the owner to play (wave 02)
- Solo > Quickplay > a song with the RB guitar: frets, strum, tilt or
  Select deploys Overdrive (listen for the deploy sound), solos show their %.
- GAMEPADS: Player 2 Instrument Drums with the RB drum kit; play a song on
  drums; hit the fill's crash with Overdrive ready.
- The USB mic: Player Vocals in GAMEPADS (or Vocals on the difficulty
  screen); sing a song and watch the pitch arrow; tap the mic in tambourine
  sections; shout in a freestyle section to deploy.
- Android: first start asks for the microphone.
- Multiplayer > Band > Quickplay with 2 to 4 pads; let one player fail and
  save them with Overdrive.
- Multiplayer > Head to Head: Score Duel and Tug of War.
- Solo Tour and Band World Tour: create a character (name, hair, head),
  play a gig, check your character is on stage, buy clothes in the Rock
  Shop, quit and restart (the save).
- Training: the tutorials for guitar, drums and vocals (do the lessons
  finish when played?), Practice by section and speed.
- Fail a song: Retry, New Song, Practice, Quit.
- Options > Credits; Options > Enhanced Options.
- Look: are venues too bright? Does the band look right in closeups?

## Final report, wave 01 (2026-11-09, about 4.3 h of agent time)

Works (verified on Linux, fresh builds):
- Solo Quickplay from RB's own menus: intro movie, title, main menu, Solo,
  Quickplay, song list, difficulty (RB's player slots), loading, the song
  in a venue, results, back to the song list; pause / resume.
  `gd05 "$ISO" --walktest quickplay` -> `walktest quickplay: OK`;
  `--walktest keys` (Enter / arrows / Esc through SDL events) -> `OK`.
- Guitar, bass, drums on RB's highway (gems, colours, beat lines, now bar,
  tails). Drums via GH2's joypad watcher (DECISIONS D6).
- Every VENUES venue loads (WorldInstance decoded), camera cuts from the
  VENUE track, lights.
- `--selftest` exit 0 (`all charts fully hit`, `all input checks passed`,
  vocals through the pitch detector 2132 / 2136 Great+); `--scenetest`
  `0 typed objects failed, 0 files failed`; `--scripttest` `OK`;
  `--videotest` OK; `--sfxtest` OK (374 moggs); core
  `gdcore_audio_input_selftest` OK.
- Enhanced Options reachable from RB's options menu.
- Packages: `gd-engine-launcher/scripts/build.sh all --force` exit 0; every
  bundle `complete (5 game module(s) ...)`: linux, steamdeck, windows,
  android-arm64 (android rebuilt after a warning fix: 0 warnings).
- Linux launcher: `--play gd05 --iso ... --exit-after` with
  GDUL_GAME_QUIT_AFTER=25000 -> exit 0 (99 songs, intro movie playing).
- Wine (Proton Experimental, own prefix): same -> `back from gd-game-05:
  exit code 0 after 25.1 s`, the Bink intro decoded.

Approximate [I]: venue lighting brightness (no reference), shot choice
(random per category), song list cloth (inner camera, no render-to-texture,
GD05-022), drum judging (joypad watcher), track colours swap, sustain tails
(flat strips), help bar, debug text HUD instead of RB's HUD.

Missing: band on stage (M6), vocals in game (D7: guitar instead), RB's HUD,
Overdrive / solo / fill / BRE looks, tours / practice / training /
multiplayer / Rock Shop / saves (M7), drum kit and mic recognition, 2-4
players, `--uitest` (42 component names open, docs/audit.md), PC wording.

Confidence per OS: Linux 🟢 (walks + launcher run), Windows 🟡 (Wine run
25 s, no walk), Steam Deck 🟡 (built and verified, not run), Android 🔴
(built and verified, not run), macOS 🔴 (not built here).

## For the owner to play (wave 01)
- Launcher > Rock Band > Solo > Quickplay > a song > Guitar > a difficulty;
  play with the keyboard, a gamepad or the RB guitar; Esc pauses.
- The bass (choose Bass on the difficulty screen) and the drums
  (`--part drum`, an RB drum kit as a gamepad: LB kick, B Y X A pads).
- Does the RB guitar's fret / strum map feel right? Are the venues too
  bright or dark against the real game?
- Options > Enhanced Options.

## Overall estimate

Written at the start (2026-11-09 13:51), from the measured times of
gd-game-03 (gd02 copied, ~4 h to its final report) and gd-game-04 (new
engine, M0..M9 in ~6.5 h). Rock Band reuses GH2's engine family (ARK v3 and
the dtb cipher read as is by gd-game-02's tools on the first look) but is
much wider: four instruments, four players, vocals with a new microphone
interface in gd-engine-core, Bink, the character creator and the Rock Shop.

| Milestone | Estimate |
|---|---|
| M0 set-up (copy, rename, rip, Ghidra) | 0.75 h |
| M1 data layer (RB Milo versions, vocals files, Bink) | 1.5 h |
| M2 songs, audio, gameplay core (4 parts, Overdrive, band rules) | 2.5 h |
| M3 scripts, UI, audit | 2 h |
| M4 highways and HUD (4 tracks incl. vocals) | 2 h |
| M5 venues | 1.5 h |
| M6 band and creator | 2 h |
| M7 modes, shop, saves | 1.5 h |
| M8 port extras (4 players, drums, vocals in GAMEPADS; mic) | 1 h |
| M9 launcher module, packages | 1 h |
| M10 gaps | at most 3 h (this run's limit) |

Total M0..M9: about 12-14 h of agent time. Parity with every rule
re-confirmed in RB's XEX is longer: owner waves follow, as gd02..04 needed.

## For the owner to play

Real-device checks only the owner can make (kept up to date):
- (filled in as features land)

## 2026-11-09 14:05, M0 set-up (gate passed)

Done:
- gd-game-02's engine, tools and build scaffolding copied and renamed
  (DECISIONS D1); version 0.0.1 (`engine/CMakeLists.txt`, `game-info.yaml`
  with `iso_signatures` title ID 45410829); FFmpeg's decoder list now Bink +
  XMA (D2); README, CHANGELOG, agent-instructions, CONTEXT.md, DECISIONS,
  OPEN_QUESTIONS; backlog rows GD05-002..013, GDCORE-011.
- Disc ripped (`extracted/disc`, 5.2 GB), archive unpacked (4,588 files),
  all 291 `.dtb` decoded to text (0 failed).
- Ghidra: `default.xex` imported (XEXLoaderWV), FixXexFunctions (41,175
  functions), exported (41,175 decompiled, 13,449 strings, 77,980 data refs).
- First M1 finding: RB's milo payload is big-endian (docs/formats.md); the
  engine and `milo.py` read the order per file.

Verified:
- `ninja -C engine/build/linux` -> 0 warnings, 0 errors (grep of the log).
- `engine/build_windows.sh` -> `Ready: dist/gd05-windows-x64`, 0 warnings
  (before the byte-order change; rebuilt at the M1 gate).
- `GD05_LOG=... timeout 8 engine/build/linux/gd05 "$ISO"` ->
  `archive: gen/main.hdr version 3, 4588 files in 2 parts (disc image ...)`
  then `game script is missing 'head'` (RB's game.dta: M2/M3 work).
- `python3 -I tools/milo.py scan extracted/ark` -> `3445 files parsed, 6 failed, 461 inline sub-directories`.
- `gd05 "$ISO" --scenetest` -> `3406 milo files parsed, 45 failed` (class
  versions: M1).
- `python3 .github/scripts/repo_guard.py` -> `0 problem(s)`.
- `pgrep -fa gd05` -> nothing running.

Not tested yet: Android build, local_build.sh packages (M9).

Next: M1, the data layer (class versions, vocals files, Bink). Time used:
~15 min; estimate unchanged (12-14 h for M0..M9).

## 2026-11-09 14:58, M1 data layer (gate passed); M2 started

Done (M1):
- Milo: RB's payload is big-endian per file; inline sub-directories follow
  their owner's end marker; relative paths from the source folder; every
  class on the disc has a known layout. Two research agents read the Load
  routines in the XEX and checked Python decoders over the whole disc
  (render / UI: 93,480 instances exact; characters / synth: 43,177 exact;
  both re-run by me); I ported the layouts into the C++ decoders (Load
  addresses in the code and in docs/formats.md).
- `.mogg` version 11 decrypted (AES-128 counter mode; own AES; DECISIONS D3,
  OPEN_QUESTIONS Q3). Bink in FFmpeg; `--videotest` added.
- RB's sound samples: XMA, big-endian PCM16, empty placeholders.

Verified (fresh binary, `engine/build/linux/gd05 "$ISO" --<test>`, with the
CONTEXT.md environment):
- `--scenetest` -> `3451 milo files parsed, 0 failed, 467 inline sub-directories`,
  `proxy directories: 364 resolve to an archive file, 0 do not`,
  `ui text chain: 1656 components resolve ..., 0 do not`,
  `scene test passed: 0 typed objects failed, 0 files failed` (exit 0).
- `--videotest` -> `videotest: 14 videos, 44189 frames decoded; 0 failed`, `videotest: OK`.
- `--sfxtest` -> `moggs: 374 files (374 version 11, decrypted), 416833 Ogg pages walked to each file's end, 22300 s of audio ...; 0 failed`, `sfxtest: OK` (exit 0).
- `--selftest` (M2 so far) -> `291 .dtb scripts decoded, 0 failed`;
  `PART GUITAR 308 charts, 138360 gems, 0 not fully hit`;
  `PART BASS 244 charts, 141962 gems, 0 not fully hit`;
  `PART DRUMS 584 charts, 499457 drum notes (with and without kick), 0 not fully hit`;
  `PART VOCALS 252 charts, 8648 phrases rated Awesome, 0 not fully hit`;
  `all charts fully hit` (exit 0).
- `ninja -C engine/build/linux` and `engine/build_windows.sh` -> 0 warnings.
- `python3 -I tools/milo.py scan extracted/ark` -> `3451 files parsed, 0 failed, 467 inline sub-directories`.
- `pgrep -fa gd05` -> nothing running.

Gate notes (honest):
- 10,128 objects of 19 classes are kept raw in C++ (layouts known and
  verified in Python; ported when used: WorldInstance and PropAnim at M5).
- `tools/tex.py` does not read RB's big-endian inline bitmap headers yet
  (the C++ does: every Tex decodes).
- The selftest's GH2 judge regression checks (GH2's 50-point values, co-op,
  face-off) are reported, not counted; the PC-wording check finds 38 RB
  strings that name the Xbox 360 or a console (M3); GH2's script sound
  list is informational until M3's audit.

M2 state: the four judges play every chart; the rules in FINDINGS.md are
from the data [C data] but the judges' logic for RB is [I] (guitar / bass
are GH2's judge, drums and vocals new). Band mechanics (band meter, saves,
Unison, Big Rock Endings, solo awards, stars) are not built yet.

Next: M3 (RB's front end through its own scripts), then M4 highways. Time
used: ~1 h 10 min of wall clock; the estimate stays 12-14 h for M0..M9.

## 2026-11-09 15:45, M2 core done, M3 menus reach the song (in progress)

Done since 14:58:
- gd-engine-core: the microphone capture interface `gd::core::audio::Input`
  (sdl3, miniaudio) with `gdcore_audio_input_selftest` (GDCORE-011).
- Vocals: our own pitch detector; `--selftest` sings every song's Expert
  vocals with a synthesised voice through it.
- Launcher: `build_module_05` and verify_bundle's rule; gd05's FFmpeg is
  built with the suffix `_gd05` (no clash with gd02's in the APK).
- Front end (RB's own scripts, config/band.dta): intro movie, title, main
  menu (RB's 3D buttons), Solo, Quickplay, the song list (RB song provider,
  rank tiers [I]), the difficulty screen (RB's player slots), the loading
  screens, game_screen with the song playing. Xbox profile / save flow
  skipped (DECISIONS D4).

Verified:
- `engine/build/linux/gdcore_audio_input_selftest` -> `sdl3: ... frames pushed in real time (24576 in 0.5 s)`, `miniaudio: ... (24000 in 0.5 s)`, `input selftest: OK`.
- `gd05 "$ISO" --selftest` -> `all charts fully hit`, `vocals through the pitch detector: 58 songs, 2132 of 2136 phrases rated Great or better` (exit 0).
- `gd05 "$ISO" --videotest` -> `videotest: OK` (with the `_gd05` FFmpeg).
- `gd05 "$ISO" --song greengrass --autoplay` with a screenshot at 50 s:
  the song plays on gd02's debug highway (score, x4, Overdrive ready).
- GD05_KEYS walk boot -> game_screen (screenshots in agents/scratch, not committed).

Not done / approximate: RB's track and HUD (M4: the debug highway is
used), RB's song list cloth (RenderDir drawn flat, GD05-022), label wrapping,
the band modes' slots (one local player), characters as objects (M6).

## 2026-11-09 16:50, M3 gate partly, M4 / M5 started

Done since 15:45:
- M3: RB's player slots, PreloadPanel, game_won / game_outro and
  band_ui_sink sync_screen, endgame stats kept after the song, quasiquote,
  Set objects, panel iterate / set_frame, stand-in characters, RB menu
  music list, the in-game sfx bank.
- M4: RB's highway from trackpanel.milo / tracksystem.milo (gem sets from
  track_graphics.dtb, slot-coloured gems, beat lines, smasher plate); bass
  and drums playable (`--part`, the slot's track type); drums through the
  joypad watcher (D6); vocals fall back to the guitar (D7).
- M5: RB's venues load and draw (VENUES list, WorldInstance as proxies,
  mixed light kinds); camera cuts from the VENUE track by shot category.

Verified (this hour, fresh build):
- `gd05 "$ISO" --walktest quickplay` -> `walktest quickplay: OK` (screens: intro_movie_screen > ... > game_screen > endgame_waiting_screen > qp_endgame_screen).
- `gd05 "$ISO" --scripttest` -> `scripttest: OK`.
- `gd05 "$ISO" --scenetest` -> `scene test passed: 0 typed objects failed, 0 files failed` (WorldInstance 2683 + 286 own decoded).
- `gd05 "$ISO" --selftest` (devices ignored) -> exit 0, `all charts fully hit`.
- `gd05 "$ISO" --uitest` -> FAILED: 42 script component names not in the milos (open, GD05-005).
- Screenshots (not committed): guitar and drums on RB's track, small_club_01 with cuts.

Approximate / missing: no band on stage yet (M6), RB's HUD (debug HUD
text), sustain tails, Overdrive / solo / fill looks, venue lighting
calibration (no reference), RB's director rules (random shot per category).

Next: M6 band on stage (prefab characters), then M9 launcher module and
packages, then M10 by impact. Time used: 3.0 h of the 12-14 h estimate.

## 2026-11-09 22:00, wave 02 gate 1 (ledger 50.5%)

Done since wave 01 (each in the commits of this branch):
- The parity ledger (docs/parity.md, tools/parity.py): 21.0% at the start.
- The band on stage: premade characters (characters.dtb) in the venue's
  four slots, built from their outfit pieces and instruments, CPU skinning.
- Vocals in the game: the mic (core input), GD05_VOCALS_FILE, the vocal
  track; RB's vocal scoring, scream deploy and tambourine from the XEX.
- RB's track through the menus (it was the debug highway there in wave 01).
- Band play for up to four (tracks side by side, vocals on top), RB's band
  multiplier, fail-out and saves, Unison; solos, fills, Big Rock Endings,
  stars from the XEX; RB's guitar watcher rules (HOPO chain, sustains) and
  drum watcher (a gem per pad).
- Meta game: profile JSON, Solo Tour, Band World Tour, the creator's maker,
  Band Quickplay and head-to-head through the menus (lobby stand-in, D10),
  the Rock Shop, options, lose / retry, practice's sections.
- Venue lights read as RB stores them; WorldDir material overrides.
- PC wording of RB's strings; four players in the input layer; Drums and
  Vocals instruments in GAMEPADS (built only).

Verified (fresh build, CONTEXT.md environment):
- walks: quickplay, quickplay_drums, quickplay_vocals, band_quickplay,
  solo_tour, band_tour, score_duel, tug_of_war, rock_shop, rb_lose -> OK
  (`--walktest <w>` -> `walktest <w>: OK`).
- `--scenetest` -> `scene test passed: 0 typed objects failed, 0 files failed`;
  `--scripttest` -> OK; `--videotest` -> OK; `--sfxtest` -> OK.
- `--selftest` -> exit 0: all charts fully hit; 232 of 232 solos perfect;
  176 endings banked; 292 drum charts through the game's drum judge;
  tambourine 10180 of 10180; vocals through the pitch detector 2136 of 2136;
  PC wording 0 strings left.
- screen sweep (`--walktest screens`): 235 of 252 screens reached (before
  the later natives).

Built only: GAMEPADS Drums / Vocals rows, four-player input slots, the
Android RECORD_AUDIO request (launcher APK not rebuilt yet).

Next: toward 60%: tutorials, crowd, creator appearance, camera, HUD,
animation, the help bar, the real-device checklist. Time used: about 4 h.

## Wave 05 running log (2026-11-10)

- GD05-030 (colours): RB sets a display gamma ramp from rnd (gamma 0.85)
  (config/default.dta; RB 0x8246BD90 builds pow(i/256, gamma) and sets it
  as the gamma ramp) [C]. The port now shows every frame through pow(c,
  0.85) (device setOutputGamma; local and core backends; screenshots
  ramped). Verified: main menu label max 214 -> 219, venue / menus
  brighter (shots); core opengl3 run renders.
- GD05-033 (Esc x3): RB's main menu Back goes to the title, which reset
  the count. Fixed; verified by the new walk rb_esc3 (the game quits).

## 2026-11-10, GD05-030 follow-up: menu colours (owner's reference)

- Causes [C]: (1) D25's pow(c, 0.85) ignored the XDK's composition of the
  ramp with its sRGB-to-TV (BT.709) table (RB 0x8253DFB8 / 0x8253DE28):
  on an HDTV the net is about identity, so the port was too bright;
  (2) RB's per-pixel shaders (ark/xbox_shaders) tint the light sum by the
  material colour once and take the highlight from the first light only;
  the port squared the colour and added every light's highlight (the
  focused button's orange went tan, labels grey); (3) BandHighlight was
  not drawn (no orange bar under QUICKPLAY). D25 replaced, D29, D30.
- Measured (1200x675 boxes; ref / before / after): SOLO top (79,21,4) /
  (185,140,113) / (115,45,7); SOLO middle (108,72,13) / (172,140,91) /
  (135,88,15); QUICKPLAY bar (113,57,5) / (11,12,11) / (177,91,0); labels
  (97th pct) QUICKPLAY 182 / 162 / 182, SOLO TOUR 97 / 55 / 96.
- Verified: gate (agents/scratch/menu/gate.sh) all 21 walks OK, selftest,
  scenetest, scripttest, videotest, sfxtest OK. Background video: Bink,
  tv range, BT.601 limited conversion already right (Y avg 56-58).
- Left: the lit orange is still ~1.3x brighter than the reference; the
  highlight's orange frame (normal.mat) shows over the pill's rim; the
  fail screen and band naming labels not compared.

## 2026-11-10, wave 05 retest (GD05-046..053)

- 046 highlight box: resource/highlight.milo's highlight.mesh (normal.mat,
  an orange frame wider than the button) is hidden in the file; the port
  drew it. Only shown meshes now; the bar (highlight_sub.mesh) sits under
  the rim as in ref.png.
- 047 Enhanced Options button: each options row's background is a
  submenu*.mesh parented to its button in options.milo; the port's copy
  now gets a copy of it, one resting step under Calibrate.
- 048 GAMEPADS: shown and clickable on every screen with main_panel.
- 049 grey labels [C]: panels without an environ drew at GH2's 0.3
  ambient; RB's UIManager::Init (0x82259408) makes a UI environ with
  (ui (env (ambient 1 1 1))) (D32). Shots of 18 screens look right; the
  band hub's list stays dark (its own environ, not compared).
- 050 keyboard: with a guitar plugged in the keyboard is player 2 and its
  Start did not pause / resume; one-player songs take every device as
  player 1 (D34). Walks rb_keys_play, rb_keys_rebind.
- 051 song list: RB's BandLabel fit (0x8226DDA8 / 0x82214FE8) measures
  height in the font's cell aspect and steps the size by 0.2: names 30 ->
  28; accented kerning pairs (sign-extended keys) found. Left: the
  heading row overlapping the highlight bar.
- 052 disappearing menus: walk rb_fuzz (seed 1) failed at step 27: after
  Back to the title and Start main_panel had no buttons (its ui_exit_back
  slid them out; RB reloads the panel fresh). enterPanel forgets exit
  poses (D33); 2000 steps pass (327 checks).
- 053 Enhanced backdrop: the venue's own band shots (D31) instead of the
  port's close-up inside the drum kit.

## 2026-11-12, song list letter sizes (GD05-063)

- Owner: "the list of musics have wrong sizes for the letters". The new
  walk rb_songlist_text measures every label text the song list draws into
  its RenderDir's texture (480 x 552 through meta.cam; popup_bg fills it,
  x 6..475, y 17..535) against the size RB gives it from the data (the
  label's text_size through RB's kFitJust loop, the font's cell aspect,
  the list's scale 2), in texture pixels; over 5% fails. RB's UIList rev 9
  fields named from its SyncProperty (RB 0x822697E8): spacing 33,
  arrow_offset, arrow_showing, arrow_only_scroll, arrows_on_highlight,
  fade_offset, display_num 7 [C].
- Data vs drawn (cell height in texture px): song names (list.lbl, fakir,
  text_size 30, kFitJust 415 x 30, leading 0.75): RB size 28.0, 36.7 /
  36.6; headings (heading.lbl, 35, 440 x 50, leading 0.70): 35.0, 45.8 /
  45.6; line widths equal to 0.1 px; on the screen through the cloth a
  44..50 px cell (the sway). 21 texts at 3 scroll positions, all within
  0.3%.
- Found [C]: RB's UILabel update (RB 0x8226E0A0) adds the label's kerning
  to the font's (font +0x1044, put back after the layout); the port
  replaced it. Fakir's and the list labels' kerning are 0, so the list is
  unchanged; labels with their own kerning on fonts with one change (D48).
- Found: the list never scrolled. drawList drew rows 0..6 only; past the
  7th song the highlight left the cloth. A window of display_num rows now
  follows the selection [I] (D48).
- Left: where RB puts a sort heading (the port lifts heading.lbl to
  list.lbl's height in its own slot [I]); a capture of RB's song list
  would settle it. The HUD's labels (rb_hud_view.cpp) still replace the
  kerning.

## 2026-11-12, the world's look, film grain, pause, frame rate (GD05-069)

- **Proof (new walk rb_look):** the world alone (GD05_TRACK / GD05_HUD 0)
  in 30 fixed shots (GD05_FORCE_SHOT=#N: the venue's BandCamShots by name,
  every 7th), luma mean / contrast / saturation / grain at 640x360 against
  the disc's own gameplay movies (videos/entersandman, tomsawyer,
  learntofly, gowiththeflow, wanteddeadoralive .bik, 289 frames, title
  cards out, through the display curve): luma 69.3, contrast 55.4, grain
  1.97; fails outside 15% (grain 40%). Also the pause rule and the world's
  pace.
- **Causes found and fixed:** (1) RB's post-process was GH2's: the grain
  was added (+0.075 on every pixel, white noise) where RB overlays two
  noise_mono.tex layers (darkens), no flicker (ColorXfm x 0.8..0.9 under
  ProFilm), GH2's bright pass for bloom (D54). (2) small_club_02's
  TexRenderer directories (tattoos_a, vehicles) drew over the whole venue:
  the white paisley over walls, floor and band (D56). (3) particles ignored
  the environ's ambient (use_environ) and screen_aspect (D56).
- **Numbers, fixed shots, luma before -> after (movies 69.3):**
  small_club_02 158.8 -> 113.4, big_club_03 185.3 -> 174.8, theater_02
  87.4 -> 63.1 (-9%: within), arena_04 166.0 -> 153.5. Grain 4.22 / 3.07 /
  5.10 / 3.74 -> 2.74 / 1.34 / 3.04 / 1.77 (movies 1.97). Contrast within
  15% in big_club_03 (58.9) only. Post off on small_club_02: 132.3 (RB's
  post now darkens, as the original's); spotlights off: 113.6 (not the
  cause in these shots).
- **Left (not fixed, measured):** big_club_03 with every particle system
  off is 51.6: its stage fog (stage_fog.part, smoke_02.mat, additive, 23
  particles of 200..500 units) fills close-ups white; RB's particle maths
  read the same as the port's (particles shader, draw 0x82476498, init
  0x821D33E8, pre-spawn 0x821D8FE8: emission per frame, sizes, colours)
  so the difference is still open. Band members lit near white under the
  presets' lights (band.env ambient 0.34 0.78 0.80, light 1.19 1.79 3.0)
  in close-ups. rb_look fails on luma in three of four venues.
- **Pause:** pause.milo's black_bg.mesh (black, alpha 0.7, SrcAlpha, the
  whole screen) is the rule; the port already draws it: the background at
  1.03 of the rule (0.3 of the colour before the display curve; median of
  32400 pixels) (D57). It looked bright because the world was.
- **Frame rate (finding):** RB paces the world by the PostProc's
  emulate_fps (D55): ProFilm (the band shots) 24, the default 30: the
  venue and the band are drawn on 2 of every 5 frames at 60 Hz (3:2), the
  track and HUD every frame. Done as the default; measured: the world drawn
  24.0 times a second while frames run at 235. Backlog GD05-075
  (Enhancement): separate world / highway rates.
- Verified: rb_look on four venues (numbers above); the gd05 gate (see the
  final report).

## 2026-11-12: the highway's draw order and Overdrive look, the crowd (wave G06 3 / 4, GD05-070 / GD05-071)

- **Causes (measured, App::rbProbes; GD05_HWY_LEGACY=1 / GD05_CROWD_LEGACY=1
  run the code before):** the HUD and the track ran every trigger anim at
  30 frames a second whatever its rate: be_ready.trig's be_color_pulse.anim
  (loop 0..1 at k1_fpb, one pulse a beat) pulsed 30 times a second, the
  Overdrive meter flickered (D59). The HUD drew each directory in file
  order: the streak meter's "Nx" tag (multi_meter_anim.grp, draw order 1)
  over its dial (streak_meter.grp, order 2) (D60). The track drew the beat
  lines after the rails and now bar, every smasher whole (bottom, top,
  effects) in gem_smashers.grp's order and its effects again after the
  gems, not smasher_plate.dta's setup_draworder (D61); star tails kept the
  slot's coloured texture (TailBuilder: tail_bonus.mat) (D61). No crowd
  clip ever played (play_group_flags a silent stub, intro_start never sent,
  no BandCamShot crowd part): the archetypes lay on the floor in their bind
  pose and their cards were empty (D58).
- **Numbers (before -> after):** Overdrive meter, back-and-forth luma jumps
  in 240 frames 101 -> 0 (jumps 198 -> 5..7, one a beat); track parts out
  of the data's order 5 of 11 -> 0; HUD pairs out of draw order 1 -> 0 (in
  14..16 directories); star tails not on tail_bonus.mat 2837 of 2837 -> 0,
  their chroma 100% -> 2..3%; crowd pixels (the audience alone, a crowd
  shot) arena_01 0 -> 1,415,355, small_club_02 0 -> 1,556,614; every arena,
  club and big club > 1.1 M px; the nine theaters 0 (their WorldCrowds are
  hidden in the data: GD05-076).
- **Verified:** `--walktest rb_highway` OK (legacy: FAIL meter_flickers is
  101); `--walktest rb_crowd` OK (legacy: FAIL crowd_clips is 0);
  `agents/scratch/hwy6/crowd_sweep.sh` (41 venues, `--song`); shots looked
  at: arena_01 behind the band (the audience standing, moving), the
  multiplier tag under the dial; the star tails' colour only measured.
- **Gate** (`agents/scratch/hwy6/gate.sh`: gate1.sh's 29 walks + rb_highway
  + rb_crowd + the five tests, 7 at a time, each with its own
  XDG_DATA_HOME, while another agent's gate ran): 34 of 36 OK; rb_highway
  failed on meter_flickers 2 under the load (slow frames: a slope step
  counted as a jump; the threshold now adds the pulse's slope over the
  step's beats, then OK; legacy 62), rb_pad_play failed twice under load
  (20 of 30 gems hit) and passed alone (86 of 87).

## 2026-11-12: loads, the song list, stutters, locked songs, Overdrive, FX (wave G06 6, GD05-073)

- **Loads (measured alone, timestamps on the log, GD05_UILOG=1; profile:
  gdb stack samples):** the hot spot was raw deflate (the band's clip
  milos, puff.c's bit-at-a-time decoder). Table-driven inflater + the
  milo's blocks side by side + moggs deciphered in parallel (D62). Boot
  to the title (movies skipped) 3.25 s -> 1.50 s (the Enhanced backdrop's
  venue and band 2.54 s -> 0.85 s); the song's load frame (on the last
  loading tip) 2.29 s -> 0.97 s (0.74..0.86 s with the GPU warm-up);
  difficulty confirmed -> playing 4.9 s -> 3.4 s, of which 2.33 s is
  loading.milo's block_transition animation (RB's) and 1.0 s on the way
  into the song list is main.dta's fx fade (25 frames, RB's). Inflater:
  0 differences over the ark's 13,236 milo blocks (2.37 GB), 2.8x faster
  on one thread; the selftest's `milo loads` line compares it with the
  reference decoder (ratio <= 0.5).
- **Song list:** RB's rows are 33 + song_select_gap 30 apart; the port had
  33, so the 65-unit highlight covered the next row or heading (D63). Also
  the artist (details.lbl) under each name and "<group> (<n> Songs)".
  rb_songlist_highlight: names under the highlight 2 -> 1 at every step.
- **Stutters:** a preview's mogg read, deciphered and opened on the frame
  (100..240 ms) -> on a thread (MusicLoader); textures / meshes uploaded
  on first draw (23 in the song's first 12 s) -> at the load's end
  (warmWorld, 1,634 in ~120 ms). rb_stutter: previews' window max / median
  frame 74.8 -> 2.0 (want <= 4.0); song_gpu_uploads 23 -> 0; the song's
  first 240 frames' longest 4.9x the median (the world's 24 fps frames,
  D55).
- **Locked songs:** quick play listed all 58 songs; RB lists 10 on a new
  profile (D64). rb_qp_locked: locked listed 48 -> 0, Random Song picks
  of locked songs 247 / 300 -> 0. The tours' unlocks: GD05-077.
- **Overdrive:** keyboard Tab and a pad's Back deployed; the guitar's tilt
  never did when its rest was read before its first report (bound either
  way from the rest; D65); the drummer's fill never deployed under the
  autoplay. rb_od_*: refused at 45 %, deployed at 60 %, drain = beats / 32
  within 1 % on every path (keys, pad, guitar tilt and Back, drums, vocals).
- **FX pass:** 661 EventTriggers in track/gen's milos; the crowd meter's
  deploy / stop / failed triggers added (D66); the rest in GD05-078 /
  GD05-079 (fills, BRE, unison, spotlight sparkles, super streak, crowd
  meter warning / glow / peak arrows / sparkler particles).
- **Gate** (agents/scratch/gen6/gate.sh: gate1.sh's walks with GD05-073's
  and GD05-072's, rb_highway, rb_crowd, the five tests, 7 at a time, each
  its own XDG_DATA_HOME, another agent's tests running too): 38 of 47 OK
  at first. Alone: rb_lose (a segfault under load) and rb_stutter (55 >
  40 under load) passed; quickplay, band_quickplay, rb_pad_play,
  rb_guitar_play, rb_guitar_none (and rb_highway's meter_flickers 1) failed
  because the lock rule changed the songs their list positions pick: they
  now take the long cheat (every song, as they were written) and pass
  alone. Selftest exit 0 (its 7 GH2 rule FAIL lines are the same before the
  wave); `milo loads` ratio 0.11.

## 2026-11-12: career, the Rock Shop and the creator (wave G06 5, GD05-072)

- **At the start:** the walks solo_tour, band_tour, rock_shop and creator
  passed, but none checked money, the save or the stage. Measured with new
  walks (`--walktest rb_career`, `rb_career_band`, `rb_shop_items`; every
  check run against the code before with GD05_WALK_KEEP_GOING=1):
  - worked: a new character's 100 (config/tour.dta default_money); a
    shirt bought at the catalog's price (100 - 99 = 1) and worn; a solo
    gig's pay (5 a star) and its outfit; the character on stage in its
    clothes; money, clothes and stars through a restart.
  - broken: the creator's skin list had no rows (BandSwatch unknown,
    set_colors a stub, the outfit panel took the list over) and every
    creator choice stored the character object (skin 0); the attitude list
    was empty (set_data ignored), the attitude a symbol the scripts never
    matched; ONEOFF hints marked "0" (or the next hint, unseen) as seen;
    `char_cache request` unhandled when a saved character is picked; the
    Band World Tour paid nobody (the lobby gave premade characters, the
    band kept its own money, 0 in the shop: "no band or no member
    character"); guitars cost nothing (the models hold the price) and the
    instrument list never got the focus (iterate UIList skipped BandLists);
    the save lost the look, the tour's place and the band's motto /
    members.
- **Fixed (D67..D70):** the reward (reward_per_star x each song's stars,
  RB 0x823EEAE8) paid to every local member's character (RB 0x823EE4A0),
  a band's money its members' (RB 0x823EDAA0), max_money; the creator's
  BandSwatch (RB 0x822D27F0), palettes, attitude list, setters; the
  instrument models (RB 0x82859E08); the save's new keys; the hint advance.
- **Expected (data) vs measured (fresh build):**

  | check | expected | measured |
  |---|---|---|
  | new character's cash | 100 (tour.dta default_money) | 100 |
  | creator attitude, one down from Rocker | 2048 (kGenreSpazz) | 2048 (before: 0) |
  | creator skin, two downs | 2 | 2 (before: 0) |
  | shirt flannelcoat_checked | 100 - 99 = 1, worn | 1, worn |
  | on stage | flannelcoat_checked, bandanna, attitude 2048 | 1 / 1 / 1 member |
  | solo gig tier1_1/small_club/song1, 1 star | 1 + 5 x 1 = 6, worntshirt_line6 won | 6, won |
  | after the restart | 6, skin 2, bandanna, flannel, 2048, hints welcome_guitar | the same (before: hints "0,welcome_guitar") |
  | band: money at the hub | the member's 100 | 100 (before: no member) |
  | band gig small_club/song1, 2 stars | 100 + 5 x 2 = 110, band 110 | 110 / 110 |
  | band after a restart | 110, 2 stars | 110 / 110, 2 |
  | guitar 5120 with 100 | refused, 100 kept | shop_error_money_screen, 100 (before: never reached) |
  | guitar 5120 after cheat_add_money 2000 | 2100 - 1299 = 801, 5120_paint | 801, 5120_paint, on stage, kept after a restart |

- **Gate** (gate1.sh's 31 walks + the five tests, 6 at a time, each with
  its own XDG_DATA_HOME, `agents/scratch/car6/gate_par.sh`): 25 of 31
  walks OK, 5 of 5 tests OK. Failed: quickplay (rb_hud_box_err_px 6: my
  `char_cache compression_done` answer moved the HUD probe's moment;
  taken out, then quickplay OK at 3), band_quickplay (rb_hud_box_err_px
  25..27: also with every career change taken out, after bec1ed4's
  quick play song list), rb_pad_play / rb_guitar_play / rb_guitar_none
  ("no sustain held with the whammy": also without the career changes),
  rb_keys_rebind (OK alone).
- **Final gate** (fresh build at 5c1672b, gate1.sh's 41 walks incl.
  rb_shop_items and the other tasks' new ones, + five tests, 6 at a time):
  40 of 41 walks OK, 5 of 5 tests OK; rb_stutter (GD05-073's frame-time
  walk) failed under the parallel load (frame_spike song_begin 259 > 150).
  The earlier band_quickplay / pad / guitar failures did not come back.
- **Not done:** the real band lobby (each player picks or makes a
  character; the stand-in takes the profile's first of the instrument);
  the instruments' finishes (group_select_screen) and drum / mic shops
  not walked; tattoos, make-up and face paint (patches: no render to
  texture yet, GD05-022); weight / height sliders not checked; the
  characters are not drawn in the menus; the tours' [I] rules (song
  order by rank, unlocks, fans / buzz) are still the wave 02 guesses.

## 2026-11-12: Rock Band on the shared core only, Vulkan by default (G06 7, GD05-074)

- **Done (D71, docs/backend.md):** every draw through gd-engine-core
  (Vulkan default, then SDL3, then OpenGL3, each with its window made again),
  every sound through a core output (miniaudio default, then the others);
  gd05's OpenGL device, GL loader, EGL HDR surface and SDL audio path
  deleted; True HDR on the Vulkan HDR swapchain; XDVDFS, random-access
  files, the pref folder and the CPU BCn decoder are the core's. No core
  change was needed, so no other game's code or binary changed. Remaining
  (not in the core yet, or not moved): input / MIDI (core `inputs/` is a
  plan), the UI, the settings / save file IO and the desktop entry.
- **Pixels** (fixed-clock song shots, 1920 x 1080, NVIDIA RTX 3060 on the
  offscreen driver, `agents/scratch/be6/shots.sh` + `cmp.py`, git-ignored:
  inbloom at 20 s / 45 s, wanteddeadoralive drums at 30 s, blitzkriegbop
  vocals at 25 s): two runs of one binary 0 differing pixels; **new default
  (Vulkan) vs Vulkan before: 0 px in all four**; **core OpenGL3 now vs the
  old default local: 0 px in all four**; Vulkan vs the old default local
  (the change a player sees): 0.03 % / 0.00 % / 0.51 % / 0.41 % of the
  pixels (mean |delta| <= 0.003, max 56 / 49 / 34 / 66: single pixels at
  mesh edges and in the blurs, as gd02's 6b results).
  CPU BCn decode (GD05_DECODE_TEXTURES=1) vs the GPU's own: gd05's decoder
  mean |delta| 0.20-0.60, core bcdec 0.13-0.40 (closer in every shot).
- **Fallbacks** (`VK_DRIVER_FILES=/nonexistent.json`): `vulkan did not
  start` -> `sdl3 did not start` -> `making the window again for opengl3`,
  the song plays. Audio `GD05_AUDIO_BACKEND=bogus` -> miniaudio; openal and
  faudio each play (null devices). True HDR saved: Vulkan says why it is
  Emulated (no HDR colour space on this surface), opengl3 "not on the
  opengl3 graphics backend yet". **Not tested:** the RGBA16F HDR output on
  a real HDR display (none here); Windows, Steam Deck, Android, macOS
  builds; the launcher module on a real display.
- **Gate on the default backends** (Vulkan + miniaudio, headless offscreen
  on the NVIDIA GPU; `agents/scratch/be6/gate.sh`: gate1.sh's 40 walks +
  rb_highway, rb_crowd + selftest, scenetest, scripttest, videotest,
  sfxtest, 6 in parallel, each its own XDG_DATA_HOME): 46 of 47 OK;
  rb_lose timed out once on qp_endgame_screen under load, then passed 4 of
  4 (alone, 3 in parallel; the crash under load is GD05-086, fixed since).
  `enhanced` / `enhanced_backends` (GH2 walks, not in the gate) fail the
  same way before and after (they never reach options_screen on RB's menu).
- **Other games** (no core change, so their code is as before; rebuilt in
  their own build/be6 dirs, Linux): gd01 `tools/walks.sh` 24 / 24 PASS;
  gd02 `--selftest`, `--scenetest`, walks quickplay / lose / pause /
  attract OK; gd03 `tools/run_tests.sh` (tests + 15 walks) 25 / 25 rc=0;
  gd04 `--formattest` all parse, `--shadertest` 5 / 5, headless smoke shot
  saved; gd10 walks boot and quickplay passed, `tools/fuzz_seeds.sh` 13 / 13,
  `--selftest` 31 FAILED (its expected 31). The launcher builds
  (gd-engine-launcher/build/be6; build.sh not run, no packages).

## 2026-11-12: G06 follow-ups (fix6: rb_lose crash, band HUD box, band stretch, venue light, theater crowds)

- **rb_lose segfault (GD05-086, D72):** the core of the 11-12 gate
  (gd05_gate6) and three new ones show RbHudView::draw -> reset ->
  runTasks -> animFrames -> TempoMap::msToTick on freed memory: on a
  retry the HUD's reset ran the panel's reset anims on the last song's
  TempoMap. Not MusicLoader nor the parallel inflate (the core's
  RandomAccessFile reads with pread or a lock). Fixed: reset takes the new
  song's tempo. rb_lose 9 times, 6 at a time, MALLOC_PERTURB_=165: 5291eb3
  3 segfaults, fixed 0 (and 9 / 9 again in the final gate).
- **band_quickplay HUD box (GD05-087, D73):** the expected box of
  star_power_01 took in the additive sun glow be_ready.trig shows when a
  member's Overdrive is ready (41..442 vs the meter's 71..409); the drawn
  glow fades out inside its quad. Glows now only bound the box; the walk
  sets Overdrive 60 % before the probe: before 17 px (33 in the gen6 gate),
  after 0 px (3_player_wide: drawn 67..415 within the glow's 40..441).
- **Band stretch (GD05-066 / 067, D74):** RB's CharIKHand elbow clamp
  [-1, 1] (RB 0x8232EB48) instead of GH2's [-0.985, 0.88]; every band IK
  hand has stretch TRUE. GD05_BANDCHECK, inbloom 60 s:

  | bone | before (5291eb3) | after |
  |---|---|---|
  | guitar L-hand | 1.243 x bind, 11 of 14,165 frames past 1.2 | 1.111..1.120, 0 |
  | bass L-hand | 1.389, 103 | 1.159..1.190, 0 |
  | drummer L-ankle | 1.208, 3,982 of 14,451 | 1.153, 0 |
  | drummer L-hand (high fps runs) | 2.0..253.9 on 4 of 9 runs | 1.51 / 2.40 on 2 of 5 runs (GD05-088) |

  CharIKScale, pelvis IK and height / weight deform were not the cause of
  the guitar / bass / ankle cases. The drummer's left hand: its kit
  targets' local offsets grow without bound (6e4..3e5 units) at high
  frame rates; an add driver's channel nobody scales down [I] (GD05-088).
- **Venue brightness (GD05-069): not fixed.** Vulkan now: luma 110.5 /
  174.1 / 147.2 / 69.9 (small_club_02, big_club_03, arena_04, theater_02;
  movies 69.3), contrast +41 / +23 / +46 / +25 %, saturation 0.19 / 0.11 /
  0.13 / 0.26 (0.40), grain 2.37 / 1.21 / 1.72 / 2.39 (1.97). Checked
  against RB: the light upload (0x827D6F00: colour, w = full until
  falloff_start, zero at range) is the port's; the particle basis x 0.5
  (0x82476498) is the port's. big_club_03's white frames are its stage
  fog systems (stage_fog*.part, additive, black at birth, keyed by their
  anims to 0.06..0.3, 16..30 quads of 100..400 units).
- **Theater crowds (GD05-076): not fixed.** rb_crowd passed in theater_01
  with 0 characters drawn (the audience-alone picture fully covered: 2,073,600
  px); it now also checks crowd_chars_max >= 1 and fails there (arena_01:
  700). CamShot show_list is read but never applied: the next lead.
- **Gate** (fresh build of ad8320f + this, gate1.sh's 41 walks + rb_highway,
  rb_crowd (default, arena_01, theater_01), rb_look in four venues, rb_lose
  x9 perturbed, five tests, bandcheck; 6 at a time, each its own
  XDG_DATA_HOME, `agents/scratch/fix6/gate_par.sh`): 52 of 57 walks OK, 5 of 5
  tests OK, bandcheck passed. Failed: rb_look in all four venues
  (GD05-069) and rb_crowd theater_01 (GD05-076), both expected.

## 2026-11-12: the tours' rules, quick play's tour unlocks, the instruments' finishes (car7: GD05-085, GD05-077, GD05-083)

- **Done (D75):** the Solo Tour runs on RB's TourCharLocal rules read in
  the XEX (vtable 0x8206C184): songs from SongMgr's ranked lists (normal /
  bonus, RB 0x82434020, `rankedSongs` in game/song_db), (songs N) /
  (unlock N) / (<inst> N) resolved as RB 0x823FE820 (clamped, negative from
  the end); a venue's open events by max_song and the bonus songs unlocked
  (0x823E46B0); towns open in the data's order once the venues before are
  completed at the difficulty or a harder one (0x823E5BB8 / 0x823DE560);
  event_status, is_venue_type_unlocked, the bonus unlocks per difficulty
  (complete_event 0x823E1178, from num_initial_unlock_songs 0x823E54E8) and
  new_unlock (0x823DE7F8), saved (rb_profile.json `unlocks`,
  `seen_unlocks`). Band World Tour: buzz from the venue's performance table
  (0x823EF7D0), fans on the (fans ...) line (0x823EE350), lose_penalty, the
  band's played songs (saved `played`). Quick play (GD05-077): the tours'
  unlocks (0x82431AC8 / 0x823D4738 / 0x823E0458 / 0x823D43D0).
- **Done (D76, GD05-083):** has_group_selection (0x823E0700) and
  group_select_panel: a model with finishes offers them, the chosen one is
  bought and worn.
- **Expected (data) vs measured** (walks on HEAD + these changes only, a
  clean export built in agents/scratch/car7/old; "before" = the same walks
  on the code before, GD05_WALK_KEEP_GOING=1):

  | check | expected (from the data) | measured | before |
  | --- | --- | --- | --- |
  | rb_tour_solo_events, new guitarist (Medium, max_song 45, 1 bonus) | 6 events: tier1_1 song1..5 = shouldistay, inbloom, maps, missqueen, hereitgoesagain; unlock1 = timewehad | the same 6, same songs | 59 events listed; song5 played timewehad, unlock1 inbloom |
  | rb_tour_unlocked, after the (unlock 1) gig | new_unlock daylatedollarshort (guitar bonus song 2); unlock2 listed (7 events) | daylatedollarshort; 7 events | not reached (the list) |
  | rb_tour_qp, restarted | 10 first by guitar + first town's 5 + 2 bonus = 12 songs | 12 (0 extra, 0 missing) | 10 (GD05_TOUR_UNLOCKS_LEGACY=1: 10, 2 bonus songs missing, FAIL) |
  | rb_bwt_buzz, band gig small_club 2 stars / 1 song | buzz performance[new_performance 2 - 1][0] = 0, fans 0, the song offered in quick play | 0, 0, 0 missing | buzz 7, fans 70, the song not offered |
  | rb_shop_finishes, guitar 5120 | 5120_paint, _sparkle, _sunburst, _triburst, _woodash, _woodmaple | the same 6 | no group_select_screen (bought in 5120_paint) |
  | rb_shop_guitar, the second finish | 2100 - 1299 = 801, 5120_sparkle worn, on stage, after a restart | 801, 5120_sparkle, 1 member, kept | - |

- **Gate** (HEAD + these changes only, a clean export built apart from the
  other agent's uncommitted edits; `agents/scratch/car7/gate_par.sh`:
  gate1.sh's 42 walks incl. rb_tour_rules + rb_highway, rb_crowd, 6 at a
  time, each its own XDG_DATA_HOME, + the five tests): 41 of 44 walks with
  OK at first (rb_bright's verdict line was interleaved in its log: OK),
  5 of 5 tests OK. rb_band_start timed out on qp_endgame_screen under the
  load, then OK alone. rb_highway failed (meter_flickers 2 under load, 1
  alone) and fails the same alone on HEAD without these changes
  (meter_flickers 1): not from this work (the flicker probe's known load
  sensitivity, PROGRESS 2026-11-12 G06 highway).
- **Not done:** GD05-082 (the real band lobby: RB's matchmaking_band_screen
  needs session_mgr add_local_player / update_band_leader, the slots'
  char.lst and the creator from the lobby; the stand-in stays); GD05-083's
  drum / mic shops walked and the characters drawn in the menus; GD05-084
  (patches: no render to texture yet; height / weight: RB's deform not
  read, the slider properties go through generic property syncs 0x8233B878
  / 0x8231FD38); GD05-085's [I] parts (D75: the buzz row's "stars before",
  the caps' order, new_town, the band towns' own events from
  tour/<town>.dta not loaded, fame / the hometown tier mapping 0x823FCED8,
  the active moments).

## 2026-11-12: G06 follow-ups (fix7: theater crowds, drummer's left hand, venue brightness)

- **Theater crowds (GD05-076, D77):** a shot shows its own WorldCrowd and
  hides the others (RB 0x82286E50 from CamShot StartAnim 0x822904B0), then
  applies its hide / show lists (0x8228F608), set back as it ends
  (0x8228F6C0). Crowd sweep (41 venues, #crowd0; agents/scratch/fix7/
  crowd_sweep.sh, git-ignored), characters drawn before -> after:
  theater_01..09 0 -> 800 each; arena_01..08 700 -> 700; small_club_01..12
  70 -> 70; big_club_01..12 350 -> 200 (two crowds drew before, the shot's
  one now). rb_crowd: theater_01 FAILED (crowd_chars_max 0) -> OK (800),
  arena_01 OK (700) -> OK (700).
- **Drummer's left hand (GD05-088, D78):** the hand / foot dmidi drivers
  write RB's limbs.blender (a CharBonesBlender blended into the servo and
  zeroed each poll); main_add.drv / zBassOffset.drv (apply 2) rotate to
  instead of adding. GD05_BANDCHECK's new channel check (each position
  channel within its drivers' largest clip values): before (GD05_BLENDER_
  LEGACY=1) 1735.7 x, 2839 of 2901 frames past; after 1.000 x, 0 frames,
  10 of 10 runs passed (8,744..14,209 frames each; drummer stretch worst
  1.000..1.001 x bind, moves on 231 of 252 notes).
- **Venue brightness (GD05-069): not fixed; cause narrowed to the
  particles.** rb_look (Vulkan, 30 fixed shots vs the movies' luma 69.3,
  contrast 55.4, saturation 0.397), now / with every ParticleSys left out
  (a temporary switch, removed):

  | venue | luma | contrast | saturation | luma, no particles | contrast, no particles | saturation, no particles |
  |---|---|---|---|---|---|---|
  | small_club_02 | 124.3 (+79 %) | 80.3 (+45 %) | 0.188 | 73.5 (+6 %) | 68.7 (+24 %) | 0.249 |
  | big_club_03 | 175.0 (+153 %) | 58.2 (+5 %) | 0.091 | 52.9 (-24 %) | 59.6 (+8 %) | 0.324 |
  | arena_04 | 141.6 (+104 %) | 79.4 (+43 %) | 0.131 | 59.2 (-15 %) | 60.8 (+10 %) | 0.174 |
  | theater_02 | 78.7 (+14 %) | 73.0 (+32 %) | 0.241 | - | - | - |

  GD05_POST=0: small_club_02 124.3 -> 126.8 (the post is not it); lights x
  0.25: 124.3 -> 104.5. Checked against RB [C], all as the port: the lit
  pixel shaders (ark/xbox_shaders, read with a scratch dumper over gd04's
  Xenos translator, agents/scratch/fix7/xs/rbshaders.cpp, git-ignored):
  ColorXfmEnv . (tex x gMultipliers.y x (diffuse x ambient + diffuse x
  sum(lightCol x sat(N.L) x sat(dist x k + o)))); gColorXfmEnv (c109) is
  the environ's ColorXfm only with use_color_adjust (0x821AB638 from the
  environ select 0x827D7570), identity in every venue environ; the light
  colours are uploaded as they are (point 0x827D6F00, directional
  0x827D7090: -y row, w 0), up to 8.0 in the presets (stage_point_b
  3.23 / 8.00 / 6.68, char_rim 6.0); textures are created with non-gamma
  formats (DXT 0x1A200152.., A8R8G8B8 0x18280186: sign bits 0), so a
  gamma-to-linear fetch (tried: small_club_02 124 -> 75, big_club_03
  175 -> 104) is not RB's; the particle pixel shader is tex x vertex
  colour; RB's particle move (0x821D1100) steps colour and size with
  smoothstep's derivative 6u(1 - u) (the port: linear; the same mean).
  The big washes are stage_fog02 / 03 (small_club_lighting's
  stage_fog.grp, a root of the venue's draw list: life 800..1000 frames,
  rate 0.01..0.05 a frame, size 100 growing over the life, colour black ->
  0.8 grey -> black, additive, not use_environ, beam_fog*_nomip.tex mean
  0.33 opaque; 16..31 quads drawn). Next: RB's RndDir draw list (does it
  draw a directory's ungrouped stage_fog.grp at all? the port's rule is
  [I]) and the emission at RB's clock (init 0x821D33E8).
- **Gate** (a clean build of afa09ca exported to agents/scratch/fix7/head;
  `agents/scratch/fix7/gate_par.sh`: gate1.sh's 42 walks + rb_highway,
  rb_crowd (default, arena_01, theater_01), rb_look in four venues, rb_lose
  x9 perturbed, five tests, bandcheck x4 and once with
  GD05_BLENDER_LEGACY=1; 6 at a time): 53 of 58 walks OK, 5 of 5 tests OK,
  bandcheck 4 of 4 passed (14,089..14,334 frames; drummer channels
  1.000 x, stretch 1.001), legacy FAILED as it should (8,496 x, 13,856 of
  14,122 frames). Failed: rb_look in all four venues (GD05-069: luma
  123.0 / 165.4 / 150.7 / 66.4, contrast +42 / +23 / +35 / +28 %,
  saturation 0.165 / 0.102 / 0.116 / 0.269) and rb_band_start (timed out
  on qp_endgame_screen under the load; OK when run alone right after).

## 2026-11-12: venue brightness (fix8, GD05-069)

- **Cause found [C]:** the stage fog is a SpotlightDrawer's proxy (D79).
  RB's SpotlightDrawer lists its second object pointer (+0x5c) as its draw
  child (ListDrawChildren 0x82299DD0), RndDir SyncObjects (0x821B3A48)
  removes every listed child from the directory's draw list, and the
  drawer's DrawShowing is an empty function (0x824A3138) in both
  SpotlightDrawer and NgSpotlightDrawer: the proxy is never drawn. Each
  venue type has one drawer, its proxy the stage fog / smoke group
  (small club and arena stage_fog.grp, big club and theater
  stage_smoke.grp): the additive fog the port drew over every shot.
  Lead 1 of fix7 answered: the port's "everything not inside another
  group" rule was right, the drawer is the "other group".
- **Checked, as the port [C]:** blend modes (NgMat 0x827D4750: dest ZERO /
  ONE, src ONE / ZERO, add ONE / ONE, srcAlpha SRCALPHA / INVSRCALPHA,
  srcAlphaAdd SRCALPHA / ONE, multiply ZERO / SRCCOLOR; additive blends
  get a black fog colour); light presets move lights with the same
  quaternion maths (0x822BAAA8 -> 0x8238DE10, slerp 0x8238F3B8); the
  directional upload (0x827D7090: -y row, w 0); gMultipliers (c5) =
  (intensify + 1, emissive_multiplier) (0x827D4750; every venue mat's
  emissive_multiplier is 1.0, so no change); the character pixel shaders
  (diffuse x (ambient + sum of lights) x texture x c5.y, no rim term).
  Presets' light colours reach 6.0 (char_rim) and 3.0 on band.env's
  directional lights: the band is lit near white in close-ups, as the
  data says (not changed). Lead 2 (emission) not needed for the fog; lead
  3: the presets carry SpotlightDrawer entries (3 f32 per keyframe), not
  particle switches.
- **rb_look, same build, GD05_SD_PROXY_LEGACY=1 (before) -> now** (movies
  luma 69.3, contrast 55.4, saturation 0.397; +-15 %):

  | venue | luma | contrast | saturation |
  |---|---|---|---|
  | small_club_02 | 131.7 -> 73.0 (+5 %) | 79.0 -> 68.8 (+24 %) | 0.152 -> 0.249 |
  | big_club_03 | 184.2 -> 52.4 (-24 %) | 65.7 -> 61.7 (+11 %) | 0.086 -> 0.309 |
  | arena_04 | 149.0 -> 81.0 (+17 %) | 79.7 -> 78.5 (+42 %) | 0.126 -> 0.191 |
  | theater_02 | 65.9 -> 42.6 (-39 %) | 69.7 -> 61.6 (+11 %) | 0.270 -> 0.323 |

  Runs vary by a few points (arena_04 78.5 / 81.0, theater_02 47.1 /
  42.6 in two runs). Not within 15 % in all four: what is left is not
  one excess any more but a spread. Measured shares: without the band
  (temporary switch) luma 53.4 / 36.3 / 73.1 / 18.1 (the band carries
  20..30 points in close-ups); without the spotlight beams 59.5 / 36.1 /
  77.3 / 32.9; arena_04's contrast comes from the sparks fountains drawn
  in nearly every frame (fountain_01..03, blend srcAlphaAdd, 20..60
  particles each; the win set sparks_fountain_blue_WIN* sits in
  win_fx_draw_first.grp, showing, with emit rates 2..7 a frame unless an
  anim sets them; what idles them in RB is not traced).
- **The pause check** (black_bg.mat's rule, rb_look) now fails or passes
  by the shot: it compares a frame before the pause with one during it,
  and without the fog over everything the world under it changes between
  the two (big_club_03 caught a blackout: 0 lit pixels; theater_02 1.85,
  small_club_02 0.46; the legacy run also gave 0.66). A measurement
  problem of the walk, not of the overlay (D57).
- Screens: agents/scratch/fix8/compare.png (git-ignored): per venue
  before | after | a disc movie frame.

## 2026-11-12: the band lobby, the drum and mic shops, the body deform, the characters in the menus (car8: GD05-082, GD05-083, GD05-084)

- **Done (D80, GD05-082):** the band lobby runs Rock Band's own scripts:
  four player slots (MultiFocusScreen, each slot directory its own focus
  and states, buttons by player as RB 0x8282EA48, a slot's state init on
  change as RB 0x8282E5E0, all_ready RB 0x8282DE20), session_mgr
  add_local_player / remove_local_player / update_band_leader (RB
  0x82832788 / 0x82834138 / 0x82831238), each player's own character list
  (get_char_provider <player>, RB 0x828572F0 / 0x82857660: (Create New),
  (Default), the profile's characters of the instrument), Create New opens
  the creator for that player and comes back to the lobby, the leader's
  CONTINUE goes on (goto_leader_confirmation); each player's character
  plays in its slot's part. GD05_BAND_PLAYERS keeps D10's shortcut for the
  walks that test what comes after the lobby.
- **Done (D81, GD05-083):** the drum and mic shops walked; the outfit lists
  as RB's OutfitProvider (RB 0x82859E08: exclude, allow_none, creator,
  filter); a piece's chosen finish worn (the mic's mic1_silver); a
  list-only panel's back after a purchase; the creator's sliders.
- **Done (D82, GD05-084):** the height / weight deform (char/deform.h, RB
  0x82300A00 / 0x82355918; the subagent's spec in
  agents/scratch/car8/sub/NOTES.txt, git-ignored).
- **Done (D83, GD05-083):** the characters in the menus
  (ui/rb_char_preview.h): the Rock Shop's (store.milo char0) and the
  creator's (practice_space.milo creator.trans), built as the band's.
- **Walks (gate1.sh):** rb_band_lobby (+ _restarted), rb_shop_drum,
  rb_shop_mic, rb_body_deform; rb_preview_drawn in rb_body_deform and
  rb_shop_drum.
- **Expected (data) vs measured** (this build; "before" = the same walks
  on the code before, HEAD 501214c + only the walks' code, GD05_WALK_KEEP_GOING=1):

  | check | expected (from the data) | measured | before |
  | --- | --- | --- | --- |
  | rb_lobby_slots, empty lobby (pads: guitar, drum) | join, join, no_instrument (no mic), join | the same | lobby skipped (timed out waiting for matchmaking_band_screen) |
  | after player 1 / player 2 join | choose_character / choose_gender / ready_leader; ready_leader, choose_character, ..., ready_not_leader | the same at every step, num_players 1 then 2, leader 0 | - |
  | rb_lobby_rows (Band World Tour: allow_create_char, no show_prefab_chars) | [create_new] then, restarted, [create_new, the saved character of the instrument] | [create_new]; [create_new rb_char2] / [create_new rb_char3] | - |
  | rb_lobby_chars after the restart | the characters made in the lobby | 0=Jett 1=Nikki, the same | - |
  | rb_lobby_stage | the guitarist in Jett's worntshirt_vintage, the drummer in Nikki's tanktop_solid | 1 and 1 | - |
  | rb_kit (drum): first piece with a cost | oysterpearl 649, 5 outfits contain it (group_select_screen); cash 2100 - 649 = 1451; the drummer plays *_drums_oysterpearl | group_select_screen, 1451, worn, 1 member | 46-row list from none, X bought nothing (timed out) |
  | rb_kit (mic) | mic1 99 owned by the singer, 4 finishes: the second, mic1_silver, worn free (2100) and on stage | 2100, mic1_silver worn, 1 member | the same timeout |
  | rb_deform_sliders | weight 1, height 1 (5-step sliders at the top) | 1.00, 1.00 | 0.50, 0.50 |
  | rb_deform_stage: the guitarist's bones = deform sample 1 (tall and fat) | spine1 2.813, L-foreArm 12.377, R-knee 17.129, neck 11.139 (neutral 4.417 / 11.754 / 18.654 / 9.596) | equal (error 0.000) | the neutral values (errors 1.60 / 0.62 / 1.53 / 1.54) |
  | rb_preview_drawn (creator / shop) | meshes > 0, > 500 pixels changed by the preview | 285 meshes, 26,280 px / 253 meshes, 54,888 px | no preview |
- **Also fixed on the way:** `{random a b}` (RB registers it with
  random_int, 0x8248F908; seldiff.dta's two-guitar part pick) was unknown;
  outside a band lobby add_local_player keeps the port's single player 0
  (a pad joining as player 2 made the solo modes a two-player game).
- **Gate (44907b0 built in engine/build/car8; agents/scratch/car8/gate_par.sh,
  6 at a time, each its own XDG_DATA_HOME, while another agent's 6-way gate
  ran too):** gate1.sh's 45 walks (with rb_band_lobby, rb_shop_drum,
  rb_shop_mic, rb_body_deform) + rb_highway, rb_crowd: 44 of 47 OK at
  first, the five tests OK, bandcheck passed (5,205 frames; the premades'
  deform keeps every bone within 1.2 x its deformed rest). rb_band_start
  (timed out on qp_endgame_screen), rb_guitar_none (rb_hud_box_err_px 5)
  and rb_highway (meter_frames 118) failed under the load and passed run
  alone. An earlier run had caught two regressions, fixed before the
  commit: the solo modes became two-player games when a pad other than
  the first joined (rb_pad_play, rb_keys_play, rb_od_pad, rb_od_guitar,
  rb_guitar_*), and bandcheck's stretch measured the premades' deformed
  bones against the undeformed bind (1.46 x).
- **Not done:** GD05-084's skins (the TexRenderer skin proxies: no render
  to texture for characters, GD05-022) and so the tattoos, make-up and
  face paint, the patch creator (ui/patchcreator.dta PatchPanel); GD05-083's
  menu cameras (select_cam shots) and the lobby slots' characters;
  GD05-091 (seldiff's per-player slots), GD05-092 (the deform's MeshDeform
  bake, CharCuff, CharIKScale).
- **Gate** (a clean build of 8959475 exported to agents/scratch/fix8/head,
  with FFmpeg; `agents/scratch/fix8/gate_par.sh`, 6 at a time): 50 of 62
  walks OK, 5 of 5 tests OK, bandcheck 4 of 4 passed (6,263..14,179
  frames; drummer channels 1.000 x, stretch worst 1.188 x bind) and the
  legacy run FAILED as it should (8,487 x). Failed: rb_look in all four
  venues (luma 71.4 / 53.8 / 78.1 / 42.3, contrast +22 / +10 / +37 /
  +13 %; small_club_02 and big_club_03 on the pause check, GD05-089);
  rb_crowd arena_01, rb_guitar_none (HUD box 6 px) and rb_highway
  (meter_frames) under the load, all OK when run alone right after (and
  rb_crowd arena_01 with GD05_SD_PROXY_LEGACY=1 too); rb_band_lobby,
  rb_body_deform, rb_shop_drum, rb_shop_mic are another task's new walks
  in gate1.sh, not in this commit (unknown walk).

## 2026-11-12: venue look, final pass (fix9: GD05-090, GD05-089, GD05-069)

- **arena_04's win fountains (GD05-090, D85 / D86):** three causes [C].
  (1) A WorldInstance's EventTriggers answer the world's events (RegisterEvents
  RB 0x8220C770 sinks each trigger on its own directory); the port ran only
  the chain's: sparks_fountain_blue_win.milo's start_hide.trig ("start")
  never hid shared.grp. 24 shared fx triggers listen for "start", others for
  game_won / bonusfx / bre_success / intro_end. (2) EventAnim (SetFrame RB
  0x822CE3A0) was kept raw: the win sequence's shots (coop_fw_*, their
  play_anim) fire the fountains' game_win.trig at frames 18 / 20. (3) RB's
  RndParticleSys moves and emits only when drawn since its last Poll (Poll
  0x821D94F0, DrawShowing 0x821D9408); the port ran every system every
  frame, hidden or not. Walk rb_win_sparks (arena_04): particles started by
  the win fountains' systems 15 s into the song 0 (fix8 build: 5,869), after
  the forced win shot coop_fw_all05 1,186 (704 drawn), 69 EventAnim
  triggers.
- **force_preset (D87):** the BandCamShots' {$world force_preset ...} was
  UNHANDLED (rb_look failed on it in every venue, fix8 too): WorldDir
  0x822A2608 / StartPreset 0x822A1B50 [C].
- **The pause check (GD05-089, D88):** measured on a still frame (paused,
  flicker / grain held by GD05_POST_FREEZE, the pause screen hidden then
  shown with black_bg.mesh as its only mesh): the paused world does not
  move (mean difference 0.00..0.52 of 255); the background is 0.99..1.01 of
  black_bg.mat's rule in every measured pause once all the art is out
  (with the art in: 1.00..2.70; with only the octopus / arms / trim out, 2
  of 30 pauses read 0.20 / 0.22). rb_look (this
  build): small_club_02 luma 72.9, contrast 68.1 (+23 %); big_club_03 55.3
  (-20 %), 60.6; arena_04 65.4 (-6 %), 60.0 (+8 %): OK; theater_02 42.9
  (-38 %), 61.6; grain +46..58 % but arena_04's +14 % (GD05-095).
- **The SpotlightDrawer's own draw (GD05-069): read, not implemented.**
  NgSpotlightDrawer (vtable 0x82034824): DrawShowing empty, DrawWorld
  0x8229BB38 per queued spotlight: the colour (x intensity) and up to four
  passes by global switches (0x828EB665..668: the spot's +0x2EC objects,
  its +0x1B0 mesh with the shared Mat, its +0xEC drawable, its +0xE8 Mat);
  the volume beams 0x822ABC28: per spotlight with +0x1BC > 0, one of three
  passes by its type (+0x1E8: 2, 3..4, other) with the gSpotPos / gSpotDir /
  gRadii / gSpotXSectionMap / gExtrudePos shaders (ark/xbox_shaders, at
  unaligned offsets the fix7 dumper skipped: agents/scratch/fix9/xs,
  git-ignored), gFogColor c90 = colour x intensity x 8 x (+0x1E0); then
  0x822A8E78 sets gFogDensity c82 = (base x 0.01, smoke x 0.01 x (1 - base
  x 0.01)). The post-process shaders read it: out = sat(scene +
  gFogDepth(s12).rgb x (c82.x + c82.y x gFogDensityMap(s6).x) x c91.x)
  (#2397, the simplest variant) [C]. The drawer's record: total 1, base 1,
  smoke 4 / 2 / 2 / 2.1 (small club, big club, arena, theater), colour
  white / 0.16 / 0.12 / (0.09, 0.16, 0.13); the presets' keyframes (1, 1..6,
  1) (15,223 entries: (1, 2, 1) 6,018, (1, 4, 1) 3,262 ...). So RB's beams
  reach the picture only through the post composite, at 0.01..0.06 of the
  beam buffer times the density map times c91.x; the density map is the
  stage fog (below, D89); c91 is set per spotlight in the volume passes
  (type 3 / 4: (0, 0, 0.625, +0x1C0); the others not read) and not by the
  post, so its value at the composite is not settled and how bright the
  beams are stays open. Implementing it
  means RB's three volume passes, the density map and the composite, not
  a gain on the port's GH2 beams: nothing changed (GD05-093).
- **The stage fog proxy (D89, a correction of D79's reason):** RB does draw
  it, through the drawer's own camera into the drawer's texture (0x822A9E58
  from the beam pass 0x822ABDC0), which is gFogDensityMap: the smoke the
  beams light in the composite. It never reaches the picture by itself, so
  leaving it out of the draw list stays right until the beams are RB's
  (GD05-093). The movies' white haze in the beams (theater, big club) is
  most likely this composite [I]: the remaining dark gap of theater_02
  (-38 %) and big_club_03 (-20 %) waits for it; nothing was tuned.
- **rb_look, before (fix8, D79) -> after (the gate's run)** (movies: luma
  69.3, contrast 55.4; +-15 %):

  | venue | luma | contrast | grain (1.97, +-40 %) |
  |---|---|---|---|
  | small_club_02 | 73.0 (+5 %) -> 71.7 (+3 %) | 68.8 (+24 %) -> 68.2 (+23 %) | 3.01 -> 3.04 |
  | big_club_03 | 52.4 (-24 %) -> 55.2 (-20 %) | 61.7 (+11 %) -> 61.1 (+10 %) | 3.10 -> 3.22 |
  | arena_04 | 81.0 (+17 %) -> 69.2 (0 %) | 78.5 (+42 %) -> 60.4 (+9 %) | 2.31 -> 2.26 |
  | theater_02 | 42.6 (-39 %) -> 41.1 (-41 %) | 61.6 (+11 %) -> 59.2 (+7 %) | 2.88 -> 3.09 |

  arena_04 passes rb_look (the win fountains hidden: with
  GD05_PARTICLES_LEGACY=1 it reads 67.0 / 60.7, so the hiding (D85) does
  most of it; D86 stops their hidden emission: rb_win_sparks reads 5,681
  started before the win with the legacy rule). The 30-shot means move with
  the timing (the presets' keyframes and blackouts at the moment of each
  shot): arena_04 read 65.0 / 65.4 / 62.5 / 69.2 in four runs and 49.6 in
  one of five at a time. Left, not tuned: theater_02
  and big_club_03 too dark, small_club_02's contrast, the grain (GD05-093,
  GD05-095).
- **Gate** (a clean export of eaa0f69 + 83bc1f0's walk fix, built with
  FFmpeg; agents/scratch/fix9/gate_par.sh, gate1.sh's walks + rb_highway,
  rb_crowd x3, rb_win_sparks, rb_look x4, rb_lose x9 perturbed, five
  tests, bandcheck; 6 at a time): 59 of 64 walks OK, 5 of 5 tests OK,
  bandcheck 4 of 4 passed (7,433..14,061 frames; the legacy run FAILED as
  it should). Failed: rb_look small_club_02 (contrast +23 %),
  big_club_03 (luma -20 %), theater_02 (luma -41 % and one pause at 0.22:
  the art, fixed in 13f1514); rb_crowd arena_01 (crowd_px 0 under the
  load; alone right after: OK, 700 characters, 1.43M px) and rb_lose_quit
  (reached qp_endgame_screen instead of the lose screen under the load;
  alone: OK).
- **The pause check, 10 runs** (13f1514, the four venues in turn, 5 at a
  time): 10 of 10 runs passed the pause check: 27 pauses measured, all
  1.00..1.01 of the rule, 3 on a blackout skipped (big_club_03); the
  rb_look runs themselves: arena_04 OK once (65.1), the others fail on
  luma (big_club_03 56.6..59.0, theater_02 42.1 / 46.3, arena_04 49.6 once),
  small_club_02's contrast (+23..26 %) or the grain.

## 2026-11-12: Rock Band's spotlight beams, the grain (fix10: GD05-093, GD05-095, GD05-069)

- **The beams (GD05-093, D91) [C]:** NgSpotlightDrawer's beam-sheet pass
  (vtable +0x44) is an empty function (0x824A3138): Rock Band never draws
  the beams' sheets with their beam material, which the port drew (GH2's,
  gain 1). Its beams are three passes, now the port's: the drawer's proxy
  (stage fog / smoke) drawn into a quarter-size density map through a copy
  of the shot's camera (0x822A9E58); per queued spotlight with a beam
  length (shape 0 / 1: every beam on the disc) xbox_shaders #2290 / #2291,
  a ray / cone intersection against the scene's depth, into a half-size
  beam buffer (0x822ABC28 -> 0x822AB858 -> 0x822AABE0, the side planes
  0x822AA458, the camera-inside test 0x822A90B0); then the post composite
  adds beams x (base x 0.01 + smoke x 0.01 x (1 - base x 0.01) x density)
  x total x 32 after the bloom (#2381 / #2397). c91.x, fix9's open
  question: 0x822AB5B8 writes it last (total x 32, 0x8288735C). The
  presets' drawer entries (0x822BF6B8) ease smoke towards the file's second
  f32 and set base to total eased towards the third (RB stores base twice).
  The shaders are gd04's Xenos translation kept as is (a scratch copy of
  the translator that names every constant register,
  agents/scratch/fix10/xs, git-ignored); only the fetches are the port's.
- **The grain (GD05-095, D92) [C]:** RB samples noise_mono.tex (512 x 512,
  six levels, scale 8 x 4.5: 3.2 texels a pixel at 1280 x 720) with a
  linear mip filter (0x827D9190 -> 8246AC70(13, 1): fetch constant bits
  23-24); the port read the top level (sharp white noise).
- **Check (rb_look):** GD05_BEAM_PROBE reads the beam buffer and the
  density map back each frame and counts the pixels the composite adds at
  least 1/255 to; the walk wants some in the 30 shots and none with the
  drawer's base / smoke at 0 (GD05_DRAWER_ZERO, on a shot that still
  draws beams). GD05_BEAMS_LEGACY=1 (the old code path) fails it: "no
  spotlight beam pixels in any frame".
- **rb_look, before (fix9 gate) -> after (this build, one run each, not
  the gate)** (movies: luma 69.3, contrast 55.4, grain 1.97):

  | venue | luma | contrast | grain |
  |---|---|---|---|
  | small_club_02 | 71.7 -> 64.7 (-7 %) | 68.2 -> 64.6 (+17 %) | 3.04 -> 2.86 |
  | big_club_03 | 55.2 -> 48.9 (-29 %) | 61.1 -> 53.1 (-4 %) | 3.22 -> 3.07 |
  | arena_04 | 69.2 -> 64.1 (-8 %) | 60.4 -> 52.5 (-5 %) | 2.26 -> 2.10 |
  | theater_02 | 41.1 -> 45.3 (-35 %) | 59.2 -> 57.9 (+4 %) | 3.09 -> 2.43 |

  arena_04 passes; beam check passed in all four (25..29 of 30 frames
  with beam pixels, 0 with base / smoke 0). Fails left: big_club_03 and
  theater_02 luma, small_club_02 contrast (+14..17 % over runs) and grain.
  Without any grain (temporary switch) grain reads 2.14 / 2.64 / 1.49 /
  2.05: the rest is scene detail, cause not found.
- **Not done (the wave ended):** the full gate (the clean HEAD export
  was stopped while configuring); GD05-094 (venuesfx_bank.milo: no
  reference in the XEX strings, the .dtb files are not plain text; not
  traced); [I] items of D91 (the density pass's +0x25 flag, the beam
  buffer's 0xA2 format / blend, cut-outs, target colour).

## 2026-11-13: the bug list's band and camera items (bl05: GD05-096..098)

- **Vocalist's face (GD05-097, D94) [C data]:** the face set's clips
  other than Base are offsets from it; the lip servo took them as poses
  (a - Base) and started each clip's pose from the identity quaternion: the
  jaw sat 90 degrees open at rest and turned 180 while singing, the lower
  lip 7..11 units out, so the chin went into the neck and the mouth's
  inside showed (every head; worst on the female tank-top singers). Now
  Base plus each lit viseme's weight of its offset. bandcheck's new "face
  rest" line (each member's jaw against the face clips' largest turn,
  x 1.5): vocals0 180.0 -> 40.8 deg (clips 41.6), vocals2 179.9 -> 23.2
  (21.8); low lip 7.17 / 11.46 -> 0.62 / 0.50 units;
  GD05_FACEPOSE_LEGACY=1 fails it. Also CharMeshHide applied (D95).
- **Song-start shot (GD05-098, D96):** the mode's intro_category; a
  guitar player in a --song run got coop_intro_bass.shot, now
  coop_intro_guitarist.shot (INTRO with guitar_focus); quick play solo
  takes INTRO_VENUE.
- **Strike line (GD05-096):** findings only: launcher 1.1.14 predates D41
  (meter / multiplier over the now bar) and D61 (track order); the branch's
  inbloom 30 s shot shows the meter under the now bar.
- **Left:** strum hand (GD05-092's IK pieces), drummer, shots in the crowd
  / behind objects, next_shot chains, a pale seam under the jaw on tank
  tops, highway effects, brightness / grain, world emulate_fps.
- Dev aid: GD05_PREFAB_<INSTRUMENT>=<premade> (GD05_PREFAB_VOCALS=vocals2)
  picks a premade from characters.dtb.
- **Gate** (gate_par.sh, bl05 binary, 4 at a time): 61 walks OK, 5 of 5
  tests, bandcheck 4 of 4 passed (face rest jaw 40.8 deg, clips 41.6), the
  legacy run FAILED as it should. Failed: rb_od_guitar / keys / pad (the
  6.7 s INTRO_VENUE intro; the walks now wait for the song clock: od_guitar
  and od_keys OK after), rb_stutter (song_begin spike 337..501; fails with
  GD05_INTRO_LEGACY=1 too: not this change), rb_look x4 (luma 31 in
  arena_04 against 64 before; GD05_INTRO_LEGACY=1 passes: the intro
  shot's lighting preset / PostProc is still in force when the walk's
  forced shots are measured; open, next step: follow BandCamShot
  start_shot's lighting_override / postprocess for forced shots and check
  against the movies).
