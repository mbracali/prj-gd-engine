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

## 2026-10-29 00:40: M1 nearly done, M2 and M3 started

Done since M0:
- Every pak type but the textures decodes in Python and C++ (`--formattest`):
  QB, scenes / models / skins / Havok collision (agent, verified), animations,
  skeletons, fonts, .wav (FMOD XMA RIFF), .nqb, .pfx, .rag, .table, .dbg
  (agent, verified). Textures: agent still working.
- Song audio: FSB3 banks decrypted (key from the script SongLoadFSB), XMA1
  stems decoded through FFmpeg, streaming song source.
- Song list (89 songs) and charts (664 part/difficulty charts) load.
- The C-function map (1,392 natives, `ghidra/analysis/cfuncs.tsv`), the HOPO
  tolerance rule (0x82107590), the tuning globals (FINDINGS.md).
- QScript: tokenizer (Python + C++), the VM's first cut (expressions, params,
  if/elseif/else, loops, switch, spawn, waits), core natives;
  `guitar_startup` runs to its end against 75 still-unwritten natives.

Verified (fresh binary, `$ISO` = the disc image):
- `gd04 --formattest "$ISO"` -> `formattest: all files parse` (decoded: .dbg
  1356, .fnt 16, .hkc 24, .mdl 11, .nqb 24, .pfx 24, .qb 2731, .rag 27, .scn 99,
  .ska 18747, .ske 35, .skin 285, .table 12, .wav 227; not decoded yet: .img
  712, .tex 396)
- `gd04 --sfxtest "$ISO"` -> `banks: 92, samples decoded: 718, 18.2 hours of
  audio`, `sfxtest: every sound decodes`
- `gd04 --selftest "$ISO"` -> `songs: 89`, `charts: 664, notes: 416881`,
  `selftest: data checks passed (no gameplay rules yet)`
- `gd04 --scripttest "$ISO"` -> `scripts: 16552 tokenized`, `scripttest: every
  script tokenizes; boot ran (75 natives to write)`
- `python3 -I gd-game-04/tools/scn.py check gd-game-04/extracted` -> `scn 99,
  mdl 11, skin 285, hkc 24, col 0, with problems: 0`
- `python3 -I gd-game-04/tools/anim.py check gd-game-04/extracted` -> `files:
  19114, with problems: 0`
- `python3 -I gd-game-04/tools/fsb.py check gd-game-04/extracted` -> `banks:
  92, samples: 718, with problems: 0`

Not yet: the gameplay rules (agent researching the input logic in the XEX),
"all charts fully hit", Windows / Android builds of this code.
Next: textures (M1 gate), then the rules and the autoplayed self-test (M2).

## 2026-10-29 01:45: M1 gate passed; M2 core done; songs play

Verified (fresh binary each time):
- `gd04 --formattest "$ISO"` -> every pak type decoded (.img 712, .tex 396
  included), `formattest: all files parse`. `python3 -I tools/img.py check
  extracted` -> `files: 1117, textures: 5747, with problems: 0`.
- `gd04 --selftest "$ISO"` -> `charts: 664, notes: 416881, ... HOPO notes:
  50176`, `autoplay: score (strummed) 116922761, HOPO landings 50171, phrases
  captured 11466, charts failing 0`, **`selftest: all charts fully hit`**
  (every song, part and difficulty; strummed, and with every legal HOPO
  tapped).
- `SDL_VIDEO_DRIVER=offscreen SDL_AUDIO_DRIVER=dummy gd04 --song slowride
  --autoplay "$ISO"` -> `song: complete, score 201310, 551 / 551 notes hit`
  (real time, the audio clock, the debug highway).
- Windows (MinGW): 0 warnings; under Proton's wine `--selftest` (data part)
  and `--sfxtest` (8 samples) pass. Android: APK builds (not run on a device).
- GL and GLES: 708 textures of global.pak upload.

Approximate / missing (labelled in code and backlog): the debug highway (M4
brings GH3's own), the song start order, co-op / face-off / battle / boss
rules, the Star Power measure length (4 beats [I]), the input offset (0 on
PC [I]), menus (the VM runs the boot script; 1,233 natives missing).
Next: GH3's 2D highway and HUD (agent researching the sprites), then the
screen-element natives for menus (M3). Estimate left: ~20 h.
