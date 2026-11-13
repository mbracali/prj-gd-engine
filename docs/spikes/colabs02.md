# Colabs 02: what ten fan projects can give us

Study, 2026-11-13 (backlog GDALL-052). Read-only: repos were cloned, read and deleted; nothing was built, run or
imported, and no issue or PR was opened on their repos (several of them reject agent-made contributions). One study
per project, linked below; each ends with its own suggested items and an empty **Owner's take:** line.

## Executive summary

| Project | Study | Licence | What it is | What we take | Games it helps |
|---|---|---|---|---|---|
| Guitar Hero II Deluxe (360 + Unified) | [study_gh2dx.md](study_gh2dx.md) | None (Harmonix data, patched XEX) | Script mod running GH2's own executable (modded 360 / Xenia) | Speed rules (track speed x difficulty, song speed and the hit window), gold stars = 100 % notes, many option ideas, black venue | gd02, gd03, gd01 |
| Guitar Hero Encore Xbox 360 port | [study_gh_encore_xbox.md](study_gh_encore_xbox.md) | None | GH80s content converted onto GH2's 360 engine | A checklist of what had to change (faces broke): the Encore wave of song sharing | gd02, gd03 (GDALL-042) |
| FastGH3 + GH3-Plus | [study_fastgh3.md](study_fastgh3.md) | AGPL-3.0 (ships GH3's exe and scripts); GH3+ none | GH3 PC patched, with a chart converter | Confirms our hyperspeed / practice rules; a high-FPS checklist; retail GH3 bugs to check; song speed, modifiers, a Clone Hero reader | gd04, all |
| GHWT: Definitive Edition | [study_ghwt_de.md](study_ghwt_de.md) | None (the gitgud repo is ripped assets) | Closed mod of World Tour PC | Its wiki's formats (pak, notes, camera / crowd / light-show lists), a controller table, launch-into-song | gd10, gd07, gd-engine-core |
| WoR-Plus | [study_wor_plus.md](study_wor_plus.md) | None (ships the EBOOT, keys, a piracy link) | Script mod of Warriors of Rock (PS3) | Ideas only: negative calibration, any part on any controller, clean look | gd17, all |
| Rock Band 2 Deluxe | [study_rb2dx.md](study_rb2dx.md) | None | Script mod (360, PS3 / RPCS3) | Shake and hair tuned for 24 FPS, PostProc values for grain / effects off, RB2's needs for RB1 songs | gd05, gd08 |
| Rock Band 3 Deluxe (+ DarkRTA/rb3) | [study_rb3dx.md](study_rb3dx.md) | None; tools GPL / MIT / BSD | The largest RB mod (360 + RB3Enhanced, PS3) | **The kick bounce of the drum highway (GD05-096)**, PostProcs with their own frame rate, intro clock (matches D98), translation layout | gd05, gd18 |
| Rock Band 4 Deluxe | [study_rb4dx.md](study_rb4dx.md) | None; LibForge LGPL | GoldHEN plugin (PS4 / ShadPS4) | Forge's rules in DTA, LibForge's format docs, HUD ideas | gd19, all |
| Milo script library | [study_milo_script_library.md](study_milo_script_library.md) | None, and it is game content | Decoded Harmonix scripts of ~35 games | A private reference corpus: gd05 lacks six builtins (arm flags), coverage checks, RB2 / RB3 sizing | gd05, gd08, gd18, all DTA games |
| Arson | [study_arson.md](study_arson.md) | LGPL-3.0-or-later | Independent DTA language implementation (Rust, many tests) | Our binary reader matches it; our text parser and / rules need work | gd01, gd02, gd03, gd05 |

**The key finding.** None of these projects can give us code: the mods have no licence and carry Harmonix or
Neversoft data, FastGH3 is AGPL and ships GH3's own executable, and the tools are GPL / LGPL. What they give is
**knowledge** (engine rules, formats, original-game bugs) to re-find on our own discs and write in our own words,
and **a long list of option ideas** that players of these games already know and expect.

**Act on now:**
- **GD02-167:** gd02 / gd03 / gd05 still keep the sign in the script's `mod`: the same store and tutorial lines that
  gave gd01 its `_shop_desc` bug (#100) are in GH2's and GH80s' career scripts and RB1's tutorials.
- **GD05-100:** RB's drum highway bounces on each kick (kick_drummer / kick_neighbor triggers): one of the highway
  effects the owner says are missing.
- **GD05-101:** six script builtins missing in gd05; the bitwise ones set the characters' arm flags (band hands).
- **GD02-168:** autoplay / no-fail runs should not write records or gold stars (#104).

Owner's take:

## Licensing

| Project | Copy code? | Use |
|---|---|---|
| GH2DX, Encore Xbox, RB2DX, RB3DX, RB4DX, GHWT:DE, WoR-Plus | **No** (no licence, game data inside) | Facts re-found on our discs; ideas |
| milo-script-library | **No** (game content) | Local reference only, never committed |
| FastGH3 | **No** (AGPL-3.0, ships GH3's files) | Facts, ideas |
| Arson, LibForge | **No** (LGPL) | Read their docs and tests as guidance |
| Mackiloha (MIT), dtab (BSD-3) | Possible with their notice, not needed | Format knowledge |
| DarkRTA/rb3 decompilation | **Owner's decision** (Harmonix code re-derived) | Left closed until decided (GD18-002) |

Owner's take:

## Backlog items added

Rows tagged "Colabs 02" in [../tasks/gd-backlog.md](../tasks/gd-backlog.md): GDALL-052 (this study), GDALL-053 song
speed, 054 track speed, 055 modifiers and cheats, 056 HUD readouts, 057 black venue (Potato mode), 058 negative
calibration, 059 translations layout, 060 Clone Hero reader, 061 script rules (/ and mod, text parser), 062 script
coverage; GD02-167 mod, GD02-168 autoplay records; GD04-122 high-FPS audit, GD04-123 retail GH3 bugs; GD05-100 kick
bounce, 101 builtins, 102 24 Hz shake / hair and PostProc rates, 103 effect switches; GD10-061 format checks;
GDUL-029 launch into a song; GDCORE-013 controller table; GD08-002, GD18-002, GD19-002 discovery notes. The other
ideas stay in each study's table for later.

Owner's take:
