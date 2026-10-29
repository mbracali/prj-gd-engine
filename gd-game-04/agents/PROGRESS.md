# gd-game-04 progress log

**Overall estimate (start, 2026-10-28 22:30):** M0-M10 of the kickoff total
about 30 h of focused work; this session covers M0 and as much of M1-M2 as
fits. Revised per milestone below.

## 2026-10-28 23:05: M0 (set-up) done

Done:
- Disc ripped (`extracted/`, 660 files); `default.xex` title ID 415607F7
  (`game-info.yaml` `iso_signatures`).
- Ghidra project + export (`ghidra/analysis/`: 32,998 functions decompiled);
  `DumpAsm.java` / `ReadF.java` added; REA tried and cannot open XEX /
  PowerPC (`CONTEXT.md`).
- Engine skeleton copied from gd-game-02's engine-independent layers, renamed
  gd04 (DECISIONS.md D3): builds with 0 warnings on Linux.
- Data layer already in (ahead of M1): compressed files, PAK/PAB, QB keys and
  the debug names, the QB decoder, in Python and C++.

Verified (commands from the repo root, fresh binary):
- `python3 -I gd-game-04/tools/pak.py check gd-game-04/extracted` ->
  `paks: 400, files: 24726, with problems: 0`
- `python3 -I gd-game-04/tools/qb.py check gd-game-04/extracted` ->
  `qb files: 2731, sections: 46899, scripts: 16552, with problems: 0`
- `gd04 --formattest "$ISO"` -> `qb: 2731 files, 46899 sections, 16552 scripts, 0 unused bytes`,
  `formattest: all files parse` (other types listed as "not decoded" yet)
- `gd04 --shadertest` -> `all 2 shader programs compiled`; with `GD04_GLES=1` the same
- headless smoke run (offscreen, `GD04_QUIT_AFTER=1500`) -> exit 0, screenshot saved

Built only: nothing else. Not tested: Windows, Steam Deck, Android builds
(the build scripts are copied but not yet run), the launcher module.

Next: M1 (textures, scenes, models, skins, animations, fonts; their Python
tools and `--formattest` coverage; XEX addresses for the key hash, pak
reader and LZSS). Estimate left: ~28 h.
