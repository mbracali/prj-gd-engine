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

## Default settings

Each download starts with Enhanced Settings tuned for its system (change
them in Options > Enhanced Settings; your changes are kept).
"Apply Steam Deck Settings" sets the Steam Deck column on any PC.

| Setting | Linux | Steam Deck | Windows | macOS | Android |
|---|---|---|---|---|---|
| Screen Mode | Windowed | Full Screen | Windowed | Windowed | Full Screen |
| In-Game Max FPS | 120 | 60 | 120 | 120 | 60 |
| Show FPS Counter | on | off | on | on | off |
| Internal Resolution | 1080p | 800p | 1440p | 1440p | 900p |
| Sharper Text & Sprites | on | on | on | on | off |
| Anti-Aliasing / AI Upscaling | None | None | None | None | None |
| Bokeh Effect | Stronger | Stronger | Stronger | Stronger | Stronger |
| Particles FX | on | on | on | on | on |
| Bloom | High | Medium | High | High | Medium |
| Cinematic Color | on | on | on | on | on |
| Color Grading | Off | Off | Off | Off | Off |
| Film Grain | Light | Off | Light | Light | Off |
| Chromatic Aberration | on | on | on | on | on |
| Anamorphic Light Streaks | off | off | off | off | off |
| Ambient Occlusion | on | off | on | on | off |
| Sharpening | Low | Off | Low | Low | Low |
| Blur on Star Power | High | Medium | High | High | Light |
| All Band Mates Cast Shadows | on | off | on | on | off |
| More Lights on Venues | Subtle | Subtle | Subtle | Subtle | Subtle |
| Dust in Light Beams | on | off | on | on | off |
| Add Fog to the Ground | Low | None | Low | Low | None |
| Disable Shaky Camera | off | off | off | off | off |
| Controls | Keyboard (touch off) | Keyboard (touch off) | Keyboard (touch off) | Keyboard (touch off) | Touch |

In the menus every controller works: any gamepad, guitar or keyboard
navigates and selects, and on a screen with nothing to choose ("press any
button") a click or a tap continues.

**Touch** (always on in the Android app; Enhanced Settings > Enable Touch
Controls elsewhere): tap to click; in a song a finger on a lane plays it;
on-screen Up / Down / START buttons (menus) and a pause button (songs) show
once you touch the screen; two-finger swipes move up / down (in a song: Star
Power / pause). Set the buttons, swipes and pause corner in GAMEPADS >
Touch Screen.

**Controllers:** GAMEPADS lists every connected controller. Each one has a
**Type** (Auto, Gamepad, PS2 guitar (adapter), Xbox guitar, Rock Band guitar):
a PS2 guitar on an adapter that calls itself a PlayStation 3 pad plays as a
guitar once its type is "PS2 guitar (adapter)". For a bug report, attach
`controllers-report.txt` from the save folder (GAMEPADS shows its path).

## Building

The quickest way, producing the same files a release publishes, in
`local_builds/`:

```sh
./local_build.sh                  # the build for your system
./local_build.sh steamdeck        # or: linux, steamdeck, windows, macos, android, all
```

Or double-click a file in [build_commands/](build_commands/): `build-linux.sh`,
`build-steamdeck.sh`, `build-windows.sh` (Linux) / `build-windows.bat`
(Windows, with MSYS2), `build-macos.command`, `build-android.sh`, `build-all.sh`.
Each opens a terminal, builds, and tells you where the files are.

See [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) for the toolchains. Developer build on Fedora:

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
| `local_build.sh` | Builds the release downloads into `local_builds/` (`build_commands/`: double-click versions) |
| `CHANGELOG.md` | What changed in each release |

## Licence

PolyForm Noncommercial 1.0.0 plus the project's additional terms (see
[LICENSE](../LICENSE) and [LICENSE-ADDITIONAL-TERMS.md](../LICENSE-ADDITIONAL-TERMS.md)):
free to play, study and fork; no selling; credit the original project; publish
your changes. Guitar Hero is a trademark of its owner; this project is not
affiliated with or endorsed by it.
