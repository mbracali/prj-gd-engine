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

## Shared code (`gd-core/`)

Code moves into a shared `gd-core/` library only when a second game needs it
("rule of two"). The likely first candidates are the platform layer (renderer
and post-processing, AI upscalers, input and controllers, GPU selection,
Android) and, for the Harmonix-made games (Guitar Hero 1, 2, Encore: Rocks
the 80s, the Rock Band series), the shared formats (ARK archives, DTA
scripts, RND scenes, VGS audio, MIDI charts). Each game then builds it with
`add_subdirectory(../gd-core gd-core)`, and changes to `gd-core/` run every
game's CI.
