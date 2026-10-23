# RndButtonEx text fit

How the original stretches button text to fill its box (main menu CAREER / QUICK PLAY /
MULTIPLAYER, career difficulty EASY ... EXPERT).

## Field layout (RndButtonEx, Load = RndButtonEx_t2_v8 @ 0x12b8e0, ctor FUN_0012afb0)

| offset | file order (v4)        | ctor default        | real meaning |
|--------|------------------------|---------------------|--------------|
| +0x144 | jitter seed (v>2)      | 0                   | per-glyph jitter seed |
| +0x138/13c/140 | jitter (v>2)   | 0                   | jitter x, z, rotation |
| +0x130 | style symbol (v>=2)    | `buttonex`          | ghui style |
| +0x134 | bool (v>=2; v<2 forced 1) | 1                | **fit width** (ui_panels `flag_134`) |
| (v2,v3)| one extra byte, discarded |                  | |
| +0x12c | bool (v>=1; v0 -> 0)   | 0                   | **caps** (ui_panels `flag_12c`) |
| +0x120 | f32                    | 400.0 (0x43c80000)  | **fit / box width** |
| +0x124 | f32                    | 40.0 (0x42200000)   | box height (no effect on the text) |
| +0x128 | string                 | `""`                | text token |
| +0x150 | -                      | own RndText         | the drawn text (a copy of `ghui::button.txt`) |

Evidence:
- `+0x12c` -> FUN_0012c3c8 @ 0x12c3c8: `text->capsMode (+0x124) = flag ? 2 : 0` via FUN_001d8388
  (RndText +0x124 is capsMode, see tools/rnd_classes/material.py decode_text). So it *replaces* the
  template's capsMode, also forcing 0 when clear.
- `+0x134` -> FUN_0012c408 @ 0x12c408 (below).
- Ctor sets text align 0x22 (Middle|Center) via FUN_001d8358 (RndText +0x120 = align).

## The rule (FUN_0012c408 @ 0x12c408)

```
M = identity 3x3
if (fit /* +0x134 */) {
    w  = FUN_001d9410(text)        // max over lines of line.width (line struct +0x30), same as LabelEx
    M.row0 *= box_width /* +0x120 */ / w     // x only; y, z rows stay 1
}
FUN_001da700(text + 0x40, &M)      // overwrite the TEXT's local rotation (position untouched)
```

- Only X is scaled. There is **no height fit** (box_height +0x124 is never used for the text;
  it is used only in the draw RndButtonEx_v5 @ 0x12bb10 for a centred box_width x box_height
  rectangle via FUN_001e37b0).
- No clamp: text is stretched when narrower and squeezed when wider than box_width.
- No `w > 0` guard in the original (empty text -> div by 0); guard it in our port.
- When fit is off the text local is reset to identity (no scale at all).
- The scale goes on the **button's own RndText** (+0x150), not on the button. The text's world is
  `button.world * text.local` (RndButtonEx_v1 @ 0x12c028 calls the text's transform update with
  `button + 0x10 + 0x60`, the button's world). So the stored button local/world is *not*
  replaced and does not contain a baked fit: every ButtonEx in extracted/ghui/*.gh has local
  scale exactly 1.0 (world scale differs only through parents, e.g. main 0.9, sel_diff_career 1.3).
  Unlike our LabelEx path, do **not** divide by the stored local scale.
- Scaling is about the text origin; with align 0x22 the text stays centred on the button.
- Tail of FUN_0012c408: if the text string is longer than 50 chars (FUN_00289938 > 0x32) and any
  jitter is set, jitter is zeroed (FUN_001d7bb0(text, 0)).

## When it runs

FUN_0012c408 is called from:
- FUN_0012c340 @ 0x12c340 = SetText (stores +0x128, localises into the text via FUN_0011db60,
  then fit, then jitter FUN_0012c9a8). Called by:
  - Load (0x12b8e0) and Copy (0x12b648) through FUN_0012c108 -> FUN_0012c138 -> SetText(+0x128)
    (FUN_0012c108 first runs FUN_0012c1c8 = style font/colour for the state);
  - handler `set_text` (RndButtonEx_t2_v1 @ 0x12cbd0, string 0x304d50).
- FUN_0012c390 @ 0x12c390 = handler `set_localized_text` (0x304d60): sets raw text, fit, jitter.

i.e. every time the string changes; the fit is computed from the final displayed (localised,
caps-transformed) text. `set_state` (FUN_0012c090 -> FUN_0012c1c8) only changes colour/font and
does not refit.

## LabelEx without `fit`

FUN_0011f868 @ 0x11f868 returns immediately when `fit` (+0x158) is 0 or the text is empty; it is
the only LabelEx writer of the text local (callers at 00110000.c:10059/10111). So non-fit labels
are drawn at natural width - nothing else stretches them. (LabelEx fit differs from ButtonEx: it
guards w > 0, also fits Z to fit_height / text height.)

## Data (all buttons, extracted/ghui)

`fit=1` e.g. main.gh main_* (box 210, caps 1), sel_diff_career.gh sd_diff* (box 150, caps 1),
sel_difficulty (210), career.gh cm_* (250, caps 0), pause/lose/tut_pause impactor_white (120/140),
store (200), dialog (150), game_settings (200).
`fit=0` e.g. options.gh op_* , data_settings, chooseprof/loadprof/delprof cp_band*, sel_venue, tutorials,
complete.gh (caps 1), bonus_material.

## Bugs in our engine

1. engine/src/scene/scene.cpp ButtonEx load (~l.463): `u->caps = r.b8()` reads +0x134, which is
   **fit**, and the real caps byte (+0x12c, `if (v >= 1) r.b8();`) is thrown away. Should be:

```cpp
if (v >= 2) {
    u->style = r.str();
    u->fit = r.b8();              // +0x134 fit width (FUN_0012c408)
    if (v < 4) r.b8();
} else {
    u->style = "buttonex";
    u->fit = true;
}
u->caps = v >= 1 ? r.b8() : false; // +0x12c -> RndText capsMode 2/0 (FUN_0012c3c8)
u->fitWidth = r.f32();             // +0x120 box width
u->fitHeight = r.f32();            // +0x124 box height (not used for the text)
u->text = r.str();
```

2. engine/src/ui/panel.cpp drawComponent: for buttons capsMode must be `u.caps ? 2 : 0` (not the
   template's), and drawLaidOut needs a ButtonEx variant of the fit:

```cpp
// drawComponent, Button branch
if (u.kind == UiObj::Kind::Button) { ts.align = 0x22; ts.capsMode = u.caps ? 2 : 0; }
...
// drawLaidOut, in the rebuild block
c.sx = c.sz = 1;
if (fitTo && fitTo->kind == UiObj::Kind::Button) {
    // RndButtonEx FUN_0012c408: text local = diag(box_width / widest line, 1, 1);
    // it is the button's own RndText, child of the button: no stored-local undo, no z fit.
    if (fitTo->fit && tm.width > 0) c.sx = fitTo->fitWidth / tm.width;
} else if (fitTo && fitTo->fit) {
    ... existing LabelEx code unchanged ...
}
```

(`c.sx` multiplies `mvp = viewProj * model * button.world` on the right, which is exactly
`button.world * text.local`.)
