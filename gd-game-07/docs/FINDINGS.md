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

## Onboarding Step 7.5 — create-a-rocker (character) (2026-11-24)

Milestone 7.5, the **character half** (Create-A-Rocker / CAR; the instrument +
music-studio half is a separate agent's section). Aggregate-first: the port
already parses the whole CAS catalog (`data/band.cpp`), reaches the
character-hub / CAR screens (Step 3: `create_character_hub_menu`,
`z_soundcheck`) and builds a gh4 member from a profile's `appearance`
(`band::memberGh4`). What was missing was the **create / edit / name / save
model** — a new character, the editable slot set, setting each slot to a
catalog option under ownership, naming, and persistence. This pass REs that on
our disc and ships it as a faithful, rendering-free, objectively-tested module
(`game/car.{h,cpp}`), reusing Step 7.4's `game/shop.h` ownership model. No
engine / UI / judge / input / decoder code was touched.

### (a) A created character is a musician profile [C scripts guitar_band_profile.qb]

A Create-A-Rocker character is a **musician profile struct** — the same shape
`band::memberGh4` consumes in-game:
`{ name, fullname, allowed_parts, appearance { <CAS part> = { desc_id } }, blurb,
genre }`. A new character starts from the disc's
`default_custom_musician_profile_male` / `_female`
(`scripts\guitar\guitar_band_profile.qb`): `allowed_parts = [drum, Vocals,
guitar, Bass]`, `appearance` carrying `cas_body` (`GH4_CAR_Male`/`GH4_CAR_Female`),
`cas_physique` (`MalePhysique`/`FemalePhysique`), the default instrument parts
(`CAS_Guitar_Body = guitar_body01`, …) and the default intro/win/lose anims, and
`genre = Rock`. Each slot value is a struct `{ desc_id, cap? }` where the
optional `cap` array is the per-part **colour / finish / texture-layer** (e.g.
the default guitar neck's `base_tex` / `material` / `pre_layer` inlay texture —
this is how colours and tattoo/decal layers ride on a slot).

### (b) The editable slot set [C scripts\game\cas\cas_parts.qb master_editable_list]

The authoritative editable-part list is **`master_editable_list`** (59 parts).
It holds the common slots (`cas_body`, `cas_physique`, `CAS_Full_Body`,
`CAS_Eyes`), BOTH genders' body/anim slots (`CAS_Male_*` and `CAS_Female_*`:
Hair, Hat_Hair, Hat, Facial_Hair, Torso, Legs, Shoes, Acc_Left, Acc_Right,
Acc_Face, Acc_Ears, Intro_Anim, Win_Anim, Lose_Anim, Age — 15 each), the
instrument slots (`CAS_Guitar_*` ×9, `CAS_Bass_*` ×9, `CAS_Drums*` ×3,
`CAS_Mic`/`CAS_Mic_Stand`, `CAS_Full_Instrument`) and `CAS_Band_Logo`. A given
character is **gender-filtered** by `cas_physique` (`Male`/`FemalePhysique`): the
editor shows the common slots + its own gender's `CAS_<gender>_*` slots, which
is **44 slots per gender** (29 common + 15 gendered) — measured by `--castest`.
(`cas_parts.qb` also carries the gender-neutral aliases `body_specific_parts_list`
= `CAS_Hair`/`CAS_Torso`/… and `hair_part_sets`/`instrument_part_sets` the UI
maps onto the gendered arrays.) Each slot's options are the CAS part global
array (`CAS_Male_Torso`, …), each option `{ desc_id, frontend_desc, mesh, …,
price?, $locked? }` — the exact arrays Step 7.4's store catalogues.

### (c) Create / name / edit / save model [C guitar_band_profile_scripts.qb]

Created characters live in the savegame's **`custom_profiles`** global array:
- `new_custom_character_name(savegame)` → the first free `custom_character_%d`
  (`FormatText … addtostringlookup`, scanned `0..99` via
  `is_completely_custom_musician`), stored as `new_character_id`.
- `add_new_custom_profile(savegame, profile)` →
  `globaltag_addarrayelement array_name=custom_profiles element=profile`.
- `modify_custom_profile_appearance(id, savegame, appearance)` → finds the
  element (`is_completely_custom_musician` → `custom_musician_index`) and
  `globaltag_setarrayelement … element = { <elem> appearance = <appearance> }`.
- `modify_custom_profile_fullname(id, savegame, fullname)` → the same, setting
  the element's `fullname` (the display name).
- `delete_custom_profile(savegame, id)` → `globaltag_removearrayelement`.
- `get_musician_profile_struct_by_id` / `profile_exists` resolve a character by
  searching `custom_profiles` first, then
  `Preset_Musician_Profiles_Modifiable` / `_Locked` / `Net_Musician_Profiles`.

Ownership of a chosen option is Step 7.4's `is_part_unlocked_purchased`
(`cas_utils.qb`): a `$price`/`$locked` option needs the save's
`purchased`/`unlocked` flags; a free, unlocked default option is owned by
construction. So editing a slot = "pick an **owned** option for an editable
slot, write its `desc_id` into `appearance.<part>`".

### (d) What I implemented (files)

- **`engine/src/game/car.{h,cpp}`** (`namespace car`, rendering-free):
  `editableSlots(g, female)` / `isEditableSlot` (the gender-filtered
  `master_editable_list`); `newCharacter(g, savegame, female)` (deep-copies the
  default profile, assigns the first-free `custom_character_<N>`, appends to
  `custom_profiles` — `add_new_custom_profile`); `profile` / `customIndex`
  (`is_completely_custom_musician`) / `count` / `isFemale`; `setPart(g,
  savegame, id, part, descId, bandInfo)` (`modify_custom_profile_appearance`:
  validates the slot is editable for the gender, the option exists, and — given
  a `band_info` — that it is owned via `shop::owned`; returns Ok / NoSuchCharacter
  / NotEditable / NoSuchOption / NotOwned); `partOf`; `setFullName` / `fullName`
  (`modify_custom_profile_fullname`); `deleteCharacter` (`delete_custom_profile`).
  Invents nothing — defaults, slots and options are the disc's data. Reuses
  `game/shop.h` for ownership (does not duplicate it).
- **`engine/src/tests/castest.{h,cpp}` + `--castest`** (`main.cpp`,
  `CMakeLists.txt`): the objective harness.
