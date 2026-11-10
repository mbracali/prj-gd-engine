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
