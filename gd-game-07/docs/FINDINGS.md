# Guitar Hero: World Tour (Xbox 360): findings

Each line **[C] address / data** (confirmed on our disc) or **[I]** (inferred).
Addresses are the real function entries in our `default.xex`
(base 0x82000000), taken from `ghidra/analysis/` (functions.tsv, cfuncs.tsv,
refs.tsv, strings.tsv, decomp/). QB checksums ("QB keys") are resolved against
the disc's debug-name table (`extracted/names.tsv`, built by
`tools/qbkey.py --table`). See `docs/FORMATS.md` for the data formats.

> World Tour runs the **gh4 engine**, the same engine as Guitar Hero: Metallica
> (gd-game-10). The gh4 controls model was audited in depth for gd10; this
> document confirms the model on the World Tour binary and focuses on the
> **World Tour-specific input** (the guitar touch strip above all).

## Controls audit (2026-11-24)

Analysis-first, cautious pass requested after the owner reported the controls
are "VERY FRAGILE" with "a bunch of errors". Goal: confirm the gh4 input model
on the World Tour binary, understand the **World Tour-specific input** (the
guitar **touch strip** is the big one), compare to our port, and produce a
prioritized list. **No code change was made** (rationale in part (e)): the
fragile input engine must stay untouched, and every grounded gap is either a
change to that fragile layer or a cross-game control-model extension the
orchestrator must integrate as one consistent model. Evidence is the static
Ghidra export (REA cannot open the XEX2), the QScript native table
(`ghidra/analysis/cfuncs.tsv`), the chart/track name table
(`extracted/names.tsv`), and the port source read in this worktree.

This section is **self-contained** and appended at the end of the file so it
can be lifted onto the feature branch independently.

### (a) The gh4 model is confirmed on the World Tour binary

Every gh4 input native from the gd10 audit is present in World Tour, with
World Tour's own addresses, and the key structures are byte-for-byte the same:

- **Per-controller record** = `FUN_8212e700(index)` (gd10's `FUN_82134d98`).
  `record+0x18` is the raw input struct; `record+0x1c` is the device
  descriptor; `descriptor+8` is the **controller-type enum**: `8`/`9` guitar,
  `10` (0xa) Guitar Hero drum kit, `11` (0xb) Rock Band drum kit.
- **Controller-type classification.** `GetControllerType`
  [C 0x8217cf68, native entry 0x828145a0] reads the `controller` tag
  (qb 0xb30d9965) and maps a subtype 0..4 to one of four QB names it hands to
  script: `guitar` (0xbdc53cf2), `drum` (0x5cc16cdf, subtypes 1 & 2),
  `vocals` (0xe8e6adcb, subtype 3), `controller` (0xb30d9965, fallback). The
  fallback branch reads `descriptor+0x9c` and the `roland_drumkit`/network
  globals - identical to gd10. Helpers:
  `IsGuitarController` [C 0x8217d0a0] (type 8 or 9),
  `IsDrumController` [C 0x8217d1d0] (type 10 or 0xb),
  `IsRBDrum` [C 0x8217d2e0 -> 0x8217d260],
  `IsGuitarTouchController` [C 0x8217d130] (flag at `record+0x18 + 0x4a`),
  `GetActiveControllers` [C 0x8217e8c8],
  `IsStandardGuitarControllerPluggedIn` [C 0x8217e978],
  `IsMicrophonePluggedIn` [C 0x8217e9d0], `GuitarControllerMake` [C 0x8217dc20].
- **Guitar.** Press/strum natives `ControllerPressed` [C 0x823e0f90],
  `ControllerMake` [C 0x823e1078], `ControllerDebounce` [C 0x823e0450] - the
  last reads a `time` float (qb 0x906b67ba, default 1.0 = 0x3f800000): the same
  **time-based press debounce** as gd10. Whammy is an analog bar driving the
  star-whammy and an audio pitch shift: `UpdateWhammyBarProps` [C 0x8218b838],
  `SetNewWhammyValue` [C 0x8218e698], `SetAllWhammyValues` [C 0x821806d0],
  `ControlWhammyPitchShift` [C 0x82180798], `WhammyFXOn/Off/OffAll`
  [C 0x8218ee58 / 0x8218f090 / 0x8218f1d0], `SongSetPlayerWhammy` [C 0x82183fd0].
  Tilt / Star Power: `check_for_star_power_CFunc` [C 0x82190128],
  `StarPowerFastTest` [C 0x821889f0]. HOPO at the judge:
  `ProcessHammerOns` [C 0x82147f00],
  `fretbar_update_hammer_on_tolerance_CFunc` [C 0x821810b0],
  `GetStrumPattern` [C 0x82180cd8], `Band_SetStrumStyle` [C 0x82117100],
  auto-strum/auto-fret toggles [C 0x82186cf8 / 0x82186d98].
- **Drums (first-class, per-pad velocity).** `GetDrumHitVelocity`
  [C 0x821860c8] is identical to gd10: it reads `record+0x18`, checks the drum
  flag at `+0x49`, and for a type-10 GH drum takes the **max of six velocity
  bytes at +0x43..+0x48** (pads + cymbals + kick), storing `velocity`
  (qb 0x41272956). `JamInputDrumStrum_CFunc` [C 0x821491a8] and the
  `_drums_notes` track back a separate, judged drum part.
- **Vocals (first-class mic/pitch subsystem).** `IsMicrophonePluggedIn`
  [C 0x8217e9d0], `Vocals_GetMicType` [C 0x821a1710],
  `Vocals_GetAvailableMics` [C 0x821a1138],
  `Vocals_ControllerHasUsableHeadset` [C 0x821a1268],
  `Vocals_SetMicGain` [C 0x821a17c0], `IsMicAttachedForController`
  [C 0x823dd1e8], `VocalDSPSetParams` [C 0x82184710],
  `PitchDetectWav` [C 0x82183c70], `InitJamMic` [C 0x8214cb70],
  `InitJamOverVocals` [C 0x821484e8], `VocalsHighway_*` [C 0x821a1...]. Pitch /
  phrase scoring is read from the mic.

**Conclusion:** World Tour's guitar/drum/vocals input model is the gh4 model
already documented for gd10. The drum and vocal gaps below are **shared with
gd10** (same engine, same missing port support). What is **World Tour-unique**
is the guitar touch strip, next.

### (b) World Tour-UNIQUE input: the guitar touch strip (tap / slide)

The World Tour guitar adds a **touch strip** (a touch-sensitive slider above
the five frets) used for **tap notes** (tap the right zone instead of
strumming) and **slides**. This is a real, substantial subsystem in the
binary, and it is **entirely absent from our port**.

- Songs carry a dedicated per-difficulty **`_Tapping`** note track, e.g.
  `goyourownway_Easy_Tapping`, `bandontherun_guitarcoop_Expert_Tapping`,
  `whativedone_guitarcoop_Medium_Tapping` [C names.tsv], plus slide tutorials
  (`tut_gtr_slide_song_rhythm_Easy`). These are the notes the touch strip
  plays. (This is distinct from "tapping HOPOs" = hammer-on/pull-off, which the
  port already judges.)
- The touch strip is **gated by** a global/flag `enable_touch_strip`
  (qb 0x09383b1b) and by `IsGuitarTouchController` (the `record+0x18 + 0x4a`
  flag). `enable_touch_strip` is checked before any touch-strip logic runs
  [C 0x8213e4b8, guarded by the `+0x4a` flag and `enable_touch_strip`].
- **Slider read + zone matching:** `Function_8213e4b8` reads the analog slider
  position from the player's `leftx_unscaled` property (qb 0xa64e101c),
  iterates the `guitar_touch_patterns` array (qb 0xe52dcb67) - each entry has a
  `touch_value` (qb 0x80eb06d4) and a `pattern_string` (qb 0xa8e2b1e6) - and
  picks the **nearest** pattern by absolute distance, with a window of **+/-16**
  units; crossing the window edge (while sliding) copies the matched
  `pattern_string` out [C 0x8213e4b8].
- **Per-frame tap / slide processing + debounce:** `Function_8213e830` runs
  each frame (gated by the `+0x4a` touch flag), tracks the current pattern in a
  state block, and only commits a change after a time/frame debounce
  `touch_pattern_switch_frames` (qb 0x780da03d); it stores the chosen
  `touch_pattern` (qb 0x30c8fcf2) and raises the script event
  `guitar_touch_update_hilite` (qb 0x02be3a14) to highlight the touched zone
  [C 0x8213e830]. `Function_82144c80` [C] is the touch-strip "current zone"
  accessor, also gated by the `+0x4a` flag.
- A related path `Function_82144d28` [C] decodes the slider analog (via the
  raw struct at `record+0x18`) and scales it to a 10..0x7f range - the
  continuous touch value used elsewhere (e.g. jam / GH-Mix style continuous
  input). Many gameplay functions call `Function_8213e4b8`
  (0x8213f7a0, 0x8213fe50, 0x82140020, 0x82141d80, 0x82143ff0, 0x82144c80,
  0x82145060 [C refs]), so the touch strip is woven through the note/jam
  gameplay, not a side feature.

So the original reads the touch strip as an analog slider, matches the finger
position to discrete **tap zones / patterns** (debounced, with slide
detection), and plays the song's `_Tapping` notes from it.

### (c) What OUR port does (and where it stops)

The port reuses gd01's **GH1/GH2-lineage** input engine
(`engine/src/input/input.{cpp,h}`), not a gh4 model. It exposes the universal
**15 controls** (`Input::Control`, input.h:106: General Start/Select/MenuUp/
MenuDown + 5-fret guitar Start/Select/StrumUp/StrumDown/Green/Red/Yellow/Blue/
Orange/Whammy/Gyro) and **three instruments**
(`enum class Instrument { Guitar, Gamepad, Midi }`, input.h:129).

- **Device classification** (input.cpp:618-622): Rock Band HID guitar ->
  `RockBand`, Xbox/Santroller guitar -> `XboxGuitar`, PS2-adapter guitar ->
  `Ps2Guitar`, else `Joypad`. There is **no** controller-type classification
  for a drum kit, headset, or a World Tour guitar's touch strip.
- **Guitar input:** strum is a pulse; whammy is an analog axis (Whammy action
  past 0.15); axis-derived buttons use a half/third hysteresis debounce + a
  ~300 ms post-connect settle. Tilt/Star Power is modelled in `player.cpp`.
- **Chart tracks parsed** (chart.cpp:152-187): `_Star`, `_StarBattleMode`,
  `_cameras_notes`, `_lightshow_notes`, `_crowd_notes`, `_drums_notes`,
  `_anim_notes`, `_scripts_notes`, `_triggers_notes`, `_performance_notes`.
  **`_Tapping` is not parsed.** `_drums_notes` drives only the AI drummer
  animation, never a playable part. Vocals are AI only.
- The port's **"touch"** code (input.h:280-325, `touchEvent`/`touchGesture`)
  is the **Android on-screen touchscreen** (fingers on highway lanes, menu
  taps, on-screen buttons). It has nothing to do with the World Tour guitar's
  **hardware touch strip**.

### (d) Gaps and errors (original vs port)

WORLD TOUR-UNIQUE (not in the gd10 audit):

- **No touch-strip / tap-note support at all (major, WT-unique).** The original
  reads the guitar touch strip (`leftx_unscaled` slider ->
  `guitar_touch_patterns` zone matcher, debounced, slide-aware) and plays the
  per-difficulty `_Tapping` track. The port parses no `_Tapping` track, reads
  no slider, has no touch-strip device concept, and no tap-zone input. On a
  real World Tour guitar the touch strip is simply dead; tap-only passages that
  the original lets you tap can only be strummed in the port (and `_Tapping`
  notes, if the chart has them as a separate track, are never scheduled).

SHARED WITH gd10 (same gh4 engine, same missing port support):

- **No drum instrument (major, shared).** Original: GH/RB drum kit (type
  10/11) with six velocity bytes [C 0x821860c8]. Port: no Drums instrument, no
  pad/cymbal/kick controls, no velocity. A real drum kit falls through device
  classification to `Joypad` and silently plays the *guitar* part on scrambled
  buttons - a **mis-detection error**, not just a missing feature. (The
  Santroller check already excludes "drum" from becoming a guitar,
  input.cpp:70, but a generic drum kit still reaches `Joypad`.)
- **No vocal instrument (major, shared).** Original: full mic/pitch/phrase
  subsystem; port: nothing (AI vocals only).
- **No controller-type classification (shared).** No port analogue of
  `GetControllerType`'s guitar/drum/vocals/generic decision.
- **Guitar debounce model differs (shared, low concern).** Original: a
  `time`-based press debounce (`ControllerDebounce`, default 1.0). Port:
  half/third axis hysteresis + 300 ms settle. Likely fine; different mechanism;
  the exact original `time` unit was not traced.
- **Engine-lineage mismatch (shared).** The port's input engine and defaults
  are GH2-era, bolted onto a gh4 title; the 5-fret guitar mapping is plausibly
  faithful but the band + touch-strip model is absent (the known limitation in
  `docs/spec-gamepad-menu.md`).

Not treated as defects here (looked correct on reading, not changed): the
whammy 0.15 action threshold, the strum/HOPO window numbers (from the tuning
table, already RE-grounded in gd10), and the per-frame SP tilt count.

### (e) Prioritized improvement list

SMALL / SAFE (low risk, isolated - still needs the owner/orchestrator's OK
before coding; none done in this cautious pass, matching the gd10 precedent):

1. **Touch-strip presence log (WT, logging-only).** When a connected guitar
   reports the touch flag (or by USB id / SDL name), log it once so the
   touch-strip gap is visible and future work is data-driven. Risk: LOW
   (logging only, no judge/detection change).
2. **Drum-kit mis-detection guard (shared with gd10).** When a device is a
   known drum kit (USB id / SDL name contains "drum"), do not silently bind it
   to the guitar `Joypad` layout; surface it in the Gamepad menu as an
   unsupported instrument so it does not scramble the guitar part. Risk: LOW
   (classification only) - but it still touches the fragile layer, so
   coordinate first.
3. **Input-area trace (shared).** A `GD07_INPUTLOG`-style trace of the detected
   controller type and chosen profile per device. Risk: LOW (logging only).

NEEDS COORDINATION (larger; must fit the ONE universal control model the
orchestrator owns - do NOT implement piecemeal):

4. **Touch-strip input + `_Tapping` track (WT-unique, biggest WT item).** Parse
   the per-difficulty `_Tapping` track (chart.cpp), add a touch-strip input
   source (analog slider -> tap zones with a switch debounce and slide
   detection, mirroring `leftx_unscaled` / `guitar_touch_patterns` /
   `touch_pattern_switch_frames`), and judge tap notes without a strum. This is
   the defining World Tour mechanic and has no analogue in any other game, so
   it needs its own place in the control model. Evidence:
   [C 0x8213e4b8, 0x8213e830, 0x82144c80], tracks `*_<Diff>_Tapping`.
5. **Drums instrument + drum track (shared with gd10).** Extend the Control set
   for drum pads/cymbals/kick + a `Drums` instrument + `ControllerMode::Drums`
   + a drum note-judge track. Cross-game (gd05 Rock Band and gd10 share this).
   Evidence: `GetDrumHitVelocity` six-byte model [C 0x821860c8],
   `_drums_notes`.
6. **Vocals instrument + mic input (shared with gd10).** Pitch/phrase per the
   `Vocals_*`/`InitJamMic` natives. Cross-game.
7. **Per-pad drum velocity / dynamics** once (5) exists [C 0x821860c8].
8. **Controller-type classification** mirroring `GetControllerType`
   (guitar/drum/vocals/generic) so the Gamepad menu offers the right instrument
   per device, and so a touch-strip guitar is recognised.

**Why no code change now:** every grounded fix either changes the fragile
input/detection layer or is a cross-game control-model extension the
orchestrator must integrate as one consistent model. The touch-strip +
`_Tapping` work (4) is a new gameplay mechanic, not a "small isolated fix". The
owner's brief accepts "a thorough findings doc with no risky change" as
success, so this pass stops at analysis.

### Uncertain / not fully traced

- The exact byte offsets for frets / strum in `record+0x18` were not traced;
  the drum velocity bytes `+0x43..+0x48`, the drum flag `+0x49`, and the touch
  flag `+0x4a` are confirmed.
- The precise unit of `ControllerDebounce`'s `time` (frames vs seconds) and of
  `touch_pattern_switch_frames` was not traced (the name implies frames).
- Whether the touch strip is read purely in the native layer or partly walked
  by the QScript interpreter inside `Function_8213e4b8`/`_830`: the slider read
  (`leftx_unscaled`) and the pattern/zone logic are confirmed; the full
  per-pattern QScript was not exhaustively decoded. The `guitar_touch_patterns`
  entries' exact shape (how many zones, their `touch_value`s) was not dumped
  from the song/guitar QB.
- No controller/XInput reading below `FUN_8212e700` was disassembled
  instruction-by-instruction; none of the functions read appeared to be
  VMX128.

## Controls Phase B: World Tour touch strip (2026-11-24)

Phase B took the audit's item (4) and shipped the **safe, tested increment**:
the `_Tapping` data is now parsed into the chart model, and a self-contained
**touch-strip input model + tap judge** mirrors the original's slider/zone
logic and is objectively tested. **Normal strummed / HOPO guitar play is
unchanged** - none of the Player/Match judge code was touched; the tap path is
new, separate code. The full hardware-slider gameplay wiring is documented as
remaining (part (e) below).

### (a) The `_Tapping` track format (confirmed on the disc)

Dumped from the World Tour disc with `tools/qb.py`/`pak.py`/`xdvdfs.py`
(song paks `DATA/COMPRESSED/SONGS/<song>_song.pak.xen`):

- Each `<song><infix>_<Diff>_Tapping` section is a **type-0xC array of
  `[time, length, count]` int triples** (one span per row). Present (usually
  empty) for every part infix (``/`_rhythm`/`_guitarcoop`/`_rhythmcoop`) and
  difficulty, exactly alongside the `_Star` phrases.
- `time`/`length` are ms; `count` is the taps in the span: **1, 2 or 4**
  observed across the disc. These are **tap spans**, not per-fret gems (a
  handful per song: e.g. satchboogie 4 spans, lazyeye/hotforteacher 4-6,
  purplehaze 2). goyourownway's `_Tapping` sections exist but are **empty** -
  the audit cited the name, not non-empty data; non-empty tap songs include
  satchboogie, hotforteacher, beatit, crazytrain, purplehaze, schism, parabola,
  lazyeye.

### (b) `guitar_touch_patterns` (dumped, now data-driven in the port)

`guitar_touch_patterns` (qb 0xe52dcb67) in `PAK/qb.pak.xen` is **9 zones**,
each `{touch_value (0x80eb06d4), touch_pattern (0x30c8fcf2), pattern_string
(0xa8e2b1e6)}`:

| touch_value | touch_pattern |
|-------------|---------------|
| -107 | 0x10000 |
| -80  | 0x11000 |
| -51  | 0x1000  |
| -26  | 0x1100  |
|  26  | 0x100   |
|  46  | 0x110   |
|  72  | 0x10    |
|  99  | 0x11    |
| 127  | 0x1     |

Five single-finger zones (0x1 / 0x10 / 0x100 / 0x1000 / 0x10000 - one nibble a
fret) and the four two-finger zones between them. `touch_pattern_switch_frames`
(qb 0x780da03d) is **0** on the disc (a change commits at once).
`enable_touch_strip` (qb 0x09383b1b) is **not a data section** - the game sets
it from a script, so the port defaults it on and lets a present 0 turn it off.

### (c) What Phase B implemented (files:line)

- **`_Tapping` parse** - `engine/src/game/chart.h` (`struct TapPhrase`,
  `PartChart::tapping`) and `engine/src/game/chart.cpp` (in `loadChart`, the
  `<base>_Tapping` rows parsed with the existing `rows()` helper, right beside
  the `_Star` phrases). Purely additive: the note/star/event tracks are
  untouched, and the parse runs unconditionally (data present; the
  `enable_touch_strip` gate governs *use*, below).
- **Touch-strip input model** - `engine/src/game/touch_strip.{h,cpp}`,
  `class TouchStrip`: loads `guitar_touch_patterns` + `touch_pattern_switch_frames`
  + `enable_touch_strip` from the globals (nine-zone disc fallback when absent),
  `nearestZone()` = the nearest zone within +/-16 units [C 0x8213e4b8], and
  `frame(slider, down)` runs the per-frame commit with the switch-frame debounce
  and **slide detection** (a committed zone change while the finger stays down)
  [C 0x8213e830]. SDL-free; fed 60 Hz like `Player`.
- **Tap judge** - `class TapJudge` (same files): judges committed taps/slides
  against the `_Tapping` spans ([time, time+length] window, +/-100 ms grace),
  clears a span after its `count` taps, and scores each tap 50 pts (a note's
  base, one finger). **Kept entirely apart from `Player`**, so the strummed
  score is never affected.
- **Objective tests** - `engine/src/game/touch_strip.cpp` `touchStripSelfTest()`
  (data-independent: nearest-zone, dead-band, tap-vs-slide, debounce, and a
  three-tap span judged to 150 pts with a pre-span tap rejected), run once from
  `engine/src/tests/selftest_play.cpp`; plus `tapOnce()` there, which plays the
  real `_Tapping` spans of every WT song through `TouchStrip`+`TapJudge` and
  asserts every span clears. New totals in `PlayTotals` (spans parsed, taps,
  judge runs/fails, self-test) are printed by `--selftest`.

### (d) Objective test evidence

`gd07 --selftest "<World Tour disc>"` prints a `touch strip (GHWT): N _Tapping
spans parsed ... tap judge runs ... logic self-test passed` line and keeps
`autoplay: ... charts failing 0` (normal strummed/HOPO play unaffected). See
the commit message / the agent report for the measured line.

### (e) What remains for full hardware touch-strip play (honest)

Phase B delivers **data + a tested judge for synthetic/fallback input**. Not
done (the fragile live-input + UI wiring, to be integrated as one control
model, never piecemeal):

1. **Live slider wiring.** Map a real World Tour guitar's analog slider /
   touch flag (the SDL axis + the `record+0x18 + 0x4a` equivalent) into
   `TouchStrip::frame`, and a sensible fallback for players without the
   hardware (e.g. a key/axis). This touches the fragile `input/` layer that
   Phase A owns - do not change the shared SDL->action axis/tilt constants.
2. **Feeding the judge in a real song.** Construct `TapJudge` from
   `chart.parts[part].tapping[diff]` at match start when `enable_touch_strip`
   is on, step it each frame beside `Player`, and fold its score into the HUD.
3. **The per-gem zone meaning.** Phase B treats a span as "`count` taps in the
   window". Whether each tap must land on a *specific* zone (the HOPO gem's
   fret within the span, via `touch_pattern`) was not fully traced - the
   `_Tapping` data is only `[time, length, count]`, and the per-gem zone would
   come from the main note track's HOPO gems inside the span. Confirm against
   `Function_8213e830`'s use of `touch_pattern` before judging per-zone.
4. **Slide tutorials / `guitar_touch_update_hilite` UI** (the touched-zone
   highlight) and the `tut_gtr_slide_*` tutorial songs.

### Uncertain / not fully traced (Phase B)

- The exact `count` semantics (taps vs. zones vs. a bitmask) - 1/2/4 look like
  tap counts but were not cross-checked against `Function_8213e830`.
- Whether `enable_touch_strip` is ever 0 in shipped play (it is script-set; the
  port defaults it on).
- Whether a real tap must match the HOPO gem's zone (see (e).3).

## Controls Phase B — drums (2026-11-24)

Mirrored from gd10 (Metallica, the same gh4 engine): detect a drum kit, guard
the guitar part from it, and add a tested, rendering-free gh4 drum judge. This
note is self-contained.

### Why

World Tour's drum kit is a first-class controller in the original (the joypad
descriptor's type enum = `10` GH drum kit / `11` RB drum kit; `IsDrumController`
[C 0x8217d1d0], `IsRBDrum` [C 0x8217d2e0]; `GetDrumHitVelocity` [C 0x821860c8]
reads the per-pad velocity bytes at `record+0x18+...`, taking the max of six for
a type-10 GH drum) — see the Controls audit above. This port grew from gd01's
five-fret guitar engine, so it had no drum instrument: a drum kit fell through
to the gamepad (`Joypad`) profile and played the **guitar** part on scrambled
pads (the active mis-detection error).

### Detection + guard (input layer)

- `input.cpp` `isDrumKit(id, pad)`: SDL joystick type `DRUM_KIT`, or `"drum"`
  in the controller name (Santroller / Rock Band / GH kits, and the HID kits
  SDL has no gamepad map for). A guitar is never matched. [I] SDL exposes only
  the joystick type and the name on PC, not the USB descriptor's type enum.
- At connect, `Pad::drum = profile == Joypad && isDrumKit(...)` (a detected
  guitar is never flagged). The seat is logged as "Drum Kit".
- `Input::drumKitSuppressesPlay(Action)` = frets | Strum | Whammy: the single
  source of truth for the guard (so `--drumtest` checks it without a device).
  `actionsOf`/`controlEdges` take a `drum` flag; for a drum seat the `on`
  lambda drops exactly those play actions and strum pulses are not counted, so
  a drum kit can no longer scramble-play the guitar. The menus, Start (pause)
  and Star Power (the gyro) still work. The flag is passed at every Pad control
  path (`releaseAll`, `update`, and the Phase-A gyro/tilt poll); the keyboard
  and MIDI paths keep the default `drum=false`, so the guitar path is
  byte-identical for every non-drum seat.
- `Instrument::Drums` ("Drum Kit") is surfaced in `instrument()`/`instruments()`
  and is fixed by detection (`setInstrument` refuses to change a drum seat).

### Drum judge (`game/drum_player.{h,cpp}`, data-free, `--drumtest`)

- Rendering-free, adapted from gd05's Rock Band `DrumPlayer` to gh4's chart.
  Builds a playable gem stream from the `<song>_drums_notes` kit-piece events
  (parsed into `Chart::drums`): note 73 kick, 75/76 toms, 77 snare, 78/79/84
  hi-hat, 80/81/82/85 cymbals, mapped onto the five GH drum lanes (kick / red /
  yellow / blue / green; the mapping is [I]). The arm-animation notes (37..57)
  in the same array are skipped. Each gem carries the recorded per-pad velocity
  (`ChartEvent::value`; `GetDrumHitVelocity` semantics), surfaced on the Hit
  event for feel/anim; a live strike velocity overrides it. Scores hits on the
  right lanes with the five-fret streak multiplier. Covered by `--drumtest`.

### Not yet done (honest scope — same as gd10)

- `_drums_notes` is the drummer's single performance/animation track (it drives
  the AI drummer), **not** gh4's per-difficulty gameplay drum chart, so this
  stream has no difficulty reduction.
- The judge is a tested component and is **NOT** wired into the live match loop
  (no HUD drum lanes, no routing of a live drum controller's pad/cymbal/kick
  into it, no Overdrive/fill deploy). Wiring it in is the remaining increment;
  it must keep the guitar path untouched for a guitar seat.

## Onboarding Step 2 — encoders + model explorer (2026-11-24)

Aggregate-first verification of the onboarding spec's Step 2 (gate 16%): can the
port READ AND USE 100% of the disc file types World Tour needs for
models/songs/venues/textures, and does the launcher's Explore show and load
every category? Evidence is fresh headless runs of the dev hooks on our own
disc (`gd-game-07/extracted/`, 31,554 files in 929 paks) plus the Ghidra/QB
format citations already in `docs/FORMATS.md`. **One test file was added
(`--exploretest`); no engine, input, judge, render or data-decoder code was
changed.**

### (a) Encoders — every model/song/venue/texture file type decodes

`gd07 --formattest gd-game-07/extracted` parses **every pak and every file in
it to its end** and prints `formattest: all files parse` (exit 0). Per-type
census and decoder status (the counts are this run's, not FORMATS.md's older
ones):

| Disc type | Count | Decoded? | Evidence (this run / port decoder) |
|---|---|---|---|
| `.pak`/`.pab` archives | 929 paks / 31,554 files | ✅ | all 929 `Pak::load` to end; `data/pak.cpp`, `data/xdvdfs.cpp` |
| QB `.qb`/`.mqb`/`.sqb` | 1,703 | ✅ | 43,601 sections, 8,292 scripts, **0 unused bytes**; `data/qb.cpp` |
| QB node arrays `.nqb` | 106 | ✅ | decoded as QB; `data/qb.cpp` |
| Particles `.pfx` | 106 | ✅ | `checkPfx`; `data/qb.cpp` |
| Scenes `.scn` | 107 | ✅ | mesh record 0x70 bytes [C FORMATS.md]; `data/scene.cpp` |
| Skinned models `.skin` | 1,696 | ✅ | `scene::parse(Skin)`; `data/scene.cpp` |
| Static models `.mdl` | 14 | ✅ | `scene::parse(Mdl)`; `data/scene.cpp` |
| Animation `.ska` | 5,707 | ✅ | Q key [C 0x82445E88], standardkeyQ byteswap [C 0x82445730]; `data/anim.cpp` |
| Skeleton `.ske` | 239 | ✅ | `data/anim.cpp` |
| Fonts `.fnt` | 15 | ✅ | `data/font.cpp` |
| Havok `.hkc` | 106 | ✅ | 7,254 rigid bodies, 7,056 constraints; Havok 5.5.0-r1; `data/havok.cpp` |
| Havok ragdoll `.rag` | 196 | ✅ | LZSS-wrapped packfiles; `data/havok.cpp` |
| Textures `.img` | 5,513 | ✅ | incl. CAS/normal-map/volume; `data/texture.cpp` |
| Textures `.tex` | 1,676 | ✅ | hash tables, 8_8/DXT1/volume (31 volumes, 352 slices, FNV-1a 1eb4a620); `data/texture.cpp` |
| Audio in-pak `.wav` (XMA2) | 3,106 | ✅ | RIFF+big-endian XMA2 chunk; `checkWav`, `audio/xma_decoder.cpp` |
| Audio banks `.fsb` (loose) | 486 | ✅ | song/preview FSB+XMA decoded by `--selftest`/`--songaudiotest`; `data/fsb.cpp`, `audio/song_stream.cpp` |
| `.table` | 70 | ✅ | u32 words 0xE5F50254 / 0x21775481 |
| Debug names `.dbg` | 2,680 | ✅ | `Disc::loadNames` (126,993 names resolved) |
| Video `.bik` (loose) | 23 | ✅ | FFmpeg; `video/movie_player.cpp` (not a model/song/venue type) |
| **Localized strings `.qs`** | **8,495** | ❌ | **no decoder — UI localization, not models/songs/venues/textures** |
| **GHTunes songs `.jam`** | **19** | ❌ | **no decoder — user music-studio content, not a set-list song** |

**Result: 100% of the model / song / venue / texture file types decode.** The
only two undecoded types (`.qs` localized UI strings, `.jam` GHTunes studio
songs) are outside the models/songs/venues/textures scope and are honestly out
of scope for Step 2 (`.qs` belongs to the UI-text milestone; `.jam` to the
music-studio feature).

### (b) Model explorer — every category shows and loads its models

Added **`--exploretest`** (`engine/src/tests/exploretest.{h,cpp}`,
`CMakeLists.txt`, `main.cpp`): a headless harness that reproduces the launcher
explorer's item list **with the same enumerators** (`venue::venueZones`,
`band::everyInstrumentGh4` split Guitarist/Bassist/Drummer/Vocalist →
Guitars/Bass/Drums/Mics, `band::everyTalentGh4` → Chars) and runs the **same
`band::loadModel` + `BandView::add`** that `module_explore.cpp`'s `select()`
runs before drawing. It does not include the module (that owns the launcher's
`GdExplorer` C ABI); it mirrors it, so a failure here is a failure the launcher
would show. `gd07 --exploretest gd-game-07/extracted` (exit 0):

| Explore category | Models | Load+render-add | Enumerator |
|---|---|---|---|
| Venues | 18 | 18 ok, 0 fail | `venue::venueZones` (+ zone pak parsed) |
| Guitars | 7 | 7 ok, 0 fail | `everyInstrumentGh4` role Guitarist |
| Bass | 7 | 7 ok, 0 fail | `everyInstrumentGh4` role Bassist |
| Drums | 3 | 3 ok, 0 fail | `everyInstrumentGh4` role Drummer |
| Mics | 3 | 3 ok, 0 fail | `everyInstrumentGh4` role Vocalist |
| Chars | 10 | 10 ok, 0 fail | `everyTalentGh4` preset roster |
| **Total** | **48** | **48 ok, 0 fail; 197,599 vertices** | — |

**Songs** is the separate `gd_game_songs` path (`module_songs.cpp`), proven by
`--selftest`: **133 songs, 928 charts, 473,025 notes, autoplay charts failing
0** (songs load, chart-parse and auto-play end to end; previews decode via the
FSB/XMA loaders). The gd07 explorer code is **identical to gd10's** apart from
the `GD07_*` env prefixes (same gh4 engine), so this also confirms the shared
explorer on World Tour's data.

Corroborating render evidence — `gd07 --chartest`: **26 character bodies
loaded, skinned and animated, 0 not on the disc, 321,604 vertices, 0 material
textures missing, 26/26 gh4 talents move their face**; the band renders in
every venue stage. (chartest's own instrument pass uses the non-gh4
`everyInstrument`, which is empty for gh4 — hence `0 instruments` there; that
gap is exactly what `--exploretest` closes by using `everyInstrumentGh4`.)

### (c) What changed

- **New, isolated:** `engine/src/tests/exploretest.{h,cpp}`, the
  `--exploretest` wiring in `engine/src/main.cpp`, and the source line in
  `engine/CMakeLists.txt`. Test-only; it touches no engine, input, judge,
  render or decoder code, so every pinned test stays byte-identical.
- No encoder/decoder change was needed: the port already decodes 100% of the
  in-scope types (part (a)).

### (d) What remains (honest)

- **`.qs` / `.jam` decoders** are still absent; both are **out of Step-2
  scope** (UI localization text; GHTunes music-studio songs). Noted for the
  UI-text and music-studio milestones.
- **Pre-existing, not Step-2:** `--selftest` reports one `FAIL jamsession: not
  on the disc` (a songlist entry with no `_song.pak.xen`; it is the music-studio
  jam, filtered out of the launcher's Songs list by `module_songs.cpp`), and
  `--chartest` reports 4 venue camera-framing FAILs (z_Goth, z_Recordstore,
  z_Harbor, z_Tool — the framed band member leaves a song-camera cut). These
  belong to the gameplay-data / animations-and-cameras milestones (Step 4/5),
  not to encoders or Explore. Explore frames venues with a fixed orbit camera,
  not the song cameras, so it is unaffected (all 18 venue paks parse).
- The explorer renders instruments/characters at the **bind pose**; animated
  Explore previews (`clip_count`/`clip_name`) are not wired for gd07 (optional
  ABI fields, same as gd10).


## Onboarding Step 3 — UI elements (2026-11-24)

Exhaustive capture of World Tour's UI/UX (backgrounds, fonts, menu
animations, gameplay widgets, menus, HUD), **aggregating what the port
already renders** against the original, with Ghidra/QB/disc evidence. The
driving fact: the port is **not a hand-built UI** — it runs the game's own
QScript + `descinterface` data through a faithful screen-element engine
(`ui/ui_system.cpp`, 143 KB, base element [C 0x8221DDB8], props [C 0x8221A738],
transforms [C 0x8221BF88], morphs [C 0x8221CEC8], menus [C 0x823164E0]). The
original's `CreateScreenElement` [C 0x823e2750], `SetScreenElementProps`
[C 0x823e2a70], `DoScreenElementMorph`, `AttachHudWidget` [C 0x82159878 /
native 0x828134a8], `CreateViewport` [C 0x823e5d40] all exist as port natives
(confirmed in `ghidra/analysis/cfuncs.tsv`). So menu layouts, positions, font
sizes and HUD widgets are **data-driven and faithful by construction** — what
is captured below is which data the port consumes and where it stops.

**Objective evidence captured this pass** (headless, `SDL_VIDEO_DRIVER=offscreen
SDL_AUDIO_DRIVER=dummy`, build `build/gd07-step3`, prebuilt ffmpeg prefix):
- `gd07 --uitest <disc>` with `GD07_UIDUMP=walk GD07_HUDLOG=1 GD07_WALKLOG=1`:
  **47 flow states started, 36 built render elements, 11 built no element**
  (all `online_*`/`leaderboard_*`/`view_leaderboard_fs` — online is stubbed,
  disabled in Step 7.8, so "no element" is expected). "**every screen was
  started through the flow manager.**" **912 ui-dump element lines** across the
  reached screens. **0 `UNHANDLED ui state/event/member`, 0 missing fonts.**