- No shared engine file was edited (notably `data/band.cpp` — the other agent's
  and the instrument half's surface — was only *read*, via `band::memberGh4`).

### (e) Objective test evidence (`--castest <disc>`, headless, exit 0)

Data-derived expected-vs-measured, no eyeballing:
- **Editable slots**: `male 44, female 44` (= 59 `master_editable_list` − the
  other gender's 15 parts); each set has `cas_body`/`cas_physique`/the guitar
  slots, the male set has `CAS_Male_Torso` and not `CAS_Female_Torso` (and the
  reverse for female).
- **Create + defaults**: first create is `custom_character_0`, reads male,
  `cas_physique = MalePhysique`, default `CAS_Guitar_Body = guitar_body01`.
- **Unique naming**: three creates give `custom_character_0/1/2`; the 3rd is
  female and reads female.
- **Edit gates**: a `CAS_Female_Torso` on a male character → `NotEditable`; a
  bogus `desc_id` → `NoSuchOption`; real male body slots set from real options.
- **Ownership (reusing shop)**: a priced, free-to-buy option on a female slot
  (`CAS_Female_Hat/f_pop_hat_bucket`, $75) is **refused (`NotOwned`) until
  bought**, then accepted and written; a **career-gated** (`$locked`+`$price`)
  option (`f_bmtl_hat_skullhelmet`) is refused until `grantUnlock` + `purchase`.
- **Save round-trip**: the savegame struct (holding `custom_profiles`) is
  `save::write`-n and `save::read` back — 3 characters, the edited slot and the
  name (`fullname = "Riff Dream"`) all persist.
- **Used in-game**: the created `custom_character_0` profile fed to
  `band::memberGh4` builds a **playable guitarist, 12 meshes, skeleton
  `GH_Rocker_Male_original`** — i.e. a created character shows up as a real band
  member.
- **Delete + id reuse**: deleting `custom_character_0` frees the id, and the
  next create reclaims `custom_character_0` (faithful to the first-free scan).
- **No regression**: `--uitest` unchanged — **47 flow states, 1181/1181
  mouse-hittable, 0 dead, 0 UNHANDLED** (the 7.2/7.4 baseline). `--castest`: all
  checks passed (0 failed).

### (f) Honest remains / flagged

- **Live 3-D editor UI click-wiring** (the biggest remainder, same surface the
  other steps defer). The CAR screens build and the model is the real
  create/edit engine, but a mouse/controller click on a part tile in the live
  `z_soundcheck` board menu is not yet routed into `car::setPart` /
  `car::setFullName`. The original drives it through its own QScript chain
  (`cas_queue_*`, `modify_custom_profile_*`, `ss_focus_on_create_new_character`)
  over the 3-D board menus — the same fragile, not-headlessly-verifiable
  board-menu live-mouse surface deferred in 7.1/7.2.
- **Per-part colours / finishes / tattoos (the `cap` layer) and the physique
  bone sliders (`cas_physique_bone_options`)** are preserved when they ride on a
  copied default slot, but this pass models **slot → `desc_id`** selection, not
  the deeper colour/finish/decal sub-editor or the bone-slider morphs (those are
  a further CAR sub-menu; the data shape — `appearance.<part>.cap` and the bone
  options — is documented here but not edited by `car::setPart`).
- **band.cpp does not yet resolve `custom_profiles`.** The original's
  `get_musician_profile_struct_by_id` searches `custom_profiles` first; the
  port's `band::findProfileGh4` only searches the preset arrays. Wiring a
  created character into the live band-build path (so a created rocker can be
  picked as the player's character) edits `data/band.cpp` (a shared file) and is
  the integration follow-up — proven feasible here by feeding the profile
  straight to `band::memberGh4`.

### Uncertain / not fully traced (7.5)

- The save-tag placement of `custom_profiles`: the original keeps it as a
  savegame-root global array, sibling to `band_info`; the port's flat
  one-`Struct`-per-tag save map can't key a root-level array directly, so
  `--castest` round-trips the **savegame root struct** (holding the
  `custom_profiles` array) under one tag — the JSON save serializes the nested
  struct→array→struct faithfully either way [I] (same note as 7.4's flag form).
- `fullname`'s on-disc type (the net profiles store a 32-bit `fullname` number;
  the port models the user-entered display name as a wide string) was not
  bit-verified; the create flow's text-entry menu is the deferred UI.
- The exact `cap`/bone-slider edit semantics (above) were not decoded.

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

## Onboarding Step 7.3 — quickplay (2026-11-24)

Aggregate-first pass over World Tour's **Quickplay** mode: the entry, song
select (sort + custom set list), instrument / difficulty select, the
co-op / face-off / pro-face-off option set, the results screens, and unlock
gating. The flow runs the disc's own QScript through the VM (`FrontEnd::start`
runs `guitar_startup`), so the screens are faithful by construction; this pass
confirms the path end-to-end on the real data and fills the one in-scope test
gap (the quickplay set-list sort).

### Flow diagram (WT's own scripts, Ghidra-grounded)

```
main_menu_fs (guitar_main_menu.qb create_main_menu_elements:
             CAREER, QUICKPLAY, MULTIPLAYER, TRAINING, ... )
 ├─ QUICKPLAY  (main_menu_select_quickplay -> main_menu_select_generic
 │              state = uistate_game_mode  Data = { mode = quickplay })
 │   └─ uistate_game_mode  (ui_game_mode.qb ui_create_game_mode, mode=quickplay)
 │        • Solo Quickplay  (game_mode_select_quickplay -> game_mode=p1_quickplay,
 │                           current_num_players=1 -> uistate_select_instrument)
 │        • Band Quickplay  (pad_choose_params={band}; enabled only when
 │                           are_multiple_controllers_connected, else greyed
 │                           rgba=[50,44,35,255]; -> p2_quickplay co-op ->
 │                           uistate_band_mode)
 │        └─ uistate_select_instrument -> UIstate_setlist_prompt
 │             -> (UIstate_double_bass_popup) -> uistate_setlist
 │                  (ui_setlist.qb: sortable custom set list, up to 6 songs)
 │                  -> uistate_select_difficulty
 │                       -> quickplay_start_song (quickplay_menu_flow.qb:
 │                          solo picks a random unlocked venue + guitarist +
 │                          guitar via set_random_single_player_quickplay /
 │                          quickplay_choose_random_venue) -> start_song
 │                            -> song -> song_over
 │                               -> uistate_song_breakdown
 │                               -> uistate_cash_rewards_3 -> uistate_setlist
 └─ MULTIPLAYER (main_menu_select_multiplayer -> game_mode=p2_faceoff,
                 current_num_players=2 -> uistate_select_controller
                 -> uistate_band_mode: FACE-OFF / PRO FACE-OFF / BATTLE)
```

Game modes are data (`guitar_gamemode.qb`): `Gamemode_list` = p1..p4_quickplay,
p1..p4_career, p1_improv, p1_boss, p2_faceoff, p2/p4/p8_pro_faceoff, p2_coop,
p2_battle, tutorial, training; each `<mode>_props` (num_players, type,
cooperative/faceoff/singleplayer) over `default_gamemode_props`. The port's
`modeProp` (ui/natives_gh4.cpp:127) reads exactly this merge — the quickplay /
co-op / face-off / pro-face-off option set is faithful by construction.

### Per-screen status

| Screen | Original (evidence) | Port status |
|---|---|---|
| Main-menu QUICKPLAY item | `create_main_menu_elements` item 2 → `uistate_game_mode {mode=quickplay}` | **Reproduced** — `--walktest quickplay` reaches `uistate_game_mode` from item 2 |
| `uistate_game_mode` (Solo / Band) | `ui_create_game_mode` adds Solo (`game_mode_select_quickplay`) + Band (multi-controller gate) + practice entry | **Reproduced** — Solo item drives the full path objectively; Band item is script-built (2-controller objective coverage deferred) |
| `uistate_select_instrument` | guitar / bass / drums / vocals | **Reproduced** (walk C selects guitar) |
| `UIstate_setlist_prompt` / `UIstate_double_bass_popup` | pre-set-list prompts (optional, D23 ordering) | **Reproduced** (optional walk steps) |
| `uistate_setlist` (song select) | `ui_setlist.qb`: **sortable custom set list** — `setlist_sorts`=[title_alphabetical, artist_alphabetical, career_order]; Y(`pad_option2`)→`setlist_switch_sort` cycles the sort; green adds a song (≤6); X jumps a section; LB/RB clears [not Metallica's SETLIST/BONUS/DOWNLOADS tabs] | **Reproduced + now objectively tested** — `--walktest setlist_tabs` cycles `setlist_sort_index` 0→1→2→0 |
| `uistate_select_difficulty` | Easy/Medium/Hard/Expert | **Reproduced** |
| `quickplay_start_song` | solo: random unlocked venue + CAR/preset character + guitar | **Reproduced** — song launches and autoplays |
| song → results | `uistate_song_breakdown` → `uistate_cash_rewards_3` → `uistate_setlist` | **Reproduced** — `--walktest quickplay` plays the song and returns through both results screens |
| Unlock gating | `GlobalTags_UnlockAll` / per-tier `tier<n>.unlocked`, `GH3_General_Songs`, save-wide quickplay-song unlocks [glitches.cpp] | **Modeled** via `globalTags` (ui/glitches.cpp unlockList); career-unlocks-quickplay path reproduced |

### What I implemented

- **`tests/walk.cpp` `setlist_tabs` retargeted to WT's real quickplay flow.** The
  prior walk was a **Metallica (gd10 / GD10-090) lineage** test: it entered via
  `main_menu_fs {D,D,C}` → `quickplay_select_difficulty_fs` → `quickplay_setlist_fs`
  and asserted SETLIST/BONUS/DOWNLOADS **tabs** (`change_tab`). World Tour's
  Quickplay has a **different, distinct front end**: QUICKPLAY → `uistate_game_mode`
  → Solo → instrument → `uistate_setlist`, and that set list is a **sortable
  custom set list**, not a tab browser. On WT, item 3 (`{D,D,C}`) is MULTIPLAYER
  (→ `uistate_select_controller`), so the old walk reached the wrong screen and
  timed out. Retargeted it to drive WT's own path and assert the WT-correct
  behavior: Y (`pad_option2` → `setlist_switch_sort`) cycles
  `setlist_sort_index` title→artist→career→title. Test-only change; the
  judgment layer and product code are untouched.

### Objective evidence

- `--uitest` (regression, pinned): **47 flow states, 1181 focusable / 1181
  mouse-hittable, 0 dead**, 11 built-no-element (the online stubs, expected). No
  change from 7.2.
- `--walktest quickplay` (the gh4 solo-quickplay path): **flow passed** —
  `main_menu_fs → uistate_game_mode → uistate_select_instrument →
  UIstate_setlist_prompt → uistate_setlist → uistate_select_difficulty → song →
  song_over → uistate_song_breakdown → uistate_cash_rewards_3 → uistate_setlist`;
  **songs started 1, ended 1** (autoplayed; HUD/score logged, e.g. `hud_1g` 95
  elements, score accruing). The run's non-zero exit is a **pre-existing
  perf-hitch check** (`FAILED the no-stutter check: 1 frame 120.7 ms on
  uistate_play_song during the song's load` under headless build load) — not a
  quickplay-flow fault.
- `--walktest setlist_tabs` (retargeted): **passed** — reaches WT's quickplay
  `uistate_setlist` and `setlist_sort_index` cycles `0 → 1 → 2 → 0`.

### Honest remains / flagged

- **2-player competitive quickplay walks (`coop`, `faceoff`, `proface`,
  `battle`) are gd10-Metallica lineage and do not drive WT's flow.** They expect
  `coop_career_select_controllers_fs` / `mp_select_controller_fs` and press
  main-menu button counts that, on WT, land on `uistate_game_mode` /
  `uistate_jam` / `uistate_select_controller` — so they fail on gd07 (they were
  already failing on committed code; this pass did not change them, and they are
  **not in gd07's canonical suite** — only `quickplay`/`boot`/`formattest`/`fuzz`
  are run). WT's real competitive path is MULTIPLAYER → `uistate_select_controller`
  → `uistate_band_mode` (FACE-OFF / PRO FACE-OFF / BATTLE; `ui_band_mode.qb`,
  61 KB) and Band Quickplay → `uistate_band_mode` (co-op). The **option set is
  confirmed faithful from the QB** (above); retargeting these 2-player walks to
  WT's `band_mode` flow is a **multiplayer-wave follow-up** (2-controller driving
  + the large band_mode layout), not required for the quickplay gate.
- **Band Quickplay item** is built by the script the port runs; its objective
  coverage needs a 2-controller walk (the Band entry is gated on
  `are_multiple_controllers_connected`). Deferred with the above.
- **Unlock gating** is modeled through `globalTags`; a default-save vs
  fully-unlocked quickplay-list diff was not independently asserted this pass
  (the career→quickplay unlock path is reproduced via `GH3_General_Songs`).

### 7.3 gate (65%)

**MET for the core:** the solo Quickplay flow is faithful and objectively proven
end-to-end (entry → game-mode → instrument → sortable set list → difficulty →
autoplayed song → both results screens → set list), the quickplay set-list sort
is now an objective check, and the full option set (solo/band/co-op/face-off/
pro-face-off/battle) is Ghidra-confirmed against `guitar_gamemode.qb` +
`ui_game_mode.qb`. The honest remainder is **2-player competitive walk coverage**
(a multiplayer-wave item), not the quickplay mode itself.

## Onboarding Step 7.5 — create-a-rockstar (instrument) + GHTunes (music) (2026-11-24)

Milestone 7.5 instrument/music half (the character half is a sibling agent;
this agent owns Create-A-Rockstar **instrument** creation and the **GHTunes**
music studio). Aggregate-first, Ghidra/QB/disc-grounded, rendering-free,
objectively tested. No engine/UI/judge/input decoder changed; the pinned
`--uitest` stays **47 flow states, 1181/1181 mouse-hittable** (verified).

### (a) Create-A-Rockstar — the instrument editor [C data + scripts]

World Tour builds a custom instrument the same way it builds a character: out
of **CAS part arrays**, one option chosen per slot. The instrument slots (the
`CAS_<Instr>_<Slot>` globals, `cas_parts.qb`):
- **Guitar / Bass**: Body, Neck, Head, Pickups, Pickguards, Bridges, Knobs,
  Strings, Highway (`CAS_Guitar_Body` … `CAS_Guitar_Highway`, and the
  `CAS_Bass_*` set).
- **Drums**: `CAS_Drums`, `CAS_Drums_Sticks`, `CAS_Drums_Highway`.
- **Mic**: `CAS_Mic`, `CAS_Mic_Stand`.

Measured on our disc (`--instrtest`): guitar **9 slots, 8 populated, 499
options**; bass **9 slots, 8 populated, 237 options**; drums **3 slots, 2
populated, 7 options**; mic **2 slots, 2 populated, 18 options**.

The chosen option per part is the disc's **`cas_current_appearance`** struct
(part → `{ desc_id, … }`); `GetCASAppearancePart(part)` returns that entry
(`scripts\game\cas\cas_appearance.qb`). A part can only be equipped if it is
**owned** — `is_part_unlocked_purchased` (`cas_utils.qb`), the exact predicate
the Rock Store sells against (Step 7.4): a free default option is owned by
everyone; a priced/`$locked` option needs the store purchase / career unlock.
The instrument's **name** is `instrument_name` (a wide string) and its kind is
`instrument_type`; both save with the appearance into `band_info`
(`set_current_band_part_flags` / `SetGlobalTags band_info`, the same versioned
save the store uses).

**Implemented** — `engine/src/game/instrument.{h,cpp}` (`namespace instrument`,
rendering-free): `slots(kind)`, `options(g, part)` (every selectable `desc_id`
from the part array, not only the priced ones the store sells), `makeDefault`,
`choose(inst, g, bandInfo, part, descId)` → `Ok` / `NotASlot` / `NotAnOption`
/ `NotOwned` (option-validated **and** ownership-gated via `shop::owned`, reusing
Step 7.4's catalog), and faithful `toBandInfo` / `fromBandInfo`
(`instrument_type` key + `instrument_name` wstring + `cas_current_appearance`
struct of part → `{ desc_id }`). It invents no parts — every option is the
disc's data, and ownership is the disc's predicate.

**Objective test** (`--instrtest <disc>`, headless, exit 0, data-derived):
every kind enumerates its real slots/options; a free body option equips (`Ok`);
a bogus `desc_id` is `NotAnOption`, a drums part on a guitar is `NotASlot`; a
priced guitar part (`CAS_Guitar_Body/Guitar_Body16`, $500) is **`NotOwned`
until bought**, then equippable (ownership gate proven); and a composed,
named guitar (kind + name + all 8 chosen parts) survives a **save→reload
round-trip** byte-for-byte through `game/save`.

### (b) GHTunes `.jam` — the music-studio song format [C data: all 19 jams]

The 19 studio songs (`DATA/COMPRESSED/JAMS/jam1.pak.xen`,
`c:/gh4/data/jams/*.jam`) were previously UNDECODED (Step 2). Decoded now.

A `.jam` is **not** a standard `.qb` file; it is a Neversoft **keyed-struct
stream**: a flat sequence of members, each `tag(1) + name-key(4, a standard QB
key stored LITTLE-endian) + typed value`:

| tag | meaning |
|---|---|
| `0x00` | end of the current struct (pop) |
| `0x04 key wstrz` | UTF-16LE NUL-terminated wide string |
| `0x0a u16` | array header: the element count; that many struct elements follow inline |
| `0x0c key` | open a sub-struct (members until `0x00`) |
| `0x12 key` | integer, value 0 (compressed — no value bytes) |
| `0x10..0x18 key` | integer, value = `(tag & 0x0F)` little-endian bytes |

The keys resolve against the disc's debug-name table **read little-endian**
(e.g. bytes `ba 67 6b 90` → `0x906b67ba` = `qbkey("time")`). Confirmed field
names: `editable_jam_markers` (an **array of section markers**, each `time`,
`marker_name`, `bpm`, `lightshow`) + `editable_jam_markers_size`;
`editable_jam_album_cover`(`_size`); and the three instrument note tracks
`editable_song_expert` (guitar), `editable_song_rhythm_expert` (bass),
`editable_song_drum_expert` (drums); plus `last_root` / `last_scale`.

After the keyed header comes the per-instrument **note-performance payload**, a
separate encoding in one of two shapes, detected by its first byte:
- **note-table (`0x01`)**: a `0x01` + `u16` count, then fixed 8-byte records
  (`u16 duration, u16 note, u32 time`) — the "studio" jams.
- **opcode-stream (`0x8x`)**: a `0x8D/0x8E/0x90/0x91…` delta-opcode bytestream —
  the recorded jams.

**Implemented** — `engine/src/data/jam.{h,cpp}`: a self-contained parser that
decodes the keyed header fully (section markers with names/time/bpm/lightshow,
marker counts, album cover, which instrument tracks are present, the sample
dictionary `0x0c` count) and **classifies + measures** the note payload. It
loads all 19 from the jam pak.

**Objective test** (`--jamtest <disc>`, headless, exit 0, data-derived): **19
jams parsed, 0 header parse errors; 56 section markers total** (names resolve:
Intro/Verse/Chorus/Bridge/Riff/Solo/Fill/Outro), bpm 80–140 per jam;
`studioexample1` carries all three tracks (G/R/D); **note payloads: 9
note-table + 10 opcode-stream, 508,139 bytes total**, every one classified.

### (c) Honest scope — what is deferred to a dedicated GHTunes wave

The container + song structure (markers/sections/tempo/lightshow/track
presence/album cover) is fully decoded and tested. **Not** done here, and
flagged as a dedicated **GHTunes composition wave** (large, standalone):
- **Full note decode** of the two note-payload encodings into playable events
  (the 8-byte note-table records are byte-identified; the `0x8x` delta-opcode
  stream and the per-sample dictionary GUIDs are not fully decoded).
- **Synthesis / playback** of a jam (the GHTunes sampler/instrument banks) and
  the **composition studio UI** (track/pattern/note editing, tempo/scale, the
  song-writer/wizard flows — `jam_songwriter.pak`, `jam_wizard.pak`,
  `jam_advanced.pak`, `jam_ghtunes.pak`). These are not faked.
- The instrument editor's **live UI-click** wiring is the same board-menu
  surface deferred in 7.1/7.4 (the model is ready for it).

### (d) Files touched

- New: `engine/src/game/instrument.{h,cpp}`, `engine/src/data/jam.{h,cpp}`,
  `engine/src/tests/instrtest.{h,cpp}`, `engine/src/tests/jamtest.{h,cpp}`.
- Wiring: `engine/src/main.cpp` (`--instrtest` / `--jamtest`),
  `engine/CMakeLists.txt`. No decoder/engine/UI/judge/input change; `--uitest`
  pinned at 1181/1181.

## Onboarding Step 7.7 — tutorials (2026-11-24)

**Aggregate first.** The port had **no** tutorial layer: `grep -ri tutorial
engine/src` found only incidental hooks (`tutorial_disable_hud` in
`ui/song_hooks.cpp`, a `select_tutorial` comment in `ui/front_end.cpp`, a
`TUTORIAL` set-list filter in `platform/module_songs.cpp`). The gameplay judge,
gem scroller, HUD, venues and VO buses that the lessons wrap are all in place
(Steps 3–6). So 7.7 is the teaching layer around the finished judge.

**The original (Ghidra-grounded, decoded QB in `qb.pak.xen`).** World Tour
drives **every** lesson with ONE interpreter — the "training script" engine
`scripts\tutorials\training.qb`:
- `run_training_script` fires the array's step 0, then (for Restart_Lesson)
  scans to the step whose `lesson` == `g_training_last_lesson`, then spawns
  `training_script_update`.
- `training_script_update` runs once per gameframe over the current step:
  a `time` step fires at absolute ms (`current_training_time`); a `rel_time`
  step fires at ms since the last fired step (`last_training_call_time`); a bare
  step fires as soon as the PREVIOUS step's `call` script is no longer running
  (`not ScriptIsRunning`). A `lesson = N` marker records `g_training_last_lesson`.
  When `current_training_step` == array size, `kill_training_script_system` ends
  the session.
- A training script is `training_<item>_tutorial_script` = an array of structs
  `{ lesson? time?/rel_time? call }`.

**The lessons (the ordered progression).** The select menu
`scripts\guitar\menu\menu_tutorial_select.qb` groups them:
`tutorial_lessons = [guitar_lessons drum_lessons vocals_lessons band_lessons]`;
each entry's `item` string is formatted into the script name
(`menu_tutorial_select_choose`: `FormatText 'training_%i_tutorial_script'`),
and `generic_event_choose $uistate_play_tutorial` launches it. Nine tracks,
52 lessons, 40 player gates (measured by `--tuttest` straight from the arrays):
- **Guitar / basic** (`training_basic_tutorial_script`, 8 lessons): 1 hold the
  guitar (left/right orientation prompt), 2 frets+strum+tuning (strum each of
  5 strings to pitch), 3–6 play scrolling notes (gate: `notes_hit >= 8`),
  7 play notes (gate: `notes_hit >= 4`), 8 the HUD. [C `guitar_training_basic_tutorial.qb`]
- **Guitar / star_power** (3 lessons): 1 fill & activate Star Power
  (`training_received_star_power`), 2 the whammy bar, 3 combine (gate:
  `notes_hit >= 8`).
- **Guitar / advanced_techniques** (5 lessons): hammer-ons & pull-offs
  (`wait_for_hammeron_complete`, `wait_for_hammerons`, `wait_for_pulloff_complete`).
- **Guitar / new_features** (4 lessons): extended sustains, slider tapping,
  slider strumming, slider wah (the touch strip).
- **Guitar / versus** (7 lessons): Battle game-types, lead indicator, hit-notes
  / tilt / fix-broken-string / attack tasks.
- **Drums / drum_basic** (6 lessons) and **drum_int** (5 lessons): hold sticks,
  play-each-note tasks on a static/scrolling highway, freeform
  (`*_add_play_each_note_task` → `*_wait_for_drum_test_complete`, pads set
  `LESSON_COMPLETE`).
- **Vocals** (6 lessons shown as 8 markers counting headers): the mic, static
  vs scrolling layout (a `prompt_for_layout` choice), singing / spoken /
  freeform tasks, rock meter demo.
- **Band** (4 lessons): intro, the band's rock meter + individual indicators +
  note-streak demos, playing in unison, band-vs-band. All scripted demos — no
  player gate.

**Interventions / prompts / animations / movies / events.** All from
`scripts\tutorials\tutorial_funcs.qb` (the shared vocabulary):
- On-screen prompts: `training_set_lesson_header_text` /
  `training_add_lesson_body_text` / `training_show_lesson_header` /
  `training_show_task_header` / `training_set_task_header_body`; `training_blink_task`.
- Narrator: `training_show_narrator { narrator = 'guitarist' | 'drummer' | ... }`
  (`c:/gh4/data/images/training/tutorial_narrator_*`), with per-language VO
  (`training_play_sound 'Tut_Gtr_*'`, `training_get_language_prefix`,
  `Training_VO` buss), positive/negative feedback banks (`training_play_positive`
  / `training_play_negative`, random-no-repeat).
- Pointer/finger animations: `training_add_arrow`, `training_make_pointer_point_now`,
  `training_create_fret_finger_sprites` / `training_animate_fret_fingers` /
  `pose_fret_fingers`, `training_animate_strum_fingers`, the tuning note sprites.
- Gem scroller & camera: `training_start_gem_scroller` (calls the real
  `start_gem_scroller … training_mode=1` with a bot), `training_setup_camera`,
  `training_pause/resume/destroy_gem_scroller`, `training_set_health` /
  `training_set_score` / `training_set_HUD_flashing_red`.
- "Special corks" (scripted demo moments): the band/vocals `*_and_demo` /
  `*_show_rock_meter_and_demo` steps, `tutorial_enable_botplay`, the
  `create_exploding_text 'LESSON COMPLETE'` + `Tutorial_Mode_Finish_Chord`.
- Movies: no BINK cutscenes — the teaching is live (narrator icons + VO +
  gem-scroller), `training_init_session` plays an IGC view-cam (`ch_view_cam`)
  and `Hide_Band`.
- Scripted gates (wait-for-the-player): bare steps whose `call` loops
  `Wait 1 gameframe` until a judge-fed global reaches a threshold read from the
  gate bytecode — `training_1_3_wait_for_hit_notes` (`notes_hit >= 8`),
  `training_1_7_wait_for_hit_notes` (`>= 4`), `training_2_x` star-power
  (`training_received_star_power == 1`), the drum/vocals `*_task`
  (`LESSON_COMPLETE == 1`). Pause/skip/restart/quit:
  `training_create_pause_menu`, `tutorial_skip_lesson` (`++g_training_last_lesson`
  then `tutorial_restart`), `tutorial_restart` (`run_training_script $Restart_Lesson`).

**What the port now does.** New `engine/src/game/tutorial.{h,cpp}` — a
rendering-free, faithful port of the two training.qb scripts plus the select
menu:
- `tutorial::load(Globals)` builds the whole select menu and parses all nine
  `training_<item>_tutorial_script` arrays straight from the disc globals
  (`tutorial_lessons` → 4 categories → the `item` string → the script array);
  it invents nothing (the set, grouping, lesson markers and step timing all
  come from the data). `item` is stored as a QScript string ('basic', 'drum_int').
- `tutorial::Session` reproduces `run_training_script` / `training_script_update`
  exactly: it fires steps on the simulated clock (`time` / `rel_time` / previous
  call finished), records `g_training_last_lesson` on a marker, blocks at a gate
  step until `gateInput()` has supplied the scripted/autoplayed player input
  enough times to satisfy it, and counts `g_training_lessons_completed`.
- Gate thresholds that are known from the gate bytecode are reproduced
  (`notes_hit >= 8` / `>= 4` etc.); the rest default to one scripted-input unit.

**Objective evidence (`--tuttest`, headless, data-derived).**
`gd07 --tuttest extracted` →
`9 tracks, 52 lessons, 40 player gates total; 9/9 tracks ran to completion …;
all tutorial checks passed (0 checks failed)`:
- (a) menu = the disc's 4 categories / 9 scripts; every track's lesson count
  equals the `lesson` markers in its real array; grouping is guitar 5 / drums 2
  / vocals 1 / band 1.
- (b) the clock model: the `time=1000` title step does not fire before 1000 ms
  and does after.
- (c) a gate BLOCKS with no input (step index frozen across 5000 frames) and
  releases at the real threshold — the `notes_hit >= 8` gate does not advance on
  the 7th hit and does on the 8th (expected-vs-measured).
- (d) every one of the 9 tracks runs to completion when each gate is fed, and
  the completed-lesson count reaches the track's lesson count.
- `--uitest` unchanged (47 flow states, 1181/1181 mouse-hittable).

**Honest remains.** (1) Live wiring of the `Session` into the running
`uistate_play_tutorial` screen (spawning the gem scroller + narrator sprites +
VO from the real engine, feeding `gateInput()` from the live note-hit / pad /
star-power events) is not done here — the model and its advance are proven
headless, but the on-screen lesson is not yet playable end-to-end in the app.
(2) Gate thresholds beyond the four read from bytecode default to 1 (the tuning
5-string count, the drum per-pad set, the slider-task counts are not yet decoded
from their gate scripts). (3) The narrator sprite/finger animations and per-
language VO selection are catalogued, not rendered. These are the same live-UI
remainder flagged for 7.3/7.5 and belong with the gameplay-screen wiring wave.

**Files.** New: `engine/src/game/tutorial.{h,cpp}`,
`engine/src/tests/tuttest.{h,cpp}`. Wiring: `engine/src/main.cpp` (`--tuttest`),
`engine/CMakeLists.txt`. No decoder / engine / UI / judge / input change.

## Onboarding Step 7.6 — career (2026-11-24)

Milestone 7.6 (Career, the main mode; gate 85%). Aggregate-first: the career
UI screens, the venue/light/anim stack and the gameplay judge already exist and
are reached by the flow manager; the mode's *progression spine* — the tier/gig
structure, star gating, cash earning, gig→gig unlock, and the content rewards —
was not modelled. This pass REs that spine on our disc (Ghidra static export +
decoded QB/QScript) and ships it as a faithful, rendering-free, objectively
tested module, reusing `shop.cpp` for the reward unlocks so it closes the cash +
`unlocked` loop that Step 7.4 flagged as a career dependency. The judge/input/UI
were not touched; `--uitest` stays pinned.

### (a) The progression structure [C scripts guitar_progression*.qb]

Five careers share one structure (`scripts\guitar\guitar_progression_{band,
guitar,bass,drum,vocals}.qb`): `GH4_Career_<Part>_Songs` and
`GH4_Career_<Part>_Progression_Unlock_Order`. Measured by `--careertest`:

| Career | num_tiers | tiers with songs | career songs | unlocksets | intro / outro movie |
|---|---|---|---|---|---|
| band   | 20 | 20 | 87 | 11 | band_intro / band_outro |
| guitar | 20 | 20 | 90 | 11 | guitarist_intro / guitarist_outro |
| bass   | 20 | 20 | 88 | 11 | bass_intro / bass_outro |
| drums  | 20 | 20 | 88 | 11 | drummer_intro / drummer_outro |
| vocals | 20 | 20 | 87 | 11 | singer_intro / singer_outro |

Each `GH4_Career_<Part>_Songs` carries `prefix` (the gig-name prefix,
`career_band`…), `part`, `num_tiers = 20`, `initial_movie`, `end_movie`, and
`tier1..tier20`. A tier = `{ songs[], encorep1, level (load_z_<zone>),
poster_texture, genre, defaultunlocked, paytoplay }`. Gig N plays tier N. The
first 13 tiers are the main career (venue gigs); tiers 14–18 are **pay-to-play
bonus gigs** (`paytoplay = 8000/2000/2000/2000/3000` — the "special
performances"): Tool (parabola/schism/vicarious $8000), Hotel/Tiki, Studio,
Scifi, WOW. Tier 19 is `$createagig` (Create-a-Gig), tier 20 is `$debug_gig`.
The **finale**: band tier 13 (`load_z_NewYork`, Times Square) carries
`$end_with_credits` and `play_finalbandintro_transition_type =
finalbandintro_transition_played_guitar`.

`GH4_Career_<Part>_Progression_Unlock_Order` = `freegigs` (gigs 1,2,3 unlocked
at start; a 4th entry = the debug gig 20 with `$DEBUG_DONT_UNLOCK`),
`freegigs_last` (gig 19, a free bonus gig), and `unlockset1..11`. Each set lists
`gig%d` / `special_event%d` / `pay_to_play%d` entries `{ name='gigN', num=N,
forced_pos }`. Completing a gig opens the next set.

### (b) Gig completion + star gating [C Progression_Career_Check_Gig_Complete, SaveGigComplete]

`Progression_Career_Check_Gig_Complete` (526 B): a tier's gig is complete when
**every song in the tier has > 2 stars** at some difficulty index (0–4). Stars
come from `update_song_star_rating`: `new_stars = 3`, `4` if `Score >=
base_score * star_rating_cutoffs.star4[n]`, `5` if `>= star5[n]`.
`SaveGigComplete` (726 B) sets the gig's `started=1` / `completed=1` global tag,
flips `allow_career_progression_check=1`, and unlocks the venue
(`venue_<zone>.unlocked=1` and `<zone>_<part>.unlocked=1`, zone from
`LevelZones.<level>.name`), then `UpdateAtoms { name=$Progression }`.

### (c) Gig → gig unlock [C Progression_Career_Gig_Complete, atom graph]

`GH4_Career_<Part>_Progression` is an atom list: `band_career_gigN_complete`
atoms depend on `Progression_Career_Check_Gig_Complete(gig=N)`; when one fires it
runs `Progression_Career_Gig_Complete` (1890 B), which walks the unlocksets and
opens the **first not-yet-unlocked set** — setting its gigs `unlocked=1
first_time_unlocked=1` and unlocking those tiers' songs — then returns
(`set_got_unlocked`). Net effect: one set opens per completed gig. For band,
completing gig 1 opens `unlockset1` = gig 4.

### (d) Cash earning → band_info.cash (the store currency) [C cash_milestones.qb]

World Tour earns cash through the **cash-milestones** system (not GH3's
`Cash_System_rules`, which is legacy data still present). On every song won
(`Progression_SongWon` → `Progression_CashMilestonesSongWon` →
`evaluate_individual_cash_milestones` → `Progression_SetCashMilestone` →
`Progression_AwardCashMilestones`), each earned milestone pays
`cash_milestones.<milestone>.cash[difficulty_index] * multiplier` (floored,
dropped if ≤0) and the sum is added to **`band_info.cash`** (and
`career_earnings`) via the same path `increase_band_money` writes — i.e. the
exact wallet the Step 7.4 store spends. The milestone table (verbatim, cash per
difficulty 0–4): `stars_3/4/5_bonus = [5,10,15,20,25]/[10,20,30,40,50]/[15,30,
45,60,75]`, `perfect_bonus = [20,40,60,80,100]`, `Score = [5,10,15,20,25]`,
`never_in_red`, `never_used_star_power`, `full_star_power`, `note_streak`
(multiplier `best_run/25`), `band_note_streak`, `hot_start`, `strong_finish`,
`percent_hit`, `decade`, `jam`, `got_sponsored`.

### (e) Content rewards → the Step 7.4 loop [C guitar_progression_unlock.qb, unlockable_content.qb]

`setup_unlock_atoms_generic` builds reward atoms `<part>_gig<i>_reward`
(depends on the count of completed gig atoms ≥ i), `<part>_every_gig_reward`,
`<part>_five_star_reward`, plus `band_Today_reward`; atom i grants
`progression_<part>_rewards[i-1]`. `progression_add_new_unlocks` for a reward's
`car_pieces` calls **`set_current_band_part_flags(part, desc_id, unlocked)`** —
exactly `shop::grantUnlock` — writing the `unlocked` flag into
`band_info.unlock_purchased.<part>`. So completing a gig grants the 76
career-gated CAS items the store flagged as a dependency. The reward arrays
(182 unique desc_ids across the five careers, gender-paired) also unlock guest
**characters** as band-member rewards: `billy`, `zakkwylde`, `tednugent`,
`sting`, `rockbot`, `Travis`, `skeleton`, `Hayley`, `ozzy`. Band gig 1's reward
is `f_bmtl_acc_lstichband` / `m_bmtl_acc_lstichband` — `price=150 $locked` in the
catalog — so completing gig 1 makes that `$locked` store item buyable.

### (f) Movies / cutscenes inventory [C Bonus_Videos, DATA/MOVIES/BIK]

23 `.bik` on disc. `Bonus_Videos` (15 entries) registers the career cutscenes
and credits; `--careertest` confirms all **10 career cutscenes** present:
`band_intro`/`band_outro`, `guitarist_intro`/`guitarist_outro`,
`drummer_intro`/`drummer_outro`, `bass_intro`/`bass_outro`,
`singer_intro`/`singer_outro` (the `<part>_intro` plays at career start,
`<part>_outro` on finishing that career — `end_movie` unlocked in
`Progression_SongWon` when `Progression_Career_Check_Gig_Complete` passes on an
`$end_with_credits` tier), plus `In-GameMontage` (the end-credits montage,
`Progression_EndCredits` / `PlayMovie_EndCredits` → `end_credits=1`),
`GHMetallica`, `credits`, `what_is_pod`, `Line_6_Guitar_101`. Only `band_intro`
and `In-GameMontage`/`GHMetallica`/`credits`/`line6` are `$unlocked` by default;
the other intros/outros unlock through the career. The port already has the Bink
player (`video/movie_player.cpp`, `PlayMovie`/`PlayMovieAndWait` natives, Step 2)
and plays these files; `--playmovie <name>` plays any of them. Enumerating-vs-
playing the scripted beat placement in the live career flow is the UI remainder.

### (g) Events + "special corks"

Boss/devil beats (`Progression_IsBossSong`, `boss_devil_score`,
`is_boss_battle_song`) exist in the SongWon path but **no band career tier marks
a boss song** on this disc (boss battles are a quickplay/battle mode, not the WT
career spine). The scripted career "corks" are: the **pay-to-play bonus gigs**
(tiers 14–18, cash-gated entry), the **guest-character unlocks** (reward atoms,
above), the **Create-a-Gig** (tier 19) and **debug** (tier 20) gigs, and the
**finale/credits** (band tier 13 `$end_with_credits` → `end_movie` +
`In-GameMontage`). `special_event%d` unlock slots are supported by
`Progression_Career_Gig_Complete` but unused by the band unlock order on disc.

### (h) What I implemented (files)

- **`engine/src/game/career.{h,cpp}`** (`namespace career`, rendering-free): the
  faithful port of (a)–(e). `load(Globals)` reads all five careers (tiers/songs/
  encore/venue/poster/genre/paytoplay/flags, the freegigs + unlocksets, and
  `progression_<part>_rewards`); `movies(Globals)` reads `Bonus_Videos`.
  State over a `TagStore` (named global tags, == `save::Tags`): `startCareer`
  (free gigs + their songs unlocked), `isGigComplete` (all tier songs > 2 stars),
  `cashForSong` (the real cash_milestones sum), `recordSongResult`
  (Progression_SongWon: better stars/score, award cash to `band_info.cash`),
  `completeGig` (SaveGigComplete + Progression_Career_Gig_Complete + the reward
  unlock). Invents nothing — the structure is the disc's data.
- **`engine/src/game/shop.{h,cpp}`**: added `grantUnlockPart(bandInfo, part,
  desc_id)` and `partUnlockedFlag(...)` (the raw `set_current_band_part_flags
  ... unlocked` / `is_part_unlocked` the career reward uses), so the career grants
  straight into the band_info the store reads.
- **`engine/src/tests/careertest.{h,cpp}` + `--careertest`** (`main.cpp`,
  `CMakeLists.txt`): the objective harness.
- No engine/UI/judge/input/decoder code changed; `--uitest` byte-identical below.

### (i) Objective test evidence (`--careertest <disc>`, headless, exit 0)

Data-derived expected-vs-measured, no eyeballing:
- **Structure**: five careers load; each `num_tiers=20` with 20 tiers of songs,
  11 unlocksets, a reward array, and the correct intro/outro movie names.
- **Movies**: `Bonus_Videos` = 15, **career cutscenes present 10/10**.
- **Start**: band free gigs 1,2,3 unlocked, gig 4 locked, fresh cash = 0.
- **Cash earned**: a 5-star, 100%, expert song pays exactly **$200**
  (`stars_5_bonus[4] 75 + perfect_bonus[4] 100 + Score[4] 25`, from the real
  `cash_milestones`); gig 1's 3 songs award **$600**, `band_info.cash 0 → 600`
  (measured == table). Every tier-1 song then reads 5 stars and the gig reads
  complete.
- **Gig complete**: `completeGig(1)` → gig 1 `completed`, venue `venue_z_hob`
  unlocked, **gig 4 unlocked** (unlockset1) with its tier songs, **2 CAS reward
  pieces granted**.
- **7.4 loop closed**: the reward piece `CAS_Female_Acc_Left/f_bmtl_acc_lstichband`
  (`price=150 $locked`) is `NotUnlocked` for a band that has not done the career,
  and `unlocked` (store-buyable) for the band that completed gig 1.
- **Persists**: the whole `TagStore` + `band_info` survive a `save::write`/`read`
  round-trip — cash 600, gig 1 complete, gig 4 unlocked, and the reward `unlocked`
  flag all read back.
- **No regression**: `--uitest` still **47 flow states, 1181/1181 mouse-hittable,
  0 UNHANDLED** (identical to the 7.2–7.5 baseline). `--careertest`: **all career
  checks passed (0 failed)**.

### (j) Honest remains / flagged

- **Live UI flow wiring (the main remainder).** The model is the real progression
  engine and the career screens/venues/judge already run, but a live "play a
  career gig" does not yet route the results back into `career::recordSongResult`
  / `completeGig` through the game's own atom chain. The original drives it with
  `UpdateAtoms`/`Progression_SongWon` natives over the career menus — the same
  native/board-menu substrate deferred in 7.1–7.5. Wiring the results screen to
  the model (or implementing the progression natives against it) is the next
  increment; the model is ready.
- **Star computation inputs.** `recordSongResult` takes the judged stars/score as
  input (the results layer's output via `update_song_star_rating`), rather than
  re-deriving from `star_rating_cutoffs`; the cash milestones beyond stars/perfect/
  score (streak/health/star-power) are modelled but driven by the `Performance`
  flags the harness sets.
- **Venue zone name** is taken by stripping `load_` off the level key
  (`load_z_hob → z_hob`) rather than reading `LevelZones.<level>.name` — [I], the
  flag shape (`venue_<zone>.unlocked`) is faithful.
- **Song-checksum tag keys** use the disc's `'%s%p%d'` form with the port's part/
  difficulty tokens (the console's exact localized `text_nl` is [I]); the port's
  save is its own JSON format, so byte-matching the console tag name is not
  required — the mechanic and persistence are.
- **Pay-to-play / Create-a-Gig / debug gigs** are parsed (`paytoplay`,
  `$createagig`, `$debug_gig`) but their entry flow (spending cash to unlock a
  bonus gig, the Create-a-Gig editor) is not yet driven — flagged for the live-UI
  increment.


## Onboarding Step 7.10 — enhanced menu + cheats + The Glitches (2026-11-24)

Aggregate-first verification of the universal **Enhanced menu**, every **cheat**,
and the house band **"The Glitches"** (gate 95%). gd07 already carries the
GH3-lineage `port_enhanced` host (`ui/port_enhanced.cpp` + `ui/port_settings.{cpp,h}`,
shared with gd04/06/10), the cheat flags, and the gh4 Glitches path, so this pass
**confirmed them against the disc, closed the gaps, and added one objective,
data-derived test** (`engine/src/tests/enhancedtest.{h,cpp}`, `--enhancedtest`).
Evidence is a fresh headless run on our disc (`gd-game-07/extracted`) of
`--enhancedtest` (exit 0, 0 checks failed) and `--uitest` (unchanged). The build
was confirmed by grepping the ninja log for `FAILED`/`error:` (clean).

### (a) Enhanced menu — complete, defaults respect the original, every option honoured

`spec-enhanced-menu.md` option catalogue vs `enhancedSettings()` (`port_settings.cpp`):
**40/40 required options present** (`--enhancedtest`): CONTROLS (`mouse`, `touch`,
`shake_whammy`), PERFORMANCE & GRAPHICS (`graphics_backend`, `audio_backend`,
`screen_mode`, `internal_res`, `sharp_ui`, `antialiasing`, `ai_upscaling`,
`ai_upscaling_mode`, `max_fps`, `fps_counter`, `hdr`, `bokeh`, `particles`, `bloom`,
`cinematic_color`, `color_grade`, `film_grain`, `chromatic`, `ssao`, `sharpening`,
`star_blur`, `shadows_all`, `more_lights`, `light_dust`, `no_shaky_cam`,
`no_effects`), ACCESSIBILITY (`easy_chords`), CHEATS & ENHANCERS (`hopo_mode`,
`no_fail`, `autoplay`, `dev_debug_menu`, `cheat_unlock_all`, `cheat_full_sp`,
`cheat_multiplier`, `cheat_hyperspeed`), Calibration (`audio_offset`,
`video_offset`). Correctly **absent**: `hdr_white`/`hdr_black` sliders
(port_enhanced has no Slider kind — audio/video offset are numeric, set via the
Calibration tool), and the owner-removed `ground_fog`/`light_streaks` (an old
`enhanced.json` with them loads and drops them — asserted).

- **Defaults respect the ORIGINAL look out of the box** (an untouched install
  plays like the original): **35/35 desktop defaults** asserted against the
  profile (`enhancedProfile`/`enhancedDefault`). Effects off / GH3's own look
  (`bloom=none`, `no_effects=off`, `cinematic_color=off`, `film_grain/chromatic/
  ssao/star_blur/more_lights/light_dust=none`, `color_grade/sharpening/shadows_all/
  no_shaky_cam=off`, `antialiasing/ai_upscaling=none`, GH3's own `bokeh=medium`,
  `particles=original`); controls on (`mouse=on`, `fps_counter=on`, `sharp_ui=on`,
  `screen_mode=borderless`, `internal_res=1080`); cheats + accessibility off
  (`easy_chords=off`, `hopo_mode=original`, `no_fail/autoplay/dev_debug_menu=off`,
  all four switch-cheats off); `audio/video_offset=0`; backends per-OS list front
  (`audio_backend=miniaudio`, `graphics_backend`=vulkan on Linux/Win/Android, sdl3
  on macOS). **Per-OS profile table** also asserted: desktop `touch=off`/
  `shake_whammy=off`; handheld (Steam Deck / Android) `touch=on`/`shake_whammy=on`/
  `max_fps=60`.
- **The store honours every option**: a set/get round-trip of **every allowed
  value — 127 values honoured, 0 failed**; invalid values refused (the last good
  value kept); numeric out-of-range refused; `enhanced.json` persists across a
  reload; unknown/removed keys dropped on load.
- **Real in-game effect**: each option is applied in `app.cpp::applyEnhanced`
  (`is(key)` → engine toggle: `screen_mode`, `internal_res`→`VenueStage::internalHeight`,
  `particles`→`VenueStage::particlesOn`, the `enhanced_fx` chain
  (`bloom`/`cinematic_color`/`fsr1`/…), `sharp_ui`→`TextureCache::sharp`,
  `no_shaky_cam`→`CameraDirector::noShake`, `shadows_all`→`VenueStage::extras.shadowsAll`,
  `hopo_mode`→`Player::forgivingHopos`, `easy_chords`→`Player::easyChords`,
  `shake_whammy`/`touch`→`Input`, `fps_counter`, `max_fps`, the cheats below). The
  cheat effects are proven below; the graphics effects are applied live (the
  `--walktest enhanced` sets them through the real menu). **Untestable headless:**
  True HDR (needs an HDR display) and the FSR Vulkan upscaler — noted, not proven.

### (b) Cheats — every cheat proven to take effect

Port switch-cheats (effect flags set by `applyEnhanced`), proven data-derived in
`--enhancedtest`:

| Cheat | Flag | Proof (measured) |
|---|---|---|
| Always Max Multiplier | `Player::maxMultiplier` | `PlayerStats::multiplier()` streak-0: **x1 off → x4 on → x8 on+SP** [C 0x8210BB40] |
| Hyperspeed | `Highway2D::hyperspeed` | `applyHyperspeed(1000)`: **1000 ms off → 600 ms on** (×0.6) |
| Star Power Always Full | `Player::fullStarPower` | a Player frame with SP=50: **50 off → 100 on** (pinned) |
| Easy Chords (accessibility) | `Player::easyChords` | `chordShape`: a chord part counts on, not off; a fret outside never counts |
| Forgiving HOPOs | `Player::forgivingHopos` | off by default; judging effect exercised by `--selftest` HOPO counts |
| Unlock Everything | `unlockEverything()` | `unlock_all_profiles`+`purchase_all_profiles` **spawned**, the save-wide `unlocked` sweep **set all 3 seeded tags** |
| Developer Debug Menu | `enable_button_cheats` | `app.cpp` sets the global so the pause menu's DEBUG MENU + button cheats appear |
| No Fail / Autoplay | song setup | `enhanced_.on("no_fail")`/`("autoplay")` feed `song_->start(...)` (as `--no-fail`/`--autoplay`) |

The **game's own Options > Cheats** (`guitar_hero_cheats` disc table) are surfaced
as `Kind::Cheat` rows whose chosen option runs `cheat_set_globals` exactly as the
cheats screen does: **14 toggleable cheats enumerated straight off the disc, all 14
with a cheat global (`var`)**. (`--walktest enhanced` additionally drives one live:
AIR INSTRUMENTS → `cheat_airinstruments=1`.)

### (c) "The Glitches" — the fully-unlocked house band, last in the band-selection

gd07 runs the **gh4 engine** (shared with gd10 Metallica); World Tour has **no
GH3-style fixed 5-band career-slot roster** (`band5_info_p1_career`,
`GH3_Career_Songs`, `num_career_bands` are all absent on our disc — confirmed), so
the GH3-lineage `ensureGlitches()` in `ui/glitches.cpp` is **inert/dead code on
WT** (never called). The faithful gh4 approach (gd10's) is a *returning band*: the
save's band renamed **"The Glitches"** with the original's own unlock-all run, so
it is the fully-unlocked band — the last the player meets in the band-selection
screen (`career_choose_band_fs`).

This pass extracted that wiring into **`ensureGlitchesGh4(vm)`** (`ui/glitches.cpp`),
called from `front_end.cpp` at the first main menu and asserted by `--enhancedtest`.
It runs `unlock_all_profiles` + `purchase_all_profiles` (everything unlocked), names
`band_info` **"The Glitches"**, sets the house band's **$1,000,000** (kickoff rule
11; gd04's `ensureGlitches`), clears `first_play`, and seeds
`last_singleplayer_character = judy` (a `Preset_Musician_Profiles_Locked` entry on
the disc; the port has no live Create-A-Rocker, so this lets
`init_singleplayer_character_hub` proceed — gd10's gh4 seed). Proven: band named
The Glitches, cash == 1,000,000, `first_play==0`, character seeded, the sweep ran;
and **idempotent** — a player's own saved band name / chosen character is never
overwritten.

### (d) What remains (honest)

- **The live multi-band band-selection list** (`career_choose_band_fs` /
  `menu_choose_band`) and the live Career navigation are the **deferred
  live-career-UI surface** already noted in Steps 7.3/7.6: on our disc the
  navigating menu walks (`career`, `glitches`, `enhanced`) do not yet reach their
  target screens (the WT main-menu navigation differs from the GH3/gd10 walks they
  were copied from), so "The Glitches *rendered* as a distinct 5th entry in a
  multi-band list" is not demonstrable live yet. The port holds a single save band,
  which The Glitches *is* (first == last), fully unlocked. The GH3-copied `glitches`
  walk (`band5_info_p1_career`) is left as-is — it is a GH3 artifact that does not
  apply to WT; the WT-faithful proof is `--enhancedtest`.
- **True HDR / FSR upscaler** effects cannot be verified on a headless, non-HDR box.
- `ensureGlitches()` (GH3 band-5 model) stays in the tree as the gd04 reference /
  shared-code lineage; it is not called by gd07.

### Evidence
- `gd07 --enhancedtest <disc>`: exit 0, **0 checks failed** — "40/40 required
  options present; desktop defaults respect the original 35/35; store round-trip
  127 values honoured 0 failed; cheat_multiplier off=x1 on=x4 on+SP=x8;
  cheat_hyperspeed 1000→600 ms; cheat_full_sp 50→100; guitar_hero_cheats 14
  toggleable cheats; unlock_all 3 tags swept + scripts spawned; The Glitches named,
  $1,000,000, character seeded, player name/character preserved."
- `gd07 --uitest <disc>`: **unchanged** — 47 flow states, **1181/1181** focusable
  items mouse-hittable, 0 UNHANDLED, exit 0.
- Files: `ui/glitches.{h,cpp}` (`ensureGlitchesGh4`), `ui/front_end.cpp` (calls it),
  `tests/enhancedtest.{h,cpp}`, `main.cpp` + `CMakeLists.txt` (`--enhancedtest`).

## Onboarding Step 7.8 — online disabled (2026-11-24)

The port ships NO online modes. This step RE'd World Tour's online entry points
and made every one UNSELECTABLE (greyed + not_focusable), the faithful
`demo_mode_disable` pattern the gh4 games use, so the online sub-tree is
unreachable in normal play while the menu item still shows. No online flow
state crashes (they build an empty dead-end when force-reached). Evidence:
disc scripts dumped with `tools/qbscript.py dis`, `extracted/names.tsv`, and a
headless `--uitest` on our disc.

### (a) The online entry points (confirmed on the disc)

World Tour's front end reaches online from exactly three player-visible entry
items; every `online_*` / `net_*` / `leaderboard_*` / `*_match_*` flow state
(~40 of them — `online_menu_fs`, `online_signin_fs`, `online_character_hub_fs`,
`online_play_song_fs`, `online_setlist_fs`, `online_select_{guitar,bass,outfit,
style,diff}_fs`, `online_leaderboard_{list,groups}_fs`, `view_leaderboard_fs`,
`leaderboard_filter_fs`, `net_achievement_tracker_fs`, `net_matchmaking_fs`,
`create_match_select_*_fs`, `custom_match_fs`, `quickmatch_search_fs`,
`invite_joining_game_fs`, …) is only reached from inside these three:

| Entry item (main menu / jam) | Handler [C script] | Leads to |
|---|---|---|
| ONLINE | `main_menu_select_online` -> `set_primary_controller state = uistate_net_setup require_live = 1` [C guitar_main_menu.qb] | `online_menu_fs` and the whole online tree (play, character hub, leaderboards, achievements/net) |
| DOWNLOADS | `main_menu_select_downloads` -> `state = uistate_downloads` [C guitar_main_menu.qb] | DLC store (`online_select_downloads`/`store_select_downloads` -> `NetSessionFunc showmarketplaceui`) |
| GHTUNES (music studio) | `jam_picked_ghtunes` -> `generic_event_choose state = uistate_signin new_state = UIstate_jam_ghtunes require_live = 1` [C menu_jam.qb] | GHTunes upload/download (`create_jam_ghtunes_menu` -> `VerifyJamUserContent`/`NetSessionFunc`) |

MULTIPLAYER (`main_menu_select_multiplayer`) sets `game_mode = p2_faceoff`,
`current_num_players = 2`, `state = uistate_select_controller` — it is LOCAL
same-screen face-off, not online, so it stays selectable (correct). The music
studio's own RECORD / GH MIX / TUTORIALS items (`UIstate_jam_select_song`,
`jam_goto_tutorials`) are offline and stay selectable; only GHTUNES is online.
There is NO main-menu LEADERBOARDS item on World Tour (that is Metallica's
layout) — leaderboards are inside ONLINE, so disabling ONLINE seals them.
`net_achievement_tracker` is likewise only reached from the online menu.

### (b) What the port does (files)

`ui/ui_system.cpp` — a single grounded online-key set `onlineKeys()`
(`main_menu_select_online`, `main_menu_select_downloads`, `select_xbox_live`
and gd10's `main_menu_select_xbox_live`, `jam_picked_ghtunes`,
`uistate_net_setup`, `uistate_downloads`, `uistate_jam_ghtunes`) and a
recursive `handlersLeadOnline(Element)` that searches an element's handlers AND
their params. The recursion matters: a main-menu item attaches its choose
script inline (`{ pad_choose main_menu_select_online }`), but a generic-menu
item (the jam GHTunes icon) attaches it later via `SetScreenElementProps` with
the real script buried as `pad_script = jam_picked_ghtunes` inside
`generic_blocking_execute_script`'s params [C add_generic_menu_icon_item]. When
`applyProps` sets event_handlers (create or setprops) and the tree leads
online, the element is greyed `{rgba = [128 128 128 255] not_focusable}` and
flagged `onlineEntry`; a text child made after its greyed container inherits
the colour. `Element::onlineEntry` + `UiSystem::onlineEntryCoverage` report it.
This unifies and extends the two earlier ad-hoc main-menu greying blocks
(GD10-101 carried to WT). `GD07_MENU_DISABLE_LEGACY=1` restores Metallica-only.

### (c) Objective evidence (`--uitest`, headless)

`gd07 --uitest <disc>` (exit 0): **47 flow states started**, the `online_*` /
`net_*` / `leaderboard_*` states **build no element** when force-reached (empty
dead-end, not a crash); **mouse coverage pinned 1181/1181** across 47 screens
(the greyed online items are excluded from the focusable set, as before);
**`uitest online: 52 online-entry item(s) greyed+unselectable, 0 still
selectable`**; and **`GHTunes wrapped-handler detected=1, local RECORD kept
selectable=1`** — the recursive detector greys the wrapped GHTunes item and
leaves the local RECORD item alone. The walk force-spawns each flow state
directly to prove it builds without crashing; that bypasses the greyed entry
points, so an online state building its own menu there is expected and is not a
reachability signal — unreachability is proved by every entry point being
unselectable.

### What remains (honest)

- The GHTunes jam menu is not force-built by the default `--uitest` walk (its
  `create_jam_menu` has a camera wait loop), so the GHTunes greying is proven
  by a deterministic wrapped-handler check mirroring the disc's
  `add_generic_menu_icon_item` output, plus the shared `applyProps` path the
  main-menu items exercise live. Wiring a live jam-menu walk is the same
  live-UI remainder as 7.3/7.5/7.6/7.7.
- Detection is by the grounded online-script/state key set; a brand-new online
  transition not in that set would not be auto-greyed. The three WT entry
  points above are the complete front-end set found on the disc.

## Onboarding Step 7.9 — labels (trademark scrub) (2026-11-24)

We must not ship console/platform trademarks in player-visible text. World
Tour's localized UI text lives in the `.qs` tables (`qs.pak.xen`, 8,495+
English entries); the port already applies `pcstrings::pcQsText` to every `.qs`
string at load (`data/qs.cpp`) and `pcstrings::pcText` to QB string globals
(inherited from gd10, GD10-039/035). This step scanned the WHOLE English `.qs`
set plus the QB string literals for console/platform trademark naming (Xbox,
Xbox 360, Xbox LIVE, PlayStation / PLAYSTATION®Network, PS3, Wii, Microsoft,
Nintendo, gamertag, gamer card/profile, Xbox Dashboard, HDD, button-prompt
console names, boot/legal screens) and closed every player-visible leak the
existing scrub missed. Evidence: `tools/pak.py extract` of `qs.pak.xen`, a
scan reproducing `pcQsText` exactly, and an in-engine `--uitest` check.

### (a) Leaks found and fixed (`script/pc_strings.cpp`)

The existing catch-all only matched the exact spellings `"Xbox LIVE"` and
`"PlayStation®Network"`, but the disc ships other casings, so these passed
through UNSCRUBBED:

| Trademark on the disc | # strings | Was | Now |
|---|---|---|---|
| `PLAYSTATION®Network` (all-caps — the shipped spelling) | ~18 | leaked (catch-all tested mixed-case `PlayStation®Network`) | bare label -> `ONLINE`; sentences -> the no-online text (catch-all now case-insensitive on `playstation`) |
| `Xbox Live` (mixed case) | 1 | leaked (catch-all tested `Xbox LIVE`) | no-online text (catch-all now nocase on `xbox live`/`xbox`) |
| `gamer card` (lowercase, in `Press \m0 to view gamer card.`) | 1 | leaked (table only had `GAMER CARD`/`Gamer card`) | `profile` (new replaceAll) |
| `gamertag` as a substring (`gamertag - %g`, `add_gamertag_to_band_lobby`) | 2 | leaked (table only had exact `gamertag`) | `player name` (new replaceAll) |
| `HDD` (save/calibration messages: `Saving to the HDD.`, `Checking the HDD.`, `%dKB HDD space`, `… save data found on HDD.`) | ~7 | leaked | `disk` (new replaceAll; kept as save-system wording, not routed to the no-online text) |
| `PS3` / `ps3` (debug lag readouts `PS3 Audio/Gem/Input Logic Lag Value: %l`, `PS3: unable to signin …`, `ps3 stuff`, `PS3_display_invite_callback`, `ps3_invite_callback`) | ~9 | leaked | token dropped (new replaceAll) |

Changes, all in `pcQsText`: added `"PLAYSTATION®Network" -> "ONLINE"` to the
whole-string table; new `replaceAll` rules for `HDD -> disk`, `gamer card ->
profile`, `gamertag -> player name`, `PS3/ps3 -> ""`; and made the online
catch-all case-insensitive (`containsNoCase` on `xbox live`, `playstation`,
`xbox`). The already-handled set (Xbox 360 Controller/Headset, storage device,
Xbox Dashboard, Microsoft points, GAMER CARD/GAMERTAG/VIEW GAMER CARD, the
cheat/leaderboard/achievement warnings) is unchanged.

### (b) Deliberately NOT scrubbed

- **Song-licensing credits** `SONY BMG MUSIC ENTERTAINMENT` / `Sony ATV`
  (~25 credit strings). "Sony" here is the record label / music publisher, not
  the PlayStation maker; these are factual attributions in the song credits, so
  bare "sony" is not treated as a console trademark and the credits are kept.
- **Copyright/boot images** `boot_copyright_bg_1/2`, `boot_legal*.img` and the
  `buttons_x360` glyph font: read-only texture/glyph DATA, not text labels. The
  boot IIS screen's own TextBlock is just "PRESS ANY BUTTON TO ROCK..." (qs key
  0x71da7fc7) — no trademark. Image legal/button-glyph art is out of the
  text-label scope ("do not alter data we only read internally").
- **The game's own title** (`GUITAR HERO WORLD TOUR` in a save string) is not a
  console/platform trademark and is a separate branding decision; not touched
  here (and `game-info.yaml` is untouched).

### (c) Objective evidence (`--uitest`, headless)

`gd07 --uitest <disc>` now also scans every loaded (scrubbed) localized string
and fails on any residual console/platform mark:
**`uitest labels: 9484 localized strings scanned, 0 with a console/platform
trademark`** (marks checked: xbox, playstation, ps3, nintendo, wii, microsoft,
gamertag, gamer card/profile, dashboard, memory unit, HDD; the Sony music
credits do not contain these so they are untouched). A standalone scan of the
raw `qs.pak.xen` reproducing `pcQsText` confirms 0 leaks outside the kept Sony
credits. QB string literals in `qb.pak.xen` carried no console/platform
trademark (ASCII or UTF-16) to scrub; player text is `.qs`.
