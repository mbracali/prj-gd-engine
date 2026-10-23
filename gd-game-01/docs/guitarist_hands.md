# Guitarist hand system RE (SLUS_212.24)

Marks: **[C]** read directly from decomp/disassembly/data. **[I]** inferred.
Addresses are EE virtual addresses. "beat" means MIDI beat = tick/480.

This report extends `docs/character_animation.md` §2.3 and §3.6. That doc says in §3.6 that `play_intro`/`play_idle` "also start the hand sets with 0x103". That is wrong. The 0x103 play in `FUN_00285190`/`FUN_00285310` goes to `wings.drv` (string `0x341628` = "wings.drv"), so it only affects Grim's wings [C].

---

## 0. The pieces

| Piece | Where | What it does |
|---|---|---|
| `GuitarAnimInfo` (MidiReceiver, 0x74 bytes) | ctor `FUN_00114658`, owned by `SongDB` at +0x44 (`FUN_0010b7b8`) | Parses the song MIDI once and builds 7 timed event lists from the `ANIM` track and the gem chart |
| Hand object (0xa8 bytes) | ctor `FUN_00286060`, at `GuitarCharacter+0x160` | Per frame: places `bone_fret`, schedules clips on `left_hand.drv` / `right_hand.drv` |
| `left_hand.drv`, `right_hand.drv` | `charbase.dta` preload (`new AnimDriver`) | Connected to `bone.servo` after `main.drv` [C `FUN_00286060` → `FUN_00182510(char+0x90, drv)`] |
| `right_hand.ik`, `left_hand.ik` | `charbase.dta` (AnimServoIK, priority 1) | Pull the wrists onto `bone_strum_hand` / `bone_fret_hand` |
| `FUN_00286f58` | end of `GuitarCharacter_v13` | Sets IK `frac` and the hand driver `weightScale` from the main clip's IK flags |

Event record (`FUN_00287028` push, 16 bytes): `{f32 start_beat; f32 end_beat; DataNode value (int or symbol)}`. A cursor (`+0x10` index, `+0x14` last query time) is kept per list [C].

Query helpers [C]:
- `FUN_00287258(t, list)` ("next"): if `t <= lastT`, reset the cursor to 0; store `lastT = t`. Advance the cursor while `ev.start < t`. Return the first event with **`start >= t`**, or null.
- `FUN_002872d8(t, list)` ("current"): run `next(t)`. If that event's `start == t`, return it. Otherwise return the event before the cursor (the latest one with `start < t`), or null.

### GuitarAnimInfo fields [C]

| Offset | Content |
|---|---|
| +0x0c | `fret` list: authored `fret_pos` from ANIM, or generated |
| +0x10 | `finger` list (left hand) |
| +0x14 | `strum` list (right hand) |
| +0x18 | singer events |
| +0x1c | face events |
| +0x20 | `HandMap_*` change events |
| +0x24 | `StrumMap_*` change events |
| +0x28 | song data |
| +0x2c / +0x30 / +0x34 / +0x38 / +0x3c | DataArrays `charsys:hand_finger_mapping` / `hand_strum_mapping` / `hand_events` / `face_events` / `singer_events` |
| +0x40 | current HandMap (init `HandMap_Default`) |
| +0x44 | current StrumMap (init `StrumMap_Default`) |
| +0x48 | track mode |
| +0x64 | int[128] note-on ticks (−1 = off; reset per track by `GuitarAnimInfo_v1`) |

---

## 1. `bone_fret` / `bone_fret_hand`: placement on the neck

### 1.1 Scene hierarchy (guitar `.rnd_ps2`, e.g. `guitar_sg`) [C from file]

```
bone_pelvis
└─ bone_pos_guitar                     (animated by main.drv: bone_pos_guitar.pos/.quat)
   ├─ guitar.mesh ─ guitar_strings.mesh ─ guitar_fire.mesh
   ├─ spot_neck_fret01 … spot_neck_fret20   (static locals; z goes 29.69 (01, nut end) → 11.42 (20, body end))
   ├─ bone_fret      ─ bone_fret_hand   (bone_fret_hand: channels .pos/.quat from left_hand.drv)
   └─ bone_strum     ─ bone_strum_hand  (bone_strum_hand: channels .pos/.quat from right_hand.drv)
```

- All 20 spots share the same world X and Y rows. Their Z row is **scaled** (length 0.165–0.362), so it must be renormalised [C].
- At runtime **`bone_fret` is detached from its parent** [C]. After `GuitarCharacter_v12` → `FUN_002862c8` finds `bone_fret.mesh`, it calls `FUN_001d9f80(bone_fret)`. That function walks the objects referencing `bone_fret` and, for each Transformable whose child list contains it, unlinks it.
- `bone_fret` therefore has no parent. When it is updated with `RndTransformable_v1(this, parentWorld=NULL, …)` (`@0x1da570`), it does **world := local**. Its child `bone_fret_hand` gets `world = local · bone_fret.world` (row vectors) from the same propagation [C].
- `bone_strum` and `bone_strum_hand` stay in the `bone_pos_guitar` hierarchy. No code touches `bone_strum`; there is no `bone_strum` string anywhere in the ELF [C]. `bone_strum` is therefore static relative to the guitar.
- `FUN_002862c8` loads `spot_neck_fret%02d.mesh` for i = 1..20 into `hand[0..19]` and `bone_fret.mesh` into `hand+0xa4` [C]. The printf argument is elided in the decomp; 1-based numbering is inferred from the file names [I].

Transformable xfm layout (`*(obj+0x10)`) [C]:

| Offset | Content |
|---|---|
| +0x20 / +0x30 / +0x40 | local rows X, Y, Z |
| +0x50 | local pos |
| +0x60..+0x80 | world rows |
| +0x90 | world pos |
| +0xa0 | dirty flag |

### 1.2 Event sources

**A. Authored: the `ANIM` MIDI track** [C]
- `GuitarAnimInfo_v5` sets the track mode from the track-name meta event (type 3): `"ANIM"` → mode 1, `"T1 GEMS"` → mode 0 (ignored here), anything else → mode 2 (face/singer notes > 107).
- In mode 1, `GuitarAnimInfo_v4` → `FUN_00115b68`. The note-on tick is stored at `+0x64[note]`; on **note-off** (status 0x80) the event is emitted.
- The note is looked up in `hand_events` (`FUN_00115710`). A matching entry is either an int equal to the note, or `(lo hi)` with `lo ≤ note < hi`.
- Only entries whose symbol is `fret_pos` are used. That is `((40 60) fret_pos 40)`, giving the event `{noteOnTick/480, noteOffTick/480, int(note − 40)}`. Every other ANIM note (24–26, 60–83) is looked up and silently ignored.
- So **note 40..59 → spot index 0..19 → `spot_neck_fret01..20`**.
- The game only emits on status 0x80. Treating `0x90` with velocity 0 as note-off is my inference [I]. With the game's code a 0x90 v0 would raise "Double note-on".
- The shipped charts do contain these notes. smokeonthewater ANIM has notes 40–58; evenrats has 40–57.

**B. Generated from gems: only if the ANIM track produced no fret events** [C] (`FUN_00114dd8`, flag `uStack_e8 = fret list empty`):
- Only **chords** (popcount(mask) > 1) emit `{gemStart, gemEnd, int(lowestFretIndex·3)}`.
- So green-based chord → value 0 (fret01), red → 3 (fret04), yellow → 6 (fret07), blue → 9 (fret10). Single notes emit no fret event.

**Post-processing** (`GuitarAnimInfo_v3` → `FUN_00116018(0.22, fretList)`) [C]:
```
for i in 1..n-1:  prev = L[i-1], next = L[i]
   if next.start - 0.22 <= prev.end and (prev.end - prev.start) > 0.22:
       prev.end = next.start - 0.22
```

### 1.3 Per-frame placement (`FUN_002868c8(beat, hand)`, from `FUN_002867b8`, only if the fret list is non-empty) [C]

The beat is the global unscaled song beat `DAT_00344268` (song-time struct +0x10), **not** the character's frame-scaled beat [C].

Hand-object fields used:

| Offset | Field | Initial value |
|---|---|---|
| +0x64 | cur spot | 3 (ctor) |
| +0x68 | prev spot | 3 |
| +0x6c | hold end | −1e30 (reset `FUN_00286420`) |
| +0x70 | move start | — |
| +0x74 | move target time | — |
| +0x8c | "hitting" | 0 at reset; 1 on `hit_gem` (PlayerMatcher_v3); 0 on `miss` (v4) / `pass` (v5); via `FUN_00109dd0` → `FUN_00284ea8` |
| +0x90 | "player active" | 1; set from BeatMatch `active_players` mask, `FUN_00286298`: `mask == 0 \|\| mask & (1 << char.player)` |

