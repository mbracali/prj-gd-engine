# Guitar Hero: Metallica (Xbox 360): findings

Each line **[C] address / data** (confirmed on our disc) or **[I]**
(inferred). Addresses are in our `default.xex` (title ID 41560830).

## Executable

- XEX2, title ID 41560830, media ID 2E0C8E5F, base 0x82000000, entry
  0x821B8BF8 [C: XEX headers].
- Ghidra: 30,337 functions after `FixXexFunctions.java`, 9,246 strings,
  94,894 data references [C: export of 2026-11-10].

## Data

See `formats.md` (CHNK, padded paks, QB struct items, type 0x1C).

## Front end

- UI state machine and descs: see `ui.md` (task_menu prefixes [C 0x82025698
  .. 0x820256E4, 0x823090D8 .. 0x8230A7C0, 0x82309028]).
- `default_lag_settings.xenon`: gem_lag_ms -45, input_lag_ms -61 (GH3's
  xenon_gem_offset / input_offset were -66) [C data].
- Health tables per instrument and difficulty (beginner .. expert) [C data].

## Audio

- Song banks: key generator [C 0x820E24F8], called for "%s_%d", n = 1..3,
  by [C 0x820E6948] (opening "music/%s"); previews by "a%s_preview.fsb"
  [C 0x820E27E8] (another path, not done).
