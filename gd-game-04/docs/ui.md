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
"not given" [I: helper defaults rely on it]; so does a value that is not
set at all (displaySprite's `z_priority = <z>` without a z: the element keeps
its parent's z + 1) [I: only so is the select-difficulty highlight over its
backdrop].
Flips are set anew by every props call: flip_h / flip_v given, kept with
preserve_flip, else cleared (element flags 0x2000000 / 0x4000000) [C apply
0x8221C790]; the setlist's first page, flipped by displaySprite, is
unflipped by the later `SetScreenElementProps {id = sl_page1_head
z_priority = ..}`. Sprite `blend` (displaySprite's blendmode): add,
subtract / sub (destination - source x alpha), anything else alpha blending
[I: the setlist's subtract shoeprints].

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
  colour rgba with a*=world_alpha [C 0x822190D8]. A sprite with dims and no
  texture is a plain rectangle in its colour (the detailed stats' paper and
  row highlights) [I].
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
- WindowElement: container that clips children to its rect. Every
  WindowElement starts with just [-1 -1] and dims = the screen (not only
  `root_window`) [C ctor 0x82315138] (the store's info text windows are
  placed by their corner).
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
Draw order: z_priority is one global key for sprites and texts (one list,
insert 0x8238D7D0, ascending; no separate text pass) [C]. An element without
z_priority gets **its parent's z + 1** (0 without a parent) when attached
(SetParent 0x8221D5F0 -> SetZTree 0x8221C160, recursive for unset children);
an explicit z (flag 0x8000) sticks [C]. Text shadows draw at z - 0.005 [C].
Ties: an element's draw instance is (re)inserted into the one list when it
becomes visible or its z changes, *before* the first equal z, so the one
inserted last draws behind its equals [C insert 0x8238D7D0, SetPriority
0x8238F6F8]; elements updated in the same frame go in tree order. The port
keeps a sequence number per element, renewed when it reappears or its z
changes (the choose-band highlight bar, z 3, is behind its menu's texts,
also z 3). Hidden element hides its subtree [I].

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
element as their object and receive `id` = the element's id unless the
handler's params name their own `id` (the main menu's empty vmenu items
recolour `{id = main_menu_career_text}`) [C data; I for the default].
`GetTags` (member, or a plain `gettags` in a script running on the element)
also gives the element's `id` [I: retail_menu_focus without an id
recolours `<id>` after its gettags].

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
- new_menu focuses its VMenu before the items exist: the first focusable
  item added takes the focus at the menu's next update or event, once the
  script has made its siblings (the pause menu's item containers recolour a
  text created after them) [I].
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
Width [C measure 0x8232ADF0]: a glyph adds width - bearing, a space the
font's space width; every character then adds the tracking. A TextElement's
font_spacing defaults to -1 = the font's own bearing (header +4) and
tracking (+8); given, it replaces the tracking and the bearing is 0 [C ctor
0x8231B788: +0x270 = -1; 0x8232B5E8]. Draw [C 0x8232B5E8]: x0 = pen - glyph
x offset - bearing, x1 = x0 + width, next pen = x1 + advance adjust +
tracking; a space moves the pen by its width - bearing + tracking; the glyph
top is line height - (height - baseline) - vShift of the glyph's own font
(the buttons font's for `\b`). (fontgrid_title_gh3 has bearing -2, tracking
-2: texts were 2 px a glyph too narrow before.) Glyph codes above 0x2100 are
scaled by 2.2 [C 0x8291E1A0]. `\b<n>`: buttons font glyph buttonMap[n],
drawn white (alpha kept) unless the text colour is black (a shadow) [C];
`\s<n>` the current font's buttonMap[n]. Button and colour numbers are
base-32 digits (0-9, a-v) [C 0x8228E0B0 writes them so].
SetText [C 0x8231A2C8]: `\m<n>` becomes `\b<x>` with x = the second value of
`meta_button_map[n]` (the Xenon column; m0 -> b4 green, m1 -> b5 red)
[C 0x8228E0B0 builds the table from the global], `\_` a space.
Shadow (`shadow`, `shadow_offs`, `shadow_rgba`, off with `no_shadow` /
`noshadow`): the text again at the element's screen position + shadow_offs
(not scaled), in shadow_rgba (its alpha x the element's), colour codes
ignored, at z - 0.005 [C 0x8231A8C0].
TextBlockElement [C wrap 0x8231B260]: words (each with its trailing space)
fill a line while the line's width (unscaled, x internal_scale) stays
within dims.x; `\n` breaks; a line's last space is dropped; a colour code
carries to the next line. Lines are lineHeight x line_spacing apart, placed
in the dims by internal_just; allow_expansion grows the height to the lines
[I] ("PRESS ANY BUTTON TO ROCK..." breaks after ANY, as on the 360).
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

## 8. Loading screens and texture-slot movies

- `DisplayLoadingScreen '<name>' [spin_texture = '<img>' spin_x spin_y]
  [localized]` shows `IMAGES/LOADINGSCREENS/<name>.img.xen` stretched over the
  screen, over every element, until `HideLoadingScreen` [C 0x8218DDE8 /
  0x8218E0A0]; guitar_startup's boot_legal (1024x512 -> 1280x720, the logo at
  x 272-620 / y 231-462 as on the 360) with the load_wheel turning, drawn
  96 x 96 at (spin_x, spin_y) [C: 00a_boot_legal; I: one turn a second].
