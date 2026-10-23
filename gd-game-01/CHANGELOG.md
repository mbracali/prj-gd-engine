# Changelog: gd-game-01 (gd01)

All notable changes for players. Versions follow SemVer; release tags are
`gd-game-01/vX.Y.Z`. The release workflow publishes the section whose heading
matches the tag's version.

## [Unreleased]

## [1.0.0] - 2026-10-23

First release of Guitar Hero (PS2) on Guitar Dream Engine.

### Added
- Easy start: download, extract, run, then click the window (or press
  Enter) and pick your Guitar Hero (PS2) `.iso`. Dropping the file on the
  window still works, and the game remembers it.
- The Linux download runs on any distribution from 2022 on, with nothing
  to install (SDL3 is included).
- The Android app is signed with the project's release key, so later
  versions install as updates.

### Changed
- The program is now called `gd01` (Guitar Dream Engine, game 01); it was
  `gh1re`. Saves and settings move to the `gd-engine/gd01` folder and are
  copied from the old `gh1re` folder on the first start. On Android the app
  is now `io.github.mbracali.gd01` ("GD Engine 01") and installs beside the old one.
- Developer variables are `GD01_*`; the old `GH1RE_*` names still work for now.
- NVIDIA DLSS is no longer bundled with the source: `tools/fetch_dlss.sh`
  downloads it from NVIDIA. Builds without it fall back to FSR 3.1 / FSR 1.

### Also added
- The credits start with the Guitar Dream Engine attribution.
- Every download carries the licence (`LICENSE`, `LICENSE-ADDITIONAL-TERMS.md`)
  and the third-party licences.

### Already in the engine at import
- Career (6 tiers, store, unlocks), Quickplay, two-player, tutorials, 47 songs
  on the original highway, venues, band, crowd, camera and lighting from the
  user's disc.
- Original gameplay rules: hit window, hammer-ons and pull-offs (with an
  optional Forgiving mode), scoring, Star Power, rock meter.
- Enhanced settings: high resolutions and frame rates, AI upscaling (FSR 1,
  FSR 3.1, DLSS), post-processing (bloom, fog, colour grading, film grain,
  Star Power blur and more), controller mapping, guitar controllers, touch.
- Linux, Windows, macOS (untested) and Android builds.