`active = (+0x8c && +0x90)` (`FUN_00286fd8`).

```
if autoSolo(+0x60) == 0 (normal play):
   if active and beat >= holdEnd:
       ev = next(beat, fretList)                 // first event with start >= beat
       if ev: holdEnd = ev.end; prev = cur; cur = ev.value; moveStart = beat; moveTarget = ev.start
t = clamp((beat - moveStart) / (moveTarget - moveStart), 0, 1)   // PS2 x/0 → ±MAX → clamps to 1 [I]
s = 0.5 - 0.5*sin(π t + π/2)          // FUN_0024cb70 is sin [C] → s = 0.5 - 0.5 cos(π t)
P = (s==0) ? spotW[prev].pos : (s==1) ? spotW[cur].pos : (1-s)·spotW[prev].pos + s·spotW[cur].pos   // world +0x90
bone_fret.local.pos = P                                    // FUN_001da730
Y = normalize(spotW[prev].row1)                            // rotation from the PREV spot only
X = normalize(Y × spotW[prev].row2)
Z = X × Y
bone_fret.local.rows = (X, Y, Z); dirty = 1
RndTransformable::UpdateWorld(bone_fret, parent=NULL)      // world := local; propagates to bone_fret_hand
```

What this means:
- **No fixed lead time.** The hand holds the spot during `[ev.start, ev.end]`. As soon as `beat ≥ ev.end`, it starts an S-curve move from wherever it is to the next event's spot, arriving exactly at `next.start`.
- Because of the 0.22 trim, back-to-back positions move over 0.22 beats. Gaps longer than that produce a slower glide over the whole gap.
- If the player is not "hitting" (has not hit yet, or just missed/passed a gem), no new target is taken. The current move finishes and the hand holds. Events whose start has passed are skipped when hitting resumes.
- The spot worlds read here are the **previous frame's**, because this runs in Character slot 10 before this frame's servos and world update [I effect, C order]. Rotation from `prev` instead of an interpolation does not matter visually: all spots share X/Y.
- Until the first event, prev = cur = 3, so the hand rests at **`spot_neck_fret04`** [C].

**Debug "auto_solo" variant** (`+0x60`, set by `player_matcher set_auto_solo`, cheat only) [C]:
- When `beat ≥ holdEnd`: `prev = cur`; `cur = (+0x5c ? 10 : 6)`; `moveStart = beat`; `moveTarget = beat + 0.15`; `holdEnd = beat + 0.4`.
- The left hand plays `finger_vibrato_{index, pinky, index_hi, ring_hi, middle_hi}[highest held fret]` (table `0x2f5850`) with mode 0x80 and delay 0, or `finger_open` (mode 0x82) when no fret is held (`FUN_002865e0`).
- Ignore this for normal play.

---

## 2. `bone_strum_hand`: animated, guitar-relative

- `bone_strum` (static local `(3.364, 0.388, 3.09)` under `bone_pos_guitar`) moves only with the guitar [C].
- `bone_strum_hand.pos/.quat` are `bone.servo` channels (`HAND_CHANNELS`). They are written **only by `right_hand.drv` clips** [C]: main sets list `(channels HERO_CHANNELS)` without hand channels, and no main `.acp` contains them.
  - `strum_open`: constant pose.
  - `strum_pluck_short`: animates `.quat`.
  - `strum_pluck_down` / `strum_down_long`: animate `.pos` and `.quat`, plus `R-upperArm.quat` and `R-foreArm.rotz`.
- `bone_strum_hand` is therefore a clip-animated local under a static, guitar-relative parent, and its world follows `bone_pos_guitar`.
- Likewise `bone_fret_hand.local` comes only from `left_hand.drv` clips. The `*_hi` and vibrato clips move `bone_fret_hand.pos`/`.quat`.
- The buffer persists (doc §1.1), so with zero driver weight both keep their last value (initially bind).

---

## 3. `left_hand.drv` / `right_hand.drv`

### 3.1 Clip sets [C]

Each archetype's `my_preload` (`charsys.dta`):
```
"left_hand.drv"  set_anims {$this find_or_load <X>_HAND_ANIMS}
"right_hand.drv" set_anims {$this find_or_load <X>_HAND_ANIMS}
```
The same set goes to both drivers. X is one of METAL, NU_METAL, PUNK, CLASSIC, ALTERNA, HAIR, HIPHOP, GRIM. The UI (`has meta`) path loads none.

