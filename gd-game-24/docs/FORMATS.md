# Rock Band Unplugged (PSP): file formats

Discovery notes (GD24-001, 2026-11-17). Our own words; no original code or data.
**[C]** = confirmed (data and/or code, with the address), **[I]** = inferred.
Addresses are the decrypted EBOOT's (fixed load address 0x08804000).

## The disc

UMD image, ISO 9660, disc ID **ULUS-10418** (`UMD_DATA.BIN`, `PARAM.SFO`
DISC_ID), title "Rock Band Unplugged™", disc version 1.00, firmware 5.03
needed [C]. 444 files, 1.2 GB.

| Path | What |
|---|---|
| `PSP_GAME/SYSDIR/EBOOT.BIN` | The game, encrypted (`~PSP`, tag C0CB167C, type 1) [C] |
| `PSP_GAME/SYSDIR/BOOT.BIN` | Zero-filled (unused) [C] |
| `PSP_GAME/SYSDIR/UPDATE/` | Sony firmware update: ignore |
| `PSP_GAME/ICON0.PNG`, `PIC1.PNG`, `ICON1.PMF` | XMB icon, background, icon movie |
| `USRDIR/FAT` | Name-hash to disc-sector table (below) |
| `USRDIR/CANARY.EDAT` | One encrypted DLC song (below): not needed |
| `USRDIR/MUSIC/<SONG>/` | 41 songs + 14 tutorial tracks `T01..T15` (no T14): `<SONG>.ATM`, `<SONG>_PREVIEW.AT3`, `BEATMATCH.STR`; tutorials have one `.ATM` per language (`EN_/FR_/GE_/IT_/SP_T01.ATM`) |
| `USRDIR/STREAMS/*.STR` | Heap images (below): 24 tour venues (`ARENA_*`, `BIG_CLUB_*`, `SMALL_CLUB_*`, `THEATRE_*`), `SMALL_VENUE01` and test rooms, `COMMON`, `GLOBAL_BINARY`, `ANIMS_0..2` (63 MB of animation), `ROCKERMAKER` (18 MB, character creator), `WORLDMAP`, `JUKEBOX`, `TUTORIAL_ROOM`, `TRAINING_ROOM`, `EMPTY`, `CTEST_BPM3` |
| `USRDIR/UI/UI_0..2.STR` | The UI, heap images (24 MB) |
| `USRDIR/PRESET/0..15.STR` | 16 preset bands (same layout as a save) |
| `USRDIR/SOUNDS/*.PSP.BNK` | Tutorial voice-overs (5 languages x 14) and `TUTORIAL_ROOM`: Sony SBlk banks |
| `USRDIR/TEXT/STR_<LANG>_*.TXT` | All text, 5 languages |
| `USRDIR/TEXTURES/UMD/LOGO_00..53.PNG` | Band logos, plain PNG 144x80 |
| `USRDIR/MOVIES/BBE_LOGO.PMF`, `RB_CREDITS.PMF` | Movies |

Other code-side paths [C strings]: `ms0:/PSP/GAME/%s/MUSIC` and
`%s/BEATMATCH.str.EDAT` (downloaded songs on the memory stick, NPDRM EDAT),
`disc0:/sce_lbn0x%x_size0x%x` (files opened by sector through `FAT`),
`STR_%s_%s.TXT`.

## EBOOT.BIN (the executable)

Decrypted with pspdecrypt (open source, libkirk): a plain non-relocatable
ELF (`ET_EXEC`, MIPS Allegrex LE, entry 0x08804114, one PT_LOAD at
0x08804000, 0x356 KB on disc + 0xF2EC4 BSS, so the image ends at 0x08C4CF44),
with section headers [C]. Module name `"rockband"`, gp 0x08B62060 [C].
Built with C++ exceptions (`.eh_frame`, `.gcc_except_table`). Source paths
in the strings show Harmonix's Rock Band code base ported to the PSP:
`../src/rock/beatmatch/` (SongParser, GameGemDB, GameGemList, BeatMatcher,
PhraseDB, MidiParser, TrackWatcher, VocalNoteList, MasterAudio...),
`../src/rock/utl/` (BinStream, ChunkStream, TextStream), `../src/rock/m2game/`
[C strings]. DTA symbol names (`PART GUITAR`, `CharClipSamples`, `COPY_*`)
are in the data. So: Rock Band's beatmatch layer with a Backbone front end,
not the Milo engine of RB1/RB2 on console.

