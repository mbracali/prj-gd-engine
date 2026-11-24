# Universal Enhanced Menu

> Purpose: A spec fed to agents building/onboarding games — the universal Enhanced menu.

## Overview

The **Enhanced menu** (a.k.a. Enhanced Settings / Enhanced Options) is a port-added
screen that every Guitar Dream Engine game must carry. It is **not in the original
games**: it collects all of the port's quality-of-life and enhancement controls in
one place — graphics and performance, HDR, accessibility, user-file import/export,
and cheats — laid out identically across games so a player finds them the same
everywhere.

- **How it opens.** Reached from the main/options menu and from the help bar's
  Universal Access Buttons. It **opens on the next frame, with no transition** —
  neither its own entrance nor the outgoing screen's exit plays, and no heavy work
  (asset/band-preview loads, MIDI scans, shader compiles) is done on open; those are
  preloaded at startup or run async (`universal-menus.md` §1.2).
- **Layout.** A scrollable list of sections on the left (scrollbar on the left);
  a real-time **band preview** shot on the right (lead vocalist in focus, drummer
  and guitarist behind, medium venue) that re-renders as options change; an
  explanation pop-up per row; `[< Back] [Export] [Import]` at the bottom.
- **Defaults philosophy.** Enhanced visual effects default to the **original look**
  (most effects off); XInput gamepads and Rock Band guitars are always on (no
  enable switches). Startup values come from a per-OS profile (see below).

Canonical requirements: `docs/guidelines/universal-menus.md` §1.2.2 and
`docs/guidelines/universal_mechanisms.md` (HDR, Accessibility, decisions).

## Menu framework

A row-driven model. Each game's host file defines a static table of rows; a row is a
struct with a `Kind`, a `key` (the `enhanced.json` setting, a section id, or a button
action), a display `name`, a `ready` flag (false = not wired in this game yet; shown
greyed and non-selectable), help text, and — for choices — a list of `{value, label, ok}`
options (`ok=false` = shown but not choosable in this game).

**Option kinds** (union across the three hosts):

| Kind | Meaning |
|---|---|
| `Section` | A collapsible header (MODES, CONTROLS, PERFORMANCE & GRAPHICS, ACCESSIBILITY, CHEATS & ENHANCERS). |
| `Separator` | Blank spacer between sections. |
| `Check` | On/off toggle (stored `"on"`/`"off"`, or a bool in gd01). |
| `Choice` | Dropdown / left-right cycle of discrete values. |
| `Button` | An action, not a setting (a Mode preset, Gamepads, Calibration, Export/Load user files). |
| `Cheat` | A toggle wired to the original game's own cheat global (enhanced_panel / port_enhanced only). |
| `Slider` | A numeric range (`smin`..`smax` step `sstep`, a `unit` label) stored as a string. **select_panels / enhanced_panel only.** |

**Gating** (a row shown only under a condition):
- `needs` — the row shows only while another setting is on (a check) or not `none`/`off` (a choice). Present in **all three hosts**.
- `needsValue` — paired with `needs`: the row shows only while that setting equals an
  exact value. Present in **select_panels and enhanced_panel only**. Canonical use:
  the **HDR White Level** and **HDR Black Level** sliders have `needs="hdr"`,
  `needsValue="true"`, so they appear **only when HDR = True HDR**.
  (`gd-game-01/.../select_panels.cpp:1340`, `gd-game-02/.../enhanced_panel.cpp:741`.)

**The three hosts** (same behaviour, layout, names and defaults; they differ only in
code lineage and the extra kinds a game needs):

| Host file | Games | `Kind` set | Notes |
|---|---|---|---|
| `engine/src/ui/select_panels.cpp` (`EnhancedPanel`) | **gd01** | Section, Separator, Check, Choice, Button, **Slider** | The reference implementation. Cheats are plain `Check` rows. Has HDR sliders + Song/Track Speed sliders/choices. |
| `engine/src/ui/enhanced_panel.{cpp,h}` | **gd02, gd03, gd05** | Section, Separator, Check, Choice, Button, **Cheat**, **Slider** | gd02 copied gd01; gd03/gd05 copied gd02. Adds a `Cheat` kind for the original games' cheats and keeps HDR sliders. |
| `engine/src/ui/port_enhanced.cpp` + `ui/port_settings.{cpp,h}` | **gd04, gd06, gd07, gd10** | Section, Separator, Check, Choice, Button, **Cheat** (no Slider) | GH3-lineage host. Store split into `port_settings`. Cheats pulled from the game's QB cheat tables plus the port's own switch-cheats. **No Slider kind** → no HDR White/Black sliders; audio/video offset are numeric settings set via the Calibration tool, not menu rows. |

