# RecompLauncher SDK (`recomplauncher.json`), GDUL-032

Owner 11-16 (branch `feature/20261116-metallica-bugfixes`): gdul is listed in
RecompLauncher, a store-like app; its SDK is one JSON file at the top of the
release that tells it how to set up and start the program. "Take total
advantage of the SDK, but only implement things that make sense ... look at
what we have on our build, not implement new stuff in our software."

So the manifest describes what gdul **already** does. Anything that would
need new code is listed under "Suggestions" for the owner to decide.

- SDK: https://recomplauncher.com/sdk (guide.md, schema
  https://recomplauncher.com/schema/recomplauncher.json, draft-07).
- The file: [gd-engine-launcher/package/recomplauncher.json](../../gd-engine-launcher/package/recomplauncher.json),
  copied by `scripts/build.sh` to the top of every desktop download (Linux,
  Steam Deck, Windows, macOS; the AppImage carries it inside, as it carries
  the Linux folder, and the release publishes it next to the AppImage too).
  Not in the APK (RecompLauncher is a desktop app).
- `scripts/verify_bundle.sh` (every build, every release archive) checks it:
  exactly one on top of a desktop download, valid JSON, `format` 1, a
  `start.program` that is in the download, every `--play gdNN` a ready game.

## What gdul has (the audit)

- **Program**: `Guitar Dream Universal Launcher` (Linux, Steam Deck),
  `.exe` (Windows), `.app/Contents/MacOS/...` (macOS), at the top of the
  download. The games are modules (`gd-engine-games/gd-game-NN/*.so|dll|dylib`)
  the launcher loads; there is no separate game program.
- **Command line** (`src/main.cpp`): `--play <game>` (gd-game-02, gd02 or 02),
  `--explore <game>`, `--iso <file>` (this run only, not saved; .iso or .chd),
  `--exit-after` (quit when the game ends, with its exit code), `--list`,
  `--version`, `--help`, `-- <args>` (to the game). No `--iso=<file>` form.
- **Exit codes**: 0 fine; 1 the game did not start (no / missing disc, not
  ready, no module) or ended with 1; 2 a wrong command line; with `--play
  --exit-after`, the game's own code (75 = Restart Now is handled inside: the
  launcher plays the game again).
- **Discs**: the launcher identifies a disc by its content (`iso_probe`,
  `game-info.yaml` `iso_signatures`): PS2 SYSTEM.CNF BOOT2 serial, Xbox 360
  default.xex title ID. A saved choice (`config.txt`, `iso.gd-game-NN=`) or a
  disc found in `original_isos/` (next to the program / AppImage, in the save
  folder, in the repo). .iso and .chd. No CRC / Redump tables in the repo.

  | Module | Game | Disc (signature) |
  |---|---|---|
  | gd01 | Guitar Hero (PS2) | SLUS_212.24 |
  | gd02 | Guitar Hero II (X360) | title ID 415607E7 (USA Rev 1) |
  | gd03 | Guitar Hero Encore: Rocks the 80s (PS2) | SLUS_215.86, volume GH2 |
  | gd04 | Guitar Hero III (X360) | title ID 415607F7 (USA Rev 1) |
  | gd05 | Rock Band (X360) | title ID 45410829 (USA) |
  | gd10 | Guitar Hero: Metallica (X360) | title ID 41560830 (USA / Europe) |

