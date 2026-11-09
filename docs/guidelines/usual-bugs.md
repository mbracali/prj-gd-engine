# Usual bugs

Bugs that came back more than once. **They must never happen again**: check
for each of them before anything leaves the machine (owner, 1.1.13). Add a
bug here when it shows up a second time, with how to catch it.

| Bug | How it happened | How to catch it |
|---|---|---|
| The universal launcher released without a game module ("This launcher build has no gd-game-NN module") | A packaging step failed quietly (`set -e` swallowed by a subshell; gdul 0.1.1 shipped without gd02 on Windows) | `gd-engine-launcher/scripts/build.sh all` ends with `verify: ... complete (N game module(s) with their libraries)` for every platform; the release workflow runs `verify_bundle.sh`. Never ship a package without that line |
| The launcher cannot load ISOs, especially on Android and Windows | Path / handle differences: Android content URIs (SAF), Windows wide paths and drive letters. Windows (found 11-09): the launcher recognised a disc through SDL (UTF-8) but the games opened it with `std::fopen`, which reads the ANSI code page, so a disc under a user folder with an accent or with a non-ASCII name did not load | Load an ISO through **Load ISO** and from `original_isos/` on every platform before a release; the ISO probe self-test; on Linux, run `--iso` and a drop with a path holding spaces, accents and brackets. Open user paths with SDL_IOFromFile, or `_wfopen` of the UTF-8 path on Windows (each game's `RandomFile::open`), never a bare `fopen` / `std::ifstream` of a UTF-8 string on Windows |
| Black frames between screens (intro to menu, press-start, song end) | A render target made mid-frame unbound the frame's target; empty frames presented (gd04 GD04-087 / 089) | gd04's `probe:black` / `check:no_black` walk acts; look at the frame probe on new transitions |
| Menus off their highlights | Layout rules not matching the original's (GH3's VScrollingMenu, VMenu width) | Walks with `check:centrex` / `check:pos`; compare with the original's scripts, not by eye |
| A real controller plugged in breaks the self-tests' device counts | The tests count devices; a physical guitar adds one | Run the self-tests with `SDL_GAMECONTROLLER_IGNORE_DEVICES` / `SDL_JOYSTICK_IGNORE_DEVICES` set to the plugged device |
| A full-history secret scan stops a release on old docs | gitleaks scans every commit on a manual run; docs that name secrets look like keys | `.gitleaksignore` holds the reviewed false positives; never write values of secrets in docs |
