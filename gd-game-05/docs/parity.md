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
| 1 | Front end | Boot: intro movie, title, attract, main menu | 2 | approximate | 1.0 | `--walktest quickplay`; the main menu's labels overlap their buttons, no attract | GD05-005 |
| 2 | Front end | Solo menu, Quickplay song list, difficulty / instrument, results | 3 | approximate | 1.5 | `--walktest quickplay`; the song list cloth is flat (GD05-022) | GD05-022 |
| 3 | Front end | Multiplayer menus (band, head-to-head, Tug of War) | 2 | missing | 0 | not reached | GD05-009 |
| 4 | Front end | Options: gameplay, audio, vocals, A/V calibration, character / band / save data | 2 | stand-in | 0.5 | the options screen opens; sub-screens unchecked | GD05-009 |
| 5 | Front end | Training menu: tutorials list, Drum Trainer menu | 1 | missing | 0 | not reached | GD05-009 |
| 6 | Front end | Pause, restart, quit, controller-lost pause | 1 | approximate | 0.5 | `--walktest quickplay` pauses and resumes; restart / quit unchecked | GD05-005 |
| 7 | Front end | Loading screens and tips | 1 | works | 1.0 | `--walktest quickplay` | |
| 8 | Front end | Credits (main menu and tour end) | 1 | missing | 0 | | GD05-009 |
| 9 | Front end | Lose screen, retry, quit | 1 | missing | 0 | not checked | GD05-009 |
| 10 | Port | Help bar, Enhanced Options, GAMEPADS, cheats, The Glitches | 2 | approximate | 1.0 | Enhanced reachable from RB's options; help bar not placed; GAMEPADS without drums / vocals | GD05-010 |
| 11 | Gameplay | Guitar: hit windows, HOPOs, sustains, whammy, streak multiplier | 5 | approximate | 2.5 | `--selftest` all charts hit; GH2's judge with RB's points [I] | GD05-021 |
| 12 | Gameplay | Bass (6x multiplier) | 2 | approximate | 1.0 | as guitar | GD05-021 |
| 13 | Gameplay | Drums: pads, kick, judge | 4 | approximate | 2.0 | joypad watcher (DECISIONS D6), `--selftest` | GD05-018 |
| 14 | Gameplay | Vocals: mic pitch, phrases, talkies, percussion, scoring | 6 | missing | 0 | judge in `--selftest` only; plays the guitar in game (D7) | GD05-018 |
| 15 | Gameplay | Every difficulty per part, no-kick / lefty options | 1 | approximate | 0.5 | difficulties play; options unchecked | |
| 16 | Gameplay | A/V calibration moves drawing and judging | 1 | stand-in | 0.25 | gd02's offset (judging only) | |
| 17 | Gameplay | Audio: stems, miss mute per part, sfx | 2 | approximate | 1.0 | `--sfxtest`; miss mute unchecked | |
| 18 | Gameplay | Song start order and countdown | 1 | approximate | 0.5 | intro then track | |
| 19 | Band | Overdrive: gain (phrases, whammy), deploy (tilt / Select, drum fill, vocals), sound | 3 | stand-in | 0.75 | GH2's Star Power with RB's numbers | GD05-019 |
| 20 | Band | Band meter, crowd, failing out, saving a bandmate | 2 | stand-in | 0.5 | GH2's rock meter | GD05-019 |
| 21 | Band | Unison bonus | 1 | missing | 0 | | GD05-019 |
| 22 | Band | Solos (solo section, % and bonus) | 1 | missing | 0 | | GD05-019 |
| 23 | Band | Drum fills and freestyle | 1 | missing | 0 | | GD05-019 |
| 24 | Band | Big Rock Endings | 1 | missing | 0 | | GD05-019 |
| 25 | Band | Stars and score (thresholds, results) | 1 | stand-in | 0.25 | | GD05-019 |
| 26 | HUD | Guitar / bass / drum highway: gems, colours, sustains, beat lines, now bar | 3 | approximate | 1.5 | screenshots; flat sustains, gem colour swap | GD05-006 |
| 27 | HUD | Vocal track: lyrics, pitch tubes, arrow, phrase markers | 2 | missing | 0 | | GD05-006 |
| 28 | HUD | RB's HUD: score, multiplier, band meter, Overdrive bar, notices | 3 | stand-in | 0.75 | debug text HUD | GD05-006 |
| 29 | HUD | 2 to 4 players on screen | 2 | missing | 0 | | GD05-006 |
| 30 | Stage | Venues with lighting and effects | 4 | stand-in | 1.0 | loads every venue; some shots inside geometry (a red wall fills the screen in greengrass at 40 s), lighting [I] | GD05-007 |
| 31 | Stage | Camera director (venue track) | 2 | stand-in | 0.5 | random shot per category | GD05-007 |
| 32 | Stage | Crowd | 1 | missing | 0 | | GD05-007 |
| 33 | Stage | Band members: characters, outfits, instruments | 4 | missing | 0 | | GD05-008 |
| 34 | Stage | Band animation: song-driven clips, lip sync, IK | 3 | missing | 0 | | GD05-008 |
| 35 | Modes | Solo Quickplay | 3 | approximate | 1.5 | `--walktest quickplay` | GD05-009 |
| 36 | Modes | Band Quickplay, 2 to 4 local players | 3 | missing | 0 | | GD05-009 |
| 37 | Modes | Head-to-head: Score Duel, Tug of War | 2 | missing | 0 | | GD05-009 |
| 38 | Modes | Solo Tour (per instrument) | 3 | missing | 0 | | GD05-009 |
| 39 | Modes | Band World Tour (cities, venues, gigs, fans, stars, unlocks, cash) | 4 | missing | 0 | | GD05-009 |
| 40 | Modes | Practice (sections, speeds) | 1 | missing | 0 | | GD05-009 |
| 41 | Modes | Tutorials (guitar, drums, vocals) and the Drum Trainer | 2 | missing | 0 | | GD05-009 |
| 42 | Custom | Character creator | 3 | missing | 0 | | GD05-008 |
| 43 | Custom | Rock Shop (clothes, hair, tattoos, make-up, instruments) | 2 | missing | 0 | | GD05-009 |
| 44 | Custom | Bands: create, name, logo, line-up | 1 | missing | 0 | | GD05-009 |
| 45 | Saves | Profiles, scores, tour progress, unlocks (versioned JSON) | 3 | missing | 0 | scores not saved | GD05-009 |
| 46 | Video | Bink movies (intro, credits, tour) | 1 | works | 1.0 | `--videotest` | GD05-016 |

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
