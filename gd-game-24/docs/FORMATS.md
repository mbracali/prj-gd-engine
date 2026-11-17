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
song images at 0x096E9A80 [C: every BEATMATCH image's 27 kPad_* symbol strings
are pointed to from that base, all 41 songs]) [I for the venue / COMMON bases].
There is no relocation table: a port reads the objects through their class
layouts (vtable address = class), so every class a feature needs must be
described from the EBOOT. This is the main cost of the port.

`JUKEBOX.STR` is not this container: it starts with u32 55 (the song count)
and holds a song table (ids, hashes, short names such as `3sand7s`) [C data;
layout for M1].

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
words differ between them) [C data, all 41 songs]. Each image is RAM
loaded at **0x096E9A80** [C: all 27 `kPad_*` symbol strings pointed to from
that base, every song]. Every image holds the gems of all four tracks and
all four difficulties (the Easy image gives the same gems as the Expert one
[C: `gd24 --charttest`]); what differs are other per-difficulty objects.

The parsed song (no MIDI file is stored: no `MThd`) [C data]. The gem
record, 20 bytes:

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | f32 | time in ms |
| 0x04 | u32 | tick (480 per beat: 2952.4 ms = tick 2160 at ABC's tempo) |
| 0x08 | u16, u16 | duration (ms?, ticks?) [I] |
| 0x0C | u8 | MIDI pitch-like id (0x3A, 0x3C...) [I] |
| 0x0E | u8 | lane bit mask 1 / 2 / 4 / 8: **four lanes**; chords OR them (5, 10, ...) [C data] |
| 0x0F | u8 | flags (0x0A / 0x0B) [I] |
| 0x10 | u32 | flags (0, 0x02000000 seen) [I] |

The tracks [C data, all 41 songs]: one object per instrument holds a
pointer to its name `PART DRUMS` / `PART BASS` / `PART GUITAR` /
`PART VOCALS`, the short name (`drum`, `bass`, `guitar`, `vocals`) and its
index 0..3; a list of the four name pointers sits before the first one.
After each track object come four difficulty objects, each with a gem
vector `{u32 pointer, u32 count, u32 capacity = count}` that follows a small
number which is the same for all of a song's vectors (0xAA in ABC, 0x5A in
WOULD); other 20-byte vectors near them (one to a few records, e.g. phrase
spans [I]) follow 0x7F800001 or another vector's capacity. The four
vectors of a track are Easy, Medium, Hard, Expert in address order [I: the
counts grow; 156 of 164 Easy charts use the two middle lanes only]. One
tick has one time in every list (the tempo map is shared) [C].
`src/data/beatmatch.*`, `gd24 --charttest [<song>] [--notes]`.

Still to map: the tempo map and time signatures as objects, the sections /
phrases (PhraseDB, `VERSECHORUSSOLO`), Overdrive phrases, the meaning of the
duration fields. The tutorials' BEATMATCH (T01..T15, one shared file size)
have one list per track.

## ATM: song audio (5 stems, encrypted): out of scope

[C data, all 55 folders]: a 0x38-byte header (stream count 5, frame size
0x190, the 5 stream sizes), then 400-byte frames interleaved round-robin
over the 5 streams (guitar, bass, drums, vocals, backing [I]); a stream
that ends early is padded with zero frames. The frames are **encrypted**.

Owner's decision (2026-11-17): the project never breaks a protection. The
full 5-track audio is **out of scope**; the engine does not read or decrypt
the `.ATM` files. Songs play their unencrypted `<SONG>_PREVIEW.AT3` from the
player's disc instead (below).

`<SONG>_PREVIEW.AT3`: a plain RIFF WAVE, WAVEFORMATEXTENSIBLE ATRAC3plus
(GUID BFAA23E9-58CB-7144-A119-FFFA01E4CE62), stereo 44.1 kHz, 2048-byte
frames (352 kbit/s), ~30 s; **FFmpeg decodes it as is** [C]. Chunks
[C data, all 41]: `fmt `, `fact` {u32 samples, u32 encoder delay} (ABC
1,292,946 and 3,070), `smpl` with one loop {delay, delay + samples - 1},
`data`. FFmpeg's atrac3p gives frames x 2048 samples = delay + samples +
368 for every song [C]; gd24 cuts the delay and keeps the fact length
(`src/audio/at3_preview.*`, `gd24 --audiotest`: 41/41, 0 decode errors).

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

UTF-8 text. Line 1: `<count>\t1`; then `count` strings, one per line, the
first being the language name ("English"); `\n` written as the two
characters [C data: every table has exactly count + 1 lines]. Strings are referred to by index. Sets: `STR_<L>.TXT` (= GLOBALS,
2,549 strings), `_MUSIC` (title / artist / album per song), `_JUKEBOX`,
`_GAMEPLAY_TIPS`, `_BANDNAME_A/B`, `_MEMBERNAME_MALE/FEMALE`; languages EN,
FR, GE, IT, SP.

