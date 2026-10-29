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
- **macOS performance.** gd01's table gives macOS 1440p and every desktop
  effect; gd02 already runs ~20 fps on an M4 Pro (GD02-062). The owner may
  want a lighter macOS column.
- **Band shadows.** gd01 projects each member's silhouette; gd02 draws a
  soft blob (no shadow pass yet, GD02-050). Labelled [I].
- **Forgiving HOPOs.** GH2 already ignores GH1's "strum first" rule, so the
  forgiving mode here means: no lock-out after a wrong hammer-on, and a
  HOPO whose fret is already held plays by itself. [I], the owner may want
  another rule.
- **Bloom.** GH2 has its own per-shot bloom (kept, as the original); the
  option adds gd01's glow on top, so "Off" is the original game's look.
- **Backlog IDs.** GDALL-015 appears twice in `gd-backlog.md` (Shadow maps,
  and this pairing). Left as is; the owner may renumber one.

## Batch 1: every pairable option (2026-10-28)

- Done: GD02-097..GD02-129, all ✅ (parity table: 6 had, 32 done counting
  the defaults table, 6 N/A plus FSR 3.1 / DLSS greyed inside AI
  Upscaling). The settings model (`ui::enhancedSettings`,
  `ui::enhancedProfile`: one table, desktop / Steam Deck / Android), the
  panel (gd01's texts, `needs` rows, unavailable dropdown options, status
  line, calibration overlay), the port's post chain on gd02's device
  (`render/enhanced_fx.*`, gd01's shaders), the venue extras
  (`render/enhanced_lights.*`), Sharper Text (`render/image_enhance.*`),
  touch lanes and buttons, calibration offsets, the cheats, export / load,
  Remove All Effects (GDALL-002, gd02's row).
- Tested: `--walktest enhanced` (every option toggled with the pad, saved,
  and its effect read through `UiHost::enhancedState`), `mouse` (+ Apply
  Steam Deck Settings), `cheats` (+ GH2's cheats from the menu),
  `gamepads`, `quickplay`, `career_song`, `--selftest`, `--venuetest`,
  `--shadertest` (GL and GLES, 601 programs), `build.sh` with 0 compiler
  warnings; screenshots of the backdrop and of a song (`--song yyz` now
  loads the Enhanced Options) with each effect group.
- Next: Windows build under Wine (the Enhanced menu renders), the package
  build, then polish.
- Not tested for real: touch on a touch screen, Rock Band guitars, Blur on
  Star Power (the walks never deploy Star Power; it is gd01's pass as is).
