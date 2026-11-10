# gd-game-05 progress log

Rock Band (Xbox 360) -> `gd05`. Kickoff: `docs/spikes/GD-GAME-05_KICKOFF.md`
(wave 01, branch `feature/20261109-rockband1-wave01`). Newest entries at the
bottom. "Verified" lines quote the command and its output; "built only" and
"not tested" are said explicitly.

## Final report, wave 02 (2026-11-09, about 5.2 h of agent time, 18:05 -> 23:15)

Completeness from the ledger (docs/parity.md, `python3 tools/parity.py
docs/parity.md`): **60.00%** (wave start 21.0%). By area:
- Front end 8.0 / 14
- Port 1.0 / 2
- Gameplay 16.25 / 22
- Band 9.0 / 10
- HUD 4.25 / 10
- Stage 7.0 / 14
- Modes 9.0 / 18
- Custom 3.0 / 6
- Saves 1.5 / 3
- Video 1.0 / 1

game-info.yaml completeness 0.6, gd05 0.0.2 (CHANGELOG).

Verified by running (Linux, this build; one process at a time, scratch
XDG, dummy audio, RB guitar ignored):
- The gate: every walk OK.
  - quickplay, quickplay_drums, quickplay_vocals, band_quickplay
    (rerun OK after the venue band-name label fix);
  - solo_tour, band_tour, score_duel, tug_of_war;
  - rock_shop, creator, credits;
  - rb_lose (retry), rb_lose_quit (QUIT to the main menu);
  - tutorials (guitar; drums and vocals with GD05_INSTRUMENT: voice-overs play);
  - rb_practice (section 3..4 at 90%: the clock starts a measure before).
- `--selftest` (all charts fully hit), `--scenetest`, `--scripttest`,
  `--videotest`, `--sfxtest`: all exit 0.
- Overdrive's deploy and depleted cues play (quickplay with
  GD05_AUTODEPLOY, logged).
- The camera picks follow the RB rules (GD05_SHOTLOG). The shots' band
  targets pose and place the band (screenshots in small_club_09). The
  crowd shows in arena_07's far shot. The created character is on stage
  in its gigs (log).
- Packages: `gd-engine-launcher/scripts/build.sh all` exit 0. Every bundle
  is `complete (5 game module(s) with their libraries)`: linux, steamdeck,
  windows, android-arm64.
- Linux launcher: `--play gd05 --iso ... --exit-after` with
  GDUL_GAME_QUIT_AFTER=25000 -> `gd05 0.0.2 started`,
  `back from gd-game-05: exit code 0 after 25.1 s`.
- Wine (Proton Experimental, own prefix), same command -> `back from
  gd-game-05: exit code 0 after 25.1 s`, 99 songs.

Built only (not run by me):
- The real microphone path (the null device under the dummy driver
  stands in), Android's RECORD_AUDIO prompt, the RB drum kit as a device,
  2 to 4 real pads.
- Steam Deck and Android packages.

Approximate:
- RB's HUD is the port's simple one (stand-in).
- The help bar does not show (D12).
- Some venues are too bright.
- Small clubs' crowd cards lie flat.
- Main menu: the first letter of each button word is dark.
- The song list cloth is flat (GD05-022) and its rows crowd together.
- The difficulty screen's labels overlap.
- Creator: no 3D preview, physique or colours.
- The band lobby is a stand-in (D10).
- Tutorials: finishing a lesson by playing is unchecked.
- Camera: no intro chains or BFTB shots.

Missing:
- The Drum Trainer (not reached).
- A/V calibration's own screens wired to the judge.
- Leaderboards, online and DLC (GD05-013, out of scope).

## For the owner to play (wave 02)
- Solo > Quickplay > a song with the RB guitar: frets, strum, tilt or
  Select deploys Overdrive (listen for the deploy sound), solos show their %.
- GAMEPADS: Player 2 Instrument Drums with the RB drum kit; play a song on
  drums; hit the fill's crash with Overdrive ready.
- The USB mic: Player Vocals in GAMEPADS (or Vocals on the difficulty
  screen); sing a song and watch the pitch arrow; tap the mic in tambourine
  sections; shout in a freestyle section to deploy.
- Android: first start asks for the microphone.
- Multiplayer > Band > Quickplay with 2 to 4 pads; let one player fail and
  save them with Overdrive.
- Multiplayer > Head to Head: Score Duel and Tug of War.
- Solo Tour and Band World Tour: create a character (name, hair, head),
  play a gig, check your character is on stage, buy clothes in the Rock
  Shop, quit and restart (the save).
- Training: the tutorials for guitar, drums and vocals (do the lessons
  finish when played?), Practice by section and speed.
- Fail a song: Retry, New Song, Practice, Quit.
- Options > Credits; Options > Enhanced Options.
- Look: are venues too bright? Does the band look right in closeups?

## Final report, wave 01 (2026-11-09, about 4.3 h of agent time)

Works (verified on Linux, fresh builds):
- Solo Quickplay from RB's own menus: intro movie, title, main menu, Solo,
  Quickplay, song list, difficulty (RB's player slots), loading, the song
  in a venue, results, back to the song list; pause / resume.
  `gd05 "$ISO" --walktest quickplay` -> `walktest quickplay: OK`;
  `--walktest keys` (Enter / arrows / Esc through SDL events) -> `OK`.
- Guitar, bass, drums on RB's highway (gems, colours, beat lines, now bar,
  tails). Drums via GH2's joypad watcher (DECISIONS D6).
- Every VENUES venue loads (WorldInstance decoded), camera cuts from the
  VENUE track, lights.
- `--selftest` exit 0 (`all charts fully hit`, `all input checks passed`,
  vocals through the pitch detector 2132 / 2136 Great+); `--scenetest`
  `0 typed objects failed, 0 files failed`; `--scripttest` `OK`;
  `--videotest` OK; `--sfxtest` OK (374 moggs); core
  `gdcore_audio_input_selftest` OK.
- Enhanced Options reachable from RB's options menu.
- Packages: `gd-engine-launcher/scripts/build.sh all --force` exit 0; every
  bundle `complete (5 game module(s) ...)`: linux, steamdeck, windows,
  android-arm64 (android rebuilt after a warning fix: 0 warnings).
- Linux launcher: `--play gd05 --iso ... --exit-after` with
  GDUL_GAME_QUIT_AFTER=25000 -> exit 0 (99 songs, intro movie playing).
- Wine (Proton Experimental, own prefix): same -> `back from gd-game-05:
  exit code 0 after 25.1 s`, the Bink intro decoded.

Approximate [I]: venue lighting brightness (no reference), shot choice
(random per category), song list cloth (inner camera, no render-to-texture,
GD05-022), drum judging (joypad watcher), track colours swap, sustain tails
(flat strips), help bar, debug text HUD instead of RB's HUD.

Missing: band on stage (M6), vocals in game (D7: guitar instead), RB's HUD,
Overdrive / solo / fill / BRE looks, tours / practice / training /
multiplayer / Rock Shop / saves (M7), drum kit and mic recognition, 2-4
players, `--uitest` (42 component names open, docs/audit.md), PC wording.

Confidence per OS: Linux 🟢 (walks + launcher run), Windows 🟡 (Wine run
25 s, no walk), Steam Deck 🟡 (built and verified, not run), Android 🔴
(built and verified, not run), macOS 🔴 (not built here).

## For the owner to play (wave 01)
- Launcher > Rock Band > Solo > Quickplay > a song > Guitar > a difficulty;
  play with the keyboard, a gamepad or the RB guitar; Esc pauses.
- The bass (choose Bass on the difficulty screen) and the drums
  (`--part drum`, an RB drum kit as a gamepad: LB kick, B Y X A pads).
- Does the RB guitar's fret / strum map feel right? Are the venues too
  bright or dark against the real game?
- Options > Enhanced Options.

## Overall estimate

Written at the start (2026-11-09 13:51), from the measured times of
gd-game-03 (gd02 copied, ~4 h to its final report) and gd-game-04 (new
engine, M0..M9 in ~6.5 h). Rock Band reuses GH2's engine family (ARK v3 and
the dtb cipher read as is by gd-game-02's tools on the first look) but is
much wider: four instruments, four players, vocals with a new microphone
interface in gd-engine-core, Bink, the character creator and the Rock Shop.

| Milestone | Estimate |
|---|---|
| M0 set-up (copy, rename, rip, Ghidra) | 0.75 h |
| M1 data layer (RB Milo versions, vocals files, Bink) | 1.5 h |
| M2 songs, audio, gameplay core (4 parts, Overdrive, band rules) | 2.5 h |
| M3 scripts, UI, audit | 2 h |
| M4 highways and HUD (4 tracks incl. vocals) | 2 h |
| M5 venues | 1.5 h |
| M6 band and creator | 2 h |
| M7 modes, shop, saves | 1.5 h |
| M8 port extras (4 players, drums, vocals in GAMEPADS; mic) | 1 h |
| M9 launcher module, packages | 1 h |
| M10 gaps | at most 3 h (this run's limit) |

Total M0..M9: about 12-14 h of agent time. Parity with every rule
re-confirmed in RB's XEX is longer: owner waves follow, as gd02..04 needed.

## For the owner to play

Real-device checks only the owner can make (kept up to date):
- (filled in as features land)

## 2026-11-09 14:05, M0 set-up (gate passed)

Done:
- gd-game-02's engine, tools and build scaffolding copied and renamed
  (DECISIONS D1); version 0.0.1 (`engine/CMakeLists.txt`, `game-info.yaml`
  with `iso_signatures` title ID 45410829); FFmpeg's decoder list now Bink +
  XMA (D2); README, CHANGELOG, agent-instructions, CONTEXT.md, DECISIONS,
  OPEN_QUESTIONS; backlog rows GD05-002..013, GDCORE-011.
- Disc ripped (`extracted/disc`, 5.2 GB), archive unpacked (4,588 files),
  all 291 `.dtb` decoded to text (0 failed).
- Ghidra: `default.xex` imported (XEXLoaderWV), FixXexFunctions (41,175
  functions), exported (41,175 decompiled, 13,449 strings, 77,980 data refs).
- First M1 finding: RB's milo payload is big-endian (docs/formats.md); the
  engine and `milo.py` read the order per file.

Verified:
- `ninja -C engine/build/linux` -> 0 warnings, 0 errors (grep of the log).
- `engine/build_windows.sh` -> `Ready: dist/gd05-windows-x64`, 0 warnings
  (before the byte-order change; rebuilt at the M1 gate).
- `GD05_LOG=... timeout 8 engine/build/linux/gd05 "$ISO"` ->
  `archive: gen/main.hdr version 3, 4588 files in 2 parts (disc image ...)`
  then `game script is missing 'head'` (RB's game.dta: M2/M3 work).
- `python3 -I tools/milo.py scan extracted/ark` -> `3445 files parsed, 6 failed, 461 inline sub-directories`.
- `gd05 "$ISO" --scenetest` -> `3406 milo files parsed, 45 failed` (class
  versions: M1).
- `python3 .github/scripts/repo_guard.py` -> `0 problem(s)`.
- `pgrep -fa gd05` -> nothing running.

Not tested yet: Android build, local_build.sh packages (M9).

Next: M1, the data layer (class versions, vocals files, Bink). Time used:
~15 min; estimate unchanged (12-14 h for M0..M9).

## 2026-11-09 14:58, M1 data layer (gate passed); M2 started

Done (M1):
- Milo: RB's payload is big-endian per file; inline sub-directories follow
  their owner's end marker; relative paths from the source folder; every
  class on the disc has a known layout. Two research agents read the Load
  routines in the XEX and checked Python decoders over the whole disc
  (render / UI: 93,480 instances exact; characters / synth: 43,177 exact;
  both re-run by me); I ported the layouts into the C++ decoders (Load
  addresses in the code and in docs/formats.md).
- `.mogg` version 11 decrypted (AES-128 counter mode; own AES; DECISIONS D3,
  OPEN_QUESTIONS Q3). Bink in FFmpeg; `--videotest` added.
- RB's sound samples: XMA, big-endian PCM16, empty placeholders.

Verified (fresh binary, `engine/build/linux/gd05 "$ISO" --<test>`, with the
CONTEXT.md environment):
- `--scenetest` -> `3451 milo files parsed, 0 failed, 467 inline sub-directories`,
  `proxy directories: 364 resolve to an archive file, 0 do not`,
  `ui text chain: 1656 components resolve ..., 0 do not`,
  `scene test passed: 0 typed objects failed, 0 files failed` (exit 0).
- `--videotest` -> `videotest: 14 videos, 44189 frames decoded; 0 failed`, `videotest: OK`.
- `--sfxtest` -> `moggs: 374 files (374 version 11, decrypted), 416833 Ogg pages walked to each file's end, 22300 s of audio ...; 0 failed`, `sfxtest: OK` (exit 0).
- `--selftest` (M2 so far) -> `291 .dtb scripts decoded, 0 failed`;
  `PART GUITAR 308 charts, 138360 gems, 0 not fully hit`;
  `PART BASS 244 charts, 141962 gems, 0 not fully hit`;
  `PART DRUMS 584 charts, 499457 drum notes (with and without kick), 0 not fully hit`;
  `PART VOCALS 252 charts, 8648 phrases rated Awesome, 0 not fully hit`;
  `all charts fully hit` (exit 0).
- `ninja -C engine/build/linux` and `engine/build_windows.sh` -> 0 warnings.
- `python3 -I tools/milo.py scan extracted/ark` -> `3451 files parsed, 0 failed, 467 inline sub-directories`.
- `pgrep -fa gd05` -> nothing running.

Gate notes (honest):
- 10,128 objects of 19 classes are kept raw in C++ (layouts known and
  verified in Python; ported when used: WorldInstance and PropAnim at M5).
- `tools/tex.py` does not read RB's big-endian inline bitmap headers yet
  (the C++ does: every Tex decodes).
- The selftest's GH2 judge regression checks (GH2's 50-point values, co-op,
  face-off) are reported, not counted; the PC-wording check finds 38 RB
  strings that name the Xbox 360 or a console (M3); GH2's script sound
  list is informational until M3's audit.

M2 state: the four judges play every chart; the rules in FINDINGS.md are
from the data [C data] but the judges' logic for RB is [I] (guitar / bass
are GH2's judge, drums and vocals new). Band mechanics (band meter, saves,
Unison, Big Rock Endings, solo awards, stars) are not built yet.

Next: M3 (RB's front end through its own scripts), then M4 highways. Time
used: ~1 h 10 min of wall clock; the estimate stays 12-14 h for M0..M9.

## 2026-11-09 15:45, M2 core done, M3 menus reach the song (in progress)

Done since 14:58:
- gd-engine-core: the microphone capture interface `gd::core::audio::Input`
  (sdl3, miniaudio) with `gdcore_audio_input_selftest` (GDCORE-011).
- Vocals: our own pitch detector; `--selftest` sings every song's Expert
  vocals with a synthesised voice through it.
- Launcher: `build_module_05` and verify_bundle's rule; gd05's FFmpeg is
  built with the suffix `_gd05` (no clash with gd02's in the APK).
- Front end (RB's own scripts, config/band.dta): intro movie, title, main
  menu (RB's 3D buttons), Solo, Quickplay, the song list (RB song provider,
  rank tiers [I]), the difficulty screen (RB's player slots), the loading
  screens, game_screen with the song playing. Xbox profile / save flow
  skipped (DECISIONS D4).

Verified:
- `engine/build/linux/gdcore_audio_input_selftest` -> `sdl3: ... frames pushed in real time (24576 in 0.5 s)`, `miniaudio: ... (24000 in 0.5 s)`, `input selftest: OK`.
- `gd05 "$ISO" --selftest` -> `all charts fully hit`, `vocals through the pitch detector: 58 songs, 2132 of 2136 phrases rated Great or better` (exit 0).
- `gd05 "$ISO" --videotest` -> `videotest: OK` (with the `_gd05` FFmpeg).
- `gd05 "$ISO" --song greengrass --autoplay` with a screenshot at 50 s:
  the song plays on gd02's debug highway (score, x4, Overdrive ready).
- GD05_KEYS walk boot -> game_screen (screenshots in agents/scratch, not committed).

Not done / approximate: RB's track and HUD (M4: the debug highway is
used), RB's song list cloth (RenderDir drawn flat, GD05-022), label wrapping,
the band modes' slots (one local player), characters as objects (M6).

## 2026-11-09 16:50, M3 gate partly, M4 / M5 started

Done since 15:45:
- M3: RB's player slots, PreloadPanel, game_won / game_outro and
  band_ui_sink sync_screen, endgame stats kept after the song, quasiquote,
  Set objects, panel iterate / set_frame, stand-in characters, RB menu
  music list, the in-game sfx bank.
- M4: RB's highway from trackpanel.milo / tracksystem.milo (gem sets from
  track_graphics.dtb, slot-coloured gems, beat lines, smasher plate); bass
  and drums playable (`--part`, the slot's track type); drums through the
  joypad watcher (D6); vocals fall back to the guitar (D7).
- M5: RB's venues load and draw (VENUES list, WorldInstance as proxies,
  mixed light kinds); camera cuts from the VENUE track by shot category.

Verified (this hour, fresh build):
- `gd05 "$ISO" --walktest quickplay` -> `walktest quickplay: OK` (screens: intro_movie_screen > ... > game_screen > endgame_waiting_screen > qp_endgame_screen).
- `gd05 "$ISO" --scripttest` -> `scripttest: OK`.
- `gd05 "$ISO" --scenetest` -> `scene test passed: 0 typed objects failed, 0 files failed` (WorldInstance 2683 + 286 own decoded).
- `gd05 "$ISO" --selftest` (devices ignored) -> exit 0, `all charts fully hit`.
- `gd05 "$ISO" --uitest` -> FAILED: 42 script component names not in the milos (open, GD05-005).
- Screenshots (not committed): guitar and drums on RB's track, small_club_01 with cuts.

Approximate / missing: no band on stage yet (M6), RB's HUD (debug HUD
text), sustain tails, Overdrive / solo / fill looks, venue lighting
calibration (no reference), RB's director rules (random shot per category).

Next: M6 band on stage (prefab characters), then M9 launcher module and
packages, then M10 by impact. Time used: 3.0 h of the 12-14 h estimate.

## 2026-11-09 22:00, wave 02 gate 1 (ledger 50.5%)

Done since wave 01 (each in the commits of this branch):
- The parity ledger (docs/parity.md, tools/parity.py): 21.0% at the start.
- The band on stage: premade characters (characters.dtb) in the venue's
  four slots, built from their outfit pieces and instruments, CPU skinning.
- Vocals in the game: the mic (core input), GD05_VOCALS_FILE, the vocal
  track; RB's vocal scoring, scream deploy and tambourine from the XEX.
- RB's track through the menus (it was the debug highway there in wave 01).
- Band play for up to four (tracks side by side, vocals on top), RB's band
  multiplier, fail-out and saves, Unison; solos, fills, Big Rock Endings,
  stars from the XEX; RB's guitar watcher rules (HOPO chain, sustains) and
  drum watcher (a gem per pad).
- Meta game: profile JSON, Solo Tour, Band World Tour, the creator's maker,
  Band Quickplay and head-to-head through the menus (lobby stand-in, D10),
  the Rock Shop, options, lose / retry, practice's sections.
- Venue lights read as RB stores them; WorldDir material overrides.
- PC wording of RB's strings; four players in the input layer; Drums and
  Vocals instruments in GAMEPADS (built only).

Verified (fresh build, CONTEXT.md environment):
- walks: quickplay, quickplay_drums, quickplay_vocals, band_quickplay,
  solo_tour, band_tour, score_duel, tug_of_war, rock_shop, rb_lose -> OK
  (`--walktest <w>` -> `walktest <w>: OK`).
- `--scenetest` -> `scene test passed: 0 typed objects failed, 0 files failed`;
  `--scripttest` -> OK; `--videotest` -> OK; `--sfxtest` -> OK.
- `--selftest` -> exit 0: all charts fully hit; 232 of 232 solos perfect;
  176 endings banked; 292 drum charts through the game's drum judge;
  tambourine 10180 of 10180; vocals through the pitch detector 2136 of 2136;
  PC wording 0 strings left.
- screen sweep (`--walktest screens`): 235 of 252 screens reached (before
  the later natives).

Built only: GAMEPADS Drums / Vocals rows, four-player input slots, the
Android RECORD_AUDIO request (launcher APK not rebuilt yet).

Next: toward 60%: tutorials, crowd, creator appearance, camera, HUD,
animation, the help bar, the real-device checklist. Time used: about 4 h.
