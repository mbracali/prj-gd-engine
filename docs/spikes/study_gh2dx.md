# Study: Guitar Hero II Deluxe (GH2DX)

Study, 2026-11-13 (backlog GDALL-052). Read-only: nothing was built, run or imported. Repos cloned without
their audio, video and model files (only scripts and docs were read). Every section ends with an empty
**Owner's take:** line for you.

## Executive summary

| Project | Repo / site | Licence | What it is | Platforms | We take | For | Value (1-10) |
|---|---|---|---|---|---|---|---|
| GH2DX 360 | hmxmilohax/Guitar-Hero-II-Deluxe-360, gh2dx.milohax.org | **None** (all rights reserved) | A mod of the original GH2: replaced DTA scripts packed into a new ARK, plus a patched `default.xex` | Modded Xbox 360, Xenia (a Xenia build is bundled) | Feature ideas (speed sliders, modifiers, HUD readouts, themes); a few engine facts | gd02, gd03, gd01 (Enhanced menus) | 7 as ideas, 3 as knowledge |
| GH2DX Unified | hmxmilohax/Guitar-Hero-II-Deluxe-Unified | **None** | Pre-alpha rewrite of the same mod's scripts for 360 and PS2 together | Xbox 360 (PS2 not working yet) | How song speed, track speed and the hit window are tied; platform differences | gd02, gd03 | 5 |
| GH2DX 1.0 (PS2) | gh2dx.milohax.org (site only) | - | The first release (drums, extended setlists, streak counter) | PS2 / PCSX2 | Feature ideas only | gd03, gd01 | 3 |

**The key finding.** GH2DX does not reimplement anything: the original Harmonix engine runs, and the mod swaps the
game's own scripts (DTA) and art for edited copies. So there is **no engine code to learn from**; what we gain is
(1) a long, battle-tested list of options players want in GH2-era games, (2) a handful of engine facts that show
up as script values (speeds, hit window, frame pacing of the world), and (3) confirmation of things we already
found in Ghidra. Their repos carry Harmonix's own scripts, milo files, audio and a patched XEX, so **nothing in them
can be copied into our repo**; the ideas are free to rebuild in our own code.

Owner's take:

## Licensing

| Item | Licence | Copy? | Notes |
|---|---|---|---|
| GH2DX 360, GH2DX Unified | No licence file | **No** | All rights reserved; also contain Harmonix / Activision scripts, milos, moggs, vgs and a patched `default.xex` (game code) |
| Their custom songs and song packs (downloaded by their scripts from other repos) | None; charts of copyrighted music | **No, never link to them** | Out of scope for us; see "Things to avoid" |
| Mackiloha (ARK / milo tools) | MIT | Ideas only | We already read ARK / milo ourselves |
| dtab (DTA / DTB library, Haskell) | BSD-3 | Ideas only | Our DTB reader exists (gd02 `dtb_format.md`) |
| hmxmilohax/tools (DTA editor syntax files) | GPL-3.0 | **No** | GPL: nothing copied |
| Onyx toolkit (song converter) | No licence file at the repo root | No | Their song pipeline; we don't need it |
| Bundled Xenia build, x360ce | BSD / own | No | Emulator route; not ours |

Their contribution rules (Unified, RB3DX, RB2DX) say they do not want their mods used in "AI recompilation"
projects and forbid agent-made pull requests. We are a reimplementation and take nothing from them, but: **never
open issues or pull requests on their repos from an agent**, never present our work as related to theirs, and
keep the facts below re-checked on our own disc.

Owner's take:

## Links