Imports: 35 libraries, 324 functions (312 named from PPSSPP's NID tables,
12 unknown) [C, `tools/prx_imports.py`]. The ones that matter for a port:
`sceAtrac3plus` (song audio), `sceSasCore` (sound effects), `sceMpeg`
(PMF movies), `sceGe_user` (graphics), `sceCtrl`, `IoFileMgrForUser`,
`sceUtility` (save data, dialogs), `scePspNpDrm_user` + `sceNpCommerce2` +
`sceHttp` (the PlayStation Store DLC; not needed).

## FAT

16 KB: 1,024 slots of 16 bytes, 438 used: `{u32 name hash, u32 start
sector, u32 size in bytes, u32 0}`, not sorted [C data: the T01..T15
`BEATMATCH.STR` size 0x1DE800 appears]. The game hashes a file name and opens
`disc0:/sce_lbn0x<sector>_size0x<size>` (FUN_08a23f5c) [C]. A port reads the
files by name and does not need it.

## STR: heap images

Every `.STR` except the presets and `GLOBAL_BINARY` has the same container
[C data]:

- `0x00`: u32 version 1, u32 (header data size, purpose not checked), u32
  `N` (19 for streams, 20 / 124 for anims, 128 for songs).
- `0x40`: `N + 1` entries of 0x40 bytes: `{u32 file offset, u32 size,
  u32 end offset, ...0}`; empty entries have size 0. Entry `N` is a small
  tail (0x800) in venue / common streams.
- Then allocator state up to the first chunk (e.g. 0x500..0x45800 in
  `COMMON.STR`: block lists with addresses in PSP RAM; matches the
  "heap0 / heap1", "FREE LIST for heap #%d" strings) [I].
- The chunks: raw memory, 0x800-aligned in the file.

The chunks are **memory images with absolute pointers** (0x088xxxxx ..
0x09Fxxxxx): objects as they sat in RAM, vtable pointers into the EBOOT,
Harmonix `Symbol` strings, `{pointer, count, capacity}` vectors [C data].
Each chunk is loaded to a fixed address (estimated by pointer density:
venue / `COMMON` chunk 0 at about 0x08F36000, chunk 1 at about 0x091C3000;
song chunks at about 0x096E9800) [I: exact bases from the loader, next].
There is no relocation table: a port reads the objects through their class
layouts (vtable address = class), so every class a feature needs must be
described from the EBOOT. This is the main cost of the port.

`GLOBAL_BINARY.STR` has a different header (`{0x1B800, 0xFB000, 0x116800}`
= two parts and the total) and is the always-resident heap (pointers in
0x08C5xxxx .. 0x08EFxxxx, i.e. right after the BSS) [I]. Song and venue
images point into it (0x08C6xxxx).

