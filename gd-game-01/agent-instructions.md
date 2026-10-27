# Agent instructions: gd-game-01

Read this before working in this folder. It applies to every AI coding agent (Claude Code, Cursor, others).

## How the system works
- Guitar Dream Engine reimplements Guitar Hero / Rock Band engines in C++17 + SDL3 + OpenGL. Each game folder holds one game's engine, docs and tools; the game loads the player's **own** disc image at run time.
- `game-info.yaml` describes the game (names, version, readiness). Keep `guitar_dream_version` equal to the engine's version.
- The Guitar Dream Universal Launcher (`gd-engine-launcher/`) loads each ready game as a module (`gd_game_run`, optional `gd_game_explore`) into its own window, and is the project's only release (`docs/release-process.md`).
- Behaviour comes from the original game's executable (Ghidra project in `ghidra/`, git-ignored). The goal is to match the original exactly; mark findings **[C]** (confirmed, with address) or **[I]** (inferred) in the docs.

## This folder
**Guitar Hero** (PlayStation 2), Guitar Dream version 1.1.3, playable and released. This folder is **frozen**: do not change anything in it unless the owner explicitly asks for that change.

See `README.md` and `docs/` here (older layout: `engine/`, `local_build.sh`, `build_commands/`).

## Guardrails
- Never commit game data, ISOs, rips (`extracted/`), decoded assets, decompiler output or Ghidra files. Run `python3 .github/scripts/repo_guard.py` (0 problems) before every commit.
- Never modify `gd-game-01/` unless the owner asks for that exact change.
- Commits and PRs are written as the owner: `git commit -s`, no AI co-author or footer lines. Push and open PRs only when the owner asks.
- Only release builds run on GitHub: no PR CI, no Dependabot, never start workflows yourself.
- Match the surrounding code style; new source files start with the SPDX header; no compiler warnings. Never name anything `environ` (a MinGW macro).
- Log every unhandled script message, stub or unknown class (lesson L1); don't hide failures.
- Ask the owner when a decision is theirs; prefer the original game's behaviour over approximations, and label any temporary approximation.
- Keep notes for agents in `agents/` (scratch material in `agents/scratch/`, git-ignored).