- **Save folders**: `SDL_GetPrefPath("gd-engine", <app>)`: Linux
  `~/.local/share/gd-engine/` (SDK `{local}`), Windows
  `%APPDATA%\gd-engine\` (Roaming, SDK `{config}`), macOS
  `~/Library/Application Support/gd-engine/` (SDK `{home}/Library/...`).
  `gdul/` is the launcher's (`config.txt` with the saved discs, `gdul.log`,
  `logs/<game>.log` of each game's last run); `gd01/` .. `gd10/` the games'
  (`career.json`, `save.json`, `rb_profile.json`, `enhanced.json`,
  `controllers.json`, `config.txt` with the disc path, Vulkan pipeline caches).
- **Settings**: each game's own `enhanced.json` (Enhanced Options:
  `graphics_backend`, `screen_mode`, `internal_res`, `max_fps`, effects ...),
  written by its in-game menu. The launcher has no settings of its own
  besides the saved discs.

## SDK field -> what we map it to

| SDK field | Used? | Mapped to / why not |
|---|---|---|
| `$schema`, `format: 1` | yes | |
| `name` | yes | "Guitar Dream Universal Launcher" (the local build's name in Developer tools) |
| `repo` | yes | mbracali/prj-gd-engine |
| `start.program` | yes | the three program paths (Windows, Linux, macOS app); the first that exists runs |
| `start.args` | yes, empty | no args: an older RecompLauncher (one tile) opens gdul's own list |
| `start.env` | no | not needed: RecompLauncher sets `RECOMPLAUNCHER_MODE` itself, and gdul reads it (Big Picture, below) |
| `games` (6) | yes | gd01, gd02, gd03, gd04, gd05, gd10: `--play gdNN --exit-after --iso {input:gdNN}` (existing options); `title_id` for the four X360 games; per-game `saves` |
| `inputs` (6) | yes | one per game, role game, .iso / .chd files; each game's tile asks only for its own disc |
| `game` (+ `check`, `dumps`) | no | one legacy copy for the whole release: six games need six discs, and `inputs` has no `check` / `dumps`. gdul checks the disc itself (content signature) when the game starts |
| `game.dumps` (Redump CRC32) | no | only exists on `game` (above); no CRC tables in the repo, so none computed |
| `game.update` | no | no title updates |
| `config` | no | see `settings` |
| `settings` | no | every game has its own `enhanced.json`, with its own option lists (backends differ per game and OS) and, for gd02 / gd03 / gd05, a `"format": "gdNN-enhanced"` marker: a file RecompLauncher creates without it is ignored by the game, and a file with a few keys skips the game's first-run profile (Steam Deck defaults). The SDK's settings are for the whole release, not per game tile. The in-game Enhanced Options already cover it |
| `groups` | no | no settings |
| `saves` | yes | each game's `*.json` in `{config}` (Windows), `{local}` (Linux), `{home}/Library/Application Support` (macOS); the top level lists all six for older launchers. Not `config.txt` (a machine path) nor the pipeline caches |
| `folders.saves` | yes | the `gd-engine` folder (each game's and gdul's), Windows when `{config}/gd-engine/gdul` exists, macOS likewise, else `{local}` |
| `folders.logs` | yes | `gdul/logs` (one `<game>.log` per game: the newest is the game that just ran) |
| `actions` | yes | open Saves, open Logs. No `clear`: nothing of ours is a cache folder |
| `exit_codes` | yes | 0 ok, 1, 2 (above) |
| `looks` | yes, `allowed: false` | the games have their own picture effects (sharpening, film grain, colour grade, bloom, AI upscaling) and switch graphics API in the same window (local GL, SDL3 GPU, Vulkan, OpenGL3); ReShade injected once per process would not follow |
| `requires` | yes | start, start.linux, start.macos, exit_codes |
| `min` | yes | 0.90.0-beta.36 (`games` needs Beta 36) |
| `prepare` | no | gdul has no setup step: the games read the disc directly |
| `mods` | no | no mod loader |
| `inputs.shared` nvidia.dlss | no | gd01's DLSS library already ships in the Linux download |

**Placeholder `{input:<id>}`**: the guide documents it for an input's path
(`"--dlss={input:dlss}"`); we use it for each game's disc. Check in the
Developer tab that Play shows the disc path there. If a game's input were
optional and skipped, the guide says the argument is left out, which would
leave `--iso` without a value (exit 2): that's why the inputs are required
(each tile only asks for its own disc).

## Validation

- `jsonschema` 4.26 (Draft 7) against the downloaded schema: valid. The
  two SDK examples also validate; a manifest with an extra key fails.
- Linux build (`build.sh linux`): `recomplauncher.json` at the top of
  `local_builds/gdul-1.1.15-linux-x86_64/`, `start.program` found, verify OK.
- The launcher with a tile's args: `--play gd02 --exit-after --iso
  "original_isos/CHD - gh2.chd"` starts Guitar Hero II (83 songs, Vulkan);
  a missing disc exits 1 with its message; `--iso` without a value exits 2.

## Release names

Owner 11-16: "The Linux release should be the AppImage."

RecompLauncher picks one file per system and processor from the words in
its name (guide: "Put the system and the processor in the name:
`MyRecomp-v1.2.0-Windows-x64.zip`, `MyRecomp-v1.2.0-Linux-x64.AppImage`";
its release standards: `-linux-x86_64.AppImage`). On Linux it takes "an
AppImage or a `.tar.gz`", and the guide does not say which wins when there
are both. So only the AppImage carries "linux" now: the other Linux-built
files have no system word, and nothing can tie with it.

| File (`gdul-release.yml`) | For RecompLauncher |
|---|---|
| `gdul-<v>-windows-x86_64.zip` | the Windows pick |
| `gdul-<v>-macos-arm64.zip` | the macOS pick |
| `gdul-<v>-linux-x86_64.AppImage` (was `gdul-<v>-x86_64.AppImage`) | the Linux pick: system and processor, the guide's own example form |
| `gdul-<v>-linux-x86_64.AppImage.zsync` | not a program (AppImageUpdate's file) |
| `recomplauncher.json` (new release asset) | the manifest next to the AppImage (no system / processor in the name, so never picked as the game) |
| `gdul-<v>-portable-x86_64.tar.gz` (was `gdul-<v>-linux-x86_64.tar.gz`) | not picked: no system word (same files as the AppImage; `gdul-<v>-linux-x86_64/` inside) |
| `gdul-<v>-steamdeck-x86_64.tar.gz` (unchanged) | not picked: no system word ("steamdeck" is not "linux") |
| `gdul-<v>-android-arm64.apk`, `SHA256SUMS.txt` | not for RecompLauncher |

Why `portable` and not `linux` for the .tar.gz: with "linux" in both names
the guide leaves the choice to RecompLauncher. The processor stays in both
.tar.gz names for people; without the system word they can only rank below
the AppImage.

**The manifest for the AppImage.** The guide only says "`recomplauncher.json`
at the top of the zip, next to your program"; it does not say whether it
looks inside an AppImage. So both: it is at the top of the AppImage's
AppDir (as in the .tar.gz) and published next to it as a release asset
(`build_appimage.sh` copies it beside the AppImage, `verify_bundle.sh`
checks it is there and the same as the one inside). Open point for
RecompLauncher: with the AppImage installed, `start.program` lists
`Guitar Dream Universal Launcher` (the program inside the AppImage); the
guide does not say how it starts an AppImage (it has a version in its
name). Check it in Developer tools, or ask them.

**AppImage updates.** The update information inside every AppImage is
`gh-releases-zsync|mbracali|prj-gd-engine|latest|gdul-*-x86_64.AppImage.zsync`
(unchanged): the `*` matches `1.1.15` (old name) and `<v>-linux` (new
name), so an installed older AppImage finds the newer `.zsync` and updates
(the `.zsync` carries the new file name). gdul has no other updater
(GDUL-031 is open). The site's buttons take both the old and the new
.tar.gz name, so they work before and after the first release with the
new names. `gdul-release.yml` unpacks the previous release's Linux
download by either name (module reuse).

## Big Picture mode (full screen)

Owner 11-16: "Start full screen" in Big Picture.

RecompLauncher sets `RECOMPLAUNCHER=1` and `RECOMPLAUNCHER_MODE`
(`bigpicture` | `desktop`) on every program it starts. With
`RECOMPLAUNCHER_MODE=bigpicture` (`platform::bigPicture`, not Android):

- the launcher's window is full screen (the desktop's own mode, no border)
  from the start and again after a game, instead of always a window;
- the game started (`--play`, or from the list) gets `GD_HOST_FULLSCREEN` in
  `GdHost::flags`; each module (gd01's in `modules/gd01`, gd02 .. gd10's
  `module_entry.cpp`) sets `GDNN_FULLSCREEN=1`, and the game's screen mode
  (Enhanced Options, `screen_mode`) treats Windowed as Full Screen (No
  Border) for that run, as the Android builds already do. Full Screen stays
  Full Screen; the saved setting is not changed.

`desktop`, or no variable: nothing changes. Log lines: `gdul: window
fullscreen|windowed WxH` (launcher), `gdNN: full screen (RecompLauncher Big
Picture): Windowed is not used this run` (game), `gdul: <game> ended, window
fullscreen|windowed WxH`.

Checked 11-16 (Linux build, `SDL_VIDEO_DRIVER=offscreen`):

- launcher alone, `bigpicture`: `gdul: window fullscreen`; `desktop` and no
  variable: `gdul: window windowed`.
- `--play gd10 --exit-after` (saved screen mode: default Windowed) and
  `--play gd05` (the same), `bigpicture`: `window: made again for Vulkan
  (..., full screen)`, `gdNN: full screen (RecompLauncher Big Picture)`,
  `gd-game-NN ended, window fullscreen`, the launcher full screen after;
  no variable: windowed all the way, as before.
- Build: `verify_bundle.sh` passes on the AppImage with `recomplauncher.json`
  beside it and fails without it; the `.zsync` says
  `Filename: gdul-1.1.15-linux-x86_64.AppImage`.

## Suggestions (would need new code; not done)

- Read `RECOMPLAUNCHER_PAD` (button glyphs) in the launcher / games.
  (`RECOMPLAUNCHER_MODE=bigpicture`: done, full screen, above.)
- `--iso=<file>` (one argument), or the standards' `--game <path>`, so an
  optional input can never leave a bare `--iso`.
- A settings file RecompLauncher can edit: one shared format for the games'
  `enhanced.json` (or creating a missing file with the game's defaults),
  then `config` + `settings` for screen mode, resolution, frame rate, graphics
  backend, per game.
- A distinct exit code for "wrong disc" (RecompLauncher shows our message).
- `portable.txt` (saves and logs next to the program).
- Recomp Achievements (RecompLauncher's achievement library).
- Ask RecompLauncher for `check` / `dumps` on `inputs`, so each game's disc
  gets verified and the Redump badge.

## How the owner tests it

RecompLauncher: Settings > Developers > Developer tools > Add a build > pick
`gd-engine-launcher/local_builds/gdul-1.1.15-linux-x86_64/` > Check. It
should show six games, six disc inputs (.iso / .chd), Play starting
`Guitar Dream Universal Launcher --play gdNN --exit-after --iso <disc>`, the
Saves and Logs buttons, looks off. Then set up one tile with a disc and press
Play: the game starts directly; quitting it returns to RecompLauncher.
