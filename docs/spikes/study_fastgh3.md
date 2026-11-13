# Study: FastGH3 and GH3+ (for gd-game-04, Guitar Hero III)

Study, 2026-11-13 (backlog GDALL-052). Read-only: the repositories were cloned shallowly, read and deleted;
nothing was built, run or imported. Every section ends with an empty **Owner's take:** line for you.

## Executive summary

| Project | Who | Licence | What it is | Platforms | Status | We take | Make it ours (1-10) |
|---|---|---|---|---|---|---|---|
| FastGH3 | donnaken15 (Wesley) | **AGPL-3.0** for its own code; the repo also ships GH3 PC's `game.exe`, Bink, FMOD DLLs | A cut-down, patched GH3 **PC** build: a C# launcher converts `.chart` / `.mid` / archives into GH3 song paks, then starts a patched `game.exe` with a rewritten `qb.pak` | Windows (a Wii branch for Dolphin) | Active: 1.1, last commit 2026-07 | Knowledge only: GH3 rules confirmed (hyperspeed scales, practice pitch), the original's bugs, feature ideas | 2 (ideas), 9 (code: not possible) |
| GH3+ | ExileLord, continued by donnaken15 | **None** (all rights reserved); bundles stb headers | A C++ DLL plugin system that patches GH3 PC's x86 code: taps, HOPO / tap chords, open notes, note-limit fix, whammy and tilt fixes, modifiers | Windows | v0.6f, last commit 2026-07 | Facts only (limits, thresholds), feature ideas | n/a |

**The key finding.** Both run the **original PC executable**, patched in memory or on disk, with a rewritten
script pak. That is the opposite of what we do (reimplement from the Xbox 360 disc) and nothing of it can enter
the repo: FastGH3 is AGPL (copyleft, incompatible with PolyForm Noncommercial), GH3+ has no licence, and both
redistribute or patch Activision's binary. Their `SOURCE/q/` scripts are **decompiled GH3 QB scripts**, edited:
game code, under our rules exactly like decompiler output. What we gain is **knowledge and ideas**: they confirm
rules we already took from our XEX (hyperspeed, practice speed), list real bugs of the retail game (some of which
our port may copy faithfully), and show which features GH3 players want most (custom songs, no-fail and FC modes,
song speed above 100 %, performance toggles).

Owner's take:

## Licensing

| Repo | Licence | Copy code? | Read for facts? |
|---|---|---|---|
| donnaken15/FastGH3 | AGPL-3.0 (`license.txt`) | **No** (copyleft would force AGPL on us) | Yes, in our own words |
| FastGH3's `SOURCE/q/` | Decompiled GH3 scripts (Activision's code) | **Never** | Only to confirm a fact we then re-find on our XEX |
| FastGH3's `game.exe`, `binkw32.dll`, `fmodex*.dll` | Proprietary (Activision, RAD, Firelight) | **Never** | No |
| donnaken15/GH3-Plus | **None** | **No** | Facts only (numbers, behaviours) |
| stb headers in GH3+ | Public domain / MIT | We have our own | - |

- Facts (a threshold, a limit, the cause of a bug) are not copyrightable: we re-find each on **our** XEX in Ghidra
  and write it with our address and [C] / [I], as `gd-game-04/docs/` already does.
- No names of these projects in our code, commits or `gd-game-04/` docs; only in this study.

Owner's take:

## 1. What they are

**FastGH3.** "Guitar Hero III without the game around it": no career, no venues by default (black background or a
picture / video), only the song engine. The launcher (`SOURCE/FastGH3/Launcher.cs`, ~5,000 lines of C#, plus a
vendored chart library) opens `.chart`, `.mid`, `.pak.xen` (GH3 / Aerosmith PC), its own `.fsp` package, or a ZIP /
RAR / 7Z holding any of these; converts the chart to GH3's note arrays and the audio (MP3 / OGG / Opus / WAV via
SoX or FFmpeg) to an FSB; caches the result; and boots `game.exe` straight into the song. `fastgh3://` links and
Google Drive / Chorus downloads play a chart from the web in one click. Settings live in an INI; a settings window
(`fastgh3 -settings`) edits them.

