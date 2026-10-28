# Guitar Dream Universal Launcher (gdul)

One program that lists every Guitar Dream Engine game, recognises your own
disc images and plays the games inside its own window. C++17, SDL3 and
OpenGL 3.3, the same stack as the games, so the launcher and every game share
one process and one window. Builds: Linux x86-64, Windows x86-64 (MinGW-w64),
Android arm64 (one APK, OpenGL ES 3.0) and macOS arm64 (an .app, not yet tried
on a Mac); see "Other systems" below.

Version: `launcher-info.yaml` (`launcher_version`, SemVer, from 0.1.0); the
changes of each version: `CHANGELOG.md`.

```
gd-engine-launcher/
  games.yaml            the games, in table order (paths of their game-info.yaml),
                        plus extra_iso_signatures for games that cannot carry them
  launcher-info.yaml    the launcher's version
  CMakeLists.txt        the launcher program
  src/                  its code (below)
  sdk/                  the game module ABI: gd_host.h, gd_module_shim.h/.cpp,
                        gd_explore_helpers.h
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
- **Explore models**: the disc's 3D models, grouped and searchable, in a
  viewer that turns, zooms, pans and plays their animations (below).

A row is lit (green) when its game is ready to play and has a disc. Disc
images are found by themselves in these `original_isos/` folders:

- next to the program (macOS: next to the `.app`; and inside it,
  `Contents/Resources/original_isos/`);
- macOS, an app run from where a download put it (App Translocation, below):
  next to its real place, which `Security.framework` tells
  (`SecTranslocateCreateOriginalPathForURL`, through `dlsym`);
- in the launcher's save folder (always writable: Program Files, a read-only
  image, a translocated app);
- for a build inside the repository (`local_builds/`), the repository's.

The header says how many were found; its "folders" / "where we looked" link
lists each folder with what it had (or why it could not be read), with Open
folder and Look again. Each folder is also logged at start. A disc chosen
with Load ISO or dropped on the window is saved in `config.txt` in the
launcher's save folder and wins over a found one.

The header's **OFFICIAL SITE** pill opens https://guitardreamengine.com
(`SDL_OpenURL`).

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

The mouse points and clicks every button; the wheel scrolls. (The list has no
legend of these keys: its buttons say what they do.)

**Menu sounds**: Guitar Hero's own (move, select, play, back, error), read at
run time from the player's Guitar Hero disc by the gd-game-01 module
(`gd_game_ui_sounds`, below) when that disc is known. Never shipped: they
live in memory only. Without that disc, the launcher's own built-in sounds
(`ui_sounds.cpp`: plucked strings and a power-chord sting, synthesized at
start, nothing taken from a game), at the same loudness. The launcher's audio
device is released before a game runs and opened again after. Quitting a
game returns to the list in the same window (closing the window while a game
runs also returns to the list). The launcher is always a window: after a game
that went full screen, maximized the window or hid the cursor, it leaves full
screen (waiting for Windows and macOS to finish) and puts back the size and
place it had.

## Explore models

The screen is the launcher's; the models are the game's. The launcher draws
the list (groups, a search box that matches every word of the name and the
archive path), the help bar and the model's card, and drives an orbit camera
(yaw, pitch, zoom, pan, eased; a slow turntable until you turn it yourself);
the game module lists the disc's models and draws the selected one with its
own loaders and renderer through the optional `gd_game_explore` entry point
(below), so the launcher knows no game format.

| Game | Models | How they are drawn |
|---|---|---|
| gd-game-02 | characters (24), crowd, guitars and basses, drum kits, venues (geometry, face-off variants), venue props | characters as `chr::Character` + `BandView` with their main driver's clips (idle first) and the venue outfits (guitar_sg, the arena kit); the rest as scenes through GH2's materials and standard shader, lit by their own Environ or a key light; guitars stood up |
| gd-game-01 | guitarists, band (band_chars.dta), crowd, guitars, venues | guitarists and band as gd01's `Character` with every clip (idle first), the rest as `Scene` + `SceneRenderer` (the views no other view draws), a key light; guitars stood up |

The list is grouped by kind; a group's header opens and closes it (click it,
or green on it; left closes and right opens it; a search shows every match).
The footer's actions are buttons in their fret's colour: click them or press
the fret.

Controls: up/down or click choose (the model loads after a short pause, at
once with green / click), left/right or drag turn, wheel / triggers / `+` `-`
zoom, right or middle drag (shoulder + right stick) pan, yellow / `R` /
double-click reset, blue / `N` next animation, orange / `T` the turntable,
`/` or `F` search, red / Esc back (after clearing the search). Opening, each
model shown (with its load time) and closing are logged.

## The action log

Every launcher action (start, disc dialog, drop and what it was identified
as, disc saved, play, back from a game with its exit code and time,
explore: opened, each model shown, closed; quit) appends a line to **`gdul.log` in the launcher's save folder**:

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
module_host.*   loads a game module and runs it in the window, or opens its
                model explorer (below)
explorer.cpp    the Explore models screen (list, groups, search, viewer, camera)
ui_sounds.*     the menu sounds (a game's, from its disc, or built-in) on SDL's audio device
palette.h       the launcher's colours
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

**The model explorer** (optional) is a second export:

```c
extern "C" const GdExplorer* gd_game_explore(const GdHost* host);  // null: no explorer

typedef struct GdExplorer {
    uint32_t size; void* self;
    int (*item_count)(void* self);
    int (*item)(void* self, int index, GdExploreItem* out);       // group, name, detail
    int (*select)(void* self, int index, char* info, int size);   // 0: shown; info: a line or the error
    int (*clip_count)(void* self);  const char* (*clip_name)(void* self, int index);
    void (*draw)(void* self, const GdExploreView* view);          // the frame, not presented
    void (*close)(void* self);
} GdExplorer;
// GdExploreView: the viewer's rectangle (window pixels), yaw, pitch, zoom,
// pan_x/pan_y (model radii), seconds, clip, background colour.
```

While it is open the module keeps its own GL context on the launcher's
window and the shims make its `SDL_GL_SwapWindow` do nothing
(`gdshim_set_exploring`). Each frame the launcher calls `draw` (the module
makes its context current and draws the whole window with the model centred
in the viewer's rectangle), reads the viewer's rectangle of that context's
back buffer (`gfx::readBackBuffer`: `glReadPixels` of the default
framebuffer's back buffer, all other state left alone), makes its own
context current again, uploads it to a texture, clears, draws the backdrop,
that texture (rounded) and its panels, and presents. Only the launcher's
context ever presents, and every frame starts cleared: drawing over another
context's back buffer flickered on macOS, where each `NSOpenGLContext` has
its own drawable and a back buffer is undefined after a swap. The launcher keeps
the input (the module polls no events); `close` frees everything and the
library is unloaded as after a game. `module_host.cpp`'s `ExploreSession`
does the loading; the game's log goes to `logs/<id>-explore.log`. How to
write one: `docs/adding-a-game.md`.

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
| `SDL_GL_SwapWindow` | presents, except while the model explorer is open (the launcher presents) |

A game gets its own OpenGL context on the launcher's window rather than the
launcher's context: whatever it allocates is freed with its context, and no
GL state leaks either way. Anything left behind (a window, a context, a
subsystem) is cleaned up by `gdshim_end()` when `gd_game_run` returns.

**Menu sounds** (optional) are a third export, `gd_game_ui_sounds(host,
sound, user)`: the module reads the disc and gives each sound (`"move"`,
`"select"`, `"play"`, `"back"`, `"error"`; variations allowed) as stereo
float samples. The launcher asks, after its first frames, each game with a
module and a disc in `games.yaml` order until one gives some (gd-game-01:
`modules/gd01/gd01_module_sounds.cpp`, Guitar Hero's `sfx/metagame` bank
rendered through gd01's software SPU2: button_toggle, button_select,
button_play, button_back, button_error, as GH1's `ghui/sfx.dta` plays them),
keeps them (`ui_sounds.*`) and logs where they came from. `fetchUiSounds`
in `module_host.cpp` loads and unloads the module; its log lines go to
`logs/<id>-sounds.log`.

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
gd-engine-launcher/scripts/build.sh [linux] [windows] [macos] [android] [all] [--force] [--previous <folder>]
```

(macOS and Android: see [Other systems](#other-systems-macos-and-android).)

Linux: cmake, ninja, a C++20 compiler and SDL3's development files (the
gd-game-02 module also builds FFmpeg, as gd-game-02 does). Windows:
cross-compiled with MinGW-w64 (Fedora: `dnf install mingw64-gcc-c++
mingw64-SDL3`). The result:

```
local_builds/gdul-0.1.1-linux-x86_64/
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
{ "launcher_version": "0.1.1", "os": "linux", "arch": "x86_64",
  "assembled_at": "2026-10-27T13:13:51Z", "source_commit": "eb65d56",
  "games": { "gd-game-02": { "guitar_dream_version": "0.1.1",
             "module": "gd-game-02.so", "built_at": "...", "reused": false } } }
```

A module is rebuilt only when the game's `guitar_dream_version`, or the
launcher's module SDK (`sdk/` and `modules/gdNN/`, recorded as `inputs_hash`),
differs from the previous build's manifest (this version's folder, else the
newest earlier one for that system; or the folder given with `--previous` /
`GDUL_PREVIOUS_BUILD`, e.g. an unpacked release download or a released APK);
otherwise its folder is copied from that build. `--force` rebuilds them all. The launcher itself is always compiled
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
| `GDUL_SCRIPT="<ms>:<cmd>[=<arg>];..."` | commands at those times since start: `up down left right green red yellow blue`, `drop=<file>` (a real drop event), `play=<game id>`, `explore=<game id>`, `shot=<file.bmp>`, `drag=1\|0` (the drop overlay), `toast=<text>`, `folders` (the disc folders dialog), `quit`; in the explorer also `collapse=<group text>\|all`, `expand=<group text>\|all`, `pick=<text>` (the first model whose name or path has it, shown at once), `search=<text>`, `orbit=<yaw>,<pitch>` (degrees), `zoom=<factor>`, `pan=<x>,<y>`, `clip=next`, `reset`, `turntable=0\|1` |
| `GDUL_WINDOW=<w>x<h>` | the starting window size |
| `GDUL_GAME_QUIT_AFTER=<ms>` | quits each game that long after it starts |
| `GDUL_GAME_ARGS="..."` | more arguments for the game, before the disc |
| `GDUL_VIRTUAL_PAD=<vendor>:<product>` | plugs in a virtual gamepad (hex, e.g. `045e:028e`) |
| `GDUL_GAMES_YAML=<file>` | another games.yaml |
| `GDUL_SOUND_LOG=1` | logs each menu sound played (always on with `GDUL_SCRIPT`) |
| `GDUL_BUILTIN_SOUNDS=1` | the launcher's built-in menu sounds even with a Guitar Hero disc |

Headless: `SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy`. For example,
play gd-game-02 from the list and come back:

```
GDUL_SCRIPT="1000:play=gd-game-02;1100:shot=/tmp/back.bmp;1200:quit" \
GD02_SKIP_BOOT=1 GD02_SCREENSHOT=/tmp/gd02.bmp GD02_SCREENSHOT_AT=12000 \
SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy \
"local_builds/gdul-0.1.1-linux-x86_64/Guitar Dream Universal Launcher"
```

## Other systems: macOS and Android

### macOS

`scripts/build.sh macos`, on a Mac with Homebrew (`brew install bash cmake
ninja sdl3 dylibbundler`; build.sh needs bash 4 or newer). Apple silicon
(arm64), ad-hoc signed, not notarised (first start: right-click > Open):

```
local_builds/gdul-0.1.1-macos-arm64/
  Guitar Dream Universal Launcher.app/Contents/
    MacOS/Guitar Dream Universal Launcher
    Frameworks/libSDL3.0.dylib                  (dylibbundler)
    Resources/gdul.icns
    Resources/gd-engine-games/                  as on Linux, modules as .dylib:
      gd-game-02/gd-game-02.dylib  libavcodec.61.dylib ...
      gd-game-01/gd-game-01.dylib
  original_isos/  licenses/  manual/  LICENSE ...
```

