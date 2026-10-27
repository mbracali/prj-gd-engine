# Changelog: gd-game-02 (gd02)

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-02/vX.Y.Z`. The release workflow publishes the section whose heading
matches the tag's version. Nothing is released yet.

## [Unreleased]

### Added
- gd02 plays inside the Guitar Dream Universal Launcher: CMake option
  `GD02_MODULE=ON` also builds it as the launcher module `gd-game-02.so` /
  `gd-game-02.dll` (entry point `src/platform/module_entry.cpp`); the `gd02`
  program is unchanged.
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
- All eight venues behind the highway, with Guitar Hero II's camera cuts,
  light shows, spotlights, bloom and depth of field, the crowd and its
  cheering, and the encore effects.
- Progress and options saved to `career.json`, and a GAMEPADS screen to set
  up guitars and gamepads (in Options > Enhanced Options).
- The band on stage: Guitar Hero II's characters play along with the song,
  with their hands on the notes, faces, blinks and lip sync, solo walks and
  band interactions.
- Guitar Hero II's crowd, drawn the way the original does it, cheering,
  clapping and raising lighters with how well you play.
- Sound effects from the disc (menus, crowd, Star Power), with the console's
  reverb.
- Character, outfit, guitar and skin choice in Career, saved with your band,
  and the band photo in the results newspaper.
- Enhanced Options (in Options): the PC version's own settings over a live
  shot of the band on stage. Screen mode, maximum frame rate, an FPS counter
  and the internal resolution (from the PS2's 448p and the Xbox 360's 720p up
  to 4K) work now and are remembered; the others are listed for later.
- Guitar Hero II's cheats: enter the original button codes at the main menu,
  or switch them in Enhanced Options. Unlock All, Air Guitar, Performance
  Mode, Hyper Speed and the Eyeball / Monkey Head crowds work; Flaming Head
  does not burn yet.
- A band called The Glitches in every save.

### Changed
- Options is laid out like the original (Audio, Video, Manage Band, Data,
  Bonus Videos, Credits) with Enhanced Options added; GAMEPADS moved there
  from Options and the main menu.
- Green selects and red goes back with every guitar, gamepad and the
  keyboard; Back (Esc) pauses a song like Start; Esc three times on the main
  menu quits.
- The logos and videos at start-up can be skipped with a button.
- Notes are judged by Guitar Hero II's own rules (hammer-ons and pull-offs,
  chords, holding sustains), and the band's faces and lip sync keep time
  with the song.

### Fixed
- Going back to the main menu no longer leaves its camera off to one side.
- Audio Settings shows its button help at the bottom and its whole poster.
