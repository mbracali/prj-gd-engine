# Rock Band (Xbox 360): file formats

Every format on the disc, how it is read, and the tool that decodes it
byte-exact (lesson L21). [C] = confirmed (in the data with the whole-disc
check named, or in `default.xex` at the address given); [I] = inferred.

## Disc and archive [C]
- XDVDFS game partition at 0xFD90000 (`tools/xdvdfs.py`, gd-game-02's,
  unchanged): `default.xex`, `charnames.zbm`, `gen/main.hdr`,
  `gen/main_0.ark`, `gen/main_1.ark`, `videos/*.bik` (14), `$SystemUpdate/`.
- `gen/main.hdr`: ARK version 3 encrypted with Harmonix's Park-Miller
  cipher, exactly GH2's layout (`tools/ark.py`): 4,588 files in 2 parts,
  none compressed (every extract matches its size).
- `.dtb`: GH2's cipher and array layout (`tools/dtb.py dump`): all 291 parse
  exactly to their end.

## Milo (`.milo_xbox`)
- Container: GH2's (`0xCBBEDEAF`, raw deflate blocks), header little-endian.
- **The directory stream and every record are big-endian** (GH2's were
  little-endian): the directory version 25 is stored `00 00 00 19`, the
  0xADDEADDE marker keeps its bytes `AD DE AD DE`. [C data: every file on
  the disc, `tools/milo.py scan`.] Loose bitmaps (`.png_xbox`,
  `.bmp_xbox`) keep GH2's little-endian header; the bitmap inside a Tex
  record is big-endian like the rest of the record.
- `tools/milo.py` and the engine (`data/bytes.h` `milo_order`) read the
  payload in the order the file's first u32 says, so GH2-style files would
  still load.

## Milo classes on the disc (C++ decoders, `--scenetest`)

Every typed class below decodes every instance on the disc exactly to its end marker, in C++ (`gd05 --scenetest`: "scene test passed: 0 typed objects failed, 0 files failed") and in the research decoders. Versions and the RB `default.xex` Load routines (PreLoad / PostLoad where split) [C]; field lists are in the code next to each decoder (`engine/src/scene/*.cpp`, `engine/src/char/char_objects.cpp`), in our own words. Field names come from each class's property table; a name the table does not give is marked [I] in the code.

| Class | RB version | Instances | Load (RB) |
|---|---|---|---|
| AnimFilter | 2 | 347 | 0x821F1DC8 |
| BandButton | 14 | 564 | 0x822D3D48 / 0x822D5578 |
| BandCamShot | 14 | 2075 | 0x822D9640 (CamShot 0x82296E20) |
| BandCrowdMeterDir | 1 | 1 | 0x822ED498 |
| BandCrowdMeterDir (own) | 1 | 2 | 0x822ED498 |
| BandHighlight | 0 | 14 | (see code) |
| BandLabel | 15 | 1534 | 0x822FE288 |
| BandList | 9 | 146 | 0x82268680 / 0x82268EA8 |
| Cam | 12 | 513 | 0x8218E6C8 |
| CharBone | 3 | 14208 | 0x8234CCD0 |
| CharBonesBlender | 1 | 2 | 0x8234DD08 |
| CharClipFilter | 2 | 297 | 0x823504F8 |
| CharClipGroup | 1 | 9197 | 0x8231DA58 |
| CharClipSamples | 13 | 9704 | 0x8234F8E0 |
| CharCollide | 3 | 1599 | 0x8232D9D0 |
| CharCuff | 7 | 421 | 0x82334660 |
| CharDriver | 7 | 171 | 0x82314B48 / 0x823154E8 |
| CharDriverMidi | 3 | 66 | 0x823426F8 / 0x823425A0 |
| CharEyes | 7 | 6 | 0x8232CB58 |
| CharForeTwist | 2 | 14 | 0x8233E670 |
| CharGuitarString | 0 | 12 | 0x8234C2A0 |
| CharHair | 9 | 225 | 0x8233BF30 |
| CharIKHand | 6 | 22 | 0x8232F9C0 |
| CharIKMidi | 4 | 2 | 0x82341AB0 |
| CharIKScale | 1 | 6 | 0x8233DCD0 |
| CharLookAt | 3 | 14 | 0x823231A0 |
| CharMeshHide | 1 | 16 | 0x8235CAE8 |
| CharNeckTwist | 1 | 6 | 0x82344870 |
| CharPollGroup | 1 | 19 | 0x82345C38 |
| CharServoBone | 1 | 77 | 0x82346008 |
| CharTransCopy | 1 | 17 | 0x8233FE10 |
| CharUpperTwist | 1 | 14 | 0x8233F330 |
| CharWeightSetter | 5 | 29 | 0x8234B6E0 |
| Character | 12 | 50 | 0x82307140 / 0x8230AE00 |
| ColorPalette | 1 | 306 | 0x822F8038 |
| CompositeCharacter | 9 | 153 | 0x822E4260 / 0x822E6508 |
| CubeTex | 1 | 157 | 0x821F8728 / 0x821F89E0 |
| EnvAnim | 4 | 27 | 0x821E9F90 |
| Environ | 9 | 749 | 0x821ADBB8 |
| EventTrigger | 11 | 2917 | 0x822123D8 |
| FaceFxLipSyncServo | 7 | 6 | 0x823252C8 / 0x82328210 |
| Flare | 4 | 97 | 0x821BFD70 |
| Font | 16 | 55 | 0x821A4A80 |
| FxSendReverb | 2 | 3 | 0x82234D30 |
| Group | 13 | 5286 | 0x821EB800 |
| Light | 12 | 1186 | 0x821AACC8 |
| LightAnim | 2 | 84 | 0x821BE9F8 |
| LightPreset | 13 | 174 | 0x822C78E8 |
| Mat | 41 | 9360 | 0x821FDB20 |
| MatAnim | 7 | 1488 | 0x821C3D68 |
| Mesh | 34 | 13620 | 0x821A02F8 |
| MeshAnim | 1 | 138 | 0x821BC9A0 |
| ObjectDir (own) | 19 | 2 | 0x8247FB78 / 0x8247D4B8 |
| OutfitConfig | 3 | 1383 | 0x822F9DD0 |
| OutfitLoader | 3 | 6 | 0x8230F488 |
| PanelDir | 4 | 85 | 0x82260A90 / 0x82260B08 |
| PanelDir (own) | 4 | 363 | 0x82260A90 / 0x82260B08 |
| ParticleSys | 36 | 2091 | 0x821D7870 |
| ParticleSysAnim | 3 | 399 | 0x821DEDC0 |
| PostProc | 18 | 102 | 0x821913A0 |
| RandomGroupSeq | 1 | 44 | 0x8222B218 |
| RndDir | 10 | 277 | 0x821B1B28 / 0x821B1278 |
| RndDir (own) | 10 | 495 | 0x821B1B28 / 0x821B1278 |
| ScreenMask | 2 | 200 | 0x821E1858 |
| Set | 0 | 61 | 0x821E2408 |
| Sfx | 9 | 315 | 0x822322D8 |
| Spotlight | 31 | 675 | 0x822B3360 |
| SynthFader | 0 | 8 | 0x8222D180 |
| SynthSample | 5 | 336 | 0x8223A070 |
| Tex | 10 | 11983 | 0x821F67A0 / 0x821F73F8 |
| Text | 17 | 123 | 0x82215A00 |
| TrackDir | 5 | 5 | (see code) |
| TrackDir (own) | 5 | 6 | 0x82250E88 / 0x82252610 |
| TrackWidget | 4 | 258 | 0x82254720 |
| Trans | 9 | 25323 | 0x821F5D18 |
| TransAnim | 6 | 2132 | 0x821C7698 |
| UIButton | 0 | 16 | 0x8226E458 |
| UILabel | 8 | 16 | 0x8226C070 |
| UIList | 9 | 3 | 0x82268680 / 0x82268EA8 |
| UIProxy | 2 | 8 | 0x8225F520 |
| UISlider | 1 | 23 | (see code) |
| UITrigger | 1 | 442 | 0x822630B0 |
| WorldCrowd | 8 | 24 | 0x822B91E0 |
| WorldDir (own) | 17 | 102 | 0x8228C468 / 0x8228B6C8 |

Classes kept raw (split by their markers, no C++ decoder yet; their layouts were decoded exactly in the research tools and are ported when the engine needs them): WorldInstance, PropAnim, MeshDeform, PatchRenderer, TexRenderer, BandMeshLauncher, Movie, EventAnim, DeferDraw, Distort, Blur, Line, MultiMesh, Fur, LightHue, Label3d, BandSwatch, SpotlightDrawer, RenderDir, the other FxSend classes (GD05-003).

Differences from GH2 found on the way [C data]:
- inline sub-directories follow their owner's end marker (GH2 nested them inside the record);
- relative paths are relative to the milo's source folder, the parent of its `gen/` folder (all 364 proxies resolve);
- a DataNode's type is a u32 (GH2's little-endian u16 pair read the same);
- the Tex records' inline bitmap header is big-endian; loose `.png_xbox` / `.bmp_xbox` headers stay little-endian;
- Mesh vertices are the console's 80-byte in-memory vertex (position, normal, four bone weights, uv, four u16 bone indices, tangent), bones a counted list;
- Sfx and RandomGroupSeq records carry no object header; SynthSample data is XMA (252), big-endian PCM16 (74) or empty (10).

## Song audio (`.mogg`) [C]
- Version 11 on every song (374 files): the Ogg stream is AES-128 in counter mode, the 16-byte initial counter right after the seek map, the counter incremented as a little-endian 128-bit number. The header reader is RB 0x827E02A8; the key is in `.data` at RB 0x828B9AB4 (DECISIONS D3). `--sfxtest` decrypts every mogg and walks its 416,833 Ogg pages to each file's end; 1 to 15 channels.

## Videos [C data]
- `videos/*.bik` (named by `config/gen/videos.dtb`): Bink video, Bink audio in the 11 with sound, none in the three background loops. `--videotest` decodes all 14 to their last frame (44,189 frames) and checks each count against the header (u32 at +8; audio tracks at +40).

## Song list and charts [C data unless noted]
- `songs/gen/songs.dtb`: 101 entries (58 songs with a `rank`, the rest tutorials, the Drum Trainer and test tracks). Per song: `tracks` (instrument -> mogg channels), `crowd_channels`, `pans`, `vols`, `cores`, `drum_solo` / `drum_freestyle` sounds, `hopo_threshold`, `song_scroll_speed`, `bank`, `anim_tempo`, `preview`, `rank`, `genre`, `decade`, `vocal_gender`.
- MIDI tracks: PART GUITAR, PART BASS, PART DRUMS, PART VOCALS, EVENTS, VENUE, BEAT in all 62 song charts. Gem lanes are base + 0..4 of each difficulty's octave (60, 72, 84, 96); special pitches read by the parser's note handler RB 0x8249F990: 116 Overdrive phrase, 103 solo, 120..124 fill / Big Rock Ending lanes; a gem shorter than 161 ticks has no sustain (RB 0x8249FB68) [C]. The parser's defaults (RB 0x824A1770): num_slots 5, phrase_slot 7, player_slot 9, the vocal range 36..84 [C].
- PART VOCALS: notes 36..84 with MIDI lyric events (meta 5) at the note's tick; 105 / 106 phrase markers; 96 / 97 percussion; 116 Overdrive. Lyric suffixes `#`, `^` (talkies), `-` (joined), `=` (a shown hyphen), `+` (a slide) [C data; their scoring I].
- Drum mix events `[mix <difficulty> drums<n>...]` select a beatmatcher.dtb submix.
