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
- A hit on a gem below the highest penalised one lowers that mark (+0xac =
  gem when gem < +0xac, 0x00111254) [C], so a later gem already penalised by
  an overstrum is penalised again when it passes.
- Face-off HOPOs: the gem lists are per difficulty and track, not per player.
  A new track adds a list to every difficulty [C 0x002447a8]; the parser
  sends each gem with its difficulty index [C 0x00245f28, 0x00246668]; the
  sink takes that difficulty's lists, then the gem's track (gem +0)
  [C 0x002448d0, 0x0024bba8]; every gem is appended whatever its player
  bits, and the HOPO test never reads them [C 0x0024d130, 0x0024d3a0]. So a
  face-off player's HOPO looks at the previous gem of the whole chart, the
  other player's gems included (the engine already did this).
- Who may fail: the crowd update fails a performer only when the game is not
  "num_players > 1 and competitive" [C 0x0010fc78, 0x001260a8, 0x00126098]
  and its vtable +0xa4 allows it [C 0x0010fe30]. Player and co-op player
  (vtables 0x003e1918 / 0x003e28c8): `competitive ? number == 0 :
  num_players == 1` [C 0x00112890, the same as 360 0x822C6B98]; the co-op
  band is a plain Performer (0x001103c8 -> 0x0010ee90, vtable 0x003e17c0),
  whose +0xa4 is TRUE [C 0x0010f310]. So the solo player and the co-op band
  can fail; co-op players and both face-off players never can (the engine's
  callers already pass no-fail for them; `Player::mayFail`). The crowd must
  also be active (+0x44 [C], which looks like crowd_rating_active, FALSE in
  practice and the tutorials [I]).
- The whammy gate +0xbc: 1 from the player constructor [C 0x001107d8]; only
  the script method set_whammystarpowerenabled changes it
  [C 0x001131a0 -> 0x00111cd8], and only tutorial_22_star_combos calls it
  (FALSE). With it off the whammy poll leaves the Star Power whammy flag
  alone [C 0x00111d88]. Its neighbour +0xb8 (set_mercuryswitchenabled,
  0x00111ce0, the tutorials 22-24) gates deploying in engage [C 0x00111940].
  The engine's player has both gates, routed from the script
  (ui/natives_game.cpp -> UiHost::setPlayerSwitch).

## Tutorial switches and the engage message
- `set_whammystarpowerenabled` (+0xbc) and `set_mercuryswitchenabled` (+0xb8),
  both 1 at construction (0x001107d8), set through 0x001131a0 -> 0x00111cd8 /
  0x00111ce0 [C]; tutorials 22-24 turn them off. With the mercury switch off an
  engage stores the engaged flag but deploys nothing (0x00111940) [C].
- `sp_engage`'s value is "mercury on, engaged, not active and ready", computed
  after any deploy (0x00111940) [C]: co-op's first engage reports 1 while the
  partner is awaited; a deploy reports 0.
- crowd_rating_active (GD03-028): the performer's +0x44 is the mode's
  crowd_rating_active at construction (0x001107d8; modes.dta FALSE in
  practice and the tutorials) and the script's set_crowd_rating_active
  (0x0010fe90, the cheats' crowd meter toggle and shuttle). While it is off
  the crowd update does nothing at all (0x0010fc78): the meter holds and
  nobody fails [C]. set_crowd_rating sets the meter (tutorial_16) [C]. The
  port: Player::setCrowdActive / crowdActive, selftest "PS2: with
  crowd_rating_active off ...". Practice's HUD (hud_practice.milo) has no
  meter.
