# Guitar Dream Universal Launcher (gdul)

One program that lists every Guitar Dream Engine game, recognises your own
disc images and plays the games inside its own window. C++17, SDL3 and
OpenGL 3.3, the same stack as the games, so the launcher and every game share
one process and one window. Builds: Linux x86-64 and Windows x86-64 (MinGW-w64);
macOS and Android are planned (see "Other systems" below).

Version: `launcher-info.yaml` (`launcher_version`, SemVer, from 0.1.0).

```
gd-engine-launcher/
  games.yaml            the games, in table order (paths of their game-info.yaml),
                        plus extra_iso_signatures for games that cannot carry them
  launcher-info.yaml    the launcher's version
  CMakeLists.txt        the launcher program
  src/                  its code (below)
  sdk/                  the game module ABI: gd_host.h, gd_module_shim.h/.cpp
  modules/gd01/         gd-game-01 built as a module, from here (gd-game-01 is frozen)
  res/                  icon, backdrop and fonts (built in); tools/make_icons.py makes the images
  third_party/stb/      stb_image, stb_truetype (public domain / MIT)
  manual/               the user manual shipped in every build
  package/              the README files of a build (top level, original_isos/)
  scripts/build.sh      builds everything into local_builds/
  local_builds/         builds, one folder per version and system (git-ignored)
  logs/                 build logs (git-ignored)
  agents/  tools/
```

## Using it

The table shows every game of `games.yaml`: `gd-game-NN/<nick name>` on the
left, its system, year, version and disc; on the right how complete it is and
three buttons:

- **Play**: when the game is ready to play (`ready_to_play: true`), the
  launcher has its module, and a disc image is known for it.
- **Load ISO** (or **Change ISO**): the system's file dialog; the chosen disc
  is checked and saved for that game.
- **Explore models**: a placeholder for now ("coming soon").

A row is lit (green) when its game is ready to play and has a disc. Disc
images are found by themselves in `original_isos/` next to the program and,
for a build inside the repository (`local_builds/`), in the repository's
`original_isos/`. A disc chosen with Load ISO or dropped on the window is
saved in `config.txt` in the launcher's save folder and wins over a found one.

**Drop a disc image anywhere** on the window: the launcher reads it,
recognises the game by its content (never by the file name), and offers to
save it for that game and play. An unknown file gets "We can't rock this!
Sorry...".

Controls (keyboard, mouse, gamepad or guitar; the colours are the frets):

| | Keyboard | Gamepad | Guitar |
|---|---|---|---|
| Move | arrows / WASD, Page Up/Down, Home/End | D-pad, left stick | strum up/down |
| Select (green) | Enter, Space | A, Start | green fret |
| Back (red) | Esc, Backspace, right click | B, Back | red fret |
| Load ISO (yellow) | L | Y | yellow fret |
| Explore models (blue) | E | X | blue fret |

The mouse points and clicks; the wheel scrolls. Quitting a game returns to
the list in the same window (closing the window while a game runs also
returns to the list).

## The action log

Every launcher action (start, disc dialog, drop and what it was identified
as, disc saved, play, back from a game with its exit code and time,
explore, quit) appends a line to **`gdul.log` in the launcher's save folder**:

```
2026-10-27T14:03:12-03:00 marcelo play gd-game-02 (Guitar Hero II 0.1.1) with /media/discs/gh2.iso
```

ISO 8601 local time with its offset, the OS user name, the action. The save
folder is used rather than the program's folder because that one may be
read-only (Program Files, a mounted image, Android). It is SDL's
preference folder for `gd-engine/gdul`:

- Linux: `~/.local/share/gd-engine/gdul/`
- Windows: `%APPDATA%\gd-engine\gdul\`
- macOS: `~/Library/Application Support/gd-engine/gdul/`

The same folder holds `config.txt` (`iso.<game id>=<path>`) and `logs/<game
id>.log`, the game's own log lines of its last run. Moving between rows is not
logged.

## Architecture

```
main.cpp        SDL_main: runs the Launcher
launcher.*      the window, the event loop, input (keys, mouse, gamepads,
                guitars, drops), dialogs, the drawing of the list
gfx.*           2D drawing on OpenGL 3.3: one batched shader for images, text
                (stb_truetype atlases per size) and anti-aliased rounded
                rectangles, glows and rings (signed distances)
