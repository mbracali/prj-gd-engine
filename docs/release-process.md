# Release process

Each game is versioned and released on its own (SemVer). A release is made
**automatically when `main` gets a new version**; nothing else runs on GitHub
by itself (no builds on pull requests or branches, no bots).

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

## Android release key

The APK must always be signed with the same key, or phones cannot update it.
The key lives in four repository secrets: `ANDROID_KEYSTORE_B64` (the
keystore, base64), `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`,
`ANDROID_KEY_PASSWORD`. The owner's copy is in `~/.config/gd-engine/`
(`gd01-release.jks` and `gd01-release-key.txt`); keep an offline backup of both.

## Going public

1. Scan the whole history: `python3 .github/scripts/repo_guard.py` on `main`,
   and `gitleaks detect` over all commits.
2. Have `LICENSE-ADDITIONAL-TERMS.md` reviewed (an IP lawyer, recommended).
3. Settings > General > Change visibility > Public.
4. Settings > Branches: protect `main` and `develop` (pull request required,
   no force-push).
