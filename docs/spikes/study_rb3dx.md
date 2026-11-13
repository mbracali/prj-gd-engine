# Study: Rock Band 3 Deluxe (RB3DX)

Study, 2026-11-13 (backlog GDALL-052). Read-only: nothing was built, run or imported. The repo was cloned
without its audio, video, model and song-update files (scripts and docs only). Every section ends with an empty
**Owner's take:** line for you.

## Executive summary

| Project | Repo / site | Licence | What it is | Platforms | We take | For | Value (1-10) |
|---|---|---|---|---|---|---|---|
| Rock Band 3 Deluxe | hmxmilohax/rock-band-3-deluxe, rb3dx.milohax.org | **None** (all rights reserved) | The largest Harmonix-game mod: edited DTA scripts, art and song updates in a new ARK, plus RB3Enhanced (a separate GPL plugin) on the 360 | Modded Xbox 360 (with RB3Enhanced), PS3, RPCS3; Wii build files | Venue / PostProc knobs, timing windows, view times, the intro clock, kick bounce, brutal / countdown logic, a huge option list, a PT-BR translation of RB3 | gd05 (RB1) now, gd-game-18 (RB3) later; GDALL-045 / -048 / -051 | 8 |
| RB3 decompilation | DarkRTA/rb3, rb3dx.milohax.org/decomp/ | **None** | A matching decompilation of the **Wii** RB3 (build 100901_A), in progress | Wii | Readable names for Harmonix engine classes (Rnd, PostProc, BandCamShot, CharHair, beatmatch) | gd05, gd-game-08, gd-game-18 (research only) | 6, legal care needed |

**The key finding.** RB3DX is a script mod like GH2DX and RB2DX, but much larger, and its scripts touch the same
engine pieces our gd05 port is still matching: PostProc frame pacing and effects, camera shake, the intro clock,
the track's triggers. Two things here explain open gd05 gaps directly: **RB1 bounces the drum highway on every
kick** (RB3DX calls it "Kick Bounce, like Rock Band 1 / 2"; our RB1 data has `kick_hit -> kick_drummer.trig`, which
the port does not fire, D100), and **two PostProcs have their own low frame rate on purpose** (bloom, photocopy).
Nothing can be copied: no licence on the mod or the decompilation, and the content is Harmonix's.

Owner's take:

## Licensing

| Item | Licence | Copy? | Notes |
|---|---|---|---|
| rock-band-3-deluxe | No licence file | **No** | Harmonix scripts, milos, moggs, song updates, a 360 `default.xex` in `platform/xbox`, PS3 / Wii patchers |
| rb3dx_assets (submodule, LinosM) | No licence | **No** | Their art |
| RB3Enhanced (plugin the 360 build needs) | **GPL-2.0** | **No** | GPL: nothing copied, not even small helpers |
| DarkRTA/rb3 (decompilation) | No licence | **No** | It is Harmonix's code re-derived; same status as our own Ghidra output: research only, never pasted (`docs/legal.md`) |
| hmxmilohax/tools | GPL-3.0 | **No** | Syntax files for DTA editors |
| Their PT-BR / Spanish / French / German / Italian texts | No licence | **No** | Fan translations of Harmonix text |

Their contribution rules forbid agent-made pull requests and say they do not want their mods used in AI
recompilation projects: facts only, never contact their repos from an agent.

Owner's take:

## Links

| What | Link |
|---|---|
| Site | https://rb3dx.milohax.org/ (features /features/, downloads /downloads/, install /install/, about /about/, bug reports /bugreports/, decomp /decomp/, Spanish /es/, Portuguese /pt/) |
| Repo | https://github.com/hmxmilohax/rock-band-3-deluxe (develop branch; "nightly" release 2026-10-07; 96 open issues) |
| Assets submodule | https://github.com/LinosM/rb3dx_assets |
| RB3Enhanced | https://github.com/RBEnhanced/RB3Enhanced |
| Decompilation | https://github.com/DarkRTA/rb3, progress https://decomp.dev/DarkRTA/rb3 |
| Group | https://milohax.org/, https://discord.gg/milohax, https://bsky.app/profile/milohax.org, https://fluxer.gg/z4nhvU9W |

## What it is

- **Who / status.** MiloHax and many contributors; repo since 2022-01, nightly builds, very active (commits on
  2026-11-13). The repo is about 15 GB when cloned in full.
- **How it works.** The player brings their own RB3; a Python + ninja build packs scripts, art and song updates into
  an ARK. Settings are written to files by RB3DX's own read / write helpers. On the 360 it needs RB3Enhanced (a
  plugin that adds script functions and patches; its patches are embedded in the DX build). On PS3 / RPCS3 some
  features unlock hidden engine options (self-shadowing, per-pixel lighting, higher-quality models).
