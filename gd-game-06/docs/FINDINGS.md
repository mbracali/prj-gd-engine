# gd-game-06 FINDINGS

Guitar Hero: Aerosmith (Xbox 360). Aerosmith ships on **GH3's engine
(GH3_xp1)**; it is largely a content/skin reskin of GH3, and its executable
shares ~97% of its functions with GH3's. Legend: [C] = confirmed from binary
evidence, [I] = inferred, [faithful] = port matches the original.

---

## Controls audit (2026-11-24)

Scope: the INPUT LAYER only (how the raw controller is read and turned into
frets / strum / whammy / tilt), not the judge. Aerosmith is GUITAR/BASS only
(no drums, no vocals). Because Aerosmith runs GH3's engine, this pass
**confirms** that its input layer is GH3's, citing the relocated Aerosmith
addresses, rather than re-deriving the model; the reference audit is
gd-game-04 (GH3) `docs/FINDINGS.md` "Controls audit (2026-11-24)" (sections
A-D below mirror its structure). Read from the Ghidra export
(`ghidra/analysis/`, base 0x82000000); REA cannot open the XEX2. Our side:
`engine/src/input/input.{cpp,h}`. The feature branch's
`docs/spec-gamepad-menu.md` is not a tracked spec of the control model; the
port's model is the one in `input.h`. No code was changed this pass
(rationale at the end).

Bottom line up front: **Aerosmith's input layer matches GH3's in every area
checked, with no Aerosmith-specific divergence.** The functions are the same
logic at shifted addresses. Our gd06 port's `input.{cpp,h}` is byte-identical
to gd04's engine apart from cosmetic `gd04`->`gd06` renames, so our gaps and
the prioritized list are the SAME as gd04's and are SHARED with it.

### A. The original, per input area (binary evidence, Aerosmith addresses)

Address map (Aerosmith -> the GH3/gd04 equivalent it matches):

| Area | Aerosmith (gd06) | GH3 (gd04) | Match |
|---|---|---|---|
| guitar-vs-pad type predicate | `0x82107d40` (and inline in the readers) | 0x82100DA0 | [C] identical |
| held-fret nibble reader | `0x82107de0` | 0x82100DA0 | [C] identical |
| strum reader + pad released-latch | `0x82108000` | 0x82100FC0 | [C] identical |
| axis byte -> float normalization (tilt/whammy) | `0x8219ff48` | 0x82112EE0 family | [C] identical |
| Select / Star-Power deploy (qbkey 0xB40D153F) | `0x82110000` region (approx +0x2877-0x2963) | section 6 | [C] identical |
| whammy/tilt axis push to QScript (`whammybar`) | `0x82108268` | section 5 | [C] identical |

**A1. Per-player struct and connected flag [C].** `FUN_820eb330()` returns
the per-player input-system struct; `FUN_820eb310(p)` is the per-port "usable
controller" flag the readers bail on when 0 (gd04: `FUN_820e7cd0` /
`FUN_820e7cb0`). Inside the struct: `+0x18` = pad record, `+0x1c` = device
descriptor -- same offsets as GH3.

**A2. Controller TYPE detection: binary guitar-vs-pad [C `0x82107d40`,
`0x82107de0`, `0x82108000`].** The guitar flag is `descriptor[+0x08] == 8`,
tested as `*(int*)(*(int*)(struct+0x1c)+8) == 8`, and only for player < 4
(p >= 4 is always a guitar). There is NO per-model / X-plorer / Les-Paul /
wireless branch at the gameplay input layer -- one "guitar" path (==8), one
"pad" path for everything else. Identical to GH3 A2; on-screen instrument
skins are chosen in QScript and are cosmetic, separate from this play path.

**A3. Fret read: nibble-per-fret [C `0x82107de0`].** The held-fret mask is
green 0x1, red 0x10, yellow 0x100, blue 0x1000, orange 0x10000 (a 20-bit
value, not bits 0-4), byte-for-byte as GH3:
- guitar path: yellow = pad[+0x08], blue = pad[+0x09], orange = pad[+0x0a],
  red = pad[+0x0b], green = pad[+0x0c];
- pad path (analog): orange = pad[+0x0e], blue = pad[+0x0c], yellow = pad[+0x0d],
  red = pad[+0x0f], green = pad[+0x0a].
A fret counts as held when its byte is non-zero (already thresholded in the
pad record; no per-fret dead zone in this layer).

**A4. Strum source [C `0x82108000`].** guitar: a strum is the strum-bar bits
`0x1000 | 0x4000` in pad[+0x34]; on the edge the held frets (A3 guitar bytes)
are latched and a strum flag raised. pad: a strum is any CHANGE of the held
fret/face set (each new press strums), with QScript rule `0x67e6859a`
consulted and a per-player "all buttons released" latch at
`DAT_8298e034[p]` (a pad chord must release fully before the next; gd04's
same latch lived at `DAT_829ba034`). Select is Star Power on both. There is
NO input-layer strum-repeat / strum-debounce timer -- same as GH3; repeated
strums are governed by the judge, not by an input-layer constant.

**A5. Tilt / Star Power [C `0x8219ff48` + `0x82110000`].** The stick-axis
bytes are normalized identically to GH3: `v = byte - 128`; `v == -128` is
clamped to -127; the result is `(v - 50) / 77` for v > 0 and `(v + 50) / 77`
for v < 0, implemented as `(float)(+/-50 + v) * 0.012987013` (0.012987013 =
1/77) -- so -127 -> approx -1.0, +127 -> approx +1.0. Star Power via tilt
counts frames past the player's calibrated threshold (the
`star_power_position_device_*` user option; loaded per controller), and
Select (qbkey `0xB40D153F`, confirmed at the `0x82110000` deploy logic and
the `0xb40d153f || 0x92313ec8` Select check at `0x82280000`) deploys at once.
Matches GH3 A5.

