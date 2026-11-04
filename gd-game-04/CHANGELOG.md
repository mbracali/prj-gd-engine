# Changelog: gd-game-04

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-04/vX.Y.Z`. Nothing is released yet.

## [Unreleased]

### Added
- Every Enhanced Option that can work now does: the picture effects (bloom, anti-aliasing, FSR 1, internal resolution, colour, grain, AO, sharpening, Star Power blur, light streaks, bokeh), sharper text, the venue extras and ground fog, no shaky camera, the cheat rows, forgiving HOPOs, touch controls and cut content (GD04-054).
- Star Power's stage effects now play for every character, including the Tesla lightning from the guitar; the hearts / bats swirl (GD04-055).
- The `volume` light beams and the stage projectors are drawn (approximated, GD04-056).
- The band receives the stage lights' shadows (GD04-053).
- The band takes its intro poses during the venue intro, as GH3's play_intro_anims (GD04-049).
- Star Power effects on the band (hearts, flames, peace signs, bats...) and the 4x finger sparks (GD04-050).
- Enhanced Options: Export User Files, Load User Settings, Load User Progress (GD04-051).
- Enhanced Options: Audio / Video Calibration, Reset Calibration and Enable Particles FX (GD04-052).
- In-Game Max FPS: 144 (#57).
- Songs open with GH3's own intro: the full 8-second venue intro with its cameras (the port used the 3-second fast one), the encore's and the boss's own intros, and the pre-encore / pre-boss endings.
- Lou's finish and defeat scenes play with their animations and cameras.

### Fixed
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
