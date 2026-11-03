# Parity gaps: what is missing for a 100% match (2026-10-29)

Only original behaviour is listed, no Enhanced items. "Unverified" means the port has the feature, but nothing has compared it with the original; the owner dropped emulator comparisons on 2026-10-31 (GD02-064, GD03-018, GD04-027). IDs are rows in `gd-backlog.md`.

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

Updated 2026-11-03 (0.1.12): every gap listed here before is closed (GD02-036,
-037/-152, -041, -049/-091/-151, -050, -075/-082/-153; GD02-060/-061/-062 marked
done for the owner's tests; GD02-066 dropped). What stays inferred, with no
behaviour left to change from code or data:

| Area | Inferred [I] | Backlog |
|---|---|---|
| Venues | The beam buffer's effective gain (kept at 1: the inferred 3.2 turns beams white) | GD02-151 |
| Band | The owner of CharDriverMidi's +8 byte (behaviour settled by the fret IK's test) | GD02-050 |
| Gameplay | The joypad poll running before the game poll in a frame (press time = the last game poll) | GD02-152 |

## gd-game-03: Guitar Hero Encore: Rocks the 80s (PS2)

Updated 2026-11-03 (0.1.12): every gap listed here before is closed. The
unvisited screens are walked (GD03-092: the final-song choice, the stats
screens, the options extras, the controller pause, the store guitars; their
fixes in GD03-092 / GD02-157..159), WorldFx follows the PS2 (GD03-100), the
Star Power tube and the camera shake were checked against the PS2 code
(GD03-099, GD03-098), Animate's unused options need nothing (GD03-104), and
the verification rows were closed by the owner (GD03-029, -030, -105, -106).
What stays inferred, with no behaviour left to change from code or data:

| Area | Inferred [I] | Backlog |
|---|---|---|
| Gameplay rules | 7 rule fixes read from the decompiler only, not re-read in assembly | GD03-029 |
| Reverb, fade | The reverb network is SPU2 hardware (no code to read); the fade's frame alpha | GD03-029 |
| Data | Field meanings from the 360 executable's old-revision branches (layouts exact to every record's end marker) | GD03-030 |
| Mono | The IOP's pan law at the centre | GD03-106 |
| Stats | Face-off's time in the lead is 0: no code on the disc writes it [C by absence] | GD03-092 |

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
