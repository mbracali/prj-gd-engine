# GH3 (Xbox 360) screen elements and the front end (implemented in engine/src/ui/)

Clean-room notes; behaviour described in our own words. [C] = confirmed by an
address in default.xex or by disc data; [I] = inferred from script usage.
Script counts are over all 3,192 scripts in `qb.pak.xen` (dump made with
`qbscript.disassemble` over every script).

## 0. Usage census (what matters)

C functions called from scripts (count): CreateScreenElement 1514,
FormatText 1249, SetScreenElementProps 980, ScreenElementExists 897,
DoScreenElementMorph 588, LaunchEvent 323, GetScreenElementDims 310,
DestroyScreenElement 303, SetArrayElement 195, RunScriptOnScreenElement 117,
SetScreenElementLock 70, GetUpperCaseString 65, GetScreenElementProps 52,
BroadcastEvent 27, GetScreenElementPosition 23, AddArrayElement 21,
GetScreenElementChildren 17, SetButtonEventMappings 16, SetFontProperties 14,
SetEventHandler 13, SetMenuAutoRepeatTimes 12, ClearEventHandlerGroup 7,
ResolveScreenElementID 7, ClearEventHandler 5, SetAnalogStickActiveForMenus 4.
Addresses: CreateScreenElement 0x82171670, SetScreenElementProps 0x821720E0,
DoScreenElementMorph 0x821721B0, GetScreenElementDims 0x82172310,
DestroyScreenElement 0x82172640, RunScriptOnScreenElement 0x82171EC0,
SetScreenElementLock 0x82172258, GetScreenElementProps 0x82171740,
GetScreenElementPosition 0x82172408, ScreenElementExists 0x82172518,
LaunchEvent 0x82158188, ScreenElementSystemInit 0x8216F140.

Member functions on elements (`<id> : Fn`, run in a script attached to the
element or naming it): SetProps 240, SetTags 181, DoMorph 127, GetTags 82,
GetSingleTag 38, Die 28, Hide 27, Unhide 19, RemoveTags 5, GetProps 3,
menu_selectedindexis 2.

Element types (`Type =` in CreateScreenElement): TextElement 595,
SpriteElement 387, ContainerElement 258, VMenu 68, TextBlockElement 61,
VScrollingMenu 47, WindowElement 11, HMenu 8, ViewportElement 3, Element3D 1.
(~75 more calls pass the type through `<...>` / args.)

Helper scripts that wrap the natives (implement natives, then run these):
`new_menu`/`destroy_menu` (guitar_menu.qb), `displaySprite`/`displayText`
(menu_setlist.qb), `create_menu_backdrop`, `create_pause_menu_frame`,
`fit_text_in_rectangle`, `retail_menu_focus`/`retail_menu_unfocus`,
`menu_focus`/`menu_unfocus` (guitar_debug_menu.qb), `add_user_control_helper`,
`ui_flow_manager_respond_to_action` (menu_flow_manager.qb).

## 1. Element model

Every element has [C base ctor 0x8221DDB8, apply 0x8221C790, getprops 0x8221CCB0]:

| field | default | notes |
|---|---|---|
| id | none (auto) | checksum; `id = {parent child = N|local_id}` resolves a child by index or local_id [C GetProps/morph use; I for exact rule] |
| parent | none | element id; scripts nearly always give it (`root_window` common) |
| pos | (0,0) | pair, parent-local, in 1280x720 virtual pixels; `{(x,y) relative}` adds to current; `{.. proportional}` multiplies by parent dims; `{.. absolute}` = screen coords converted into parent space [C 0x8221C240 for morph; I same for props] |
| dims | (0,0) | base size; for sprites see §2 |
| just | [0 0] = centre | pair in -1..1 or array of keys: left/top = -1, right/bottom = +1, anything else (center) = 0 [C parser 0x8221B098; default 0 at 0x8221E0FC] |
| child_anchor | [-1 -1] | where children's pos origin sits inside this element: (ca+1)/2 * dims * scale; NOT the key `internal_just` (only menus/text read that) [C 0x82218908 default, 0x8221BF88 use] |
| scale | 1 | number or pair; `relative_scale` multiplies current [C] |
| alpha | 1.0 | float 0..1 [C] |
| rgba | [255 255 255 255] | 4 bytes 0..255 (3-element arrays allowed, alpha then unchanged/0) [C 0x8221A738 + ctor stores 0xff]; final vertex alpha = rgba.a * world_alpha [C sprite update 0x822190D8] |
| rot_angle | 0 | degrees in script, stored radians [C 57.2958 in 0x8221CCB0]; `relative_rot_angle` adds |
| z_priority | 0 | float; absolute draw key (see §3) |
| flags | | hide/unhide, flip_v/flip_h (also preserve_flip), focusable/not_focusable, block_events/unblock_events, passive_events, pause/unpause, preserve_local_orientation (child does not inherit parent rotation), no3dscaling, usegametimer/usesystemtimer, skew=(x,y), exclusive_device=N, priority=N, local_id=key, tags={..}, remove_tags, event_handlers=[..], replace_handlers [C key switch 0x8221A738 / 0x821F3660] |

Props passed as an empty struct `{}` (displaySprite's `dims = {}`) mean
"not given" [I: helper defaults rely on it].

GetScreenElementProps returns pos, scale (number if x==y else pair), alpha,
rgba, rot_angle (degrees), z_priority, just, events_blocked, plus type-specific
(sprite: texture) [C 0x8221CCB0, 0x822192B8].

## 2. Types

- ContainerElement: no drawing; dims default 0 (so child_anchor has no effect
  unless dims given). `internal_just` on containers (13 uses) is ignored [C:
  key absent from 0x8221A738].
- SpriteElement [C parse 0x82218D80, apply 0x82219C78]: texture (key ->
  .img entry of that name; UI textures are in ZONES/global.pak.xen e.g.
  main_menu_bg2, white, gradient_128, GH3_Pause_Frame_01; some in other paks),
  material, clonematerial, materialprops, blend (key), base_pass/second_pass.
  On texture set, base dims = texture width,height [C 0x82218F80]. If dims was
  given, scale is set to dims/texsize, i.e. dims = on-screen size and an
  explicit `scale` in the same call is applied after dims [I order; C both
  paths]. Without dims: size = texture size * scale. Draw: quad at world corner,
  size = dims*world_scale, rotation world rot, uv flipped by flip_h/flip_v,
  colour rgba with a*=world_alpha [C 0x822190D8].
- TextElement: font (key of a .fnt), text (wide string), scale, rgba,
  shadow / no_shadow|noshadow, shadow_offs (pair, px), shadow_rgba,
  font_spacing (extra px per glyph), space_spacing, padding_scale,
  internal_just, block_events [I from usage; parse code not decompiled].
  Base dims = measured text extents (width = sum glyph advance + spacing,
  height = font line height) [C width measure 0x8232ADF0].
- TextBlockElement: wrapped text in `dims` box; line_spacing, allow_expansion
  (grow height to fit), internal_just (alignment of lines), internal_scale,
  enforce_max_width [I usage; keys C in 0x82310000 range].
- VMenu / HMenu (same class, byte vertical=1/0) [C ctors 0x82315AA0 /
  0x82315B08]: internal_just (default [0 0] = centre), regular_space_amount
  (default 0 = off), padding_scale (default 1), spacing_between (default 0),
  dont_allow_wrap (wrap on by default), exclusive_device; selected index
  starts at -1 [C base menu ctor 0x823156D8]. Layout and focus: §5.
- VScrollingMenu: a window that scrolls its VMenu child to keep the focused
  item visible; scroll_speed, num_items_to_show, reset_window(_top/_bottom)
  [C keys at 0x82317348; behaviour I].
- WindowElement: container that clips children to its rect. `root_window` is a
  WindowElement with just [-1 -1], pos (0,0), dims = screen [C 0x82315138].
- ViewportElement (3D view in a rect), Element3D (model), MovieElement: rare.

## 3. Transform composition and draw order [C 0x8221BF88, 0x8221AFC8]

For element E with parent P (world values w*):
```
wscale(E) = wscale(P) * scale(E)            (per axis)
walpha(E) = walpha(P) * alpha(E)
wrot(E)   = wrot(P) + rot(E)   (unless preserve_local_orientation)
anchor    = (P.child_anchor + 1)/2 * P.dims * wscale(P)   // default = P's top-left
off       = anchor + pos(E) * wscale(P)
wpos(E)   = corner(P) + R(wrot(P)) * off
corner(E) = wpos(E) - R(wrot(E)) * ((just(E)+1)/2 * E.dims * wscale(E))
```
corner = top-left of E's rectangle in screen space; E is drawn at corner with
size dims*wscale. Root: corner (0,0), scale 1, alpha 1.
GetScreenElementDims returns width = dims.x*scale.x, height = dims.y*scale.y
using the element's own (not world) scale, truncated to int [C 0x82172310].
GetScreenElementPosition returns ScreenElementPos = parent-local corner
(default), `absolute` = screen corner, `local` = pos [C 0x82172408].
Draw order: z_priority is a global key, not relative to the parent — scripts
give children z = container z + 1 (create_pause_menu_frame) [I]; second key is
a 3D depth for pos3d elements [C 0x8221BAF0]; equal z -> tree/creation order
[I]. Hidden element hides its subtree [I].

## 4. Morphs [C SetMorph 0x8221CEC8, step 0x8221BC28, ease 0x822A6B28, parse 0x822A7318]

DoScreenElementMorph id=.. (or `id = {x child = n}`) and `<id>:DoMorph` accept
pos (with relative/absolute modifiers), scale (number|pair, relative_scale),
rot_angle (relative_rot_angle), alpha, rgba, skew, just, time, motion.
- On a call: current values are copied as the start; given props become the
  target; time (seconds, default 0) * 1000 -> integer ms duration.
- time 0 -> target applied immediately.
- Each frame: t = elapsed_ms/duration clamped to 1; f(t) by motion:
  linear 0 (default; unknown keys such as `gentle` stay linear), smooth =
  3t^2-2t^3, ease_in = t^2, ease_out = 2t-t^2; value = start + (target-start)*f
  for pos, scale, rot, alpha, and each rgba byte.
- Clock: game timer (pauses with the game) unless usesystemtimer [C 0x822A6BD0].
- DoScreenElementMorph returns immediately (scripts follow it with
  `Wait <time> Seconds`); `<id>:DoMorph` in a script running on the element
  blocks until done (loops like glow_menu_element have no Wait) [I].

## 5. Events, focus, menus

Event handler entry: `{ <event_type> <script> params = {..} }`; several per
type allowed and run in array order (main menu items carry three `focus`
handlers) [C table add 0x821741C8/0x822FD4E0; I order]. `replace_handlers`
replaces existing handlers of the same types. Handler scripts run with the
element as their object and receive `id` = the element's id merged with
params (menu_focus uses `<id>` without being given it) [I].

LaunchEvent type=.. target=id [data={..}] queues an event to one element
(source `system`) [C 0x821578E8 calls]. BroadcastEvent sends to all.
Types used: focus 267 handlers/160 launches, unfocus 215/134, pad_choose 298,
pad_back 120, pad_up 92, pad_down 92, pad_start 51, pad_left 12,
pad_right 11, pad_l3 10, pad_square/option/option2 few.

Controller -> pad events from `xenon_pad` (menubuttonremap.qb) [C data]:
A->pad_choose & pad_btn_bottom, B->pad_back & pad_circle, Back->pad_back &
pad_select, X->pad_square/option/backspace, Y->pad_option2/space/expand,
LB->pad_l1, RB->pad_r1, LT->pad_l2, RT->pad_r2, LS/RS click->pad_l3/r3,
d-pad->pad_up/down/left/right (xenon_guitar_flip swaps up/down for lefty),
Start->pad_start. Guitar: green=A, red=B, yellow=Y, blue=X, orange=LB, strum =
d-pad up/down. SetMenuAutoRepeatTimes (0.3, 0.05) = held-direction repeat
delay/interval in s [I].
Delivery: a pad event goes to every element that currently has focus (a menu
and its selected child are both focused), filtered by exclusive_device; an
element with block_events ignores events [I; device check C 0x821F5040].

VMenu/HMenu event handling [C 0x823164E0]:
- focus (if not focused): store `controller` from data; if data.grid_index,
  select the last child whose tag_grid_x <= grid_index; if data.child_id,
  select that child; mark focused; select the current index (initially 0,
  skipping not_focusable children) and send `focus` to that child with
  data {controller}.
- unfocus: clear focused, send `unfocus` to selected child.
- VMenu pad_up / HMenu pad_left: move -1; VMenu pad_down / HMenu pad_right:
  +1 (only while focused and enabled). Move = send unfocus to old child, step
  the index skipping not_focusable children, wrap at the ends (dont_allow_wrap
  -> stays at the end), send focus {controller} to the new child
  [C 0x82315E60, 0x82315CA8].
- Tags kept: tag_selected_id / tag_selected_index (menu_selectedindexis).
Layout [C 0x82315FB0]: children in creation order. Vertical: each child's
height*scale*padding_scale stacked with spacing_between gaps (or a fixed pitch
regular_space_amount); menu width = widest child (unless dims set), height =
sum. Each child gets just = [internal_just.x, 0] and pos =
((ij.x+1)/2*menu_w, cursor + h/2); block start = (menu_h-total)*(ij.y+1)/2.
HMenu: same on the other axis. Relayout when children change;
SetScreenElementLock id ON suspends relayout, OFF re-enables and relayouts
(scripts do ON;OFF then GetScreenElementDims) [C flag 0x4000 0x8221A588; I].
new_menu defaults: VScrollingMenu dims (400,480) at menu_pos, VMenu inside
with internal_just [center top], font text_a1, font_size 0.75, then
`LaunchEvent type=focus target=vmenu`.

DestroyScreenElement id [recurse default]: destroys the element and its
subtree (children first), unlocks the parent; `dont_recurse` only the element;
`preserve_parent` keeps the parent untouched [C 0x82172640/0x82171C68].
RunScriptOnScreenElement id script params: spawns `script` with the element as
object; it dies with the element [C 0x82171EC0; I lifetime].
ScreenElementExists id -> bool.

## 6. Text

Fonts [C data]: ZONES/global.pak.xen: buttonsxenon, fontstrip_numbers_mono_sk8,
fontgrid_title_gh3, text_a3..a6, num_a7, text_a8, num_a9, text_a10(_Large),
text_a11, text_A11_Large; FONTS/text_a1.pak.xen: text_a1; FONTS/debug.pak.xen:
debug. Script use: fontgrid_title_gh3 188, text_a1 162, text_a4 92, text_a6 35,
text_a3 26, text_a11 21, text_a5 19, text_a10 17, `dialog` 15 / `small` 1
(no such .fnt on disc). Startup (guitar_startup) calls SetFontProperties
'<font>' color_tab = Default_Font_Colors for each font, and ButtonsXenon as
the buttons font.
Escape codes (backslash + char) [C measure 0x8232ADF0, draw 0x8232B5E8]:
- `\cN` N hex: N=0 back to the element colour; N>=1 colour = color_tab[N-1]
  (alpha kept from the element). Default_Font_Colors = [[200,200,200,255],
  [180,80,80,255],[80,120,180,255],[80,180,120,255],[180,140,60,255],
  [200,100,40,255],[140,100,180,255],[0,180,180,255],[0,0,0,255],[40,40,40,255],
  [90,90,90,255],[140,140,140,255]].
- `\bN` glyph N (hex) of the buttons font's buttonMap; `\mN` meta button
  (m0 green, m1 red ...) mapped to a buttons-font glyph for the platform;
  `\sN` special glyph of the current font's buttonMap; `\uX`, `\L`: consumed,
  no glyph; `\\` literal backslash; `\n` newline.
- add_user_control_helper strings: green `\m0`, red `\m1`, yellow `\b6`, blue
  `\b7`, orange `\b8`, strumbar `\bb`.
Space advance = font space width; each glyph advance + font_spacing. Glyph
codes above 0x2100 are scaled by 2.2 (button glyphs) [C 0x8291E1A0].
Localisation: no string tables; each language has its own full script pak
(qb_f / qb_g / qb_i / qb_s .pak.xen) with the strings baked into the scripts
(create_main_menu in qb_f says CARRIERE) [C data]. GetUpperCaseString for case.
fit_text_in_rectangle (script) scales a text to a box via GetScreenElementDims.

## 7. ui_flow_manager

Flow state = struct {Create, destroy, actions = [{action, flow_state |
flow_state_func(+params) | use_last_flow_state, func(+func_params),
spawned_func, transition_left/right, transition_screen{Create,destroy}}],
popup, create_params, destroy_params, is_null}. Globals:
ui_flow_manager_state[2], previous_flow_manager_state[2],
ui_flow_manager_under[2], menu_flow_locked, transitions_locked.
ui_flow_manager_respond_to_action (script, not C) needs: GotParam,
GetArraySize, StructureContains, SoundEvent (Generic_menu_back_SFX for
go_back, else ui_sfx_select), SetArrayElement arrayname=<global>
globalarray index=i newvalue=v resolveglobals=0 (write v into element i of
the global array, storing the checksum as-is), LaunchEvent unfocus/focus on
root_window for popups, spawnscriptnow, ScriptAssert, calling a script held
in a variable (`<arg> create_func ...`), menu_transition_* scripts.
Order: find action -> play sfx -> (popup: keep current under, unfocus root;
else destroy current & under, focus root) -> set previous/current -> run func
-> create new state.
