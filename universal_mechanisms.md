# Universal mechanisms

The owner's "Universal menus" requirements of 2026-10-29: the mechanisms and
menus every game in this project must have, so that a player finds them the
same in every game. **This file is the reference for every game**; the owner
updates it from time to time. The general rules for every task (section 0 of
the requirements) are in
[guidelines/general_guidelines.md](guidelines/general_guidelines.md); how the
port's own screens look and behave in detail (gd-game-01 is the reference) is
in [guidelines/universal_launcher.md](guidelines/universal_launcher.md).

- **Context:** these rules create consistency across all the games we
  decompile and rebuild.
- **Outliers:** some games have needs of their own, but these rules are the
  starting point for every game.
- Where a game cannot do something, it says so in its own docs and backlog.

Implemented so far: **gd-game-01** (1.1.7), **gd-game-02** (0.1.8) and
**gd-game-03** (0.0.2). The
reference components are gd-game-01's: `engine/src/input/input.*` (the
universal controls and their bindings), `engine/src/input/midi_in.*`
(Generic MIDI through RtMidi), `engine/src/ui/gamepad_panel.cpp` (1.2.1),
`EnhancedPanel` in `engine/src/ui/select_panels.cpp` with `enhancedPreset` /
`migrateEnhanced` in `engine/src/app.cpp` (1.2.2), the help bar's Universal
Access Buttons (`addAccessButtons` in `engine/src/ui/ui_system.cpp`), and the
walks in `tools/walks.sh`. gd-game-02 copies them: the same `input.*` and
`midi_in.*`, `ui/gamepad_panel.cpp`, `ui/enhanced_panel.cpp`
(`enhancedPreset`, `migrateEnhanced`), the access buttons in `HelpBarPanel`
(`ui/panels.cpp`), and its `--walktest` walks (gamepads, gamepads_pad,
enhanced, mouse, cheats, band, logo_skip). gd-game-03 copies gd-game-02's (its engine
came from gd-game-02): the same `input.*`, `midi_in.*` and
`ui/gamepad_panel.cpp`, `ui/enhanced_panel.cpp` limited to what gd-game-03
has, the access buttons, and the walks gamepads, gamepads_pad, enhanced,
mouse, band and logo_skip.

## 1.1 Universal behaviours

These behaviours are the default in every game.

### 1.1.1 Logo and credit screens

- The player can skip every credit, logo and movie screen.
- Any of these skips it: the **green fret**, the **red fret**, a **left
  mouse click**, a **touch** on the screen, **Enter** or **Start**. Watch out
  for gamepad inputs.

### 1.1.2 Band naming

The band-naming screens have caused recurring problems; they can be tricky.

- The screen works with **any up and down input** (d-pad, strum bar, arrow
  keys, on-screen buttons).
- **Start** and **Enter** are enough to confirm and move on to the next
  screen.
- When a letter needs confirming, the **green fret** always confirms it.

### 1.1.3 Navigation in menus

- Every game supports the **mouse and touch** by default: the player can
  click every option of the game with either.
- Some screens get clunky because of the way they were originally made, so
  the port adds workarounds to get through the menus: the **Universal Access
  Buttons (UAB)**:
  - **Up**: moves up in any menu of any game.
  - **Down**: moves down in any menu of any game.
  - **Start**: gives the Start input in any game.
  - **Confirm**: the same as the green fret; OK in any menu.
  - **Cancel**: the same as the red fret; cancel in any menu.
  - **Gamepads**: partly a necessity, partly an enhancement. Mostly on the
    main screen, there is always a way to set up the gamepads (see 1.2.1).
    Older games make this simple; newer ones can make it a mess.
- **Warning:** some games already show Confirm and Cancel; add them only
  where needed.
- How each family of games does it:
  - **Guitar Hero games:** almost every Guitar Hero game shows its commands
    in a bar at the bottom of the screen. Integrate the UAB into it: (1) the
    games show each command as a **pill** (background + icon + label), and
    the UAB (2) follow that pattern; (3) the pills have an orientation and
    order that look good on screen, and the UAB (4) match it so they look
    natural in the original game.
  - **Rock Band games:** their screens are more varied and hard to fit to one
    rule, Rock Band 3 above all. When their time comes, work it out by
    iterating and asking the owner, always with suggestions.

### 1.1.4 Default control mapping

Every game has default mappings set from the start; players count on them.
This is the general rule for control mapping, closely tied to the Gamepad
menu (1.2.1).

- Players set their inputs and choose their type. The types supported now:
  **5 fret guitar**, **Drums**, **Vocals**, **Gamepad**, **Generic MIDI
  input**.
  - **Generic MIDI:** show the input, bind it to a player and capture its
    inputs. Nothing is pre-selected for this kind of instrument. Players will
    probably need the keyboard to help: Backspace, Enter, Up and Down are the
    helpers.
- Rock Band 3 supports MIDI guitars and synths; we do not support them out of
  the box. If the engine recognises their inputs, fine, but players are on
  their own.
- A player binds **one instrument**: no player uses several inputs at once
  (see 1.2.1).
- **Mouse buttons can never be mapped.**

#### Keyboard

The defaults most players use.

| Original input | Key 1 | Key 2 | Notes |
|---|---|---|---|
| Green fret | 1 | F1 | Watch out for double inputs |
| Red fret | 2 | F2 | Watch out for double inputs |
| Yellow fret | 3 | F3 | Watch out for double inputs |
| Blue fret | 4 | F4 | Watch out for double inputs |
| Orange fret | 5 | F5 | |
| Red drumpad | 1 | -- | |
| Yellow drumpad | 2 | -- | |
| Blue drumpad | 3 | -- | |
| Green drumpad | 4 | -- | |
| Kick drum | Space | -- | |
| Yellow cymbal | F2 | -- | |
| Blue cymbal | F3 | -- | |
| Green cymbal | F4 | -- | |
| Up | Up arrow | -- | Watch out for double inputs |
| Down | Down arrow | -- | Watch out for double inputs |
| Strum up | Up arrow | -- | Watch out for double inputs |
| Strum down | Down arrow | -- | Watch out for double inputs |
| Start | Enter | -- | |
| Select | Tab | -- | |
| Whammy | Shift | -- | |
| Effect selector up (mostly Rock Band guitars) | Left arrow | -- | Rock Band only, only in songs |
| Effect selector down (mostly Rock Band guitars) | Right arrow | -- | Rock Band only, only in songs |
| Gyro special trigger (special) | Tab | -- | |
| Small green fret | 6 | F6 | Rock Band only |
| Small red fret | 7 | F7 | Rock Band only |
| Small yellow fret | 8 | F8 | Rock Band only |
| Small blue fret | 9 | F9 | Rock Band only |
| Small orange fret | 0 | F10 | Rock Band only |

| Option | Effect | Type | Default |
|---|---|---|---|
| Require strum | Guitar gameplay: notes need a strum | Check box | **On** |

#### Touch controls

A smaller set, for smaller screens.

| Original input | Key 1 | Key 2 | Notes |
|---|---|---|---|
| Green fret | Virtual button at the bottom of the screen | -- | |
| Red fret | Virtual button at the bottom of the screen | -- | |
| Yellow fret | Virtual button at the bottom of the screen | -- | |
| Blue fret | Virtual button at the bottom of the screen | -- | |
| Orange fret | Virtual button at the bottom of the screen | -- | |
| Red drumpad | Virtual button at the bottom of the screen | -- | |
| Yellow drumpad | Virtual button at the bottom of the screen | -- | |
| Blue drumpad | Virtual button at the bottom of the screen | -- | |
| Green drumpad | Virtual button at the bottom of the screen | -- | |
| Kick drum | Virtual button at the bottom of the screen | -- | |
| Yellow cymbal | -- | -- | |
| Blue cymbal | -- | -- | |
| Green cymbal | -- | -- | |
| Up | Virtual button at the right of the screen | -- | Watch out for double inputs |
| Down | Virtual button at the right of the screen | -- | Watch out for double inputs |
| Strum up | Virtual button at the right of the screen | -- | Watch out for double inputs |
| Strum down | Virtual button at the right of the screen | -- | Watch out for double inputs |
| Start | Virtual button at the right of the screen | -- | Watch out for double inputs |
| Select | Virtual button at the right of the screen | -- | Watch out for double inputs |
| Whammy | -- | -- | |
| Effect selector up (mostly Rock Band guitars) | -- | -- | |
| Effect selector down (mostly Rock Band guitars) | -- | -- | |
| Gyro special trigger (special) | Two fingers on the left of the screen | -- | |
| Small green / red / yellow / blue / orange fret | -- | -- | |

| Option | Effect | Type | Default |
|---|---|---|---|
| Require strum | Guitar gameplay: notes need a strum | Check box | **Off** |

#### XInput gamepad

Simpler: the PlayStation 2's default layout. PS2 button names are used here;
every gamepad has something similar.

| Original input | Key 1 | Key 2 | Notes |
|---|---|---|---|
| Green fret | L2 | -- | |
| Red fret | L1 | -- | |
| Yellow fret | R1 | -- | |
| Blue fret | R2 | -- | |
| Orange fret | X (Cross) | -- | |
| Red drumpad | L2 | -- | |
| Yellow drumpad | L1 | -- | |
| Blue drumpad | R1 | -- | |
| Green drumpad | R2 | -- | |
| Kick drum | X (Cross) | -- | |
| Yellow cymbal | Square | -- | |
| Blue cymbal | Triangle | -- | |
| Green cymbal | Circle | -- | |
| Up | D-pad up | -- | Watch out for double inputs |
| Down | D-pad down | -- | Watch out for double inputs |
| Strum up | D-pad up | -- | Watch out for double inputs |
| Strum down | D-pad down | -- | Watch out for double inputs |
| Start | Start | -- | Watch out for double inputs |
| Select | Select | -- | Watch out for double inputs |
| Whammy | Left analog stick | Right analog stick | Watch out for double inputs |
| Effect selector up / down (mostly Rock Band guitars) | -- | -- | |
| Gyro special trigger (special) | Select | -- | Watch out for double inputs |
| Small green / red / yellow / blue / orange fret | -- | -- | |

| Option | Effect | Type | Default |
|---|---|---|---|
| Require strum | Guitar gameplay: notes need a strum | Check box | **Off** |

Fixes to the requirements' tables (accepted by the owner on 2026-10-29): the
gamepad's blue fret is R2 (the table said L2), the gamepad's green drumpad is
R2 (it said L2), and the third "Yellow cymbal" row of each table is the
**Green cymbal** (Circle on the gamepad, F4 on the keyboard).

#### Guitar Hero and Rock Band original instruments

Map what makes sense for each instrument (looking it up on the web is fine).

#### Generic inputs

We cannot map every input device out there, so the engine lets the player
connect anything and map its buttons as they wish. The **Require strum**
option is a must here too.

## 1.2 Universal menus

Build them as reusable components: every game reuses them.

**They open on the next frame, with no visible delay** (owner, 2026-10-30).
Clicking or selecting GAMEPADS / the Gamepad menu or Enhanced Options shows
the menu at once:

- No screen transition into a universal menu: neither the menu's own
  entrance nor the outgoing screen's exit animation plays before it.