catalog.*       games.yaml + every game-info.yaml, the discs found or saved, config.txt
iso_probe.*     disc identification: ISO 9660 (PVD, root folder, SYSTEM.CNF
                BOOT2) and XDVDFS (at the XGD partition offsets, the root tree,
                default.xex's title ID)
module_host.*   loads a game module and runs it in the window (below)
action_log.*    the action log
platform.*      folders, user name, time, shared libraries per system
yaml.*          the small YAML subset the files use
```

The window is created once, with the games' OpenGL setup (3.3 core, forward
compatible, 24-bit depth, double buffered, high pixel density), and never
closed until the launcher quits.

### Games as modules

Each playable game is built as a shared library, `gd-engine-games/<id>/<id>.so`
(Linux) or `<id>.dll` (Windows), which the launcher loads into its own process.
The ABI is plain C (`sdk/gd_host.h`):

```c
extern "C" int gd_game_run(const GdHost* host);  // exported by the module

typedef struct GdHost {
    uint32_t abi_version, size;      // GD_HOST_ABI_VERSION (1), sizeof(GdHost)
    void* window;                    // the launcher's SDL_Window*
    void* gl_context;                // the launcher's SDL_GLContext
    int argc; const char* const* argv;   // "gdNN", [GDUL_GAME_ARGS...], the disc image
    const char *launcher_version, *game_id, *game_dir, *save_dir;
    void (*log)(void* user, const char* line); void* log_user;   // the action log
} GdHost;
```

Both the launcher and the modules link the one SDL3 shared library next to the
launcher (`libSDL3.so.0` / `SDL3.dll`), so they share SDL's state. A run
(`module_host.cpp`):

1. **Load** the library (`dlopen` RTLD_NOW|RTLD_LOCAL; Windows
   `LoadLibraryEx` with the module's folder first, for its own DLLs such as
   FFmpeg's) and find `gd_game_run`.
2. **Hand over**: the launcher closes its gamepads, stops text input and
   relative mouse mode, empties the event queue, and announces every connected
   controller again (`SDL_EVENT_JOYSTICK_ADDED` / `SDL_EVENT_GAMEPAD_ADDED`,
   which SDL sends only once per process; the games open their controllers on
   them) plus the window's pixel size. The game's `SDL_Log` lines go to the
   console and to `logs/<id>.log` in the save folder.
3. **Run**: `gd_game_run` plays the game in the launcher's window and returns
   when the player quits.
4. **Take back**: the launcher's GL context current again, vsync, cursor,
   title, icon and app id restored, events emptied, gamepads opened again.
5. **Unload** the library, and check that it really left the process (so all
   of the game's globals start fresh next time; logged as "module unloaded").
   GCC's `-fno-gnu-unique` keeps unique symbols from pinning it on Linux.

SDL subsystems: the launcher keeps video, events and gamepad running; a game
initialises what it needs (audio, for example) and quits only that, through
the shims below. Audio devices are the game's own: it opens and closes them.

**The shims** (`sdk/gd_module_shim.h`, force-included into every C++ file of
a module, with `gd_module_shim.cpp` compiled in) let a game written as a
standalone SDL program run as a module unchanged:

| The game calls | In a module it does |
|---|---|
| `SDL_Init(flags)` | `SDL_InitSubSystem(flags)`, remembered |
| `SDL_Quit()` | `SDL_QuitSubSystem` of each of those, never all of SDL |
| `SDL_CreateWindow(...)` | the launcher's window (its title set); a hidden window stays real |
| `SDL_DestroyWindow(launcher's window)` | nothing |
| `SDL_GL_CreateContext` / `SDL_GL_DestroyContext` | real contexts on the launcher's window, tracked |

A game gets its own OpenGL context on the launcher's window rather than the
launcher's context: whatever it allocates is freed with its context, and no
GL state leaks either way. Anything left behind (a window, a context, a
subsystem) is cleaned up by `gdshim_end()` when `gd_game_run` returns.

**gd-game-02** has its own entry point: `src/platform/module_entry.cpp`
(`gd_game_run` -> `gd02Main`, the former `main`), and its CMakeLists.txt option
`GD02_MODULE=ON` builds `gd-game-02.so/.dll` from the same sources and settings
as the `gd02` program, which is unchanged.

**gd-game-01** is finished and frozen, so `modules/gd01/CMakeLists.txt` builds
its sources as they are, from here: the source list and version are read from
its CMakeLists.txt, `src/main.cpp` is compiled with `-Dmain=gd01_main`, the
shims are force-included, and `modules/gd01/gd01_module_entry.cpp` provides
`gd_game_run`. Before calling it, the entry sets `GD01_GPU=system` (gd01 would
otherwise choose a hybrid laptop's GPU through driver variables and, if that
failed, restart its process with `execv`: the launcher's process) and
`GD01_NO_DESKTOP_ENTRY=1` (likewise `GD02_NO_DESKTOP_ENTRY` for gd02). NVIDIA's
DLSS model library goes to the launcher's root, where gd01's NGX looks for it.

## Disc signatures

A game's `game-info.yaml` says how to recognise its disc:

```yaml
iso_signatures:
  - format: xdvdfs            # iso9660 (PlayStation 2, ...) or xdvdfs (Xbox, Xbox 360)
    xex_title_id: "415607E7"  # Xbox 360: default.xex's title ID
  - format: iso9660
    ps2_boot: "SLUS_212.24"   # PlayStation 2: SYSTEM.CNF's BOOT2 program
    volume_id: "..."          # optional: the ISO 9660 volume name
    files: [SYSTEM.CNF]       # optional: names that must be in the root folder
```

Every field given must match; a disc matching any signature belongs to the
game. `games.yaml`'s `extra_iso_signatures` (the same fields plus `game:`)
holds the signatures of games whose folder must not change (gd-game-01).

## Building

```
gd-engine-launcher/scripts/build.sh [linux] [windows] [all] [--force]
```

Linux: cmake, ninja, a C++20 compiler and SDL3's development files (the
gd-game-02 module also builds FFmpeg, as gd-game-02 does). Windows:
cross-compiled with MinGW-w64 (Fedora: `dnf install mingw64-gcc-c++
mingw64-SDL3`). The result:

```
local_builds/gdul-0.1.0-linux-x86_64/
  Guitar Dream Universal Launcher          the program (.exe on Windows)
  Guitar Dream Universal Launcher.png      its icon (.ico on Windows)
  libSDL3.so.0 (SDL3.dll), libnvidia-ngx-dlss.so.*   platform libraries
  LICENSE  LICENSE-ADDITIONAL-TERMS.md  README.md
  licenses/          every licence: SDL3, stb, the fonts (OFL), and each
                     game's (FFmpeg LGPL + notice, stb_vorbis, FidelityFX, volk,
                     Vulkan headers, NVIDIA RTX SDK)
  manual/            the user manual
  original_isos/     README.md: put your disc images here
  gd-engine-games/
    games.yaml  launcher-info.yaml  manifest.json
    gd-game-NN/game-info.yaml       every game (the list)
    gd-game-01/gd-game-01.so  licenses/  README.md
    gd-game-02/gd-game-02.so  libavcodec.so.61 ...  licenses/  README.md
```

**The manifest** (`gd-engine-games/manifest.json`) records, per module, the
game's `guitar_dream_version` it was built from, the module file, when it was
built and whether it was reused:

```json
{ "launcher_version": "0.1.0", "os": "linux", "arch": "x86_64",
  "assembled_at": "2026-10-27T13:13:51Z", "source_commit": "eb65d56",
  "games": { "gd-game-02": { "guitar_dream_version": "0.1.1",
             "module": "gd-game-02.so", "built_at": "...", "reused": false } } }
```

A module is rebuilt only when the game's `guitar_dream_version` differs from
the one in the previous build's manifest (this version's folder, else the
newest earlier one for that system); otherwise its folder is copied from that
build. `--force` rebuilds them all. The launcher itself is always compiled
(CMake skips what is up to date) and must have 0 compiler warnings.
`scripts/build.sh` writes `logs/build-<os>.log` and one log per step
(`logs/<os>-launcher.log`, `logs/<os>-gd-game-NN.log`); intermediate files go
to `build/<os>/` (git-ignored).

Each game folder keeps its own `local_build.sh` and `local_builds/` for its
standalone program.

A new game needs a recipe in `scripts/build.sh` (`build_module_NN`): see
`docs/adding-a-game.md`.

## Development aids

| Variable | Effect |
|---|---|
| `GDUL_SCRIPT="<ms>:<cmd>[=<arg>];..."` | commands at those times since start: `up down left right green red yellow blue`, `drop=<file>` (a real drop event), `play=<game id>`, `shot=<file.bmp>`, `drag=1\|0` (the drop overlay), `toast=<text>`, `quit` |
| `GDUL_WINDOW=<w>x<h>` | the starting window size |
| `GDUL_GAME_QUIT_AFTER=<ms>` | quits each game that long after it starts |
| `GDUL_GAME_ARGS="..."` | more arguments for the game, before the disc |
| `GDUL_VIRTUAL_PAD=<vendor>:<product>` | plugs in a virtual gamepad (hex, e.g. `045e:028e`) |
| `GDUL_GAMES_YAML=<file>` | another games.yaml |

Headless: `SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy`. For example,
play gd-game-02 from the list and come back:

```
GDUL_SCRIPT="1000:play=gd-game-02;1100:shot=/tmp/back.bmp;1200:quit" \
GD02_SKIP_BOOT=1 GD02_SCREENSHOT=/tmp/gd02.bmp GD02_SCREENSHOT_AT=12000 \
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
"local_builds/gdul-0.1.0-linux-x86_64/Guitar Dream Universal Launcher"
```

## Other systems (planned)

- **macOS**: the same program as an .app; modules as `.dylib` inside it
  (`platform.cpp` already maps the names), signed together.
- **Android**: one APK with the launcher as SDL's activity and the games'
  `libgd-game-NN.so` in it, loaded with `dlopen` the same way; the window is
  the activity's surface (OpenGL ES 3.0: `gfx.cpp`'s shader is close to ES
  already); discs come from the storage access framework.
- Release workflows that publish these builds.
