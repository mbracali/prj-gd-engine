# Changelog: gd-game-04

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-04/vX.Y.Z`. Nothing is released yet.

## [Unreleased]

### Added
- The career's movies play: the career intro at the first venue select, each tier's completion movie and the ending (GD04-070).
- The band reacts in battles: the attacker plays its attack, the target its hit reaction, as GH3 (GD04-077).
- The drummer's and the singer's arms and feet follow GH3's IK (GD04-078).
- Esc (Back) three times on the main menu returns to the launcher; Enhanced > Remove All Effects (GD04-074).
- Sparks and debris bounce off the stage as in GH3 (GD04-065).
- GH3's stage projectors: the round light pools where the spotlights hit the stage, with the band's shadow in them, and the encore helicopter's and crowd spotlights' light on whatever they hit (GD04-064).
- The band's twist and split helper bones (wrists, elbows, knees, hips) and the leg IK (GD04-064).
- The `volume` light beams of seven venues use GH3's own raytraced beam shader against the scene's depth; they were not drawn (GD04-063).
- The universal Gamepad menu: four player boxes (instrument, device, Customize inputs), two bindings per control with Save / Discard, Detect, Export / Import, Generic MIDI controllers (GD04-060).
- The universal Enhanced menu: one-click MODES (Steam Deck, Original Feel, Clean Image, Max Out, Rocking on a Budget, Good Old Days) and the shared sections and ranges (GD04-060).
- Every Enhanced Option that can work now does: the picture effects (bloom, anti-aliasing, FSR 1, internal resolution, colour, grain, AO, sharpening, Star Power blur, light streaks, bokeh), sharper text, the venue extras and ground fog, no shaky camera, the cheat rows, forgiving HOPOs, touch controls and cut content (GD04-054).
- Star Power's stage effects now play for every character, including the Tesla lightning from the guitar; the hearts / bats swirl (GD04-055).
- The band receives the stage lights' shadows (GD04-053).
- The band takes its intro poses during the venue intro, as GH3's play_intro_anims (GD04-049).
- Star Power effects on the band (hearts, flames, peace signs, bats...) and the 4x finger sparks (GD04-050).
- Enhanced Options: Export User Files, Load User Settings, Load User Progress (GD04-051).
- Enhanced Options: Audio / Video Calibration, Reset Calibration and Enable Particles FX (GD04-052).
- In-Game Max FPS: 144 (#57).
- Songs open with GH3's own intro: the full 8-second venue intro with its cameras (the port used the 3-second fast one), the encore's and the boss's own intros, and the pre-encore / pre-boss endings.
- Lou's finish and defeat scenes play with their animations and cameras.

### Fixed
- The results magazine shows your own character, outfit and style (each body's Mag_Photo by its asset context), for every character in 1P and 2P.
- Switching characters on the character select is smooth: the next characters are read ahead, a new one loads in the background while the old one stays up, and its textures upload a few milliseconds a frame (frames of 150-300 ms before).
- The bassist's and the singer's hair no longer turns white under the stage lights: GH3's hair shader read one of its terms with the wrong sign, and its multiplications now treat 0 x anything as 0 like the console's GPU.
- Quick play no longer picks the empty test venue (a black venue for the whole song); The Glitches' unlock left it open.
- The menus' characters (character select, the hub) take the soundcheck stage's PRT light as GH3 does instead of a stray top light.
- The keyboard's Enter finishes every name entry (the high score name after a song, the band name, a new cheat), as in the other games; it still selects in the menus.
- Every pill of the help bar takes a mouse click (SELECT, BACK, DELETE, ACCEPT, UP/DOWN...), and the pause menu takes the mouse during a song.
- The guitarists' fret hands grip the neck from below with the fingers on the strings: GH3's own IK, decoded from the game (GD04-066).
- Stutters and freezes: the press-start screen no longer drops to a few frames a second, character select and the end of the loading screen no longer hitch (the venue loads in the background) (GD04-072).
- Quick play picks a new random guitarist, guitar and venue each time (GD04-071).
- The Glitches band can pick every venue (GD04-069).
- White glare over the band: Enhanced > Bloom's Original is GH3's own bloom alone (it doubled it), and the light beams blend as GH3's (GD04-068).
- Hair and other layered band materials draw with GH3's render states (no longer see-through) (GD04-067).
- Star notes turn into normal notes when their Star Power phrase is lost; Star Power's glow and big lightning sit under the highway (GD04-073).
- Lou's finish ends in every venue (it could freeze in his own) (GD04-072).
- The band receives the stage lights' shadows again, without the speckles (a missing depth bias) (GD04-064).
- The guitarist's fret hand grips the neck; it floated above it with the fingers in the air (GD04-062).
- Sustain tails are GH3's straight ribbons at its width; they were short, wide cones (GD04-062).
- Esc pauses a song and goes back in the pause menu, where Enter selects; settings from 0.0.3 move Start from Tab to Enter (GD04-062).
- The venues' scripted cameras (Backyard Bash's intro, every encore, the boss cameras) play; they started in a wrong spot (GD04-061).
- A song's preview stops when the song starts (GD04-061).
- Song previews play again on the setlist (GD04-059).
- Career venue select moves one venue per press (GD04-059).
- Removed the experimental ground fog and the light streaks; the band's self-shadowing and the approximated projector and volume lights are off (GD04-059).
- The band froze and their hands stopped: the animator ran its events out of order (GD04-058).
- The music played seconds ahead of the notes: the song clock subtracted the intro twice (GD04-058).
- The Enhanced page's backdrop is a still close-up of the band with the singer on the right, not dimmed; the FPS counter uses the game's font (GD04-058).
- Menu music played over itself and on into songs; sound events keep GH3's instance limits and the song stops the menu music (GD04-057).
- The Neversoft logo video had no sound; videos now play all their audio tracks (GD04-057).
- The band's intro stance was lost; the venue's intro crowd sound plays (GD04-057).
- The Enhanced and Gamepads pages show the band behind them, and their help items look like GH3's (GD04-057).
- Practice played the whole song instead of the chosen section; it now ends at the section's end and shows its results (GD04-043).
- The Soundcheck venue's shadow comes from its own light (SetShadowDirFromLight) (GD04-043).
- Menu sounds, menu music, crowd and every other sound effect were silent: they were dropped before reaching the audio device (GD04-048).
- Songs in some venues (z_party) ran at 3 fps on integrated GPUs: venue particles were walked by the millions every frame (GD04-046).
- The venue intro lost its first second or more: the song clock ran while the venue loaded (GD04-047).
- Android: the same buffer-update path as macOS, which phone graphics drivers handle without stalling (#55).
- The mouse wheel in menus no longer jumps back when the mouse moves slightly (#51).

## [0.0.3] - 2026-11-03

### Fixed
- Career: winning the song that unlocks a boss now leads to the boss, with its help screen first, as in GH3.
- Scripts read a missing parameter as nothing, as GH3 does (it used to fall back to a global of that name).
- Sustain tails widen with the whammy bar, as in GH3 (the bar's recent history rolls up the tail).
- The whammy bar now bends the guitar's sound while you hold a note, like the
  original's pitch shifter, and analog bars follow your hand instead of acting
  as on / off.
- Character hub: your guitarist stands next to the menu again (it was cut off
  at the left edge), and in co-op both players' characters show above their
  menus.
- Menu scripts that start other scripts now run them at once, as the original
  does, so menu animations and dialog boxes start on the right frame.

## [0.0.2] - 2026-11-01

### Added
- Santroller guitars are detected as guitars.
- Android: shake the phone to whammy, with a Shake for Whammy checkbox.

## [0.0.1] (not released)

- The engine's shell: window, disc image (picked, remembered, opened as an
  image or a folder), input, audio device, screenshots, scripted keys; copied
  from gd-game-02's engine-independent layers.
- GH3's data layer: compressed files, PAK / PAB archives, QB keys and the
  debug-name table, the QB decoder, textures, scenes / models / skins,
  animations, fonts, sounds (`--formattest`).
- Song audio (decrypted FSB3, XMA1 stems), song list and charts, GH3's
  gameplay rules (`--selftest`: all charts fully hit), `--song` with GH3's
  2D highway, the QScript VM's first cut, Bink movies, the asset audit.
