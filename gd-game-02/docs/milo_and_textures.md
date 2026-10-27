# Guitar Hero II (Xbox 360): scene files and textures

First look at the `.milo_xbox` scene containers, the `.bmp_xbox` / `.png_xbox`
textures and the `.fx_xbox` shaders, from the files that `tools/ark.py`
extracts. Everything here comes from inspecting the data; nothing has been
confirmed against the game executable yet. Where a field's meaning is a guess,
it says so.

Tools (Python 3, standard library only):

- `tools/milo.py list <file>` lists class, version and size of every object;
  `unpack <file> <dir>` writes the decompressed stream and one blob per
  object; `scan <dir>` parses every milo under a folder and prints totals.
- `tools/tex.py info|png|milo|all` reads texture headers and converts
  textures (loose files or the Tex objects inside a milo) to PNG.

PNG output goes under `gd-game-02/extracted/png/`, which git ignores.

## Byte order

The 360 is big-endian, but the Harmonix serialisation in these files is
**little-endian** throughout: the container, the directory, object fields,
floats, and the texture header. The only big-endian part is the texture pixel
data, which is stored in the GPU's 16-bit byte order (see Textures).

## Container (`.milo_xbox`)

All 503 files use the same block-compressed wrapper as GH1's PS2 files:

| offset | type | meaning |
|---|---|---|
| 0 | u32 | magic `0xCBBEDEAF` (bytes `AF DE BE CB`) |
| 4 | u32 | header size: where the first block starts (`0x810` in every file) |
| 8 | u32 | block count |
| 12 | u32 | size of the largest block once inflated (checked: always matches) |
| 16 | u32 × count | compressed size of each block, then zero padding up to the header size |

Each block is a raw deflate stream with no zlib header or checksum (inflate
with window bits -15). Inflated blocks are concatenated to give the directory
stream. Blocks are at most about 340 KB inflated. All 503 files inflate without
error and their blocks end exactly at end of file. `milo.py` also accepts
`0xCABEDEAF` (stored blocks) for safety, but GH2 does not use it.

## Directory stream

Every file is **version 25** (GH1 was 10). Strings are a u32 length followed by
that many bytes, with no terminator.

```
u32    version                     25
string directory class             e.g. PanelDir, Character, WorldDir
string directory name              e.g. "track", "crowd_male01"
u32    string-table count          looks like an allocation hint (name count)
u32    string-table size           looks like an allocation hint (bytes)
u32    object count
       per object: string class, string name
<directory object's own data>  AD DE AD DE
<object 0 data>                AD DE AD DE
...                            one record per table entry, in table order
```

Compared with GH1 v10, the new parts are the directory's class and name, the
two string-table hints, and the directory's own serialised data as the first
record. GH1's list of external file paths (after the object table) is gone.

Records are still terminated by the `0xADDEADDE` marker, so they can be split
without knowing each class's layout. In 490 of the 503 files the marker count
is exactly object count + 1.

### Inline sub-directories

The other 13 files (`char/crowd/ng/gen/crowd_*.milo_xbox`) contain an `RndDir`
object, `lighter_flame`, whose contents are stored inline. Inside that
object's data, after its proxy path `../lighter_flame.milo`, there is a
complete directory header: version 25, class `RndDir`, name, the two hints,
and a 5-entry object table. Next come the sub-directory's own record, then its
5 objects (Tex, Mat, Mesh, AnimFilter, MatAnim), each with its own marker, and
finally the outer object's remaining data and marker. `milo.py` finds these
headers by looking for a valid version-25 header inside a record and parses
them recursively. After that, every file parses with no bytes left over.
Other `RndDir` objects (110 in total) only reference external `.milo` proxies.

### Common object header (new in GH2)

Each object's data starts with its class version (a u32, as in GH1). In GH2
this is followed by a header that GH1 does not have. For most objects it is
13 bytes: `u32 1`, `u32 0`, `u32 0`, `u8 0`. Some objects carry a non-empty
string there. CamShot uses a `band...` value and OutfitLoader uses `guitar`,
so this looks like a per-object *type* string followed by optional property
data (Harmonix's `Hmx::Object` type and props). That part is inferred. Every
class decoder for GH2 has to skip this header before reading the GH1-style
fields.

## Class inventory (all 503 files)

There is one data version per class (the u32 at the start of each record).
"Objects" counts entries across all files, including those in inline
sub-directories. "Files" is the number of milos that contain the class.

Top-level directory classes: PanelDir 146, Character 107, RndDir 97,
CharClipSet 82, WorldDir 24, BandCharacter 19, ObjectDir 18, and one or two
each of BandCrowdMeterDir, BandScoreDisplay, BandStarMeterDir,
BandStreakDisplay, BandLeadMeter and GHTrackDir. Directory record versions:
PanelDir 2, RndDir 9, Character 10, CharClipSet 16, WorldDir 12, ObjectDir 17,
BandCharacter 1.

Rendering:

| class | objects | files | ver | GH1 ver |
|---|---:|---:|---:|---:|
| Mesh | 9974 | 324 | 28 | 25 |
| Trans | 6677 | 130 | 9 | 8 (as part of other classes) |
| Tex | 3531 | 345 | 10 | 8 |
| Mat | 3111 | 364 | 28 | 21 |
| Group | 1521 | 288 | 12 | (View 7) |
| Environ | 207 | 69 | 5 | 1 |
| Light | 198 | 53 | 9 | 3 |
| Spotlight | 341 | 9 | 25 | - |
| LightPreset | 174 | 8 | 9 | - |
| ParticleSys | 272 | 30 | 30 | 22 |
| Text | 111 | 53 | 17 | 15 |
| Cam | 51 | 39 | 12 | 9 |
| Font | 40 | 36 | 15 | 7 |
| CubeTex | 60 | 60 | 1 | - |
| PostProc | 28 | 24 | 9 | - |
| Flare | 23 | 2 | 4 | 3 |
| Set | 50 | 5 | 0 | |
| ScreenMask | 9 | 9 | 2 | |
| Screenshot | 1 | 1 | 1 | |

Animation:

| class | objects | ver |
|---|---:|---:|
| TransAnim | 1050 | 6 (GH1 4) |
| AnimFilter | 819 | 1 |
| CamShot | 440 | 27 |
| MatAnim | 409 | 7 (GH1 5) |
| ParticleSysAnim | 125 | 3 (GH1 2) |
| MeshAnim | 41 | 1 (GH1 0) |
| EnvAnim | 34 | 4 (GH1 3) |
| LightAnim | 9 | 2 (GH1 1) |
| CamAnim | 2 | 2 (GH1 0) |

Characters: CharBone 5797 (v3), CharClipSamples 1994 (v11), CharClipGroup 330
(v1), CharHair 92 (v2), CharUpperTwist 88, CharClipFilter 82, CharForeTwist 79,
Character 85 (as objects inside venue `*_chars` milos), CharDriver 58 (v5),
CharServoBone 57, CharDriverMidi 38, CharWeightSetter 38, CharIKHand 38,
CharLookAt 36, FaceFxLipSyncServo 22, OutfitLoader 20, CharIKMidi 19, CharWalk
19, CharEyes 18, BandCharacter 16, WorldCrowd 15 (v6), Waypoint 68, BandPlacer
10, CharPosConstraint 4, CharIKRod 4.

Audio / events: Sfx 346 (v5), SynthSample 313 (v5), SfxSeq 180, RandomGroupSeq
39, WaitSeq 8, SerialGroupSeq 8, FxSendReverb 3, EventTrigger 344 (v10).

UI / HUD: BandLabel 443 (v12), BandButton 197 (v10), UITrigger 13, CheckBox 11,
UIList 10, BandSlider 9, UIProxy 6, BandTextEntry 6, UILabel 4, UIPicture 2,
TrackWidget 27 (v4), plus the HUD directory classes listed above.

Run `python3 -I tools/milo.py scan extracted/ark` for the full table with
versions.

## Textures

### Bitmap format

Each `.bmp_xbox` / `.png_xbox` file is a single bitmap, and so is the tail of
every Tex record. The header is 32 bytes, little-endian, with the same layout
as GH1's version-1 bitmap header:

| offset | type | meaning |
|---|---|---|
| 0 | u8 | version (1) |
| 1 | u8 | bits per pixel: 4 (DXT1) or 8 (DXT5, ATI2) |
| 2 | u32 | encoding: **8 = DXT1/BC1**, **24 = DXT5/BC3**, **32 = ATI2/BC5** (two-channel normal maps) |
| 6 | u8 | number of *extra* mip levels (0 = full size only) |
| 7 | u16 | width |
| 9 | u16 | height |
| 11 | u16 | bytes per line (width × bpp / 8) |
| 13 | 19 bytes | zero |

Mip levels follow, largest first. Each level is packed 4×4 blocks in plain
row-major order, at its exact size, with no padding between levels. This was
checked on all 3533 loose files (1971 DXT1, 1232 DXT5, 330 ATI2): header size
plus the sum of the mip sizes equals the file size every time.

**Xbox 360 specifics.** The block data is **16-bit byte-swapped**: swap every
pair of bytes to get the PC DXT layout. The blocks are **not tiled** (no
`XGAddress2DTiled*` swizzle and no 32-block alignment). The decoded images
come out correct with only the 16-bit swap. This was checked visually on UI
art (a venue "locked" card, a 1024² career map), HUD art (rock meter), track
gems, venue textures (speaker atlas, stone venue) and an ATI2 face normal map.
For ATI2, `tex.py` writes X and Y to red and green and rebuilds Z into blue so
the result looks like a normal map. Which BC5 channel is X and which is Y
still needs checking in the shader.

Dimensions seen: powers of two from 8 up to 1024 (most often 512², 256²,
128²); one texture has 8 mips (7 extra).

GH1's PS2 bitmaps were paletted 4/8-bit with a CLUT and no compression. GH2 has
no palettes at all, and every texture is block-compressed.

### Tex objects (version 10)

```
u32 version (10)
13-byte common object header
u32 width, u32 height, u32 bpp
string source path            e.g. "../textures/gem_green.bmp"
f32                           -8 usually (0, -0.5, -7 also seen); GH1 called it mipMapK
u32 type                      1 normal; 0x22 for empty render targets (inferred)
u8  ?                         0 or 1
bitmap (header above + mips)
```

Apart from the object header, this is GH1's Tex v8 layout. **Every Tex in the
data carries its bitmap inline.** Of the 3505 top-level Tex objects with a non-empty bitmap, 3499 are
byte-identical to a loose `gen/*.bmp_xbox` / `*.png_xbox` file, so the loose
files look like build outputs or runtime-loaded copies of the same data. Thirteen
Tex objects are placeholders with a 0×0 bitmap header. Six are the track fade
render targets (type `0x22`, e.g. `track_fade.tex` 128×32, whose placeholder
header says bpp 32, encoding 1). The other seven have an empty DXT5 header
(e.g. `winner_photo1.tex` in the multiplayer UI and `drums_camo.tex`), and are
probably filled at runtime.

The 164 plain `.bmp` / `.png` files (`track/surfaces`, `ui/image`) are ordinary
Windows BMP and PNG images. They are probably sources for textures built at
runtime (`*_keep`).

## Mat v28 and Mesh v28: first look

### Mesh (v28 vs GH1 v25)

The structure is the same as GH1. All 9961 top-level Mesh records parse
exactly to the end with this layout:

```
u32 version 28, 13-byte object header
Trans v9:  12 f32 local, 12 f32 world (3x3 rows + position, as GH1),
           u32 constraint, string target, u8 preserve-scale, string parent
           (GH1 v8 had a child list before the constraint; v9 has none)
Draw v3:   u32 version, u8 showing, 4 f32 bounding sphere, 4 more bytes (0 so far;
           GH1's draw list is gone)
string material, string geometry owner, u32 mutable, u32 volume, u8 bsp (0)
u32 vertex count, 48-byte vertices: pos xyz, normal xyz, 4 floats
           (vertex colour RGBA, or bone weights when skinned), uv
u32 face count, faces of 3 x u16
u32 group count, u8 faces per group
string bone 0; if set: 3 more bone names + 4 x (3x4) matrices (still 4 bones max)
```

The PS2-only per-group strip data that GH1 sometimes appended is never
present.

### Mat (v28 vs GH1 v21)

GH1's material was a list of *stages*: texture, blend, texgen and transform
per pass. GH2's is a single flat record with dedicated texture slots for a
per-pixel lighting pipeline. In order (field names are from looking at the
data plus the shader constants):

- object header, u32 blend, RGBA colour (4 f32)
- a few flag bytes and u32s: probably use-environ, pre-lit, z-mode,
  alpha cut/write, texgen and wrap (the exact order is still open)
- 12 f32 texture transform
- string diffuse texture, string next pass (empty so far)
- two bytes (probably intensify and cull), f32 emissive multiplier (1.0),
  RGB specular colour, f32 specular power (10.0 is common)
- string normal map, string emissive/luminance map (e.g.
  `st_hanglamp01_luminance.tex`), string specular map, string environment map
- a few trailing u32/u8 flags

Records are 150 to 250 bytes. A full decoder needs the load routine from the
executable, or careful field-by-field checking across the 3111 records.

## `.fx_xbox`: compiled shaders

There are 2564 files, all in `system/run/shaders/gen/`. They are **compiled
Xbox 360 GPU shaders**, 1282 vertex and 1282 pixel, as pairs. The magic is
`0x102A1101` for vertex shaders and `0x102A1100` for pixel shaders (big-endian
here). Each file has a D3DX-style constant table naming the uniforms:
`gViewProj`, `gModel0..3`, `gDiffuseMap`, `gNormalMap`, `gSpecularMap`,
`gEnvironMap`, `gGlowMap`, `gShadowMap`, `gLight`/`gLightCol`, `gEnvAmbient`,
`gFogColor`/`gFogRange`, `gFadeColor`/`gFadeRange`, `gFocalBlur*`, `gBloomIntensity`
and others.

File names are `<effect>_<hex key>_<vs|ps>4314.fx_xbox`. The effects are
`standard` (2444 files), `shadowmap`, `multimesh`, `particles`, `postprocess`,
`blur`, `line`, `downsample*` and `bloom`. The hex key appears to be a
permutation bitmask: a material's features choose one precompiled variant.

The plain-text HLSL sources ship next to them in `system/run/shaders/*.fx`
(`common`, `lighting`, `standard_vs`/`standard_ps`, `postprocess`, `shadowfactor`
and others). The `standard` shader is built from preprocessor switches:
number of bone weights, normal, specular, glow, environment and diffuse maps,
per-pixel lighting, lights and projected lights, shadow buffer, fog, fade-out
mode, pre-lit / use-environ, texgen, HDR. Those switches map directly onto the
GH2 Mat fields. For the PC port these sources describe exactly what each
material should look like. They are game data, so they stay out of the repo,
and our shaders must be written independently.

## GH1 (PS2) vs GH2 (Xbox 360) at a glance

| | GH1 | GH2 |
|---|---|---|
| container | `0xCBBEDEAF` deflate blocks | same |
| byte order | little-endian | little-endian (except 16-bit-swapped texels) |
| directory version | 10 | 25 |
| directory header | object table + external paths | class + name + string hints + object table; dir's own record first |
| per-object header | none | ~13-byte object header (type string + props) |
| sub-directories | external files | external proxies, or inline (crowd lighters) |
| textures | paletted 4/8 bpp, PS2 | DXT1 / DXT5 / ATI2, linear, 16-bit swapped |
| Tex | v8 | v10 (same fields) |
| Mat | v21, stage list | v28, flat: diffuse/normal/specular/emissive/env maps + specular |
| Mesh | v25, 48-byte verts, 4 bones | v28, same geometry layout; Trans v9 / Draw v3 trimmed |
| shading | fixed-function GS stages | HLSL permutations (`.fx_xbox`) |
| new families | | Spotlight, LightPreset, PostProc, CubeTex, CamShot-based directing, Char* rig/IK classes, Band* UI |

## Open questions

- The exact meaning of the 13-byte object header (type string + property
  tree?) and which classes carry non-empty ones.
- Mat v28 field order in the flag area before the texture transform, and the
  trailing flags.
- Draw v3: what the last 4 bytes are (draw order?).
- The Tex `f32` (−8 is typical) and the `u8` after the type.
- How the `.fx_xbox` permutation key bits map to the shader switches (needs
  the executable or a brute-force comparison of constant tables).
- Which BC5 channel holds X and which holds Y in ATI2 normal maps.
- Inline sub-directory detection is heuristic (it looks for a valid header);
  it would be better to read the ObjectDir fields that announce it (proxy path
  and inline flag) once the ObjectDir layout is known.

## Confirmed in the executable (gd02's C++ loader, `engine/src/scene/`)

Each class's Load was found through its class-name string → ClassName
virtual → vtable (Load is slot 9, PreLoad 13, PostLoad 14). `gd02
--scenetest` decodes every typed object of all 503 milos to its end marker.

- **Hmx::Object** `0x82315350`: u32 rev, symbol type, u8 has-props + DataArray,
  a note string when rev > 0. The common 13 bytes = rev 1, empty type, no
  props, empty note (CamShot: `band…`; HUD dirs: `gh2`).
- **Trans v9** `0x821b0a28`: 12 f32 local, 12 f32 world, u32 constraint,
  target, u8 preserve-scale, parent.
- **Draw v3** `0x821b36c0`: showing, sphere (4 f32), draw_order.
- **Animatable v4** `0x821e96c0`: two u32 (the second is `rate`).
- **Tex v10** (PreLoad `0x821d62d0`, PostLoad `0x821d6438`; bitmap
  `0x821efd90` / `0x821f1f38`): each further mip is (w·bpp/8)·h bytes at half
  size (levels under 4×4 are shorter than a block).
- **Mat v28** `0x821eebb8` (names `0x821ed930`), load order: blend, color,
  use_environ, prelit, z_mode, alpha_cut, alpha_write, tex_gen, tex_wrap,
  tex_xfm, diffuse_tex, next_pass, intensify, cull, emissive_multiplier,
  specular_rgb + power, normal_map, emissive_map, specular_map,
  custom_diffuse_map, environ_map, per_pixel_lit, normalize, stencil_mode.
- **Mesh v28** `0x821c2b60`: Object, Trans, Draw, mat, geom owner, mutable,
  volume, BSP tree (`0x8226ab80`), 48-byte vertices, u16×3 faces, u8 group
  sizes, bone 0 and (when set) 3 more bones + 4 transforms
  (`0x821c3278`–`0x821c33f4`).
- **Group v12** `0x821dc6d0`; **Cam v12** `0x821d8c38` (near, far, y_fov, …);
  **Environ v5** `0x821d9f68`; **Light v9** `0x8220c3e8`.
- **TransAnim v6** `0x821b6230`, **MatAnim v7** `0x82204590`, **AnimFilter v1**
  `0x82207d88`, **ParticleSys v30** `0x82216ad0` (GH1 v22's fields, Object
  block read twice), **ParticleSysAnim v3**, **MeshAnim v1**, **Font v15**,
  **Text v17**, **TrackWidget v4** `0x82367260`.
- **ObjectDir v17** (`0x82322598` / `0x8231ff08`): viewports, proxy file,
  subdirs, inline-proxy flag (1 or 2 → inline sub-directory). RndDir v9,
  PanelDir v2, **TrackDir v5** (y_per_second, top_y, bottom_y, slots,
  draw_group, anim_group), **GHTrackDir v3** (cameras, gem / hopo / star /
  star-hopo widget lists), Band* HUD dirs (values kept in load order).
- Proxies resolve through the engine's og/ → ng/ path swap (`0x821c9480`).

### The highway and HUD scenes

- **Track:** `ui/track_panel.dta` → `track/track_panel_1p.milo` (`_2p` holds
  two) → GHTrackDir proxies to `track/gen/track_new.milo_xbox` (571 objects):
  cameras `track.cam` / `track_p1.cam` / `track_p2.cam` (near 50, far 250,
  y_fov 0.41), y_per_second 60, top_y 120, bottom_y −10, slots at x = −8, −4,
  0, 4, 8. Surface `track_surface.grp`; gems `<color>_gem.wid`, `_hopo`,
  `_star`, `_star_hopo`, bonus (Star Power), miss; sustain tails are built
  by code (tail_* materials); `gem_smasher.view`; hit effects
  `smash_normal/star/bonus/combo.view`; beat lines `bar_widgets.grp`; Star
  Power surface and lightning; `track_explode.view` (fail).
- **HUD:** `modes.dta hud_file` (hud_sp / hud_vs / hud_coop / hud_practice /
  hud_blank) via `ui/hud_panel.dta`. `hud_sp`: meters_slide_in.view,
  hud1_score_meter0.view, hud1_rock_meter.view, and proxies to
  `streak_display.milo` (multiplier, streak lights), `score_display.milo`
  (digits), `star_meter.milo` (the amp tube), `crowd_meter.milo` (rock meter
  needle and lights).

Open: Animatable's first u32; Cam's last three fields; Band* field names;
enum meanings (Mat blend, z_mode, tex_gen, Light type, Trans constraint);
spline TransAnim interpolation (sampled linearly for now).
