# gd-game-05 progress log

Rock Band (Xbox 360) -> `gd05`. Kickoff: `docs/spikes/GD-GAME-05_KICKOFF.md`
(wave 01, branch `feature/20261109-rockband1-wave01`). Newest entries at the
bottom. "Verified" lines quote the command and its output; "built only" and
"not tested" are said explicitly.

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
