# Guitar Hero (PS2, SLUS_212.24) — reverse-engineering findings

Status: first pass (2026-10-20). Addresses are Ghidra addresses in `SLUS_212.24`.
"Confirmed" means read from decompiled code or decoded game data; anything
else is marked as an inference.

## Tooling

| Path | What it is |
|---|---|
| `ghidra/GH1.gpr` | Ghidra 12.1.4 project, analysed with the Emotion Engine (R5900) language. Open it in the Ghidra GUI. |
| `analysis/functions.tsv` | 8,125 functions: entry, name, size, caller count, callees |
| `analysis/strings.tsv` | 31,070 defined strings with referencing functions |
| `analysis/decomp/*.c` | Decompiled C for every function, one file per 64 KiB of address space |
| `analysis/classes.txt`, `analysis/vtables.tsv` | 355 C++ class names; vtables recovered for 274 of them |
| `analysis/ark_files.tsv` | Index of the 3,167 files in `MAIN_0.ARK` |
| `extracted/`, `extracted_dta/` | Extracted archive files; all 156 `.dtb` scripts decoded to readable `.dta` |
| `tools/ark.py` | List/extract the ARK archive |
| `tools/dtb.py` | Decrypt `.dtb` and print it as `.dta` text |
| `tools/vtables.py`, `tools/ghidra_scripts/` | Vtable recovery, naming and export scripts |

REA 6.7.0 could not be used on this binary: its Ghidra adapter only admits
standard MIPS32r2/o32 ELFs and rejects this one's header flags
(`0x20924001`: MIPS III, R5900 machine, EABI64), with no override. Faking the
header would make Ghidra decode the file as generic MIPS and garble the
128-bit R5900 (MMI) instructions, so the analysis drives Ghidra headless directly.

## Binary facts

- ELF32 little-endian, entry `0x100010`, stripped (no symbol table).
  Sections include `.vutext` (vector-unit microcode) and DVP overlays.
- Compiler: GCC 2.x. Evidence: the RTTI name mangling (`9StarPower`) and the
  type-info functions in vtable slots.
- **Vtable layout** (confirmed): 8-byte entries `{s32 this_delta, void* fn}`,
  entry 0 = type-info function. An object's vptr points 4 bytes *after* the
  address labelled `<Class>_vtable`. A call through offset `0x0c + 8k`
  reaches the method listed as slot *k* in `vtables.tsv` / named `<Class>_v<k>`.
- `FUN_002896d0` = `strcmp` (128-bit MMI-optimised); `FUN_00241328` =
  `Symbol` from C string; `FUN_0022d888` = `DataArray` find-array-by-symbol;
  `FUN_00235c20` / `FUN_00235d08` = DataNode int / float.

## Data archive (`GEN/MAIN.HDR` + `MAIN_0.ARK`) — confirmed

Header (little-endian): `u32 version=3`, `u32 part_count`, `u32 n`,
`u32 part_sizes[n]`, `u32 string_table_size`, string table, `u32 n`,
`u32 string_offsets[n]`, `u32 n`, then n entries of
`{u32 offset, u32 name_index, u32 dir_index, u32 size, u32 zero}`.
Files are stored uncompressed. Contents by size: `.vgs` audio 1.29 GB (255
files), `.pss` FMV 0.89 GB, textures, meshes, 58 `.mid` charts (47 songs plus
tutorials), 156 `.dtb` scripts.

## Script files (`.dtb`) — confirmed

- Encryption (`BinStream`, `FUN_0022af80` / `FUN_0024a028` / `FUN_0024a140` /
  `FUN_0022b000`): the first 4 bytes are a plaintext seed. A 256-word table is
  filled with the C `rand()` LCG (`x = x*0x41c64e6d + 0x3039`; each word is
  `(x1 >> 16) | (x2 & 0x7fff0000)`). Each step does `t[a] ^= t[b]` with
  `a = 0`, `b = 103` initially, both wrapping after index 248. Every later
  byte is XORed with the low byte of the step result.
- The plaintext is a serialised DataArray: a `0x01` byte, then arrays of
  `u16 count, u16 line, u16 id` holding nodes `{u32 type, value}`. Types:
  0 int, 1 float, 2 `$var`, 5 symbol, 6 unhandled, 7 `#ifdef`, 8 `#else`,
  9 `#endif`, 0x10 `()` array, 0x11 `{}` command, 0x12 string, 0x13 `[]`
  property, 0x20 `#define` (its value is the *next sibling* node), 0x21
  `#include`, 0x22 `#merge`, 0x23 `#ifndef`.

## Song charts (`songs/<name>/<name>.mid`) — confirmed

Standard MIDI type 1 at 480 ticks/beat. Tracks are selected by name in
`DifficultyParser_v5` (`0x262100`): `T<n> GEMS`, `T<n> SOLO`, `T<n> PITCH`,
`EVENTS`, `TRIGGERS`. Other tracks seen in the data: `ANIM`.

In `T1 GEMS`, with `d` = difficulty 0–3 (Easy to Expert):

| MIDI pitch | Meaning | Code |
|---|---|---|
| `60 + 12d + 0..4` | frets green..orange | `FUN_00262300` |
| `60 + 12d + 7` | Star Power phrase | `FUN_00262328` |
| `69 + 12d + 0..1` | Player 1 / Player 2 sections (applied only with >1 player) | `FUN_00262358` |
| 108 | probably singer lip-sync (inference: not yet traced) | — |

Gem construction (`FUN_002619d0`, emitted on note-off):
- frets whose note-on lies within **10 ticks** of each other form one chord
- a gem shorter than **161 ticks** is flagged as having no sustain
- ticks are converted to milliseconds through the tempo map

Gem record (16 bytes, `FUN_0025a8b8`):
`{f32 start_ms; s32 start_tick; s16 duration_ms; s16 duration_ticks; u8 fret_mask; u8 flags (bit 0 = HOPO-able, bit 1 = no sustain); u8 player_mask; u8 pad}`.

