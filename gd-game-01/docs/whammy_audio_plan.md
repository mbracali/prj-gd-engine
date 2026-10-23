# Whammy pitch bend of the guitar stream (GH1 PS2) and how to reproduce it in the Mixer

Marks: **[C]** confirmed in code or data, **[I]** inferred.
EE addresses refer to `original_files/SLUS_212.24` (`analysis/decomp/*.c`). IOP addresses refer to
`original_files/IOP/SYNTH_S.IRX` (the Ghidra output from the audio_sfx session, `scratchpad/irx.c`).

## 1. Summary

- The whammy does **not** pitch-shift. The guitar channels of the song stream (`slip_tracks`, e.g. `(2 3)`)
  play through a second SPU voice, the **slip voice**. During a whammy that voice runs at
  `speed = 2^(semitones/12) <= 1`, so the guitar plays **lower and slower** than the song and falls
  behind it [C].
- The bend only goes **down**, by at most `pitch_bend_range` = **1 semitone** (speed 0.94387). There is no
  oscillation in the audio code; any warble comes from the player moving the bar [C].
- Bend depth = `whammy × clamp((now − hitTime) / ms_to_full_pitch_bend, 0, 1) × range`, with
  `ms_to_full_pitch_bend` = **1000 ms**. The depth grows linearly over the first second after the note was
  **struck**. It does not ramp with the bar's own movement [C].
- The bend applies only while a hit sustain is held, and only after the bar has been at rest
  (exactly 0) at least once since the hit [C].
- When the lag reaches **0.9 × max_slip = 270 ms**, the speed snaps back to 1.0 and the guitar stays
  270 ms late [C].
- The lag resets **only** by a hard jump back to the song position. That happens at the next hit, and at
  every following gem time while the player is still in the "hit" state [C].
- `quick_bend_*` has nothing to do with this. It belongs to the debug "auto solo" MIDI improviser
  (the `soloer`), and its `quick_bend_frequency` is 0.0 [C].

## 2. Config values

| key | value | file:line | read by | stored at |
|---|---|---|---|---|
| `pitch_bend_range` | `1` (int, semitones) | `extracted_dta/config/gen/beatmatch.dta:53` (`beatmatcher/watcher`) | `FUN_00262d10` (watcher ctor), `FUN_00235c20` = int | watcher+0x80, default 1 [C] |
| `ms_to_full_pitch_bend` | `1000` (int, ms) | `beatmatch.dta:54` | same | watcher+0x84, default 1000 [C] |
| `slop` | `100` (ms) | `beatmatch.dta:51` | same (float) | watcher+0x3c [C] |
| `cheat_error` | `0` | `beatmatch.dta:52` | same | watcher+0x34 [C] |
| `max_slip` | `300` (ms) | `extracted_dta/config/gen/synth.dta:5` (`iop/max_slip`). Also `system/run/config/gen/default.dta:87` | `FUN_00288b68` (SlipTrack init) and the EE stream (`DAT_0042bb04`) | SlipTrack[6] [C] |
| `slip_tracks` | `((2 3))` per song | `extracted_dta/config/gen/songs.dta:11-14` and every song | `UnmuteBeatMatchAudio_v22` | audio+0xe4 list [C] |
| `audio` mode | `unmute` | `beatmatch.dta:59-66` | `FUN_00257270` picks `UnmuteBeatMatchAudio` (`slipstream` = cheat alternative, `cheats_funcs.dta:95-115`) | [C] |
| `quick_bend_delay/duration/frequency` | `10 / 100 / 0.0` | `extracted_dta/_up_/_up_/system/run/config/gen/beatmatch.dta:10-12` (`soloer`) | `FUN_0025de68` (Soloer ctor) | soloer+0x24/+0x28/+0x2c [C] |

## 3. The control chain (EE) [C]

Each frame the watcher Poll (`FUN_002630b0`) runs, in this order:
1. Sustain end. If `now > gemTime + duration`, it calls `FUN_002640d8`.
2. Autoplay.
3. Sweep/pass.
4. Gem-time crossings (`FUN_00263db0`).
5. The whammy step `FUN_00263e60(now)`:

