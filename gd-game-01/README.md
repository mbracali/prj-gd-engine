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
see [gd-backlog.md](../gd-backlog.md) (section gd-game-01) for what is still missing.

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

Every enhanced effect starts off: the game looks like the original. Change
them in Options > Enhanced Settings (your changes are kept); the **Modes**
buttons set many at once (Steam Deck, Original Feel, Clean Image, Max Out
(Full HD), Max Out (4K), Rocking on a Budget, Good Old Days). Only the window,
frame rate, resolution and touch follow the system:

| Setting | Linux | Steam Deck | Windows | macOS | Android |
|---|---|---|---|---|---|
| Screen Mode | Windowed | Full Screen | Windowed | Windowed | Full Screen |
| Max FPS | 120 | 60 | 120 | 120 | 60 |
| Resolution | 1080p | 800p | 1440p | 1440p | 900p |
| Enable Touch Controls | off | on | off | off | on (always) |
| Bokeh Effect / Particles FX | Medium (original) / Original | the Steam Deck mode | as Linux | as Linux | as Linux |
| Every other effect | None / off | the Steam Deck mode (light bloom, cinematic colour, light Star Power blur, FXAA) | None / off | None / off | None / off |

**HDR** (Performance & Graphics): None (the default, the original image),
Emulated (high dynamic range tone mapped to any screen) or True HDR (an HDR
display on Windows, or on a Linux Wayland desktop whose driver offers an
scRGB / PQ surface; it starts with the game, and falls back to Emulated
with the reason in the help box where it cannot run).

Settings saved by 1.1.6 and older keep their meaning (for example Bloom High
is now 3x, Chromatic Aberration on is Medium). XInput gamepads and Rock Band
guitars always play (their switches are gone); mouse selection is on.

In the menus every controller works: any gamepad, guitar, keyboard, mouse or
touch screen navigates and selects; logos, the intro movie and the credits
skip with green, red, Enter, Start, a click or a tap; the help bar's buttons
(CONFIRM, BACK, START, UP / DOWN) are clickable. NAME YOUR BAND: any up / down
changes the letter, green takes it, Start or Enter accepts the name.

**Default controls** (universal_mechanisms.md at the repository root):

| Control | Keyboard | Gamepad (PS2 layout) |
|---|---|---|
| Green / Red / Yellow / Blue / Orange fret | 1 / 2 / 3 / 4 / 5 and F1 to F5 | L2 / L1 / R1 / R2 / Cross (A) |
| Up / Down, Strum Up / Strum Down | Up / Down arrows | D-pad up / down |
| Start | Enter | Start |
| Select, Gyro (Star Power) | Tab | Select |
| Whammy | Shift | either analog stick |
| Require Strum | on | off (a fret press also strums) |

Esc / Backspace go back and the arrows move in every menu, whatever the
bindings. A key bound to two controls (Up is menu Up and Strum Up) acts once.

**GAMEPADS** (the yellow fret or the GAMEPADS button in the main menu): four
player boxes (Guitar Hero has two players; 3 and 4 are shown unavailable).
Click "Player 2" to turn player 2 on or off. Each player has an
**Instrument** (5 Fret Guitar, Gamepad, Generic MIDI) and **Bind to:** any
connected keyboard, gamepad, guitar or MIDI device, or "Detect", which takes
the next device you press. **Customize inputs** shows that device's buttons
(two per control) and Require Strum; **Save** or **Discard** finishes.
**Export** / **Import** save and load all controller settings as a file.
Generic MIDI (Linux, Windows, macOS; not on Android yet) starts with nothing
bound: bind the notes you want. A PS2 guitar on an adapter that calls itself
a PlayStation 3 pad plays as a guitar once its instrument is 5 Fret Guitar.

**Touch** (always on in the Android app; Enhanced Settings > Enable Touch
Controls elsewhere): tap to click; in a song a finger on a lane plays it;
on-screen Up / Down / START buttons (menus) and a pause button (songs) show
once you touch the screen; two-finger swipes move up / down (in a song: Star
Power / pause). Set the buttons, swipes and pause corner in GAMEPADS >
Player 1 > Customize inputs. For a bug report, attach
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
tools/walks.sh engine/build/linux/gd01 "path/to/Guitar Hero (USA).iso"   # the headless menu walks
```

Generic MIDI on Linux needs the ALSA headers to build (`alsa-lib-devel` on
Fedora, `libasound2-dev` on Debian / Ubuntu); without them the game builds
and shows MIDI as unavailable.

## Folder

| Path | What |
|---|---|
| `engine/` | The program (C++20, SDL3, OpenGL; Vulkan for the AI upscalers) |
| `tools/` | Python readers for the game's formats, Ghidra scripts, `fetch_dlss.sh` |
| `docs/` | Research notes: `FINDINGS.md` (rules and formats), one note per system (the backlog is `gd-backlog.md` at the repository root) |
| `local_build.sh` | Builds the release downloads into `local_builds/` (`build_commands/`: double-click versions) |
| `CHANGELOG.md` | What changed in each release |

## Licence

PolyForm Noncommercial 1.0.0 plus the project's additional terms (see
[LICENSE](../LICENSE) and [LICENSE-ADDITIONAL-TERMS.md](../LICENSE-ADDITIONAL-TERMS.md)):
free to play, study and fork; no selling; credit the original project; publish
your changes. Guitar Hero is a trademark of its owner; this project is not
affiliated with or endorsed by it.
