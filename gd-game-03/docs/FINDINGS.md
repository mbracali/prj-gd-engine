# gd03: gameplay findings

Rules of Guitar Hero Encore: Rocks the 80s, as SLUS_215.86 implements them.
[C] = confirmed at the PS2 address given; [C 360] = gd-game-02's GH2 Xbox 360
finding, still to be re-confirmed in SLUS_215.86 (the engine is the same
generation); [I] = inferred. The rules engine is gd-game-02's
(`engine/src/game/`); this file records what has been re-checked on the PS2.

## Beatmatch
- Strum filter: a strum 52 ms or less after the last accepted one is dropped
  (`last + 52 < now` accepts) [C 0x002356f8] (the 360's 0x8233aa80 does the
  same). Consequence: runs of notes 48.7 ms apart (ballroomblitz, expert) can
  only be played with hammer-ons; `--selftest`'s strum-only bot allows those
  misses.
- Hit window, HOPO window, hopo_threshold: data driven (`config/gen/beatmatcher.dtb`:
  slop 100 ms, hopo_threshold 170 ticks, per-song thresholds in songs.dtb) [C data].

## Scoring and co-op
- Co-op stars use `config/gen/coop_max_scores.dtb` (per song, four values) [C data].

## Still to re-confirm on the PS2 (inherited from gd02)
Star Power (phrases, meter, deploy), multiplier, rock meter, sustain and
whammy accrual, face-off and co-op rules: see gd-game-02/docs/FINDINGS.md.

## Rules re-confirmed on the PS2
Checked against SLUS_215.86 where the GH2 (360) port differed; each is a
`--selftest` rule check ("PS2: ..."). Re-read in assembly (GD03-029, 0.0.3):
the strum expiry (0x0024deb0: `c.lt.s deadline, now`), the signed accrual
(0x0012a168: progress clamped to 1 x points, rounded half away, stored at
+8, the difference returned) and the penalised-gem mark (GD03-015); the
others are read from the decompiler.
- Held notes: the watcher holds every gem whose duration_ms is not 0, short
  notes (160 ticks or less) too [C 0x00248910; the parser never clears the
  length, 0x00236428]. Its end time, a miss or a new hit drop it silently
  [C 0x00248c80, 0x002495d8, 0x002490f8]; a release (a fret let go, a fret
  pressed above it, any joypad press) tells the player [C 0x00249038,
  0x0024e050].
- The player's release (0x00111810) finishes the open sustain record only when
  it belongs to the released gem; otherwise the record is thrown away [C].
- A hit accrues the open record up to the new gem's start_ms and finishes it
  only when complete; a new sustain replaces an unfinished record
  [C 0x001111d0, 0x00112280, 0x0012a0e0]. Accrual adds the signed difference
  [C 0x0012a168].
- A penalty finishes the record with the last poll's points [C 0x00112348].
- Pass of a phrase's first gem: phrase miss [C 0x00111358]. Phrase miss: a
  gem outside every phrase, or a phrase's last gem, sets the state to 0
  [C 0x0011e6a8].
- Wrong strum: the after-HOPO flag is `hopo-hit gem == j - 1` with no guard
  (a wrong strum on gem 0 with no HOPO hit expires silently) [C 0x0024d560];
  it expires when `deadline < now` [C 0x0024de90].
- Joypad: a press for a gem other than the waiting chord drops the chord with
  a miss, then is judged itself; the chord continues with no deadline test;
  the window expires in the poll [C 0x0024e050, 0x0024e2d0, 0x0024e450].
- No eight_x on the PS2 [C: 0x0010f910 / 0x0010fa98, no string].
  recharge_rate is read (0x0011e1e0) but 0.0 in scoring.dta.
- A hit on a gem below the highest penalised one lowers that mark (+0xac =
  gem when gem < +0xac, 0x00111254) [C], so a later gem already penalised by
  an overstrum is penalised again when it passes.
- Face-off HOPOs: the gem lists are per difficulty and track, not per player.
  A new track adds a list to every difficulty [C 0x002447a8]; the parser
  sends each gem with its difficulty index [C 0x00245f28, 0x00246668]; the
  sink takes that difficulty's lists, then the gem's track (gem +0)
  [C 0x002448d0, 0x0024bba8]; every gem is appended whatever its player
  bits, and the HOPO test never reads them [C 0x0024d130, 0x0024d3a0]. So a
  face-off player's HOPO looks at the previous gem of the whole chart, the
  other player's gems included (the engine already did this).
- Who may fail: the crowd update fails a performer only when the game is not
  "num_players > 1 and competitive" [C 0x0010fc78, 0x001260a8, 0x00126098]
  and its vtable +0xa4 allows it [C 0x0010fe30]. Player and co-op player
  (vtables 0x003e1918 / 0x003e28c8): `competitive ? number == 0 :
  num_players == 1` [C 0x00112890, the same as 360 0x822C6B98]; the co-op
  band is a plain Performer (0x001103c8 -> 0x0010ee90, vtable 0x003e17c0),
  whose +0xa4 is TRUE [C 0x0010f310]. So the solo player and the co-op band
  can fail; co-op players and both face-off players never can (the engine's
  callers already pass no-fail for them; `Player::mayFail`). The crowd must
  also be active (+0x44 [C], which looks like crowd_rating_active, FALSE in
  practice and the tutorials [I]).
- The whammy gate +0xbc: 1 from the player constructor [C 0x001107d8]; only
  the script method set_whammystarpowerenabled changes it
  [C 0x001131a0 -> 0x00111cd8], and only tutorial_22_star_combos calls it
  (FALSE). With it off the whammy poll leaves the Star Power whammy flag
  alone [C 0x00111d88]. Its neighbour +0xb8 (set_mercuryswitchenabled,
  0x00111ce0, the tutorials 22-24) gates deploying in engage [C 0x00111940].
  The engine's player has both gates, routed from the script
  (ui/natives_game.cpp -> UiHost::setPlayerSwitch).

## Tutorial switches and the engage message
- `set_whammystarpowerenabled` (+0xbc) and `set_mercuryswitchenabled` (+0xb8),
  both 1 at construction (0x001107d8), set through 0x001131a0 -> 0x00111cd8 /
  0x00111ce0 [C]; tutorials 22-24 turn them off. With the mercury switch off an
  engage stores the engaged flag but deploys nothing (0x00111940) [C].
- `sp_engage`'s value is "mercury on, engaged, not active and ready", computed
  after any deploy (0x00111940) [C]: co-op's first engage reports 1 while the
  partner is awaited; a deploy reports 0.
- crowd_rating_active (GD03-028): the performer's +0x44 is the mode's
  crowd_rating_active at construction (0x001107d8; modes.dta FALSE in
  practice and the tutorials) and the script's set_crowd_rating_active
  (0x0010fe90, the cheats' crowd meter toggle and shuttle). While it is off
  the crowd update does nothing at all (0x0010fc78): the meter holds and
  nobody fails [C]. set_crowd_rating sets the meter (tutorial_16) [C]. The
  port: Player::setCrowdActive / crowdActive, selftest "PS2: with
  crowd_rating_active off ...". Practice's HUD (hud_practice.milo) has no
  meter.

## Audio options and effects (GD03-029 / GD03-031, 0.0.3)
- Stereo / mono (options set_stereo 0x0010dbc0 -> 0x0010dc00) [C]: mono is
  not a fold of the mix. The synth gets the mono pan mode (0x00225cd8 ->
  0x0021e490: stored, every voice marked to be panned again) and the song's
  channels are panned to 0 instead of their own pan (0x00123110 ->
  0x0023a4c0 -> 0x0023ab90, vtable +0x8c with `stereo ? pan : 0`). So every
  source plays from the centre, at the centre gains of the pan laws (the
  song's: gd01's law, 0 = both sides at full [I: the IOP side, LGAUD /
  SDRDRV.IRX, not read]). The port did "(L + R) / 2" on the output, 6 dB
  low for a stereo song's hard-panned pair; now audio/mixer.cpp and
  audio/sfx_player.cpp pan to the centre while audiodev::mono().
- SPU2 reverb: what the code holds is [C]: the presets (LIBSD.IRX .data
  0x4DE0), the work areas, depth, delay and feedback through SynthPS2's
  conversions (0x00225D50, 0x0022CDD8..0x0022D100). The reverb network
  itself is the SPU2 chip's, in no code on the disc, so it stays from the
  hardware documentation [I by nature].

---------------------------------------------------------------------------------------------------
## 2026-11-23 — Issue #123: pausing in the tutorial killed the controller

**Symptom (reported):** pressing Start during a tutorial lesson brought up the
pause overlay, but the controller then did nothing — you could not select
Resume/Restart/Quit, so the tutorial was stuck.

**Original behaviour (ui/gen/tutorials.dta, confirmed from the shipped data):**
- `tut_pause_screen`'s `BUTTON_DOWN_MSG` eats every button (returns `TRUE`)
  while its local `[paused]` is `FALSE`, and only lets the press reach the
  focused component (Resume/Restart/Quit) once `[paused]` is `TRUE`.
- `[paused]` is set `TRUE` by the screen's `pause` handler, which its `enter`
  calls only when `{== {tutorial get state} ""}`.
- The `tutorial` object (`new TutorialPanel tutorial`) runs lessons with its
  `[state]` driven by a small machine: `loading` -> `wait_for_song_load` ->
  `start_lesson` -> `""`. During actual lesson play `[state]` is `""`, so the
  pause arms and input flows to the menu.
- Crucially, that object's definition declares the property **twice**:
  `(state "")` near the top (tutorials.dta line ~113) and a trailing
  `(state intro)` at the very end (line ~743). Harmonix's `DataArray::FindData`
  returns the **first** match, so `{tutorial get state}` reads `""`.

**Port discrepancy:** `ScriptObject::getProp` resolved a property read through
`handler()`, whose list is built back-to-front so later-added handlers win
message dispatch (correct for overrides) — but that made a property read return
the **last** duplicate, `(state intro)`. So `{tutorial get state}` returned
`intro`, the `pause` handler was never called, `[paused]` stayed `FALSE`, and
`tut_pause_screen`'s `BUTTON_DOWN_MSG` swallowed every press -> dead controller.
Verified at runtime (`tutorials_screen` -> tut_1 -> pause): `tut.state='intro'`,
`scr.paused='0'`, the Resume press logged "handled by screen button_down" and
nothing happened.

**Fix (engine/src/script/script.{h,cpp}):** added `ScriptObject::propDef(key)`,
which scans `handlers_` in reverse to return the **first-defined** `(key value)`
array, matching `DataArray::FindData`. `getProp`/`hasProp` now use it for
property reads; message dispatch (`handler()`, last-wins) is unchanged. Objects
that define a property once are unaffected (first == only); only duplicates flip
to the original's first-match semantics.

**Objective before/after (offscreen, gh80s.chd, Start->pause->Resume):**
- before: pause overlay up but `paused=0`; the Resume `confirm` produced no
  screen change (stuck on `tut_pause_screen`).
- after: `paused=1` while the overlay is up; the Resume `confirm` pops back to
  `tut_script_screen` with in-game input re-armed (`inGame=1`).
- Regression checks: `--scripttest` ok, `--uitest` 26/0, `--walktest pause` OK,
  `--walktest career` OK, 187 .dtb scripts decoded / 0 failed. (`--selftest`
  still fails only its pre-existing `internal_res` 900-vs-1080 platform-defaults
  assertion, which reads the hardcoded C++ `platformDefault` and is unrelated to
  this change.)

## Controls audit (2026-11-24)

Self-contained review of input handling, requested because the controls feel
very fragile. GH Encore: Rocks the 80s runs the **GH2 engine on PS2** (MIPS,
SLUS_215.86): its gameplay-input *rules* are the GH2 family's, but it reads a
**PS2 controller** through Sony's `libpad` (the SIO2/padman protocol), not
XInput. [C] = confirmed at the PS2 address given (image/file offsets, from
`ghidra/analysis/`); [C 360] = confirmed in gd02's GH2 Xbox 360 audit for the
same engine generation; [I] = inferred; [data] = read from the shipped `.dtb`.
Scope split: the gameplay beatmatch (strum/HOPO/whammy *rules*) is the shared
gd02 rules engine (`engine/src/game/`) and is **faithful**; the fragility is in
the desktop device→action layer (`engine/src/input/input.{cpp,h}`), which is a
much larger SDL abstraction than anything the PS2 had.

### (a) PS2 GH80s input, per area, with evidence

**Pad read / protocol.** `libpad` is statically linked (`PsIIlibpad 3020`, the
version strings live at file offsets 0x45fef0..0x45ff90). Pad ports are opened
through `scePadPortOpen` (FUN_002f0fb8, the "pad port is already open" /
"buffer addr is not 64 byte align" diagnostics), driven from the platform pad
init FUN_002aae48, which zeroes a 6-slot pad state block (loop over +0x148..
+0x14e, fills 0xff) and installs the pad module via FUN_002f0c08 (the
"libpad.a / padman.irx version mismatch" check). Per-frame reads go through the
`libpad` read/state helpers in the 0x2f11a0 cluster. The pad buffer is the
standard DualShock report: a 16-bit active-low button word (SELECT, L3, R3,
START, D-pad U/R/D/L, L2, R2, L1, R1, Triangle, Circle, Cross, Square) plus the
analog-mode bytes (RX, RY, LX, LY and, in full/pressure mode, per-button
pressure). There is **one** pad protocol — no per-device heuristics — because
every controller is a PS2 pad. [C 0x002f0fb8, 0x002aae48, 0x002f0c08; libpad
strings 0x45fef0]

**Controller model split (the "watcher").** The engine builds a
`BeatMatchController` (ctor FUN_0038a370) that is one of two concrete watchers,
exactly the binary guitar-vs-pad split from the other audits:
`GuitarController` (vtable at 0x0038bc40 — strum bar plus latched frets) or
`JoypadController` / `JoypadGuitarController` (vtables 0x0038bcd8 / 0x0038bd70 —
every fret press is itself a strum). A `BeatMatchControllerSink` (0x0038ae90)
receives the decoded fret/strum events. [C vtable RTTI strings 0x436820,
0x436ac8, 0x436d38, 0x435e50]

**Guitar vs DualShock distinction.** GH80s distinguishes the PS2 Guitar Hero SG
controller from an ordinary DualShock by the controller *type name* it resolves
for the connected pad, not by any analog fingerprint. FUN_0010df80 interns the
two guitar type names `ro_guitar` (string 0x3f4140 -> DAT_00464e30) and
`ro_guitar_xbox` (0x3f4150 -> DAT_00464e38) once, then compares the connected
pad's resolved type id against them; a match selects the guitar watcher and its
`beatmatch_controller.dta` fret slots, anything else falls through to the joypad
watcher. The same function performs the per-type fret-slot remap (e.g. slot
1<->6-ish and the lefty 0xc<->0xe swap) so yellow/blue and the lefty order come
out right. Note the shipped diagnostics "New Controller: %s only available on
PC" / "NewController: Bad controller type %s" (0x435af8, 0x435b20): the Xbox
guitar types are a PC-engine concept; on the PS2 disc the controllers are the
PS2 ones. [C 0x0010df80; strings 0x3f4140/0x3f4150]

**5 frets + strum up/down.** The guitar watcher latches the five fret bits and
fires a strum on the strum-bar edge (D-pad up or down). The decoded strum is
gated by a **52 ms refractory filter** before it reaches the sink: the handler
accepts only when `lastAcceptedStrumTime(+0xc4) + 52.0 < now(+0xa4)` (times in
ms), then sets +0xc4 = now and forwards the *current* fret mask (+0xa0) to the
gem check (FUN_00247fa8). So the fret state is sampled **at the strum instant**,
and strums closer together than 52 ms are silently dropped. [C 0x002356f8; the
360's player sink 0x8233aa80 does the same]. Consequence already noted in this
file: 48.7 ms note runs (ballroomblitz expert) are only playable as hammer-ons.

**HOPO / hammer-on timing.** Data driven, not hard-coded: `beatmatcher.dtb`
gives slop 100 ms and hopo_threshold 170 ticks, with per-song overrides in
`songs.dtb`; the chart parser reads `hopo_threshold` (near 0x438ee0) and the
beatmatch poll applies it (FUN_00243de8 and the gem-check cluster around
0x0024d130..0x0024e450). The HOPO "previous gem" test ignores player bits, so in
face-off it looks at the previous gem of the whole chart. [C data; C
0x0024d560, 0x0024de90, 0x0024e050]

**Whammy (analog).** The PS2 guitar's whammy is an analog axis read as a float
position, but the engine cares about **movement, not position**: whammy counts
as "moving" when `|Δvalue / Δms| > whammy_speed × 0.01` (0.0005 per ms for the
shipped whammy_speed), and the whammy flag stays on for `whammy_timeout` (500 ms)
after the last qualifying move, but only while a sustain in an armed/captured
Star-Power phrase is held. Config is loaded by FUN_0011bd20 from the `gems.dta`
"guitar" block (keys whammy_rate 0x3f6ed0, whammy_speed 0x3f6f20 stored ×0.01,
whammy_timeout 0x3f6f30 stored ×1000); the poll is FUN_00111d88. With the
per-player whammy gate +0xbc off (tutorial_22 only) the poll just records value
and time. The raw 0..1 value range is [I]. [C 0x00111d88, 0x0011bd20]

**Tilt / Star Power deploy.** Deploy is `force_mercury kPad_Select` — the Select
button — present in every controller type [C 360 `beatmatch_controller.dta`].
On the PS2 SG guitar the "tilt" is a mercury tilt *switch* reported as an
ordinary pad bit, i.e. a digital deploy, **not** a calibrated analog-tilt
threshold: there is no tilt/accel/sensitivity string anywhere in the image, and
no per-controller tilt calibration path. Deploy is gated by the mercury switch
+0xb8 (`set_mercuryswitchenabled`, tutorials 22-24 only) and, in `engage`,
stores the engaged flag and deploys if armed (FUN_00111940). So on GH80s PS2,
Star Power is button/switch-driven and instant; the "tilt after N calibrated
frames" model from the guitar-only games does **not** apply here. [C 0x00111940,
0x001131a0 -> 0x00111cd8/0x00111ce0]

**Start / Select.** Start pauses in a song; Select is Star Power (above) and is
also the pause/back affordance in menus via the guitar frets (green select, red
back) at the UI layer. [C 360 UiManager::action; consistent with 0x0010df80 slot
map]

**Input-layer debounce / dead zones.** The only input-layer timer is the 52 ms
strum filter above. There is no separate fret debounce and no analog dead-zone
tuning in the pad path — the pad reports clean digital fret bits, so none is
needed. This is the key reconciliation point for a universal model: the **GH2
family (incl. GH80s) DOES carry a 52 ms strum refractory**, unlike the
guitar-only (GH3+) games that the other audits found to have no input-layer
strum debounce.

### (b) Our gd03 port — gaps and errors

The gameplay rules are faithful. `engine/src/game/rules.h` keeps
`strumRepeatMs = 52` (cited to 0x8233aa80); `player.cpp` reproduces the 52 ms
drop (lines ~89-90), the rate-based whammy (`setWhammyValue`, |Δ/Δt| >
whammy_speed×0.01, lines ~843-850), the 500 ms whammy_timeout and the SP-phrase
eligibility (`whammyPoll`, ~857-868), and the SP meter math (`spPoll`). No gap
found there.

The fragility is the SDL device layer, `engine/src/input/input.{cpp,h}`, which
has no PS2 counterpart — it is gd01's universal controls (per-device profiles,
two-slot rebinding, controllers.json, touch, MIDI, shake) dressed with GH2's
layouts. Concrete gaps/errors, each a desktop-only risk the original never had:

1. **PS2-guitar detection is a name-substring heuristic** (`isPs2Guitar`,
   input.cpp ~59-63): a device is a "PS2 guitar" only if SDL's reported name
   contains "guitar" (and not rock band/harmonix/xbox). Real PS2→USB adapters
   usually report generic names ("USB Gamepad", a vendor string), so an actual
   PS2 GH guitar is misclassified as a plain Joypad -> every fret press strums,
   the opposite of the guitar watcher. The original resolved `ro_guitar` by a
   stable type id (0x0010df80), never by a free-text name. [error, high impact]

2. **PS2-guitar fret/whammy mapping is guessed and marked [I]** (defaults,
   input.cpp ~210-221): green on the right-trigger axis, whammy on RIGHTX *or*
   LEFTX "adapters differ". Adapters vary wildly in axis assignment and resting
   value; there is no calibration, so a given adapter can land whammy on the
   wrong axis or leave green dead. [error/uncertain, high impact]

3. **Axis-as-button strum uses fixed raw thresholds** (sourceActive /
   SDL_EVENT_JOYSTICK_AXIS_MOTION, ~581-591, 927-933): press > 16000, release
   < 10000 from the re-read rest. For an adapter whose D-pad or strum sits on an
   axis that rests off-centre (handled by the 300 ms settle + `dpadaxis` path),
   a noisy axis can double-strum or miss a strum entirely. The 52 ms rules
   filter hides double-strums *in a song* but **menus have no such filter**, so
   a jittery strum double-advances menus — a classic "fragile" symptom.
   [error, medium impact]

