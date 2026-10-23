# Credits screen (CreditsPanel + RndCreditList): RE spec and implementation plan

Scope: Options → Credits (`ui goto_screen credits`, `ghui/gen/options.dta:35-36`). This closes
`docs/career_e2e_plan.md` G8 ("credits panel is empty in our engine"). GH1 shows credits **only**
from Options; there is no roll after the career.

- **[C]** means confirmed from code or data. **[I]** means inferred.
- Addresses are EE virtual addresses in `original_files/SLUS_212.24`.
- Vtable slot numbering follows `docs/select_panels.md` §0: the entries start at `+0xc`, each 8 bytes, so
  `vtbl+0x14` is slot 1 and `vtbl+0x24` is slot 3.

---------------------------------------------------------------------------------------------------
## 1. Pieces and where they live

| piece | what | source |
|---|---|---|
| `new_credits_panel credits` | panel verb and instance; `(focus credits.lst)`, enter `{meta_music_stop}`, exit `{meta_music_start}` | `ghui/gen/options.dta:753-765` [C] |
| `new_gh_screen credits` | `(panels meta credits helpbar)`, helpbar `fret2 help_back`, handler `go_back` → `{$this backwards_anim} {ui goto_screen options}` | `options.dta:766-785` [C] |
| `ghui/credits.gh` | `CreditList credits.lst` (display_num **15**, element spacing **25.0**, pos (0,0,222.526)), `meta.cam`, `credits.view`, `ui.env` | `tools/ui_panels.py list extracted/ghui/credits.gh` [C] |
| `config/credits.dta` | the credit rows (`config/gen/credits.dtb` in the ARK) | string 0x308358, loaded by CreditsPanel Load [C] |
| ghui config `components credits` | `(fade_offset 2) (max_step 0.018) (element (text "credit_title.txt") (text "credit_center.txt") (text "credit_name.txt") (text "credit_center_name.txt"))` | `ghui/gen/config.dta:626-637`, read by `FUN_002242e0("ghui","components","credits")` in the RndCreditList ctor `FUN_0012dc00` [C] |
| text templates | `credit_title.txt`, `credit_center.txt`, `credit_name.txt`, `credit_center_name.txt` and their fonts and mats | `ghui/gen/resources.rnd_ps2` (the `ghui` ObjectDir, string 0x3050f0) [C] |
| music | `sfx/streams/credits.vgs`: 2 channels, 44100 Hz, 315002 blocks ≈ **200.0 s** | string 0x3083a8; header read from MAIN_0.ARK @0x5eb2fac [C] |
| sfx | `SCROLL_MSG` plays `button_toggle` **unless** `{ui focus_panel}` is `{ui panel credits}` | `ghui/gen/sfx.dta:30-44` [C] |

---------------------------------------------------------------------------------------------------
## 2. Data source and format [C]

`config/credits.dta` is a flat list of rows. It has **907 rows** (`CreditsPanel_v1` @0x142600 returns
the array size). A row's arity picks the template(s) that show it:

| arity | count | example | shown by |
|---|---|---|---|
| 0 `()` | 186 | blank line | nothing (all four columns return `""`) |
| 1 `("X")` | 75 | `("PROGRAMMERS")` | col 1, `credit_center.txt` (section heading) |
| 2 `("role" "name")` | 349 | `("Game Systems" "Dan Schmidt")`, `("" "Marc Flury")` | col 0 `credit_title.txt` = role (only if non-empty), col 2 `credit_name.txt` = name |
| 3 `("" "text" "")` | 297 | `("" "© 2005 Shaimus" "")` | col 3, `credit_center_name.txt` = the middle string |

Provider text, `CreditsPanel_v0` @0x142500 (`Text(row, col)`, provider vtable slot 0; `+0x88` = the
loaded DataArray). Its fallback is 0x308310 = `""`:
```
col 0: size==2 && str(row[0]) != ""  -> row[0]
col 1: size==1                       -> row[0]
col 2: size==2                       -> row[1]
col 3: size==3                       -> row[1]
else ""
```
The column index is the position in the config `element` list: 0 title, 1 center, 2 name,
3 center_name. `FUN_0012d968` loops the slot's elements with j = 0..3. `CreditListSlot_v2` @0x12d5a8
calls `provider->Text(row, j)` (vtbl+0xc) and then `RndText::SetText` (`FUN_001d8c48`).

Notable rows:
- Rows 0-14 are blank (15 leading blanks = display_num), so the screen starts empty.
- Row 15 is "GUITAR HERO CREDITS".
- Row 864 is the easter egg "Did you go out and get a real guitar yet?". After 23 blanks, row 888 is "How about now?".
- Rows 889-906 are blank (18 trailing).
- The strings use `\q` (the DTA quote escape) and the Latin-1 `®` / `©`. All the credit fonts have `©®` in their `chars`.

---------------------------------------------------------------------------------------------------
## 3. Text templates, fonts, colours [C unless marked]

From `resources.rnd_ps2` (decoded with `tools/rnd_classes/material.py`):

| template | font → mat (texture) | align | size | wrap | leading | local xfm | mat colour |
|---|---|---|---|---|---|---|---|
| `credit_title.txt` | `credit_title.font` → `credits_title.mat` (serif.tex) | 0x24 MiddleRight | 20 | 290 | 0.7 | scale 1.1, pos (-8,0,0) | (0.8,0.8,0.8,1) |
| `credit_center.txt` | `credit_center.font` → `credits_center.mat` (impactor.tex) | 0x22 MiddleCenter | 25 | 512 | 0.7 | scale (1.0,1.4,1.2), pos 0 | (0.7,0.4,0.0,1) orange |
| `credit_name.txt` | `credit_name.font` → `credits_name.mat` (serif.tex) | 0x21 MiddleLeft | 20 | 290 | 0.7 | scale 1.1, pos (1,0,0) | (1,1,1,1) |
| `credit_center_name.txt` | `credit_center_name.font` → `credits_center_name.mat` (serif.tex) | 0x22 MiddleCenter | 20 | 600 | 0.7 | scale 1.1, pos 0 | (1,1,1,1) |

- All four have `markup=True` and `caps_mode=0`. The credit data has no `<` tags.
- Fonts: cell 25x28. `space` is 0.02, except credit_center, which uses 0.04. The kerning table is empty (a single a/b pair with amount 0).
- Mats: `use_environ=True`, z-mode Disable, blend SrcAlpha, stage "Add (Multiply)".
- Text vertex colours are white, except `credit_center.txt`, whose colour is **(0,0,0,0)**. If the vertex colour were used, every heading would be invisible. So the colour on screen comes from the **mat colour**, lit by the environ [I]. Grey roles, orange headings and white names match what the game shows [I].
- **Layout**: two columns about the list's x=0.
  - The role ends at x=-8 (right-aligned) and the name starts at x=+1 (left-aligned).
  - Headings and 3-tuples are centred.
  - Line height for 20-size text is 20*28/25*1.1 ≈ 24.6. This is about the row spacing (25), so long wrapped entries overlap the next row, as in the original.
- **Alpha**: each element's alpha is written straight into its **font's mat** alpha. `CreditListSlot_v3` @0x12d5f8 does `text+0x114` (font) → `+0x414` (mat), then `mat+0x3c = a` and `mat+0x64 |= 1`. This runs just before each element is drawn (`FUN_0012da00`). So the element alpha = the slot alpha × 1.0.

---------------------------------------------------------------------------------------------------
## 4. RndCreditList (class "CreditList", ctor `FUN_0012dc00`, vtable 0x2f7bd0..) [C]

Object fields:
- `+0x100`: the config array.
- `+0x104`: UIComponent.
- `+0x120`: provider.
- `+0x124`: the scroll state S.
- `+0x144`: display_num.
- `+0x148`: spacing.
- `+0x14c`: the slot pool.
- `+0x15c`: the visible slot vector.
- `+0x16c`: the spare slot.
- `+0x170`: fade_offset.

