# Guitar Hero III (Xbox 360): findings

Gameplay rules and engine behaviour as read in the executable and the data,
each with **[C]** (confirmed, with the address or data) or **[I]**
(inferred). Formats: `formats.md`. Tooling: `../agents/CONTEXT.md`.

## The executable

- XEX2, title ID 415607F7, media ID 43048167, base 0x82000000, entry
  0x82136AC8 [C: XEX optional headers 0x00040006, 0x00010201, 0x00010100].
- Ghidra (XEXLoaderWV): 32,998 functions after `FixXexFunctions.java`.
- Paths the code builds: `%s%s.pak.%s`, `%s%s/%s%s.pak.%s` (0x82182310),
  `pak/qb.pak`, `pak/dbgq.pak` (0x820D2200), `P\Compressed\compress.toc.xen`
  [C: strings and their referencing functions].
- 0x82188C50 rewrites `.pak` / `.pab` paths under `cutscenes`, `missions`,
  `pak`, `zones` (lower-cases the path, moves the file name after the
  folder) [C].

## QScript C functions

`ghidra/analysis/cfuncs.tsv` (`DumpCFuncs.java`): 1,392 `{name, code}`
pairs; the main table is 0x82916xxx-0x82919xxx (e.g. GuitarInputLogicInit
0x8210D258, GuitarInputLogicPerFrame 0x8211FA28, Create2DGem 0x82121C58,
fretbar_update_hammer_on_tolerance_CFunc 0x82107590) [C].

## Gameplay

Much of the tuning is QB data, `scripts\guitar\guitar_tweaks.qb` [C data]:
check_time_early 0.116 s, check_time_late 0.100 s, hammer_time_early /
late 0.083 s, default_hammer_on_measure_scale 2.95, strum_before_forming
0.034 s, form_before_strumming 0.025 s, ignore_strums_after_hammers 0.05 s,
the health_change_* table per difficulty and mode, star_power_whammy_add
3.333, star_power_drain_rate 12.5, star_tilt_threshold 16,
star_power_trigger_frames 3, xenon_input_offset / xenon_gem_offset -66 ms.

The scripts copy these into the player's `player_status` struct
(`gem_scroller`, `update_slomo`: check_time_early x current_speedfactor) and
the C input logic reads the struct (lesson L1: run the scripts) [C: the
scripts' text, `tools/qbscript.py dis`].

**HOPO tolerance** [C: 0x82107590, fretbar_update_hammer_on_tolerance_CFunc]:
as the song passes each fretbar i, `player_status.hammer_on_tolerance =
(fretbar[i+1] - fretbar[i]) / hammer_on_measure_scale x floor(den / 4)`,
`den` the time signature's denominator in force at fretbar i.
`hammer_on_measure_scale` is set per song (`call_startup_scripts`: the
song's struct may carry one) [C: script text].

**Note masks.** Bits 0-4 are the frets (green..orange); bit 0x20 is set on
5,815 notes [C data]; its meaning (forced HOPO flip?) is open [I].

The note -> gem expansion 0x821044C8 splits a note's length per fret
(each fret bit x length) and remaps lanes for the battle attacks / lefty
(switch on the pattern from 0x82101FD8) [C, partly read].
