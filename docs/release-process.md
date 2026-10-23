# Release process

Each game is versioned and released on its own, with SemVer and a tag prefixed
by its id: `gd-game-01/v0.1.0`, `gd-game-01/v0.2.0-beta`.

## Cutting a release

1. On `develop`, move the `[Unreleased]` notes in `gd-game-NN/CHANGELOG.md`
   into a new section `## [X.Y.Z] - YYYY-MM-DD` (the release workflow takes the
   notes from exactly this heading), and bump `project(... VERSION X.Y.Z)` in
   the game's `engine/CMakeLists.txt`.
2. Open a PR `develop -> main` titled "Release gd-game-NN vX.Y.Z"; merge it when green.
3. Tag the merge commit on `main` and push the tag:
   ```sh
   git checkout main && git pull
   git tag -a gd-game-01/v0.1.0-alpha -m "gd-game-01 v0.1.0-alpha"
   git push origin gd-game-01/v0.1.0-alpha
   ```
4. The **gd-game-01 release** workflow builds every platform (with DLSS on
   Linux and on the MSVC Windows build), signs the Android APK with the release
   key, and creates a **draft** GitHub Release with:
   - `gd01-<v>-linux-x86_64.tar.gz`
   - `gd01-<v>-windows-x64.zip` (MSVC, with DLSS; when that build succeeds)
   - `gd01-<v>-windows-x64-mingw.zip` (always)
   - `gd01-<v>-macos.zip` (when that build succeeds)
   - `gd01-<v>-android.apk` (when the release key is configured)
   - `SHA256SUMS.txt`
   Tags with `-alpha`, `-beta` or `-rc` become pre-releases.
5. Download the draft's files, try them, then press **Publish release**.
6. Merge `main` back into `develop` if anything changed on `main` alone (hotfixes).

## Android release key

The APK must always be signed with the same key, or phones cannot update it.
The key lives in four repository secrets: `ANDROID_KEYSTORE_B64` (the
keystore, base64), `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`,
`ANDROID_KEY_PASSWORD`. Keep an offline backup of the keystore and passwords.
Without the secrets, releases are published without an APK.

## Going public (first public release)

1. Scan the whole history: `python3 .github/scripts/repo_guard.py` on `main`,
   and `gitleaks detect` over all commits.
2. Have `LICENSE-ADDITIONAL-TERMS.md` reviewed (an IP lawyer, recommended).
3. Cut the release as above and test the draft.
4. Settings > General > Change visibility > Public.
5. Settings > Branches: protect `main` and `develop` (PR required, CI green,
   no force-push). Settings > Code security: Dependabot alerts, secret scanning.
6. Publish the draft release.