The defaults are display 5 and spacing 25. Load `RndCreditList_t2_v8` @0x12e2e8 reads both from the .gh (15 / 25).

- **Slot** (`FUN_0012d668`, 0x1c bytes): `[0]` a RndView (typeinfo 0x2c82e0), used as the row group; `[1..4]` the element vector; `[5]` alpha (1.0).
  - `FUN_0012ed08` builds one slot. For each `(text "<tmpl>")` in `element`, `FUN_0012d378` makes a RndText (0x2c4be0) and **Copy**s `<tmpl>` from the `ghui` dir into it.
  - The copy is parented to the group's transform (`FUN_001da100`). So element world = list world · T(0,0,-y) · template local [I for "Copy keeps the template's local xfm"].
- **Init** (`RndCreditList_v6` @0x12e9c0, called at the end of Load): fade_offset ← config.
  - `FUN_0012ea50` creates display_num (15) visible slots and 1 spare.
  - `FUN_0012ec40` places slot k (1-based) at z = -spacing·k.
- **S** (`+0x124`; ctor `FUN_0012f658`):
  - `[0]` the owner list.
  - `[1]` pos: the data row in the first slot.
  - `[2]` target.
  - `[3]` sel: the in-window offset.
  - `[4]` frac (float).
  - `[5]` dir.
  - `[6]` moving.
  - `[7]` max_step = 0.018 (config `max_step`, 0x305120; the message `set_max_step` at 0x3051b0 also sets it, `RndCreditList_t2_v1` @0x12fbe0, but no script sends it).
  - All indices wrap modulo NumData (`FUN_0012fb40`).
- **Fill** (`FUN_0012ef30`, `FUN_0012eed0(i)`): slot i shows row `wrap(pos + i)`. Index i == display_num uses i-1.
- **Reset** (`FUN_0012e980(list, 0)` → `FUN_0012f740`): pos = min(0, NumData-display) = 0, sel = 0, target = pos. Then it refills. It does **not** clear frac/dir/moving.
- **Scroll(n)** (`FUN_0012f7c8`):
  - If a move is in progress in the other direction, it returns.
  - If NumData ≥ 6 and the circular distance(pos, target) ≥ NumData/3, it returns.
  - sel += n.
  - sel < 0: target += sel (clamped ≥ 0) and sel = 0.
  - sel > maxSel (= min(display, NumData) - 1): target += sel - maxSel, clamped ≤ NumData - display, and sel = maxSel.
  - Otherwise it sends the scroll msg with done=1.
- **Step**, once per Poll (`FUN_0012f9e8`; the UIComponent Poll `FUN_0012db98` → `FUN_0012f0b0`):
  ```
  if !moving && pos == target: idle
  if frac == 0:
      dir = pos < target ? +1 : -1            // FUN_0012f9d0, not circular
      pos = wrap(pos + dir)
      rotate (FUN_0012f290): dir>0 -> spare appended at the end, filled with row wrap(pos+display-1)
                             dir<0 -> spare inserted at the front, filled with row pos
      moving = 1
  d    = circular_distance(pos - dir, target)  // FUN_0012f958, >= 1 while moving
  frac += clamp(d * 0.1, -max_step, +max_step)  // = +0.018 every poll here
  if frac >= 1:
      frac = 0;  moving = (target != pos)
      FUN_0012f4b0(dir, done = target == pos): dir>0 -> first slot removed -> spare
          and send component_scroll (component, 0, screen, dir, done) to the UIManager
      dir = 0
  ```
  So each row takes exactly **56 polls**. 0.018·55 = 0.99 and 0.018·56 = 1.008; the 0.008 overshoot is discarded.
- **Placement and fade** (`FUN_0012f0b0`). It runs every Poll after the step:
  ```
  fadeTop = spacing * fade_offset                       = 50
  fbs     = (display + 1 - fade_offset) * spacing      = 350
  off     = spacing * min(frac*dir, 1); if off < 0: off += spacing
  for k = 1.. (each slot in the visible vector, 15 or 16 while moving):
      y = spacing*k - off;   slot.localPos.z = -y       (FUN_0012ecd0 keeps x,y)
      alpha = y < fadeTop ? y/fadeTop
            : y > fbs     ? 1 - (y - fbs)/((display+1)*spacing - fbs)
            : 1
  ```
  With dir=+1, y shrinks, so the **rows move up the screen**. The top two rows fade out and the bottom two fade in.
- **Draw** (`RndCreditList_v5` @0x12e3e8): for slot index i < NumData, `FUN_0012da00(1.0, slot)` runs over each element (title, center, name, center_name): `SetAlpha(1.0*slot.alpha)` and then Draw. The extra bounding-box draw when `+8 != 0` is an editor/debug path [I]. Skip it.

---------------------------------------------------------------------------------------------------
## 5. CreditsPanel (verb registered by `FUN_00141828`, factory 0x2a0548 → ctor `FUN_00141700`) [C]

It has 3 vtables: the CreditListProvider part at +0, UIPanel at +4 (`+0x8`), and +0x24.

Fields:
- `+0x84`: the pending file load.
- `+0x88`: the credits DataArray.
- `+0x8c`: the `credits.lst` list.
- `+0x90`: the music state.
- `+0x94`: the Stream.
- `+0x98`: the fader (panel ptr + LinearInterpolator).

