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
