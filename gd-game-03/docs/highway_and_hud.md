# gd03: highway and HUD

The PS2 track is one PanelDir, `track/track.milo` (ui/track_panel.dta's
TrackPanel), not GH2 360's track_panel_1p / GHTrackDir. Ported from
SLUS_215.86 (render/track_view.cpp; addresses there) [C unless marked]:
- Panel init 0x0014faa8: track.cam; the track group lit by track.env draws
  track_surface5, track_lane_lines5 (0x00154c08), the beat lines,
  track_side_rails5, then the gems (tails first).
- Draw 0x001511c0: the track, then the far-end fade over it (track_fade.trans,
  track_fade.mat / track_add.mat) - NOT DONE (needs a frame copy), then the
  smashers, hit effects and lightning_track.view.
- y = ms x 0.06 x track_speed (0x001524c8); track_speed = the players'
  difficulty values averaged, x 1.5 with Hyper Speed (0x00153a48).
- Slot x = (slot - 2) x 4; lefty slot 4 - s (0x00152530 / 0x00152500).
- Surface: track_surface.filt at frame now_y (0x00151670 -> 0x00155470);
  track_anims.view at now x 0.03 (0x00153ea0).
- Gems (template 0x00159e28; looks 0x00158238 / 0x00158858; miss 0x00158c80):
  normal top/`<c>_hopo` + `<c>_gem` + glow; Star Power bonus blue_hopo +
  gem_bonus + gem_bonus2; star phrase star_base + `<c>_star` + star2 +
  top_star_black / `<c>_top_star`; misses swap to miss / star_miss. HOPO look:
  chart flag, not the first gem, previous gem this player's (0x001562e8).
  Star spin from star.tnm (0x0015bae8). Window: added below now_y + 110,
  freed below now_y - 15 (0x00156530).
- Tails 0x0015bcf0: 30 segments (20 in multiplayer [I]).
- Beat lines every 240 ticks (0x0015d5b8 / 0x0015c990); rails by state
  (0x0015e190); lightning at (now - Star Power start) x 0.03 (0x00151580);
  smashers per slot (0x0015fd50); combo2_angle one-player only (0x0015caa0).
- Two players: track scaled 0.76, track.cam's rect the player's half
  (0x00154b90); explode with track_explode.cam, mirrored for P2.
- Missing: far-end fade, per-character surface override
  (track/surfaces/%s_keep.bmp), the song-start gem sparkle and the Star Power
  gem_bonus_spark bursts.

HUD: hud/hud_sp|vs|coop|practice.milo as GH2 (render/hud_view.cpp);
BandStarMeterDir revision 4 has five names (docs/formats.md).