```
w      = BeatMatcher_v3()            // controller slot: GuitarController_v5 / JoypadController_v5 = min(axis, 0) → [-1, 0]
if (w == 0.0f) armed(+0x7c) = 1
if (sustainGem(+0x60) == -1) {       // not sustaining
    sinks.whammy(0); audio.SetPitchBend(0 semitones)      // speed 1.0 every frame
} else if (!armed) {
    sinks.whammy(0)                                         // no pitch call: speed stays at what the hit reset it to (1.0)
} else {
    sinks.whammy(w)
    ramp = clamp((now - hitTime(+0x88)) / ms_to_full(+0x84), 0, 1)
    audio.SetPitchBend(w * ramp * range(+0x80))             // semitones in [-1, 0]
}
```

- `hitTime` (+0x88) is the **time of the hit**, not the gem time. `FUN_00263728` sets it when the hit
  gem has a duration (its param_1 = now).
- `FUN_002640d8` is called on a hit (`FUN_00263728`), a miss or overstrum (`FUN_002641e0`, 50 ms
  debounce), a pass (`FUN_002642e8`), an early fret release during a sustain (`FUN_00263578` →
  `FUN_00264018`), the natural sustain end, and a reset. It calls `audio.ResetPitchBend()`, clears
  the sustain and sets **armed = 0**.
- `BeatMatcher_v7` (slot 0x44) = SetPitchBend: `speed = powf(1.0594631 /*0x3f879c7d = 2^(1/12)*/, semis)`.
  It then calls the audio slot 0x104.
- `BeatMatcher_v8` (slot 0x4c) = ResetPitchBend: calls the audio slot 0x10c.
- `UnmuteBeatMatchAudio_v31` (0x104, SetSpeed), for **every** slip track in audio+0xe4:
  `if |GetSlipOffsetMs(track)| > max_slip * 0.9 → SetSlipSpeed(1.0) else SetSlipSpeed(speed)`.
- `UnmuteBeatMatchAudio_v32` (0x10c): if the stream is ready, `SetSlipSpeed(1.0)` on all slip tracks.

### Sink callbacks that move the slip voice (UnmuteBeatMatchAudio, primary vtable 0x2fd648) [C]

| watcher event (slot) | Unmute handler | effect |
|---|---|---|
| Hit (0x24, `FUN_00264120`) | `_v3` | `SlipTo(track, 0)` (`FUN_002658f0` → `FUN_00288cb8`): re-sync the guitar to the song. Sets hit-state +0xf4=1 and +0xf8=gem |
| Gem time reached (0x14, `FUN_002645d0`) | `_v1` | if hit-state: `SlipTo(track, 0)` |
| Miss (0x2c) / Pass (0x34) / 0x3c | `_v4/_v5/_v6` | `StopSlip` (`FUN_00265918` → `FUN_00288e70`): the slip voice is freed, so the guitar is **silent**. Clears hit-state (`_v4/_v5`) |
| HOPO window expired unstrummed (0x94) | `_v18` | if gem == +0xf8 and gem flag bit 9 is clear: StopSlip, clear hit-state [I meaning] |
| Poll (0xdc) | `_v26` | timed StopSlip (SlipTrack[5]); only SlipStream mode sets it |

So in Unmute mode the **miss mute is the slip voice being killed**, and a hit restarts it in sync. Our
`guitarGain_` fade is already the miss part.

## 4. The audio side

SlipTrack (`FUN_00288738/888b0/b68`) holds `{stream, channels[], …, [5] endTime, [6] max_slip, [7] slipping}`.
It calls these Stream vtable slots per channel [C]:

| slot | StreamEE | RPC | IOP |
|---|---|---|---|
| 0xd4 EnableSlip | `_v25` (sets the channel flag) | — | channel flag +0x4058 |
| 0xdc SlipTo(ms) | `_v26`: clamp(ms, ±max_slip), `samples = ms*rate/1000` | 0x19b | `FUN_00003264` |
| 0xe4 StopSlip | `_v27` | 0x19c | `FUN_0000321c` (free the slip voice) |
| 0xec GetSlipOffset | `_v28`: `offsetSamples / rate * 1000` (ms) | status | written by `FUN_00003384` |
| 0xf4 SetSpeed(f) | `_v29`: `speed1000 = (int)(f*1000 ± 0.5)` | 0x19d | `FUN_00003098` → `FUN_00002fcc` |

IOP stream channel with slip enabled [C] (`FUN_00002e34`):
- The **main voice** (+0xc) plays the ring buffer at the normal rate with **volume 0**. It is only the
  clock.
- The audible **slip voice** (+8) is created by SlipTo at
  `mainVoiceAddr + (offsetSamples/28)*16`. ADPCM is 28 samples per 16 bytes, so the position is
  block-quantised. There is a wrap in the ring, a new voice and key-on, and **no crossfade**.
- A channel can be linked (+0x4064) to another channel's slip voice, so the stereo pair stays together.
- The slip voice pitch is `FUN_000051e4(rate, rate1000*speed1000/1000)`. That is the stream pitch × speed,
  which is a **real resample: pitch and tempo change together**.
- The IOP measures the offset every tick as `(slipAddr − mainAddr)` in samples, wrapped to ±half the ring.

Resulting behaviour of a full whammy, held 1 s after the hit:
- The lag grows at `(1 − 0.94387)` = 56 ms per second of song.
- It reaches 270 ms after about 4.8 s. Then the speed is 1.0: normal pitch, still 270 ms late.
- After a release, a sustain end or a reset, the speed is 1.0 and the lag stays.
- The lag disappears with a jump forward at the next gem time (if still in hit state) or at the next hit.
  A miss silences the guitar instead. [C]

### Multiplayer
- `_v31` and `_v32` loop over all slip tracks of that audio object [C].
- Whether the two players share one object is not traced [I].

## 5. quick_bend (not the whammy) [C]

- `FUN_0025de68` is the **Soloer** ctor. The BeatMatcher builds it from the song's
  `(solo (riffs standard))` (`FUN_00258fc8`, key `solo`).
- It is only active while BeatMatcher+0xb8 == 1. That mode is entered only when `auto_solo` is set
  (`PlayerMatcher_v1` `set_auto_solo`, cheat `toggle_auto_solo` in `cheats_funcs.dta:395-410`) and a
  solo section is under the now line (`FUN_0025a770`).
- While it is active, fret, strum and whammy input drives a MIDI-synth improviser (riffs, ranges, key).
- The `quick_bend_*` keys are synth pitch-bend events for those generated notes [I: likely a
  bend-up `delay` ms after a note, lasting `duration` ms, with probability `frequency`].
- Because `frequency` = 0.0, they never fire.
- **Ignore for the port.**

## 6. Implementation plan for gd01

### 6.1 Mixer: a guitar delay line (the same model as the slip voice)

Because the speed is always ≤ 1, the slip voice is always **behind** the song. Model it as a variable
delay line on the guitar channels:
- `d` = lag in source frames, `d >= 0`.
- Each output frame: `d += 1 − speed`.
- The guitar sample = the guitar history read at `writePos − d`.

This is exactly the IOP behaviour: pitch and tempo both scale by `speed`, and the lag accumulates. It
needs no look-ahead, and the song clock is untouched.

New Mixer state (audio thread unless noted):
```cpp
// public, main thread
void setGuitarSlipSpeed(float s)  { slipSpeed_.store(s); }   // 0.94387..1
void resyncGuitar()               { slipResync_.fetch_add(1); }
double guitarSlipMs() const       { return slipLagMs_.load(); } // >= 0 (the original reports it negative)
// private
std::atomic<float> slipSpeed_{1};
std::atomic<int>   slipResync_{0};  int slipResyncSeen_ = 0;
std::atomic<double> slipLagMs_{0};
double slipLag_ = 0;                          // d, in source frames
static constexpr int kSlipRing = 16384;       // >= 300 ms at 48 kHz (14400) + margin; power of two
std::vector<std::array<float,kSlipRing>> slipHist_;   // one per guitar channel
uint32_t slipWrite_ = 0;
```

