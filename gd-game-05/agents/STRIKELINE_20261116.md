# Strike line (now bar) study, 2026-11-16

The owner's report on the current build: "The higher part of the notes is wrong ... where we catch the notes by pressing a button and strumming". This study compares the port's strike line with the disc data and with Rock Band 1 reference pictures. It ends at the study: no code was changed. The one real candidate (item 1) needs the XEX before it can be fixed without guessing.

## How it was checked

- Port: `engine/build/bl05/gd05` (built 11-16 09:59), run from gd-game-05/ with `--song inbloom --part guitar|drum --difficulty 3`. Two kinds of run: one on autoplay at 20.0 / 20.15 / 20.3 / 30 s; one with `--no-fail` and no autoplay, so the gems pass the line un-hit, taking `GD05_SCREENSHOT_AT` every 16 ms from 20000 to 20496 (19 guitar and 21 drum frames). Gem times come from inbloom.mid: PART GUITAR expert has green at 19991 / 20184 / 20378 ms, and PART DRUMS has yellow at 19991 and 20378.
- Disc: `track/gen/tracksystem.milo`, `smasher_plate_guitar/_drum.milo`, `gem_smasher_guitar.milo` and `track_shared.milo`, unpacked with `tools/milo.py`. Mesh vertices, transforms, TransAnim keys and PropAnim keys were read from the records; the textures were decoded (big-endian header, DXT1).
- References:
  - Wikipedia, File:Rock-band-screen.jpg (retail, 350 px, full band).
  - Worthplaying press shots of 2007-11-06 (`/wpimages/r/o/rockband/175343.jpg`, `175344.jpg`, 1079 px).
  - outcyders.net press shots `/images/screenshots/523/rockband_01..05.jpg` (3780 x 2100, "(c) 2007 Harmonix") and `Screen_2.jpg` (1280 px, single-player bass).
  - The scratch crops are in the agent's scratchpad and were not committed: they are decoded game pictures.
- Pixel rows below are for 1920 x 1080. The track's own y_per_second here is about 95 units/s (configure(): top_y 105 / view_time). The gems move 0.87 px/ms at the line, which is 9.2 px per track unit in y. Across the track it is 33 px per unit. Height (z) is about 31.7 px per unit, because the line of sight meets the track at about 16 degrees.

## What matches the data (measured)

| Check | Data | Port, measured |
|---|---|---|
| The now line (y = 0) | smasher window centred on y = 0 (the rim box is v 0.25..0.75 of black_smasher_*_nomip.tex, mesh y -1.04..1.16) | green rim rows 883..903 (centre 893), now line 896 (0.83 of the view) |
| Smasher shape, yellow lane (x = 955) | hood y 1.46..2.5, z 0.42; rim y -1.04..1.16; front to y -3.18 | hood 855..871 (predicted 858..875), rim 873..895 (predicted 875..894), front to about 920 (predicted 920) |
| Gem placement in time | a gem's base at y = (t_gem - now) x y_per_second | the green gem's footprint crosses the now line at its chart time ±1 frame (19991 / 20184 / 20378) |
| Draw order | smasher_plate.dta setup_draworder puts every base_bottom.grp and base_top.grp in before_gems.grp; widgets (tails, gems) next, smasher_fx.grp last (D61) | the same; glow.mat has z mode 0 and cull 1, so the gem draws over the smasher, as in the data |
| Smasher material | glow.mat: black_smasher_<colour>_nomip.tex (black, dark coloured rim, grey end strips) + black_smasher_norm.tex; smasher_glow.anim swaps emissive_map to square_smasher_bright_<colour>.tex while a fret is held | black body, coloured rim, bright window while held |
| Hit bounce | smasher_react.tnm: z 0.7 -> 0 and a ±0.026 roll over 7 frames (rest frame 7 is identity) | played by hit.trig / hit_anims.grp (D40) |

The silver smashers and grey windows in the 3780 px press shots and the Worthplaying shots come from a pre-release look. square_smasher_bright*.tex and glow_2nd_pass.mat are still on the disc, but no mesh draws glow_2nd_pass.mat. Retail's visible mesh is square_smasher_plate.mesh with glow.mat (black). Screen_2 shows the black smasher with coloured rims that the port draws.

## Differences