`<X>_HAND_ANIMS = ((main::<x>_hand.cset (directory "charsys/<dir>/anims/finger") HERO_HAND_ANIMATIONS <X>_DEPS))`. The directories are metal, nu_metal, punk, classic, alterna, hair_metal, hiphop, grim; each has 29 `.acp` files.

`HERO_HAND_ANIMATIONS` (`hero_graphs.dta`) has `(blend_width 0.24)`, no play_flags (A = 0, so no beat-phase snap; not realtime), and no graph. Clips:
- **Left hand, channels `HERO_LEFTHAND_CHANNELS`** (17: `bone_fret_hand.pos/.quat`, `L-{index,middle,ring,pinky,thumb}01.quat`, and the ten `02/03.rotz`): `finger_powerchord_1/2`, `finger_chord_{bar,cmajor,dmajor,openmajor,openminor,seventh}`, `finger_hold_{index,middle,ring,pinky}[_hi]`, `finger_vibrato_{index,middle,ring,pinky}[_hi]`, `finger_open`.
- **Right hand, channels `HERO_STRUM_CHANNELS`** (20: `bone_strum_hand.pos/.quat`, `R-clavicle.quat`, `R-upperArm.quat`, `R-foreArm.rotz`, R finger channels): `strum_open`, `strum_down_long`, `strum_pluck_down`, `strum_pluck_short`.

Metal `.acp` headers (other characters are similar) [C]:

| Clip(s) | Frames | Notes |
|---|---|---|
| hold, chord, open | 0..0.556 | Constant pose (all channels in the "one" set) |
| `finger_vibrato_*` | 1.0..1.5 (`*_pinky_hi`, `*_ring_hi`: 0..0.5) | 9–13 animated samples |
| `strum_pluck_short` / `strum_pluck_down` | 0..0.5 | 7 samples |
| `strum_down_long` | 0..3 | 31 samples |

All have header flags 0x80000000 and blend 0.24. Frames advance in (character-scaled) beats.

### 3.2 Note → clip tables (charsys.dta, `GuitarAnimInfo`) [C]

`hand_events`: 24 `strum_down_long`, 25 `strum_pluck_down`, 26 `strum_pluck_short`, 60 `finger_powerchord_1`, 61 `_2`, 62 `chord_openmajor`, 63 `openminor`, 64 `dmajor`, 65 `seventh`, 66 `cmajor`, 67–70 `hold_{index,middle,ring,pinky}`, 71–74 `hold_*_hi`, 75–78 `vibrato_*`, 79–82 `vibrato_*_hi`, 83 `finger_open`. `finger_chord_bar` is never referenced.

`hand_strum_mapping` (`FUN_00115608`, **positional**: `[single|chord]`, then element 1 = short or element 2 = long, then the value):

| map | single short | single long | chord short | chord long |
|---|---|---|---|---|
| StrumMap_Default | 26 pluck_short | 26 | 25 pluck_down | 24 down_long |
| StrumMap_punk | 25 | 26 | 25 | 24 |
| StrumMap_softpick | 26 | 26 | 26 | 26 |

Abbreviations in the finger table below: hI/hM/hR/hP = finger_hold_index/middle/ring/pinky, `_hi` = high variant, vI…vP = finger_vibrato_*, pc1/pc2 = finger_powerchord_1/2.

`hand_finger_mapping` (looked up by key `short`/`long`, then `FUN_001154c0` scans entries in order):
- An int entry `gems g` matches only if `mask == 1<<(g−1)`.
- A pair entry `gems (a b)` matches if both bits are set, other bits allowed. So 3+ note chords take the first pair in list order.

| HandMap | short singles G R Y B O | short pairs | long singles | long pairs |
|---|---|---|---|---|
| Default | hI, hP, hI_hi, hR_hi, hM_hi | pairs with G or R → pc1; 3+4, 3+5, 4+5 → pc2 | vI, vM, vR, vP, (75 77)* | as short |
| Linear | hI, hM, hR, hP, (67 68 69)* | as Default | vI, vM, vR, vP, (75 77)* | as Default |
| NoChords | hI, hM, hR, hP, (67 68 69)* | 1+x → hI, 2+x → hM, 3+4/3+5 → hR, 4+5 → hP | vI, vM, vR, vP, (75 77)* | as short pairs |
| AllChords | pc1, pc1, pc1, pc2, pc2 | as Default | as short | as Default |
| DropD | open(83), pc1, pc1, pc2, pc2 | as Default | as short | as Default |
| DropD2 | open, hI_hi, hM_hi, hR_hi, hP | as Default | open, vM, vR, vP, (75 77)* | as Default |
| Solo | as Default | all pairs → dmajor(64) | as Default | 1+2 vI, 1+3 vM, 1+4 vR, 1+5 vR, 2+3 vP, 2+4 vI_hi, 2+5 vM_hi, 3+4 vR_hi, 3+5 vP_hi, 4+5 vR_hi |