- `gd07 --song beatit --difficulty 3 --autoplay <disc>`: full song, **815
  notes**, venue `z_Metalfest` 107 textures **0 missing**, star-power on/off
  tracked, 113 drum-kick particles, **0 `UNHANDLED` HUD widget output types**.

### Backgrounds (menu/scene)

| Background | Original evidence | Port status | Action |
|---|---|---|---|
| Front end / main menu | `z_frontend` / `Z_Frontend` zone shown through `bg_viewport` (`setup_bg_viewport`), camera `PlayIGCCam` [C names.tsv; script] | **Reproduced.** `MenuScene` draws the current zone through `bg_viewport`, `PlayIGCCam` gated on `vp==bg_viewport` (`menu_scene.cpp:447`) | none |
| Store / character hub / select | `z_soundcheck` zone, camera morph (`store_camera_script`, `ch_view_cam`), crowd/menu boards drop in (`obj_movetorelpos`) | **Reproduced.** `menu_scene.cpp` handles `z_soundcheck`, node moves (`NodeMove`), camera morphs, light groups | none |
| Other zones | `z_viewer`, `z_tushino`, `z_harbor`, `z_credits` [C names.tsv] | **Reproduced.** generic zone loader (`ensureVenue`/`VenueView`) | none |
| UI viewport textures (WT-specific) | `create_viewport_ui` / `CreateViewportTextureOverride` [C 0x823e6000]: a viewport's windows drawn into a texture that overrides a zone texture (sound-stage `Alpha_texture*.dds`) | **Reproduced.** `MenuScene::uiTargets_` + `UiSystem::overrides_` (`drawUiViewports`) | none |
| Loading screen | `DisplayLoadingScreen <img> [spin_texture spin_x spin_y]` [C 0x8218DDE8], `HideLoadingScreen` [C 0x8218E0A0] | **Reproduced.** `UiSystem::loadingTex_`/`loadingSpin_`, full-screen over everything (`ui_system.cpp:2438`) | none |
| `bg_viewport` screen FX | `enable_bg_viewport_properties`: DOF_Off, TOD BnS + sbloom, character shadow | **Reproduced.** `MenuScene::fx_`/`fxParams_` + shadow pass | none |

### Fonts

Font families on the disc (`names.tsv`, `tools/pak.py list`): `fontgrid_text_a1,
a3, a6, a8, a10, a11`; `fontgrid_title_a1, a2`; the flaming variant
`fontgrid_text_a6_fire`; `buttons_x360` (button glyphs). Sizes/usage are
**data-driven** (each element's `dims` / `internal_scale` / `font_spacing` /
fit modes [C TextElement ctor 0x8231B788; fit data per desc]) — there is no
hard-coded size table to get wrong.

| Font asset | Original evidence | Port status | Action |
|---|---|---|---|
| `fontgrid_text_a6` and family | `ui_shared.pak.xen` `.fnt` entry; menus/HUD reference by name | **Loaded.** `loadFonts("DATA/COMPRESSED/PAK/ui_shared.pak.xen")` + `global.pak`, `text_a1.pak`, `debug.pak` (`ui_system.cpp:126-131`) | none |
| `fontgrid_text_a6_fire` (flaming) | `.fnt` in `ui_shared.pak.xen`; `.dds` `fonts\fontgrid_text_A6_fire.dds` | **Font loaded** (flaming glyphs available). Its animated fire **sprite overlay material** (`fire_2D_*`) is not drawn — see HUD follow-up | follow-up (GD10-068) |
| `buttons_x360` glyphs | `\bN`/`\mN` meta-button codes in text → buttons-font glyph [C 0x8228E0B0] | **Reproduced.** `UiSystem::setText`/`buttonsFont` | none |
| `\cN` colour codes | `Default_Font_Colors` table [C data] | **Reproduced.** `colorTab_` loaded, applied in text draw (`ui_system.cpp:132-138, 1632`) | none |

### Menu animations / transitions

| Animation | Original evidence | Port status | Action |
|---|---|---|---|
| Screen-element morph | `DoScreenElementMorph`/`List` [C 0x8218c1d0], 4 motions: linear / smooth / ease_in / ease_out [C 0x822A6B28] | **Reproduced + objectively correct.** `ease()` = `t`, `t²(3−2t)`, `t²`, `2t−t²` (`ui_system.cpp:92-98`); `startMorph` maps `motion` keys | none |
| State anim-in | state `anim_in_script`, else `ui_task_menu_default_anim_in_script`, spawned on enter [I] | **Reproduced.** `UiStates::enter` spawns it (`ui_states.cpp:84-88`) | none |
| State anim-out | `menu_anim_out_done` notification [I] | **Partial.** The port treats `menu_anim_*_done` as a no-op notification (`ui_states.cpp:160`); whether task states play an explicit exit anim before leaving not confirmed | follow-up |
| Scrolling menu | `VScrollingMenu` scroll / `scroll_speed` 10000 / `num_items_to_show` [C 0x82316E58, 0x82317348, 0x82317640] | **Reproduced.** `Element::scroll/scrollSpeed`, `scrollMenu` | none |
| Help bar (pills) | `helper_pills` / `helper_pill` descs (GD10-062) | **Reproduced.** `helperPills`/`helpBarLayout` from the descs | none |
| 2D particle FX (highway) | `Create2DParticleSystem` [C 0x8210EEB8] / `Destroy2DParticleSystem` [C 0x8211AA28] | **Reproduced.** `Fx2D` (`fx2d.{h,cpp}`); 113 particles in the beatit run | none |
| Enhanced menu open | port-added; opens next frame, no transition (`spec-enhanced-menu.md`) | **Reproduced (port-added).** `PortScreens` | none |

### Gameplay widgets (HUD)

The port's `HudWidgets` implements `AttachHudWidget` [C scripts `guitar_hud.qb`,
`hud_widgets.qb`] and drives every widget each frame from the judge's values.
**11 output types handled** (0 UNHANDLED in the full-song run):

