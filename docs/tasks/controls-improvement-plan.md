# Controls improvement plan (the "controls house-cleaning" wave)

> Synthesis of the 2026-11-24 Ghidra controls audits of every decompiled game
> (gd01 GH1, gd02 GH2, gd03 GH80s, gd04 GH3, gd05 Rock Band, gd06 Aerosmith,
> gd07 World Tour, gd10 Metallica). Each game's evidence is in its
> `gd-game-NN/docs/FINDINGS.md` → "Controls audit (2026-11-24)". This is the
> plan for the owner to sign off; nothing here is implemented yet.

## The one key finding

The gameplay **judgment layer is faithful** across every game and must **not** be
touched: the original timing numbers are reproduced (GH2 family example —
hit window ±100 ms, strum-repeat 52 ms *guitar-mode only, in the judge*, wrong-strum
grace 60 ms, HOPO 170/250 ticks, HOPO window −100…+160 ms, overstrum 1/50 ms,
joypad chord 50 ms, SP-whammy "moving" >0.0005/ms held 500 ms, fill 0.034/beat,
SP ready 0.5; Metallica/gh4 and Rock Band confirm their own numbers).

**All the fragility is in the shared raw SDL→action layer** — `gd-game-01`'s
`engine/src/input/input.{cpp,h}` (the reference engine copied to every game). The
originals never had to solve it because each read one known controller; we read
arbitrary SDL devices with invented constants.

## Root-cause bugs (the "bunch of errors") — Phase A

Confirmed in the reference engine itself; each is shared across all games, so it
is fixed **once at the anchor** (gd01 `input.cpp`) and inherited everywhere.

| # | Bug | Evidence | Effect |
|---|---|---|---|
| A1 | **Axis-as-button threshold uncalibrated & too low**: `sourceActive()` press \|Δ\|>16000 / release <10000 ≈ **0.49 / 0.31** on a 32767 axis; the originals imply **~0.75**. | every audit; gd01/gd02/gd04 `input.cpp` | Half-pulled triggers/sticks false-fire frets & strums; noisy adapter axes chatter. **Core fragility.** |
| A2 | **Tilt / Star-Power deploy has no debounce or calibration**: Gyro = `AxisAny` on a stick/one axis; one frame crossing ~0.49 in *either* direction fires Star Power. | gd02 G2, gd04 A5, gd10/gd07 (tilt 3-frame), gd05 (mercury 0.93×12) | **Accidental Star Power** — the most likely visible error. |
| A3 | **Controller detection by name-substring** ("guitar") instead of the originals' stable **type-id / USB-id**. | gd03 #1, gd05 (subtype table), gd10 (descriptor+8 enum) | Adapters misclassified → a real guitar falls to the joypad path; a **drum kit plays guitar on scrambled buttons**. |
| A4 | **Menus have no strum debounce** (the 52 ms refractory is in the in-song judge only). | gd03 #3 | A noisy strum **double-advances menus**. |
| A5 | **Whammy input-layer deadzone/action invented** (0.08 / 0.15; digital whammy only — analog is faithful). | gd02 G4, gd04 #4 | Centre-resting whammy chatters. |

**Hard constraint:** every Phase-A change alters input *feel*, so per the owner's
objective-checks rule it must be backed by **real-device expected-vs-measured
logs** before it ships — no blind constant changes. Phase A therefore starts with
a logging/capture step (per-game input-type + axis trace; the small-safe first
step every audit proposed), then the calibrated constants, then a per-device
"wiggle once" calibration in the Gamepads screen.

## Missing capability (needs the model extended) — Phase B

The universal `Control` set is **15 controls, guitar-only** (general + 5 frets) —
identical in gd01/gd04/gd05. It cannot represent:

- **B1 — Drums.** No pad/cymbal/kick controls. Rock Band drums are faked by
  collapsing the drum chart onto the 5 frets (the faithful `game/drum_player.cpp`
  exists but is wired **only in tests**). gh4 (Metallica/World Tour) drums are
  first-class with **per-pad velocity** (`GetDrumHitVelocity`, 6 velocity bytes).
  Needs: Drum lanes + kick (+ cymbals behind a flag for RB2/RB3-Pro) + velocity,
  drum-kit detection by subtype, and the live `DrumPlayer` wiring (retire the
  fret-collapse, GD05-018).
- **B2 — Vocals.** No mic instrument; vocals run *outside* the input engine today
  (VocalPlayer + a stand-in PitchDetector). The originals have a full mic/pitch
  subsystem (RB Fonix @16 kHz; gh4 `Vocals_*`). Needs a first-class mic
  instrument owned by the shared engine.
- **B3 — World Tour guitar touch strip.** Entirely unhandled. WT songs carry
  `_Tapping` note tracks + `guitar_touch_patterns` / `leftx_unscaled` slider
  (`enable_touch_strip`, touch flag record+0x18+0x4a). Our "touch" is the Android
  on-screen touchscreen, a different thing. Needs the `_Tapping` track + a
  tap/slide judge.

## Reconciled per-family facts (so one model fits all)

- **Strum 52 ms refractory** = GH1 / GH2 / GH80s family only (in the judge,
  guitar-mode); GH3+ have none; Rock Band repeats ≤52 ms.
- **Star Power deploy**: PS2 (GH1/GH80s) = digital **Select / mercury switch**, no
  analog tilt; GH3 & gh4 = Select instant **+ calibrated tilt held 3 frames**;
  Rock Band = Select **+ mercury-filter tilt (sign held past 0.93 for 12 frames)**.
- **Controller discrimination** in every original = **type-id / USB subtype**, not
  an analog fingerprint.
- **Anchor:** gd01's 15-control `Input::Control` vocabulary + `Input::defaults`
  per-profile tables are canonical; the one change that must happen at the anchor
  is axis calibration (the hard-coded 0.49/0.31 → a single named calibration used
  by every profile).

## Proposed order

1. **Phase A, step 0 — logging** (small-safe, no feel change): per-game input-type
   + axis/tilt trace + input self-tests pinning the current constants. Lets us
   capture real-device logs to set the new constants objectively.
2. **Phase A, steps A1–A5** — the calibrated axis threshold, the tilt/SP deploy
   policy, USB-id detection, menu-strum debounce, whammy calibration — **once at
   the gd01 anchor**, re-tested on every game with captured logs. Fixes the
   "bunch of errors".
3. **Phase B, B1 Drums → B2 Vocals → B3 WT touch strip** — extend the control
   model; the biggest work, done deliberately after the fragility is fixed.
4. **Docs** — refresh `docs/spec-gamepad-menu.md` with the resolved findings
   (drums/vocals *are* read today via hacks; the per-family facts; the gaps).

## Sources

Per-game "Controls audit (2026-11-24)" sections in `gd-game-NN/docs/FINDINGS.md`
(gd01–07, gd10); `docs/spec-gamepad-menu.md`; `docs/guidelines/universal-menus.md`,
`universal_mechanisms.md`.