In `Mixer::fill`, after `scratch_` is decoded at song speed (the existing `songSpeed_` path stays as is),
do this per audible frame `i`:
```cpp
const float sp = slipSpeed_.load();          // read once per callback (the original updates per game frame)
if (slipResync_.load() != slipResyncSeen_) { slipResyncSeen_ = slipResync_.load(); slipLag_ = 0; xfade = kXfade; }
for (i...) {
    for each guitar channel c: hist[c][slipWrite_ & (kSlipRing-1)] = scratch_[c][i];
    slipLag_ = std::min(slipLag_ + (1.0 - sp), maxLagFrames);   // maxLagFrames = max_slip(300 ms)*rate/1000
    double rp = double(slipWrite_) - slipLag_;
    // 4-point Hermite (or the SPU 4-tap Gaussian) between rp-1 .. rp+2; rp+2 <= slipWrite_ needs slipLag_ >= 2,
    // so for slipLag_ < 2 use linear between floor(rp) and slipWrite_, and for slipLag_ == 0 use scratch_ directly.
    guitarSample[c] = interp(hist[c], rp);
    ++slipWrite_;
}
slipLagMs_.store(slipLag_ * 1000.0 / rate);
```
- Then mix `guitarSample[c]` in place of `scratch_[c][i]` for `t.guitar` channels, keeping `guitarGain_`
  and `guitarVolume_`.
- On `play()`/`stop()`/seek: clear `slipHist_` and `slipLag_ = 0`, and set the speed to 1.
- Keep filling the history while the guitar is muted, so a resync after a miss is clean.
- **Resync click:** the original jumps hard (SPU key-on, no crossfade) [C]. A 5 ms (≈220 frames) linear
  crossfade from the old read head to `d = 0` is recommended [I, port nicety]. With `d` < 1 frame,
  skip the fade.
- Do the delay on the **pre-pan per-channel** samples, so the stereo pair (2,3) stays locked. That matches
  the IOP link +0x4064.
- Optional fidelity: quantise the speed like the original:
  `s1000 = int(speed*1000 + 0.5)`, then
  `pitch = ((rate<<12)/48000) * s1000 / 1000`, then `speedEff = pitch / ((rate<<12)/48000)`.
  The difference is under 0.1 %.
- Slip channels: parse `slip_tracks` from songs.dta. In every GH1 song it equals the `cores == 1`
  channels that `Track::guitar` already marks [I], so `t.guitar` can be reused.

### 6.2 Game side: per-player whammy bend (port of `FUN_00263e60` + `_v31`/`_v32`)

Add to `Player`:
- `bool bendArmed_` (+0x7c).
- `float bendSpeed_ = 1`.
- `bendHitMs_` (+0x88): reuse `heldSinceMs_`, which `hitGem()` already sets to the hit time.

Do these steps:
1. In `endSustain(t)`, which is the port of `FUN_002640d8` and is reached from hit, overstrum, pass,
   early release and natural end, add `bendArmed_ = false; bendSpeed_ = 1;`.
   Check that the pass path (`FUN_002642e8`) and the miss path also go through `endSustain`. In the
   original, both call `FUN_002640d8`.
2. Every poll, **after** `pl.update(t)` (the original runs it last in Poll), with `w = whammyValue()`
   (engine 0..1 = −original):
   ```cpp
   constexpr float kRange = 1;            // pitch_bend_range  beatmatch.dta:53
   constexpr float kMsToFull = 1000;      // ms_to_full_pitch_bend beatmatch.dta:54
   constexpr float kMaxSlipMs = 300;      // synth.dta:5
   if (w <= 0.0f) bendArmed_ = true;      // original: value == 0.0f exactly
   float speed = 1;
   if (sustainGem_ >= 0 && bendArmed_) {
       float ramp = std::clamp((t - heldSinceMs_) / kMsToFull, 0.0f, 1.0f);
       float semis = -w * ramp * kRange;                   // [-1, 0]
       speed = std::pow(1.0594631f, semis);                // 0.94387 .. 1
   } else if (sustainGem_ >= 0) speed = bendSpeed_;      // not armed: leave as is (1 after the hit)
   bendSpeed_ = speed;
   ```