- **Decompilation.** A separate project decompiles the Wii version only (other versions used link-time
  optimisation, which makes a matching decompilation infeasible). It is not a port and does not plan one before it
  is complete.

Owner's take:

## Features (as listed by the project)

- Play: "Play A Show" song voting (from RB4); 100+ added characters; gamepad and keyboard play; button remap and
  instrument slots; Brutal Mode (gems hidden as you do well); Kick Bounce (RB1 / RB2 drum track bounce); Karaoke;
  Precision and Drunk modes; Party Play (drop in without pausing); Acceleration Mode (speeds up as you play well);
  Overdrive in practice; auto-activating drums (no fills); gem colour shuffle; vocal pitch display choice; no-fail;
  perfection (restart on a miss); no Big Rock Endings; no lanes / RB4 lanes; auto kick; autoplay with watermarks.
- Speeds: song speed and track speed in 5 % steps (per instrument, synced online), practice 10..100 % in 5 % steps,
  Select restarts a practice section.
- Display: track themes (gems, highway length and angle, fret colours, background, HUD; theme editor with preview,
  save and import; shareable); lyrics; song time; note streak; Pro Keys key highlight; song title overlay; FC
  indicator; measure-break countdowns; end-of-song feedback (FC, missed notes, overstrums); "Full Combo" sound;
  Nice (69 %) and Awesome Choke (98-99 %) solo callouts; detailed judgements, accuracy and average stats; mid-song
  calibration in 1 ms steps; per-instrument latency.
- Venues: selectable venues including a black venue; venue frame rate up to 60; music-video venues; a
  "Post Processing" menu (post-processing, motion blur, camera blur, grain, slow-motion effects); camera shake off;
  band attitude; hide band members without a part; skip the venue intro (7 s to 1 s); cycle camera, screensaver
  and jukebox modes.
- Library: 4,000+ songs on hardware (32,000 on RPCS3), keyboard search (chatpad), scroll by 10, Expert default,
  sources sorted (official, DLC, RBN, customs), song upgrades (harmonies, keys, Pro parts), author finder.
- Platform: Spanish, French, Portuguese and German translations; RB3Enhanced compatibility; PS3 fixes.

Owner's take:

## What we can use

### (a) Knowledge for our ports

