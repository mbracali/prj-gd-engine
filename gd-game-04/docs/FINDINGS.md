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
  note is a star gem: star_power_sequence := 0, star_miss_note (remaining star gems
  of the phrase become normal gems) [C 0x8211F56C-0x8211F5B0, 0x8211E8B8,
  0x821195C0 star_miss_note branch].
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
  [C script]. whammy_cutoff (1100) not located in code [unknown].

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

### 8. Modes

- Co-op: parts guitarcoop_/rhythmcoop_ note tracks when the song has
  use_coop_notetracks [C setup_gemarrays]; shared streak, SP, averaged fail.
- Battle: phrase completion -> battlemode_ready (power-up from battlemode_powerups,
  weights/drain_time in guitar_battle.qb) [C 0x8211E7B0]; attacks fired via the SP
  trigger path [I].
- Face-off: '%s_faceoffp1/p2' section arrays; gem_filler_enabled_time_on/off limit
  which notes enter a player's input array [C faceoff_init, 0x82104558-0x821045CC].

### Unknown / not done
- whammy_cutoff use; exact star_miss_note boundary; tilt units; coop trigger flow;
  battle attack effects; boss AI (CheckButtonsBoss 0x821096E8).

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
