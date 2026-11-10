# Universal menus

The menus every game has, so a player finds them the same everywhere: the
owner's "Universal menus" requirements (2026-10-29), section 1.2 of
[universal_mechanisms.md](universal_mechanisms.md), kept here as their own
guideline (owner, 1.1.13). The universal behaviours (section 1.1) stay in
universal_mechanisms.md; the backend options (Enhanced > Performance:
Graphics backend, Audio backend) are in
[engine-backend-guidelines.md](engine-backend-guidelines.md). How the port's
own screens look in detail (gd-game-01 is the reference) is in
[universal_launcher.md](universal_launcher.md).

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
| Performance & graphics | Graphics backend | local, SDL3, Vulkan, OpenGL3 (`graphics_backend`: `local` / `sdl3` / `vulkan` / `opengl3`; only those the platform runs) | Dropdown, default local; the **first row** of the section; applies on the next start (a change asks Restart Now / Later, see "Backends" below). Selectable on gd-game-01 only for now ([engine-backend-guidelines.md](engine-backend-guidelines.md)) |
| Performance & graphics | Audio backend | local, miniaudio, OpenAL, FAudio (`audio_backend`: `local` / `miniaudio` / `openal` / `faudio`) | Dropdown, default local; the **second row** of the section; applies on the next start (Restart Now / Later, as Graphics backend). Selectable on gd-game-01 only for now |
| Performance & graphics | Screen mode | Windowed, full screen, or full screen without borders | Dropdown |
| Performance & graphics | Resolution | Keep what we have today | Dropdown |
| Performance & graphics | Force sharper Text & Sprites | What is already implemented | Check box |
| Performance & graphics | Anti-Aliasing | What is already implemented | Dropdown |
| Performance & graphics | AI Upscaling | FSR and DLSS options | Dropdown |
| Performance & graphics | Upscaling Quality | Quality, Balanced, Performance | Dropdown |
| Performance & graphics | Max FPS | 60, 120, 240 or unlimited | Dropdown |
| Performance & graphics | Show FPS Counter | FPS counter in the bottom-left corner | Check box |
| Performance & graphics | HDR | None, Emulated, True HDR (`hdr`: `none` / `emulated` / `true`) | Dropdown, default None. See "HDR" below |
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

#### Backends (Graphics backend, Audio backend)

Every game where the backends are selectable follows these rules (gd-game-01
first; the others when they adopt the core):

- **Position:** Graphics backend and Audio backend are the first two rows of
  Performance & graphics, before Screen mode.
- **Restart Now / Later:** choosing a backend other than the one running opens
  a pop-up in the Enhanced page's own look (dark box, orange top line, the
  page's font): the title "Restart the game?", one line saying the new backend
  runs when the game starts again, and two buttons, **Restart Now** (green,
  focused) and **Later** (yellow). Left / right or up / down moves between
  them, confirm (green, Enter) chooses, back (red, Esc) is Later, the mouse
  hovers and clicks them. The value is saved before the pop-up opens, so Later
  keeps it for the next start. Choosing the backend that is already running
  asks nothing.
- **Restart Now** ends the game and starts it again **in the same process**
  (in the launcher the module keeps running and gets the launcher's window
  again, for the new graphics API): no boot screens, straight back to
  Enhanced Settings, with the same profile. A backend that cannot start falls
  back to local (engine-backend-guidelines.md rule 2).
- **Indicator:** on the Enhanced Settings screen only, the backends in use are
  shown in small white text: `Graphics: <name>` and `Audio: <name>`, one per
  line, bottom right, right of the help bar (the last line on the FPS
  counter's line), whether the counter is on or off; it never runs over the
  help bar's pills (above the FPS counter it ran over SELECT, #76). It shows what really runs: a
  chosen backend that could not start is shown as `local (<name> could not
  start)`.
