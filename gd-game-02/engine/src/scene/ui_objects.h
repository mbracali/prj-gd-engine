// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "data/bytes.h"
#include "scene/objects.h"

// GH2's UI component classes, as their Load routines in the Xbox 360
// executable read them (gd-game-02/ghidra/analysis; tools/milo.py `ui` decodes the
// same layouts). A component's Load is its PreLoad then its PostLoad (the
// Hmx::Object sub-vtable's +0x34 / +0x38), so a record is the PreLoad data
// followed by the PostLoad data. Field names come from each class's
// SyncProperty strings; "Q:" marks names that are still guesses.

namespace scene {

// UIComponent PreLoad @ 0x8236FA30 (SyncProperty @ 0x8236FAF0): u32 revision
// (<= 1), the object block, Trans, Draw and (revision >= 1) the names of the
// components the d-pad moves to (@ 0x8236F568). Its PostLoad (@ 0x8236E870)
// reads nothing; it loads the type's resource_file (@ 0x82372D40).
class UIComponent : public Object {
public:
    uint32_t componentRev = 0;
    Trans trans;
    Draw draw;
    std::string navRight, navDown;
};

// UILabel PreLoad @ 0x8236DDB8: u32 revision (<= 1), UIComponent, (revision
// >= 1) bool localize; then the symbol text_token when localized, else the
// string text (both set the label's Text through SetText @ 0x821D1710).
// Also a class of its own (UILabel v1, the cheat panel's labels).
class UILabel : public UIComponent {
public:
    uint32_t labelRev = 0;
    bool localize = false;
    std::string text;       // when !localize
    std::string textToken;  // when localize: a locale token
};

// The text fields BandLabel and BandButton share (a mix-in at the same
// offsets in both; setters @ 0x8214C498..0x8214CAB0, re-applied to the
// component's Text by 0x8214C610 after every change):
struct BandText {
    // 0 none; 1 scale x to `width` and z to `height` (each on its own);
    // 2 shrink both by one factor so the text fits width x height, never
    // grow (@ 0x8214C7B0, the Text's local scale).
    uint32_t fitText = 0;
    float width = 0, height = 0;
    float leading = 1;      // -> Text leading
    uint32_t alignment = 0x11;  // -> Text alignment (RndText flags, text_layout.h)
    uint32_t jitterSeed = 0;    // Q: no property; seeds the jitter RNG (@ 0x8214CD88)
    float deltaX = 0, deltaZ = 0, deltaRot = 0;  // per-glyph jitter ranges (units, degrees)
    bool allCaps = false;   // -> Text caps mode 2
    float kerning = 0;      // replaces the font's kerning while the text is laid out (@ 0x8214CAC8)
    float textSize = 30;    // -> Text size
    float wrapWidth = 0;    // -> Text wrap width
};

// BandLabel v12 PreLoad @ 0x8214ACB8 (PostLoad @ 0x8214A7A8 reads nothing;
// names from SyncProperty @ 0x8214B400): UILabel, the BandText fields
// (fit_text width height leading alignment [seed] deltaX deltaZ deltaRot
// all_caps kerning text_size wrap_width) and the colour (4 floats @
// 0x821222E0). The colour becomes the font material's colour when the label
// draws (@ 0x8214B0A0; rgb only).
class BandLabel : public UILabel {
public:
    BandText band;
    std::array<float, 4> color{1, 1, 1, 1};
};

// BandButton v10 PreLoad @ 0x82123398 (PostLoad @ 0x821228D0; SyncProperty
// @ 0x82123B88): UIButton (@ 0x82370498: u32 revision <= 0, then UILabel)
// and the BandText fields. Its colours come from the panel's style
// (normal / focus / disabled / selecting_color, ui_styles.h).
class BandButton : public UILabel {
public:
    uint32_t buttonRev = 0;
    BandText band;
};

// UIList v3: PreLoad @ 0x8237A4E8 (UIComponent), PostLoad @ 0x8237A540
// (names from SyncProperty @ 0x8237AD88).
class UIList : public UIComponent {
public:
    float spacing = 0, arrowOffset = 0;
    int32_t fadeOffset = 0;
    int32_t displayNum = 0;
    bool circular = false;
    float scrollTime = 0;
    bool paginate = false;  // v3
};

// CheckBox v0 (PreLoad @ 0x82140D10, PostLoad @ 0x821408C0): UIComponent;
// its mesh and mat_on / mat_off come from the type.
class CheckBox : public UIComponent {};

// BandSlider v2 PreLoad @ 0x82145B90: UISlider (@ 0x823757F0: u32 revision
// <= 0 + UIComponent), then the symbol text_token.
class BandSlider : public UIComponent {
public:
    std::string textToken;
};

// BandTextEntry v3: PreLoad @ 0x82142DF8 (UIComponent), PostLoad @
// 0x82141F78 (names from SyncProperty @ 0x82144910).
class BandTextEntry : public UIComponent {
public:
    float textScale = 1, flashTime = 0, arrowOffset = 0;  // v2+
    std::array<float, 4> enteredColor{1, 1, 1, 1}, dynamicColor{1, 1, 1, 1};  // v3
};

// UIPicture v0 (PreLoad @ 0x8237E6F0, PostLoad @ 0x8237E740): UIComponent;
// mesh and tex_file come from the type.
class UIPicture : public UIComponent {};

// UITrigger v0 PreLoad @ 0x8237D898: UIComponent, symbol event (ui_enter,
// ui_exit_forward...), the animation it plays (an ObjPtr, @ 0x82127788) and
// block_transition.
class UITrigger : public UIComponent {
public:
    std::string event, anim;
    bool blockTransition = false;
};

// UIProxy v0 (PreLoad @ 0x82377E18, PostLoad @ 0x823787F8): UIComponent;
// file / main_trans come from the type.
class UIProxy : public UIComponent {};

// RndScreenMask v2 Load @ 0x8221A970 (SyncProperty @ 0x8221AA28): a quad over
// the screen (or the camera's screen rect) drawn with `mat` and `color`.
class ScreenMask : public Object {
public:
    Draw draw;
    std::string mat;
    std::array<float, 4> color{1, 1, 1, 1};       // rgb + alpha
    std::array<float, 4> screenRect{0, 0, 1, 1};  // x y w h
    bool useCamRect = false;
};

// BandPlacer v2 (its Load was not located; the layout below ends every
// record exactly): the object block, Draw, Trans and one object name (Q:
// "center"; the other SyncProperty string @ 0x8212A4A0 is "proxy").
class BandPlacer : public Object {
public:
    Draw draw;
    Trans trans;
    std::string center;
};

// Decoders for the classes above, used by decodeObject (objects.cpp).
bool isUiClass(const std::string& cls);
std::unique_ptr<Object> makeUiObject(const std::string& cls);
// Reads the record after its first u32 (already in o.version) starting at
// `pos`; throws on malformed data. On return `pos` is where reading stopped.
void readUiObject(Object& o, const Bytes& record, size_t& pos);

}  // namespace scene
