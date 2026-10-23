# Guitar Dream Engine - Game 1
id = `gd-game-01`


**Generals about Guitar Dream Engine, approach on re implementations**:
- Don't contain any original code.
- Don't contain any original asset.
- Don't distribute any original asset.
- If you want to play, buy the original media.
(Support the industry)


**This folder contains the re implementation of**:
- **Game**: Guitar Hero
- **Version**: Playstation 2.
- **Launched**: Nov/2005.
- **Musics**: 47 (30 career + 17 bonus)
- **Venues**: 6
- **Chars**: 8

**In-game instrument support**:
- [x] Guitar
- [ ] Bass
- [ ] Drums
- [ ] Vocals
- [ ] Keys
- [ ] Real guitar/bass (MIDI)
- [ ] Real drums (MIDI)

**In-game features**:
- [X] Single player
    - [X] Carrer
    - [X] Quick play
- [X] Two players
    - [ ] Carrer
    - [X] Quick play
- [ ] Band play
    - [ ] Carrer
    - [ ] Quick play
- [X] Unlocks and/or in-game shops
- [ ] Character creation
- [ ] Music creation


**Enhanced features by Guitar Dream Engine**:
- Higher resolutions.
- Higher framerates.
- AI upscalers.
- Image enhancers.
- Rich image filtering.
- Easy cheat codes.
- Cut content restored.
- Extended gamepad support.
- Mods support


# gd01: the program

`gd01` plays Guitar Hero (PS2, 2005) from **your own disc image**: the songs,
venues, band, menus and scripts all come from your copy of the game, and the
game's rules (hit window, hammer-ons and pull-offs, scoring, Star Power, rock
meter, career and store) were rebuilt from how the original behaves. It runs
on Linux, Windows, macOS and Android.

**Status:** alpha. Career, Quickplay, two-player and the tutorials play;
see [docs/backlog.md](docs/backlog.md) for what is still missing.

## Download

Releases: https://github.com/mbracali/prj-gd-engine/releases (files named
`gd01-<version>-<platform>`). No game data is included.

## Running

1. Rip your Guitar Hero (PS2) disc to an `.iso`, or copy the game folder
   (the one with `GEN/MAIN.HDR`).
2. Start `gd01` and drop the `.iso` (or the folder) onto the window, or pass
   it on the command line: `gd01 "Guitar Hero (USA).iso"`. The path is remembered.
3. Android: see [engine/README.md](engine/README.md) for where to put the image.

Saves and settings live in the `gd-engine/gd01` folder of your user data
(`~/.local/share`, `%APPDATA%`, `~/Library/Application Support`). Saves from
the earlier name (`gh1re`) are copied over on the first start.

## Building

See [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md). In short, on Fedora:

```sh
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel mingw64-gcc-c++ mingw64-SDL3
tools/fetch_dlss.sh          # optional: NVIDIA DLSS (downloaded from NVIDIA, 57 MB)
engine/build.sh              # Linux + Windows (+ Android when its SDK is installed)
engine/dist/gd01-linux-x86_64/gd01 --selftest "path/to/Guitar Hero (USA).iso"
```

## Folder

| Path | What |
|---|---|
| `engine/` | The program (C++20, SDL3, OpenGL; Vulkan for the AI upscalers) |
| `tools/` | Python readers for the game's formats, Ghidra scripts, `fetch_dlss.sh` |
| `docs/` | Research notes: `FINDINGS.md` (rules and formats), one note per system, `backlog.md` |
| `CHANGELOG.md` | What changed in each release |

## Licence

PolyForm Noncommercial 1.0.0 plus the project's additional terms (see
[LICENSE](../LICENSE) and [LICENSE-ADDITIONAL-TERMS.md](../LICENSE-ADDITIONAL-TERMS.md)):
free to play, study and fork; no selling; credit the original project; publish
your changes. Guitar Hero is a trademark of its owner; this project is not
affiliated with or endorsed by it.
