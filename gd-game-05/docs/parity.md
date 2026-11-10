# Rock Band (Xbox 360) parity ledger

This file measures how much of the original game works in `gd05`. It is the
only source of `game-info.yaml`'s `completeness` (kickoff section 8, wave 02
target). Each row is one feature or screen group of the original, with a
weight. The weights were fixed before any wave 02 work (2026-11-09 18:30)
and do not change to make a number look better. When a row is split later,
its weight is shared out among the parts.

## How it is scored

- **works** (1.0): it behaves as the original, as far as the code and the
  data show. Checked by running it (a test, a walk or a screenshot). Rules
  marked [I] (inferred) in FINDINGS.md still count as works only if the
  behaviour a player sees matches the original's data.
- **approximate** (0.5): it runs and a player can use it, but parts are
  missing or guessed. The row says what.
- **stand-in** (0.25): something is there, but it is clearly not the
  original's (a debug text HUD, a placeholder). The row says what.
- **missing** (0).

completeness = sum(weight x score) / sum(weight). Weights add up to 100, so
the sum is the percentage. `game-info.yaml` takes this value rounded down to
two decimals, never higher.

"How checked" names the test or walk that showed it. "built only" means the
code is there but nothing ran it, and it scores as missing.

## The ledger

| # | Area | Feature | Weight | Status | Score | How checked / what is missing | Backlog |
|---|---|---|---|---|---|---|---|
| 1 | Front end | Boot: intro movie, title, attract, main menu | 2 | approximate | 1 | walks reach the main menu (`quickplay`, `keys`); the menu buttons' labels overlap them [I], no attract mode | GD05-005 |
| 2 | Front end | Solo menu, Quickplay song list, difficulty / instrument, results | 3 | approximate | 1.5 | `--walktest quickplay`; the song list cloth is drawn flat (GD05-022) | GD05-022 |
| 3 | Front end | Multiplayer menus (band, head-to-head, Tug of War) | 2 | approximate | 1 | `band_quickplay`, `score_duel`, `tug_of_war` walks; RB's band / head-to-head lobby is the port's stand-in (DECISIONS D10) | GD05-009 |
| 4 | Front end | Options: gameplay, audio, vocals, A/V calibration, character / band / save data | 2 | approximate | 1 | `credits` walk (options), screen sweep reaches the audio, gameplay, calibration, character / band data screens; volumes apply; calibration's click track [I] | GD05-009 |
| 5 | Front end | Training menu: tutorials list, Drum Trainer menu | 1 | approximate | 0.5 | the training menu and the tutorials' lesson menu open (`tutorials` walk) | GD05-009 |
| 6 | Front end | Pause, restart, quit, controller-lost pause | 1 | approximate | 0.5 | pause / resume (`keys`); restart from the lose screen (`rb_lose`); quit unchecked | GD05-005 |
| 7 | Front end | Loading screens and tips | 1 | works | 1 | `--walktest quickplay` |  |
| 8 | Front end | Credits (main menu and tour end) | 1 | approximate | 0.5 | credits_screen scrolls RB's config/credits.dtb rows (heading, title_name ...) in list_credits.milo's labels; the labels' placement is off [I] | GD05-009 |
| 9 | Front end | Lose screen, retry, quit | 1 | approximate | 0.5 | `rb_lose`: a failed song, RB's lose screen, retry | GD05-009 |
| 10 | Port | Help bar, Enhanced Options, GAMEPADS, cheats, The Glitches | 2 | approximate | 1 | Enhanced reachable; GAMEPADS: four players, Drums / Vocals instruments (built only); help bar not placed | GD05-010 |
| 11 | Gameplay | Guitar: hit windows, HOPOs, sustains, whammy, streak multiplier | 5 | works | 5 | RB's guitar watcher read in the XEX (FINDINGS: window, HOPO chain, sustains, multipliers) [C]; `--selftest` all charts; unsettled: which notes become HOPOs, the 50 ms overstrum gap | GD05-021 |
| 12 | Gameplay | Bass (6x multiplier) | 2 | works | 2 | as guitar, the 6x cap for bass [C RB 0x8245BDF0] | GD05-021 |
| 13 | Gameplay | Drums: pads, kick, judge | 4 | works | 4 | RB's drum watcher read in the XEX [C] (a gem per pad, the nearest time group, the closest lane on a miss, same-lane deadlines, chords passed once, freestyle before the first gem); `--selftest` plays all 292 drum charts through it; the kit's buttons map as frets (D6) | GD05-018 |
| 14 | Gameplay | Vocals: mic pitch, phrases, talkies, percussion, scoring | 6 | approximate | 3 | RB's vocal scoring read in the XEX [C] (accuracy curve, octave rule, hit multipliers, caps, strict ratings, streak on Awesome, scream deploy in freestyle, tambourine taps); `--selftest` sings every chart and taps 10180 of 10180 tambourine gems; talkies' speech matcher and the pitch detector are the port's [I] | GD05-018 |
| 15 | Gameplay | Every difficulty per part, no-kick / lefty options | 1 | approximate | 0.5 | every difficulty plays; no-kick / lefty options unchecked |  |
| 16 | Gameplay | A/V calibration moves drawing and judging | 1 | stand-in | 0.25 | the port's offset moves the judge; the RB tracks draw with the video offset; RB's calibration screens not wired |  |
| 17 | Gameplay | Audio: stems, miss mute per part, sfx | 2 | approximate | 1 | `--sfxtest`; stems by part; miss mute unchecked |  |
| 18 | Gameplay | Song start order and countdown | 1 | approximate | 0.5 | intro then track |  |
| 19 | Band | Overdrive: gain (phrases, whammy), deploy (tilt / Select, drum fill, vocals), sound | 3 | works | 3 | drain per beat, spotlight_phrase, band multiplier, the singer's scream deploy, the fill's crash after 3 hits [C]; player.dta deploy / stop_deploy play rp_deployed_<part>.cue / rp_depleted_<part>.cue (`quickplay` walk with GD05_AUTODEPLOY, logged) | GD05-019 |
| 20 | Band | Band meter, crowd, failing out, saving a bandmate | 2 | works | 2 | RB's fail-out, saves (at most two, save_level, save_energy, back from the brink), the out member's drain and the band meter [C] | GD05-019 |
| 21 | Band | Unison bonus | 1 | approximate | 0.5 | Unison with failed members excluded [C]; the phrase mask's exact rule [I] | GD05-019 |
| 22 | Band | Solos (solo section, % and bonus) | 1 | works | 1 | RB's solo award (percent, award, bonus x the solo's gems) [C]; `--selftest` 232 of 232 solos perfect | GD05-019 |
| 23 | Band | Drum fills and freestyle | 1 | approximate | 0.5 | the crash deploys after crash_fill_ready_hits [C]; when a fill shows [I] | GD05-019 |
| 24 | Band | Big Rock Endings | 1 | works | 1 | RB's coda pot (min(gap, 1500 ms) x 100 / s a lane) and the band-wide all-or-nothing [C]; `--selftest` 176 banked | GD05-019 |
| 25 | Band | Stars and score (thresholds, results) | 1 | works | 1 | band_thresholds by the band's instrument set, bonus_thresholds, Expert for gold [C]; the bonus base is the solos' [I] | GD05-019 |
| 26 | HUD | Guitar / bass / drum highway: gems, colours, sustains, beat lines, now bar | 3 | approximate | 1.5 | RB's track through the menus (`rb_track_frames` counter); flat sustains | GD05-006 |
| 27 | HUD | Vocal track: lyrics, pitch tubes, arrow, phrase markers | 2 | approximate | 1 | the port's vocal track (tubes, lyrics, arrow, phrase meter), not RB's widgets | GD05-006 |
| 28 | HUD | RB's HUD: score, multiplier, band meter, Overdrive bar, notices | 3 | stand-in | 0.75 | the debug text HUD and the port's band HUD | GD05-006 |
| 29 | HUD | 2 to 4 players on screen | 2 | approximate | 1 | the instruments side by side, vocals on top (`band_quickplay`) | GD05-006 |
| 30 | Stage | Venues with lighting and effects | 4 | approximate | 2 | lights read as RB stores them, WorldDir overrides applied; some venues still too bright [I] | GD05-007 |
| 31 | Stage | Camera director (venue track) | 2 | approximate | 1 | RB's pick_new_shot [C data]: coop_ shots only, PS3 ones dropped, NORMAL re-keyed by focus flags, held focus notes 61..64 / 70..73, facing alternation, no far after far, the deck and selection_weight, directed cuts hold note-60 picks, LOSE at the fail; GD05_SHOTLOG shows the picks; no intro chains, no band targets (teleports), no BFTB shots | GD05-007 |
| 32 | Stage | Crowd | 1 | stand-in | 0.25 | the WorldCrowd archetypes load and draw (the `crowd_drawn` counter: 70 in small_club_09); not yet confirmed by eye | GD05-007 |
| 33 | Stage | Band members: characters, outfits, instruments | 4 | approximate | 2 | the four slots filled with premade characters and their outfit pieces and instruments; skin colours flat; `band_members` counter | GD05-008 |
| 34 | Stage | Band animation: song-driven clips, lip sync, IK | 3 | stand-in | 0.75 | the base character's stand / win / lose clips; no playing or lip sync | GD05-008 |
| 35 | Modes | Solo Quickplay | 3 | approximate | 1.5 | `--walktest quickplay` | GD05-009 |
| 36 | Modes | Band Quickplay, 2 to 4 local players | 3 | approximate | 1.5 | `band_quickplay` walk | GD05-009 |
| 37 | Modes | Head-to-head: Score Duel, Tug of War | 2 | approximate | 1 | `score_duel`, `tug_of_war` walks; Tug of War's split parts and meter not done | GD05-009 |
| 38 | Modes | Solo Tour (per instrument) | 3 | approximate | 1.5 | `solo_tour` walk: creator, hub, gig guide, hints, event, song, results, gig complete | GD05-009 |
| 39 | Modes | Band World Tour (cities, venues, gigs, fans, stars, unlocks, cash) | 4 | approximate | 2 | `band_tour` walk: band, name, hub, hints, gig set list, results | GD05-009 |
| 40 | Modes | Practice (sections, speeds) | 1 | stand-in | 0.25 | practice reaches its game screen (sections, speed); not checked by ear | GD05-009 |
| 41 | Modes | Tutorials (guitar, drums, vocals) and the Drum Trainer | 2 | missing | 0 | the lesson menu opens; no lesson runs | GD05-009 |
| 42 | Custom | Character creator | 3 | approximate | 1.5 | `creator` walk: maker (name, hometown, attitude), appearance (hair / head lists), confirm, saved; the created look is the one on stage in its gigs; no 3D preview, no physique / colours | GD05-008 |
| 43 | Custom | Rock Shop (clothes, hair, tattoos, make-up, instruments) | 2 | approximate | 1 | `rock_shop` walk: the punk store's torso list, an affordable shirt bought (money paid by the script) and worn; no 3D preview, no colours | GD05-009 |
| 44 | Custom | Bands: create, name, logo, line-up | 1 | approximate | 0.5 | a band created and named in `band_tour`; no logo | GD05-009 |
| 45 | Saves | Profiles, scores, tour progress, unlocks (versioned JSON) | 3 | approximate | 1.5 | rb_profile.json (characters, bands, tours); Quickplay scores not saved | GD05-009 |
| 46 | Video | Bink movies (intro, credits, tour) | 1 | works | 1 | `--videotest` | GD05-016 |

Weights: front end 16, gameplay 22, band 10, HUD 10, stage 14, modes 18,
customisation 6, saves 3, video 1 = 100.

## Not counted

Online play, leaderboards, achievements, the Music Store and downloads are
not in the port (GD05-013); the PS3 and Wii screens on the disc are not
counted either. Port-only extras beyond row 10 (HDR, backends, Steam Deck
preset) are tracked in the backlog, not here.

## History

| When | Score | Note |
|---|---|---|
| 2026-11-09 18:30 (wave 02 start) | 21.0% | the ledger agrees with wave 01's 0.2 |
| 2026-11-09 22:00 (wave 02 gate 1) | 50.5% | band on stage, vocals, band play, tours, shop, judges read in the XEX |
