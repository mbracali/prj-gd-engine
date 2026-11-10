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