4. **Whammy is position-thresholded at the input layer** (update, ~947-954,
   960): dead zone 0.08, and the Whammy *action* fires at position > 0.15. The
   port then feeds the 0..1 position to the faithful rate-based `setWhammyValue`.
   That is fine for SP fill, but a PS2-guitar whammy that rests at centre (not
   -32768) triggers whammy on movement in *either* direction and can chatter
   around 0.15. [minor, cosmetic for SP]

5. **No analog tilt is wired for the PS2-guitar profile** (Gyro = Back only,
   ~221): correct vs the mercury *switch* (Select deploys), but if an adapter
   exposes the tilt switch as a distinct button it is unbound. [minor]

6. **Many adapter-robustness mechanisms can themselves eat real input**: the
   300 ms connect settle (no actions, rest re-read), the "stuck button"
   suppression (a bit held at connect is ignored until released), and the 80 ms
   same-press de-dup across two devices. Each is defensible, but together they
   are untestable without real hardware and are plausible sources of "a bunch of
   errors". [uncertain]

### (c) Prioritized improvement list

PS2 = PS2-path specific; SHARED = touches the cross-game control model.
small-safe = isolated + data-derivable + unit-testable now; needs-coord =
affects the fragile engine or needs real-device logs, so it is a PROPOSAL for
the orchestrator, not a fix to land here.