(gd08/gd09 and gd11+ have not yet adopted a universal Enhanced host.)

## Settings store (enhanced.json)

Each game owns a settings catalogue and a persistence store, written to
`enhanced.json` in the save folder as `{"version": N, "settings": {key: value}}`
(written to a `.tmp` file and renamed). In select_panels/enhanced_panel the catalogue
and defaults live in `app.cpp` (`enhancedDefaults`, `profileValues`, `enhancedPreset`,
`migrateEnhanced`); in the port_enhanced host they live in `port_settings.{cpp,h}`
(`EnhancedSetting`, `enhancedSettings()`, `EnhancedStore`). Mechanics (port_settings is
the clearest reference, `gd-game-04/engine/src/ui/port_settings.cpp`):

- **Registration.** Every option is one `EnhancedSetting{key, values, lo, hi}`.
  A choice/check lists its allowed `values` (first value = base default); a numeric
  setting has empty `values` and a `[lo, hi]` range.
- **Defaults.** The start-up default is the **per-OS profile** (`enhancedProfile(deck)`
  in port_settings; `profileValues(deck)` in gd01), not merely the list front —
  `enhancedDefault(key, deck)` returns the profile value, falling back to the list
  front. `get(key)` returns the stored value or the default if absent.
- **Validation.** On `load`, each stored key must be a known setting **and** pass
  `enhancedValid` (value in the list, or a number within `[lo, hi]`); otherwise it is
  dropped and the default is kept. **Unknown keys are dropped** (so removed options —
  Ground Fog, Light Streaks — load and vanish). `set` refuses invalid values.
- **Migration.** A file's `version` triggers in-place remaps on load (e.g. old Bloom
  `none`/`original` → `original`; a pre-Vulkan `graphics_backend="local"` → the new
  default once; audio `"local"` → `miniaudio` once). `migrateEnhancedValue` maps legacy
  value names (e.g. Bloom `high`→`3x`, Chromatic `on`→`medium`). Current version: 4.
- **Export / Import.** "Export User Files" writes every option out as JSON; "Load User
  Settings" reads it back through the same validation (missing/invalid keys keep their
  value). Accessibility and HDR keys are included like any other.

## Option catalogue

Grouped by section, in menu order. `key` — Kind — values (default **bold**) — games.
"All" = every game with a universal Enhanced host (gd01–07, gd10 as applicable).

**MODES** (Buttons — each applies many values at once via `enhancedPreset`): Steam Deck,
Original Feel, Clean Image, Max Out (Full HD), Max Out (4K), Rocking on a Budget,
Good Old Days. — All.

