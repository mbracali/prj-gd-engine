# Adding a game

1. Copy `.gd-game-00-boilerplate/` to `gd-game-NN/` (NN from the root README
   table) and fill in its README and `game-info.yaml` (below). The folder
   layout, with `gd-game-02/` as the model:

   ```
   gd-game-NN/
     README.md  CHANGELOG.md  local_build.sh  game-info.yaml
     build_commands/  docs/  engine/  tools/
     agents/         notes for AI coding agents (agents/scratch/ is git-ignored)
     extracted/      your own rip of the disc (git-ignored)
     ghidra/         Ghidra project + analysis/ export (git-ignored, original code)
     local_builds/   local test builds (git-ignored)
   ```

   Disc images go in the repository root's `original_isos/` (git-ignored),
   shared by every game, not inside the game folder.
2. Give the game its own program name `gdNN`, its own save folder
   (`SDL_GetPrefPath("gd-engine", "gdNN")`) and Android package
   (`io.github.mbracali.gdNN`).
3. Copy `.github/workflows/gd-game-01-{build,release}.yml` to
   `gd-game-NN-*.yml` and change the paths, names and tag prefix.
4. Add a `gd-game-NN` label.
5. Make sure `gd-engine-launcher/games.yaml` lists `gd-game-NN/game-info.yaml`.
6. When the game becomes playable, make it a launcher module (below).

## game-info.yaml

Read by the Guitar Dream Universal Launcher. Keep it up to date with each
release:

```yaml
id: gd-game-NN
name: "Guitar Hero II"              # the original game's full name
nick_name: "Guitar Hero II"         # short name
launch: 2007-04-03                  # release date of the version reimplemented
platforms: [PlayStation 2, Xbox 360]
based_on: Xbox 360                  # the version this folder reimplements
guitar_dream_version: "0.1.1"       # this folder's own version
ready_to_play: true                 # the launcher can play it
completeness: 0.6                   # 0..1
```

Add `iso_signatures` (below) so the launcher recognises the game's disc.

`guitar_dream_version` must match the game's build version (gd-game-02:
`project(gd02 VERSION X.Y.Z)` in `engine/CMakeLists.txt`); the launcher only
rebuilds games whose version changed.

## Disc signatures (`iso_signatures`)

The launcher identifies a dropped or found disc image by its content. List
one or more signatures; every field given must match, and any matching
signature identifies the game:

```yaml
iso_signatures:
  - format: xdvdfs            # Xbox / Xbox 360 (XDVDFS, at any XGD offset)
    xex_title_id: "415607E7"  # default.xex's title ID
  - format: iso9660           # PlayStation 2 and other ISO 9660 discs
    ps2_boot: "SLUS_214.47"   # SYSTEM.CNF's BOOT2 program
    volume_id: "..."          # optional: the volume name
    files: [SYSTEM.CNF]       # optional: names in the root folder
```

