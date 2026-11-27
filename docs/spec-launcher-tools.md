# Guitar Dream Universal Launcher and its tools

> Purpose: A spec fed to agents building/onboarding games — the universal launcher and its tools.

This file is handed to every agent onboarding a new game. It describes the
Guitar Dream Universal Launcher (`gd-engine-launcher/`, binary name "gdul"):
how it finds games and discs, how it launches each game's module through the
SDK/ABI, and the three tools a game can light up — the **Explore** model
explorer (with its in-pane stem player), the **Additive Music** menu, and the
**File explorer / Load ISO** disc-loading flow. It is grounded in the current
code and docs (see Sources); where a feature is partial or deferred, it says
so rather than overstating.

## Overview

The launcher is the project's **only release**: it is one SDL3 window for its
whole life, and each game ships as a **shared-library module**
(`gd-game-NN.so` / `.dll`) loaded into the launcher's own process. A missing
module is a broken release (it has shipped twice before), so **every platform
build carries every ready module** — this is the hard rule enforced by
`scripts/verify_bundle.sh` (see Onboarding notes).

**Finding games.** `Catalog::load` (`src/catalog.cpp`, `src/catalog.h`) reads
`gd-engine-launcher/games.yaml` — an ordered list of per-game
`game-info.yaml` paths — then each game's `game-info.yaml`. The launcher's own
version comes from `gd-engine-launcher/launcher-info.yaml`
(`launcher_version`, SemVer). The game list order is fixed (owner rule): every
game with `ready_to_play: true` is listed before the rest, each group in
`games.yaml` order; `Catalog::load` sorts once and everything (desktop list,
Android list, keyboard/gamepad/mouse nav, first focus, `--list`, Explore)
follows it. Saved state is keyed by game id, never by row. `ctest -R
gdul_order` checks it.

