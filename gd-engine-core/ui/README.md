# gd-engine-core/ui (planned)

The shared **UI** area (`gdcore_ui`, namespace `gd::core::ui`): the components
the universal menus are built from. No code yet; this file records its purpose
and plan.

## Purpose

The universal menus (docs/guidelines/universal-menus.md: the Enhanced menu,
its Modes / Graphics / Performance / Audio sections, the controller screens,
the help bar) look and behave the same in every game, but each game draws
them with its own code today (gd01 `src/ui/`, gd02's engine `ui/`, the
launcher's `gfx` 2D). A change to a shared menu (for example the Graphics
backend / Audio backend dropdowns of GDCORE-006) is made four times.

## Plan

1. **Model first**: the menu structure as data (sections, items: toggle,
   dropdown, slider, button; ids, labels, help text, enabled / greyed state,
   "applies on the next start"), with the settings keys they edit
   (`graphics_backend`, `audio_backend`, ...). Games read and write the same
   keys; each game keeps drawing in its own style at first.
2. **Navigation and input**: focus, wrap, auto-repeat, mouse hit areas (the
   help bar pills, GD04-111), shared by every game.
3. **Drawing** through `gdcore_render` once it exists (a small 2D batcher:
   textured quads, text from the game's fonts), so a game on a core graphics
   backend gets its menus for free.
4. Headless self-test: walk a menu model with injected input, check focus and
   values; screenshots through the render area's offscreen device.

Nothing here may know a game: fonts, textures and the original game's look
come from the game (rule 5 of engine-backend-guidelines.md).