3. In App, after both players are polled, apply `_v31`:
   `if (mixer_.guitarSlipMs() > 0.9 * kMaxSlipMs) speed = 1;` then `mixer_.setGuitarSlipSpeed(speed)`.
   - For one shared guitar track in face-off, use the speed of the player who owns the current
     section [I]. The original applies one audio object's call to all of its slip tracks.
4. Resync (`SlipTo 0`): call `mixer_.resyncGuitar()`:
   - on every `PlayerEvent::Hit`, which is Unmute `_v3`, called after `FUN_002640d8` reset the speed;
   - each time a gem's time passes the now line while the player is in hit state, which is `_v1`.
     "Hit state" = last judgement was a hit (`guitarAudible_` true and set by a hit). The gem-time
     crossing is `FUN_00263db0`: for every gem whose `ms < t` that has not yet been crossed (a running
     index), when it is a playable gem for this player (`FUN_002640a8`).
   - A resync while the lag is 0 is a no-op, so calling it freely is safe.
5. A miss or pass already mutes the guitar through `setGuitarAudible(false)`, which is the StopSlip
   equivalent. On the next hit, resync **before** or at the same time as un-muting, so the fade-in
   starts in sync.
6. The digital whammy wiggle (`0.3 + 0.3 sin`) never reaches exactly 0, so the bend would never arm. For
   keyboard play, arm on `w <= 0.01` [I], or arm when the key is pressed. The analog bar at rest gives
   0 (`min(axis,0)` clips the positive side) [C/I].
7. Pause: the slip voice pauses with the stream (IOP state 3 restores its pitch × speed) [C]. Freeze
   `slipLag_`, which happens naturally because `fill` returns early.

### 6.3 Expected result / test
- Hold a 3 s sustain and press the bar fully at once.
  - The pitch drops smoothly from 0 to −1 semitone over the first second after the strum.
  - It then stays at −1 semitone.
  - The lag at 3 s is ≈ (1 s × 0.028 average during the ramp + 2 s × 0.056) ≈ 140 ms.
- At the next note the guitar jumps 140 ms forward, back in sync with the drums.
- Hold the bar down before strumming: no bend until the bar is released to rest once.
- Short notes (no duration): never bent.
- Selftest idea: feed a sine on the guitar channel and set speed 0.94387 for 1 s. Expect the measured
  frequency × 0.9439 and `guitarSlipMs()` ≈ 56 ms. After `resyncGuitar()` it reads 0.

## 7. Address index
- Watcher: ctor `FUN_00262d10`; Poll `FUN_002630b0`; whammy `FUN_00263e60`; hit `FUN_00263728`;
  reset `FUN_002640d8`; miss `FUN_002641e0`; fret release `FUN_00263578` / `FUN_00264018`;
  gem crossing `FUN_00263db0`; sinks `FUN_00264120` (hit), `FUN_002645d0` (0x14), `FUN_002646f0`
  (whammy 0x84), `FUN_002647f0` (0x94).
- BeatMatcher (vtable 0x2fd190): `_v3` whammy getter, `_v7` SetPitchBend (`2^(1/12)` pow), `_v8` reset.
  Ctor `FUN_00258fc8` (+0x1c audio, +0x7c soloer). Built by PlayerMatcher `FUN_00107e90`.
- Audio: `FUN_00257270` (unmute/slipstream); `UnmuteBeatMatchAudio` ctor `FUN_00264880`; `_v1/_v3/_v4/_v5/_v6/_v18/_v26/_v31/_v32`.
- SlipTrack: `FUN_00288738`, `FUN_00288b68` (max_slip), `FUN_00288cb8` (SlipTo 0), `FUN_00288e18`,
  `FUN_00288e70` (stop), `FUN_00288f38` (speed), `FUN_00289038` (offset).
- StreamEE `_v25.._v29`. IOP RPC 0x19b/0x19c/0x19d → `FUN_00003264` / `FUN_0000321c` / `FUN_00003098`.
  Slip offset `FUN_00003384`. Voice start `FUN_00002e34`.
- Controller whammy: `GuitarController_v5` 0x25b820, `JoypadController_v5` 0x25be60 (`min(axis,0)`).
- Soloer: `FUN_0025de68`; mode switch `FUN_0025a350` / `FUN_0025a770`; `auto_solo` `FUN_00109e10`.
