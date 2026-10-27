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

`guitar_dream_version` must match the game's build version (gd-game-02:
`project(gd02 VERSION X.Y.Z)` in `engine/CMakeLists.txt`); the launcher only
rebuilds games whose version changed.

## Shared code (`gd-core/`)

Code moves into a shared `gd-core/` library only when a second game needs it
("rule of two"). The likely first candidates are the platform layer (renderer
and post-processing, AI upscalers, input and controllers, GPU selection,
Android) and, for the Harmonix-made games (Guitar Hero 1, 2, Encore: Rocks
the 80s, the Rock Band series), the shared formats (ARK archives, DTA
scripts, RND scenes, VGS audio, MIDI charts). Each game then builds it with
`add_subdirectory(../gd-core gd-core)`, and changes to `gd-core/` run every
game's CI.