**GH3+.** A loader (`core`) plus DLL plugins, each patching GH3 PC at fixed x86 addresses: TapHopoChord (taps,
HOPO chords, open notes and open sustains), NoteLimitFix, SongLimitFix, WhammyFix, TiltFix, modifiers (all strums,
all taps, all doubles, mirror, gem shuffle), a speed hack, texture helper, logger, Discord presence. FastGH3 ships
these plugins.

Owner's take:

## 2. Links

| What | Link |
|---|---|
| Fandom article | https://guitarhero.fandom.com/wiki/FastGH3 |
| FastGH3 repo | https://github.com/donnaken15/FastGH3/ |
| GH3+ repo | https://github.com/donnaken15/GH3-Plus |
| Changelog (dev log) | https://donnaken15.com/fastgh3/dev.html |
| QB mods / themes | https://donnaken15.com/fastgh3/mods.html, https://donnaken15.com/fastgh3/zones.html |
| Planned features board | https://github.com/users/donnaken15/projects/3 |

Owner's take:

## 3. Knowledge for gd-game-04 (to re-find on our XEX, never copied)

| Fact (from their scripts / plugins) | Our state | Use |
|---|---|---|
| Hyperspeed scales the scroll time and the game speed by 0.88 / 0.83 / 0.78 / 0.73 / 0.68 for levels 1..5 | Already in `gd-game-04/docs/highway.md` ([C]) | Confirms our numbers. Their extended table (more levels) is an Enhancement idea, see GDALL-051 |
| Practice's slow speed sets every stream's pitch to `current_speedfactor x 100` (setslomo) | Already ported (`audio/mixer.h`, `natives_game.cpp`; speed clamped to 0.1..1.0) | Confirms. Above 1.0 is an Enhancement only |
| A song's note array is a flat list of `[time ms, length ms, fret mask]` triples | Known to gd04 (`formats.md`) | Confirms the layout a `.chart` / `.mid` importer must produce |
| The PC game's note arrays are fixed at **4,000** notes per array; longer charts overflow | Check our loader has no fixed cap | Custom / foreign songs (GDALL-042) can exceed it; our vectors must stay unbounded |
| Tilt Star Power on PC: held **16 frames at 60 fps (~266 ms)** past an angle | gd04 has the 360's own rule: `star_power_trigger_frames` (3) game frames [C 0x82112F28], counted per `Player::frame` call | The PC value differs (theirs is a PC patch); ours stays. Check `Player::frame` runs on the 60 Hz game tick, not per drawn frame, or the 3 frames shrink to 12 ms at 240 fps |
| Whammy "wibble" advances by a fixed **1/60 per frame** (`wibble_delta`), so at 120 fps it runs twice as fast | Check gd04 whammy pitch / visual are time-based | High-FPS correctness; the same family of bugs colabs01 lists |
| Particles' velocity and friction are applied per frame (60 fps assumed) | gd04 particles: check | High-FPS correctness |
| Whammy line width was tuned for 1280 x 720 and looks thinner at higher resolutions | gd04 highway: check | 1080p / 4K look |
| Battle scripts that count frames | gd04 battle (GD04-120 just overhauled) | Check attacks / timers are in seconds |
| Retail bugs fixed by them: held sustains cut off when unpausing; calibration screen slowing audio; a leftover Star Power effect; the highway drifting off-centre after many hits / misses; the face-off "newspaper" winner picked by notes hit, not score; Star Power phrase not cleared after many misses at hyperspeed > 5 | Unknown in gd04 | Each one: reproduce on our port; if the original does it, keep it faithful and offer the fix as an Enhanced toggle (our "switch per fix" rule from colabs01) |
| `.chart` HOPO threshold = chart resolution / 3 ticks; a `song.ini` `hopo_frequency` override; forced / flipped HOPO and tap markers | n/a | Rules for a `.chart` reader (see suggestions) |
| GH3's own debug menu (show measures, CPU / GPU time, camera names, change venue / guitar) exists in the scripts | GD04-074 mentions it | Already known; a Developer option could expose it |

Owner's take:

## 4. Feature ideas for Enhanced menus and the launcher

