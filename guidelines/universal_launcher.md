# Universal launcher and the games inside it

Rules for the Guitar Dream Universal Launcher (`gd-engine-launcher/`) and for
the screens every game module shares inside it: the Enhanced menu, the
GAMEPADS screen, controls (gamepad, mouse, Enter, touch) and packaging. Read
with [general_guidelines.md](general_guidelines.md) and
[smilar_behaviors.md](smilar_behaviors.md).

(This file absorbed the former `docs/guidelines/controls-and-enhanced-menu.md`
on 2026-10-28.)

## The owner's rules (wave B, 2026-10-28)

These win over anything older below.

- **Enhanced menus (every game module).**
  - Options are always on the **left**.
  - A multiple choice is always a **dropdown**.
  - A single (on / off) choice is always a **check box**.
  - On the **right**, always a **static close-up of the vocalist**, framed
    with room to see the **whole band**.
  - Every option changed on the left renders **at once** on the band.
- **Gamepad configs and menus.**
  - Every game works with the **mouse**.
  - **Enter is always Start.**
  - A **touch** works as a mouse click.
  - The touch controls and on-screen buttons have the **same layout in every
    game** (unless the owner explicitly says otherwise).
- **Window.** Every desktop build is windowed by default. Android has no
  window mode: leave its settings alone.
- **Never forget a package** (below).

## Never forget a package

The launcher is the project's only release, so a missing module is a broken
release. It already happened twice (gdul 0.1.1 and 0.1.2 on Windows, then on
Android: "This launcher build has no gd-game-01 module"). The full story is in
[docs/release-process.md](../docs/release-process.md) "Never again:
incomplete bundles".

- **Every platform carries every ready module**, in every build: Linux, Steam
  Deck, Windows, macOS and Android. "Ready" means `ready_to_play: true` in the
  game's `game-info.yaml` (listed in `gd-engine-launcher/games.yaml`) and a
  module for that system (`module_systems_NN` in
  `gd-engine-launcher/scripts/build.sh`). A ready game without a module for a
  system is a bug to fix, not a build to ship.
- **`scripts/verify_bundle.sh` runs on every build** `build.sh` assembles and,
  in the release workflow, on every archive (`.tar.gz`, `.zip`, `.apk`) before
  it is uploaded. For each ready game it requires:
  - the module, non-empty, exporting `gd_game_run` (objdump / nm);
  - its libraries (gd02: FFmpeg's avformat, avcodec, avutil and the LGPL
    licence; gd01 on Linux: the DLSS library; on Windows, `libwinpthread-1.dll`
    and its licence);
  - on Windows, every import of **every** `.dll` / `.exe` resolved inside the
    download or by Windows' own DLLs;
  - its entry in `gd-engine-games/manifest.json`.
- **Build steps fail loudly.** Each `build.sh` step runs in a fresh
  `bash -euo pipefail` (with `inherit_errexit`); never wrap steps in
  `|| return 1` inside functions (bash then ignores `set -e`).
- **If verify fails, do not publish.** Read what it prints, then the step log
  (`gd-engine-launcher/logs/<os>-<game>.log`, the run's `logs-<os>` artifact,
  FFmpeg's `ffmpeg-build.log`). Check any download by hand with
  `gd-engine-launcher/scripts/verify_bundle.sh <file or folder>`.
- **Load each module for real** before calling a build good: on Windows with
  Proton's `wine` and the `LoadLibraryExW` test; on Android, open a game on a
  device or emulator; everywhere, load an ISO (the other recurrent bug).
- The launcher itself says at start when its download lacks a module its
  manifest lists, and logs it.
- **Local builds.** After any bugfix or improvement, build the latest launcher
  with all the latest modules locally (every system except macOS). Keep only
  the **current and the previous** version in
  `gd-engine-launcher/local_builds/`; delete older ones.
- **Steam Deck** has its own release with its own out-of-the-box settings
  (backlog GDREL-001), and the launcher recognises the Deck's controllers
  (Steam virtual pads).

## GAMEPADS screen, Enhanced menu and mouse: the same in every game

Every gd-game has two screens of its own that are not in the original game:
the **GAMEPADS** screen and the **Enhanced** menu. A player who knows them in
one game must find them the same in every other game. **gd-game-01 is the
reference**: copy its structure, flow, names, order and widgets, and only
swap in the game's own fonts and colours. Where a game cannot do something
yet, it keeps the row and greys it out; it does not leave the row out or
move it.

Reference code:

- gd-game-01: `engine/src/ui/gamepad_panel.cpp` (GAMEPADS),
  `engine/src/ui/select_panels.cpp` (`EnhancedPanel`), `UiSystem::mouse` in
  `engine/src/ui/ui_system.cpp`, and the input layer in `engine/src/input/`.
- gd-game-02 (the first port of them): `engine/src/ui/gamepad_panel.cpp`,
  `engine/src/ui/enhanced_panel.cpp`, the shared drawing in
  `engine/src/ui/port_draw.*`, and `PanelObj::mouse` in
  `engine/src/ui/script_classes.cpp`.

### Shared look rules

- **Menu units.** The screens are laid out in 1280 x 720 menu units, at
  gd-game-01's coordinates. Text heights are line heights, as gd-game-01's
  `drawText` takes them. Convert them if the game's text drawer measures
  something else (gd-game-02's `ScreenText` takes the capitals' height, so
  `PortDraw::text` divides by 1.45).
- **Fonts and colours.** Use the game's own menu font. Use gd-game-01's
  colour roles, filled in with the game's palette:
  - title and section headings: the game's title colour;
  - normal rows: a soft white;
  - the focus: highlighted text on a translucent bar in the game's accent
    colour, with a solid marker at the bar's left edge;
  - live presses and the "on" state: green, so they read as "working";
  - not available yet: grey, with gd-game-01's yellow warning sign.

  If the font has no `&`, write "AND" (gd-game-02 writes "CONTROLS AND
  GAMEPAD").
- **Widgets** (gd-game-01's, drawn the same in every game):
  - a check box: a frame with a tick;
  - a dropdown: a frame with the value and a down triangle. It opens below
    its row, or above it when there is no room below. The current option has
    a tick. A click outside the dropdown, or Back, closes it.
  - a choice with exactly two values: drawn and used as a check box;
  - a button: a framed label such as "Open...", "Calibrate..." or "Apply";
  - a section header: a triangle (pointing right when collapsed) and the
    heading; Select collapses or expands the section;
  - a separator line between sections;
  - up and down arrows when the list scrolls.
- **Transition.** The page slides 0.6 screen widths and fades with the
  screen transition, forwards or backwards, as in gd-game-01.
- **Help bar.** Use the game's own help bar: select, back and up / down.

### The GAMEPADS screen

- **Opening it.** The main menu's **yellow** fret or button opens it (on a
  joypad, also its yellow Y button). The main menu's help bar shows a yellow
  "GAMEPADS" item before the strum bar, and a click on that item opens the
  screen too. The Enhanced menu also has a Gamepads button. Back returns to
  the screen it came from. (gd-game-01, frozen, still opens it with its
  blue fret, the help bar's `fret4` item. The owner asked for yellow in
  every game, so new games use yellow.)
  - If the game enters cheat codes on the main menu with the frets (GH2
    does), the yellow fret opens GAMEPADS only after the codes' timeout
    (GH2: 750 ms) passes with no other button. A code that completes, or
    any other button, cancels it.
- **Layout** (gd-game-01's coordinates):
  - The title "GAMEPADS", the line "Controllers, their players and their
    buttons", and the path of the controller report.
  - Left, CONTROLLERS: one row per device (the keyboard, then the touch
    screen while touch is on, then each pad). Each row shows the device's
    name, its type, and its player (`< P1 >` when focused, `--` for none).
    Below the rows, LIVE INPUT shows the selected device's pressed buttons
    and its axes as bars.
  - Right, "BUTTONS: <device>": the controls in this order: Green, Red,
    Yellow, Blue and Orange Fret (each with a fret-coloured square), Strum
    Up, Strum Down, Whammy, Star Power, Start (Pause), Select / Confirm,
    Back, Menu Up, Menu Down, Menu Left, Menu Right. Each control shows its
    bindings and the mouse's "x" that clears them. Then Reset to Defaults,
    then the keyboard's "Frets also strum" check box or a pad's
    "Type: < ... >".
- **Flow.**
  - One focus runs down the controllers and then the selected controller's
    rows.
  - On a controller, Left / Right or Select changes its player: none, P1,
    P2. Each player has one controller; the controller that had that player
    takes the other's old one.
  - On a control, Select waits for the next button, axis or key, which
    replaces the binding. Esc cancels, Delete leaves the control unbound,
    and the wait ends by itself after 10 s.
  - Reset to Defaults restores the bindings.
  - A pad's Type cycles through Auto (detected) and the game's controller
    types.
  - The touch screen shows its own settings instead of bindings.
  - A row lights green while its control is held.
  - Changes are saved at once in `controllers.json` in the save folder
    (players and types per controller model).
- **Per-controller type.** Pick a type automatically (guitar or gamepad, by
  the game's own detection), and let the player override it per model. The
  type picks the default bindings and how the frets play: a guitar strums
  with its strum bar, a gamepad's frets also strum.
- **Input-layer rules** (so nothing presses by itself):
  - An axis counts as pressed only past half its range from its rest value,
    and released below a third. The rest is read as the device connects and
    for 300 ms after.
  - A button that SDL's mapping reads from an axis (a D-pad on axes) follows
    the same rule against that axis's rest. Never trust SDL's mid-range test
    for it.
  - A button held as the device connects counts as released until it is
    let go.
  - A device that goes away releases everything it held.
  - While a menu control (a direction, Start, Select or Back) waits for its
    binding, it accepts only buttons and keys, never an axis.
  - A test for each rule belongs in the input self-test.

### The Enhanced menu

- **Background.** A still shot of one of the game's venues with the band
  caught mid-song, posed and **not moving** (render a fixed number of update
  steps, then freeze). The camera is a **static close-up of the vocalist on
  the right**, with room to see the **whole band** (gd-game-01's Enhanced
  pose is the model). The band must always be there: a backdrop with no
  character is a bug. Every option changed on the left is rendered at once
  on this shot (it goes through the same post chain as a song). The venue is
  a little darker behind the box. If the game cannot draw a venue yet, use a
  black page. Never use a live, moving band.
- **Layout** (gd-game-01's coordinates):
  - On the left, a dark box (black at about 0.65) with a thin light edge and
    a top bar in the title colour. In it: the title (gd-game-01:
    "ENHANCED SETTINGS"; a game may use its own word for settings, as
    gd-game-02's "ENHANCED OPTIONS"), the line "Features of this PC version,
    beyond the original game", and the list.
  - Bottom right, aligned with the box's bottom, the **explanation box**:
    the focused row's name, its explanation wrapped, and for a row not built
    yet, the warning sign with "Not available yet".
- **Sections, options and order** (gd-game-01's names; a game adds its own
  rows only where the list says so):
  1. STEAM DECK MODE (starts collapsed): Apply Steam Deck Settings.
  2. CONTROLS & GAMEPAD:
     - Gamepads (the button, in games that have it here);
     - Enable Mouse Selection;
     - Enable Touch Controls;
     - Enable Rock Band Guitars;
     - Enable XInput Controllers.
  3. PERFORMANCE & GRAPHICS:
     - Audio / Video Calibration;
     - Screen Mode (dropdown: Windowed / Full Screen / Full Screen (No
       Border));
     - In-Game Max FPS (dropdown: 60 / 120 / 240);
     - Show FPS Counter (check box);
     - Internal Resolution (dropdown). The original console's own height is
       labelled, for example "448p (ps2)" or "720p (x360)".
     - Sharper Text & Sprites;
     - Anti-Aliasing;
     - AI Upscaling, followed by Upscaling Quality when AI Upscaling is on;
     - Bokeh Effect;
     - Enable Particles FX;
     - Bloom;
     - Cinematic Color;
     - Color Grading;
     - Film Grain;
     - Chromatic Aberration;
     - Anamorphic Light Streaks;
     - Ambient Occlusion;
     - Sharpening;
     - Blur on Star Power.
  4. ENHANCERS:
     - All Band Mates Cast Shadows;
     - More Lights on Venues;
     - Dust in Light Beams;
     - Add Fog to the Ground (Experimental);
     - Disable Shaky Camera;
     - Export User Files;
     - Load User Settings;
     - Load User Progress.
  5. CHEATS: the port's cheats (Enable Cut Content, No Fail Mode, Autoplay,
     Forgiving Hammer-ons & Pull-offs), then the original game's own cheats,
     with the game's own names. Each one says how it is entered in the
     original.
- **Behaviour.**
  - Up / Down move the focus. Separators are skipped, and the list scrolls
    smoothly so that a section's first row keeps its heading in view.
  - Select toggles a check box, opens a dropdown, presses a button, or
    collapses / expands a section.
  - Left / Right step a dropdown's value, or set a check box off / on.
  - In an open dropdown, Up / Down pick an option, Select chooses it and
    Back closes it.
  - Every change is saved at once (`enhanced.json`) and applies at once.
  - Rows not built yet stay in place: greyed, with the warning sign. Trying
    to change one plays the error sound.

### Mouse (every screen, original and port)

- Hovering a menu entry focuses it, the same way the pad would. A list
  takes clicks only; a hover would scroll it.
- A left click selects. A click on a list row moves the selection to that
  row, then selects it.
- A right click is Back.
- The wheel sends one Up or Down per notch.
- The help bar's items are clickable: select, back, Start and GAMEPADS.
- On a screen with nothing to choose from, a click continues, as Select
  does.
- On the original game's 3D menu panels, hit-test the projected bounds of
  what each component drew in the last frame. Where entries overlap, the
  one whose centre is nearest wins.
- The port's own screens hit-test their own rows, dropdown options and
  "x" buttons.
- Hovering works only while the window has focus.
- The pointer shows when the mouse moves. It hides after 3 s without
  movement, on any key or button press, and during a song (the pause menu
  takes the mouse again).
- Enhanced > Enable Mouse Selection turns the mouse off.
- Taps on a touch screen are clicks.

### Tests every game keeps

Each game keeps a walk test for each of these, plus the input self-test
rules above:

- a mouse-only walk: main menu, Options, Enhanced, a dropdown change, then
  back with the right click and with the help bar;
- a GAMEPADS walk: the yellow button, a rebinding saved, back, then the help
  bar's GAMEPADS item;
- a virtual pad whose D-pad is an axis resting off centre, which must give
  no left / right press while it rests.

gd-game-02's are `--walktest mouse`, `--walktest gamepads` and
`--walktest stickyaxis`.
