# gd-game-03 decisions taken without the owner

Each entry: what, why, how to undo.

## D1. Work in the shared checkout, branch `feature/20261028-gd-games-03-04`
- What: no worktree, no own branch (the coordinator's override of kickoff section 3).
- Why: owner's instruction for this run; gd-game-04 and the Enhanced-menu work share the checkout.
- Undo: move the gd-game-03 commits to their own branch.

## D2. Start from gd-game-02's whole engine, not an empty skeleton
- What: `gd-game-03/engine` is a renamed copy (gd02 -> gd03) of gd-game-02's
  engine at commit 0d04efe, with the Xbox 360 layers replaced by gd-game-01's
  PS2 ones.
- Why: the disc is GH2's engine on PS2 (volume id `GH2`; `config/gen/gh2.dtb`;
  the same script files as GH2 360 minus the Xbox ones; milo directory version
  24 vs GH2 360's 25; ~30 classes one or a few revisions older). Emptying the
  game logic would throw away the GH2 rules this game runs on. Every rule
  still has to be re-confirmed in SLUS_215.86 (addresses 0x00xxxxxx); comments
  that cite 0x82xxxxxx are GH2 Xbox 360 addresses inherited from gd-game-02.
- Undo: none practical; this is the base of the module.

## D3. No FFmpeg in gd03
- What: the PSS movies use gd-game-01's own MPEG-2 / PSS decoder; sound
  effects are SPU ADPCM (gd-game-01's decoder). The FFmpeg build and the
  `GD03_VIDEO` option are dropped.
- Why: nothing on the PS2 disc needs FFmpeg (no WMV, no XMA); smaller packages
  and no LGPL shipping.
- Undo: copy gd-game-02's `build_common.sh` FFmpeg part back.

## D4. `game is_missing_multi_controller` is always false
- What: the main menu's MULTIPLAYER (and pause's) stay enabled with one controller.
- Why: the PS2 greys them out without a pad in port 2; the port assigns
  players to any device (keyboard, pads, touch) in GAMEPADS, like gd02's
  `is_missing_controller`.
- Undo: return whether `input` has fewer than two player devices (natives_game.cpp).

## D5. Goldens are this disc's first baselines
- What: the director (105 / 094b806a), band (353 / d8bf0829), crowd (453 /
  266c768b) and MIDI parser (2268 / 539bfa4f) goldens were regenerated from
  the port's own output on this disc; the facefx graph check keeps only the
  guitarist / _blinktrack case (byte-identical files, reference values from
  gd-game-02's research parser).
- Why: the old ones were GH2 360 songs and venues; no reference for this
  disc exists yet (OPEN_QUESTIONS Q3). They now guard against regressions.
- Undo: recapture from an emulator run and replace the constants.

## D6. gd03 is ready_to_play (completeness 0.5)
- What: game-info.yaml ready_to_play true, so the launcher builds carry it.
- Why: quick play, career, co-op / face-off / pro face-off, practice-free
  menus and every walk pass on the disc; missing pieces are listed in the
  backlog (movies, track fade, mono, rules not yet re-read in SLUS_215.86).
- Undo: set ready_to_play false.
