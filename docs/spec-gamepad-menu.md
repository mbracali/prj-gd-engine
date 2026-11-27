# Universal Gamepad Menu & Default Controls

> Purpose: A spec fed to agents building/onboarding games — the universal gamepad menu and default controls.

## Overview

The **Gamepad menu** (a.k.a. GAMEPADS / the Gamepad panel) is a port-added screen that
every Guitar Dream Engine game carries. It is **not in the original games**: it lets a
player assign input devices to players, choose each device's instrument/type, remap every
button, and test input — laid out identically across games so a player finds it the same
everywhere. It is backed by one reusable input engine per game
(`engine/src/input/input.{cpp,h}`, gd01 is the reference) plus one of two UI hosts
(`gamepad_panel.cpp` for the GH1/GH2 lineage, `port_gamepads.cpp` for the GH3 lineage).

- **How it opens.** Reached from **Enhanced > CONTROLS > Gamepads** (a `Button` row in the
  port_enhanced hosts), and from the help bar's **Universal Access Button "Gamepads"** —
  "there is always a way to set up the gamepads" (universal_mechanisms.md §1.1.3). It opens
  on the next frame, with no transition, like every universal menu (universal-menus.md §1.2).
- **What it does.** Detect every connected device and assign it to a player; choose each
  device's **Instrument** (and, for pads, a **Controller Type** profile); **remap** any
  control over up to two sources per button; **test** input via the device's live raw
  button/axis state; and Export/Import all bindings as `controllers.json`.
- **Always-on vs needs-enabling.** Since gd01 1.1.7 the old enable switches are gone:
  **XInput/standard gamepads and Rock Band / Guitar Hero HID guitars always play**
  (`gamepadPlay_` and `rockBand_` default true; the setters remain only for tests). **Mouse
  selection is on by default** (but mouse buttons are never bindable). **Touch controls**
  and **Shake-for-Whammy** are **off on desktop, on only on Steam Deck and Android**
  (per-OS Enhanced profile). **Generic MIDI** is always listed where a MIDI backend exists.

Canonical requirements: `docs/guidelines/universal-menus.md` §1.2.1 (menu layout and the
ideal binding list) and `docs/guidelines/universal_mechanisms.md` §1.1.4 (default control
mapping) + §1.1.3 (Universal Access Buttons) + Accessibility.

## The gamepad menu

**Layout** (the owner's mockup, universal-menus.md §1.2.1.1; implemented in gd01
`engine/src/ui/gamepad_panel.cpp`):

- **Left — four player boxes** (Player 1 blue, 2 green, 3 red, 4 orange). Clicking the
  **"Player N"** label turns that player on/off; a disabled player is not recognised in
  game. **Player 1 is always available** (`playerEnabled(0)` is always true; the GH-family
  games have **two** players, so players 3 and 4 are shown unavailable). Each box has an
  **Instrument** dropdown, a **"Bind to:"** dropdown (every connected device, plus a
  "Detect…" entry that binds the next device a button is pressed on), and a **Customize
  inputs** button.
- **Bottom-left:** `[< Back] [Export] [Import]` (controllers.json to/from a file).
- **Right — INPUTS MAPPING**, empty until **Customize inputs** is chosen, then showing the
  selected player's binding rows plus **Save / Discard** at the bottom. **Everything on the
  left is locked until the edit is saved or discarded** (`beginEdit`/`commitEdit`/
  `discardEdit`). Edits apply immediately so they can be tried; Save writes
  `controllers.json`, Discard reverts the device.

**Auto-detection & types.** On connect each pad is opened as an `SDL_Gamepad` and
classified into a `Profile` (`gd-game-01/engine/src/input/input.cpp:482`): a **Rock Band**
HID guitar (`isHidRockBandGuitar`, by USB id / SDL mapping) → `RockBand`; an **Xbox** guitar
(`isXboxGuitar` / Santroller) → `XboxGuitar`; a **PS2-adapter** guitar (`isPs2Guitar`) →
`Ps2Guitar`; anything else → `Joypad`. The player may override this via **Controller Type**
(`deviceType` / `cycleDeviceType`): **Auto, Gamepad, PS2 guitar (adapter), Xbox guitar,
Rock Band guitar** (gd04 adds "Xbox guitar (old layout)"). A pad's chosen type is saved per
controller model. Device ids: keyboard `"keyboard"`, pads `"pad:<SDL id>"` (keyed by GUID
for persistence), MIDI ports `"midi:<port name>"`, touch `"touch"`.

**Instrument dropdown.** Only the instruments a game has are offered. GH-family games
(gd01–04, 06, 07, 10) offer **5 Fret Guitar, Gamepad, Generic MIDI** (a gamepad plays the
guitar part, so its mapping shows the 5-fret section). **Rock Band (gd05)** additionally
offers **Drums** and **Vocals (microphone)** (`enum class Instrument { Guitar, Gamepad,
Midi, Drums, Vocals }`, `gd-game-05/engine/src/input/input.h:122`).

**Remapping flow (INPUTS MAPPING rows, as shipped — gd01 `rightRows()`):**
- **GENERAL CONTROL BINDINGS:** Start, Select, Up, Down.
- **5 FRET GUITAR BINDINGS:** Start, Select, Strum Up, Strum Down, Green, Red, Yellow, Blue,
  Orange, Whammy, Gyro Special Trigger.
- **OPTIONS:** Require Strum (per device model), Reset to Defaults.
- **TOUCH SCREEN (PLAYER 1):** shown for player 1 only when touch is on (on-screen buttons
  mode, two-finger swipe action, pause corner).
- **Save / Discard.**

Each control holds **up to two sources** (`kSlots = 2`, "Key 1 / Key 2"). Left/right picks
the slot; confirm captures the next button/axis/key/MIDI note (`beginCapture`), **Esc**
cancels, **Delete** clears the slot. A source already bound elsewhere stays bound to both:
the engine **merges a device's controls into actions before emitting**, so overmapped
inputs (menu Up + Strum Up on the same key; Start in both sections on Enter) fire **once**,
and releasing one never releases an action another still holds — "no double inputs, no
missed inputs" (input.h class comment; universal-menus.md §1.2.1). A device can also be
**tested**: the panel reads `rawPressed(id)` / `rawAxes(id)` for live button/axis state.

**Persistence.** Bindings, chosen player, chosen type and Require Strum are saved per device
GUID in **`controllers.json`** in the SDL pref (save) folder, written on `commitEdit`. Old
(1.1.6-era) `controllers.json` files still load and are remapped. Built-in menu helpers are
never rebound and always work: keyboard Esc/Backspace (back), Enter (select when unbound),
the arrows; a pad's d-pad left/right and south/east buttons. **Mouse buttons can never be
mapped.** A `controllers-report.txt` (device names, USB ids, SDL mappings, types, bindings,
recent raw events) can be written for bug reports (`writeReport`).

## Controller types & default controls

Every game ships default mappings from the start (universal_mechanisms.md §1.1.4). The
shipped input engine exposes **15 bindable controls** — General (Start, Select, Up, Down)
and 5-fret guitar (Start, Select, Strum Up/Down, Green, Red, Yellow, Blue, Orange, Whammy,
Gyro) — for **every** device. Action mapping: the **Gyro/Select control drives Star Power**
(`input.cpp`: `case C::Select: case C::GuitarSelect: case C::Gyro: on(Action::StarPower)`),
**Start** drives pause/Start, and strum is a pulse. Defaults below are from
`Input::defaults()` in `gd-game-01/engine/src/input/input.cpp:158`; the keyboard drum/cymbal
and gamepad drum rows are from the §1.1.4 requirements table (see the honesty note after).

### Keyboard (`Profile::Keyboard`) — grounded in code

| Action | Key 1 | Key 2 |
|---|---|---|
| Green fret | `1` | `F1` |
| Red fret | `2` | `F2` |
| Yellow fret | `3` | `F3` |
| Blue fret | `4` | `F4` |
| Orange fret | `5` | `F5` |
| Strum up | Up arrow | — |
| Strum down | Down arrow | — |
| Menu up | Up arrow | — |
| Menu down | Down arrow | — |
| Whammy | Left Shift | Right Shift |
| Star Power (Gyro / Select) | Tab | — |
| Start (pause) | Enter (Return) | — |
| Select | Tab | — |
| Back (helper, fixed) | Esc / Backspace | — |
| Confirm (helper, fixed) | Enter | — |

Require Strum default: **on** (keyboard). Split-keyboard 2-player (`--kb2p`): P2 frets `H J K
L ;`, strum `N`/`M`, Star Power `B`, Start `O` (`input.cpp:675`). Keyboard **drum/cymbal**
defaults from §1.1.4 (Red=1, Yellow=2, Blue=3, Green=4 pads, Kick=Space, cymbals F2/F3/F4)
and Rock Band **small frets** (6–0 / F6–F10) are **not present as distinct controls in the
shipped engine** — see honesty note.

### Xbox / XInput gamepad (`Profile::Joypad`) — grounded in code

PS2-style layout; SDL button names in parentheses.

| Action | Source | Notes |
|---|---|---|
| Green fret | Left Trigger (L2), axis past half | |
| Red fret | Left Shoulder (L1) | |
| Yellow fret | Right Shoulder (R1) | |
| Blue fret | Right Trigger (R2), axis past half | |
| Orange fret | South / Cross (X) | |
| Strum up | D-pad up | overmaps Menu up |
| Strum down | D-pad down | overmaps Menu down |
| Whammy | Left stick X | and Right stick X (either) |
| Star Power (Gyro / Select) | Back (Select) | |
| Start (pause) | Start | |
| Back (helper, fixed) | East button | |

Require Strum default: **off** (a gamepad's frets also strum — the original's joypad mode).

### Guitar Hero guitar — Xbox (`Profile::XboxGuitar`) — grounded in code

| Action | Source |
|---|---|
| Green / Red / Yellow / Blue frets | South / East / North / West |
| Orange fret | Left Shoulder |
| Strum up / down | D-pad up / down (gd04 also binds left-stick Y) |
| Whammy | Right stick X |
| Star Power / Tilt | Back (gd04: right-stick Y tilt + Back) |
| Start / Select | Start / Back |

(`Profile::XboxGuitarOld` in gd04 swaps Yellow/Blue to West/North.) PS2-adapter guitar
(`Profile::Ps2Guitar`): Green = Right Trigger (axis), Red/Yellow/Blue/Orange =
East/North/South/West, Whammy = right or left stick X, Star Power = Back. Require Strum
default: **on** (a guitar strums with its bar).

### Rock Band guitar (`Profile::RockBand`) — grounded in code