1. **The outer lanes' gems float above their smashers (candidate, [I]).** The track is curved:
   - flat_curve.mesh: z = -0.0044·x² (-0.048 at x 3.28, -0.191 at 6.56, -0.43 at 9.84).
   - beat.mesh / measure.mesh and the kick gem have the curve built into the mesh.
   - The smashers follow the curve through their orientation TransAnims. Guitar: gem_smasher0/4 at z -0.281 with a ±0.036 roll (4.1°), and 1/3 at z -0.075 with ±0.022. Drums: z -0.237 at ±7.5 with a ±0.036 roll, and -0.035 at ±2.5.

   The port draws every gem at z = 0 with no roll (RbTrackView::drawGems / drawWidget). On screen:
   - The green and orange gems land 0.28 units, about 9 px, higher on their smasher than the yellow gem does on its own.
   - The gems' outer ends sit another 0.14 units, about 4.5 px, higher, because they are not rolled.
   - Red / blue: 0.075 units, about 2.4 px. Drum outer pads: 0.24 units, about 7.5 px.

   Measured at hit time, the green gem's highlight is 17 px above its smasher window's centre. The model gives 8-9 px from the gem's height plus 9 px from the curve, which matches. The gem meshes themselves are flat (z -0.03..1.04 at both ends, every colour), so something has to place them on the curve. Neither the TrackDir slots (x, 0) nor any property carries the curve, and the RB code that builds a gem instance's transform was not traced. Do not fix this from the inference: confirm it in the XEX first (see Left).
2. **A gem is drawn above its smasher at hit time, even in the middle lane (data, not a bug).** The gem is 1.04-1.12 units tall (drums 1.46) and stands on y = 0. At the 16° line of sight its body projects 8-10 px (drums 11-12 px) above the window's centre when its footprint is centred. The data gives this with RB's camera (track_configuration 1_player_wide, highway.md row 2), so the port should keep it.
3. **The hood above the window looks taller than in Screen_2 (not a port bug, as far as the data shows).**
   - In Screen_2, the band above the coloured rim is 0.27 of the rim's height and the front below is 1.5.
   - In the port it is 0.75 and 0.8. That is what the retail mesh and camera give (the rows in the table above, to within 3 px).
   - The track's projection in Screen_2 matches the port to within 3 %: strike-line width 0.379 vs 0.385 of the screen, top / bottom width 0.43 vs 0.42, now line 0.81 vs 0.83. So the hood difference comes from that build's smasher model, not from the camera. Its HUD (no meter under the track) and the silver press shots suggest a pre-release build.

## Not fixed

None of the differences is both confirmed and small. Item 1 changes where every outer gem is drawn. "Match the original exactly" rules out doing that from an inference, so it waits for the XEX check below.

## Left (for an RE or build agent)

- XEX: find where RB builds a gem widget instance's transform: the gem manager's add-instance, near the track code at RB 0x8281xxxx (TailBuilder 0x8281A970; gem_smasher%d at 0x82817D28). Check whether z / roll come from the slot x on the track's curve, or whether gems stay at z = 0 (then the port is right and item 1 is RB's own look). Do the same for the tails (0x8281C4A0's row z).
- If confirmed: in drawGems, offset z by the curve at the slot's x (the smasher TransAnims' z: guitar 0 / -0.075 / -0.281, drums -0.035 / -0.237) and roll by its tangent. Put the old flat placement behind `GD05_GEMCURVE_LEGACY=1`. Check: gem centre minus smasher centre at hit time is the same in every lane (today green 17 px measured; yellow about 8 px predicted, not measured: inbloom has no yellow guitar gem near 20 s).
- Ask the owner which part he means, showing the guitar crops (gem at the line in the green lane vs the yellow lane) next to Screen_2's black smashers.

## Follow-up (same day, GD05-111, D115)

Difference 3 was a port bug after all. square_smasher_plate.mesh names
black_smasher.mesh as its geom_owner, and RB draws a mesh's geometry
through the owner [C]. The port drew the mesh's own 471 vertices (the
taller hood measured above), so the "matches the mesh" rows describe the
wrong mesh. Now fixed (GD05_GEOMOWNER_LEGACY=1 for the old look). Item 1
(the gems on the curve) is still open.

## Follow-up 2 (same day, D129)

Item 1 confirmed in the XEX and fixed: RB's gem manager sets each TrackDir
slot to its smasher's world (x, z) and the smasher's roll (RB 0x828167F0),
and builds every gem from that slot (0x822509D8 turned by the roll,
0x82818B68). GD05_GEMCURVE_LEGACY=1 for the flat gems. Tails still flat.

