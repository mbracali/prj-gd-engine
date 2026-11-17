# gd-game-06 progress log

## 2026-11-17: GD06-001 discovery (done)

- Disc extracted into `gd-game-06/extracted/` (806 files, 4.67 GB) with
  gd04's `xdvdfs.py`; name table `extracted/names.tsv` (41,234 names).
- `default.xex` imported into `gd-game-06/ghidra/gha.gpr` exactly as gd04
  (XEXLoaderWV, FixXexFunctions, DumpCFuncs, ExportAnalysisXex; 6 GB heap,
  7 minutes): 33,178 functions, all decompiled, 8,282 strings, 85,940 data
  refs, 1,401 C-function table entries in `ghidra/analysis/`.
- Compared with GH3 (gd04's rip and analysis): every format but the `.ska`
  rotation keys parses with gd04's decoders; the XEX is GH3's build
  (`GH3_xp1`) with ~97% of functions pairing by size and callees and 9 new C
  functions; content: 6 new venues, 43 songs, Aerosmith's band, a career
  with opening acts and encores, Joe Perry as the boss, no co-op career.
  Details: `docs/FORMATS.md`, `agents/CONTEXT.md` "The game".
- Recommendation: copy gd04's engine without its song decryption
  (DECISIONS D2); owner questions in OPEN_QUESTIONS.md.

Agent time: about 45 minutes (extraction, Ghidra, comparison, docs).

## Plan (milestones; estimates are agent time)

Estimates come from measured agent time on the closest precedent:
gd-game-10 went from a copy of gd04's engine to a Quickplay song played to
the results screen in about 2.7 h (11-10 23:50 -> 11-11 02:32) on a newer,
more different engine (gh4). Aerosmith is GH3's own engine, so the early
milestones should be faster; the venues, band and career are new content
and carry most of the work.

| Milestone | What | Gate | Estimate |
| --- | --- | --- | --- |
| M1 data layer | Copy gd04's engine / tools / build files as gd06 (D2, decryption removed, one song-audio entry point), names and title ID, the `.ska` keys, the chart's aux part, `--formattest` over Aerosmith's disc | `gd06 --formattest <disc>`: all files parse; pak / qb / qbscript / anim checks 0 problems | 1-1.5 h |
| M2 boot to main menu | Boot movies and legal screens, the new `menu_main`, UI frame / physics / exploding text, the 9 new C functions, sign-in natives answered offline | `--walktest boot` reaches Aerosmith's main menu, screenshot; no unhandled native in the log | 1-2 h |
| M3 one Quickplay song | Quickplay mode select, set list, difficulty, a song on the highway to the results (song audio: silent unless the owner supplies it), co-op quickplay selectable | `--walktest quickplay` plays one song to the results and back to the set list | 1-2 h |
| M4 venues / band | The six venues (scenes, lights, cameras, crowd, particles), Aerosmith's members with their per-venue rigs and per-song anims, DMC, the entrance / DMC-intro transitions, star-power effects | `--venuetest` over the six venues; every band member animated in a song | 4-6 h |
| M5 career | Tiers with opening acts, Aerosmith unlock, encores, movies with subtitles between tiers, Joe Perry's boss battle, Bonus songs, store / Vault, cheats, saves, face-off / pro face-off / battle | a career walk through tier 1 into tier 2; boss battle walk | 3-5 h |
| M6 module / release | Launcher module (build_module_06, games.yaml, iso_signatures with title ID 41560819), packages on every system, verify_bundle, ready_to_play (version only when the owner asks) | every bundle verified; the launcher runs gd06 on Linux | 1-2 h |

Total: about 11-18 h of agent time, spread over several runs. Builds at -j2
with one build at a time while another agent shares the machine.
