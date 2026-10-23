# Guitar Hero 1 (PS2, SLUS_212.24): video (PSS movies and IPU venue movies)

Marks: **[C]** confirmed in code or data, **[I]** inferred, **[U]** unknown or unused.
EE addresses refer to `original_files/SLUS_212.24` (exports in `analysis/`).
I measured every number below on the real files from `original_rom/Guitar Hero (USA).iso` with small Python
scripts and `ffprobe`/`ffmpeg` 8.1, used only as an oracle. The scripts are in the session scratchpad:
- `pes.py`: lists the PES streams of a .pss
- `aud.py`: pulls out the audio and its SShd header
- `m2v.py`: reads the sequence and picture-coding extensions

**Summary**
- **Videos the user can see: 4, all `.pss`.** These are the intro and the three store "bonus videos".
  They use MPEG-2 Main@Main video and **uncompressed 16-bit PCM** audio. There is no ADPCM.
- **The in-venue `.ipu` movies never show in the shipped game.** All 7 venues have `use_video 0`, and
  no Mat references the movie texture. The IPU format is documented below anyway. It is small to
  support because it is MPEG-2 intra-only.
- **pl_mpeg cannot play these files.** It supports MPEG-1 only, and every `.pss` is MPEG-2.
- **Recommendation (§6):**
  - A self-contained MPEG-2 decoder in `engine/src/video/`, about 2–2.5 kLOC, with no new
    dependency. It can start from pl_mpeg's MIT skeleton.
  - The IPU decoder is a subset of it, about 300 lines.
  - The audio is just PCM pushed to an SDL3 audio stream.

---------------------------------------------------------------------------------------------------

## 1. Files on the disc [C]

The disc holds `GEN/MAIN.HDR` and `GEN/MAIN_0.ARK`. `MAIN_0.ARK` starts at ISO sector 1917, at
byte 0xEFA800. Files are stored uncompressed (`tools/ark.py`, `analysis/ark_files.tsv`).

| ARK path | ARK offset | bytes | what |
|---|---|---|---|
| `videos/ghintro.pss` | 0x0021188b | 16,023,556 | intro FMV (`cut_scene`) |
| `videos/wavegroup.pss` | 0x781a76ea | 259,309,572 | bonus video1 (`store.dta` price 350) |
| `videos/character.pss` | 0x535fb2c2 | 389,709,828 | bonus video2 (325) |
| `videos/venues.pss` | 0x6a9ab6e6 | 226,476,036 | bonus video3 (325) |
| `arena/movies/fest_1.ipu` | 0x53157770 | 145,102 | `(song_movie fest_1)` of the fest venue (dead, §5) |
| `../../system/run/arena/movies/snow_1c.ipu` | 0x0c38a5 | 320,552 | `arena/gen/fx.rnd_ps2` → `snow.mov` (dead) |
| `../../system/run/arena/movies/70s_3.ipu` | 0x8794d1fa | 511,059 | `arena/movies/gen/70s_3.rnd_ps2` → `song.mov` (leftover) |

The three `.ipu` files are already extracted under `extracted/`.

---------------------------------------------------------------------------------------------------

## 2. PSS container [C]

### 2.1 Streams
A PSS is a plain **MPEG-2 Program Stream** (Sony "PSS"). It is built as follows:
- Pack headers `00 00 01 BA` are 14 bytes plus `(byte13 & 7)` stuffing bytes, in MPEG-2 style.
- One system header `BB` sits at the start.
- PES packets are about 4 KB, with lengths 4090 or 4076.
- There are 3 `BE` padding packets.
- The stream ends with `00 00 01 B9`.

| PES id | contents |
|---|---|
| `0xE0` | MPEG-2 video elementary stream |
| `0xBD` (private stream 1) | audio. The payload after the PES header starts with a constant 4-byte sub-header `FF A0 00 00`: 0xFF is the sub-stream id, and the player skips 4 bytes (`FUN_0024fa48`: `param_2+4, size-4`) |

PTS is present on the packets. The audio and video start PTS are equal: 0.0463 s for ghintro, 0.0462 s for
character and wavegroup, and 0.057 s for venues.

ffprobe does **not** list the audio. It does not know sub-stream 0xFF, so we demux it ourselves.

### 2.2 Video (all four files)
| | ghintro | wavegroup / character / venues |
|---|---|---|
| codec | MPEG-2, profile/level 0x48 = **Main@Main** | same |
| size | **640×448**, 4:2:0, `aspect_ratio_information` 1 (square), shown full screen on a 4:3 TV | same |
| frame rate | code 4 = **29.97** | 29.97 |
| frames / duration | **600 / 20.02 s** | 9546 / 318.5 s, 22789 / 760.3 s, 15744 / 525.3 s |
| bitrate (header) | 5 Mbit/s max, 6.4 average | 5 / 5 / 2 Mbit/s |
| `progressive_sequence` | 1 | **0 (interlaced, bottom field first, `top_field_first`=0)** |
| `picture_structure` | 3 (frame) on **every** picture | 3 on every picture (no field pictures) |
| `frame_pred_frame_dct` | 1 | **0**: field MC and field DCT allowed |
| `intra_dc_precision` | 1 (9-bit) | 1 |
| `q_scale_type` / `intra_vlc_format` | 1 / 1 (non-linear q, table B.15) | 1 / 1 |
| `alternate_scan` | 0 | **1** |
| `concealment_motion_vectors`, `repeat_first_field` | 0, 0 | 0, 0 |
| matrices | default intra, **custom non-intra** (`load_non_intra_quantiser_matrix`=1) | same |
| GOP | `IBBPBBPBBPBBPBBPBB` (18), closed by I | same |

The encoder was TMPGEnc 2.521.58.169 (user data). `character.pss` repeats the sequence header 1287 times;
the others have it once.

Whether dual-prime motion (`motion_type` 3 in P frames) is used is not measured **[I: TMPGEnc does not use
it]**. A decoder should detect it and log it.

### 2.3 Audio: 16-bit PCM, not ADPCM
The concatenated `0xBD` payloads (after the 4-byte sub-header) form a Sony "SShd/SSbd" stream:

```
'SShd' u32 0x18  u32 type=1  u32 rate  u32 channels=2  u32 interleave=0x200  u32 -1  u32 -1
'SSbd' u32 data_size        ; then data_size bytes
```

- `type` 1 means **signed 16-bit little-endian PCM**. The libmpeg stream callback is registered as
  type 2 = `sceMpegStrPCM` (`FUN_00269eb8(mp, 2, 0, FUN_0024ec18)`).
- The data is **stereo, block-interleaved by 0x200 bytes**: 256 L samples, then 256 R samples, and so on.
  This is the SPU2 AutoDMA layout. I checked it: de-interleaving by 512-byte blocks gives smooth waveforms.
- The header rates are 48000 for the bonus videos and **44100 for ghintro**. ghintro's PES PTS spacing also
  says 44.1 kHz (23.08 ms per 4072 bytes).
- **The game ignores the header.** `FUN_0024fe20` swallows the first 0x28 bytes. The data goes to an IOP
  buffer (0x18000 B), then to the SPU2 core-0 AutoDMA "block transfer" (`sceSdRemote` 0x80e0, mode 0x13,
  in `FUN_0024fc98`), which always plays at **48 kHz** [C for the path; 48 kHz is the hardware rate of
  AutoDMA].
- At 48 kHz, ghintro's 3,880,960 bytes last **20.21 s**, which matches the 20.02 s of video. At 44.1 kHz they
  would last 22.0 s. Bonus videos: 61.16 MB → 318.5 s, 100.86 MB → 525.3 s, 148.36 MB → 772.7 s.
  **→ Always play PSS audio as 48 kHz stereo s16le**, like the PS2 [I: the "44100" is an encoder mislabel].
- The audio runs free. Audio starts once 0x18000 bytes are queued to the IOP (`FUN_0024fea0`). There is no
  PTS-based A/V correction. The movie ends when the video ends, and the remaining audio is cut.
- While the movie plays, the player saves the synth's volumes and zeroes them, and restores them afterwards
  (`FUN_0024f710` / `FUN_0024f910`) [I: other music and SFX are silent].

The engine's `audio/adpcm.cpp` (SPU2 PS-ADPCM) is **not needed** for video.

---------------------------------------------------------------------------------------------------

## 3. How PSS movies are played [C]

**Who calls it.** `meta play_movie <file>`, handled by `FUN_0014ccf0`:
- It runs `sprintf("videos/%s")`, then calls `FUN_0024edf8(minSec=0, path, alloc=0x14cb98, free=0x14ccb0)`.
- It plays only if `FUN_00224238()` (`DAT_002f1d74`) or `set_tool_movie` is set
  (see `docs/boot_profiles_memcard.md`).
- Scripts that call it:
  - `splash.dta` `cut_scene`, on `TRANSITION_COMPLETE`: `{meta play_movie ghintro.pss}` `{ui goto_screen splash}`.
  - `options.dta` `play_bonus_video` (from `bonus_material`): `{meta play_movie {$this get video}}` then
    `{ui goto_screen bonus_material}`. The file comes from `{game video_provider} get_video_file video1..3`,
    which reads `store.dta`. Buttons are enabled when `campaign is_unlocked videoN`.

**The player** (`FUN_0024edf8`, Sony libmpeg / "ezmpeg"-style) is a **blocking loop**. No UI, script or
render task runs until it returns.
1. It opens the file and reads 0x4000-byte chunks.
2. `sceMpegCreate` gets a buffer of `w*h*9/2 + 0x176c` bytes (`FUN_0024ec40`).
3. It registers callbacks:
   - Error
   - No-data (feeds IPU DMA, `FUN_0024f5e0`)
   - Background (reads more file data while waiting, `FUN_0024eb70`)
   - The video (`FUN_0024f4c0`, a 0x30000 ring) and PCM (`FUN_0024fa48`) stream handlers
4. It sets the display to 640 wide (`FUN_0019a4f0(0x363f60, 0x280, h, …)`).
5. Each loop it calls `sceMpegGetPicture` → RGB32 → DMA image upload straight into the frame buffer
   (`FUN_0019fdc8` / `FUN_0019ff48`, 16×16 macroblock rectangles), with no scaling. 640×448 fills the
   NTSC screen.
6. **Pacing:** RCNT0 counts HBLANKs (mode 0xC3) with target 0x20D, i.e. 525 lines = one NTSC frame (0x271 =
   625 on PAL). `FUN_0024f308` waits until the count is ≥ 0x20B/0x26F before presenting. **One picture per
   TV frame: 29.97 fps on NTSC** (25 on PAL, which would play slow).
7. The audio pump runs in `FUN_0024fb20` (§2.3).
8. **Exit:**
   - at end of stream (`sceMpegIsEnd`), or
   - when `elapsed_ms > minSec*1000` (= 0) **and** pad 0 has a button in mask `0x842`.
     That means **R2/green fret, X, or Start skip it at any time**.
9. On exit it frees everything, re-initialises the IPU for PsMovie (`FUN_0019e630` → `sceIpuInit`
   `FUN_00271688`), and restores the display.

---------------------------------------------------------------------------------------------------

## 4. IPU movie format (`.ipu`) [C]

```
0x00  'ipum'
0x04  u32  size          ; file-8 for fest_1; 0x40136a (stale) in the two leftovers. Don't trust it.
0x08  u16  width   (LE)  ; fest_1 256, snow_1c 256, 70s_3 128
0x0A  u16  height        ; 256, 256, 128
0x0C  u32  frame count   ; 45, 46, 150 (equals the number of 00 00 01 B0 codes)
0x10  frame[count], then 00 00 01 B1 (end of stream)
frame = u8 flags, macroblocks..., zero bits to the next byte boundary, 00 00 01 B0
```

The PsMovie loader reads `w=+8, h=+10, frames=+0xC`, sets `mbCount = (w>>4)*(h>>4)`, and sends the data
from +0x10 as one DMA chain (`PsMovie_v2 @0x19ea28`).

Frame sizes: fest_1 3111–3325 B, snow_1c 5953–7506 B, 70s_3 3252–3496 B.

### 4.1 The flags byte = IPU_CTRL picture fields [C]
For each frame the player first runs FDEC peek (`IPU_CMD 0x40000000`) to read the byte, then FDEC 8 to skip
it (`FUN_001a4470`). It then programs (`FUN_0019efa0`):

```
IPU_CTRL = (flags & 0xFB) << 16      -> bits 0-1 IDP (intra_dc_precision), bit4 AS (alternate_scan),
                                        bit5 IVF (intra_vlc_format, B.15), bit6 QST (q_scale_type),
                                        bit7 MP1 (MPEG-1 coefficient/escape semantics)
IPU_CMD  = 0x18010000 | ((flags>>2)&1) << 24
         = IDEC, OFM=1 (RGB16 output), QSC=1 (initial quantiser), DTD = flags bit 2 (dct_type present)
FROM-IPU DMA: QWC = mbCount*32  (512 B = 16x16 RGB16 per MB)
```

When the DMA completes, the interrupt handler (`FUN_0019ef10`) sends FDEC 32 (`0x40000020`) to skip the
`00 00 01 B0` code.
- Shipped flags: fest_1 **0x60** (QST + IVF). snow_1c and 70s_3 **0x00**. DTD is 0 everywhere.
- FFmpeg's `ipu` decoder (`mpeg12dec.c ipu_decode_frame`, `ipudec.c`, `ipu_parser.c`, FFmpeg ≥ 4.4) uses the
  same bit layout. It decodes all three files: 45, 150 and 46 frames, and the images look correct.

### 4.2 Macroblock syntax (IDEC) [C from the bit dumps and the IPU semantics; table names from ISO 13818-2]
Macroblocks come in **raster order**, `ceil(w/16) * ceil(h/16)` of them, with no slices and no other headers:

```
for each MB:
  if (not first MB) macroblock_address_increment   ; always the 1-bit code '1'
  macroblock_type (Table B.2, I-picture): '1' = intra, '01' = intra + quant
  if (DTD) dct_type : 1 bit                        ; never set in shipped files
  if (quant) quantiser_scale_code : 5 bits         ; QST picks linear (2*code) or non-linear table
  6 blocks: Y0 Y1 Y2 Y3 (2x2, frame DCT) Cb Cr      ; all coded, no coded_block_pattern
     dct_dc_size  (B.12 luma / B.13 chroma) + dct_dc_differential
     AC run/level (B.14 if IVF=0 else B.15) until EOB
       escape = 000001 + 6-bit run + 12-bit signed level (MPEG-2); MP1=1 -> MPEG-1 8/16-bit escape
```

- The first MB of each frame has no address-increment bit. Checked: the first payload bits are `01`
  (intra+quant) in all three files. The quantiser codes are 18, 15 and 30.
- The DC predictors reset to `1 << (7+IDP)` at each frame start. Dequantisation is standard MPEG-2 intra:
  - DC × `8 >> IDP`
  - AC = `level * qscale * W[i] * 2 / 32` with saturation and mismatch control (MP1 = MPEG-1 oddification)
- **W = the default MPEG intra matrix.** `sceIpuInit` (`FUN_00271688`) runs SETIQ with the 64 bytes at
  0x2f2400: `8,16,16,19,16,19,22,22,…,69,83`, the default matrix in zig-zag order. Non-intra is all 16s.
  Nothing else sends SETIQ, and the PSS player re-runs `sceIpuInit` on exit.
- **Colour:** IPU CSC, BT.601 limited range → RGB [I: standard IPU coefficients]. The output is **RGB16**
  (5:5:5:1).
- **Alpha:** set by SETTH `0x91000001` (TH0=1, TH1=0x100). A pixel whose R, G and B are all < 1 (pure
  black) gets A=0; all others get A=1 [I from the IPU threshold semantics]. In practice, treat it as opaque
  and let the Mat blend decide.

### 4.3 RndMovie / PsMovie object [C]
- Serialized as `Movie` v6 (`tools/rnd_classes/material.py decode_movie`):
  - Animatable with filters
  - `file`
  - `tex` (+0x44)
  - `stream` (+0x3c)
  - `loop` (+0x40)
- The Tex: `PsMovie_v3 @0x19ed08` → `FUN_001d5d98(tex, w, h, 16bpp, type 4 "Movie")`. **The Tex takes the
  movie's size**, and the Tex file's declared size is ignored. `snow.tex` says 128×128, but the movie is
  256×256.
- Loading:
  - `stream=0` loads the whole file into memory.
  - `stream=1` streams through a 0x20000 ring, with `(movie_readahead 0)` from `default.dta`.
- **SetFrame(f)** (`PsMovie_v1 @0x19f128`, RndAnimatable slot):
  1. `target = loop ? f mod n : clamp(f, 0, …)`. When not looping and `f > n-1`, the current frame is held.
  2. If `target < cur`, it rewinds to frame 0 (IPU reset and the DMA chain restarted) and decodes frame 0.
  3. Otherwise it decodes **only `cur+1`**. It is sequential: one frame per call at most, and it never skips.
  4. Decodes are queued and async (DMAC ch3 IPU→EE interrupt; "blocked on movie decode" if the object is
     touched mid-decode). The RGB16 result is uploaded to the Tex.
- Timing in the data: both Movies (`snow.mov`, `song.mov`) have one **ScaleOffset filter (scale 0.03,
  offset 0)**, loop TRUE, stream FALSE. They are driven with the **song clock in ms** (`DAT_00363798`), so
  **frame = floor(song_ms × 0.03) mod n**, i.e. **30 fps locked to the song**. That gives loops of
  1.5 s (fest_1), 1.53 s (snow_1c) and 5 s (70s_3).
  FFmpeg's "25 fps" for .ipu is its own default.

---------------------------------------------------------------------------------------------------

## 5. In-venue video screens: dead in the shipped game [C]

**Startup path.** `Arena` startup (`FUN_00169960`) reads the venue key `use_video` (default 2). Only when it
is non-zero does it build the song-movie object (`FUN_001796e8`, 0x3c bytes). That object does this:

- It makes a **new** Movie and Tex for `arena/movies/<song_movie>.ipu`, with filter 0.03, `stream=0`.
- It looks up `snow.mov` (from `arena/gen/fx.rnd_ps2`, always loaded, `../movies/snow_1c.ipu` → `snow.tex`)
  and **`arena::karaoke_logo.mat`**.
- It sets the Mat to 2 stages. Stage 0 gets snow's Tex. Stage 1 gets the song movie Tex or, when
  `use_video == 2`, a **Rendered** Tex of the singer close-up.
- Config: `movie_min_singer 0.2`, `movie_max_singer 5`, `movie_hud_height 0.66` (`arena.dta`).
- `FUN_00179c20` picks per frame between the movie and the singer view, from the singer's screen bbox.
- Both movies are animated with `song_ms`.

**Why it is dead:**
- All 7 venues in `arena/gen/venues.dta` set **`(use_video 0)`**. Fest also has `(song_movie fest_1)`, but it
  is never used.
- No shipped scene has `karaoke_logo.mat`.
- No Mat references `snow.tex`.

This is a leftover of a "musicvideo"/karaoke venue (`cam_z_range` also tests `venue_name musicvideo`).
`snow.mov` is created at Arena load, preloading 320 KB, but nothing calls SetFrame on it and nothing draws
its Tex.

**→ For a faithful port, nothing in a venue shows video.** Implementing RndMovie is optional: for
completeness, mods, or a debug view of the clips.

---------------------------------------------------------------------------------------------------

## 6. Implementation route for the engine

### 6.1 Decoder choice
| option | MPEG-2 PSS | IPU | deps / licence | verdict |
|---|---|---|---|---|
| **pl_mpeg** (MIT, single header) | ❌ MPEG-1 only. No sequence/picture extensions, field MC or field DCT, alternate scan, non-linear q, B.15, MPEG-2 escapes or DC precision. Its PS demuxer also expects MPEG-1 pack headers | ❌ as is (needs B.15 and non-linear q) | none | not usable as is; **good skeleton to fork** |
| **own MPEG-2 decoder** (MP@ML, frame pictures only) | ✅ | ✅ (subset) | none | **recommended** |
| libmpeg2 (GPL-2+, ~6 kLOC C, video only) | ✅ incl. dual-prime | ❌ (write IPU anyway) | makes the whole binary GPL | fallback if GPL is fine |
| FFmpeg libavcodec/-format (LGPL-2.1+) | ✅ | ✅ (`ipu`) | large; needs dynamic linking on 3 OSes. Its demuxer drops the 0xFF audio, so we still demux ourselves | use only as a **test oracle** |

**What the own decoder needs** (everything measured in §2.2):
- PS demux: MPEG-2 pack headers, `0xE0`, `0xBD` with a 4-byte sub-header.
- Sequence header with the custom non-intra matrix, sequence extension, picture coding extension.
- I, P and B frame pictures.
- MC: frame **and field** prediction in frame pictures (MPEG-2 `frame_motion_type` 1/2, and 3 dual-prime as
  an optional log-and-approximate path), half-pel, with B bidirectional averaging.
- Field/frame DCT (`dct_type`), zig-zag and alternate scan.
- Linear and non-linear quantiser, tables B.14 and B.15, MPEG-2 escapes, 8–11-bit DC precision,
  saturation and mismatch control.
- An IDCT: an integer or float AAN is fine. No need for bit-exactness with the PS2.
- Size: about 2–2.5 kLOC C++.
- Speed: 640×448 at 30 fps plain scalar code is a few ms per frame on any desktop CPU.

**The IPU decoder** then reuses the bit reader, the tables (B.12–B.15), dequantisation, the IDCT and colour
conversion. It adds only the §4.2 MB loop, about 150–300 lines.

### 6.2 Suggested layout (doc only; engine source untouched)
- `engine/src/video/bitreader.h`, `mpeg2_tables.h`: B.1–B.15 VLCs, default intra matrix, zig-zag and
  alternate scans, non-linear q table.
- `engine/src/video/mpeg2_decoder.{h,cpp}`: elementary-stream decoder → YUV420 frames (display order,
  B-frame reordering).
- `engine/src/video/pss.{h,cpp}`: PS demuxer.
  - Video ES chunks go to the decoder.
  - `0xBD` payloads: skip the 4 bytes, skip `SShd`/`SSbd` (0x28 B), then de-interleave 0x200-byte L/R
    blocks into s16 stereo.
- `engine/src/video/ipu.{h,cpp}`:
  - Parse the 16-byte header.
  - Index the frame offsets by scanning for `00 00 01 B0`, so any frame can be decoded on demand (every
    frame is intra).
  - Decode to RGBA8.
- **Streaming from the ISO:** `GameData::read` returns the whole file, which is 390 MB for `character.pss`.
  Add a range or stream accessor (`RandomFile::read(offset, …)` already exists; expose the ARK entry's
  offset and size) and read 64 KB at a time.

### 6.3 Behaviour to reproduce
**`meta play_movie` (ui_system.cpp currently returns at once):**
- Run a **nested blocking loop** like the PS2:
  - Pump SDL events.
  - Decode.
  - Present one picture every 1/29.97 s, using a wall clock started when the first picture is shown.
  - Draw full-screen at **4:3** with letterboxing, whatever the 640×448 storage size.
  - Skip on pad-0 **R2/green fret, X or Start** (`0x842`) at any time.
  - Return when the video ends. Script execution then resumes (`goto_screen splash` / `bonus_material`).
  - The UI clock and tasks must not advance meanwhile; the PS2 runs nothing else.
- **Audio:** create a separate `SDL_AudioStream` (src S16, 2 ch, **48000 Hz**, ignoring SShd's rate) bound
  to the mixer's device. Push PCM as it is demuxed, start it with the first picture, and clear or destroy it
  on exit. Mute or duck the mixer's music and SFX voices for the duration.
- **Deinterlace** the bonus videos (bottom field first, frame-coded interlaced). A cheap line blend or bob
  is enough. ghintro is progressive.
- Colour: BT.601 limited range → RGB in a shader (upload Y, U, V as three R8 textures) or on the CPU.
- On NTSC the PS2 shows 29.97 fps. Use that rate, not PAL's 25.

**RndMovie (optional, §5):**
- Implement it as an Animatable whose `SetFrame(f)` takes `loop ? f mod n : clamp`.
- It is fed through the object's ScaleOffset filter from `song_ms` (0.03 → 30 fps).
- Decode the target frame (random access is fine) into the Movie Tex, sized w×h of the .ipu, RGBA8.
- Make pure black alpha 0 only if a Mat needs it.

### 6.4 Validation
- Reference frames: `ffmpeg -i x.ipu -f rawvideo -pix_fmt yuv420p ref.yuv` and the same for `.pss`.
  Compare YUV planes with a small PSNR threshold (IDCTs differ, so expect ≥ 45 dB, not bit-exactness).
- Frame counts:
  - .pss: 600, 9546, 22789, 15744
  - .ipu: 45, 46, 150
- PCM sample counts per channel: ghintro 970,240 (= 20.21 s at 48 kHz).
- Add a `--selftest` hook that decodes N frames of each file and checks the counts and checksums.

---------------------------------------------------------------------------------------------------

## 7. Corrections to other docs
- `docs/boot_profiles_memcard.md` estimated ghintro at "≈30–60 s". It is **20.02 s** (600 frames at 29.97,
  with audio ending at 20.2 s).
- `FINDINGS.md`:
  - The `.pss` audio is **PCM16** (SShd type 1), not ADPCM.
  - The `.ipu` header `size` field is not reliable.
- `docs/camera_walk_crowd_lighting.md` says "No venue scene has a Movie or a movie-type Tex". Precisely:
  `arena/gen/fx.rnd_ps2` (loaded for every venue) has `snow.mov` and `snow.tex` (type Movie). Neither is
  drawn.
