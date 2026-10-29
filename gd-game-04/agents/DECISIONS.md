# gd-game-04: decisions taken without the owner

Each: what was chosen, why, how to undo it.

**D1. Working branch and checkout.** Work in the main checkout on
`feature/20261028-gd-games-03-04` (owner's override of the kickoff's worktree
plan; two other agents share the checkout). Only `gd-game-04/`, the
gd-game-04 backlog section, `build_module_04` in the launcher's `build.sh`
and `games.yaml` are touched. Undo: none needed.

**D2. C++20, not C++17.** The kickoff says C++17, but gd-game-02 (the model,
whose code we copy) is C++20 (`set(CMAKE_CXX_STANDARD 20)`, designated
initialisers, `= default` comparisons, `std::u8string`). The guidelines say
"match gd-game-02's style". Undo: set 17 in `engine/CMakeLists.txt` and
replace the C++20 constructs.

**D3. Copied from gd-game-02 (engine-independent layers only).** Platform
(paths, icon, disc picker, module entry), XDVDFS + RandomFile (+ a `list()`
walk), raw inflate (moved to `data/`), JSON, dev aids (scripted keys,
screenshots), input (controllers, bindings, touch), audio device / mixer /
XMA decoder / DSP helpers, the GL device, texture codec (360 untiling, DXT),
2D canvas, shader test, build scripts, Android project, launcher module
entry. Removed from the copies: GH2's material shaders, post FX and
spotlights (the renderer), the `.mogg` / Vorbis path, the WMV player, the
Milo / DTA layers. Undo: delete the files.

**D4. Input layouts: GH3's (2026-10-29).** The guitar layout is the Xbox
guitar's; the gamepad plays as in the original (frets LT LB RB RT A, strum
with the D-pad / left stick, A selects, B goes back), confirmed by running
the original in Xenia (GD04-027). The input layer itself (rebinding,
controllers.json) is gd-game-02's. Undo: n/a.

**D5. FFmpeg build: Bink instead of ASF/WMV.** GH3's movies are Bink 1
(`BIKi`), so the minimal LGPL build enables the `bink` demuxer and the
`bink`, `binkaudio_rdft`, `binkaudio_dct` decoders, plus `xma1` / `xma2` as
before. Undo: `FFMPEG_CONFIGURE` in `engine/build_common.sh`.

**D6. REA vs Ghidra.** REA cannot open the XEX, the Ghidra project or raw
PowerPC (agents/CONTEXT.md); every XEX finding comes from Ghidra headless
with XEXLoaderWV. Undo: if a later REA release supports PowerPC / XEX,
re-check findings with it.

**D7. No -66 ms gem / input offset on PC.** GH3 (360) shifts gems and input
by xenon_gem_offset / xenon_input_offset (-66 ms) to cover the console's
video and controller latency. The PC port draws and judges at the chart's
times, and leaves latency to a calibration option (as the original's lag
calibration). Undo: pass the offsets to Player and Highway2D::depthOf.

**D8. Enhanced Options' entry: a help-bar item on GH3's Options.** GH3's
Options menu (options_select_option_fs, create_options_menu) is a QScript
menu; adding a scripted item would mean patching its bytecode. Instead the
port adds an "ENHANCED" item (a blue fret square) after GH3's own help-bar
items on that screen: the blue fret (a guitar's, the keyboard's 4, a joypad's RT) or a
click on it opens Enhanced Options, a native page drawn by the app while
GH3's scripts wait (ui/port_screens.*). The main menu's "GAMEPADS" item
(yellow) works the same way; it sits after GH3's UP/DOWN pill rather than
before it, so GH3's own centred pills stay as the scripts lay them out.
Undo: remove PortScreens::drawItems / itemAt and the Fret2 / Fret3 checks in
App::menuAction.

## D9 The Glitches: the Vault scoped to their band (2026-10-29, revised)

Kickoff rule 11: The Glitches band has everything unlocked. GH3 keeps a
band's career (tiers, songs, stars, cash) in its own global-tag sections
(push_bandtags / pop_bandtags), but the Vault's purchases (guitars, finishes,
characters, outfits, styles, videos, bonus songs) are save-wide tags. The
owner's answer (2026-10-29, GD04-026): purchases count only for The
Glitches' band. So the port names band slot 5 "The Glitches" (solo and
co-op, unless the player already uses that slot), completes its career at
every difficulty in its sections as GH3's own debug unlock does
(playday_unlockall -> GlobalTags_UnlockAll), gives it $1,000,000, and while
that band's career is popped the Vault's tags read "all bought"; popping
another band or pushing the Glitches' section back restores the save's own
state, and saves always store the save's own state (ui/glitches.cpp). The
quick play songs a finished career unlocks stay save-wide, as in GH3.
Tests: --walktest glitches (bought for The Glitches), --walktest vault
(band 1: for sale).