- Movies on a texture slot (not PlayMovie's modal boot movies, slot 0):
  `LoadMovieIntoBuffer {buffer_slot movie}` + `PlayMovieFromBuffer
  {buffer_slot textureslot texturepri [no_hold] [wait_until_rendered]}`, or
  `PreLoadMovie {movie textureslot texturepri [no_looping] [no_hold]}` +
  `StartPreLoadedMovie {textureslot}`; IsMoviePlaying / IsMoviePreLoaded /
  KillMovie / PauseMovie / ResumeMovie by textureslot. The picture draws full
  screen at z = texturepri among the elements; it loops unless no_looping /
  no_loop; no_hold drops it at its end [C scripts create_loading_screen,
  win_song, GuitarEvent_SongFailed_Spawned, PlayMovie_EndCredits; I flags].
  `wait_until_rendered` holds the script until its first picture is on the
  screen [I: create_loading_screen relies on it to show LOADING before the
  song loads]. The port decodes on a thread (video/movie_player.h
  startStream / takeFrame) without the sound.
- The song's loading screen (10a): loading_flying.bik + a random
  loading_screen_tips text block + the LOADING letters. It is up for the
  frames the scripts take between create_loading_screen and its destroy
  (one or two in the port: its loads finish at once); the port's own song
  load then runs in one long frame after it.

## 9. The menus' 3D scene (ui/menu_scene.h)

setup_bg_viewport makes `bg_viewport` (a ViewportElement under the
off-screen `ui_clip_root`, shown full screen on the 360); PlayIGCCam {name
viewport = bg_viewport [controlscript] [LockTo pos Quat FOV] [time]} puts a
camera on it: a pose at once, or a control script spawned on it that moves
it with `ccam_domorph {<pose> time motion}` / `ccam_waitmorph`
(store_camera_script with store_camera_props) [C scripts]. The port draws
the zones map's current zone (SetPakManCurrentBlock map = zones:
z_soundcheck for the store, the character select and the hub) with the
venue renderer behind the elements while bg_viewport is shown and no song
plays. Poses: pos, Quat (x, y, z; w >= 0), FOV, LockTo world; morphs use the
elements' curves [I]. Not drawn yet: the composite objects on it (the
character, CreateCompositeObject / obj_* members, the store's instrument
models).

## 10. Reference comparison (the 360 under Xenia, 2026-10-29)

Captures: `~/.local/share/gd-engine/refcaps/gd04/` (INDEX.md). Port shots
(not in the repo): the release-shaped build (`-DGD04_VIDEO=ON`) headless
with `GD04_KEYS` scripted buttons, a fresh save in its own XDG_DATA_HOME,
`GD04_NO_MOVIES=1`, `GD04_SCREENSHOT_AT`; the vault / faceoff / cheats
walks for theirs; `GD04_AUTOPLAY=1` (with `GD04_END_AT` for the short runs)
for the results. Every screen is ~7 % lighter in the port's mid tones (the
360 picture is about the port's ^1.17: 101 -> 87, 196 -> 186, 214 -> 208)
[I: the console's display gamma; render side, not changed here].

| Screen | Capture | Port shot | Differences found | Fixed / why not |
|---|---|---|---|---|
| Legal | 00a_boot_legal | 00a_legal | black: DisplayLoadingScreen a stub | fixed (§8) |
| Press any button | 01_press_any_button | 01_press_any_button | the text block on one line | fixed: text block wrap (§6) |
| Controller instructions | 02_controller_instructions | 02_controller_instructions | texts 2 px a glyph narrow; the caption box black | fixed: bearing / tracking (§6); the 4x4 `white` decoded black (formats.md "Tiling") |
| Main menu | 03_main_menu | 03_main_menu | no cream highlight, CAREER not red, no shadows, no button glyphs | fixed: `white`, handler `id`, shadows, \m / \b glyphs; the eyes' glow phase is random (glow_menu_element); the GAMEPADS pill is the port's |
| Options | 04_options | 04_options | AUDIO unreadable (focus colour) | fixed: handler object, GetTags `id` (§5). The "V2" bottom left is in no script (a C-side label): not done. ENHANCED is the port's |
| Choose band | 06_store_choose_band | 06_choose_band | the selected row's text under its bar | fixed: draw-list ties (§3). The Glitches row and the random ticket art are the port's / random |
| Difficulty | 07a_quickplay_difficulty | 07a_difficulty | no highlight bar | fixed: unset z_priority ignored (§1), `white` |
| Setlist | 07_quickplay_setlist | 07_setlist | no torn first page / tabs, light shoeprints, dark tab buttons, random lines missing | fixed: flips per props call, subtract blend, \b white, `random` branches (qscript.md); the decorations are random |
| Cheats | 08_options_cheats, 08b, 08c | 08_cheats, 08b_cheat_entry, 08c_cheat_unlocked | rows 4-18 px off (the scrolling window, rot 1) | not done (small; VScrollingMenu 0x82317640 read, not matched) |
| Select controller | 09_multiplayer_select_controller | 09_select_controller | two guitar icons vs one pad | not a bug: the walk's two players count as guitars |
| Loading screen | 10a_loading_screen | 10a_loading | none drawn | fixed: texture-slot movies (§8); up for 1-2 frames (the port's script loads are instant, its song load stalls after) |
| Song intro | 10b-10g | 10c_intro_title | title and HUD ~2 s early (no load time), cameras | not mine (song start, venue) |
| Pause | 20_pause_menu | 20_pause | RESUME not white | fixed: the first item's focus deferred (§5). The background is not blurred / dimmed and the highway is hidden: render / song side, not done |
| Pause options | 21_pause_options | 21_pause_options | the pause menu stayed (for_options lost) | fixed: struct parameters merged (qscript.md). CALIBRATE WHAMMY: the port's pad counts as a guitar |
| Restart warning | 22_pause_restart_warning | 22_restart_warning | none drawn before | fixed by the above |
| Newspaper | 30_results_newspaper | 30_newspaper | RETRY SONG in the short runs | right: shown unless the score is a new top score (create_newspaper_menu); the full-song run matches |
| More stats | 31_results_more_stats | 31_more_stats | page body, rows, section names missing; "1 X" | fixed: multi-line conditions and default structs, keys naming globals, pointer fields, untextured sprites, FormatText's 3 decimals (§1, qscript.md). The per-section counts (p1_last_song_detailed_stats, set by the 360's game code) are not filled by the port's song core: N/A, not mine |
| Top rockers | 32_results_top_rockers_name_entry | 32_top_rockers | no thousands commas | fixed: FormatText usecommas / decimalplaces |
| Enter band name | 40_store_enter_band_name | 40_enter_band_name | matches | - |
| Store | 41_store_main | 41_store_main | black background; the menu 20 px high | the 3D scene fixed (§9); the 20 px (= -spacing) not found in the layout code [C 0x82315FB0 / 0x82317640 read]: not done |
| Store guitars | 42_store_guitars | 42_store_guitars | the info text outside its window, black background, no guitar model | fixed: window defaults (§2), the camera morph; the instrument (composite objects) not done |
| Store songs | 43_store_songs | - | not reached by the scripted runs | - |
| Character select / hub | - | char_select, char_hub | (no capture) black before; now the zone, no character | the characters need CreateCompositeObject / obj_*: not done |
