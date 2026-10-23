# Guitar Hero 1 (PS2, SLUS_212.24): the sound-effects system

Marks: **[C]** confirmed in code or data, **[I]** inferred, **[U]** unknown or unused.
EE addresses refer to `original_files/SLUS_212.24` (exports in `analysis/`).
IOP addresses refer to `original_files/IOP/SYNTH_S.IRX`. I ran it through Ghidra headless during this
session (MIPS:LE:32). The output is in `scratchpad/irxout/`, and a copy with the libsd stubs renamed is
`scratchpad/irx.c`. The IOP module does the per-voice work: voice allocation, volume, pan law, pitch,
ADSR and effect sends. Most "hardware" facts below are therefore confirmed, not guessed.

Artifacts in the scratchpad:
- `bnk.py`: the decoder (`check`, `dump`, `wav`, `wavidx`, `seq`)
- `bnk_dump.txt`: a full dump of all 16 banks
- `seq/*.dta`: all 16 `.seq` files decoded
- `wav/*.wav`: decoded samples
- `irx.c`, `irxstubs.py`: the IOP decompilation and its import-stub table

---------------------------------------------------------------------------------------------------

## 0. Architecture in one page

```
script / C++ ──► Synth (EE, DAT_002f1d10, SynthEE 0x219c60)
   play_sfx  ─► Sfx (0x68 B, FUN_002127f8) ─┐
   play_sequence ─► Sequence tree ─► SfxSeq ─┘ ─► BankManager lookup (FUN_00216898 / FUN_00216530)
                                                   └► per SampleDesc: AudioSampleEE (0x34 B, FUN_00218f70)
                                                         │ RPC (FUN_002d8xxx, opcodes 700..0x2c4)
                                                         ▼
                              IOP SYNTH_S.IRX: Sample (0x24 B) ─► SPU2 voice (48, 0x18 B each)
```

- A **Bank** is one `.bnk` (metadata) plus one `.nse` (raw SPU2 ADPCM uploaded into SPU RAM). There
  are up to `bank_slots`=5 slots (`default.dta`) [C].
- A **Sequence** is one entry of a `.seq` file. Each `.seq` loads into the same slot as its bank, through
  a `SequenceLoader` (`FUN_00217488`) [C].
- One sfx name maps to **N SampleDescs** in the bank. All N play at the same time as layers, usually an
  L/R pair with slight detune [C].
- All volumes are **linear**. There is no dB conversion anywhere in the sfx path [C].
- The EE ticks once per frame (`Synth_v4` 0x213a48). The IOP thread ticks every 5 ms
  (`FUN_00000500`: `DelayThread(5000 - elapsed)`) [C].

---------------------------------------------------------------------------------------------------

## 1. The `.bnk` format (Bank, `FUN_00218358`) [C]

The file is little-endian. Each vector is a `u32 count` followed by the elements.
The `Bank` object is 0x70 bytes, built by `FUN_00218298`.

| off | type | field | notes |
|---|---|---|---|
| 0 | u32 | version | always 19 (0x13); read into a local and ignored |
| 4 | u32 | platform | stored at Bank+4: 0 = PS2, 1 = Xbox, 2 = GC (`Bank_v1/v2` 0x218788/0x218808 switch on it). All GH1 banks are 0 |
| 8 | u32 | bank_id | stored at Bank+0 (default 0xFFFFFFFF). Used only for explicit-bank lookup (`FUN_00216978`), which GH1 never does. Values: ingame 1, venues 2, others 0 |
| … | vec<SampleDesc> | descs | Bank+0x3C, `FUN_002d4de8`, element reader `FUN_00218130` |
| … | vec<KeyMap> | keymaps | Bank+0x4C, `FUN_002d52f0`, element reader `FUN_002181a0` |
| … | vec<SfxMap> | sfxmap | Bank+0x5C, `FUN_002d5808`, element reader `FUN_00218218`; then sorted by Symbol pointer (`FUN_002d6220`) |
| … | vec<SamplePS2> | samples | read through the Bank vtable slot 2 → `Bank_v2` → `FUN_002d78f8`, element reader `FUN_00217ea8` |

### SampleDesc: 16 bytes (`FUN_00218130`; applied in `FUN_00215ff0`)
| off | type | field | runtime meaning |
|---|---|---|---|
| 0 | u32 | sample | index into the sample table |
| 4 | u16 | fx_core | **effect send**. 0xFFFF = dry; 0 = voice on core 0 with its send to core-0 FX (Echo); 1 = voice on core 1 with its send to core-1 FX (Large Studio). Sent via RPC 0x2C4. On the IOP: `FUN_0000409c(core)` allocates on that core and `FUN_000044b4` sets VMIXEL/VMIXER [C] |
| 6 | s16 | transpose | in **1/128 semitone**. ratio = 2^((t/128)/12) (`FUN_00215ff0`: `t*0.0078125` → `FUN_00217450` = `pow(2, x/12)`) |
| 8 | u16 | ADSR1 | SPU2 ADSR1 (`FUN_00218ef0`) |
| 10 | u16 | ADSR2 | SPU2 ADSR2. The pair is sent as `u32 ADSR1 | ADSR2<<16` (RPC 0x2C3) |
| 12 | u16 | volume | linear: vol = v/32767 |
| 14 | s16 | pan | pan = p/127, range -1..+1 (-127 = left, +127 = right) |

Default ADSR = 0x8F1F / 0x03C7 (`FUN_00218e98`, and also the IOP default `FUN_00005070`).

### KeyMap: 12 bytes (`FUN_002181a0`)
`u32 desc, s16 program, u16 lo_key, u16 hi_key, u16 root_key`.

Match rule: `program == prog && lo <= note && note <= hi` (`FUN_00218420`/`FUN_00218520`). The semitone
offset is `note - root` (`FUN_00216148`). Keymaps are used only by the MIDI instrument path (MidiSeq →
MidiChannel → `FUN_00216148`). No GH1 `.seq` contains a `midi_seq`, so **keymaps are unused at runtime
in GH1** [C].

### SfxMap: 9 + len bytes (`FUN_00218218`; struct 12 bytes)
`u16 desc, u8 flag(always 0), Symbol name (u32 len + chars, no NUL), u16 unk8 (always 1000), u16 unk10 (always 0)`.

Only `desc` and `name` are read by the play path [C]. `flag`, `unk8` and `unk10` are never consulted
[U]. The 1000 is probably an authoring "priority" or "max instances" value.

**One name maps to several entries.** Lookup is `lower_bound` on the symbol (`FUN_002185e8`), then the
code walks the following entries while the name stays the same (`FUN_00218690`). Every matching entry
creates its own AudioSample and they all start together [C].

### SamplePS2: 12 bytes (`FUN_00217ea8`)
`u32 offset, u32 size, u32 rate` (bytes into the `.nse`; the sample rate in Hz).

`FUN_002186f0` builds the voice from `(slot_spu_base + offset, rate)`. **The size is not sent.** The
voice runs until it reaches the ADPCM end flag [C]. The sample table is contiguous and covers the
whole `.nse` byte-exactly (verified for all 16 banks).

The Xbox and GC sample readers are also present: Xbox `FUN_00217f00` (u32, u32, u32, bool, u32) and
GC `FUN_00217f88` (DSP-ADPCM coefficients). They are unused on PS2.

### `.nse` [C]
This is raw SPU2 PS-ADPCM. It is streamed in 0x7800-byte chunks into SPU RAM at the slot's base,
which is the sum of the sizes of the earlier slots (`FUN_00216b10`, `FUN_00215fb8`).

Every sample has the same layout, checked on all 429 samples:
- The **first block is 16 zero bytes**: 28 samples of silence.
- **One-shots:** the second-to-last block has flags=1 (END without REPEAT), so the voice stops there.
- **Loops:** the second-to-last block has flags=3 (END+REPEAT), and block 1 has flags=4 (LOOP_START).
- The **last block** is the conventional silent loop pad with flags=7.

Looping samples: ingame s30 (`postshow_loop`), metagame s13 (`meta_crowdlp`), and tutorial s45/s61
(`bkgd_music`). Everything else is a one-shot.

The ADPCM filter table is the standard one (0, 60/0, 115/-52, 98/-55, 122/-60). This is the same
decoder as `tools/vgs.py`.

### From parameters to SPU2 registers (IOP) [C]
- **Pitch** (`FUN_000051c0`, `FUN_000051e4`):
  `pitch = ((rate << 12) / 48000) * speed1000 / 1000` (integer math).
  `speed1000 = (int)(speed * 1000)` (EE `AudioSampleEE_v9`).
  speed = (Sfx transpose ratio × doppler 1.0) × desc ratio.
  The playback rate in Hz is `pitch * 48000 / 4096`; 22050 Hz gives pitch 0x759. The SPU2 output runs
  at 48 kHz with 4-tap Gaussian interpolation [I: standard SPU2].
- **Volume:** EE `vol = clamp(sfxVol*globalVol*groupVol * descVol, 0, 1)`, then `(short)(v*32767)`
  (`AudioSampleEE_v7`).
- **Pan:** EE `pan = clamp(sfxPan + descPan, -1, 1)`, then `(short)(p*127)` (`AudioSampleEE_v8`).
- **Pan law (IOP `FUN_00005330` / `FUN_00005458`).** It uses a 255-entry table
  `T[i] = round(255*sin(π/2 · i/254))` (verified byte for byte at IRX .data 0x37190):
  `L = vol * T_sin((pan+381) mod 1016) / 255`, `R = vol * T_sin((pan+127) mod 1016) / 255`.
  This is a **constant-power sine/cosine law**:
  `θ = (pan+127)/254 · π/2;  L = vol·cos θ,  R = vol·sin θ`.
  So pan 0 gives 0.707/0.707, pan +127 gives L=0/R=1, and pan -127 gives L=1/R=0.
  The register value is `VOLL = L >> 1` and `VOLR = R >> 1` (15-bit fixed-volume mode).
- **ADSR:** written straight to the voice's ADSR1 and ADSR2 (`FUN_0000435c`).

### ADSR decoding (SPU2 envelope, 48 kHz ticks) [C for the bits, I for timings]
```
ADSR1: 15 attack exp | 14-10 attack shift | 9-8 attack step (+7,+6,+5,+4) | 7-4 decay shift | 3-0 sustain level ((N+1)*0x800)
ADSR2: 15 sustain exp | 14 sustain dir (1=dec) | 12-8 sustain shift | 7-6 sustain step | 5 release exp | 4-0 release shift
per tick: cycles = 1<<max(0,shift-11); step = s<<max(0,11-shift)
          exp+inc and level>0x6000: cycles*=4;  exp+dec: step = step*level/0x8000
decay: exp, step -8 ; release: step -8
```

All ADSR values used in the banks (`bnk.py` simulates them):

