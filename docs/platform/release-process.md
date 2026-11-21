# Release process

The project publishes **one kind of release: the Guitar Dream Universal
Launcher (gdul)**, `gdul/vX.Y.Z`, always marked *Latest*. Each download packs
the launcher and the newest version of every ready game. Games are no longer
released on their own. A release is made **automatically when `main` gets a
new `launcher_version`**. Nothing else runs on GitHub by itself (no builds on
pull requests or branches, no bots).

## Versions

- The launcher: `launcher_version` in `gd-engine-launcher/launcher-info.yaml`.
- Each game: its engine version (`project(gdNN VERSION X.Y.Z)` in
  `gd-game-NN/engine/CMakeLists.txt`, or the newest `## [X.Y.Z]` of
  `gd-game-01/CHANGELOG.md`), copied into `guitar_dream_version` in its
  `game-info.yaml`. Keep the two the same, and the game's `CHANGELOG.md`
  up to date.

## Cutting a release

1. On your branch, bump the game versions that changed (engine and
   `game-info.yaml`), then bump `launcher_version`. A game version bumped
   without a launcher bump is not shipped (the release check warns about it).
2. Check it builds: `gd-engine-launcher/scripts/build.sh all` (Linux, Windows,
   Android here; macOS on a Mac).
3. Merge into `develop`, then `develop` into `main`.
4. The merge into `main` starts **gdul release**
   (`.github/workflows/gdul-release.yml`):
   - it checks `launcher_version`: already released means nothing more
     happens;
   - finds the previous `gdul/v*` release and downloads its files;
   - runs the repo guard and the secret scan, and builds each system with
     `gd-engine-launcher/scripts/build.sh <os> --previous <its download,
     unpacked>`: a game module whose build key (`buildkey.txt` next to the
     module, see "Release build speed" below) is the same as in that
     release's download is copied from it, the others are built;
   - publishes the release `gdul/vX.Y.Z` (tagged on the merge commit, marked
     Latest; its notes carry the `## X.Y.Z` section of
     `gd-engine-launcher/CHANGELOG.md`, so write the Highlights there first)
     with:
     - `gdul-<v>-linux-x86_64.AppImage`, `gdul-<v>-linux-x86_64.AppImage.zsync`
       and `recomplauncher.json` (#83, GDUL-032: the Linux download, the
       Linux build (Ubuntu 22.04, SDL3 included:
       `gd-engine-launcher/scripts/build_linux_portable.sh`) in one file,
       `gd-engine-launcher/scripts/build_appimage.sh`, with pinned
       appimagetool and runtime, SHA-256 checked, cached in
       `~/.cache/gd-engine/appimage/`). Its update information is
       `gh-releases-zsync|mbracali|prj-gd-engine|latest|gdul-*-x86_64.AppImage.zsync`,
       so AppImageUpdate and AppManager update it from the Latest release;
       the pattern matches the AppImage's name up to 1.1.15
       (`gdul-<v>-x86_64.AppImage`) and after, so an installed older
       AppImage still finds the newer one. Inside it the launcher looks for
       discs next to the `.AppImage` file (`$APPIMAGE`: beside it and in
       `original_isos/` there), saves in the usual XDG folder and loads the
       modules from the mount. `recomplauncher.json` is the one inside it,
       published beside it for RecompLauncher (which picks the file with
       the system and processor in its name: only the AppImage has
       "linux"). The site's Linux button matches "x86_64.AppImage", never
       the `.zsync`. Locally: `build.sh appimage` (or `linux appimage`)
       makes `local_builds/gdul-<v>-appimage-x86_64/`.
     - `gdul-<v>-portable-x86_64.tar.gz` (the same Linux build as a folder,
       `gdul-<v>-linux-x86_64/` inside; `gdul-<v>-linux-x86_64.tar.gz` up
       to 1.1.15, renamed so RecompLauncher never takes it for the Linux
       download; the site's "Linux .tar.gz" button takes either name)
     - `gdul-<v>-steamdeck-x86_64.tar.gz` (the Steam Deck download: built in
       Valve's Steam Runtime 3 "sniper" SDK container,
       `gd-engine-launcher/scripts/build_steamdeck.sh`, every module with
       its Steam Deck defaults, `STEAM-DECK.txt` inside; the site's Steam
       Deck button picks the asset with "steamdeck" in its name; no
       "linux" in it, so RecompLauncher does not take it for Linux)
     - `gdul-<v>-windows-x86_64.zip` (MinGW-w64)
     - `gdul-<v>-macos-arm64.zip` (when that build succeeds; ad-hoc signed)
     - `gdul-<v>-android-arm64.apk` (signed with the gdul release key)
     - `SHA256SUMS.txt`

   Versions with `-alpha`, `-beta` or `-rc` become pre-releases (not Latest).

Actions > gdul release > Run workflow does the same by hand on `main`.

The older per-game releases (`gd-game-01/v*`) stay on the Releases page as
history; no new ones are made.

## Building locally (same as the release)

```sh
gd-game-01/local_build.sh                 # the build for your system
gd-game-01/local_build.sh steamdeck       # or any of: linux steamdeck windows macos android all
gd-game-01/local_build.sh linux --native  # Linux without a container (uses your SDL3)
gd-engine-launcher/scripts/build.sh linux steamdeck windows android   # the launcher downloads (steamdeck: in the sniper container)
```

Or double-click the file for a build in `gd-game-01/build_commands/`
(`build-linux.sh`, `build-steamdeck.sh`, `build-windows.sh` / `.bat`,
`build-macos.command`, `build-android.sh`, `build-all.sh`).

Each build says what it is doing, and a build whose sources did not change
since its last package is skipped and reported "up to date (nothing to
build)" (`--force` builds it anyway). The run ends with a summary per build
(built / repackaged / up to date / FAILED), and each build's full output is in
`gd-game-01/local_builds/logs/<build>.log`.

The results land in `gd-game-01/local_builds/` (git-ignored): a ready-to-run
folder per platform and the same file a release would publish.

## Never again: incomplete bundles

**What happened.** gdul 0.1.1's Windows download shipped without gd02's module
(`gd-game-02.dll`, the FFmpeg DLLs and their licence), so the launcher said
"This launcher build has no gd-game-02 module". Two faults together:
- The Windows job's Fedora container had no host C compiler (`gcc`); FFmpeg's
  `configure` needs one even when it cross-compiles ("Host compiler lacks C11
  support"), so FFmpeg was never built (the cache missed every time).
- `scripts/build.sh` hid that failure: it ran each step as `step ... || return 1`,
  and bash ignores `set -e` in a function called in a condition and in
  everything it runs (subshells and background jobs too). The module step
  went on after the failed FFmpeg build and copy, and "succeeded" with the
  status of its last command.

**The checks now in place.**
- Each `build.sh` step runs in a fresh `bash -euo pipefail` (with
  `inherit_errexit`): any failing command fails the step and the build.
- `scripts/verify_bundle.sh` runs on every build `build.sh` assembles and, in
  the release workflow, on every archive (`.tar.gz`, `.zip`, `.apk`,
  `.AppImage`: also its update information and `.zsync`) before it
  is uploaded. For each game in `games.yaml` with `ready_to_play: true` (on
  every system, no exemption) it requires the
  module, non-empty, exporting `gd_game_run` (objdump / nm), its libraries
  (gd02: FFmpeg's avformat, avcodec, avutil and the LGPL licence; gd01 on
  Linux: the DLSS library), and the entry in `gd-engine-games/manifest.json`.
- The Windows job installs `gcc`. A failing job uploads `logs-<os>` (the
  launcher's `logs/` and FFmpeg's `ffmpeg-build.log`) as an artifact.
- The launcher says at start when the download lacks a ready game's module
  ("This download is incomplete ... please download again") and logs it.

**If verify fails.** Do not publish. Read the lines it prints (what is missing),
then the step log it points to: `gd-engine-launcher/logs/<os>-<game>.log`
(locally, or the run's `logs-<os>` artifact), and for FFmpeg
`gd-game-02/engine/build/ffmpeg/<os>/ffmpeg-build.log`. You can check any
download by hand: `gd-engine-launcher/scripts/verify_bundle.sh <file or folder>`.

Second case (gdul 0.1.2, Windows): `gd-game-02.dll` was there, but FFmpeg's
`avutil-59.dll` imports MinGW's `libwinpthread-1.dll`, which no download
carried, so Windows refused to load the game ("LoadLibrary failed (error
126)"). The check only looked at the module's own imports. Now:

- `verify_bundle.sh` follows the imports of **every** `.dll` / `.exe` in a
  Windows download: each must be in the download or one of Windows' own DLLs.
- `ship_winpthread` (gd-game-02/engine/build_common.sh) puts the DLL and its
  licence next to FFmpeg, for the launcher and for gd02's own download.
- A module is reused from the previous release only if the packaging
  scripts are unchanged too (they are part of its build key, GDREL-010).
- Before calling a Windows build good, load each module for real: Steam's
  Proton has a `wine` (`files/bin/wine`) that runs a 10-line
  `LoadLibraryExW` test program against the download.

Third case (gdul 0.1.5, Android): the APK had no gd01 module ("This launcher
build has no gd-game-01 module"). `build.sh` built gd01 for `linux windows
macos` only (`module_systems_01`: "gd01 has its own APK; in the launcher's APK
it is listed only"), and `verify_bundle.sh` skipped a ready game a system had
no recipe for, so the check passed. Now:

- **Every launcher, on every system, carries every ready game's module.**
  `module_systems_NN` lists all four systems; a ready game without a recipe
  or without the system fails the build ("every build must carry it").
  `verify_bundle.sh` has no exemption at all: each `ready_to_play` game's
  module is required on every system (macOS too), and in CI (`CI=true`) a
  module whose exports cannot be checked fails as well.
- gd01 on Android is built from the launcher's side (`modules/gd01`, OpenGL
  ES 3.0 and the upscaler stub, as gd01's own APK).
- The launcher's start-up check uses `games.yaml`: in any build (a
  `manifest.json` is there) a ready game without its module is an
  incomplete download, whatever the manifest lists.
- Android 15 could not open a disc image chosen with Load ISO: the launcher
  copied it into its own folder (7 GB for Guitar Hero II; the "Open folder"
  button pointed at that private folder, which no app can show: "Cannot open
  /data/data/io.github.mbracali.gdul/files/original_isos"), and a game cannot
  reopen the descriptor's `/proc/self/fd/<n>` path (FUSE: EACCES). Now the
  launcher keeps the document's `content://` URI (persistable permission),
  opens it with `ContentResolver.openFileDescriptor` when a game starts and
  hands the game `/proc/self/fd/<n>`; the module shim's `fopen` reads such a
  path through the descriptor (`pread`). Test it on the x86-64 emulator:
  `GDUL_ABI=x86_64 scripts/build.sh android`, `adb push` the discs to
  `/sdcard/Download`, Load ISO, start each game.

## Release build speed: caches and module reuse (GDREL-010)

Nearly all of a release job's time is the six game modules (1.1.16, every
module built, ccache still empty: Windows 33.5 of 35 min, Android 30 of 33,
Steam Deck 22 of 24, Linux 14.5 of 17, macOS 11.4 of 12; 3.5 to 6.5 min per
module on Windows). Three things keep work between releases; none of them
changes what a download contains.

- **ccache** (each job, `~/.ccache`, 1.5 GB, a new entry per run restored
  from the newest one): unchanged source files are not compiled again. The
  "ccache hits" line at the end of each job shows the rate.
- **Library caches** (`actions/cache`, in the folders the builds already use):
  the four FFmpeg builds (gd02, gd04, gd05 with its `_gd05` suffix, gd10;
  `gd-game-NN/engine/build/ffmpeg/<os>/ffmpeg-prefix`), gd-engine-core's
  pinned sources and its `gdcore_shaderc` host tool (glslang / SPIRV-Tools,
  built once per machine; `gd-engine-core/build/deps`), the FFmpeg tarball,
  Linux's SDL3 and Android's SDL3 source / Gradle. Keys: the platform, the
  job's toolchain (`TOOLCHAIN_ID`: compiler and package versions, the
  "Toolchain id" step) and the files that pin and configure each library.
  FFmpeg falls back to the newest entry of the same toolchain: a prefix is
  used only when its `FFMPEG-BUILD.txt` stamp (version, configure options) is
  the recipe's, else it is built again. gd-engine-core's sources are checked
  by their SHA-256 stamps as always.
- **Module reuse**: `build.sh` writes `gd-engine-games/gd-game-NN/buildkey.txt`
  next to every module (in the APK under `assets/`), the SHA-256 of
  everything that goes into it on that system (`module_build_key`): the
  game's folder (git's files, by content) except `game-info.yaml`,
  `CHANGELOG.md`, `agents/` and `docs/`; gd-engine-core (except `docs/`);
  the module SDK (`sdk/`), `modules/gdNN/`; `build.sh`,
  `build_linux_portable.sh`, `build_steamdeck.sh`; the system, processor
  and toolchain (C / C++ compiler versions, CMake, SDL3's headers, the
  flags, the NDK, Fedora's MinGW packages, the macOS SDK; gd01's DLSS
  library on Linux). The next release takes a module from the previous
  release's download only when that download has the same key for it, its
  manifest lists the module and every file the folder lists is there
  (`native-libs.txt`, `core-libs.txt`, `root-files.txt`); the game's
  `game-info.yaml` (its new version) is copied fresh and the manifest says
  `"reused": true`. A version bump alone reuses the module (versions are not
  compiled in). Anything else builds it: no key (1.1.16 and older), a
  different key, an unreadable previous download, a missing file, `--force`.
  The line "gd-game-NN: building (why)" says which; the key's inputs are in
  `gd-engine-launcher/logs/<os>-gd-game-NN.buildkey.txt`.
  `verify_bundle.sh` still checks every download in full, and now also that
  the manifest's version of each game is its `game-info.yaml`'s and that a
  `buildkey.txt` is a SHA-256.