\* **Random pick quirk** [C, disassembly `0x115118`: `lh a1,8(v0); jal 0x24a1d0; addiu a0,1`]. The index is `RandomInt(1, size)` in `[1, size)` (`FUN_0024a080`), so element 0 is never chosen:
- `(75 77)` → always **77** (vibrato_ring).
- `(67 68 69)` → 68 or 69 (hold_middle or hold_ring, 50/50).
- The same `RandomInt(1, size)` applies in the strum map, but those arrays have size 2, so they are deterministic.

The current map is switched by MIDI **text events** (meta type 1) on any track [C, `GuitarAnimInfo_v5`]:
- Text starting with `HandMap_` (8 chars) adds `{tick/480, +0.25, symbol}` to the +0x20 list.
- Text starting with `StrumMap_` (9 chars) adds the same to the +0x24 list.
- Charts use, for example, `HandMap_NoChords` and `HandMap_Default` (smokeonthewater), and `HandMap_Solo` and `HandMap_Default` (spanishcastlemagic).

### 3.3 Event generation from gems (`FUN_00114dd8`, run once from `GuitarAnimInfo_v3` after the MIDI parse, only if the finger list is empty, which is always) [C]

Gem source: `*(songData+0x4c)[0]`, 20-byte records. +0x08 = start tick, +0x0c = duration ticks, +0x10 = fret mask (bit 0 = green) [C offsets / I names]. This is presumably the guitar (T1) gems at the loaded difficulty [I].

```
for i in 0 .. nGems-2:                       // the LAST gem is skipped [C, off-by-one]
   g = gems[i]; start = g.tick/480; end = start + g.durTicks/480
   chord = popcount(g.mask) > 1;  low = lowest set bit index
   e = current(start, handMapList);  if e: handMap = e.symbol          // persists between gems
   e = current(start, strumMapList); if e: strumMap = e.symbol
   long = (end - start) > 0.25               // > 120 ticks
   strumNote  = strum_mapping[strumMap][chord ? "chord" : "single"][long ? 2 : 1][1]
   fingerNote = pick(first matching entry of finger_mapping[handMap][long ? "long" : "short"], g.mask)
   finger list += {start,          end,            hand_events[fingerNote].symbol}
   strum  list += {start - 0.0625, start + 0.1875, hand_events[strumNote].symbol}
   if fretListWasEmpty and chord: fret list += {start, end, int(low*3)}
```
Then `FUN_00116018(0.22, fret)` and `FUN_00116018(0.12, finger)` trim the lists (§1.2 rule). The strum list is **not** trimmed [C].

### 3.4 Per-frame scheduling (`FUN_002867b8`, GuitarCharacter_v10, normal mode) [C]

Fields: `nextL` (+0x78), `nextR` (+0x7c), both −1e30 at reset. `openL` (+0x80) = `finger_open` clip and `openR` (+0x84) = `strum_open` clip, resolved in `FUN_00286420`. `bw` = clip blend_width (`clip+0x20`) = 0.24.

```
beat = global song beat
if fretList nonempty: FUN_002868c8(beat)                       // §1.3
if fingerList nonempty and beat >= nextL:
   ev = next(beat, fingerList)
   if ev: LEFT(ev)                                             // FUN_00286d88
if strumList nonempty and beat >= nextR:
   ev = next(beat, strumList)
   if ev: RIGHT(ev)                                            // FUN_00286bc8

LEFT(ev):
   if !active or beat <= ev.start - 1.0:
       if left.headClip != finger_open: left.play(finger_open, kAnimNow|kAnimLoop = 0x82)   // FUN_0017fc28
       nextL = ev.start - 0.25
   else:
       nextL = ev.end
       clip = left.set[ev.symbol]                               // FUN_0017e938
       if left.headClip != clip:
           delay = max(0, ev.start - clip.bw - beat)
           left.push(ClipDriver(frame = clip.start, delay, clip, mode = kAnimLoop 0x80))   // FUN_0017fd18 → FUN_0018a178

RIGHT(ev):
   if !active or beat <= ev.start - 4.0:
       if right.headClip != strum_open: right.play(strum_open, kAnimNow 0x2)
       nextR = ev.start - 0.25
   else:
       nextR = ev.end
       clip = right.set[ev.symbol]
       delay = max(0, ev.start - clip.bw - beat)                // NO same-clip check: every strum re-triggers
       right.push(ClipDriver(frame = clip.start, delay, clip, mode 0))
```

