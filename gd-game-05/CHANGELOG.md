# Changelog: gd-game-05

All notable changes for players. Versions follow SemVer; gd-game-05 ships
inside the Guitar Dream Universal Launcher's release
(`docs/platform/release-process.md`).

## [0.0.3] - 2026-11-10

### Fixed
- No more crowd cheering at the start of the game: the menus play Rock
  Band's own music and crowd ambience only.
- Pressing the red fret (keyboard 2) as a song starts no longer loads the
  song again; back goes only where each screen's own back goes, with the
  backward transition.
- The main menu's words are whole and lit (no dark first letters), the
  focused button golden.
- The loading screens show a real tip instead of raw data; the lose screen
  shows how far the song got; confirm dialogs' messages are readable.

### Changed
- The menus play Rock Band's concert videos behind them, and the intro
  movie fills the screen.
- The song list sits on its cloth with readable rows, headings and the
  highlight bar; the difficulty screen is Rock Band's own (the koi banner,
  the player card: guitar or bass first, then the difficulty); the results
  show the player's panel; the shop and tour rooms are drawn as in Rock
  Band.
- Menu text takes Rock Band's colours; the help bar shows its actions with
  fret icons and takes mouse clicks; GAMEPADS is on the main menu's help
  bar.
- After 30 seconds on the title screen the attract videos play.

## [0.0.2] - 2026-11-09

### Added
- The band on stage: Rock Band's characters in their outfits and
  instruments, each camera shot posing and placing them; your own created
  character plays in its gigs.
- Vocals with a real microphone: Rock Band's pitch scoring, phrases,
  talkies, the tambourine and Overdrive by singing; on Android the game
  asks for the microphone at first start.
- Drums judged as Rock Band does (pads, kick, fills that deploy Overdrive,
  freestyle).
- Overdrive (gain, deploy, its sounds), the band multiplier, Unison
  bonuses, guitar solos with their percent bonus, Big Rock Endings,
  failing out and saving a bandmate, stars.
- Local multiplayer for 2 to 4 players: Band Quickplay, Score Duel and
  Tug of War; Drums and Vocals as instruments in GAMEPADS.
- Solo Tour and Band World Tour with saves, the Rock Shop, the character
  creator (name, hometown, hair, head), bands.
- Practice (sections and speeds), the tutorials for guitar, drums and
  vocals with their voice-overs, the credits, the lose screen's retry and
  quit.
- The camera follows each song's venue track the way Rock Band picks its
  shots; the crowd in the venues.

### Known gaps
- The HUD is a simple one, not Rock Band's; no help bar yet.
- Some venues are too bright; small clubs' crowds lie flat.
- The character creator has no 3D preview, physique or colours; the band
  lobby is a simple stand-in.
- Finishing a tutorial lesson by playing and the Drum Trainer are not
  checked yet.

## [0.0.1] - 2026-11-09

### Added
- The start of the Rock Band (Xbox 360) reimplementation, `gd05`: Guitar
  Hero II's engine (gd-game-02) as the base, renamed; it reads Rock Band's
  disc (XDVDFS, title ID 45410829) and its archive.
- Rock Band's own front end from the disc: the intro movie, the title, the
  main menu, Solo > Quickplay, the song list, the difficulty screen, the
  loading screens, the results and back to the song list; pause and resume.
- Play the guitar, the bass or the drums (`--part`, or the part chosen on
  the difficulty screen) on Rock Band's own highway: its gems, colours,
  beat lines, now bar and sustain tails.
- Rock Band's venues behind the track, with camera cuts from each song's
  venue track and the venues' lights.
- The songs' multitrack audio (the disc's encrypted .mogg files), the
  Bink movies, the menu music.
- Enhanced Options from Rock Band's options menu.

### Known gaps
- No band on stage yet; the crowd does not move; the HUD is a simple one.
- Vocals play the guitar part for now (the singing judge and the
  microphone input are written and tested, not in the game yet).
- Only Quickplay: no tours, practice, training or multiplayer modes yet;
  no saves of scores.
