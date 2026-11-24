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