| ADSR1/2 | uses | attack | decay | sustain | release |
|---|---|---|---|---|---|
| 8F1F/03C7 | 779 descs (default) | exp, ~1.1 ms | none (SL=max) | hold | linear, **5.3 ms** |
| AA1F/03C7 | 1 | 0.12 s | – | hold | 5.3 ms |
| AB1F/03C7 | 1 | 0.15 s | – | hold | 5.3 ms |
| B41F/03CE | ingame `postshow_loop` (2) | **0.68 s** | – | hold | **0.68 s linear** |
| B61F/03CF | tutorial `bkgd_music` (3) | 0.96 s | – | hold | 1.37 s |

Fades for looping sfx come **only** from ADSR attack and release (key-on and key-off). No EE code
ramps sfx volume.

### The decoder
`bnk.py check` parses **all 16 banks byte-exactly**, and every `.nse` size equals the end of its
sample table:
```
ingame 3006/3006 nse=880496 OK · metagame · song_default · tutorial · 7 venue banks · 5 orphan song banks: all OK
```
`bnk_dump.txt` lists for each bank the samples (offset, size, rate, pitch, length, loop), the descs
(sample, fx, transpose, vol, pan, decoded ADSR), the keymaps, and every sfx name with its layers.

The WAVs written to `wav/` prove the offsets:

| file | content |
|---|---|
| `ingame_miss_chink4.wav` | 0.39 s |
| `ingame_sp_deployed_{0,1}.wav` | 2.0 s |
| `ingame_postshow_loop_*.wav` | 10.2 s, loop body |
| `meta_button_select1.wav` | |
| `arena_crowd_win_*.wav` | 4 layers |
| `basement_clap1_*.wav` | |
| `song_default_m1.wav` | 11025 Hz |

All of them decode to clean audio: peaks near full scale and normal zero-crossing rates.

Banks at a glance (bank_id / descs / keymaps / sfx / samples):
- **ingame** 1/65/1/64/42
- **metagame** 0/58/0/58/45
- **song_default** 0/19/11/8/1. Note: m1..m8 → "mayhem" sequence on core-0 echo. Nothing references
  it, so it is unused [C: the string `mayhem` is in no script and nowhere in the ELF].
- **tutorial** 0/74/0/74/62
- **venues** (arena, basement, big_club, fest, small_club, small_club_multi, theatre): id 2, 31–33
  sfx each, 14 samples. Contents: crowd_begin, crowd_{upto,dnto}_*, crowd_win, crowd_lose, clap1..6.
- **songs/{cross,more,sharp,spanish,symph}**: orphans. `songs.dta` uses `sfx/song_default` for all 56
  songs.

---------------------------------------------------------------------------------------------------

## 2. `.seq` sequences

File layout: `u32 0`, then a `0x01` byte and an unencrypted DataArray. The root is `sequences`
followed by the sequence definitions [C] (`FUN_00217680`).

The loader constructs each definition once to read its name (vtable slot 1). It then keeps the
**DataArray** in a map keyed by Symbol. Each `play_sequence` builds a **new instance** from the array
(`FUN_00217950` → `FUN_0020ed38`) [C].

Started sequences go into the active list (`FUN_0020f1a0`, list `DAT_00369454`). Every frame,
`FUN_0020ea40` calls `Poll` (slot 7). Once `IsPlaying` (slot 3) returns false, the instance is
removed and destroyed [C].

**Consequence:** triggering the same sequence again does **not** cut the previous one. Instances
overlap, limited only by voices.

The class factory is `FUN_0020ed38`. The vtable layout is slot n at offset 0xC+8n:
1 Name, 2 Stop, 3 IsPlaying, 4 SetVolume, 5 SetPan, 6 SetTranspose, 7 Poll, 8 Play.

| type | ctor | params | semantics [C] |
|---|---|---|---|
| `sfx_seq` | `FUN_0020f290` | `(name)` `(sfx sym)` required · `(volume X)` **required** · `(pan X)` opt · `(transpose X)` opt. `(speed …)` appears in the data but is **not parsed** | **Play** (`SfxSeq_v8`): deletes the old Sfx, creates `Sfx(sfx, fireAndForget=0, bank=-1)`, then sets volume = `eval(volume)*runtimeVol`, pan = `eval(pan)+runtimePan`, transpose = `transpose+runtimeTranspose` (semitones → `2^(t/12)`), and plays. **IsPlaying** = any of its samples still playing. **Stop** = key-off. volume and pan accept `(rand lo hi)` → `lo+(hi-lo)*randFloat()` (`FUN_0020f5a8`); transpose does not |
| `random_group_seq` | `FUN_00210c78` | `(children …)` `(num_seqs N)` default 1 | **Play** (`RandomGroupSeq_v8`): stops itself, then n = min(num_seqs, #children). If n==1, it plays `children[RandomInt(0,count)]`. Otherwise it copies all children and removes random elements (`RandomInt(0,size)`) until n remain (order kept), then **plays all n at once**. **There is no "no repeat" rule**: the same child can come up twice in a row. IsPlaying = any chosen child playing |
| `serial_group_seq` | `FUN_00210690` | `(children)` | plays the children one after another; the next starts when the current one stops |
| `parallel_group_seq` | `FUN_00210988` | `(children)` | starts all children at once; playing while any child plays |
| `loop_group_seq` | `FUN_00211548` | `(children)` | serial, wrapping back to the first child forever (until Stop) |
| `wait_seq` | `FUN_0020ff48` | `(secs X)` (rand allowed) | playing until `now + secs*1000 ms` |
| `midi_seq` | `FUN_0020f960` | `(duration)(note)(channel)(bank)(instrument)` | sends program change, bank select (CC0) and pan (CC10), pitch bend (transpose*128), then note-on (vel = volume*127). Note-off after `duration` s |
| `vol_mod_seq` | `FUN_00211a38` | `(wrapped seq)(value X)` | sets the child's volume to `{* v value}` (**linear multiply**) |
| `pan_mod_seq` | `FUN_00211ca0` | `(wrapped)(value)` | child pan = `{+ p value}` |
| `transpose_mod_seq` | `FUN_00211f08` | `(wrapped)(value)` | child transpose = `{+ t value}` (semitones) |
| `loop_mod_seq` | `FUN_00212170` | `(wrapped)` | replays the child every time it stops, until Stop. **IsPlaying** = loop flag |

Group SetVolume, SetPan and SetTranspose pass the value down to every child (`LoopGroupSeq_v4/5/6`).

**What the GH1 data actually uses** (all 16 files): only `random_group_seq` (33) containing
`sfx_seq` (233). Every `volume` is 1, every `pan` 0, every `transpose` 0. num_seqs is 1, except
`claps` in **arena, theatre and fest, where it is 2** (two different claps at once).

The sequences:
- **ingame:** miss_chink(4–9), button_play, button_toggle, button_select, sp_gemhit(1–4),
  sp_awarded(1–4), sp_awarded_multi.
- **metagame:** button_toggle(1–3), button_play(1–8), button_select(1–3), button_back(1–3),
  meta_people (6 people + 2 horns + 2 traffic), meta_lick(0–9).
- **tutorial:** the same UI and gameplay sequences as ingame/metagame.
- **venues:** claps(1–6). basement has clap1 listed twice, which doubles its odds.
- **song banks:** mayhem.

### RNG [C]
There is one global generator at 0x422638, shared with gameplay.
- **Seeding:** 256 words are filled by the LCG `x = x*0x41C64E6D + 0x3039`, keeping
  `(a>>16) | (b & 0x7FFF0000)` (`FUN_0024a028`). The seed is 0x29A at static init, then reseeded at
  boot with `RTC sec + min*60 + hour*3600` (`FUN_00223f08` → `FUN_0024a1a8`).
- **Generation:** `next = t[i] ^= t[j]` with i=0, j=0x67 (103), both wrapping after 0xF8 (249).
  This is an R250-style lagged XOR (`FUN_0024a140`).
- `RandomInt(lo,hi) = lo + (int)next % (hi-lo)` (`FUN_0024a080`).
- `RandomFloat = (next & 0xFFFF) / 65536` (`FUN_0024a108`).
- `RandomFloat(a,b) = a + f*(b-a)`.

The same code is `tools/dtb.py`'s Cipher. A port does not need bit-exact streams, but the code is
known if wanted.

### Time base
`Count`-based ms clock (`DAT_0038a2c0` × factor). WaitSeq and MidiSeq use ms.

---------------------------------------------------------------------------------------------------

## 3. Synth, mixing, effects, voices

### Configuration [C]
GH's `config/synth.dta` **merges onto** `system/run/config/default.dta`. The `mic` and `sfx` sections
come from default.dta.
- `sfx (global_volume 1.0)` → `DAT_003694b8` (`FUN_002124b8`). `distance_scale 1`,
  `listener_radius 1.5`, `sound_speed 250` and `max_angle_delta 0.4` drive 3D panning and doppler,
  **which GH1 never uses** (Sfx+0x44 is always 0).
- There are 128 per-group volumes `DAT_003694c0[]`, all 1.0. Every Sfx uses group 0.
- `set_sfx_global_volume` exists but no script calls it.
- **Effective sfx gain** = `fxVolumeOption (0..1, linear) × globalVol(1) × group(1) × seqVol/sfxVol × descVol`.
  - `{options get_fx_volume}` is Options+0x18. The options slider has 12 steps; the value is
    `pos/11`, linear [C].
  - It is passed as `(volume …)` by the `play_game_sfx` / `play_game_sequence*` script funcs
    (`config/system_script.dta`).
  - Plain `play_sfx` (UI) and `synth play_sequence` (UI) get **no** fx-volume scaling.
- `iop (max_sfx 100)`: at most 100 live sample objects on the IOP. A create with the table full is
  silently dropped (`FUN_00001258`). GH overrides `max_streams 12` and `spu_buffers 52`.
- `mic`: gain 0.1, vol 0.9, fx TRUE, compressor TRUE 0.7. There is no mic in GH1 [U].
- `fx`: GH's `synth.dta`, applied by `FUN_00213af8` → SynthEE slots 7/9/11/13/15. One 0x14-byte
  struct per core at Synth+0x28: `{core, mode, s16 depthL, s16 depthR, int delay, int feedback}`,
  which is libsd's `sceSdEffectAttr`.
  - `chain FALSE` → RPC 5 → IOP `FUN_00000848`. This clears the core-1 MMIX SIN→effect bits, and
    dry voices may use **all 48 voices** (with chain TRUE, dry voices are restricted to core 1).
  - **core_0:** `mode kFXModeEcho`(7), `volume 0.3` → depth 9830 (EVOLL/R = wet return),
    `delay 0.5` → **64/127**, `feedback 0.3` → **38/127**.
  - **core_1:** `mode kFXModeLargeStudio`(4), `volume 0.4` → depth 13107. delay and feedback are 0
    (ignored by studio presets).
  - The mode values are 0 Off, 1 Room, 2 SmallStudio, 3 MedStudio, 4 LargeStudio, 5 Hall, 6 Space,
    7 Echo, 8 Delay, 9 Pipe (macros.dta). They match the libsd presets.
  - IOP: a mode change is RPC 3 → `FUN_00000720`, which sets EEA = 0x1DFFFF (core 0) / 0x1FFFFF
    (core 1), then `sceSdSetEffectAttr(mode|0x300)` (clear work area), and writes EVOLL/EVOLR.
    vol/delay/feedback changes are RPC 4 → libsd #33 (set mode params) plus EVOL.
  - Script hooks `synth set_fx (cfg)` and `synth set_fx_vol core v` exist; GH1 scripts do not call them.

### Which voices go to the effects [C]
The desc's `fx_core` field decides it (§1).
- **Large Studio (core 1):** ingame sp_gemhit1-4 (vol 0.22), gtr_smash1-3, track_exp, beat_easy,
  beat_other, beat_venue, crowd_lose (one layer per venue), all metagame meta_lick*, and some
  tutorial descs.
- **Echo (core 0):** song_default m1..m8 (unused), tutorial s19902 / s206xx.
- **Dry:** everything else.

The send level is the voice's own L/R volume (VMIXEL/VMIXER gate). The wet return is
EVOL = depth.

### Master volume and fades [C]
- **Master volume:** RPC 2 → MVOL L/R on both cores = `v*32767 >> 1` (`SynthEE_v5`, IOP `FUN_00001510`).
  It starts at 0x7FFF.
- **FadePanel** (`FadePanel_v10` 0x112a98): while a screen fade runs with `fade_synth` TRUE (the
  default), master volume = `1 - fadeFraction`, a linear ramp over the fade's ms. This affects
  **everything** on the SPU, streams included. Scripts pass `(fade_synth FALSE)` where the music must
  survive the fade (win screens, the final win anim).
- **Pause:**
  - `synth pause_all_sfx b` and GamePanel SetPaused (`GamePanel_v7` → `FUN_00212fb8`) → RPC 0x2C0 →
    IOP `FUN_000041dc`: VOLL = VOLR = 0 and PITCH = 0 (freezes the voice); unpause restores both.
  - The crowd stream pauses with it (`ArenaPanel_v7` → `FUN_00112280`).
  - BeatMatchAudio pause also pauses MIDI voices (Synth slot 18).
- **Stop:** `stop_sfx name` (`FUN_00213198` → `FUN_00212e70`) keys off every Sfx with that name and
  every fire-and-forget sample with that name. `synth stop_all_sfx` (`FUN_00212ef8`) keys off
  everything and clears the fire-and-forget list. **Key-off = ADSR release** (5.3 ms default,
  0.68 s for postshow_loop). Note: `stop_all_sfx` does not stop sequences, but they die on the next
  poll because their samples stop (GH1 has no loop sequences).

### Voices, limits, stealing [C] (IOP)
- **48 hardware voices:** core 0 voices 0–23 and core 1 voices 0–23 (`FUN_00004698`).
  Voice struct: `{core, voice, sdId, allocated, paused, u16 vol, s16 pan, u16 pitch, u32 freeStamp}`.
- **Ranges:** fx=-1 → voices 0..47 (`FUN_0000405c`); fx=c → voices c*24..c*24+23 (`FUN_0000409c`).
  Stream channels allocate from **the same pool** (`FUN_00002d60`).
- **Allocation** (`FUN_000042ec`): among *unallocated* voices in the range, take the one with the
  largest `freeStamp`. Never-used voices hold 0xFFFFFFFF; a freed voice gets `counter--`, starting
  at 0xFFFFFFFE. The effect is that never-used voices go first, then the voice freed longest ago (LRU).
- **No voice stealing.** If none is free, the sample does not sound. The IOP reports it finished on
  the next tick.
- **Key on/off batching** (`FUN_00004128`, `FUN_00004180`, `FUN_000045dc`): KON and KOFF masks are
  flushed once per 5 ms tick, and a key-off in the same tick cancels a pending key-on.
- **End detection** (`FUN_00000dd0`, `FUN_00004254`): a voice is finished when no key-on is pending,
  ENDX is set and ENVX == 0. The IOP then frees the voice and sends message 0xB(sampleId) to the EE,
  which then reports `IsPlaying` false (`AudioSampleEE_v1`).
- **Destroying a sample** (RPC 0x2BD) keys it off and frees the voice at once. The release tail can
  then be reused by the next allocation.

### EE → IOP RPC opcodes (AudioSampleEE)
| opcode | function | arguments |
|---|---|---|
| 700 | create | `{id, spuAddr, rate}` |
| 0x2BD | destroy | |
| 0x2BE | play / key-on | |
| 0x2BF | stop / key-off | |
| 0x2C0 | pause | `b` |
| 0x2C1 | vol/pan | `{u16 vol, s16 pan}` |
| 0x2C2 | speed | `speed*1000` |
| 0x2C3 | ADSR | |
| 0x2C4 | fx core | |

IOP dispatcher: `FUN_0000173c`.

---------------------------------------------------------------------------------------------------

## 4. Who plays what and when

### Script entry points [C]
- `{play_sfx name (volume v) (transpose semis) (pan p) (stop bool)}` (`FUN_00213038`).
  - It builds a temporary Sfx with **fireAndForget=1** and bank=-1.
  - `stop TRUE` first stops all same-named sounds.
  - The samples are moved to a global list (`DAT_00369710`) and live until they end or are stopped.
    Retriggering does **not** cut earlier instances unless `stop` is given.
  - Defaults: volume 1, transpose 0, pan 0.
- `{stop_sfx name}`.
- `{synth play_sequence name (volume)(transpose)(pan)}` (`FUN_002149b0`). It finds the sequence in
  the loaded `.seq` files, slot order 0..4, first match. The values are applied via
  SetVolume/SetPan/SetTranspose before Play.
- `{synth stop_all_sfx}`, `{synth pause_all_sfx b}`, `set_sfx_global_volume`, `set_fx`, `set_fx_vol`
  (`Synth_v1`).
- Script functions in `config/system_script.dta`:
  - `play_game_sfx $s` = `play_sfx $s (volume {options get_fx_volume})`.
  - `play_game_sfx_player $single $multi $p` picks `$multi` if `{game multiplayer}`, else `$single`,
    and adds `(pan {player_pan $p})`. `player_pan` is -1 for P1 and +1 for P2 in multiplayer, else 0.
  - `play_game_sequence $q` = `synth play_sequence $q (volume fxvol)`.
  - `play_game_sequence_player`: the same, with the single/multi choice and pan.

### Name resolution [C]
An sfx name is a Symbol. With bank = -1, `FUN_00216898` scans the BankManager slots 0..4 in order.
The **first bank whose SfxMap contains the name** wins, and all of that bank's entries for the name
play as layers. If no bank has it, the console prints "WARNING: couldn't find sfx". Sequences work
the same way across the slots' SequenceLoaders (`Synth_v26`).

### Which banks are loaded [C]
Banks load in order through the BankLoader `DAT_002edb74` (`FUN_0010d998`). Its index starts at -1;
each queued bank goes into the next slot via Synth slot 20 (size from the `.nse`, unloading higher
slots), then slot 22 (`.seq` + `.bnk` + stream the `.nse`). `bank reset` sets the index back to -1.
`.bnk`/`.nse` paths are `"%s/gen/%s.%s"` (`FUN_00215b50`), `.seq` is `"%s/gen/%s.seq"`.

| context | slot 0 | slot 1 | slot 2 |
|---|---|---|---|
| Menus (MetaPanel_v13 0x14c670) | `sfx/metagame` (sound banks meta) | – | – |
| In-game (ArenaPanel `FUN_0010d0d0`, queued after the `loading` screen does `stop_all_sfx` + `{game get_bank_loader} reset`) | `venues/<v>/<bank>` (venues.dta `(sound (bank …))`) | the song's `(bank sfx/song_default)` | `sfx/ingame` (sound banks game) |
| Tutorial (`tut_loading`: reset + `arena_panel set_load_banks FALSE`; TutorialPanel queues sound banks tutorial) | `tutorial/tutorial` | – | – |

There are no name collisions within a context. In the tutorial no venue bank is loaded, so the
crowd_* / claps sfx calls fail silently there [C].

### Crowd audio is streamed, not banked [C]
Object `crowd_audio` (`CrowdAudio_v1` 0x112570, ctor `FUN_00111ef8`).
- **Stream files:** `venues/<v>/streams/<file>.vgs`, configured by venues.dta
  `(sound (crowd (intro vol file) (levels (kExcitementBoot vol file) …)))`.
  Basement: intro 0.4 crowd_v1_0intro; Boot 0.3 1danger; Bad 0.3 2poor; Okay 0.2 3norm; Great 0.3
  4good; Peak 0.4 4good.
- **Messages:**
  - `play_intro` plays the intro stream.
  - `set_excitement N` switches to level N's stream.
  - `play_stream vol file` plays any stream (e.g. `wingame` in the final win anim).
  - `turn_off` stops.
  - `set_volume`, `set_enabled`, `set_paused` and `enabled` also exist.
- **Playback:** stereo, ch0 pan -1 and ch1 pan +1. Each stream is created with a jump marker
  (`0xc4(-1.2e-7, 0)`), so it **loops** [I].
- **Volume** = level vol × mixer crowd vol (× !mute) × fx-volume option (`FUN_00112470`).
- The cheers and boos that happen *on transitions* are bank one-shots (below).

### Gameplay and UI triggers (scripts, file:line) [C]
| event | sound(s) | source |
|---|---|---|
| note hit on a star-power gem | seq `sp_gemhit` (random of 4, core-1 reverb, vol 0.22), player pan | config/beatmatch.dta:11 |
| **note miss** (strum or missed gem) | seq `miss_chink` (random of 4..9, vol 0.75), player pan | config/beatmatch.dta:25 |
| SP phrase captured | +200 ms → seq `sp_awarded` / `sp_awarded_multi` | config/player.dta:36 |
| star power ready | `sp_available` / `_multi` (unless game over) | ghui/hud_panel.dta:55 |
| SP deployed (start_using) | `sp_deployed`/`_multi` + stop and replay `crowd_upto_good` | config/player.dta:51-53 |
| SP ends (stop_using) | `sp_depleted`/`_multi` | config/player.dta:68 |
| every beat while any player is in star mode (not tutorial) | +440 ticks → seq `claps` (venue) | arena_game.dta:123 |
| `intro_start` | `crowd_begin` + `crowd_audio play_intro` | arena_game.dta:64 |
| `music_start` | `crowd_upto_norm`; +480 ticks → `crowd_audio set_excitement` | :74 |
| `excitement` change | +480 ticks → `set_excitement` | :144 |
| excitement_boot | stop crowd_upto_poor, play `crowd_dnto_danger` | :149 |
| excitement_bad_rising | stop crowd_dnto_danger, play `crowd_upto_poor` | :154 |
| excitement_bad_falling | stop crowd_upto_norm, play `crowd_dnto_poor` | :159 |
| excitement_okay_rising | stop crowd_dnto_poor, play `crowd_upto_norm` | :164 |
| excitement_okay_falling | stop crowd_upto_good, play `crowd_dnto_norm` | :169 |
| excitement_great | stop crowd_dnto_norm, play `crowd_upto_good` | :174 |
| game_over | stop `sp_deployed`, `sp_deployed_multi` | :185 |
| game_won | `track_exp` (TRACK_EXPLODE_SFX) + `crowd_win` | :191-194 |
| game_lost | `crowd_lose`; song speed ramps 1→0 over 2 s | :211 |
| game_outro | `synth stop_all_sfx` | :235 |
| debug beep (`$debug.beep`) | `click` on beat (not in any bank) | :93 |
| track slides out | `track_unfurl` (TRACK_EXTEND_SFX) | ghui/track_panel.dta:18 |
| track extend sequence | `nowbar_1..5` at +1300/1400/1500/1600/1700 ms (pans -1, -.5, 0, .5, 1) | track_panel.dta:75-109 |
| meters slide in | +250 ms `meter_slide` | hud_panel.dta:45 |
| guitar smash anims (char anim events) | `gtr_smash1/2/3` at the anim's event times | charsys/smash_sfx.dta |
| lose / endgame / multi_compete enter | `crowd_audio turn_off` | lose.dta:11, endgame.dta:10, multiplayer.dta:611 |
| post-show / win / compete screens | +300 ms `postshow_loop` (loops, 0.68 s fade in/out); stopped on leave | endgame.dta:37/59/1230, multiplayer.dta:616 |
| win_easy / win_game | `beat_easy` or `beat_other` + `crowd_win` (plain play_sfx) | endgame.dta:383/498 |
| status screen | `beat_venue` | endgame.dta:753 |
| cash award | `cash_award` per award | endgame.dta:810 |
| high-score flush | `flush` | endgame.dta:1062 |
| store buy OK / fail | `cash_register` / `button_error` | career.dta:1416 |
| cheat entered | `cheat` / `cheat_off` | long_cheats.dta |
| options sliders | `slider_band`, `slider_gtr`, `slider_sfx` with `(volume pos/11)(stop TRUE)` | options.dta:325-343 |
| pause → restart | `stop_all_sfx` + seq `button_play` | pause.dta:57 |
| title/splash and main enter | seq `meta_lick` (random of 10 guitar licks, large studio) | splash.dta:86, main.dta:27 |
| menus background | seq `meta_people`, every `U(10,20)` s while metamusic plays | MetaPanel_v10 0x14c878 + `FUN_0014ca70` |
| tutorial | `bkgd_music` (looping, 1 s attack, 1.4 s release), `tuning_complete`, `s19901`/`s19902`; VO is streamed (`tutorial/streams`); `miss_chink` from `FUN_00156390` | tutorial_*.dta |

**UI handlers** (ghui/sfx.dta, `docs/ui_system.md` message routing). These are not fx-volume scaled.
- **SELECT_MSG:** seq `button_play` if the screen is sel_song / multi_seldiff / qp_diff, or the
  component is pause_restart, pause_controller_resume, lose_restart or comp_restart. Otherwise seq
  `button_select`.
- **SCROLL_MSG:** seq `button_toggle`, unless the focus panel is credits.
- **FOCUS_MSG:** seq `button_toggle`, unless the UI is in a transition.
- **BAD_SELECT_MSG:** `play_sfx button_error`.
- **SCREEN_BACK_MSG:** seq `button_back`, if `{meta is_up}`.

All of these return `kDataUnhandled`, so the normal handling continues.

### Not sfx [C]
- **Whammy** bends the guitar stream; there is no whammy sfx.
- Guitar **muting on a miss** belongs to BeatMatchAudio (streams).
- `kick_drum` / `bass_hit` in midi_triggers.dta are venue animation triggers.
- Metamusic (`meta_loop1a..g`) and crowd audio are VGS streams.
- Unused content: song_default's `mayhem`/m1-m8, the keymaps, the tutorial `s10101..s10115` and
  `s20601..05` bank samples, and the five per-song banks.

---------------------------------------------------------------------------------------------------

## 5. Implementation plan (C++ / SDL3 software mixer)

### Data
- **Loader:** a port of `bnk.py` → `Bank{descs, keymaps, multimap<name, descIdx>, samples}`.
  - Decode each sample's ADPCM to int16 at load (`decode_adpcm` stopping at the END block), storing
    `loopStart` (the block with flag 4) and a `loops` bool (END block flag & 2).
  - Index by name. All entries with the same name are layers.
- **Sequences:** parse `.seq` with the existing DataArray reader. Build a factory for the 11 classes
  of §2; only `random_group_seq` and `sfx_seq` are needed for GH1 data, but `rand` support is cheap.
- **BankManager:** 5 slots. `loadSlot(i, path)` clears slots ≥ i. Lookups go through the slots in
  order. Provide the three context presets of §4 (menus / game / tutorial).

### Engine objects (mirror the original)
- **`Sfx`** fields: `{name, vol=1, pan=0, ratio=1, fireAndForget, voices[]}`.
  - `play()` resolves the layers.
  - Per layer:
    - `v = clamp(vol*global*desc.vol/32767, 0, 1)`
    - `p = clamp(pan + desc.pan/127, -1, 1)`
    - `speed = ratio * 2^((desc.transpose/128)/12)`
    - `core = desc.fx` (-1/0/1)
    - ADSR from the desc
  - Changing vol, pan or transpose later updates the live voices.
- **Fire-and-forget list** (play_sfx): keep the voices until they end. `stop_sfx(name)` keys off
  both the Sfx objects and the list entries.
- **Active-sequence list:** new instance per `play_sequence`; poll every frame; drop when not playing.

### Voice (software SPU2), 48 voices, mixer at 48 kHz
- **Allocation:** pool range by core (0..47 / 0..23 / 24..47); free-voice LRU by free stamp; **no
  stealing**. Streams share the pool if you route them through it; otherwise reserve the stream
  channels' count.
- **Pitch:** `step = (((rate<<12)/48000) * (int)(speed*1000) / 1000)`, clamped to 0x3FFF, in 4.12
  fixed point per output sample. This reproduces the integer truncation.
  - Interpolation: SPU2-style 4-tap Gaussian (the PSX/PS2 table) for authenticity, or cubic.
  - When the read position reaches the END block: if `loops`, jump to `loopStart`; otherwise set
    ENDX, envelope = 0, voice done.
- **Volume and pan:** `L = vol*cos θ`, `R = vol*sin θ`, with `θ = (round(pan*127)+127)/254·π/2`.
  To match the 8-bit table exactly, quantize to `T[i]/255`.
- **ADSR:** implement the SPU2 envelope exactly as in §1, ticking once per output sample (48 kHz).
  - Key-on starts the attack from 0; key-off goes to release.
  - With the default 0x8F1F/0x03C7 this is essentially a gate: 1.1 ms attack, 5.3 ms linear release.
  - Long fades come only from B41F/03CE (0.68 s each way) and B61F/03CF.
- **Command batching:** apply key-on and key-off at a 5 ms granularity (240 samples). A key-off in the
  same quantum as a key-on cancels it. This matters little, but matches the original.
- **Pause:** voice volume 0 and step 0 (frozen), then restore.

### Mix buses
- `dry = Σ voices (L,R)`.
- `send0 = Σ voices with core 0 and fx=0`, `send1 = Σ with fx=1`. The send level is the same L/R as
  the dry signal (the VMIXE gate). Dry voices with fx=-1 do not send.
- `out = (dry + Echo(send0)*0.3 + LargeStudio(send1)*0.4) * master`.
- `master` = FadePanel `1 - fade` (when `fade_synth`) × pause.
- The SFX gain from the options is already folded into each play call (`play_game_*`).

### Reverb approximation
- The SPU2 reverb is the PSX reverb algorithm (24 kHz internal, preset register tables).
- **Best fidelity:** port the psx-spx reverb algorithm with the "Studio Large" and "Echo" preset
  tables. Those tables come from public SPU docs, not from this binary [I], so verify them by ear.
- **Cheap alternative:**
  - **Echo** (core 0): one feedback delay per channel. Wet 0.3, feedback ≈ 38/127 ≈ 0.30, delay =
    the libsd mapping of 64/127 [I: ≈ half the echo work area, roughly 0.3–0.7 s; tune by ear].
    Only the tutorial s19902/s206xx use it in practice.
  - **Large Studio** (core 1): a Schroeder/Freeverb-style room, RT60 ≈ 1.5–2 s, pre-delay ≈ 30–40 ms,
    damping like the SPU's IIR, wet 0.4 [I]. It is used on sp_gemhit, gtr_smash, track_exp,
    beat_*, crowd_lose and meta_lick.
- `chain FALSE`: core 0's output is not fed into core 1's reverb.

### Event list (hooks to add in the game code; scripts already exist for most)
- **Beatmatch:**
  - `hit(star gem)` → seq `sp_gemhit` (fxvol, player pan).
  - `miss` → seq `miss_chink`.
- **Star power:**
  - phrase captured → +200 ms `sp_awarded[_multi]`.
  - ready → `sp_available[_multi]`.
  - deploy → `sp_deployed[_multi]` + restart `crowd_upto_good`.
  - end → `sp_depleted[_multi]`.
  - while any player is in star mode, each beat → +440 ticks seq `claps`.
- **Arena flow:**
  - intro → `crowd_begin` + crowd intro stream.
  - music_start → `crowd_upto_norm` + (+480 ticks) crowd level stream.
  - the six excitement transitions (stop/play pairs above).
  - won → `track_exp` + `crowd_win`.
  - lost → `crowd_lose` (+ music slowdown).
  - game_over → stop `sp_deployed*`.
  - outro → stop_all.
- **Track and HUD:** `track_unfurl`, `nowbar_1..5` (+1300..1700 ms), `meter_slide` (+250 ms).
- **Character:** `gtr_smash1-3` from the smash anim events.
- **UI:** select/play/toggle/back/error per ghui/sfx.dta; sliders; cash/flush/beat_* /
  postshow_loop on the end screens; `meta_lick` on splash/main; `meta_people` every U(10,20) s in
  menus while metamusic plays.
- **Loading:** `stop_all_sfx` + bank reset on entering `loading`; load venue → song_default →
  ingame into slots 0–2.
- **Pause:** pause all sfx + crowd stream; resume restores.
