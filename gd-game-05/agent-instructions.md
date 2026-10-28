# Agent instructions: gd-game-05

Read this before working in this folder. It applies to every AI coding agent (Claude Code, Cursor, others).

## How the system works
- Guitar Dream Engine reimplements Guitar Hero / Rock Band engines in C++17 + SDL3 + OpenGL. Each game folder holds one game's engine, docs and tools; the game loads the player's **own** disc image at run time.
- `game-info.yaml` describes the game (names, version, readiness). Keep `guitar_dream_version` equal to the engine's version.
- The Guitar Dream Universal Launcher (`gd-engine-launcher/`) loads each ready game as a module (`gd_game_run`, optional `gd_game_explore`) into its own window, and is the project's only release (`docs/release-process.md`).
- Behaviour comes from the original game's executable (Ghidra project in `ghidra/`, git-ignored). The goal is to match the original exactly; mark findings **[C]** (confirmed, with address) or **[I]** (inferred) in the docs.
- Read the guidelines in `guidelines/` at the repository root before working (`general_guidelines.md`, `universal_launcher.md`: the GAMEPADS screen, Enhanced menu, mouse / Enter / touch and packaging, the same in every game, modelled on gd-game-01; `smilar_behaviors.md`), and the single backlog `gd-backlog.md` at the root (section of this game). Update the backlog at the start and at the end of every task.

## This folder
**Rock Band** (Xbox 360): not started (Guitar Dream version 0.0.0, not ready to play). Before writing code, plan it with the owner and model the folder on `gd-game-02`.

See `docs/adding-a-game.md` at the repository root.

## Guardrails
- Never commit game data, ISOs, rips (`extracted/`), decoded assets, decompiler output or Ghidra files. Run `python3 .github/scripts/repo_guard.py` (0 problems) before every commit.
- Never modify `gd-game-01/` unless the owner asks for that exact change.
- Commits and PRs are written as the owner: `git commit -s`, no AI co-author or footer lines. Push and open PRs only when the owner asks.
- Only release builds run on GitHub: no PR CI, no Dependabot, never start workflows yourself.
- Match the surrounding code style; new source files start with the SPDX header; no compiler warnings. Never name anything `environ` (a MinGW macro).
- Log every unhandled script message, stub or unknown class (lesson L1); don't hide failures.
- Ask the owner when a decision is theirs; prefer the original game's behaviour over approximations, and label any temporary approximation.
- Keep notes for agents in `agents/` (scratch material in `agents/scratch/`, git-ignored).
