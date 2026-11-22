# General guidelines

The owner's rules for every task in this repository, for people and AI agents
alike. Read every file in [docs/guidelines/](.) before you start.

## Before you start

- **Guidelines.** Read and understand all the guideline files before starting:
  this one, [universal_launcher.md](universal_launcher.md),
  [universal_mechanisms.md](universal_mechanisms.md),
  [universal-menus.md](universal-menus.md),
  [universal-behaviors.md](universal-behaviors.md),
  [engine-backend-guidelines.md](engine-backend-guidelines.md),
  [usual-bugs.md](usual-bugs.md) and, for the additive-songs mod,
  [additive-and-custom-songs.md](additive-and-custom-songs.md).
- **System and app.** Understand the project before changing it: read the
  documentation ([README.md](../../README.md), [docs/](../README.md), the
  game folder's `README.md`, `agent-instructions.md` and `docs/`).
- **Backlog (extreme importance).** [gd-backlog.md](../tasks/gd-backlog.md) is the
  single backlog. Read it and update it **at the start and at the end of every
  task**: mark what you start, mark what you finish, add what you found. Keep
  IDs stable and classify every item (Original re-implementation, Enhancement
  or Bugfix).
- **Branching.** Work happens in the current feature branch (now
  `feature/20261028-gen-improvements`). Commit and push what is already there
  before starting. Pull requests are a gate the owner triggers.
- **Time to finish.** Give an estimate when you start.
- **Questions.** Ask whenever you are not 100% sure, with the interactive
  question tool.

## While you work

- **Memory.** Anything needed outside the current context goes in a markdown
  file (the game's `docs/`, `agents/`, the backlog or a guideline), never only
  in a conversation.
- **Match the original.** Behaviour of the original game comes first; label
  any temporary approximation.
- **Multi-platform.** Test on as many systems as possible (Linux, Steam Deck,
  Windows, macOS, Android) before finishing.
- **Tests.** Test what you did: people use the app a lot. Regular tests run
  mainly on Linux; every bug fix that can be reproduced headless gets a check
  in the game's self-tests.
  - **Quick tier, during work:** `tools/quicktest.sh` (GDALL-049) builds only
    the games your changes touch (against `origin/develop`; `--base <ref>` or
    game ids such as `gd02 gd04` to choose) and runs each one's `--selftest`
    plus at most 3 short walks picked from the source folders changed,
    printing one PASS / FAIL table (logs in `~/.cache/gd-quicktest/`). Builds
    run one game at a time with `-j4` after a free-memory check; tests run one
    game at a time unless `--jobs <n>`. Target: under 5 minutes a game.
    `--list` shows what it would run, `--walks "a b"` picks the walks.
  - **Full gates, only before a release:** every walk, venuetest, chartest and
    the other long suites of the game (e.g. `gd-game-03/tools/run_tests.sh`,
    `gd-game-01/tools/walks.sh` with no walk named). Never run them as a gate
    during normal work (owner 11-13: no 30-40 minute gates).

## When you finish

- **Codebase.** When something is finished, commit and push it. The owner
  says when it is time for a pull request.
- **Version.** **NEVER bump the launcher version (`launcher_version` in
  `gd-engine-launcher/launcher-info.yaml`) unless the owner explicitly asks
  for it.** A launcher bump merged into `main` starts a public release, so
  it has a huge impact. "Commit, push and PR" is **not** a request to bump.
  Bump a game module's version (`game-info.yaml` `guitar_dream_version`, its
  engine version, its CHANGELOG) only when the owner asks for a bump as well.
  When a bump seems due, ask the owner first; never bump on your own.
- **Packages.** After any bugfix or improvement, build everything locally:
  the latest launcher with all the latest game modules (every platform except
  macOS). In the launcher's `local_builds/` keep only the current and the
  previous version; delete older ones. See
  [universal_launcher.md](universal_launcher.md) "Never forget a package".

## Recurrent bugs that must never happen again

- The universal launcher released without a game module ("This launcher build
  has no gd-game-NN module"), on any platform.
- The launcher unable to load ISOs, especially on Android and Windows.

Check both on every build before it leaves your machine.
