# Changelog: gd-game-05

All notable changes for players. Versions follow SemVer; gd-game-05 ships
inside the Guitar Dream Universal Launcher's release
(`docs/platform/release-process.md`).

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
