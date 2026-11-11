# gd-game-10: decisions

Each entry: what, why, how to undo.

## D1. gd10 starts from a copy of gd04's engine (2026-11-10)

What: `engine/`, `tools/`, `build_commands/`, `local_build.sh` copied from
gd-game-04's tracked files, renamed gd04 → gd10, GD04 → GD10,
gd-game-04 → gd-game-10, user-facing names → Guitar Hero: Metallica.
The engine version was set to 0.0.0 (matching `game-info.yaml`; not a bump).
Why: the kickoff (section 1); gh4 is GH3's engine one generation later.
Caveat: the copied code's comments keep GH3's **[C] addresses**: they are
GH3 XEX addresses, not Metallica's, until re-confirmed (each is **[I]** for
gd10 until then).
Undo: delete the copy.

## D2. Kickoff owner questions: defaults for the proof of concept (2026-11-10)

What: the run prompt's defaults (kickoff "Owner's answers"). Undo: the owner
answers differently.
