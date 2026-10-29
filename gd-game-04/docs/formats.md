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
| `DATA/ANIMS/standardkey{Q,T}.bin.xen`, `DATA/FXFILES/MaterialLibrary.bin.xen` | loose binaries (M5 / M6; the material library: "Material library" below) |

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

**LZSS** (scripts): a 4 KiB ring filled with spaces (0x20), writes from 4078; a flag
byte per 8 tokens, LSB first, 1 = literal; a reference is 2 bytes, position
`b0 | (b1 & 0xF0) << 4`, length `(b1 & 0x0F) + 3` [C data: every script
unpacks to exactly its size; XEX 0x823071B0 / 0x823072D0, Ghidra: the
ring's space fill; with zeros 1079 scripts decode wrongly].

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

## Textures (.img, .tex) — `tools/img.py`, `engine/src/data/texture.cpp`

[C data: 1,117 files, 5,747 textures decode, every byte accounted for.]
Loaders: 0x82415FD8 (.img fix-up), 0x82382970 / 0x82382D88 (.tex),
0x82553C40 (resource base / mip addresses), type classes 0x821881E8 [C].

- **Image header** (0x28 bytes): u16 0x0A28, u8 flags, u8 class, u32 name
  key, u16 w / h / depth (twice), u8 levels, u8 bits per texel, u8
  compression (0 none, 1 DXT1, 2 DXT3 (also on 22 DXT1 textures), 5 DXT5, 6
  DXN), u8 0, then offsets: +0x18 mips, +0x1C D3D header (0x34 bytes ending
  in the Xenos fetch constant), +0x20 size, +0x24 base.
- **Fetch constant**: format (0x06 8_8_8_8, 0x12 DXT1, 0x13 DXT3, 0x14
  DXT5, 0x1F 16_16_FLOAT, 0x31 DXN), endian swap (8in16 blocks, 8in32
  texels), tiled bit, pitch, dimension (2D / 3D / cube), max mip, packed-mip
  address. 8_8_8_8 is stored A,R,G,B with a ZYXW swizzle (B and R swap).
- **.img**: data from 0x1000 (`(img + 0x105B) & ~0xFFF`), mips at 0x1000 +
  the mip address. **.tex**: 0xFACECAA7, u8 1, u8 0x1C, u16 count, table
  offset, runtime area, -1, hash bits, hash offset; image headers, then
  their D3D headers; base / mip offsets are file offsets (textures share
  each other's tile padding).
- **Tiling**: Xenos 2D tiled addressing (32x32-block macro tiles, bank /
  pipe interleave); levels >= 1 are npot(w) >> i; the packed mip tail starts
  at the first level with min(w, h) <= 16 texels, laid out at (0, h) or
  (w, 0) in a tile of the base pitch [C data].
- **Volume (3D) tiling** (GD04-014): the one volume on the disc
  (`global_gfx.tex` #49, key 0x2E2E1ADF, DXT5 128x128x32, one level,
  class byte 5, 0x80000 bytes = 32x32 blocks x 32 slices x 16) is laid out
  by XGAddress3DTiledOffset, as Xenia writes it (`texture_address.h`
  Tiled3D, github.com/xenia-project/xenia at 95a5c3e): pitch and height
  aligned to 32 blocks, depth to 4; macro tiles of 32x16x4 blocks in x, y, z
  order (`outer = ((z>>2) * (H>>4) + (y>>4)) * (P>>5) + (x>>5)`, x128
  blocks); inside, `inner = (z&3)<<5 | ((y>>1)&3)<<3 | (x&7)`; bank = (y>>3
  ^ z>>2) & 1, pipe = ((x>>3)&3) ^ bank<<1; the address interleave as in 2D
  (bits 0-3 / 5 / 8-10 / 12+ from outer|inner scaled by the block size, y&1
  at bit 4, pipe at 6-7, bank at 11). [C data: every slice decodes
  seamless (block, micro- and macro-tile edges as smooth as the insides,
  the 2D slice-by-slice guess is not); the slice order is the formula's
  (the 32 slices are independent noise layers, no continuity in z to check
  it against).] tools/img.py and texture.cpp decode it (FNV-1a of the slices
  fe4c4c91 in both: `img.py check`, `gd04 --formattest`); the device has no
  volume kind, so it stays unsupported for upload.
- **What uses it**: only material 0xD2BFD967 of `global_gfx.scn` (template
  `VolumeMap`: the pixel shader `tfetch3D`s the volume at the interpolated
  position and writes its alpha as grey). No mesh, no particle system, no
  QB script and no XEX constant names that material or the texture key
  [C data: a search of every pak and the decompiled XEX], so nothing draws
  it at runtime: a leftover test material.
- Counts: DXT1 2874, DXT5 1553, DXN 1181, 8_8_8_8 cubes 134, DXT3 2,
  8_8_8_8 1, 16_16_FLOAT 1, DXT5 volume 1. **Gaps**: cube 1x1 levels are box-
  filtered from 2x2 [I]; the class byte's meaning [I].

## Scenes, models, skins (.scn, .mdl, .skin), collision (.hkc, .rag) — `tools/scn.py`, `engine/src/data/scene.cpp`

[C data: 419 geometry files, 0 problems.] One format for the three: a file
header (offset to the skin trailer, 0xFAAABACA x7), a material section
(materials with their key, shader template key, two vec4 constant arrays
and texture keys resolved in the parent .tex; walker 0x822B27E8), then the
scene section after 0xBABEFACE (loader 0x822D7E78, relocation 0x822D6D48):
a 0x90 header (bounds, mesh order, counts), sectors (0x60), geoms (0x60),
meshes (0x80: material, vertex flags, index / vertex counts, triangle strips
with 0x7FFF restarts, vertex and skin blocks). Vertex declarations from the
mesh flags as 0x823B9A28 builds them (positions, HEND3N normals / tangents,
colours, blend weights / indices, texcoord sets as FLOAT or FLOAT16).
Skins: groups of 1-3 bones, 32-byte vertices with two u16 weights.
.hkc / .rag: Havok 4.6.0-r1 packfiles, decoded (next section).

## Havok packfiles (.hkc, .rag) — `tools/havok.py`, `engine/src/data/havok.cpp`

[C data: 24 .hkc + 27 .rag, every object reached, every pointer fixup read
by a reflected member, 0 problems (`havok.py check`, `gd04 --formattest`).]
Loader strings in the XEX: hkPackfileReader (Havok 4.6.0-r1, also
accepts 3.0.0 / 3.1.0 / 4.0.0-b1), `levels\%s\%s%s.hkc.%s` (0x8226B728).

- **Header** (0x40): magic 57E0E057 10C0C010, u32 0x786E6B68 (user tag), u32
  4 (version), layout rules 04 00 00 01 (4-byte pointers, big-endian, no
  padding / empty base), u32 3 sections, contents section 1 / offset,
  contents class name section 0 / offset, "Havok-4.6.0-r1" padded with FF,
  two u32 -1. Then a 0x30-byte record per section: 19-byte name + FF, u32
  start, then six offsets from it: data size (= local fixups), global
  fixups, virtual fixups, exports, imports, end (exports / imports empty).
- **Fixups**: local (u32 src, u32 dst: a pointer to the same section),
  global (src, dst section, dst), virtual (src, 0, class name offset: an
  object of that class starts at src); tables padded with -1 to 16 bytes.
  Pointers are stored as 0 and set from these.
- **`__classnames__`**: {u32 signature, u8 9, name, NUL} entries, FF padded.
- **`__types__`** (the reflection, 37-68 classes per file): hkClass objects
  (0x30: name, parent, u32 object size, u32 interfaces, enums, s32 enum
  count, members, s32 member count, defaults (null on the disc), 3 words 0)
  with their hkClassMember arrays (0x14: name, class, enum, u8 type, u8
  subtype, s16 C-array size, u16 flags, u16 offset; enum storage in the flags
  ENUM_8 / 16 / 32 = 8 / 16 / 32) and hkClassEnum records (0x0C: name,
  items {s32 value, name}, count). Types: Havok 4.6's hkClassMember::Type
  (REAL, VECTOR4, TRANSFORM, POINTER, ARRAY = {ptr, s32 size, u32 capacity |
  0x80000000}, SIMPLEARRAY = {ptr, s32 size}, STRUCT, ENUM, CSTRING, ZERO =
  runtime-only, ...). A member's offset is absolute in its class; a class's
  members follow its parent's.
- **`__data__`**: the objects, laid out by those classes; the contents is
  an hkPhysicsData {worldCinfo, systems[]}; each hkPhysicsSystem holds
  rigidBodies[], constraints[], actions[], phantoms[] (the last two empty).
- **.rag** (27, `global.pak`: ragdoll_ped, ragdoll_<character>[_2],
  ragdoll_male_singer / female_singer / bassist / drummer,
  Ragdoll_DevilGirl): one system of 15 (ped), 10 (DevilGirl) or 35 rigid
  bodies named after the skeleton's bones (Bone_Pelvis ... Bone_Ankle_R and
  15 Bone_ACC_01..15 accessory bones), each an hkRigidBody (mass from
  inertiaAndMassInv.w, MOTION_BOX / SPHERE_INERTIA, friction / restitution,
  damping, motionState.transform = the bone's bind pose) with an
  hkCapsuleShape or an hkConvexTranslateShape around an hkBoxShape (the
  accessory bones: thin boxes); one constraint per body to its parent bone,
  hkRagdollConstraintData (cone / twist / plane limits, the pivots in the
  local transforms) or hkLimitedHingeConstraintData (forearms, knees), 873
  in all; an hkSpatialRigidBodyDeactivator on 805 of the 903 bodies.
- **.hkc** (24, every zone: `<zone>.hkc` and `<zone>_gfx.hkc`): 21 are an
  empty world (two systems, no bodies); z_budokan_gfx, z_dive_gfx and
  z_testlevel_peds hold one MOTION_FIXED body with an hkMoppBvTreeShape over
  an hkNxStorageExtendedMeshShape (3 subparts of 2 triangles: 28 triangles
  on the whole disc, a few floor quads).
- **Use** [C scripts]: `create_guitarist` / the band's composite objects
  get a `ragdoll` component (ragdollname = the profile's `ragdoll` /
  `ragdoll2` for outfit 2, ragdollcollisiongroup =
  RagdollCollisionGroups_<character>), the anim trees put a `ragdoll` node
  over the animation (guitarist_static_tree), and `ragdoll_setaccessorybones
  accessory_bones = $Guitarist_accessory_bones` (Bone_ACC_01..15;
  DevilGirl_accessory_bones 03..06) makes those bones simulated: the hair,
  coat tails and chains swing with Havok while the body bones follow the
  animation (keyframed) as their collision; ragdoll_markforreset after
  teleports. Ragdoll_budget = 20 ms [C data]. The .hkc collision has no
  gameplay or camera use found (the cameras are scripted, docs/venues.md).
- **The port**: decoded and checked, not simulated. Without them the
  Bone_ACC bones only follow the animation clips, which is the visible
  difference: no secondary motion of hair / accessories. A simulation (a small verlet / PBD chain per
  accessory bone with the cone limits and the capsule / box bodies as
  colliders) is enough to match it; the .hkc can be ignored.

## Material library (`DATA/FXFILES/MaterialLibrary.bin.xen`) — `tools/matlib.py`, `engine/src/data/matlib.cpp`

[C data: 418 templates, 4,981 shaders, 0 problems; the loader is
Function_82383768, called with "fxfiles\\MaterialLibrary.bin.xen" by
Function_82384668.] A compressed copy is under `DATA/COMPRESSED/FXFILES`.

- Header: "MATL", u32 2, u32 count (418), u32 0, u32 string base, u32 0,
  u32 offsets[count]; then B = the template records, 0x28 bytes each:
  u32 0, u32 name (offset from B), u32 key = qbkey(name) (the key a
  scene material holds at +0xa0), then the relocated effect (techniques of
  0x1b8 bytes, passes of 0xa8 with their render-state and sampler-state
  lists (sampler, state, value), parameters with their annotations: UIName,
  UIMin, ParamID, FlagValue, EntryVertexShader, EntryPixelShader). The
  effect is read by Function_823A5768 (material constants by ranges,
  render states through the device's table, sampler states) [C]; gd04
  does not decode it yet (render states come from the scene material).
- The shaders: XDK-compiled Xenos microcode, magic 0x102A1101 (vertex) /
  0x102A1100 (pixel), one pair per technique (Default, DepthOnly, AlphaOnly,
  DynamicShadowReciever, ReducedLOD), named by their debug path
  `..._Intermediate\Xbox360\Updb\<template>_<technique>.updb`. Header words:
  +0x04 region offset, +0x08 region size (the next shader follows), +0x0c
  0x24, +0x10 the D3DXSHADER_CONSTANTTABLE's size word (the big-endian
  table follows: names, register set 0 bool / 2 float4 / 3 sampler,
  register, count), +0x14 the literal ranges or 0, +0x18 the shader info;
  +0x24 hash, +0x28 path length, path.
- Shader info: u32 offset of the microcode in the region (after the
  literal floats), u32 its size; vertex shaders: [7] fetch count, [8] patch
  count, from [10] one word per vertex fetch (instruction address | usage
  << 12 | usage index << 16; D3DDECLUSAGE), one per export (semantic byte
  | register << 8 | mask << 12 | first patch << 16) and the patch list (the
  ALU instructions writing each export); pixel shaders: from [8] one word
  per input register (semantic | register << 8 | mask << 12). A pixel input
  takes the vertex export of the same semantic (0x50 = TEXCOORD0) [C data:
  the pairs' tables agree].
- Literals: pairs (register << 16 | float count, byte offset in the
  region's prefix), register 252.. for vertex, 0x1fc (pixel bank 256+252)..
  for pixel shaders: c252..c255 hold the compiler's constants [C: e.g.
  D_3ChkBox_Pass0's pixel c255 = (1, -1, 0, 0)].
- Microcode: the Xenos format (control flow pairs of 48-bit instructions,
  then 96-bit ALU / fetch instructions), as the Xenia emulator documents it
  (ucode.h); decoded and translated to GLSL by `render/xenos_glsl.cpp`: 412
  of the 415 templates with a Default pair (3 water templates use loops;
  DepthOnly and DepthOnly_Fixed have no Default technique).
- A scene material feeds its template: constsA = vertex c0.., constsB =
  pixel c0.., textures = samplers s0.. in order [C data: the constant tables
  of the venue and character templates against their materials, e.g.
  D_N_S_1SeperateChnl: c0 m_psSpecularPower 64, c1 bumpiness 2, c2 colour,
  c3 specular colour; samplers normal, diffuse, spec, shadow map]. The
  engine's own constants are c128..c255 (one layout for both stages:
  g_World 216, g_WorldView 220, g_WorldViewProj 224, g_View 232,
  g_Projection 236, g_InvView 240, g_ViewProj 244, g_Time 155, lights
  176..191: g_ambientLightColor 176, g_dirLight0Dir / Color / SpecColor
  177..179, the spec colours of dirLight1..3 181 / 183 / 185, g_shLight0..3Color
  188..191 (L1 spherical harmonics: constant, x, y, z), shadow terms
  137..143) [C: the constant tables; Function_823A5768 and the matrix
  uploads at 0x823A3xxx (Function_824B6698 vertex / Function_824B6780
  pixel SetShaderConstantF in Function_823A6190 / 823A61F8 / 823A6320:
  c216 x12, c224, c220, c232, c240, c244)].

## Animation (.ska, .ske), fonts (.fnt), pak sounds (.wav) — `tools/anim.py`, `engine/src/data/{anim,font}.cpp`

[C data: 19,114 files, 0 problems.]
- **.ske**: u32 0x00010030, bone count, then tables of names, parents,
  mirror names / indices, flags, inverse-bind matrices, translations,
  rotations (script LoadSkeleton -> 0x8227C4E8).
- **.ska**: header at 0x20 (flags, duration s, bones, key counts, offsets of
  the Q and T data and per-bone sizes); times in frames at 60 fps. Q keys
  (0x824213B8): u16 W; three s16 / 16384 (or unsigned bytes per flag bits
  0x2000 / 0x1000 / 0x800), w = sqrt(1 - x^2 - y^2 - z^2). T keys
  (0x82421840): a byte with a short time, three f32 at the next 16-aligned
  file offset. Single-pose clips (flags 0x10000000) store plain floats; a
  bone mask when flags 0x80000. Compressed translations are offsets from the
  bone's bind translation, single-pose ones absolute; flags 0x200 marks a
  difference clip (the hands' `_D` anims) [C data; docs/band.md "Clips"].
- **.fnt**: vShift, bearing, tracking, line height; u16 charMap[65536];
  u16 buttonMap[32]; glyphs of 0x24 bytes; an embedded .img (class 2,
  linear) at the pointer at 0x200A0 (0x8232B458).
- **.wav**: RIFF/WAVE with XMA fmt (tag 0x165), 2048-byte packets and a
  seek chunk, read by FMOD's XMA codec (0x826F8FF8).
- **.pfx**: node-array prefix index. **.table**: one u32 0xE5F50254 [I].
