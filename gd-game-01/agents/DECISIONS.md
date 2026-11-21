# gd-game-01 decisions taken without the owner

Each entry: what, why, how to undo.

## D1. The enhanced walk follows the Vulkan default; the code stays (GD01-327, 2026-11-17)
- What: tools/walks.sh's backend walks start from Vulkan (Right steps OpenGL3,
  then local; Restart Now goes to OpenGL3); the HDR walks pin graphics_backend
  local with settings_version 2.
- Why: the 5 FAILs were the walk expecting local as the default (GDALL-050 made
  Vulkan the Linux default). On Vulkan, True HDR picked in the menu correctly
  says it takes effect at the next start (the surface is made at start), and
  the gamescope check is about OpenGL's wording, so those walks run on local.
- Undo: revert the walks.sh commit (they fail again on Linux).

## D2. #98 tutorials: no gameplay change; a dev aid and two checks (GD01-322, 2026-11-17)
- What: GD01_TUT_STATE=<state> (development aid, TutorialPanel::enter) starts
  the picked tutorial at that lesson (as the original's next_tutorial_lesson
  cheat); walks tutorial_cartoons and tutorial_pulloff; --selftest's pull-off
  check on tutorial206.
- Why: the pull-off trainer (FUN_001576c0, re-read) counts the three runs
  (walk), and the song part's pull-offs all hit with Require Strum on (48 / 48)
  but none with it off (0 / 48): the reporter's failures match 1.1.15's d-pad
  bug that switched Require Strum off (GD01-326, fixed in 1.1.16). The cartoons
  draw on first play and on Restart (headless, default and max effects); the
  reporter's case is not reproduced, so nothing was changed for it.
- Undo: remove the env check in tutorial_panel.cpp and the two walks.
