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

## Rules re-confirmed on the PS2
Checked against SLUS_215.86 where the GH2 (360) port differed; each is a
`--selftest` rule check ("PS2: ...").
- Held notes: the watcher holds every gem whose duration_ms is not 0, short
  notes (160 ticks or less) too [C 0x00248910; the parser never clears the
  length, 0x00236428]. Its end time, a miss or a new hit drop it silently
  [C 0x00248c80, 0x002495d8, 0x002490f8]; a release (a fret let go, a fret
  pressed above it, any joypad press) tells the player [C 0x00249038,
  0x0024e050].
- The player's release (0x00111810) finishes the open sustain record only when
  it belongs to the released gem; otherwise the record is thrown away [C].
- A hit accrues the open record up to the new gem's start_ms and finishes it
  only when complete; a new sustain replaces an unfinished record
  [C 0x001111d0, 0x00112280, 0x0012a0e0]. Accrual adds the signed difference
  [C 0x0012a168].
- A penalty finishes the record with the last poll's points [C 0x00112348].
- Pass of a phrase's first gem: phrase miss [C 0x00111358]. Phrase miss: a
  gem outside every phrase, or a phrase's last gem, sets the state to 0
  [C 0x0011e6a8].
- Wrong strum: the after-HOPO flag is `hopo-hit gem == j - 1` with no guard
  (a wrong strum on gem 0 with no HOPO hit expires silently) [C 0x0024d560];
  it expires when `deadline < now` [C 0x0024de90].
- Joypad: a press for a gem other than the waiting chord drops the chord with
  a miss, then is judged itself; the chord continues with no deadline test;
  the window expires in the poll [C 0x0024e050, 0x0024e2d0, 0x0024e450].
- No eight_x on the PS2 [C: 0x0010f910 / 0x0010fa98, no string].
  recharge_rate is read (0x0011e1e0) but 0.0 in scoring.dta.
- Not changed: a hit on a gem below the highest penalised one lowers that
  mark (+0xac = gem when gem < +0xac, 0x00111254) [C]; the engine does not
  do this yet.
