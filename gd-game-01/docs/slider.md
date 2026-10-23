# UI Slider (RndSlider + SliderPanel)

The volume sliders on `game_settings` / `pause_settings` (`gs_band.sld`, `gs_guitar.sld`, `gs_sfx.sld`).
These are the only Slider objects in the game data. Every one uses style `char`.

Tags: **[C]** = read directly from the decomp or data. **[I]** = inference.

## 1. Classes and addresses

| what | address |
|---|---|
| class registration `"Slider"` (FUN_00127298) | 0x127298 |
| ctor | FUN_001272e0 |
| dtor | RndSlider_t2_v0 0x127710 |
| Handle (script methods) | RndSlider_t2_v1 0x128828 |
| Print | RndSlider_t2_v3 0x127a30 |
| Save | RndSlider_t2_v4 0x127a88 |
| Replace (obj ref) | RndSlider_t2_v5 0x127c30 |
| Copy | RndSlider_t2_v7 0x127948 |
| **Load** | RndSlider_t2_v8 **0x127b10** |
| Drawable slot 5, **DrawShowing** [I name] | RndSlider_v5 0x127cf8 |
| Drawable slot 2 / 4 (these forward to slider_mesh) | 0x127ea0 / 0x127e50 |
| Transformable UpdatedWorldXfm [I name] | RndSlider_v1 0x127ed0 |
| `RndSlider::Component` vtable | 0x2f7880 |
| Component dtor | 0x126be8 |
| Component **Handle** (button_down) | 0x127178 → FUN_00126c10 |
| Component **SetState** (UI state → style state) | 0x126fe0 |
| `SliderPanel` (`new_slider_panel`) ctor | FUN_00151420 (0x88 bytes) |
| SliderPanel factory / register | FUN_00151558 / FUN_00151520 |
| SliderPanel Handle | SliderPanel_v1 0x151900 |
| SliderPanel builds its slider list (slot 16, after load) | SliderPanel_v16 0x1515a8 |
| SliderPanel clears its slider list (slot 14) | SliderPanel_v14 0x1516f8 |

The vtable at 0x2f7880 holds these slots [C, read from the ELF]:
- +0x04 typeinfo 0x29c2c0 (`Q29RndSlider9Component`)
- +0x0c dtor 0x126be8
- +0x14 Handle 0x127178
- +0x24 SetState 0x126fe0

## 2. Object layout (RndSlider, offsets from `this`) [C]