| Idea | Seen in | Maps to |
|---|---|---|
| **Play custom songs**: `.chart` / `.mid` + audio folders (the Clone Hero format), archives, one-click links | FastGH3 | GDALL-042 (song sharing / additive menu): a `gd::core::songs` reader for the Clone Hero folder format would serve every game |
| Song speed **above** 100 % (practice and quickplay), with pitch kept | FastGH3, GHWT:DE (20-200 %) | gd04 practice (clamped at 1.0 today); every game's Enhanced menu |
| Extended hyperspeed scale (more than 5 levels) | FastGH3 | GDALL-051 (hyperspeed in GH1, kept across games) |
| Modifiers: all strums, all HOPOs, all taps, mirror (lefty-style flip of colours), colour shuffle, double notes | GH3+ / FastGH3 | GDALL-048 (extra cheats) |
| **No-fail** and **FC mode** (fail on the first miss), precision mode, large gems, hide gems once hit | FastGH3 | GDALL-048 |
| Autoplay with Star Power deployed by the bot; bot co-op | FastGH3 | We have autoplay in tests; a player-facing "bot" toggle |
| Practice: A-B loop, section loop, Select to restart | FastGH3 | gd04 practice |
| Toggle the highway / HUD intro, hide the HUD, no highway shake | FastGH3 | Enhanced (Remove All Effects already covers some) |
| Lefty flip without leaving the song | FastGH3 | Pause menu |
| **Performance:** particles all / minimal / none, no venue (black or picture background), no band models | FastGH3 | GD04-121 follow-ups and GDALL-044 (potato mode) |
| Input viewer and live stats overlay; FPS counter | FastGH3 QB mods | Universal overlay (gd-engine-core) |
| Taps, HOPO chords, open notes and open sustains for custom charts | GH3+ | Only for foreign / custom songs (GDALL-042), never changing GH3's own charts |

Owner's take:

## 5. Things to avoid

- Any file from either repo: AGPL code, unlicensed code, decompiled scripts, Activision / RAD / FMOD binaries.
- Their approach (patch the PC exe, rebuild `qb.pak`): ours reads the user's Xbox 360 disc and reimplements.
- Copying their **fixes as defaults**: our rule is to match the original; a fix is an Enhanced toggle, off by default,
  unless the owner decides otherwise.
- Their web download features (Google Drive, Chorus): content the player does not own; out of scope.

Owner's take:

## 6. Does it explain any open bug?

- **GD04-121 (low FPS)**: no direct cause, but their list of frame-counted logic (whammy wibble, particles, battle
  scripts, tilt) is a ready checklist for the high-FPS behaviour GD04-118 made reachable (Max FPS above 60 now
  applies). Worth a `--venuetest`-style 60 vs 240 fps comparison of whammy and particles.
- **GD04-120 (battle)**: their "FPS-based battle scripts" note says the original's battle timers count frames:
  check our battle at 120+ fps.

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent h) | Value |
|---|---|---|---|---|---|
| NEW-1 | gd04 | Bugfix | High-FPS audit of frame-counted logic: whammy wibble (1/60 a frame), particle velocity / friction, tilt threshold (16 frames), battle timers; measured 60 vs 240 fps, same result expected | 2-3 | High |
| NEW-2 | gd04 | Original | Check the retail bugs they list on our port (sustain cut on unpause, highway off-centre after many misses, face-off newspaper winner by notes hit, Star Power phrase stuck at hyperspeed > 5, leftover Star Power effect); keep the faithful ones and log each with [C] | 2 | Medium |
| NEW-3 | gd04 | Enhancement | One Enhanced toggle per retail-bug fix found in NEW-2, off by default | 1-2 | Medium |
| NEW-4 | gd04 (then all) | Enhancement | Song speed 50-200 % in practice and quickplay (time-stretched, pitch kept), scores not saved above / below 100 % | 2-3 | High |
| NEW-5 | all | Enhancement | Modifiers (all strums / HOPOs / taps, mirror, colour shuffle, double notes) and no-fail / FC mode, folded into GDALL-048 | 3-4 | Medium |
| NEW-6 | gd-engine-core | Enhancement | Clone Hero folder reader (`notes.chart` / `notes.mid` + `song.ini` + OGG / Opus stems) feeding `gd::core::songs` (GDALL-042), with the `.chart` HOPO / tap / forced rules; the player's own charts only | 6-8 | High |
| NEW-7 | gd04 | Enhancement | Particles all / minimal / none and "no venue" background, as GD04-121 follow-ups (and a step towards GDALL-044) | 1-2 | Medium |
| NEW-8 | gd04 | Enhancement | Practice A-B loop and Select-to-restart | 1-2 | Low |

Owner's take:
