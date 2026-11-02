# Changelog: gd-game-04

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-04/vX.Y.Z`. Nothing is released yet.

## [Unreleased]

### Fixed
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