| Widget / output type | Original evidence | Port status | Action |
|---|---|---|---|
| Score nixie | `HUD2D_score_nixie_%d` [C Function_821570E8] | **Reproduced** (`descpropertyfloat` / text) | none |
| Note-streak bulbs | `HUD2D_Note_Streak_Text` [C Function_821136C0]; GH3 lights [C 0x82117204], colours [C 0x821172D8] | **Reproduced** (`senotestreaklights`) | none |
| Multiplier nixie | x1–x4, SP x2/x4/x6/x8 [I gh4 widget] | **Reproduced** (`semultipliernixie`) | none |
| Star-power tubes | `HUD2D_rock_tube_%d...tube/full` [C Function_821570E8]; GH3 tubes [C 0x821164D0] | **Reproduced** (`setubes`, `_b` blue in SP) | none |
| Rock meter (health) | needle pos [I], red lights/backgrounds, red blink | **Reproduced** (`descpropertypair`/`individualhealthneedlepos`, `screenelementconditionalfloat`, `screenelementplayerhealthblink`) | none |
| Projected song stars | `projected_song_stars` star meter [I] | **Reproduced** (`sestarmeter`) | none |
| Missed-note alpha | `has_missed_note` [I] | **Reproduced** (`screenelementalphatoggle`, `screenelementrgbaswitch`) | none |
| Vocalist streak text | `divide_by_10` int-to-string [I] | **Reproduced** (`seinttostring`) | none |
| Band / face-off (2+ players) | `band1_*`/`band2_*`/`faceoff_ratio` [I: C's band values not decoded] | **Reproduced from the port's own band values**; exact C sourcing uncertain (`hud_widgets.cpp:181`) | follow-up |

### Menus (each screen)

QScript-driven; the port runs the game's own `create_*_menu` scripts. The
`--uitest` walk started **47 flow states**:

| Menu group | Screens reached | Port status |
|---|---|---|
| Boot | `boot_movie_atvi/red_octane/neversoft/intro`, `boot_iis`, `band_name_logo`, `boot_guitar`, `optimal_drum` | built / movies played |
| Main flow | `main_menu_fs`, `game_mode`, `select_instrument`, `setlist_prompt`, `double_bass_popup`, `setlist`, `select_difficulty`, `pausemenu` | built, 912 ui-dump elements, 0 UNHANDLED |
| Hubs / shop | `create_character_hub_menu`, `create_store_menu`, `create_newspaper_menu`, `create_select_venue_menu`, `create_enter_band_name_menu`, practice section/part, `popup_warning` | built |
| Online (stubbed) | `online_character_hub/select/play_song/select_guitar/outfit/style`, `leaderboard_filter`, `net_achievement_tracker`, `view_leaderboard` (11) | **built no element** — expected (Step 7.8 disables online) |
| Port-added | `port_gamepads` (GAMEPADS), `port_enhanced` (ENHANCED) | native pages, reached from GH3 help bars |

### HUD elements

| HUD element | Original evidence | Port status | Action |
|---|---|---|---|
| HUD root / layout | `setup_band_hud` picks `uidesc_hud_1g/1g1v/2g/2g1v/3g1v/band_battle/boss_battle/gamertag` (+`_nxgui`) via `update_hud_layout` | **Reproduced.** `hud_window` container present in the walk's ui-dump; the game script selects the desc, port builds it via `descinterface` | confirm nxgui pick (follow-up) |
| HUD messages | `GetHUDMessageParent`, `hud_messages.qb`, `uidesc_hud_message_fire` | **Reproduced** (parent lookup); flaming overlay not drawn (below) | follow-up |
| Loading screen / spinner | [C 0x8218DDE8 / 0x8218E0A0] | **Reproduced** | none |
| Score / streak / multiplier / SP / health / stars | see Widgets table | **Reproduced** | none |

### Mismatches fixed

**None.** No clear, low-risk, objectively-verifiable mismatch was found: across
47 menu screens (`--uitest`) and a full-song gameplay run (`--song beatit
--autoplay`) the UI engine produced **0 `UNHANDLED` UI states, events, members
or HUD widget output types** and **0 missing fonts / venue textures**. Font
sizes, menu positions and HUD layouts are consumed from the original's own
`descinterface`/QScript data, so they match by construction. The remaining gaps
are genuinely larger than "low-risk one-liners" and are documented below rather
than patched blindly (per the controls-audit precedent and the fragile-layer
rule).

### Follow-ups (documented, not changed)

1. **Flaming HUD-message overlay (GD10-068).** The HUD "fire" messages
   (`fire_2D_Notestreak_Small/Large`, `fire_2D_Starpower`; material
   `sys_fontgrid_text_a6_fire_sys_fontgrid_text_a6_fire`) are **not drawn** —
   the animated fire sprite material is on a pak the port does not load, so the
   port shows nothing (was a white rectangle) [C `uidesc_hud_message_fire.qs`,
   `hud_messages.qs`]. The `fontgrid_text_a6_fire` **font is loaded**
   (`ui_shared.pak.xen`), so the flaming glyphs' shapes are available; only the
   animated fire material overlay is missing. 25 `GD07_HUDLOG` "material … has
   no texture" lines in the walk. Fix = load the fire material's texture + wire
   the material animation (shared with gd10).
2. **Menu anim-out.** Confirm whether task states play an explicit exit
   animation before leaving (`menu_anim_out_done` is a no-op notification in
   `ui_states.cpp:160`); if so, add the anim-out spawn mirroring anim-in.
3. **nxgui HUD desc.** Confirm which desc `update_hud_layout` selects on World
   Tour (`uidesc_hud_1g` vs `uidesc_hud_1g_nxgui`) and that the port builds the
   nxgui variant identically.
4. **Band (2-player) HUD values.** `hud_widgets.cpp:181` sources band / face-off
   values from the port's own band aggregation; the original C's `band*_` value
   derivation was not decoded — confirm against the C before multiplayer HUD.

### Uncertain / not fully traced (Step 3)

- The exact easing address for `motion` was inherited from gd10 ([C 0x822A6B28]);
  the formulas are standard and match, but the Xbox decomp of the easing math
  was not re-derived on the WT binary this pass.
- Per-widget `value_range → range` output rules are marked [I] in
  `hud_widgets.cpp` (written from the desc parameters, the C's rules not
  decoded); they produce correct-looking output and 0 UNHANDLED, but were not
  bit-verified against the native widget code.


## Onboarding Step 4 — animations & physics (2026-11-24)

Aggregate-first pass over the port's character/venue/menu **animations** and its
**physics** (collision, rigid bodies, ragdoll, anim↔physics). World Tour runs
the **gh4 engine shared with gd10 (Metallica)**, so this reuses the gd10 gh4
animation/physics audits rather than redoing them. The port already reimplements
the gh4 animation tree and the Havok ragdoll faithfully; this pass **confirmed
that in code + headless dev-hook runs** and **closed the two gd10-proven gaps
that had not been carried over to World Tour**. Evidence: the port source read in
this worktree, `extracted/names.tsv` (QB keys), and fresh headless runs of
`--chartest` / `--drumtest` on our disc (`gd-game-07/extracted`).

### (a) Character animation inventory (every class, trigger, port status)

Driver: `render/band_anim.{h,cpp}` (`bandanim::Animator`) — GH3's anim tree
(guitarist/drummer/vocalist `_static_tree`) + the iterators, evaluated as a
function of the song clock (frame-rate-independent, per-member seed).

| Animation class | Trigger | Port status | Evidence |
|---|---|---|---|
| Body idle / adjusting random anims | song clock, per-member RNG (`hero_play_adjusting_random_anims`) | ✅ plays | `--chartest`: 26/26 bodies animated, `stats.bodyAnims>0` each |
| Strum (short/med/long) | guitar note (`Strum_iterator`) | ✅ | `Ev::Strum`→`strum()`; chartest |
| Fret position / fingers | `FretPos_iterator` / `FretFingers_iterator` | ✅ | `Ev::Fret`/`Ev::Chord`, IK onto guitar bones |
| Chord / chord-release | note on/off | ✅ | `Ev::Chord`/`Ev::ChordRelease` |
| GH3 drummer arms + cymbal hits | `Drum_iterator` / `Drum_cymbal_iterator` (`!gh4` only) | ✅ | `Ev::DrumArm`/`Ev::Cymbal`, `cymbal_anims` |
| **gh4 DrumKit piece wobble** (cymbals/toms/snare per hit) | `Drumkit_anim_iterator` (`drums_notes 73-85→drumkit_anims[i]`) | ✅ **FIXED this pass** (was absent for gh4) | `--chartest`: **320 DrumKit difference-anims fired** (was 0) |
| Mocap / choreographed (`Band_PlayClip`) | `<song>_performance` array | ✅ | `Ev::Mocap`, `mocaps_`, start-node teleport |
| Facial / lip-sync | `facial_anim_loop`, `Band_PlayFacialAnim` | ✅ | `--chartest`: **26/26 gh4 talents move their face** |
| Stance change / play_anim | `handle_change_stance` / `handle_play_anim` (menus + perf) | ✅ | `changeStance`/`playAnim` |
| Star Power (special anim) | `handle_star_power` `play_special_anim Stance_A Starpower` | ✅ (non-gh4 playables verified; gh4 talents' SP clip not in their song anim sets [I], not checked — same as gd10) | `playSpecial`; chartest SP pass |
| Win / lose anims | `play_win_anims` / `play_lose_anims` (blend 0) | ✅ | `playSpecial(...,blend)` |
| Missed-note (Bad_Strums) | `handle_missed_note` until next hit | ✅ | `playerNote(ms,hit)` |
| Drummer auto-twist | `drummer_autotwist` per frame | ✅ | `Ev::Twist` |
| ped_animevents | clip `animevents` fire scripts | ✅ | `clipEvents()` |
| Accessory bones (hair/coat/chains) | Havok ragdoll over the pose | ✅ (see physics) | `AccessorySim` |

**Result: every character animation class the gh4 engine drives is reproduced.**
The one World-Tour gap vs. the shared gh4 engine was the **DrumKit
difference-animation** (below), now closed. Crowd-interaction anims are venue/ped
driven (next).

### (b) Venue animations — reproduced

Confirmed in code + the venue dev hooks; no gap found (venue **lighting/cameras**
are Step 5's lane and excluded here):
- **Stage element / prop mocap + transitions:** `render/venue_stage.cpp`
  (`introAnims`, `mocap_.addTransition`, `setFollow`, `starPowerAnim`, the
  particle `setAnimHook`), driven by the venue scene's clips.
- **Background peds / crowd actors:** `render/stage_peds.cpp` (`StagePeds::play`
  evaluates each ped's `bandanim` body over the song clock) and
  `render/crowd.cpp` (crowd cards — matched to the original per the crowd rule).
- Exercised headless by `--venuetest` and by `--chartest`'s per-venue stage pass
  (the band renders and animates in every venue stage).

### (c) Physics — collision, rigid bodies, ragdoll (the main fix)

Decoder: `data/havok.{h,cpp}` (Havok 4.6 reflected packfiles + a fixed-offset
**`Pack55`** reader for the gh4 **Havok 5.5.0-r1** `.rag`). Solver:
`render/band_ragdoll.{h,cpp}` (`AccessorySim`) — the band's accessory bones as
rigid bodies with ragdoll/hinge/prismatic constraints, Gauss-Seidel in 4
sub-steps at a fixed 60 Hz, plus capsule-capsule contact against the keyframed
body capsules so hair/chains/coat tails stay outside the character.

**Gap found and fixed (the gd10 hair-ragdoll-collision fix, not yet carried to
World Tour):** gd07's `Pack55` read the body's shape class name but hardcoded
`shape.kind = Shape::Other` — so **every World Tour `.rag` collision shape
decoded as "Other" and the ragdoll's capsule-collision loop (which requires
`Shape::Capsule`) never ran**: hair, chains and coat tails could clip the body
(the gd10 bug class). Ported gd10's `Pack55::readShape` (decode
`hkpConvexTranslateShape`→`hkpCapsuleShape`/`hkpBoxShape`; a thin accessory box
becomes a capsule along its longest axis) and bumped the ragdoll contact
relaxation 2→4 passes to match gd10's deep-overlap handling [GD10-044].

**Objective evidence (`--chartest`, data-derived, no eyeballing):**
- **Collision shapes now decode:** 22 band bodies carry **207 accessory + 607
  body `.rag` collision capsules** (expected >0; **was 0** before the fix — the
  collision path was provably dormant).
- **Anim + physics together, no clipping/explosion:** worst residual
  accessory-into-body-capsule overlap over the settled seconds = **0.0002 m**
  (0.2 mm) across the whole band (assert <0.10 m passes); the per-ms accessory
  stutter check (worst 1 ms move <12× mean) still passes — the hair follows the
  animation smoothly and stays off the body. The data-free ragdoll self-test (a
  hair segment keeps its cone + joint for 3 s) still passes.
- `.hkc` zone collision (7,254 rigid bodies) and `.rag` (196 files) all parse
  (Step 2 `--formattest`); this pass makes the `.rag` **shapes** usable, not just
  parseable.

### (d) DrumKit difference-animation (the gd10 gh4-cymbal fix, carried over)

World Tour's data has `drumkit_anims` (qb 0x88439652), `drum_kit_blend_time`
(0xdc170e85) and `applydrumkitdifference_playanim` (0x214d7b3b) — confirmed in
`names.tsv` — so the shared gh4 `Drumkit_anim_iterator` applies to World Tour.
gd07 only ran the **`!gh4`** song cymbal iterator, so a **gh4 (World Tour)
drummer's kit pieces did not wobble per hit** (arms are mocap, pieces were
static). Ported gd10's DrumKit path surgically: `drumKitIndex(note)`
(`drums_notes 73-85 → drumkit_anims[0..13]`), `Ev::DrumKit`, `drumKit_[14]` delta
nodes composed onto the drummer pose in `evaluate`, `drumKitBlend_` from
`drum_kit_blend_time`. Note 85 (Cymbal_4, idx 13) stays gated on
`is_current_band_metallica`, which is always false on World Tour (no Metallica
band), matching the shared iterator. **Evidence:** `--chartest` reports **320
DrumKit difference-anims fired** (cymbals/toms/snare wobble per hit) on the gh4
drummers over the test song — **was 0**. `--drumtest` (the drum *judge*,
unrelated) still passes 20/20.

### (e) Menu / widget animations

Confirmed complete as an animation *system* (Step 3 already captured the UI):
screen-element **morph** (4 ease motions), **anim-in** spawn
(`ui_task_menu_default_anim_in_script`), scrolling menus, help pills, 2D
particles — all data-driven and faithful (`ui/ui_system.cpp`, `ui/ui_states.cpp`;
`--uitest` 0 UNHANDLED). On Step 3's **anim-out** follow-up: `ui_states.cpp`
treats `menu_anim_out_done` as a no-op notification and spawns no explicit exit
script. This is a **UI-flow** question (does the original's flow manager spawn an
`anim_out` script before leaving a task state?), **not an animation-system
capability gap** — the system can already spawn an anim-out script by the same
path as anim-in once the flow is confirmed. Left to the UI lane (Step 3/5) to
avoid touching the pinned `--uitest` flow without the decoded exit sequence.

### (f) What I changed

- `data/havok.cpp`: `Pack55::readShape` (capsule/box/convex-translate decode);
  `#include <algorithm>`. (The dormant `Shape::Other` hardcode removed.)
- `render/band_ragdoll.{h,cpp}`: ragdoll contact 2→4 passes; new public
  `AccessorySim::metrics()` (decoded-capsule counts + worst residual penetration)
  for objective chartest evidence.
- `render/band_anim.{h,cpp}`: the gh4 **DrumKit** difference-animation
  (`drumKitIndex`, `Ev::DrumKit`, `drumKit_[14]`, `drumKitBlend_`, iterator +
  handler + `evaluate` compose).
- `tests/chartest.cpp`: measure and print the ragdoll collision-capsule counts /
  worst hair-body overlap and the DrumKit anim-event count (assert overlap
  <0.10 m). Test-only.
- No change to the gameplay **judgment** layer (`game/rules*`, `player.cpp`
  timing) or the input layer.

### (g) What remains (honest)

- **Pre-existing, NOT Step-4, NOT a regression:** `--chartest` still reports the
  **same 4 venue camera-framing FAILs** (z_Goth, z_Recordstore, z_Harbor,
  z_Tool — a framed band member leaves a song-camera cut), identical to the
  Step-2 baseline (this file, Step-2 §(d)). These are **camera framing**
  (Step 5's lane); my ragdoll/DrumKit changes move accessory/kit bones, never a
  member's root/chest, so framing is unaffected.
- **Menu anim-out** (above) — UI lane, pending the decoded flow-exit sequence.
- **gh4 talents' Star Power clip** not verified in their song anim sets [I]
  (same open item as gd10).
- A handful of `.rag` accessories tie to a non-parent bone (`Bone_ACC_*` → an
  ankle/thigh) and are **animated, not simulated** (logged by `AccessorySim`);
  this is the original's own skeleton wiring, unchanged.

### Uncertain / not fully traced (Step 4)

- The World-Tour `Drumkit_anim_iterator`'s exact **note-85 gate** was taken from
  the shared gh4 script via gd10 (`is_current_band_metallica`, always false on
  WT); WT's `guitar_hand_events.qb` was not re-decoded bit-for-bit this pass, and
  whether any WT chart even uses note 85 was not dumped. Worst case a 4th-cymbal
  hit would not wobble — low impact.
- The `Pack55` shape offsets are `[I]` (inferred from the gh4 bands' `.rag`
  values, shared with gd10); every World Tour `.rag` parses and the decoded
  capsules give a 0.2 mm worst overlap, which corroborates them, but they were
  not reflected from a `__types__` section (the 5.5 `.rag` has none).

## Onboarding Step 5 — lights & effects (2026-11-24)

Aggregate-first pass on the gh4 light/camera/effects engine (lane: lighting,
camera effects/zooms, transparency, bloom, depth of field, full-scenario
composition — **not** animation/physics, which is Step 4's). The gh4 lighting
and camera-cut engine was audited in depth for gd04 (GH3) and gd10 (Metallica,
the same gh4 engine) — **those audits are reused, not redone** (see
`gd-game-04/docs/FINDINGS.md` "Venue light show audit" and
`gd-game-10/docs/FINDINGS.md` "Venue lighting"). This section confirms the port
reproduces them on World Tour's own data and resolves the Step 2 venue
camera-framing FAILs. Evidence is fresh headless runs on our disc
(`gd-game-07/extracted`), the decoded QB (`tools/qb.py`), and the port source.

### (a) Lighting — faithful (aggregated; verified on WT)

- **Mood/light-show pipeline is the gd10 port.** `render/venue_lights.cpp`
  `LightShow::enterMood` / `applyNote` / `cycle`: a mood entry with no inline
  `processor` defaults to **`default_generic`** (the gd10 dead-moods fix — WT
  venues name no processor for most moods, e.g. z_bayou intro/medium, so
  without it the stages stayed at their dark base lights). `Strobe_Generic`,
  `Blackout_Generic`, `Flare_Generic`, `Poor_Generic` handled; generic moods
  cycle on enter and on the `snapshotchange` notes (57/58); colour overrides
  (61–69, `lightshow_coloroverrides`), `SetTime` blend (39–53), pyro (56).
  `GD07_LIGHTSHOW_DEFAULT_LEGACY=1` restores the no-processor behaviour.
- **Lights + housings.** `light_setparams` r/g/b/intensity/specularintensity/
  shadowintensity [C 0x821B4950]; light housings blended (pos/Quat/radii/range/
  `volumedensity`/`volumecolor*`/`projectorcolor*`) for volumes and projectors.
  State flags `LS_ALWAYS` / `LS_3_5_PRE` / `LS_ENCORE_PRE` /
  **`LS_SPOTLIGHT_GUITARIST`** are set (`LightShow_InitEventMappings`), so the
  per-member follow-spot snapshots select (consistent with gd10's follow-spot).
- **Characters are not transparent or too dark.** The band is lit by the
  venue's light groups (group ambient + per-light N·L with the venue's fall-off
  and cones) plus a guaranteed small fill (`GD07_BAND_FILL`, default 0.25,
  `render/band_view.cpp`), so a member is never pitch-black; the dynamic shadow
  keeps one light and draws the band into **both** GH3 cascades (venuetest:
  `bothCascades == shadowFrames`, no "dynamic shadow drew N of M frames").
- **Objective:** `--venuetest` (song aggro), 17/18 venues pass with
  **0 missing textures each**, the light show running (e.g. z_Goth 483, z_Harbor
  420 snapshot changes), and **lights identical at 60 vs 240 fps** (788
  checkpoints). The one failure (z_Tool) is a pre-existing *camera*
  FR-divergence, not a light one — see (c)/remains.

### (b) Camera — cut director, effects, zooms, tracking (faithful)

`render/venue_camera.cpp` `CameraDirector` reproduces the gh4 cut director:
performance-keyed camera arrays (`<zone>_cameras_<perf>` → … → `default_cameras`
via `findArray`), the camera notes, the control types Static / TwoCam /
SpecialStepMove / HandCam / HandCamZoom2 / QuickZoom / Pan / Orbit /
OrbitAndMorphTwoCam / Spline / control-script, hand-cam sway
(`CameraCuts_SetHandCamParams`), FOV zooms, the Star Power camera
(`do_starpower_camera`), intro transitions, per-shot DOF manager and
`ApplyScreenFx`, deferred white flash, and member tracking
(`lookat`/`lookatbone`/`screenoffset`/`distfromlookat` → `aim`). Cuts and poses
are frame-rate independent (chartest/venuetest 60 vs 240 identical) except the
z_Tool case in (c)/remains.

### (c) The 4 Step-2 venue camera-framing FAILs — root cause + resolution

`--chartest` (song aggro) reported 4 venues where "the framed band member leaves
a song-camera cut": z_Goth, z_Recordstore, z_Harbor, z_Tool. Root-caused with
measured data (`GD07_CAMLOG=2` per-cut aim geometry; `GD07_VENUETEST_BAND` band
positions vs the waypoint box). **None is a camera-engine defect; none is a
large root-motion error.**

- **Root motion ruled out (measured).** `--venuetest GD07_VENUETEST_BAND=1`: every
  member stays within ≤1.12 m of the venue's waypoint box and **0 samples get
  past its front** (farthest 0.88 m). The band stands at its waypoints; the same
  band positions pass in 14 other venues with the same song cuts, so the
  failures are driven by the *failing venues' camera data*, not placement.
- **z_Harbor, z_Recordstore (`Guitarist_CloseUp02`): authored edge-crop; camera
  faithful.** Measured so (screenoffset) = **(0.63, 1.02)** — a tight fov-22
  close-up that deliberately places the chest 2 % past the bottom frame edge;
  `aim` lands the bone exactly at that authored offset (authored pre-aim offset
  e.g. (0.48,1.01) or (−5.00,1.22) → reframed to so). The chartest asserted a
  strict ±1.0 NDC frustum, so it flagged the intended crop.
- **z_Tool (`AUDIENCE_R`): non-tracking wide shot; camera faithful.** z_Tool has
  no own audience array, so the audience-prefix cut falls back to the global
  `default_cameras_audience` (`AUDIENCE_R`), which carries a `lookat` hint but
  **no `lookatbone`** — `aim` does not reframe a member (correct), and the wide
  crowd-POV shot does not place the vocalist's chest in the 16:9 frustum. All 20
  of z_Tool's "framed" checkpoints were this one shot.
- **z_Goth (1/237, 72500 ms): root-motion transition transient.** A single
  checkpoint at a band waypoint transition (the band shifts `vocalist_start` →
  `own`, confirmed by `GD07_VENUETEST_BAND=2`) where the tracked bone is briefly
  ~89° off the shot's authored direction and `aim` bails (keeps the authored
  composition). The member leaves on its own root motion. **Flagged to Step 4**
  (whether the original's director avoids a member close-up during that member's
  node transit is node-aware-selection / root-motion territory).
- **Fix (verification layer, camera lane, non-behavioral).** The objective check
  was over-strict and mis-scoped, not the camera. `CameraDirector::aim` now
  records its framing decision (member / bone / interpolated authored
  screenoffset / target) and whether the aim is *engaged* (lookat + lookatbone,
  bone resolved, within the reframable cone); new `framedNow()`
  (`render/venue_camera.{h,cpp}`). `tests/chartest.cpp` asserts framing **only**
  for a shot that actually tracks a member's bone, with the on-screen bound set
  to the shot's **authored screenoffset** (`max(1,|so|)+0.06`), so a correctly
  framed close-up crop passes but a real tracking miss (bone far from its
  authored offset while the aim is engaged) still fails. The rendered camera
  pose is **byte-identical** (`framing_` is written, never read in the pose
  path), so the pinned render/venue tests are unaffected. Result: **chartest
  18/18 venues, 0 off screen, 0 FAILs** ("the band in 18 venues frame-rate
  independent and on screen"); framed coverage stable (z_Tool 20→0 non-tracking,
  z_Metalfest 225→169 drops its audience shots, z_Goth 237→236 drops the
  transit frame, the other 15 unchanged).

### (d) Bloom + depth of field — implemented and applied

`render/screen_fx.cpp`: the DOF/BnS/sbloom instance chain in the scripts' order
(`823DE658`), each reading the previous output. **DoF** (`824440F8`): colour
blurred at a quarter (and a sixteenth for `resolution≥2`) and lerped by the
linear depth, with the `m_vInvProjMatT` / `m_vDepthOfFieldParams` registers
[C]; per-shot via the cut's `dof` (`CameraCuts_StartCallback`, else
`DOF_Off_TOD_Manager`). **Bloom**: `sbloom`, the uber passes
`_bloom1_noise1_color1[…]` with/without `dof1`, the TOD profiles, and
`_Bloom_SubtractModulate`. Applied every drawn frame (venuetest draws
235..391/frame, 0 empty) and composed in the real song (e).

### (e) Full-scenario (objective)

- **venuetest** (aggro, all 18 venues, drawn at 60 and 240 fps): **0 missing
  textures per venue**, no empty frame (`drawsMin>0`), both shadow cascades, the
  light show cycling; 17/18 fully pass. Screenshot luminance (lum.py): the 17
  render dim-but-visible (meanLum 7–130).
- **autoplay** `--song beatit --difficulty 3 --autoplay` (venue z_Metalfest, the
  full in-game chain: highway + venue lights + camera cuts + screen FX + HUD):
  venue **107 textures 0 missing, 35 lights, 410/410 GH3-shader meshes, 0
  approximated**; 4 frames at 12/30/60/90 s have **meanLum 29–50, 0 near-black
  screens, ≤0.3 % near-white** — properly lit, not black, not overbright.

### Honest remains / flagged

- **z_Tool (pre-existing, flagged):** `--venuetest` fails "camera rotation
  differs 60 vs 240 fps at 61250 ms" — a frame-rate-dependent camera path in
  z_Tool's `CameraCuts_BlockControl` (Blocked) / mocap-camera sequence, **not**
  introduced here (the Step-5 change writes only the non-behavioral `framing_`
  state; the pose math is untouched). z_Tool also renders near-black mid-song
  (meanLum 1.9–5.6 across several cuts) — the minimal 8-light venue's light show
  is not lighting the stage. Both belong to the venue-artifacts / camera-cut
  determinism backlog (the owner's "find root causes later"), not to the
  framing-FAIL fix.
- **z_Goth 72500 ms** transition transient → Step 4 (root-motion node transit).
- **screen fx: 2 sys textures missing** (autoplay log) — a small screen-FX asset
  gap (same family as the GD10-068 fire-material follow-up); DoF/bloom still
  compose.
- **Uncertain / not re-derived here:** the `aim` member-tracking law
  (turn-to-screenoffset, `distfromlookat` ray-follow) is [I] inferred (the
  shot's authored composition is [C]); the gh4 SH/light-selection core is VMX128
  and does not decompile (reconstruction followed, per gd10); overbright has no
  hard engine gate (checked only by screenshot luminance).

## Onboarding Step 6 — gameplay (2026-11-24)

Aggregate-first confirmation of the onboarding spec's Step 6 (gate 50% — "confirm
gameplay is working") for World Tour's four instruments and same-screen
multiplayer, with the one safe, objectively-testable increment taken (real-disc
drum autoplay evidence). The gameplay judgment layer (`game/rules*.h`,
`game/player.cpp` timing) was **not touched** — it is pinned by the autoplay
tests, which stay green (charts failing 0). Ghidra evidence is from the XEX
symbol/string export `ghidra/analysis/strings.tsv` (the shipped import/symbol
names) + the decoded QB; the XEX still cannot be opened live by REA.

### Guitar + Bass (Rhythm) — confirmed faithful, playable, 0 failing

- **Mechanics (port):** the five-fret judge (`game/player.cpp` + `game/rules*.h`)
  reproduces the gh4 numbers already pinned by the Controls audit above — hit
  window, strum refractory (guitar-mode, in the judge), HOPO ticks/window,
  overstrum, sustains + whammy, Star Power phrases/deploy/tilt. Bass is the
  `Rhythm` / `RhythmCoop` part through the **same** judge (there is no separate
  bass judge in the original either). The WT touch-strip `_Tapping` tap/slide
  path is parsed + judged as a separate model (Controls Phase B above).
- **Evidence (`--selftest <disc>`, headless):** `autoplay: score (strummed)
  205,469,430, HOPO landings 51,999, phrases captured 13,963, charts failing 0`
  over every song × part (Guitar/Rhythm/GuitarCoop/RhythmCoop) × 4 difficulties,
  plus calibration 792/0 failed, easy-chords 44/0, overstrum-cut, face-off
  269,686 notes split. The touch strip: 424 `_Tapping` spans, tap judge 0 failed.
  **Verdict: guitar + bass gameplay matches the original — met.**

### Drums (gh4, per-pad velocity) — judge proven on real data; live-wiring remains

- **Mechanics (original):** drums are a first-class controller — `IsDrumController`
  / `IsRBDrum` (descriptor type 10 GH kit / 11 RB kit), and `ProcessDrumVelocity`
  → `GetDrumHitVelocity` read the per-pad velocity bytes (six, max taken for a
  type-10 kit). The playable stream comes from `%s_drums_notes` (kit-piece notes
  73..85). [All four symbols confirmed in `strings.tsv`.]
- **Port status:** the input layer **detects** a drum kit and **guards** the
  guitar part from it (`Input::isDrumKit`, `drumKitSuppressesPlay`,
  `Pad::drum`), so a kit no longer scramble-plays guitar (Controls Phase B
  above). The judge `game/drum_player.{h,cpp}` (`DrumPlayer`) builds a playable
  5-lane gem stream (kick/red/yellow/blue/green) from `Chart::drums` with the
  recorded per-lane velocity, and scores with the five-fret streak multiplier.
- **What I wired this step (objective, verifiable, zero touch to the pinned
  judge / fragile input constants):** real-disc **drum autoplay** in the
  self-test harness — `drumOnce()` in `engine/src/tests/selftest_play.cpp` builds
  a `DrumPlayer` from every song's *real* `<song>_drums_notes` and autoplays it
  (strike each gem's lanes at its time, then sweep), asserting every lane-note is
  hit with no miss/overhit. New `PlayTotals` drum counters are printed by
  `--selftest`. Previously only a *synthetic* track (`--drumtest`) exercised the
  judge; this proves it on the shipped data.
- **Evidence (`--selftest`):** `drums (gh4): 89 songs' real _drums_notes
  autoplayed, 100,885 gems / 151,156 lane-notes built, 151,156 hit, 0 songs
  failed` — 100% of real drum lane-notes judged correctly. `--drumtest`: all
  checks pass (lane map, velocity capture, chord merge, streak, live-velocity
  override, input guard).
- **Honest remains (the fragile live path, not taken here):**
  1. **Live input routing.** Map an SDL drum kit's pad/cymbal/kick buttons to the
     five `DrumPlayer` lanes and step `DrumPlayer` beside `Player` in
     `SongScreen::tick` for a drum seat. This touches the Phase-A-owned SDL→action
     layer; per the owner's objective-checks rule a pad→lane mapping must be set
     from **real-device logs**, not invented constants, so it is deferred to the
     controls wave rather than guessed.
  2. **Real per-pad velocity.** SDL's gamepad API exposes only buttons on PC, not
     gh4's six velocity bytes (that needs raw HID / the USB descriptor). Live
     strikes therefore fall back to the chart's recorded velocity today.
  3. **HUD drum lanes.** No drum-lane highway renderer yet (the guitar HUD is
     5-fret). Not added blind — it cannot be verified headlessly, and the owner's
     rule forbids eyeballed visual changes.
  4. `_drums_notes` is the drummer's single **performance/animation** track, not
     gh4's per-difficulty gameplay drum chart (no difficulty reduction) — the
     same honest scope noted for gd10.

### Vocals — AI-only in the port; a mic path is a dedicated later wave

- **Mechanics (original):** gh4 has a full mic/pitch subsystem — confirmed
  symbols: `Vocals_ControllerHasUsableHeadset`, `Vocals_GetAvailableMics`,
  `Vocals_GetMicType`, `Vocals_SetMicGain`, `Vocals_MuteOutput`,
  `VocalDSPSetParams`, `VocalsHighway_*`, and the per-song data `%s_vocals_phrases`,
  `%s_vocals_freeform`, `%s_vocals_note_range`, `%s_lyrics`, `%s_song_vocals`
  (all in `strings.tsv`). Scoring is phrase-based against a pitch track with a
  freeform (talky) mode; `BandManager_AddVocalist` seats a vocalist.
- **Port status:** vocals are **AI-only** — there is no `VocalPlayer`, no vocals
  parse (`Chart` has no vocal phrase/lyric/note-range tracks), and no mic
  instrument in the input engine (the universal `Instrument` set is
  Guitar/Gamepad/Midi/Drums). The band's vocalist animates, but the human cannot
  sing.
- **Verdict (honest):** a minimal playable mic path is **NOT feasible as a Step-6
  increment and should not be faked.** It needs, as net-new first-class
  subsystems: (a) a vocals data parse (`_vocals_phrases` / `_vocals_note_range` /
  `_lyrics` / `_vocals_freeform`), (b) a mic instrument owned by the shared input
  engine with a real pitch detector (the original runs DSP at a fixed rate), (c)
  a vocals highway renderer, (d) a phrase/pitch judge. This is a dedicated wave
  (it matches the controls plan's Phase B2 "mic instrument owned by the shared
  engine"), not a bolt-on. Deferred, documented, not stubbed.

### Multiplayer — same-screen 2-player confirmed; mixed-instrument band is the gap

- **Original:** `CreatePlayerServer` + `BandManager_Add{Guitarist,Bassist,
  Drummer,Vocalist,Rhythmist}` seat a band; co-op / face-off / pro-face-off /
  battle / boss are the GH3-family modes (`use_coop_notetracks`).
- **Port status:** `game/match.h`/`match.cpp` already instantiate **2 local
  players same-screen** (`sides_[2]`, `n_`, `GameMode::{Coop,FaceOff,ProFaceOff,
  Battle,Boss}`), with the 2-player HUD (`Hud2P`), two highways and the shared /
  face-off / battle rules. The app flow already drives `twoPlayers_` in
  `SongScreen`.
- **Evidence (`--selftest`, real charts through `Match`):** `two players: co-op
  400 charts (471,229 notes, Star Power raised together 2,436 times), battles 132
  (224 attacks, 89 KOs)` — both autoplayed players hit every note; counters rise
  together. **Verdict: same-screen MP works for guitar + bass/rhythm (co-op,
  face-off, battle, boss).** The gap is a **mixed-instrument band** (a drum seat
  and a vocal seat as distinct players), which is blocked by the drum live-wiring
  (above) and the vocal instrument (above), not by the `Match` 2-player core.

### Step 6 status

**Met for the core** — guitar + bass gameplay is faithful and 0-failing, the drum
judge is now proven on 100% of real disc drum data, and same-screen 2-player MP
is confirmed. **Partial** on the two large, honestly-deferred pieces: live drum
controller + HUD wiring (fragile input layer + an unverifiable renderer) and a
playable vocal mic path (a net-new subsystem = its own wave).

## Onboarding Step 7.2 — universal gamepad menu + 7.1 review (2026-11-24)

Milestone 7.2 (universal gamepad menu, gate 55%) plus the recurrent 7.1 review
(mouse in every menu; the intermittent "left mode" bug). **Aggregate-first:** the
gd07 input engine already carries the whole universal model (Phase A+B, Steps
2–5), so this pass **verified** 7.2 and its default, **root-caused and fixed the
left-mode bug**, and **added objective mouse-hit coverage** to `--uitest`. The
gameplay judgment layer was not touched.

### (a) Universal gamepad menu — present & faithful (verified, no change)

- **Host:** `engine/src/ui/port_gamepads.cpp` (`GamepadScreen : PortScreen`), the
  GH3-lineage host per `docs/spec-gamepad-menu.md`: four player boxes (P1 blue
  ALWAYS ON, P2 green, P3/P4 shown "NOT IN THIS GAME" — GH has two players), each
  with Instrument + "Bind to:" (every device + Detect…) + Customize inputs;
  bottom `[< Back] [Export] [Import]`; right INPUTS MAPPING with the doc's
  sections (GENERAL, 5 FRET GUITAR), two slots/control, Require Strum, Controller
  Type (pads), Reset, Touch (P1), Save/Discard; the left locks during an edit.
- **Reached from** Enhanced > CONTROLS > **Gamepads** (`port_enhanced.cpp:135`
  Button `gamepads` → `PortScreens::Id::Gamepads`), the help bar's **GAMEPADS**
  pill (clickable), and the main-menu **yellow fret / key 3** shortcut
  (`app.cpp` `menuAt`/`Fret2`). Instruments offered: 5 Fret Guitar, Gamepad,
  Generic MIDI (+ Drums detected-not-played, the GH/RB drum guard).
- **Objective proof** — `--walktest gamepads` **PASSES**: opens via key 3,
  Customize inputs → Whammy Key 1 **rebound to E by the mouse** → Save →
  `controllers.json` has it ("gamepads: Whammy on E, controllers.json has it"),
  re-opened via the port help-bar item (mouse). Every field/dropdown/row is
  mouse-clickable (`GamepadScreen::mouse`, the whole screen).

### (b) Default controller — keyboard as a strum-required guitar (verified)

The default device is the **keyboard**, which plays the 5-fret guitar part as a
**guitar requiring strum** (the task's "keyboard mapped as an Xbox 360 guitar,
strum required"):

- `Input::defaults(Profile::Keyboard)` (input.cpp:251): frets `1..5`/`F1..F5`,
  **strum on Up/Down arrows** (a strum bar, not the frets), Star Power on Tab,
  Start on Enter.
- `requireStrum("keyboard")` defaults **true** (input.cpp:1243) → the keyboard's
  `ControllerMode` is **Guitar** (input.cpp:936) and `gh2Type("keyboard")` is GH2's
  **`guitar`** (the strum-bar watcher), not `joypad_guitar` (input.cpp:810).
- The keyboard owns player slot 0 whenever no pad holds it (input.cpp
  `keyboardSlot`), and `lastMode_` starts Guitar — so at boot with no pad the
  active controller is the keyboard-as-strum-guitar.
- **Objective proof** — the input self-test (`--selftest`, `runInputChecks`):
  `1 + F1: green pressed once (Require Strum on: no strum)` (a fret alone does
  **not** strum) and `Up arrow: Up and Strum once` (the arrows strum, in Guitar
  mode). All input checks pass.

### (c) The "left mode" bug — root cause (data) & fix

**Symptom (owner):** left-handed mode "doesn't work consistently for everyone."

**Root cause (grounded):** `Input::setLefty(p1, p2)` is **never called in the
gd07 app** — only in the self-test. The reference GH1/GH2 engine gd01 calls it
every frame (`gd-game-01/engine/src/app.cpp:2566`); the whole **gh4 lineage
(gd04/gd10/gd07) dropped the call** (grep: only `setup.lefty = {...}` at
`app.cpp:1658`, which feeds the **render/HUD** — `song_screen.cpp:269-270`,
`highway2d.cpp` — but never the input layer). So the input `lefty_` flag stays
`{false,false}`.

Lefty is a **visual mirror**: `highway2d.cpp` draws each gem/button at
`lane = 4 - fret` (587, 860, 871, 944, 981) while the **judge matches by fret
index**. The input layer's lefty does two things (input.cpp `actionsOf`):
mirror a **joypad's** fret order `fret = 4 - fret` (1092, GH2's
`lefty_joypad_guitar` [C]) and swap a **guitar's** menu strum (1098-1099). A real
guitar and the keyboard **keep their frets** (you flip the instrument; the lanes
mirror to match) — so lefty "worked" for them. A **gamepad can't be flipped**, so
without the input mirror its green button still hits the green gem now drawn on
the far side: the lanes are mirrored but the buttons are not → **lefty is broken
for gamepad players only**, which is exactly the "inconsistent for everyone."

**Fix** (`app.cpp` `frame()`, minimal, non-judgment): feed the input layer from
the song's lefty while a song plays, off in menus —
`if (front_ && song_ && !front_->songPaused) input_.setLefty(rq.lefty[0],
rq.lefty[1]); else input_.setLefty(false,false);` — mirroring gd01's intent. Off
in menus so a joypad's frets are never mirrored during navigation (the pinned
`--uitest` cannot regress).

**Objective proof** — input self-test, under the same `setLefty(true,true)`:
`lefty: pad A is green (lefty_joypad_guitar)` (a **joypad's** frets mirror) **and
the new** `lefty: a guitar's frets are NOT mirrored (Y still yellow); only a
joypad's are` — the two together are the root cause (device-dependent lefty).
All input checks pass.

### (d) Mouse in every menu — audit + objective per-screen coverage

Mouse is routed through three uniform paths, so it is structurally supported in
**every** menu: data-driven GH3 menus via `UiSystem::menuAt` (one hit-test over
all focused `VMenu/HMenu`, incl. the texture-board menus a prior GD07 pass fixed
via `pointerToViewport`, ui_system.cpp:1817-1830), the port screens via the
pure-virtual `PortScreen::mouse`, and the help bar via `helperAt`/`helperPills`
(`app.cpp::menuMouse`; right-click = Back).

New objective check: `UiSystem::menuHitCoverage(focusable, hittable)` counts the
focusable, visible menu items on the focused menus and how many have mouse-hit
bounds (directly or via their focus handler's named text — exactly menuAt's
hittability). `--uitest` runs it on every started screen and FAILS on a "dead
screen" (a menu with items, none hittable).

- **`--uitest`: 47 flow states, exit 0, "every screen started through the flow
  manager"; mouse coverage = 47 screens with a menu, 1181 focusable items,
  1181 mouse-hittable (0 partial gaps, 0 dead screens).** (11 states build no
  element — the disabled `online_*`/`leaderboard_*` screens, Step 7.8.)
- `--walktest gamepads` **PASSES** (live mouse on the gamepad menu, §a).

### Honest remains / flagged

- **`--walktest mouse` / `mouseall` fail on World Tour's main menu (PRE-EXISTING
  harness, not product):** the main menu is a **3-D board** menu (venue
  z_soundcheck). `mouse` fails at `hover:menu:main_menu_options_text` because the
  `hover:menu:` act uses `drawnBounds` (the item's **texture-space** center) as a
  **screen** point, which `menuAt` then re-maps screen→texture — a double
  transform that misses. `mouseall` fails at `helpend:UP/DOWN` because WT's main
  menu help bar shows **SELECT + GAMEPADS** (its `check:pills` prints them at real
  screen coords, both clickable), not an UP/DOWN pill. Both are walk assumptions
  inherited from the 2-D-menu lineage (walk.cpp last touched at the gd07 stand-up;
  my walk edits run only in the `uitest_` path; my app edit is in-song only — the
  baseline behaves identically). The **live board-menu mouse itself is code-wired**
  (the GD07 `pointerToViewport` pass) and the render target is real (OpenGL on the
  box's GPU). **Follow-up (test harness, not product):** teach `hover/click:menu:`
  to drive board menus by scanning a screen point through `pointerToViewport` into
  the item's texture bounds, and adapt the help-pill expectations to WT's pills —
  this would turn board-menu live mouse into an objective check too.
- **1 pre-existing `--selftest` FAIL, unrelated:** `jamsession` song pak
  (`DATA/COMPRESSED/SONGS/jamsession_song.pak.xen`) is **not on this disc dump** —
  a data-availability gap (no code here touches song loading); all input/judge
  checks pass.
- **7.2 gate (55%): MET for the gamepad menu + default controller + left-mode
  fix + mouse structural coverage.** The remaining 7.1 board-menu live-mouse
  harness check is the honest follow-up.

## Onboarding Step 7.4 — shop (2026-11-24)

Milestone 7.4 (the in-game store, the "Rock Store" / GH Store; gate 68%).
Aggregate-first: the store **screens** are already built and reached (Step 3:
`create_store_menu`, `create_character_hub_menu`, the `z_soundcheck` sound-stage
background and camera morphs), and the port already parses the whole
Create-A-Rocker (CAS) catalog (`data/band.cpp`). What was missing was the
**purchase mechanic** — a currency balance, the priced catalog, and
purchase → save → unlock. This pass REs that mechanic on our disc and ships it
as a faithful, rendering-free, objectively-tested module. The gameplay judgment
layer and the input layer were not touched.

### (a) What the World Tour store sells, and where the catalog lives [C data]

The store's inventory is the **Create-A-Rocker (CAS) part catalog**: clothing,
accessories, heads/torsos/legs/shoes, and instrument parts (guitars, basses,
drums, mics) and their finishes. The authoritative part list is
`master_editable_list` (`scripts\game\cas\cas_parts.qb`, 59 parts). Each part is
a global array (`CAS_Female_Acc_Left`, `CAS_Guitar_Body`, …, one per
`car_*`/`cas_*` qb), and each option struct carries:
`desc_id`, `frontend_desc` (localized display name), `mesh`, `materials`,
`genre`, `polaroid` (thumbnail), an optional **`price`** (int, the cash cost)
and an optional **`$locked`** flag (career-gated). Example
(`car_female_rocker_acc.qb`): `f_bmtl_acc_lblackspike price=200 $locked`,
`f_bmtl_acc_llaceband price=75` (buyable at once).

Measured on our disc by `--shoptest` (over the 59 `master_editable_list` part
arrays): **439 priced-or-locked catalog items across 31 populated part arrays;
310 priced (234 free-to-buy, 76 career-gated `$locked`+`$price`), 205 locked;
total catalog value $1,655,409.** (Cross-checked: 329 `price =` lines and 228
`$locked` flags across the whole qb.pak — the extra priced/locked entries are in
the progression-reward arrays, not the editable catalog.)

Two other content types the store surfaces are present but **not cash-priced on
this disc**: bonus **songs** (`Bonus_Songs` = eyeofthetiger, satchboogie;
`defaultunlocked=0`, career-unlocked) and bonus **videos** (`Bonus_Videos`,
`unlockable_content.qb`). **Characters** use a separate mechanism
(`purchase_character` → `band_info.purchased_profiles`, and `is_profile_purchased`
= free when the profile has no `price`): on our disc **no preset profile carries
a `price`** (`Preset_Musician_Profiles_Locked/_Modifiable`: 0 price fields), so
characters are career-unlock-gated, not cash-bought.

> NOTE: the GH3-style store arrays the port's `glitches.cpp` still reads
> (`Bonus_Guitars`, `Secret_Guitars`, `Bonus_Outfits`, `Bonus_Styles`, …) **do
> not exist on the World Tour disc** (only `Bonus_Songs`/`Bonus_Videos`/
> `Bonus_progression` do) — they are gd04 (GH3 Vault) inheritance. World Tour's
> store is the CAS cash catalog above, not GH3's Vault.

### (b) The currency — band cash [C scripts bandmoney.qb]

The currency is the current band's **`cash`** (`band_info.cash`, per savegame;
`get_current_band_info` → `GetGlobalTags band_info`). Helpers
(`scripts\game\menu\bandmoney.qb`): `has_enough_money(price)` = `price <= cash`;
`decrease_band_money(price)` = `cash -= price` + `SetGlobalTags band_info`;
`increase_band_money(amount)` the inverse. Cash is **earned in the career**:
`increase_band_cash_and_earnings` (`cas_common_arrays.qb`) →
`increase_career_earnings` + `increase_band_money`, driven by the
`Progression_CashMilestones*` / gig-earnings scripts
(`guitar_progression.qb`). **Career is onboarding Step 7.6**, so cash *earning*
(and the `unlocked` flags for the 76 career-gated items) is a **dependency**;
the purchase mechanic itself is self-contained given a cash balance.

### (c) The purchase + unlock model [C scripts cas_utils.qb, guitar_globaltags.qb, listmenu.qb]

Ownership (`scripts\game\cas\cas_utils.qb`):
- `is_part_unlocked(part, desc_id)`: an option with `$locked` is unlocked only
  if its save flags contain `unlocked`; without `$locked` it is unlocked by
  default.
- `is_part_purchased(part, desc_id)`: an option with `$price` is purchased only
  if its save flags contain `purchased`; without `$price` it is purchased by
  default.
- `is_part_unlocked_purchased` = both → owned & usable elsewhere in the game.

Purchase (`continue_purchase_item`, `scripts\engine\menu\listmenu.qb`):
`has_enough_money(price)` → `SoundEvent Menu_Purchase_Item` →
`decrease_band_money(price)` → `set_current_band_part_flags(part, desc_id,
purchased)`. The store UI is `make_character_purchase_menu` /
`character_purchase` with the `character_purchase_price` text and a yes/no
(`uidesc_character_purchase_menu.qs`); a locked item shows the
"not enough money" dialogue on failure.

### (d) The save format [C scripts guitar_globaltags.qb; port game/save.cpp]

`set_current_band_part_flags` (`scripts\guitar\guitar_globaltags.qb`) stores the
flags in **`band_info.unlock_purchased`**, a struct keyed by `part`
(e.g. `CAS_Female_Acc_Left`), each an **array of `{ desc_id, unlocked?,
purchased? }`** entries; `get_current_band_part_flags` reads the entry back by
`desc_id`. In the port the global tags (incl. `band_info`) are the versioned
JSON save (`game/save.{h,cpp}`, `SaveToMemoryCard`/`LoadFromMemoryCard` natives);
`cash` is an int field and `unlock_purchased` a nested struct→array→struct — the
JSON serializer already round-trips all of these.

### (e) What I implemented (files)

- **`engine/src/game/shop.{h,cpp}`** (`namespace shop`, rendering-free): the
  faithful port of (b)–(d). `catalog(Globals)` enumerates every priced/locked
  CAS option from the real part arrays (`master_editable_list`); `cash` /
  `hasEnoughMoney`; `partUnlocked` / `partPurchased` / `owned` (exactly the
  cas_utils predicates); `purchase(bandInfo, item)` (continue_purchase_item:
  check → deduct cash → set the `purchased` flag in
  `band_info.unlock_purchased.<part>`), returning Ok / NotPriced / NotUnlocked /
  AlreadyOwned / NotEnoughCash; `grantUnlock` (a career reward's `unlocked`
  flag). Invents no items — the catalog is the disc's data.
- **`engine/src/tests/shoptest.{h,cpp}` + `--shoptest`** (`main.cpp`,
  `CMakeLists.txt`): the objective harness.
- No engine/UI/judge/input/decoder code changed; every pinned test stays
  byte-identical (verified: `--uitest` below).

### (f) Objective test evidence (`--shoptest <disc>`, headless, exit 0)

Data-derived expected-vs-measured, no eyeballing:
- **Catalog** = the disc's real priced CAS options (counts in (a)).
- **Buy** a deterministic free-to-buy item (`CAS_Female_Acc_Left/f_punk_acc_lspike`,
  $75): cash **575 → 500** (measured == start − the item's own price), the
  `purchased` flag set, `is_part_unlocked_purchased` true; a repeat buy returns
  `AlreadyOwned` with cash unchanged.
- **Persists to the SAVE:** the purchased `band_info` is `save::write`-n and
  `save::read` back from a save.json round-trip; the read-back tag still has the
  deducted cash and the item still reads owned.
- **Not enough cash:** a buy with `cash = price − 1` returns `NotEnoughCash`,
  cash untouched, item not owned.
- **Career gate:** a `$locked`+`$price` item
  (`CAS_Female_Acc_Left/f_rock_acc_lskullwatch`, $200) returns `NotUnlocked`
  (cash untouched); after `grantUnlock` (standing in for the career reward) it
  buys to Ok and reads owned — proving the two-flag (`unlocked` + `purchased`)
  gate.
- **No regression:** `--uitest` still **47 flow states, 1181/1181
  mouse-hittable, 0 dead screens, 0 UNHANDLED** (identical to the 7.2 baseline);
  the store/character-hub flow states still build. `--shoptest`: **all store
  checks passed (0 failed)**.

### (g) Honest remains / flagged

- **Live UI-click purchase (the biggest remainder).** The store *screens* build
  and the model is the real purchase engine, but a mouse/controller click in the
  live store is not yet routed into `shop::purchase`. The original drives it
  through its own QScript chain (`continue_purchase_item` /
  `make_character_purchase_menu`) over the 3-D `z_soundcheck` board menus, whose
  full native substrate + board-menu live mouse is the same fragile,
  not-headlessly-verifiable surface deferred in 7.1/7.2. Wiring the click to the
  model (or implementing the money/flag natives so the game scripts transact
  against it) is the next increment; the model is ready for it.
- **Career dependency (Step 7.6).** Cash *earning* and the `unlocked` flags for
  the 76 career-gated items come from the career (Progression cash milestones /
  gig earnings). Until 7.6 those are not produced in normal play; the 234
  free-to-buy items are fully exercisable now (and the cheat path already grants
  cash/unlocks — `glitches.cpp`).
- **Characters / bonus songs / videos:** the engine paths exist
  (`purchase_character`/`purchased_profiles`, `Bonus_Songs`/`Bonus_Videos`) but
  carry no cash `price` on this disc, so they are career-unlock content, not a
  cash purchase — not modelled as store items here (would be inventing prices).

### Uncertain / not fully traced (7.4)

- The exact in-memory form of the saved `unlocked`/`purchased` flag (named vs.
  bare key) is [I]: the port stores and reads them as bare flag keys (matching
  the disc's `$locked`), and the save JSON round-trips either; the game's own
  `StructureContains` match form was not bit-verified against the native.
- `frontend_desc` is read as a localized-string key for display but the store's
  per-item name/price *rendering* is the UI lane (not drawn here).
- Whether any World Tour store UI also sells venues for cash (the task listed
  venues) was not found in the disc data — venues on this disc are
  career/`venue_<zone>.unlocked`-gated (see The Glitches venue unlock), not
  cash-priced.
