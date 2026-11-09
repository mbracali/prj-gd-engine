# Agent instructions: gd-tool-02

Read this before working in this folder. It applies to every AI coding agent (Claude Code, Cursor, others).

## How the system works
- Guitar Dream Engine reimplements Guitar Hero / Rock Band engines. This folder is a **tool**, not a game: `Dream charter engine`.
- The charter for creating your own songs.
- `tool-info.yaml` describes the tool (name, status, version). Keep `version` equal to the tool's version.
- Status today: **Concept phase**. Nothing here is built yet.
- The games live in `gd-game-01/` … `gd-game-26/`. Shared code lives in `gd-engine-core/`. The launcher is `gd-engine-launcher/`. `gd-engine-tools/` is shared tooling, separate from this folder.
- Read the guidelines in `docs/guidelines/` before working, and the single backlog `docs/tasks/gd-backlog.md`. Update the backlog at the start and at the end of every task.

## This folder
**Dream charter engine** (`gd-tool-02`): Concept phase. Before writing code, plan it with the owner.

## Guardrails
- Never commit game data, ISOs, rips, decoded assets, decompiler output or Ghidra files. Run `python3 .github/scripts/repo_guard.py` (0 problems) before every commit.
- Never modify `gd-game-01/` unless the owner asks for that exact change.
- Commits and PRs are written as the owner: `git commit -s`, no AI co-author or footer lines. Push and open PRs only when the owner asks.
- Only release builds run on GitHub: no PR CI, no Dependabot, never start workflows yourself.
- Match the surrounding code style; new source files start with the SPDX header; no compiler warnings. Never name anything `environ` (a MinGW macro).
- Ask the owner when a decision is theirs.
- Keep notes for agents in `agents/` (scratch material in `agents/scratch/`, git-ignored).