**HOPO rule** (`FUN_0025b5f0`): a gem is HOPO-able if it is a single fret
(popcount table at `0x2f2160`), shares no fret with the previous gem, and
starts **≤ 169 ticks** after it (a twelfth note plus 10 ticks at 480 ticks/beat).

## Hit detection — confirmed

Configured by `config/beatmatch.dta` (`beatmatcher watcher`) and read by the
watcher constructor `FUN_00262d10`:
- **`slop 100`**: the hit test is `|input_ms − gem_ms| <= slop`
  (`FUN_00263680`), a symmetric ±100 ms window
- **window end** (`FUN_00263b20`): `min(gem_ms + slop, midpoint to the
  neighbouring gem)`; after it closes, a gem becomes a miss or pass
- **HOPO hit** (`FUN_002639b8`): a fret change without a strum hits a
  HOPO-able gem if the previous gem was the last one hit, the frets held
  exactly equal the gem's single fret, and the timing passes the slop test
  against an offset field (`+0x74`). Resolved: `+0x74` is a constant 60.0
  set by the constructor (never read from config). It is the strum grace
  (pending deadline = strum + 60) and the HOPO window's lateness offset:
  `|t − gem − 60| ≤ slop`, i.e. −40..+160 ms (disassembly 0x263a70..0x263a84;
  the next-gem time fetched at 0x263a68 is discarded).

## Input judgment ("watcher") — confirmed, `0x263248`–`0x2647f0`

Controller mappings come from `config/beatmatch_controller.dta`:
- guitar: R2 / Circle / Triangle / X / Square = green..orange
- joypad: L2 / L1 / R1 / R2 / X
- strum = D-pad up/down (Harmonix button IDs 12/14); Star Power = Select (8)

Guitar mode (`FUN_0025b868`):
- **Strum** (`FUN_0025a0e0` → `FUN_00263248`): repeat strums within 52 ms are ignored.
  The target is the gem nearest the strum time (`FUN_0025b468`; ties go to
  the earlier gem; already-hit gems count), and it must be within ±slop,
  otherwise it's an overstrum.
  - single note: hit if the **highest held fret** equals the note (anchoring allowed)
  - chord: hit only if the held frets **exactly** equal the chord
  - wrong frets: the gem becomes "pending" until strum + 60 ms (watcher
    `+0x74`). A fret change that matches before then still hits
    (`FUN_00263860`); otherwise it becomes an overstrum (`FUN_002630b0`).
  - strumming a gem already hit as a HOPO is forgiven (`+0x50`)
- **HOPO attempt** (`FUN_002639b8`): allowed only after a strum and until all
  frets are released. Pressing a fret above all held frets tries a hammer-on
  with that fret. Releasing the top fret tries a pull-off with the highest
  remaining fret. Conditions: the gem is HOPO-able, the previous gem was hit
  and was the last one hit, the gem's fret mask == the fret's bit, and
  `|t − gem − 60| ≤ slop` (as decoded; i.e. −40..+160 ms). A failed attempt
  blocks further attempts on that gem.

HOPO re-check (round F, against the disassembly):
- window `|t − gem − 60| ≤ slop` confirmed instruction by instruction
  (0x263a70 `sub.s t−gem`, 0x263a74 `lwc1 +0x74` (= 60.0, `0x42700000` set by
  `FUN_00262d10`), 0x263a80 `abs.s`, 0x263a84 `c.le.s` against `+0x3c` slop):
  a hammer-on / pull-off lands from 40 ms before to 160 ms after the gem.
- guitar mode (`FUN_0025b868` / `FUN_0025b9b0`): `+0x1c` "strummed" is set by a
  strum (D-pad 12/14) and cleared when the last fret is let go; a fret press
  tries a hammer-on only while `held >> fret == 0`; a release tries a pull-off
  with the highest remaining fret only when it is below the released one.
- **the HOPO look** (`FUN_0015d840`, `slti s2,2` at 0x15d960, then
  `FUN_0025a988` on gem i−1 with `movz`): coloured top only for a HOPO-able gem
  with index ≥ 2 whose previous gem belongs to the track's player; gems 0 and 1
  always draw the black-ring top even when HOPO-able.
- engine: `Player::hopoAttempt` matches; `Autoplay` now hammers / pulls every
  HOPO it legally can (the self-test plays each chart strummed-only and with
  HOPOs: 12,708 gems land as hammer-ons / pull-offs, all charts fully hit), and
  an input-injection run (frankenstein Hard, green strum then red without a
  strum) logged the `hopo` event.

HOPO on PC (round G): the PS2 reads the pad once a frame and turns the
difference into ButtonDown / ButtonUp messages, so letting go of one fret and
pressing the next inside one frame never shows "no fret held" — `+0x1c` stays
set. Event-driven PC input saw that split-second gap and ended the HOPO chain.
The engine now ends it only when no fret is held for longer than a pad frame
(1/59.94 s). The window (−40..+160 ms) was re-checked against the raw
instructions at 0x263a58..0x263a8c and the constructor's `0x42700000` (60.0)
at watcher +0x74. Enhanced "Hammer-ons & Pull-offs: Forgiving" widens the early
side to −100 ms, needs no strum / held fret, and does not lock out a gem after a
wrong fret. `GD01_HOPOLOG=1` logs every try and why it failed.

Joypad mode (`FUN_0025bea8`): each fret press is also a strum, and the 60 ms
grace applies only to chords.

Common rules:
- **overstrum** (`FUN_002641e0`): ends the current sustain; at most one per
  50 ms; reported to the player as a miss (gem = nearest, or +1 if that
  is the sweep cursor). The gem itself stays hittable. The sink call is
  `miss(player, slot, gem)`: slot = the fret strummed (-1 for none). A
  pending wrong strum that expires (0x2630cc) or is replaced by a new strum
  (0x2633b8) reports the pending gem and its fret, so it can break a Star
  Power phrase. The track pops that slot's smasher (z 0.71) and stops the
  hit effects (FUN_0015e2f8 → FUN_00164ae8); a slot of -1 does neither.
