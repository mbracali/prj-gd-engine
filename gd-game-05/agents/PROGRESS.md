# gd-game-05 progress log

Rock Band (Xbox 360) -> `gd05`. Kickoff: `docs/spikes/GD-GAME-05_KICKOFF.md`
(wave 01, branch `feature/20261109-rockband1-wave01`). Newest entries at the
bottom. "Verified" lines quote the command and its output; "built only" and
"not tested" are said explicitly.

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
