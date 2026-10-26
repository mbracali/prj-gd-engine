# Changelog: gd-game-02 (gd02)

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-02/vX.Y.Z`. The release workflow publishes the section whose heading
matches the tag's version. Nothing is released yet.

## [Unreleased]

### Added
- A first screen when no disc image is set: click, press Enter (or tap on
  Android) to choose your Guitar Hero II (Xbox 360) `.iso` with the system's
  file dialog, or drop it onto the window. The game remembers it, and finds an
  `.iso` lying next to the program by itself (the Steam Deck way).
- The Guitar Dream Engine badge as the icon of the window, `gd02.exe`,
  `gd02.app`, the Android app and (Linux downloads) the application menu entry.
- Downloads for Windows, Linux, Steam Deck, macOS and Android ("GD Engine 02").
- Guitar Hero II's own menus, from its scripts: the intro, title screen,
  main menu, Quick play song list, difficulty, loading, results (the Daily
  Dose), high scores with name entry, pause, options and credits.
- Guitar Hero II's highway and HUD, with sparks, the intro sequence,
  "You Rock!" and the fail wind-down; Lefty Flip.
- The intro and bonus videos (a minimal FFmpeg ships with the game, under the
  LGPL; its licence and source link are in `licenses/`).
- Progress and options saved to `career.json`, and a GAMEPADS screen to set
  up guitars and gamepads.
