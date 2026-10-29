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
    float ijustX = 0, ijustY = 0;          // internal_just (menus, text)
    float sx = 1, sy = 1, alpha = 1, rot = 0, z = 0;  // rot in degrees
    std::array<uint8_t, 4> rgba{255, 255, 255, 255};
    uint32_t texture = 0, font = 0;
    std::string text;
    bool hidden = false, flipH = false, flipV = false, notFocusable = false, blockEvents = false;
    bool allowExpansion = false, locked = false, preserveOrientation = false;
    int exclusiveDevice = -1;
    float fontSpacing = 0, spaceSpacing = 0, lineSpacing = 1;
    // Menus [C 0x82315AA0 / 0x82315B08]
    float regularSpace = 0, paddingScale = 1, spacingBetween = 0;
    bool dontWrap = false;
    int selected = -1;
    bool focused = false;
    int controller = 0;
    struct Handler {
        uint32_t event = 0, script = 0;
        qb::Struct params;
    };
    std::vector<Handler> handlers;
    qb::Struct tags;
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
    void update(double dtMs);
    void draw(Canvas& c);
    // A pad event (pad_choose, pad_back, pad_up ...) to every focused element.
    void padEvent(uint32_t event, int device);
    Element* find(uint32_t id) const;
    Element& root() { return *root_; }
    size_t count() const { return byId_.size(); }
    // The text of every visible text element (walk checks).
    std::vector<std::string> visibleTexts() const;
    // GD04_UIDUMP: the element tree, one line per element.
    void dump() const;
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
    void loadFonts(const std::string& pakPath);
    void applyProps(Element& e, const qb::Struct& p, bool create);
    void setTexture(Element& e, uint32_t key);
    void measureText(Element& e) const;
    float textWidth(const FontEntry& fe, const std::u16string& s, const Element& e) const;
    void destroy(Element* e);
    void forget(Element* e);
    bool launch(Element* target, uint32_t event, const qb::Struct& data);
    void focusMenu(Element* m, const qb::Struct& data);
    void moveMenu(Element* m, int dir);
    void layoutMenu(Element& m);
    Element* resolve(const qb::Value* v) const;
    bool member(qs::ScriptRun& run, uint32_t obj, uint32_t method, qb::Struct& p, bool& handled);
    void startMorph(Element& e, const qb::Struct& p);
    std::vector<Element*> focusedElements() const;

    qs::Vm& vm_;
    const Disc& disc_;
    TextureCache& tex_;
    gfx::RenderDevice& dev_;
    std::unique_ptr<Element> root_;
    std::unordered_map<uint32_t, Element*> byId_;
    std::unordered_map<uint32_t, FontEntry> fonts_;
    std::vector<std::array<uint8_t, 4>> colorTab_;  // Default_Font_Colors
    Stats stats_;
};

}  // namespace ui