- **miss sweep** (`FUN_00263b20`) starts after the last hit gem, so unhit
  gems before a later hit are never reported as misses. Deadline =
  `min(t + slop, (t + t_next) / 2)`; the last gem uses t_next = t + 4000.
- **sustains**: tracked when duration_ms ≠ 0. A sustain ends early on
  release of any of its frets, or on pressing a fret above a single note
  (or any extra fret on a chord sustain) (`FUN_00263fa0`).
- **poll order** (`FUN_002630b0`): pending expiry → sustain end → HOPO
  forgiveness expiry → autoplay cheat → miss sweep → passed-gem events → whammy.

Star Power phrases (`FUN_0011c390` hit / `FUN_0011c700` miss; state 0 none,
1 broken, 2 armed, 3 captured):
- hitting a phrase's first gem arms it; hitting its last gem while armed
  adds `star_phrase`
- any miss inside the phrase breaks it
- while Star Power is deployed, phrase gems count as misses (no capturing)
- a miss reported more than 500 ms before its gem is attributed to the
  previous gem (`FUN_001166f0`)

Player scoring order (`FUN_00116580`): the streak is incremented, then head
points use the *previous* multiplier, and the multiplier is recomputed at the
end. So the 10th consecutive note still scores at 1×.

## Scoring, Star Power and rock meter — confirmed

Values come from `config/scoring.dta`. They are read into a singleton at
`0x3443f0` (`FUN_0010ada0` / constructor `FUN_0010adf8`).

**Points** (`FUN_00116e98`, `FUN_00117270`, `FUN_0010b3d8`, `FUN_0011d470`):
- hit: `score += head(50) × fret_count × multiplier`
- sustain maximum: `round(tail(25) × duration_ticks / 480) × fret_count`,
  i.e. 25 per beat per fret, counted in chart ticks
- multiplier (`FUN_00117160`, `FUN_0010b450`): the last `streaks` entry whose
  count ≤ current streak (0→1, 10→2, 20→3, 30→4), × `star_power multiplier`
  (2) while Star Power is deployed
- timing class (`FUN_0010b398`): |error| ≤ 30 ms → perfect (2), ≤ 70 ms →
  good (1), otherwise 0

**Star Power** (config struct at singleton `+0x28`: recharge_rate,
deploy_rate, star_phrase, whammy_rate, ready_level, multiplier, crowd_boost;
update `FUN_0011c090`):
- capturing a phrase (hitting all its gems) adds `star_phrase` (0.25)
- whammy on a phrase sustain adds `whammy_rate` (0.034) **per beat**
- deploying requires meter ≥ `ready_level` (0.5) (`FUN_0011c9a0`)
- while deployed the meter drains `deploy_rate` (0.125) **per measure**, so a
  full meter lasts 8 measures and one phrase is worth 2 measures

**Rock meter / crowd** (object built by `FUN_0011d0b8`, update `FUN_0011d268`, caller `FUN_001171b8`):
- `raw = clamp01((1 − w)·raw + w·target)`, `w = note_weight × k`; k = 1, or
  `crowd_boost` (6) while Star Power is deployed and target > raw
- the displayed rating maps raw linearly over `range` (e.g. 0.65–0.9 on
  Expert → 0–1) and starts at 0.5
- target: 1.0 for a hit with no sustain; 0.0 for a miss (which also ends the
  streak); for a sustain, on release, `(head + earned_tail) / (head + max_tail)`
- the rating cannot drop below `warning_level` while song progress (elapsed /
  length) ≤ `free_ride` (Easy 0.2, Medium 0.1)
- below `lose_level` the song is failed (`FUN_0011d3b8`), unless the game's
  multiplayer flag (GameConfig+0x8c) is set; once there, the rating can only rise
- star ratings: `thresholds` 2.0 / 3.0 for every difficulty, mapping to
  3/4/5 stars. What they are compared against is not yet traced.

## Audio (`.vgs`) — confirmed (`VAGFileReader_New`, `FUN_0021d7c0`)

- header (0x80 bytes): `"VgS!"`, `u32 version (2)`, then per channel
  `{u32 sample_rate, u32 block_count}`
- data from offset 0x80 is read in 8 KiB chunks of 16-byte PS-ADPCM blocks;
  **byte 1 & 0x7f is the channel ID** (in place of the usual ADPCM flags)
- decoded with `tools/vgs.py`; block counts match the header exactly
- channel roles (inference from audio energy in one song): ch 0–1 = band
  stereo, ch 2–3 = guitar stereo

## UI panels (`ghui/*.gh`) — confirmed (`tools/ui_panels.py`)

The menu system (screens, panels, navigator, message routing, components,
native objects, controller mapping, screen flow) is specified in
`docs/ui_system.md`; fonts and text layout (RndFont glyph table measured from
the sheet's alpha, RndText layout and quads) in `docs/fonts_and_text.md`.

- **How they're loaded**: every panel class uses `UIPanel::Load` (`HudPanel_v13`, `0x205e38`). The file is the
  panel's `(rnd_file ...)`, or else `"%s/%s.%s"` = the script's directory,
  the panel name and `rnd_extension` (`gh`, from `config/gh.dta`). The file
  is read by the normal `RndLoader` (`FUN_001bb100`, flags 0xd) into the
  panel's RndDir. Only `*.rnd` names get remapped to `<dir>/gen/<n>.rnd_ps2`
  (`FUN_001bb078`). `ghui/common.gh` is the UI `shared_rnd_files`.
- **Container** (RndLoader states `FUN_001bb598`→`7a8`→`9a0`→`bc040`→`bc400`), the same as `.rnd_ps2`:
  `[0xCBBEDEAF wrapper]`, `u32 version (≤10)`, `u32 count`, count ×
  `{string class, string name, u8 if 1≤version≤7}`. For version > 8 this is
  followed by `u32 n` and n **strings**: external files to preload. In `.gh` these are the panel's textures
  (`image/brick02.bmp` → `ghui/image/gen/brick02.bmp_ps2`). In `.rnd_ps2` they are empty strings, which is why they look like zero u32s.
  Then come the objects in order. After each one the loader **scans forward to `AD DE AD DE`**
  (`FUN_002e8a78`), so the game tolerates extra bytes in a record.
- UI classes (table 2 slot 8 Loads): `LabelEx` `0x11e9b8` (v6: trans, draw,
  fit bool + w/h, leading, align, per-glyph jitter seed + x/z/rotation,
  style symbol, caps bool, text token, wrap width), `ButtonEx` `0x12b8e0`,
  `PictureEx`, `PictureToggle`, `List` (RndScrollList), `Slider`,
  `TextEntry`, `CreditList` / `Placer` (draw *before* trans), `Text`
  (RndText v15). The style symbols name entries in `ghui/config.dta`
  (e.g. `label_serif_black` → `(text label_serif_black.txt)` + colours,
  template objects in `ghui/gen/resources.rnd_ps2`).
  RndText align: high nibble 1/2/4 = top/middle/bottom; low nibble = left/centre/right.
- Validation: all 61 files parse; 435 UI objects decode to exactly their
  record length. The other 1,832 objects (Mesh/Mat/Tex/View/…) are listed only.

## Character animation (`.acp`, `.acg`, `.acs`) — confirmed (`tools/char_anim.py`)

- `.acp` = one `AnimClipSamples`. The AnimSet loader `FUN_0017d0e8` reads
  `"%s/gen/%s.acp"` (the set's `directory`, a clip from its `animations`
  list in `charsys/*_anims.dta`), parsed by `FUN_00189f30` → `AnimClipSamples_v12` (`0x17cdb8`).
  Layout: `string class, string name`; AnimClip
  (`0x189da0`) `u32 ver 18, f32 start, f32 end, f32 frames_per_sec, u32
  flags, u32 play_flags, f32 blend_width`; samples `u32 ver 5`, then two
  SampleSets "Full" and "One", each `u32 n, n×string channel, u32
  num_samples, u32 compressed`, then the data of each set. For each sample:
  every `.pos` channel (3×f32), then every `.quat` (4×s16/32767 if
  compressed, else 4×f32), then every `.rotz` (s16/1638.4 rad, else f32)
  (`FUN_0017c358`). Playback: `t=(frame-start)/(end-start)`, index
  `t·(n−1)`, linear interpolation (`FUN_0017ace8`, `FUN_0017ae98`).
  **Frames are beats**: frames_per_sec is beats/s at the authored tempo, and
  every clip is sampled at 30 samples/s. "One" = constant channels (1 sample).
- `.acg` = transition graph of a clip set (`FUN_0017f440`):
  `<directory>/<set name>.acg`, e.g. `main::hiphop.cset` → `hiphop.acg`.
  Layout: `u32 ver 1, u32 clip_count, per clip: u32 n, n×{u32 dest_clip,
  f32 src_frame, f32 dest_frame}`. The two floats are inferred from the data.
- `.acs` = plain-text DTA listing the clip-set macros to precache.
- Validation: 926/926 `.acp`, 25/25 `.acg`, 2/2 `.acs` consumed exactly;
  all sample sets are compressed; every quaternion has norm 1 ± 0.001.

## Textures (`.bmp_ps2`, `.png_ps2`, embedded in Tex) — confirmed (`tools/tex.py`)

32-byte header: u8 version 1, u8 bpp (4/8/16/24), u32 3, u8 mip count @6,
u16 width @7, u16 height @9, u16 bytes per line @11. For 4/8 bpp, a palette
of 2^bpp RGBA follows (alpha 0..0x80; 8-bit palettes in GS CSM1 order, so
entries 8–15 and 16–23 of each 32 are swapped), then indices (4-bit: low
nibble first); 16 bpp is RGBA5551, 24 bpp RGB. Each mip level follows at half
size. All 1,523 texture files match this size exactly.

## Scene files (`.rnd_ps2`) — confirmed (`tools/rnd.py`, `tools/rnd_classes/`)

Container: optional 0xCBBEDEAF chunked raw-deflate wrapper (u32 header size,
u32 block count, u32 ?, u32 block sizes); then u32 version 10, u32 object
count, (type, name) string pairs, u32 n + n external file paths, then each
object's record followed by 0xADDEADDE. Each class's Load is vtable slot 8
(Print = slot 3, Save = slot 4). Renderer load code lives in the separate
`rnd_load` section (0x2e8130). Every record of all 30 classes in the 105
scene files decodes exactly; see the decoders for field layouts:
- `base.py`: Transformable v8 (local + world 3x4 row-vector transforms,
  `p' = p·R + t`, scenes Z-up), Drawable v1, Animatable (frame filters)
- `geometry.py`: Mesh v25 (48-byte vertices: pos, normal, 4 floats = bone
  weights or RGB(A) color, uv; u16 triangles; up to 4 bones with inverse-bind
  matrices), MultiMesh, View (groups: draw list order), Morph, MeshAnim
- `material.py`: Mat v21 (stages: blend, texgen, uv transform, wrap, tex;
  blend 0 Dest, 1 Src, 2 Add, 3 SrcAlpha, 4 SrcAlphaAdd, 5 Subtract;
  zmode 0 Disable, 1 Normal, 2 Transparent, 3 Force, 4 Decal), Tex v8
  (inline bitmap), Environ, Light, Cam (fov treated as horizontal for 4:3),
  Font, Text, Flare, Movie
- `animation.py`: TransAnim, MatAnim, LightAnim, EnvAnim, CamAnim,
  ParticleSys, ParticleSysAnim (keys: values + f32 frame; venue anims run on
  MIDI ticks, `_rt` views on ms, UI at 30 fps)

The highway is `track/gen/track.rnd_ps2`: `track.view` (camera `track.cam`,
surface, rails, lane lines, hit effects), `gem_template.view`,
`gem_star.view`, `gem_smasher.view`. Vertex alpha is 0 or 1 with no visible
meaning and is ignored by the engine (inference).

## Animation playback — confirmed (`engine/src/scene/anim.cpp`)

- `RndAnimatable::SetFrame` (`FUN_001abab8(frame, blend, anim)`): frame →
  filters in list order (`FUN_001ac468`; only ScaleOffset `f*a+b` and
  MinMaxLoop clamp/wrap occur in the data) → class SetFrame (Animatable
  vtable +0x14) → the same filtered frame to every child anim. `blend`
  mixes the keyed value into the target's current one: `cur + (key-cur)*blend`.
- Keys: binary search; frames outside the keys hold the end key; linear
  interpolation unless noted. Each class uses its keys owner's keys.
  - TransAnim (`FUN_001de038`): position lerp or cubic Hermite (`trans_spline`;
    tangents `FUN_0024cd68`: 2 keys `p1-p0`; first `1.5(p1-p0) - 0.25(p2-p0)`;
    last `1.5(pn-pn-1) - 0.25(pn-pn-2)`; interior `(pi+1 - pi-1)/2`).
    Rotation: quaternion nlerp (slerp with `rot_slerp`), blended with the
    current rotation by nlerp, matrix by `FUN_0024ae78` (rows
    `[1-2yy-2zz, 2xy+2zw, 2xz-2yw]`, `[2xy-2zw, 1-2zz-2xx, 2yz+2xw]`,
    `[2xz+2yw, 2yz-2xw, 1-2xx-2yy]`). Then each row × the scale key.
    Writes the target's local transform.
  - MatAnim: colour keys set RGB (their alpha is uninitialised), alpha keys
    set alpha; stage translation keys scroll UVs; texture keys step.
  - LightAnim: light colour. EnvAnim (`RndEnvAnim_v1 @ 0x1b5f70`): ambient
    (environ +0x50), fog colour (+0x70), fog start/end (+0x64/+0x68). CamAnim: fov.

## Venues — confirmed (`engine/src/game/venue_script.cpp`)

- Global script `arena/gen/venue.dta`: loads the venue script
  (`venues/<v>/gen/<v>.dta`), section `venue` from `<v>.rnd_ps2`, the
  camera script and `campaths`. A venue script can load more sections
  (`{arena load_section lighting lighting}`).
- Sections (`VenueSection`, `FUN_00178db8`): `<section>.view` (drawn;
  animated on song **ticks**), `<section>_rt.view` (animated on song **ms**),
  `<section>_transparent.view` (a member of the main view, drawn last).
  Each frame `FUN_00178ce8` sets the `_rt` view's frame to ms, then the main
  view's to ticks.
- Environments: drawing an `Environ` that sits in a draw list selects it for
  every following draw (no restore at group end). Materials with
  an environ are lit by the VU1 lighting programs (full spec with addresses:
  `docs/lighting.md`): `out = min((vert_amb ? vtx : mat)·(use_environ ?
  ambient : 1) + (vert_dyn ? vtx : mat)·Σ col·max(0, n·D), 1)` per vertex,
  `D = −normalize(light world +Y)` (light travels along +Y); the first point
  light replaces all directionals, otherwise ≤ 3 directionals; unlit when
  `!use_environ && vert_amb` or decal. Textured colour goes to the GS ×128
  (no overbright) except stage blend 3 "Multiply2" (×255). Fog is linear in
  view depth between the environ's start and end.
- Transform owner: the last Transformable field (read before as `parent`)
  names the object whose transform this one is drawn with (`0x2eadc8` stores
  it at xfm+0x10; `PsMesh_v5` draws with the owner's world). Venue proxies
  such as `stage.1.mesh` (owner `stage.mesh`) depend on it.
- `{<parent> add_trans <child>}` parents an object to another; every venue
  attaches a key or rim light and `color_plane.mesh` to `6 foot camera.cam`,
  which is the camera the shot director moves.
- Lighting state = crowd excitement × song section. Excitement
  (`FUN_0010d370`, rock meter `r`): Boot below the player's threshold
  (+0x94), Bad < 0.33, Okay < 0.66, else Great (Peak is unused).
  `verse`/`chorus`/`solo` text events of the EVENTS track set the section; a
  change of either sets `lights_need_update`, and the next downbeat calls
  `set_lights_bad` / `set_lights_<okay|great>_<section>`. `intro_start`
  → bad, `music_start` → okay_verse. Excitement changes also send
  `excitement_<level>` (and `_rising`/`_falling`) to the venue script. One bar
  before a section (`FUN_0010f700`, 1920 ticks) `upcoming_section` goes to
  the camera director.
- `{arena switch_anim <anim> (range|loop|shuttle a b) (scale s) (blend b)}`
  (`FUN_0016c6a0` → `FUN_0017a1d0`; `_rt` variant uses ms): one slot per anim
  (layout `FUN_0017a078`). Defaults from `config/gen/arena.dta`: scale 1,
  blend 240. On switch: previous range/scale/offset/mode are kept for the
  crossfade; `offset = -t*scale + filter(0) + min`; `a == b` forces range
  mode; without a range the current frame continues at the new scale. Each
  frame (`FUN_0017a518`): `f = t*scale + offset`, then range clamps, loop
  wraps into `[min, max)`, shuttle reflects (negating scale and adjusting
  offset) (`FUN_0017a0b0`); SetFrame(f, 1) without the top-level filters,
  then the previous range at weight `(blendEnd - t) / blend` while positive.

## Venue camera — confirmed (`engine/src/game/venue_camera.cpp`)

- Shots: `{set $camedit.<group> ({arena switch_cam <path> <shot> (start 0)
  (end 100) (duration d) (loop) (offset_in x y z) (offset_out x y z)
  (singer_in x y) (singer_out x y) (near) (far) (fov_in deg) (fov_out deg)
  (ease e) (shaky) (hide_crowd) (crowd_region) (real_time) ...} ...)}` in
  `venues/<v>/gen/camera.dta`; paths are TransAnims `arena::<path>.tnm` in
  `campaths.rnd_ps2`. `arena/gen/cam_paths.dta` gives each path's `parent`
  and `target` (fallbacks for character objects: `arena::stage_spot_01.mesh`).
- switch (`FUN_0016f3b8`): start/end are percentages of the path's end
  frame; a frame task runs start → end from now to now + duration (ticks, or
  ms with `real_time`): `LinearInterpolator`, or with `ease` an
  `ATanInterpolator` (`frame = mid + (end-start)·atan(x)/(2·atan(e))`, x
  going −e → e). No parent/target entry = the band-focus object.
- per frame (`FUN_0016e390`): u = progress between start and end frames.
  Path transform (identity for missing keys) + lerp(offset_in, offset_out, u),
  then × parent world. Orientation: look at the target, keeping the path's
  up axis (right = fwd × up). Then the camera shifts by
  `right·(−sx·dist/proj_x) + up·(sy·dist/proj_y)` with
  (sx, sy) = lerp(singer_in, singer_out, u). proj_y is negative (PS2 screen
  y down), so the target lands at screen (sx, sy) with +y up. fov =
  lerp(fov_in, fov_out, u).
- Band focus (`FUN_0016e080` / `FUN_0016e390`): guitarist0's `bone_head.mesh`
  world position, followed each frame by `t = cam_filter (0.1) · min(screen
  error, 1)` toward the head (error = head's screen position vs the shot's
  singer position). It jumps to the head on a cut. With no guitarist: walk
  spot 2 + 60 in z.
- Director (`arena/gen/camera.dta` + native `CameraSystem` 0x110040–0x111b20):
  `intro_start` plays the first `INTRO` shot for 6 bars. `one_bar_to` a
  section always picks a **regular** shot and only remembers whether a solo
  is coming; solo shots (`SOLO_NEAR` = the hand close-ups, `SOLO_FAR`) come
  when that shot times out inside the solo. Each downbeat counts down
  `bars_left` (skipped while player 0 is in Star Power); `beat_check` re-picks
  at once if the current shot became invalid. Pick: candidates from every
  pool with all wanted flags (kCamNoSolo / kCamSolo), valid and compatible;
  each pool's unused ("fresh") shots first, the pool refreshed when all are
  used; weight = pool weight / candidate count. Invalid: special shots,
  `walk_ok 0` while walking, the nearest walk spot in `bad_walk_spots`, not
  kCamNear while a Star Power clip plays, `low_excitement_ok 0` below Great.
  Incompatible with the previous shot: the same shot, the same side
  (Left/Right), the same distance (Closeup, Far or Behind; Far↔Behind too).
  Durations (bars): `camera_durations` in `arena/gen/venues.dta`. Per shot
  also `enable_dof` (depth of field at 2·|subject − camera|, a 4-tap blur of
  everything at or beyond it) and `hide_crowd`. Full spec:
  docs/camera_walk_crowd_lighting.md §1, docs/render_gems_dof.md §4.

## Walking — confirmed unless noted (`engine/src/game/guitar_walk.cpp`)

- Guitarists only (`create_walk` in charbase.dta). Spots `arena::walk_spot_NN`;
  the **last is a hub** every walk passes through. Single player starts on
  walk_spot_01 (spot 02 is player 1 in multiplayer).
- Random walks: in the playing state, venue `allow_walks`, after a delay
  rolled from `walk_delays` by excitement (Okay 35–55 s, Great 20–40 s,
  others never). Blocked before solos (`pre_solo` events, from
  `walk_beats_per_inch` 0.069 and `walk_end_beats` 3) and for 4 beats after
  `solo_off`; the arena walks to `solo_walk_point` before solos.
- A walk = turn clip (best angle to the hub, < 45°) + walk cycles (LRU of the
  song tempo; Normal below Great, Extreme at Great) + stop clip (best end
  facing), queued through the clip graph. The clips' `bone_facing` deltas
  move the root (bone.servo `use_delta`); light steering keeps the path.
  [I: our layout counts whole walk cycles instead of the native 2-beat
  simulation; no speed correction.]
- Losing mid-walk plays `kGuitarLoseWalk`; `lose_teleport` venues snap the
  guitarist to the nearest spot if within 72 units of the singer.

## Script interpreter notes

- `switch` / `cond`: the default branch is **every** remaining non-list
  command, not only the first (mem_card.dta's `{set ...} {ui goto_screen ...}`
  defaults depend on it).

## Characters — confirmed unless noted (`engine/src/scene/character.cpp`)

- Files: `charsys/<model>/gen/<model>.rnd_ps2` (body; `top.view` holds
  `lod0_*` / `lod1_*` views plus extras), `<model>_face.rnd_ps2` (Morph
  face expressions, not used yet), guitars in `charsys/guitars/gen/`. A
  song's `quickplay` gives `character` and `guitar`; `config/guitars.dta`
  skins give `(outfit kGuitar*)`, whose number indexes the `GUITARS` list in
  `charsys.dta`, and `(mat ...)`, the guitar body material.
- Skeleton: bones are Meshes; the hierarchy comes from each object's
  Transformable `children` list (a bone's own `parent` field names itself);
  rebuilding worlds from locals reproduces every stored world exactly. The
  guitar file repeats `bone_pelvis` / `bone_pos_guitar`; its parts hang off
  the character's copies.
- Skinning: vertices carry 4 weights (summing to 1) for the mesh's ≤ 4 named
  bones; `world = Σ wᵢ · (v · offsetᵢ · boneWorldᵢ)`, offsets being inverse
  bind matrices (verified against the bind pose).
- Clips: `.pos` sets the bone's local translation, `.quat` its rotation (same
  quaternion convention as TransAnim), `.rotz` a pure Z rotation (the bind
  locals of `.rotz` bones are pure Z rotations). Categories are the
  `(flags ...)` bits: kGuitarIdle / Normal / Bad / Extreme / Intro / Win /
  Lose for guitarists, kBandActive / Idle / Allbeat / Double / Half / Win /
  Lose for the band, plus kTempoMedium 1 / kTempoFast 2 matched against the
  song's `anim_tempo`. kAnimRealTime (play flag 16) clips run on seconds ×
  frames_per_sec, the rest on song beats.
- Placement: the guitarist stands on `walk_spot_02`; the band (`charsys.dta`
  `(band SINGER_MALE_METAL BASS_METAL DRUMMER_METAL)`, models from
  `band_chars.dta`) on `stage_spot_0(n+1)` per `band_spots` (singer 0, bass
  1, drummer 2). Spot markers are scaled (0.377) and turned to face the
  crowd; only position and facing are used. The EVENTS track drives the
  band: `gtr_on/off`, `bass_on/off`, `sing_on/off`, `drum_on/off`,
  `drum_allbeat`, `drum_double`, `drum_half_tempo`, `drum_normal`.
- Lighting: characters are lit by the environment the venue script selects
  (`{arena set_singer_env singer0.env}`), whose lights live in the venue.
  Skin programs set the vertex colour to 1, so characters use the material
  colour. Light direction: see "Venues" (settled from the VU1 microcode).
- Clip playback, servos and timing are specified in
  `docs/character_animation.md`: AnimClipDriver frames advance by the beat
  delta and snap to `beat mod A` (A = play_flags & 0xf) every tick; new clips
  enter at `.acg` edges `{dest_index, src_frame, dest_frame}` and blend with
  `0.5 − 0.5cos(π·blend)`; clips are picked least-recently-used; band moods
  follow `upcoming_<event>` (one beat early); the guitarist's clips finish
  blending on the event beat. Most arm skin hangs off twist bones that only
  the AnimServoForeTwist / UpperTwist servos (priority 1000) move.
- Guitarist hands (`docs/guitarist_hands.md`): GuitarAnimInfo builds fret
  positions from the ANIM track (notes 40..59 → `spot_neck_fret01..20`) and
  finger / strum events from the gems (`hand_finger_mapping`,
  `hand_strum_mapping`, `HandMap_*` / `StrumMap_*` text events); `bone_fret`
  is detached and glides on an S curve between spots; left / right hand
  drivers blend over the main clips by the IK flag weights; two-bone IK
  (align_quat, stretch) puts the hands on `bone_fret_hand` / `bone_strum_hand`.
- Not yet reimplemented: walking (root motion), face morphs and eyes,
  drummer/keyboard realign.
- Script `if` (`FUN_002313a0`) returns kDataUnhandled when its condition is
  false and 0 when true.
- End of song (`docs/end_of_song.md`): the player wins once the `end` event
  passes, loses when the rock meter drops below lose_level on a note event;
  the guitarist's win/lose clip ends → `game_outro`. Stars: 5 at score ≥ 3·B,
  4 at ≥ 2·B, else 3, B = Σ per gem (50 + 25·dur/480 truncated, sustains ≥ 161
  ticks) × frets. GameConfig+0x8c is the multiplayer flag (no fail in
  multiplayer), not a no-fail flag.
- Character / guitar select panels (CharsysPanel, GuitarDisplayPanel,
  GuitarSelectPanel, MultiCharSelPanel): `docs/select_panels.md`.
- Sound effects (banks, sequences, SPU2 voices, reverb, who plays what):
  `docs/audio_sfx.md`. End screens and the in-game HUD / track / title
  overlay / intro timeline: `docs/endgame_and_hud.md`. Career (campaign data
  model, unlocks, cash, store, high scores, save layout): `docs/career.md`.
  The gameplay sound and flow logic lives in scripts (`arena_game.dta`
  handlers on `arena`, `config/beatmatch.dta` player_matcher_handlers,
  `config/player.dta` star_power_handlers), which the engine runs as-is.

## Crowd — confirmed unless noted (`engine/src/render/crowd_renderer.cpp`)

- Script `arena/crowd.dta`: `{crowd load_archetypes (CROWD_MALE1 CROWD_MALE2
  CROWD_MALE3 CROWD_FEMALE1 CROWD_FEMALE2 CROWD_FEMALE3)}`; each archetype
  loads `charsys/crowd/gen/crowd_*.rnd_ps2` (no `top.view`; 8 head meshes, 3
  hand sets, outfit mats), with clips from `CROWD_ANIMS`
  (`arena/anims_macros.dta`, `charsys/crowd/anims/gen/*.acp`), restricted
  by `C_MALE1` 1 / `C_MALE2` 2 / `C_FEMALE` 4 and grouped as `C_IDLE` 8,
  `C_BAD` 16, `C_OK` 32, `C_GOOD` 64, `C_GREAT` 128, `C_CLAP` 256, `C_JUMP`
  512, `C_CHEER` 1024. A clip's `HAND_CLAP` / `HAND_FIST` / `HAND_HORN`
  enter event shows `hand0/1/2_left|right.mesh`.
- `update_crowd` per excitement: `set_sizes` (full, flat) 0.1 / 0.25 / 0.5 /
  1 / 1 and `animate_crowd` weights (bad, ok, great, idle) (1,0,0,0) /
  (.8,.2,0,0) / (0,.8,.2,0) / (0,0,1,0) / (0,0,1,0).
- Construction (`FUN_00171558`): a 128×256 render target
  (fallback `system/run/arena/textures/crowd128_mip.bmp`), a quad
  (`FUN_00170160`: `crowd_flat_height` (config `arena.dta`, 100.8) tall,
  half as wide, centred on the instance, in the XZ plane; an 8-vertex
  "striped" variant with `crowd_striped_quad`). For i = 1..6,
  `arena::Crowd%02d.mm` gets that quad as its mesh and archetype i−1; its
  instances are the flat crowd members. Then for each
  `arena::crowd_limits%02d.mesh`, `FUN_00170da8` picks instances inside the
  region (0 < height < flat height and a point-in-mesh test, `FUN_001e3600`)
  to become full 3D crowd members (not reimplemented).
- The impostor is re-rendered every frame per block (`FUN_00172350`) by a
  camera with the main camera's world rotation, looking at the archetype
  from `h/4 / tan 10°` back (20° FOV, near 10, far 1000); the quads carry
  transform constraint 8 (FastBillboardXYZ) and turn to the camera. Every
  member of a block moves identically. `set_sizes` shows the first
  `(int)(count·flat)` instances. Full spec, the 3D crowd regions and the
  archetype list: `docs/crowd.md`.

## Other file types — identified

- `.bnk` + `.nse` + `.seq` (per venue, song, `sfx/gen`, tutorial): the
  Synth/BankManager sound effects. `.bnk` is the `Bank` (`FUN_00218358`):
  `u32 ver, u32, u32`, then vectors of SampleDesc (16 bytes:
  `u32, u16, u16`, SPU2 ADSR, …), KeyMap and SfxMap, then the PS2 sample
  table. `.nse` is raw SPU2 PS-ADPCM, streamed in 0x7800-byte chunks into
  the bank's SPU RAM slot (`FUN_00216b10`, "NoiseLoadingBuffer"). `.seq`
  (`"%s/gen/%s.seq"`, `FUN_00217488`) is `u32 0` followed by an
  **unencrypted** serialised DataArray of sequences (`sfx_seq`,
  `random_group_seq`, …). `tools/dtb.py`'s Reader decodes all 16 exactly.
- `.pss`: MPEG-2 program streams (Sony PSS) in `videos/`: `ghintro`
  (the splash `CUT_SCENE_VIDEO`), and `wavegroup`/`character`/`venues`
  (store bonus videos, `config/store.dta`).
- `.ipu`: `"ipum"`, `u32 size` (file−8 for fest_1; 0x40136a in the two leftovers), `u16 w, u16 h`, `u32 frames`, then a PS2
  IPU stream. It's the venue `(song_movie fest_1)` → `arena/movies/%s.ipu`
  (`FUN_001796e8`), for the in-venue screens. `70s_3`/`snow_1c` are leftovers.
- `.icn`: the PS2 memory-card 3D icon (`config/mc.dta` `ps2_icon (file "gh.icn")`).

## Open questions / next steps

1. (Resolved: `+0x74` is a constant 60 ms; see "Hit detection".)
2. (Resolved.) Guitar muting on a miss and the face-off guitar mix: see "Guitar audio (UnmuteBeatMatchAudio)" below and docs/guitar_audio.md.
3. (Resolved.) Sustain accrual, every poll (FUN_00116ef0 → FUN_0011d4f0):
   `earned = (int)(clamp((t − start_ms) / duration_ms, 0, 1) × max_tail + 0.5)`,
   the difference scored × multiplier; `max_tail = round(25 × ticks / 480) ×
   frets` (FUN_0011d470). Once earned == max the held note is finished at
   once (FUN_00116f50: crowd update, `held_note_released`), even while still
   held. Phrase misses (FUN_0011c700) match the rules under "Star Power
   phrases"; the 500 ms attribution is in FUN_001166f0.
4. (Resolved: no chart on the disc has `T<n> SOLO` or `T<n> PITCH`; all 58
   MIDIs hold only T1 GEMS, EVENTS, ANIM and TRIGGERS. The parser
   callbacks are leftovers with nothing to drive.)
5. Star-rating comparison and the song-select flow.
6. Venue extras: the TRIGGERS track (kick/bass hits → `kick_drum`,
   `bass_hit`), flares, particles, shaky cam, `hide_crowd`/crowd regions,
   the native shot picker (`cam change_to_regular_shot`), hand IK.
7. (Resolved: over-bright characters came from the light direction sign;
   see `docs/lighting.md`.)
8. Propagate names (strcmp, Symbol, DataArray, …) into the Ghidra project.

## Guitar audio (UnmuteBeatMatchAudio)

Full table in docs/guitar_audio.md. Summary [C]:
- One `UnmuteBeatMatchAudio` per PlayerMatcher (player); each owns its own song stream instance (+0x54,
  FUN_002662b0 / FUN_002665c0) and a SlipTrack on the `slip_tracks` channels (`(2 3)`).
- The miss mute is the slip voice: hit (`_v3`) SlipTo 0 + hit state (+0xf4, gem +0xf8); own gem time reached
  (`_v1`) SlipTo 0 if in hit state; miss/overstrum (`_v4`) and own pass (`_v5`) StopSlip + clear; opponent's gem
  pass (`_v6`, at the gem's time) StopSlip keeping the hit state; pre-start gem pass (`_v7` → `_v3`) as a hit;
  sustain let go with time left on the hit-state gem (FUN_00264018 → `_v18`, gem flag bit 9 = no sustain) StopSlip + clear.
- Channel volume FUN_002660c0 = vol × f3 (levels; 1.0 in GH1) × (active players 0 or 3 ? common attenuation : 1,
  latched at the next set_volume) × f4 (set_volume) × +0x10.
- Face-off (player count ≠ 1): each player's guitar channels `pan × 0.33 ∓ 0.67` (P1 left, P2 right), volume × 10^(−1.5/20)
  (FUN_00267d08); player 2's stream mutes every non-guitar channel (FUN_00267b08). Each player's misses and whammy act
  on their own copy.

