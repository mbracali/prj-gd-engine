# gd-game-05 decisions

Choices the owner would normally make, taken while the owner was away
(kickoff section 2: the original's behaviour first, else gd-game-02's
pattern). Each says what, why and how to undo it.

## D1. Start from gd-game-02's engine at the branch's base (2026-11-09)
- **What:** every tracked file of `gd-game-02/engine`, `tools`,
  `build_commands` and `local_build.sh` copied into `gd-game-05/` and renamed
  (gd02 -> gd05, GD02_ -> GD05_, gd-game-02 -> gd-game-05). gd02 backlog IDs
  in comments (`GD02-nnn`) are kept: they point at gd02's history.
- **Why:** the kickoff's plan (the gd-game-03 path); the first look showed
  GH2's ARK v3 and cipher read as is.
- **Addresses:** every `0x82xxxxxx` address in a comment inherited from gd02
  is a **GH2** address until re-confirmed. Rock Band confirmations are
  written `RB 0x82xxxxxx` in code and `[C] 0x82xxxxxx` in
  `gd-game-05/docs/` (whose addresses are always RB's).
- **Undo:** none needed; this is the base.

## D2. FFmpeg decoders: Bink in, WMV out (2026-11-09)
- **What:** `engine/build_common.sh` builds FFmpeg 7.1.5 with the `bink`
  demuxer and the `bink`, `binkaudio_rdft`, `binkaudio_dct`, `xma1`, `xma2`
  decoders; GH2's ASF / WMV3 / WMA2 are dropped (no WMV on RB's disc).
- **Why:** RB's movies are `videos/*.bik`; the kickoff asks for the Bink
  decoders in our own FFmpeg build (LGPL, pinned).
- **Undo:** the `FFMPEG_CONFIGURE` line.

## D3. The version 11 mogg key is a constant in the engine (2026-11-09)
- **What:** every Rock Band `.mogg` (374 on the disc) is version 11: the Ogg
  stream is AES-128 in counter mode (16-byte nonce after the seek map,
  128-bit counter incremented little-endian). The key is 16 bytes in the
  executable's `.data` at 0x828B9AB4, set up by the mogg header reader at
  0x827E02A8 [C]. `engine/src/data/mogg.cpp` holds it as a constant and
  `data/aes128.cpp` is our own AES-128 (forward cipher).
- **Why:** without it no song plays. Reading it from the player's own
  `default.xex` at run time is not simpler: the executable on the disc is
  itself encrypted (XEX2 file format: encryption 1, basic compression), so
  that path would put the Xbox 360's own executable key in the source
  instead. gd-game-02's Harmonix cipher constants are the precedent for a
  format constant in the code.
- **Undo:** replace the constant with a key read at run time (from a file
  the player provides, or the decrypted XEX); `decryptMogg` is the only user.
  OPEN_QUESTIONS.md Q3 asks the owner.

## D4. No Xbox profile sign-in; the port's own save (2026-11-09)
- **What:** `memcardmgr get_new_signin_player_num` and
  `get_autosavable_player_num` answer -1 (nobody signed in), so RB's Xbox
  save / load screens are skipped; `platform_mgr is_player_signed_in` is
  FALSE. Progress goes to the port's versioned JSON save (lesson L12, M7).
- **Why:** there is no Xbox Live profile or memory unit on PC; gd-game-02
  did the same with GH2's memory card flow.
- **Undo:** `engine/src/ui/rb_natives.cpp` (memcardmgr, platform_mgr).

## D5. The song list's rank groups by count (2026-11-09)
- **What:** RB's song select groups the songs by rank into the nine
  generic_tier headings; the port sorts the songs with a rank by the
  instrument's rank (the band's for a band) and splits them into nine groups
  of about equal size, "random_song" first.
- **Why:** the original's grouping (RB 0x82404A10 -> 0x824024F0) is not
  read yet; the data has the ranks and the tier names [C data], not the cut.
- **Undo:** `engine/src/ui/rb_natives.cpp` (RbSongProvider).

## D6. Drums played through GH2's joypad watcher in wave 01 (2026-11-09)
- **What:** with the drum part, the player is gd02's `Player` in joypad
  mode on the drum chart, the pads as frets (kick = orange / LB, red, yellow,
  blue, green pad = green / A, as an Xbox 360 drum kit reports them); the
  chart's lanes are permuted to those frets at song load and back for the
  track. `game/drum_player.cpp` (RB's drum rules from scoring.dtb, tested by
  `--selftest`) is not the in-game judge yet.
- **Why:** it makes the drums playable on RB's track now with the HUD,
  endgame and save paths the guitar uses; the joypad watcher already hits a
  gem by pressing its frets (no strum) and a chord within 50 ms.
- **Undo:** `engine/src/app.cpp` (the `Part::Drums` branch in the song
  load); wire `DrumPlayer` in its place (GD05-018).

## D7. Vocals fall back to the guitar part in wave 01 (2026-11-09)
- **What:** choosing the vocals plays the guitar part (logged); the vocal
  judge and pitch detector exist and pass `--selftest` but are not in the
  game loop, nor is a vocal track drawn.
- **Why:** time: the highway, the band and the launcher came first.
- **Undo:** `engine/src/app.cpp` (`part0 == Part::Vocals`).

## D8. Wave 02: a signed-in local profile (2026-11-09)
- **What:** `platform_mgr is_player_signed_in` answers TRUE for every local
  player: the port's own profile (`rb_profile.json` in the save folder,
  version 1: characters, bands, the tours' stars, money, items, hints seen)
  stands for the Xbox profile. The Xbox save screens stay skipped: the
  scripts' `xbox_autosave_start` is answered natively (save the JSON, then
  the screen change the save would have made). Rock Central (online name
  checks, `entity_uploader`) accepts at once.
- **Why:** RB's tours and the character creator need a signed-in profile;
  D4's "nobody signed in" blocked them.
- **Undo:** `ui/rb_natives.cpp` (platform_mgr, entity_uploader), `ui/rb_meta.cpp` (xbox_autosave_start).

## D9. Vocals without a microphone, names without a keyboard (2026-11-09)
- **What:** with no microphone the vocal judge hears silence (the song
  plays, the phrases rate Awful); `GD05_VOCALS_FILE=<wav>` sings a file.
  RB's on-screen keyboard (`platform_mgr show_keyboard`) is answered at once
  with the offered text (a random character or band name).
- **Why:** the owner's real-device checks cover the mic; typing names is a
  port feature still to come (gd-game-04's typed names are the model).
- **Undo:** `audio/vocal_input.cpp`, `ui/rb_natives.cpp` (show_keyboard).

## D10. The band lobby is a stand-in (2026-11-09)
- **What:** `matchmaking_band_screen` (RB's lobby: each player joins a slot
  with a button, picks a character, a leader) is skipped: every local
  player with a controller joins (`GD05_BAND_PLAYERS=<n>` in the tests) with
  the premade character of their instrument (GAMEPADS' instrument or the
  device), player 1 leads, a second guitarist plays the bass, and the
  mode's `post_matchmaking_screen` follows. Band players without a
  controller autoplay. Other players use the leader's difficulty.
- **Why:** breadth first (wave 02 target): Band Quickplay and the Band World
  Tour are reachable now; the real lobby (four slot panels, per-player
  input) is a later wave.
- **Undo:** `ui/rb_meta.cpp` (`ui.redirect`), `GD05_REAL_LOBBY=1` shows the real screen.

## D11. Band play layout and rules [I] (2026-11-09)
- **What:** the instruments' tracks side by side in the lower part, the
  vocal track on top; a player whose meter empties fails out and any other
  player's Overdrive saves them; the band fails under
  multiplayer_lose_level; Unison gives a phrase's worth of Overdrive again.
- **Why:** RB's band rules are not read in the XEX yet; the values are
  scoring.dtb's.
- **Undo:** `app.cpp` (makeSeats / updateSeats / drawSeats).

## D12. The help bar is Rock Band's own panel, placement not done yet (2026-11-09)
- **What:** the owner asked for gd-game-02's help bar, placed where it does
  not cover Rock Band's elements. Rock Band ships its own help bar
  (`helpbar.milo`, meta.dta's HELPBAR_HANDLERS: the icons per controller
  and the action labels along the bottom edge, below the screens' panels).
  The port keeps that panel and its placement (the bottom strip Rock Band
  reserves for it) rather than drawing gd-game-02's over it. It is loaded,
  updated and drawn, but its meshes and labels still land off screen, so
  nothing shows yet (GD05-005).
- **Why:** Rock Band's layout leaves the bottom strip free for this bar, so
  it covers nothing. The port's extras (GAMEPADS on the main menu, the
  items as mouse buttons) go into it the way gd-game-02 adds them.
- **Undo:** `ui/ui_manager.cpp` updateHelpbar / the screen-change call of
  `helpbar update`.

## D13. Camera shots move the band [C data, I] (2026-11-09)
- **What:** each BandCamShot's band targets apply when the shot starts.
  The member plays the target's clip group and, with teleport, moves to
  its mark. The marks are not restored afterwards; the next shot sets its
  own. IK, props and fast-forward are not done.
  `GD05_NO_TELEPORT=1` turns off the moves.
- **Why:** Rock Band's shots are authored around those marks. Without
  them the closeups framed empty stage or the inside of the set.
- **Undo:** `Band::rbShotTargets` (game/rb_band.cpp) and its call in `App::rbShots`.

## D14. Tutorials run their own scripts on the tutorial clock (2026-11-09)
- **What:** Rock Band's tutorial lessons run as written (tutorial/*.dta
  thread tasks, now with the positional task units). Their voice-overs
  come from tutorial/streams and their cues from the tutorial banks
  (loaded with the others). The tutorial panels' other natives
  (ambience, controller locks, fades, metronome) are quiet for now.
- **Why:** it keeps the lessons' timing and words exactly the original's.
- **Undo:** `RbTutorialPanelObj` (ui/rb_meta.cpp).

## D15. Back goes only to a screen's own (back ...) (2026-11-10, wave 03)
- **What:** RB's UIScreen button_down (RB 0x8225AEE8) [C]: kPad_Tri goes to
  the screen's (back <screen>) (evaluated: `{gamemode get ...}`,
  `$lose_confirm_screen_return_to`) with the backward transition; a screen
  without one ignores kPad_Tri. GH2's "back to the screen we came from"
  (GHScreen) is gone. `{ui goto_screen x <force> <back>}` plays the
  backward transition when its third argument is true.
- **Why:** the owner's key 2 bug (GD05-024): the red fret before a song's
  first note sent game_screen back to the loading screens, which loaded the
  song again.
- **Undo:** `ScreenObj::nativeButtonDown` / `backTarget` (ui/script_classes.cpp).

## D16. The Enhanced Options backdrop is silent (2026-11-10, wave 03)
- **What:** the port's backdrop venue (posed at boot for Enhanced Options)
  plays no sound: its world is `silent` (sounds counted, not played; no
  crowd stream).
- **Why:** the owner's "people screaming at the start" (GD05-023): the
  backdrop's excitement change played the venue's crowd_upto_good cheer at
  boot. RB's own menu ambience (meta.dta music_start: shell_crowd.cue with
  the shell music) stays, as the scripts play it.
- **Undo:** `world_->silent` in `App::loadBackdrop`.

