# 0.1.12 plan: Guitar Hero II to 100% (owner, 2026-11-03)

Branch `feature/20261103-0.1.12` (from develop). No PRs or merges: the owner triggers them.
Estimate given: 2.5–3 weeks.

## Items (backlog rows)
1. Band animation: pose, hands / fret position, hair, look-ats, drum hits (GD02-050)
2. Beams' buffer gain and scene-depth softness (GD02-151, from 049 / 091)
3. Bass hit / miss path (GD02-036)
4. Note presses read once per frame, as the 360 (GD02-152, from 037)
5. Developer keyboard cheats (GD02-045)
6. Main menu word spacing final check (GD02-041)
7. Song list font and help bar in transitions final check (GD02-153)
8. Achievements / leaderboards / DLC: dropped (GD02-066 ➖)
9. Broken walks career_encore / career_win / career_win_medium / lose (GD02-150)
10. No PS2 / Xbox 360 labels in menus or loading screens (GD02-154)
11. Loading screens hold and animate (GD02-155)
Owner-test rows GD02-060/061/062/143/144/145 marked done.

## Owner's decisions
- Labels: same as GH1 (`gd-game-01/engine/src/ui/pc_strings.cpp`): GD-Engine save
  messages, Xbox Live loading tips replaced by GD-Engine dev tips, "guitar controller
  / gamepad" instead of Xbox 360 controller.
- Loading / saving screens hold at least 2 s with their animation, as GH1.
- Dev cheats (quick_cheats): implemented and listed in the Enhanced menu's cheats,
  labelled as developer cheats.
- Band animation: one background research agent at a time (decomp tracing) while
  I implement; no other agents.

## Progress
(update as items finish)
