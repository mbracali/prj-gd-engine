# Guitar Hero: Aerosmith (gd-game-06), branch `feature/20261117-aerosmithing`

Owner, 11-17: start the reimplementation of Guitar Hero: Aerosmith (Xbox 360), expected to be very close to Guitar
Hero III (gd-game-04). Another agent works on the same branch on Guitar Hero: Metallica (gd-game-10) bugs: each
side touches only its own game; shared files (backlog, launcher) only for its own lines.

## Rules

- Commit only our files (gd-game-06, this file, our backlog rows) through a private index on top of the newest of
  local / origin; never stash, pull, rebase or force; a rejected push or diverged branch is reported, not fixed by force.
- Memory: the other agent may build too: build at -j2, `free -g` first, one game binary at a time, Ghidra at most
  6 GB and never during a build. Quick checks only; no long gates.
- Decrypted executables and extracted data stay in gd-game-06's git-ignored `ghidra/` / `extracted*/`.
- Song audio: GH3-engine songs are encrypted FSB banks; Claude does not write or wire the decryption of song audio
  (owner informed 11-17); the engine keeps one entry point for it.
- Never PR / merge / bump versions unasked.

## Plan

1. GD06-001 discovery: the disc and its formats against gd-game-04's (paks, QB / scripts, textures, scenes,
   anims, fonts, charts), the executable in Ghidra (XEXLoaderWV, as gd-game-04 / gd-game-10), the differences from
   GH3; a milestone plan with estimates and what gd-game-04's code gives (shared or copied).
2. Then the milestones the owner approves (data layer, boot to the main menu, one song).

## Progress

- 11-17 GD06-001 done (about 45 min): disc extracted (806 files), XEX in Ghidra (`gha.gpr`, 33,178 functions,
  `analysis/` exported); Aerosmith is GH3's engine (`GH3_xp1`): all formats but the `.ska` rotation keys parse with
  gd04's decoders, ~97% of the XEX's functions pair with GH3's. Recommendation: copy gd04's engine without its song
  decryption (gd-game-06/agents/DECISIONS.md D2); milestones M1..M6, about 11-18 h of agent time
  (gd-game-06/agents/PROGRESS.md); owner questions in gd-game-06/agents/OPEN_QUESTIONS.md; backlog GD06-002..008.
