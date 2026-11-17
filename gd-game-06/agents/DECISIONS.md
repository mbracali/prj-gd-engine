# gd-game-06: decisions

Each entry: what, why, how to undo.

## D1. Discovery uses gd-game-04's tools in place (2026-11-17)

What: the disc was extracted and checked with `gd-game-04/tools/*.py` run on
`gd-game-06/extracted` (read-only use, nothing copied into gd-game-06 yet);
only the five Ghidra scripts (FixXexFunctions, ExportAnalysisXex, DumpCFuncs,
DumpAsm, ReadF) were copied into `gd-game-06/tools/ghidra_scripts`, unchanged
except their `@category`. Why: GD06-001 writes no engine code; the copy of
the engine and tools is M1's decision (D2). Undo: none needed.

## D2. Recommended (owner to approve): copy gd-game-04's engine, no shared library (2026-11-17)

What: M1 copies gd-game-04's tracked `engine/`, `tools/`, `build_commands/`,
`local_build.sh` into gd-game-06, renamed gd04 -> gd06 / GD04 -> GD06 /
user-facing names -> Guitar Hero: Aerosmith, version 0.0.0 (as gd-game-10's
D1), **without** gd04's song-audio decryption (below), then changes only what
Aerosmith needs (the diff list).

Why:
- The XEX is GH3's with small changes (97% of functions pair by size and
  callees, every C function name kept, FORMATS.md) and every data format but
  the `.ska` keys parses with GH3's decoders, so a copy works almost at once.
- A shared "GH3 engine" library for gd04 and gd06 would mean refactoring the
  released gd04 (its tests, its packages) and touching gd-game-04, and the
  library would carry gd04's song decryption into gd06, which this port must
  not reuse. The repository's pattern is one engine per game folder
  (gd-game-10 started the same way) with the common backends in
  gd-engine-core.
- Cost: fixes in gd04's copy do not reach gd06 by themselves; the copy's
  comments keep GH3's [C] addresses, which are [I] for gd06 until re-found.

Removed from the copy: `fsbDecrypt` and every call to it (data/fsb.*,
audio/song_stream.cpp, audio/sfx.cpp, tests/sfxtest.cpp). gd06 keeps a
single entry point where song-bank bytes would be turned into playable data,
which returns "not available"; songs then play without their audio (the
chart, highway and clock still run) until the owner decides (OPEN_QUESTIONS 1).
streamall and in-pak sounds are not encrypted and play as in gd04.

Diff list (what gd06 changes in the copy, by milestone):
- M1: names / title (Guitar Hero: Aerosmith, title ID 41560819 in
  `iso_signatures`, save folder gd06), the `.ska` rotation keys
  (FORMATS.md), the chart's aux part, every GH3 address in comments marked
  for re-check.
- M2: main menu (`menu_main.qb` is new), `ui_frame`, `ui_physics`,
  `ui_exploding_text`, the 9 new C functions, movie subtitles (AO movies),
  the Xbox sign-in natives.
- M3: Quickplay's mode select (`menu_quickplay_select_mode.qb`), co-op
  quickplay, the difficulty analyser screen, song audio's entry point.
- M4: six new venues, the Aerosmith band (per-venue singer / drummer rigs,
  tempo anim events, per-member animation data), Aerosmith entrance /
  DMC intro transitions, new particles (pyro, fireworks, Tesla lightning,
  star-power bones / wings), `SetLightMapOverlayParams`.
- M5: career with opening acts, Aerosmith unlock and encores, the movies
  between tiers, Joe Perry's boss battle, Bonus songs, the store / Vault,
  achievements, no co-op career.

Undo: if the owner prefers a shared library, M1 extracts gd04's
engine-independent parts first (with gd04's tests passing) and gd06 links it.
