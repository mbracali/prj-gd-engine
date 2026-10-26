# gd02 (Guitar Dream Engine, game 02)

Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine

## Quick start

1. You need your own Guitar Hero II (Xbox 360) disc image (`.iso`). No game
   data is included.
2. Start `gd02` (`gd02.exe` on Windows, `gd02.app` on macOS: right-click > Open
   the first time; "GD Engine 02" on Android).
3. Click the window (or press Enter, or tap on Android) and pick your `.iso`,
   or drop it onto the window. That's it: the game remembers it. An `.iso`
   next to the program is found by itself.

Linux: the first start adds "GD Engine 02", with its icon, to your
application menu (pointing at this folder; set GD02_NO_DESKTOP_ENTRY=1 to skip).

A from-scratch reimplementation of Guitar Hero II (Xbox 360, 2007) for Linux,
Steam Deck, Windows, macOS and Android. It plays the songs from **your own
copy** of the game: no game data is included. This is an early build: a song
list and a debug highway that play every song's chart against its audio with
the original game's rules (see `gd-game-02/docs/FINDINGS.md` in the
repository). The game's own menus, highway art and venues come later.

## Running

    gd02                                  the first screen, or the disc image picked last time
    gd02 "Guitar Hero II (USA).iso"
    gd02 --song yyz --difficulty 3 path/to/game-folder

The game folder is the disc's extracted files (the one containing
`gen/main.hdr`). Options: `--song <id>`, `--difficulty 0-3`, `--autoplay`,
`--no-fail`, `--selftest` (checks the data and auto-plays every chart without
a window), `--help`.

## Controls

| Action | Keyboard |
|---|---|
| Frets green..orange | 1–5 |
| Strum | Up / Down / Enter |
| Star Power | Space |
| Whammy | hold W |
| Back / quit | Esc |

In the song list: Up / Down pick a song, Left / Right the difficulty, Enter
plays. Guitar controllers and gamepads come later.

## Building

From the repository: `gd-game-02/local_build.sh` (or the double-click scripts
in `gd-game-02/build_commands/`) builds exactly what a release publishes, into
`gd-game-02/local_builds/`. `./build.sh` here is the quick developer build
(Linux with the system SDL3, Windows with MinGW-w64, Android when its SDK is
found) into `dist/`.

| Build | Script | Needs |
|---|---|---|
| Linux | `build_linux_portable.sh` | an ubuntu:22.04 container (podman or docker); SDL3 is built from source and ships next to the program |
| Steam Deck | `build_linux_steamdeck.sh` | Valve's Steam Runtime 3 "sniper" SDK container |
| Windows | `build_windows.sh` | MinGW-w64: Fedora `mingw64-gcc-c++ mingw64-SDL3`, or an MSYS2 UCRT64 shell |
| macOS | `build_macos.sh` | a Mac with `brew install cmake sdl3 ninja dylibbundler` |
| Android | `build_android.sh` | see below |

`package.sh <platform> <version> <out dir>` turns a `dist/` folder into the
download (`gd02-<version>-<platform>.<zip|tar.gz|apk>`).

### Android

`./build_android.sh` writes `dist/android/gd02.apk` (arm64-v8a and x86_64,
Android 7.0+). One-time setup, no root:

1. A JDK 17 or 21 (`JAVA_HOME`; `~/.local/opt/jdk-21*` is found automatically).
2. The Android command-line tools from
   https://developer.android.com/studio#command-tools unpacked as
   `~/Android/Sdk/cmdline-tools/latest`, then
   `sdkmanager "ndk;28.2.13676358" "cmake;4.1.2" "platforms;android-36" "build-tools;36.1.0" "platform-tools"`.
3. The first build downloads Gradle and the SDL3 source release matching the
   desktop SDL3 (into `build/android/`).

The APK is signed with the Android debug key (created if missing); set
`GD02_RELEASE_KEYSTORE` (+ `_PASSWORD`, `GD02_RELEASE_KEY_ALIAS`) to sign with
your own (`local_build.sh` uses `~/.config/gd-engine/gd02-release.jks` when it
is there). `ABIS=arm64-v8a ./build_android.sh` builds one ABI only.

Install with `adb install -r dist/android/gd02.apk`. On the first start, tap
the screen and pick your disc image: it is copied into the app's folder
(`Android/data/io.github.mbracali.gd02/files/`, about 8 GB), or copy any `.iso`
there yourself (USB or `adb push`).

## Licence

PolyForm Noncommercial 1.0.0 plus the additional terms in
`LICENSE-ADDITIONAL-TERMS.md` (both next to this file in every download):
free to play, study and share; no selling; keep the credit to the original
project; publish any changes you distribute. The third-party licences are in
`licenses/`. Guitar Hero is a trademark of its owner; this project is not
affiliated with or endorsed by it.
