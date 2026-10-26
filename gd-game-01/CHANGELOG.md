# Changelog: gd-game-01 (gd01)

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-01/vX.Y.Z`. The release workflow publishes the section whose heading
matches the tag's version.

## [Unreleased]

## [1.1.3] - 2026-10-25

**Lefty Flip works, and PS2 guitars on adapters play as guitars.**

### Fixed
- Lefty Flip (Options > Game Settings, per player) did nothing. As in the
  original: the highway is mirrored (green on the right, the gems, tails,
  fret buttons and hit effects too), a gamepad's frets take the original's
  lefty order (Cross, R2, R1, L1, L2), and a guitar's strum moves the menus
  the other way. Touch lanes follow the mirrored highway. (#15)
- Game Settings (Lefty Flip, the band / guitar / sound effects volumes,
  stereo) were not saved: they were lost when the game restarted. Leaving the
  screen now saves them, as in the original.
- PS2 guitars on USB adapters that say they are a PlayStation 3 pad played
  like a gamepad (each fret strummed, the strum bar did nothing). GAMEPADS has
  a **Type** for each controller: Auto, Gamepad, PS2 guitar (adapter), Xbox
  guitar or Rock Band guitar. Choose "PS2 guitar (adapter)" and the guitar
  plays with its strum bar, with the original's PS2 guitar buttons. It is
  remembered for that controller.

### Added
- The version (e.g. "v1.1.3") in the top-right corner of the main title
  screen.
- A controller report for bug reports: GAMEPADS writes
  `controllers-report.txt` in the save folder (its path is shown on the
  screen) with every controller's name, USB id, SDL mapping, type, player and
  buttons, and the last raw button / axis presses.

### Removed
- "Replace Textures" in Enhanced Settings (never built; dropped from the
  roadmap).

## [1.1.2] - 2026-10-25

**macOS: the black screen is fixed.** On Macs, songs, venues and the
Enhanced Settings menu showed a black screen while the menus and the highway
still drew. They now show the full venue, band and lighting. Windows, Linux,
Steam Deck and Android players see no change.

### Fixed
- macOS: the venue, the band and the 3D scenes (in a song and behind
  Enhanced Settings) were black. One post-processing shader ("super fog") used
  a name Apple's OpenGL compiler reserves, so the whole effects chain failed
  to start and the scene was never shown. The shader is fixed, and if an
  effects shader ever fails to compile on a graphics driver again, the game
  now draws the scene without effects instead of a black screen.

### Added
- `--shadertest`: compiles every shader on this computer's graphics driver
  and reports any that fail (no game data needed). The macOS local build runs
  it before packaging.

## [1.1.1] - 2026-10-23

### Added
- Touch: on-screen Up / Down / START buttons at the right edge of the menus
  and a pause button in a song. By default they appear once you touch the
  screen, and hide again when a keyboard or controller is used.
- Touch: two-finger swipes move up and down in the menus; in a song, swipe
  up deploys Star Power and swipe down pauses (both can be changed).
- The touch screen is listed in GAMEPADS ("Touch Screen", always P1), with
  its settings: on-screen buttons (when touched / always / off), each swipe
  (Star Power / pause / nothing) and the pause corner (top right / top left /
  none).
- Controllers SDL has no mapping for get a generic one, so every connected
  controller shows in GAMEPADS and can be set up there.
- Linux and Steam Deck downloads add the game, with its icon, to the
  desktop's application menu on the first start, so the window and taskbar
  show the icon (set `GD01_NO_DESKTOP_ENTRY=1` to skip). This replaces
  `add-to-app-menu.sh`.

### Fixed
- One press counted two or three times in the menus (most visible on the
  Steam Deck): a controller seen twice (the device and Steam's virtual copy),
  or Steam's desktop layout typing Enter for A, now acts once.
- The Steam Deck's controls missing when the game is started from Steam:
  Steam's virtual gamepad is now used when Steam hides the real one.
- In the menus a tap clicks when the finger lifts and only if it did not
  slide, so swipes no longer click by accident.
- GAMEPADS fits more controllers (the rows shrink instead of running into the
  live input box).
- Icons: the small sizes (taskbar, title bar, file lists, launchers) show the
  badge's centre so the letters stay readable; the Windows icon uses classic
  bitmaps; the Android adaptive icon fills its shape without a black ring.

### Changed
- Android: touch controls are always on (a phone may have no other control).
- Texts: "GD01" in the loading, save and splash texts is now "GD-Engine", and
  "BRACA DEVELOPER" is now "GD-Engine Devs".
- The splash line says "free and source-available" (the licence is not an
  open-source one).
- The credits: the GH1RE project's developers (mbracali, Anthropic Claude),
  the open-source thanks, the beta testers (now with Viajor, ogabrielluiz and
  Arthur Brum) and the original heroes, before the original game's credits.

## [1.1.0] - 2026-10-23

### Added
- The Guitar Dream Engine icon: on the Windows `.exe`, the macOS app, the
  Android launcher (adaptive on Android 8+), the game window and the first
  screen. The Linux and Steam Deck downloads include `gd01.png` and
  `add-to-app-menu.sh` (adds the game, with its icon, to the app menu).
- Steam Deck download (`gd01-<version>-steamdeck.tar.gz`): built in Valve's
  Steam Runtime, so it runs from Game Mode as a non-Steam game; starts with
  the handheld settings; finds an `.iso` copied into its folder by itself.
- Every download starts with settings tuned for its system (see the README,
  "Default settings").
- Bloom has a Medium level.
- A `.iso` next to the program is used automatically on the first start.

### Changed
- Steam Deck Mode is now a button, "Apply Steam Deck Settings": it sets every
  option to the handheld values once, and nothing is locked afterwards.
- Every controller drives the menus: a second gamepad, a guitar or the
  keyboard can select, not just player 1's.
- On screens with nothing to choose ("press any button", the boot logos), a
  click or a tap continues; on a phone the touch screen is enough.
- An option with only two values is a check box ("Forgiving Hammer-ons &
  Pull-offs").
- The Android app's version now follows the game's (1.1.0), so new
  versions install as updates over the old one.

### Kept
- The Glitches, the ninth band with everything unlocked, is in every
  download (Choose Band / Load Band).

## [1.0.0] - 2026-10-23

First release of Guitar Hero (PS2) on Guitar Dream Engine.

### Added
- Easy start: download, extract, run, then click the window (or press
  Enter) and pick your Guitar Hero (PS2) `.iso`. Dropping the file on the
  window still works, and the game remembers it.
- The Linux download runs on any distribution from 2022 on, with nothing
  to install (SDL3 is included).
- The Android app is signed with the project's release key, so later
  versions install as updates.

### Changed
- The program is now called `gd01` (Guitar Dream Engine, game 01); it was
  `gh1re`. Saves and settings move to the `gd-engine/gd01` folder and are
  copied from the old `gh1re` folder on the first start. On Android the app
  is now `io.github.mbracali.gd01` ("GD Engine 01") and installs beside the old one.
- Developer variables are `GD01_*`; the old `GH1RE_*` names still work for now.
- NVIDIA DLSS is no longer bundled with the source: `tools/fetch_dlss.sh`
  downloads it from NVIDIA. Builds without it fall back to FSR 3.1 / FSR 1.

### Also added
- The credits start with the Guitar Dream Engine attribution.
- Every download carries the licence (`LICENSE`, `LICENSE-ADDITIONAL-TERMS.md`)
  and the third-party licences.

### Already in the engine at import
- Career (6 tiers, store, unlocks), Quickplay, two-player, tutorials, 47 songs
  on the original highway, venues, band, crowd, camera and lighting from the
  user's disc.
- Original gameplay rules: hit window, hammer-ons and pull-offs (with an
  optional Forgiving mode), scoring, Star Power, rock meter.
- Enhanced settings: high resolutions and frame rates, AI upscaling (FSR 1,
  FSR 3.1, DLSS), post-processing (bloom, fog, colour grading, film grain,
  Star Power blur and more), controller mapping, guitar controllers, touch.
- Linux, Windows, macOS (untested) and Android builds.
