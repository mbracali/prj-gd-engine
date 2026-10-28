# Changelog: gd-game-02 (gd02)

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-02/vX.Y.Z`. The release workflow publishes the section whose heading
matches the tag's version. Nothing is released yet.

## [0.1.5] - 2026-10-28

### Fixed
- Venue cameras frame their shots as Guitar Hero II does: they look at the
  band with the shot's own tilt and slide sideways to put it where the shot
  wants it, and camera paths start from the shot's first keyframe. Stone
  Henge's intro is no longer upside down (the crowd "hanging from the sky"),
  the arena's intro no longer flies under the floor, and every other shot
  frames the band where the original does.
- Venue animations that move along curves (paper planes, flash pots, swinging
  lights and more) follow their smooth paths instead of straight lines.
- Controllers no longer press left / right (or any direction) by themselves:
  a D-pad that SDL reads from an axis (many adapters and guitars) is now
  judged from that axis's resting value with hysteresis, a button held as the
  controller connects counts only once released, and a controller that goes
  away releases what it held. Before, the menus' key repeat kept moving.
- GAMEPADS: a menu control waiting for its new binding takes only buttons and
  keys, so a guitar's tilt or a drifting stick can no longer be bound to it.
- The venue camera no longer speeds up on fast screens: its follow filter and
  shake run in Guitar Hero II's 1/60 s steps at any frame rate (they ran once
  a frame, so 2.4 times too fast at 144 Hz), and the frame rate no longer
  changes which shots the director picks.
- Band members no longer vanish when filmed from behind or from afar: the
  lower-detail models (the behind-the-band shots ask for them) were never
  drawn.
- Guitars and basses: the guitar's shadow model was drawn over the neck,
  hiding parts of it; outfits' detail and shadow groups now merge as in
  Guitar Hero II.
- Menus: going back plays Guitar Hero II's backward transition instead of
  the forward entrance again, and screens no longer get darker after going
  back and forth (the bricks' light comes back on).
- A crash when a song ended with some outfits.
- Windows: the game writes `gd02.log` in its save folder, with the address of
  a crash, for bug reports.

### Added
- The mouse in every menu, as in Guitar Hero (gd-game-01): pointing at an
  entry highlights it, a click chooses it (a list row too), a right click goes
  back, the wheel scrolls; the help bar's items are clickable. The pointer
  hides after a few seconds, on a key or button press and during a song.
  Enhanced Options > Enable Mouse Selection turns it off. Taps on a touch
  screen are clicks.
- The main menu's yellow fret (or a gamepad's yellow Y button) opens
  GAMEPADS again, with its item in the help bar; Guitar Hero II's cheat codes
  still work (it opens once no other button follows within the codes' 750 ms).
- `GD02_KEYS` mouse items (`hover<x>_<y>`, `click<x>_<y>`, `rclick<x>_<y>`,
  `wheelup` / `wheeldown`) and axis items (`jleftx=-20000`, `ja6=0`);
  `GD02_VIRTUAL_PAD=<vid>:<pid>:dpadaxis=<rest>`; `GD02_INPUTLOG=1`; walk
  tests `mouse`, `gamepads` and `stickyaxis`.
- `GD02_PERF=1`: a frame-time breakdown in the log every 2 s (update, draw,
  present, GPU wait, draws and uploads per frame). `GD02_LOG=<file>` copies
  the log to a file.

### Changed
- GAMEPADS has Guitar Hero (gd-game-01)'s layout, flow and mouse in Guitar
  Hero II's menu font and colours (the rows no longer overlap; the mouse's
  "x" clears a binding).
- Enhanced Options looks and works like Guitar Hero's Enhanced Settings:
  collapsible sections, check boxes, dropdowns (Screen Mode, In-Game Max FPS,
  Internal Resolution), the explanation box and the mouse, over a still shot
  of the venue with the band (it no longer plays live behind the menu).
- macOS: uniforms and streamed vertices are written through unsynchronized
  buffer mappings, the path Apple's OpenGL handles without stalls
  (`GD02_GL_STREAM=map|subdata` chooses on any system).

## [0.1.4] - 2026-10-28

### Fixed
- Windows: Guitar Hero II starts again ("LoadLibrary failed (error 126)"):
  the download now carries `libwinpthread-1.dll`, which FFmpeg needs.

## [0.1.3] - 2026-10-27

### Fixed
- The band's name with the keyboard: Enter (and the keypad's Enter) now
  finishes the name, like Start, on every text entry. Before, Enter added a
  letter and a keyboard player could not create a band. Green / X still add
  letters on guitars and gamepads.

## [0.1.2] - 2026-10-27

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
- Career mode, played end to end as in Guitar Hero II: pick your band,
  difficulty, character and guitar, choose a venue on the map and play its
  setlist (scores, stars and checks on each song); pass enough songs and the
  crowd calls for an encore; finish the encore to complete the tier, get
  sponsored and drive to the next venue. Cash for every new star and every
  tier, exactly as the original pays it (easier difficulties pay too when you
  play a harder one first).
- The store: spend your cash on bonus songs, guitars, finishes, characters,
  outfits and videos, shown in the shop as in the original. What you buy is
  yours at once: bonus songs in your setlist and in Quick Play, guitars and
  finishes in Change Guitar, characters and outfits in Change Character.
  Beating a tour puts its reward guitar on sale.
- Your tours, cash and purchases are saved with each band.
- Two-player modes, through Guitar Hero II's own Multiplayer menus:
  - Co-op: lead guitar with bass or rhythm, one shared Star Power meter that
    both players must tilt (or press Select) at the same time to use, a band
    streak and multiplier, one band score and a band rock meter that fails
    the song for both. Your co-op records and bass unlocks are saved.
  - Face-off: you take turns on the song's sections, the higher score wins.
  - Pro Face-Off (unlocked by beating a tour): both of you play every note.
  - Each player picks a part, difficulty, character and guitar; scores are
    balanced between parts as in the original; the band on stage shows both
    of you.
  - Player 2 plays on a second guitar or gamepad, or on the keyboard's right
    half (H J K L ; frets, N / M strum, B Star Power, O pause) when there is
    no second controller.

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
- Store items belong to the band that bought them, as in the original; Quick
  Play offers every song and item any band has unlocked.

### Fixed
- Going back to the main menu no longer leaves its camera off to one side.
- Audio Settings shows its button help at the bottom and its whole poster.