| slot | addr | behaviour |
|---|---|---|
| 13 Load | 0x141860 | base load, then an async load of `config/credits.dta`; music state = 0 |
| 15 IsLoaded | 0x1418a8 | base && the file is done |
| 16 FinishLoad | 0x141900 | `+0x88` = the array; `+0x8c` = `ui` dir → find `credits.lst` (RndCreditList); `list->provider = this` (`FUN_0012e9b0`) |
| 3 Enter | 0x141bd0 | base Enter (the script's `enter` → `meta_music_stop`), then `FUN_0012e980(list,0)` (Reset), `FUN_0012f050(list, NumData-1)` (→ Scroll(NumData-1) [I: a1 carried over in the call], **target = NumData - display = 892**), then `FUN_00141e18` (start the music) |
| 4 Exit | 0x141c20 | base Exit (`meta_music_start`); music state 4; fade to 0 at rate `DAT_00344e70` = 0.001·0.8 = 0.0008/ms → **1250 ms** |
| 6 busy | 0x141c68 | true while the fader runs: the screen transition waits for the fade-out |
| 10 Poll | 0x141a58 | base Poll (this polls the list, i.e. the scroll step above), then the music state machine below |
| 14 Unload | 0x1419f8 | frees the array, deletes the stream |
| 1 Handle | 0x142248 | on `component_scroll` (`FUN_0012fe40`, 0x3056a8) → `FUN_00142388`: if the msg's node 6 (`done`) is non-zero, it sends `go_back` (0x3083c0) to the UIManager. It always returns unhandled, so the base Handle / scripts still see the message. Nothing else: **no button handling** (no speed-up, skip or pause) |

The message built by `FUN_0012fe90` is (component_scroll component 0 screen dir done), with nodes 2..6.

**Music** (`FUN_00141e18`, Poll states). The interpolator is `FUN_002488e8`; time is in ms (`Count`·1/294912):
- state 1 (create): `Synth::NewStream("sfx/streams/credits")` (vtbl+0x124).
  - Volumes: ch0..3 = 0 (vtbl+0x74).
  - Pans: ch0 -1, ch1 +1, ch2 -1, ch3 +1 (vtbl+0x84).
- When the stream is ready (vtbl+0x14):
  - All channel volumes = 0 (`FUN_00141d28(0)`).
  - Fade from GetVolume(0) to 1 at rate `DAT_002edbec` = 0.001/ms → **1000 ms**.
  - `SetJump(-1.2e-7, 0)` (vtbl+0xc4): **loop from the end to 0** [C call, I meaning; the same idiom as the crowd streams].
  - Play (vtbl+0x24). Then → state 2.
- state 2: each poll sets channel volume = clamp(fader,0,1)·**0.8** (linear, `docs/crowd_audio_plan.md`). When the fade is done → state 3.
- state 3: stream Poll. state 4 (after Exit): fade to 0, then delete the stream → state 0.

The 200 s track loops about 4 times over the full roll.

**End behaviour**:
- The scroll stops when target is reached: the window shows rows 892..906, all blank, and the last real row ("How about now?", 888) has scrolled off the top.
- That step's message carries done=1, so the panel sends `go_back`. The screen handler runs `backwards_anim` and `goto_screen options`.
- So the credits run **once and return to Options**. They do not loop.
- Back (`fret2`) leaves earlier through the normal GHScreen back. The scroll sound is suppressed by sfx.dta.

**Duration**: 892 rows × 56 polls = 49 952 polls. At 60 Hz [I: UI poll rate] that is **832.5 s ≈ 13 min 52 s**: 0.933 s per row, or 26.8 list units/s.

---------------------------------------------------------------------------------------------------
## 6. Implementation in gd01

Current state:
- `new_credits_panel` is created as a generic panel (`ui_system.cpp` installVerbs → `makeNativePanel` returns null).
- Its scene loads, and `credits.lst` becomes a `ComponentObj` of kind CreditList.
- `Panel::drawComponent` draws nothing for CreditList. So the screen is the meta background plus the helpbar.

### Step 1: text with alpha (`engine/src/ui/panel.{h,cpp}`)
- Add `float alpha = 1` to `Panel::drawText(...)` and `drawLaidOut(...)`, and set `st.tint[3] = alpha`. Fold alpha into the cache-free tint, not the mesh key.
- Have `drawText` copy `t->markup` into `TextStyle`, for fidelity.
- Add `std::array<float,4> UiResources::fontMatColor(const std::string& font)`. It returns the font mat's `MatObj::color` (there is `fontUsesEnviron` next to it).

### Step 2: music hooks (`ui_system.h` Hooks + SelectHost, `app.cpp`)
- Add `virtual void creditsMusic(bool on)` to SelectHost, implemented through `hooks_.creditsMusic`.
- In App:
  - `on`: read `sfx/streams/credits.vgs` and use tracks pan -1/+1. Call `mixer_.playStream(Mixer::kMusic, vgs, tracks, 0, 0, /*loop*/true, /*fadeMs*/1000, /*volume*/0.8f)`.
  - `off`: `mixer_.stopStream(Mixer::kMusic, 1250)`.
- Nothing else is needed:
  - `meta_music_stop` has already cleared `menuMusicOn_`.
  - On exit, `meta_music_start` sets `menuMusicOn_` again, and `pollMenuMusic` waits until `!mixer_.musicPlaying()`, i.e. until the credits fade-out ends.

### Step 3: `CreditsPanel : NativePanel` (new `engine/src/ui/credits_panel.cpp`, or in `select_panels.cpp`)
- Register it in `makeNativePanel`: `if (verb == "new_credits_panel") return makeCreditsPanel(host);`.
- **Data**: load `parseDtb(host.data().read(dtbPath("config/credits.dta")))` once, at construction or on first enter. Precompute per row `std::array<std::string,4> col` with exactly the §2 rules. Keep `n = rows.size()` (907).
- **Config**:
  - Read `ghui/config.dta` `components credits`: fade_offset (2), max_step (0.018).
  - Read the element template names in order.
  - Read display_num / spacing from `credits.lst`'s `UiObj` (count 15, spacing 25). Fall back to these constants.
- **State**: `pos, target, sel, frac, dir, moving` as S, plus `std::deque<int> slots` (the data row per visible slot). Keep the explicit spare-slot logic from §4.
- **`entered()`**: use entered, not `enter`. The engine runs the native `enter()` *before* the script `enter` (`meta_music_stop`), but the original starts the stream after it.
  - Reset: pos = 0, sel = 0, target = 0, and fill the slots with rows 0..14.
  - Scroll(n-1), which gives target = n - 15.
  - Call `host.creditsMusic(true)`.
  - Recommended: also zero frac/dir/moving. The original does not, which only matters when re-entering mid-move [I].
- **`poll(dtMs)`**:
  - Accumulate `dtMs`. Run the §4 step once per 1000/60 ms tick [I 60 Hz], so the speed does not depend on the frame rate. Cap catch-up at about 4 ticks per frame.
  - When a row completes, `host.route("component_scroll", {component, player_num 0, screen, direction dir, done})`. sfx.dta stays silent because the focus panel is `credits`.
  - On `done`, `host.route("go_back", {})`. The screen script does `backwards_anim` + `goto_screen options`.
  - Fire `go_back` only once (latch it).
- **`exit()`**: `host.creditsMusic(false)`. Optional: emulate slot 6 (busy) so the transition waits 1250 ms. The engine has no hook for this yet, so skip it unless the screen-transition code gains one.
- **`buttonDown`**: return false for everything. Back reaches the auto-back (`UiSystem::route`, credits.back_ = options), and the original has no other input.
- **`drawAfter()`**: the scene (meta.cam / credits.view) is drawn first by the generic panel.
  ```
  Mat4 vp; if (!scene_->viewProj(vp)) return;
  const SceneObject* lst = scene_->scene().find("credits.lst");
  Mat4 listW = Mat4::fromRowXfm(lst->ui->trans.world.rot, lst->ui->trans.world.pos);
  off = spacing * min(frac*dir, 1); if (off < 0) off += spacing;
  for k = 1..slots.size():
      if (k-1 >= n) break;                                   // RndCreditList_v5 guard
      y = spacing*k - off; a = fade(y) (§4)
      Mat4 slotW = listW * Mat4::fromRowXfm(identity, {0, 0, -y});
      for j in 0..3: if (col[row][j] empty) continue;
          tmpl = elements[j]; local = res.scene().find(tmpl)->text->trans.local;
          scene_->drawText("cr" + to_string(row % 32) + "_" + j, col[row][j], tmpl,
                           rgb(fontMatColor(tmpl's font)), vp, slotW * Mat4::fromRowXfm(local.rot, local.pos), a);
  ```
  - Key the cached meshes by `row % 32`, which covers more than the 16 visible slots, so a row's mesh is built only once while it crosses the screen.
  - Use the mat colour for rgb, not the template colour (§3). `drawLaidOut` already multiplies lit fonts by the panel's `ui.env` ambient, which handles the transition fade.

### Step 4: verification
- Run `--selftest` with a `goto_screen credits` + screenshot hook:
  - Frame 0 is empty.
  - After about 10 s, "GUITAR HERO CREDITS" (orange, impactor) is near the middle, and "HARMONIX MUSIC SYSTEMS, INC." follows two rows below.
  - Roles are grey and right-aligned at x=-8; names are white and left-aligned at x=+1; the top and bottom 2 rows fade.
- Fast-forward the poll (a test-only multiplier) to confirm the last steps:
  - "How about now?" leaves the top.
  - `go_back` fires once, and Options returns with the menu loop starting after the 1.25 s credits fade.
- Check that the back button exits at any time and that no `button_toggle` click plays per row.
- Update `docs/career_e2e_plan.md` G8 and `docs/ui_system.md` (the panel list) when this is done.

### Open points [I]
- **UI poll rate**: is it really 60 Hz on PS2 (NTSC field rate)? If it were 30 Hz, the roll would take 27:45. 60 Hz is assumed.
- **Lit-text colour**: assumed to be mat colour × environ, with the vertex colour ignored, because `credit_center.txt` has colour 0. Compare by eye against the real game.
- **`FUN_0012f050` argument**: the decomp drops a1. The register carry-over of `NumData-1` into `Scroll` is inferred. Any value ≥ 15 gives the same target, so the result is robust.
