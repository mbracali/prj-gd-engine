# General guidelines

The owner's rules for every task in this repository, for people and AI agents
alike. Read every file in [guidelines/](README.md) before you start.

## Before you start

- **Guidelines.** Read and understand all the guideline files before starting:
  this one, [universal_launcher.md](universal_launcher.md) and
  [smilar_behaviors.md](smilar_behaviors.md).
- **System and app.** Understand the project before changing it: read the
  documentation ([README.md](../README.md), [docs/](../docs/README.md), the
  game folder's `README.md`, `agent-instructions.md` and `docs/`).
- **Backlog (extreme importance).** [gd-backlog.md](../gd-backlog.md) is the
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

## When you finish

- **Codebase.** When something is finished, commit and push it. The owner
  says when it is time for a pull request.
- **Version.** At PR time, bump the version of everything that changed: each
  game module (`game-info.yaml` `guitar_dream_version`, its CHANGELOG) and the
  launcher (`gd-engine-launcher/launcher-info.yaml`, its CHANGELOG).
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
