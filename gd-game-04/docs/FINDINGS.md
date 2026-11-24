# Guitar Hero III (Xbox 360): findings

Gameplay rules and engine behaviour as read in the executable and the data,
each with **[C]** (confirmed, with the address or data) or **[I]**
(inferred). Formats: `formats.md`. Tooling: `../agents/CONTEXT.md`.

## The executable

- XEX2, title ID 415607F7, media ID 43048167, base 0x82000000, entry
  0x82136AC8 [C: XEX optional headers 0x00040006, 0x00010201, 0x00010100].
- Ghidra (XEXLoaderWV): 32,998 functions after `FixXexFunctions.java`.
- Paths the code builds: `%s%s.pak.%s`, `%s%s/%s%s.pak.%s` (0x82182310),
  `pak/qb.pak`, `pak/dbgq.pak` (0x820D2200), `P\Compressed\compress.toc.xen`
  [C: strings and their referencing functions].
- 0x82188C50 rewrites `.pak` / `.pab` paths under `cutscenes`, `missions`,
  `pak`, `zones` (lower-cases the path, moves the file name after the
  folder) [C].

## QScript C functions

`ghidra/analysis/cfuncs.tsv` (`DumpCFuncs.java`): 1,392 `{name, code}`
pairs; the main table is 0x82916xxx-0x82919xxx (e.g. GuitarInputLogicInit
0x8210D258, GuitarInputLogicPerFrame 0x8211FA28, Create2DGem 0x82121C58,
fretbar_update_hammer_on_tolerance_CFunc 0x82107590) [C].

## Gameplay

Much of the tuning is QB data, `scripts\guitar\guitar_tweaks.qb` [C data]:
check_time_early 0.116 s, check_time_late 0.100 s, hammer_time_early /
late 0.083 s, default_hammer_on_measure_scale 2.95, strum_before_forming
0.034 s, form_before_strumming 0.025 s, ignore_strums_after_hammers 0.05 s,
the health_change_* table per difficulty and mode, star_power_whammy_add
3.333, star_power_drain_rate 12.5, star_tilt_threshold 16,
star_power_trigger_frames 3, xenon_input_offset / xenon_gem_offset -66 ms.

