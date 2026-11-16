# Study: Rock Band 2 Deluxe (RB2DX)

Study, 2026-11-13 (backlog GDALL-052). Read-only: nothing was built, run or imported. The repo was cloned
without its audio, video and model files (scripts and docs only). Every section ends with an empty
**Owner's take:** line for you.

## Executive summary

| Project | Repo / site | Licence | What it is | Platforms | We take | For | Value (1-10) |
|---|---|---|---|---|---|---|---|
| Rock Band 2 Deluxe | hmxmilohax/rock-band-2-deluxe, rb2dx.milohax.org | **None** (all rights reserved) | A mod of the original RB2: edited DTA scripts and track art in a new ARK; the original executable runs | Modded Xbox 360, PS3 / RPCS3 (Wii / PS2 trees in the repo, not released) | Venue frame-rate side effects (camera shake, hair), black venue, grain / post-processing switches, track camera numbers, RB1-to-RB2 song fields, many option ideas | gd05 (RB1), gd-game-08 (RB2), GDALL-042 / -044 / -048 / -051 | 7 |

**The key finding.** RB2 runs RB1's engine with additions, so RB2DX's script edits point straight at RB1 behaviours
we are chasing in gd05: what the world's frame rate does to the camera shake and the hair, which PostProc settings
make a black venue, how grain and the venue's post-processing are switched off. Their repo also carries RB2's own
scripts (useful later for gd-game-08 but we will read those from our own RB2 disc). Nothing can be copied: no licence,
and the content is Harmonix's.

Owner's take:

## Licensing

| Item | Licence | Copy? | Notes |
|---|---|---|---|
| rock-band-2-deluxe | No licence file | **No** | Also contains Harmonix scripts, milos, moggs, RB1 song data for export |
| Custom track art, gem / smasher / flame textures | None | **No** | Fan art over Harmonix art |
| Mackiloha, dtab, swap_rb_art_bytes | MIT, BSD-3, none | Ideas only | We read the formats ourselves |

Their contribution rules forbid agent-made pull requests and say they do not want their mods used in AI
recompilation projects: facts only, never contact their repos from an agent.

Owner's take:

## Links

| What | Link |
|---|---|
| Site | https://rb2dx.milohax.org/ (features.html, downloads.html, install.html, about.html, index_es.html) |
| Repo | https://github.com/hmxmilohax/rock-band-2-deluxe |
| Sister project | https://rb3dx.milohax.org |
| Group | https://milohax.org/, https://discord.gg/milohax, https://bsky.app/profile/milohax.org |

## What it is

- **Who / status.** MiloHax; repo since 2021-10, an "initializer" release 2022-05, commits still landing in 2026-11.
  Downloads moved to the site; the repo holds the build (Python, batch / shell scripts per platform).
- **How it works.** The player brings their own RB2; the build packs edited scripts and textures into an ARK
  (ArkHelper, Superfreq, dtab). Settings are saved to a small milo of their own (`dx_settings.milo`) next to the game.
  An in-game overshell holds the options. RB1 and Lego Rock Band songs come from the player's own exports (RB2 can
  import RB1's songs; DX adds the "unexportable" ones' metadata).
- **Platforms.** Xbox 360 (modded) and PS3 / RPCS3; 1080p is a PS3 / RPCS3 feature.

Owner's take:

## Features (as listed by the project)

- Gameplay: no strum limit; static drum fills (fills off, Overdrive auto-deployed); no flames; double bass on stock
  kits; autoplay; gamepad play (guitar or drums on a pad); synced track speeds; song speed, track speed, track length
  and venue frame-rate sliders; no quick restart; auto kick; calibration mode (tiny hit window).
- Visuals: improved gems and smashers, chosen per part (gems, smashers, highway per instrument, streak meter,
  spotlight, multiplier ring, Overdrive bar, rails, flames, score, solo box, stars, fonts, vocal arrows / notes);
  gem colour per slot; track angle presets (RB2 / RB3 / RB4 / custom); 60 FPS venues; black venue / focus mode;
  "fireworks" gems; post-processing off, grain off, camera shake off.
