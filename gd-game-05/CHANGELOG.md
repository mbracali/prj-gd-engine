# Changelog: gd-game-05

All notable changes for players. Versions follow SemVer; gd-game-05 ships
inside the Guitar Dream Universal Launcher's release
(`docs/platform/release-process.md`).

## [Unreleased]

### Fixed
- A Rock Band guitar (or any controller) set to "None" in GAMEPADS now plays a one-player song; before, the song took only its Start and ignored every fret and strum.
- A gamepad or guitar's Start works on the title screen.
- A gamepad plays the song with its frets even after the keyboard was used in the menus.
- Missed notes play the miss sound again (miss_gtr / miss_bass / drum misses from the original's player handlers).
- The loading screen's band icons are lit by their own light (they were dark olive).
- Options > Audio: the volume sliders show as full bars again (they were drawn edge-on, almost black); Options > Calibrate opens.
- The fret board reacts as in Rock Band: hit flames, shockwaves and glass shards from the smashers, held frets light their smashers, sustains burn, misses flash, Overdrive phrases flash when captured, and deploying Overdrive brings the golden spotlight to the track.
- Venue shots get their film look again (ProFilm post-processing was never selected when the band appeared); the highway rises in at the song start as in the original.

## [0.0.6] - 2026-11-10

### Added
- Enhanced: a new Accessibility section (before Cheats) with Easy Chords: a chord counts when you press any of its frets (#67). Off by default. (guitar and bass)

### Fixed
- Menu colours as on the original: the focused buttons are a deep orange
  again (not pale tan), the sub-menu labels light grey and white, and the
  picture is no longer washed out (the 0.85 gamma is what the console sends
  a TV, which on a TV cancels out).
- The sub-menus show their orange highlight bar on the focused item
  (QUICKPLAY under SOLO).
- The sub-menus' orange bar stays inside the button, under its metal rim
  (no orange box around it any more).
- Options > Enhanced Options has its button background like the others.
- The GAMEPADS button stays on the main menu whichever sub-menu is open.
- Labels are no longer grey on the menus without lights of their own (the
  results, fail, pause, tour, shop, band and practice screens).
- The keyboard plays a song (frets, strum, Overdrive on Tab, Enter to
  pause and resume) even with a guitar left plugged in; keys changed in
  GAMEPADS work in the song.
- The song list's names fit their rows as on the original (smaller text).
- The main menu no longer loses its buttons after going back to the title
  and pressing Start.
- The Enhanced Options backdrop shows the band on stage through one of the
  venue's own camera shots.

## [0.0.5] - 2026-11-10

### Fixed
- Brighter, truer colours everywhere: the game now applies Rock Band's own
  display gamma (0.85), so whites are white and the menus' orange is
  stronger.
- Esc three times on the main menu quits again (Rock Band's main menu goes
  back to the title, which reset the count).
- The instrument and difficulty card has its ornate paper again instead of
  a plain white sheet.
- The drummer is back on stage in slow songs (his animations were all
  filtered out), and sits at his kit.
- Clicking a main menu button while another one's sub-menu is open now
  opens its menu.
- Enhanced Options and GAMEPADS: orange titles and gold names (they were
  white), a bold menu font and Rock Band's Back / Select help bar, as in
  the other games.

### Added
- GAMEPADS is a button in the main menu's bottom-left corner, always there
  (click it, or press the yellow fret).

## [0.0.4] - 2026-11-10

### Fixed
- The loading screen's band (guitarist, singer, drummer, bassist) stands
  side by side and moves, instead of four icons piled on one spot.
- The strike line's fret smashers are drawn, each in its colour (green,
  red, yellow, blue, orange; the drum pads red, yellow, blue, green and the
  orange kick).
- The gems keep their own colours (the green gems no longer take another
  texture), and the drum gems sit on their pads without overlapping.
- Sustain tails show: thin glowing lines that wobble with the whammy bar
  and fade at their end, as in Rock Band.
- Screen changes no longer wait a second with nothing moving: screens
  change as Rock Band's do, waiting only for the animations Rock Band
  waits for.
- The results show the stars earned (gold ones too).
- QUIT from the pause menu is checked all the way back to the main menu.

### Changed
- The song list, the difficulty / instrument card and the shop, tour and
  creator rooms hang on their swaying cloth, seen through their own
  cameras.
- Menu animations play as Rock Band plays them: their speed, delays and
  loops (the lights, the pause menu's buttons coming in one after another,
  the loading animations), and a main menu button moves when chosen.

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
