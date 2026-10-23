# Character animation RE: servos, drivers, timing (SLUS_212.24)

Conventions used below. Matrices are row-vector, as in the game: `world = local · parentWorld`, and row *i* of a 3x3 is the image of axis *i*. A transform struct is `xfm = obj+0x10`, with local 3x3 rows at +0x20/+0x30/+0x40, local pos at +0x50, world 3x3 at +0x60..+0x8c, world pos at +0x90, and dirty flag at +0xa0. `S(x) = 0.5 − 0.5·cos(πx)` is the cosine ease that every blend uses. `wrapPi(a)` = `fmod(a+π, 2π)`, then −π if ≥0 else +π. Helpers: `FUN_0024cb70` = sinf (cos x is computed as sin(x+π/2)), `FUN_0027d9b0` = fmodf, `FUN_0027d428` = floorf, `FUN_0027d888` = atan2f(y,x). Vtables are GCC-2 style, so slot *k* sits at vtable+0xc+8k. "(inf)" marks an inference. Everything else was read from the decompile or the disassembly.

---

## 0. Short answer: why the two bugs happen

**(a) Wrong proportions.** These are the arm servos, not the clip application. Our clip application (replace pos/quat/rotz) is the same as the game's. However, about 85% of the upper-arm skin weight sits on `bone_?-upperTwist1/2`, and most of the forearm skin sits on `bone_?-foreTwist1/2`. Measured on metal.rnd: R-upperArm 13.3 vs twist 79.3; L-foreArm 46.9 vs foreTwist 76.3.

- The upper twists are children of the **clavicle**. The fore twists are children of the **upperArm**, sitting at the elbow, as siblings of foreArm. No clip channel drives any of them.
- Without `AnimServoUpperTwist` / `AnimServoForeTwist` (§2), the upper-arm skin stays in the bind pose while the arm moves. The forearm skin also ignores the elbow bend, because `foreTwist1 = Rx(θ/3)·foreArm.R` and that is where the elbow rotation reaches the forearm skin.
- `AnimServoIK` is also missing. Clips leave the hands 6–10 units away from `bone_fret_hand` / `bone_strum_hand`; I measured this with our convention, which I verified (§1.6). With `stretch 1`, the game puts the wrist exactly on the target.
- Minor: drummer clips have `bone_?-finger0.rotx`, a pure **X** rotation that we currently treat as Z.

**(b) Band out of sync.** We use `frame = start + (beat − beatChosen)`. The game instead **phase-locks every clip to the song beat modulo its BeatAlign value, every tick**:

`frame ≡ beat (mod A)`, with `A = play_flags & 0xf` (2 for nearly all clips, 4 for `*_half`).

Clip start frames are fractional (−1.33, −2.04, −3.63…), so our phase is wrong by up to a whole beat. Three more differences:
- Clips change at `.acg` graph transition frames, not at the clip end.
- The band switches mood on the `upcoming_*` events, which fire one beat early, so the blend finishes on the event beat.
- `beat` = MIDI tick/480 from the tempo map, multiplied by the per-character `set_frame_scale` (0.5 / 1 / 2).

Details follow.

---

## 1. AnimServoBone (`bone.servo`) and what `use_delta` means

Addresses:
- Constructor `FUN_001823b8`.
- Message handler `AnimServoBone_v1 @0x182d88`: `connect`, `use_delta`, `set_channels`.
- Setup `AnimServoBone_v6 @0x182b48`.
- Per-frame `AnimServoBone_v4 @0x182730`. `_v3 @0x182688` only polls the drivers; it is the not-visible path.

Fields: +0x10 sample buffer ("AnimBoneData"), +0x54..+0x60 `vector<AnimDriver*>`, +0x64 pelvis, +0x68/+0x6c/+0x70/+0x74 pointers to the buffer slots `bone_facing_delta.rotz` / `bone_facing_delta.pos` / `bone_facing.rotz` / `bone_facing.pos`, +0x78 `use_delta`.

### 1.1 Which channels and bones

- The servo owns **only the channels listed in `(channels …)`**: HERO_CHANNELS + HAND_CHANNELS for guitarists, `BASS_CHANNELS` etc. for the band.
- Setup `FUN_001892c8` resolves each `bone_x.<type>` to `bone_x.mesh`. A missing bone gets a shared dummy transformable.
- The buffer layout is grouped by type, with the type taken from the suffix (`FUN_00188820`): pos (16-byte vec4) | quat (vec4) | **rotx | roty | rotz** (floats). The type is `'x'/'y'/'z' − 0x76`, i.e. 2/3/4.
- The buffer is **initialised once from the bind locals**: pos = local.pos, quat = matrix→quat (`FUN_0024a7d8`), rot* = angle extraction (`FUN_0024a628/648/670`).
- The buffer is **persistent**. It is never reset to bind per frame.
- Bones without a channel are never touched, so they keep whatever local they have (bind).
- `.pos` therefore only reaches the bones listed. For the hero that is `bone_pelvis`, `bone_pos_guitar`, `bone_fret_hand`, `bone_strum_hand`, plus the special `bone_facing*`.
- I checked every `.acp` of metal, metal_drummer, metal_bass and metal_singer: all of their channels are inside the servo lists. Hiphop clips also carry `bone_hair[LR][12].quat`, which belong in HIPHOP_CHANNELS.

### 1.2 Blending into the buffer (per frame, in driver order)

The `bone.servo` drivers are, in connect order: `main.drv` (connected by AnimInstance_v3 `@0x180dc0`, servo priority **−100**), then `left_hand.drv`, then `right_hand.drv` (connected in `FUN_00286060`).

```
for drv in drivers:
    w = drv.Poll(beat, dBeat, dSec)        // AnimDriver_v0 @0x17ff18, see §3.2
    drv.Scale(1 - w, buf)                  // FUN_0017b6b8 on the head clip's channels only;
                                           //   if 1-w == 0: pos := (0,0,0,1), quat := 0, float := 0
    drv.Apply(1.0, buf)                    // buf[c] += weight * sample  (FUN_0017b238)
```

`FUN_0017b238` does the accumulation:
- pos: `+= w·v`.
- quat: `+= ±w·q`, with the sign chosen so that `dot(acc, q) ≥ 0` (hemisphere alignment).
- floats: `+= w·v`.
- Compressed values: quat s16/32767, float s16·(1/1638.4).

