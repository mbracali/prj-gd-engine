# Study: Rock Band 4 Deluxe (RB4DX)

Study, 2026-11-13 (backlog GDALL-052). Read-only: nothing was built, run or imported. The repo was cloned
without its textures, sound banks and binaries (scripts and docs only). Every section ends with an empty
**Owner's take:** line for you.

## Executive summary

| Project | Repo / site | Licence | What it is | Platforms | We take | For | Value (1-10) |
|---|---|---|---|---|---|---|---|
| Rock Band 4 Deluxe | hmxmilohax/Rock-Band-4-Deluxe, rb4dx.milohax.org | **None** (all rights reserved) | A mod of the PS4 RB4: loose DTA scripts and Forge textures loaded by a GoldHEN plugin (RB4DX-Plugin); the original executable runs | Jailbroken PS4 (GoldHEN), ShadPS4 emulator; **not Xbox One** | Proof that RB4 (Forge engine) is still driven by Harmonix DTA config; config values (hit window, view times, track muting, Overdrive switches); the format toolchain (LibForge, DtxCS); option ideas | gd-game-19 (RB4, our Xbox One disc), GDALL-051 | 5 |

**The key finding.** RB4 moved to Harmonix's Forge engine (new mesh, texture, scene, song and sound formats), but
its game rules are still read from DTA config that the mod rewrites at start-up (`syscfg`: beatmatcher, gamemodes,
track graphics). So the knowledge path we used for GH2 and RB1 (read the DTA, find the C++ that uses each value)
still works for gd-game-19, and the community already documents the Forge file formats (LibForge, LGPL-3.0: docs
and 010 templates to read, not code to copy). The mod targets the PS4 only; our disc is Xbox One, so every format
fact needs checking on our files.

Owner's take:

## Licensing

| Item | Licence | Copy? | Notes |
|---|---|---|---|
| Rock-Band-4-Deluxe | No licence file | **No** | Also ships Harmonix scripts, Forge textures / entities, FMOD banks and a compiled plugin (`RB4DX-Plugin.prx`) |
| RB4DX-Plugin (GoldHEN plugin) | No licence | **No** | PS4 code; not useful to us |
| LibForge / ForgeTool | **LGPL-3.0** | **No code** | Read its format notes and 010 templates for facts; write our own readers (LGPL would bind any copied code) |
| DtxCS (C# DTA / DTB library) | No licence found | **No** | We have our own DTB reader |

Their contribution rules (same group) forbid agent-made pull requests and say they do not want their mods used in
AI recompilation projects: facts only, never contact their repos from an agent.

Owner's take:

## Links

| What | Link |
|---|---|
| Site | https://rb4dx.milohax.org/ (features.html, downloads.html, install.html, about.html) |
| Repo | https://github.com/hmxmilohax/Rock-Band-4-Deluxe (feature list in `dependencies/features.md`) |
| Plugin | https://github.com/LlysiX/RB4DX-Plugin |
| Forge formats | https://github.com/mtolly/LibForge |
| DTA library | https://github.com/InvoxiPlayGames/DtxCS |
| Group | https://milohax.org/, https://discord.gg/milohax, https://bsky.app/profile/milohax.org |

## What it is

- **Who / status.** MiloHax; repo since 2022-07, an "initializer" release 2022-08, commits until 2026-11.
- **How it works.** On a jailbroken PS4, GoldHEN loads RB4DX-Plugin for the US (CUSA02084) and EU (CUSA02901)
  games; the plugin lets the game read RB4DX's loose files. Their DTA hooks rewrite values in the game's loaded
  config (`syscfg`) at start-up. Settings are files whose presence switches an option on (e.g.
  `settings/visuals/60fpsbg.dta`); positions are numbers read from files. A batch build converts textures with
  ForgeTool and scripts with DTXTool.
- **Platforms.** PS4 and ShadPS4. Nothing for Xbox One.

Owner's take:

## Features (as listed by the project)

- Quality of life: skip the intro movie; Remote Play; a settings menu; track themes; menu themes; Overdrive gain off;
  Overdrive in practice; whammy pitch bend off; pad play ("Padtar"); song speed in 5 % steps; practice 5..500 %;
  synced track speeds; 60 FPS venues; selectable venues (including a static background); song title always on (and
  movable); calibration mode (small window); calibration in 1 ms steps.
- Authoring: autoplay with watermarks (no saving, "(AUTOPLAY)" in the title); performance mode (band without the
  gameplay); controller mapping menu.
- Other: Precision and Drunk modes; Insta-Fail; Tough Crowd; menu backgrounds off; crowd meter / HUD off; Nice /
  Awesome Choke solo callouts; song metadata; online-locked skins unlocked; screensaver mode; track shine off; note
  hit effects off; track muting on miss off; overstrum sound off; strike line glow / bounce options; HUD element
  positions (score, solo box, countdown, song title) movable.

Owner's take:

## What we can use

### (a) Knowledge for our ports (gd-game-19)

| Fact (re-check on our Xbox One files) | Seen in | Use for us |
|---|---|---|
| RB4's rules still come from DTA config: `beatmatcher watcher slop` (100), `pitch_bend_range`, `beatmatcher audio mute_volume` (vanilla -16 dB on a miss), `gamemodes defaults allow_overdrive_phrases` / `enable_overdrive`, practice overrides | `dx/ui/dx_hooks.dta` | gd-game-19's discovery: start from the DTA as in gd02 / gd05 |
| View times per instrument and difficulty: Easy 2.4, Medium 2.0, Hard 1.6, Expert 1.2 (Pro guitar Medium 2.2); `hopo_threshold` 170; vocals' lyric timings (`lyric_shift_ms`, `phrase_highlight_ms` 500...) | `config/include/track_graphics.dta`, `beatmatcher_system.dta` | Same family as RB1 / RB3: gd05's highway code carries over |
| File types: `.dtb` scripts, `.entity_*` / `.scene_*` / `.sgraph_*` models and scenes, `.png_*` / `.bmp_*` Forge textures, `.songdta_*` song data, `.rbmid` charts, FMOD `.bank` audio, `.mogg` for some songs | repo tree, LibForge | The format list for GD19-001; LibForge's docs and templates give the layouts to verify |
| Venues are Forge scenes under `venue/<type>/<variant>/<name>.scene` (big_club toys / standard / industrial / gym / arcade / boat / desert...), plus a "fake background" scene for no venue | `config/rb_venues.dta` | Venue loading map for gd-game-19 and a cheap "no venue" mode (GDALL-044) |
| Practice speed, song speed and Overdrive are config switches, not new code | `dx/track/practice_speed.dta`, hooks | Options cost little once the config reader exists |

Owner's take:

### (b) Ideas for our Enhanced menus and launcher

- HUD element positions (score, solo box, countdown, song title) movable in the Enhanced menu: useful on ultrawide and
  phones (all games).
- Track muting on a miss as a three-way choice (original / off / full mute).
- Overdrive (Star Power) off, or allowed in practice (GDALL-048 cheats).
- Practice speeds beyond 100 % (up to 500 %) for drills.
- Strike line glow / bounce and track shine off as accessibility options.
- Static picture instead of the venue (GDALL-044).
- Song speed, synced track speeds (GDALL-051).

Owner's take:

### (c) Things to avoid

- The plugin, textures, banks and patched songs: not ours.
- LibForge code (LGPL-3.0): formats re-written by us from the documentation and our own files.
- PS4-only facts presented as Xbox One facts: our disc decides.

Owner's take:

## Bugs of ours this explains

- None: we have no RB4 port yet.

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent time) | Value |
|---|---|---|---|---|---|
| NEW-27 | gd-game-19 | Original | GD19-001 input: the format list (dtb, entity, scene, sgraph, Forge textures, songdta, rbmid, FMOD banks) and LibForge's notes as a map; confirm each on our Xbox One files | 3-4 h | High |
| NEW-28 | all | Enhancement | Movable HUD elements (score, solo box, song title) in Enhanced, for ultrawide and phones | 1 day per engine family | Medium |
| NEW-29 | all | Enhancement | Track Muting on a miss: Original / Off / Full | 1-2 h per game | Low |
| NEW-30 | all | Enhancement | Practice speed above 100 % (to 200 % or more) | 1-2 h per game | Low |

Owner's take:
