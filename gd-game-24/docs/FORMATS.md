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
| 0x08 | u16, u16 | lengths in ms: the second = the note's length (sustains up to ~10 s), the first = the second minus a per-song constant (48..74 ms) [I: WOULD, EVERLONG, AQUALUNG] |
| 0x0C | u8 | MIDI pitch-like id (0x3A, 0x3C...) [I] |
| 0x0E | u8 | lane bit mask 1 / 2 / 4 / 8: **four lanes**; chords OR them (5, 10, ...) [C data] |
| 0x0F | u8 | flags: 0x0A / 0x0B most gems; bit 0x02 clear (0x08 / 0x09) = a sustain [I: those have lengths up to 9,982 ms, the others at most 405] |
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

### Mesh objects, materials, placement

Object pointers in COMMON point 0x80 past the object as the file has it
(with the chunk bases above) [C: every mesh -> material -> texture -> block
link lands on an object header that way] (`src/data/highway_assets.*`).

**Mesh object** [C data, 50 in COMMON]: +0x00 u32 0x4D, +0x10 0x77359400,
+0x30 f32[3] box min, +0x40 f32[3] box max, +0x50 centre, +0x60 radius,
+0x68 name pointer, +0x6C type (2 skinned, 3 prop), +0x7C u32 n, +0x80 n x
{material pointer, mesh block pointer}. The block's normalized positions
times the box's largest |coordinate| give model units [C: 8-bit 127 and
16-bit 32767 land on the box's extremes for every mesh checked, e.g.
track_plane_A 6.5635, gem_4a 0.3474, plate_4a 0.3729].

**Material**, 0x53 [C data]: +0x0C flags (byte 1: 1 alpha-blended, 3
additive [I: the 3s are the glows, rails, track surface strips, beat bars,
failed X, all with opaque grey textures]; byte 2: 1 on the track's
Overdrive layer `od` [I: shown in Overdrive]), +0x1C texture pointer, name
at +0x80. Texture names sit at +0x3D (+0x3C is a small number).

**Mesh blocks**, more: the words after the counts vary ({0, 1, -1} on
props, {1, 1, -1, 0x20} on the track plane's pieces, bone data on skinned
meshes); skinned meshes have one display list per bone group ({VTYPE,
PRIM..., RET} repeated; plate_4a: two lists, 5 strips) [C data]. Before the
VTYPE the list may set the material ambient alpha (0x58) and colour (0x55,
BGR), which the GE uses for vertices without a colour [C data: beat bars
0xC4C4C4, the track icon alpha 0xC0, others 0xFFFFFF]. Vertex colours come
as 565 / 5551 / 4444 / 8888 (the track edge mesh: 5551, alpha 0 at the far
end, so the rails fade out).

**Model space** [C data]: x across, y along the highway (+y away from the
player), z up. `track_plane_A` (3.21 x 12.92, y -6.56..6.36) has five
sub-meshes: `TrackPlaneEdge` (177 vertices, the rails, the strike bar and
its lane marks, textured from an atlas: red / yellow / green / blue row at
v 0.11..0.19), additive strips `rails`, `od` (Overdrive) and `base` on the
far third, and `track_instrument_icon` (y -3.9..-1.4; placeholder texture
`file4`; the materials `track_instrument_icon0..3` carry `icon_drum`,
`icon_bass`, `icon_guitar`, `icon_mic`). **The lanes and the strike line**:
the four lane-mark quads of the strike bar are centred at x -1.100,
-0.362, 0.379, 1.127, y -4.488 [C data]; the lane plates (0.746 wide) and
the gems (0.69) fit that 0.74 spacing; the beat bars are 2.90 wide.

Not found yet [I]: the camera (no highway camera object in COMMON; the
`entity_camera` / `anim cam` objects look like venue cameras), the spacing
of the four tracks, the scroll speed actually used by the code (below).

## The configuration in BEATMATCH.STR (DTA)

Every BEATMATCH image carries the game's whole Harmonix DTA tree as data
arrays [C data, all 41 songs] (`src/data/dta.*`): an array {u32 nodes
pointer, u32 (file symbol [I]), u16 count, u16 ...}, nodes {u32 value, u32
type: 0 int, 1 float, 5 symbol, 0x10 / 0x11 / 0x13 array, 0x12 string}.
Top level: system, prototype, songs, objects, synth, beatmatcher, audio,
game, mc, mem, joypad, ui, rnd, world, ... Much of it is inherited from the
console games (iop modules, PS2 joypad slots); the executable has none of
the `y_per_second` / `song_scroll_speed` / `slop` strings, so whether its
code reads them is not known [I].

- `(songs (<folder lower case> (name ...) (artist ...) (song (tracks (drum 0)
  (bass 1) (guitar 2) (vocals 3)) ...) (song_scroll_speed 2300) (preview
  <start ms> <end ms>) (rank ...) (initial_track 2) ...))`, 55 entries
  (41 songs + tutorials).
