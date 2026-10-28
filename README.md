
# Guitar Dream Engine

A re implementation of all major guitar hero and rock band engines.
- Don't contain any original code.
- Don't contain any original asset.
- Don't distribute any original asset.
- We don't support or endorse piracy in any regard.
- This repo does not contain any ISO or original asset.
- If you want to play, buy and rip original media.

**This is a labor of love!**

# Contents

| id         | Original game                     | Base version  | Done         |
| ---------- | --------------------------------- | ------------- | ------------ |
| gd-game-01 | Guitar Hero                       | Playstation 2 | 94% Playable |
| gd-game-02 | Guitar Hero II                    | Xbox 360      | In progress  |
| gd-game-03 | Guitar Hero Encore: Rocks the 80s | Playstation 2 | Evaluating   |
| gd-game-04 | Guitar Hero III: Legends of Rock  | Xbox 360      | Evaluating   |
| gd-game-05 | Rock Band                         | Xbox 360      | --           |
| gd-game-06 | Guitar Hero: Aerosmith            | Xbox 360      | --           |
| gd-game-07 | Guitar Hero World Tour            | Xbox 360      | --           |
| gd-game-08 | Rock Band 2                       | Xbox 360      | --           |
| gd-game-09 | The Beatles: Rock Band            | Xbox 360      | --           |
| gd-game-10 | Guitar Hero: Metallica            | Xbox 360      | --           |
| gd-game-11 | Guitar Hero: Smash Hits           | Xbox 360      | --           |
| gd-game-12 | Guitar Hero 5                     | Xbox 360      | --           |
| gd-game-13 | Lego Rock Band                    | Xbox 360      | --           |
| gd-game-14 | Band Hero                         | Xbox 360      | --           |
| gd-game-15 | Guitar Hero: Van Halen            | Xbox 360      | --           |
| gd-game-16 | Green Day: Rock Band              | Xbox 360      | --           |
| gd-game-17 | Guitar Hero: Warriors of Rock     | Xbox 360      | --           |
| gd-game-18 | Rock Band 3                       | Xbox 360      | --           |
| gd-game-19 | Rock Band 4                       | Xbox One      | Considering  |

# Repository layout

```
prj-gd-engine/
  README.md LICENSE LICENSE-ADDITIONAL-TERMS.md
  .gd-game-00-boilerplate/   the template for a new game folder
  .github/                   workflows and the repo guard
  gd-backlog.md              THE single backlog (every game, the launcher, releases, the site)
  guidelines/                the rules for every task (read before working)
  spikes/                    studies and spikes (platforms, signing, stores, HDR...)
  docs/                      the official codebase documentation
  gd-engine-docs/            the engine codebase (shared engine documentation)
  gd-engine-launcher/        the Guitar Dream Universal Launcher
  res/                       official Guitar Dream assets (icon)
  original_isos/             your own disc images and ROMs (git-ignored)
  gd-game-01/ ... gd-game-19/
```

Every game folder (`gd-game-02` is the model; `gd-game-01` keeps its older
layout for now) has `README.md`, `agent-instructions.md` (how the system works and the rules for AI agents), `CHANGELOG.md`, `local_build.sh`,
`game-info.yaml`, `build_commands/`, `docs/`, `engine/`, `tools/`, `agents/`
(notes for AI coding agents) and the git-ignored `extracted/` (your rip),
`ghidra/` (reverse-engineering project; never committed) and `local_builds/`.
`game-info.yaml` holds the game's id, names, launch date, platforms, its
Guitar Dream version, whether it is ready to play and how complete it is.

# Before you work

Read [guidelines/](guidelines/README.md) (general guidelines, the universal
launcher rules and the behaviours every game shares) and the single backlog
[gd-backlog.md](gd-backlog.md). Update the backlog at the start and at the end
of every task. Studies and spikes are in [spikes/](spikes/README.md).

# Guitar Dream Universal Launcher

`gd-engine-launcher/` holds the launcher (gdul): one program, one window, that
lists every game from `gd-engine-launcher/games.yaml` (the path of each
`game-info.yaml`), recognises your disc images by their content (found in
`original_isos/`, picked with Load ISO, or dropped onto the window) and plays
the ready games inside its own window. Each game is a module (a `.so` /
`.dll`) the launcher loads into its process and hands its window; quitting the
game returns to the list. Every action is logged (date, user, action) in
`gdul.log` in the launcher's save folder. Its version is in
`gd-engine-launcher/launcher-info.yaml` (from 0.1.0).

Build it (Linux and Windows x86-64) with `gd-engine-launcher/scripts/build.sh`;
the result is in `gd-engine-launcher/local_builds/gdul-<version>-<os>-x86_64/`,
and a game module is only rebuilt when its `guitar_dream_version` changed.
Architecture, the module ABI, the build manifest and the log:
[gd-engine-launcher/README.md](gd-engine-launcher/README.md).

# Licence

Free to play, study and fork, **not to sell**: PolyForm Noncommercial 1.0.0
([LICENSE](LICENSE)) plus the project's
[additional terms](LICENSE-ADDITIONAL-TERMS.md). In short:

- no selling, no paywalled builds, no ads;
- every copy and fork credits *"Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine"*;
- whoever distributes a modified version publishes its full source, marks
  every changed file and lists the changes in a `CHANGES.md`;
- gameplay videos and streams are welcome, monetised or not.

Third-party code keeps its own licence. The games' names are trademarks of
their owners; this project is not affiliated with or endorsed by them.

# Documentation

- [docs/README.md](docs/README.md): the official codebase documentation
- [docs/legal.md](docs/legal.md): what never goes into this repo, and why
- [docs/contributing.md](docs/contributing.md): branches, commits, pull requests
- [docs/release-process.md](docs/release-process.md): how the launcher (with every game) is released
- [docs/adding-a-game.md](docs/adding-a-game.md): starting a new game folder
- Each game's own README, `docs/` and `CHANGELOG.md`