Result per channel: `buf = (1−w)·buf_prev + w·Σ(chain weights · samples)`. With w = 1 this is a plain replace, as we do. With w < 1 (the hand drivers, see §2.3) the value mixes with whatever is already in the buffer *this frame* (main.drv's value), not with the bind pose.

### 1.3 Writing the buffer to bones (`FUN_001896f8`)

Quats are normalised first (`FUN_0024a290`). Then:

| channel | bone local written |
|---|---|
| `.pos` | translation := xyz (`FUN_001da730`); the 3x3 is untouched |
| `.quat` | 3x3 := Q(q) (`FUN_0024ae78`, the same convention as in FINDINGS); translation untouched; **no scale** (any bind scale is lost) |
| `.rotx` a | rows `(1,0,0) (0,c,s) (0,−s,c)` |
| `.roty` a | rows `(c,0,−s) (0,1,0) (s,0,c)` |
| `.rotz` a | rows `(c,s,0) (−s,c,0) (0,0,1)`, i.e. +a about local Z: X goes to (cos a, sin a, 0); matches our sign |

Every animated bone has unit scale in the bind pose; only `bone_hair03` has scale, and it has no channel. Scale is therefore not an issue.

### 1.4 `bone_facing` and `use_delta` (root motion; nothing to do with bind or delta poses)

- `use_delta` (+0x78) only selects how the `bone_facing` channels are used. Bone channels are always replaced.
- **Clip side (`FUN_0017bb90`, AnimClipSamples_v1).** Each Apply also computes, from the clip's `bone_facing.pos` / `.rotz` samples at t (now) and tPrev (`frame − dFrame`):
  - `Δθ = wrapPi(θ(t) − θ(tPrev))`, accumulated as `+= w·Δθ` into `bone_facing_delta.rotz`.
  - `Δp = P(t) − P(tPrev)`, rotated by **−θ(t)**: `x' = x·cosθ + y·sinθ`, `y' = −x·sinθ + y·cosθ`. It is accumulated as `+= w·Δp'` into `bone_facing_delta.pos`.
- **Servo side.** The servo zeroes both `facing_delta` slots at the start of every frame.
  - **use_delta = 1** (all guitarists): `M = character root local` (`FUN_0018e238`). If Δθ: `M.R = M.R · Rz(Δθ)` (`FUN_0024af18` rotates each row's xy), then re-orthonormalise rows. Then `M.pos += Δp'·M.R`, written with `FUN_001da6c8`. This is incremental root motion, used for walking and turning. `bone_facing.pos/.rotz` are written to the dummy bone, so they are ignored.
  - **use_delta = 0** (band): if `bone_facing.pos` is a servo channel, `pelvis.R = pelvis.R·Rz(facing.rotz)` (re-orthonormalised), `pelvis.pos.xy` is rotated by facing.rotz, then `pelvis.pos += facing.pos`. The band channel lists contain no facing channels, so this never happens in practice.
  - Idle and play clips have constant `bone_facing.pos`, so Δ = 0. Root motion only matters for the walk, turn and stop clips.

### 1.5 Servo execution order (`FUN_001817c0`, AnimInstance poll; called from Character_v13 `@0x18d780`)

- `add_servo` (`FUN_00181350`) inserts into a vector of `{float priority, servo*}` **before the first entry with a greater priority**. Equal priorities therefore keep their add order.
- The character stores `+0x84 = beat·scale`, `+0x88 = dBeat·scale`, `+0x8c = dSec`, with `scale = +0x98` (`set_frame_scale`).
- If not visible (+0x34 == 0), only slot 3 (poll) runs, on servos with priority < 0.
- Otherwise:
  1. Servos with priority < 0 run slot 4 (pass 0). This is `bone.servo` at −100; Grim's `wings.servo` is −1.
  2. World transforms of the whole hierarchy are updated (`this+0x18` vtable +0x124 slot, then `this` vtable +0x5c).
  3. Servos with priority ≥ 0 run in order with pass 0. Any servo that returns > 0 is re-run with pass 1, 2, …; later servos are re-run after it. IK sets +0x58 = 0, so in practice everything runs exactly once.
- Effective order for a guitarist:
  `bone.servo(−100)` → world update → `right_hand.ik(1)` → `left_hand.ik(1)` → `foreTwist_L, foreTwist_R, upperTwist_L, upperTwist_R (1000)`.
  Grim adds rig 2000 and pcon 4000. The band has only `upperTwist_L/R (1000)` (bandbase.dta).
- Each servo updates the world of the bones it changes via the transformable vtable (+0xb4 table, +0x14 slot), passing the parent's world (`xfm+0x60`). (inf) That call must propagate to children: IK reads the hand's world after it changes the elbow.

### 1.6 Convention check

FK of `metal.rnd` + `guitar_sg.rnd` (guitar bones under the character's pelvis and pos_guitar), with clips applied by the rules above:

| clip | L-hand → fret_hand | R-hand → strum_hand |
|---|---|---|
| metal_stand_medium_01 | 6.2 | 6.2 |
| metal_idle_medium | 6.9 | 10.3 |
| metal_extreme_medium_01 | 10.3 | 6.2 |

With the quaternion transposed the distances are 37–53, so our quat convention is right; IK closes the remaining gap. Main clips contain **no** `bone_fret_hand` / `bone_strum_hand` channels. Those come from the hand clip sets (`charsys/<char>/anims/finger`) and from code (§3.6).

---

## 2. Arm servos

### 2.1 AnimServoForeTwist (`_v6 @0x183cf8` setup, `_v4 @0x183a20` per frame; ctor `FUN_00183930`)

- `(bones foreArm foreTwist1 foreTwist2 hand)` → +0x18, +0x1c, +0x20, +0x24.
- `(offset deg)` → +0x28 = offset·π/180: L = +90°, R = −90°.
- +0x14 = parent(foreTwist1), i.e. upperArm.

```
H   = hand.local.R                          // hand relative to foreArm
q   = arcQuat(H.row0 → +X)                  // FUN_0024b1b0: q=(0, v.z/2w, −v.y/2w, w), w=sqrt(.5+.5v.x)
y'  = H.row1 · Q(q)                         // FUN_0024b238 (swing removed)
θ   = atan2(y'.z, y'.y)                     // twist of the hand about the forearm X axis
θ'  = wrapPi(θ + offset)
foreTwist2.local.R = Rx(θ'/3)               // rows (1,0,0)(0,c,s)(0,−s,c), c=cos(θ'/3), s=sin(θ'/3)
foreTwist1.local.R = Rx(θ'/3) · foreArm.local.R      // row-vector product
foreTwist2.local.pos.x = hand.local.pos.x · 0.5
foreTwist1: update world from upperArm.world
```

Bind check, exact on metal: hand θ = −87.55° (L) and +87.58° (R). After the offset that gives ±2.45°. `Rx(θ'/3)·foreArm` reproduces bind foreTwist1 to 3 decimals, foreTwist2 = Rx(0.82°), and foreTwist2.x = 4.4665 = 8.933/2.

### 2.2 AnimServoUpperTwist (`_v6 @0x187c40`, `_v4 @0x1878a8`)

`(bones upperTwist1 upperTwist2 upperArm)` → +0x18, +0x1c, +0x20; +0x14 = parent(upperArm) = clavicle.

```
U  = upperArm.local.R
θ  = twist of U about X, computed as above (arcQuat(U.row0→X), y' = U.row1·Q, θ = atan2(y'.z, y'.y))
T  = Rx(−θ/2)
upperTwist2.local.R = T · U          // half the twist removed
upperTwist1.local.R = T · upperTwist2.local.R = Rx(−θ)·U   // no twist, swing only
positions untouched (all three share the bind pos 8.95 under the clavicle)
update world of twist1 and twist2 from clavicle.world
```

The bind pose does not match this rule exactly. Metal bind twists are arm 15.1°, t2 9.1°, t1 3.2°, so the bind was authored separately. Use the formula.

### 2.3 AnimServoIK (ctor `FUN_00184020`, setup `_v6 @0x184de8`, per frame `_v4 @0x184198`)

Parameters:
- `frac` (+0x70, default 1)
- `align_quat` (+0x74)
- `stretch` (+0x78)
- `source` (+0x40 = `bone_R-hand`), with +0x44 = parent(source) = foreArm
- `dest` (+0x24 = `bone_strum_hand` / `bone_fret_hand`)
- `(bones "bone_R-foreArm.mesh" 2)` → chain `[foreArm, upperArm, clavicle]`, where `chain[i+1] = parent(chain[i])`

**Setup (first run):**
- Save `source.local.pos` → +0x10.
- `a = |chain0.world.pos − source.world.pos|` (forearm length).
- `b = |chain0.local.pos|` (upper-arm length).
- Store `+0x84 = a²+b²` and `+0x80 = 1/(2ab)`.

**Per frame, pass 0:**
```
if frac == 0: (if stretch and hand.local.pos != saved) restore hand.local.pos = saved; return
if stretch and hand.local.pos != saved: restore it, update hand world
T = lerp(source.world.pos, dest.world.pos, frac)               // +0x60
// elbow (i = 0, chain size >= 3)
c = clamp((|upperArm.world.pos − T|² − (a²+b²)) / (2ab), −0.985, 0.985); s = sqrt(1−c²)
foreArm.local.R = rows (c,−s,0) (s,c,0) (0,0,1)     // = rotz(φ), φ = −acos(c)  (replaces the clip's foreArm.rotz)
update foreArm world (from upperArm.world)
// aim (i = 1): bone = upperArm
v1 = (source.world.pos − bone.world.pos) in bone frame: (v·W.row0, v·W.row1, v·W.row2)
v2 = (T − bone.world.pos) in bone frame
q  = arcQuat(v1 → v2)                // FUN_0024b0b8: xyz = (v1×v2)/(2|v1||v2|w), w = sqrt(.5+.5cos)
upperArm.local.R = Q(q) · upperArm.local.R
update upperArm world (from clavicle.world)
// finish (pass == +0x58 == 0)
P⁻¹ = inverse(foreArm.world)                                   // FUN_0024b748
if align_quat and dest:
    R = dest.world.R; if frac < 1: R = slerp(frac, source.world.R, dest.world.R)  // FUN_0024ab90 (inf: slerp/nlerp)
    hand.local.R = R · P⁻¹.R
if stretch: hand.local.pos = T · P⁻¹        // wrist lands exactly on T
update hand world
```

**Where `frac` comes from** (`FUN_00286f58`, end of GuitarCharacter_v13, every frame):
- `wL = FUN_00180518(main.drv, kGuitarLeftIK 0x400000)`, `wR = …(kGuitarRightIK 0x800000)`.
- `left_hand.ik.frac = wL` and `left_hand.drv.weightScale (+0x18) = wL`; likewise for the right side.
- `FUN_00180518` walks the clip-driver chain from the newest clip:
  `acc += remaining·S(blendᵢ)` if `clipᵢ.flags & F`; then `remaining *= 1 − S(blendᵢ)`.
- So the IK and the hand clips are on exactly as much as the playing main clips carry the IK flags. Idle, stand and extreme clips all carry both flags (e.g. 0x80c10001).

---

## 3. Drivers and timing

### 3.1 Time source

- `GamePanel` builds a song-time struct at `0x344258` (`FUN_00106a70`): `{ms, dMs, tick, dTick, beat = tick/480, dBeat = dTick/480}`. Tick comes from the tempo map at the audio clock (`FUN_0010e1f0`). ms and tick are clamped so they never go backwards.
- `FUN_00281a10` calls `CharMan_v8(beat, dBeat, dSec = dMs/1000)` → every character's poll.
- Each character scales: `beat_c = beat·k`, `dBeat_c = dBeat·k`, where `k` = `set_frame_scale` (+0x98). The events `gtr_/drum_/bass_/keys_/sing_ half_tempo|double_tempo|normal_tempo` set it to 0.5 / 2 / 1. dSec is not scaled.
- **Use MIDI beats (tick/480), not seconds × bpm.**

### 3.2 AnimDriver (`FUN_0017f978` ctor; Poll `_v0 @0x17ff18`; Apply `_v1 @0x180440`)

Fields:
- +0xc AnimSet (`anims`)
- +0x10 `restrict_flags`, OR-ed into every flag-based selection
- +0x14 fade (1)
- +0x18 weight scale (1)
- +0x1c / +0x20 fade timers (1e30 = off)
- +0x24 last beat
- +0x28 `realign`
- +0x2c head of the clip-driver list (newest first, linked via +0x28)
- +0x30 / +0x34 `set_starved` callback

`Poll(beat, dBeat, dSec)`:
- **realign** (drummer and keyboard only, `band_chars.dta`): if `floor(beat) != floor(last)`, let `A = max(clip.play_flags & 0xf)` over the chain. If `(floor(beat) XOR (floor(last)+1)) & (A−1) != 0`, the scaled beat jumped (frame-scale change or seek). Then:
  1. Clear the align bits of all clip drivers.
  2. If `& (A_head−1)` is also set, re-play the head clip with `kAnimNow|kAnimGraphLoop` (0x102).
- If there is no blend in progress and `set_starved` is set, send the starved message.
- **kAnimGraphLoop**: when no blend is in progress (`FUN_0017fc00`: list empty or head has no predecessor) and the head was played with 0x100, it re-plays **the same clip** with `kAnimLongest|kAnimGraphLoop` (0x104). That queues a self-transition taken from the graph (§3.4).
- Return value: `w = weightScale · S(fade) · head.Poll(...)`.

`Apply(w)` → `head.Apply(w·weightScale·S(fade))`.

### 3.3 AnimClipDriver (0x38 bytes; ctor `FUN_0018a038` / `FUN_0018a178`; Poll `_v0 @0x18a4b0`; Apply `_v1 @0x18a870`)

Fields:
- +4 play mode flags
- +8 speed (1)
- +0xc clip play_flags (low nibble = align A, 0x10 = realtime)
- +0x10 delay
- +0x14 frame
- +0x18 dFrame
- +0x1c blend (0..1)
- +0x20 dFrame incl. realign
- +0x24 clip
- +0x28 prev (older) driver
- +0x2c / +0x30 / +0x34 next event frame, event index, `events` array

**Construction by `play` (`FUN_0018a038`, prev = current head):**
- `mode & 7 == kAnimReset(1)`: delete the whole old chain, no transition.
- Transition `tr = FUN_0017f1d0(prev.frame, set, prev.clip, newClip, mode)`:
  - `kAnimFirst(3)`: the first `.acg` edge prev→new (in file order) with `src_frame > prev.frame`.
  - `kAnimLongest(4)`: among edges with `src > prev.frame`, the one minimising `(prev.end − src)² + (dest − new.start)²`.
  - `kAnimNow(2)`, or no edge found: `src = prev.frame`; for mode 4, `src = max(prev.frame, prev.end − new.blend_width/2)`. Then `dest = new.start + ((src mod A) − (new.start mod A))`, +A if negative, with `A = new.play_flags & 0xf` (no adjustment if A = 0).
- If a transition exists: `frame = tr.dest`, `delay = tr.src − prev.frame`, `blend = 0` (1e−10 if delay == 0).
- With no prev (or Reset): `frame = clip.start`, **blend = 1** (instant).
- The direct constructor (`FUN_0018a178`, used by the guitarist's `set_playing`) takes `frame` and `delay` explicitly, with `blend = 0`.

**Poll(beat, dBeat, dSec), returns the chain weight:**
```
wPrev = prev ? prev.Poll(...) : 0
delay -= prev ? prev.dFrameEff(+0x20) : (realtime ? dSec : dBeat)   // waits until prev reaches src
dFrame = dFrameEff = 0
if delay < 0:
    old = frame
    d = (realtime ? dSec·clip.fps : dBeat) · speed;  dFrame = d;  frame += d
    r = (A != 0 && speed == 1 && !(mode & kAnimLoop 0x80)) ? wrapHalf(fmod(beat − frame, A)) : 0   // FUN_0018a9d0
        // wrapHalf: r<0 → r+A; r > A/2 → r−A   (r ∈ (−A/2, A/2])
    frame += r;  dFrameEff = d + r                         // => frame ≡ beat (mod A) every tick
    fire clip `events` whose frame lies in (old, frame]
    if blend < 1: blend = (clip.blend_width == 0) ? 1 : min(1, blend + d / clip.blend_width)
if !clip.loaded: blend = 0
if prev and blend == 1: delete prev chain
first time blend > 0: run the clip's `enter_event`
if mode & kAnimLoop (0x80) and frame > clip.end: frame = clip.start + (frame − clip.end) + realign
return S(blend) + (1 − S(blend))·wPrev
```

- `beat` is the character's scaled beat.
- Realtime clips (play_flags 16: win/lose/intro…) run on `dSec·fps`. Their A is 0.

**Apply(w):** `clip.Apply(w·S(blend), frame, dFrame)` then `prev.Apply(w·(1−S(blend)))`, recursively.

**Clip sample** (`AnimClipSamples_v1 @0x17c0c8`): `t = clamp((frame − start)/(end − start), 0, 1)`, index `t·(n−1)`, linear between the two samples (`FUN_0017ace8`, the interp flag is 1). The "One" set uses sample 0. Both are added with weight w. A clip past its end therefore **holds its last sample**.

**Clip parameters actually used:** `FUN_00189a80` copies `flags`, `play_flags`, `blend_width` from the **set entry** (the `*_anims.dta` `(flags …)(play_flags …)` plus BAND_GRAPH defaults `(blend_width 1)(play_flags kAnimBeatAlign2)`) into the clip. The `.acp` header values match them, e.g. drummer `*_half` is 0x4. Treat the dta as authoritative.

### 3.4 `.acg` graph: now confirmed

Per source clip, a list of `{dest_index, src_frame, dest_frame}` (12 bytes). It is consumed by `FUN_0017f108` (First) and `FUN_0017efe0` (Longest):
- `[1]` is compared with the old clip's current frame and end.
- `[2]` is compared with the new clip's start.

Self-edges exist; e.g. drummer clip 0: `0→0 @11.65 → 1.65`. `src − dest` is always even for align-2 clips, so the beat phase is preserved. The graph is rebuilt from `(graph (transitions…)(criteria…))` only when the stored clip count differs (`FUN_0017f440`).

### 3.5 Choosing a clip by flags

- `{drv play <flags|symbol|clip> <mode>}`: the default mode is 4 if omitted (`FUN_00180a10`).
- Selection (`FUN_0017e9a0` / `FUN_0017e890`): candidates are entries with `(entry.flags & (wanted|restrict|0x80000000)) == that`.
- The pick is **least-recently-used**: the smallest `entry+0x18` timestamp, with ties going to the first in dta order. The timestamp is updated on pick. This is deterministic, not random.
- (inf) `restrict_flags` carries the song's `anim_tempo` (kTempoMedium 1 / Fast 2); FINDINGS already matches tempo.

### 3.6 Who plays what

**Band** (`charsys/gen/theband.dta`, sent to every stage char via `{char_sys stage_chars}`):
- `init`: `kBandIdle` with `kAnimReset|kAnimGraphLoop`. The first clip has no blend, frame = start, then phase-snap.
- **`upcoming_<evt>`** events are sent **480 ticks (one beat) before** the EVENTS-track event (`FUN_002589b8`: `tick + 0x1e0 ≥ evt.tick`; `BeatMatch_v3 @0x10eed8` prefixes `upcoming_` and sets `$upcoming_beat = evtTick/480`). Mapping, each played with **`kAnimNow|kAnimGraphLoop`** on that character only:

  | event | character | category |
  |---|---|---|
  | `upcoming_drum_on` / `drum_normal` | drummer | kBandActive |
  | `upcoming_drum_off` | drummer | kBandIdle |
  | `upcoming_drum_double` | drummer | kBandDouble |
  | `upcoming_drum_half` | drummer | kBandHalf |
  | `upcoming_drum_allbeat` | drummer | kBandAllbeat |
  | `upcoming_bass_on/off` | bass | Active / Idle |
  | `upcoming_sing_on/off` | singer | Active / Idle |
  | `upcoming_keys_on/off` | keyboard | Active / Idle |

  With kAnimNow the blend starts immediately (src = current frame, dest phase-aligned), so with blend_width 1 it completes on the event beat.
- After that, kAnimGraphLoop keeps re-queuing the same clip through its graph self-edge (Longest).
- `downbeat` (each new bar, `BeatMatch_v1 @0x10e458`): for each stage char whose current clip has kBandActive, with probability 0.2 (`random_float < 0.2`), play `kBandActive` with `kAnimFirst|kAnimGraphLoop`. That gives the LRU Active clip at the first graph edge after the current frame.
- `game_won`: `kBandWin kAnimFirst`. `game_lost`: `kBandLose kAnimNow`. Both are realtime clips.
- Singer skipped while `$camera.stare_at_singer`; nobody re-plays after game over.
- `*_half_tempo` / `double` / `normal` → `set_frame_scale` 0.5 / 2 / 1. Drummer and keyboard drivers have `realign 1` to absorb the beat jump.

**Guitarist** (`GuitarCharacter_v1 @0x285978` handlers, `guitarist.dta`). State +0x108: 0 idle, 1 playing, 2 wail, 3 solo, 4 game over.

Mood flags (`FUN_00285440`):
- if the script `is_doing_badly` is true → `kGuitarBad` (0x4000). Single player: `crowd_rating < 0.2`. Multiplayer: behind the other player by more than max_score/15.
- else by state: idle `kGuitarIdle`, playing `kGuitarNormal`, wail `kGuitarExtreme`, solo `kGuitarSolo`.

Handlers:
- `upcoming_gtr_on/off $upcoming_beat` → `set_playing`. `upcoming_wail_*` → `set_wail` (on → state 2; off → back to 1). `upcoming_solo_*` → `set_solo` (on → 3; off → 1 and walk bookkeeping). After each, `FUN_002856a0(upcomingBeat)`:
  - If the current clip is kGuitarSpecial (0x2000000): only re-queue.
  - Otherwise: `clip = select(mood)`; create a ClipDriver with `frame = clip.start` and `delay = upcomingBeat − clip.blend_width − beat` (`FUN_0017fd18`), mode 0. The blend starts `blend_width` beats before the event and is complete **on the event beat**, then phase-snaps.
- **Every frame** (`GuitarCharacter_v13 @0x284390`): if no blend is in progress and the character is not walking or game over, `FUN_002855f8` → `main.drv play <mood flags> kAnimLongest`. The next LRU clip of the mood is queued as soon as the previous one has fully blended in; it takes over at the Longest graph edge near the end, which avoids holding the last frame.
- `play_intro`: `kGuitarIntro kAnimReset`, then `kGuitarIdle kAnimLongest`. `play_idle`: `kGuitarIdle kAnimReset`. Both also start the hand sets with `0x103`.
- `start_starpower`: if no Special clip is playing, `kGuitarSpecial kAnimFirst`.
- `set_game_over`: `kGuitarWinFinal`, or `kGuitarWin` / `kGuitarLose`, or `kGuitarLoseWalk` while walking.
- **Excitement does not choose body clips.** It only drives face poses (`excitement_poses`) and the venue lighting. Bad comes from `is_doing_badly`.
- Hands: `hand_events` in charsys.dta, `(40 60) fret_pos` and the strum/finger notes, are handled in `FUN_002867b8` / `FUN_002868c8`. (inf) `bone_fret` is placed at an eased (`S`) lerp between `spot_neck_fret<N>` world positions, and the hand drivers play finger/strum clips. These are the IK targets (not traced further).

---

## 4. Concrete fixes, in priority order

1. **Phase-lock frames:** each tick, `frame += dBeat_c·speed; frame += wrapHalf(fmod(beat_c − frame, A))`, with `A = play_flags & 0xf` (skip if A = 0, realtime or kAnimLoop). Use `beat_c = tick/480 · frame_scale`.
2. **Transitions:** queue the next clip at an `.acg` edge (Longest: minimise `(end−src)² + (dest−start)²`, `src > cur`). New clip `frame = dest`; it starts when the old frame reaches `src`. Blend `+= d/blend_width` with weight `S(blend)`. The old clip keeps advancing and holds its last sample past end. Fallback: `src = max(cur, end − bw/2)`, `dest` phase-aligned.
3. **Band moods from the `upcoming_*` events** (kAnimNow), plus downbeat changeup and graph self-loop. **Guitarist:** blend start `= upcomingBeat − blend_width`; LRU selection.
4. **Servos after clip application and a world update:** IK R, IK L (frac = IK-flag weight, stretch, align_quat), then foreTwist L/R (offset ±90°), then upperTwist L/R. For the band: upperTwist only.
5. `.rotx` = rows `(1,0,0)(0,c,s)(0,−s,c)`; `.roty` = `(c,0,−s)(0,1,0)(s,0,c)`.
6. Root motion (`use_delta`) only for walk clips; can wait.
