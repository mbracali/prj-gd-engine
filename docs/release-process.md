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
     unpacked>`: a game module whose `guitar_dream_version` (game-info.yaml)
     and launcher module SDK (`sdk/`, `modules/gdNN/`) are the same as in that
     release's `manifest.json` is copied from it, the others are built;
   - publishes the release `gdul/vX.Y.Z` (tagged on the merge commit, marked
     Latest) with:
     - `gdul-<v>-linux-x86_64.tar.gz` (Ubuntu 22.04, SDL3 included:
       `gd-engine-launcher/scripts/build_linux_portable.sh`)
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
  the release workflow, on every archive (`.tar.gz`, `.zip`, `.apk`) before it
  is uploaded. For each game in `games.yaml` with `ready_to_play: true` and a
  module for that system (`module_systems_NN` in `build.sh`) it requires the
  module, non-empty, exporting `gd_game_run` (objdump / nm), its libraries
  (gd02: FFmpeg's avformat, avcodec, avutil and the LGPL licence; gd01 on
  Linux: the DLSS library), and the entry in `gd-engine-games/manifest.json`.
- The Windows job installs `gcc`. A failing job uploads `logs-<os>` (the
  launcher's `logs/` and FFmpeg's `ffmpeg-build.log`) as an artifact.
- The launcher says at start when the download lacks a module its manifest
  lists ("This download is incomplete ... please download again") and logs it.

**If verify fails.** Do not publish. Read the lines it prints (what is missing),
then the step log it points to: `gd-engine-launcher/logs/<os>-<game>.log`
(locally, or the run's `logs-<os>` artifact), and for FFmpeg
`gd-game-02/engine/build/ffmpeg/<os>/ffmpeg-build.log`. You can check any
download by hand: `gd-engine-launcher/scripts/verify_bundle.sh <file or folder>`.

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
