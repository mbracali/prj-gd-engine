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
