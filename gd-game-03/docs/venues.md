# gd03: venues and cameras

Venues (config/gh2.dta): battle, small1, small2, theatre, fest, arena; each
world/<v>/gen/<v>[_mp|_coop].milo (WorldDir revision 11) with
og/gen/<v>_geom, _lighting and the bank. The engine is gd-game-02's
(gd-game-02/docs/venues.md).

- World milos no venue loads [C data: nothing on the disc names them]:
  arena_attempt, fest_attempt, zombie_export, lightcan (shared), small1 test,
  randall_export, rock_ball_export, theatre_geom_shadow_town.
- The drummer explosion proxy is battle's only (drummer_explode_PROXY).
- fest's zombie.grp and bikers are also shown / hidden by EventTriggers
  (start.trig, excitement_great.trig), not only by the hide lists.
- arena's floor is near z = -300 (its shots sit between -250 and -320).
- `--venuetest`: every shot of every venue checked; 60 vs 240 fps identical
  (a shot's first sample skipped: arena flr_near_lft03 / battle
  balcony_rt02 paths jump at their first key). The director golden
  (18andlife in arena: 105 lines, 094b806a) is this disc's first baseline,
  not yet compared with the original shot by shot.

## fest_1.ipu (the fest song movie): never shown [C]

`world/shared/fest_1.ipu` (145,102 bytes, the same file as GH1's
`arena/movies/fest_1.ipu`) is named only by fest.dta's `(song_movie fest_1)`
and config/gen/arkorder.dta. The original does not show it:

- `song_movie` is not in SLUS_215.86 at all (no string, so no code looks the
  key up), and no script on the disc reads it [C]. It is a leftover of
  GH1's Arena song movie (gd-game-01 docs/video.md 5, dead there too:
  `use_video 0`). gd-game-02 found the same on the 360.
- No milo of the disc has a Movie (RndMovie, `.mov`), a VideoScreen
  (`.vscr`, whose default / blend movies would play one) or a Tex made from
  a `.ipu` [C data: every .milo_ps2 listed and its stream searched for
  `fest_1`, `.ipu`]. `TexMovie` exists only in the old-class rename table
  (0x2be2f8).
- The engine code is there: RndMovie (class 0x3603e0), PsMovie (load
  0x19e690, SetFrame 0x19eef0, per-frame IPU kick 0x19ecf0, flags byte
  0x1a3d38, decode-done handler 0x19ec60), as in GH1.

Format and decode (video/ipu.{h,cpp}, mpeg2.cpp `decodeIdec`): `ipum`, u32
size (file-8 here), u16 256, u16 256, u32 45 frames, per frame an IPU_CTRL
flags byte (0x60 in every frame: IVF + QST), 256 intra macroblocks in raster
order, zero bits to a byte, `00 00 01 B0`; `00 00 01 B1` at the end. The PS2
sends SETTH 0x91000001 (TH0 1: pure black transparent) and IDEC with RGB16
output, QSC 1 and no dither [C, 0x19ecf0]. The CSC coefficients and the
16-bit texel widening are [I]. SetFrame decodes forward only, one frame per
call; a smaller frame rewinds to frame 0 [C, 0x19eef0]. A Movie's frame rate
comes from its ScaleOffset filter; GH1's were 0.03 on the song ms (30 fps).

Checks: `tools/ipu.py <file.ipu> [--png dir]` decodes it independently,
byte for byte the same (FNV-1a per frame, all frames a98f1be53bddb31d);
its YUV matches FFmpeg's ipu decoder within 1 (72 dB). `--venuetest` prints
`ipu movie:`: song_movie names fest_1, 45 frames decode with that hash and
change every frame, the player advances and loops, no venue object could
draw it (drawn 0). Nothing binds it to a venue texture, as in the original.