How this plays out:
- **Strums.** The strum clip starts at `gem − 0.0625 − 0.24 = gem − 0.3025` beats and is fully blended in at `gem − 0.0625`.
  - The clip plays from its frame 0 in beats: 0.5 beats for pluck, 3 for down_long. Mode 0 means it holds its last sample after the end.
  - The next strum is only looked up at `beat ≥ gem + 0.1875`. Strum events whose start (`gem2 − 0.0625`) is before that are skipped, so gems closer than 0.25 beats get no separate strum [C].
  - If the next strum is more than 4 beats away, `strum_open` blends in right away (kAnimNow, 0.24 beats).
- **Fingers.** The finger clip blends in over the 0.24 beats before the gem, loops (mode 0x80) and holds until `ev.end` (trimmed to `next.start − 0.12` when notes touch).
  - The same clip is never re-pushed.
  - If the next note is more than 1 beat away, `finger_open` blends in (kAnimNow|Loop).
- **Inactive player** (not hitting, or not in the active-player mask): both hands go to their open clips and re-check 0.25 beats before each event.
- Delays are computed in song beats but consumed by the ClipDriver in the character's frame-scaled beat [C], which matters under `gtr_half/double_tempo`. A pushed clip's delay only counts down while its predecessor is advancing (`delay -= prev.dFrameEff`, doc §3.3).

**Player strum (inferred).** `PlayerMatcher_v2` (sink "swing" callback) → `FUN_00284250` → `FUN_00286530` pushes `strum_pluck_down` on `right_hand.drv` with frame = start, delay 0, mode 0. This happens only when `matcher.+0x18(Player).+0x28 != 0`. That flag is set by `PlayerMatcher_v21` (BeatMatchSink slot 21) when its argument == 1. Fret down/up (`PlayerMatcher_v11/_v12` → `FUN_002864e8/_500`) set and clear bits in `hand+0x54`; those bits are only read in auto_solo mode. I could not pin down when the Player+0x28 flag is set. My guess is "this player's part is live" [I]. If it is set in normal play, every player strum also triggers `strum_pluck_down` immediately.

### 3.5 Weights and combination with `main.drv` [C]

`FUN_00286f58` runs at the end of `GuitarCharacter_v13`, after the servos have run, so the value applies **next frame**:
```
wL = FUN_00180518(main.drv, kGuitarLeftIK  = 0x400000)
wR = FUN_00180518(main.drv, kGuitarRightIK = 0x800000)
left_hand.ik.frac  = wL ; left_hand.drv.weightScale(+0x18)  = wL
right_hand.ik.frac = wR ; right_hand.drv.weightScale(+0x18) = wR
```
- `FUN_00180518`: `acc = 0; rem = 1`; for each clip driver from the newest: `if clip.flags & F: acc += rem·S(blend)`; then `rem *= 1 − S(blend)`. `S(x) = 0.5 − 0.5cos(πx)`.
- From the `*_anims.dta` flags: Idle, Normal, Bad, Extreme and walk clips normally carry both IK flags. Some extremes carry only `kGuitarLeftIK` (classic/grim `*_extreme_fast_04`, classic `*_extreme_medium_04`, punk `extreme_medium_02`, hair `extreme_fast_03` has none). Intro clips carry only Left. Special, Win, WinFinal and Lose carry none.

`bone.servo` buffer per frame (doc §1.2), drivers in connect order main → left_hand → right_hand:
```
for drv in (main, left, right):
    w = drv.weightScale · S(fade) · drv.head.Poll(beat_c, dBeat_c, dSec)
    scale the head clip's channels in buf by (1 - w); add w · Σ(chain weights · samples)
```
- The hand clips' channels (the whole left-fingers + `bone_fret_hand` set; the R arm + fingers + `bone_strum_hand` set) are mixed `(1−w)·main + w·hand`.
- The `bone_*_hand` channels are not in main, so for those `(1−w)` multiplies the persistent previous value.

---

## 4. IK servos and the other per-frame hand/IK logic