- `(game (track (y_per_second (0 7.5) (1 8.5) (2 9.5) (3 11.5))))`: highway
  units per second, Easy..Expert (gd24 uses it: 11.5 shows 0.94 s of Expert
  between the strike line and the far end).
- `(beatmatcher (watcher (type joypad) (slop 100) (intent_window 250)))`:
  the hit window gd24 uses.
- `(prototype ... (config_standard_4_config1 (joypad ... (drum_slots 15 0 12
  1 7 1 13 2 4 2 5 3) (remap_left 2) (remap_right 3)) (num_slots 4)))`:
  {button, lane} pairs in Harmonix's PS2 kPad order [I: legacy; not used
  for the PSP defaults].

**The preview's place in the song**: `(preview start end)`; for 18 of 41
songs the AT3 starts exactly at `start` (onset lag 0 +-30 ms; mostly the
songs whose AT3 length = end - start); for the others the AT3 length
differs from end - start by up to 11 s and the onsets sit 40..200 ms off.
`gd24 --synctest` measures it (`src/game/preview_sync.*`) and the song
screen applies the measured lag (within +-200 ms) [I].

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

## Gameplay rules (GD24-003)

What the disc and the executable say about the rules, and what gd24 uses
(`src/game/song_play.*`, `gd24 --rulestest`). Our own words.

**Bars** [C data, all 55 BEATMATCH files]: each image holds the song's time
signatures as 16-byte records {u32 bar, u32 numerator, u32 denominator,
u32 tick}, the first at bar 0 / tick 0, pointed to by a vector {pointer,
count, capacity = count} (seven equal copies per image); the ticks agree
with the bars (tick difference = bar difference x num x 1920 / den). E.g.
ABC: 4/4, bar 19 3/4, bar 20 4/4, ... (9 changes). Next to them: the tempo
map, 12-byte records {u32 tick, u32 microseconds per beat, f32 ms}, and a
beat list of 16-byte records every 960 ticks (not used yet).

**Gem flags** [C data, I meaning]: the gem record's last word carries
0x02000000 on runs of about 6 gems, the last one 0x06000000 (0x04000000 =
the run's end): the Overdrive phrases ("glowing white notes"); 0x08000000
on drum-solo gems (the code raises the multiplier per such gem in a solo
mode [C 0x08a91240], not done).

**The rules in the code** (the gameplay object at `*0x08b7d5a8`, one 0x130
block per track from +0x40; the tuning values sit in a block at 0x08b5a440
that no code writes: loaded at run time from a source not found, so every
number is [I]):

| What | Where | Rule |
|---|---|---|
| Multiplier | 0x08a0a204 and its copies | float at +0x67c; shown = int part clamped 1..5; Overdrive (+0x664 bit 8) doubles it, 5 -> 11 (the `pa_2x..11x` badges); no-phrase modes use notes / 10 + 1 [C] |
| Gem hit | 0x08a91240 | score (+0x688) += gem points x shown multiplier; the track's health (+0xd0) += a per-mode amount (x another in Overdrive; in Overdrive all four tracks) [C] |
| Capture | 0x08a86368 | multiplier = int + 1 (at most 5), bonus = capture points x shown multiplier added to the score, the track's captured count +1, a timer set [C] |
| Miss | 0x08a8de08 | only for gems inside the track's phrase: health -= a per-difficulty amount (x a factor off the active track, x another in Overdrive); after the capture timer, multiplier = 1 [C] |
| Release | 0x08a80ad8 | captured tracks queue with their bar; one is released once more than 7 bars went by (unless the multiplier is 2+ and fewer than 11) or when no other track has a phrase coming [C, the queue's finer rules I] |
| Crowd | 0x08af608c | mean of the four healths - (+0x69c) x a per-difficulty amount, shown in 5 steps; Easy keeps healths at 2 x the miss amount for the first 50 % of the song, Medium 30 % [C] |

**The rules in the text** [C, STR_EN_GAMEPLAY_TIPS.TXT]: hit all the notes in
the Phrase Indicator to play a phrase; the instrument then plays on its own
for a while; the indicator follows the player's track; a red track is in
danger, blinking about to fail; a failed instrument can't be played and
Overdrive rescues it; glowing white notes hit perfectly fill the Energy
meter, half full deploys Overdrive (two buttons); "With no failed
instruments, go into Overdrive to double your score multiplier"; "wait to
deploy Overdrive until your multiplier is at 5x"; misses outside the
indicator don't break the streak, notes outside it still score; the results
count the longest streak in phrases and each instrument's phrases.

**The DTA** in every image (`(game (player (health (init 0.6) (failure
-0.04 .. -0.07) (capture 0.06)) (max_streak 8) (scoring (gem_value 25)
(max_bar_value 500) ...)))`, `(beatmatcher (phrases (finder (bar 2))
(capturer (abp 2 2))))`, per song `(max_points ...)` / `(min_points ...)`)
is largely inherited from Harmonix's earlier games; gd24 takes its numbers
where the code's are missing [I]. `max_points` is about 1,000 x the song's
gem count on all four tracks (760..1,520), far above our perfect scores
(about 1/11): the real point values are larger or the formula differs [I].