- Heavy work is never done when the menu opens: asset and band preview
  loads (the Enhanced backdrop's venue and band), MIDI port scans, file
  I/O, shader compiles and first-draw texture uploads are preloaded at
  startup (and the backdrop drawn once unseen) or done asynchronously.
- A fade-in is allowed only for a part that is still loading when the menu
  opens (for example the backdrop after a song unloaded it); the menu
  itself is never held back for it.

### 1.2.1 Gamepad menu

A central part of our enhanced ports.

- A player can set **up to two inputs for the same in-game button**. Two
  buttons that "overmap" each other never count as two inputs at the same
  time.
- Some games tell menu up from strum up. This is tricky: the mapping
  mechanism must understand it and **never double an input or miss one**.
  This is very important.

#### 1.2.1.1 UI and layout

The ideal layout, to be the same in every game (the owner's mockup; the
buttons' colours as noted):

```
+----------------------------------------------------------------------------------------+
| +- Player 1 box (blue) -------------------------------+ +----------------------------+ |
| | +----------+  Instrument  [Guitar              v]   | |                            | |
| | | Player 1 |  Bind to:    [Keyboard USB2       v]   | |                            | |
| | +----------+                                        | |                            | |
| | [                 Customize inputs                ] | |                            | |
| +-----------------------------------------------------+ |                            | |
| +- Player 2 box (green) ------------------------------+ |                            | |
| | +----------+  Instrument  [Bass                v]   | |                            | |
| | | Player 2 |  Bind to:    [Harmonix Guitar USB1v]   | |                            | |
| | +----------+                                        | |                            | |
| | [                 Customize inputs                ] | |                            | |
| +-----------------------------------------------------+ |                            | |
| +- Player 3 box (red) --------------------------------+ |    +------------------+    | |
| | +----------+  Instrument  [Drums               v]   | |    |  INPUTS MAPPING  |    | |
| | | Player 3 |  Bind to:    [MIDI USB5           v]   | |    +------------------+    | |
| | +----------+                                        | |                            | |
| | [                 Customize inputs                ] | |                            | |
| +-----------------------------------------------------+ |                            | |
| +- Player 4 box (orange) -----------------------------+ |                            | |
| | +----------+  Instrument  [Vocals              v]   | |                            | |
| | | Player 4 |  Bind to:    [MIC USB4 + Xinput   v]   | |                            | |
| | +----------+                                        | |                            | |
| | [                 Customize inputs                ] | |                            | |
| +-----------------------------------------------------+ |                            | |
|                                                         |                            | |
| [< Back] (red)  [Export] (yellow)  [Import] (green)     +----------------------------+ |
+----------------------------------------------------------------------------------------+
```

- Clicking the **"Player X"** box enables or disables that player. A disabled
  player is not recognised by the game. **Player 1 is always available.**
- Even though some games have only two players, the menu **always lists
  four**. In two-player games, players 3 and 4 are shown as unavailable.
- Not every game has drums, vocals and synth: only the instruments the game
  has are offered. Synth is Rock Band 3 only.
- **"Bind to:"** lists, and captures inputs from, every input device
  connected to the computer.
- The inputs mapping area shows something only after **"Customize inputs"**.
- The inputs mapping has **Save** and **Discard** buttons at the bottom.
  Every other option on the screen is unavailable until the player saves or
  discards the changes.

#### 1.2.1.2 Inputs list inside INPUTS MAPPING

How the mappings are listed. Some options show only for some instruments;
mind the universal mechanism behind the mapping (two inputs per button, no
double inputs, no missed inputs). In order:

- **General control bindings**
  - Start
  - Select
  - Up
  - Down
- **5 fret guitar bindings** (only when the controller is a guitar or MIDI)
  - Start
  - Select
  - Strum up
  - Strum down
  - Green fret
  - Red fret
  - Yellow fret
  - Blue fret
  - Orange fret
  - Whammy
  - Gyro special trigger
- **Rock Band guitar bindings** (only when the controller is a guitar or MIDI)
  - Small green fret
  - Small red fret
  - Small yellow fret
  - Small blue fret
  - Small orange fret
  - Effect selector up
  - Effect selector down
- **Drums** (only when the controller is a drum kit or MIDI)
  - Red drumpad
  - Yellow drumpad
  - Blue drumpad
  - Green drumpad
  - Kick drum
  - Yellow cymbal
  - Blue cymbal
  - Green cymbal
- **Vocals** (only when the controller is set to vocals)
  - Mic selection (lists every microphone input)
  - Confirm button (accepts buttons on any other input)
  - Cancel button (accepts buttons on any other input)
  - Start button (accepts buttons on any other input)
  - Select button (accepts buttons on any other input)
  - Up button (accepts buttons on any other input)
  - Down button (accepts buttons on any other input)

### 1.2.2 Enhanced options

#### 1.2.2.1 UI and layout

The ideal layout, to be the same in every game (the owner's mockup):

```
+-------------------------------------------------------------------------------+
|   Menu                             +----------------------------------------+ |
| |#|- Section A                     |                                        | |
| |#|    - option 1                  |                                        | |
| | |    - option 2                  |                                        | |
| | |    - option 3                  |                                        | |
| | |    - option 4                  |                                        | |
| | |                                |    +------------------------------+    | |
| | |- Section B                     |    |                              |    | |
| | |    - option 1                  |    |   Band shot for real time    |    | |
| | |    - option 2                  |    |           previews           |    | |
| | |    - option 3                  |    |                              |    | |
| | |                                |    +------------------------------+    | |
| | |- Section C                     |                                        | |
| | |    - option 1                  |                                        | |
| | |    - ...                       |                                        | |
| | |- ...                           |                                        | |
| (scrollbar on the left)            |                                        | |
| [< Back] [Export] [Import]         +----------------------------------------+ |
+-------------------------------------------------------------------------------+
```

- Every option renders **in real time** in the band preview.
- Options are **dropdowns or check boxes**, nothing else (the Modes and the
  file actions are buttons, as the table says).
- Some games have specific enhanced options of their own, but the options
  below are in **every** game.
- The band shot is always the same shot, with the loaded game's assets:
  - focused on the **lead vocalist**;
  - the **drummer and the guitarist** visible;
  - a "3-plane" shot (the singer in front, the band behind);
  - a **medium-sized venue** (it may differ per game);
  - able to show every enhanced option affecting the game.
- The **explanation pop-up** stays.

#### 1.2.2.2 Enhanced options

The options **every game** has, in order, with their sections. Reuse the
components: we will.

- Sections can be expanded or collapsed.
- Leave some space between the sections.

| Section | Feature | What it does | Notes |
|---|---|---|---|
| Modes | Steam Deck | Sets the enhancements to fit the Steam Deck (60 FPS) | A button: one click sets everything at once |
| Modes | Original feel | Sets the enhancements to look like the original game (60 FPS) | A button: sets everything at once. Differs per game |
| Modes | Clean image | Removes every enhanced effect, resolution full HD (120 FPS) | A button: sets everything at once |
| Modes | Max out (Full HD) | Every effect at its maximum, resolution full HD (240 FPS) | A button: sets everything at once |
| Modes | Max out (4K) | Every effect at its maximum, resolution 4K (240 FPS) | A button: sets everything at once |
| Modes | Rocking on a budget | Every effect at its minimum (60 FPS) | A button: sets everything at once |
| Modes | Good old days | Every effect at its maximum, colour grading sepia, resolution of the PS2 | A button: sets everything at once |
| *(space)* | | | |
| Controls | Enable Mouse Selection | Enables mouse interaction | On by default |
| Controls | Enable Touch Controls | Enables touch controls, and the virtual buttons | On by default only on Steam Deck and Android |
| Controls | Audio / Video Calibration | Opens our own calibration tool | A button that opens a new screen |
| *(space)* | | | |
| Performance & graphics | Screen mode | Windowed, full screen, or full screen without borders | Dropdown |
| Performance & graphics | Resolution | Keep what we have today | Dropdown |
| Performance & graphics | Force sharper Text & Sprites | What is already implemented | Check box |
| Performance & graphics | Anti-Aliasing | What is already implemented | Dropdown |
| Performance & graphics | AI Upscaling | FSR and DLSS options | Dropdown |
| Performance & graphics | Upscaling Quality | Quality, Balanced, Performance | Dropdown |
| Performance & graphics | Max FPS | 60, 120, 240 or unlimited | Dropdown |
| Performance & graphics | Show FPS Counter | FPS counter in the bottom-left corner | Check box |
| Performance & graphics | Bokeh Effect | None, light, medium, strong, stronger | Dropdown |
| Performance & graphics | Enable Particles FX | None, original, 2x | Dropdown |
| Performance & graphics | Bloom | None, original, 2x, 3x | Dropdown |
| Performance & graphics | Cinematic Color | True or false | Check box |
| Performance & graphics | Film Grain | None, light, medium, strong, stronger | Dropdown |
| Performance & graphics | Chromatic Aberration | None, light, medium, strong | Dropdown |
| Performance & graphics | Ambient Occlusion | None, light, medium, strong, stronger | Dropdown |
| Performance & graphics | Blur on Star Power | None, light, medium, strong, stronger | Dropdown |
| Performance & graphics | All Band Mates Cast Shadows | True or false | Check box |
| Performance & graphics | More Lights on Venues | None, 1.5x, 2x, 2.5x | Dropdown |
| Performance & graphics | Dust in Light Beams | None, 1.5x, 2x, 2.5x | Dropdown (always available) |
| *(space)* | | | |
| Cheats & enhancers | Export User Files | Exports the user's settings and progress as JSON files | Button |
| Cheats & enhancers | Load User Settings | Loads a user settings JSON file | Button |
| Cheats & enhancers | Load User Progress | Loads a user progress JSON file | Button |
| Cheats & enhancers | *Every cheat of the game* | Each one can be turned on or off | Per game |

One key difference from older versions, which had switches to enable the
guitars and XInput: here **everything is on by default**. Some code may need
to be redeveloped, adapted or improved for that.

## Decisions taken for gd-game-01 (2026-10-29)

- gd-game-01 implemented this first; gd-game-02 (0.1.8) copies its
  components. gd-game-03 and gd-game-04 are next.
- **Enhanced visual effects still default to off** (the original look, the
  "Original feel" mode), as decided earlier for every module; the Modes are
  presets that set many options at once.
- **XInput gamepads and Rock Band guitars are always on**: their switches are
  gone. Mouse selection is on by default; touch is on by default only on
  Steam Deck and Android.
- **Generic MIDI** uses RtMidi (MIT, vendored in
  `gd-game-01/engine/third_party/rtmidi/`): ALSA on Linux, WinMM on Windows,
  CoreMIDI on macOS. Android shows MIDI as unavailable for now; a Linux
  build without the ALSA headers does too.
- Guitar Hero has **two players**, and neither drums, vocals nor synth: the
  Instrument dropdown offers 5 Fret Guitar, Gamepad (a gamepad plays the
  guitar part, so its mapping shows the 5 fret guitar section too) and
  Generic MIDI; players 3 and 4 are listed as unavailable; the Rock Band,
  drum and vocal sections of 1.2.1.2 are not shown.
- 1.1.6 and older `enhanced.json` and `controllers.json` files still load:
  their values are mapped to the new ones (for example Bloom High is 3x,
  Chromatic Aberration on is Medium).

## Removed from every game's Enhanced menu (2026-10-30)

The owner removed two options from the Enhanced menu of every game; they
**must not be added to future games** (gd-game-03, gd-game-04 and later):

- **Add Fog to the Ground (Experimental)** (`ground_fog`), with its height
  fog and the volumetric Super Fog (light shafts);
- **Anamorphic Light Streaks** (`light_streaks`).

Their code, shaders, presets and tests are gone from gd-game-01 (1.1.7)
and gd-game-02 (0.1.8). An `enhanced.json` or an imported settings file
that still has these keys loads normally: the keys are ignored and dropped.
(The original games' own environment fog, and note streaks in gameplay, are
unrelated and stay.)

## Decisions taken for gd-game-02 (2026-10-29)

- The same behaviour, layout, names and defaults as gd-game-01, in Guitar
  Hero II's menus, font and colours. Guitar Hero II has **two players**:
  players 3 and 4 are listed as unavailable; the Instrument dropdown offers
  5 Fret Guitar, Gamepad and Generic MIDI.
- Kept from gd-game-02's own menus: a pad's **Controller Type** row in
  Customize inputs (Guitar Hero II's Xbox 360, old Xbox, PS2-adapter, Rock
  Band and gamepad layouts), and **Remove All Effects** (the last row of
  Performance & graphics), with Color Grading (and Sepia), Sharpening and
  Disable Shaky Camera.
- The keyboard's Enter is Start (the keyboard table): it selects in the
  menus and pauses a song; unbound, Enter still strums in a song.
- 0.1.7 `enhanced.json` and `controllers.json` files still load, mapped to
  the new values as gd-game-01's 1.1.6 files are.

## Decisions taken for gd-game-03 (2026-10-30)

- The same behaviour, layout, names and defaults as gd-game-02 0.1.8, in
  Guitar Hero Encore's menus, font and colours. Two players (3 and 4
  unavailable); the Instrument dropdown offers 5 Fret Guitar, Gamepad and
  Generic MIDI; a pad's Controller Type row is kept, as in gd-game-02.
- The Enhanced menu lists every option of the table, in its order, but
  gd-game-03 does not have the visual effects, touch, calibration, Sharper
  Text, anti-aliasing or upscaling yet: those rows are greyed ("not
  available yet"), and the Modes set only what gd-game-03 has (Original
  Feel: effects off + 60 FPS; the window, resolution and aspect kept).
  Kept from gd-game-03's own menu: **Widescreen (16:9)** after Resolution
  (4:3 is the default, as on the PS2), Color Grading, Sharpening and Disable
  Shaky Camera (greyed). Ground Fog and Light Streaks are not there.
- The port's universal menus open without a transition (also skipping the
  old screen's exit); the game's own screens without a transition still
  see the old screen's exit, as on the PS2 (gd-game-03 marks its universal
  menus `port_universal_menu`; gd-game-02 skips the exit for any screen
  without a transition).
- 0.0.1 `enhanced.json` and `controllers.json` files still load (the XInput /
  Rock Band switches are dropped).