Exact definitions (`charbase.dta`, `preload`, non-meta branch, in this order) [C]:
```
{new AnimDriver "left_hand.drv"} {new AnimDriver "right_hand.drv"}
{$this add_servo {new AnimServoIK "right_hand.ik" (source "bone_R-hand.mesh") (dest "bone_strum_hand.mesh")
                   (bones "bone_R-foreArm.mesh" 2) (align_quat 1) (stretch 1)} 1}
{$this add_servo {new AnimServoIK "left_hand.ik"  (source "bone_L-hand.mesh") (dest "bone_fret_hand.mesh")
                   (bones "bone_L-foreArm.mesh" 2) (align_quat 1) (stretch 1)} 1}
```
- No `frac` is given in the dta. The default is 1 (`AnimServoIK_v5` resets +0x70 = 1). Every frame `FUN_00286f58` overwrites it with wL/wR (`FUN_00185268`).
- The parameters are identical for both sides: align_quat 1, stretch 1, chain foreArm → upperArm (2 bones + clavicle as the parent).
- The algorithm is in doc §2.3: elbow by the law of cosines clamped to ±0.985; upperArm aim by arcQuat; hand world rot := dest world rot (slerp by frac when frac < 1); wrist pos := T exactly (stretch).
- Setup reads `a = |foreArm.world − hand.world|`, `b = |foreArm.local.pos|` at first run (`AnimServoIK_v6`).
- Other servos from the same file: foreTwist L/R (offset ±90) and upperTwist L/R, all at priority 1000.

Frame order for a guitarist [C]:
1. `GuitarCharacter_v13`: mood clip queueing on `main.drv`.
2. `GameCharacter_v13` → `Character_v13 @0x18d780`.
3. **slot 10 = `GuitarCharacter_v10` → `FUN_002867b8`**: `bone_fret` placement and hand clip scheduling, using the *previous* frame's spot worlds.
4. `FUN_001817c0`:
   1. `bone.servo` (−100): main, left and right drivers blended and written.
   2. World update of the character hierarchy. `bone_fret` is detached, so it is not in it [I].
   3. `right_hand.ik` (1), then `left_hand.ik` (1).
   4. foreTwist L, foreTwist R, upperTwist L, upperTwist R (1000).
5. `FUN_00286f58`: IK frac and hand weights for the next frame.

Side effect [I]: `bone_fret_hand.world` is refreshed only when `bone_fret` is updated in step 3. The left IK therefore sees the previous frame's `bone_fret_hand.local` (a 1-frame lag). A reimplementation should simply recompute it.

No other code touches the hands. There is no string for `bone_strum`/`bone_strum_hand`, and `bone_fret.mesh` is only used in `FUN_002862c8`. Walking and solo walk only affect main-clip selection, and so the IK flags.

---

## 5. Implementation algorithm (per frame, in order)

**Load time (once per song)**
1. Parse the song MIDI.
   - `ANIM` track notes 40..59 → `fretEv {on/480, off/480, note−40}`, in note-off order.
   - Text events `HandMap_*` / `StrumMap_*` (any track) → map-change lists `{t/480}`.
2. If `fretEv` is empty, mark `genFret = true`.
3. For gems 0..n−2 of the guitarist's chart, run §3.3: build `fingerEv`, `strumEv`, and chord `fretEv` when `genFret`.
   - Keep the map state across gems; the defaults are `HandMap_Default` / `StrumMap_Default`.
   - Use the game's RNG semantics for the arrays: index in `[1, size)`.
4. Trim `fretEv` with 0.22 and `fingerEv` with 0.12 (rule in §1.2). Do not trim `strumEv`.
5. Resolve clips from `charsys/<dir>/anims/finger/gen/*.acp` (blend_width 0.24, A = 0, not realtime) for both drivers. Look up `finger_open` and `strum_open`.
6. Skeleton setup:
   - Attach the guitar so that `bone_pos_guitar`'s children (spots, `bone_strum`→`bone_strum_hand`, guitar meshes) hang off the character's `bone_pos_guitar`.
   - **Detach `bone_fret`**: it becomes a root whose world = local. Keep `bone_fret_hand` as its child.
7. State:
   - `cur = prev = 3`, `holdEnd = nextL = nextR = −1e30`, `moveStart = moveTarget = 0`.
   - `active = false` until the player's first hit. Set it true on hit and false on miss/pass. In multiplayer it also requires the player to be in the active mask. For an attract/autoplay character you may force it true.
   - IK frac and hand weights = 0 (or the wL/wR of the first main clip).