**A6. Whammy [C `0x82108268`; QScript].** `0x82108268` reads the raw whammy /
tilt axis bytes (pad[+0x22], pad[+0x23], each `- 0x80`) and pushes them to
QScript (`whammybar`, qbkey at 0x82007df4; plus `SetNewWhammyValue`,
`MakeStarWhammy`, `UpdateWhammyBarProps`). The judge consumes a continuous
0..1 whammy; the raw-axis->0..1 scaling and its dead zone live in the
XInput->pad-record fill, which is not in the decompiled set (see Uncertain).
Matches GH3 A6.

### B. What OUR gd06 port does

`engine/src/input/input.{cpp,h}` is byte-identical to gd04's engine except for
cosmetic `gd04`->`gd06` string/comment/env-var renames (e.g. `GD06_SHAKE_*`,
`GD06_VIRTUAL_PAD`, `gd-engine/gd06/` pref dir). So gd04's section B applies
verbatim:
- Pipeline: SDL event -> `Input::handle` -> `InputEvent` -> `SongScreen::handle`
  latches held/strum/star/tilt/whammy -> fixed 60 Hz step builds `FrameInput`
  -> the C judge.
- TYPE model: pad profiles collapse to the same binary guitar (strum bar) vs
  joypad (every fret press strums) split as GH3 A2.
- Fret read: SDL digital buttons + axis-as-button with hysteresis
  (`|axis-rest| > 16000` to press approx 0.49, `< 10000` to release approx 0.31).
- Whammy: axis normalized from rest, dead zone 0.08, value `(x-0.08)/0.92`;
  discrete action past 0.15; continuous value to the judge.
- Tilt: a Gyro axis source held alone sets `p.tilt`, deploying after 3 judge
  steps; Back/Select deploys at once. The axis uses the generic 16000/10000
  threshold, NOT a per-controller calibrated tilt position.

### C. Gaps and errors (original vs port) -- SHARED WITH gd04

Because the port is byte-identical to gd04's, these are the same items as
gd04's section C and should be fixed once, across games:
1. [I, needs-coordination] Tilt threshold is not calibrated: GH3/Aerosmith
   fire per `star_power_position_device_*` (A5); the port fires at a generic
   approx 0.49 axis deflection and ignores the calibration screen.
2. [I, needs-coordination] Axis-as-button press threshold approx 0.49 (GH3's
   own pad frets are digital d-pad bits + raw-thresholded analog bytes, so
   there is no single number to copy -- a universal-model tuning decision).
3. [I, low] The Joypad fret order is GH2's `joypad_guitar`, not GH3's pad
   mapping (A3/A4 pad bits). Non-primary path, rebindable, but a documented
   divergence.
4. [I, low] Whammy dead zone 0.08 and the 0.15 action threshold are port
   inventions; the original raw whammy range / dead zone were not recovered
   (A6), so there is nothing to verify them against. The continuous value the
   judge consumes is faithful regardless.
5. [faithful, note only] The 60 Hz fixed step quantises strum/fret timing to
   step boundaries and collapses multiple SDL strums in a step to one pulse;
   the original also samples once per game frame, so this is faithful.
6. [faithful] guitar-vs-pad binary split, strum-bar frets latched on the edge,
   "each press strums" for pads, no input-layer strum-repeat timer -- all
   match A2/A4.

### D. Prioritized improvement list (shared with gd04/GH3)

Small-safe (isolated, unit-testable with virtual pads; still gated by the
owner rule that input BEHAVIOUR changes need real-device logs -- none here, so
deferred this pass to keep it documentary):
- [S1] Add input-selftest coverage pinning the current axis-as-button
  thresholds and the whammy dead zone (regression guard before any tuning).
  Test-only, no behaviour change.
- [S2] Name the 0.49/0.31 and 0.08/0.15 constants as `constexpr` with the
  GH3/RB references beside them (no value change).

Needs-coordination (touch the shared control model; do once, across games):
- [N1] A per-controller calibrated tilt threshold wired into the Gyro axis
  test (GH3's A5). Risk: medium (shared input path, every guitar).
- [N2] Decide one axis-as-button threshold + release hysteresis across games,
  grounded in device logs. Risk: medium (changes fret/strum feel).
- [N3] Optionally offer GH3's own pad fret order (A3/A4) as the Joypad
  default. Risk: low-medium.

### Uncertain / not recovered

- The XInput/XamInputGetState -> pad-record fill is not in the decompiled set,
  so the raw stick/trigger dead zones, the whammy axis->byte scaling, and the
  exact sources of pad[+0x01]/pad[+0x22]/pad[+0x23] (axes) and pad[+0x34]
  (buttons) are inferred from how the gameplay layer consumes them, not read
  at the source. Same limitation as gd04.
- Menu auto-repeat timing (hold/repeat) was not pulled this pass.

No code change was made. Per the owner rule (objective checks, not eyeballing:
input behaviour changes need data-derived expected-vs-measured checks and
real-device logs) there are no device logs in this pass, and confirming the
layer matches GH3 -- which it does, with the addresses above -- is the safe
result. Every candidate change is either test-only (S1/S2, deferred) or a
shared-model change for the orchestrator to coordinate across games.
