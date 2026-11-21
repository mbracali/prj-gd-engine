# Guitar Hero World Tour (gd-game-07), started 11-17

Owner, 11-17: start the reimplementation of Guitar Hero World Tour (Xbox 360), taking Guitar Hero: Metallica
(gd-game-10, the same gh4 engine one year later) as the example. Work started on `feature/20261117-aerosmithing`,
shared with the Aerosmith (gd-game-06) and Metallica (gd-game-10) lanes: each side touches only its own game; shared
files (backlog, launcher) only for its own lines.

## Rules

- Never commit or push (owner 11-17): the other agents on the branch commit when needed. Never stash, pull, rebase or force.
- Memory: one build at a time, `free -g` first; Ghidra at most 6 GB and never during a build. Quick checks only.
- Decrypted executables and extracted data stay in gd-game-07's git-ignored `ghidra/` / `extracted*/`.
- Song audio: gd10's bank reader, as copied (owner 11-17, gd-game-07/agents/DECISIONS.md D3).
- Never PR / merge / bump versions unasked.

## Plan

1. GD07-001 discovery: the disc and its formats against gd-game-10's, the executable in Ghidra, the differences
   from Metallica; a milestone plan with estimates.
2. Then the milestones the owner approves (gd-game-07/agents/PROGRESS.md "Plan", backlog GD07-002..008).

## Progress

- 11-17 GD07-001 done (about 30 min): disc extracted (6,635 files), XEX in Ghidra (`ghwt.gpr`, 30,030 functions,
  `analysis/` exported, title ID 4156081A); paks / QB / scripts parse with gd10's tools; gd10's `--formattest`
  fails on 10,656 files in 8 groups (compressed .ska keys, earlier scene header, older Havok, .wav chunks, ...);
  1,692 of 1,708 C functions shared with Metallica. Recommendation: copy gd10's engine (gd-game-07/agents/DECISIONS.md
  D2); M1..M7, about 17-29 h of agent time; owner questions in gd-game-07/agents/OPEN_QUESTIONS.md.
- 11-17 GD07-002 (M1) done (about 25 min): gd10's engine copied as gd07 (gd-game-07/agents/DECISIONS.md D5);
  `gd07 --formattest`: all files parse (from 10,656 failures). Boot smoke: disc and scripts load, black at
  uistate_null: next is GD07-003 (M2, boot to the main menu).
- 11-17 GD07-003 (M2) in progress: `gd07 --walktest boot` reaches the main menu (legal, movies, press start, band
  name, controller tip). Left: menu layout scale / backdrop (gd-game-07/agents/PROGRESS.md).
- 11-17 GD07-004 (M3) done: `gd07 --walktest quickplay` plays a song (Aggro, Frat House) to the results and back.
  Menu fixes (screen FX grain, stale menus). Left for M2: the menus drawn onto the sound stage (viewport UI).
- 11-17 GD07-003 (M2) done: the menus are drawn as World Tour does, into textures on the sound stage (main menu
  board, the rockers' jacket patches); LevelObject props placed and animated. Next: M4 (venues, band, CAS).
- 11-17 the darker scenes (light show snapshots never played; gh4 vertex-light attenuation; shared textures) and the
  band on stage (CAS paks under COMPRESSED); 18 venues draw (gd-game-07/agents/DECISIONS.md D9). Metallica (gd10) shares
  the three lighting gaps: worth porting there.
