// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/font.h"
#include "data/qb.h"
#include "render/device.h"

class Canvas;
class Disc;
class MoviePlayer;
class TextureCache;
namespace qs {
class Vm;
struct ScriptRun;
}  // namespace qs

// GH3's screen elements (docs/ui.md, from the XEX: base element 0x8221DDB8,
// props 0x8221A738, transforms 0x8221BF88, morphs 0x8221CEC8, menus
// 0x823164E0): the tree its menu and HUD scripts build with
// CreateScreenElement and drive with SetScreenElementProps,
// DoScreenElementMorph, LaunchEvent and member calls; drawn on 1280x720.
namespace ui {

struct Element {
    uint32_t id = 0, type = 0, localId = 0;
    Element* parent = nullptr;
    std::vector<std::unique_ptr<Element>> children;
    float x = 0, y = 0, w = 0, h = 0;      // pos (parent-local), dims (base size)
    bool dimsGiven = false;
    float justX = 0, justY = 0;            // -1 left / top .. 1 right / bottom; default centre [C 0x8221E0FC]
    float anchorX = -1, anchorY = -1;      // child_anchor [C 0x82218908]
    // gh4: a desc element's own pos_anchor (where in its parent its pos counts
    // from), used instead of the parent's child_anchor [I].
    bool posAnchorGiven = false;
    float posAnchorX = -1, posAnchorY = -1;
    // The morphable values before the last applyProps (gh4 se_setprops with time).
    struct Snapshot {
        bool valid = false;
        float x = 0, y = 0, sx = 1, sy = 1, alpha = 1, rot = 0;
    } lastBefore;
    uint32_t desc = 0;  // gh4 descinterface: the uidesc_<name> global it was built from
    float ijustX = 0, ijustY = 0;          // internal_just (menus, text)
    float sx = 1, sy = 1, alpha = 1, rot = 0, z = 0;  // rot in degrees
    bool zGiven = false;  // without its own z_priority an element draws at its parent's [I]
    std::array<uint8_t, 4> rgba{255, 255, 255, 255};
    uint32_t texture = 0, font = 0;
    uint32_t texContext = 0;  // the asset context the texture was set under (PushAssetContext), 0: global
    std::string text;
    bool hidden = false, flipH = false, flipV = false, notFocusable = false, blockEvents = false;
    bool allowExpansion = false, locked = false, preserveOrientation = false;
    bool layoutOnce = false;  // SetScreenElementLock on: one more layout first [C 0x8221A588]
    // A VMenu's width is set by its first-update event only (made, a child
    // added or gone, SetScreenElementLock), not when a child's text changes
    // later: the children it was set with (count, last one) (GD10-099).
    size_t widthKids = size_t(-1);
    const Element* widthLast = nullptr;
    int exclusiveDevice = -1;
    uint32_t exclusiveMask = 0;  // exclusive_device as an array of devices (bit per device; 0: none) [C script create_select_difficulty_menu]
    float fontSpacing = 0, spaceSpacing = 0, lineSpacing = 1;
    // font_spacing given: it replaces the font's tracking and drops its
    // bearing (default -1 = the font's own) [C TextElement ctor 0x8231B788, 0x8232ADF0]
    bool fontSpacingGiven = false;
    // shadow: the text again at pos + shadow_offs (screen px), in
    // shadow_rgba, z - 0.005, colour codes ignored [C 0x8231A8C0]
    bool shadow = false;
    float shadowX = 0, shadowY = 0;
    std::array<uint8_t, 4> shadowRgba{0, 0, 0, 255};
    float internalScale = 1, givenH = 0, givenW = 0;
    // gh4 text blocks' fitting [C data: the descs' fit_width / fit_height /
    // scale_mode / text_case values; I: their rules, ui_system.cpp blockFit]:
    // fitW 0 wrap (default), 1 expand dims, 2 scale each line if larger,
    // 3 scale each line to fit; fitH 0 none, 1 clip bottom lines, 2 clip
    // top lines, 3 expand dims, 4 scale down if larger, 5 scale to fit.
    uint8_t fitW = 0, fitH = 0;
    // gh4 menus' fit_major / fit_minor [C data; I rules]: 0 keep dims,
    // 1 size dims to content / fit content, 2 fit content if larger /
    // expand if content larger.
    uint8_t fitMajor = 0, fitMinor = 0;
    // gh4 scrollingmenu: adjust_visibility (items outside the window hidden),
    // center_selection / top_selection [C data uidesc_setlist_b; I rules].
    bool adjustVisibility = true, centerSelection = false, topSelection = false;
    bool scalePerAxis = false, upperCase = false;
    uint8_t blend = 0;  // a sprite's blend: 0 alpha, 1 add, 2 subtract (destination - source) [I: the setlist's shoeprints]  // text blocks: internal_scale, the dims' height before allow_expansion
    // Menus [C 0x82315AA0 / 0x82315B08]
    float regularSpace = 0, paddingScale = 1, spacingBetween = 0;
    bool dontWrap = false;
    // VScrollingMenu [C ctor 0x82316E58, props 0x82317348, update 0x82317640]:
    // its menu's offset (0x270), scroll_speed (0x274, 10000), num_items_to_show
    // (0x278, -1), the refresh flag (0x26d); the port's record of the item
    // and child count it last saw (GH3: the focus / first-update events).
    float scroll = 0, scrollSpeed = 10000;
    int numItemsToShow = -1;
    bool scrollDirty = true, scrollRelayout = false;
    int scrollSel = -2;
    size_t scrollKids = 0;
    int selected = -1;
    bool focused = false;
    int controller = 0;
    struct Handler {
        uint32_t event = 0, script = 0;
        qb::Struct params;
    };
    std::vector<Handler> handlers;
    qb::Struct tags;
    // Where it was drawn (screen units, axis-aligned) and in which draw
    // (UiSystem::drawCount): the mouse hit-tests what was drawn last.
    mutable float bx0 = 0, by0 = 0, bx1 = 0, by1 = 0;
    mutable uint64_t drawnIn = 0;
    mutable bool updated = false;  // laid out by a draw (GetScreenElementPosition summed)
    // Its place in GH3's one draw list: (re)inserted when it becomes
    // visible or its z changes, in front of the older equals there, i.e.
    // drawn behind them [C 0x8238D7D0, 0x8238F6F8].
    mutable uint64_t listSeq = 0, listSeen = 0;
    mutable float listZ = 0;
    struct Morph {
        bool active = false;
        double t = 0, duration = 0;
        int motion = 0;  // 0 linear, 1 smooth, 2 ease_in, 3 ease_out [C 0x822A6B28]
        float x0 = 0, y0 = 0, sx0 = 1, sy0 = 1, a0 = 1, r0 = 0, x1 = 0, y1 = 0, sx1 = 1, sy1 = 1, a1 = 1, r1 = 0;
        std::array<float, 4> c0{}, c1{};
        bool pos = false, scale = false, alpha = false, rot = false, rgba = false;
    } morph;
};

class UiSystem {
public:
    UiSystem(qs::Vm& vm, const Disc& disc, TextureCache& textures, gfx::RenderDevice& device);
    ~UiSystem();
    void addNatives();
    // The texture-slot movies' natives (ui_movies.cpp), after the game's stubs.
    void addMovieNatives();
    void update(double dtMs);
    void draw(Canvas& c);
    // A pad event (pad_choose, pad_back, pad_up ...) to every focused element.
    void padEvent(uint32_t event, int device);
    // A focused element of `device` handles pad_start as GH3's text entries
    // do (finish the name): the keyboard's Enter is Start there (FrontEnd::handle).
    bool startFinishesEntry(int device) const;
    Element* find(uint32_t id) const;
    // gh4's task menus focus their menu after ui_create_ (ui/ui_states.cpp):
    // the `focus` event to element `id`, if it exists; false if not.
    bool focusElement(uint32_t id);
    // gh4 descinterface (ui_desc.cpp): builds the uidesc_<desc> tree under
    // `e`; the element named by a desc alias / prop path; se_setprops on a desc.
    bool buildDesc(Element& e, const std::string& desc);
    // autosizedims = true: the element takes the desc's rect size and the
    // rect's corner sits on its top-left (the desc's root moves by -corner).
    // True when it applied.
    bool descAutoSize(Element& e, const qb::Struct& props);
    Element* descPath(Element& e, const qb::Value* path);
    Element* descAlias(Element& e, uint32_t alias);
    void descSetProps(Element& e, const qb::Struct& p);
    bool descGetProp(Element& e, uint32_t name, qb::Value& out);
    void morphFromSetProps(Element& e, const qb::Struct& p);
    // DisplayLoadingScreen's picture is up (over everything).
    bool loadingScreenUp() const { return loadingTex_ != 0; }
    Element& root() { return *root_; }
    size_t count() const { return byId_.size(); }
    // The text of every visible text element (walk checks).
    std::vector<std::string> visibleTexts() const;
    // GD10_UIDUMP: the element tree, one line per element.
    void dump() const;
    // -- the port's own screens and the mouse (ui/port_*.cpp; not GH3's)
    // Text in one of GH3's fonts (by its key, e.g. fontgrid_title_gh3),
    // outside the element tree: `h` the line height in screen units,
    // `align` 0x11 top left .. 0x22 centre .. 0x44 bottom right (high
    // nibble vertical, low horizontal). \bN (hex) is the buttons font's
    // glyph N, as in GH3's own texts.
    void drawText(Canvas& c, uint32_t font, const std::string& s, float x, float y, float h, const std::array<uint8_t, 4>& rgba,
                  uint32_t align) const {
        drawText(c, font, s, x, y, h, rgba, align, TextStyle{});
    }
    // A text element's other props: rot_angle (degrees, clockwise, about the
    // (x, y) anchor), a horizontal scale (a scale pair's x / y: the score's
    // squeeze) and font_spacing (font units between glyphs) [C: TextElement
    // props; pivot I].
    struct TextStyle {
        float rot = 0, sx = 1, spacing = 0;
    };
    void drawText(Canvas& c, uint32_t font, const std::string& s, float x, float y, float h, const std::array<uint8_t, 4>& rgba,
                  uint32_t align, const TextStyle& style) const;
    float textWidth(uint32_t font, const std::string& s, float h, float spacing = 0) const;
    // A script text's \m<n> button codes as the buttons font's glyphs (SetProps text) [C 0x8228E0B0].
    std::string setText(const std::string& s) const;
    static std::string upperText(const std::string& s);  // text_case = upper
    // The focused elements: menus and their selected items (walk checks too).
    std::vector<Element*> focusedElements() const;
    // A GH3 texture (by key) stretched over x0..x1, y0..y1 in menu units,
    // flipped left-right if `flipU`; false if the texture is not loaded.
    bool drawSprite(Canvas& c, uint32_t texture, float x0, float y0, float x1, float y1, const std::array<uint8_t, 4>& rgba, bool flipU = false) const;
    // A font's line height in its own units (0 if the font is not loaded).
    float fontLineHeight(uint32_t font) const;
    // The mouse over GH3's menus: the focused menus' items, hit-tested at
    // their last drawn bounds (an item that draws nothing itself stands for
    // the element its focus handler names, as the main menu's do). `focus`
    // moves the selection there (a hover). Returns whether one was hit.
    bool menuAt(float x, float y, bool focus, bool* isList = nullptr);
    // The help bar's item (user_control_container) at (x, y): its text, upper
    // case ("SELECT", "BACK"), or "". `button`: its button ("green", "red",
    // "yellow", "blue", "orange", "strumbar", "start" or ""), `midY` the
    // pill's middle (the strum bar: above it up, below down).
    std::string helperAt(float x, float y, std::string* button = nullptr, float* midY = nullptr) const;
    // The help bar's pills as drawn in the last frame: the text (upper case),
    // its button (as helperAt), and the whole visible pill (glyph, text and
    // the backing: Control_pill_body and its two ends) that helperAt takes.
    struct HelpPillHit {
        std::string text, button;
        float x0, y0, x1, y1;
    };
    std::vector<HelpPillHit> helperPills() const;
    // The drawn bounds of an element (false: not drawn in the last frame).
    bool drawnBounds(uint32_t id, float& x0, float& y0, float& x1, float& y1) const;
    uint64_t drawCount() const { return drawCount_; }
    struct Stats {
        uint64_t created = 0, destroyed = 0, morphs = 0, events = 0, handlersRun = 0, textDrawn = 0, spritesDrawn = 0;
        uint64_t missingTextures = 0, missingFonts = 0, memberCalls = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct FontEntry {
        font::Font f;
        gfx::TextureId tex;
    };
    const FontEntry* fontFor(uint32_t key) const;
    const FontEntry* buttonsFont() const;
    void layoutTree(Element& e);
    void loadFonts(const std::string& pakPath);
    void applyProps(Element& e, const qb::Struct& p, bool create);
    void setTexture(Element& e, uint32_t key);
    void measureText(Element& e) const;
    float textWidth(const FontEntry& fe, const std::u16string& s, const Element& e) const;
    // A text block's lines, word-wrapped to its dims [C 0x8231B260].
    std::vector<std::u16string> blockLines(const FontEntry& fe, const Element& e) const;
    // A text block's lines after fit_height's clipping, each line's x scale
    // (fit_width) and the block's scale (fit_height), in internal_scale units.
    struct BlockFit {
        std::vector<std::u16string> lines;
        std::vector<float> lineScale;
        float sx = 1, sy = 1;
    };
    BlockFit blockFit(const FontEntry& fe, const Element& e) const;
    // A text block's lines are its children in the original (text elements:
    // dims = the line's measured text, scale = internal_scale x the fit's
    // scale); GetScreenElementChildren hands out ids for them (scripts read
    // their dims: ui_setlist_unfocus_song places the icons after the title).
    mutable std::unordered_map<uint32_t, std::pair<uint32_t, int>> lineIds_;
    bool lineProps(uint32_t id, float& w, float& h, float& sx, float& sy) const;
    bool linePos(uint32_t id, bool summed, float& x, float& y) const;
    // SetText: "\m<n>" (a meta button) becomes "\b<glyph>" through
    // meta_button_map, "\_" a space [C 0x8231A2C8, 0x8228E0B0].
    // A text element's glyphs (its shadow with `shadowPass`) at the corner
    // (cx, cy), world scale / alpha / rotation.
    void drawElementText(Canvas& c, const Element& e, const FontEntry& fe, float cx, float cy, float wsx, float wsy, float wa, float wrot,
                         bool shadowPass) const;
    void destroy(Element* e);
    void forget(Element* e);
    bool launch(Element* target, uint32_t event, const qb::Struct& data);
    void focusMenu(Element* m, const qb::Struct& data);
    void moveMenu(Element* m, int dir);
    void layoutMenu(Element& m);
    void scrollMenu(Element& s);
    Element* resolve(const qb::Value* v) const;
    bool member(qs::ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled);
    void startMorph(Element& e, const qb::Struct& p);
    // An element's screen frame as draw() composes it: the point its pos
    // lands on (wx, wy), its top-left (cx, cy), world scale and rotation.
    struct Frame {
        float wx = 0, wy = 0, cx = 0, cy = 0, wsx = 1, wsy = 1, wrot = 0;
    };
    Frame frameOf(const Element& e) const;
    // The local pos that puts e's pos point on screen point (x, y) (`pos =
    // {absolute (x, y)}`).
    void absoluteToLocal(const Element& e, float x, float y, float& lx, float& ly) const;
    // A native's params with included (unnamed) structs merged in.
    static qb::Struct flattenParams(const qb::Struct& p);