Same fret/strum/whammy layout as the Xbox guitar, except **Tilt (Star Power)** is the HID
guitar's tilt bit on the **Right Shoulder** (plus Back): `set(C::Gyro,
{btn(RIGHT_SHOULDER), btn(BACK)})`. Always-on by USB id / SDL mapping (PS3 / Wii / Rock Band
4 guitars). Require Strum default: **on**. Rock Band's **solo "small" frets** and the
**effect selector** are listed in the ideal menu (universal-menus.md §1.2.1.2) but are **not
distinct bindable controls** in the shipped engine — see honesty note.

### Drums & microphone (Rock Band, gd05) — partial in code

gd05 adds **Drums** and **Vocals (microphone)** as **instrument types** a device can be
assigned to (`input.cpp:1468`: a pad can be Guitar/Gamepad/Drums/Vocals; MIDI can be
Drums). However, gd05's `Control` enum is **identical to the guitar set** (no separate
Red/Yellow/Blue/Green drumpad, Kick, cymbal, Mic-selection, or vocal Confirm/Cancel
controls). So the per-drumpad / per-cymbal / microphone **default bindings and menu rows**
described in universal-menus.md §1.2.1.2 and universal_mechanisms.md §1.1.4 are **spec/ideal,
not implemented as distinct controls** in the current input engine — see honesty note.

### Generic MIDI (`Profile::Midi`) — grounded in code

**Nothing is pre-selected** (empty default bindings): the player captures each control from
the device's notes/CCs. Keyboard helpers (Backspace, Enter, Up, Down) stay usable. Backed by
RtMidi (ALSA / WinMM / CoreMIDI); unavailable on Android and on Linux builds without ALSA.

### Steam Deck & Android — see Platform notes below.

## Platform & handheld notes

- **Steam Deck virtual gamepad.** The launcher sets
  `SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD=1` **before `SDL_Init`**
  (`gd-engine-launcher/src/launcher.cpp:99`) so SDL sees the real pad behind Steam's virtual
  one in Gaming Mode. A press seen from two controllers at once (the real pad + Steam's
  virtual, or Steam's desktop layout typing Enter for A) is de-duplicated within a short
  window — in the launcher (`launcher.cpp:476`, the `kSameNs` filter) and inside each game
  (`input.h` `recent_` / `dropped_`). The Deck runs the normal Linux build with the
  **handheld (`deck`) profile** (`platform::isSteamDeck()` → `GD_HOST_HANDHELD`): **touch on,
  shake_whammy on, 60 FPS**. No separate Steam Deck download exists
  (`docs/spikes/study_steam_deck.md`). A Deck guitar/pad still uses the gamepad/guitar
  defaults above; it plays either through its own controller or the virtual gamepad.
- **Android on-screen touch controls.** `Profile`/touch path in `input.{cpp,h}`: a finger on
  a highway lane holds that fret and strums (fingers landing within 30 ms strum once, as a
  chord); a tap on the Star Power meter or a two-finger tap above the highway deploys Star
  Power; the pause corner is Start; on-screen buttons (`touchButtons`) give Up/Down/Start in
  menus and Start in a song. **There are no touch default bindings for Whammy or the small
  frets/cymbals** (universal_mechanisms.md §1.1.4 touch table — those rows are empty).
  Require Strum default for touch: **off**.
- **Shake for Whammy** (`shake_whammy`, Android only): the phone accelerometer (SDL sensors)
  drives the existing Whammy path (`pollShake`). The sensor is opened lazily on Android only;
  everywhere else it is a no-op, and physical whammy bars / Shift / touch keep working.
  **Tilt/Gyro (Star Power) is never driven by shaking.**
- **Per-OS control defaults** live in the Enhanced profile (`profileValues(deck)` in gd01
  `app.cpp` / `enhancedProfile(deck)` in port_settings; see `docs/spec-enhanced-menu.md`):
  `touch` and `shake_whammy` are **off on Linux/Windows/macOS, on on Steam Deck and
  Android**; `mouse` is on everywhere.

## Onboarding notes

When wiring a new game into the universal gamepad system:

1. **Reuse the input engine.** Copy `engine/src/input/input.{cpp,h}` (+ `midi_in.*`) from
   the nearest lineage — gd01 for GH1/GH2, gd04 for the GH3 family. Do **not** reinvent the
   control model: keep the 15 universal controls, the two-slots-per-control binding table,
   the merge-before-emit "no double / no missed inputs" rule, and the built-in menu helpers.
2. **Consume the launcher's controllers.** The launcher hands the game its window and
   **re-announces every connected pad** as `SDL_EVENT_GAMEPAD_ADDED` after hand-over
   (`gd-engine-launcher/src/module_host.cpp:53` `announceDevices`), and passes
   `GD_HOST_HANDHELD` / `GD_HOST_FULLSCREEN` flags via `gd_host.h`. The game must open
   controllers on those events (not assume it owns them at init) and apply the handheld
   profile when `GD_HOST_HANDHELD` is set.
3. **Map to the game's actions.** Route the universal `Action`s (Fret0–4, Strum, StarPower,
   Whammy, Up/Down/Left/Right, Confirm/Back, Start) into the game's fret/strum/whammy/SP
   gameplay. Keep Star Power on the Gyro/Select control and pause on Start.
4. **Honor remap & persistence.** Read bindings/types/Require Strum from the shared engine;
   never hard-code device buttons in gameplay. Persist to `controllers.json`; keep Export /
   Import working and load older files through the same validation.
5. **Keep navigation consistent.** Menus must work with any Up/Down source (d-pad, strum,
   arrows, on-screen buttons), green fret / Enter confirms, red fret / Esc cancels; wire the
   Universal Access Buttons (incl. **Gamepads**) into the game's help bar (§1.1.3).
6. **Expose the menu correctly.** GH3-lineage (port_enhanced) games reach it via the
   **Gamepads** button under Enhanced > CONTROLS (`port_gamepads.cpp`); GH1/GH2-lineage games
   use `gamepad_panel.cpp`. Offer only the instruments the game has (add Drums/Vocals only
   for a Rock Band-style game, as gd05 does).
7. **Never bump the launcher version** for input work — that is a public release and happens
   only when the owner explicitly asks.

## Sources

- `docs/guidelines/universal-menus.md` — §1.2.1 Gamepad menu (layout mockup, the ideal
  per-instrument binding list §1.2.1.2, Export/Import, Save/Discard, player on/off).
- `docs/guidelines/universal_mechanisms.md` — §1.1.3 Universal Access Buttons (Gamepads),
  §1.1.4 default control mapping (keyboard / touch / XInput tables, Require Strum),
  Accessibility, the gd01–03 controller decisions.
- `docs/spikes/study_steam_deck.md` — Steam Deck virtual gamepad, `ALLOW_STEAM_VIRTUAL_
  GAMEPAD`, handheld profile, duplicate-press filter.
- `docs/spec-enhanced-menu.md` — CONTROLS section (`mouse`, `touch`, `shake_whammy`),
  per-OS defaults, the Gamepads button in port_enhanced hosts.
- gd01 input engine: `gd-game-01/engine/src/input/input.{cpp,h}` (`Input::defaults()`:158,
  profiles/detection:482, action mapping:793, split keyboard:675); menu UI:
  `gd-game-01/engine/src/ui/gamepad_panel.cpp` (`rightRows()`, player boxes, Export/Import).
- gd04 (GH3 lineage): `gd-game-04/engine/src/game/controller_mode.h`,
  `gd-game-04/engine/src/input/input.cpp` (XboxGuitarOld, stick-Y strum/tilt),
  `gd-game-04/engine/src/ui/port_gamepads.cpp`.
- gd05 (Rock Band): `gd-game-05/engine/src/input/input.h:122` (Drums/Vocals instruments),
  `gd-game-05/engine/src/input/input.cpp` (rb_part, instrument options).
- Launcher hand-over: `gd-engine-launcher/src/module_host.cpp` (`announceDevices`, host
  flags), `gd-engine-launcher/src/launcher.cpp` (virtual-gamepad env, dedupe, openGamepads).

---

**Honesty notes (unconfirmed / not grounded in code):**

- The shipped input engine's `Control` enum has **15 controls only** (General + 5-fret
  guitar), identical across gd01, gd04 and gd05. The **drum pads, cymbals, kick**, the
  **Rock Band "small" solo frets**, the **effect selector**, and the **vocal
  mic-selection / Confirm / Cancel** rows listed in universal-menus.md §1.2.1.2 and the
  keyboard/gamepad drum defaults in universal_mechanisms.md §1.1.4 are **requirements/ideal
  spec** — I could not find them implemented as distinct bindable controls or default
  bindings in the code I read. Their §1.1.4 default values (e.g. keyboard Kick = Space,
  cymbals F2/F3/F4; gamepad drumpads L2/L1/R1/R2, Kick = Cross) are reproduced above as
  **spec defaults, not code-verified bindings**; a game that truly needs independent drum/
  vocal inputs will have to extend the Control set.
- gd05 marks a device's Rock Band **part** (Drums/Vocals) but I did not trace how drum hits
  or vocal pitch are actually read at gameplay time — only that no separate bound controls
  exist for them.
- Touch **Whammy** has no default (empty in §1.1.4); Shake-for-Whammy is the handheld
  substitute. I did not independently verify the exact on-screen-button touch layout beyond
  the `input.h`/`touchButtons` comments.
- "Gyro special trigger" is the control that emits `Action::StarPower`; I confirmed this in
  gd01 `input.cpp:793`. Whether every game labels this row identically in its UI was not
  checked per-game.
