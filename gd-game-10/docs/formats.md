# Guitar Hero: Metallica (Xbox 360): file formats

The base is GH3's (gd-game-04 `docs/formats.md`): the same QB key hash,
`.pak.xen` entry table, QB sections and compiled QScript. This file lists
what changed in gh4 (Metallica), decoded byte-exact by `tools/` over the
whole disc (L21). [C] = confirmed on the disc data over every file (or at an
XEX address); [I] = inferred.

## Files under DATA/COMPRESSED

GH3 deflated every such file as one raw-deflate stream. Metallica stores each
one either **as is** (206 paks, 3 images, the TOC files) or as a chain of
**CHNK** blocks (807 paks, 3 pabs, 10 images, `MaterialLibrary.bin.xen`)
[C: every file on the disc]. No file is a whole raw-deflate stream any more.

### CHNK

Blocks follow each other; each is a 32-byte big-endian header, padding up to
the header size, then a raw-deflate stream (RFC 1951, no zlib header):

| Offset | Type | Field |
|---|---|---|
| 0 | char[4] | `CHNK` |
| 4 | u32 | header size (0x80: the stream starts here) |
| 8 | u32 | deflated size |
| 12 | u32 | block size (header + stream, padded; `0xffffffff` on the last block) |
| 16 | u32 | the next block's size (0 on the last block) |
| 20 | u32 | inflated size (0x80000 except on the last block) |
| 24 | u32 | inflated offset (where these bytes go in the file) |
| 28 | u32 | 0 |

The file is the concatenation of the inflated blocks [C: `pak.py check`, all
1,060 paks]. Readers: `tools/pak.py` `read_chnk`, `engine/src/data/disc.cpp`
`inflateChunks`.

### compress.toc.xen / compress.toc.lst.xen

`compress.toc.lst.xen` is text, one line per file: `Name: <path> <size>
<hex key>`. `compress.toc.xen` starts `TOC1`, then 16-byte entries whose first
u32 is the line's key and whose third is its size [I: not decoded further;
nothing reads it yet].

## Paks with a pab

Three paks have a `.pab.xen` (`qb`, `dbgq`, `zones/global`). As in GH3 the
entry offsets count through pak + pab as one buffer, but the `.pak.xen` is
padded on the disc to 32 KiB: the pab's bytes start at the header's real
size, which is the lowest data offset of its entries (4 KiB for `dbgq`, 8 KiB
for `global`, 32 KiB for `qb`; `compress.toc.lst` gives the same sizes)
[C: the three files; the XEX's own rule still to find, I].

## QB

- **Struct items**: GH3 wrote the item type as `0x00TT0000` with bit
  `0x800000`; Metallica writes `0x0001TT00`, the same shape as an array
  header [C: every .qb on the disc].
- **Type 0x1C**: a key into the localized string tables (`.qs.<lang>`),
  stored inline like a key (`lyrics` texts in the song charts) [C: data;
  name I].
- Result: `qb.py check` decodes all 1,544 .qb files to their last byte
  (38,466 sections, 8,853 scripts) [C].

## GZ pak entries

Some pak entries (.img 2,807 of 5,917, .tex 1,881, .skin 2,074, .ske 171,
.rag 172) start `GZ\0\0`, u32 inflated size (big-endian),
8 zero bytes, then a raw-deflate stream [C: every one inflates to its
size]. `pak.py` and `Pak` unwrap them at load.

## Fonts (.fnt)

GH3's layout with one more f32 after `tracking` (1.0 in every font [C data];
meaning I): every later field moves by 4 (glyph table at 0x200A8). The
embedded texture's magic is 0x0A281100 (GH3 0x0A281102).

## QS (localized strings)

PAK/qs.pak.xen (English; qs_f/g/i/s/n for the other languages) holds .qs
entries: UTF-16LE with a BOM, one line per string: 8 hex digits (the key),
a space, the text in double quotes [C data]. QB type 0x1C and the script
token 0x4E name a string by that key. Many strings start with `\L` (shown
without it, I).

## Script bytecode (gh4 additions)

Every script on the disc tokenizes with GH3's table plus [C data]:
0x4E a u32 localized-string key; 0x4F and 0x50 before a pair: a random
float / integer in that range (meanings I from their uses).

## Song audio (FSB4)

DATA/MUSIC/<song>_1 .. _3.fsb.xen: FSB4 (48-byte header; 80-byte sample
headers as FSB3 plus, for XMA, u32 0 x3, u32 size, u32 streams, u32
entries, the seek table), each one sample of interleaved stereo XMA streams
(8 / 6 / 4 channels), 48 kHz [C data]. Encrypted: plain = bit-reverse(stored)
XOR key, the key made from the bank's name by 0x820E24F8 [C] (see
data/fsb.cpp fsbKeyGh4). `<song>_preview.fsb.xen`: the same
generator on "<song>_preview" (0x820E45E8), one stereo XMA2 sample of
about 21 s (D81). streamall.fsb.xen is FSB4, not encrypted, 2,629 samples; its .dat as
GH3's.

