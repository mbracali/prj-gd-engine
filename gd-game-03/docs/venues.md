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