## Graphics in the heap images (the highway: COMMON.STR)

COMMON.STR's chunk 0 loads at 0x08F36600 (65 name pointers agree) and
chunk 1 at 0x091C32E0 (the track plane's sub-mesh pointers land on its
blocks) [C]. Pointers to texture pixels and mesh blocks are 0x80 past the
data in the file (string pointers are not) [C data; why: not known].

The engine's resource directory (dump function FUN_088dc2d4) keeps lists:
textures (+0x04), materials (+0x10), animations (+0x1C), meshes (+0x28),
another list (+0x34); a mesh records vertices, triangles and a type 1
Scenery / 2 Skinned / 3 Prop [C code].

**Texture**, 0x54-byte object [C data, 80 in COMMON]: +0x00 u32 0x54, +0x04
u16 width, height, +0x08 the same again, +0x0E bits per pixel (4 / 8),
+0x11 flags (2; 32 on some 8-bit effects), +0x18 / +0x1C pixel and palette
pointers, +0x20 {1, log2 width, log2 height}, +0x2C 0x77359400, +0x30 the
list node, name at +0x3D. Pixels at +0x50, **PSP-swizzled** (16-byte x
8-row blocks), the palette right after (RGBA8888, alpha 0x80 = opaque)
[C: decoded images are the gems, rails, badges, emblems, plates; chosen by
the smoothest image over start offsets]. A few character textures (e.g.
the 128x128 head) still decode wrong [I: another layout].

**Mesh block** [C data, 46 in COMMON]: {u32 size, u32 vertices, u32
primitives, u32 0, u32 vertex data offset, u32 1, u32 -1, ...} (skinned:
{..., 1, offset, -1}), then a GE display list (VTYPE, PRIM..., RET) and the
vertices at the offset, in the GE's VTYPE layout. gem_4a: VTYPE 0xA1 (8-bit
UV, normal, position), 8 triangle strips, 139 vertices. Positions are
normalized (8 / 16-bit: -1..1); the mesh object before the block holds
bounds (gem_4a: x -0.347..0.347, centre (0, 0.02, 0.13), 0.35) that give
the scale [I], and material / block pairs (track_plane_A: five sub-meshes).

The highway's pieces [C names]: `track_plane_A` (with the `rails`,
`rails_glow`, `surface`, `seekerRail` textures [I]), `plate_4a..d` (the lane
plates, skinned, red / yellow / green / blue textures), `beat_bar_a`,
`beat_bar2_a`, `gem_4a..d` + `PBT_Gem4A..D` (red, yellow, green, blue:
RED / YELLOW / GREEN / BLUE NOTE [C: measured colours]), `gem_4od`,
`gem_4pass` (Prism pass), the `*_crash` pieces, `OD_glow_ray_A`, the
multiplier badges `pa_2x..11x`, `OD_drums/bass/guitar/vocals`. The
executable has none of these names (objects are looked up by hash [I]).
`src/data/psp_gfx.*`, `gd24 --highwaytest [--sheet f.bmp]`.

Not known yet: how the scene places them (node transforms, the per-mesh
scale, the camera), the lane positions and the track's length / speed.

## Controls (what the data say)

[C] STR_EN.TXT lists the original's Options > Controls actions: TRACK SWITCH
RIGHT / LEFT, RED / YELLOW / GREEN / BLUE NOTE, OVERDRIVE ("--- unassigned
---" also appears: assignable [I]). The help bars use tokens `$button1` (select /
confirm / continue), `$button2` (back / cancel), `$button3`, `$button4`,
`$left_trigger`, `$right_trigger`, `$stick`; the code's token table at
0x08b4ef38 lists {name, getter 0x08932c24, index}: left 0, right 1, up 2,
down 3, button1..4 4..7, start 8, menu 9, left_trigger 10, right_trigger
11, left_trigger2 / right_trigger2, stick, stick2, updown, leftright,
voice_on / voice_off. The glyphs come from string ids filled at run time
(BSS 0x08b5b4d0), so the defaults are not in a data table. The tips
(STR_EN_GAMEPLAY_TIPS.TXT, "(default controls)") name Overdrive on two
buttons, one per thumb (glyphs А and Е of the ButtonsSM font), track
switching on two (И, Й) and pause on one (К). [I] the default lanes L, Up,
Triangle, R, Overdrive Cross / Down, track switching Left / Right
(`gameplayOf` in `src/input/input.h`) until the glyph font or the code's
controls table is decoded. No PSP-mask table exists in .rodata [C: scanned
for four distinct button masks at strides 2..24].

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