`SDL_GetBasePath()` is the app's `Contents/Resources/`, so the launcher
finds `gd-engine-games/` there as on the other systems, and looks for discs
in the `original_isos/` folder next to the app (and inside it, and in the
save folder).

**App Translocation**: an app unpacked from a download keeps the quarantine
mark, and until it is moved with the Finder, macOS runs it from a read-only
copy at a random path (`/private/var/folders/.../AppTranslocation/...`), so
"next to the app" is not the download's folder. The launcher detects it,
asks macOS for the app's real place and looks next to that too, and, when
no disc is found, opens the folders dialog with what to do: move the app
with the Finder, or `xattr -dr com.apple.quarantine "<the .app>"`. macOS may
also ask for permission to read Downloads, Documents or Desktop (a folder
denied is listed as "cannot be read"). The modules' libraries are
rewritten to find FFmpeg next to them (`@loader_path`) and SDL3 in the app's
`Frameworks/` (`@executable_path/../Frameworks`, the launcher's own copy),
then everything is signed. Not yet tried on a real Mac. gd-game-01's module
needs a Vulkan loader (MoltenVK), as gd01's own macOS build does.

### Android

`scripts/build.sh android`: one APK, `gdul-<version>-android-arm64.apk`, in
`local_builds/gdul-<version>-android-arm64/` (with the `gd-engine-games/`
folder next to it, for module reuse). Needs a JDK 17/21 and the Android SDK
at `$ANDROID_HOME` (default `~/Android/Sdk`) with the packages listed in
`gd-game-02/engine/build_android.sh`; SDL3 is the same source release as
gd02's Android build. The Gradle project is `android/` (applicationId
`io.github.mbracali.gdul`; its icons come from `tools/make_icons.py`).

- **The launcher** is `libmain.so`, run by SDL's activity
  (`android/.../GdulActivity.java`), next to `libSDL3.so`. OpenGL ES 3.0:
  `gfx.cpp`'s shaders get a `#version 300 es` header (also with
  `-DGDUL_GLES` on desktops); GL functions are loaded through
  `SDL_GL_GetProcAddress` as everywhere.
- **The modules** are `lib/arm64-v8a/libgd-game-NN.so` in the APK (with
  FFmpeg's `libav*.so`), extracted at install time (`useLegacyPackaging`):
  the launcher opens `<native library folder>/libgd-game-NN.so` with
  `dlopen`, the folder being where its own `libmain.so` was loaded from.
  gd02's module is built by its CMakeLists.txt (`GD02_MODULE=ON`) with the
  NDK against the same SDL3 source, so it uses the launcher's `libSDL3.so`.
  gd-game-01 (Vulkan, frozen, with its own APK) has no Android module: it is
  listed only.
- **games.yaml, game-info.yaml, launcher-info.yaml, manifest.json** are APK
  assets under `gd-engine-games/` (SDL reads relative paths from the assets;
  `platform::baseDir()` is "" on Android). Licences and the manual are
  assets too.
- **Discs**: Load ISO opens Android's file picker (a `content://` URI). The
  launcher identifies it there (SDL reads such URIs), then copies it into
  `Android/data/io.github.mbracali.gdul/files/original_isos/<game id>.iso`,
  showing the progress, because the games open their disc with `fopen` (as
  gd02's own Android build does). Discs put in that folder by hand are found
  too.
- **Signing**: the Android debug key (`~/.android/debug.keystore`, created if
  missing), or `GDUL_RELEASE_KEYSTORE` (+ `GDUL_RELEASE_KEYSTORE_PASSWORD`,
  `GDUL_RELEASE_KEY_ALIAS`, `GDUL_RELEASE_KEY_PASSWORD`). Releases use the
  `ANDROID_GDUL_*` secrets (docs/release-process.md).

Releases of all four systems: `.github/workflows/gdul-release.yml`, see
docs/release-process.md.
