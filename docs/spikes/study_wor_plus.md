# Study: WoR-Plus (for gd-game-17, Guitar Hero: Warriors of Rock)

Study, 2026-11-13 (backlog GDALL-052). Read-only: the repository was cloned shallowly, its text files read, and
deleted; nothing was built, run or imported. Every section ends with an empty **Owner's take:** line for you.

## Executive summary

| Project | Who | Licence | What it is | Platforms | Status | We take | Make it ours (1-10) |
|---|---|---|---|---|---|---|---|
| WoR+ (WoR-Plus) | kernaltrap8 (formerly "WoRmod") | **None** | A mod of Warriors of Rock **PS3**: edited, recompiled QB scripts (`qb.pak.ps3`), a settings menu written in Q script, byte patches to the PS3 `EBOOT` | PS3 (console with custom firmware, or RPCS3) | Small, last commit 2024-12; TODO list open | **Ideas only** (the feature list); a few general engine facts | n/a |

**The key finding.** WoR+ is a script-level mod of the PS3 build. The repository holds the game's own
`EBOOT.BIN`, its decompiled script paks (`src/gh6/*.pak.ps3`, thousands of `.ps3` files), disc assets, a PS3
license key file and a link to a shared folder of every GH DLC: content we must never touch. Its own code is six
small Q-script files (~1,800 lines: the WoR+ menu and its settings) and no licence. **Our gd-game-17 kickoff rule
already applies**: a repo with no licence is not used, "not even as a lead". So the study keeps **the feature ideas**
(which need no code) and, in section 3, three general facts seen while reading, recorded only so you can decide;
until you say otherwise, gd-game-17 agents do not use them (they would find them on our XEX in wave 01 anyway).
Saving on the port: about 0 %; value: Enhanced menu ideas for gd-game-17 and its family.

Owner's take:

## Licensing

| Part | Licence | Copy? | Use |
|---|---|---|---|
| WoR+'s own Q scripts (`src/worplus_*.q`) | **None** (all rights reserved) | **No** | Not used |
| `base/WoRmod/EBOOT.BIN`, `src/gh6/*.pak.ps3`, `assets/disc`, `assets/klic` | Activision's game, keys | **Never** (and never opened) | - |
| `assets/dlc` (a link to every GH game's DLC) | Piracy | **Never** | - |
| Submodule `JamesIsWack/gh6_burn` (not fetched) | Unknown | No | - |

Per `docs/spikes/GD-GAME-17_KICKOFF.md` §2: no names of this project in our code, commits or `gd-game-17/` docs.

Owner's take:

## 1. What it is and how it works

The author edits the PS3 game's QB scripts as text (decompiled with community tools), recompiles them into
`qb.pak.ps3`, and patches a few bytes of the `EBOOT` (a no-CD check, one float). A new "WoR+ Settings" screen
under Options registers its own UI states and toggles global flags that the game's scripts read: highway, gems,
FX, post-processing and misc settings. Install: copy the paks over the game's on a CFW PS3 or RPCS3.

Owner's take:

## 2. Links

| What | Link |
|---|---|
| Repository | https://github.com/kernaltrap8/WoR-Plus |
| QB tools the author credits | https://github.com/chc/qb-tools |
| Submodule (not opened) | https://github.com/JamesIsWack/gh6_burn |
| Discord | https://discord.gg/Pdm7KJGMjV |

Owner's take:

## 3. Knowledge for gd-game-17 (general, to be found on our XEX in any case)

For your decision only (see the key finding): not a lead for gd-game-17 agents unless you allow it.

| Fact | Use |
|---|---|
| WoR ships a **list of scripts the engine compiled to native code** ("q to cpp": highway scale / rails / string angles, whammy angles, the drummer and auto-vocal (lip) controllers, crowd animation, song list queries, Quickplay award points, quest chapter unlocks). The QB versions stay in the pak | Our QScript VM (copied from gd04) can run the QB bodies; where the XEX's native version differs, the XEX wins. Look for this list in our pak in wave 01 and mark each script [C] when read |
| The PS3 build updates the **highway at 30 Hz** (a 30.0 / one-thirtieth constant pair; their "highway FPS patch" sets it to 60 and says it fixes felt latency) | On our 360 XEX, find the highway / input update rate before porting; our engine ticks at 60 Hz or the original's rate, never ties judging to the render rate |
| The retail debug menu is gated by a global cheat flag (`enable_button_cheats`) that the no-CD check forces off | Same pattern as gd04's developer debug menu (GD04-074): a Developer option can expose it |

Owner's take:

## 4. Feature ideas for Enhanced menus (no code needed)

| Idea | Maps to |
|---|---|
| Drums chart on a guitar / pad, guitar chart on a pad (any part on any controller) | gd17 / gd12 / gd10 instrument select; GAMEPADS |
| **Negative** audio / video calibration values | All games' calibration (check our ranges allow below 0) |
| Fast boot (skip intro movies) | Already a launcher / Enhanced option in several games: make it universal |
| No post-processing except AO and tone mapping ("clean look") | gd17 per-effect FX switches (colabs01 lists the same) |
| Black highway, gem / FX profiles from other GH games, gamertag on the HUD | Highway themes when the user owns the other disc (GDALL-041 model) |
| No vocalist gender lock (any character sings any song) | gd17 / gd12 / gd10 band select |
| Remove the main-menu Party Play timer (attract mode) | gd17 menus option |
| All Quest powers in Quickplay+; switch instrument from the pause menu | gd17 later waves |

Owner's take:

## 5. Things to avoid

- The repository itself beyond its README (game binaries, decompiled scripts, keys, DLC links).
- Their open bugs are theirs (Star Power maths in Quickplay+ powers, leaderboard rank 1, section list scrolling):
  do not assume the original has them; if we meet them, check on our XEX.

Owner's take:

## 6. Does it explain any open bug?

No: gd-game-17 has no code yet (GD17-001 kickoff). The 30 Hz highway fact is worth a line in the kickoff's
"frame-counted logic" checklist.

Owner's take:

## Suggested backlog items

| ID | Game | Class | Description | Effort (agent h) | Value |
|---|---|---|---|---|---|
| NEW-1 | gd17 | Original | Wave 01 (only if you allow section 3 as a lead): find WoR's natively compiled script list and the highway / input update rate in our XEX; mark the scripts [C] and add the rate to the kickoff's frame-counted checklist | 1 | Medium |
| NEW-2 | all | Enhancement | Negative calibration values allowed in every game's audio / video calibration | 1 | Medium |
| NEW-3 | gd17 (and gd10 / gd12) | Enhancement | "Any part on any controller" (drums on a guitar or pad, guitar on a pad) and no vocalist gender lock | 3 | Medium |
| NEW-4 | gd17 | Enhancement | "Clean look": post-processing off except the original's AO and tone mapping, per-effect switches | 1-2 | Low |

Owner's take:
