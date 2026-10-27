# Guitar Hero II (Xbox 360): song audio and charts

How a GH2 song is stored: the `.mogg` audio container, how its channels map
to instruments (from `config/gen/songs.dtb`), and the `.mid` chart layout,
compared with GH1 (see `gd-game-01/docs/FINDINGS.md`, "Song charts"); and
the sound effects (section 4: SynthSample banks, XMA).

Tools (Python 3, stdlib only):

- `tools/mogg.py info|ogg|scan`: header fields, Vorbis channels and rate,
  duration; strip the header to a plain `.ogg`; table of every mogg in a tree.
- `tools/midi_chart.py <mid>|notes|survey`: tracks, tempo map, per part and
  difficulty gem/chord/sustain counts, Star Power, face-off sections, big
  notes, animation text events; `survey` lists pitch and text usage per track
  name over every chart.

Everything below was checked over all 794 moggs and all 84 charts on the
disc, unless marked as inference.

## 1. The .mogg container

A `.mogg` is a small Harmonix header followed by an ordinary Ogg Vorbis
stream. All values are little-endian `u32`.

| Offset | Field | GH2 value |
|---|---|---|
| 0x00 | version | always **10** = unencrypted. (Later Harmonix titles use 11 and up for encrypted Ogg; GH2 has none.) |
| 0x04 | Ogg offset | where `OggS` begins; always `20 + 8 * entry count` |
| 0x08 | map version | always 14 |
| 0x0C | map interval | always 20000 (PCM samples per map entry) |
| 0x10 | map entry count | about `length in samples / 20000` |
| 0x14 | map entries | `count` pairs of `u32 byte offset` (into the Ogg data), `u32 sample` |

The map is a seek table for a streamer that reads the Ogg data in 32 KiB
blocks. Entry `i` gives the last 32 KiB-aligned offset whose sample position
is at or before `i * 20000`, so neighbouring entries often repeat. Offsets
are block starts, not page starts (a reader resyncs on the next `OggS`). The
sample column is a little past the granule of the page that straddles the
block boundary: probably the first sample a decoder can produce after
resyncing there (inference). Both columns rise monotonically in every file.
A PC port does not need the map: seek with the Vorbis decoder.

After the header comes one logical Ogg Vorbis stream (one serial number, no
chaining, Vorbis version 0). `mogg.py ogg` writes it out unchanged, and the
result plays and probes as a normal `.ogg` (`ffprobe` agrees on channels,
rate and duration for every file checked). Vorbis block sizes are
256/2048 in the 117 files at 32 kHz and above and 512/1024 in the 677 at
22050 Hz. `synth.dta` has `(use_xma FALSE)`: the 360 build decodes Vorbis in
software, as the PS2 GH1 did with VAG.

### Song audio formats

The 91 song moggs (74 songs + 17 co-op; `arterialblack` uses
`arterialblack_sp.mogg` for single player) use 4 to 6 channels at
32000, 32250 or 44100 Hz (32250 is unusual but real: the Vorbis header says
it and `ffprobe` agrees):

| Channels | 32000 Hz | 32250 Hz | 44100 Hz |
|---|---|---|---|
| 4 | 7 | 8 | 6 |
| 5 | 10 | 15 | 8 |
| 6 | 7 | 5 | 25 |

Nominal bitrate is 72 or 80 kbps per channel (e.g. 6 ch at 44.1 kHz = 480
kbps). Tutorial songs are 4 ch, 44100 Hz. The full per-song table is at the
end of this file (`docs/songs.md` has the same audio facts with career data).

### The other moggs (703 files)

| Where | Count | Format | What it is |
|---|---|---|---|
| `tutorial/streams/{eng,fre,deu,ita,esl}/` | 5 x 125 | mono, 22050 Hz (5 at 44100) | Tutorial narration, one set per language. `v<lesson><line>` lines named in `tutorial/gen/tutorial_vo.dta` (e.g. `main_intro V10001`, `intro_1 V10101`), plus feedback lines `vgood*`, `vgreat*`, `vmiss*`, `vtimeout*` and `placeholder_bleep`. |
| `tutorial/tutorial1xx, 2xx.mogg` | 9 | 4 ch, 44100 Hz, 81-89 s | Tutorial backing songs (songs.dta entries `tutorial102`..`206`). |
| `world/<venue>/streams/crowd_*` | 8 x 5 | stereo, 22050 (theatre: 44100) | Crowd loops per venue, by excitement: `0intro`, `1danger`, `2poor`, `3norm`, `4good`. Named in `world/<venue>/gen/<venue>.dta` (`sound (crowd (intro ...) (levels (kExcitementBoot ...) ...)))`, with a dB level and pan mode each; `kExcitementPeak` reuses `4good`. `fest` and `stone` share the same `v5` files. |
| `world/<venue>/streams/encore_*` | 8 x 2 | stereo | Encore crowd: `encore_intro`, and `encore_outro` with a loop region (`loop_ms`) in the venue dta. |
| `world/stone/streams/wingame.mogg` | 1 | stereo, 44100, 85 s | Played after beating the game (`char_objects.dta`: `crowd_audio play_stream 0.0 wingame`). |
| `sfx/streams/metaloop*.mogg` | 9 | stereo, 22050, 50-127 s | Menu music: the `metamusic` list in `config/gen/synth.dta` (volume -6 dB, picked at random, plays from memory). |
| `sfx/streams/credits.mogg` | 1 | stereo, 32250, 7:18 | Credits music. |
| `xboxlive/gh2xp0001/songs/rodgort/` | 2 | | Byte-identical copies of `trogdor.mogg` / `trogdor_coop.mogg` (with a chart and `.voc`): a sample download package, not in songs.dta. |

`songs/<id>/<id>.voc` (70 files, also `songs/_blinktrack/`) is not audio;
probably singer lip-sync (inference, not decoded).

## 2. Audio channels to instruments (songs.dtb)

Each songs.dtb entry has a `song` block (and `song_coop` for 17 songs):

```
(song
   (name songs/yyz/yyz)                        ; + ".mogg"
   (tracks ((guitar (2 3)) (bass (4 5))))      ; instrument -> channels
   (pans (-1.0 1.0 -1.0 1.0 -1.0 1.0))         ; per channel, -1 left .. 1 right
   (vols (-3.0 -3.0 -3.0 -3.0 -1.0 -1.0))      ; per channel, dB
   (cores (-1 -1 1 1 -1 -1))                   ; per channel flag
   (midi_file songs/yyz/yyz.mid)
   (hopo_threshold 250))                       ; optional, 31 blocks
```

Rules that hold for all 108 song/co-op blocks whose audio is on the disc:

- Channel count of the mogg = length of `pans` = length of `vols` =
  length of `cores`.
- Channels 0-1 are never listed in `tracks`: they are the stereo backing
  mix (band without the playable parts). Pans are always -1/+1 there.
  (The `budget_*` test entries list only guitar, leaving yyz's bass
  channels 4-5 in the backing.)
- Guitar is always channel 2 (mono) or 2-3 (stereo). The second part, if
  any, is channel 3, 4 or 4-5 and is `bass` (56 songs) or `rhythm` (all
  co-op blocks, plus `gemini` and `billiondollar`'s practice leftovers).
- `cores` is 1 exactly on the guitar channels. Inference: these are the
  channels of the player's own part that drop out on a miss (the same idea as
  GH1's guitar stream).

| Song layout | `tracks` | Songs |
|---|---|---|
| 4 ch | `guitar (2 3)` | 17: exactly the 17 songs that have a co-op mogg |
| 5 ch | `guitar (2 3) bass 4` | 33 (mono bass) |
| 6 ch | `guitar (2 3) bass (4 5)` | 23 |
| 6 ch | `guitar (2 3) rhythm (4 5)` | `gemini` |
| co-op | `guitar (2 3) rhythm (4 5)` / `guitar 2 rhythm 3` / `guitar (2 3) rhythm 4` | 17 `*_coop.mogg` |

Which mogg plays: single player uses `song`; co-op uses `song_coop` when it
exists (the second player gets `rhythm`), otherwise `song` (the second
player gets `bass`). Tutorials list only guitar. The 8 test entries
(`budget_*`) reuse `songs/yyz/yyz.mogg` with charts under
`test/budget_songs/` that are not on the disc.

`song_practice_1..3` blocks (68 songs) point at `<id>_p85/_p65/_p50` (or
`_p90/_p75/_p60`) 2-channel moggs (`bass 0`, `guitar 1` or `guitar 0`,
`rhythm 1`). **None of them is on the 360 disc.** The 360 practice mode
probably slows the full mogg instead (open question). `billiondollar`'s
practice blocks still name `songs/newsong03/...`, a leftover.

Mixing parameters live in `config/gen/beatmatcher.dta` (`audio`):
`multiplayer_scale 0.33`, `multiplayer_attenuation 1.5`,
`background_attenuation 5.0`, `practice_attenuation 7.5`, and `submixes`
used by the tutorials.

## 3. The chart (.mid)

Standard MIDI type 1, 480 ticks per quarter note in all 84 files. Track 0
is the tempo map, named after the song. Tracks are found by name:

| Track | In files | Contents |
|---|---|---|
| `PART GUITAR` | 84 (every song and tutorial) | Lead guitar: gems, Star Power, face-off sections, fret positions, big notes, animation texts. |
| `PART BASS` | 56 | Bass part, same layout. |
| `PART RHYTHM` | 19 | Rhythm guitar, same layout. |
| `PART GUITAR COOP` | 16 | Lead guitar chart used in co-op (a slightly different chart from `PART GUITAR`). |
| `BAND BASS` | 74 | Bass player character: note 36 on each pluck (also drives `speaker_pulse` -> `world bass_hit`); texts `[play]` `[idle]` `[wail_on/off]` `[*_tempo]`. |
| `BAND DRUMS` | 75 | Drummer: 36 = kick (`world kick_drum`), 37 = crash (`world crash_symbal`); texts `[play]` `[idle]` `[allbeat]` `[nobeat]` `[double_time]` `[half_time]` `[*_tempo]`. |
| `BAND SINGER` | 69 | Texts only: `[play]` `[idle]` `[*_tempo]`. |
| `BAND KEYS` | 7 | Texts only: `[play]` `[idle]`. |
| `EVENTS` | 84 | Song structure, lighting cues, crowd and band cues (below). |
| `TRIGGERS` | 75 | 24/25/26 = click kick/snare/hat (`click_parser`), 48/49/50 = lighting next/prev/first keyframe, 52 = `venue_effect`. |

There is no `VENUE`, `ANIM`, camera, vocals or drum-gem track. GH1's `ANIM`
track is folded into the PART tracks; cameras are chosen by the game
(`camera_durations` by excitement in each venue dta), not by the chart.

The parsers that read these tracks are declared in
`config/gen/midi_parsers.dta` (`midi_parsers` in beatmatcher.dta). It
names a `PART GUITAR VS1` / `PART GUITAR VS2` pair for face-off, which no
chart has; the second name in each `track_name` (`'PART GUITAR'`) appears to
be the fallback (inference). Player track choice: `{game get_track_sym n}`
picks `PART BASS`, `PART RHYTHM` or `PART GUITAR COOP` for co-op.
`trogdor` has `song_coop` but no `PART GUITAR COOP`, so its co-op lead must
fall back to `PART GUITAR`.

### PART tracks: note layout

Difficulty `d` = 0 Easy, 1 Medium, 2 Hard, 3 Expert; `base = 60 + 12*d`.

| Pitch | Meaning | GH1 | GH2 |
|---|---|---|---|
| base+0..4 (60-64, 72-76, 84-88, 96-100) | green, red, yellow, blue, orange | same | same. Easy charts use only 3 frets (60-62), Medium 4 (72-75), as a charting rule. |
| base+7 (67, 79, 91, 103) | Star Power phrase | same | same; phrases are usually identical across difficulties. |
| base+9, base+10 (69/70, 81/82, 93/94, 105/106) | face-off player 1 / 2 sections | same | same; only in `PART GUITAR` (all 74 songs). |
| base+5, base+6 | (nothing) | | not used: GH2 has **no forced HOPO notes** (stray 63/66/76/90/95 hits exist, 1-3 each). |
| 40-59 | guitarist fret-hand position, `spot_neck_fret01..20` | in `ANIM` | in the PART track (`player*_fret_pos`) |
| 110 | "big note" (`big_note_parser_guitar/bass/rhythm`) | | new; 291 in guitar, long notes (mostly 3-7 beats; range 1/4 to 32 beats). Effect not traced (probably an animation/camera accent). |
| 108 | singer "open" | in `T1 GEMS` | gone (`BAND SINGER` is text-only; lip-sync is in `.voc`). |
| 60/61 in `TRIGGERS` | kick / bass hit | GH1 | moved to `BAND DRUMS` 36 and `BAND BASS` 36 |

GH1 track `T1 GEMS` became `PART GUITAR`, and GH2 adds bass, rhythm and
co-op lead in the same layout. A GH1-style parser works per part unchanged
(chord window 10 ticks, sustain cut-off 161 ticks: GH1 values, assumed).

HOPO: `config/gen/beatmatcher.dta` `(parser (hopo_threshold 170))`, in
ticks, overridable per song (`hopo_threshold 250` in 9 songs: `dead`,
`kicked`, `monkeywrench`, `onefortheroad`, `rawdog`, `tattooedloveboys`,
`thunderhorse`, `whowasinmyroom`, `xstream`). GH1 hard-coded `<= 169`,
i.e. the same 170 boundary (whether GH2 uses `<` or `<=` is not traced).

Text events in PART tracks (character animation, not scoring): `[play]`,
`[idle]`, `[wail_on]`/`[wail_off]`, `[solo_on]`/`[solo_off]`,
`[ow_face_on]`/`[ow_face_off]`, `[normal_tempo]`/`[half_tempo]`/
`[double_tempo]`, `[map HandMap_Default|DropD2|Solo|NoChords]`,
`[map StrumMap_SlapBass]` (bass). HandMap/StrumMap tables are in
`midi_parsers.dta` (`GUITARFRETMAPPINGS`, `GUITARSTRUMMAPPINGS`,
`bass_strum`). GH2 on the 360 has no solo scoring; `[solo_on]` is a pose.

Gem totals over the 74 songs (chord = one gem):

| Part | Easy | Medium | Hard | Expert |
|---|---|---|---|---|
| PART GUITAR | 31663 | 42128 | 57982 | 67804 |
| PART BASS | 24881 | 33114 | 43803 | 46169 |
| PART RHYTHM | 7537 | 10275 | 13176 | 15090 |
| PART GUITAR COOP | 6417 | 8546 | 11496 | 13565 |

### EVENTS track

| Text | Use |
|---|---|
| `[music_start]`, `[end]` | song start / end (`end` in all 84) |
| `[section <name>]` | practice-mode section names (`intro`, `verse_1`, `chorus_2`, `gtr_solo_a`, `main_riff_1`, `bridge`, `outro`, ...) |
| `[verse]`, `[chorus]`, `[solo]` | song part for camera / crowd (as in GH1) |
| `[lighting (<preset>)]` | venue lighting: `()` default, `flare`, `blackout`, `strobe`, `color1`, `color2`, `chase`, `sweep` |
| `[crowd_lighters_off/slow/fast]`, `[crowd_half/normal/fast_tempo]` | crowd behaviour |
| `[sync_head_bang]`, `[sync_wag]`, `[band_jump]`, `[sync_band_jump]` | band animation cues |

A few charts contain typos (`[allbreat]`, `[doulbe_time]`, `[sectioni bridge]`,
`[lighting(blackout)]` without the space, trailing spaces); a parser should
trim and tolerate them.

Tutorial charts have only `PART GUITAR` and `EVENTS` (`[end]`) at 120 BPM,
although songs.dta gives them `(bpm 108)`.

## 4. Sound effects (SynthSample banks)

[C] = confirmed in the executable (addresses are functions in
`gd-game-02/analysis`), [C data] = checked over every file on the disc,
[I] = inference.

### Where they are

313 SynthSamples in 15 milos [C data]: `sfx/gen/{ingame,metagame,practice,
song_default}_bank`, `tutorial/gen/tutorial_bank`, the eight
`world/<v>/gen/<v>_bank` (a venue chain's last directory, docs/venues.md),
`char/shared/gen/guitar_smashing_sounds` and `system/run/char/gen/chartest`
(an earlier count of 329 was wrong: 313 class-table entries, 313 records).
Rates: 22050 Hz 161, 44100 Hz 144, 16000 Hz 8; 900.6 s in all; 5 loop.
`config/sound.dta` names the banks the Synth keeps loaded: `(banks (game
sfx/ingame_bank) (meta sfx/metagame_bank) (tutorial tutorial/tutorial_bank))`
[C data]; a venue's bank comes with its world. The bank milos also hold
`Sfx` (346), `SfxSeq` (180), `RandomGroupSeq` (39), `SerialGroupSeq` (8),
`WaitSeq` (8) and `FxSendReverb` objects.

`.hxma` is something else: the Synth's streams (`0x826820D8`) open
`<name>.hxma` when `use_xma` is set, else `<name>.mogg` [C]; the retail
`config/synth.dta` has `(use_xma FALSE)` and no `.hxma` is on the disc
[C data]. `XMABuffer` (`0x82687380`) is that streamed reader, not the
samples' path.

### SynthSample v5 and SampleData v11

- **SynthSample::Load** `0x822FEAB8` [C]: u32 revision (< 6); Hmx::Object
  (rev > 1, the usual 13 bytes); file path (`../samples/x.wav`); u8
  looping; i32 loop start; i32 loop end (rev > 2; −1 = the end); then, when
  the Synth loads samples from the bank (a global flag) and rev ≥ 5,
  SampleData::Load; otherwise (rev ≥ 4) it would load the `.wav` from disk.
- **SampleData::Load** `0x823005F8` [C]: u32 revision (< 12); u32 format;
  u32 sample count; u32 rate; u32 size in bytes; u8 **has data** (rev > 10;
  1 when absent); then `size` bytes into physical memory ("SampleData(phys)",
  allocator `*0x8280e6b0`). Older data has no revision: its first u32 is
  the rate (1001–150000 Hz), then the size. The format's byte-size estimate (`0x82300790`): 0 / 1 →
  count × 2 (16-bit PCM), 2 → ((count + 111) / 112) × 64, 3 → count / 5
  (XMA) [C]; every sample on the disc is format 3 [C data].
- **The data is plain XMA2** [C data, decoder-verified]: one mono stream,
  2048-byte packets in stream order. The earlier "packets out of order"
  reading came from starting the data one byte early: the byte before it is
  the has-data flag, so there is no trailing byte. Each packet starts with
  the XMA2 header (big-endian bits: 6 frame count, 15 first-frame bit offset
  after the header, 3 metadata = 1, 8 packet skip = 0); every fourth packet
  has offset 0 (the encoder starts a frame on each 8 KiB block) [C data]; the
  last packet may be the padding packet `0x03FFF800` (no frame, metadata 0).
  FFmpeg's `xma2` decoder (one stream, XMA2WAVEFORMATEX channel mask
  centre) decodes all 313 to exactly their SampleData sample count with no
  error [C data], the start skip and end trim coming from the stream itself.
  Loop points are all inside the sample [C data].
- Python reference (L21; kept outside the repository, it writes decoded
  audio): parse the record, wrap the data in a RIFF `WAVE_FORMAT_XMA2`
  (0x166) header and decode with an FFmpeg built from the same 7.1.5 tarball
  with the `xma2` decoder. Checks over all 313: no decoder error, decoded
  frames = sample count, loops in range, RMS 0.028–0.398, the peaks at full
  scale only on loud hits (guitar smashes, miss chinks); spectrograms show
  ordinary effects (the 44.1 kHz ones band-limited near 16 kHz). gd02's decoder
  matches the reference within 1 LSB (16-bit rounding) on every sample.

### Sfx and the sequences

Each Load was found through the class's ClassName virtual (Sfx
`0x822EDFC8`, SfxSeq `0x822F2E60`, WaitSeq `0x822F30A0`, RandomGroupSeq
`0x822F3A80`, SerialGroupSeq `0x822F3D88`) → vtable (Load six slots later)
[C]; property names from each SyncProperty.

- **Sfx v5** `0x822EE8B0` (props `0x822EE598`) [C]: Hmx::Object (rev > 1);
  `sfxmaps`: u32 count, each SfxMap (`0x822FF580`): sample name (ObjPtr
  SynthSample), and for rev > 2 f32 volume (dB), f32 pan, f32 transpose
  (semitones), i32 fx core (kFXCoreNone / kFXCore0 / kFXCore1, the PS2's
  SPU2 cores: system/run/config/synth_objects.dta), for rev > 3 an ADSR;
  then (rev > 4) `send` (an FxSend: `FXverb.send` / `SPverb.send` on 8,
  else empty) and f32 `send_volume` (1.0 on 341). A stereo sound is two maps
  of mono samples panned −1 and 1 [C data].
- **ADSR v1** `0x822F8E50` [C]: f32 attack_rate, decay_rate, sustain_rate,
  release_rate (seconds), sustain_level, then u32 attack_mode, sustain_mode,
  release_mode (offsets from SyncProperty `0x822EC0E0`). The PS2 packs them
  into SPU2 ADSR registers (`0x822F8CE0`, tables at `0x8204E290`...); the
  360 uses attack_rate and release_rate only (below). In the data: attack
  1.3 ms on 472 maps, 1 ms on 92, up to 1 s on a few; release 5.8 ms / 5 ms,
  up to 1.5 s [C data].
- **FxSendReverb v1** `0x822FC6F0` [C]: FxSend (`0x823000D8`: u32 rev < 2,
  Hmx::Object, next_send ObjPtr, stage i32), then the environment symbol
  (an I3DL2 preset name). Three in the data: ingame_bank `SPverb.send`
  (plate) and `FXverb.send` (small_room), tutorial_bank `SPverb.send`
  (plate); 8 Sfx send to them (sp_gemhit1–4, beat_venue / easy / other,
  drummer_exp) [C data].
- **Sequence v1** `0x822EFB30` (props `0x822EFBE8`) [C]: avg_volume,
  volume_spread, avg_transpose, transpose_spread, avg_pan, pan_spread. The
  sequence records of these revisions carry no Hmx::Object block (SfxSeq
  rev < 3 would).
- **SfxSeq v3** `0x822F1280`: Sequence, the child (ObjPtr Sequence; empty in
  song_default_bank). **WaitSeq v2** `0x822EFFE0`: Sequence,
  avg_wait_seconds, wait_spread. **GroupSeq v3** `0x822F4C20`: Sequence,
  children (u32 count, names). **RandomGroupSeq v1** `0x822F5100`: GroupSeq,
  u32 num_simul. **SerialGroupSeq v1** `0x822F5200`: GroupSeq [C].

### Playing them

How the 360 build plays a sound, from the Synth360 classes (`Synth360`
vtable `0x820D4CF4`, `SampleInst360` vtable `0x820D5C94`, `Voice`
`0x82689618`...) [C unless marked]:

- **Sfx::Play** `0x822ED7F0` (the caller's volume dB, pan, transpose): one
  SampleInst per map (`0x822ED458`). Its volume is the product of three
  linear factors, each through dB → linear (`0x826910A0`: 10^(dB/20), 0 at
  ≤ −96): the caller's, the map's and the Sfx's fader group (the Synth's
  master Fader is in every Sfx's group, Sfx ctor `0x822EE190`) (`0x822FF828`
  / `0x822FF880` / `0x822FF8D8`); the voice clamps it to 0..1 (`0x8268A078`).
  Pan = caller's + map's (`0x822FF6E0`), wrapped to −4..4 as fmod(p + 4, 8)
  − 4 (`0x8268A0A8`). Speed = 2^(caller's transpose / 12) × 2^(map's / 12)
  (`0x822FFAB8`, `0x822FF720`), at least 0.01 and, for XMA, at most 2
  (`0x8268A100`); XAudio gets it as pitch in octaves (12/ln 2 × ln speed /
  12, `0x822FFAF0`, `0x82689BB0`). The map's fx core goes to an empty slot
  (SampleInst360 +0x48 = `0x822F5468`): **the 360 has no FX cores**, and
  config/synth.dta's `(fx (chain) (core_0 kFXModeEcho ...) (core_1
  kFXModeLargeStudio ...))` reach only empty Synth360 slots (+0x4C ... +0x6C,
  set by `0x822E6C20`); `set_fx` / `set_fx_vol` likewise. Re-playing a
  playing instance stops it first (`0x822FF7E8`).
- **The voice** (`0x82689C68`, one XAudio source voice per play): rate =
  int(SampleData rate × 0.9975) (`0x82689768`, the float at `0x820D5C88`) —
  the effects play 0.25 % slow; XMA loop points round down to 128-frame
  multiples. XAudio mixes at 48 kHz in 256-frame passes.
- **Envelope**: GH2's own XAudio effect (id 4, vtable `0x820D4F6C`) on every
  voice; SetADSR `0x8268A590` passes only attack_rate and release_rate
  (×48000 frames, `0x82684430`); Stop (`0x826896F0`) starts the release.
  Process `0x826844C8`: per 256-frame pass the gain moves 60 dB × 256 /
  frames dB (up in the attack, down in the release) from a −60 dB floor,
  stepping linearly in amplitude across the pass; the attack ends at 0 dB
  (then the voice passes through), the release when below −60 dB (then
  silence, and the voice no longer counts as playing: IsPlaying
  `0x826897A8` asks the effect). The first pass starts from gain 0. Decay,
  sustain and the modes are not used on the 360.
- **5.1 pan** (`0x826898C8`): equal power (cos / sin of a quarter turn)
  between neighbouring speakers at −5 RR, −3 RL, −1 FL, 0 C, 1 FR, 3 RR,
  5 RL, times the clamped volume, to the mastering voice (FL FR C LFE RL RR).
- **Sends** (`0x826898C8`, `0x8268A168`): a voice whose Sfx has a send
  (dynamic_cast to FxSend360) also outputs to the send's three submix voices
  (FxSend360 `0x826861D0` / `0x826863D0`: front 2 ch, centre 1 ch, rear 2 ch,
  48 kHz) at send_volume (clamped 0.0001..5) × the pan gains — **not** times
  the voice's volume; for an FxSendReverb the centre also gets half of each
  front and each rear its side's front. Each submix voice runs XAudio's
  reverb (effect id 3) set to the environment's I3DL2 preset (the 30-entry
  table in `0x826848A0`, SetEffectParam(0, 1, I3DL2, 0x30 bytes)); without a
  next_send their output maps take input channels 0 (/ 1) to the master's
  FL FR / C / RL RR (`0x826861D0`).
- **XAudio's reverb** (effect id 3: create `0x826AB9A8`, factory
  `0x826AD5D8`, Process `0x826AD2F0` → `0x826AD0E0` → network `0x826AB9C0`;
  I3DL2 → native `0x826B6A18` → `0x826B6740` → `0x826B4D48`, the same mapping
  as XAudio2's ReverbConvertI3DL2ToNative, delay lengths rounded to primes
  by `0x826BDC20`) [C]: a 16384-sample cross-fading reflections delay, a
  two-pole room filter (`0x826AA970`), tapped delays and all-passes
  (`0x826AAAA0`…`0x826AB320`), a late field of 12 all-passes, 4 two-tap
  delays and 10 decay-EQ filters, 4 output all-passes (`0x826AB048`) and a
  5 ms rear delay (`0x826AB3C8`). Its output is 5.1 (FL FR SL SR written,
  C LFE untouched), wet only (wet = parameter 2, never set by GH2, so 1.0;
  output stage `0x826ACF54`). A mono submix voice feeds both halves of the
  network. gd02's port (`audio/xaudio_reverb.{h,cpp}`) matches the original
  code run in a PowerPC interpreter bit for bit (all 30 presets + 300 random
  parameter sets for the conversion; impulses and noise, preset switches,
  for the output) [C]. That the submix voices' output maps apply to the
  effect's output channels (so each voice's reverb FL / FR return and its SL
  SR do not) is XAudio's voice semantics [I]. An environment not in GH2's
  table makes GH2 read one entry past it (garbage); gd02 uses "default".
- **Master limiter**: GH2's XAudio effect id 9 on the mastering voice
  (`0x82682958`, params `0x82682A68`), from default.dta `(synth (limiter
  (attack_ms 1) (release_ms 50) (threshold -2)))` (Synth360::Init
  `0x82682558`), on from the start; `synth toggle_limiter` flips it
  (`0x826822C8`). Per frame: peak over the channels; above threshold × g the
  divisor g grows 1 / (attack × 48000), else falls 1 / (release × 48000),
  never below 1; the output is divided by g. It covers everything
  (songs, crowd, effects).
- **Stereo**: the master is 5.1; folding it to stereo is the console's
  system software, not GH2 [I: gd02 uses the ITU-R BS.775 Lo/Ro fold, FL +
  0.7071 C + 0.7071 RL; 20 maps pan into the rears].
- **No voice limit or stealing** in GH2's play path (`0x822ED458` →
  `0x8268A3C0` → `0x82689C68`); a failed XAudio voice is silent [C]; gd02
  caps at 256 voices and starts nothing beyond [I].
- **Sequences**: each child gets RandomFloat(avg − spread, avg + spread) (a
  16-bit draw, `0x8226DDA0`) added to volume / transpose / pan (SfxSeq
  `0x822F0398` / `0x822F0410` / `0x822F0488`, groups per child `0x822F0560`
  / `0x822F06A0` / `0x822F0600`); WaitSeq waits avg ± spread (`0x822EF118`).
  **RandomGroupSeq** (`0x822F4F18`): n = min(num_simul, children) (num_simul
  1 on 35, 2 on 4); n = 1 picks RandomInt(0, children) uniformly; otherwise
  selection sampling over the list in order (taken when needed = left, or
  when RandomFloat ≤ needed / left); no repeat rule; num_simul 0 plays
  nothing. SerialGroupSeq: one after another [I: from the instance class].
- **Script messages** (Synth::Handle `0x822EAB08`): `play_sequence <name>
  (volume dB) (pan p) (transpose t)` (`0x822E8A90`; the Sfx's `play` takes
  the same), `stop_all_sfx` (Sfx and sequences), `pause_all_sfx` (Sfx
  instances), `set_master_vol <dB>` / `master_vol` (the master Fader,
  `0x822F7878`; the mute cheat uses kDbSilence), `toggle_limiter`,
  `toggle_hud` (debug), `set_fx` / `set_fx_vol` (PS2 only).
- **The options' effects volume** is not a global gain: `{options
  get_fx_volume}` (dB from the slider table) is passed as the volume of the
  game's own sounds — system_script.dta play_game_sfx / play_game_sequence,
  world_objects_worldbase.dta play_sfx — and of the crowd's reactions
  (`0x822DF350` with `0x822DF140`: fx volume, 0 dB in the attract mode).
  Menu sounds (button_select...) play at 0 dB.
- The crowd's reactions (CrowdAudio `0x822DFAB0`, `0x822DF590`,
  `0x822DF350`) [C]: crowd_begin (intro), crowd_win (game_won_msg),
  crowd_lose (game_lost) and per excitement change
  `crowd_<upto|dnto>_<danger poor norm good peak>` (slot (level + 14) × 2 +
  (previous ≤ level)); a new reaction stops the one playing. The banks have
  upto_poor / norm / good and dnto_danger / poor / norm only: the others
  (dnto_good, upto_peak) are null and silent [C data].
- **The menus' background sounds** (MetaMusic `0x822A23F8`, poll
  `0x822A1C10`): config/synth.dta `(background_sfx (sequence meta_people)
  (min_delay_sec 10) (max_delay_sec 20))`; at start (`0x822A1D60`) and after
  each check the next is due RandomFloat(min, max) s later (`0x822A1440`);
  when due it plays the sequence (0 dB) only if the menu music stream is
  playing, and reschedules either way; stop (`0x822A1330`) clears it [C].
- The options' sound effects volume: `config/sound.dta (options_screen
  (slider -96 -28 -24 -20 -16 -12 -10 -8 -6 -4 -2 0))` dB per index
  [C data].

### In gd02

`scene/synth_objects.{h,cpp}` (the classes; --scenetest decodes all of
them), `audio/xma_decoder.{h,cpp}` (FFmpeg's xma2, the LGPL libraries the
videos use: `--enable-decoder=xma1,xma2` in build_common.sh),
`audio/sfx_dsp.{h,cpp}` (envelope, pan, sends, stereo fold, limiter, dB),
`audio/xaudio_reverb.{h,cpp}` (XAudio's reverb), `audio/sfx_player.{h,cpp}`
(48 kHz voices in 256-frame passes, the send buses, the 5.1 master folded to
stereo), `audio/audio_device.{h,cpp}` (one logical SDL device for the song,
the crowd and the effects, with the master limiter as its postmix) and
`audio/synth.{h,cpp}` (the banks, the sequences, a decode cache: the three
config banks decode at load in ~0.15 s, a venue's bank with the venue).
The UI's `synth play_sequence` / `play_sfx` / `play_meta_sfx` (with their
volume / pan / transpose), `stop_sfx`, `synth stop_all_sfx` /
`pause_all_sfx` / `set_master_vol` / `master_vol` / `toggle_limiter`, the
venues' `<sfx> play` / `stop`, EventTrigger sounds and the crowd's reactions
play. `gd02 --sfxtest` checks the voice DSP against the rules above (numbers
only), decodes every sample and checks the banks, the sends and the names
the scripts play (`GD02_SFX_DUMP=<dir>` writes WAVs).

Not exact: XAudio's sample-rate converter (linear here), the console's
5.1 → stereo fold (ITU here), the limiter running on the device's mix (its
rate and channels) rather than the 48 kHz 5.1 master, the random number
generator (same distributions, not GH2's sequence), SerialGroupSeq's
details [I], the master Fader fade GH2 applies in `0x822D50E0` (not
reproduced), the reverb's pow / log10 / cos coming from the host's C
library (last-bit differences never seen in 330 parameter sets).

## Open questions

1. Practice mode: the `_pNN` moggs are not on the 360 disc. Does the game
   time-stretch the full mogg, or is practice audio missing?
2. Exact meaning of the mogg map's sample column (resync point vs granule).
3. Note 110 "big note": what it triggers.
4. HOPO comparison (`<` or `<=` 170), chord window and sustain cut-off in
   GH2: need the 360 executable.
5. `cores`: confirm it is "mute on miss".
6. `.voc` format.

## Per-song audio and charts

`Channel map`: channels not listed are the backing mix (0-1). `Chart parts`:
PART tracks in the song's MIDI. `HOPO`: per-song `hopo_threshold` (default 170).

| Song | Audio file | Ch | Rate (Hz) | Length | Channel map (0-1 = backing) | Chart parts | HOPO |
|---|---|---|---|---|---|---|---|
| badreputation | badreputation | 5 | 32250 | 3:15 | guitar 2+3 bass 4 | guitar, bass |  |
| beastandtheharlot | beastandtheharlot | 6 | 44100 | 5:43 | guitar 2+3 bass 4+5 | guitar, bass |  |
| billiondollar | billiondollar | 5 | 44100 | 3:39 | guitar 2+3 bass 4 | guitar, bass |  |
| cantyouhearme | cantyouhearme | 4 | 32250 | 4:31 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| cantyouhearme (co-op) | cantyouhearme_coop | 6 | 32250 | 4:31 | guitar 2+3 rhythm 4+5 |  |  |
| carrymehome | carrymehome | 6 | 44100 | 3:13 | guitar 2+3 bass 4+5 | guitar, bass |  |
| carryonwayward | carryonwayward | 5 | 32250 | 5:21 | guitar 2+3 bass 4 | guitar, bass |  |
| cherrypie | cherrypie | 6 | 44100 | 3:17 | guitar 2+3 bass 4+5 | guitar, bass |  |
| crazyonyou | crazyonyou | 5 | 32250 | 4:54 | guitar 2+3 bass 4 | guitar, bass |  |
| dead | dead | 4 | 44100 | 3:17 | guitar 2+3 | guitar, guitar coop, rhythm | 250 |
| dead (co-op) | dead_coop | 6 | 44100 | 3:17 | guitar 2+3 rhythm 4+5 |  | 250 |
| freebird | freebird | 4 | 44100 | 9:25 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| freebird (co-op) | freebird_coop | 6 | 44100 | 9:26 | guitar 2+3 rhythm 4+5 |  |  |
| freya | freya | 6 | 44100 | 4:54 | guitar 2+3 bass 4+5 | guitar, bass |  |
| girlfriend | girlfriend | 4 | 32250 | 3:46 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| girlfriend (co-op) | girlfriend_coop | 4 | 32250 | 3:45 | guitar 2 rhythm 3 |  |  |
| hangar18 | hangar18 | 5 | 32250 | 5:13 | guitar 2+3 bass 4 | guitar, bass |  |
| heartshapedbox | heartshapedbox | 6 | 44100 | 5:02 | guitar 2+3 bass 4+5 | guitar, bass |  |
| hush | hush | 6 | 44100 | 3:43 | guitar 2+3 bass 4+5 | guitar, bass |  |
| institutionalized | institutionalized | 4 | 32000 | 3:50 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| institutionalized (co-op) | institutionalized_coop | 6 | 32000 | 3:50 | guitar 2+3 rhythm 4+5 |  |  |
| jessica | jessica | 4 | 44100 | 6:01 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| jessica (co-op) | jessica_coop | 6 | 44100 | 6:04 | guitar 2+3 rhythm 4+5 |  |  |
| johnthefisherman | johnthefisherman | 6 | 32000 | 3:49 | guitar 2+3 bass 4+5 | guitar, bass |  |
| killinginthenameof | killinginthenameof | 5 | 32250 | 5:16 | guitar 2+3 bass 4 | guitar, bass |  |
| laidtorest | laidtorest | 4 | 32250 | 3:53 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| laidtorest (co-op) | laidtorest_coop | 6 | 44100 | 3:53 | guitar 2+3 rhythm 4+5 |  |  |
| lastchild | lastchild | 4 | 32250 | 4:23 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| lastchild (co-op) | lastchild_coop | 6 | 32250 | 4:23 | guitar 2+3 rhythm 4+5 |  |  |
| lifewasted | lifewasted | 5 | 44100 | 3:54 | guitar 2+3 bass 4 | guitar, bass |  |
| madhouse | madhouse | 5 | 32250 | 4:14 | guitar 2+3 bass 4 | guitar, bass |  |
| messageinabottle | messageinabottle | 6 | 44100 | 5:07 | guitar 2+3 bass 4+5 | guitar, bass |  |
| misirlou | misirlou | 4 | 32000 | 2:41 | guitar 2 bass 3 | guitar, bass |  |
| mother | mother | 4 | 44100 | 3:32 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| mother (co-op) | mother_coop | 6 | 44100 | 3:32 | guitar 2+3 rhythm 4+5 |  |  |
| monkeywrench | monkeywrench | 6 | 44100 | 3:53 | guitar 2+3 bass 4+5 | guitar, bass | 250 |
| possum | possum | 6 | 44100 | 5:02 | guitar 2+3 bass 4+5 | guitar, bass |  |
| psychobilly | psychobilly | 5 | 44100 | 2:42 | guitar 2+3 bass 4 | guitar, bass |  |
| rockandroll | rockandroll | 5 | 44100 | 3:44 | guitar 2+3 bass 4 | guitar, bass |  |
| rockthistown | rockthistown | 6 | 32250 | 3:26 | guitar 2+3 bass 4+5 | guitar, bass |  |
| salvation | salvation | 6 | 44100 | 2:52 | guitar 2+3 bass 4+5 | guitar, bass |  |
| searchanddestroy | searchanddestroy | 5 | 32000 | 3:41 | guitar 2+3 bass 4 | guitar, bass |  |
| shoutatthedevil | shoutatthedevil | 5 | 32250 | 3:26 | guitar 2+3 bass 4 | guitar, bass |  |
| stop | stop | 5 | 32250 | 3:58 | guitar 2+3 bass 4 | guitar, bass |  |
| strutter | strutter | 4 | 44100 | 3:17 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| strutter (co-op) | strutter_coop | 6 | 44100 | 3:19 | guitar 2+3 rhythm 4+5 |  |  |
| surrender | surrender | 5 | 32250 | 4:21 | guitar 2+3 bass 4 | guitar, bass |  |
| sweetchild | sweetchild | 6 | 32250 | 5:57 | guitar 2+3 bass 4+5 | guitar, bass |  |
| tattooedloveboys | tattooedloveboys | 5 | 32000 | 3:01 | guitar 2+3 bass 4 | guitar, bass | 250 |
| thembones | thembones | 5 | 32250 | 2:42 | guitar 2+3 bass 4 | guitar, bass |  |
| tonightimgonna | tonightimgonna | 4 | 32250 | 2:38 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| tonightimgonna (co-op) | tonightimgonna_coop | 6 | 32250 | 2:39 | guitar 2+3 rhythm 4+5 |  |  |
| trippinonahole | trippinonahole | 5 | 32250 | 2:59 | guitar 2+3 bass 4 | guitar, bass |  |
| trooper | trooper | 5 | 44100 | 4:14 | guitar 2+3 bass 4 | guitar, bass |  |
| warpigs | warpigs | 6 | 32000 | 7:40 | guitar 2+3 bass 4+5 | guitar, bass |  |
| whowasinmyroom | whowasinmyroom | 4 | 44100 | 3:16 | guitar 2+3 | guitar, guitar coop, rhythm | 250 |
| whowasinmyroom (co-op) | whowasinmyroom_coop | 6 | 44100 | 3:17 | guitar 2+3 rhythm 4+5 |  | 250 |
| woman | woman | 5 | 32250 | 2:52 | guitar 2+3 bass 4 | guitar, bass |  |
| youreallygotme | youreallygotme | 5 | 32000 | 2:41 | guitar 2+3 bass 4 | guitar, bass |  |
| yyz | yyz | 6 | 44100 | 4:30 | guitar 2+3 bass 4+5 | guitar, bass |  |
| arterialblack | arterialblack_sp | 4 | 32000 | 3:19 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| arterialblack (co-op) | arterialblack_coop | 6 | 44100 | 3:24 | guitar 2+3 rhythm 4+5 |  |  |
| collide | collide | 6 | 44100 | 3:18 | guitar 2+3 bass 4+5 | guitar, bass |  |
| elephantbones | elephantbones | 5 | 32000 | 3:33 | guitar 2+3 bass 4 | guitar, bass |  |
| fallofpangea | fallofpangea | 6 | 44100 | 7:02 | guitar 2+3 bass 4+5 | guitar, bass |  |
| ftk | ftk | 5 | 32000 | 3:14 | guitar 2+3 bass 4 | guitar, bass |  |
| gemini | gemini | 6 | 32000 | 4:20 | guitar 2+3 rhythm 4+5 | guitar, rhythm |  |
| jordan | jordan | 5 | 32000 | 3:54 | guitar 2+3 bass 4 | guitar, bass |  |
| kicked | kicked | 5 | 44100 | 3:37 | guitar 2+3 bass 4 | guitar, bass | 250 |
| ladylightning | ladylightning | 5 | 32250 | 4:45 | guitar 2+3 bass 4 | guitar, bass |  |
| laughtrack | laughtrack | 4 | 32250 | 2:46 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| laughtrack (co-op) | laughtrack_coop | 5 | 32250 | 2:47 | guitar 2+3 rhythm 4 |  |  |
| lesstalkmorerokk | lesstalkmorerokk | 4 | 32000 | 5:02 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| lesstalkmorerokk (co-op) | lesstalkmorerokk_coop | 6 | 32000 | 5:01 | guitar 2+3 rhythm 4+5 |  |  |
| mrfixit | mrfixit | 5 | 44100 | 2:37 | guitar 2+3 bass 4 | guitar, bass |  |
| newblack | newblack | 5 | 32000 | 2:52 | guitar 2+3 bass 4 | guitar, bass |  |
| onefortheroad | onefortheroad | 6 | 44100 | 4:26 | guitar 2+3 bass 4+5 | guitar, bass | 250 |
| parasite | parasite | 6 | 32000 | 3:24 | guitar 2+3 bass 4+5 | guitar, bass |  |
| radiumeyes | radiumeyes | 6 | 32000 | 3:30 | guitar 2+3 bass 4+5 | guitar, bass |  |
| redlottery | redlottery | 6 | 44100 | 5:57 | guitar 2+3 bass 4+5 | guitar, bass |  |
| rawdog | rawdog | 6 | 44100 | 3:28 | guitar 2+3 bass 4+5 | guitar, bass | 250 |
| six | six | 5 | 32000 | 3:23 | guitar 2+3 bass 4 | guitar, bass |  |
| soybomb | soybomb | 4 | 32000 | 2:39 | guitar 2+3 | guitar, guitar coop, rhythm |  |
| soybomb (co-op) | soybomb_coop | 4 | 32250 | 2:39 | guitar 2 rhythm 3 |  |  |
| thelightthatblinds | thelightthatblinds | 5 | 32000 | 5:03 | guitar 2+3 bass 4 | guitar, bass |  |
| thunderhorse | thunderhorse | 5 | 32000 | 2:34 | guitar 2+3 bass 4 | guitar, bass | 250 |
| trogdor | trogdor | 4 | 32000 | 1:42 | guitar 2+3 | guitar, rhythm |  |
| trogdor (co-op) | trogdor_coop | 4 | 32000 | 1:43 | guitar 2 rhythm 3 |  |  |
| xstream | xstream | 5 | 32250 | 5:02 | guitar 2+3 bass 4 | guitar, bass | 250 |
| drinkup | drinkup | 6 | 44100 | 2:48 | guitar 2+3 bass 4+5 | guitar, bass |  |
| yeswecan | yeswecan | 5 | 44100 | 4:11 | guitar 2+3 bass 4 | guitar, bass |  |
| tutorial102 | tutorial102 | 4 | 44100 | 1:21 | guitar 2+3 | guitar |  |
| tutorial103 | tutorial103 | 4 | 44100 | 1:21 | guitar 2+3 | guitar |  |
| tutorial104 | tutorial104 | 4 | 44100 | 1:21 | guitar 2+3 | guitar |  |
| tutorial105 | tutorial105 | 4 | 44100 | 1:21 | guitar 2+3 | guitar |  |
| tutorial202 | tutorial202 | 4 | 44100 | 1:21 | guitar 2+3 | guitar |  |
| tutorial203 | tutorial203 | 4 | 44100 | 1:21 | guitar 2+3 | guitar |  |
| tutorial204 | tutorial204 | 4 | 44100 | 1:21 | guitar 2+3 | guitar |  |
| tutorial205 | tutorial205 | 4 | 44100 | 1:28 | guitar 2+3 | guitar |  |
| tutorial206 | tutorial206 | 4 | 44100 | 1:28 | guitar 2+3 | guitar |  |