**CONTROLS**
- Gamepads — Button (opens the Gamepad menu). — port_enhanced hosts.
- `mouse` Enable Mouse Selection — Check — off/**on**. — All.
- `touch` Enable Touch Controls — Check — **off** desktop / **on** Steam Deck+Android. — All.
- `shake_whammy` Shake for Whammy — Check — **off** desktop / **on** handheld. — All.
- Audio / Video Calibration — Button (opens the calibration tool). — All.
- Reset Calibration — Button. — port_enhanced hosts.

**PERFORMANCE & GRAPHICS**
- `graphics_backend` Graphics Backend — Choice — **vulkan** (Win/Linux/Deck/Android), **sdl3** (macOS); also local/opengl3 where the platform runs. Applies on restart. — Selectable on gd01 for now; listed elsewhere.
- `audio_backend` Audio Backend — Choice — **miniaudio**; also local/openal/faudio. Applies on restart. — As above.
- `screen_mode` Screen Mode — Choice — windowed / fullscreen / **borderless**. — All.
- `internal_res` Resolution — Choice — 0/720/800/900/**1080**/1440/2160 (gd01 adds 448/480 for PS2 res). — All.
- `sharp_ui` Sharper Text & Sprites — Check — off/**on**. — All.
- `antialiasing` Anti-Aliasing — Choice — **none**/fxaa/ssaa/taa. — All.
- `ai_upscaling` AI Upscaling — Choice — **none**/fsr1 (FSR/DLSS; often greyed where the Vulkan upscaler is absent). — All.
- `ai_upscaling_mode` Upscaling Quality — Choice — **quality**/balanced/performance. — All.
- `max_fps` Max FPS — Choice — **120** (Win/macOS) / **60** (Linux/Deck/Android); also 144/240/unlimited. — All.
- `fps_counter` Show FPS Counter — Check — off/**on**. — All.
- `hdr` HDR — Choice — **none**/emulated/true. — All. (See HDR in `universal_mechanisms.md`.)
- `hdr_white` HDR White Level — **Slider** 80–480 nits, default **203**. **Gated: HDR=True HDR.** — gd01, gd02, gd03, gd05 only (no Slider kind in port_enhanced).
- `hdr_black` HDR Black Level — **Slider** 0–1 nits step 0.05, default **0**. **Gated: HDR=True HDR.** — gd01, gd02, gd03, gd05 only.
- `bokeh` Bokeh Effect — Choice — none/light/medium/strong/stronger. Default **none** (gd01) or **medium** (GH3's own look in gd04). — All.
- `particles` Enable Particles FX — Choice — none/original/2x. Default **none** (gd01) / **original** (gd04). — All.
- `bloom` Bloom — Choice — **none**/original/2x/3x. — All.
- `cinematic_color` Cinematic Color — Check — **off**/on. — All.
- `color_grade` Color Grading — Choice — **off**/warm/cold/vintage/noir/neon/sepia. — All.
- `film_grain` Film Grain — Choice — **none**/light/medium/strong/stronger. — All.
- `chromatic` Chromatic Aberration — Choice — **none**/light/medium/strong. — All.
- `ssao` Ambient Occlusion — Choice — **none**/light/medium/strong/stronger. — All.
- `sharpening` Sharpening — Choice — **off**/low/high. — All.
- `star_blur` Blur on Star Power — Choice — **none**/light/medium/strong/stronger. — All.
- `shadows_all` All Band Mates Cast Shadows — Check — **off**/on. — All.
- `self_shadow` **Sharper Character Shadows** — Choice — **original**/2x/4x (dynamic self-shadow map resolution). — **gd04 only** (newly added; not yet in gd06/07/10).
- `more_lights` More Lights on Venues — Choice — **none**/1.5x/2x/2.5x. — All.
- `light_dust` Dust in Light Beams — Choice — **none**/1.5x/2x/2.5x. — All.
- `no_shaky_cam` Disable Shaky Camera — Check — **off**/on. — port_enhanced + enhanced_panel.
- `no_effects` **Remove All Effects** — Check — **off**/on (forces all effects + HDR off at once). — gd02, gd03, gd04, gd05, gd06, gd07, gd10 (not gd01).

**ACCESSIBILITY**
- `easy_chords` Easy Chords — Check — **off** (original rules). gd01 stores a bool; others `off`/`on`. Rock Band (gd05): guitar & bass only. — All.

**CHEATS & ENHANCERS**
- Export User Files / Load User Settings / Load User Progress — Buttons. — All.
- `song_speed` Song Speed — Choice — 50–200 % step 10, **100**. Practice: a non-100 % run saves no record. — gd01, gd02, gd03 only.
- `track_speed` Track Speed — Choice — 0.5x–3.5x step 0.25, **1x**. Shared key across games. — gd01, gd02, gd03 only.
- `hopo_mode` Forgiving Hammer-ons & Pull-offs — Choice — **original**/forgiving. — All.
- `no_fail` No Fail Mode — Check — **off**/on. — port_enhanced hosts.
- `autoplay` Autoplay — Check — **off**/on. — port_enhanced hosts.
- `dev_debug_menu` Developer Debug Menu — Check — **off**/on. — port_enhanced hosts.
- **Per-game cheats** (each off by default):
  - gd01 (as Checks): `cheat_air_guitar`, `cheat_gh_guitar`, `cheat_unlock_all`, `cheat_full_meter`, `cheat_no_venue`, `cheat_monkey_crowd`, `cheat_skull_crowd`.
  - **port_enhanced exclusive switch-cheats** (gd04/06/07/10): `cheat_unlock_all` (Unlock Everything), `cheat_full_sp` (Star Power Always Full), `cheat_multiplier` (Always Max Multiplier), `cheat_hyperspeed` (Hyperspeed). Plus each game's own original cheats read from its QB cheat table (`Kind::Cheat`).
  - enhanced_panel games expose their original game's cheats similarly.

**Removed — must not be re-added** (owner 2026-10-30): `ground_fog` (Add Fog to the
Ground), `light_streaks` (Anamorphic Light Streaks). Old keys load and are dropped.

## Default settings per OS

Most defaults are **OS-independent**: `mouse=on`, `screen_mode=borderless` (Full Screen,
No Border), `internal_res=1080`, `sharp_ui=on`, `fps_counter=on`, `bloom=none`,
`audio_backend=miniaudio`, HDR `none`, all effects off (per game's original-look), all
cheats/accessibility off. Only the rows below vary by OS, set by
`profileValues(deck)` (gd01 `app.cpp`) / `enhancedProfile(deck)` (port_settings).
Steam Deck is the desktop build at runtime with the handheld (`deck`) profile; Android
is always handheld.

| Setting | Linux | Steam Deck | Windows | macOS | Android |
|---|---|---|---|---|---|
| `graphics_backend` | vulkan | vulkan | vulkan | sdl3 | vulkan |
| `audio_backend` | miniaudio | miniaudio | miniaudio | miniaudio | miniaudio |
| `max_fps` | 60 | 60 | 120 | 120 | 60 |
| `touch` | off | on | off | off | on |
| `shake_whammy` | off | on | off | off | on |

Notes: The "Steam Deck" column is the handheld profile (`deck=true`); a non-Deck Linux
build is the Linux column. On macOS there is no Vulkan default (sdl3) and no OpenGL HDR
path, so True HDR falls back to Emulated at runtime. These values match both hosts
(gd01 `profileValues`, gd04 `enhancedProfile`); they supersede the older GDALL-050
"local elsewhere" default (wave 20261118 §4).

## Onboarding notes

When adding Enhanced options to a new game:

1. **Pick the host by lineage.** GH3-family / newest games → copy the `port_enhanced.cpp`
   + `port_settings.{cpp,h}` host (gd04/06/07/10). GH1/GH2-family → copy
   `enhanced_panel` (gd02/03/05) or `select_panels` (gd01). Keep names, order, layout
   and defaults identical to the catalogue above; add only the game's own extras.
2. **Register in the store.** Add an `EnhancedSetting{key, values, lo, hi}` (or the
   `enhancedDefaults` entry in gd01) — first value is the base default; set the real
   start-up default through the per-OS profile. Add the key to Export / Load validation.
   Keep shared keys identical across games (`song_speed`, `track_speed`,
   `easy_chords`, `hdr`) so the launcher/profiles can share them.
3. **Gate correctly.** Use `needs` (and `needsValue` where the host supports it) so
   dependent rows hide — e.g. HDR sliders only under True HDR. If the host has no
   `Slider` kind (port_enhanced), do not add sliders; use a Choice or route the value
   through a tool/button.
4. **Respect the Modes.** Any new graphics option should be set sensibly by the seven
   Mode presets (`enhancedPreset`): lowest / original / highest buckets.
5. **Defaults stay original-look.** New effects default off; new cheats/accessibility
   default off; a file without the key loads as the default.
6. **Never bump the launcher version for this.** Adding or changing Enhanced options is
   not a release; bumping the launcher version is a public release and happens only when
   the owner explicitly asks.

## Sources

- `docs/guidelines/universal-menus.md` — §1.2.2 Enhanced options, option table, Backends.
- `docs/guidelines/universal_mechanisms.md` — HDR, Accessibility (Easy Chords), removed options, per-game decisions.
- `docs/guidelines/engine-backend-guidelines.md` — Graphics/Audio backend rules (referenced).
- gd01 host: `gd-game-01/engine/src/ui/select_panels.{cpp,h}`; store/defaults in `gd-game-01/engine/src/app.cpp` (`enhancedDefaults`, `profileValues`, `enhancedPreset`).
- gd02/03/05 host: `gd-game-0{2,3,5}/engine/src/ui/enhanced_panel.{cpp,h}`.
- gd04/06/07/10 host: `gd-game-0{4,6,7,10}/engine/src/ui/port_enhanced.cpp` + `ui/port_settings.{cpp,h}` (`enhancedSettings`, `enhancedProfile`, `enhancedPreset`, `EnhancedStore`).
- HDR sliders: `gd-game-01/engine/src/ui/select_panels.cpp:1175`, `gd-game-02/engine/src/ui/enhanced_panel.cpp:413`; levels consumed in each game's `app.cpp` / renderer (#80).
