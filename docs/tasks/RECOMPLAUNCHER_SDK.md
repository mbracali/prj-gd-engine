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
  the Linux folder). Not in the APK (RecompLauncher is a desktop app).
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
| `start.env` | no | nothing of ours reads an env var for this |
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

RecompLauncher picks the file with the system and processor in its name
(`-Windows-x64.zip`, `-Linux-x64.AppImage`; the standards page also shows
`-linux-x86_64`). Ours, as `gdul-release.yml` publishes them:

| File | OK? |
|---|---|
| `gdul-<v>-windows-x86_64.zip` | yes |
| `gdul-<v>-macos-arm64.zip` | yes |
| `gdul-<v>-linux-x86_64.tar.gz` | yes (x86_64, not x64; the standards page uses it) |
| `gdul-<v>-steamdeck-x86_64.tar.gz` | unclear: no "linux" in the name; a second x86_64 .tar.gz beside the Linux one |
| `gdul-<v>-x86_64.AppImage` (+ .zsync) | unclear: no "linux" in the name; a second Linux candidate; the manifest is inside the AppImage, not next to it |
| `gdul-<v>-android-arm64.apk`, `SHA256SUMS.txt` | not for RecompLauncher (no desktop system in the name) |

Every archive has one top folder (`gdul-<v>-<os>-<arch>/`), which
RecompLauncher unwraps. Not renamed: the AppImage name is in its zsync
update information (GDREL-009) and the Steam Deck name is the owner's.
Owner's call: ask RecompLauncher which Linux file it takes, or rename.

## Suggestions (would need new code; not done)

- Read `RECOMPLAUNCHER_MODE=bigpicture` (full screen, controller first) and
  `RECOMPLAUNCHER_PAD` (button glyphs) in the launcher / games.
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
