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
