# gd03: disc and data formats

Guitar Hero Encore: Rocks the 80s (PS2, SLUS_215.86) is Guitar Hero II's
engine on the PS2. Its formats are gd-game-01's (GH1 PS2) containers with
gd-game-02's (GH2 Xbox 360) object model, one or a few revisions older. Each
entry: [C] confirmed (address in SLUS_215.86 = 0x00xxxxxx, or the GH2 360
executable = 0x82xxxxxx when the 360 Load reads the old revision; "data" = every
instance on the disc decodes exactly to its end), [I] inferred.

## Disc
- ISO 9660, volume id `GH2`, `SYSTEM.CNF` `BOOT2 = cdrom0:\SLUS_215.86;1` [C data].
- Root: `GEN/MAIN.HDR`, `GEN/MAIN_0.ARK` (one 1.84 GB part), `IOP/` (IRX
  modules), `VIDEOS/INTRO.PSS`, `SLUS_215.86`, `SYSTEM.CNF`.

## ARK (`GEN/MAIN.HDR` + `MAIN_0.ARK`)
- Version 3, stored plain (no cipher), the same layout as GH1's [C data]:
  u32 version, u32 part count, u32 size count + sizes, u32 string table size +
  table, u32 name count + offsets, u32 entry count + entries of five u32
  (offset, name index, dir index, size, inflated size). 1761 entries, none
  compressed. Paths may start `../../system/run/` (the engine's shared files).
- Code: `engine/src/data/game_data.cpp`, `data/iso9660.cpp`; tool `tools/ark.py`.

## .dtb scripts
- u32 seed, then every byte XORed with the low byte of the next word of a
  256-word table cipher seeded with the C rand() LCG; read indices start at 0
  and 0x67, wrap after 0xF8 [C 0x002d95b0]. The same as GH1. All 187 files
  decode (`tools/dtb.py dta`).
- Plaintext: the serialised DataArray of GH1 / GH2.
- Code: `engine/src/data/crypt.cpp`, `data/dta.cpp`.

## Milo scene files (`.milo_ps2`)
- Container: u32 0xCBBEDEAF, u32 header size (0x210 here; 0x810 on the 360),
  u32 block count, u32 largest block, block sizes; raw deflate blocks [C data].
- Directory stream version **24** (360: 25), same header layout [C data].
- A record's data can contain the end-marker bytes (a texture in
  `ui/gen/endgame`): the loader retries the next marker when a typed decoder
  fails (`scene/milo.cpp`).

### Class revisions (PS2 disc vs GH2 360)
| Class | PS2 | 360 | Difference |
|---|---|---|---|
| ObjectDir | 16 | 17 | Object header read in PreLoad (before the viewports), not PostLoad [C 360 0x82322598 + data] |
| RndDir | 8 | 9 | PostLoad adds three dropped strings after the environ (one for 3..8, two more for 4..8) [C 360 0x821ba4c8 + data] |
| WorldDir | 11 | 12 | PostLoad ends after the camera transform (no hides / bitmaps) [C 360 0x8224de60 + data] |
| WorldFx | 1 | - | Its revision word, then a RndDir (revision 8): a proxy (ObjectDir proxy file, e.g. `../../../shared/og/cheat_headflames.milo`, not inline) trans-parented to a bone; world_objects.dta: superclass RndDir [D; data: all 80 records on the disc end at their markers] |
| BandStarMeterDir | 4 | 5 | five names (the sixth group from 5) [C 360 0x821321a8 + data] |
| Mat | 27 | 28 | no stencil mode [C 360 0x821eebb8 + data] |
| Mesh | 28 | 28 | plus a per-group block: u32 n, u32 m, n u32, m u16 (the platform mesh form the 360 Load reads when it keeps one) [C data; 360 0x821c2b60] |
| Light | 6 | 9 | no spot radii, softness, displacement, texture, shadow casters [C 360 0x8220c3e8 + data] |
| ParticleSys | 27 | 30 | one object header; no spin / damping / drag; frame_drive on, pre_spawn off [C 360 0x82216ad0 + data] |

| Character | 9 | 10 | own record: LOD list stored directly, then shadow, self-shadow, sphere base; no CharacterTest block [C 360 0x82153cf8 / 0x82157678 + data] |
| BandCharacter | 1 | 1 | u32 1 then the Character v9 layout [C 360 0x82126048 + data] |
| CharBone | 2 | 3 | no weight (1.0) [C 360 0x8215fdf0 + data] |
| CharClipSamples | 10 | 11 | bone-set headers hold the name only (weight 1.0) [C 360 0x82162c68 + data] |
| CharClipSet (own) | 14 | 16 | no default flags (0) [C 360 0x82168e08 + data] |
| CharDriver | 3 | 5 | no rev-4/5 fields (starved clip etc.) [C 360 0x8216be80 / 0x8216c800 + data] |
| CharEyes | 3 | 4 | no interests list [C 360 0x82172708 + data] |
| CharIKHand | 2 | 5 | no finger, one target instead of a list, move-elbow on [C 360 0x82174470 + data] |
| CharLookAt | 2 | 3 | no allow-roll (on) [C 360 0x8216ebb0 + data] |
| CharWeightSetter | 2 | 3 | no invert [C 360 0x8218dfc0 + data] |
| OutfitLoader | 1 | 2 | no category name; each item has a leading dropped byte [C 360 0x8215b528 + data] |
| CamShot | 20 | 27 | blur depth stored as 1 - depth plus two dropped f32; no max blur, focus multiplier, focal target, zoom FOV; below 26 the 360 sets blur 0.5 and depth of field on [C 360 0x8222de20 + data] |
| Spotlight | 20 | 25 | beam is a list (kept on PS2, 0x002744b0), no brighten / cross-section / expand / cutouts; the light can is kept (PS2 0x0032c5c8) [C PS2 Load 0x002737f0 accepts 9..20; 360 0x82238098] |
| LightPreset | 8 | 9 | one dropped f32 after each spot entry's flare flag [C 360 0x82243660 + data] |
| EventTrigger | 8 | 10 | one trigger symbol instead of a list; anim entries anim / blend / wait / delay only; hide delays are plain names [C 360 0x8225d708 + data] |
| Sfx | 4 | 5 | no send / send volume [C 360 0x822ee8b0 + data] |
| SynthSample | 5 | 5 | SampleData v11, format 2 (VAG) everywhere, size a multiple of 64, no end flags: the sample count is the length [C data] |

## Textures (RndBitmap)
- Header as GH2's; encoding 3 on every PS2 texture [C data]. 4- and 8-bit
  palettes (RGBA, alpha 0..0x80, 8-bit in CSM1 order), 16-bit RGBA5551,
  24 / 32-bit; converted to RGBA8 at load (`scene/bitmap.cpp`, from
  gd-game-01's decoder).

## Audio
- Songs, menu loops, crowd streams: `.vgs` version 2, up to 15 channels of SPU
  PS-ADPCM, 16-byte blocks tagged with the channel in byte 1 [C data, gd-game-01].
  `songs.dta` (song (name songs/x/x) (tracks ...) (pans ...) (vols ...) (cores ...))
  names `<name>.vgs`; practice speeds have their own `_p85/_p65/_p50` streams.
- Sound effects: SynthSample records in the banks (`sfx/gen/*_bank`), SPU ADPCM.

## Movies
- `VIDEOS/INTRO.PSS`: MPEG-2 program stream, 16-bit stereo PCM 48 kHz in private stream 1 (SShd type 1, 0x200-byte interleave) [C data]; played by video/movie_player.cpp with gd-game-01's decoders.
