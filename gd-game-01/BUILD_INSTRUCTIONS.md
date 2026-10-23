# Build instructions

gd01 is a C++20 program built with CMake (3.24 or newer) and SDL3. The
source is in `engine/`. It contains no game data: to play, you need your own
copy of Guitar Hero (PS2), as a disc image (`Guitar Hero (USA).iso`) or an
extracted game folder (the one containing `GEN/MAIN.HDR`).

| Platform | Built on | Result |
|---|---|---|
| Linux x86_64 | Linux | `engine/dist/gd01-linux-x86_64/gd01` |
| Windows x64 | Linux (MinGW cross-compiler) or Windows (MSYS2) | `engine/dist/gd01-windows-x64/gd01.exe` + `SDL3.dll` |
| macOS (Apple Silicon or Intel) | a Mac | `gd01.app` |

---

## Linux

### Fedora (the reference setup)

```sh
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel
cd engine
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
```

The program is `engine/build/linux/gd01`.

`engine/build.sh` builds Linux and Windows in one go and copies both into
`engine/dist/` (see "Windows, cross-compiled from Fedora" below for the extra
packages).

### Ubuntu / Debian

You need a distribution with SDL3 packages (Ubuntu 25.04 or newer, Debian 13
or newer):

```sh
sudo apt install build-essential cmake ninja-build libsdl3-dev
cd engine
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
```

If your distribution has no SDL3 package yet, build SDL3 from source
(https://github.com/libsdl-org/SDL, release 3.2 or newer) and install it with
`cmake --install`, then build gd01 as above.

### Running on Linux

The binary uses the system SDL3 library, so the machine that runs it needs it
installed (`sudo dnf install SDL3` or `sudo apt install libsdl3-0`).

```sh
./gd01 "path/to/Guitar Hero (USA).iso"
```

Or start `./gd01` without arguments and drop the `.iso` (or the game folder)
onto the window. The path is remembered for the next start.

---

## Windows

### Cross-compiled from Fedora (how `build.sh` does it)

```sh
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel mingw64-gcc-c++ mingw64-SDL3
cd engine
./build.sh
```

The result is `engine/dist/gd01-windows-x64/`, which contains `gd01.exe`,
`SDL3.dll` and `README.md`. Copy the **whole folder** to the Windows PC;
`gd01.exe` needs `SDL3.dll` beside it.

### Built on Windows with MSYS2

1. Install MSYS2 (https://www.msys2.org) and open the **UCRT64** shell.
2. Install the toolchain and SDL3:
   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake \
       mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-sdl3
   ```
3. Build:
   ```sh
   cd engine
   cmake -S . -B build/windows -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build/windows
   ```
4. To run the exe outside the MSYS2 shell, copy the DLLs it needs from
   `C:\msys64\ucrt64\bin` next to `gd01.exe`: `SDL3.dll`, and if the exe
   asks for them, `libstdc++-6.dll`, `libgcc_s_seh-1.dll` and
   `libwinpthread-1.dll`.

Visual Studio also works: install SDL3's development package (or vcpkg
`sdl3`), point CMake at it with `-DSDL3_DIR=...`, and build the generated
solution.

### Running on Windows

Double-click `gd01.exe` and drop your `.iso` onto the window, or run it from a
command prompt:

```bat
gd01.exe "C:\Games\Guitar Hero (USA).iso"
```

---

## macOS

macOS builds must be made on a Mac (Apple's SDK cannot be cross-compiled from
Linux).

1. Install the Xcode command-line tools: `xcode-select --install`.
2. Install Homebrew (https://brew.sh), then the dependencies:
   ```sh
   brew install cmake ninja sdl3
   ```
3. Build:
   ```sh
   cd engine
   cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build/macos
   ```
   The result is the app bundle `engine/build/macos/gd01.app`.
4. Run it from the terminal (so you can pass the disc image):
   ```sh
   ./build/macos/gd01.app/Contents/MacOS/gd01 "path/to/Guitar Hero (USA).iso"
   ```
   Or open the app and drop the `.iso` onto its window.

### A self-contained app (to copy to another Mac)

The app above uses Homebrew's SDL3. To bundle SDL3 inside the app:

```sh
brew install dylibbundler
app=build/macos/gd01.app
dylibbundler -od -b -x "$app/Contents/MacOS/gd01" -d "$app/Contents/Frameworks/" \
    -p @executable_path/../Frameworks/
codesign --force --deep --sign - "$app"
```

The app is not notarised: on another Mac, open it the first time with
right-click > Open (or allow it in System Settings > Privacy & Security).

---

## All three platforms with GitHub Actions

`.github/workflows/build.yml` builds Linux and Windows (in a Fedora container)
and a bundled macOS app on every push. To use it:

1. Put the project in a GitHub repository. Do **not** commit game data:
   `original_rom/`, `extracted/`, `extracted_dta/`, `extracted_png/` and
   anything else copied from the disc stay out (add them to `.gitignore`).
2. Push. The workflow's run page offers three downloads: `gd01-linux-x86_64`,
   `gd01-windows-x64` and `gd01-macos-arm64`.

---

## Checking a build

```sh
gd01 --selftest "path/to/Guitar Hero (USA).iso"
```

It plays every chart automatically through the game's rules and must end with
`all charts fully hit`.

## Command-line options

| Option | Meaning |
|---|---|
| `--song <id>` | start a song directly, e.g. `--song smokeonthewater` |
| `--difficulty 0-3` | Easy, Medium, Hard, Expert (with `--song`) |
| `--av-offset <ms>` | audio / video latency compensation |
| `--no-fail` | the rock meter cannot fail you |
| `--autoplay` | the game plays the notes |
| `--no-venue` | a plain background instead of the venue |
| `--kb2p` | two players on one keyboard (player 2: H J K L ; frets, N / M strum, B Star Power, O Start) |
| `--selftest` | the automatic test above |

## Controls

| Action | Keyboard | Guitar controller | Gamepad |
|---|---|---|---|
| Frets green..orange | 1–5 or F1–F5 | frets | L2, L1, R1, R2, A / Cross |
| Strum, menu up / down | Up / Down | strum bar | (pressing a fret strums) |
| Select | Enter | green fret | A / Cross |
| Back | Esc or Backspace (Esc three times on the main menu quits) | red fret | B / Circle |
| Star Power | Space or Right Shift | Select / Back | Select / Back |
| Whammy | hold W or Right Ctrl | whammy bar | — |
| Pause | P or Tab | Start | Start |

The mouse works in the menus (hover to highlight, click to select, click
CONTINUE / BACK). It can be switched off in Options > Enhanced Settings.
