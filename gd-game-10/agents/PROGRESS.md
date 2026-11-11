# gd-game-10 progress log

Guitar Hero: Metallica (Xbox 360) -> `gd10`. Kickoff:
`docs/spikes/GD-GAME-10_KICKOFF.md` (wave 01, branch `feature/spike-metal`).
Newest entries at the bottom. "Verified" lines quote the command and its
output; "built only" and "not tested" are said explicitly.

Estimate (kickoff 2): 4-6 h for the proof of concept (M0..M4 on guitar);
stop at about 6 h whatever the milestone.

## 2026-11-10 23:40 M0 set-up

- gd04's engine, tools and build scripts copied, renamed gd10 (D1). Version
  0.0.0. Built: `ninja -C gd-game-10/engine/build/dev`: 0 warnings (one
  GCC 16 false positive in `port_gamepads.cpp` silenced by masking the index).
- Rip: `xdvdfs.py extract` -> "extracted 1470 files, 7.22 GB".
- Ghidra: import + `FixXexFunctions` + `ExportAnalysisXex` -> "Exported 30337
  functions, 30337 decompiled, 94894 data refs, 9246 strings".
- Verified: `pak.py check gd-game-10/extracted` -> "paks: 1060, files:
  33406, with problems: 0" (after the CHNK reader and the padded-pak rule).
- Verified: `qb.py check gd-game-10/extracted` -> "qb files: 1544, sections:
  38466, scripts: 8853, with problems: 0" (after the gh4 struct items and
  type 0x1C).
- Verified: headless run on the ISO boots, loads the disc and logs what
  fails: "Guitar Hero: Metallica disc: 0 songs", fonts "glyph table
  offset", "streamall.fsb.xen: not an FSB3 bank", "guitar_startup did not
  start". `--formattest`: 18,202 failures by reason (formats.md "Not
  decoded yet").