## D17. RB's menus: background video, RenderDirs, proxies (2026-11-10, wave 03)
- **What:**
  - movie_panel ((videos background) (loop TRUE)) plays rb_video*.bik behind
    the menu panels, chosen by MoviePanel's rule (0x828557E8) [C], no sound.
  - A RenderDir panel draws each TexRenderer's panel into its texture and
    its own scene through its own camera (seldiff_solo's koi banner and
    cards, the shop and tour rooms) [C data]. The song list (a RenderDir
    whose record names its inline panel as the input panel) is drawn flat:
    its texture under the title bar, centred [I]. Through its own camera
    (cloth.cam at lower3rd.trg's frame 3; GD05_SONGLIST_3D=1 shows it) the
    cloth is big and its top rows sit under the title bar; the sway
    (CharHair) is not done (GD05-022).
  - A UI milo's inline proxied directory takes its proxy file's objects
    when the disc has the file [I]: the inline copies were stale
    (seldiff_solo's slots lacked seldiff.grp / selpart.grp that the scripts
    show and hide).
  - Text colours from ui/color_presets.dta by the label's text style
    (BandTextComp 0x822FFDF0) [C].
  - `{rnd aspect}` answers kWidescreenAspect (the port is 16:9).
- **Undo:** ui/panels.cpp MoviePanelObj, render/ui_view.cpp drawRenderDir3d
  and the flat path, scene/milo.cpp useProxyFiles, scene/ui_styles.cpp
  textStyleColor, ui/natives_meta.cpp (rnd).

## D18. Macro arrays are shared, RandomContext is RB's (2026-11-10, wave 03)
- **What:** an empty array a #define holds is one array for every use
  (meta.dta's connected_controller_types, endgame's SELECTED_AWARDS) [I:
  how Milo's macro DataArrays are referenced]; `{random_context ...}`
  weighs and filters as RB's (0x823CD810 / 0x823CCF30) [C]; `venue` is
  false and `unlocked` true [I].
- **Why:** the help bar's icons and the loading tips.
- **Undo:** script/script.cpp (macro splice, toValue), script_builtins.cpp.

## D19. GAMEPADS on the main menu's help bar (2026-11-10, wave 03)
- **What:** RB's main_screen has no help bar; the port adds RB's own help
  bar panel to it with one item, GAMEPADS on the yellow action (the yellow
  fret or a click opens the port's GAMEPADS screen). RB's help bar takes
  clicks on every action (its label or icon presses its button).
- **Why:** the owner's answer to Q1 (universal access buttons, "always a way
  to set up the gamepads, mostly on the main screen"), placed in RB's own
  help bar strip so it covers nothing of the menu.
- **Undo:** port_patches main_screen in ui/port_screens.cpp; PanelObj::helpMouse.

## D20. The cloth: RenderDir pollables, skinned, through its camera (2026-11-10, wave 04)
- **What:** a RenderDir with a CharHair (qp_selsong, seldiff, the shop,
  tour and creator RenderDirs) polls its own objects once a frame before it
  draws: CharTransCopy (RB 0x8233F6F8 [C]: the dest's local transform
  becomes the source's) and the CharHair strands (the character code's
  hair, 60 Hz), then draws its skinned meshes (popup_bg.mesh on bone_01..10)
  on the CPU (render/ui_cloth.cpp). The song list is drawn through
  cloth.cam like the other RenderDirs (GD05_SONGLIST_FLAT=1 shows wave 03's
  flat texture). The list's mouse hits map the texture over the cloth's
  screen bounds [I: the bend is left out]. The inline panel's copy of the
  cloth (drawn into the texture) is not skinned: its bones are not in its
  directory [I].
- **Why:** GD05-022, the owner's "the cloth is missing".
- **The RenderDir's own transform [C data; I: found in its raw record by
  its shape]:** a RenderDir is a RndDir, so a RndTransformable: its record
  keeps one (qp_selsong: turned 9.5 degrees about z, set back 121 and down
  84; the tour hub: 217 to the left). The objects parented to the directory
  (the cloth's root, popup_bg) hang from it (ScenePose::setDirXfm). Without
  it the song list's first row sat half under the title bar and the tour
  hub's menu ran off the right edge; with it both sit as the disc places
  them.
- **Undo:** `UiView::drawRenderDir3d` (cloth_, renderDirXfm), render/ui_cloth.*, ScenePose::setDirXfm.

## D21. UITrigger anims as RB plays them; transitions wait on blocking triggers (2026-11-10, wave 04)
- **What:** each trigger anim plays from its start to its end frame at the
  rate's fps x scale (or |end - start| / period), after its delay, looping
  for type loop, from the current frame for dest (RndAnimatable::Animate RB
  0x8221BE38 [C]); a 0..0 range sets frame 0 (wave 03 played the whole
  anim). A screen change waits while a panel is Exiting / Entering: one of
  its block_transition triggers has not run the longest of its anims'
  whole ranges / 30 s (UITrigger 0x82262B28 / 0x82262918, PanelDir
  0x8225FEE0 / 0x8225FF60) [C]; GH2's fixed 0.5 s out + 0.5 s in (its
  metacam) is gone [I: the UIManager poll's states]. Input stays dropped
  during a transition, as before. Script `animate` takes (loop a b) and
  (delay d) (0x8221C040) [C].
- **Why:** the owner's "delays, animations" and "strange behaviour": every
  RB screen change took a second with nothing moving; the lights, buttons'
  cascades (pause's 0.09 s delays) and loops never ran.
- **Undo:** `UiView::triggers`, `UiManager::animateTrigger`, beginTransition / switchScreens.

## D22. A proxy file's objects under its root hang from their holder (2026-11-10, wave 04)
- **What:** an inline directory replaced by its proxy file (D17) keeps the
  file's root name; an object whose parent is that name hangs from the
  object that holds the directory (RndTransformable's parent is the dir
  itself). A RndDir used as an anim (an AnimFilter's target) sets its
  anims' frames [I: which of its objects count].
- **Why:** GD05-026: loading.milo's band (load_icon_guitar / vox / drum /
  bass from their files, root "loading") all stood on one spot, frozen.
- **Undo:** `ScenePose::world` (toDir), `Directory::proxyRoot`, ScenePose::setFrame (heldDir).

## D23. RB's gems and tails (2026-11-10, wave 04)
- **What:** the gems draw with their meshes' own materials (no slot
  colour swap: RB swaps per slot only the tails' materials, 0x82819A48 from
  the TailBuilder 0x8281A970) [C]. The tails are RB's: tail02.mesh's
  cross-section every 4.444 for up to 90 segments (0x8281A8C8), a 300-sample
  wobble written at tail_pulse_rate while held (amplitude and frequency by
  the smoothed whammy), the alpha between tail_min_alpha and tail_max_alpha,
  the last tail_fade_distance fading (0x8281C928 / 0x8281C4A0) [C; I: the
  lerps' direction, the star tails' colour blend, no glow line].
- **Why:** GD05-029, the owner's "how the notes are displayed".
- **Undo:** `RbTrackView::drawWidget`, `drawTails` / `pollTail`.

## D24. The results' stars: scoring.dta's symbols (2026-11-10, wave 04)
- **What:** a performer's star_rating is config/scoring.dta's (star_ratings
  (symbols (* ** *** **** ***** 66666 77777))) entry n - 1 for n stars, ""
  for none [C data; I: the index rule, the native is not read]. The stars
  font draws `*` and the gold digits; the port wrote `***__`.
- **Undo:** `starSymbol` in ui/natives_game.cpp.

## D25 (replaced 2026-11-10, GD05-030 follow-up)
- **What:** the port shows what the Xbox 360 sends an HDTV. RB's ramp
  pow(i/256, 0.85) (rnd (gamma 0.85), RB 0x8246BD90, set once at device
  reset 0x8246D220) goes through the XDK's 0x8252B898 / 0x8253DFB8, which
  composes it with 0x8253DE28's table: the value read as sRGB (0x8253DCC8)
  and re-encoded for VdGetCurrentDisplayGamma's display, BT.709 on a TV
  (0x8253DD78) [C]. On an HDTV the two nearly cancel (0.2 -> 0.193, 0.5 ->
  0.509; shadows a bit darker), so the old pow(c, 0.85) alone made every
  frame too bright (the owner's "washed out"). Device setOutputGamma(g) now
  applies BT709(sRGBdecode(c^g)) (render/device.h outputCurve; local, core
  and HDR paths, screenshots too).
- **Undo:** `GD05_GAMMA=0` (none) or another exponent; outputCurve.

## D26
- **What (wave 05, GD05-036 / 037):** GAMEPADS is a port button in the main
  menu's bottom-left corner (a pill like RB's menu buttons, white text,
  orange on hover), drawn by UiManager::drawGamepadsButton on main_screen
  only; the main menu's help bar patch is gone (RB's main_screen has none).
  A click on a button of a non-focus panel (main_panel beside its sub-menus)
  gives that panel the focus (set_focus_panel) and selects [I].
- **Undo:** `gamepadsButton` / `drawGamepadsButton` and the mouse fallback
  in ui/ui_manager.cpp.

## D27
- **What (wave 05, GD05-035):** the port's screens: GH2's bold menu font
  (helveticablack) is RB's kabel_bold (ui/resource/gen) [I]; a prelit font
  material takes the text's colour in its vertices (the state colour
  replaces the text's); the port screens use RB's help bar format.
- **Undo:** ScreenText::font, TextDrawer::draw, port_screens.cpp patches.

## D28
- **What (wave 05, GD05-041):** RB's anim tempo bits (16 slow, 32 medium,
  64 fast, mask 112; macros.dta) filter rb_* members' clips [C data]; the
  drummer falls back to `sit` (RB's drum type start_group) [C data].
- **Undo:** Band::filterClips, Band::rbPoll.

## D29
- **What (2026-11-10, GD05-030):** RB's per-pixel shaders (ark/xbox_shaders,
  read with a Xenos disassembler as a read-only helper): the light sum is
  tinted by the material colour once (sum * gDiffuse + ambient * gDiffuse)
  and the base variant of every lit + specular shader takes its highlight
  from the first light only (gLight c64 / gLightCol c72; the environ's
  +0x13c bits add terms) [C data]. The port squared the colour and lit the
  highlight from every light: the focused button's orange went tan
  (button_focused.mat: specular 0.5, power 1, under main_buttons.env's
  white second light) and the sub-menu labels grey (colour 0.44 squared).
  Per-vertex lighting keeps GH2's rule [I].
- **Undo:** GD_SPEC_DIR / GD_SPEC_POINT and the per-pixel tint in
  render/standard_shader.cpp.

## D30
- **What (2026-11-10, GD05-030):** RB's BandHighlight (main_solomode's
  highlight.hlit) is drawn: its resource (resource/highlight.milo) at its
  panel's focused component, each mesh at its own place in the resource;
  normal.mat (orange) while its panel holds the UI focus, else
  disabled.mat (0x822FF4F8 / 0x822FF428 / 0x822FEE60) [C; I: the material
  swap by name, the 0.1 s slide not drawn].
- **Undo:** UiView::drawHighlight / setUiFocus.

## D31
- **What (2026-11-10, GD05-053):** the Enhanced Options backdrop (the
  port's own: RB has no such screen, D16) shows one of the venue's own band
  shots instead of the port's hand-made singer close-up (which put the
  camera inside the drum kit): from RB's picker categories NORMAL1111 (the
  far shots of the whole band) then directed_all, the shot whose camera
  (CameraDirector::evaluate at frame 0) shows most of the band's heads in
  the part of the picture the Enhanced boxes leave free (right half, above
  the help box). It plays as in a song (band targets' clips, the director)
  for the backdrop's posing second, then freezes [C: the shots; I: the
  choice].
- **Undo:** App::updateBackdrop's shot block.

## D32
- **What (2026-11-10, GD05-049):** a panel without an environ of its own
  draws in RB's UI environ, ambient (ui (env (ambient 1 1 1)))
  (config/default.dta; UIManager::Init RB 0x82259408 creates the environ
  and sets its ambient from it) [C]. The port fell back to GH2's 0.3, so
  every use_environ font (kabel_bold, avantgarde, bodoni ...) of such a
  panel was a third of its colour preset: the grey labels.
- **Undo:** UiView::defaultEnvironment / setDefaultAmbient.

## D33
- **What (2026-11-10, GD05-052):** a panel exited by a screen change is
  unloaded by RB and loads fresh when it enters again (the panel's screen
  count); the port keeps it loaded, so on entering again it forgets what
  its exit triggers posed and stops their animations [C: the unload; I:
  forgetting only those]. main_panel's ui_exit_back slides its five buttons
  out (solo.tnm .. training.tnm 25 -> 0) with no ui_enter to bring them
  back: after Back to the title and Start, the main menu had no buttons.
- **Undo:** UiManager::enterPanel / exitPanel (exitPosed_).

## D34
- **What (2026-11-10, GD05-050):** in a one-player song every device plays
  for player 1 (gd02's rule) and every device's Start / menu press acts as
  player 1 while the song lives (paused too); the keyboard falls back to
  player 1 when pads hold every enabled slot. A Rock Band guitar left
  plugged in held player 1 and the keyboard's Enter (Start) went to
  player 2: the song could not be paused or resumed from the keyboard.
- **Undo:** App::handleAction's `ev.player = 0`, Input::keyboardSlot's
  fallback.

## D35
- **What (2026-11-11, GD05-056):** D34 extended: in a one-player song a
  device without a player (GAMEPADS' None, or a pad past the enabled
  players) plays it too (Input::setOnePlayerSong). The owner's
  controllers.json had his Rock Band guitar at None beside the keyboard as
  player 1: the song took only its Start, every fret and strum was dropped.
  In multiplayer songs None still means "the menus only".
- **Also:** a script's {$panel button_down b p} runs the panel's
  BUTTON_DOWN_MSG and then UIPanel's native ButtonDown (the title's
  splash_panel turns Start into kPad_X): a pad's or guitar's Start did
  nothing on the title [I].
- **Undo:** Input::actionsOf's `!onePlayerSong_`, PanelObj::handleNative's
  "button_down".

## D36
- **What (2026-11-11, GD05-054):** a RndDir that names an environ draws its
  objects in it (RndDir `environ`, SyncProperty RB 0x821B3438) [C].
  loading.milo's band icons (load_icon_*: intruments.env, a warm white point
  light) were drawn in ornate_leader.grp's dim environ: icons p97 luma
  44 -> 215. LOADING's grey is the label's own custom colour (normal
  0x3b3b3b) under ornate_leader.env [C data].
- **Undo:** UiView::drawObject's inline-directory environ block.

## D37
- **What (2026-11-11, GD05-055):** $world is the attached WorldDir (RB's
  band_objects.dta start_shot finds ProFilm_a.pp / override_blender.pp
  through it; unset, every shot from the band's first on drew with no
  post). BandCamShots without a PostProc keep the selection and blend via
  pp_blend_task (RB 0x8220A8A0 / PostProcBlendTask 0x8220A508). RB
  PostProc bloom is not drawn until RB's alpha-mask bloom is ported (the
  GH2 bright pass misread its fields: white blobs) [I]. The RB track is
  hidden until intro.trig (ui/game.dta on_extend at 0, gem_track.dta
  play_intro +0.55 s solo, 0.25 s x index + 0.05 s in a band), then
  intro_anim.grp's camera_intro.tnm at 30 fps [I fps].
- **Undo:** World attach's $world, postProcState's bloom gate,
  RbTrackView::setIntroAt.

## D38
- **What (2026-11-11, GD05-056 / 057):** in a one-player song the watcher
  follows the device that plays (a gamepad's every fret press strums; a
  guitar's or the keyboard's strum), as D34 / D35 take every device. RB
  has no PlayerMatcher handlers: a miss goes to config/player.dta's player
  handler (miss $player_num $slot) -> miss_gtr.cue / miss_bass /
  miss_kick.cue / miss_drum [C data]; the port only sent GH2's matcher,
  so misses were silent.
- **Undo:** App::playerAction's setControllerMode, gameCallbacks' K::Miss.

## D39
- **What (2026-11-11, GD05-054):** slider resource meshes are drawn through
  their resource parents (slider_audio.milo's slider.grp turns the flat
  quads to the camera; the port drew them edge-on: the audio sliders were
  2-3 px, nearly black). ProfileMgr keeps the global-options pad and a flag
  set by set_global_options_pad (RB 0x823D6280) [C]. `calibrate_*`
  UIScreens stay screens (makeRbMetaClass made them panels). LOADING is
  drawn as RB does (lit Lubalin, custom normal 0x3b3b3b, ornate_leader.env
  ambient 0; RB 0x82213470, 0x822FE7B8, 0x827D6F00, 0x821AACC8): it
  brightens only when loading.tnm's 3 fps glint reaches the word (~4.5 s
  after the screen opens); the port's ~1 s loads never show it. Not
  changed (a port choice for the owner).
- **Undo:** UiView::drawStyleMeshes' slider world, rb_natives
  global_options_pad_set, makeRbMetaClass's screen check.

## D40
- **What (2026-11-11, GD05-045):** the fret board's effects are
  tracksystem.milo's own EventTriggers, played by RbTrackView: the
  gem_smasher.dta / gem_track.dta handlers (hit / hit_bonus, burn /
  stop_burn, miss, combo, set_glow, deploy / stop_deploy) from changes in
  the player's state [I: which native sends each]; particle_trigger
  launches BandMeshLaunchers and part_launcher launches each system once
  [C data; I: counts]. PropAnims are read in full (120/120 in
  tracksystem.milo) [C data]. ParticleSys rev 36 reads grow_ratio before
  shrink_ratio: the other order left the hit flares at size 0 [C data];
  this also changes venue / band particles (their walks pass; shots not
  compared).
- **Undo:** RbTrackView::fxEvents / fxUpdate / drawSmasherFx,
  readParticleSys' order.

## D41
- **What (2026-11-11, GD05-058 / 059):** (1) controllers send while no
  window has the keyboard focus (SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS):
  without it SDL drops every joystick and gamepad event then [C: SDL 3.4
  SDL_PrivateJoystickShouldIgnoreEvent; proven: a window made again without
  focus got 0 events, 2 with the hint]. The owner's session of 11-11 17:35
  (the launcher made the window again for Vulkan) logged no raw event at all
  from his guitar and 0 notes hit; the report now prints the focus state and
  every raw event with what the game did with it (GD05_INPUTLOG=1 logs them;
  `--inputcapture` writes input-capture.txt). (2) Transitions: RB's
  GotoScreen exits the old screen's panels and loads the new screen's as the
  transition starts (UIScreen Exit; a panel's load script every time its
  screen comes up), while the old screen still shows; main.dta's main_panel
  fades the fx panel to black on load and on exit and the new screen fades
  in on TRANSITION_COMPLETE. The port's switch waits for that fade (at most
  2 s; on the 360 the panel loads covered it [I]).
- **Undo:** Input::prepareSdl / load's hint; UiManager::beginTransition's
  early exit / load, updateTransition's fxFadingOut hold.

## D43
- **What (2026-11-11, GD05-054):** a directory's drawables sort as RB's
  (0x821B3A48 / comparator 0x821FF100): by draw order, then the same class
  by name [C]; different classes keep the file's order [I].
  tour_band_welcome's all.grp (blackmask, 85% black) now draws first:
  "Congratulations!" 22 -> 180 (expected 180 from its colour, material,
  lights and the D25 curve; walk rb_text_colors).
- **Undo:** sortDraws in render/ui_view.cpp.

## D44 (Owner decision; replaced by D49 on 2026-11-12)
- **What (2026-11-11):** LOADING reads bright from the moment the loading
  screen shows: drawn at its own focus colour (custom_colors 0xfdfdfd) in
  RB's UI environ (ambient 1). RB draws it at normal 0x3b3b3b under
  ornate_leader.env (ambient 0), lit only while loading.tnm sweeps
  loading.lit (range 35) across the word (keys 0 / 25 / 50, rate 2, from
  enter) [C data]. p97 28 -> 253.
- **Undo:** the loading.lbl block in UiView::drawObject.

## D45
- **What (2026-11-11, GD05-060):** RB's in-song HUD is trackpanel.milo's
  own elements (render/rb_hud_view.h), placed by its track_configuration
  objects (N_player_wide: objects / visibles / xfms) through the panel's
  Cam.cam and driven as their RndDir type scripts do (track/gen/*.dta)
  [C data]; scoreboard values follow the TrackPanel poll 0x82803750 [C];
  the per-track senders are not traced [I]. In a band each gem track draws
  the one-player picture scaled so its now bar sits on its own Overdrive
  meter [I: RB uses one camera with x_offset / side_angle].
- **Undo:** App::rbHud_, RbTrackView rects from RbHudView::trackRects.

## D46
- **What (2026-11-12, GD05-062):** the sub-menu labels are not changed.
  Owner: "the sub-menu letters are grey". Measured against his capture of
  RB (main_solomode_screen, QUICKPLAY focused; 99th percentile luma per
  word box at 1200 x 675): QUICKPLAY focused RB 188 / port 183 (Vulkan in
  his launcher build 191), SOLO TOUR normal RB 102 / port 97 (98); the
  same on the second visit (186 / 96). Their colours are the buttons' own
  custom_colors (normal 0x707070, focused 0xd7d7d7) [C data], lit by
  popup_main_test.env's one point light bottom_right.lit (0.92, range
  750, falloff from 178) [C data]; RB packs the falloff as
  saturate(d * 1/(start - range) + range/(range - start)) (0x827D6F00) [C],
  as the port does. light.tnm (looped by light.trg) names no trans: the
  light stays put in RB too [C data]. Main words: SOLO 207 / 203, MULTI
  215 / 216, TRAINING 222 / 220, OPTIONS 226 / 215, COMMUNITY 224 / 191
  (RB's darker on its left: open, GD05-062).
- **Undo:** nothing to undo; walk rb_text_colors' refcolors:solo /
  refcolors:solo_2nd fail a sub-menu label outside 10% of RB's capture.

## Owner: completeness 36% (2026-11-10)
- **What:** the owner set Rock Band's completeness to 36% (game-info.yaml
  0.36, README, the site) after playing the build; the parity ledger
  (docs/parity.md, 61%) scores rows too generously for what a player sees.
- **How to apply:** never raise game-info.yaml above the owner's figure
  without the owner's OK; report the ledger's figure next to it.

## D47
- **What (2026-11-12, GD05-061):** the guitar's loss inside the launcher is
  located by the controller report, not guessed. Each Linux pad's kernel
  device (SDL_GetGamepadPath, /dev/input/eventN) is opened read-only by
  Input beside SDL and drained each frame; the report prints what the
  controller sent vs SDL's joystick events. An extra evdev reader changes
  nothing for SDL (evdev copies every event to each reader). Device walks
  can drive a kernel device (GD05_WALK_EVDEV) so the launcher's whole path
  (its SDL, its window made again for Vulkan, the module) is tested from
  the kernel up, not from SDL's virtual joystick.
- **Why:** two of the owner's sessions showed 0 SDL events from his
  guitar while every test from the kernel up passes in the same launcher
  build; the next report has to say on which side of the kernel the
  events stop.
- **Undo:** Input::kernel_ / drainKernel / closeKernel and the report's
  Kernel device line; walktest's EvdevOut and dev* helpers (SDL virtual
  setters again).

## D48
- **What (2026-11-12, GD05-063):** a label's text is laid out with the
  font's kerning plus the label's (RB 0x8226E0A0 raises the font's +0x1044
  by the label's kerning around the layout, then puts it back) [C]. A
  UIList with more rows than its display_num draws display_num rows from
  a first row that follows the selection [I: the window moves only as far
  as the selection needs]. The song list's texts are measured against
  RB's sizes by walk rb_songlist_text (5%).
- **Why:** the owner's "wrong sizes for the letters" on the song list: the
  sizes match the data (within 0.3%), the kerning rule was not RB's, and
  the list was stuck on its first seven rows.
- **Undo:** UiView::drawLabel / drawTemplate (kerning), UiView::drawList
  and ListState::first.

## D49 (Owner decision, replaces D44)
- **What (2026-11-12, GD05-065):** LOADING (loading.milo's loading.lbl) is
  drawn at its focus colour (custom_colors 0xfdfdfd) lit as the four band
  icons are: intruments.env's instruments.lit (1 1 0.68, range 500, falloff
  from 200, parented to ornate_leader.grp, the label's own group) moved by
  the word's offset from the icons, plus loading.tnm's sweep light
  loading.lit (range 35) from ornate_leader.env [C data: lights, colours,
  sweep; I: that RB lights the word so].
- **Why:** the owner rejected D44's flat white: in the original the word has
  the icons' colour grading and effects. Lit at its own place (normal
  0x3b3b3b, ornate_leader.env, ambient 0) it is grey (p97 25..44).
  Measured: word luma 194, blue/red 0.69 vs the icons' 172 / 0.68.
- **Undo:** the loading.lbl block in UiView::drawObject.

## D50
- **What (2026-11-12, GD05-064):** each gem track takes the panel's
  track_configuration as track_paneldir.dta's update gives it [C data]:
  rotater.grp pitch, game.cam's y_fov and place (0, y_pos, z_pos),
  track.env's fade and the far plane from the camera to fade_start and
  fade_start + fade_length, top_y = fade_start + fade_length + 5,
  y_per_second = top_y / view_time (view_time_easy .. _expert by the gem
  players' mean difficulty; the GH2 per-difficulty speed is not used). The
  one-player configuration (1_player_wide) for every track; the band's are
  that picture on their meters (D45) [I]. rig.grp is kept at its origin
  (the gems are placed relative to now) [I]; TransAnim repeat_trans goes on
  past the keys, each span adding last - first [C data: the flag; I: form].
- **Why:** the owner's "something on top of where the notes pass": the
  Overdrive meter and multiplier covered 39% of a gem at the now bar; the
  rig stopped at y 100 and the gems crossed the smashers 1.2 s early.
- **Undo:** RbTrackView::configure, App::drawRbTracks' configure block, the
  rig reset in RbTrackView::draw, ScenePose::transAnim's repeat.

## D51
- **What (2026-11-12, GD05-066/067/068):** the band moves with the song as
  Rock Band does [C data: char_objects.dta, midi_parsers.dta; C code
  addresses in char/rb_midi_parser.h and game/rb_band.cpp]. RB's MidiParser
  is its own class (char/rb_midi_parser.cpp: script handlers gem / midi /
  text, add_message, gaps and idles RB 0x824BB8F0 / 0x824BB9D8, inversion,
  poll RB 0x824B8210 sends every due event). Each member hooks its
  CharDriverMidi drivers (fret, strum, string_hi / _low, the drummer's nine),
  fret.ikmidi (new_spot), the drummer_play / guitar_lookat / singalong
  setters and itself (guitarist_parser ... play modes) to the parsers
  world_configure names (guitar_* or bass_*). A driver's clip starts
  `length` before its first frame and blends in over it (RB 0x82342168);
  the main clip is the start group's FindClip with the play, attitude and
  instrument flags (RB 0x822E5908 / 0x822E4A80 / 0x8231BF10); the drummer
  plays with main_add.drv while the mode is mellow .. solo. [I]: a new
  clip of the group as the playing one ends; a group named by midi_parser
  plays its next clip; the CharDriverMidi +0x7c flag is taken as true.
- **Why:** the owner: hands wrong, drummer still, lips still. Nothing of
  this ran (the GH2 parsers had other names and form; no hand clip ever played).
- **Undo:** Band::rbLoadSong / rbEnter / rbChangeGroup / rbPlayGroup /
  rbDriverClip and RbPart (game/rb_band.cpp); GD05_BAND_LEGACY=1 shows the
  band without them.

## D52
- **What (2026-11-12, GD05-066/067):** RB's rig where it differs from GH2's
  [C]: CharWeightSetter base = offset + scale x the driver's flag weight,
  followed at 1 / beats_per_weight per beat (RB 0x8234A320; GH2's was the
  flag weight itself: the hand IK came on exactly when the main clip wanted
  the arm free); CharIKHand with several targets weighs each 144 / its
  squared local offset, the IK's weight times the sum when under 1 (RB
  0x8232EB48; GH2 averaged them by 1 / offset at full weight: hands pulled
  between hip and mouth); CharPollGroup hand_iks orders the hand IKs (strum
  after right_hand) [I: only its IK hands swap places]; CharForeTwist puts
  twist2 at hand x x length_scale (RB 0x8233DE58; GH2 halfway); RndGroup
  draw_only (hands.grp -> mic.grp, sticks.grp -> singlestick.grp).
- **Why:** measured (GD05_BANDCHECK): pick to strum target p95 18.97 ->
  0.00 units; arm bones up to 2.98 x bind -> 1.25; mic hand drew every prop.
- **Undo:** Rig::Impl::pollSetter / pollHand / pollFore, the poll group
  block after sort(), Character::drawOnly.

## D53
- **What (2026-11-12, GD05-068):** the RB face: char/singer.fac (from the
  milo's source folder), the viseme set (the head outfit's `visemes`, else
  char/male/anim/male_viseme_head_1.milo), the graph's bone pose nodes that
  are clips of it; each poll the face is Base plus weight x (viseme -
  Base), times the servo's weight (expression.weight / singalong.weight)
  [C data; I: the delta blend and q = (qi qb^-1)^w q]. The singer plays the
  song's .voc, the others the blink track [I: the guitar type's song_owner
  and the 0.05 s song_offset are not followed; expression.drv's faces not
  played].
- **Why:** the singer's lips never moved (the .fac was looked up under
  char/male/, and RB has no `visemes` clip of frames as GH2).
- **Undo:** Character::setRbFace and pollFace's RB branch, Band::setupFace's
  RB branch.

## D54
- **What (2026-11-12, GD05-069):** the world's post-process is Rock Band's
  NgPostProc (DoPost RB 0x827DA600, ark/xbox_shaders "postprocess" /
  "bloom" / "downsample" / "blur", read with gd04's Xenos translator as a
  read-only helper) [C]: the film grain is an overlay blend of two
  noise_map layers, mix(c, overlay(c, n0 * n1), noise_intensity)
  (0x827D9190: c112 phase, c113 scale / top / intensity; stationary: fixed
  phase); the ColorXfm's 3x3 times the flicker (0x827D93D8; 0x8218E948:
  1 - smoothstep towards random(intensity) every random(secs_range) s);
  bloom when intensity > 0 and not (threshold >= 4 and alpha <= 0)
  (0x827D94D0): rgb x sat((bloom_color . rgb - threshold) / (4 - threshold))
  (gBloomColor 0x827D8C18), two 2x downsamples, a 15-tap blur at
  (i + 0.5) x bloom_size / 7 of 1/180 / 1/320 (0x827D8970), added x
  bloom_intensity. [I]: the mask from the colour (RB's scene shaders write
  it in alpha), the blur's centre tap and normalisation, the world clamped
  at 1 before the composite (RB's buffer is 8-bit with the mask in alpha).
  PostProc rev 18 fields named from SyncProperty 0x8218FC00.
- **Why:** the port added the noise (n0 * n1 * 0.3: +0.075 on every pixel,
  white noise, no ProFilm noise_mono.tex) and drew no flicker: brighter,
  flat, no grain (owner: "darker with a stronger film grain").
- **Undo:** render/post_fx.cpp (kFragment's composite / bright passes,
  end()), postProcState.

## D55
- **What (2026-11-12, GD05-069):** Rnd paces the world by the selected
  PostProc's emulate_fps (RB 0x8218EF80 / 0x8218EEA0, Rnd 0x821876F0 sets
  the frame's mode; DxRnd begins the world on bit 1 (0x8246F358), resolves
  and post-processes it on bit 2 (0x8246DAB0)) [C]: 120 / fps half frames
  rounded, n = that / 2 frames at 60 Hz, alternating n + r and n (r the
  remainder); the world is drawn in the period's first frame and the last
  picture is shown in the others. ProFilm_a / ProFilm_b (every band shot)
  ask for 24: the venue and the band at 24 a second in a 3:2 pattern; the
  track and HUD at the display's rate; the default PostProc's 30
  (constructor 0x8218F3E8). The port counts 60 Hz ticks of the clock [I]
  (any display rate); GD05_WORLD_FPS overrides (0: every frame).
- **Why:** the owner saw the band update at a different rate from the
  highway: it is RB's film look.
- **Undo:** PostFx::hold and its call in App::drawWorld.

## D56
- **What (2026-11-12, GD05-069):** a TexRenderer's draw and its directory
  are left out of the venue's draw list (as the UI's RenderDir already
  did) [I: RB's RndTexRenderer draws its directory into its texture];
  small_club_02's tattoos_a / vehicles RndDirs drew their white paisley
  over the whole venue. RB's NgMat select (0x827D4750) sets gAmbient (c1)
  to the environ's ambient for a use_environ material, else white, and the
  particle vertex shader multiplies each particle's colour by it [C]; the
  particle draw (0x82476498) scales gParticleUp by screen_aspect [C].
- **Undo:** venue_view.cpp drawList, material.cpp standardKey's particles,
  particles.cpp appendQuads.

## D57
- **What (2026-11-12, GD05-069):** pause: pause.milo's black_bg.mesh
  (black_bg.mat: black, alpha 0.7, kBlendSrcAlpha, the whole 853 x 480
  screen) is the original's rule for the game behind the pause menu: 0.3
  of its luma [C data]. The port already draws it (measured 0.28..0.30);
  the pause looked bright because the world under it was.
- **Undo:** none (a check: rb_look's look:pause).

## D58
- **What (2026-11-12, GD05-071):** Rock Band's crowd animates as its
  scripts say [C data: world/crowd.dta, band_objects.dta BandCamShot;
  C code: CharDriver play_group_flags RB 0x82313140, game panel start RB
  0x8242E520 -> game.dta pick_intro -> {handle ($this intro_start)}]:
  `main.drv play_group_flags <group> <flags> [<play flags>]` plays the
  group's clip holding every flag (FindClip RB 0x8231BF10; the song's
  anim_tempo 16 / 32 / 64, song_mgr anim_tempo) with the play flags; the
  crowd hears `intro_start` when the port sends intro_start_msg [I: the
  moment]; a BandCamShot's start runs the type's crowd part itself
  (override_anim [crowd_anim], crowd_update, rotate [crowd_face_camera],
  set_fullness [crowd_density], defaults '' FALSE 1) because the world's
  scripts hold no BandCamShot type.
- **Why:** no crowd clip ever played (play_group_flags was a silent stub,
  intro_start never came, crowd_update never ran): the archetypes kept
  their bind pose, lying on the floor (head 27 units along x from the
  pelvis at the same height), their cards empty: 0 crowd pixels.
- **Undo:** Crowd's driver play_group_flags, Crowd::listen's intro_start,
  Crowd::startShot's RB block; GD05_CROWD_LEGACY=1 runs without them.
- **Open:** the theaters' WorldCrowds are hidden in the data (Draw showing
  FALSE in theater_char.milo; the arenas' and clubs' TRUE); what shows
  them in RB is not traced: no crowd pixels in the nine theaters yet.

## D59
- **What (2026-11-12, GD05-070):** an EventTrigger's anim entry runs at
  its own rate [C data: macros.dta RATE_ENUM]: k30_fps / _ui / _tutorial
  30 a second, k480_fpb 480 a beat, k1_fpb one a beat, on the song's tempo
  (render/anim_rate.h; 120 a minute before the song). The HUD and the
  track's effects took every rate as 30 a second.
- **Why:** the owner's flickering Overdrive bar: be_ready.trig loops
  be_color_pulse.anim 0..1 at k1_fpb (one pulse a beat); at 30 a second it
  looped 30 times a second (measured: 101 back-and-forth jumps in 240
  frames -> 0). star_deploy.trig's track_streak_glow.mnm (1000..1480 at
  k480_fpb) took 16 s a loop instead of a beat.
- **Undo:** RbHudView / RbTrackView tasks' `rate`; GD05_HWY_LEGACY=1.

## D60
- **What (2026-11-12, GD05-070):** the HUD's directories draw their draw
  list as RB's RndDir sorts it (RB 0x821B3A48 / 0x821FF100: by draw order,
  then same-class objects by name) [C]: render/draw_order.h, shared with
  the UI (ui_view.cpp's sortDraws moved there).
- **Why:** the owner's "multiplier drawn on top": streak_meter.milo's
  multi_meter_anim.grp (order 1: the "Nx" tag) drew after streak_meter.grp
  (order 2: the dial, its black circle and glass) in the file's order; RB
  draws the dial over the tag.
- **Undo:** RbHudView::drawDir; GD05_HWY_LEGACY=1.

## D61
- **What (2026-11-12, GD05-070):** the track draws static.grp in its own
  order with the view's parts at their places [C data]: beat_markers.grp
  (the bar widgets), now_bar.grp (the smasher plate after smasher_plate.dta
  setup_draworder: smasher_plate.mesh, every smasher's base_bottom.grp
  then every base_top.grp; gem_smashers.grp and after_hide.grp hidden),
  widgets.grp (tails, then gems [I: their order]), smasher_fx.grp (every
  smasher's effects.grp, gem_track.dta track_reset). The plate draws with
  now_bar.grp's now_bar.env. A star (Overdrive / unison) tail takes
  tail_bonus.mat (track_flash_style.tex) [C: Tail SetType RB 0x8281BEC8,
  TailBuilder RB 0x8281A970's material table].
- **Why:** the beat lines drew over the rails and the now bar; each
  smasher drew whole (bottom, top, effects) in gem_smashers.grp's order
  (2 3 1 0 4), its effects under the gems and the next smasher, then the
  effects again after the gems (5 of 11 parts out of the data's order). Star
  tails kept their slot's coloured texture (chroma 100% -> 2%).
- **Undo:** RbTrackView::drawPart / drawSmasherPlate, drawTails' material;
  GD05_HWY_LEGACY=1.

## D62
- **What (2026-11-12, GD05-073):** loads and the frame: milo blocks
  inflate with a table-driven decoder (scene/inflate.cpp, the same bytes:
  0 differences over the ark's 13,236 blocks, 2.37 GB) and side by side
  (each block is its own deflate stream); version 11 moggs decipher in
  parallel ranges (CTR: block i's counter is the nonce + i); the menus'
  music streams (preview, meta loop, credits) open on a thread
  (App's MusicLoader: one at a time, the latest request played); the
  world's, track's and HUD's textures and meshes go to the GPU at the end of
  the song's load (App::warmWorld), not on the frame that first draws them.
- **Why:** the owner's "loads take too long" and the stutters at a preview
  and a song's start: boot 3.3 s to the title (the band's clip milos, 2.5
  s), 2.3 s frozen on the loading tip, a 100..250 ms frame at every
  preview (the mogg read, deciphered and opened on the main thread), 23
  texture / mesh uploads in the song's first 12 s. RB streams previews and
  has its textures in memory once a milo is loaded [I].
- **Undo:** GD05_MUSIC_LEGACY=1 (previews on the frame), GD05_WARM_LEGACY=1
  (uploads on first draw); the inflater / ciphers have no switch (the walks
  and the selftest check their output).

## D63
- **What (2026-11-12, GD05-073):** the song list lays its rows out as RB's
  UIList (0x82271C58): row i at i x spacing plus the gaps of the rows above
  it, each row's gap from its provider (vfunc +0x24; RB's SongProvider
  0x82400180: band.dta's song_select_gap, 30) [C]; the highlight at the
  selected row's place (0x82271F80) [C]. The provider's columns (0x82400A68)
  [C]: details.lbl the artist (random_song: random_song_by_line), the
  headings "<group> (<n> Songs)" (song_select_song / song_select_songs).
- **Why:** list_song.milo's highlight.mesh is 65 units (the name and the
  details line) and the rows were 33 apart: the yellow bar covered the next
  row too (the owner's "highlights more than one song"); 63 apart it covers
  one.
- **Undo:** UiView::ListRow::gap; GD05_SONGLIST_LEGACY=1.

## D64
- **What (2026-11-12, GD05-073):** the song lists of every mode but
  leaderboards hold only the songs RB's SongMgr makes available
  (0x82432C60 -> 0x82431AC8) [C]: downloaded (none on the disc), all
  unlocked (profile_mgr unlock_all_songs: long_cheats.dta's code), or one of
  the mode's unlock_song_initial_num (modes.dta: 10) first songs by the rank
  of its unlock_song_instrument (qp_solo: the leader's instrument; band
  quick play: band) among the songs that are not (unlockable TRUE)
  (0x82434020's ranked lists) [C]. The tours' unlocks (a song played in a
  tour, 0x823D43D0; the tours' progress per instrument, 0x823D4738) wait
  for the career (GD05-077) [I].
- **Why:** quick play listed all 58 songs, and Random Song picked locked
  ones (247 of 300 picks) (owner, G06 6).
- **Undo:** RbSongProvider::availableSongs; GD05_SONGLIST_LEGACY=1.

## D65
- **What (2026-11-12, GD05-073):** Overdrive: an Xbox guitar's tilt is bound
  one way (AxisPos on the right stick Y) and a captured tilt takes its
  direction, not either way from the rest; a meter set from outside (a
  cheat, a walk) under ready_level is not ready; the autoplay drummer strikes
  a pad every 150 ms in a shown fill [test aid]; player.dta's enable_solos /
  disable_solos are accepted (RbBonus shows a fill only when ready as it
  starts, the same gate) [I: the 2400 ms lead].
- **Why:** with the rest read before the guitar's first report (0, the
  guitar level at -32768) the tilt read as held for good and never gave a
  press: tilt never deployed (rb_od_guitar failed). The drummer's crash
  needs crash_fill_ready_hits strikes in the fill (RB 0x824C1530) [C]; the
  autoplay hit the crash alone.
- **Undo:** Input::defaults' Gyro, the capture's Kind; App's fill strikes.

## D66
- **What (2026-11-12, GD05-073):** the HUD's crowd meter gets crowd_meter.dta's
  deploy / stop_deploy (band_energy_deploy.trig with the first member
  deploying, band_energy_stop.trig with the last stopping, the instrument
  icon's deploy.trig / stop_deploy.trig) and set_player_icon_state failed /
  normal (state_failed.trig / state_normal.trig) [C data].
- **Why:** the visual FX pass (G06 6): the Overdrive activation showed on the
  track and the meter but not on the band's crowd meter. The other effects
  the data has and the port does not draw are backlog GD05-078 / GD05-079.
- **Undo:** RbHudView's crowd_meter block.

## D67
- **What (2026-11-12, GD05-072):** a tour event's reward is the event's
  reward_per_star times each song's stars, summed (TourPerformer reward
  RB 0x823EEAE8 over the stars vector RB 0x8298F7D0 that complete_song RB
  0x823D0640 appends to) [C]. The Band World Tour's complete_event (RB
  0x823EF0F0) ends by paying it (vtable +0x74, RB 0x823EE4A0): every local
  member's character gets the whole reward; a band's money is its members'
  characters' money summed (RB 0x823EDAA0) [C]. Money stops at
  config/tour_solo.dta's max_money [C data]. The band lobby stand-in (D10)
  gives each joining player the profile's first character of the
  player's instrument not taken yet (else the premade one, not a member)
  [I: RB's lobby has the player pick or make one].
- **Why:** the band paid nothing to its members (the band kept its own
  money, 0 in the shop) and the lobby put premade characters on stage; the
  solo reward counted only the last song's stars.
- **Undo:** RbMeta::reward / pay / localMembers, RbBandObj money, the
  lobby redirect's character pick.

## D68
- **What (2026-11-12, GD05-072):** the creator works through its own
  scripts (ui/charcreator.dta) [C data]: the setters take the character
  shown first ({$tour_char set_skin_color $char <pos>}: the value is the
  last argument); skin.lst is a BandSwatch (Load RB 0x822D27F0: rev 1
  adds a palette name, then the UIList rev 9) [C] whose set_colors
  {$char get skin_color_palette} gives a row per colour of
  char/gen/skin.milo's skin.pal (eyes.pal for the eyes) [I: the palette
  file]; attitude.lst's set_data (<sym> ...) is its rows; the attitude is
  the kGenre* value (macros.dta: the scripts compare numbers) and the
  character on stage plays with it (its play flags). The OutfitPanel gives
  catalog providers only to the catalog's categories (not skin.lst).
- **Why:** the skin list had no rows (BandSwatch unknown, set_colors a
  stub, the outfit panel replaced its provider), the attitude list was
  empty, and every choice stored the character object instead of the
  value (skin 0, attitude the object).
- **Undo:** ComponentObj set_colors / set_data / store / undo / confirm,
  readBandSwatch, RbCharObj get / setters, OutfitPanelObj
  isOutfitCategory.

## D69
- **What (2026-11-12, GD05-072):** the Rock Shop's instruments are
  char/instruments.dta's models (groups (<model> (cost n) (default_outfit
  <finish>))): the list holds the models (a model without a cost only
  when owned), the price is the model's (OutfitProvider RB 0x82859E08,
  cost RB 0x828587E0) [C]; a model bought is worn in its default finish
  [I: RB's finish choice (group_select_screen) not offered:
  has_group_selection 0]. rb_profile.json keeps the creator's eye /
  eyebrow / weight / height / attitude, the solo character's town, venue
  and seen towns, the band's members, motto, venue and difficulty (new
  keys, read with defaults). While a hint's (advance ...) runs, {tour
  hint} stays that hint (ONEOFF_HINT's add_seen_hint).
- **Why:** guitars cost nothing (the outfits' cost was read, the models
  hold it); the save lost the look and the tour's place; the ONEOFF
  advance recomputed the hint and marked "0" (or the next hint, unseen)
  as seen.
- **Undo:** OutfitList groups / wear, RbMeta save / load, RbTourObj hint.

## D70
- **What (2026-11-12, GD05-072):** the career walks check a restart:
  restart_app asks the App to start again (Enhanced's Restart Now path,
  runApp), the walk goes on as <walk>_restarted with the save read back;
  GD05_WALK_KEEP_GOING=1 prints a failed check and goes on (also through
  the restart) to run every check of a walk against an older build.
- **Undo:** the walks' restart_app check; the fail lambda's keepGoing.

## D71
- **What (2026-11-12, GD05-074):** gd05 runs only on gd-engine-core (owner,
  G06 7): every draw through `CoreDevice` on the core's render backends,
  **Vulkan by default**, then SDL3, then OpenGL3 when one cannot start (each
  with its window made again for its API; render/core_backend.cpp
  backendsToTry); every sound through a core output, **miniaudio by
  default**, then the core's other backends (audio/audio_device.cpp). gd05's
  own OpenGL device ("local": gl_device.cpp, gl_api, the EGL HDR surface
  hdr_output) and its SDL audio device path are deleted; True HDR is the
  Vulkan backend's HDR swapchain with local's conversion shader (CoreDevice
  applyOutput); the XDVDFS reader, RandomFile, the pref folder and the CPU
  BCn decoder are the core's. A saved "local" is dropped (the default takes
  its place). Development tools and the launcher's model explorer draw on
  the core's OpenGL3. Inventory and what remains: docs/backend.md.
- **Why:** Rock Band is the scaffolding for later games, so it must show a
  game built on the shared parts alone; the engine-backend-guidelines'
  "local is always the default" (rule 2) is replaced for gd05 by a fallback
  through the core's own backends (never a black screen, never no sound
  while any core backend starts). The other games keep local by default.
- **Measured:** fixed-clock song shots (docs/backend.md "Checks"): Vulkan
  now = Vulkan before (0 px); core OpenGL3 now = local before (0 px);
  Vulkan vs the old default local: PROGRESS.md 2026-11-12 GD05-074.
- **Undo:** core_backend.cpp kOrder / kDefaultBackend and
  audiodev::kDefaultBackend choose the defaults; local itself is in git
  history (before this decision's commit).

## D72
- **What (2026-11-12, GD05-086):** the in-song HUD (render/rb_hud_view.h)
  keeps no tempo pointer across songs: `RbHudView::reset(introAtMs, tempo)`
  takes the new song's TempoMap, and `draw` sets it before its own reset;
  RbTrackView takes its song's before its effects' reset. The reset runs
  the panel's reset anims (runTasks(0)) at their beat rates; on a retry
  (rb_lose's) it ran them on the last song's TempoMap, freed by the new
  load: a segfault in TempoMap::msToTick when the freed memory had been
  reused (core of the 11-12 gate: RbHudView::draw -> reset -> runTasks ->
  animFrames -> msToTick). Not a thread race: MusicLoader's reads go
  through the core's RandomAccessFile (pread or a lock).
- **Measured:** rb_lose 9 times, 6 at a time, MALLOC_PERTURB_=165: 5291eb3
  3 segfaults (same stack), fixed 0 (gate: lose_stress_1..9).
- **Undo:** the `tempo` argument of RbHudView::reset.

## D73
- **What (2026-11-12, GD05-087):** an in-song HUD element's expected box
  (RbHudView::expectedBoxes, the walks' rb_hud_box_err_px and the band
  layout's meterBox) is its meshes' without its glows: additive meshes
  (blend Add / SrcAlphaAdd), whose textures fade out before their quads'
  edges, only bound how far its pixels may reach (Box::ox0..oy1). The
  Overdrive meter's player_meter_sun*.mesh (be_ready.trig's sunburst.anim,
  alpha pulsing) reached 30 px past the meter's background: whenever a
  member's Overdrive was ready at the probe the box check failed by 17..33
  px (band_quickplay: Wave of Mutilation's guitar was ready at 15 s).
  band_quickplay now sets the first member's meter to 60 % before the probe.
- **Undo:** Box::ox0..oy1 and the `side` rule in App's HUD probe.

## D74
- **What (2026-11-12, GD05-066 / 067):** CharIKHand's elbow cosine is
  clamped to [-1, 1] as Rock Band's (Poll RB 0x8232EB48 [C]: DAT_820071d8
  -1.0, DAT_8200097c 1.0); the [-0.985, 0.88] was GH2's. Every band IK hand
  has stretch TRUE (RB moves the hand onto the goal after the elbow), so
  an arm or leg that could not straighten stretched its hand / ankle bone.
- **Measured (GD05_BANDCHECK, inbloom 60 s):** guitar L-hand 1.243 x bind
  (11 frames past 1.2) -> 1.11..1.12 (0); bass 1.39 (103) -> 1.16..1.19 (0);
  drummer's L-ankle 1.208 (~27 % of frames) -> 1.153 (0).
- **Not fixed:** at a high frame rate (~14,000 frames in 60 s) the drummer's
  left_hand.ikhand targets (bone_L-tip_<pad>, children of the kit's
  bone_target_*) drift to 6e4..3e5 units and keep growing, and the hand's
  local position leaves the forearm (1.5..254 x bind, 2 of 5 runs): a
  channel the add driver (CharDriver apply != 0, skipScaleDown) adds into
  every frame while no blend driver scales it down [I]. Scaling the add
  drivers down too only reduced it (254 frames past 1.2). RB's CharDriver
  Poll for apply add has to be read (GD05-088).
- **Undo:** the clamp in Rig::Impl::pollHand.

## D75
- **What (2026-11-12, GD05-085):** the Solo Tour's rules are RB's
  TourCharLocal (vtable 0x8206C184) [C]: an event's (songs N) is the Nth
  song of SongMgr's ranked list for the performer's instrument, (unlock N)
  of its bonus list ((unlockable TRUE)), (<inst> N) / (band N) of that
  instrument's, N < 0 from the end, clamped (RB 0x823FE820; lists RB
  0x82434020, `rankedSongs`); a venue's open events are those with N up to
  max_song of the difficulty (Easy 30) and (unlock N) up to the bonus songs
  unlocked (RB 0x823E46B0); the open towns (vfunc +0x48, RB 0x823E5BB8) go
  in the data's order while every open event of the venues before was
  completed at the difficulty or a harder one (RB 0x823DE560), the bonus
  venue whenever it has open events; event_status is kTourPlayed /
  kTourNew (RB 0x823DBF80), kTourLocked for what the lists leave out;
  is_venue_type_unlocked is an open town's normal venue of that kind (RB
  0x823E0010). Each difficulty keeps the bonus songs unlocked (from
  num_initial_unlock_songs, RB 0x823E54E8), one more for each event first
  completed whose song is a bonus song (complete_event RB 0x823E1178);
  new_unlock is the newest one not yet announced (RB 0x823DE7F8).
  Band World Tour: a gig's buzz is (buzz (venues (<venue> (performance
  ...)))) at row (the event's stars before, new_performance for a first
  gig) - 1, column max(average stars, 2) - 2 (RB 0x823EF7D0) [C]; fans are
  the line through (fans ...)'s points around the buzz (RB 0x823EE350)
  [C]; a failed song costs lose_penalty [C data]. [I]: the row's "stars
  before" (RB reads the band's event record +0x14), the caps' order
  (buzz_caps: none, van, bus, jet), when lose_song applies, new_town (the
  town the event opened), the random song pick, the band towns' own events
  (tour/<town>.dta, RB 0x823FE1D8's second set) still not loaded.
- **What (GD05-077):** quick play offers, besides modes.dta's first songs,
  the songs a band of the profile played (RB 0x823D43D0) and, per
  instrument (guitar, vocals, drum), the first P of its ranked list (bonus
  list for a bonus song), P the most over the profile's characters of the
  instrument (RB 0x823D4738) of: the song number of the last event of the
  first venue of the last open normal town, over the difficulties, and
  for bonus songs the bonus songs unlocked (RB 0x823E0458) [C].
- **Why:** song order, unlocks, fans and buzz were wave 02's guesses
  (sequential events, the band's rank for every instrument, (unlock N) by
  an unranked list, fans = 10 x buzz); quick play ignored the tours.
- **Measured:** walk rb_tour_rules (PROGRESS.md 2026-11-12 GD05-085).
- **Undo:** RbMeta::resolveSong / soloOpenEvents / soloOpenTowns /
  soloQuickplayProgress / buzzChange / buzzToFans, rbTourUnlocks;
  GD05_TOUR_UNLOCKS_LEGACY=1 (quick play without the tours).

## D76
- **What (2026-11-12, GD05-083):** has_group_selection is RB's (RB
  0x823E0700) [C]: more than one of the category's (outfits ...) contains
  the model's name (strstr), so a guitar / bass / drum / mic model with
  finishes goes through group_select_screen. OutfitGroupPanel
  (group_select_panel) lists those outfits in outfit.lst [C: the match; I:
  the catalog's order, RB's 0x8282AD38 not read]; the finish chosen with X
  is what the outfit panel's confirm_outfit wears (bought through
  shop_confirm_screen at the model's price, or worn when owned). The
  group panel's SELECT_MSG buys on {&& {! has_outfit} $cost}: RB's script
  reads the $cost the outfit panel's handlers left, the port's do restores
  its locals, so the panel sets $cost to the model's price before its
  SELECT_MSG [I: RB's do semantics].
- **Why:** every model was bought in its default finish
  (has_group_selection 0, D69).
- **Measured:** rb_shop_items: 5120's six finishes listed as the data's,
  the second (5120_sparkle) bought at 1299, worn on stage, kept through a
  restart (PROGRESS.md 2026-11-12 GD05-083).
- **Undo:** OutfitGroupPanelObj, modelFinishes, gChosenFinish.

## D77
- **What (2026-11-12, GD05-076):** a camera shot shows its own crowd and
  applies its hide and show lists as Rock Band's CamShot StartAnim (RB
  0x822904B0) [C]: after start_shot, when the shot's directory is a
  WorldDir (dynamic cast to `WorldDir`, RTTI 0x82886CDC), 0x82286E50 sets
  every WorldCrowd of the world (WorldDir +0x29C, gathered through its
  proxies by 0x82288188, class `WorldCrowd` RTTI 0x82886CA8) showing only
  when it is the shot's `crowd` (none: all hidden); then 0x8228F608 hides
  the hide list's showing objects and shows the show list's hidden ones,
  remembering only what it changed, and EndAnim (0x822906A8) sets exactly
  those back (0x8228F6C0) before stop_shot. The theaters' WorldCrowds are
  showing FALSE in theater_char.milo: their shots' crowd shows them.
  render: CameraDirector::showShotCrowd / applyShowLists / restoreShowLists.
- **Why:** rb_crowd drew 0 audience characters in all nine theaters (the
  port hid list ran, the show list was read and dropped, and nothing showed
  the shot's crowd); the old end of a shot also showed hidden objects that
  had been hidden before it.
- **Measured (crowd sweep, 41 venues, #crowd0, 9 s):** theater_01..09 0 ->
  800 characters drawn (714k..732k px); arenas 700 / 700, small clubs
  70 / 70, big clubs 350 -> 200 (only the shot's crowd now, as RB; px
  1.10M -> 1.01M..1.06M).
- **Not done:** the crowd's rotate (+0x48 of the shot's crowd set from the
  shot's crowd_rotate by 0x82286E50) is still the script's.
- **Undo:** the three CameraDirector functions.

## D78
- **What (2026-11-12, GD05-088):** Rock Band's CharDriver apply modes and
  CharBonesBlender [C]: CharDriver Poll (RB 0x82313988) with apply 0
  (blend) scales the newest clip's channels down (CharClipSamples slot 6,
  RB 0x8234EB08) and adds the chain (0x823613C8), apply 1 (add) adds the
  chain without scaling down, apply 2 (rotate to: main_add.drv,
  zBassOffset.drv) runs 0x82361490 -> slot 8 (0x8234EC10 -> 0x823647F0 ->
  0x8231F790): positions and angles += a x sample, quaternions
  q = q x (a s.xyz, a s.w +- (1 - a)) for the two samples around the frame.
  A driver writes into its `bones`: the drummer's hand and foot dmidi
  drivers (left_hand / right_hand.dmidi apply 1, left_foot / right_foot)
  write limbs.blender, a CharBonesBlender whose Poll (0x8234D520) blends
  its channels into its dest (0x8231F1C8: (1 - w) d + s for positions,
  sign-aligned (1 - w) d + w s for quaternions, (1 - w) d + w s for
  angles; w the channel's weight) and then zeroes itself (0x8231E2F8,
  0x8231E9A8(0)); it polls after the drivers writing it (its PollDeps
  0x8234DE60 changes its dest). The [I] scale-down of older clips' channels
  (GD05-067, Bones::scaleDownOthers) is gone: RB scales down the newest
  clip's only.
- **Why:** the hand drivers added into the servo every frame and nothing
  zeroed those channels: the kit targets (bone_L-tip_<pad>.pos) summed to
  6e4..3e5 units (weight 1 a frame), the left hand left the forearm on
  high frame-rate runs.
- **Measured (GD05_BANDCHECK, inbloom 60 s; new check: every position
  channel within the sum of its drivers' largest clip values x 1.01):**
  GD05_BLENDER_LEGACY=1 (the code before): drummer channels 1735.7 x
  (bone_L-tip_R-cymbal.pos), 2839 of 2901 frames past; now 1.000 x, 0
  frames, in 10 of 10 runs of 8,744..14,209 frames, drummer stretch worst
  1.001 x bind (was 1.153..254).
- **Undo:** GD05_BLENDER_LEGACY=1 (no blenders, rotate-to drivers added).

## D79
- **What (2026-11-12, GD05-069):** a SpotlightDrawer's proxy is never
  drawn [C]. SpotlightDrawer (rev 4, Load RB 0x82299660: u32 4, Draw,
  total, base_intensity, smoke_intensity, an unnamed f32 (250 in every
  venue), color, then two object pointers; SyncProperty 0x82298FC8 names
  only the first three) keeps the second pointer at +0x5c; its
  ListDrawChildren (RndDrawable vtable +0x14, 0x82299DD0) pushes that
  object, so RndDir SyncObjects (0x821B3A48: every drawable of the
  directory and its sub-directories, then minus every drawable's
  ListDrawChildren, then sorted by 0x821FF100) leaves it out of the
  directory's draw list; the drawer's own DrawShowing (vtable +0x10) is
  0x824A3138 (`blr`) in SpotlightDrawer and NgSpotlightDrawer, and
  nothing else reads +0x5c (the Ng drawer's EndWorld 0x822AA340 draws the
  queued beams, 0x822A9CC8 / 0x8229BB38). On the disc every venue type's
  lighting milo has one drawer whose proxy is its stage fog / smoke:
  small_club_lighting and arena_lighting stage_fog.grp, big_club_lighting
  and theater_lighting stage_smoke.grp. render/venue_view.cpp drawList
  reads the record and drops the proxy.
- **Why:** those groups' additive fog (stage_fog02 / 03 and the others:
  100..250-unit quads, black -> 0.65..0.88 grey -> black, 16..31 on screen)
  were drawn over every shot: the "far too bright" venues (luma up to 2.7
  times the movies').
- **Measured (rb_look, 30 fixed shots, GD05_SD_PROXY_LEGACY=1 -> now,
  same build; movies: luma 69.3, contrast 55.4, saturation 0.397):**
  small_club_02 luma 131.7 -> 73.0, contrast 79.0 -> 68.8, saturation
  0.152 -> 0.249; big_club_03 184.2 -> 52.4, 65.7 -> 61.7, 0.086 ->
  0.309; arena_04 149.0 -> 81.0, 79.7 -> 78.5, 0.126 -> 0.191;
  theater_02 65.9 -> 42.6, 69.7 -> 61.6, 0.270 -> 0.323. rb_look now
  checks the venue's drawer proxies are out of the draw list (1 found,
  0 listed; legacy: 1 listed, FAILED).
- **Not done:** the drawer's total / base_intensity / smoke_intensity /
  color (and the presets' drawer entries, 3 f32 per keyframe) are not
  used: the Ng beam pass sets c82 = (base x 0.01, smoke x 0.01 x
  (1 - base x 0.01)) (0x822A8E78) [meaning I].
- **Undo:** GD05_SD_PROXY_LEGACY=1.

## D80
- **What (2026-11-12, GD05-082):** the band lobby (matchmaking_band_screen)
  runs its own scripts (ui/gen/matchmaking_band.dta, matchmaking.dta) [C
  data]. A MultiFocusScreen gets one slot per (slots COOP_SLOTS_ORDER) entry
  (guitar drum vocals guitar), each the PanelDir of its own directory
  (slot0..3: its own focus, list, states; UiView keys a slot's components
  "<slot>/<name>"); a slot's player_num is its type's command, evaluated as
  the slot; a button goes to the slot whose player_num is the player's,
  else to the first free slot whose allow_button_msg takes it (RB
  0x8282EA48) [C], its handlers then its own focus, the screen's last; a
  slot's update first runs its new state's init when current_option
  changed (RB 0x8282E5E0) [C]; all_ready = every slot's is_ready and the
  screen's (RB 0x8282DE20) [C]. session_mgr add_local_player puts the
  player in the session, the first one leads unless the mode is the Band
  World Tour (RB 0x82832788) [C], its new_local_player sinks hear it at once
  [I: RB's session completes asynchronously]; update_band_leader acts only
  in the Band World Tour with choose_leader_from_band (RB 0x82831238) [C]:
  a known band's leader is the player playing its first member [I], else
  the leader stays, else the first player [I: 0x82616DD8]; remove_local_player
  hands the lead to the first player left [I]. get_char_provider <player>:
  four lists (RB 0x828572F0, CharProvider 0x82857878) [C]: (Create New)
  with the mode's allow_create_char, (Default) with its show_prefab_chars,
  then the profile's characters of the player's instrument (RB 0x82857660 /
  0x82855BD0) not taken by another player [I: 0x82421B68]. The lobby's
  session: Game joining_allowed opens it (players 0..3 out until they
  join); session clear (main_panel) ends it. A slot's instrument with no
  free pad shows no_instrument (no microphone: the vocals slot) [C data].
  Band Quickplay gives every player a premade character (modes.dta
  auto_choose_char TRUE, allow_create_char FALSE) [C data]; the Band World
  Tour has each pick or make one. GD05_BAND_PLAYERS=<n> (the walks that
  test what comes after the lobby) still skips it as D10's stand-in; the
  head-to-head lobby stays the stand-in [I].
- **Why:** the stand-in put the profile's first character of the
  instrument on stage; the players could not choose.
- **Measured:** rb_band_lobby (PROGRESS 2026-11-12 car8).
- **Undo:** the redirect without the GD05_BAND_PLAYERS condition;
  multiSlotScreen false.

## D81
- **What (2026-11-12, GD05-083):** OutfitProvider's outfits as RB's (RB
  0x82859E08) [C]: no (exclude TRUE) piece (the venues' drum kits), none
  only with the category's allow_none (default TRUE; the instruments'
  FALSE), in the creator no (creator FALSE) piece, with (filter <category>)
  only the pieces named after the character's piece of that category (the
  eyes of its head) [C]. The finish chosen in group_select_panel is worn
  for any piece with finishes (the mic's mic1_silver, has_group_selection
  RB 0x823E0700 counts every (outfits) entry containing the name, exclude
  or not) [C]; a model the character owns: its finishes free (D76).
  After confirm_outfit a panel of lists only (shop_instrument.milo) keeps
  no focus, so its screen's back works [I: RB 0x8283E638 not read]. A
  focused UISlider moves with left / right and sends the scroll message
  (charcreator.dta / options_audio.dta give it the focus while it is set;
  SCROLL_MSG sets the weight / height) [I: RB's UISlider not read]; its
  frame / set_frame is current / (steps - 1).
- **Why:** the drum list held 46 rows (none, the venues' kits); the mic's
  finish was not worn and the shop could not be left; the creator's
  sliders set nothing.
- **Undo:** OutfitPanelObj::listFor's filters, OutfitList::wear, the
  slider's focusedMove.

## D82
- **What (2026-11-12, GD05-084):** the body deform (char/deform.h) [C]:
  char_objects.dta DEFORM_COMMON blends the character's deform_clips
  "deform" clip (five samples: 0 neutral, 1 tall and fat, 2 short and fat,
  3 tall and skinny, 4 short and skinny) with deform_frame's weights (RB
  0x82300A00 over the constructor's fixed table, RB 0x822EA0C0 ->
  0x823013A8: points (weight, height) (0.5 0.5) (1 1) (1 0) (0 1) (0 0),
  triangles (0 2 1) (0 1 3) (0 3 4) (0 4 2), barycentric), char_utl_deform
  (RB 0x82355918) poses the bones' positions, rotations, then scales (RB
  0x823630F0). Applied to every band member as it is built: the player's
  character's sliders, a premade's (height) / (weight) [C data]; the
  deformed pose is the bones' rest from then on (Skeleton::setBind: the
  IK's lengths, measured at its first poll, and GD05_BANDCHECK's stretch
  compare against it).
  Not done [I] (GD05-092): the outfit meshes' MeshDeform bake (the exo_*
  bones' girth, RB 0x821E6D50 / 0x821E6398), the CharCuffs (0x82334FC8),
  CharIKScale capture_before / after (the animated pelvis height scaled;
  the IK hands measure their lengths from the deformed pose at their
  first poll, as CharIKHand measure_lengths).
- **Measured:** rb_body_deform: the sliders at the top give sample 1's
  bone positions exactly (PROGRESS).
- **Undo:** the applyDeform call in Band::assembleRb.

## D83
- **What (2026-11-12, GD05-083):** the characters in the menus
  (ui/rb_char_preview.h): the Rock Shop's CharPanel draws the shopped
  character at stores/store.milo's char0 (its CompositeCharacter slot) and
  the creator's practice space at practice_space.milo's creator.trans [C
  data: the slots], built as the band's members (Band::rbOutfitMilos,
  rbSkin, the deform), playing the main driver's stand group (a drummer:
  sit) with the attitude's flag [I: RB's shell clips]. Not done [I]: the
  shots' cameras (select_cam: practice_space.milo's creator.shot, the
  store's BandCamShots; CameraDirector::evaluate needs a venue): the
  panels' own cameras, so the creator's character stands behind the menu;
  the lobby slots' characters (CharLoadingPanel) are not drawn.
- **Undo:** CharPanelObj / PracticeSpacePanelObj draw.

## D84
- **What (2026-11-12, GD05-022 / GD05-084):** the characters' skins are
  rendered at run time as RB's (render/char_skin.h). The heads' and torsos'
  diffuse textures are placeholders (head_head<n>_diffuse.tex in
  head_<n>.milo, torso_naked.tex in <gender>_<instrument>.milo) a
  TexRenderer (head_skin.rndtex, torso_skin.rndtex; Load RB 0x821ED160:
  header, (rev > 2) Anim and Draw, draw, output texture, (rev > 1) force,
  impostor_height) fills from its inline skin proxy (head_skin_proxy,
  torso_skin_proxy) [C data]. RndTexRenderer::DrawShowing (RB 0x821ED620)
  [C]: when dirty, the proxy's first Cam (Cam.cam: y_fov 0, a parallel
  projection, RndCam RB 0x8218CAE0: x -1..1 across, z up scaled by the
  target's height / width) targets the texture, the proxy draws (its draw
  list: head1.grp / Group.grp in their order), then it is clean. The
  proxies draw ScreenMasks (rect x y w h from the top left, the material's
  texture across it [I: the uvs], mask colour x material colour, no depth
  [I]) and uv-space meshes (the patch meshes, the arms' overlays); their
  materials drawn unlit, white ambient [I: the proxies hold no Environ].
  PatchRenderer (Load RB 0x822D3998: a TexRenderer, the test patch and
  side symbols, back and front textures) renders its patch directory into
  its output (patch_chest / _left_arm / _right_arm / _facepaint); without
  one nothing is drawn, the output transparent [I: RB's untouched target].
  CompositeCharacter's sync (RB 0x822E94B8) [C]: every skin.mat takes
  skin.pal's colour at skin_color_index (set_skin_color RB 0x822E5110,
  rgb), each skin_proxy's tattoo_overlay_mat(2, 3) skin_tattoo_overlay.pal's
  (world_objects.dta skin_proxy set_color) [C data]; make-up:
  makeup_eyes.mask / makeup_eyes_overlay.mask draw the character's
  makeup_eyes.tex (the eyes outfit) through makeup_eyes(_overlay).mat,
  makeup_lips.mask the lips', a mask showing only when its texture and
  material exist; RB looks for makeup_lips_overlay.mask, the data's is
  makeup_lips_ovelay.mask, so that one stays as loaded [C]. The torso
  proxy's ObjectDir subdir (outfits/torso/naked/torso_skin_subdir.milo,
  the base skin texture; RB 0x823C2C10) is loaded and linked. An outfit's
  directory resolves names in the character's (its inline head proxy
  finds the eyes' make-up) [I: RB merges outfits into the character].
  Placeholders nothing renders (the naked legs) keep wave 05's stand-in
  (the skin colour) [I]. The targets render once per change
  (Character::skinSerial) and are freed 20 s after their character was
  last drawn. Device: readTarget (a target's pixels, the walks' checks).
- **Why:** every head and torso was one flat colour (the placeholders
  drawn in the skin tone): no face texture, no make-up, no tattoos.
- **Measured:** rb_makeup (PROGRESS 2026-11-12 car9).
- **Undo:** GD05_SKIN_LEGACY=1 (nothing rendered); BandView's skin_.

## D85
- **What (2026-11-12, GD05-090):** a WorldInstance's (proxy's) EventTriggers
  answer the world's events, and EventAnims run. EventTrigger
  RegisterEvents (RB 0x8220C770) [C] adds the trigger as a sink of its own
  directory (dynamic_cast<MsgSource*>(Dir())) for its trigger / enable /
  disable / wait_for events (handlers "trigger", "enable", ...); the
  shared fx milos' triggers listen for world events [C data]: 24 on
  "start" (the win fountains' and pin wheels' start_hide.trig hide their
  shared.grp; the flashpots', fountains', lightwalls' start triggers),
  game_won, bonusfx, bre_success, intro_end. The port registered only
  the chain's triggers (a proxy's answered its proxy calls only); now
  every proxy milo's too, once per milo (instances of one file share it
  here, as they share their objects) [I: the path from the WorldDir's
  events to an instance directory]. EventAnim v0 [C]: SetFrame (RB
  0x822CE3A0) fires, in key order, every (proxy, trigger) of the keys
  from the last frame set (inclusive) up to the frame and keeps the frame;
  EndFrame (0x822CE390) is the last key's frame; no StartAnim of its own.
  The record: header, Animatable, keys (f32 frame, u32 n, n x proxy and
  trigger names) [C data: every record ends at its marker]. The win
  sequence's shots (worldbase.dta win_sequence: coop_fw_<part>.shot)
  play one through their play_anim property (band_objects.dta start_shot
  {[play_anim] animate}); arena_04's win_drums.eventanm fires
  sparks_fountain_blue_WIN..07 game_win.trig at frames 18 / 20 (show
  shared.grp, play it 0..60). A proxy trigger's sound that neither its
  milo nor the venue holds plays nothing (mushroom_burst.cue sits in
  world/shared/gen/venuesfx_bank.milo, which nothing loads) [I].
- **Why:** arena_04's win fountains emitted and drew in nearly every shot
  (fix8: contrast +42 %); in RB they wait for the win.
- **Measured:** walk rb_win_sparks (arena_04): particles started by
  sparks_fountain_blue_win.milo's systems 15 s into the song 0 (before:
  5,869), after the forced win shot coop_fw_all05 1,186, drawn 704;
  EventAnim triggers 69.
- **Undo:** World::registerTriggers' proxy milos; World::eventAnimFrame;
  readEventAnim.

## D86
- **What (2026-11-12, GD05-090 / GD05-069):** a venue particle system
  runs only while drawn, as RB's RndParticleSys [C]: Poll (RB 0x821D94F0)
  moves and emits only when its counter (+0x104) is 0 (drawn since the
  last Poll) and the system is showing and has something to do, else
  notes the time; the counter then counts up. DrawShowing (0x821D9408)
  zeroes it and, after two or more Polls without a draw, catches up (move
  and emit over the time since). A hidden system, or one inside a hidden
  group or not in the draw list, stays still; it emits nothing while the
  win fountains' shared.grp is hidden. The port polled every venue system
  every frame. ParticleSystem::pollDrawn / drawShowing, ParticleSet::update
  (drawGated) for VenueView only. [I]: RB's draw also culls by the
  bounding sphere (0x8218AC08); the port draws every listed system.
- **Undo:** GD05_PARTICLES_LEGACY=1.

## D87
- **What (2026-11-12, GD05-069):** WorldDir force_preset (Handle RB
  0x822A46A8 -> 0x822A2680 -> 0x822A2608) [C]: a LightPreset is started
  now (StartPreset 0x822A1B50, delay 0) and held as the override (+0x54);
  StartPreset starts nothing while one is held, so the picks
  (set_lighting) leave it; none ('' from the BandCamShots' end_shot)
  clears it and picks again now (0x822A1E10 with delayed 0: the first
  candidate unless it is the current one). LightPresets::native.
- **Why:** BandCamShots with lighting_override and a lighting_preset (win
  shots, special shots) sent it and it was UNHANDLED: rb_look failed on it
  in every venue (fix8's gate too).
- **Undo:** the force_preset branch.

## D88
- **What (2026-11-12, GD05-089):** rb_look's pause check measures the
  overlay on a still frame: paused, with the flicker and grain phases held
  (GD05_POST_FREEZE=1, a test aid in PostFx), the pause screen's panels
  (pause_panel, helpbar) hidden, then shown: the paused world must not move
  (mean difference <= 2 of 255, measured 0.00) and the outer strips with
  the screen shown must be 0.3 of the colour through the display curve
  (D57 / D25) within 10 %; for that frame every pause.milo mesh but
  black_bg.mesh (the art: octopus, arms, trim and the rest, placed
  differently each pause and reaching into the strips; with only the
  octopus / arms / trim out 2 of 30 pauses still read 0.20 / 0.22) and the
  helpbar are left out, so black_bg.mesh alone is over the world; three pauses 4 s apart, one on a
  dark world (blackout) is skipped, at least one measured.
- **Why:** it compared a frame before the pause with one during it; the
  world changed between them (0.46..1.85 of the rule by the shot).
- **Measured:** 11 of 11 measured pauses 0.99..1.01 of the rule (four
  venues, three pauses each, one on a blackout skipped); the paused world
  0.00..0.52 of 255 between two frames.
- **Undo:** the walk's steps.

## D89
- **What (2026-11-12, GD05-069, a correction of D79's "never drawn"):**
  the SpotlightDrawer's proxy (the stage fog / smoke group) is drawn, but
  not into the picture [C]: NgSpotlightDrawer's beam pass (RB 0x822ABDC0)
  first calls 0x822A9E58, which, when the proxy (+0x5C) is set, draws it
  (RndDrawable Draw 0x8218AC08, with a device flag +0x25 set) through the
  drawer's own RndCam (+0x64: near +0x2B4, far +0x2B8) into the drawer's
  texture (+0x6C, 8218D170 sets the camera's target), with c91 = (0.1, 1,
  0, 1 / far) and VS c55 = (near, -1 / near, 1, 0), and then binds that
  texture as sampler 6, gFogDensityMap. Then the volume passes
  (0x822ABC28) draw each queued spotlight with +0x1BC > 0 into the beam
  target (DAT_828EBCD0), set c82 = gFogDensity (0x822A8E78) and the post
  shaders add gFogDepth (s12) x (c82.x + c82.y x gFogDensityMap) x c91.x
  to the scene (PROGRESS fix9). So the stage fog shows only where a
  beam's light falls on it, as "smoke" in the beams; drawn straight into
  the world (the port before D79) it was the far-too-bright wash. The
  port draws no density map and no RB beam composite (GD05-093), so the
  proxy stays out of the draw list (D79's code is right; its reason is
  this one).
- **Undo:** none (documentation; the code is D79's).

## D90
- **What (2026-11-12, GD05-083, car9):** the menus' 3D panels draw through
  RB's select_cam shots [C data]: practice_space_panel (practice_space.milo,
  a WorldDir: creator / creator_heads / coop_enter / solo_enter ... .shot)
  and shop_char_panel (store.milo: idle, makeup, tattoo_* ... .shot) take
  the named BandCamShot, framed by CameraDirector::evaluate over the
  panel's own objects (ShotScene: targets, parents, path; a target naming
  the character's slot is the previewed character's bone [I]), its first
  frame [I: the shot's keyframe blend over time is not played]; UiView's
  camera override. creator.shot aims at creator.trans with screen offset
  (0.24, 0.37); the character stands at creator.pxy, the UIProxy RB loads
  it into [C data: the milo's objects] (D83 put it at creator.trans, 67
  units up, behind the menu with the UI camera). The practice space also
  draws the lobby's players' characters (a CharSelectPanel,
  matchmaking.dta): each slot with a player whose show_char is true shows
  its player config's character at coop<slot>.pxy (hth / solo for two / one
  slot) [I: RB's CharSelectPanel not read; the char list's highlighted
  character is not previewed before it is chosen]. CameraDirector's
  framing (frameShot / keyframeXfm) now takes a ShotScene (the venue's
  WorldShotScene for the game, unchanged).
- **Also (D84):** CharSkin's pose keeps only the current render's patch
  overrides (a material freed with an earlier preview lent a patch target
  to the next character's skin.mat: the creator's torso went black after a
  slider moved).
- **Measured:** rb_front_creator (rb_body_deform), rb_slots_drawn
  (rb_band_lobby) (PROGRESS 2026-11-12 car9).
- **Undo:** GD05_SHOTCAM_LEGACY=1 (the panels' own cameras, creator.trans,
  no slot characters).

## D91
- **What (2026-11-12, GD05-093 / GD05-069):** Rock Band's spotlight beams
  (NgSpotlightDrawer) replace GH2's beam sheets. [C]: the Ng drawer's
  classic beam pass (vtable +0x44) is an empty function (0x824A3138), so
  no beam sheet is drawn with its beam material; the beams reach the
  picture only as follows. (1) Density map (0x822A9E58): the drawer's
  camera (+0x64, a copy of the shot's, 0x822A9CC8) draws the proxy (the
  stage fog / smoke group) into the beam system's density texture (+0xC,
  a quarter of the picture each way, type 0x122, 0x822A9A58). (2) Volume
  passes (0x822ABDC0 -> 0x822ABC28): the beam buffer (+8, half the
  picture each way, 32 bits, type 0xA2) cleared; each queued spotlight
  with a beam length > 0 drawn by its beam shape (+0x1E8; every beam on
  the disc is shape 0 or 1: 0x822AB858 -> 0x822AABE0) with xbox_shaders
  #2290 / #2291 (a ray / cone intersection against the scene's depth),
  kept as gd04's Xenos translator gives them, the fetches only the
  port's. Constants: gDepthHalf c9 0.125 (0x82887374); gCamPos c10;
  gSpotPos c25 (pos, 1 / length); gSpotDir c26 (the light's y row,
  length); gSpotToCam c27; gConst_u c28 (k^2 + 1, a k^2 + m, |c - p|^2 -
  m^2 - a^2 k^2, 1; k the radius slope, a the apex distance + m, m = dir .
  (cam - pos)); gRadii c29 (top, bottom, dir . pos, + length; radii x
  expand, a sheet's x (1 - side border x 0.7), 0x82297B88); gCamDir c30;
  gFogDensity c82 (0, 1 / far, 0, 0) (0x822A8EF8); gXSectionStrength c86
  (0 with the camera inside the cone, 0x822A90B0); gPlaneRight / Left
  c87 / c88 (the sheet's side planes through the camera, 0x822AA458);
  gZRange c89 (near, far, 1, 0); gFogColor c90 = colour x intensity x 8
  (0x822AB858, 0x82887360) x brighten (0x822AABE0); gFogRange c91 (0,
  the drawer's unnamed f32 (250), 0, 1 / far). (3) Composite (the post's
  gFogDepth variants, #2381 / #2397): scene + beams x (gFogDensity.x +
  gFogDensity.y x density.x) x gFogRange.x, after the bloom, before the
  grain and the ColorXfm, with gFogDensity = (base x 0.01, smoke x 0.01 x
  (1 - base x 0.01)) (0x822A8E78, 0x82010224) and gFogRange.x = total x
  32 (0x822AB5B8, 0x8288735C): c91.x's last writer before the post is
  0x822AB5B8, not a spotlight pass (fix9's open question). The presets'
  drawer entries (0x822BF6B8): smoke eases towards the file's second f32;
  base towards the first, then overwritten by total eased towards the
  third (two stfs to +0x3C): base ends as total -> third (1 on the disc).
  The drawer's record (Load 0x82299660): total (+0x28), base (+0x3C),
  smoke (+0x40), range (+0x44), colour (+0x2C), a texture, the proxy.
  [I]: the density pass draws the proxy's own materials onto black (the
  device's +0x25 flag is set, what it changes is not read); the volume
  passes add (ONE / ONE) into an RGBA8 buffer (the pass's blend state and
  the 0xA2 colour format not read); each beam's mesh is the camera-facing
  sheet between the c87 / c88 planes (RB draws the beam's own mesh);
  cut-outs (0x822AB068) and the target colour (+0x21C) are not drawn;
  the preset entries are lerped between keyframes as the spotlights'.
- **Why:** the port drew GH2's sheets at gain 1 and no smoke (GD05-093).
- **Measured:** rb_look's new check (GD05_BEAM_PROBE: the pixels the
  composite adds at least 1/255 to): beam pixels in 25..29 of 30 frames
  in every venue, 0 with base / smoke at 0 (GD05_DRAWER_ZERO, 8..65 beams
  still drawn); GD05_BEAMS_LEGACY=1 fails it ("no spotlight beam pixels").
  Look numbers: PROGRESS fix10.
- **Undo:** GD05_BEAMS_LEGACY=1 (GH2's sheets; also without post-processing).

## D92
- **What (2026-11-12, GD05-095):** the film grain's noise map is sampled
  trilinearly, as RB's [C]: 0x827D9190 binds noise_map to s13 with
  Function_8246AC70(13, 1), which sets the fetch constant's mag / min
  filter linear and its mip filter (bits 23-24 of the dword at +0x48C)
  linear, and 8246ACF8(13, 0) (clamp bits 10-15: wrap). ProFilm_a /
  failed / savior.pp use noise_mono.tex (512 x 512, six levels) at
  noise_base_scale 8 x 4.5 (top 1.359, intensity 0.3 / 0.5): 3.2 texels a
  pixel across RB's 1280 x 720, read from the smaller levels; the port
  read level 0 (sharp white noise).
- **Measured (rb_look grain, movies 1.97):** small_club_02 3.03 -> 2.86,
  big_club_03 3.38 -> 3.07, arena_04 2.35 -> 2.10, theater_02 2.65 ->
  2.43. Without the grain at all (a temporary switch, removed) 2.14 /
  2.64 / 1.49 / 2.05: the rest is the scene's own detail, not the post.
- **Undo:** the noise slot's SamplerState in PostFx::end.

## D93
- **What (2026-11-12, GD05-084, car9):** the patch creator (tattoos and
  face paint). The shop's pickers (PatchSelectPanel tattoo_select_panel /
  facepaint_select_panel, shop.dta) and the creator (PatchPanel patch_panel,
  patchcreator/patchcreator.dta, patch.milo) run their own scripts [C
  data]; the port's natives [I: RB's PatchPanel / PatchSelectPanel and the
  TourCharLocal patch records not read]: patch.lst lists none then the
  set's patches (ui.dta (patch (sets (general 10) (chest 5) (arm 5))), read
  as the slots of each set [I]), patch_dir the selected one (has_layers,
  clear); the creator's category_provider (the (stickers ...) whose (sets
  ...) hold the patch's set), sticker_provider (the category milo's Tex
  objects of type sticker, in order, their display_name), layer_provider
  (10 layers a patch [I]), edit_layer (sticker_category, sticker_idx,
  color_idx, set_scale, set_default_color = the sticker's palette_index
  when allow_color, flip_x / flip_y, has_sticker, allow_color),
  cache_patch / restore_patch, store_undo / restore_undo, clear_layer,
  dupe_layer, swap_layers; {$this set mode m} runs on_set_mode [I: RB's
  property sync]; in move / rotate / scale a d-pad press steps the layer
  (0.05 of the patch square, 5 degrees, x 1.1) [I: RB moves it while held,
  with velocities]; warp does nothing; the creator's own canvas
  (patch.rndtex) is not drawn [I]. A patch is drawn into its
  PatchRenderer's output (patch_chest / _left_arm / _right_arm /
  _facepaint, the "<location>.patch" RB 0x822E5010 names): each layer's
  sticker a quad of half extents sizex / sizey x the layer's scale about
  its centre in the target's -1..1 square, turned, flipped, alpha-blended
  in sticker.pal's colour (patch_warpmesh.milo) [C data: the sizes and the
  palette; I: the quad stands in for patch_warpmesh's grid]. The profile
  keeps each character's patches and tattoo locations (rb_profile.json
  "patches", "tattoos"); the shop preview and the band on stage wear them
  (Look / RbLook patches, Band::rbPatches). Also: the script builtin
  {basename} (RB 0x8248E098 -> FileGetBase 0x823A73B0) [C], missing (the
  tattoo location was empty); a PatchSelectPanel's (set ...) property no
  longer swallows the set message; patchcreator/ panels' milos found;
  {shop_char_panel save} saves the profile.
- **Measured:** rb_tattoo (+ _restarted, a gig), rb_facepaint (PROGRESS
  2026-11-12 car9).
- **Undo:** PatchPanelObj / PatchSelectPanelObj, CharSkin::drawPatch,
  Band::rbPatches.

## D94
- **What (2026-11-13, GD05-097, bl05):** the RB face set's clips are
  relative. Base holds the face's absolute rest; every other clip of the
  set (the visemes, the exp_* expressions) holds offsets from it [C data:
  female head_1's Base bone_jaw.pos 0.31 0.91 and .quat a half turn
  (-0.456 -0.890 0 0, the bind); Oat_hi's .pos 0 0 0.009, .quat
  (0 0 -0.166 0.986); every viseme's quaternion within 21 degrees of the
  identity]. The lip servo's pose is Base, then per lit viseme its weight
  of the offset: positions + w x a, rotations the identity nlerped to a by
  w, composed on the bone's side (q x d) [I: RB's relative clip blend not
  read; the other side shut the jaw into the upper lip]. A clip's pose
  starts from zero, not the identity (Bones::add starts a quaternion at
  w 1, which turned Base's half-turn jaw and chin 90 degrees). The port had
  taken the visemes as absolute poses (a - Base): the jaw turned 90 degrees
  at rest and 180 while singing, the lower lip 7..11 units out: the
  vocalist's chin and jaw went into the neck and showed the mouth's inside
  (the owner's "face not rendered, inside visible").
- **Measured:** bandcheck (inbloom, 60 s; GD05_PREFAB_VOCALS picks the
  singer): the new "face rest" line, every member's jaw from its bind
  against the face clips' largest turn from Base (x 1.5, two visemes lit
  at once): vocals0 180.0 -> 40.8 deg (clips 41.6), vocals2 179.9 -> 23.2
  (clips 21.8); low lip 7.17 / 11.46 -> 0.62 / 0.50 units.
  GD05_FACEPOSE_LEGACY=1 fails it.
- **Undo:** Character::facePoseOf / pollFace (GD05_FACEPOSE_LEGACY=1).

## D95
- **What (2026-11-13, GD05-097, bl05):** CHAR_COMMON sync_objects'
  {char_mesh_hide $this} (RB 0x8235BB58 -> 0x8235B760 -> 0x8235AF50) [C]:
  the flags of every CharMeshHide of the character (its outfits' too) are
  ORed; each hide's entry (drawable, flags) shows only when none of its
  flags is among them. 16 CharMeshHides on the disc (female coats and
  skirts, legs); the port decoded them and never applied them.
  GD05_BANDLOG logs each. Not the face's cause (D94), done while looking.
- **Undo:** Character::applyMeshHides (GD05_MESHHIDE_LEGACY=1).

## D96
- **What (2026-11-13, GD05-098, bl05):** the song's first shot as
  worldbase.dta's game start [C data]: the mode's intro_category
  (modes.dta: qp_solo / tour_solo / practice INTRO_VENUE, the band's
  coop_intro_category, defaults INTRO); INTRO_VENUE / INTRO_QUICK pick a
  shot of that category (find_shot); INTRO runs construct_active_shot: an
  INTRO shot with the active player's focus flag (guitar_focus,
  bass_focus, drums_focus, vocal_focus) [I: the chain's next_shot not
  followed; one player]. The port drew any INTRO shot: a close-up of a
  random member (a guitar player got coop_intro_bass.shot).
- **Measured:** GD05_SHOTLOG on inbloom (guitar, its venue): before
  coop_intro_bass.shot, after "intro category INTRO" ->
  coop_intro_guitarist.shot.
  The quick play solo intro (INTRO_VENUE) runs 6.7 s where the port's
  ran 0.7: rb_od_* and rb_look now wait for the song clock (walk check
  song_clock_running) then as long as before.
- **Undo:** App::rbIntroShot (GD05_INTRO_LEGACY=1).