What to expect: the first release with this (1.1.17) has nothing to reuse
(1.1.16 has no keys) and fills the library caches; it is the first run with
a warm ccache. From the release after on, each game whose key did not change
costs seconds instead of its minutes. A change to gd-engine-core, the SDK,
`build.sh` or a compiler update changes every key: that release builds every
module (with ccache). In 1.1.14 .. 1.1.16 every game and the core / SDK /
recipes changed each time, so reuse pays off when a release leaves games
untouched; ccache and the library caches help every release.

## Test builds on GitHub (by hand)

Actions > **gd-game-01 build** (or **gd-game-02 build**) > Run workflow, on any branch: builds every
platform and keeps the files 3 days in the run. Costs Actions minutes, so only
when you need a build you cannot make locally (macOS, for example).

## Keeping the actions current (by hand)

Actions > **check action updates** > Run workflow (or
`python3 .github/scripts/check_action_updates.py`) lists actions with a newer
major version. Update the `uses:` lines by hand. No Dependabot.

## Android release keys

The APK must always be signed with the same key, or phones cannot update it.
The key lives in four repository secrets: `ANDROID_KEYSTORE_B64` (the
keystore, base64), `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`,
`ANDROID_KEY_PASSWORD`. The owner's copy is in `~/.config/gd-engine/`
(`gd01-release.jks` and `gd01-release-key.txt`); keep an offline backup of both.

The launcher's APK (`io.github.mbracali.gdul`) has its own key, in four more
secrets: `ANDROID_GDUL_KEYSTORE_B64` (the keystore, base64),
`ANDROID_GDUL_KEYSTORE_PASSWORD`, `ANDROID_GDUL_KEY_ALIAS`,
`ANDROID_GDUL_KEY_PASSWORD`. Without them the release APK is signed with a
throwaway debug key (a warning in the run), which phones cannot update
later, so create the key before the first gdul release, for example:

```sh
keytool -genkeypair -keystore gdul-release.jks -alias gdul -keyalg RSA -keysize 4096 -validity 10000 \
  -dname "CN=Guitar Dream Universal Launcher"
base64 -w0 gdul-release.jks   # the value of ANDROID_GDUL_KEYSTORE_B64
```

and keep `gdul-release.jks` with its passwords outside the repo (e.g. next
to gd01's in `~/.config/gd-engine/`), with an offline backup. Locally,
`scripts/build.sh android` signs with the Android debug key, or with
`GDUL_RELEASE_KEYSTORE` (+ `GDUL_RELEASE_KEYSTORE_PASSWORD`,
`GDUL_RELEASE_KEY_ALIAS`, `GDUL_RELEASE_KEY_PASSWORD`).

## Going public

1. Scan the whole history: `python3 .github/scripts/repo_guard.py` on `main`,
   and `gitleaks detect` over all commits.
2. Have `LICENSE-ADDITIONAL-TERMS.md` reviewed (an IP lawyer, recommended).
3. Settings > General > Change visibility > Public.
4. Settings > Branches: protect `main` and `develop` (pull request required,
   no force-push).
