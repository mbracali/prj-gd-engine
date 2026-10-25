# gd01 (Guitar Dream Engine, game 01)

Based on Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine

## Quick start

1. You need your own Guitar Hero (PS2) disc image (`.iso`). No game data is included.
2. Start `gd01` (`gd01.exe` on Windows, `gd01.app` on macOS: right-click > Open
   the first time; "GD Engine 01" on Android).
3. Click the window (or press Enter, or tap on Android) and pick your `.iso`.
   That's it: the game remembers it.

Linux: the first start adds "GD Engine 01", with its icon, to your
application menu (pointing at this folder; set GD01_NO_DESKTOP_ENTRY=1 to skip).

A from-scratch reimplementation of Guitar Hero (PS2, 2005) for Linux, macOS
and Windows. It plays the songs from **your own copy** of the game: no game
data is included. The gameplay rules (hit window, HOPOs, scoring, Star Power,
rock meter) were recovered from the original executable; see
`gd-game-01/docs/FINDINGS.md` in the repository
(https://github.com/mbracali/prj-gd-engine).

Songs play on the original highway in front of the song's venue, both drawn
from your game data: venue geometry, the lighting that follows the crowd
and the song's verse/chorus/solo sections, animated props, and the game's
camera shots, and the band (guitarist, singer, bassist, drummer) animated
with the original clips. The crowd isn't drawn yet.

## Running

Start `gd01` and drop your disc image (`.iso`) or extracted game folder (the
one containing `GEN/MAIN.HDR`) onto the window. The path is remembered.
Or pass it on the command line:

    gd01 "Guitar Hero (USA).iso"
    gd01 --song smokeonthewater --difficulty 3 path/to/game

Options: `--song <id>`, `--difficulty 0-3`, `--av-offset <ms>`, `--no-fail`,
`--autoplay`, `--no-venue`, `--selftest`, `--shadertest` (compiles every shader
on this graphics driver and reports any that fail; no game data needed).

## Controls

| Action | Keyboard | Guitar controller | Gamepad |
|---|---|---|---|
| Frets green..orange | 1–5 or F1–F5 | frets | L2, L1, R1, R2, A/Cross |
| Strum | Up / Down | strum bar | (pressing a fret strums) |
| Star Power | Space / Right Shift | Select / Back | Select / Back |
| Whammy | hold W / Right Ctrl | whammy bar | — |
| Pause / back | Esc | Start / Back | Start / B |

Guitars are detected as Xbox-style guitars, or by "guitar" in the controller
name for PS2 guitars on USB adapters. The gamepad layout is the PS2 game's own
joypad mode.

## Building

Linux and Windows (from Fedora): `./build.sh` writes `dist/`. macOS: build on a
Mac with `brew install sdl3 ninja` and
`cmake -S . -B build -G Ninja && cmake --build build`, or use the GitHub
Actions workflow, which builds all three platforms.

### Android

`./build_android.sh` writes `dist/android/gd01.apk` (arm64-v8a and x86_64,
Android 7.0+, OpenGL ES 3.0). `./build.sh` also runs it when an Android SDK is
found at `$ANDROID_HOME` (default `~/Android/Sdk`). One-time setup, no root:

1. A JDK 17 or 21 (`JAVA_HOME`; `~/.local/opt/jdk-21*` is found automatically).
2. The Android command-line tools from
   https://developer.android.com/studio#command-tools unpacked as
   `~/Android/Sdk/cmdline-tools/latest`, then
   `sdkmanager "ndk;28.2.13676358" "cmake;4.1.2" "platforms;android-36" "build-tools;36.1.0" "platform-tools"`.
3. The first build downloads Gradle and the SDL3 source release matching the
   desktop SDL3 (into `build/android/`).

The APK is signed with the Android debug key (created if missing); set
`GD01_RELEASE_KEYSTORE` (+ `_PASSWORD`, `GD01_RELEASE_KEY_ALIAS`) to sign with
your own. `ABIS=arm64-v8a ./build_android.sh` builds one ABI only.

Install with `adb install -r dist/android/gd01.apk` (or copy the APK to the
phone and open it, allowing installs from that app). On the first start, tap
the screen and pick your disc image with the file picker: it is copied into the
app's folder (`Android/data/io.github.mbracali.gd01/files/`, about 2.2 GB), or copy any
`.iso` there yourself (USB or `adb push`). Touch controls are on by default;
USB and Bluetooth controllers work as on the desktop, and the back button is
the game's Back. FSR 3.1 and DLSS 4 are desktop-only (FSR 1 and the other
effects work). On the desktop, `GD01_GLES=1` runs the same OpenGL ES renderer.

## Licence

PolyForm Noncommercial 1.0.0 plus the additional terms in
`LICENSE-ADDITIONAL-TERMS.md` (both next to this file in every download):
free to play, study and share; no selling; keep the credit to the original
project; publish any changes you distribute. The third-party licences are in
`licenses/`. Guitar Hero is a trademark of its owner; this project is not
affiliated with or endorsed by it.
