// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
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

// GH3's screen elements (docs/ui.md): the tree its menu and HUD scripts
// build with CreateScreenElement and drive with SetScreenElementProps,
// DoScreenElementMorph and LaunchEvent; drawn on the 1280x720 frame.
namespace ui {

struct Element {
    uint32_t id = 0;     // the script's id (0: none)
    uint32_t type = 0;   // key: containerelement, spriteelement, textelement, ...
    Element* parent = nullptr;
    std::vector<std::unique_ptr<Element>> children;
    // Properties (screen units, 1280x720).
    float x = 0, y = 0, w = 0, h = 0;
    float justX = 0, justY = 0;          // -1 left / top .. 1 right / bottom
    float ijustX = 0, ijustY = 0;        // internal_just (text inside dims)
    float sx = 1, sy = 1;
    std::array<uint8_t, 4> rgba{255, 255, 255, 255};
    float alpha = 1;
    float z = 0;
    float rot = 0;                       // degrees
    uint32_t texture = 0, font = 0, material = 0;
    std::string text;
    bool hidden = false, allowExpansion = false, focusable = false, focused = false;
    struct Handler {
        uint32_t event = 0, script = 0;
        qb::Struct params;
    };
    std::vector<Handler> handlers;
    qb::Struct tags;  // SetTags / GetTags
    // A running morph toward target values over `duration` ms.
    struct Morph {
        bool active = false;
        double t = 0, duration = 0;
        float x0, y0, sx0, sy0, a0, r0, x1, y1, sx1, sy1, a1, r1;
        bool pos = false, scale = false, alpha = false, rot = false;
    } morph;
    int menuIndex = 0;  // vmenu / hmenu: the focused child
};

class UiSystem {
public:
    UiSystem(qs::Vm& vm, const Disc& disc, TextureCache& textures, gfx::RenderDevice& device);
    ~UiSystem();
    // Registers the screen-element natives on the VM.
    void addNatives();
    // Advances morphs by dtMs.
    void update(double dtMs);
    void draw(Canvas& c) const;
    // Sends a pad event (pad_up, pad_down, pad_choose, pad_back, pad_start,
    // pad_left, pad_right) to the element with focus [I until docs/ui.md].
    void padEvent(uint32_t event, int device);
    Element* find(uint32_t id) const;
    Element& root() { return *root_; }
    size_t count() const { return byId_.size(); }
    // Counters for the walk tests.
    struct Stats {
        uint64_t created = 0, destroyed = 0, morphs = 0, events = 0, handlersRun = 0, textDrawn = 0, spritesDrawn = 0;
        uint64_t missingTextures = 0, missingFonts = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct FontEntry {
        font::Font f;
        gfx::TextureId tex;
    };
    const FontEntry* fontFor(uint32_t key) const;
    void applyProps(Element& e, const qb::Struct& p);
    void destroy(Element* e);
    void forget(Element* e);
    bool launch(Element* target, uint32_t event, const qb::Struct& data);
    Element* resolve(const qb::Value* v) const;

    qs::Vm& vm_;
    const Disc& disc_;
    TextureCache& tex_;
    gfx::RenderDevice& dev_;
    std::unique_ptr<Element> root_;
    std::unordered_map<uint32_t, Element*> byId_;
    std::unordered_map<uint32_t, FontEntry> fonts_;
    Element* focus_ = nullptr;
    mutable Stats stats_;
};

}  // namespace ui
