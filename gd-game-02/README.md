# Guitar Dream Engine - Game 2
id = `gd-game-02`

**Generals about Guitar Dream Engine, approach on re implementations**:
- Don't contain any original code.
- Don't contain any original asset.
- Don't distribute any original asset.
- If you want to play, buy the original media.
(Support the industry)


**This folder contains the re implementation of**:
- **Game**: Guitar Hero 2
- **Version**: Xbox 360
- **Launched**: Apr/2007 (Xbox 360; the PS2 original came out Nov/2006)
- **Musics**: --
- **Venues**: --
- **Chars**: --


**In-game instrument support**:
- [X] Guitar
- [X] Bass (partial)
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
    - [X] Carrer
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


**Default settings**: each download starts with Enhanced Options tuned
for its system, the same table as gd-game-01's (change them in Options >
Enhanced Options; your changes are kept). "Apply Steam Deck Settings" sets
the Steam Deck column on any PC; "Remove All Effects" turns every effect
off.

| Setting | Linux | Steam Deck | Windows | macOS | Android |
|---|---|---|---|---|---|
| Screen Mode | Windowed | Full Screen | Windowed | Windowed | Full Screen |
| In-Game Max FPS | 120 | 60 | 120 | 120 | 60 |
| Show FPS Counter | on | off | on | on | off |
| Internal Resolution | 1080p | 800p | 1440p | 1440p | 900p |
| Sharper Text and Sprites | on | on | on | on | off |
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
| Touch Controls | off | off | off | off | on |


**Running** (`engine/README.md` has the details, it ships in every download):
- You need your own Guitar Hero II (Xbox 360) disc image (`.iso`). No game data is included.
- Start `gd02` and click the window (or press Enter, or tap on Android) to pick the `.iso`, or drop it onto the window. It is remembered (`config.txt` in the save folder `gd-engine/gd02/`); an `.iso` next to the program is found by itself.
- Command line: `gd02 [--song <id>] [--difficulty 0-3] [--autoplay] [--no-fail] [disc.iso | game folder]`.
- `gd02 --selftest <disc.iso>` checks the data, auto-plays every chart and checks the rules without a window.
- Bug reports: on Windows the log is `gd02.log` in the save folder (with a crash's address); elsewhere it goes to the terminal, or to a file with `GD02_LOG=<file>`. `GD02_PERF=1` adds a frame-time breakdown every 2 s.
- Developers: keep the disc image in the repo root's `original_isos/` (git-ignored), e.g. `engine/build/linux/gd02 "../original_isos/Guitar Hero II (USA) (Rev1) (X360).iso" --selftest` from `gd-game-02/`.


**Building** (the same scripts the release workflow runs):
- `gd-game-02/local_build.sh [linux|steamdeck|windows|macos|android|all] [--native] [--force]` builds the downloads into `gd-game-02/local_builds/` (git-ignored): a ready-to-run folder and the release file (`gd02-<version>-<platform>.<tar.gz|zip|apk>`) per build, a log per build in `local_builds/logs/`, and a summary. Builds whose sources did not change are reported "up to date". Without a build name: the one for the system you are on.
- `gd-game-02/build_commands/`: the same, by double-click (`build-linux.sh`, `build-steamdeck.sh`, `build-windows.sh` / `.bat`, `build-macos.command`, `build-android.sh`, `build-all.sh`).
- Linux: an ubuntu:22.04 container (podman or docker), SDL3 built from source and shipped next to the program; `--native` uses this machine's compiler and SDL3. Steam Deck: Valve's Steam Runtime 3 "sniper" SDK container. Windows: MinGW-w64 (Fedora `mingw64-gcc-c++ mingw64-SDL3`, or an MSYS2 UCRT64 shell). macOS: on a Mac with `brew install cmake sdl3 ninja dylibbundler`. Android: the Android SDK, NDK and a JDK (`engine/README.md`); signed with `~/.config/gd-engine/gd02-release.jks` when it is there, else the debug key.
- `engine/build.sh`: the quick developer build (Linux with the system SDL3, Windows, Android when its SDK is found) into `engine/dist/`.
- The version is `project(gd02 VERSION X.Y.Z)` in `engine/CMakeLists.txt` (the Android `versionCode` is X*10000 + Y*100 + Z). Keep `guitar_dream_version` in `game-info.yaml` the same; gd02 ships inside the launcher release (docs/release-process.md). `gd-game-02-build.yml` can be started by hand for test builds.
- Icons: `python3 gd-game-02/tools/make_icons.py` (needs Pillow) remakes them from the badge `res/icon_full.jpg`.


**Folder layout**:
- `game-info.yaml`: id, names, launch date, platforms, version, playable flag and completeness; read by the Guitar Dream Universal Launcher (`gd-engine-launcher/games.yaml`).
- `engine/`: the C++ source. `docs/`: formats and findings. `tools/`: Python decoders and Ghidra scripts. `build_commands/` and `local_build.sh`: builds.
- `agents/`: context notes for AI coding agents (`agents/scratch/` is local only, git-ignored).
- Local only, git-ignored: `extracted/` (your own rip of the disc), `ghidra/` (the Ghidra project and its `analysis/` export: original code, never commit), `local_builds/`.