XMA packets: gh4's FSB4 samples (the song banks and streamall) are XMA2
packets (the header's first 6 bits a frame count, 8 to 10), not GH3's XMA1
(a 4-bit sequence number); decoded as XMA1 they fail ("overflow in spectral
RLE", "frame would have to skip N bits") [C data].

## Sound effects (.wav in the sound paks)

`global_sfx.pak.xen` (1,017) and the zones' paks: RIFF/WAVE with the "data"
chunk first (XMA2 packets, 2048 bytes, 0xFF padding), then a big-endian
"XMA2" chunk (44 bytes: version 4, streams, reserved, loop count, loop
begin, loop end, rate at 12, encode options, bytes per second, block size,
samples encoded, samples in source, block count, then each stream's
channels / reserved / u16 mask from 40) and a "seek" chunk. No "fmt "
chunk (GH3's had XMAWAVEFORMAT there) [C data]. `--sfxtest` decodes every
one and fails on a silent one.

## Charts (.mid.qb)

Notes: (time, packed) int pairs, packed = flags << 16 | length (ms); flags
0x1f frets, 0x20 open, 0x40 forced, 0x0f80 per-lane bits, 0x2000 (drums)
[C data; meanings I]. Event arrays (*_notes: cameras, lightshow, crowd,
drums, anim): (time, packed) pairs, packed = velocity << 24 | note << 16 |
length. Star Power / tapping: (time, length, count) rows as GH3. Parts:
song, rhythm, drum, aux, guitarcoop, rhythmcoop; five difficulties in the
tuning tables (beginner .. expert).

## UI descs (ui\uidesc_<name>.qb)

See docs/ui.md.

## Scenes (.scn / .mdl / .skin)

GH3's file (0xFAAABACA header, material section, 0xBABEFACE, scene header,
sectors, geoms, meshes) with three changes [C: z_forum, z_forum_gfx,
Gem_Set_01 parse to their meshes; the engine reads them, data/scene.cpp]:
- **Materials**: 0xD0-byte headers (GH3 0x100): +0 key, +4 a second key
  (name, I), +0x70 template, +0x78/+0x7C, +0x80/+0x84, +0x88/+0x8C the two
  vec4 arrays and the texture keys (count, offset), +0x94 and +0xB4 the
  size; GH3's +0xC8..+0xF0 words at +0x98..+0xC0 [I].
- **Meshes**: 0x90-byte records: +0 sphere, +0x10 vertex block (from the
  scene base; 0x20-byte descriptor first), +0x1C its size, +0x24 vertex
  flags (GH3's declaration rule fits every stride), +0x2C index block
  (0x20-byte descriptor), +0x38 its size, +0x4C material, +0x5C indices << 16
  | vertices; the pointer table follows at 0x90 x meshes.
- **Indices**: triangle lists (GH3: strips with 0x7FFF restarts).
- **Skinned meshes** (vertex flag 0x80) [C: every .skin on the disc decodes,
  every character and instrument of DATA/PAK/CAS]: +0x10 holds stream 1
  (colour, the 0x4000 bytes, UVs; its stride in the low byte of +0x28),
  +0x7C (from the scene base, 0x1000-aligned, 0xEE fill before it) GH3's
  CPU-skinning block: 0xCAFEBAB4 x 4, u32 group counts (0, 1-bone,
  2-bone, 3-bone), each group u32 vertices, 4 bone bytes (skeleton
  indices), u32 vertices again, 0xFACEF000 (GH3: 0xFACEF000, 0xFACEF001),
  then its 32-byte vertices as GH3's (position, u32 weights w0 | w1 << 16,
  stream 0's packed normal / tangent / binormal, 0xBAADF00D). Stream 0 may
  be 16 or 24 bytes (vertex flags), always in a 32-byte vertex.
- **.skin trailer**: the scene ends at the first word + 0x20; then 20 bytes
  `02 14 00 FE`, u32 count, u32 0x14, two flag words [I], then (count != 0:
  the CAS bodies, faces and accessories) deform records, not decoded [C].

## Skeletons (.ske)

GH3's tables (names, parents, mirror names / indices, flags, inverse bind
matrices, translations, rotations) with: u32 at 4 = u16 file size, u16 bone
count; the table offsets at 16..44 as GH3's, the translations at 0x80, the
rotations at the next 0x80 boundary after them (zero fill), the rest packed
[C: 225 of 251 .ske; the 24 of global_rag_skel.pak start "LZSS" (packed,
not decoded); Face_deform and GH_Male_Deform fail GH3's mirror-name check].
The characters' skeletons have 128 bones; the global pak keeps them as
`<skeleton>_original` and `<skeleton>_Drummer` (the musician_body's
skeleton name plus the suffix) [C data]; every talent .skin uses bones < 112.

## Animations (.ska)

File header: u32 file size, u32 0x20, two words [I], -1, -1, the bone mask's
offset or -1, -1. Anim header at 0x20: u32 0x68 (GH3 0x48), u32 flags, f32
duration, u16 bones, u16 Q keys, 4 x vec4 (GH3 2), u16 T keys, u16 custom
keys (0 or 2) [I], u32 their offset or -1, then the offsets of the Q data
(0x100), T data, Q track sizes (u16 per bone), T track sizes; zeros to 0x100.
- Every non-empty track starts 16-aligned (0xDEAD fill), the sizes exclude it.
- Q key: u16 frame (60 Hz), 3 x s16 / 32768 (x, y, z; w = sqrt(1 - |xyz|^2)):
  GH3's uncompressed key; the 0x4000 compressed forms are not on the disc.
- T key: f32 frame, 3 x f32 (16 bytes).
- Flags 0x40: one pose (Q float quaternions, T vec4 with w 1; GH3 used
  0x10000000, set on almost every gh4 clip).
- Bone mask (flags 0x80000): u32 count, then the bits, as GH3's.
[C: every one of the 8,255 .ska on the disc parses to its last frame
(first key 0, last = duration x 60); names I]. A song's mocap clips
(`<Song>_G_01`, `_B_`, `_D_`, `_S_` = guitarist, bassist, drummer, singer;
`_cNN_` = 3-bone GH4_Camera clips) are played by `Band_PlayClip {clip
startframe}` in `<song>_performance`, the clip struct
(`<song>_song_scripts.qb`) naming each member's anim, start node and arms.

## gh4 character additions (2026-11-11, D60..D66)

- Skeletons: global.pak's talent .ske entries: full name `<skeleton>_original`,
  short name `<skeleton>`; the 24 of global_rag_skel are "LZSS", u32 size,
  Neversoft LZSS of the same bytes. Face_deform / GH_Male_Deform: one mirror
  index wrong (Bone_Cheek_Lower_L -> 19).
- Ragdolls (.rag, gh4): "LZSS" + u32 size + LZSS of a Havok-5.5.0-r1
  packfile (header version 5, contents section 2, empty __types__); layouts
  as D65.
- Animations: facial clips (perm_anims GH_Rocker_*Face*, song paks
  GHM_Singer_Male_<Song>_N) animate bones 63..92 with a bone mask; T keys are
  offsets from the bind (<= 1 cm), Q keys absolute.
- Textures: talent materials also use PAK/global_model_tex.tex (eyes, skin
  detail, environment maps).
- Camera anims: <loop>_c01 / _c02, <song>_<S|B|G|D>_cNN_MM and the venues'
  camera anims are 3-bone clips on GH4_Camera (ZONES/global.pak); bone_camera
  is the camera (looks down its +z, D43).

## Raw CAS paks (DATA/PAK/CAS)

Not compressed; GH3's 32-byte entry table; entry +16 = the key of the
full path (`c:/gh4_1_burn/data/models/talent/jameshetfield.skin.xen`),
+20 the key of the base name without extension (`jameshetfield`), +24 the
parent .tex's full key, flags 0x100. A part desc's `mesh` ('models/Talent/
JamesHetfield.skin') is found by its base name across `cas_archive_paks`
[C data; I: the engine's own lookup].

## Textures (.tex / .img)

GH3's dictionary and image headers; besides DXT1/3/5, DXN, 8_8_8_8 and
16_16_FLOAT, gh4 uses 8 (0x02), 1_5_5_5 (0x03) and 5_6_5 (0x04), 16-bit words with
the 8in16 swap [C: Z_Forum.tex].

## Material library (MaterialLibrary.bin.xen)

MATL version 3: a 0x10-byte header ("MATL", 3, count, the shader base),
count + 1 offsets, then GH3's v2 template records (name, key = the name's
checksum, techniques) and the XDK shaders (0x102A1100 / 0x102A1101) [C:
758 templates, 3,845 shaders; every template key checks]. GH3's template
names (D_3ChkBox_Pass0, AnimatedTexture, Billboard_FastLit, ...). The
vertex shaders' engine constants moved: g_PointLight c176 x 14, g_SpotLight
c192 x 20, g_pointLightGroup0Attenuation* / g_spotLightGroup0Attenuation*
c248..c253 (GH3: g_ambientLightColor c176 ...); g_World c216 and
g_WorldViewProj c224 stay [C data: D_3ChkBox_Pass0_Default's constant
table]. Fed GH3's way they draw black, so they are off for now.

## Not decoded yet (C++ `--formattest`, 2026-11-10)

`.ska` (8,255: decoded since), `.img` (2,548 "image version" + 259 short), `.wav` (2,466: decoded since, see above),
`.skin` (2,166: decoded since), `.tex` (1,891), `.ske` (251: 225 decoded since), `.rag` (200), `.hkc` (45),
`.scn` (46), `.table` (46), `.fnt` (15), `.mdl` (14); the FSB used by
`streamall.fsb.xen` (not FSB3). Each needs its gh4 layout.
