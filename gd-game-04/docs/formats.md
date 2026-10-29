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
| `DATA/MUSIC/<song>.fsb.xen` + `<song>.dat.xen` | the song audio (FSB3, encrypted; "Audio" below) |
| `DATA/STREAMS/streamall.fsb.xen` + `.dat.xen` | streamed sounds (plain FSB3, 367 samples) |
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

## Audio (FMOD Ex 4.06.12 banks)

The XEX links FMOD Ex 4.06.12 (`C:\nxcommon_gh3\fmod\src\fmodexsrc40612_xbox360\...`
source paths in its strings) [C].

**Encryption.** `DATA/MUSIC/<song>.fsb.xen` is stored as
`bitreverse(plain) XOR key[i % 10]`, i.e. plain = bitreverse(stored XOR key),
`i` the byte's offset in the file, key `5atu6w4zaw` [C: the script
`SongLoadFSB` (`scripts\guitar\guitar_song.qb`) passes this string to the
bank loader; all 91 song banks then read "FSB3"]. The transform itself is
FMOD's (XEX address of FMOD's decryption: TODO).

**FSB3** (little-endian): `"FSB3"`, u32 samples, u32 sample-header bytes,
u32 data bytes, u32 version 0x00030001, u32 mode; per sample: u16 header
size, char name[30], u32 length (frames), u32 stored bytes, u32 loop start,
u32 loop end, u32 mode (0x01100040 for the stems), s32 rate (44100), u16
volume, s16 pan, u16 priority, u16 channels (2), f32 min / max distance, then
header size - 80 bytes of XMA seek table; zeros pad the headers to a 16-byte
boundary; the sample data follows back to back (24 + headers + data = file
size) [C: all 92 banks, 718 samples].

**Codec.** XMA1 (2048-byte packets whose header starts with a 4-bit sequence
number) [C data: FFmpeg's xma1 decoder decodes all 718 samples to at least
their header length, 18.2 hours; `gd04 --sfxtest`].

**Stems.** Each song bank holds `<song>_guitar`, `<song>_rhythm`,
`<song>_song` and (most) `<song>_preview`, stereo 44.1 kHz. There are no
co-op banks: co-op plays the guitar and rhythm stems [C data].

**.dat** (big-endian): u32 count, u32 size of the .fsb, then 20-byte entries:
u32 QB key of the stem name, u32 sample index, 12 zero bytes [C data].

## QScript bytecode

`tools/qbscript.py` (opcode table; `check` tokenizes every script: all
16,552 do, every byte accounted for). Numbers in the bytecode are
little-endian. Highlights [C data unless noted]: `01` end of line, `16` name
(u32 key), `17` int, `1a` float, `1b` string (u32 length incl. NUL), `1e`
vector, `1f` pair, `07` `=`, `08` `.`, `0e` / `0f` `(` `)`, `03` / `04`
`{` `}`, `05` / `06` `[` `]`, `2d` `<arg>`, `2c` `<...>`, `4b` `$` (global
value), `20` begin, `21` repeat, `22` break, `24` end of script, `47`
fast-if (u16 forward offset), `48` fast-else (u16), `27` elseif (two u16),
`28` endif, `49` short jump (u16), `3c`..`3f` switch / endswitch / case /
default, `4a` an inline QB struct (u16 length, aligned to 4, the struct in
the QB file layout), `00` a no-op between lines [I]. The semantics and the
interpreter's addresses: M3, `docs/qscript.md`.
