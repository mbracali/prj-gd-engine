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
  (D7) [C scripts gem_scroller, call_startup_scripts]. The practice pitch-shift offset is not
  applied (no slowed practice yet).

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
  gems, next phrase) [C 0x8211F938-0x8211F9EC]. total_notes unchanged.
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
  playline row 1024); the tail runs from there. The
  port pins the head at the playline (render/highway2d.cpp, `vHead`): a
  follow-up for the highway code (not in this item's files).

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
  (3) consecutive frames fire, Select (0xB40D153F) at once. The port merges
  tilt and Select into FrameInput::starPower (input/, game/player.h), so the
  3-frame wait of the tilt is not modelled [I: a follow-up there].
- Co-op raise-axe prompt [C 0x82112850 + script]: the trigger in p2_career /
  p2_coop spawns show_coop_raise_axe_for_starpower with the **other**
  player's status (player 1 triggers -> player2_status, player 2 ->
  player1_status); the script places it at hud_screen_elements[10].pos
  - (225, 60) for player_status.player = 1 (left, (415, 240)) and + (225,
  -60) for player 2 (right, (865, 240)); so it shows on the side of the
  player who has not raised yet. Not shown while that player's
  star_power_used, showing_raise_axe, attract or performance mode. The port
  always draws it at the left (render/hud2p.cpp): a follow-up for the HUD
  (Match needs to say who triggered).

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
- Co-op raise-axe side: §6 (the other player's side; HUD follow-up).
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
is the original's. **None of the GH3 song scripts below runs.**

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
  practicemode_init (training: not supported), the boss intro audio
  GH_SFX_Preload_Boss_Intro_Audio and the boss helper screen (L),
  Progression_SetProgressionNodeFlags (node flags: R where venues need them).
- gem_scroller's iterators: gem_iterator x4, fretbar_iterator x3,
  check_buttons_fast, button_checker, check_for_star_power,
  update_score_fast, check_note_hold, the band iterators, lightshow_iterator,
  cameracuts_iterator, win_song, faceoff_init (R, as the C they call);
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
reimplemented natively and match the C ([C] above). Also not modelled:
end_credits (no health loss during the credits song) and tutorial /
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
  tod_manager.qb [C]; the light and the matrices are gd04's [I].
- Back-face culling fixes the M5 stage-rear cameras (they stand behind
  walls) [I: Direct3D's default CCW, the templates' own states not decoded].

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