| What | Link |
|---|---|
| Wiki page (soundtrack) | https://guitarhero.fandom.com/wiki/Guitar_Hero_II_Deluxe#Soundtrack (behind a bot check: not readable from here) |
| Site | https://gh2dx.milohax.org/index.html (features: https://gh2dx.milohax.org/features.html) |
| Unified repo | https://github.com/hmxmilohax/Guitar-Hero-II-Deluxe-Unified |
| 360 repo | https://github.com/hmxmilohax/Guitar-Hero-II-Deluxe-360 |
| Group | https://milohax.org/ (Discord https://discord.gg/milohax) |
| Tools they use | https://github.com/PikminGuts92/Mackiloha, https://github.com/mtolly/dtab, https://github.com/mtolly/onyxite-customs |

## What it is

- **Who / status.** The MiloHax modding group. GH2DX 1.0 was a PS2 release (site dated 2021-11-07: drums, expanded
  campaigns). GH2DX 360 started 2022-03 (last tagged "3.6.0-Alpha" build archive 2022-04, commits until 2026-10).
  Unified started 2023-12 as a rewrite because the 360 version had grown unstable (score meter misaligned, career
  and stats screens broken, by their own README); it is pre-alpha, PS2 not working yet.
- **How it works.**
  - The player brings their own GH2 files (raw 360 folder or PS2 ISO). A Python / batch build packs the edited DTA
    scripts and art into a new ARK (ArkHelper), converts textures (Superfreq), and copies in a patched `default.xex`
    (no strum limit; an optional no-overscan variant). Real hardware needs a modded console.
  - Options are changed in game through an "overshell" (press Select twice on most screens): sub-menus for
    modifiers, speeds, colours, venue / character / guitar overrides, themes.
  - Settings that must survive a reboot are edited in a DTA file and the ARK is rebuilt. Unified stores its track
    speed as two fake high scores on a hidden song in a hidden career slot (GH2's save has no free room).
  - Songs: the full 360 setlist, GH80s, part of GH1 and fan charts, installed as song packs (Xenia folders or LIVE
    files, max 16 packs; saving is turned off above 250 loaded songs because GH2's save breaks).
- **Soundtrack (from the repos; the wiki page was not reachable).** The career is GH2's eight tiers of six songs
  (Unified's `campaign.dta`), plus the 80s tiers and merged song lists (`songs_merge.dta`, `songs_80s.dta`); extra
  songs come from packs.

Owner's take:

## Features (as listed by the project)

- Gameplay: no strum limit; song speed and track speed sliders; venue frame rate slider; no fail; autoplay (with
  saving off and a marker on the results); lefty flip in the overshell; select to restart a practice section;
  Star Power in practice; drums ("four lanes + kick", PS2 1.0); single-player bass.
- Modifiers: Focus Mode (black venue, intro / outro and pop-ups off); Whammy FX off; track muting off; no flames;
  Tight Speedups; Towel Hyperspeed (notes hidden until about mid highway, track speed locked at 100 %); Brutal
  Mode (notes vanish at a fixed line); Name Title; Song Title Always; Clean MTV; Less Beat Lines; No Division
  Lines; Skip Intro; Skip Outro; Force Encore; crowd sounds off; streak / Star Power pop-ups off; Highway Shake
  off; GH2's own cheats (Monkey Head, Eyeball Head, Flaming Head, Performance Mode); Neckless Mode (highway gone,
  HUD kept); Roxbury Mode (the track rocks on the beat; they say it is hidden in the original files).
- Track looks: Void Frets; Eyeball Gems; a Clone Hero style HUD; Bright HOPOs / Dark Strums; GH1 gems and smashers;
  GH3 frets; custom highway images; colour picker for UI and track elements; UI themes; shareable theme files.
- HUD / results: FC indicator (streak counter turns colour), note streak, overstrums, missed notes, song progress,
  stars and star progress (to the hundredth); results with average multiplier, Star Power phrases, notes hit
  (to the hundredth), the modifiers used; separate "FC" stars.
- Menus: unlimited setlist scrolling, random song button, detailed song info (artist, author, album, year, genre,
  origin, difficulty, length), album art from the song folder, forced or random character / venue / guitar per song,
  fast menus (screen transitions off), "Modern" star calculation.

Owner's take:

## What we can use

### (a) Knowledge for our ports

| Fact (re-check on our disc before use) | Seen in | Use for us |
|---|---|---|
| Track speed is a factor on the difficulty's `track_speed` (our disc: Easy / Medium 1, Hard / Expert 1.4, `config/gen/track_graphics.dta`); DX lets it run 0.5..3.5 in 0.1 steps | Unified `track_graphics.dta`, speed sliders | A free track speed slider (GDALL-051) is one multiplier on the value we already read; GH2's own Hyper Speed is x1.5 (gd02 `highway_and_hud.md`) |
| Song speed: the beatmatcher's music speed is set when the track extends / the intro is skipped; track speed is divided by song speed so gems keep their on-screen speed; above 100 % the hit window (`slop`, 100 ms) is multiplied by the song speed, at or below 100 % it stays 100 | Unified `beatmatcher.dta`, `game.dta`, `track_graphics.dta` | The rule set for a "Song Speed" option in gd02 / gd03 / gd01 (practice already slows songs: same path) |
| Whammy pitch bend comes from the watcher's `pitch_bend_range` (1) and `ms_to_full_pitch_bend` (1000); 0 turns the bend off | Unified `beatmatcher.dta`; our gd02 `FINDINGS.md` has the same values [C] | "Whammy FX off" is one value, no new code |
| The strum limit is in the executable (DX patches the XEX to remove it); our gd02 found 1 overstrum per 50 ms [C] | GH2DX 360 README; gd02 `FINDINGS.md` | "No Strum Limit" cheat for gd02 / gd03 (and check gd01 for the same) |
| The world's frame pacing is each PostProc's `emulate_fps`; DX sets 30 or 60 on every PostProc after the world loads | Unified `funcs.dta` (`dx_emulatefps_setter`), gd02 `renderer.md` (+0x100) | A "Venue FPS" option is one override; GH2 at uncapped frame rates shakes the camera wildly (their issue tracker): our gd02 already runs the shake at 60 Hz (`venues.md`), keep that in gd03 |
| Black venue = hide `world_panel` and disable `crowd_audio`; the intro / outro and pop-ups are skipped with it | Unified `funcs.dta`, 360 README | The cheapest form of GDALL-044 (potato mode) for GH2-engine games |
| Every screen has an `animate_transition` flag | Unified `ui/*.dta` | "Fast menus" option: transitions off |
| PS2 and 360 differ in the scripts behind `HX_PS2` / `HX_XBOX` (e.g. `horizon_y` 110 on PS2, 120 on 360) | Unified `track_graphics.dta` | A checklist when gd03 (PS2) borrows gd02 (360) code; read gd03's own value from its disc |
| GH2's gold stars mean 100 % of the notes, not "no overstrum"; DX adds its own FC stars and hides them when autoplay was on | 360 `multiplayer.dta`, our gd02 `career_and_store.md` (flag 0x80) | Two leads for GD02-166 (#104): a 100 % run with overstrums is gold in the original; if our Autoplay or No Fail runs write records, the setlist shows gold too (DX turns saving off under autoplay) |
| GH2's save breaks above about 250 songs | 360 README | Confirms GDALL-042's plan: extra songs' scores go to our own `shared_songs.json`, never GH2's save |
| GH2 keeps average multiplier, Star Power phrases and notes hit per song | 360 endgame | Data for an extended results screen (Enhanced) |

Owner's take:

### (b) Ideas for our Enhanced menus and launcher

- Speeds: Track Speed (any value, not only GH2's x1.5 cheat), Song Speed (5 % steps), Venue FPS; keep the choice
  across games (GDALL-051).
- Cheats (GDALL-048): No Strum Limit, Towel Hyperspeed (gems hidden until mid highway), Brutal (gems vanish), Focus
  / black venue, Neckless, Roxbury (if it is really in the original's data: the name is not in our GH2 ARK list;
  check the track milos), Force Encore intro.
- HUD readouts (one Enhanced group, off by default): FC indicator, note streak, overstrums, missed notes, song
  progress, star progress; results to the hundredth; modifiers shown on the results.
- Track looks from the player's other discs: GH1 gems / smashers in GH2 and GH80s, GH3 frets (we own those games'
  data through the launcher; same idea as GDALL-006 / GDALL-042). Bright HOPOs / Dark Strums as an accessibility
  option; less beat lines; no lane dividers.
- Setlist: random song, detailed song info, unlimited scrolling, album art read from the player's own files, forced
  or random character / venue / guitar per song (Enhanced).
- Practice: Select restarts the section; Star Power in practice.
- Translations (GDALL-045): Unified keeps its new strings in one locale file per language and asks for one fixed
  text encoding (Latin-1 for the PS2); our PT-BR work should keep our strings in our own files, never in the disc's.
- Settings: theirs need a rebuild; ours (`enhanced.json`, applied at once) are already better. Shareable theme
  files are a nice idea for a later "track theme" export / import.

Owner's take:

### (c) Things to avoid

- Their song packs, custom charts, highways, art and the patched XEX: copyrighted or unlicensed. No links to them
  from our app, site or docs beyond this study.
- GPL tools (hmxmilohax/tools): nothing copied.
- Their names and branding in our code or menus ("Deluxe", "Overshell"): our options keep our own names.
- Approximations they accept ("a poor man's Brutal Mode"): we match the original or label the option ours.

Owner's take:

## Bugs of ours this explains

- **GD02-166 (#104, gold stars on Message in a Bottle):** see the gold-star row above; check whether the reporter
  played with Autoplay / No Fail and whether our port writes records then. [I]
- **Camera shake at high frame rates:** the original shakes harder when uncapped (their issue); gd02 already
  fixed it by running the shake at 60 Hz. Nothing open.

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent time) | Value |
|---|---|---|---|---|---|
| NEW-1 | gd02, gd03, gd01 | Enhancement | Song Speed option (5 % steps): music speed, track speed divided by it, hit window x speed above 100 % | 3-5 h per game | High |
| NEW-2 | gd02, gd03, gd01 | Enhancement | Track Speed slider (0.5..3.5) on top of the difficulty's value, kept across games (part of GDALL-051) | 1-2 h per game | High |
| NEW-3 | gd02, gd03 | Enhancement | Cheat: No Strum Limit (the 50 ms overstrum limit off) | 1 h per game | Medium |
| NEW-4 | gd02, gd03, gd01 | Enhancement | Cheats: Towel Hyperspeed, Brutal, Neckless, Force Encore (GDALL-048) | 4-6 h for the set | Medium |
| NEW-5 | gd02, gd03, gd01 | Enhancement | HUD readouts group: FC indicator, streak, overstrums, missed notes, song / star progress; results to the hundredth | 6-8 h per game | High |
| NEW-6 | gd02, gd03 | Enhancement | Whammy FX off, Track Muting off, Fast Menus (transitions off) | 1-2 h per game | Medium |
| NEW-7 | gd02, gd03 | Enhancement | Black venue / focus mode as the first step of GDALL-044 (world and crowd audio off, intro / outro skipped) | 2-3 h per game | High (weak phones) |
| NEW-8 | gd02, gd03 | Enhancement | Track looks from the player's other discs: GH1 gems / smashers, GH3 frets; Bright HOPOs / Dark Strums | 1-2 days | Medium |
| NEW-9 | gd02 | Bugfix | GD02-166 follow-up: Autoplay / No Fail runs must not write records or gold stars (check the original rule for No Fail first) | 1-2 h | Medium |
| NEW-10 | gd02, gd03, gd01 | Enhancement | Setlist extras: random song, detailed song info, forced / random character, venue and guitar per song | 4-6 h per game | Medium |

Owner's take:
