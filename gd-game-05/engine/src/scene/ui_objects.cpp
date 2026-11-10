// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/ui_objects.h"

#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>

namespace scene {

namespace {

// Little-endian reads at a position of one record.
class Rd {
public:
    Rd(const Bytes& b, size_t& pos) : b_(b), p_(pos) {}
    uint32_t u32() {
        need(4);
        uint32_t v = miloU32(&b_[p_]);
        p_ += 4;
        return v;
    }
    int32_t i32() { return int32_t(u32()); }
    float f() {
        uint32_t u = u32();
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    }
    bool b() {
        need(1);
        return b_[p_++] != 0;
    }
    std::string str() {
        uint32_t n = u32();
        need(n);
        std::string s(reinterpret_cast<const char*>(b_.data() + p_), n);
        p_ += n;
        return s;
    }
    template <size_t N> void fs(std::array<float, N>& a) {
        for (float& v : a) v = f();
    }
    ObjectHeader header() { return readObjectHeaderAt(b_, p_); }
    void trans(Trans& t) { readTransAt(b_, p_, t); }
    void draw(Draw& d) { readDrawAt(b_, p_, d); }

private:
    void need(size_t n) const {
        if (n > b_.size() || p_ > b_.size() - n) throw std::runtime_error("record ends early");
    }
    const Bytes& b_;
    size_t& p_;
};

[[noreturn]] void bad(const std::string& what) { throw std::runtime_error(what); }

uint32_t version(Rd& r, const char* cls, uint32_t lo, uint32_t hi) {
    uint32_t v = r.u32();
    if (v < lo || v > hi) bad(std::string(cls) + " version " + std::to_string(v) + " (decoder reads " + std::to_string(lo) + ".." + std::to_string(hi) + ")");
    return v;
}
void checkVersion(uint32_t v, const char* cls, uint32_t lo, uint32_t hi) {
    if (v < lo || v > hi) bad(std::string(cls) + " version " + std::to_string(v) + " (decoder reads " + std::to_string(lo) + ".." + std::to_string(hi) + ")");
}

void component(Rd& r, UIComponent& c) {
    c.componentRev = version(r, "UIComponent", 1, 1);  // RB 0x82270C80 [C]
    c.header = r.header();
    r.trans(c.trans);
    r.draw(c.draw);
    if (c.componentRev >= 1) {
        c.navRight = r.str();
        c.navDown = r.str();
    }
}

// UILabel PreLoad RB 0x8226C070 (rev 8) [C]: UIComponent (RB 0x82270C80,
// rev 1), localize, the token or text, then the text fields RB moved from
// GH2's BandLabel into every label: text_size, alignment, caps_mode,
// markup, leading, kerning, italics, fit_type, width, height, fixed_length,
// reserve_lines (names: SyncProperty RB 0x8226D428).
void label(Rd& r, UILabel& l, bool ownVersion = false) {
    if (ownVersion) checkVersion(l.labelRev = l.version, "UILabel", 8, 8);
    else l.labelRev = version(r, "UILabel", 8, 8);
    component(r, l);
    l.localize = r.b();
    if (l.localize) l.textToken = r.str();
    else l.text = r.str();
    BandText& t = l.style;
    t.textSize = r.f();
    t.alignment = r.u32();
    l.capsMode = r.i32();
    t.allCaps = l.capsMode == 2;
    l.markup = r.b();
    t.leading = r.f();
    t.kerning = r.f();
    l.italics = r.f();
    t.fitText = r.u32();
    t.width = r.f();
    t.height = r.f();
    // fit_type (system/run/config macros.dta): kFitWrap 0 wraps at width;
    // kFitStretched 1 and kFitJust 2 scale the one line to the box, so they
    // never wrap ("PLAYER 1" broke into "PLAYE / R 1" on the slots).
    t.wrapWidth = t.fitText == 0 ? t.width : 0;
    l.fixedLength = r.i32();
    l.reserveLines = r.i32();
}

// BandTextComp::Load RB 0x823007E0 (rev 1) [C]: the style symbol; with
// "custom_colors" four more 32-bit values.
void textComp(Rd& r, UILabel& l) {
    version(r, "BandTextComp", 1, 1);
    l.textStyle = r.str();
    if (l.textStyle == "custom_colors")
        for (auto& c : l.customColors) c = r.u32();
}

void readUILabel(Rd& r, UILabel& o) { label(r, o, true); }


// BandLabel PreLoad RB 0x822FE288 (rev 15) [C]: a UILabel and its text style.
void readBandLabel(Rd& r, BandLabel& o) {
    checkVersion(o.version, "BandLabel", 15, 15);
    label(r, o);
    textComp(r, o);
    o.band = o.style;
}

// BandButton PreLoad RB 0x822D3D48 / PostLoad RB 0x822D5578 (rev 14) [C]:
// UIButton (rev 0 or 1), the label, the text style, the text mesh.
void readBandButton(Rd& r, BandButton& o) {
    checkVersion(o.version, "BandButton", 14, 14);
    o.buttonRev = version(r, "UIButton", 0, 1);
    label(r, o);
    textComp(r, o);
    o.textMesh = r.str();
    o.band = o.style;
}

void readUIButton(Rd& r, BandButton& o) {
    checkVersion(o.buttonRev = o.version, "UIButton", 0, 1);
    label(r, o);
    o.band = o.style;
}

// UIList PreLoad RB 0x82268680 / PostLoad RB 0x82268EA8 (rev 9) [C]; BandList
// shares it [C: its vtable's Load slots].
void readUIList(Rd& r, UIList& o) {
    checkVersion(o.version, "UIList", 9, 9);
    component(r, o);
    // Field meanings [I: from the data: ss_song.lst's 33.0 is the rows'
    // spacing as GH2's first UIList field, its 7 the rows shown]; the
    // order and sizes [C: RB 0x82268680 / 0x82268EA8].
    o.spacing = r.f();
    o.gridSpan = r.i32();
    o.circular = r.i32() != 0;
    o.scrollPastMinDisplay = r.b();
    o.scrollPastMaxDisplay = r.b();
    o.speed = r.i32();
    o.displayNum = r.i32();
    o.numData = o.displayNum;
    r.b();
    o.scrollTime = r.f();
    o.paginate = r.b();
    r.b();
    r.b();
    o.autoScrollPause = r.i32();
}

// UIProxy PreLoad RB 0x8225F520 (rev 2) [C]: the UIComponent only.
void readUIProxy(Rd& r, UIProxy& o) {
    checkVersion(o.version, "UIProxy", 2, 2);
    component(r, o);
}

// UISlider PreLoad RB 0x82265E00 (rev 1) [C]: UIComponent, vertical.
void readUISlider(Rd& r, BandSlider& o) {
    checkVersion(o.version, "UISlider", 1, 1);
    component(r, o);
    o.vertical = r.b();
}

// BandHighlight PreLoad RB 0x822FED20 (rev 0) [C]: UIComponent.
void readBandHighlight(Rd& r, UIProxy& o) {
    checkVersion(o.version, "BandHighlight", 0, 0);
    component(r, o);
}

template <class T> void readPlain(Rd& r, T& o) {
    checkVersion(o.version, o.cls.c_str(), 0, 0);
    component(r, o);
}

void readBandSlider(Rd& r, BandSlider& o) {
    checkVersion(o.version, "BandSlider", 2, 2);
    version(r, "UISlider", 0, 0);
    component(r, o);
    o.textToken = r.str();
}

void readBandTextEntry(Rd& r, BandTextEntry& o) {
    checkVersion(o.version, "BandTextEntry", 1, 3);
    component(r, o);
    if (o.version > 1) {
        o.textScale = r.f();
        o.flashTime = r.f();
        o.arrowOffset = r.f();
    }
    if (o.version > 2) {
        r.fs(o.enteredColor);
        r.fs(o.dynamicColor);
    }
}

// UITrigger::Load RB 0x822630B0 (rev 1) [C]: a whole EventTrigger record
// (its own rev 11, RB 0x822123D8) then block_transition. The EventTrigger
// part is skipped field by field here (scene/world_objects.cpp decodes the
// class itself).
void readUITrigger(Rd& r, UITrigger& o) {
    checkVersion(o.version, "UITrigger", 1, 1);
    version(r, "EventTrigger", 11, 11);
    o.header = r.header();
    auto list = [&] {
        std::vector<std::string> v(r.u32());
        for (auto& s : v) s = r.str();
        return v;
    };
    o.triggerEvents = list();
    const uint32_t anims = r.u32();
    for (uint32_t i = 0; i < anims; ++i) {
        // EventTrigger's Anim: the animatable, blend, wait, delay, enable,
        // rate, start, end, period, type ("range"), scale [C: rev 11].
        UITrigger::Anim a;
        a.name = r.str();
        if (o.anim.empty()) o.anim = a.name;
        r.f(), r.b(), r.f(), r.b(), r.u32();
        a.start = r.f(), a.end = r.f();
        r.f();
        a.type = r.str();
        r.f();
        o.anims.push_back(a);
    }
    list(), list();  // sounds, shows
    const uint32_t hides = r.u32();
    for (uint32_t i = 0; i < hides; ++i) r.str(), r.f();
    list(), list(), list();  // enable, disable, wait-for events
    r.str();                 // next link
    const uint32_t calls = r.u32();
    for (uint32_t i = 0; i < calls; ++i) r.str(), r.str(), r.str();
    if (!o.triggerEvents.empty()) o.event = o.triggerEvents.front();
    o.blockTransition = r.b();
}

void readScreenMask(Rd& r, ScreenMask& o) {
    checkVersion(o.version, "ScreenMask", 0, 2);
    o.header = r.header();
    r.draw(o.draw);
    o.mat = r.str();
    r.fs(o.color);
    if (o.version > 0) r.fs(o.screenRect);
    if (o.version > 1) o.useCamRect = r.b();
}

void readBandPlacer(Rd& r, BandPlacer& o) {
    checkVersion(o.version, "BandPlacer", 2, 2);
    o.header = r.header();
    r.draw(o.draw);
    r.trans(o.trans);
    o.center = r.str();
}

struct UiDecoder {
    std::function<std::unique_ptr<Object>()> make;
    std::function<void(Rd&, Object&)> read;
};

template <class T> UiDecoder dec(void (*fn)(Rd&, T&)) {
    return {[] { return std::unique_ptr<Object>(new T); }, [fn](Rd& r, Object& o) { fn(r, static_cast<T&>(o)); }};
}

const std::map<std::string, UiDecoder>& uiDecoders() {
    static const std::map<std::string, UiDecoder> m = {
        {"UILabel", dec<UILabel>(readUILabel)},
        {"BandLabel", dec<BandLabel>(readBandLabel)},
        {"BandButton", dec<BandButton>(readBandButton)},
        {"UIList", dec<UIList>(readUIList)},
        {"CheckBox", dec<CheckBox>(readPlain<CheckBox>)},
        {"UIPicture", dec<UIPicture>(readPlain<UIPicture>)},
        {"UIProxy", dec<UIProxy>(readUIProxy)},
        {"UIButton", dec<BandButton>(readUIButton)},
        {"BandList", dec<UIList>(readUIList)},
        {"UISlider", dec<BandSlider>(readUISlider)},
        {"BandHighlight", dec<UIProxy>(readBandHighlight)},
        {"BandSlider", dec<BandSlider>(readBandSlider)},
        {"BandTextEntry", dec<BandTextEntry>(readBandTextEntry)},
        {"UITrigger", dec<UITrigger>(readUITrigger)},
        {"ScreenMask", dec<ScreenMask>(readScreenMask)},
        {"BandPlacer", dec<BandPlacer>(readBandPlacer)},
    };
    return m;
}

}  // namespace

bool isUiClass(const std::string& cls) { return uiDecoders().count(cls) != 0; }

std::unique_ptr<Object> makeUiObject(const std::string& cls) {
    auto it = uiDecoders().find(cls);
    return it != uiDecoders().end() ? it->second.make() : std::make_unique<Object>();
}

void readUiObject(Object& o, const Bytes& record, size_t& pos) {
    auto it = uiDecoders().find(o.cls);
    if (it == uiDecoders().end()) bad("no UI decoder for " + o.cls);
    Rd r(record, pos);
    it->second.read(r, o);
}

}  // namespace scene
