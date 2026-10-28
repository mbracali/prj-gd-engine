# Guitar Dream Universal Launcher: changes

## 0.1.3

Fixes
- Windows: Guitar Hero II did not start ("gd-game-02.dll: LoadLibrary failed
  (error 126)"): FFmpeg's avutil needs MinGW's libwinpthread-1.dll, which
  no download carried. It ships now (gd-game-02 0.1.4), and the download
  check follows every DLL's own imports.
- Guitar Hero (gd-game-01 1.1.4): Enter on the keyboard finishes the band
  name, like Start.

## 0.1.2

Fixes
- The Windows download has Guitar Hero II (gd-game-02) again: 0.1.1 shipped
  without its module and FFmpeg libraries. The build now stops on any failed
  step, and every download is checked for each ready game's module and
  libraries before it is published.
- An incomplete download is said at start ("This download is incomplete:
  ... please download again") and written to the log, not only when a disc
  is loaded.
- Guitar Hero II 0.1.3: Enter finishes a band's name with the keyboard.

## 0.1.1

Fixes
- Explore models no longer flickers (seen on macOS). The game module still
  draws the model with its own OpenGL context, but the launcher now reads
  that frame back and draws it with its own context, which alone presents,
  every frame cleared. Before, the two contexts drew into one window and
  the launcher drew over a back buffer whose content macOS does not keep.
- Disc images are found in more places: next to the `.app` and inside it on
  macOS; next to the app's real place when macOS runs it from a translocated
  copy (an app still where the download put it), with a notice explaining
  how to fix it; and in `original_isos/` in the launcher's save folder on
  every desktop system. The header's folders link lists each folder searched
  and what it held (or why it could not be read), with Open folder and Look
  again; the folders are written to `gdul.log` at start.

Improvements
- Menu sounds: Guitar Hero's own (move, select, play, back, error), read at
  run time from your Guitar Hero disc by the gd-game-01 module (new optional
  module export `gd_game_ui_sounds`). Silent without that disc; the audio
  device is released while a game runs.
- The header's OFFICIAL SITE pill opens https://guitardreamengine.com.
- The title reads "Guitar Dream Engine", over "UNIVERSAL LAUNCHER" and the
  version.
- The list's footer no longer shows a legend of keys (every button is
  clickable). In Explore models the footer's actions are clickable buttons,
  and the model groups open and close (click a header, or green / left /
  right on it).
- The badge no longer repeats "Guitar Dream Engine" at the bottom; every
  icon made from it is new (launcher, Android, gd-game-02, the website).

## 0.1.0

The first release: the game list, disc recognition, games run as modules in
the launcher's window, Explore models, Linux, Windows, macOS and Android.
