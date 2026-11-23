# gd-game-28 — Guitar Hero (iOS) — KICKOFF

**Status:** placeholder / backlog only, LOW PRIORITY (GitHub #139, backlog GD28-001).
Nothing is implemented yet. This file exists so an agent starting this game is
oriented from day one.

- **Game:** Guitar Hero (iOS), Activision, 2010 (confirm exact date in Discovery).
- **Env prefix / quicktest / build:** none yet — `gd-game-28/local_build.sh` is a stub.
- **First task:** the Discovery task GD28-001 (see `docs/tasks/gd-backlog.md`):
  the game's package and formats, its input model (on-screen taps + drag/strum
  gestures — map to buttons for PC and touch), what the existing games give it,
  and a plan with the owner. See [docs/platform/adding-a-game.md](../docs/platform/adding-a-game.md).

**Recurrent-bug warnings (read before any engine work):** see
[docs/guidelines/usual-bugs.md](../docs/guidelines/usual-bugs.md) — never raise
brightness to fix dark scenes; keep hair/cloth/accessory physics correct; keep
menus fully mouse-clickable and correctly framed. (These bit every other game.)
