# Guitar Dream Universal Launcher: changes

## 0.1.7

Games
- New: Guitar Hero Encore: Rocks the 80s (gd-game-03, PS2) and Guitar Hero III:
  Legends of Rock (gd-game-04, Xbox 360), both 0.0.1 and playable from the
  launcher. Every build carries all four game modules.
- Guitar Hero II (gd-game-02) 0.1.7: Enhanced options start off (the original
  look).

## 0.1.6

Fixes
- Android: the launcher no longer touches the window mode (no window mode
  there: leaving full screen, sizes and the minimum size are desktop-only).
- Android: Guitar Hero (gd-game-01) is in the APK ("This launcher build has
  no gd-game-01 module" is gone). It runs with OpenGL ES 3.0, as its own APK
  does; its AI upscalers (Vulkan FSR / DLSS) are not available on Android.
- Android 15: a disc image chosen with Load ISO could not be used ("Cannot
  open /data/data/io.github.mbracali.gdul/files/original_isos", "Cannot open
  /proc/self/fd/..."). The launcher no longer copies the disc into its own
  folder (a 7 GB copy that could fail for lack of space): it reads the disc
  where it is, through the file the file dialog gave, and keeps the right to
  read it after a restart. If that right is lost or the file is not on the
  device (a cloud drive), it says so and asks to choose the disc again.
- Android: the launcher, Guitar Hero and Guitar Hero II each keep their own
  settings and saves (they shared one folder, so one game could overwrite
  the launcher's disc choices); Guitar Hero II's earlier saves come along.
- Android: "Where we look for discs" offers Load ISO instead of an "Open
  folder" button no app on Android can open.

Downloads
- Every launcher download, on every system, carries the module of every
  ready game; a build without one fails (scripts/build.sh, verify_bundle.sh),
  and the launcher says at start when a ready game's module is missing,
  whatever its manifest lists.
- Steam Deck is back: gdul-<version>-steamdeck-x86_64.tar.gz, built in
  Valve's Steam Runtime 3 ("sniper"), with every game set up for the Deck
  out of the box (Guitar Hero and Guitar Hero II: full screen at 1280 x 800,
  60 FPS, lighter effects; your saved settings still win) and STEAM-DECK.txt
  (adding it to Steam as a non-Steam game, Gaming Mode tips).

New
- Steam Deck: the launcher recognises a Deck (also the plain Linux download:
  SteamDeck=1, or the Deck's board) and tells the games, which then start
  with their Steam Deck settings; the Deck's built-in controls work in
  Gaming Mode (Steam's virtual controller) and without Steam (SDL's Steam
  Deck driver).
- Command line: --play <game> and --explore <game> start a game or its
  models at once (--iso <file> for this run's disc, -- <args> for the game,
  --exit-after to quit afterwards with the game's exit code; otherwise the
  list comes back), --list (games, versions, ready, discs found), --version,
  --help. On Windows the text goes to the terminal the launcher was started
  from.
- Explore models: characters start in their rest pose (Guitar Hero II's in a
  T-pose), so no arm or hand goes through the body; Next animation plays
  the clips, then the rest pose again. A "Show instrument" checkbox (click,
  I, or the left stick's click) shows or hides a character's guitar, bass or
  drum kit.

Games
- Guitar Hero (gd-game-01) 1.1.6 and Guitar Hero II (gd-game-02) 0.1.6 (see
  their changelogs). Guitar Hero II shows 79% complete.

## 0.1.5

New
- Guitar Hero II 0.1.5: the mouse in its menus, the GAMEPADS screen and the
  Enhanced Options laid out as in Guitar Hero, and controllers that no longer
  press left / right by themselves.

## 0.1.4

Fixes
- The launcher is always a window: after a game that ran full screen (or
  maximized it, hid the cursor or grabbed the mouse), it leaves full screen,
  waits for the system to finish (Windows and macOS do it asynchronously) and
  puts its window back at the size and place it had. It never goes full
  screen by itself.
- Steam Deck (Gaming Mode) and Steam Input: the list sees Steam's virtual
  controller, as the games do, and one press seen from two controllers at
  once acts once.

New
- Menu sounds without a Guitar Hero disc: the launcher's own, made at start
  (plucked strings and a small power-chord sting, nothing taken from a game),
  at the same loudness as the disc's. With a Guitar Hero disc it still uses
  the game's own.

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
