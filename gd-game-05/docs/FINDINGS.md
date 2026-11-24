# Rock Band (Xbox 360): gameplay rules and findings

Every rule with the address in `default.xex` where it was read ([C]) or
marked [I] (inferred; blocks "done" for gameplay, lesson L2). Addresses here
are always Rock Band's.

## The executable [C]
- XEX2, title ID 45410829, image base 0x82000000, entry 0x824F9000.
- Ghidra (XEXLoaderWV + FixXexFunctions): 41,175 functions, 13,449 strings,
  77,980 data references.

## Rules loaded from the data [C data]
`game/rules.cpp` reads Rock Band's `config/gen/scoring.dtb`,
`star_thresholds.dtb`, `beatmatcher.dtb` and `track_graphics.dtb`:
- points per instrument: guitar / bass / drums head 25, tail 12; vocals 0
  (scored per phrase: `phrase_value` 200 / 400 / 800 / 1000);
- `error_class` good 70, perfect 30; the watcher's `slop` 100 ms;
- streak multipliers: singleplayer and default 10 / 20 / 30 notes for 2x /
  3x / 4x; bass up to 6x (40, 50); multi 20 / 40 / 60; vocals 1 / 2 / 3
  phrases;
- Overdrive (`star_power`): 0.25 a phrase, 0.15 a common (unison) phrase,
  0.35 a drum fill, ready at 0.5, x2, whammy 0.034, drum fills need 3 crash
  hits when ready; `band_energy`: deploy 32 beats, unison 0.501, save 0.5;
- the band multiplier by members in Overdrive (1 2 4 6 8), crowd per
  difficulty and kind (default / drum / vocals), save_level 0.8333,
  time_to_return_from_brink 3.5 s, crowd_loss_per_sec 0.04;
- vocals per difficulty: slop 140 / 120 / 120 / 120 ms, pitch_margin 3.3 /
  2.3 / 1.5 / 1.2 semitones, rating thresholds 0.6 / 0.75 / 0.9 / 0.99;
- solos: awards at 60 / 70 / 80 / 90 / 95 / 100 % hit; the Big Rock Ending's
  coda: 100 points a rate, 1500 ms mash;
- stars: `thresholds` 0.3 / 0.6 / 0.9 / 1.8 / 3.0 / 4.0 / 5.0 and the band
  thresholds per line-up.

## How the judges use them (wave 01)
- **Guitar and bass**: gd-game-02's GH2 judge (`game/player.cpp`, every rule
  there [C] for GH2, [I] for RB until RB's watcher is read) with RB's head /
  tail points and streak tables per instrument (`rulesForInstrument`).
- **Drums** (`game/drum_player.cpp`): each lane a note, hit inside the slop
  window, missed when it passes; Overdrive deployed by a fill's crash when
  ready. Pairing of strikes to notes and the cost of an extra strike: [I].
- **Vocals** (`game/vocal_player.cpp`): a phrase is rated by the share of its
  notes' time sung within the pitch margin (octave-free), plus the slop on
  each side; talkies count any voice. The weighting and the scoring curve
  are [I] until RB's vocal scorer is read.
- **Band mechanics** (the band meter, saving a bandmate, Unison bonus, Big
  Rock Ending, solo awards, stars): not implemented yet (GD05-004).

## Highway (M4) [C data unless marked]

- `ui/track_panel.dta`: `track_panel` loads `track/trackpanel.milo`, a
  PanelDir whose TrackDirs `guitar`, `bass` and `drum` proxy
  `track/tracksystem.milo` (TrackDir v5: y_per_second 52, y -21..180, slots
  x -8 -4 0 4 8, draw group `static.grp`, anim group `rig.grp`, camera
  `game.cam`).
- `config/track_graphics.dtb` keeps its `#define`s: `(gems (guitar
  GUITAR_SET) (drum DRUM_SET))` names the macro; the macro's body follows its
  Define node. Per slot: normal, normal_hopo, star, star_hopo, unison, miss,
  miss_hopo, mash widgets; the drums' slot 0 is the kick (`gem_kick.wid`),
  slots 1-4 red, yellow, blue, green.
- The gem meshes share `prism_mat.mat` / `prism_glow_blue.mat`; per-colour
  copies exist (`prism_mat_green.mat` ...). The swap per slot is [I].
- The track's textures are in `track/track_shared.milo`; the tails'
  template and `tail_<colour>.mat` in `track/gem_tail.milo`.
- `static.grp` holds every instrument's parts (`guitar_draw.grp`,
  `drum_draw.grp`, the bass superstreak plane, the spotlight, the Unison
  hit); which shows when is [I].

## Venues and camera (M5)

- `config/macros.dta` VENUES: blank_01, small_club_01..12, big_club_01..12,
  theater_01..09, arena_01..08; each `world/<kind>/<venue>/gen/<venue>.milo`
  (WorldDir v17), the kind's shared milos in `world/<kind>/gen`.
- WorldInstance v0 (RB 0x822CC990 / 0x822CDF98) [C]: the shared
  directory's file, then a whole RndDir record; 2683 + 286 on the disc, all
  decoded. Drawn as proxies of their file.
- Band slots are CompositeCharacters `player_guitar0/1`, `player_bass0/1`,
  `player_drum0/1`, `player_vocals0/1` in `world/<kind>/gen/<kind>_char.milo`.
- BandCamShot categories in small_club_01: INTRO, INTRO_MULTI,
  INTRO_QUICK, INTRO_VENUE, NORMAL (110), directed_* (guitar, bass,
  drums, vocals, duo_*, all, bre ...), WIN, WIN_POST, WIN_QUICK, LOSE,
  OUTRO_MULTI, BFTB_* (fail / revive).
- The VENUE track (greengrass): notes 37-39, 48, 50, 60-64, 70-73, 87;
  texts `[lighting (<adjective>)]`, `[do_directed_cut <category>]`,
  `[FogOn]` / `[FogOff]`, `[verse]`, `[chorus]`, `[bonusfx]`.
  `config/midi_verify.dta` lists the allowed ones. The port cuts to a
  NORMAL shot on note 60 and to the named category on do_directed_cut [I:
  RB's BandDirector chooses by the members in view, not read].
- Lights: an RB Environ mixes point lights (range, falloff_start) and
  directional ones; colours go past 1.0 (3.2, 8.0, 6.7). The port lights
  each by its kind, falloff from falloff_start to range [I: RB's shader].

## The gem judge in RB's XEX (wave 02, read from the Ghidra exports)

Applied in the code (each line names RB's address):
- Watchers (factory RB 0x824BD060): "guitar" -> GuitarTrackWatcherImpl (0x824C1598);
  "joypad" -> JoypadTrackWatcherImpl (0x824C0DE0), or DrumTrackWatcherImpl
  (0x824C1250) when the track is a drum track [I: the flag's source, 0x824A62A8].
  Base TrackWatcherImpl 0x824B6120 reads (watcher (slop 100)) for every difficulty.
- Hit window |now + offset - gem| <= slop (0x824B4A30) [C, matches]; late
  deadline slop - offset, +60 ms for a HOPO-eligible gem (0x824C2030);
  hammer-on window -slop .. slop + 60 (0x824C2078); the chord / wrong-strum
  grace 60 ms (Swing 0x824C1A88); a strum <= 52 ms after the last dropped
  (0x824AED60) [C, matches GH2's numbers].
- HOPO eligibility (0x824C1980) needs the watcher's chain byte, set by a hit
  (0x824B4BF8), cleared by any reported miss / overstrum (0x824B4CD8) or
  pass (0x824B4E10) [C, now in player.cpp].
- Sustains: tail x duration_ticks / 480 per fret (0x82435DC0, 0x82467890);
  added unrounded each poll (0x82467928, 0x82461498), the score a float
  (0x82450E78) [C, now in player.cpp].
- Streak multiplier = min(table(streak), cap), cap 6 for bass, else 4
  (0x82441A58, 0x8245BDF0) [C; which table symbol: I].
- hopo_threshold: beatmatcher (parser (hopo_threshold 170)), the song's
  first (0x824A67A8) [C].
- Overdrive: drains 1 / band_energy deploy_beats a beat (0x82445188 +0xE0,
  0x82442C48); star_power deploy_rate has no reader; a phrase adds
  spotlight_phrase 0.251 (0x82464A98 -> 0x82442A70); deploy at
  deploy_threshold 0.5 (0x82442350); saving a member costs save_energy 0.5
  (0x82442B20); whammy 0.034 a beat (0x824644E0) [C, now in player.cpp].
- The band's multiplier: bonuses multiplier[members deploying] (1 2 4 6 8)
  for every member (0x8243F810, 0x824415A8) [C, now in app.cpp updateSeats].

Not applied yet (GD05-018):
- Drums go through DrumTrackWatcherImpl (Swing 0x824A2320): every pad of a
  chord is its own gem (+1 streak, its own points; 0x824A21E8), a wrong pad
  misses the closest unhit lane of the nearest time group, the miss
  deadline uses the next gem in the same lane (0x824A26A8), passed chords
  penalise once per time group (0x82463F10), hits more than 1000 ms before
  the first gem play drum_freestyle (0x824638D0). The port still plays the
  drums through the joypad watcher (DECISIONS D6).
- A miss more than 200 ms before its gem charges the phrase of gem - 1
  (0x82461368).
- Unsettled: which notes become HOPOs, the 50 ms overstrum gap (+0x5C has no
  reader found), the section queries (watcher vtable +0x1C..+0x2C, +0x60/+0x64:
  fills, coda, solos), kick handling.

## Venue lights and WorldDir overrides (wave 02)
- RndLight::Load (RB 0x821AACC8) reads colour, **range, type, falloff_start**
  (the object's +0xD0 / +0xD8 / +0xD4; the kind switches test +0xD8,
  0x821A8900 reads +0xD0 as the range) [C]; every RB light's type is 0..4
  [C data]: 0 point, 1 directional, 2 fake spot, 3 floor spot, 4 shadow ref.
  Wave 01 read type and range swapped: every point light shone without
  falloff (the white venues).
- The environ select (RB ~0x827D7580): with any point light only the point
  lights (at most 4, 0x827D6F00); else the directional ones (0x827D7090);
  fake spots per drawn object, floor spots in the projected slots
  (0x827D72E8); a light black in 8 bits is skipped. Colours are HDR on
  purpose: nothing divides them on the CPU [C]. The port draws points or
  directionals; fake and floor spots are not drawn yet [I].
- WorldDir (SyncProperty 0x82289DC8) hide_overrides and mat_overrides of a
  venue variant's own record: the meshes that take the variant's materials
  (arena_geom's stage uses the green placeholder temp.mat otherwise) and the
  objects it hides; now applied at load (game/venue.cpp) [C data; I: timing].

## Vocals in RB's XEX (wave 02; VocalPlayer around RB 0x82452000..0x8245AA00)
Applied (game/vocal_player.cpp):
- Each frame credits the one best active note over [start - slop, end +
  slop] (0x82453B68, 0x82455E18) with dt x hit multiplier x accuracy
  (0x82454208); accuracy 10^-(d/pitch_margin)^2, 0 under 0.01 (0x82455D78);
  octaves folded only within 2.5 semitones modulo 12 (0x82453A20); hit
  multiplier pitch_hit_multiplier / nonpitch_hit_multiplier (x
  nonpitch_easy_multiplier for '^') x short_note_multiplier under
  short_note_threshold_ms; the credit capped by the phrase maximum (notes'
  ms x note_length_factor, under 40 ms none; 0x82452800) and by
  vocal_cap_growth x the weights reached [C].
- Rating: the highest threshold strictly passed (0x824587A0); points
  round(phrase_value x credit/max) x streak multiplier + the Overdrive bonus
  the same way (0x824587EC); only Awesome extends the streak (0x824537E8);
  an Overdrive phrase pays spotlight_phrase only when Awesome (0x824661B8) [C].
- The singer deploys by singing at 0.1 or louder for over 390 ms inside a
  freestyle section (phrase start + freestyle_pad to the first note -
  freestyle_pad when at least freestyle_min_duration, or an empty phrase;
  0x824AD9D0, 0x82458F70; vocals.dta set_scream_thresholds) [C].
- The crowd meter moves toward rating / 4 by phrase_weight 0.1 (0x824673D0) [C].
Not applied: the talkies' speech matcher (Fonix: any voice counts, [I]),
the dropped-credit feedback ring (0x824531F0), slides' start / end pitch
interpolation, the phrases re-cut end to end (0x824AD9D0), the tambourine
(mic onsets, +/-120 ticks, 25 points; 0x82456080). RB detects pitch with
Fonix at 16 kHz; the port's own detector stands in [I].

## Band bonuses in RB's XEX (wave 02)
Applied (game/rb_bonus.cpp, app.cpp updateSeats):
- Solos: at the span's end, truncate(hit x 100 / total), the highest award
  at or below, bonus x the solo's gems (0x8245F268, 0x8245DF78) [C].
- Big Rock Ending: each strike adds trunc(min(now - the lane's last
  strike, max_mash_ms) x point_rate / 1000) (0x8245EF20) [C]; banked by the
  final gem (the band-wide wait, 0x82440838, is per player here [I]).
- Fills: the crash deploys after crash_fill_ready_hits pad hits in the
  fill (0x824C1370, 0x824C1530) [C; when a fill shows: I].
- Stars: band_thresholds by the band's instrument set x base +
  bonus_thresholds x bonus base, difficulty_thresholds' Expert for gold
  (0x82435A00, 0x82435918) [C; the bonus base: I].
- Fail-out (0x824418C8), saves by a deploy for members failed at most twice
  at save_level costing save_energy (0x8243F548, 0x82442B20), the out
  member's meter draining by crowd_loss_per_sec x (players - 1), the band's
  weighted meter under multiplayer_lose_level (0x82466D28) [C]; the brink
  delay (time_to_return_from_brink) is not applied [I].
- Unison: members out of play neither block nor receive it (0x82465950) [C].

## The camera director (wave 02; mostly script: world/worldbase.dta, config/midi_parsers.dta, band_objects.dta)
- VENUE note 60 asks for a new shot, 1/8 beat early [C]. Notes 61..64
  (bass, drums, guitar, vocal focus) and 70..73 (no_behind, force_far,
  force_closeup, no_closeup) are flags held from note-on to note-off [C].
- At world enter, every NORMAL shot is renamed NORMAL<b><d><g><v> from its
  focus flags; a far shot in coop becomes NORMAL1111. `special` shots are
  never picked [C].
- `change_proxies` keeps only the mode's shots (`coop_` in a band). PS3
  shots are `platform_only` 4 [C].
- A pick:
  - with no focus flag, the category is drawn from 0001 0010 0100 1000
    1111 1100 0110 0011;
  - facing alternates left / right;
  - no far shot after a far shot, none with a focus flag;
  - no behind shot after a behind shot [C].
- The picker (WorldDir 0x8229F0D0) draws from the unused shots that pass
  the filter, and resets them when none is left; each draw is weighted by
  `selection_weight` [C].
- `[do_directed_cut <cat>]` forces a shot of that category and blocks
  note-60 picks for (min_time + zero_time) / 30 s [C].
- LOSE plays when the band fails; WIN after the song. In a band, WIN is a
  chain: WIN_PRE, one shot per player, WIN_POST [C, not done].
- BandCamShot start (0x822D8108): each band target's member plays the
  target's clip group, and teleports to `to` when the target says so [C;
  IK and props not done].
- Scripts' tasks: Rock Band passes the units first and positionally
  (`{thread_task kTaskTutorialSeconds (script ...)}`); GH2 used `(units ...)` [C data].

## Controls audit (2026-11-24)

Reverse-engineers Rock Band's input path end to end (device detection ->
beatmatch controller mapping -> watcher) and compares it to the port's
`engine/src/input/input.{cpp,h}` and gameplay path (`app.cpp`). The reference
is deliberately broad: the owner wants one universal control model across all
games, and RB is the richest input game. Analysis-first; no change was made to
the fragile input engine (see "Why no code change" at the end).

Evidence is Rock Band's unless marked: `[C data]` a decoded `extracted/dta/`
file, `[C]` an address in `default.xex` (image base 0x82000000), `[I]`
inferred. Named functions come from `strings.tsv` (the string's referencing
function); watcher/judge addresses are carried over from the "gem judge" and
"Vocals" sections above.

### A. The original, per input area

**Two-layer model.** RB turns every physical controller into one of a small
set of *beatmatch controller* profiles, then feeds one of three *watchers*.
The profile is pure data; the watcher is code.
- Device -> profile: `system/run/config/gen/joypad.dta` `(controllers ...)`
  per platform, detected by the XInput/USB subtype `[C data]`. On Xbox
  (`#ifdef HX_XBOX`): `strat_xbox` <- `kJoypadXboxHxGuitar` (RB Stratocaster,
  `lefty_flip`), `ro_guitar_xbox` <- `kJoypadXboxRoGuitar` (a Guitar Hero
  RedOctane guitar, `ignore_buttons kPad_L2 kPad_R2`, `lefty_flip`),
  `hx_drums_xbox` <- `kJoypadXboxDrums`, `stagekit_xbox` <-
  `kJoypadXboxStageKit`, else `digital`/`analog` pads. So RB keys off the
  controller's reported *type*, not its USB name. Detection code:
  `get_controller_type_from_pad` / `get_controller_type_from_controller` /
  `get_instrument_from_controller` / `get_instrument_track_order` /
  `has_participating_guitar_controller` / `is_instrument_available` (all in
  Function_824254E8) `[C]`; the raw pad layer is the joypad manager
  Function_823B0CC8 (`joypad_controller_type`, `joypad_is_button_down`,
  `joypad_get_lx/ly/rx/ry/tl/tr` analog reads, `joypad_stage_kit_raw`,
  `joypad_get_player_from_padnum`) `[C]`.
- Profile -> slots: `config/gen/beatmatch_controller.dta` maps each profile's
  physical buttons to the beatmatcher's gameplay slots `[C data]`.
- Profile -> watcher: `system/run/config/gen/beatmatch.dta`
  `(beatmatch_controller_mapping ...)` `[C data]`: guitars (`guitar`,
  `hx_guitar_xbox`, `ro_guitar_xbox`, `hx_guitar_ps3`, `strat_xbox` proto
  guitars) -> the `guitar` watcher (GuitarTrackWatcherImpl 0x824C1598);
  **every drum profile (`hx_drums_xbox`, `hx_drums_ps3`, `drums`,
  `lefty_drums`, `midi_drums`, `stagekit`) -> the `joypad` watcher**, which
  becomes DrumTrackWatcherImpl (0x824C1250 / Swing 0x824A2320) when the track
  is a drum track `[C/I]`; `joypad`/`joypad_guitar` -> the joypad watcher.
  So in RB a drum kit is a *joypad-class* controller (each pad is a discrete
  strike, no strum bar) — which is exactly the port's wave-01 decision (D6).
- Global input tuning (`joypad.dta`): `deadzone 36`, axis-as-button
  `threshold 0.75`, menu `hold_ms 500` / `repeat_ms 50`, `(ignore 1 2 3 4 5
  6 7)` `[C data]`.

**Guitar / bass.** Profile `ro_guitar_xbox` / `strat_xbox` / `hx_guitar_ps3`.
- 5 frets: `beatmatch_controller.dta` `(slots ...)` per profile `[C data]`,
  e.g. `ro_guitar_xbox (slots 6 0 4 1 5 2 7 3 2 4)` and
  `hx_guitar_ps3 (slots 6 0 5 1 4 2 7 3 2 4)` (raw button -> slot 0..4 =
  green..orange). `button_filters` remaps the same buttons for menu nav.
- Strum up/down: the guitar watcher reads the strum bar; `strummed_up` /
  `strummed_down` / `deploy_count` / `failed_deploy` live in the watcher
  Function_824511F0 `[C]`. Strum/HOPO input-layer timing (carried over from
  the gem-judge section, confirmed [C]): a strum <= 52 ms after the last
  dropped strum (0x824AED60); HOPO-eligible late window +60 ms / hammer-on
  window -slop..slop+60 (0x824C2030 / 0x824C2078); chord / wrong-strum grace
  60 ms (0x824C1A88); `hopo_threshold 170` ticks (`beatmatch.dta`).
- Whammy: a continuous bar, `whammy_rate` (Function_824352E8), `send_whammy`
  (0x8245E4B8) / `whammy_end` (0x8245E900); it adds Overdrive 0.034 a beat
  while a tail is held (0x824644E0) `[C]`. No separate "whammy range"
  calibration beyond the axis read.
- Tilt / Overdrive (Star Power): **two sources, both in every profile.** (1)
  `force_mercury kPad_Select` in each `beatmatch_controller.dta` block — the
  Select button always deploys `[C data]`. (2) the guitar's tilt sensor (the
  "mercury switch"), filtered by `beatmatch.dta` `(mercury_switch_filter
  (any_sign_filter (window 0.5) (num_frames 12) (threshold 0.93)))` — tilt is
  recognised only when the accelerometer's sign holds past 0.93 for 12 frames
  `[C data]`. Deploy logic: `can_deploy` / `is_deploying` / `force_deploy`
  (Function_82442D68), `deploy_threshold`/`deploy_bonus`/`deploy_beats`
  (Function_82445188) `[C]`.
- The "upper / solo frets": **RB1 has none.** The RB1 Stratocaster is a
  10-fret guitar; `five_way_controllers strat_xbox hx_guitar_ps3`
  (Function_8241F8C8) `[C data]` names the controllers with the Strat's
  5-way *pickup / effect selector*, used only in freestyle solo sections
  (`in_freestyle_section` 0x82456C20, `drum_freestyle`) to pick an audio
  effect (WahEffect / FlangerEffect / PitchShiftEffect strings at
  0x82885...). It is **not** a scored note input. (Upper solo frets are a
  later Guitar Hero feature, not RB1.)

**Drums.** Profile `hx_drums_xbox` (Xbox 360 4-pad kit + kick).
- Pad -> lane: `hx_drums_xbox (slots 2 0  9 0  4 1  5 2  7 3  6 4)` `[C data]`
  — `num_slots 5` (`beatmatcher.dta`). Four of the five lanes are single
  buttons (slot 1=btn4, 2=btn5, 3=btn7, 4=btn6); **slot 0 is fed by two
  buttons (2 and 9)** — the kick lane taking both the kick pedal and a second
  source `[C data; which physical input is which: I]`. MIDI kit:
  `midi_joypad (mapping (36 kPad_L2)(38 kPad_L1)(46 kPad_R1)(48 kPad_R2)(45
  kPad_X)) (thresholds (36 50))` — notes 36/38/46/48/45 -> the 5 lanes.
- Cymbals: **RB1 has none** (4-pad kit only; cymbals are an RB2/RB3-Pro kit).
  There is no cymbal slot in `hx_drums_xbox` and no pad-vs-cymbal lane split.
  (`hit_cymbal` 0x82818... is the drummer *character* animation, not input.)
- Velocity / dynamics: **RB1 does not score drum velocity.** The slots are
  binary button->lane; the only velocity in the input data is the MIDI-kit
  *gate* `thresholds (36 50)` (a note counts if velocity >= 50), not graded
  dynamics `[C data]`. No "velocity"/"dynamic" string appears in a drum
  input/scoring context (only `stretch_with_velocity`, an animation helper,
  and debug breakpoints). (Graded drum dynamics are an RB3 feature.)
- Drum watcher timing & fills (DrumTrackWatcherImpl, carried over, [C]):
  every pad of a chord is its own gem (0x824A21E8); a wrong pad misses the
  closest unhit lane of the nearest time group; the crash lane deploys after
  `crash_fill_ready_hits` pad hits inside a fill (0x824C1370 / 0x824C1530);
  hits >1000 ms before the first gem enter `drum_freestyle` (0x824638D0).

**Vocals.** Not a button device at all.
- The mic is sampled continuously; RB detects pitch with **Fonix at 16 kHz**
  `[I, from the vocal section]`. The scorer (VocalPlayer 0x82452000..) grades
  each phrase against the chart.
- Pitch model (`config/gen/scoring.dta (vocals ...)`) `[C data]`:
  `pitch_margin 3.3 / 2.3 / 1.5 / 1.2` semitones by difficulty,
  `slop 140 / 120 / 120 / 120` ms, `rating_thresholds 0.6 / 0.75 / 0.9 /
  0.99`, `phrase_value 200 / 400 / 800 / 1000`, octave-free matching,
  `pitch_hit_multiplier`, `short_note_*`, `max_detune 1.0` / `detune_factor`.
- Talkies (non-pitched): `nonpitch_slop 150`, `nonpitch_history 500`,
  `nonpitch_warping_penalty 0.3`, `nonpitch_hit_multiplier 2.0`,
  `nonpitch_easy_multiplier 2.5` — any voice counts `[C data]`.
- Deploy gesture: **no button.** The singer deploys by singing >= 0.1 loud
  for > 390 ms inside a freestyle gap (`freestyle_min_duration 600`,
  `freestyle_pad 100`; `screamPoll` 0x82458F70 / 0x824AD9D0) `[C]`.
- Percussion sections: tambourine from mic onsets (`tambourine_points 25`,
  `tambourine_window_ticks 120`, 0x82456080) `[C data]`.

**Controller-type -> player/part assignment.** `get_controller_type_from_pad`
/ `get_instrument_from_controller` / `get_instrument_track_order` /
`is_controller_type_with_lowest_pad_num` / `get_join_state_for_instrument`
(Function_824254E8) resolve each pad to an instrument and a track, so a guitar
joins GUITAR, a kit joins DRUMS, a mic joins VOCALS automatically `[C]`.

### B. What the port does today

- **One control model, guitar-shaped.** `Input::Control` has 15 controls:
  General (Start, Select, MenuUp, MenuDown) + 5-fret guitar (GuitarStart,
  GuitarSelect, StrumUp/Down, Green/Red/Yellow/Blue/Orange, Whammy, Gyro)
  (`input.h:99`). Identical to gd01/gd04. There are **no** drum-pad, cymbal,
  kick, mic, or effect-selector controls. Profiles (`XboxGuitar`,
  `XboxGuitarOld`, `Ps2Guitar`, `RockBand`, `Joypad`, `Keyboard`, `Midi`) and
  their default slot layouts mirror `beatmatch_controller.dta` for guitars.
- **Drums ARE playable, as a joypad guitar.** When a player's part is Drums,
  `app.cpp:1332` permutes the drum chart's lanes onto the 5 frets
  (`g.frets = ((f&1)<<4) | (f&0x0E) | ((f>>4)&1)`: kick bit -> orange fret,
  green pad -> green fret), `splitDrumChords` makes a gem per pad, and a
  regular `Player` judges it in `ControllerMode::Joypad` with
  `setDrumMode(true)` (`app.cpp:1360-1366`). The drum controller itself is a
  `Profile::Joypad` whose 4 pads + kick are SDL gamepad buttons mapped to
  Fret0..4. **This is the same two-layer idea as RB (drum = joypad watcher),
  but collapsed onto the guitar's 5 fret controls.**
- **Vocals ARE playable, outside the input engine.** `VocalPlayer`
  (game/vocal_player.cpp) is driven by `VocalInput` (audio/vocal_input.cpp:
  real mic via gd-engine-core, or `GD05_VOCALS_FILE`) through a
  `PitchDetector`; `app.cpp:1376-1393` wires it, and `handleGameAction`
  returns early for a vocal seat (`app.cpp:2104-2110`) so controller buttons
  are ignored. The port's own pitch detector stands in for Fonix.
- **Instrument marking is a label only.** Setting a device to Drums/Vocals in
  the Gamepad menu writes `rbParts_[guid]` (`input.cpp:1476`) and
  `controllers.json "rb_part"` (`input.cpp:514/567`); `instrument()`
  (`input.cpp:1461`) returns it so `App::controllerType` (`app.cpp:2134`)
  picks the part. It creates **no** new bindings — the device still emits the
  15 guitar controls.
- **Detection** is by USB id / SDL mapping (`isHidRockBandGuitar`,
  `isXboxGuitar`, `isPs2Guitar`) + device-name substring "drum"
  (`app.cpp:2159`); there is no XInput-subtype drum-kit detection like RB's
  `kJoypadXboxDrums`.
- `DrumPlayer` (the faithful RB drum watcher, `game/drum_player.cpp`, lanes
  0=kick..4=green) and the DrumTrackWatcherImpl rules **exist but are wired
  only in `tests/selftest.cpp`** — `strike()` has no live-input caller.

### C. Gaps and errors

1. **The universal control model cannot express a drum kit or a mic.** Drums
   borrow the guitar's 5 frets (works for RB1's 4 pads + kick because
   4+1 = 5); a mic is not representable at all. This is the headline gap for a
   universal model — see D. `[fact]`
2. **Drums use the GH2 joypad watcher, not DrumTrackWatcherImpl.** The port's
   wave-01 D6 decision. RB pairs strikes to pads differently (wrong pad ->
   nearest unhit lane, per-time-group chord penalties, fill/crash deploy,
   drum_freestyle). This is a *judge* gap already tracked as GD05-018 (above),
   not an input-engine gap, but it is the biggest behavioural difference a
   drummer will feel. `[C]`
3. **No drum-kit auto-detection.** A real Xbox 360 RB/GH drum kit that SDL
   does not name "drum" is detected as a generic Joypad and plays as a
   gamepad-guitar until the player manually picks Drums in GAMEPADS. RB keys
   off `kJoypadXboxDrums`. Likely one of the owner's "errors". `[C data vs code]`
4. **Tilt / Star Power has no debounce filter.** RB gates tilt through the
   12-frame, 0.93 `mercury_switch_filter`; the port maps Star Power to the
   Gyro/Select control (RB guitar tilt = the HID tilt bit on Right Shoulder,
   `input.h:170`) with no equivalent sign-hold filter. On a twitchy
   accelerometer bit this can mis-fire or drop Overdrive — a plausible
   fragility. `[C data vs code]`
5. **Axis-as-button thresholds differ from RB.** The port treats an axis as
   pressed "past half way, released below a third" (`input.h` AxisButton) with
   a 300 ms settle; RB uses `deadzone 36` and `threshold 0.75`
   (`joypad.dta`). Different trigger/stick break-points can make frets on
   triggers (L2/R2 -> green/blue) feel early or sticky. Worth a real-device
   log comparison. `[C data vs code]`
6. **Menu auto-repeat is not confirmed to match.** RB menus repeat at
   `hold_ms 500` / `repeat_ms 50`; the port's menu repeat was not located in
   this pass. `[I — needs a code check]`
7. **No velocity / no cymbals in either engine.** Correct for RB1 (it has
   neither); a problem only if a universal model must also serve RB2/RB3-Pro
   kits. `[fact]`
8. **Vocals live entirely outside the shared engine.** Functionally fine, but
   the "one control model" does not own the mic, so the Gamepad menu cannot
   test/calibrate it and other games cannot reuse it through the same path.
   `[fact]`

### D. Prioritized improvement list (for the universal control model)

Marked **small-safe** (isolated, testable, low risk) vs
**needs-coordination** (touches the shared control model / other games — a
proposal for the orchestrator, one consistent model across all games).

1. **(needs-coordination) Extend `Control` with a drum lane set + kick.** Add
   DrumRed/Yellow/Blue/Green + Kick (and, behind a flag for RB2/3-Pro,
   Hat/Crash1/Crash2 cymbals) as distinct controls with their own defaults
   (RB `hx_drums_xbox` slots; keyboard Space=kick etc.). Route them to
   `DrumPlayer::strike(lane)` (already written and tested). This is the
   correct fix for gaps 1/2 and the core of the universal model, but it
   changes the shared enum used by all games and must be designed once for
   all. **High value, high blast-radius.** Risk: binding table, save-file and
   every game's `actionsOf` change.
2. **(needs-coordination) First-class mic "instrument".** Let the shared
   engine own a mic device (open/level/pitch via `VocalInput`) so the Gamepad
   menu can list and test it and other games reuse it. Risk: audio-capture
   lifetime in the input layer.
3. **(small-safe) Drum-kit detection by controller subtype/name.** Broaden
   detection so a known RB/GH Xbox-360 drum kit auto-selects the Drums
   instrument (today only a name containing "drum", `app.cpp:2159`, or a
   manual pick). Isolated to detection; testable with a virtual pad
   (`GD05_VIRTUAL_PAD`) carrying a drum-kit USB id and an objective assertion
   that `instrument(id) == Drums`. Fixes gap 3. **Do first.**
4. **(small-safe) Tilt debounce to match RB.** Add a sign-hold filter to the
   tilt/Gyro source equivalent to `mercury_switch_filter` (hold past 0.93 for
   ~12 frames) before it drives StarPower. Isolated; testable by feeding a
   synthetic axis trace and asserting the deploy edge count. Needs a
   real-device tilt log to set the constant honestly (owner rule: no
   eyeballing). Addresses gap 4.
5. **(small-safe) Reconcile axis thresholds with `joypad.dta`.** Document and,
   if a real-device log shows a mismatch, align the fret-on-trigger break
   points to `deadzone 36` / `threshold 0.75`. Data-derived only. Gap 5.
6. **(small-safe) Confirm/align menu auto-repeat to 500/50 ms.** Locate the
   port's repeat and compare; a one-line constant change if it differs. Gap 6.
7. **(needs-coordination) Wire DrumPlayer to live input** once (1) exists, and
   retire the fret-collapse path. This is GD05-018 territory; large.

### Why no code change in this pass

The owner called the controls "VERY FRAGILE" and asked for analysis-first.
Every gap above is either (a) needs-coordination (the shared enum / model,
which must be designed once across all games) or (b) a small-safe fix whose
*honest* test needs a data-derived expected-vs-measured check against a
real-device log (owner rule: no eyeballing), which this pass could not
capture. The highest-confidence small-safe fix, drum-kit auto-detection (D3),
is isolated and virtual-pad-testable and is the recommended first change — but
it still alters device->part behaviour shared with the band code, so it is
left as a reviewed proposal rather than slipped in unverified. The deliverable
here is the reference model above.

## Controls Phase B — first-class vocals (mic) [port]
A microphone is now a first-class seat in the universal input/controller
model, parallel to the drum kit, without changing the (working) vocal
pitch-scoring path.

Implemented (engine/src/input/input.cpp, input.h, engine/src/app.cpp):
- A mic is detected when it reaches SDL as a HID "gamepad" (some Rock Band /
  SingStar USB mics present a small control interface beside their USB-audio
  one): `isRockBandMic(id, name)` matches by device name ("microphone",
  "singstar", "rock band mic") or a short known-USB-id list (0x1415:0x0000
  SingStar/Logitech wired mic; 0x12ba:0x00ff Harmonix [I]). The pad carries a
  `mic` flag (Pad::mic), set at SDL_EVENT_GAMEPAD_ADDED.
- `Input::instrument()` returns `Instrument::Vocals` for a mic by default;
  `Input::instruments()` offers {Vocals, Gamepad} (Vocals first). The label is
  already "Vocals (microphone)" (instrumentName). `setInstrument` to a real
  controller type clears the mic flag, so a mis-detection is overridable.
- Guard: a mic emits no gameplay actions (frets, strum, whammy, Star Power) —
  `Input::update` skips `controlEdges` for a mic and zeroes its whammy — so a
  buttonless or stray-HID mic is never a dead guitar that strums / misses.
  Menu helpers still work, so a mic button can navigate menus. This mirrors
  the drum-kit detection + guard.
- `App::controllerType` routes a mic seat to the vocal part (type 2): via
  `Input::instrument()==Vocals`, plus a name fallback ("microphone"/
  "singstar") for devices consulted by name.
- Test aid: `attachVirtualPad("<v>:<p>:mic")` plugs in a virtual
  "Rock Band Microphone"; the input self-test asserts the seat is Vocals (not
  a dead gamepad), is surfaced as "Vocals (microphone)", offers Vocals first,
  is a listed device, plays no frets/strum when its button is pressed, and
  that a guitar still frets alongside it.

Evidence (gd05 --selftest, GD05_VIDEO=OFF, RB USA disc): all input checks
passed (incl. the 6 new mic checks); PART VOCALS 252 charts, 8648 phrases
Awesome, 0 not fully hit; "vocals through the pitch detector: 58 songs,
2136 of 2136 phrases Great or better"; GUITAR/BASS/DRUMS 0 not fully hit;
0 judge rule checks failed.

Remaining (deeper routing — larger follow-up, deliberately NOT done here):
- The mic's VOICE is still judged entirely outside the universal input
  engine: `VocalInput` (gd-engine-core mic, or GD05_VOCALS_FILE) feeds a
  `PitchDetector` into `VocalPlayer`, and `handleGameAction` still returns
  early for a vocal seat (the singer deploys by singing, screamPoll). This
  increment makes the *instrument/seat/detection* first-class; it does NOT
  move pitch events through the universal Action model. Fully routing the
  mic through the input engine (pitch as Actions) is a large change that
  risks the working vocals and is left as a follow-up.
- Associating a detected HID-mic device with the VocalInput audio capture of
  the same physical mic (today VocalInput opens the default capture device
  independently of the HID device) is not wired.
- gd10 / gh4 vocals are NOT playable: gd10 has no VocalPlayer and no mic /
  pitch path at all — that is a separate, larger feature (a new pitch
  pipeline), out of scope for this increment.