    qs::Vm& vm_;
    const Disc& disc_;
    TextureCache& tex_;
    gfx::RenderDevice& dev_;
    std::unique_ptr<Element> root_;
    std::unordered_map<uint32_t, Element*> byId_;
    std::unordered_map<uint32_t, uint32_t> aliases_;  // gh4 AssignAlias: alias -> element id
    std::unordered_map<uint32_t, FontEntry> fonts_;
    std::vector<std::array<uint8_t, 4>> colorTab_;  // Default_Font_Colors
    Stats stats_;
    uint64_t drawCount_ = 0;
    uint64_t listSeq_ = 0;
    // DisplayLoadingScreen's image (full screen) and spinning wheel, over
    // everything until HideLoadingScreen [C 0x8218DDE8 / 0x8218E0A0].
    uint32_t loadingTex_ = 0, loadingSpin_ = 0;
    // PushAssetContext / PopAssetContext: textures set meanwhile come from
    // the context's pak first (a character body's Mag_Photo) [C script
    // create_newspaper_menu]; 0 entries: a context with no body pak (zones).
    std::vector<uint32_t> assetContexts_;
    uint32_t loadAssetContext(uint32_t context, const qb::Struct* body);
    float spinX_ = 0, spinY_ = 0;
    double loadingMs_ = 0;
    // Texture-slot movies (PlayMovieFromBuffer, PreLoadMovie +
    // StartPreLoadedMovie): drawn full screen at their texturepri in the
    // elements' z order [C scripts create_loading_screen, win_song].
    struct MovieSlot;
    std::unordered_map<int, std::unique_ptr<MovieSlot>> movies_;
    std::unordered_map<int, std::string> movieBuffers_;  // buffer_slot -> movie (LoadMovieIntoBuffer)
    void updateMovies(double dtMs);
    void moviesToDraw(std::vector<const Element*>& out);  // decodes due pictures; the slots to draw
    gfx::TextureId movieTexture(uint32_t key) const;
    std::vector<uint32_t> pendingFocus_;  // menus whose first item takes the focus at their next update
    void resolvePendingFocus(uint32_t menuId);
    bool unionBounds(const Element& e, float& x0, float& y0, float& x1, float& y1) const;
};

}  // namespace ui
