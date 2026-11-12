# gd-game-05 wave G06-WAVE1: the owner's requirements (2026-11-12)

Branch `feature/20261110-G06-WAVE1` (from develop). Goal: at least 80 % matching
the original Rock Band; 100 % matching is the target. Rock Band 1 is the
scaffold for later games: organised, clean, reusable code. Ghidra / the original
game may be analysed again as needed. Backlog rows GD05-066 onwards.

## 1. Band visuals and glitches
- Guitar / bass hands look wrong vs the original; arms sometimes stretch like
  bubble gum.
- The drummer does not move at all.
- The singer: lips don't move, odd movement, visual bugs with the microphone.
- Overall: overhaul the band's movement, behaviour and features.

## 2. Venues and general visuals
- Everything is overbright (similar to an earlier bug), far from the original.
- The original is darker with a stronger film grain: check the game's filters.
- Pause: the background stays too bright.
- FPS: the original's highway runs at 60 fps but the band seems to run at a
  different rate. Investigate; if relevant, add a backlog item (maybe separate
  FPS options for highway and band later).

## 3. Notes highway
- The yellow Overdrive bar below the notes flickers (it should glow steadily).
- The multiplier count is drawn on top of other elements.
- The hit area (now bar / smashers) looks wrong: check the draw order.
- Overdrive (white-ish) colour must apply to sustained notes' tails too, not only
  the gems.

## 4. Crowd
- No crowd visible in any venue: add it as the original.

## 5. Career mode, the shop and character creation
- All must work by the end of the wave.

## 6. General bugs and enhancements
- Loads take too long.
- Song list: the yellow selector behind the rows highlights more than one song.
- Micro-stutters when a song preview starts and when a song starts.
- Highway 60 fps vs the band's rate: verify.
- Quickplay: clicking a random song selects songs that are not unlocked (wrong).
- Overdrive activation may not work: double check and fix.
- Quick pass over visual FX we might have missed.

## 7. Backend
- Rock Band is the first game deployed using only the shared core
  (gd-engine-core) components, Vulkan by default. Make the needed changes without
  breaking the other games that use the core.

## 8. Expectations
- Every point done; ≥ 80 % matching; the same mistakes not repeated; tight
  deadline.
