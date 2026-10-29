# Enhanced Options pairing: progress (GDALL-015)

The Enhanced-menu developer's log for pairing gd-game-02's Enhanced Options
with gd-game-01's Enhanced Settings. The parity table is in
`guidelines/universal_launcher.md` "Enhanced options: parity"; the backlog
rows are GD02-097..GD02-129.

## Batch 0: parity table (2026-10-28)

- Done: the parity table (44 gd01 rows: 6 had, 31 to implement plus the
  defaults table, 7 N/A) and the gd02-only rows; backlog GD02-097..GD02-129.
- Next: the settings model (typed defaults per platform, Steam Deck preset,
  export / load), the cheats, shaky camera, bokeh, particles; then the post
  chain; then lights, shadows, input, calibration, sharper text.

## Doubts for the owner

- **Defaults.** `guidelines/smilar_behaviors.md` says enhanced features
  default to the original, but gd-game-01's `profileValues` (the reference
  the brief asks to copy) ships bloom, cinematic colour, grain, AO... on for
  every desktop build. gd02 follows gd01's table; the owner may want gd02's
  defaults at the original instead (one table to change).
- **Backlog IDs.** GDALL-015 appears twice in `gd-backlog.md` (Shadow maps,
  and this pairing). Left as is; the owner may renumber one.
