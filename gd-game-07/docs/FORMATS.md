# Guitar Hero World Tour (Xbox 360): disc formats against Metallica's

World Tour is the first gh4 game; Metallica (gd-game-10) is its successor on
the same engine, so this file only records where World Tour differs from
`gd-game-10/docs/formats.md`. Counts come from gd-game-10's checkers run on
World Tour's own disc (2026-11-17, GD07-001). [C] = confirmed from data or
code (address), [I] = inferred.

## Same as Metallica [C data: 0 problems]

- Disc: XDVDFS, `default.xex` + `DATA/` (`COMPRESSED/{PAK,SONGS,ZONES,ZONESCMP,JAMS,TEX,IMAGES,MODELS,FONTS,FXFILES}`,
  `MUSIC`, `MOVIES/BIK`, `ANIMS`, `STREAMS`, `SCRIPTS`), every file `.xen`.
- Paks / pabs: 929 paks, 31,554 files.
- QB: 1,809 files, 45,233 sections; QScript: 9,606 scripts, every opcode
  known to gd10's disassembler.
- In the C++ decoders: `.dbg` 2,680, `.fnt` 15, `.img` 5,497 of 5,513,
  `.nqb` / `.pfx` 106, `.qb` 1,703, `.scn` 52, `.ska` 335, `.ske` 228,
  `.tex` 1,657, `.table` 4, one `.mdl`.

## Differences (gd10's `--formattest` failures on World Tour's disc)

| Files | Reason (gd10 message) | Notes |
| --- | --- | --- |
| `.ska` 5,372 | `gh4 compressed q key` 2,730, `gh4 q size` 2,605, `q sizes` / `t sizes` 9, `quaternion xyz longer than 1` 28 | World Tour uses the compressed rotation keys (0x4000 forms) Metallica's disc never has; the anim header's flags byte is 0x06 (Metallica 0x96) and the second header word 0 (Metallica 0x19a) [C data, first 3,000 clips]. The main M1 item. |
| `.skin` 1,696, `.scn` 55, `.mdl` 13 | `gh4 scene header offsets` | An earlier gh4 scene header [I]. |
| `.hkc` 106, `.rag` 196 | `hkc: packfile header` / `no Havok packfile magic` | Discovery guessed an older Havok; it is the same 5.5.0-r1 as Metallica's ("Decoded since"). |
| `.wav` 3,106 | `unknown chunk` | The drum-kit / music-studio sample banks (e.g. `Bliphop_*`, `BrazilianPercussion_*`) [I]. |
| `.table` 66 | `unexpected .table` | |
| `.ske` 11 | `ske: header` | |
| `.img` 16, `.tex` 19 | image class, tex hash table, format 10, volume slice size | |
| `.jam` 19, `.qs` 8,495 | no decoder | `.jam`: GHTunes songs (music studio). `.qs`: localised strings, not decoded by gd10 either. |

Song banks (`DATA/MUSIC/*.fsb.xen`) are not plain FSB, as Metallica's; not
examined (owner item, agents/OPEN_QUESTIONS.md 1).

## Decoded since (GD07-002, 2026-11-17; gd07 `--formattest`: all files parse)

- `.ska`: animated Q tracks are packed back to back (Metallica: 16-aligned).
  A Q key [C 0x82445E88]: clip flags 0x100: u16 frame, then u16 w; else w with
  frame = w & 0x7FF. w & 0x4000 clear: 3 x s16; w & 0x3800 clear: a 1-byte
  index into standardkeyQ (xyz 0: identity); else x / y / z as one byte where
  w has 0x2000 / 0x1000 / 0x0800, else s16 on an even offset; keys end even.
  standardkeyQ.bin is stored little-endian (the game byte-swaps it,
  0x82445730); entry i = (0, 0, 0, i). Animated T tracks: GH3's byte-coded
  keys (0x40 | frame byte, or a u16 frame; 3 x f32 on 16 bytes from the
  file start). Every animated clip decodes from frame 0 to duration x 60.
- Scenes: mesh records 0x70 bytes: as Metallica's to +0x2B, then
  Metallica's +0x44..+0x6F at +0x2C (material +0x34, index / vertex counts
  +0x44), index block offset +0x58, skin block +0x5C, primitive type +0x64.
- Havok: the same Havok-5.5.0-r1 packfiles as Metallica (11 `.rag` LZSS-wrapped).
- `.wav`: RIFF with "data", a 44-byte big-endian "XMA2" chunk (version 4, one
  stream, 32 / 44.1 / 48 kHz, 1-2 channels) and "seek", as Metallica's.
- `.table`: one or two u32 words, 0xE5F50254 or 0x21775481 (meaning I).
- Textures: 8_8 (format 10, gem_set_01.tex), class-1 normal-map .img (CAS),
  .tex hash tables of 9 bits for more than 128 textures (z_bayou, z_hob),
  8-bit and DXT1 volume textures.
