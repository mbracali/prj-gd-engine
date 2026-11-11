# gd-game-05 fret board (the highway): element by element (wave 04)

The guitar, bass and drum tracks against the disc's track milos
(`track/trackpanel.milo`, its TrackDirs' proxy `track/tracksystem.milo`,
`track/gem_tail.milo`, `track/track_shared.milo`), `config/track_graphics.dta`
and the executable. Each element is **matches**, **differs** (what) or
**missing**, with how it was checked. [C data] = from the disc, [C 0x...] =
from the XEX, [I] = the port's choice (DECISIONS.md D23). Code:
`engine/src/render/rb_track_view.cpp`.

Screenshots looked at (wave 04, `--song <id> --autoplay --part <p>
--difficulty 3` with `GD05_SCREENSHOT` / `GD05_SCREENSHOT_AT`): Gimme
Shelter (guitar, bass, drums), Tom Sawyer (guitar, drums), (Don't Fear) The
Reaper and Maps (guitar), at several song times each. `GD05_TRACKLOG=1`
logs the track's camera, the now bar's screen place, the draw group's
materials, the smasher plate and the tails. `GD05_TAILTEST=1` gives every
guitar / bass gem a 600 ms tail (the autoplay lets go of the sustains it
hits, so it shows only the tails still coming).

| # | Element | Status | What / how checked |
|---|---|---|---|
| 1 | Track surface, rails, far end | matches | static.grp's flat_curve.mesh with surface.mat (black at 80%, track_reflect.tex scrolled by rig.grp at now_y) and curve_rails.mesh [C data]; the far end fades into the venue's own fog. Shots. |
| 2 | Camera per instrument | matches | the TrackDir's game.cam; the now bar at 0.905 of the view's height (log). Per player count: the panel layout of wave 02 [I]. |
| 3 | Gems (guitar / bass) | matches | each slot's widget (gem_green.wid ... from GUITAR_SET) with its meshes' own materials: the colours come from the meshes' coordinates in prism_gems.tex. Wave 01..03 swapped prism_mat for prism_mat_<colour>.mat (green gems drawn with prism_green.tex through the atlas' coordinates); RB swaps per slot only the tails' materials (0x82819A48, from the TailBuilder 0x8281A970) [C]. Shots. |
| 4 | HOPO, Overdrive (star), missed gems | matches | normal_hopo / star / star_hopo / miss widgets per slot [C data]; star gems white in shots. |
| 5 | Drum gems, kick | matches | DRUM_SET widgets [C data]; the pads where smasher_plate_drum's gem_smasher1..4 stand (x -7.5, -2.5, 2.5, 7.5) [C data], wave 01's -6 -2 2 6 [I] overlapped neighbouring gems; the kick bar across the track. Shots. |
| 6 | Smashers (strike line) | matches | the smasher plate's draw_order.grp with its inline RndDirs gem_smasher0..4 (they were not drawn at all), coloured as gem_smasher.dta's set_color does: particle_slot_colors.anim at the colour's index (glow.mat's black_smasher_<colour>_nomip.tex ...), smasher_glow.anim at 20 x it [C data]; guitar green .. orange, drums red, yellow, blue, green and the orange kick [I: the native caller of set_color]. Shots. |
| 7 | Sustain tails | matches | TailBuilder (0x8281A970 / 0x8281A8C8): tail02.mesh's row every 4.444 for up to 90 segments; rows keep the template's uv, a held tail's rows move sideways by its 300-sample wobble (written at tail_pulse_rate, amplitude / frequency by the smoothed whammy, 0x8281C928), the last row closes at the centre, the last tail_fade_distance fades (0x8281BC48 / 0x8281BCD8 / 0x8281C4A0) [C]; tail_<colour>.mat (additive), tail_miss when missed [C]. Wave 02 drew flat strips with a v ramp that sampled the texture's dark part (invisible). Shots with GD05_TAILTEST. |
| 8 | Tail glow line, star tail blend | missing | tail_glow_<colour>.mat line and the star tails' colour blend are not drawn [I] (GD05-029). |
| 9 | Beat lines | matches | bar_measure / bar_beat / bar_half_beat widgets [C data]. Shots. |
| 10 | Hit flames, sparks, smasher press, misses | matches (partly) | GD05-045: each gem_smasherN's own EventTriggers as gem_smasher.dta calls them: hit.trig / hit_bonus.trig in Overdrive (particle_trigger: its BandMeshLaunchers launch radial_flare / _slow, radial_debris, the shockwaves; hit_anims.grp: smasher_react.tnm; break.trig: glass and gem caps), burn.trig / burn_bonus / stop_burn.trig (burn_anims.anim's emit rates), miss.trig (miss.anim, miss_glow.anim), spotlight_phrase_success.trig on an Overdrive phrase's last gem; set_glow (smasher_glow.anim at base_frame + 19 while a fret is held, back to base_frame on release) [C data]. Which native sends each, and on which smashers, is [I] (render/rb_track_view.cpp fxEvents); the launchers' mid colour and emit direction are not applied. Shots (Gimme Shelter, autoplay): the flash, shockwave and shards at the smasher hit. The walk rb_guitar_play counts them (rb_fx_*). |
| 11 | Overdrive, solo, fill, BRE highway visuals | Overdrive matches (partly); solo / fill / BRE missing | GD05-045: gem_track.dta deploy / stop_deploy: star_deploy.trig (part_launcher: the spotlight_flare_*.part; spotlight_FX.grp, track_streak_glow.mnm, spotlight_track_light.mnm, success_flasher_deploy.anim ...; shows spotlight_draw.grp) and star_stop_deploy.trig [C data]; launch_part's count (one burst) is [I]. The solo / fill / BRE lanes (mash widgets) are not drawn. |
| 12 | The port's HUD over the smashers | differs | "OVERDRIVE READY" of the port's stand-in HUD sits on the strike line (row 28 of the ledger). |

## Counts

Of the 12 elements: 8 match, 2 match in part (the hit effects, the
Overdrive deploy: GD05-045), 1 differs (the stand-in HUD's notice), 1 is
missing (the tails' glow); the solo / fill / BRE lanes are still missing.
