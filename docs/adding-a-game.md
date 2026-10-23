# Adding a game

1. Copy `.gd-game-00-boilerplate/` to `gd-game-NN/` (NN from the root README
   table) and fill in its README.
2. Give the game its own program name `gdNN`, its own save folder
   (`SDL_GetPrefPath("gd-engine", "gdNN")`) and Android package
   (`io.github.mbracali.gdNN`).
3. Copy `.github/workflows/gd-game-01-{build,ci,release}.yml` to
   `gd-game-NN-*.yml` and change the paths, names and tag prefix.
4. Add a `gd-game-NN` label.

## Shared code (`gd-core/`)

Code moves into a shared `gd-core/` library only when a second game needs it
("rule of two"). The likely first candidates are the platform layer (renderer
and post-processing, AI upscalers, input and controllers, GPU selection,
Android) and, for the Harmonix-made games (Guitar Hero 1, 2, Encore: Rocks
the 80s, the Rock Band series), the shared formats (ARK archives, DTA
scripts, RND scenes, VGS audio, MIDI charts). Each game then builds it with
`add_subdirectory(../gd-core gd-core)`, and changes to `gd-core/` run every
game's CI.
