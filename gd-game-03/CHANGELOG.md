# Changelog: gd-game-03

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-03/vX.Y.Z`. Nothing is released yet.

## [Unreleased]

## [0.0.3] - 2026-10-30

### Added
- Tutorials 2 (Star Power) and 3 (hammer-ons and pull-offs) play through
  to the end: the hammer-on / pull-off trainer off the track, the lessons
  hearing every hit, miss, HOPO, whammy and Star Power, their pauses and
  their voice-overs.
- Winning a career tour shows its win screen (beating Easy, or the other
  difficulties) with the crowd's cheer before the results.
- Practice: the guitar keeps its pitch at slower speeds as on the PS2, and
  the metronome follows the music volume.
- The Horse Head cheat (Encore's own, code Blue, Orange, Orange, Blue,
  Yellow, Blue, Orange, Orange, Blue, Yellow): the band wear horse heads.
  Also in Enhanced Options > Cheats.
- The Flaming Head cheat now sets the guitarist's head on fire, and the
  band's other effects (hand flames, god particles) burn when the songs
  start them. Flaming Head is now available in Enhanced Options.

### Fixed
- A tutorial voice-over asked for while the one before is still loading
  now replaces it, as on the PS2 (it was dropped).
- Career practice and pause menus' buttons, and a material's texture set
  by script, now act as the original.
- Mono sound (Options) now plays every sound and the song from the centre,
  as the PS2 does, instead of averaging left and right (songs were 6 dB too
  quiet in mono).
- With the crowd meter off (practice, the tutorials, the crowd meter
  cheat) the meter now holds still instead of moving with your playing; the
  interface tutorial's meter shows the values it sets.
- Whammying a held sustain now makes its tail swell, as on the PS2: the
  tail widens up to four times as the bar goes down and the bulges run down
  the tail as you wave it, then it settles back once you let go. The tail
  used to get thinner instead. A Star Power phrase's tail and its glow line
  fade to their whammy colours (and its sparks grow) over a few frames, as
  the original, instead of switching at once. This works the same with an
  analog whammy bar, a gamepad stick, a MIDI controller and a whammy key or
  button.

## [0.0.2] - 2026-10-30

**Universal menus (as Guitar Hero II 0.1.8), and the menus move again: the
screens' camera transitions and the buttons' focus animation of the
original.**

### Added
- GAMEPADS is rebuilt as the universal Gamepad menu (the yellow fret on the
  main menu, or Enhanced Options > Gamepads): four player boxes (the game has
  two players; 3 and 4 are shown unavailable), an **Instrument** (5 Fret
  Guitar, Gamepad, Generic MIDI) and the device each player is **bound to**
  (the keyboard, any gamepad or guitar, any MIDI device, or "Detect"),
  **Customize inputs** with two buttons per control, Require Strum, the
  controller type and Reset, then **Save** or **Discard**; **Export** /
  **Import** save and load your controller settings (GD03-controllers.json).
- Generic MIDI input (Linux, Windows, macOS): any MIDI keyboard, drum pad or
  MIDI guitar can play once you bind its notes. Not on Android yet.
- Enhanced Options is rebuilt as the universal Enhanced menu: **Modes**
  buttons (Steam Deck, Original Feel, Clean Image, Max Out (Full HD), Max Out
  (4K), Rocking on a Budget, Good Old Days) set many options at once; the
  sections in the shared order (Modes, Controls, Performance & Graphics,
  Cheats & Enhancers), each can be folded; a scrollbar; Back, Export and
  Import buttons; Max FPS Unlimited. **Export User Files**, **Load User
  Settings** and **Load User Progress** now work. Options this version does
  not have yet (Enable Cut Content, FSR 3.1 / DLSS) stay listed, greyed.
- The help bar adds the buttons a screen is missing (SELECT, BACK, START on
  the band name, UP / DOWN) in the original's style; all are clickable.
- Menu animations, as the original plays them: every screen change moves the
  menus' shared camera out of the old screen and into the new one (going
  back, the other way), and a button that gets the focus grows to its focused size
  over a tenth of a second (a pressed button is a little bigger still).
- The menus' looping animations the scripts loop: the character select's
  flickering highlight and the store's spinning video case; the band name
  screen zooms to the name's length as you type.
- The Enhanced Options' effects, as in Guitar Hero II: **HDR** (None,
  Emulated, True HDR), Bokeh, Particles, Bloom, Cinematic Color, Color
  Grading (Warm, Cold, Vintage, Noir, Neon, Sepia), Film Grain, Chromatic
  Aberration, Ambient Occlusion, Sharpening, Blur on Star Power, All Band
  Mates Cast Shadows, More Lights, Dust in Light Beams, Disable Shaky Camera
  and Remove All Effects; Sharper Text & Sprites, Anti-Aliasing (FXAA, SSAA,
  TAA) and FSR 1 upscaling; touch controls; Audio / Video Calibration; No
  Fail, Autoplay and Forgiving Hammer-ons and Pull-offs. They work in 4:3
  and 16:9; the Modes set them (Max Out turns HDR to Emulated). Every effect
  starts off: the game looks as the original until you turn one on.
- The focused button's letters jiggle as in the original (a new jitter every
  tenth of a second), and the menus fade through black on every animated
  screen change, as the original's screens do.

### Changed
- New default controls, the same in every game: keyboard frets 1-5 (or
  F1-F5), arrows for menus and the strum, Enter is Start; two buttons per
  control without double inputs. Rock Band guitars and XInput gamepads
  always play (their switches are gone). controllers.json and
  enhanced.json from 0.0.1 still load.
- GAMEPADS and Enhanced Options open at once (no screen transition into
  them; the venue behind Enhanced Options is ready at startup).
- Every logo, movie and the credits skip with green, red, Enter, Start, a
  click or a tap.
- The cheats' codes in Enhanced Options are this game's (they were Guitar
  Hero II's).