**game-info.yaml** (per game) carries: `id`, `name`, `nick_name`, `launch`
(the reimplemented release's date), `platforms`, `based_on`,
`guitar_dream_version` (the folder's own version), `ready_to_play`,
`completeness` (0..1, drives the playability % icon), and `iso_signatures`
(how the launcher recognises the disc by content). gd-game-01 is frozen, so
its signatures live in `games.yaml`'s `extra_iso_signatures` instead.

**Finding discs.** The catalog scans `original_isos/` folders near the app
(and the launcher's save folder, the repository when run from source) for
`.iso` and `.chd` images, identifies each by **content, not file name**
(`probeDisc`, `src/iso_probe.cpp`), and matches it to a game's
`iso_signatures`. Each game keeps one chosen disc (`Game::iso`), saved in
`config.txt` in the save folder.

**Launching a module.** `runGameModule` (`src/module_host.cpp`,
`src/module_host.h`) loads the library, hands over the window + GL context +
controllers + SDL, calls the module's `extern "C" int gd_game_run(const
GdHost*)`, then takes everything back and **unloads the library** (so every
module global starts fresh next run). The ABI is `gd-engine-launcher/sdk/gd_host.h`
(`GD_HOST_ABI_VERSION 1`; fields are only ever appended, guarded by
`GD_HOST_HAS`). Plain C types only — launcher and modules may be built with
different compilers/runtimes, but share the same SDL3 shared library. Key
`GdHost` fields: `window`, `gl_context`, `argc`/`argv` (argv[0] program name,
**last argv is the disc image path**), `launcher_version`, `game_id`,
`game_dir`, `save_dir`, `log`, `flags` (`GD_HOST_HANDHELD` for Steam Deck,
`GD_HOST_FULLSCREEN` for Big Picture), `request_window` (switch the window to
SDL_GPU/Vulkan/Metal for games built on gd-engine-core), and `extra_discs` /
`extra_disc_count` (the additive-songs discs, below). `gd_game_run` returning
`GD_GAME_EXIT_RESTART` (75) makes the launcher re-run the game from scratch
(used by Enhanced > backend change).

**CLI flags** (`src/main.cpp`; `--help`):
- `--play <game>` — start that game at once. Game id accepts `gd-game-02`,
  `gd02` or `02`.
- `--explore <game>` — open that game's Explore screen at once.
- `--iso <file>` — the disc image for this run only (not saved); without it,
  the game's saved or found disc.
- `--exit-after` — quit when the game/explorer ends, with the game's exit
  code; without it the list returns.
- `--list` — print games (version, ready, module present, disc found) plus
  the disc search folders.
- `--version` / `-v`, `--help` / `-h` / `/?`.
- `-- <args>` — everything after `--` is passed to the game's own command
  line (before the disc image); `setExtraGameArgs`.
- Diagnostics: `--update-selftest`, `--shortcut-selftest`.
- Exit codes: 0 fine, 1 game did not start, 2 bad command line.
- Env hooks: `GDUL_WINDOW`, `GDUL_SCRIPT`, `GDUL_NO_UPDATE_CHECK`,
  `RECOMPLAUNCHER_MODE` (Big Picture / RecompLauncher-managed updates).

## Launcher UI

The main screen (`src/launcher.cpp`, drawn in 1280x720 design units scaled to
the window) is a **table**: a header row of column names, then one row per
game, with sticky **horizontal and vertical** scrollbars and a footer
"Developed with ❤ by Marcelo Bracali". Material icons throughout.

**Per-row layout and columns** (`enum Column`, order is the on-screen order):
- **Play** — drawn **before the game name** (`green(play_circle)`). Enabled
  when the game is `playable()` (ready + module present + disc set). Clicking
  Play on a not-found game opens the Load ISO flow instead.
- Game name column header is `GAME NAME`.
- **% Play** (`kPlay`, display only) — the playability % icon from
  `completeness`, colour by bucket (`pctIcon`): 0 → white circle; ≤10/20/40 →
  neon-yellow `clock_loader_*`; ≤60/80/<95 → neon-green `clock_loader_*`; 95–99
  → neon-green full stroke; 100 → gold stars. Header `% PLAY`.
- **Additive musics** (`kAdditive`) — `neon_red(music_note_add)`; opens the
  Additive Music tool. Enabled when the game is playable. Header `ADDITIVE
  MUSICS`.
- **Explore files** (`kExplore`) — `blue(database_search)`; opens the Explore
  screen. Header `EXPLORE FILES`.
- **Mod menu** (`kMod`) — `neon_purple(add_shopping_cart)`; **disabled
  placeholder**, hover tip "Coming soon". Header `MOD MENU`.
- **Load ISO** (`kLoad`) — `yellow(album)` when a disc is set, `yellow(disc_full)`
  when not; stays yellow for every not-found game, grey for found ones. Opens
  the disc picker. Header `LOAD ISO`.

Buttons are coloured only when the game's ISO is found and ready, grey
otherwise (except Load ISO, which is always actionable). Navigation: arrows /
gamepad move a single focus across rows and columns; Enter is Start; mouse
hover focuses, left-click selects, right-click is Back; wheel scrolls; touch
taps are clicks.

**Header.** Shows the launcher version badge (also the entry to the in-launcher
updater) and a disabled `yellow(handyman) DREAM FORGE ENGINE` button (never
selectable, hover tip "Coming soon!"). There is a desktop full-screen /
windowed toggle (also F11).

**In-launcher updater** (`src/update_ui.cpp`, `src/updater.cpp`,
`src/updater.h`; #114). At start-up (unless under RecompLauncher, `--play`/
`--explore`, `GDUL_SCRIPT` or `GDUL_NO_UPDATE_CHECK`) it asks GitHub's public
REST API for the latest gdul release (no token; User-Agent `gdul/<version>`;
nothing personal). If a newer build exists it warns the player; only on the
player's confirmation does it **download this build's package to the user's
Downloads folder** and verify SHA-256 against `SHA256SUMS.txt`. It **never
touches the current install** — the player installs the new build themselves.
A failed check is silent (one `gdul.log` line). Android opens the APK URL in
the browser. The updater also has a manual "check now" path and a per-version
"skip".

**Right-click "Add instant icon"** (`src/shortcut.cpp`, `src/shortcut.h`;
#141). Right-clicking a game row (that has a disc loaded) opens a quick-access
menu with **Add instant icon**, which writes a desktop shortcut that starts
that one game directly, bypassing the list. It reuses the launcher's own
command line (`--play <id> --iso <disc> --exit-after`). Linux writes a
`.desktop` file, Windows a `.lnk`, macOS a `.command`. The shortcut's visible
name is exactly `(GD-ENGINE) <game name>`. Not available on Android, or where
the launcher's own path cannot be found (`shortcut::supported()`).

## File explorer & disc loading

The launcher never requires extraction or copying — it reads each disc **in
place**.

**Scanning** (`Catalog`, `src/catalog.cpp`). At load and on `rescanDiscs`, it
looks through `original_isos/` folders in platform-specific places (next to
the app/AppImage, inside the macOS bundle, the launcher save folder, the
repository when run from source — all reported by `--list` and the "disc
folders" dialog). Any `.iso` or `.chd` file is probed; if its content matches
a game's signature and that game has no disc yet, it becomes that game's
found disc.

**Identifying a disc by content** (`probeDisc`, `src/iso_probe.cpp`). It reads
the file system (ISO 9660 for PS2 discs, XDVDFS for Xbox/360 discs), the
volume id, the root file names, the PS2 boot program (`SYSTEM.CNF` BOOT2), the
PSP disc id (`UMD_DATA.BIN`) and the Xbox 360 title id (`default.xex`), then
matches a game's `iso_signatures`. File names are never trusted.

**Supported disc formats:**
- **ISO** images: ISO 9660 (PS2) and XDVDFS (Xbox/360).
- **CHD** images (GDUL-030): detected by the CHD signature and read through
  `gd::core::platform::ChdImage` (gd-engine-core), transparently to the rest
  of the launcher.
- **Folders** are supported where a game's disc reader accepts an extracted
  folder (e.g. gd-game-04 reads an XDVDFS image or a folder). The launcher's
  scan itself enumerates `.iso`/`.chd` files; a folder disc is chosen via Load
  ISO / the game's own path.

**Load ISO flow** (`pickDisc` / `handleDisc`, `src/launcher.cpp`). Opens the
system file dialog (its answer arrives on another thread, collected via
`picked_`). The chosen file is probed; the launcher offers to **Save & play**,
**Save only** (remember it as that game's disc), or Save when the game is not
yet playable. A disc may also be **dropped onto the launcher window**. Clicking
Play (or the right-click menu) on a game with no disc routes here.

**Per-platform specifics:**
- **Android / SAF** (`platform.cpp`, `GdulActivity.java`). An app's private
  folders cannot be shown to a file manager, so Load ISO is the path: the user
  picks a disc anywhere on the device (Downloads, SD card) through the Storage
  Access Framework. The launcher keeps a `content://` URI with persisted read
  permission across restarts and never copies the file; the module is handed
  `/proc/self/fd/<n>` as its disc path (`module_host.cpp`,
  `platform::openDocument`).
- **macOS translocation.** When the app runs from a translocated (read-only,
  random) copy, the `original_isos` folder beside it may not be seen; the disc
  dialog explains how to clear quarantine / move the app.
- The disc path is stored in `config.txt` (desktop) or as the `content://`
  URI (Android) — never the disc bytes.

## Explore (model explorer + stem player)

The Explore screen (`src/explorer.cpp`) lets the player browse a disc's assets
and hear its songs. **The launcher draws the screen** (list, search, help,
camera) **and the module draws the chosen model** through the optional
`gd_game_explore` export returning a `GdExplorer*` (`sdk/gd_host.h`).

**Standardized groups.** Items carry a `group` string; the launcher builds the
group list with **Songs first** (owner 11-23), then the model groups in order.
The intended standard groups are **Songs · Venues · Guitars · Bass · Drums ·
Mics · Chars** (Characters). Every group starts **collapsed**. Each model item
is `{ group, name, detail }` (`GdExploreItem`): `name` is the list label
(e.g. "Alterna (Johnny Napalm)", "Gibson SG"), `detail` a second line (archive
path, etc.). (The exact group names and coverage are each game's
responsibility; the launcher simply groups by whatever strings the module
returns, and a game onboarding should use the standard set so players find the
same layout everywhere.)

**Model viewer** (`GdExploreView`). The camera orbits the model's centre; the
module frames the model (its bounds) and the launcher turns, zooms and pans
relative to it: **drag / arrows / right stick** rotate (yaw, pitch ±1.4);
**wheel / triggers / +-** zoom; **right or middle drag** pans. The module's
`draw(view)` renders into the launcher's window with the module's own GL
context (the launcher reads back the viewer area and composites). `select`
loads a model (shows a loading state, returns a short `info` string).
`clip_count` / `clip_name` expose animation clips; the Blue fret / animation
keys cycle rest pose → each clip → rest pose. Optional hooks:
`has_instrument` / `show_instrument` add a **"Show instrument"** checkbox for a
character's guitar/bass/drum kit; `instrument_variant_count` /
`instrument_variant_name` / `set_instrument_variant` offer an era/line choice
(Guitar Hero / Rock Band / World Tour…) for games that ship more than one.
Extras: a **fullscreen** view (model alone, V/Esc), and a high-resolution,
transparent-background **PNG capture** for wikis.

**In-pane song / stem player** (the "Songs" group). Backed by the module's
`gd_game_songs` export (`GdSongs*`). The launcher plays a **whole song** and
mutes/solos its parts live while it plays. `GdSongs` provides: `song_count` /
`song` (`GdSong` = id, title, artist, detail); `decode_preview` (short
preview); and the optional full-song set — `part_count` / `part` (`GdSongPart`
= name like "Guitar"/"Bass"/"Drums"/"Vocals"/"Song", `default_on`),
`play_song`, `read_song` (next chunk with the current part mask), `set_part`
(mute/unmute live), `stop_song`, `song_frames` (for a progress bar). The pane
shows transport (play/pause/stop), a progress bar, and a **checkbox per part**.
When a module has only `decode_preview`, the player falls back to a preview.

**"Hear the songs" screen** (`src/songs.cpp`, `Launcher::Songs`). A separate,
simpler screen that lists a module's songs and plays one as a short preview
through the launcher's own audio device (same `GdSongs` ABI, preview path).
The Explore "Songs" group is the richer full-song/stem experience; both use
the same module export, and the module does no window/GL work for either.

## Additive music menu

**What it is** (docs/guidelines/additive-and-custom-songs.md;
docs/spikes/study_additive_music_menu.md; GitHub #58, GDALL-042). Each game can
play the **songs of the other games the user has loaded**, plus **custom
songs** the user drops in (Clone Hero / YARG / Guitar Hero DLC / Rock Band
formats). Songs are read **directly from each original disc in memory** — no
extraction, no copying, nothing written to a disc, nothing downloaded. It is
universal to every game from now on. (Not compatible: gd-game-19 RB4, 20–23
DS titles, 24 RB Unplugged, 25–26 DJ Hero.)

**The launcher's Additive Music management tool** (`src/additive.cpp`,
`src/additive.h`, `Launcher::Additive`; GDUL-036). Reached from each game row's
**Additive musics** button.
- **Left**: a table of **playable games only**, columns Game name, Number of
  songs (counted via the game's `gd_game_songs`, `additiveCountSongs`), an
  **EXPOSE** checkbox and a **CUSTOM** checkbox.
  - **EXPOSE on** → this game's songs become available in other games, and
    other games' songs (that also have EXPOSE on) become available here.
  - **CUSTOM on** → this game loads every custom song from its custom-songs
    folder.
- **Right**: a folder-tree view of the selected game's custom-songs folder —
  create / rename / delete folders, drop songs, open in the OS file manager,
  and a right-click per-song menu (remove / quick-evaluate / rename / open).
  **Each song is its own folder**; organiser sub-folders are allowed.
- **Bottom**: Save, Back, Refresh; a loading overlay on open / save / evaluate.
- **Quick validation** (`classify`) tags each dropped song by format:
  `clone-hero` (CH, from `notes.chart`/`notes.mid` + `song.ini`), `yarg`,
  `rock-band` (RB, from `songs.dta`/CON/`.rba`/`.mogg`), `gh-dlc` (DLC, from
  `.fsb`/`.qb`), else `folder`/`invalid`/`unknown`. "Evaluate all" re-validates
  the tree.

**Where things live.** Custom-songs folders are under a `custom_songs/<game-id>`
root — next to the launcher when writable, else the save folder (Android or a
read-only install) (`customRootBase`). The tool's state persists to
`<save_dir>/additive_music.json` (`additiveWriteFile`): per-game EXPOSE/CUSTOM
flags plus cached song counts.

**The plumbing into a game** (`sdk/gd_host.h`, `module_host.cpp`,
`launcher.cpp`). Before a Play, `exposedDiscsFor(host)` computes which other
playable games' discs to share: only when the **host itself** has EXPOSE on,
each other game with EXPOSE on is added as a `(game_id, disc_path)` pair. These
are set via `setExtraDiscs` (single-use, cleared per module host) and handed to
the module as `GdHost.extra_discs` / `extra_disc_count` (ABI 1,
`GD_HOST_HAS(host, extra_disc_count)`). The host's own disc is never in the
list. On Android each extra disc is its own `/proc/self/fd/<n>`. Standalone
runs/walks can inject them with `--extra-disc <id>=<path>` /
`GD_EXTRA_DISCS`. A module built without the field simply sees no foreign
songs.

**Per-game stem behaviour** (docs/guidelines §4). When a foreign/custom song
plays in a host, each stem is either **played as a gameplay track** (the host
has that instrument) or **mixed into the backing** (it does not). The
play/mix matrix per game is tabulated in the guideline (e.g. GH1 plays guitar
only and mixes the rest; RB-era games play guitar/bass/drums/vocals; RB3 also
plays keys). Instruments the host has but the song lacks are left empty; a
no-stem song just plays its mix with no notes.

**Status / scope — be honest about what is deferred.**
- **Done (launcher side, Wave 1):** the UI refresh, the management tool
  (EXPOSE/CUSTOM flags, custom-songs folders, quick validation,
  `additive_music.json`), and the `extra_discs` ABI plumbing.
- **Deferred / in progress (engine side):** the shared in-memory song library
  (`gd::core::songs` — `Library`, index cache, `NeutralChart`, `StemStream`),
  the per-game readers consolidated into the core, and the **in-game "Custom
  songs" overlay menu** (search bar, by-folder listing, instrument icons)
  reached from quickplay. The study schedules these as Wave 2 (GH1+GH2+Encore)
  and Wave 3 (GH3, then Rock Band). Band animations for custom songs (random
  fallback; DLC-of-this-game uses the game's real mechanisms) are a later wave.
- Foreign songs are **quickplay only** (no career/store/practice in the first
  wave), play by the **host's rules** with the **source's HOPO marking**, use
  the host's venue/band/camera, and keep scores in a separate versioned JSON
  (`shared_songs.json`) that never counts toward career totals.
- The main list's **Mod** button is a disabled "coming soon" placeholder, and
  the header's **Dream Forge engine** button is not selectable.

## Onboarding notes

To light up each tool, a new game's module (`gd-game-NN.so`/`.dll`,
`sdk/gd_host.h`, built with the `gd_module_shim` so core-based games need no
window code of their own) should implement:
- **Play (required):** export `gd_game_run`. Read the disc from the **last
  argv** (Android: `/proc/self/fd/<n>`); use `save_dir`, `log`, `flags`.
- **game-info.yaml:** add the folder, register it in `games.yaml`, set `id`,
  `name`/`nick_name`, `based_on`, `guitar_dream_version`, `completeness`
  (drives the % icon), `ready_to_play`, and accurate `iso_signatures` (so
  `probeDisc` identifies the disc by content).
- **Explore:** export `gd_game_explore` returning a `GdExplorer`. List the
  disc's models under the standard groups (**Songs · Venues · Guitars · Bass ·
  Drums · Mics · Chars**), implement `select`/`draw`/`clip_*`, and optionally
  `has_instrument` / instrument variants.
- **Songs (Explore "Songs" group + "Hear the songs"):** export
  `gd_game_songs` returning a `GdSongs`. Implement at least `song_count` /
  `song` / `decode_preview`; for the full stem player also `part_count` /
  `part` / `play_song` / `read_song` / `set_part` / `stop_song` /
  `song_frames`.
- **Menu sounds (optional):** export `gd_game_ui_sounds` so the launcher plays
  the disc's own menu sounds.
- **Additive music:** read `GdHost.extra_discs` to offer the other games'
  songs, and expose the game as a source (it only needs a song reader in the
  core, not a finished port). Follow the play/mix stem matrix for the game.
- **Universal screens:** the module must also ship the shared GAMEPADS screen
  and Enhanced menu (see `docs/guidelines/universal_launcher.md` and
  `universal_mechanisms.md`); gd-game-01 is the reference.

**Hard rule — every build carries every ready module.** A game with
`ready_to_play: true` (in its `game-info.yaml`, listed in `games.yaml`) and a
module for a system must have that module in **every** platform build (Linux,
Steam Deck, Windows, macOS, Android). `scripts/verify_bundle.sh` runs on every
build and on every release archive, checking each ready game's module is
present, non-empty, exports `gd_game_run`, carries its required libraries and
`manifest.json` entry, and (Windows) resolves every DLL import. **If verify
fails, do not publish.** The launcher itself warns at start when its download
lacks a module its manifest lists.

## Sources

- `docs/guidelines/universal_launcher.md` — launcher rules, list order,
  "Never forget a package", GAMEPADS/Enhanced screens.
- `docs/guidelines/additive-and-custom-songs.md` — the additive-songs mod and
  custom songs (owner requirements, waves).
- `docs/spikes/study_additive_music_menu.md` — engine-side study: shared song
  library, `GdHost` discs, per-host behaviour, estimate.
- `gd-engine-launcher/sdk/gd_host.h` — the module ABI (`GdHost`, `GdExplorer`,
  `GdSongs`, `GdUiSounds`, `extra_discs`, `request_window`, flags).
- `gd-engine-launcher/src/main.cpp` — CLI flags, id normalisation, `--list`.
- `gd-engine-launcher/src/launcher.cpp` / `launcher.h` — rows/columns, actions,
  playability icon, header, right-click menu, disc dialogs, layout.
- `gd-engine-launcher/src/catalog.cpp` / `catalog.h` — games.yaml →
  game-info.yaml, disc scan, config.txt.
- `gd-engine-launcher/src/iso_probe.cpp` / `iso_probe.h` — content-based disc
  identification, ISO 9660 / XDVDFS / CHD.
- `gd-engine-launcher/src/module_host.cpp` / `module_host.h` — running a
  module, `setExtraDiscs`, window switching, Explore/Song sessions.
- `gd-engine-launcher/src/explorer.cpp` — the Explore screen, groups, viewer,
  in-pane stem player, capture.
- `gd-engine-launcher/src/songs.cpp` / `songs.h` — the "Hear the songs" screen.
- `gd-engine-launcher/src/additive.cpp` / `additive.h` — the Additive Music
  management tool, quick validation, `additive_music.json`, `exposedDiscsFor`.
- `gd-engine-launcher/src/update_ui.cpp`, `updater.cpp` / `updater.h` — the
  in-launcher updater (#114).
- `gd-engine-launcher/src/shortcut.cpp` / `shortcut.h` — "Add instant icon"
  desktop shortcuts (#141).
- `gd-engine-launcher/src/platform.cpp` / `platform.h` — save folders, Android
  SAF (`content://`, `openDocument`), macOS translocation.
- `gd-engine-launcher/games.yaml`, per-game `game-info.yaml`,
  `gd-engine-launcher/launcher-info.yaml`, `gd-engine-launcher/README.md`.
- `gd-engine-launcher/scripts/verify_bundle.sh`,
  `docs/platform/release-process.md` — the "every build carries every module"
  rule.