`PRESET/n.STR`: `{u32 load address 0x08B72AA8 (a BSS buffer), u32 size
0x10F3C8, u32 1, u32 1, ...}` then a band: `BAND`, name ("Uncle Larry's
Riot"), "Stars / Money / Fans" text, logo name, members [C data].

## BEATMATCH.STR (the charts)

Same container, `N` = 128; entries 125, 126, 127 and 128 hold four images
of the same size, one per **difficulty** (a word at +0x2F4 in each is 0..3;
the first word is the entry number 0x7D..0x80; only ~2,000 of ~200,000
words differ between them) [C data, ABC and 3SAND7S]. Pointers are the same
in all four, so they load at one address.

Inside: the parsed song (no MIDI file is stored: no `MThd`) [C data]. Arrays
of 20-byte gems were found by their ascending float times [C data, ABC]:

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | f32 | time in ms |
| 0x04 | u32 | tick (480 per beat: 2952.4 ms = tick 2160 at ABC's tempo) |
| 0x08 | u16, u16 | duration (ms?, ticks?) [I] |
| 0x0C | u8 | MIDI pitch-like id (0x3A, 0x3C...) [I] |
| 0x0E | u8 | lane bitmask 1/2/4/8: **four lanes** [C data] |
| 0x0F | u8 | flags (0x0A / 0x0B) [I] |
| 0x10 | u32 | 0 |

This matches Rock Band's GameGem record. The tempo map, the phrase / section
lists (PhraseDB, `VERSECHORUSSOLO`), the four instrument tracks (`PART GUITAR /
BASS / DRUMS / VOCALS`) and the star-power phrases are found through the
SongData object graph: to map next (GD24-002) from GameGemDB.cpp /
SongData.cpp users (strings at 0x08B40964, 0x08B40B88).

## ATM: song audio (5 stems, encrypted ATRAC3plus)

[C data, all 55 folders]:

- Header 0x38 bytes: u32 (random-looking, per file), u32 stream count 5,
  u32 (random), u32 frame size 0x190, u32 (random), then 5 x u32 stream
  sizes, then 16 bytes (random). The random words look like a key / seed.
- Data from 0x38: **400-byte frames interleaved round-robin over the 5
  streams**; a stream that ends early is padded with zero frames (DELUXE:
  stream 2 shorter, its padding is exactly the missing size); file size =
  0x38 + 5 x the largest stream size.
- 400-byte frames = ATRAC3plus at ~69 kbit/s per stream [I], but the frames
  are **encrypted**: half start with a set first bit (a plain ATRAC3plus
  frame always starts with 0), and FFmpeg's atrac3plus decoder rejects
  them. The zero padding frames are left in clear.
- The decryption is in the EBOOT: the ATRAC stream reader FUN_089b4fdc
  calls FUN_088f4c90 (walks the 5 streams), which calls two 3 KB routines,
  FUN_0888ec50 and FUN_0888fa1c, full of XORs with 24-bit masks (0xFFFFFF,
  0x800000, 0x400000, 0x1000000): a custom stream cipher [C call graph; the
  algorithm is the first M1 job].
- The 5 stems are guitar, bass, drums, vocals and the backing track, each
  muted when its track is missed, as in Frequency / Amplitude [I: order to
  confirm].

`<SONG>_PREVIEW.AT3`: a plain RIFF WAVE, WAVEFORMATEXTENSIBLE ATRAC3plus
(GUID BFAA23E9-58CB-7144-A119-FFFA01E4CE62), stereo 44.1 kHz, 2048-byte
frames (352 kbit/s), ~30 s; **FFmpeg decodes it as is** [C].

## PSP.BNK: sound banks

`{u32 3, u32 2 sections, u32 info offset 0x18, u32 info size, u32 data
offset, u32 data size}`; the info section starts `SBlk` (Sony SCREAM bank);
the data are PS-ADPCM (VAG) frames (all 52,861 16-byte frames of
`TUTORIAL_ROOM` pass the ADPCM header check) [C data]. gd-game-01's PS2
ADPCM decoder applies. The gameplay sound effects are not in BNK files on the
disc: they come from the heap images (`GLOBAL_BINARY` / `COMMON`) and play
through sceSasCore [I].

## PMF: movies

PSMF: a 0x800 header, then an MPEG program stream with H.264 Main 480x272
29.97 fps [C: ffprobe]. FFmpeg decodes them (mpegps demuxer + h264).
`BBE_LOGO` 3.9 s, `RB_CREDITS` 79 s, `ICON1` XMB.

## TEXT: strings

UTF-8 text. Line 1: `<count>\t1`; line 2: the language name ("English");
then `count` strings, one per line, `\n` written as the two characters
[C data]. Strings are referred to by index. Sets: `STR_<L>.TXT` (= GLOBALS,
2,549 strings), `_MUSIC` (title / artist / album per song), `_JUKEBOX`,
`_GAMEPLAY_TIPS`, `_BANDNAME_A/B`, `_MEMBERNAME_MALE/FEMALE`; languages EN,
FR, GE, IT, SP.

## CANARY.EDAT

NPDRM EDAT (`\0PSPEDAT`), content ID `UP8802-ULUS10418_00-JUSTAGIRL0000000`:
one store song shipped on the disc to test the DLC path, encrypted for a
licence we do not have [C header]. Not needed; DLC is out of scope.

## Venues, band, animation (size of the job)

- Venues: 24 tour venues (+ test rooms), heap images of ~3.8 MB, two chunks each (about 2.5 MB + 1 MB).
  Contents are PSP GE-ready meshes and textures (swizzled, paletted) as
  objects [I]. The renderer needs the mesh / material / texture / light /
  camera classes from the EBOOT.
- Band and animation: `ANIMS_1.STR` 63 MB in 104 chunks (one per clip set
  or character) [C data], `ROCKERMAKER.STR` 18 MB (character parts).
- UI: `UI_0..2.STR`, 24 MB, UIList / UIElement / UIAnim / UITextBox objects
  ("script linking not valid in stream builds") [C strings].
