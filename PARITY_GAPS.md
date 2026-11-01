# Parity gaps: what is missing for a 100% match (2026-10-29)

Only original behaviour is listed, no Enhanced items. "Unverified" means the port has the feature, but nothing has compared it with the original yet, because no reference captures exist (GD02-064, GD03-018, GD04-027). IDs are rows in `gd-backlog.md`.

## gd-game-01: Guitar Hero (PS2)

| Area | Missing or wrong | Backlog |
|---|---|---|
| Characters | Walking between spots, start-of-song walks, win / lose animations, Star Power poses (the Star Power move plays only about half the time), player strum `strum_pluck_down` | GD01-060, -272, -046 |
| Star Power | Wrong meter gauge; no Star Power animation on the highway | GD01-046 |
| Menus | Strum icon split in half; career status font and overlap; hero-select layout; menu character shadows | GD01-291, -050, -061, -068, -232, -233 |
| Career | Status / win screens, Battle Axe, store purchases and multiplayer screens: check by playing | GD01-240 |
| Modes | Tutorials, practice mode, multiplayer face-off | GD01-092, -093, -279 |
| Venues | Glitches: see-through Toxic mountains, environment-map stages, additive blend / vertex alpha, a flat brown frame in one big-club shot | GD01-037, -135, -249, -251, -252, -254 |
| Lighting | Weaker than the original | GD01-196 |
| Crowd | Fewer people, no reaction to how well you play, head and outfit variety, facing and lighting | GD01-197, -246, -274 |
| Audio | SPU2 reverb approximated (Freeverb), mix levels | GD01-282 |
| Performance | Microstutters: 60 FPS not guaranteed | GD01-088 |

16 old gd-game-01 rows that were already done were marked outdated on 2026-10-29.

## gd-game-02: Guitar Hero II (Xbox 360)

| Area | Missing or wrong | Backlog |
|---|---|---|
| Menus | Button animation on screen change (does GH2 have it?); song-list font; "Beat X/5" label drawn over other labels; help bar boxes (`help_box_*`); typing letters into band names; endgame receipt lighting; live volume sliders | GD02-075, -082, -084, -085, -046 |
| Band | Walks and interactions (CharWalk, GuitarCharacter timeline: walks to the solo and win spots, holds, Star Power, interacts); full pose system | GD02-050, -051 |
| Venues | Every venue's entering animation and missing effects; GH2's beam shader | GD02-091, -049 |
| Cheats | Flaming Head particles (the cheat is accepted but nothing shows) | GD02-043 |
| Modes | Practice (sections, 4 speeds, click track, rosewood surface, pitch); tutorials (9 charts, narration); hopo and other tutorial script callbacks | GD02-056, -038, -034, -058, -087 |
| Gameplay research | Watcher bass cues; lefty flip, input sampling and controller latency; sinks after RESTART | GD02-036, -037, -086 |
| Career | About 20 Easy career songs not covered by the walk test | GD02-088 |
| Platform reports | Salvation not starting on Windows and the camera going under the venue: both not reproduced; ~20 fps on macOS M4 Pro | GD02-060, -061, -062 |
| Not in the port | Achievements, leaderboards, DLC sample package | GD02-066 |

## gd-game-03: Guitar Hero Encore: Rocks the 80s (PS2)

| Area | Missing or wrong | Backlog |
|---|---|---|
| Menus | Transitions, focus growth / re-jitter, looping anims as on the PS2 (0.0.2); the Horse Head cheat (0.0.3). Still missing: Animate's (blend) / (delay) / (wait) options (no menu uses them) | GD03-026, GD03-042, GD03-044, GD03-045 |
| Highway | Nothing known: the sustain tails swell with the whammy as the PS2 (0.0.3, GD03-091); gem sparkle / bonus spark are never shown by the PS2 either (verified, GD03-027) | GD03-091, GD03-027 |
| Gameplay rules | Nothing known open (0.0.3: crowd_rating_active as the PS2); 7 of the 10 rule fixes read from the decompiler only, not re-read in assembly | GD03-028, GD03-029 |
| Band effects | WorldFx effects (Flaming Head, hand flames, god particles) drawn from 0.0.3; their anim clock and start / stop semantics from world_objects.dta, not the PS2 WorldFx code [I] | GD03-048 |
| Audio / screens | Every scripted sound heard except two never used by the game; screens no walk visits yet (end-of-song stats, final-song choice, manage bands, PS2 options extras) | GD03-092 |
| Stage director, band, crowd, MIDI | Run, but their goldens are the port's own baseline; unverified | GD03-018 |
| Reverb, fade | Reverb network is SPU2 hardware (no code on the disc to read) [I by nature]; the fade's frame alpha and strip scaling to the picture height [I] | GD03-029 |
| Data | Class layouts exact to every record's end marker; only Mat's first fields re-read in the PS2 loader (0x001bfc40), the rest from the 360 executable's old-revision branches | GD03-030 |
| Mono | Mono is every source panned to the centre (0.0.3, [C]); the music's pan law at the centre is the IOP's (not read) [I] | GD03-031 |
| Reference | PCSX2 captures blocked (needs the owner's PS2 BIOS) | GD03-018 |

## gd-game-04: Guitar Hero III: Legends of Rock (Xbox 360)

| Area | Missing or wrong | Backlog |
|---|---|---|
| Menus | Menu animations and composite menu objects (checkpoint in progress); 678 natives the scripts call are still missing | GD04-016, GD04-030 |
| Song engine | The song runs on native code, not on GH3's gem-scroller scripts (events are now raised); the full move is open | GD04-021 |
| Highway / HUD | Approximated: strings, Star Power glow, HUD easing (curves now [C]); the battle visuals, P2's highway and Lou's finish were added but are unverified | GD04-018, -019 |
| Venues | One approximated shader for all venues; no crowd, light cones, particles or LODs | GD04-007 |
| Band | One approximated shader; Havok accessories (hair, clothes) decoded but not simulated | GD04-008, -015 |
| Modes | Career, co-op, face-off, battle, bosses and the Vault: built, unverified | GD04-009 |
| Android | In-place disc reading built but not run on a device | GD04-028 |
| Not in the port | Online, leaderboards, achievements | GD04-013 |
| Reference | Xenia captures recorded for the menus (GD04-027); gameplay, venues and the band not yet compared | GD04-027 |

## All games

| Area | Missing | Backlog |
|---|---|---|
| Verification | Side-by-side comparison with the original for every screen, venue and song (emulator captures) | GD02-064, GD03-018, GD04-027 |
| Devices | Real Windows PC, Steam Deck, Android device and macOS runs for 0.1.7 | GDALL-030 |