- Content: RB1 / Lego Rock Band exports, all genres from later games, menu music from every main game, NPC characters
  playable.
- Fit and finish: Set Gem Colors menu; song title always on (with stats); chart authors at the start; Expert default;
  no lost fans on restart; FC indicator; countdowns before notes after long gaps; jukebox / demo mode.

Owner's take:

## What we can use

### (a) Knowledge for our ports

| Fact (re-check on our disc before use) | Seen in | Use for us |
|---|---|---|
| When the venue frame rate is raised to 25 or more, DX scales every BandCamShot keyframe's `shake_noisefreq`, `shake_noiseamp` and `shake_maxangle` by 0.25 and resets each CharHair with 35 (RB1's own `band_objects.dta` resets hair with 15 at a shot's start). So RB's camera shake and hair are tuned for the film rate (24, D55): run faster, they shake and swing too much | `dx/config/dx_macros.dta` (DX_FPS_SETUP), RB1 `band_objects.dta` | GD05-075 (separate world / highway rates): if the port ever draws the world above 24, keep shake and hair on RB's 24 Hz world clock (or scale them); a check for GD05-098's "shots that feel wrong" |
| Every PostProc except `photocopy.pp` gets the new `emulate_fps` | same | The photocopy effect keeps its own rate (a stutter look): GD05-075 must leave it alone |
| Black venue: every PostProc's brightness, contrast and lightness to -800, `emulate_fps` 1, `world_panel` hidden, crowd sounds off; for the song title and HUD, `mod_nohud` and the black venue exclude each other | `ui/game.dta`, `config/modifiers.dta` | GDALL-044 (potato mode) on gd05 / gd08 with RB's own switches |
| Grain off: every PostProc's `noise_intensity` 0 | `ui/game.dta` | An Enhanced "Film Grain: Off" for gd05 that is the original's own knob (GD05-095 grain is still above the movies) |
| "Post-processing off" replaces the VENUE track's PostProc overrides (notes 96..110) with `ProFilm_a.pp`, i.e. ProFilm_a is RB's neutral look | `config/midi_parsers.dta` | Confirms gd05's fallback to ProFilm_a.pp (camera_director.cpp); a "Venue Effects: Off" option |
| RB2's VENUE PostProc parser shifts events 0.15 s earlier in co-op and drops blends under 0.5 s; RB1's parser (`world_postproc_parser`) differs | RB2 `midi_parsers.dta` vs our RB1 copy | For gd-game-08: read RB2's own parser, do not reuse RB1's rules |
| Track camera presets (pitch, FOV, camera y / z, fade start / length): RB2-like -43.5 / 19 / -87 / 19 / 74 / 15; RB3 -40.5 / 19 / -100 / 22 / 70 / 50; RB4 -40.15 / 21 / -112 / 26.25 / 130 / 15; 2- and 3-player sets differ | `dx/ui/dx_overshell_macros.dta` | gd05 already reads RB1's own track configuration (pitch, FOV, place, fade: D50); these are a cross-check and the values for a "Track Angle" option |
| Speed sliders: track speed 0.5..x in 5 % steps, song speed in 5 % steps, track length (how far the highway reaches) from 0.25 up, venue FPS 1..60 | `ui/overshell_sliders.dta` | GDALL-051 for RB games (track speed and track length are separate knobs in RB) |
| Static fills: with fills off, a drummer with Overdrive deploys it on every fourth measure | `config/beatmatcher.dta` | An Enhanced "No Drum Fills" for gd05 / gd08 that matches how DX players expect it |
| RB1 songs inside RB2 need `format 3`, a precomputed `base_points` (the stars' base score) and extra fields (decade, preview, album track) | `_ark/songs/rb1/missing_song_data.dta` | GDALL-042's RB wave (RB1 songs in gd08): compute base points ourselves from the chart as RB2 does |
| RB2 stars: per-instrument and per-band threshold tables (`star_thresholds.dta`) | RB2 data | gd08: data-driven, read from our RB2 disc |
| Smasher plate draw order (before / after the gems) is the same script in RB1 and RB2 (only formatting differs) | `track/smasher_plate.dta` vs our RB1 copy | Confirms gd05's D61 draw order; nothing to change |

Owner's take:

### (b) Ideas for our Enhanced menus and launcher

- Speeds for RB games: Track Speed, Track Length, Song Speed, Synced Track Speeds (all difficulties at Expert's), kept
  across games (GDALL-051).
- Venue switches: Venue Effects off (ProFilm_a), Film Grain off, Camera Shake off, Venue FPS (with the shake / hair
  rule above), Black Venue (GDALL-044).
- Track looks: gem colours per slot, track angle presets, gems / smashers from the player's other RB / GH discs.
- Gameplay: No Drum Fills, Auto Kick, Calibration Mode (tiny window), No Flames, Gamepad play for drums and guitar
  (gd05 already maps drums on pads, D6).
- HUD: FC indicator, song title always with stats, chart author at the start (from the song's own data), countdowns
  before notes after long gaps.
- Content (GDALL-042): RB1 songs in RB2 and the other way, read in place from the player's discs.
- Cheats (GDALL-048): jukebox / demo mode (autoplay + no HUD + chosen venue).

Owner's take:

### (c) Things to avoid

- Their fan art, textures, menu music bundles, NPC characters and song metadata files: not ours.
- Hacks that cover a bug instead of fixing it (their black venue restarts the song once to dodge a "notes too
  fast" bug): we fix causes.
- The DX names ("Overshell", "Deluxe Settings").

Owner's take:

## Bugs of ours this explains

- **GD05-075 / GD05-069 (band 24 FPS vs highway 60):** the film rate is part of RB's look, and raising it needs the
  shake and hair rule above; it supports keeping 24 as the default.
- **GD05-096 (highway effects, grain):** the grain is the PostProcs' `noise_intensity`, switchable without touching
  the rest; the smasher draw order is the same as RB2's, so the "something over the gems" is not a script order
  difference.
- **GD05-098 (cameras):** no direct cause; their numbers for shake at higher rates are a check if our shots look too
  shaky.

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent time) | Value |
|---|---|---|---|---|---|
| NEW-14 | gd05 | Enhancement | GD05-075 rule: when the world runs above RB's film rate, keep the camera shake and the hair on a 24 Hz clock; photocopy.pp keeps its own rate | 3-4 h | High |
| NEW-15 | gd05 | Enhancement | Venue switches from RB's own knobs: Venue Effects off (ProFilm_a), Film Grain off (`noise_intensity` 0), Camera Shake off | 2-3 h | High |
| NEW-16 | gd05 | Enhancement | Black venue (GDALL-044 for RB): PostProcs dark, world panel hidden, crowd sounds off | 2 h | High |
| NEW-17 | gd05 | Enhancement | RB speeds: Track Speed, Track Length, Song Speed, Synced Track Speeds (GDALL-051) | 4-6 h | High |
| NEW-18 | gd05 | Enhancement | No Drum Fills (Overdrive every fourth measure), Auto Kick, Calibration Mode, No Flames | 3-4 h | Medium |
| NEW-19 | gd05 | Enhancement | Track Angle presets (RB1 / RB2 / RB3 / RB4) after verifying RB1's own track camera | 2-3 h | Low |
| NEW-20 | gd-game-08 | Original | Discovery notes for GD08-001: RB2 = RB1 engine + RB2 scripts; diff RB2's DTA against RB1's (VENUE parsers, stars, songs fields, modifiers, tour) | 4-6 h | High |

Owner's take:
