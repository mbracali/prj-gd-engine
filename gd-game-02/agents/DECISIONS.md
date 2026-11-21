# gd-game-02 decisions taken without the owner

Each entry: what, why, how to undo.

## D1. HDR and Cinematic Color never darken the stage (GD02-163, #115, 2026-11-17)
- What: Enhanced > HDR (Emulated / True HDR) tone maps with the identity below
  linear 0.8 and an exponential shoulder above (slope 1 at the knee, reaching
  1.0 or the display's peak) instead of ACES; its auto exposure only opens dark
  shots (0..+0.5 stop, was -0.5..+0.5). Cinematic Color's S-curve only lifts
  values above mid grey (max(c, curve)) and its vignette is 0.88 at the corners
  (was 0.7). The HUD and track were never touched (they are drawn after).
- Why: measured on yyz (--song, autoplay, frames at 12 s / 30 s, top half =
  the stage): original 19.1 / 21.5; HDR Emulated 1.1.16 crushed the dark
  stage (ACES's toe has slope 0.21 at black: top-left 7.8 -> 3.7), Cinematic
  -33 % there; HDR + Cinematic + SSAO Stronger 12.8 / 13.4 (-33 %). After:
  HDR 22.2 / 25.2, Cinematic 19.3 / 21.5, all three 20.6 / 23.6. There is no
  original to match (port-only options); the owner's rule is that they never
  look darker than the original.
- Undo: GD02_HDR_TONE_LEGACY=1 (ACES and the old exposure range),
  GD02_CINEMATIC_LEGACY=1 (the old curve and vignette).
- Not changed: SSAO darkens creases by design (Stronger alone: frame mean 34.8 -> 33.5).
