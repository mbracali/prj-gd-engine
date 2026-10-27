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
- **Explore models**: the 3D models on your disc (characters, guitars,
  drum kits, venues and more), to turn, zoom and watch them move (below).

The light on the left: green, ready to rock (playable and a disc is known);
yellow, playable but needs a disc; grey, coming soon. A green row is lit up.

At the top right, **OFFICIAL SITE** opens the project's website,
https://guitardreamengine.com, in your browser; under it, how many games are
ready and how many disc images were found.

## Disc images

- Put them in `original_isos/` next to the launcher (on macOS, next to
  `Guitar Dream Universal Launcher.app`), or in `original_isos/` in the
  launcher's save folder (below): they are found every time it starts.
  The **folders** link at the top right (it says **where we looked** when
  nothing was found) lists every folder searched and what was in it, and
  has **Open folder** and **Look again**.
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

**Sounds**: if a Guitar Hero (PlayStation 2) disc is known, the launcher's
menus use Guitar Hero's own menu sounds, read from your disc each time it
starts (nothing is copied). Without that disc the launcher is silent.

## Playing

Press **Play**: the game opens in the same window. Its controls and options
are the game's own (see its README in `gd-engine-games/<game>/`). When you
quit the game (or close the window), you are back in the list.

## Exploring models

**Explore models** (blue) opens the disc's 3D models in the same window: the
list on the left, grouped (for Guitar Hero II: characters, crowd, guitars
and basses, drum kits, venues, venue props; for Guitar Hero: guitarists, band,
crowd, guitars, venues), and the chosen model on the right, slowly turning
on a turntable. Characters play their animations (idle first). Click a
group's header (or press green on it) to close or open it; left and right
on a header close and open it too. The buttons at the bottom (Show, Back,
Reset view, Next animation, Turntable) can be clicked.

| | Keyboard | Mouse | Gamepad | Guitar |
|---|---|---|---|---|
| Choose a model | arrow keys up / down | click | D-pad / left stick | strum |
| Search | / or F, then type | click the search box | | |
| Turn | arrow keys left / right | drag | right stick | |
| Zoom | + / - | wheel over the model | triggers | |
| Move (pan) | | right or middle drag, Shift + drag | right shoulder + right stick | |
| Reset the view | R | double-click | Y, right stick click | yellow fret |
| Next animation | N, Tab | | X | blue fret |
| Turntable on / off | T | | left shoulder | orange fret |
| Back to the list | Esc | right click on the list | B | red fret |

Esc first clears the search; pressing it again goes back to the list.

## Your files

The launcher keeps its settings and a log in your user folder:

- Linux: `~/.local/share/gd-engine/gdul/`
- Windows: `%APPDATA%\gd-engine\gdul\`
- macOS: `~/Library/Application Support/gd-engine/gdul/`
- Android: the app's own storage (removed with the app)

On **macOS** put your disc images in `original_isos/` next to
`Guitar Dream Universal Launcher.app`. If they are not found, the app is
probably still where the download put it: macOS then runs it from a hidden
temporary copy (App Translocation) and the launcher tells you so. Move the
app to another folder with the Finder (Applications, say; take
`original_isos/` along or keep the discs in the save folder's
`original_isos/`) and open it again, or in Terminal run
`xattr -dr com.apple.quarantine "/path/to/Guitar Dream Universal Launcher.app"`.
If macOS asks whether the launcher may read your Downloads, Documents or
Desktop folder, allow it when your discs are there. On **Android** use Load ISO: the
launcher copies the disc image you pick into its folder
(`Android/data/io.github.mbracali.gdul/files/original_isos/`, which takes a
few minutes and as much space again; afterwards the file you picked is no longer needed) and
says when it is ready. Only the games with an Android module can be played
there (Guitar Hero II, gd-game-02).

`config.txt` remembers the disc image chosen for each game; `gdul.log` has
one line per action (date, user, action); `logs/` the games' messages of
their last run. Each game keeps its own saves and settings in its own folder
(`gd-engine/gd01`, `gd-engine/gd02`).

## Problems

- A game is not lit: check that its disc image is in `original_isos/` or
  load it with Load ISO; the row says which disc it uses, and the folders
  link at the top right says where the launcher looked.
- A game does not start: the launcher shows why; `gdul.log` and
  `logs/<game>.log` in the folder above have the details. Please include them
  when you report a problem.
