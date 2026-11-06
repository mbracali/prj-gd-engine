
# Guitar Dream Engine

[![Quiver Launcher](https://img.shields.io/badge/Quiver_Launcher-Get_it_here-7C3AED?style=for-the-badge)](https://quiverlauncher.com/apps/guitar-hero-guitar-dream-engine)
[![Recomp Launcher](https://img.shields.io/badge/Recomp_Launcher-Get_it_here-0EA5E9?style=for-the-badge)](https://recomplauncher.com/game/prj-gd-engine)

![C++](https://img.shields.io/badge/C%2B%2B-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![GLSL](https://img.shields.io/badge/GLSL-5586A4?style=flat-square&logo=opengl&logoColor=white)
![Python](https://img.shields.io/badge/Python-3776AB?style=flat-square&logo=python&logoColor=white)
![Shell](https://img.shields.io/badge/Shell-4EAA25?style=flat-square&logo=gnubash&logoColor=white)
![Java](https://img.shields.io/badge/Java-ED8B00?style=flat-square&logo=openjdk&logoColor=white)
![CMake](https://img.shields.io/badge/CMake-064F8C?style=flat-square&logo=cmake&logoColor=white)
![HTML](https://img.shields.io/badge/HTML-E34F26?style=flat-square&logo=html5&logoColor=white)

A re implementation of all major guitar hero and rock band engines.
- Don't contain any original code.
- Don't contain any original asset.
- Don't distribute any original asset.
- We don't support or endorse piracy in any regard.
- This repo does not contain any ISO or original asset.
- If you want to play, buy and rip original media.

**This is a labor of love!**

# Contents

## Games

This is a list of all the planned and already decompiled games. Every game is planned, but may not be fully featured in the launcher yet.

| id         | Original game                     | Base version  | Done          |
| ---------- | --------------------------------- | ------------- | ------------- |
| gd-game-01 | Guitar Hero                       | Playstation 2 | 99% Playable  |
| gd-game-02 | Guitar Hero II                    | Xbox 360      | 99% Playable  |
| gd-game-03 | Guitar Hero Encore: Rocks the 80s | Playstation 2 | 99% Playable  |
| gd-game-04 | Guitar Hero III: Legends of Rock  | Xbox 360      | 96% Playable  |
| gd-game-05 | Rock Band                         | Xbox 360      | Tunning in... |
| gd-game-06 | Guitar Hero: Aerosmith            | Xbox 360      | --            |
| gd-game-07 | Guitar Hero World Tour            | Xbox 360      | --            |
| gd-game-08 | Rock Band 2                       | Xbox 360      | --            |
| gd-game-09 | The Beatles: Rock Band            | Xbox 360      | --            |
| gd-game-10 | Guitar Hero: Metallica            | Xbox 360      | --            |
| gd-game-11 | Guitar Hero: Smash Hits           | Xbox 360      | --            |
| gd-game-12 | Guitar Hero 5                     | Xbox 360      | --            |
| gd-game-13 | Lego Rock Band                    | Xbox 360      | --            |
| gd-game-14 | Band Hero                         | Xbox 360      | --            |
| gd-game-15 | Guitar Hero: Van Halen            | Xbox 360      | --            |
| gd-game-16 | Green Day: Rock Band              | Xbox 360      | --            |
| gd-game-17 | Guitar Hero: Warriors of Rock     | Xbox 360      | --            |
| gd-game-18 | Rock Band 3                       | Xbox 360      | --            |
| gd-game-19 | Rock Band 4                       | Xbox One      | Not sure yet  |
| gd-game-20 | Guitar Hero: On Tour              | Nintendo DS   | --            |
| gd-game-21 | Guitar Hero On Tour: Decades      | Nintendo DS   | --            |
| gd-game-22 | Guitar Hero On Tour: Modern Hits  | Nintendo DS   | --            |
| gd-game-23 | Band Hero (DS)                    | Nintendo DS   | --            |
| gd-game-24 | Rock Band Unplugged               | PSP           | --            |
| gd-game-25 | DJ Hero                           | Xbox 360      | --            |
| gd-game-26 | DJ Hero 2                         | Xbox 360      | --            |


## Tools

The dream engine also feature some tools to enhance the experience of the decompilations. In the future you will be able to create your own songs, play songs from one game in another and add the characters of your dreams.

The open dream engine is a bigger project. The idea is to create a engine that everybody can use to create their own version of guitar games.

| id         | Name                 | Done          |
| ---------- | -------------------- | ------------- |
| gd-tool-01 | Additive music menu  | Concept phase |
| gd-tool-02 | Music mod / DLC menu | Concept phase |
| gd-tool-03 | Band mod menu        | --            |
| gd-tool-04 | Open dream engine    | --            |



# Repository layout

```
prj-gd-engine/
  README.md LICENSE LICENSE-ADDITIONAL-TERMS.md
  .gd-game-00-boilerplate/   the template for a new game folder
  .github/                   workflows and the repo guard
  docs/                      the official codebase documentation (and the site)
    guidelines/              the rules for every task (read before working): general guidelines,
                             universal launcher, mechanisms, menus, behaviours, engine backends, usual bugs
    tasks/gd-backlog.md      THE single backlog (every game, the core, the launcher, releases, the site)
    tasks/PARITY_GAPS.md     the parity gaps
    platform/                adding a game, the release process
    spikes/                  studies and spikes (platforms, signing, stores, HDR...), the kickoffs
    res/                     official Guitar Dream assets (icon, store art)
  gd-engine-core/            the universal backends every game can load (platform, inputs, audio, render, image, ui)
  gd-engine-launcher/        the Guitar Dream Universal Launcher
  gd-engine-tools/           shared tools (in construction)
  original_isos/             your own disc images and ROMs (git-ignored)
  gd-game-01/ ... gd-game-26/
```

Every game folder (`gd-game-02` is the model; `gd-game-01` keeps its older
layout for now) has `README.md`, `agent-instructions.md` (how the system works and the rules for AI agents), `CHANGELOG.md`, `local_build.sh`,
`game-info.yaml`, `build_commands/`, `docs/`, `engine/`, `tools/`, `agents/`
(notes for AI coding agents) and the git-ignored `extracted/` (your rip),
`ghidra/` (reverse-engineering project; never committed) and `local_builds/`.
`game-info.yaml` holds the game's id, names, launch date, platforms, its
Guitar Dream version, whether it is ready to play and how complete it is.

# Before you work

Read [docs/guidelines/](docs/guidelines/) (general guidelines, the universal
launcher rules and the behaviours every game shares),
[docs/guidelines/universal_mechanisms.md](docs/guidelines/universal_mechanisms.md) (the menus and mechanisms
every game has) and the single backlog
[docs/tasks/gd-backlog.md](docs/tasks/gd-backlog.md). Update the backlog at the start and at the end
of every task. Studies and spikes are in [docs/spikes/](docs/spikes/README.md).

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
- [docs/platform/release-process.md](docs/platform/release-process.md): how the launcher (with every game) is released
- [docs/platform/adding-a-game.md](docs/platform/adding-a-game.md): starting a new game folder
- Each game's own README, `docs/` and `CHANGELOG.md`