| off | field |
|---|---|
| +0x100 | the embedded UIComponent (`RndSlider::Component`). Its +0x14 points back to the slider. |
| +0x118 | `selected_pos` (int, starts at 0) |
| +0x11c | `num_steps` (int, starts at **1**) |
| +0x120 | text token (Symbol, starts empty) |
| +0x124 | bar material (Mat*, from the record's object ref; starts null) |
| +0x12c | style (Symbol). The default is `FUN_0011dcb8(ui,"slider")`. |
| +0x130 | mesh_color rgb (from config) |
| +0x140 | text_color rgb |
| +0x150 | style state Symbol: `normal`, `focused`, `selected` or `disabled` (starts `normal`) |
| +0x154 | slider_mesh (RndMesh, its own object) |
| +0x158 | bar_mesh (RndMesh) |
| +0x15c | pod_mesh (RndMesh) |
| +0x160 | pod_anim (RndTransAnim) |
| +0x164 | text (RndText) |

## 3. Serialized record (Load 0x127b10) [C]

```
u32 version              must be 1 (0 → "Sliders older than version 1 are no longer supported")
Transformable
Drawable
Symbol style             → +0x12c   ("char")
Symbol text token        → +0x120   ("BAND", "GUITAR", "gs_sound_fx" / "SOUND_FX")
ObjRef bar material      → +0x124   (cast to RndMat; '' in every record)
```

After Load or Copy, FUN_001285b0 runs:
1. FUN_00127fb8 re-reads the colours.
2. FUN_00128060 rebuilds the parts from the style.

The engine's decodeUi "Slider" branch already reads these fields (`style`, `symbol` = token, `object` = bar mat).

The placed records [C]:

| panel | sld | token | local pos | local scale | world |
|---|---|---|---|---|---|
| game_settings | gs_band | BAND | (-150,-15,32) | 1 | (25.1,-15,82.0) |
| game_settings | gs_guitar | GUITAR | (-150,-15,-16) | 1 | (24.4,-15,36.5) |
| game_settings | gs_sfx | gs_sound_fx | (-150,-15,-64) | 1 | (23.6,-15,-9.1) |
| pause_settings | gs_band | BAND | (-246,-2,-10) | x1.2 z1.1 | (-41.8,-62,45.7) |
| pause_settings | gs_guitar | GUITAR | (-246,-2,-65) | x1.2 z1.1 | (-41.8,-62,-2.7) |
| pause_settings | gs_sfx | SOUND_FX | (-246,-2,-120) | x1.2 z1.1 | (-41.8,-62,-51.1) |

The game_settings sliders have a small parent rotation and scale (world rot ≈ [.9995 0 -.017; 0 1 0; .0166 0 .949]).

Text tokens:
- `gs_sound_fx` and `SOUND_FX` localize to "SOUND FX".
- `BAND` and `GUITAR` are **not** in locale.dta, so the token itself is shown (FUN_0011db60 falls back to the token).

## 4. Style data (config.dta `slider`) [C]

```
(slider (colors (mesh_color mesh) (text_color text))
  (styles
    (char (slider_mesh "char_slider.mesh") (bar_mesh "char_slider_bar.mesh")
          (pod_mesh "char_slider_pod.mesh") (pod_anim "char_slider.tnm")
          (text "char_slider.txt") (default_bar_mat "char_slider_default.mat")
          (colors (text_color (normal .9 .9 .9) (focused .9 .9 0) (selected .9 .9 0) (disabled .4 .4 .4))
                  (mesh_color (normal .9 .9 .9) (focused .9 .9 0) (selected 0 1 0)  (disabled .4 .4 .4))))
    (mic  ... mic_slider.* ...)))    ; mic is never used in GH1
```

Resource names are resolved as `ghui::<name>` (FUN_001285e0 sprintf "ghui::%s"), looked up in `ghui/gen/resources.rnd_ps2`.

Building the parts (FUN_00128060):
- The ctor creates fresh Mesh×3, TransAnim and Text objects.
- Each one then **Copies** its template (vtable+0x44, copy type 0).
- `FUN_001dd008(pod_anim, pod_mesh)` retargets the anim onto the slider's own pod copy.
- Then `SetBarMat(+0x124)` (FUN_00128330 → `bar_mesh.SetMat`) and `SetText(+0x120)` (FUN_001282f8) run.

### Resource contents (decoded from resources.rnd_ps2) [C]

All coordinates are in the slider's local space: x right, y depth, z up.

**char_slider.mesh** (the track):
- A quad of ±40 × ±35.
- Local transform: scale (2, 1, 0.6), position (85, 0, -5). So in slider space it covers **x 5..165, z -26..16**.
- Mat `char_slider_default.mat`: tex `slider_base.tex` = `image/slider_base.png` (128×32), base colour .9, blend SrcAlpha, zmode Normal, use_environ TRUE.
- The texture itself shows a "0" label, tick marks, and an "11" label, so it already draws the 12 positions.

**char_slider_bar.mesh**:
- **Empty**: no vertices, no faces, no material. It is positioned at (132.17, 0, 0) with scale (.52, 1, .42).
- So the "bar" draws nothing. **No bar fill exists.** The value is shown only by the pod's position over the printed ticks. [C]

**char_slider_pod.mesh** (the knob):
- A quad of ±25 × ±25.
- Template local: scale (.28, .999, .9), position (133, -1, -5). That gives a **14 × 45 quad**.
- Mat `char_slider_pod.mat`: tex `slider_knob.tex` = `image/slider_knob.png` (16×32, a thin white bar), colour white, zmode Normal, use_environ TRUE.

**char_slider.tnm** (TransAnim, target char_slider_pod.mesh):
- trans_keys: frame 0 → (31, -1, -5), frame 1 → (133, -1, -5). trans_spline is TRUE, but with only 2 keys the Hermite curve is exactly linear.
- scale_keys: frame 0 → (1, 1, 1).
- No rot keys.

**char_slider.txt**:
- impactor.font, size 30, align **MiddleRight** (0x24), leading 1, template text "SAMPLE".
- Local scale (.66, 1, .5) at the origin.
- So the label's right edge sits at the slider's origin, and the track starts 5 units to the right of it.

`slider.view`, `char_slider.view` and `mic_slider.view` are editor previews only. The slider never uses them.

## 5. Drawing

### DrawShowing (0x127cf8) [C]

1. `slider_mesh.mat.color.rgb = mesh_color` (mat +0x30..0x38) and the mat is marked dirty (+0x64 |= 1).
   Only the track material is tinted. The pod and the bar are **not** tinted.
2. `text.font.mat.color.rgb = text_color` (text +0x114 = font, font +0x414 = mat).
3. If the bar mat (+0x124) is null, `bar_mesh.SetMat(ghui::<default_bar_mat>)`.
4. Draw, in this order: **slider_mesh, bar_mesh, pod_mesh, text**.
5. `bar_mesh.SetMat(+0x124)` (this restores null).

These are shared template materials and fonts: each slider writes its colour right before it draws. [I]

### World transform (0x127ed0)

After the slider's own world xfm updates, each of the 4 parts gets the slider's world matrix pushed into it (part vtable+0x14 on the transformable).
In practice, `part.world = slider.world * part.local`. [I: the exact call semantics are not traced, but this matches the layout]

### Pod position (FUN_00128408) [C]

```
frame = num_steps < 2 ? 0 : selected_pos * (1 / (num_steps - 1))     // 0..1
RndAnimatable::SetFrame(pod_anim, frame, blend 1.0)                  // FUN_001abab8
```

- TransAnim SetFrame (RndTransAnim_v1 0x1de7a8) copies the pod's local xfm and calls FUN_001de038 with reset = 0.
- Rotation keys are absent, so the rotation and scale are left alone. The scale key **multiplies** the rows by (1,1,1).
- Net effect: **pod.local.pos = (31 + 102·frame, -1, -5)**, and the pod keeps its template scale (14×45 knob). [C]
- With 12 steps, each step moves the pod 102/11 ≈ 9.27 units.

**Engine bug**: `AnimPlayer::evalTrans` (engine/src/scene/anim.cpp) *replaces* the scale with the key value. The original multiplies the rows by it when blend = 1.
If the slider is driven through evalTrans, the knob would become 50×50. Either fix evalTrans (when there are no rot keys and reset is 0, `rows *= key`), or set only the pod position.

SetFrame runs **only** from `set_selected_pos`. It does not run on Load or on `set_num_steps`.
Until a script sets the position, the pod sits at its template x = 133 (the right end). Both settings panels call `set_selected_pos` on `enter`.

### Colours per state (FUN_00127fb8 → FUN_0011dfb8(ui,"slider",style,key,state)) [C]

Colours are re-read whenever the style state changes.

| state | track (mesh_color) | label (text_color) |
|---|---|---|
| normal | .9 .9 .9 | .9 .9 .9 |
| focused | .9 .9 0 (yellow) | .9 .9 0 |
| selected (editing) | **0 1 0 (green)** | .9 .9 0 |
| disabled | .4 .4 .4 | .4 .4 .4 |

The colour multiplies the material's texture, and use_environ applies the panel ui.env ambient (the same as other UI meshes). [I]

### Text [C]

- `set_text tok` stores +0x120 and calls FUN_0011db60: localize, or use the token itself when it is missing, then `RndText::SetText`.
- Layout comes from the copied char_slider.txt (impactor.font, size 30, MiddleRight, scale .66/.5) at the slider origin.
- The text never changes with the value. There is no numeric readout: the numbers are printed on slider_base.png.

## 6. State and input

### Component SetState (0x126fe0) [C]

The base SetState runs first (UIPicture_v3, misnamed). If the state changed:
- 0 → `normal`, 1 → `focused`, 2 → `disabled`, each via FUN_001284f0.
- Then the colours are refreshed.

### FUN_001284f0 SetStyleState(s) [C]

```
if (s == selected && cur == selected) s = focused;   // selecting again toggles back
if (s != cur) { cur = s; refresh colours }
```

### Component Handle (0x127178 → FUN_00126c10), on `button_down` only [C]

The other messages return unhandled. `ui` = DAT_00344920; the button and pad come from the message.

1. If `is_select_button(button, pad)` (FUN_0011dbe8: guitar R2 / joypad X or Select):
   - `SetStyleState(selected)`. This **toggles**: focused → selected (start editing), selected → focused (confirm).
   - Send **SELECT_MSG** (`component_select`, built by FUN_0020b5d0) = `($component $panel $screen $button)`:
     component = the slider, panel = the UIManager focus panel, screen = the current screen, **button = 6** (constant).
   - Do **not** return here. Fall through to step 2.
2. If `is_back_button(button, pad)` (FUN_0011dbb0: guitar Circle / joypad Triangle) **and** the state is `selected`:
   - `SetStyleState(focused)`.
   - Send **GH_CANCEL_MSG** (`component_cancel_selection`, FUN_00130c38) = `($component $panel $screen $button=6)`.
   - Return **handled**.
3. If button == 12 (DUp) or 14 (DDown):
   - `delta = (button == 14) ? -1 : +1`. So **Up increases the value and Down decreases it**.
   - FUN_001283c8: `p = selected_pos + delta`. If `0 <= p < num_steps`, call `set_selected_pos(p)` (this moves the pod).
     There is no wrap: the value clamps silently.
   - If the value changed, send **SCROLL_START_MSG** (`component_scroll_start`, FUN_001301c8) = `($component $panel $screen)`.
   - Return **handled**, even when the value is clamped.
4. Otherwise return unhandled.

No SCROLL_MSG (`component_scroll`) is ever sent by the slider.

Up/Down only reach the slider through SliderPanel, which happens only while it is `selected`. Otherwise the panel navigator consumes them first (UIPanel order: navigator → panel script → focused component).

### SliderPanel Handle (0x151900) [C]

- Method **`slider_selected`**: returns the first slider in the panel's list whose style state is `selected`, or 0. The type is an object, so scripts use it as a boolean.
- On `button_down` with **DUp(12), DDown(14) or is_back_button** (FUN_00151728):
  - If a slider is `selected`, forward the message to that slider's Component Handle (above) and return its result.
  - Otherwise fall through.
- Everything else, or the fall-through, goes to the normal UIPanel Handle (0x207748): navigator, then the panel script, then the focused component.
  So **Select** reaches the focused slider by the normal path, and Up/Down with no slider selected move focus through the `(navigator (vertical …))`.
- The slider list is built in slot 16 after load. It holds every panel component that dynamic_casts to `RndSlider::Component` (typeinfo 0x29c2c0).

The resulting user flow:

| state | Up/Down | Select (fret1) | Back (fret2) |
|---|---|---|---|
| focused (yellow) | navigator moves focus | → selected (green), SELECT_MSG | screen back handler (leaves screen) |
| selected (green) | ±1 step, SCROLL_START_MSG if changed | → focused, SELECT_MSG again ("confirm") | → focused, GH_CANCEL_MSG |

Lefty guitar players have DUp and DDown swapped before the UI sees them (see ui_system.md §4).

## 7. Script methods (RndSlider Handle 0x128828) [C]

| method | effect |
|---|---|
| `set_text sym` | FUN_001282f8: store the token and localize it into the text |
| `set_num_steps n` | FUN_001283c0: store only. It does **not** move the pod or clamp the position. |
| `set_selected_pos i` | FUN_001283a0: store with **no clamp**, then do the pod SetFrame (§5) |
| `selected_pos` | returns int +0x118 |
| `num_steps` | returns int +0x11c |

Anything else goes to the Drawable, then Transformable, then Object handlers (`set_showing`, `showing`, …). There are **no** `set_current` / `current` methods; that string belongs to ThreadTask.

On SliderPanel: `slider_selected`. All the other panel methods are inherited (UIPanel/GHPanel).

## 8. What the settings scripts do (options.dta `SETTINGS_PANEL_HANDLERS`) [C]

`game_settings` is a `new_slider_panel` with `(navigator (vertical gs_band.sld gs_guitar.sld gs_sfx.sld gs_stereo.btn gs_left_p1.btn gs_left_p2.btn))` and `(has_lefty TRUE)`. `pause_settings` is the same without the lefty rows, and with `(has_lefty FALSE)`.

`SLIDER_LEVELS` = 12.

Panel handlers:
- `(focus gs_band.sld)`.
- **finish_load**: `set_num_steps 12` on all three sliders.
- **calc_slider_pos v** = `int(v * 11 + 0.5)`. **reset_slider_pos s v** = `s set_selected_pos (calc v)`.
- **enter**:
  - reset_slider_pos from `options get_band_volume / get_guitar_volume / get_fx_volume`.
  - Store `orig_pos.band/.guit/.sfx` = the current selected_pos.
  - Set the toggles (`gs_stereo.tog`, the lefty toggles).
- **get_slider_volume s** = `selected_pos / 11` (a float 0..1).
- **SELECT_MSG** on a slider: `orig_vol = get_slider_volume slider`. This runs on both the enter-edit press and the confirm press. On the buttons, it toggles stereo/lefty. It returns kDataUnhandled.
- **GH_CANCEL_MSG**: `reset_slider_pos <that slider> orig_vol`, which restores the value from when editing started.
- **SCROLL_START_MSG**: `$vol = selected_pos/11`, then `play_sfx slider_band|slider_gtr|slider_sfx (volume $vol) (stop TRUE)`. This previews the new level.
  The live volume is **not** applied while scrolling.
- **set_volumes**: if `slider_changed` (any selected_pos ≠ its orig_pos.*), call `options set_band_volume/set_guitar_volume/set_fx_volume` with get_slider_volume. Returns 0.
- **FOCUS_MSG**: `focus_component = $new_focus`.
- **poll** helpbar:
  - If `slider_selected`: `(fret1 help_confirm "CONFIRM") (fret2 help_cancel "CANCEL") (strum help_adjvol "ADJUST VOLUME")`.
  - Else on a stereo/lefty button: `(fret1 help_onoff)(fret2 help_back)(strum help_updown)`.
  - Else: `(fret1 help_select)(fret2 help_back)(strum help_updown)`.

Screen `game_settings` BUTTON_DOWN_MSG:
- When `!{{ui panel game_settings} slider_selected}` and `meta is_back_button`: `backwards_anim`, `synth play_sequence button_back`, `$this set_volumes`, `autosave_goto options`.
- While a slider is selected, Back falls through to the panel, which cancels the edit.

Screen `pause_settings` BUTTON_DOWN_MSG: on is_back_button it runs `set_volumes` and then `ui goto_screen pause`.
The script does **not** check slider_selected here [C]. Because the screen handler runs before the panel, Back while editing in the pause menu leaves the screen, and the volumes are applied from the current (uncancelled) positions. [I: follows from the routing order in ui_system.md §1.5]

Global sfx.dta: SELECT_MSG plays `button_select`. There is no handler for SCROLL_START or GH_CANCEL. FOCUS_MSG plays the toggle sound.

## 9. Implementation checklist for engine/src/ui

1. **ComponentObj "Slider"**:
   - Fields: `pos=0`, `steps=1`, `styleState`, `textToken`.
   - Methods: `set_num_steps`, `num_steps`, `selected_pos`, `set_selected_pos`, `set_text`.
   - SetState mapping and the selected toggle, as in §6.
2. **new_slider_panel**:
   - The `slider_selected` method.
   - The button_down pre-filter: DUp/DDown/back go to the selected slider before the navigator.
3. **Slider input**:
   - Select toggles and sends SELECT_MSG (button 6, then falls through).
   - Back while selected sends GH_CANCEL_MSG.
   - Up +1 / Down -1, clamped to [0, steps-1]; SCROLL_START_MSG only when the value changed.
4. **Draw**, in slider-local space × slider world:
   - Track: the `char_slider.mesh` quad tinted with mesh_color[state].
   - Pod: the `char_slider_pod.mesh` quad at x = 31 + 102·pos/(steps-1) (0 when steps < 2), y -1, z -5, with its template scale.
     Before the first set_selected_pos it sits at x = 133.
   - Label: localized token, `char_slider.txt` style, tinted with text_color[state].
   - Skip the bar (empty mesh).
5. Sound: `play_sfx slider_*` comes from the script. It needs the sfx entries `slider_band`, `slider_gtr` and `slider_sfx` (they are not in extracted_dta; see audio_sfx.md).
