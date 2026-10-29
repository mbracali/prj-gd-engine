# Guitar Hero III (Xbox 360): disc and file formats

Everything is big-endian unless said otherwise. **[C]** = confirmed (in the
data over the whole disc, or at an XEX address), **[I]** = inferred. Our
decoders: `tools/*.py` (validated over the whole disc first, lesson L21), then
`engine/src/data/`.

## The disc

XDVDFS game partition at 0xFD90000 (XGD2) [C]. 660 files:

| Path | What |
|---|---|
| `default.xex` | the executable (XEX2, title ID 415607F7) |
| `DATA/COMPRESSED/{PAK,SONGS,ZONES,FONTS,FXFILES,IMAGES}/*.xen` | compressed archives and images (below) |
| `DATA/COMPRESSED/compress.toc.xen`, `compress.toc.lst.xen` | the table of the compressed files (binary and a text listing) |
| `DATA/MUSIC/<song>.fsb.xen` + `<song>.dat.xen` | the song audio (FSB4, encrypted; M2) |
| `DATA/STREAMS/streamall.fsb.xen` + `.dat.xen` | streamed sounds (plain FSB3) |
| `DATA/MOVIES/BIK/*.bik.xen` | Bink 1 videos (`BIKi`) |
| `DATA/SCRIPTS/ENGINE/engine_params.qb.xen` | a loose QB file (not compressed) |
| `DATA/ANIMS/standardkey{Q,T}.bin.xen`, `DATA/FXFILES/MaterialLibrary.bin.xen` | loose binaries (M5 / M6) |

## Compressed files

Every file under `DATA/COMPRESSED/` (except the `.lst`) is one raw deflate
stream (RFC 1951, no zlib header) of the whole file [C: all 417 inflate
exactly to the size in the TOC].

`compress.toc.xen`: `"TOC1"`, u32 count (0x1a1 = 417), u32 0x20 (header
size), u32 total, 16 zero bytes, then `count` entries of 16 bytes: u32 QB key
of the path relative to `DATA/COMPRESSED` with backslashes
(`pak\crowd_ped_03.pak.xen`), u32 inflated size, u32 stored size, u32 method
(3 = deflate) [C data]. `compress.toc.lst.xen` is the same list as text
(`Name: <path> <size> <key hex>`).

## QB keys

`key(name)` = CRC-32 (reflected polynomial 0xEDB88320, initial value
0xFFFFFFFF) of the lower-cased name **without the final XOR** [C data: every
pak entry type, every TOC key; XEX address: TODO]. `tools/qbkey.py`,
`engine/src/data/qbkey.h`.

**Names.** `PAK/dbg.pak.xen` holds one text file per script file
(`[LineNumbers]` then `[Checksums]` lines `0x%08x name`); with the disc's
file names they give 41,012 names (`Disc::loadNames`, `tools/qbkey.py
--table`).

## PAK (`.pak.xen`, optional `.pab.xen`)

A table of 32-byte entries, ended by a `.last` entry [C: all 400 paks, 24,726
files, no overlaps]:

| Offset | Field |
|---|---|
| 0 | type: QB key of the extension (`.qb`, `.img`, `.tex`, `.scn`, ...) |
| 4 | offset of the data **from the start of this entry** |
| 8 | size |
| 12 | pak name key (0 on the disc) |
| 16 | full name key (path) |
| 20 | short name key |
| 24 | parent's full name key (`.scn` -> its `.tex`, `.hkc` -> its `.scn`), or 0 |
| 28 | flags (0, or 4 for some zone files) |

With a `.pab.xen` next to the `.pak.xen` (`qb`, `qb_<lang>`, `dbgq`,
`global`), the data lies past the end of the `.pak`: offsets count through
pak + pab as one buffer [C data]. File data is aligned (0x20 or more).

Types on the disc: `.ska` 18,747, `.qb` 2,731, `.dbg` 1,356, `.img` 712,
`.tex` 396, `.skin` 285, `.wav` 227, `.scn` 99, `.ske` 35, `.rag` 27, `.hkc`
24, `.nqb` 24, `.pfx` 24, `.fnt` 16, `.table` 12, `.mdl` 11.

## QB (`.qb`)

| Part | Layout |
|---|---|
| header | u32 0, u32 file size, 20 fixed bytes `1c 08 02 04 10 04 08 0c 0c 08 02 04 14 02 04 0c 10 10 0c 00` |
| section | u32 type `0x0020TT00`, u32 name key, u32 file key (= the pak entry's full name key), u32 value or offset, u32 0 |
| array | u32 `0x0001TT00` (TT 0: empty), u32 count, u32 the single value / offset, or the offset of the list of values / offsets |
| struct | u32 `0x00000100`, u32 offset of the first item (0: empty) |
| struct item | u32 `0x00TT0000` with TT = 0x80 \| type, u32 name key (0: unnamed flag), u32 value or offset, u32 offset of the next item (0: last) |
| pair / vector | u32 `0x00010000`, 2 / 3 floats |
| string | NUL-terminated, padded to 4; wide strings UTF-16BE |
| script | u32 checksum, u32 size, u32 stored size, the bytecode (LZSS when the sizes differ), padded to 4 |

Types: 1 int, 2 float, 3 string, 4 wide string, 5 pair, 6 vector, 7 script,
0xA struct, 0xC array, 0xD key, 0x1A key reference (a global's name), 0x1B
string reference. Offsets are from the start of the file; a section's data
follows its header [C: all 2,731 QB files, 46,899 sections, 16,552 scripts
decode with every byte accounted for].

**LZSS** (scripts): a 4 KiB ring filled with zeros, writes from 4078; a flag
byte per 8 tokens, LSB first, 1 = literal; a reference is 2 bytes, position
`b0 | (b1 & 0xF0) << 4`, length `(b1 & 0x0F) + 3` [C data: every script
unpacks to exactly its size; XEX address: TODO].

## Song charts (`songs\<id>.mid.qb` in `SONGS/<id>_song.pak.xen`)

Arrays per song (`<id>_...`), times in ms [C data, slowride]:

- `song_<diff>`, `song_rhythm_<diff>`, `song_guitarcoop_<diff>`,
  `song_rhythmcoop_<diff>`: flat int triples (time, length, fret mask);
- `<diff>_Star`, `<diff>_StarBattleMode` (per part): (time, length, notes);
- `FaceOffP1/P2`, `BossBattleP1/P2`: (time, length);
- `timesig`: (time, numerator, denominator); `fretbars`: times;
- `markers`: structs {time, marker (text key)};
- `*_notes` arrays (scripts, anim, triggers, cameras, lightshow, crowd,
  drums, performance): (time, note, length); and event structs
  {time, scr, params} (`performance`).

The meaning of each fret-mask bit, HOPO flags and the rest: M2.
