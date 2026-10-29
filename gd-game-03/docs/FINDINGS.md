# gd03: gameplay findings

Rules of Guitar Hero Encore: Rocks the 80s, as SLUS_215.86 implements them.
[C] = confirmed at the PS2 address given; [C 360] = gd-game-02's GH2 Xbox 360
finding, still to be re-confirmed in SLUS_215.86 (the engine is the same
generation); [I] = inferred. The rules engine is gd-game-02's
(`engine/src/game/`); this file records what has been re-checked on the PS2.

## Beatmatch
- Strum filter: a strum 52 ms or less after the last accepted one is dropped
  (`last + 52 < now` accepts) [C 0x002356f8] (the 360's 0x8233aa80 does the
  same). Consequence: runs of notes 48.7 ms apart (ballroomblitz, expert) can
  only be played with hammer-ons; `--selftest`'s strum-only bot allows those
  misses.
- Hit window, HOPO window, hopo_threshold: data driven (`config/gen/beatmatcher.dtb`:
  slop 100 ms, hopo_threshold 170 ticks, per-song thresholds in songs.dtb) [C data].

## Scoring and co-op
- Co-op stars use `config/gen/coop_max_scores.dtb` (per song, four values) [C data].

## Still to re-confirm on the PS2 (inherited from gd02)
Star Power (phrases, meter, deploy), multiplier, rock meter, sustain and
whammy accrual, face-off and co-op rules: see gd-game-02/docs/FINDINGS.md.