- [PS2, needs-coord] Replace the "guitar" name-substring detection (#1) with a
  stable allow-list of known PS2-guitar adapter USB ids (mirroring the Rock Band
  id table already in input.cpp), falling back to name only. Needs device ids
  from real adapters -> PROPOSAL.
- [PS2, needs-coord] Per-adapter whammy/strum axis calibration (#2, #3): a
  short "wiggle the whammy / strum once" capture in the Gamepads screen that
  records the live axis and rest, instead of hard-coded RIGHTX/LEFTX and
  16000/10000. Needs real-device logs -> PROPOSAL.
- [SHARED, small-safe] Debounce strum-as-menu-navigation with the **same 52 ms**
  the rules engine already uses (#3): the number is already recovered
  (`strumRepeatMs`), so a menu-side guard is data-derived and unit-testable
  against a synthetic double-edge. Still gated on the orchestrator because it
  touches the shared input layer.
- [SHARED, needs-coord] One documented control model across games that states
  the 52 ms strum refractory belongs to the GH1/GH2/GH80s family and is absent
  in the guitar-only games, and that PS2 Star Power is a digital switch (Select)
  with no analog-tilt threshold. This is the reconciliation the orchestrator
  asked for.
- [PS2, small-safe] Tighten the whammy centre-rest handling (#4) so a
  centre-resting PS2-guitar axis maps to 0..1 one-directionally; data-derived
  from the axis rest, unit-testable, but still a shared-file edit -> defer to
  orchestrator.

### (d) Fix made this pass

None. Per the owner's rules (objective checks from real-device logs, no
eyeballing; do not rewrite the fragile input engine) every candidate above
either needs real adapter ids/logs I do not have, or touches the shared input
layer that other agents are on. Landing an unverifiable input change blind would
be exactly the repeat the owner is furious about. This pass is analysis +
documentation only, which the brief states is a success.

### (e) Uncertain / blocked

- The raw whammy 0..1 value range and the exact shipped whammy_rate/speed
  numbers are in `gems.dta` on the disc, not recovered here (mechanism is [C],
  values are [data, unread]).
- The PS2 pad's per-frame button-word decode (active-low mask, analog-mode
  bytes) is standard libpad and is asserted from the protocol + the libpad
  strings, not traced bit-by-bit in the 0x2f11a0 read cluster.
- Whether any real PS2-guitar USB adapter in the wild reports a name containing
  "guitar" is unknown without hardware; that is what makes gap #1 a PROPOSAL
  rather than a confident fix.