**Every frame (inputs: song beat `b = tick/480`, character beat `b_c = b·frameScale`, dBeat_c, dSec)**
1. Main mood logic (existing).
2. **Fret placement** (if `fretEv` is non-empty), §1.3.
   - If `active && b ≥ holdEnd`: `ev = first fretEv with start ≥ b`. If found: `holdEnd = ev.end; prev = cur; cur = ev.value; moveStart = b; moveTarget = ev.start`.
   - `t = (moveTarget > moveStart) ? clamp((b − moveStart)/(moveTarget − moveStart), 0, 1) : 1`; `s = 0.5 − 0.5cos(πt)`.
   - `bone_fret.world.pos = lerp(spot[prev].world.pos, spot[cur].world.pos, s)`, using the spot worlds from the current guitar pose (or last frame's, to be exact).
   - `Y = norm(spot[prev].world.row1)`, `X = norm(Y × spot[prev].world.row2)`, `Z = X × Y`.
3. **Hand clip scheduling**, §3.4 LEFT/RIGHT, with `b` and the thresholds 1.0 / 4.0 / 0.25, `bw` = 0.24, and modes:
   - left finger clip: Loop, pushed with a delay
   - left `finger_open`: Now|Loop
   - right strum clip: mode 0, pushed with a delay, every event
   - right `strum_open`: Now
   - (optional) a player strum pushes `strum_pluck_down` with delay 0.
4. **Bone servo**:
   - Poll and blend `main.drv` (weight 1).
   - Then `left_hand.drv` with `weightScale = wL_prev`, then `right_hand.drv` with `weightScale = wR_prev` (doc §1.2: scale the head clip's channels by 1−w, add w·samples).
   - Write the locals, including `bone_fret_hand` and `bone_strum_hand`.
5. **World update**: the character hierarchy, then `bone_fret_hand.world = bone_fret_hand.local · bone_fret.world` (the detached branch).
6. **IK**: right (`source R-hand`, `dest bone_strum_hand`, frac = wR_prev), then left (`L-hand` → `bone_fret_hand`, frac = wL_prev). Both use align_quat and stretch (doc §2.3).
7. foreTwist L/R (±90), then upperTwist L/R.
8. Compute `wL = flagWeight(main, 0x400000)` and `wR = flagWeight(main, 0x800000)` for the next frame. Using them in the same frame is a harmless deviation.

**Function index**:

| Address | Role |
|---|---|
| `FUN_00114658` | GuitarAnimInfo ctor |
| `GuitarAnimInfo_v1` | track reset |
| `GuitarAnimInfo_v3` | end of parse: generate + trim |
| `GuitarAnimInfo_v4` | note routing |
| `GuitarAnimInfo_v5` | track names and text events |
| `FUN_00114dd8` | gem → finger/strum/fret events |
| `FUN_001154c0` | finger mapping match |
| `FUN_00115608` | strum mapping |
| `FUN_00115710` | hand_events lookup |
| `FUN_00115b68` | ANIM `fret_pos` |
| `FUN_00115d68` | face/singer |
| `FUN_00115f38` | note on/off pairing |
| `FUN_00116018` | trim |
| `FUN_00286060` | hand ctor (drivers, IK servos) |
| `FUN_002862c8` | spots, `bone_fret`, detach |
| `FUN_00286420` | per-song reset |
| `FUN_00286298` | active-player mask |
| `FUN_00284ea8` | hitting flag (hit/miss/pass) |
| `FUN_002864e8` / `FUN_00286500` | fret button bits |
| `FUN_00286520` / `FUN_00286528` | +0x5c / auto_solo |
| `FUN_00286530` | player strum pluck |
| `FUN_002865e0` | auto_solo hands |
| `FUN_002867b8` | per-frame entry |
| `FUN_002868c8` | fret placement |
| `FUN_00286d88` | left scheduling |
| `FUN_00286bc8` | right scheduling |
| `FUN_00286f58` | IK and weights |
| `FUN_00286fd8` | active test |
| `FUN_00287028` | push event |
| `FUN_00287258` / `FUN_002872d8` | event queries |
| `FUN_001d9f80` | detach from parents |
| `RndTransformable_v1 @0x1da570` | UpdateWorld |
| `FUN_001da730` | set local pos |
| `FUN_0024cb70` | sin |
| `FUN_0024a080` | RandomInt [lo, hi) |