The scripts copy these into the player's `player_status` struct
(`gem_scroller`, `update_slomo`: check_time_early x current_speedfactor) and
the C input logic reads the struct (lesson L1: run the scripts) [C: the
scripts' text, `tools/qbscript.py dis`].

**HOPO tolerance** [C: 0x82107590, fretbar_update_hammer_on_tolerance_CFunc]:
as the song passes each fretbar i, `player_status.hammer_on_tolerance =
(fretbar[i+1] - fretbar[i]) / hammer_on_measure_scale x floor(den / 4)`,
`den` the time signature's denominator in force at fretbar i.
`hammer_on_measure_scale` is set per song (`call_startup_scripts`: the
song's struct may carry one) [C: script text].

**Note masks.** Bits 0-4 are the frets (green..orange); bit 0x20 is set on
5,815 notes [C data]; its meaning (forced HOPO flip?) is open [I].

The note -> gem expansion 0x821044C8 splits a note's length per fret
(each fret bit x length) and remaps lanes for the battle attacks / lefty
(switch on the pattern from 0x82101FD8) [C, partly read].

## Gameplay rules (the judge, scoring, Star Power, rock meter)

Read in the XEX at the addresses given (assembly where the decompiler cut
functions short); the HOPO decision 0x821047FC-0x82104908 was re-read
instruction by instruction. Implemented in `engine/src/game/note_track.cpp`
and `player.cpp`; `gd04 --selftest` plays every chart with them. "PS" = the
player's `player_status` struct.

### 0. Data the judge works on

- `input_array<p>` entries (built per note by 0x821044C8 -> 0x82101770):
  `[time_ms, len_green, len_red, len_yellow, len_blue, len_orange, hopo, next_time]`.
  Each fret's length = (chart bit k set) x note length; `next_time` = time of the
  chart note 3 words on, or 7,200,000 if none [C 0x82104AF4-0x82104B54].
- Globals set by GuitarInputLogicInit 0x8210D258 (per player p, x4 stride) [C]:
  0x829CE718 PS.check_time_early, 0x829DB6F8 PS.check_time_late,
  0x829DB728 hammer_time_early, 0x829BF9F8 hammer_time_late (globals, NOT
  speed-scaled), 0x829BD4D0 strum_before_forming, 0x829BA004
  form_before_strumming, 0x829E4B48 split_late_percent, 0x829CD708
  points_per_note[difficulty], 0x829BC0BC points_per_note_per_beat[difficulty],
  0x829D8468 star_power_sequence_bonus, 0x829BC0C4 whammy_shorten,
  0x829BD548 whammy_units_per_second, 0x829E39A0 time_offset, 0x829E3918 =
  fretbar[1]-fretbar[0] (first beat, ms), 0x829E3A28 current note index (first
  note with time > now+offset), 16-entry fret history per player (masks at
  0x829E4BC0+p*0x40, times at 0x829C1A08+p*0x40, cleared to 0 / -1.0).

### 1. Hit window and offsets

- PS.check_time_early/late = global x current_speedfactor (gem_scroller)
  [C script gem_scroller]. hammer_time_* are read raw [C 0x8210D384/0x8210D39C].
- time_offset passed to check_buttons_fast = PS.check_time_early*1000 +
  input_offset; input_offset = time_input_offset - user lag_calibration;
  time_input_offset = default_input_offset (+ song struct input_offset, + practice
  pitch-shift offset); default_input_offset := xenon_input_offset (-66)
  [C scripts gem_scroller, call_startup_scripts, platform setup at all2 l.35057].
- Per frame [C 0x8211FA28]: now_ms = song clock x1000; T = now_ms + time_offset.
  E = check_time_early; L = min(check_time_late, gap x split_late_percent);
  HL = min(hammer_time_late, gap x split_late_percent); HE = hammer_time_early;
  gap = (time[cur+1]-time[cur]) s (only when a next note exists).
  * Strum window for note time n: T-(E+L)*1000 <= n <= T, i.e.
    now+off-L <= n <= now+off+E with off = input_offset.
  * HOPO (tap) window: T-(E+HL)*1000 < n <= T-(E-HE)*1000, i.e.
    now+off-HL < n <= now+off+HE.
  * Miss: if time[cur] < T-(E+L)*1000 the note is missed and cur advances
    (one note per frame) [C 0x8211FD40-0x8211FDC0].
- Hit offset reported to scripts (off_note) = n - now_ms [C 0x821207BC].
- In the port (GD04-020): input_offset = the song's `input_offset` - user_options.lag_calibration
  (read from the global tag at the song's start), passed to the judge; gem_offset = the song's
  `gem_offset`, shifting gems and fretbars on the highway; the 360's -66 ms defaults stay out
  (D7) [C scripts gem_scroller, call_startup_scripts]. The practice pitch-shift offsets are the
  360's platform offsets too (xenon: -64 song, 0 slow / slower / slowest) and stay out with them.

### 2. HOPO

- Natural HOPO [C 0x82104850-0x82104908]: note i is a hammer-on iff there is a
  previous entry, time(i)-time(i-1) <= PS.hammer_on_tolerance, note i is a single
  fret, and its mask differs from the previous entry's mask. Chords and repeated
  same-fret notes are never natural HOPOs.
- hammer_on_tolerance per fretbar = (fretbar[i+1]-fretbar[i]) /
  hammer_on_measure_scale x floor(den/4) (FINDINGS.md) [C 0x82107590];
  hammer_on_measure_scale = song struct value or default 2.95 [C script].
- Mask bit 0x20: hopo = natural XOR (bit 0x20 set AND length != 0) [C
  0x821047FC-0x82104908]. So 0x20 is a forced flip (forces a HOPO or forces a
  strum). A forced-HOPO chord still needs a strum (tap path is single-note only).
- Tap without strum [C 0x82120160-0x82120200 and 0x82120560]: on a frame with no
  strum, if the last-hit mask (0x829E3A20) != 0 (previous note was hit; cleared on
  miss/overstrum), take the highest held fret (lower frets ignored); if it is
  non-zero and != last-hit mask it is a tap candidate. A single-note HOPO in the
  HOPO window whose mask equals the candidate is hit. Only the first candidate
  note can be tapped (later notes in the scan are strum-only) [C: puVar19 reset].
  This is evaluated every frame, so holding the fret before the note arrives works.
- After a tap hit: 0x829BD540 = tapped mask, 0x829CD710 = time until that note
  leaves its window (capped at next note time - T). While that timer >= 0 a strum
  whose shape overlaps the tapped mask is ignored (no overstrum) and that shape is
  excluded from the strum shape list [C 0x82120840, 0x821208B8-0x821208D8].
  `ignore_strums_after_hammers` is read but its value is discarded [C 0x8211FAB8].
- dragonforce_hack (=0) adds every newly-pressed fret as a tap candidate [C 0x82120130].

### 3. Strum judging, chords, overstrum

- Strum source [C 0x82100FC0]: guitar = strum bar (button flags 0x1000|0x4000);
  pad = any change of the face-button set. Held frets from 0x82100DA0, minus
  PS.broken_string_mask.
- On a strum: collect the distinct fret shapes in the 16-frame history whose time
  >= now - form_before_strumming (25 ms) [C 0x8211FF38]. Then if
  disallow_tolerance_strums=1 (guitar only) and a previous strum is still pending
  (0x829E4B3C >= 0), the current note is missed (bum note) [C 0x8211FF80-0x8211FFC8].
  If no post-tap timer runs, pending := strum_before_forming*1000 (34 ms).
- While pending >= 0 on non-strum frames, shapes held within the last
  strum_before_forming are also collected (form after strumming) [C 0x82120090].
- Note scan from cur while n <= T [C 0x82120260-0x82120880]: single note: any
  collected shape whose highest fret == note fret hits (anchoring: lower frets
  ignored, higher frets fail). Chord: a collected shape must equal the chord
  exactly (pad: only after all buttons were released, 0x829BA034). First match is
  hit; every earlier unmatched note in the scan is missed [C 0x82120700-0x82120740].
- Strum with no note in reach (first note n > T) -> overstrum, unless inside the
  post-tap ignore window with overlapping frets [C 0x821208B0-0x82120924].
  A strum with wrong frets while a note is in the window is not penalised at once;
  the note is missed when it leaves the window; if a strum is still pending at that
  moment it is also an overstrum [C 0x8211FD88-0x8211FD9C].
- Overstrum (0x8211F7B0): unnecessary_note event (wrong-note sound, guitar mute,
  CrowdDecrease), current_run := 0 (both players in co-op) [C 0x8210CB10]; if the
  previous note lies in the current star phrase the phrase is lost (stars ->
  gems, next phrase) [C 0x8211F938-0x8211F9EC]. total_notes unchanged. Its
  last step sets the byte 0x829E4C80[player] = 1 [C 0x8211FA10-0x8211FA14];
  a sustain's start (0x82106758) clears it [C 0x82106B84] and the sustain
  update 0x8211C9D0 seeing it clears it and takes the released path: a
  sustain held through an overstrum ends there (whammy_off finished 0, the
  tail dead, no more sustain points / star whammy) [C 0x8211C9D0] (GD04-108).
- Miss (0x8211F328): missed_note event (bum_note = a strum happened),
  CrowdDecrease, total_notes+1, current_run := 0 [C 0x8210C800]; if any gem of the
  note is still tagged `star` (0x3624A5EB; no earlier loss of its phrase),
  0x8211E8B8(player, note time): change_stars_to_gems if star_power_used;
  return there for player 2 when new_net_logic (online); star_power_sequence
  := 0; **if phrase time <= note time < phrase time + length** (the
  half-open range of the gem builder) the current phrase is given up (the
  pointer moves to the next, 0x82102830); broadcast star_miss_note {array_entry,
  song} and send it to the player's gems (they turn into normal gems) [C
  0x8211F56C-0x8211F5B0, 0x8211E8B8-0x8211EA3C, 0x821195C0].
- clear_history_on_hit=1: fret history wiped after every hit [C 0x821207F0].

### 4. Score

- Hit (0x8210BB40): current_run+1, notes_hit+1, total_notes+1; multiplier from
  run AFTER increment: <10 x1, <20 x2, <30 x3, else x4; x2 if star_power_used;
  score += scale x points_per_note(50) x multiplier x number_of_frets_in_note
  [C 0x8210BB40]. scale (0x829BA010) = 1.0, except face-off: (base_p1+base_p2) x
  0.5 / base_p (score normalised between difficulties) [C 0x8210B9A0-0x8210BA50,
  0.5 = DAT_820010BC [I value]].
- Co-op: multiplier uses p1.current_run + p2.current_run; notes_hit/total_notes
  increment for both; misses reset both runs [C 0x8210BB40, 0x8210C800].
- Sustain (0x82106B98, per held frame): units += (now - last)/playline_song_beat_time
  x points_per_note_per_beat(25); whenever floor(units) - awarded >= 2, award that
  many points x multiplier x scale (not beyond the note's max); when held to the end
  award (max - awarded) [C]. Max per note (CalcSongScoreInfo 0x8210AF70):
  round(length expressed in fretbar beats x 25), only for notes with
  length > whammy_shorten x first_beat and > 0.5 x first_beat [C 0x8210B774-0x8210B83C].
- guitar_score.qb `hit_note` script is unused (C does scoring) [C: no caller].

### 5. Sustains and whammy

- Sustain exists iff max fret length > 0.5 x first beat; held length = length -
  whammy_shorten(0.25) x first beat [C 0x82102E28]. Hold check: single-fret
  sustain allows lower frets (anchoring), chord sustain needs exact frets [C
  0x8211C9D0]. Releasing with remaining <= whammy_min_hold_pct (10%) of the hold,
  or <= 35% (whammy_min_hold_pct_short) if the hold < 750 ms
  (whammy_min_hold_mute_ms), counts as complete (health gain); otherwise guitar
  muted [C 0x8211C9D0 end]. A sustained note's health gain is deferred to the end
  of the sustain [C 0x82110E8C-0x82110EA4].
- Star whammy (script star_power_whammy): while a star sustain is held and the
  whammy moved > 0.03 within the last 5 frames, star_power_amount +=
  star_power_whammy_add (3.333; co-op 2.5) x dt / beat_time, i.e. 3.333 % per beat
  [C script].
- whammy_cutoff (1100) [C 0x8210E750, the 2D gem / tail mover]: while a
  sustain's element is `holding`, its head start time is reset each frame to
  now - whammy_cutoff x T / N (T the travel time, N = gem_time_table512's
  size, 1152), so the head sits at row whammy_cutoff x 1024 / 1152 = 977.8
  (1P: 0.870 of the way down the highway through time_accum, above the
  playline row 1024); the tail runs from there. The port draws it so
  (render/highway2d.cpp, `vHead` at rowDepth(977.8)); the sustain's element
  outlives its head's last row while its tail is on the highway (a held
  one always), so long sustains keep their tail [C 0x8210E750].

- Whammy sound: CheckNoteHoldPerFrame runs control_whammy_pitchshift every frame
  of a hold, CheckNoteHoldEnd runs reset_whammy_pitchshift [C 0x8211D078,
  0x8211D49C]. set_whammy_pitchshift sets Guitar1PitchShift / Guitar2PitchShift
  to 1 - 0.057 x bar, about a semitone down at full bar. The unit (FastPitchShift,
  PitchShiftEffect1 / 2, first in Player1Effects) is 0x826E5C38: a linear
  resampler on an 8192-frame ring whose lag grows by 1 - pitch a frame, resets
  past 4096 frames or at pitch >= 1, with a crossfade of up to 100 frames.
  audio/fast_pitch.cpp.

### 6. Star Power

- star_power_amount 0..100 [C 0x8210C5B8 clamp]. Phrase: each star-note hit
  increments star_power_sequence; hitting the gem flagged `last` (only still set if
  no star note of the phrase was missed) awards star_power_sequence_bonus = 25
  via 0x8210C5B8, unless star_power_used (then the phrase is just consumed)
  [C 0x8211E4E8]. Hitting a non-star note resets sequence/note_count [C 0x8211F16C].
- No SP gain in battle or boss battle (they use the phrase for power-ups /
  bossbattle_ready) [C 0x8211E704, 0x8210C5B8].
- Activation needs amount >= 50.0 [C 0x82112D7C, DAT_82001564]. Triggers: tilt
  sensor value <= PS.star_tilt_threshold (16) for star_power_trigger_frames (3)
  consecutive frames [C 0x82112F28-0x82112F90], or Select [C 0x82112F9C].
- Drain (script star_power_activate_and_drain): amount -= 12.5 x dt_ms /
  measure_ms per frame (co-op 16.67) -> 8 measures from 100 (6 in co-op); while
  active multiplier x2, health gain uses health_change_star_* [C].
- Co-op: amount is shared (both players set to the same value) [C 0x8210C5B8];
  activation needs both players within star_power_coop_trigger_ms (1000) [I from
  0x82112BC4 and show_coop_raise_axe_for_starpower].
- Tilt units [C 0x82112EE0-0x82112F90, 0x822AFD90, scripts]: the value is
  byte 1 of the controller's pad record (right stick Y [I]), v = b - 128,
  as (v - 50) / 77 for v > 0, (v + 50) / 77 for v < 0 (-127 -> -1.0, +127
  -> +1.0); a frame counts when it is <= the player's star_tilt_threshold,
  which start_gem_scroller sets from the user option
  star_power_position_device_<controller> (get_star_power_position; -1.0 by
  default, the calibration screen stores the held `righty` clamped to <= 0;
  guitar_tweaks' 16.0 is only the placeholder); star_power_trigger_frames
  (3) consecutive frames fire, Select (0xB40D153F) at once. The port keeps
  them apart: input/ marks a Star Power press held by the tilt axis alone
  (InputEvent::tilt), FrameInput::tilt fires after star_power_trigger_frames
  frames, FrameInput::starPower (Select) at once (game/player.cpp) [the
  wait C; the byte mapping against the calibrated threshold is not applied
  to SDL's axes, input/'s axis threshold stands for it: I].
- Co-op raise-axe prompt [C 0x82112850 + script]: the trigger in p2_career /
  p2_coop spawns show_coop_raise_axe_for_starpower with the **other**
  player's status (player 1 triggers -> player2_status, player 2 ->
  player1_status); the script places it at hud_screen_elements[10].pos
  - (225, 60) for player_status.player = 1 (left, (415, 240)) and + (225,
  -60) for player 2 (right, (865, 240)); so it shows on the side of the
  player who has not raised yet. Not shown while that player's
  star_power_used, showing_raise_axe, attract or performance mode. The port
  draws it there: Match::raiseAxeBy() is the player who raised first,
  render/hud2p.cpp puts the prompt at (865, 240) / (415, 240).

### 7. Rock meter

- Health 0..health_scale (2.0); start 1.0 (encore 1.6666, battle 1.0) [C crowd_reset].
- Gain per hit = health_change_good_<diff> (star if SP active; battle: good_battle;
  boss battle human: good_boss); loss per miss/overstrum = health_change_bad_<diff>
  (bad_battle / bad_boss). Values in guitar_tweaks.qb. Multiplier: x1.35 if health
  <= 0.6666 (red), x0.85 if >= 1.3333 (green), else x1 [C 0x821062A8, 0x8210F6A0,
  0x8210FD18]. Clamp at 2.0 on gain [C].
- Boss player uses current_boss.gainpernote/losspernote[difficulty] x 0.02 [C].
- Fail when health <= 0 (single); co-op: when (h1+h2)/2 <= 0 [C 0x8210FD18].
- Invincibility: health_invincible_time = song_length x 20% (easy) / 10% (medium)
  (quickplay, career, battle; co-op by both difficulties); before that time no loss
  while health <= health_invincible_minimum x 0.02 (= 0.5) [C script
  calc_health_invincible_time, 0x8210FD18]. No loss with cheat_nofail (non-career),
  tutorial, training.

### Boss battles: deep per-boss pass + invincibility fix (2026-11-24)

A full per-boss RE of the three career bosses (Tom Morello, Slash, Lou/the
Devil) against `guitar_battle.qb` and the XEX. Everything below is [C] from the
disc data / decompiled C unless noted; one genuine defect was found and fixed.

- Per-boss `PowerUps` (card pool both players draw from, `bossbattle_select`),
  by `name` key against `battlemode_powerups` (0xab5dd1f9): Tom Morello
  (Boss_TomMorello_Props 0x9a5885e7) = Amp Overload, Difficulty Up, Double
  Notes, Whammy; Slash (0x79cdef14) = + Broken String; Lou (Boss_Devil_Props
  0xd38033de, `character_name` "Lou") = + Lefty Flip + Broken String. No boss
  uses Powerup Steal or Death Drain as a card (Death Drain only in sudden
  death, which boss battles do not have; the boss drain is the scripted
  `boss_battle_begin_deathlick`). gainpernote/losspernote/PowerUpMissedNote/
  WhammySpeed/BrokenStringSpeed/BrokenStringMissedNote all read per-difficulty
  from the props at runtime (BattleData::loadBoss); field keys verified. The
  port's filter and enum ordering match the table exactly.
- `bossbattle_select` (0xc6bd40ce) vs `battlemode_select` (0x503e0639):
  identical roll EXCEPT the boss path has no health-<=0.5 "losing" branch
  (always `weight`, `weight_low` for the last-rolled and the top card) and no
  sudden-death forced Death Drain. It skips Difficulty Up when the victim's
  difficulty (current_difficulty2 for P1, current_difficulty for the boss) is
  expert. The port's `select()` with `bossRules` reproduces this.
- Boss AI: firing (0x82108948) = reset a 100-frame counter at 0 cards, count
  down at 1 card, fire at once at 2; `CheckButtonsBoss` (0x821096E8) +
  `CheckBossCutoff` (0x82109A78) = plays the rhythm chart, HOPO tapped 4 times
  then strummed on the 5th (a global hammer counter reset on every strum), hold
  = max fret sustain /1000 + 1 s (1 s when < 61 ms), frets released when the
  hold expires; `bossbattle_ai_damage` (0x5076aabf) + ApplyBossBattleGemMisses
  (0x82103eb8) = pct % of the fret-gems in each [start,end) window missed
  (immediate cards from the boss's current note, delayed cards one travel+1 s
  ahead), Whammy has `no_ai_damage` so only kills the frets, Broken String runs
  until all five broken counters < 3; 200 ms lead. All match the port
  (`bossInput`, `bossDamage`, `applyBossMisses`). The `game_mode == tutorial`
  (0x39940016) guard in the firing/select scripts is irrelevant to a real boss
  battle (not tutorial).
- Outcome: boss at 0 -> Outcome::P1Wins -> `failed()` false -> `r.won` ->
  GuitarEvent_SongWon (newspaper); player at 0 -> Outcome::Failed ->
  GuitarEvent_SongFailed. `battleKo` is Battle-only, so a boss win is a normal
  SongWon, not a KO-fail. Lou's death-drain restart is wired through
  `setup.devilFinish`/`startMs` (player 2 off-screen at x 1000). Faithful.
- GuitarEvent_BattleAttackFinished (0x30252d4c): still not raised, and this
  stays a faithful-in-effect omission. It only resets battle audio-mixing state
  (ramps the five guitar busses over 0.15 s and zeroes the per-player balance
  buss DSP 0xf6a36814). No attack-start script (battle_lightning /
  _up_difficulty / _death_lick / _trigger_on / _steal / _broken_string /
  _whammy_attack) applies any guitar buss/DSP, and the port applies none per
  attack either, so there is nothing stuck to reset -- implementing it would be
  a no-op without first adding the attack-time ducking it reverses. Documented,
  not fixed.

FIX -- boss early-window invincibility (game/player.cpp). CrowdDecrease
(0x8210FD18) gates every health loss on `song_time < player_status.invincible_time
(0xf4cecb3d) && health <= health_invincible_minimum x 0.02 (0.5)`, reading
whichever `player_status` it is handed; the boss's own loss is computed past the
same gate. In a career boss battle game_mode is `p1_career` (0x76362f1e, per the
KO logic's own `iVar6 == 0x76362f1e` test), which is in
`calc_health_invincible_time`'s first branch, so it sets the window for BOTH
players by their own difficulty (the boss shares P1's via current_difficulty2) --
easy 20 % / medium 10 %. The port's `changeHealth` had `if (!rules_.boss && ...)`,
which denied the boss the window, so on easy/medium the boss could be drained
below 0.5 and killed in the opening of the song, unlike the original. Dropping
the `!rules_.boss` guard (the boss's `invinciblePct` is -1 -> by difficulty, diff
= P1's) restores it; hard/expert unchanged (0 % window). Verified with a
deterministic Player-level harness (before: boss 0.50 -> 0.395 inside the easy &
medium windows; after: held at 0.50 inside, still loses after the window and
when > 0.5, hard still loses).

### 8. Modes (details, per mode, in `modes.md`)

- Co-op (p2_career; p2_coop is online only: the local MULTIPLAYER menu has
  FACE-OFF / PRO FACE-OFF / BATTLE [C create_mp_select_mode_menu]): the parts
  from the part menu (guitar / rhythm), guitarcoop_ / rhythmcoop_ with
  use_coop_notetracks [C setup_gemarrays]; multiplier from p1.run + p2.run
  [C 0x8210A748]; a miss resets both runs; notes_hit / total_notes rise for
  both; every SP gain to both (increase_star_power), whammy 2.5 / beat, drain
  16.67 / measure; activation: both triggers while the raise-axe prompt
  (~1.24 s) shows [C 0x82112850, 0x821131E4]; health may go below 0, fail
  when (h1 + h2) / 2 <= 0 [C 0x8210FD18].
- Face-off: only the notes in the player's `<song>_faceoffp1/p2` sections
  enter (and show on) his highway [C 0x82104558]; score scale (sim_p1 +
  sim_p2) x 0.5 / sim_p (sim_bot_score) [C 0x8210B9A0]; never fails; the
  centre needle = clamp(-0.0065 x (p1 - p2), +-65) [C 0x82116AC4]. Pro
  face-off: full chart, scale 1.0, one difficulty.
- Battle / boss: `_StarBattleMode` phrases give cards (battlemode_powerups,
  a stack of 3), fired with the SP trigger (no 50 % gate) [C 0x8211E7B0,
  0x82112D84]; the card roll [C 0x821217A0-0x82121B50, 0x8211E7B0-0x8211E890,
  0x8211F034-0x8211F084]: PS.battlemode_creation_selection is rolled
  (battlemode_select / bossbattle_select) when a phrase gem is made while
  it is -1, every phrase gem carries the value as `battle_gem`, the hit of
  the gem flagged `last` awards that gem's (battlemode_ready battle_gem = ..),
  and it is set back to -1 by a gem made outside every phrase and by a
  completed phrase (a phrase made while the one ahead is still on the
  highway, with no plain gem between, keeps that one's card); attacks and their numbers in `modes.md`; Double Notes is the
  only lane change, at fill time [C 0x821044C8]; broken_string_mask is taken
  off the held frets [C 0x82100DA0]. KO: a player at 0 with the healths
  unequal; both alive at the end: sudden death (the song again, every new
  card Death Drain) [C GuitarEvent_SongWon]. Boss: plays the rhythm chart,
  a perfect bot except gems marked by ApplyBossBattleGemMisses and dead
  frets [C 0x821096E8, 0x82103EB8], fires its first card after 100 frames
  [C 0x82108948], health by its props x 0.02.
- `ArrayContains` (0x82267808) was missing: Is_LevelZone_Downloaded then
  failed and `setup_generalvenuetags` locked every venue after each load (the
  face-off venue list was empty) [C script, fixed].

### Mid-song starts (starttime)
- start_gem_scroller's `starttime` (current_starttime, ms) [C script]: the song clock starts at
  starttime - the transition's time, the gem iterators begin at starttime (earlier notes never
  exist), the intro's times count from it; Lou's finish restarts at `<song>_markers[37].time`
  with devil_finish_restart = 1 (any other start clears devil_finish, BossDevil then at 0)
  [C scripts start_devil_finish, start_gem_scroller]. Port: Match / Player / Autoplay skip the
  notes before it (their phrases cannot complete), the song's timed scripts before it are not
  run (the boss Death Drain), and the audio starts there.
- Audio: the FSB3 XMA sample header's codec data is a seek table: 24 bytes, then one u32 (LE)
  per 2048-byte packet = the frames decoded before that packet [C data; checked: a stream
  started at packet k with the table's frame count matches a from-0 decode sample for sample].
  XMA1 packet headers carry no frame count (4-bit sequence number first) [C data].
- Motions (0x822A6B28): 1 smooth = 3t^2 - 2t^3 (written (t - 1.5) t^2 x -2), 2 ease_in = t^2,
  3 ease_out = 2t - t^2, else linear; the step (0x8221BC28) sets each prop to start + (target -
  start) x f, alpha and the four rgba bytes too (truncated to bytes) [C]. ui_system and the HUDs
  use these.

### GD04-004: the last unknowns, confirmed (2026-10-29)

Read with Ghidra headless (DumpAsm.java, ReadF.java) and the analysis
export; REA not used (it cannot open the XEX). All [C]:
- whammy_cutoff: §5 (the held sustain's head row, 0x8210E750).
- star_miss_note boundary: §3 Miss (phrase time <= t < time + length).
- Tilt units: §6 (normalised -1 .. 1, threshold from the calibration).
- The gem builder's phrase test (0x82102B70, called by the gem creator
  0x82120C90): the phrase pointer advances past every phrase whose end <=
  the note time (0x82102830; none left: start 10,000,000), then the note is
  in the phrase iff start <= time < start + length; it counts the phrase's
  distinct note times; `last` = end <= the note's input_array word 7 (the
  next chart note's time, 7,200,000 at the end). Face-off keeps the full
  chart's `last` (word 7 is read from the chart, not from the player's
  sections), so a phrase whose last note is the other player's never
  completes. game/note_track.cpp now keeps the full chart's flag in
  faceOffTrack (it re-marked the last kept note before).
- Battle card roll: §8 (battlemode_creation_selection); game/match.cpp now
  rolls and resets it as the original does.
- Co-op raise-axe side: §6 (the other player's side; drawn so now).
- CrowdDecrease (0x82110570 -> 0x8210FD18) returns before any loss when:
  player 2 with new_net_logic; cheat_nofail = 1 outside p1_career /
  p2_career / p2_battle / p2_coop and not is_network_game; game_mode
  tutorial or training; song time < PS.health_invincible_time and health
  <= health_invincible_minimum x 0.02; health <= 0 outside co-op;
  crowd_debug_mode, end_credits or use_character_debug_cam = 1 (globals
  cached by 0x821062A8). game/player.cpp: gd04's no-fail now behaves as
  cheat_nofail (no loss at all; it only clamped at 0 before).

## The song path: scripts vs C (GD04-021, 2026-10-29)

Read from qbscript.py dumps of qb.pak, a reachability walk of the scripts
from start_gem_scroller, a tally of every song's event arrays, and the
natives each script calls (ghidra/analysis/cfuncs.tsv); REA not used.

**Split in the original** [C scripts, C addresses in flow.md §11]. The
scripts own the setup and the timing loops; every per-frame decision is C:
- Scripts: start_gem_scroller (setup, below), gem_scroller (per player:
  setup_gemarrays, calc_health_invincible_time, the hit window x speed, and
  the spawns), every iterator's loop (`X_CFunc_Setup; begin if X_CFunc
  break; Wait 1 gameframe repeat; X_CFunc_Cleanup`), win_song, the event
  hooks (GuitarEvent_*), the Star Power scripts, the crowd / band / camera /
  light-show event handlers, the HUD's morphs.
- C: the gem iterators (gem_array_stepper_CFunc: fill_input_array ->
  FillInputArray 0x82104B90 with the face-off filter and Double Notes,
  create_gem -> Create2DGem 0x82121C58 -> 0x82120C90 with the star / battle
  tagging, GemStep 0x82122B10, the gem mover 0x8210E750), the fretbars
  (fretbar_iterator_CFunc, Create2DFretbar), fretbar_update_tempo /
  _hammer_on_tolerance, the judge (check_buttons_fast:
  GuitarInputLogicInit / PerFrame 0x8211FA28 with hit / miss / overstrum,
  scoring 0x8210BB40, health 0x8210FD18 / 0x8210F6A0, Star Power
  0x8210C5B8 / 0x8211E4E8), button_checker (the fret buttons),
  check_for_star_power_CFunc (trigger, co-op prompt), update_score_fast
  (UpdateScoreFastPerFrame: the HUD's score / multiplier / meter values),
  check_note_hold (sustains), CheckButtonsBoss / _bot, the band iterators
  (Strum / FretFingers / FretPos / Drum, TimeMarkerReached), LightShow_*,
  CrowdIncrease / CrowdDecrease, StarPowerOn, NoteFX, the
  GuitarEvent_HitNotes_CFunc. C reports to the scripts by events
  (hit_notes, missed_note, unnecessary_note, star_power_on, star_hit_note,
  star_miss_note, star_sequence_bonus, whammy_on/off, multiplier changes,
  crowd changes, song_won / song_failed) that gem_array_events /
  event_spawner turn into GuitarEvent_* scripts.

**What gd04 runs instead.** The native start_gem_scroller
(ui/natives_game.cpp) reads the song / difficulties / mode / boss / lefty /
lag and hands over to ui/song_screen + game/match + game/player (the C
judge reimplemented, [C] in this file), render/highway2d (gems, fretbars,
tails, buttons, flames, pulses), render/hud2d / hud2p, audio/song_stream,
the venue (render/venue_*: cameras incl. the intro transition, light show
from `_lightshow_notes`, crowd, particles) and the band (render/band_*:
the performance array's Band_ChangeStance / PlayAnim / PlayFacialAnim /
WalkToNode, the C hand / drum iterators). The song ends into GH3's own
GuitarEvent_SongWon / GuitarEvent_SongFailed (front_end.cpp), so the end
is the original's; a won song keeps drawing (venue, highway until
disable_highway) under GuitarEvent_SongWon_Spawned and the songwon
transition until kill_gem_scroller (docs/flow.md section 12). **None of the GH3 song scripts below runs.**

**Scripts not run, with an effect the port therefore lacks** (L) or
re-implements natively (R) [C scripts]:
- start_gem_scroller setup: load_songqpak (R), call_startup_scripts (the
  song's startup_script / exit_script, gem / input offsets, hammer-on scale:
  R for the offsets; the only startup scripts songs name, default_startup
  and LS_SetupVenueLights, are empty: L none), GuitarEvent_EnterVenue (the venue's reverb on the sound busses: L),
  GH3_Set_Guitar_Verb_And_Echo_to_Dry (L), Menu_Music_Off (R),
  create_cameracuts / create_movie_viewport / create_crowd_models /
  crowd_create_lighters / Crowd_StageDiver_Hide / create_band (R),
  preload_song / begin_song_after_intro (R), reset_score / crowd_reset /
  star_power_reset / difficulty_setup / calc_songscoreinfo (R),
  setup_highway / generate_pos_table / move_highway_2d / setup_hud /
  reset_hud (R), Transition_Play + GuitarEvent_Transition* (empty hooks;
  the intro cameras R), battlemode_init / bossbattle_init (R),
  practicemode_init (R, ui/song_hooks: spawned in training; Hide_Band hides the
  band, practicemode_section shows the section's name from current_section_array
  = <song>_markers and current_section_array_entry, which the port moves on as
  the song passes each marker [I]; SetNoteMappings for the drums is a stub),
  setslomo current_speedfactor (R: Mixer::setSlomo resamples every stream to the
  speed and the song clock runs at it; the scripts' clock too while the song
  plays; player 1's guitar shifted back up by PitchShiftSlow1's pitch with a
  two-tap delay-line shifter [I: GH3's is FMOD's FFT PitchShift]), the boss intro audio
  GH_SFX_Preload_Boss_Intro_Audio and the boss helper screen (L),
  play_intro's sounds (R, ui/song_hooks: Song_Intro_Highway_Up_SFX_Waiting
  at highway_start_time, Notes_Ripple_Up_SFX per player at
  button_ripple_start_time, Song_Intro_Kick_SFX_Waiting at hud_start_time,
  from current_intro = the intro transition's sequence props),
  Progression_SetProgressionNodeFlags (node flags: R where venues need them).
- gem_scroller's iterators: gem_iterator x4, fretbar_iterator x3,
  check_buttons_fast, button_checker, check_for_star_power,
  update_score_fast, check_note_hold, the band iterators, lightshow_iterator,
  cameracuts_iterator, win_song (R: its end time and movie, ui/song_screen
  and ui/song_hooks), faceoff_init (R, as the C they call);
  faceoff_volumes_init (the guitar track at 100 % for both, the face-off
  volume switching by section: R in part, mixer guitar mute only);
  event_iterator + notemap_startiterator x8 over `<song>_scripts`, `_anim`,
  `_triggers`, `_cameras`, `_lightshow`, `_crowd`, `_drums`,
  `_performance` (R for lightshow LightShow_SetTime, performance Band_*;
  L for the rest: Crowd_StageDiver_Jump (29 songs), Crowd_StartLighters /
  StopLighters (12 songs), GH3_Crowd_Event_Listener crowd surges / applause
  (1 song, 15 events), Band_PlayChord* (1 song), dragonforce_hack_off;
  the boss Death Drain (boss_battle_begin_deathlick) R in Match).
- Event hooks: GuitarEvent_HitNote(_Spawned) -> hit_note_fx (flames, R);
  GuitarEvent_MissedNote / _UnnecessaryNote -> Guitar_Wrong_Note_Sound_Logic
  (**the bad-note sound**, guitar / bass / player 1 / player 2 variants: L),
  guitar mute (R), highway_pulse_black (R), CrowdDecrease (R);
  guitarevent_multiplier2x/3x/4xoff (UI_SFX_Lose_Multiplier_* and
  Lose_Multiplier_Crowd sounds: L; highway_pulse_multiplier_loss: R);
  multiplier4xon (the 4x particle system: L); GuitarEvent_StarSequenceBonus
  (Star_Power_Awarded_SFX: L; the big bolts and star particles on the
  buttons: L; sp_phrases_hit: R); GuitarEvent_StarPowerOn ->
  GH_Star_Power_Verb_On (Star_Power_Deployed_SFX + _Cheer_SFX, the Star
  Power buss set and a tempo-synced guitar echo / reverb: L),
  Do_StarPower_StageFX (per-venue stage effects: shotgun, flamethrower,
  hearts, bats, butterflies, peace, tesla particle FX: L), StarPowerOn (R);
  star_power_activate_and_drain (drain R; hud_activated_star_power R;
  LaunchGemEvent star_power_on R; Crowd_AllPlayAnim, the crowd's Star Power
  animation: L; UpdateNixie: L); GuitarEvent_StarPowerOff (verb off L,
  rock_meter_star_power_off R, Kill_StarPower_StageFX / Kill_StarPower_Camera:
  L with the FX); do_starpower_camera (the Star Power camera array prefix: L);
  show_star_power_ready (its SoundEvent: L; meter bulbs and lightning alert:
  R); star_power_whammy (the gain R; MakeStarWhammy blue tails R);
  GuitarEvent_PreFretbar (Crowd_Individual_Clap_To_Beat on every beat while
  Star Power is on: L); GuitarEvent_Fretbar -> set_sidebar_flash (R);
  GuitarEvent_FirstNote_Window_Open / check_first_note_formed (guitar
  volume at the first note: R by the mixer); GuitarEvent_CreateFirstGem ->
  first_gem_fx (L); GuitarEvent_Whammy On/Off -> WhammyFXOn/Off (C 0x8211C1A0 / 0x8211C390:
  the whammy effect on the guitar stream [I: not read further]: L); GuitarEvent_BattleAttackFinished
  (attack SFX, battle DSP reset: L) and the battle scripts' sounds
  (Battle_Power_Awarded_SFX_P1/P2 and the attack sounds: L);
  GuitarEvent_crowd_* (printf only); GuitarEvent_KillSong (stops SFX: n/a).
- Debug-only hooks (Note_Window_Open / _Close, Fretbar_Early / _Late,
  Debug_Audible_*): nothing to miss.

So the visible / audible gaps are all on the script side: every in-song
sound effect (bad notes, Star Power deploy / awarded / ready, multiplier
loss, claps, crowd surges, battle sounds), the Star Power stage FX, camera
switch and crowd animation, the 4x / first-gem / phrase-bonus particles,
the stage divers and lighters, the venue reverb and the Star Power guitar
echo, the whammy effect. The judge, highway, HUD numbers and song audio are
reimplemented natively and match the C ([C] above). Also not modelled
(then): end_credits (no health loss during the credits song; done since,
below) and tutorial /
training (CrowdDecrease's early returns, §GD04-004).

**Proposal** (agents/DECISIONS.md D10): run GH3's own song scripts
(start_gem_scroller's real body, gem_scroller and everything it spawns, the
event hooks) on the VM, with the port's core behind the natives the
scripts call (gem_array_stepper_CFunc, FillInputArray, Create2DGem,
GemStep, GuitarInputLogicPerFrame, CheckNoteHold*, ButtonChecker*,
check_for_star_power_CFunc, UpdateScoreFast*, CrowdIncrease / Decrease,
StarPowerOn, NoteFX, TimeMarkerReached, LightShow_*), keeping game/match
and game/player as the C, and the highway / HUD as the renderer of the
elements the scripts create.

### D10 done: GH3's song scripts over the native core (2026-10-29)

Read with Ghidra headless (DumpAsm.java over 0x820FF000-0x82127000,
ReadF.java on the event tables 0x82919490..) and the analysis export; REA
not used (it cannot open the XEX). The C raises its events with
LaunchEvent (0x821578E8, `broadcast` from `system`) of a per-player key
from a table (`<event>p1` / `p2` = ExtendCRC of the event and the
player's Text) and a parameter struct built with 0x821449B8 (key),
0x82144888 (int), 0x82144910 (float), 0x82144B18 (pair), 0x821446D0
(string) [C]:

| event (table) | raised by | parameters |
|---|---|---|
| hit_notes (0x829194B8) | hit 0x8211EC00, before the scoring | guitar_stream, array_entry, pattern, player_status, song (= input_array<p>), off_note, whammy_length, hammer_strum |
| hit_note (0x829194C0) | the same, per gem of the note | pos (button_up_models.<color>.pos_2d, left_pos_2d lefty), color, entry, player, player_text, hammer_strum, fx_id (fx_<g/r/y/b/o><p>) |
| missed_note (0x829194D0) | miss 0x8211F328 | guitar_stream, array_entry, player_status, song, silent_miss, bum_note |
| unnecessary_note (0x829194E8) | overstrum 0x8211F7B0 | guitar_stream, player_status, array_entry, song, songtime |
| star_hit_note / star_sequence_bonus (0x829194D8 / C8) | 0x8211E4E8 | array_entry, player_status, song |
| star_miss_note (0x829194E0) | 0x8211E8B8 (miss, overstrum) | array_entry, song |
| whammy_on / whammy_off (0x82919528 / 30) | the sustain check 0x8211C9D0 | pattern, time, guitar_stream, song, array_entry, player(_status), player_text; off: finished |
| first_gem (0x82919510) | Create2DGem 0x82121C28 | player_status, gem_id (is_star) |
| firstnote_window_open (0x82919538) | 0x82120300 | song, player_status |

bum_note is the flag 0x829BA008: set by a strum, cleared by a hit and by a
miss [C 0x8211FF38, 0x8211EC6C, 0x8211F3DC]. The C also spawns scripts
directly (0x8210BA68 {player_status}): guitarevent_multiplier4x/3x/2xon when
the run reaches 30 / 20 / 10, ..xoff (the multiplier held) when a miss or
an overstrum ends a run of 10 or more, co-op with the shared run for both
statuses [C 0x8210BF70, 0x8210C9F8, 0x8210CB10]; show_star_power_ready at
50 [C 0x8210C7A0]; star_power_activate_and_drain at the trigger [C
0x82113244 / 0x82113460]; do_starpower_camera from StarPowerOn in one-player
songs [C 0x8211DC08]; crowd_transition_sfx_<a>_to_<b> and
change_crowd_looping_sfx {crowd_looping_state player} when current_crowd
(player 1's health, two players outside battle the mean) crosses
crowd_poor_medium / crowd_medium_good, not in face-off [C 0x8210F200];
battlemode_ready / battle_trigger_on (and the boss's) on a battle phrase /
the trigger [C consts in 0x8211E4E8, 0x82112xxx]. GuitarEvent_HitNotes_CFunc
(0x82110D98) sets guitar_volume = 100 and does the health of a note without
a sustain (0x8210F6A0) and the buttons' pixels [C].

What gd04 runs now (ui/song_hooks.cpp; GD04_NO_HOOKS=1 turns it off):
- every event above, raised from the native judge's events
  (game/player.h PlayerEvent: Hit / Hopo, Miss with bum_note, Overstrum,
  StarHit, StarPhrase, StarMiss, SustainDone / SustainLost) to the handlers
  create_guitar_events registered (event_spawner -> GuitarEvent_*, id
  song_event_scripts), so GH3's GuitarEvent_MissedNote /
  Guitar_Wrong_Note_Sound_Logic (the bad-note sounds),
  GuitarEvent_StarSequenceBonus (Star_Power_Awarded_SFX, the big bolts and
  the star particles), GuitarEvent_CreateFirstGem (the flare),
  GuitarEvent_WhammyOn / Off (WhammyFXOn: the sustain sparks),
  GuitarEvent_FirstNote_Window_Open run as scripts;
- the C's spawns above as scripts: the multiplier scripts
  (UI_SFX_Lose_Multiplier_*, Lose_Multiplier_Crowd, the 4x finger FX
  script), show_star_power_ready (Star_Power_Ready_SFX),
  star_power_activate_and_drain (its broadcast star_power_on / _off ->
  GuitarEvent_StarPowerOn / Off -> GH_Star_Power_Verb_On / Off:
  Star_Power_Deployed_SFX, _Cheer_SFX, the guitar buss echo / reverb, the
  Star_Power_BussSet; Do_StarPower_StageFX; Crowd_AllPlayAnim), the crowd's
  transition sounds and loop;
- gem_scroller's fretbar iterators' GuitarEvent_PreFretbar (the Star Power
  claps) and GuitarEvent_Fretbar, player 1, at the fretbar - prefretbar_time
  - gem_offset / - gem_offset;
- the song's event arrays (scripts_array: <song>_scripts, _anim,
  _triggers, _cameras, _lightshow, _crowd, _drums, _performance at time -
  gem_offset - lead_ms) and note maps (Crowd_AutoNoteMapping: crowd
  surges / applause; Drums_AutoNoteMapping: the count-off) at their times,
  spawned with {time params} / the mapping's params + length + velocity,
  through a native equivalent of event_iterator / notemap_iterator; the
  song pak's QB is loaded into the globals first (load_songqpak);
- start_gem_scroller's GuitarEvent_EnterVenue (the venue's crowd buss echo
  / reverb) and GH3_Set_Guitar_Verb_And_Echo_to_Dry; at the end
  GH_Star_Power_Verb_Off and GuitarEvent_ExitVenue;
- player1_status / player2_status kept current from the judge every frame
  (score, current_run, best_run, notes_hit, total_notes, current_health,
  star_power_amount, star_power_used, sp_phrases_hit,
  playline_song_beat_time / measure_time; at the start guitar_volume 0,
  lefthanded_gems / _button_ups, check_time_early / late), current_crowd,
  input_array<p> as the C builds it;
- battle / boss sounds: the Match plays the cards and attacks natively
  (battlemode_ready, battle_trigger_on and the attack scripts would play
  them twice), so only their sounds run: Battle_Power_Awarded_SFX_P<n> on a
  card, GH3_Battle_Play_Crowd_Reaction_SFX and the attack's
  GH_BattleMode_Player<n>_SFX_* (Death Drain: + the heartbeat) on an attack
  [C scripts]; GuitarEvent_BattleAttackFinished is not raised [L].

Natives for them (ui/song_hooks.cpp): setsoundbusseffects (GuitarEcho1 /
GuitarReverb1 drive audio/guitar_fx: a feedback echo and a small reverb
with the scripts' delay, DecayRatio, Drymix / Wetmix, I3DL2 levels, DSP [I];
the crowd buss's CrowdEcho1 / InGameReverb1 are counted [L]). Star Power
(GH_Star_Power_Verb_On) is the only time the guitar buss is wet in a song:
echo delay = the beat (halved to <= 400 ms), Wetmix 0.5 in 0.1 s,
ReflectionsLevel -1200 / ReverbLevel -550 mB in 0.1 s, Star_Power_BussSet
(Right_Notes -2 dB in 0.5 s), Star_Power_Deployed_SFX + _Cheer_SFX; _Off pops
the busses, Wetmix 0 in 0.1 s, the reverb levels -10000 in 0.5 s [C scripts].
The reverb's late level is normalised to its impulse energy and the early
reflections are four taps (GD04-098, 11-05: the uncalibrated Schroeder
network made the reverb ~8 dB over -550 mB and a 0 ms reflection tap added
the dry signal in phase, so the guitar came out ~4 dB louder in Star Power
and clipped on loud songs; now +0.3 dB on noise, the -2 dB buss set
balancing the wet paths as GH3's data intends) [I: FMOD's DSP not reimplemented].
SetSoundBussParams (the busses below, and Right_Notes_Player1's vol: the
guitar buss's volume) / Push / Pop, Create2DParticleSystem /
Destroy2DParticleSystem (ui/fx2d: motion [I]), WhammyFXOn / Off (a
whammy_particle_params system per fret at its button), get_song_tempo_cfunc,
GetSongTime, GetStrumPattern, SafeGetUniqueCompositeObjectID; and the
CFuncs whose work the native core did, counted only: CrowdIncrease /
CrowdDecrease, NoteFX, LaunchGemEvent, UpdateNixie, StarPowerOn (the
highway colour; its camera is render/venue_camera's starPower),
KillPulsateStarPowerBulbs, GuitarEvent_HitNotes_CFunc (guitar_volume only).
Proxy elements stand for the native highway: gem_container<p> follows
Highway2D::containerPos (the scripts' bolts and particles sit in it), the
first gem's element its gem; button_up_models / button_models get
generate_pos_table's pos_2d / left_pos_2d / angle / left_angle from the
native lanes. The big bolts (sys_Big_Bolt01) are the material's 8-frame
flip-book, drawn by the hooks over the element (ui/ has no material
animation) [frame order I]. The song's scripts pause with the pause menu.

Still not shown (counted in the song hooks' log line): the 3D effects the
scripts ask for -- CreateParticleSystem(_fast) on a band member's bone
(the 4x finger flames, the Star Power stage FX), Crowd_PlayAnim (the crowd's
Star Power animation), EngineWind, the stage divers' and lighters'
composite objects -- need the venue / band / crowd renderers' support
(render/particles, render/crowd); LightShow_SetTime from <song>_lightshow
(1117 entries in 49 songs: the light show's blend time) needs the native
light show to take it (render/venue_lights reads it only from the note
mapping).

What stays native, and why: start_gem_scroller / gem_scroller themselves
(step 3). Their bodies create the 2D highway, gems, fretbars, whammy tails
and the HUD as screen elements moved by C (Create2DGem, the gem mover
0x8210E750, Set2DHighwaySpeed / Fade, the strip meshes 0x8238ECF8,
UpdateScoreFastPerFrame on ~100 HUD elements); the port's screen-element
system has no perspective rows, strip meshes, material animation or
gem-fade shading, while render/highway2d and render/hud2d reproduce them
[C]. Running the scripts would only move that drawing into ui/ at a loss
of fidelity, so the native start_gem_scroller stays, with the setup
scripts that do not need those elements run as scripts (above). Also kept
native (each [C] above): the judge, scoring, health, Star Power drain (the
script's drain is overwritten by the judge's amount every frame), the
mixer's guitar mute (UpdateGuitarVolume), the band's performance array
(render/band_anim: Band_ChangeStance / PlayAnim / PlayFacialAnim, 39
entries skipped in a quick-play song), the boss's Death Drain (Match), the
camera cuts and the Star Power camera (render/venue_camera).

end_credits: start_gem_scroller clears it unless end_credits_restart = 1 or
the song is thrufireandflames; with it set CrowdDecrease takes no health
(game/player.cpp PlayerRules::endCredits) [C script, 0x8210FD18].

## Venues, cameras, light show (M5; details in `venues.md`)

- Venue = `LevelZones.<current_level>.zone` -> `ZONES/<zone>.pak.xen`
  (`Load_Venue`); quickplay picks a random unlocked venue
  (`quickplay_start_song`), career the tier's `level` [C scripts].
- Zone pak: `<zone>.scn` + node array `<zone>_NodeArray` (.nqb), `_gfx`
  scene + nodes (lights), `<zone>_cameras*`, `<zone>_snapshots`,
  `<zone>_lightshow_mapping` [C data].
- Sector flag 0x20 = vertices local to the node of the sector's name;
  node rotation `qy * qz * qx` of `angles` [C data: matches every light's
  `rotationquat`]; y up; cameras and lights look down local +z, screen right
  = local -x [C data: camera quats project their look-at targets to their
  `screenoffset`]; FOV horizontal [C viewer `sethfov`; I for cuts].
- Camera cuts: `<song>_cameras_notes` through `CameraCuts_NoteMapping`
  (77..117: enable / disable, array prefix, forced type or name, length = shot
  length); otherwise 3..8 s to the next lightshow note 58 (downbeat); the
  intro transition cuts to `cameras_fastintro` / `cameras_intro` [C scripts
  `guitar_cameras.qb`, `guitar_transitions.qb`]. Performance arrays chosen by
  C code [I: rock meter bands].
- Light show: `<song>_lightshow_notes` (moods 70..76, colour overrides 61..69,
  snapshot change 57 / 58, strobe 60, blend time 39..53) step through the
  zone's snapshots (per light: colour, intensity, far attenuation); 100 ms
  offset; default blend 0.15 s [C scripts `lightshow.qb`].
- Materials [I]: `+0xe8` top byte alpha cut-off, `+0xd8` blend mode, `+0xd4`
  draw order; last texture = lightmap (2048x2048 single-level DXT1) [C data];
  crowd cards = `tw_billboard0N` placeholders replaced by crowd renders [C
  `create_crowd_models`].

## Rendering: GH3's shaders (GD04-007 / GD04-008; details in `venues.md`, `formats.md`)

- The material templates and their compiled Xenos shaders are one file,
  `fxfiles\MaterialLibrary.bin.xen` (Function_82384668 -> loader
  Function_82383768): 418 templates (key = qbkey(name), the scene
  material's +0xa0), 4,981 shaders, a vertex + pixel pair per technique
  [C data].
- A material's constsA / constsB are the template's vertex / pixel c0..,
  its textures the samplers in order; Function_822B18C8 / 822B1818 set a
  material parameter by name (+0xa4 the template, +0xac / +0xb4 the two
  constant arrays, flag 1 vertex / 2 pixel); Function_823A5768 uploads a
  pass: the constant ranges, the render states (device table +0x40), the
  sampler states (+0x1d4) [C].
- Engine constants: one register layout for both stages, set with
  Function_824B6698 (SetVertexShaderConstantF, dirty mask at device+0) and
  Function_824B6780 (pixel, device+8): c216 (World, WorldView,
  WorldViewProj, 12 regs), c232 View, c236 Projection, c240 InvView, c244
  ViewProj (Function_823A6190, 823A61F8, 823A6320); the device's constant shadow: vertex c_n at
  device + 0x780 + 16n, pixel at + 0x1780 + 16n [C].
- Lighting model (from the microcode) [C]: ambient c176, one key
  directional light (dir c177 = direction of travel, colour c178, w =
  the shadow map's weight on it, spec c179), the specular colours of three
  more (c181, c183, c185), L1 spherical harmonics c188..c191 (constant, x,
  y, z), g_shLightShadowIntensity c137 = the shadow map's weight on the SH;
  the venue's "lightmap" texture is m_sampShadowMap. Which lights the light
  manager puts there is VMX128 code Ghidra does not disassemble
  (Function_822B6E08) [I: gd04's choice, venues.md].
- Vertex semantics: venue meshes declare their shadow-map coordinates as
  TEXCOORD4 / 5 and the templates fetch that index [C].
- Dynamic shadows: the DynamicShadowReciever technique (cascades
  c144 / c148, maps s14 / s15, 4-tap bilinear PCF, c141..c143), shadow map
  sizes 1024 / 512 and SetDynamicShadowIntensity 0.35 from tod_profiles.qb /
  tod_manager.qb [C]; the two cascades' slices, fade, light-space
  perspective projections and biases are GH3's (venues.md "Dynamic
  shadows", GD04-106), the light gd04's [I].
- Back-face culling fixes the M5 stage-rear cameras (they stand behind
  walls) [I: Direct3D's default CCW, the templates' own states not decoded].

## Character roster, selection and the store (audit, 2026-11-24)

GH3 has **no Create-A-Rocker / appearance creator** (that arrives in GHWT /
Metallica). "Character creation" in GH3 = a fixed + unlockable character
**roster**, character **selection** (career / quick play / multiplayer band
setup), and a **store** that buys characters, guitars, finishes, outfits,
styles, songs and videos with career cash. All of it is **GH3's own QScript
running on the port's VM** (ui.md): the port supplies the screen-element /
menu-object natives and persists the globals; the roster / unlock / price /
cash logic is the original scripts' [C scripts]. **Audited faithful; no code
change.**

- **Unlock model** [C scripts guitar_globaltags.qb]: every character body,
  guitar, finish, outfit, style and video is a GlobalTag keyed by its
  `desc_id` with `unlocked` 0/1 (and `unlocked_for_purchase`); band cash is
  the GlobalTag `band_info`'s `cash` field. `setup_globaltags` (called at
  new-save setup) runs `setup_unlocks` (Secret_Guitars / Secret_Basses
  `unlocked_for_purchase = 0`; Bonus_Guitars / _Finishes / Bonus_Basses
  `unlocked_for_purchase = 1`; Secret_Characters `default_charactertags` =
  unlocked 0; Bonus_Outfits unlocked 0; Bonus_Styles every 6th (one per
  character) unlocked 1), `setup_character_tags` (Secret_Characters[0..2] =
  Slash / Morello / Satan `unlocked_for_purchase = 0`, boss-gated),
  `setup_characterguitar_tags` (per profile x player, default guitar /
  bass, falls back to Les Paul Black / LP VBRST), `setup_store_tags`
  (videos). Secret_Characters = Slash 10000, Tom Morello 10000, Satan (Lou)
  15000, RockGod 20000, Grim Ripper 10000 [C data Secret_Characters].
- **Store** [C scripts menu_store.qb]: `store_buy_character` guards on
  `unlocked = 0` (no re-buy), special-cases Satan Outfit2 needing Satan
  Outfit1 first, checks `cash >= price`, deducts cash into `band_info`,
  sets the body **and** its musician_instrument's tag `unlocked = 1`,
  autosaves. `Progression_AwardCash` (guitar_progression.qb) adds
  Cash_System_rules cash (by difficulty / new-vs-old stars) to `band_info`.
  Pages: create_store_{characters,guitars,guitar_finishes,outfits,styles,
  songs,videos}_menu. The Vault is these tags read back.
- **Selection** [C script menu_character_select.qb]: `get_valid_character_index`
  — a profile is valid iff downloaded, not `debug_only`, `Type = Guitarist`,
  and (its body tag exists with `unlocked = 0` -> invalid); the main roster
  carries no such tag so it is always selectable, secret characters are
  gated by their bought tag. `cs_scroll_up_or_down` (no wrap) / paging via
  `cs_check_if_new_page`.
- **Appearance customisation that does exist**: per-character outfit + style
  + selected guitar / bass (player_status.outfit / style,
  `current_selected_guitar` / `_bass`), bought in the store. No freeform
  editor.
- **Port backing, verified faithful**: GetGlobalTags / SetGlobalTags
  (natives_core.cpp:330 / 356), Push/PopGlobalTags sections
  (natives_core.cpp:371 / 380), SaveToMemoryCard / LoadFromMemoryCard ->
  save.json tags + sections (natives_flow.cpp:94 / 102, game/save.cpp). The
  lookups the scripts use (get_musician_profile_size / _struct,
  is_musician_profile_downloaded, find_instrument_index,
  is_musician_instrument_downloaded, get_current_band_info,
  find_profile_by_id) are GH3 **scripts**, run as-is. The port adds only:
  the neighbour-prefetch heuristic (natives_game.cpp:95, mirrors
  get_valid_character_index's Guitarist / not-debug_only / body-tag-unlocked
  filter), `cs_check_if_new_page` for retail's 13-icon page (GD04-094,
  natives_flow.cpp:182), the store character-name layout fix (GD04-099), and
  The Glitches Vault isolation (ui/glitches, GD04-026 / 096).
- **Objective evidence** (bl04 build, headless `--walktest`, isolated
  XDG_DATA_HOME): `store` passes all six pages (finishes / songs / characters
  / outfits / styles / videos) with their models and character_name_text
  centred at x 897.5 (GD04-099 check); `vault` passes — The Glitches' band 5
  reads Instrument_focus01.unlocked = 1 while the save view and band 1 read
  0 (GD04-026 / 096 isolation); `career` passes (character -> hub -> song ->
  newspaper mag_photo = Guitarist_Axel_Outfit1_Style1 from axel_1 ->
  cash_reward); `faceoff` passes (character select scrolls Axel / Casey /
  Izzy / Judy); `cutcontent` passes (paging `cs: jumped 0 icon 12 total 13
  name "GRIM RIPPER"`).

## The band (M6; details in `band.md`)

- `create_band` (one player): guitarist from `player1_status`
  (character_id, outfit, style -> `Guitarist_<name>_Outfit<o>_Style<s>`,
  instrument_id), drummer, the song's `bassist` (else 'bassist'), the song's
  `singer` (none / female / bret; `_alt` microphones in z_wikker, z_budokan,
  z_hell); placed at `<zone>_TRG_Waypoint_<member>_start` [C scripts].
- Members are `Musician_Profiles` entries; bodies / instruments are
  `musician_body` / `musician_instrument` descs naming `PAK/<x>.pak.xen`;
  every skin (body, guitar, bass, mic, drum set) is skinned to the member's
  skeleton in `ZONES/global.pak` [C data: 193 skins].
- Clips: rotations absolute, compressed translations are offsets from the
  bind translation, single-pose clips absolute, flags 0x200 = difference
  clip (the hands' `_D` anims) [C data; I: the composition order]. Body clips
  leave the arms to IK onto `Bone_IK_Hand_Guitar_L/R`, which the fret
  (`BONE_GUITAR_FRET_POS`), strum and chord clips move [C data].
- Body anims: `anim_set.<stance>.<anim>.<skill>.<speed>`, random without
  repeating the last; skill by player 1's health (0.66 / 1.33), speed by
  tempo against the means of slow/med/fast_tempo (95 / 130 bpm) [C
  0x82108E58, 0x82108C90, 0x82108B60]. Stances and anims from the song's
  `<song>_performance` (Band_ChangeStance, Band_PlayAnim,
  Band_PlayFacialAnim, Band_WalkToNode) [C data].
- Hands: strums on every expert gem 250 ms early (Short < 150 ms < med <
  600 ms < Long), fret positions from `<song>_anim_notes` 118..127 / 101..110,
  chords from the gem's colours released 10 ms after the gem [C scripts,
  FretFingers 0x821119F8]. Drummer: drums_notes 37..45 / 49..57 arms 150 ms
  early, cymbals 41..45 / 53..57, body twist toward the mean of the arms'
  factors at 0.05 a frame [C 0x821121D8, 0x82107F48].
- Cameras with `lookat` / `lookatbone` / `screenoffset` now aim at the live
  bone [I: GH3's tracking and smoothing].

## Rendering R2: the light manager, screen effects, stage peds (details in `venues.md`, `band.md`)

- The light registers c176..c191 are 823DA990 / 823DB168's (VMX128 code,
  read through `tools/vmx128.py`): candidates by light box and group
  (defaultgroup everywhere, band for alt_band), ranked by luminance x
  intensity x attenuation (x 50000 `shadow`), the first directional / point
  light the key light (not in the SH), the next three the specular slots
  (in the SH), L1 SH with 4/17 and 8/17, spot lights SH-only by their
  unnormalised axis distance, ambient summed; lights shine along
  rotationquat x (0, -1, 0), default intensity 1, attenstart / attenend 7.5
  / 15, attenuation linear in the squared distance; snapshots' FarAtten*
  are not applied [C].
- The venue viewport's ScreenFX run in insertion order on 8-bit targets:
  venue_DOF (quarter-size blur, lerp by strength x the linear depth over
  backdist x far), BnS (an identity bright_sat: hue 1 is a whole turn,
  alpha = red), sbloom (materialfilter: the colour where the material
  stencil (+0xdc) is 1, quarter-size, two 7-tap blurs with gain falloff1 = 5
  then 1, added as 0.35 x B x B.a) [C]. gd04 runs them with the engine's own
  filter techniques of the material library.
- Star Power (StarPowerOn, one player) switches the cameras to
  cameras_starpower (ForceChangeTime 2.5 s, camera notes off) for 7 s [C].
- Stage peds and moving level objects come from the nodes' trigger scripts
  (GameObj_PlayAnim, obj_rot*, obj_movetopos, CallScriptOnNode,
  AddPulseEvent); the stage divers are hidden at the start; lighters are 12
  songs' Crowd_StartLighters spans [C scripts].
- The three water templates whose shaders loop are used by no scene on the
  disc [C data: every zone and character material].

## Sound busses (2026-11-05)

GH3's FMOD channel groups, built by Master_SFX_Adding_Sound_Busses at boot
(CreateBussSystem <BussTree>, SetSoundBussParams <Default_BussSet>, then
SoundBussLock on master, User_Guitar / Band / Sfx / Music, Crowd_Beds,
Crowd_Singalong, Band_Balance, Guitar_Balance, Music_Setlist) [C scripts,
data sound_buss.qb]; the port's audio/sound_buss.h:
- the tree: master > User_Sfx (> ui > Front_End, Pause_Menu, UI_Star_Power,
  UI_Battle_Mode, Wrong_Notes_P1/P2; > Crowd > Crowd_W_Reverb > Crowd_Singalong,
  Crowd_One_Shots; Crowd > Crowd_Beds ... Crowd_Transitions; default,
  Test_Tones, Encore_Events, binkcutscenes, Practice_Band_Playback,
  Training_VO), User_Band > Band_Balance (> Band_Playback,
  Single_Player_Rhythm_Playback, music_frontend, Music_Setlist, Countoffs),
  User_Guitar (> Guitar_Balance > Guitar_Balance_First_Player >
  First_Player_Lead_Playback, Guitar_Balance_Second_Player > Second_Player_*;
  Test_Tones_DSP, Right_Notes_Player1/2): 45 busses [C data].
- vol is dB-like: 100 * 2^(v/6) percent, at most 400 %, silent at -100 or
  below [C 0x8217AFB0]; pitch is semitones: 100 * 2^(p/12) percent, silent at
  -48 or below, at most 1600 % [C 0x8217B0B8]. A buss sounds at its own and
  its ancestors' levels together (FMOD channel groups) [I]; a change with
  `time` fades linearly [I].
- SoundBussLock: SetSoundBussParams leaves a locked buss alone (the scripts
  always unlock, set, lock: menu_audio_settings_update_*, preload_song,
  GH3_AdjustCrowd*Surge, end_song, create_attract_mode) [I from that pattern].
- Default levels: Front_End -6 (with master -2.5: menu and intro sounds at
  -8.5 dB), Crowd -1, Crowd_PreSong_Intro -6 (the zone's intro crowd at -9.5),
  Crowd_Beds -7, music_frontend -11 (the menu music at -13.5), Music_Setlist
  the song's band_playback_volume (setlist_songpreview_monitor) [C data].
- Who plays where: SoundEventFast on <event>_struct's default_buss, PlaySound
  on its buss=, else `default` [I]; the song's stems as startpreloadpaused_song
  (player 1's part First_Player_Lead_Playback, the other part
  Single_Player_Rhythm_Playback, or Second_Player_Rhythm_Playback with two
  players on different parts, the song Band_Playback) [C script];
  preload_song's Band_Balance / Guitar_Balance = the song's
  band_playback_volume / guitar_playback_volume - 1.5 dB, set by the native
  start_gem_scroller [C script preload_song]. Stop / Pause / UnPauseSoundsByBuss
  reach the busses below (PauseGh3Sounds pauses master) [I]. The song's stems
  take no buss pitch (Failed_Song_Pitch_Down runs after the port's song has
  stopped) [L].
- The audio settings: ChangeSpinalTapVolume (0..11, the knob) feeds
  menu_audio_settings_get_buss_volume (11 = 0 dB ... 1 = -21, 0 = -100) into
  User_Band / User_Guitar / User_Sfx [C scripts; I: the CFunc's sum within
  0..11]. Unhandled before, the knobs never moved.
- Measured (quickplay walk, GD04_AUDIODUMP): the intro's Highway_Rise rises
  +5.1 dB over the crowd intro under it (was +4.4 before the busses),
  Notes_Ripple_Up +1.6 (+1.4), Song_Intro_Kick +9.5 (+9.1); every sound
  effect is now 8.5 dB (Front_End) or 9.5 dB (crowd) under its old level;
  the song by master -2.5 and its Band_Balance / Guitar_Balance (Slow Ride's
  band_playback_volume -3, guitar_playback_volume -4: -4.5 / -5.5 dB), about
  7-8 dB under its old level in all. Compressor_Master_Buss (GAINMAKEUP 12)
  is defined but no script creates it [C data].
- GD04_BUSSLOG=1 logs every buss change (development aid).
- Practice's section preview (GD04-105): create_choose_practice_section_menu
  (end_practice_song_slomo first: current_speedfactor 1) spawns
  practice_songpreview_monitor; when the highlight's section
  (menu_choose_practice_section_refreshsongpreviewposition:
  target_practice_songpreview_starttime = <song>_markers[index - 1].time, 0 for
  the full song) changes, it fades Band_Balance / Guitar_Balance to -20 dB in
  0.5 s (if one played), end_song, preload_song starttime fadeintime = 1.0
  (the busses back to the song's playback volumes - 1.5 dB over 1 s; the
  streams on startpreloadpaused_song's busses at pitch current_speedfactor *
  100, sought to the marker by SetSeekPosition_Song), begin_song;
  practice_songpreview_fadeinandrepeat restarts it at the section's end. The
  speed menu keeps the section menu (and the preview); its destroy (the speed
  chosen, Back) waits for practice_songpreview_changing = 0, kills both
  scripts, end_song [C scripts]. end_song's StopStream {unique_id =
  <song_unique_id>} has no such local, so StopStream gets neither a
  unique_id nor a name: StopAudioStreams, every stream [C 0x8227E580 ->
  0x8223EDE0(1), as StopAudioStreams 0x8227E668]. The port runs these
  scripts; ui/song_preview.cpp's PreloadStream / StartPreloadedStream /
  SetSoundSeekPosition / PauseSound / StopStream play the song's SongStream on
  the song mixer while no song is (Mixer::play at the song's start resets the
  speed); walk practice checks it (check:preview).

## Career mode audit (2026-11-24): faithful, no fix

A full career audit (tier/setlist progression and the gating, boss battles,
encores, co-op career, unlocks/store/cash/stars, the menus / screen-flow,
save/profile). Ground: the static Ghidra export (`ghidra/analysis/decomp`,
functions.tsv), the decoded QB, and headless walks. REA was not used (it
cannot open the XEX2). **Verdict: faithful.** The career layer runs GH3's own
QB scripts (progression, store, cash, stars, the newspaper, the flow states)
over the native shims; only the atom VM is a reimplemented driver, and it
matches the binary. No code change.

- **Atom VM** (`script/atoms.cpp`), the one reimplemented career driver,
  checked instruction-for-instruction against the XEX and found faithful:
  the condition met-test `met()` = Function_8239E740 (`required[diff] == 0`
  -> met, else state == 0x4e29e602); the conditions loop `conditions()` =
  Function_8239EBE0 (re-evaluate each condition via Function_82422B18 in
  order, break on the first unmet); the permanent-skip = Function_82422B18
  (vtable+8 "permanent?" AND already-met -> skip evaluation), mirrored by the
  guard `!(permanent && !isAtom && met(d))`; the per-atom update `step()` =
  Function_8239F040 (Locked + conditions -> tool_only complete (8239E8F8)
  else active+spawn+runNow (8239EDB8); Active + script ended -> complete);
  tool_only on a retail disc = Function_8239E960 (DAT_82a6ca11 "is CD"); the
  complete/relock `complete()` = Function_8239E8F8 / 8239F158 (disabled
  waiters relocked, deps reset, updated at once); the UpdateAtoms walk
  `update()` = Function_822B0978 (every atom in order; one whose script is
  still running stops the pass). State constants: Locked 0x152d376e, Active
  0xb4e103fd, Complete 0xbd982fdc, Disabled 0x7b01911c; condition true
  0x4e29e602 / false 0x0f636d0d. SetProgressionDifficulty (Function_82270C90)
  stores the raw numeric `difficulty` param (no key->index conversion), which
  `natives_misc.cpp`'s `Vm::number` matches; it feeds `met()`'s `required[diff]`.
- **Boss / battle outcome routing** (`game/match.cpp` 699-730, `app.cpp`
  1299-1302, `ui/front_end.cpp` 170): GameMode::Boss is distinct from Battle,
  so a boss defeat (h2 <= 0 -> P1Wins, "song_wonp2") takes the won path ->
  `GuitarEvent_SongWon`; a player loss (h1 <= 0) -> Failed ->
  `GuitarEvent_SongFailed`; a regular Battle KO (healths differ) sets
  `battleKo` -> `GuitarEvent_SongFailed` (GH3 then raises GuitarEvent_SongWon
  battle_win = 1). Matches modes.md §5/§6 [C 0x8210FD18].
- **Save** (`ui/natives_flow.cpp` SaveToMemoryCard / LoadFromMemoryCard):
  serialises the whole tag store -- `glitchesSaveView` (global tags) AND
  `globalTagSections` (the per-band career sections) -- so no career field is
  dropped. Cash, stars and store prices are GH3-script-driven (no native
  `AwardCash`): faithful by construction given GetGlobalTags / SetGlobalTags.
- **Objective evidence** (headless, build/linux, disc data): walk `career`
  passed (band -> difficulty -> character -> venue -> setlist -> song -> won
  -> newspaper -> cash_reward -> autosave -> setlist; 1 song); walk
  `career_encore` passed with every embedded check (num_songs_to_progress
  2->1->0, careertier1.complete 0->1, g_encore_ready = 1, encore =
  rocknrollallnite, progression_completion_tier = 1, reaching
  career_sponsored_fs; 4 songs) -- the atom-driven tier -> encore ->
  tier-complete -> sponsor chain; walk `boss_devil` passed (the devil beaten,
  the song restarts at 336682 ms "Lou's finish", then the newspaper; 2 songs).
## Multiplayer audit (2026-11-24): faithful, no change

A full re-audit of local multiplayer (face-off, pro face-off, battle, co-op
career, boss battle) against the disc's QB and scripts (`qb.py` / `qbscript.py`
on `COMPRESSED/PAK/qb.pak.xen`) and the XEX addresses already in this file.
Found faithful; no code changed. Ground truth confirmed and recorded here:

- **Battle power-ups (`battlemode_powerups`, scripts\guitar\guitar_battle.qb)**
  are exactly **8**, in this order: Lightning / DifficultyUp / DoubleNotes /
  PowerUpSteal / LeftyNotes / BrokenString / WhammyAttack / DeathLick [C data].
  Weights: all `weight 10` except DeathLick `0`; `weight_low 0` throughout;
  `weight_losing 10` only for PowerUpSteal / LeftyNotes / WhammyAttack (else 0).
  drain_time 7500 Lightning, 10000 DeathLick, 5000 rest. flicker 2/2/3/3
  (Lightning); repair 6/8/10/12 (BrokenString), 3/4/5/6 (Whammy); kill_rate
  0.4/0.3/0.25/0.2 (DeathLick). `immediate` Lightning / BrokenString / Whammy;
  `no_ai_damage` Whammy; `fire_bolt` all but PowerUpSteal / DeathLick.
  `max_num_powerups 3`; `battle_{easy,medium,hard,expert}_drain_offset`
  0/1500/3000/4500. This is GH3's canonical 8-card set (incl. Death Lick and
  sudden death), the opposite of Metallica/gd10's 9 -- the port matches it
  exactly: game/match.h `enum Attack` and `BattleData::load` require size 8,
  game/match.cpp reads every field [C].
- **battlemode_select / bossbattle_select** (disassembled): weight-sum roll,
  skip DifficultyUp vs an expert victim, `weight_low` for the last-selected and
  the top-of-hand cards, `weight_losing` when health <= 0.5 (human only; the
  boss path has no health gate and filters by `current_boss.PowerUps`),
  sudden death forces DeathLick (index 7). game/match.cpp `select()` matches,
  incl. the bossRules branch for both the human's and the boss's cards [C].
- **battlemode_ready**: a full hand destroys the top card (index
  current_num_powerups-1), then stores the new card there -- i.e. the new card
  replaces the top, older cards kept. game/match.cpp `award()`
  (`cards.pop_back(); cards.push_back(card)`) matches [C].
- Face-off section filter + `last` from the full chart (GD04-004) intact
  (game/note_track.cpp `faceOffTrack`); score scale, co-op shared run / shared
  SP / fail-on-average, the 2P HUDs (co-op sum score + 74x(avg-1) needle,
  face-off +-65 score-gap needle, battle +-42 per-player needle) all as in
  `modes.md` [C].
- **gd10-lesson check (local-MP setup reaching a playable song): PASSES.**
  Unlike Metallica's unimplemented band-mode lobby, GH3's local-MP hubs advance:
  the `battle`, `coop` and `boss_keys` walks (dev_ff build, FFmpeg prefix,
  offscreen) all reach `song` -> `song_over` -> newspaper. Evidence (walk logs):
  - battle: 2 songs; song 1 both perfect (70/70, 64/64) -> tie -> sudden death;
    song 2 Death-Drain-only, P2 drained to health 0 -> KO, `outcome 2` P1 wins,
    UI_1stPlayerWins_SFX; all 8 attacks observed (SFX log), dead frets repaired.
  - coop: both parts 38/38 notes (shared total), separate scores 3652 / 2738,
    streaks 24 / 14, health 1.644 / 1.397.
  - boss_keys (real key input): 178/181 keys hit, 10 cards got + fired, 6 boss
    attacks received, 3 fret recoveries (none dead at end), 58/60 hit after the
    last attack, whammy bar drawn; boss bot 40 forced misses -> KO, P1 beats the
    boss. All three walks: `passed`.
## UI/mechanics re-audit (2026-11-24): HUD, scoring, judge, mouse — faithful

A verification pass over the in-game HUD and the core mechanics against the
XEX (Ghidra analysis export; REA cannot open the XEX2). Each subsystem was
cross-checked binary-vs-port source; verdict **faithful** for every area
below. No code changed.

- **Scoring / multiplier** [C 0x8210BB40, re-read]: the fret count is bits
  0x10000 / 0x1000 / 0x100 / 0x10 / 0x1 of the pattern; the run multiplier is
  `iVar6 < 10 -> 1, < 0x14 -> 2, < 0x1e -> 3, else 4`; star_power_used
  (key 0x92be3d33) shifts it left once (x2); score += scale (0x829ba010) x
  points_per_note (0x829cd708 = 50) x multiplier x fretcount. game/player.cpp
  `PlayerStats::multiplier()` (run < 10/20/30, x2 on Star Power) and `hit()`
  (`kPointsPerNote * multiplier() * fretCount()`) match; the face-off scale
  `(base_p1 + base_p2) x 0.5 / base_p` is applied in `addScore` via
  rules_.scoreScale (match.cpp:204) [C 0x8210B9A0]. Co-op (both runs,
  both notes_hit/total_notes, both runs reset on a miss) match [C 0x8210A748].
- **Judge** (window / HOPO / tap / chord / overstrum / sustain): game/player.cpp
  `frame()` / `updateSustain()` match the rules already confirmed in
  §§1-5 above (hit window, split_late cap, tap candidate = highest held fret
  != last-hit mask, post-tap ignore window, overstrum-kills-sustain, sustain
  points in chunks of >= 2). No deviation found on re-read.
- **In-game HUD numeric drivers** (UpdateScoreFastPerFrame 0x821167B0, the
  per-part updates read into agents/scratch/hud_spec.md §1.3):
  - Score text, Star Power tubes (per = 100/6 [C 0x82007A78]).
  - Multiplier progress lights [C jump table 0x821172D8]:
    `L = run>30 ? 10 : run>20 ? run-20 : run>10 ? run-10 : run`, `full = L/2`;
    L<=1 -> all off, L odd -> halflit[full], allwaylit[0..full-1].
    render/hud2d.cpp line 269 / 370-383 reproduce this exactly (including the
    full-at-the-boundary note, which is the binary's own behaviour).
  - Nixie digits (list {1,2,3,4,6,8}, 'b' in Star Power).
  - Rock-meter needle (1P) [C 0x82116D08, 0x82007A5C/58/68/6C]:
    `ang = (S-h)/S x -84 + 42`, zones T1 14 / T2 31.5; red/green health
    multipliers 1.35 / 0.85 [C 0x821062A8]. render/hud2d.cpp lines 184-202
    match.
- **Two-player / co-op / face-off HUD** (render/hud2p.cpp): the co-op needle
  uses `ang = 74 x (havg - 1)` = `((d1+d2) x 0.5 / S) x -148 + 74` with zones
  T1 24.6667 / T2 55.5 [C 0x82007A60..74]; face-off needle
  `clamp(diff x -0.0065, +-65)` [C 0x82116AC4]; the single-player fallback
  `-84/+42`. All three match the binary; the co-op wider gauge is correctly
  switched by mode (hud2p.cpp:114-115, 306-316).
- **Mouse-clickability** (the recurring bug class, docs/guidelines/usual-bugs.md):
  app.cpp `menuMouse` + ui_system.cpp `menuAt` hit-test **every** focusable,
  non-hidden child of every focused menu generically by its drawn bounds
  (empty items resolve to the text their focus handler highlights), and
  `helperPills` / `helperAt` cover the whole visible help-bar pill (glyph,
  text and backing). No menu depends on a hardcoded hit list. Faithful to the
  universal rule.

Not re-covered here (other agents / earlier passes): career mode, multiplayer
rules, character select/roster, venue lighting, character animation. Known
open item unchanged: the store main menu's "20 px high" layout discrepancy
(ui.md §10) is still not root-caused in the VMenu/VScrollingMenu layout code
[C 0x82315FB0 / 0x82317640 read]; left for a future pass, not a confident fix.

Method note: this was a static binary-vs-source cross-check (no fresh gd04
build, to respect the one-build/RAM/token budget); the runtime paths are
already exercised by the repo's walks/self-tests and recent GD04-121/122 runs.

## Character animation, collision, physics audit (2026-11-24)

Audit of the band's animation, collision and ragdoll physics against the
original. Read with the Ghidra headless export (`ghidra/analysis/decomp`,
REA cannot open the XEX2), the ragdoll data through `tools/havok.py`
(`ragdoll_axel` of `global.pak`) and the QB through `tools/qb.py`. Code:
`engine/src/render/band_anim.*`, `render/band_ragdoll.*`, `render/band.*`,
`data/havok.*`. **No code change: the three areas are faithful** (the one
gap, accessory-vs-body collision, is blocked from grounding by VMX128 and is
immaterial by the data's own constraint limits, below).

### Animation -- FAITHFUL [C as cited in "The band" / `band.md`]
- Body loop (`hero_play_tempo_anim_cfunc` 0x821090B0 / 0x82108E58, skill
  0x82108C90, speed 0x82108B60), stance transitions, `Band_PlayAnim` /
  `ChangeStance` / `PlayFacialAnim`, strum / fret / finger-chord iterators
  (FretFingers 0x821119F8), the drummer's arms / cymbals / twist
  (0x821121D8, 0x82107F48), facial loop, IK (0x823F78E0 / 0x823F65C0),
  `hero_ConstraintBones`, difference-clip order and win/lose/special all
  match the RE in `render/band_anim.cpp`. No pop / wrong-clip / retarget /
  timing defect found.
- Probed one edge case: the drummer body twist is applied with
  `evalDelta`, which early-returns on `strength <= 0`, so a negative twist
  would be dropped. Checked the data: every `drummer_{left,right}_arm_twist_
  factors.track_*` is in [0.0, 1.0] (qb.pak), so the mean is >= 0 and the
  twist is one-directional by design -- the early-return never drops a
  meaningful value. Not a bug.
- Out of scope (noted, not animation): the `modes.md` S9 items
  (card-slide / lefty-flip-button-slide 2D HUD approximations, and
  `GuitarEvent_BattleAttackFinished` -- a battle-DSP-reset sound, not a
  character anim).

### Physics (ragdoll / accessory dynamics) -- FAITHFUL [C 821AFC08, 823FE140, 823FFC28, 82253B10]
- `render/band_ragdoll.cpp` reproduces GH3's Havok 4.6 setup: world gravity
  (0, -9.8, 0) [C 82253B10 -> 8200E4E4], transformed to the member's model
  frame in `render/band.cpp:112`; 4-iteration solver (tau 0.6, damp 1);
  accessory bodies MOTION_STABILIZED_SPHERE_INERTIA with their constraint
  to the parent in the world, the rest hard-keyframed; pose read one frame
  behind; `checkAndFixConstraint(0.05, 0.8)`. On by default (GD04_NO_RAGDOLL
  off). The `.rag` ball-socket / ragdoll twist+cone+planes / limited-hinge /
  prismatic constraints, masses, sphere inertia, damping and velocity caps
  are all decoded (`data/havok.cpp:488-527`) and enforced in `solve()`.
- Verified the port decodes the SAME constraint limits the original data
  carries: `data/havok.cpp` reads coneMax, twist min/max, planes min/max,
  their isEnabled flags, axes (twistAxisInA / refAxisInB) and
  `angleMeasurementMode` (aligned vs perpendicular); `band_ragdoll.cpp`
  `solve()` applies them (`coneRow`, `limit`). A decode bug here would let
  the hair swing free -- none found.
- Frame-rate independence and NaN-freedom are the repo's objective gate
  (`tests/chartest.cpp:219-266`: accessories sampled every ms, worst 1 ms
  move < 12x the mean; GD04-104). No change made, so that baseline stands.

### Collision (accessory hair/cloth vs body) -- documented LIMITATION, immaterial; NOT a groundable defect
- The usual-bugs rule (hair/cloth must not penetrate the body) was judged
  objectively from the data, not by eye:
  - `ragdoll_axel` (35 bodies): the body is capsules (Pelvis/Chest/Head/
    arms/legs), the accessories (Bone_ACC_01..15) are small boxes that at
    the bind pose sit FLUSH on the body surface (e.g. chest accessory
    ACC_09 at z=0.172 vs the Chest capsule surface ~z=0.166, ~6 mm; the
    ACC_01..08 hair boxes just above the Head capsule).
  - Each accessory's ragdoll constraint has a TIGHT cone/twist/planes
    limit: hair (ACC_01..08) cone <= 7-30 deg (most <= 10-15), twist +-5,
    planes +-0..25; chest/stomach accessories cone <= 30-35, planes
    +-10..30. These bound each segment to a few cm of its rest pose, so the
    hair physically cannot swing far enough to grossly penetrate a
    non-adjacent body part. The port enforces these exact limits, so its
    secondary motion stays within the same envelope.
  - Adjacent (constraint-connected) accessory<->parent pairs do not collide
    in Havok by default (disableCollisionsBetweenConstraintBodies), which is
    why the flush rest contact is intended, not a penetration.
- So the "colliders on, no penetration" requirement is satisfied here by the
  constraint limits, which the port reproduces -- not by narrow-phase
  collision. The port implements no accessory-vs-body narrow-phase
  (`band_ragdoll.*`), as already noted in `band.md` S6b.
- Whether GH3 additionally enabled non-adjacent accessory-vs-body collision
  CANNOT be grounded: the world setup (Function_82253B10) and the
  collisionFilterInfo computation (Function_823360F0) both truncate on
  VMX128 "bad instruction data" in the decomp, and the layer collision
  matrix was not found (confirmed this audit). Per the owner rule (fix only
  confidently-groundable defects, do not invent), narrow-phase collision is
  left unimplemented -- a known limitation, not a defect to fix blind.
## Venue light show audit (2026-11-24)

A full re-RE of the light-show processor pipeline against the disc data and the
XEX, with the gd10 "dead-mood / processor-table" bug class as the lead
hypothesis. Sources: `qb.pak.xen` (globals), the eight zone paks'
`<zone>_lightshow_mapping` / `<zone>_snapshots` / `<zone>_lightshow_processors`,
`dbg.pak.xen` (symbol table), and the XEX decomp (`ghidra/analysis/decomp`).

- **Processor-table / inline-processor (the gd10 bug): FAITHFUL.** GH3's
  `<zone>_lightshow_mapping` stores an inline `processor` (qbkey 0xd63fb9af) on
  **every** mood x performance entry, in all ten zones (checked: artdeco,
  budokan, dive, hell, party, prison, video, wikker, soundcheck, credits -
  0 entries missing a processor). `LightShow_InitEventMappings` (script
  0xdb830c61) registers `LightShow_SharedProcessors` (global 0xa1b494ab, 10
  entries: `name` 0xa1dc81f9 -> enter 0x3be47b24 / event 0xb93ab1ed / exit
  0x072c7f7e scripts) plus the optional `<zone>_lightshow_processors`, then
  calls `LightShow_SetParams` (native 0x82128c50) perf=medium mood=intro. There
  is **no `default_generic`** in GH3 and no defaulting of a missing processor
  (that is the Metallica/gd10 addition). The port reads the inline processor
  (`venue_lights.cpp:202`) and hardcodes each processor's behaviour by name; as
  every entry carries the name, no mood is dead. The two non-obvious moods
  0x79d8d4b6 / 0x4f35c489 are the song-lose / song-win moods, processors
  `Fail_Generic` (0xc29f7b4d) / `Win_Generic` (0x7d17e328); both share the
  generic enter/event/exit, so the port's generic treatment is correct.

- **Blackout / Flare intensity override: BUG (FIXED).** `Blackout_Generic`
  (enter 0x7236971a) and `Flare_Generic` (enter 0x5e1e8383) call
  `LightShow_AppendSnapshotParams { intensity=0.25, specularintensity=0.25 }`
  before their single `LightShow_CycleNextSnapshot`, held until the mood's exit
  `clear` (a `GetPakManCurrent` switch skips it only for z_soundcheck /
  z_training / z_viewer). `LightShow_AppendSnapshotParams` (native 0x82125148)
  stores the params into slot DAT_82a307d4; the snapshot applier (0x82128e28)
  merges that slot into every played entry, **overriding** the per-light value.
  Proven override (not default) by the strobe case: the saved lit snapshot's
  stored intensities (e.g. z_dive verse 0x1df0cdc0: 0.5/2.0/1.0/...) are all
  shown at 1.0 while `AppendSnapshotParams { intensity=1.0 }` is active - the
  port already modelled that one inline. The blackout snapshot (z_dive
  0x2dff7ec7) stores intensity 0.5/0.5/0.0..., so GH3's blackout/flare moods
  show every light at 0.25 intensity + 0.25 specular; the port used the stored
  values, so both moods were too bright and too shiny in all seven venues that
  use the shared processors (blackout: all but z_wikker; flare: all but
  z_prison). Fix: a general append-params override
  (`venue_lights.cpp` enterMood sets 0.25 for Blackout_Generic / Flare_Generic
  off the debug zones; play() applies it like the strobe 1.0),
  `venue_lights.h` `appendIntensity_` / `appendSpecular_`. --venuetest: all 8
  venues still 1016/1016 checkpoints identical at 60 vs 240 fps.

- **Custom venue processors Z_Prison_Flare / Z_Wikker_Blackout: partial gap
  (NOT fixed - staging, not light values).** z_prison's `flare` mood uses
  `Z_Prison_Flare` (0x7fe26da1) and z_wikker's `blackout` uses
  `Z_Wikker_Blackout` (0x3eadcdd6), defined in their `<zone>_lightshow_processors`
  (other zones' tables are empty). Their enters cycle one snapshot (save=false,
  one-shot) and their events do not cycle, and both mood lists are a single
  snapshot - so the port's generic treatment produces the **correct light
  values** (verified). What the port misses is the scripts' venue-specific
  extras, which the port never runs because the LightShow class does not execute
  processor enter/event/exit QScripts: `Z_Prison_LightShow_Flare_Enter`
  (0x56847ae3) fires `screenflash` (ScreenFlashOn), `Z_Prison_LightShow_Flare_Event`
  (0xd45ab02a) swaps the Z_Prison_Flare_Skeleton01 ped and plays anims, and
  `Z_Wikker_LightShow_Blackout_Enter`/`_Exit` hide/unhide the flower petals. The
  port implements the ScreenFX_* / FlashBS natives (particles.cpp) but nothing
  triggers them from the flare mood (its only caller is the flare processor
  enter). Left as a documented gap: wiring it means running the venue lightshow
  processor scripts on the venue VM on mood transitions.

- Other areas FAITHFUL: snapshot field keys (lightchecksum 0x7ce4d954, r/g/b,
  intensity 0x164d476d, specularintensity 0xb9832749, shadowintensity
  0xa0bad95d; FarAttenStart/End present but not applied - correct); generic
  (enter 0xbd9fb969 cycle save=true, event 0x3f4173a0 cycle on `snapshotchange`
  0x1b36b057) and Poor (event 0x1cd91b07 waits but does not cycle) match the
  port; `lightshow_statenodeflagmapping` groups `performance` 0x7d28697e /
  `mood` 0xfcc65109 and `lightshow_coloroverrides` (8 colours, no black/off)
  match the port's reads.
## Singer (vocalist) animation audit (2026-11-24)

Full Ghidra/QB audit of the SINGER's movement and animation against the port
(render/band_anim.cpp, the vocalist path). The vocalist tree is
`constraintbones( ragdoll( ik(Singer_IK_Params)( partialswitch( face, body ) ) ) )`
with `state = $ON` [C guitar_anim_tree.qb vocalist_static_tree]. Profiles
Singer / Singer_Bret / Singer_Alt use `singer_male_animations`, Singer_Female
uses `singer_female_animations`, all `Type = $vocalist`, `stance = $Stance_A`,
no `ik_params` / finger / fret / strum fields [C guitar_band_profile.qb].

- **FIXED - female singer stance transitions skipped.** The singer alternates
  Stance_A (not singing, `*_nosingidle*`) and Stance_B (singing, `*_singidle*`,
  `*_singsolo*`), switched by the song's `Band_ChangeStance` events, with a
  transition clip between (nosing_to_sing / sing_to_nosing)
  [C guitar_animation_data_singer_male/female.qb; welcometothejungle_performance
  vocalist Stance_B at 4766, barracuda 4 switches]. `anim_set.<old>.transitions.
  <new>` is a clip key for the **male** singer and the guitarists, but the
  **female** singer stores it as a skill/speed tree
  `transitions.<new>.<skill>.<speed>` (the `good` skill using `_hop` variants),
  resolved by play_stance_transition_cfunc (0x821138D8) like a tempo anim
  [C guitar_animation_data_singer_female.qb; it is the only character with this
  form]. band_anim.cpp's bodyFinished read the entry as a plain key
  (`keyOf(trs->find(new))`), which is 0 for a struct, so the female singer
  played NO transition and snapped between stances. Fix: `Animator::
  transitionClip` resolves either form (skillKey / speedKey shared with
  tempoAnim). Objective (barracuda, Singer_Female, z_dive, GD04_ANIMLOG):
  before, Stance_A idle ended 30567 ms and the Stance_B idle began at 30567 ms
  (gap 0, no Trans clip); after, GH3_Singer_Fem_Trans_NoSngSng (2.87 s) plays
  30567->33433 ms, then the Stance_B idle (measured 2866 ms gap = the clip's
  data duration). sing->nosing at 151900 ms resolved GH3_Singer_Fem_Trans_
  SngNoSng_hop (good skill). Male singer (welcometothejungle) unchanged:
  GH3_Singer_Male_nosing_to_sing 1.63 s / sing_to_nosing 2.40 s still resolve.
  chartest 60/240 fps: 1097 checkpoints identical, 0 off screen (frame-rate
  independent). Files: render/band_anim.cpp (transitionClip, skillKey,
  speedKey, bodyFinished), render/band_anim.h.

- **Faithful - body loop.** `bandmember_idle` spawns `hero_play_adjusting_
  random_anims {Anim = idle}`; the singer's event table
  (BandMember_Idle_EventTable) routes only `play_anim` -> handle_play_anim and
  `change_stance` -> handle_change_stance (no strum/fret/star-power) [C
  guitar_character.qb]. The port's bodyIteration / playAnimEvent /
  changeStanceEvent match; anim_set.<stance>.<idle|special|Jump>.<skill>.<speed>
  random no-repeat [C].

- **Faithful - facial / lip-sync.** `facial_anim_loop` plays
  `anim_set.facial_anims.idle` at random (+1 gameframe) [C]; `Band_PlayFacialAnim`
  (guitar_midi_anim_funcs.qb) spawns `play_special_facial_anim {Anim}` on the
  member, which kills the loop, plays the lip-sync clip, then respawns the loop
  [C]. The lip-sync clips live in the song pak (e.g. gh3_singer_Male_Welcome_1,
  .ska flags 0x068b5000 = masked partial, 30 face bones) and gd04 loads them
  (data/band.cpp:458). The port's facial() + the Perf band_playfacialanim case
  match; song clips drive the mouth/face over the body via the face partial.

- **Faithful - win / lose.** play_win_anims / play_lose_anims call
  `play_special_anim {stance = win|lose, Anim = idle, blendduration = 0.0}` on
  the vocalist (always `win` / `lose`, not the small-stage variants) [C
  guitar_character.qb]; play_special_anim excludes the vocalist from
  hero_disable_arms (Guitarist/Bassist/Drummer only), plays the win/lose idle
  once, then Stance_A + bandmember_idle. venue_stage.cpp outro + playSpecial
  (vocalist not guitarLike, so no arm toggle) match.

- **Faithful - IK is legs-only.** Singer_IK_Params has only the two leg chains
  (bone_thigh/knee/ankle -> Bone_IK_Foot_Slave_L/R); the singer has NO arm IK
  (unlike the drummer's two hand-slave chains), the arms come straight from the
  body/face clips [C guitar_anim_tree.qb]. The port builds chains from the data,
  so the behaviour was already correct; only the band_anim.cpp / band.md
  comments (which claimed hand-slave IK for the singer) were wrong and are
  corrected.

- **Faithful - the singer never walks.** All 79 Band_WalkToNode performance
  entries across every song target the Guitarist; none target the vocalist, so
  the port's un-done Band_WalkToNode does not affect the singer [C: scan of
  every SONGS/*_song.pak.xen].

- **Out of scope / cross-member - ragdoll_markforreset not reproduced.**
  handle_change_stance, handle_play_anim and play_special_anim (win/lose) all
  call `ragdoll_markforreset` [C guitar_hand_events.qb / guitar_character.qb],
  which snaps the hair ragdoll to the animated pose at rest on each such event.
  render/band_ragdoll.cpp only resets on start or a > 2 s song-time jump
  (band_ragdoll.cpp:422), so after a hard stance/anim/win-lose pose change the
  hair carries momentum the original would have cleared. This affects all band
  members' hair (a shared subsystem), not only the singer; flagged for the
  orchestrator, not fixed here.

## Controls audit (2026-11-24)

Scope: the INPUT LAYER only (how the raw controller is read and turned into
frets / strum / whammy / tilt), not the judge (the hit / HOPO / overstrum
windows are already [C] in "Gameplay rules" §1-3 above and are NOT revisited
here). Read from the Ghidra export (`ghidra/analysis/`, base 0x82000000);
REA cannot open the XEX2. Our side: `engine/src/input/input.{cpp,h}`,
`engine/src/game/controller_mode.h`, `engine/src/game/player.{cpp,h}`,
`engine/src/ui/song_screen.cpp`. No tracked spec `spec-gamepad-menu.md`
exists in the repo (the kickoff named it); the port's control model is the
one in `input.h`. RB numbers below are the kickoff's Rock Band audit, used
only to reconcile a future universal model. No code was changed this pass
(rationale at the end).

### A. The original, per input area (binary evidence)

**A1. Per-player input struct and the connected flag [C].**
`FUN_820e7cd0(p)` returns the per-player input-system struct: for p < 4 it
is `DAT_82a74a0c[p + 8]` (a pointer table), for p >= 4 a second table
(`DAT_829b858c`). `FUN_820e7cb0(p)` = the byte `DAT_829b8594[p]`, the
per-player "this port has a usable controller" flag; the readers bail when it
is 0. Inside the struct: `+0x18` = the pad record pointer, `+0x1c` = the
device descriptor.

**A2. Controller TYPE detection: binary guitar-vs-pad [C 0x82100DA0,
0x82100FC0].** The reader sets a guitar flag by `descriptor[+0x08] == 8`
(only for p < 4; p >= 4 is always a guitar). There is NO separate X-plorer /
Les Paul / wireless branch at the gameplay input layer: every guitar model
is the one "guitar" code path (==8), everything else the "pad" path. The
descriptor's `+0x08` field is an internal device class, not the raw XInput
`SubType` (the XInput SubType is only touched by the rumble/caps helper
0x824ACBF8, which gates vibration on SubType == 2 and the motor caps bits).
QScript side: `get_musician_instrument_type`, `instrument_type`,
`get_instrument_from_character` pick the on-screen instrument (Les Paul /
other guitar skins such as `Instrument_LP_Pro6X`) — cosmetic, separate from
the play path.

**A3. Pad record layout and the fret read [C 0x82100DA0].** The judge's
held-fret mask is built nibble-per-fret: green 0x1, red 0x10, yellow 0x100,
blue 0x1000, orange 0x10000 (so a 5-fret shape is a 20-bit value, not bits
0-4). The source bytes in the pad record differ by path:
- guitar path: yellow = pad[+0x08], blue = pad[+0x09], orange = pad[+0x0a],
  red = pad[+0x0b], green = pad[+0x0c];
- pad path: the digital button field pad[+0x34] (green/red/yellow/blue/orange
  from bits 0x40 / 0x02 / 0x08 / 0x04 / 0x01) OR'd with analog bytes
  orange = pad[+0x0e], blue = pad[+0x0c], yellow = pad[+0x0d], red = pad[+0x0f],
  green = pad[+0x0a].
A fret counts as held when its byte is non-zero: the pad record already holds
a thresholded value, so there is no per-fret dead zone in this layer (any raw
dead-zone / analog-to-byte conversion lives in the XInput->pad-record fill,
which is not in the decompiled set — see "Uncertain").

**A4. Strum source [C 0x82100FC0, matches §3].** guitar: a strum is the
strum-bar bits `0x1000 | 0x4000` set in pad[+0x34]; on a strum the held
frets (A3 guitar bytes) are latched and a strum flag is raised. pad: a strum
is any change of the held fret/face set (each new press strums), with a
QScript rule keyed `0x67e6859a` consulted and a per-player "all buttons
released" latch at `DAT_829ba034[p]` (a pad chord must release fully before
the next, cf. §3 chord rule). force_mercury Select is Star Power on both.
There is no strum-repeat / strum-debounce constant here (RB's "strum <= 52 ms
repeats" has no GH3 equivalent): repeated strums are governed by the judge's
`disallow_tolerance_strums` + `strum_before_forming` pending model (§3), not
by an input-layer timer.

**A5. Tilt / Star Power [C 0x82112EE0-0x82112F90, already §6].** The tilt
value is pad[+0x01] (the right-stick-Y byte [I]); `v = b - 128`, normalised
`(v - 50) / 77` for v > 0 and `(v + 50) / 77` for v < 0 (so -127 -> -1.0,
+127 -> +1.0). A frame "counts" when the value is <= the player's
`star_tilt_threshold`, which `start_gem_scroller` loads from the user option
`star_power_position_device_<controller>` (default -1.0, the calibration
screen stores the held value clamped <= 0; guitar_tweaks' 16.0 is only a
placeholder). `star_power_trigger_frames` (3) consecutive counting frames
deploy; Select (qbkey 0xB40D153F) deploys at once. Co-op needs both triggers
inside `star_power_coop_trigger_ms` (1000).

**A6. Whammy [C scripts; §5].** The judge reads a continuous whammy value
0..1; star whammy requires movement > 0.03 over the last 5 frames
(`star_power_whammy_add` 3.333 / co-op 2.5 per beat); the pitch shift is
`1 - 0.057 * bar`. `whammy_units_per_second`, `whammy_shorten` (0.25),
`whammy_min_hold_pct` (10% / 35% short) are in guitar_tweaks.qb. The raw
whammy axis -> 0..1 mapping and its dead zone are in the pad-record fill, not
recovered (see "Uncertain"); a `deadZone` string exists at 0x82056ca0 but has
no code xref in `refs.tsv` (data / VMX).

### B. What OUR port does (input.cpp / song_screen.cpp / player.cpp)

- Pipeline: SDL event -> `Input::handle` -> `InputEvent` (action, down,
  per-device player, flags analog / joypad / fretsStrum / tilt) ->
  `SongScreen::handle` latches `held_` (bits 0 green..4 orange), a `strum_`
  pulse, `star_`/`tilt_`, `whammyBar_`/`whammyKey_` -> `SongScreen::tick`
  runs a FIXED 60 Hz step loop building `FrameInput{frets, strum, fretStrum,
  starPower, tilt, whammy}` once per step -> `Match::frame` / the C judge.
- TYPE model: 5 pad profiles (XboxGuitar, XboxGuitarOld, Ps2Guitar, RockBand,
  Joypad) + Keyboard + Midi, chosen by GUID and saved in controllers.json;
  functionally they collapse to a guitar watcher (strum bar) vs a joypad
  watcher (every fret press strums, `Require Strum` off) — the same binary
  split as GH3's A2, with the extra labels only changing the default fret
  bindings and tilt source. `controller_mode.h` is just {Guitar, Joypad}.
- Fret read: SDL buttons (digital) and axis-as-button. Axis-as-button
  (`sourceActive`, input.cpp:943): pressed when `|axis - rest| > 16000`,
  released below 10000 (hysteresis), i.e. ~0.49 of full range to press,
  ~0.31 to release. The Joypad profile puts green on LT and blue on RT as
  AxisPos (input.cpp defaults): a ~half trigger pull registers a fret.
- Whammy (input.cpp:961-976): a bound axis is normalised from its rest,
  dead zone 0.08, value `(x-0.08)/0.92`; the discrete Whammy ACTION fires
  past 0.15; the continuous value goes to the judge via `whammy()`.
- Tilt (input.cpp:979-989, player.cpp:255-264): Gyro held by an axis source
  ALONE sets `p.tilt`; `FrameInput::tilt` then deploys after
  `star_power_trigger_frames` (3) judge steps; a Back/Select button sets
  `starPower` and deploys at once. The axis uses the generic half-way
  threshold (16000/10000), NOT a per-controller calibrated tilt position.
- Connect hygiene: 300 ms settle after plug-in (rest re-read, no actions);
  buttons held across connect are masked until released (`stuck`); SDL
  mapping axis-buttons get the same hysteresis. Steam virtual pads taken.

### C. Gaps and errors (original vs port)

1. [I, needs-coordination] Tilt threshold is not calibrated. GH3 fires the
   tilt per `star_power_position_device_*` (default near full up, -1.0; A5);
   the port fires at a generic ~0.49 axis deflection. Net: tilt Star Power is
   easier (or, for a guitar whose rest is off-centre, harder) than the
   original, and ignores the calibration screen. Already flagged [I] in §6.
   RB reconciliation: RB's "mercury filter" holds the sign past 0.93 for 12
   frames; GH3 holds the value past the calibrated threshold for 3 frames.
2. [I, needs-coordination] Axis-as-button press threshold ~0.49 vs RB's
   0.75. For frets/strum bound to triggers or a stick (Joypad green=LT,
   blue=RT; pad strum on LEFTY) a half-pull already registers, which can feel
   like accidental frets or ghost strums. GH3's own pad frets are the digital
   d-pad bits (instant) plus raw-thresholded analog bytes (A3), so there is
   no single number to copy; this is a universal-model tuning decision.
3. [I, low] The Joypad fret order is GH2's `joypad_guitar` (LT LB RB RT A =
   green..orange), not GH3's pad mapping (A3/A4: d-pad bits 0x40/0x02/0x08/
   0x04/0x01). A gamepad is a non-primary path and users can rebind, so this
   is cosmetic-ish, but it is a documented divergence from GH3's own layout.
4. [I, low] Whammy dead zone 0.08 and the 0.15 action threshold are port
   inventions; GH3's raw whammy range / dead zone were not recovered, so
   there is nothing to verify them against. The continuous value the judge
   consumes (movement > 0.03 for star whammy) is faithful regardless.
5. [faithful, note only] The 60 Hz fixed-step quantises strum/fret timing to
   step boundaries (+-~8 ms) and collapses multiple SDL strums in one step to
   one pulse. GH3 also samples the controller once per game frame, so this is
   faithful; it is listed so it is not mistaken for a bug later.
6. [faithful] guitar-vs-pad binary split, strum-bar = frets latched on the
   strum edge, "each press strums" for pads, no input-layer strum-repeat
   timer: all match A2/A4.

### D. Prioritized improvement list

Small-safe (isolated, unit-testable with virtual pads; still gated by the
owner rule that input BEHAVIOUR changes need real-device logs — none here):
- [S1] Add input-selftest coverage that pins the current axis-as-button
  thresholds and the whammy dead zone (regression guard before any tuning).
  Change: new cases in `input_selftest.cpp` driving a virtual pad trigger to
  0.4 / 0.5 / 0.8 and asserting fret edges; risk: none (test-only).
- [S2] Document the 0.49/0.31 and 0.08/0.15 constants as named `constexpr`
  with the RB/GH3 references beside them (no value change). Risk: none.

Needs-coordination (touch the shared control model; do with the other games):
- [N1] A per-controller calibrated tilt threshold (wire the existing
  `star_tilt` / calibration concept into the Gyro axis test) so GH3's A5 and
  RB's mercury filter become one model. Risk: medium (shared input path,
  affects every guitar). Concrete: replace the generic Gyro axis threshold
  with a stored per-GUID tilt position, counting frames past it.
- [N2] Decide one axis-as-button threshold across games (0.49 vs 0.75) and a
  release hysteresis, grounded in device logs; GH3 has no single number so
  this is a product decision. Risk: medium (changes fret/strum feel).
- [N3] Optionally offer GH3's own pad fret order (A3/A4) as the Joypad
  default for gd04, leaving rebinding intact. Risk: low-medium.

### Uncertain / not recovered (VMX128 / undecompiled)

- The XInput/XamInputGetState -> pad-record fill is not in the decompiled
  function set (only the caps/rumble 0x824ACBF8 and keystroke 0x824ACCF8
  helpers are), so the raw stick/trigger dead zones, the whammy axis->byte
  scaling, and the exact source of pad[+0x01] (tilt) / pad[+0x34] (buttons)
  are inferred from how the gameplay layer consumes them, not read at the
  source. pad[+0x01] as right-stick-Y is [I] (as in §6).
- `SetMenuAutoRepeatTimes` (menu hold/repeat, referenced in
  `ui/natives_flow.cpp`) — its GH3 values (RB uses 500 ms hold / 50 ms
  repeat) were not pulled this pass.
- The `deadZone` string (0x82056ca0) has no code xref in `refs.tsv`.

No code change was made. Per the owner rule (objective-checks-not-eyeballing:
input behaviour changes need data-derived expected-vs-measured checks and
real-device logs) there are no device logs in this pass, and the owner called
the input engine fragile and asked for analysis-first; every candidate above
is either test-only (S1/S2, deferred to keep this pass purely documentary) or
a shared-model change for the orchestrator to coordinate across games.
