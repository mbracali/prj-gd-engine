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