Add one per region or revision the game supports. To read a disc's values,
drop it on the launcher: `gdul.log` shows what it found ("Xbox 360 disc, title
ID 415607E7", "PlayStation 2 disc, SLUS_212.24").

## The launcher module

The launcher plays a game by loading it as a shared library into its own
process (`gd-engine-launcher/README.md`, "Games as modules"):

1. Keep the game's `main` in a function (`int gdNNMain(int, char**)`) and add
   `src/platform/module_entry.cpp`, like gd-game-02's: it exports
   `extern "C" int gd_game_run(const GdHost* host)` (`gd-engine-launcher/sdk/gd_host.h`),
   calls `gdshim_begin(host)`, runs `gdNNMain` with `host->argv` (the
   program name, then the disc image), then `gdshim_end()`.
2. Add a CMake option `GDNN_MODULE` that builds `gd-game-NN.so` / `.dll`
   (`OUTPUT_NAME gd-game-NN`, no `lib` prefix) from the program's sources,
   with `gd-engine-launcher/sdk/gd_module_shim.cpp`, the include
   `-include gd_module_shim.h` (it turns the game's `SDL_Init`, `SDL_Quit`,
   `SDL_CreateWindow`, `SDL_DestroyWindow` and GL context calls into the
   launcher's), hidden visibility, `-fno-gnu-unique` (GCC) and RPATH `$ORIGIN`
   for libraries next to it. See the end of `gd-game-02/engine/CMakeLists.txt`.
3. In the game: open controllers on `SDL_EVENT_GAMEPAD_ADDED` (the launcher
   announces the connected ones), quit on `SDL_EVENT_QUIT`, free what you
   allocate, join your threads, and never call `exit()` or `execv()`.
4. Add `build_module_NN` to `gd-engine-launcher/scripts/build.sh`: configure
   with the option on, build the module, copy it, the libraries it needs and
   their licences (`licenses/`) into the game's folder.

### The model explorer (optional)

The launcher's **Explore models** screen shows the disc's models with the
game's own loaders and renderer. A module that has one exports a second
entry point (`gd-engine-launcher/sdk/gd_host.h`):

```c
extern "C" const GdExplorer* gd_game_explore(const GdHost* host);
```

The launcher draws the list, the search box and the help, and drives the
camera; the module only lists models and draws the selected one. See
`gd-game-02/engine/src/platform/module_explore.cpp` (built into
`gd-game-02.so` next to `module_entry.cpp`) or, for a game whose folder
cannot change, `gd-engine-launcher/modules/gd01/gd01_module_explore.cpp`.

1. `gd_game_explore` calls `gdshim_begin(host)` and
   `gdshim_set_exploring(true)` (from then on `SDL_GL_SwapWindow` does
   nothing: the launcher presents the frames), opens the disc
   (`host->argv[host->argc - 1]`), `SDL_Init(SDL_INIT_VIDEO)`, creates its
   renderer on `host->window` (its own GL context, through the shims), lists
   the models and returns a `GdExplorer` (a static one is fine), or null when
   it cannot (the launcher then shows "We can't rock this!").
2. `item_count` / `item`: the models in list order, each with a `group`
   ("Characters", "Guitars", "Venues", ...; a group's items together), a
   `name` and a `detail` line (the archive path). Strings stay valid until
   `close`.
3. `select(index, info, size)`: loads one model (the launcher has shown
   "Loading..." first), returns 0 and a short description in `info` ("247
   bones, 118 animations"), or non-zero and the reason. `clip_count` /
   `clip_name`: its animations (the launcher plays clip 0 first; put an idle
   one there).
4. `draw(view)`: `gdshim_make_current()` first (the launcher's context is
   current between calls), draw the whole window (clear it to
   `view->background`), with the model centred in the viewer area
   `view->x, y, w, h` (window pixels): frame it from its bounds, then orbit by
   `yaw` / `pitch`, scale the distance by `zoom` and move the centre by
   `pan_x` / `pan_y` model radii; play `view->clip` at `view->seconds`. Never
   present, and leave your context current: the launcher reads the viewer's
   area of its back buffer, draws it with its own context under its panels
   and presents the frame (one presenting context: no flicker on macOS,
   where each context has its own drawable).
5. `close`: free the models and the renderer, then `gdshim_end()`. The
   launcher unloads the library afterwards.

`gd-engine-launcher/sdk/gd_explore_helpers.h` has helpers that know no
format (e.g. `uprightRows`, which stands a guitar up for the camera).

### Menu sounds (optional)

A module may give the launcher its game's menu sounds, read from the disc,
for the launcher's own menus (only one game's are used: the first in
`games.yaml` with a disc that gives some):

```c
extern "C" int gd_game_ui_sounds(const GdHost* host, GdUiSoundFn sound, void* user);
```

Read the disc (`host->argv[host->argc - 1]`) and call `sound(user, role,
stereo, frames, rate)` for each sound: `role` is `"move"`, `"select"`,
`"play"`, `"back"` or `"error"` (a role may come several times: variations,
one is picked at random), the samples interleaved stereo float. Return how
many you gave. No window or GL work; the library is unloaded afterwards. See
`gd-engine-launcher/modules/gd01/gd01_module_sounds.cpp` (Guitar Hero's
metagame bank through gd01's SPU2).

## GAMEPADS, the Enhanced menu and the mouse

Every game has the same GAMEPADS screen (opened with the yellow button on
the main menu), the same Enhanced menu and the same mouse support, modelled
on gd-game-01's: follow
[guidelines/controls-and-enhanced-menu.md](guidelines/controls-and-enhanced-menu.md).

## Shared code (`gd-core/`)

Code moves into a shared `gd-core/` library only when a second game needs it
("rule of two"). The likely first candidates are the platform layer (renderer
and post-processing, AI upscalers, input and controllers, GPU selection,
Android) and, for the Harmonix-made games (Guitar Hero 1, 2, Encore: Rocks
the 80s, the Rock Band series), the shared formats (ARK archives, DTA
scripts, RND scenes, VGS audio, MIDI charts). Each game then builds it with
`add_subdirectory(../gd-core gd-core)`, and changes to `gd-core/` run every
game's CI.