| Fact (re-check on our disc before use) | Seen in | Use for us |
|---|---|---|
| **Kick bounce:** RB1 / RB2 bounce the drum track on each kick hit; RB3 dropped it. Our RB1 data has `gem_track.dta: (kick_hit {kick_drummer.trig trigger})`, and gd05 does not fire `kick_drummer` / `kick_neighbor` (D100's list) | RB3DX features and `dx_track_funcs.dta` (`kick_drummer.trig`, `kick_success.tnm` on `cameras_top.grp`); our RB1 `gem_track.dta` | A missing "highway effect" in GD05-096: fire the kick triggers on kick hits |
| `bloom.pp` and `photocopy.pp` keep their own low `emulate_fps` on purpose (RB3DX's "slow-mo" switch zeroes or restores them; its venue FPS skips them) | `dx/world/dx_camera_funcs.dta` | GD05-075: never override those two; check which PostProcs on RB1's disc carry their own rate |
| PostProc motion blur is four values (`motion_blur_velocity`, `_blend`, `_weight`, `_exposure`); some PostProcs are their own effect (photocopy, shitty_tv, video_security, film_b+w, tvscreen...) and must not be touched by a global switch | same | An Enhanced "Motion Blur: Off" for gd05 / gd18 with the original's knobs; the exception list is a check for GD05-069 |
| `rnd toggle_all_postprocs` exists in the engine | same | One switch for "Venue effects off" in RB-engine ports |
| Intro clock: the song clock starts at minus the intro shot's total duration (`set_intro_real_time`); skipping the intro sets 0 | `world/world_objects.dta` (`set_intro_shot`) | Confirms gd05's D98 intro timing; a "Skip Venue Intro" option is that one value |
| BandCamShot carries `self_shadow` and `ps3_per_pixel`; BandCharacter carries `self_shadow` | `dx_camera_funcs.dta` (`dx_ultra_shadows`) | For gd18 (and a check in gd05's BandCamShot layout): shadows and per-pixel lighting are per-shot settings in RB3 |
| Hit window: `slop` 100 ms (RB3); DX's precision 50, calibration 15; above 100 % song speed the window is multiplied by the speed; keys / Pro guitar miss windows 200 ms, keys fat-finger 100 ms | `config/beatmatcher.dta` | gd18 timing; the same speed rule as GH2DX for a "Song Speed" option |
| View times (seconds of highway): Easy 2.4, Medium 2.0, Hard 1.6, Expert 1.2; Pro guitar differs (Medium 2.2...); "synced" uses 1.2 for all | `config/track_graphics.dta`, `dx/game/dx_game_macros.dta`; RB4DX shows the same table | gd05 already reads RB1's view times from the panel (D50); track speed divides the view time; GDALL-051 for RB games |
| Brutal Mode maps the crowd rating (cooked = 1.25 x (rating - 0.2)) to how much of the track bottom is hidden, in six steps | `dx/track/brutal/dx_brutal_funcs.dta` | Their own rule (not the original's): an idea only |
| Measure-break countdowns are computed from the gap to the next gem per instrument | `dx/track/countdown/` | Idea for a HUD option (our own code) |
| Hide band members without a part: hide the character's meshes except placement / base, scale drum sticks to 0 | `dx_char_hider` | Idea for an option in gd05 quickplay |

Owner's take:

### (b) Ideas for our Enhanced menus and launcher

- Venue menu for RB games: venue choice, black venue, venue FPS (keeping bloom / photocopy), post-processing off,
  motion blur off, grain off, camera shake off, skip intro, hide band members without a part (GDALL-044 overlaps).
- Speeds (GDALL-051): song speed, track speed per instrument, synced speeds, track length, practice 10..100 %.
- Timing: mid-song calibration in 1 ms steps, per-instrument latency, a calibration mode with a tiny window.
- HUD: FC indicator and sound, note streak, song time, lyrics, measure-break countdowns, solo callouts, end-of-song
  feedback (missed notes, overstrums), detailed judgements and average accuracy.
- Library (GDALL-042): keyboard search, scroll by 10, sources sorted per game, Expert default.
- Cheats (GDALL-048): Brutal, Acceleration, Perfection, Drunk, gem colour shuffle, no Big Rock Endings, jukebox and
  screensaver modes, Kick Bounce for later RB games.
- Translations (GDALL-045): RB3DX shipped a full PT-BR translation of RB3 (about 21,000 lines) and keeps one folder
  per language (`deu`, `eng`, `esl`, `fre`, `ita`, `ptb`); the layout is a good model for ours, the text is theirs
  (we write our own PT-BR, using common game terms).
- Track themes with a live preview and import / export (our `enhanced.json` export already exists).

Owner's take:

### (c) Things to avoid

- Their song updates, harmonies, Pro upgrades, characters and art: fan work over Harmonix's songs, no licence.
- RB3Enhanced code (GPL-2.0) and the decompilation's code (Harmonix's): read for facts at most, never copied; if the
  owner prefers a strict clean room, skip the decompilation entirely and keep to our Ghidra.
- Online features (Stealth servers, remote track themes): out of scope.

Owner's take:

## Bugs of ours this explains

- **GD05-096 (highway effects missing):** RB1's drum highway bounce on kicks (`kick_drummer` / `kick_neighbor`) is not
  fired by the port; RB3DX documents it as an RB1 / RB2 behaviour. Strong lead. [C data: our RB1 `gem_track.dta`]
- **GD05-075 / GD05-069 (frame rate, look):** bloom and photocopy PostProcs pace themselves; a global world-FPS
  change must skip them.
- **GD05-098 (cameras):** confirms the intro clock (D98); nothing on shots inside the crowd.
- **GD05-097 (band):** nothing direct.

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent time) | Value |
|---|---|---|---|---|---|
| NEW-21 | gd05 | Original | Fire the track's kick triggers on kick hits (`kick_drummer` / `kick_neighbor`, gem_track.dta's `kick_hit`): RB1's drum highway bounce (GD05-096) | 2-3 h | High |
| NEW-22 | gd05 | Original | Audit RB1's PostProcs that carry their own `emulate_fps` (bloom, photocopy, others) before any world-FPS option (GD05-075) | 1-2 h | Medium |
| NEW-23 | gd05 | Enhancement | Venue options from RB's own knobs: post-processing off (`toggle_all_postprocs`), motion blur off, skip venue intro, hide members without a part | 4-6 h | Medium |
| NEW-24 | gd05 | Enhancement | HUD extras: FC indicator, note streak, song time, measure-break countdowns, end-of-song feedback | 6-8 h | Medium |
| NEW-25 | all | Enhancement | GDALL-045 layout: one folder per language for our own strings, PT-BR first, written by us | 2-3 h (layout) | Medium |
| NEW-26 | gd-game-18 | Original | Discovery notes for GD18-001: view times, slop / miss windows, per-shot shadows and per-pixel lighting, intro clock, PostProc list; decide whether the Wii decompilation may be read (owner) | 4-6 h | High |

Owner's take:
