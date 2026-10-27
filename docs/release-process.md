# Release process

Each game is versioned and released on its own (SemVer). A release is made
**automatically when `main` gets a new version**. The launcher (gdul) is
released when a `gdul/vX.Y.Z` tag is pushed (see below). Nothing else runs on
GitHub by itself (no builds on pull requests or branches, no bots).

## Cutting a release

1. On your branch, bump the version in `gd-game-NN/engine/CMakeLists.txt`
   (`project(gd01 VERSION X.Y.Z ...)`) and add the notes to
   `gd-game-NN/CHANGELOG.md` under a heading `## [X.Y.Z] - YYYY-MM-DD`
   (the release page shows exactly this section).
2. Check it builds: `gd-game-01/local_build.sh` (see below).
3. Merge into `develop`, then `develop` into `main`.
4. The merge into `main` starts **gd-game-01 release**:
   - it checks the version: already released means nothing more happens;
   - the repo guard and the secret scan run, and every platform builds;
   - the release `gd-game-01/vX.Y.Z` is published with how-to-play notes, the
     CHANGELOG section and:
     - `gd01-<v>-windows-x64.zip`
     - `gd01-<v>-linux-x86_64.tar.gz` (built on Ubuntu 22.04, SDL3 and DLSS included)
     - `gd01-<v>-steamdeck.tar.gz` (built in Valve's Steam Runtime 3 SDK, handheld defaults)
     - `gd01-<v>-macos.zip` (when that build succeeds)
     - `gd01-<v>-android.apk` (arm64, signed with the release key)
     - `SHA256SUMS.txt`

   Versions with `-alpha`, `-beta` or `-rc` become pre-releases.

A merge into `main` without a version bump publishes nothing.

## Releasing the launcher (gdul)

The Guitar Dream Universal Launcher has its own version
(`gd-engine-launcher/launcher-info.yaml`, `launcher_version`) and its own
release, made **only when a tag `gdul/vX.Y.Z` is pushed** (merges into
`main` alone never release it; `.github/workflows/gdul-release.yml`):

1. On your branch, bump `launcher_version` in
   `gd-engine-launcher/launcher-info.yaml`; check it builds:
   `gd-engine-launcher/scripts/build.sh all` (Linux, Windows, Android here;
   macOS on a Mac).
2. Merge into `develop`, then `develop` into `main`.
3. Tag the merge on `main` and push the tag:

   ```sh
   git checkout main && git pull
   git tag gdul/v0.2.0 && git push origin gdul/v0.2.0
   ```

4. **gdul release** then:
   - checks that the tag's version is `launcher_version`, that the tagged
     commit is on `main` and that the version is not released yet (otherwise
     it stops: delete the tag, fix, tag again);
   - finds the previous `gdul/v*` release and downloads its files;
   - runs the repo guard and the secret scan, and builds each system with
     `gd-engine-launcher/scripts/build.sh <os> --previous <its download,
     unpacked>`: a game module whose `guitar_dream_version` (game-info.yaml)
     and launcher module SDK (`sdk/`, `modules/gdNN/`) are the same as in that
     release's `manifest.json` is copied from it, the others are built;
   - publishes the release `gdul/vX.Y.Z` with:
     - `gdul-<v>-linux-x86_64.tar.gz` (Ubuntu 22.04, SDL3 included:
       `gd-engine-launcher/scripts/build_linux_portable.sh`)
     - `gdul-<v>-windows-x86_64.zip` (MinGW-w64)
     - `gdul-<v>-macos-arm64.zip` (when that build succeeds; ad-hoc signed)
     - `gdul-<v>-android-arm64.apk` (signed with the gdul release key)
     - `SHA256SUMS.txt`

   Versions with `-alpha`, `-beta` or `-rc` become pre-releases.

The games' own releases (below) stay: each game is still published on its
own (`gd-game-NN/vX.Y.Z`, standalone programs) when `main` gets a new game
version. The launcher picks up a new game version at its next release (the
module is rebuilt because the version changed).

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

## Test builds on GitHub (by hand)

Actions > **gd-game-01 build** > Run workflow, on any branch: builds every
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
