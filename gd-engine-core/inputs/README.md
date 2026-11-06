# gd-engine-core/inputs (planned)

The shared **input** area (`gdcore_inputs`, namespace `gd::core::inputs`):
controls, bindings, gamepads, guitars and drums, MIDI devices. No code yet;
this file records its purpose and plan so the area starts in the right shape.

## Purpose

Every game reads the same kinds of devices and offers the same controller
screens (docs/guidelines/universal-menus.md, 1.2.1 "Gamepads"), but each game
has its own copy today:

- gd-game-01: `src/input/input.cpp` (keyboard, SDL gamepads, guitars, the
  virtual pad for walks), `src/input/midi_in.cpp` (RtMidi, desktop only),
  `src/ui/gamepad_panel.cpp` (controllers.json).
- gd-game-02 / 03 / 04: their own input layers on gd02's engine.
- The launcher: its own menu navigation and auto-repeat (GH3's
  SetMenuAutoRepeatTimes, GD04-114).

## Plan

1. An interface for **devices** (`Device`: id, kind, name, buttons / axes /
   frets / whammy / tilt state, rumble) and **events**, fed by SDL3 gamepad /
   joystick / keyboard / mouse events and by MIDI, with no game types: games
   map device state to their own actions.
2. **Bindings**: a data model (action id to device inputs) and its JSON form,
   shared by every game's controller screen; the file format of gd01's
   `controllers.json` as the starting point, with a migration for each game's
   current file so no player loses a binding.
3. **Guitars and drums**: the known controllers (Rock Band / Guitar Hero
   guitars and drums by USB / Bluetooth id) recognised in one table, their
   axes normalised (whammy, tilt, the RB guitar's pickup switch).
4. **MIDI** behind a backend (RtMidi on desktop; Android's MIDI API later), so
   platforms without one simply list none.
5. **Menu repeat**: the auto-repeat timing as a reusable helper.
6. A headless self-test with SDL's virtual joysticks (`SDL_AttachVirtualJoystick`)
   and recorded MIDI messages.

Games adopt it one at a time (rule 1 of engine-backend-guidelines.md); the
game's own input code stays until then. Note for the self-tests: a physical
guitar plugged in changes device counts (usual-bugs.md): tests must ignore
real devices (`SDL_GAMECONTROLLER_IGNORE_DEVICES`) or use virtual ones only.
