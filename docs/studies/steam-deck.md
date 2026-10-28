# Steam Deck: bring the dedicated build back for the launcher?

Status: study, 2026-10-28.

## Verdict

**Do not bring back a separate Steam Deck download. Make the normal Linux
launcher Deck-aware at run time instead.** That is about 1 to 2 days of work and
costs nothing more in Actions. A Flathub listing is a reasonable later step. The
Steam store is not.

## Why the Deck build went away (repo history)

- gd01 (from 1.1.0) and gd02 (M0, `354f238`) each had a `steamdeck` job.
  `.github/workflows/gd-game-0N-build.yml` built in
  `registry.gitlab.steamos.cloud/steamrt/sniper/sdk` with gcc-14, a static
  libstdc++ and `-DGD0N_STEAM_DECK=ON` (handheld defaults: 60 FPS, 800p, full
  screen, lighter effects). It also shipped `STEAM-DECK.txt` and found an `.iso`
  placed next to the program.
- Commit `d21904a` ("Launcher-only releases…", 2026-10-27) deleted
  `gd-game-01-release.yml` and `gd-game-02-release.yml`, which carried the
  `gdNN-<v>-steamdeck.tar.gz` rows. Since then `gdul-release.yml` is the only
  release. It builds Linux x86_64 (ubuntu:22.04), Windows, macOS arm64 and
  Android, with **no Steam Deck job**. No commit says the Deck was dropped on
  purpose. It fell away when releases were consolidated: the launcher had no
  sniper build script, and every extra job costs Actions minutes
  (see `github-actions-budget`).
- The per-game `build_linux_steamdeck.sh` scripts and the `steamdeck` jobs in the
  manual build workflows still exist. They are not used for releases.

## What the Deck actually needs today

| Need | Status in gdul | Fix |
|---|---|---|
| Binary runs on SteamOS 3 | The Linux build targets glibc 2.35 (Ubuntu 22.04) and ships SDL3. SteamOS 3 is newer, so it runs on the host. Non-Steam shortcuts run on the host unless a compat tool is picked. | Nothing to change. Optional: test once under "Steam Linux Runtime 3.0 (sniper)". |
| Controls in Gaming Mode | **Done in gdul 0.1.4** (the variable and the 80 ms filter). Was a gap: gd01 and gd02 set `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1` before SDL starts its gamepad code (reuse_and_lessons L9). `gd-engine-launcher/src` never sets it. Steam hides the real pad, so the launcher list may see no controller. | Set the variable in `main.cpp` before `SDL_Init`, and move gd01's 80 ms duplicate-press filter (`kSameNs`) into the launcher. |
| Handheld defaults | These used to be a compile flag in a separate build. | Detect the Deck at run time instead: `SteamDeck=1` in the environment, or DMI `board_name` of `Jupiter` (LCD) or `Galileo` (OLED). Pass a `handheld` profile to modules through `gd_host.h`, so they apply their Steam Deck settings once and lock nothing. |
| Gaming Mode window | The game needs to be full screen at 1280x800 and must not open a file dialog it can't handle. | Use full screen by default on the Deck. Gaming Mode has no portal file picker, so rely on the `original_isos/` auto-discovery that already exists. |
| On-screen keyboard (band name, search) | Not handled. | Call `SDL_StartTextInput` (Steam shows its keyboard when the user presses Steam+X), or open it directly with `steam://open/keyboard`. |
| Adding the game to Steam | Done by hand. | Bring back `STEAM-DECK.txt` as a section of the user manual. Optionally add an "Add to Steam" button that runs `steam steam://addnonsteamgame/<path>`. |

## Distribution options

- **tar.gz plus a non-Steam shortcut** (current Linux download). Costs nothing.
  This is the recommended route.
- **Flatpak (Flathub).** Gives a Discover store install in Desktop Mode, and the
  game then appears in Game Mode once added to Steam. Flathub accepts apps with
  non-free licences (PolyForm NC plus our additional terms). Costs: a
  manifest, sandbox permissions (`--filesystem` for ISO folders, `--device=all`
  for guitars and HID), and building through Flathub's own CI (no Actions
  cost). It needs review. Plan on about 3 to 5 days.
- **Steam store.** This means Steamworks plus a $100 app fee. Valve blocked
  Dolphin's Steam release in 2023 after Nintendo objected, so a front end for
  Guitar Hero discs (Activision IP) is a takedown risk. It would also make Steam
  part of our release process. **Not recommended.**
- **Separate sniper build again.** This adds an Actions job (around 10 to 15
  minutes per release). It only buys ABI safety, which the Ubuntu 22.04 build
  already provides. **Not needed.**

## Concrete steps

1. (Done in gdul 0.1.4, in `launcher.cpp` before `SDL_Init`.) In `launcher/main.cpp`, set `SDL_setenv_unsafe("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD","1",0)` before `SDL_Init`. Move the duplicate-press filter into the launcher's input code.
2. Add `platform::isSteamDeck()` (environment variable plus DMI) and a `handheld` flag in `gd_host.h`. Modules map it to their existing Steam Deck profile.
3. Add a "Steam Deck" section to `manual/user-manual.md` and a row to the release notes ("use the Linux download; Add a Non-Steam Game").
4. Test on a Deck from Game Mode before each release (L9 rule).
5. Later, if wanted: a Flathub manifest `io.github.mbracali.gdul`.

## Sources

- Repo: `git show d21904a`, `.github/workflows/gd-game-01-build.yml` (job `steamdeck`), `gd-game-02/docs/reuse_and_lessons.md` L9, `gd-game-01/CHANGELOG.md` 1.1.0/1.1.1.
- Steam Runtime sniper: https://gitlab.steamos.cloud/steamrt/sniper/sdk
- Flathub requirements: https://docs.flathub.org/docs/for-app-authors/requirements
- Valve and Dolphin (2023): https://dolphin-emu.org/blog/2023/05/27/dolphin-steam-indefinitely-postponed/
