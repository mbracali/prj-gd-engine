# Guitar Dream Universal Launcher: user manual

## What it is

The Guitar Dream Universal Launcher lists every game of the Guitar Dream
Engine project and plays the ready ones from your own disc images, inside
its own window. You need your own original discs: the launcher and the games
contain no game data.

## The list

Each row is one game: `gd-game-NN/<name>`, then its system, year, Guitar
Dream version and disc image. On the right:

- **Complete**: how much of the original game is reimplemented.
- **Play**: starts the game. It works when the game is ready to play and has
  a disc image.
- **Load ISO** / **Change ISO**: choose the game's disc image with your
  system's file dialog.
- **Explore models**: coming soon (browse the disc's 3D models).

The light on the left: green, ready to rock (playable and a disc is known);
yellow, playable but needs a disc; grey, coming soon. A green row is lit up.

## Disc images

- Put them in `original_isos/` next to the launcher: they are found every
  time it starts.
- Or **drop a disc image anywhere on the window**: the launcher reads it,
  tells you which game it is, and offers to remember it for that game and
  play. If it is not a disc of a game it knows, it says
  "We can't rock this! Sorry...".
- Or use **Load ISO** on the game's row.

The launcher recognises discs by their content, not their name.

## Controls

| | Keyboard | Gamepad | Guitar |
|---|---|---|---|
| Move | arrow keys | D-pad / left stick | strum up / down |
| Select | Enter | A | green fret |
| Back / quit | Esc | B | red fret |
| Load ISO | L | Y | yellow fret |
| Explore models | E | X | blue fret |

You can also point and click with the mouse, and scroll with the wheel.

## Playing

Press **Play**: the game opens in the same window. Its controls and options
are the game's own (see its README in `gd-engine-games/<game>/`). When you
quit the game (or close the window), you are back in the list.

## Your files

The launcher keeps its settings and a log in your user folder:

- Linux: `~/.local/share/gd-engine/gdul/`
- Windows: `%APPDATA%\gd-engine\gdul\`

`config.txt` remembers the disc image chosen for each game; `gdul.log` has
one line per action (date, user, action); `logs/` the games' messages of
their last run. Each game keeps its own saves and settings in its own folder
(`gd-engine/gd01`, `gd-engine/gd02`).

## Problems

- A game is not lit: check that its disc image is in `original_isos/` or
  load it with Load ISO; the row says which disc it uses.
- A game does not start: the launcher shows why; `gdul.log` and
  `logs/<game>.log` in the folder above have the details. Please include them
  when you report a problem.
