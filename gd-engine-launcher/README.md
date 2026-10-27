# Guitar Dream Universal Launcher (gdul)

One program that lists every Guitar Dream Engine game, finds your own disc
images and plays the games inside its own window. Version: `launcher-info.yaml`
(starts at 0.1.0).

- `games.yaml`: the path of each game's `game-info.yaml` (id, names, launch
  date, platforms, `guitar_dream_version`, `ready_to_play`, `completeness`).
- `scripts/`: scripts that build the launcher.
- `tools/`: helper tools.
- `agents/`: context notes for AI coding agents.
- `local_builds/`: local launcher builds, one folder per version and system
  (git-ignored).
- `logs/`: build and error logs (git-ignored).

Disc images: local builds look in the repository root's `original_isos/`
(git-ignored); a launcher download has its own `original_isos/` folder next to
the program.
