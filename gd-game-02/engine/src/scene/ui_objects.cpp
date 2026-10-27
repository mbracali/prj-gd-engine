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
        uint32_t v = uint32_t(b_[p_]) | uint32_t(b_[p_ + 1]) << 8 | uint32_t(b_[p_ + 2]) << 16 | uint32_t(b_[p_ + 3]) << 24;
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
    c.componentRev = version(r, "UIComponent", 0, 1);
    c.header = r.header();
    r.trans(c.trans);
    r.draw(c.draw);
    if (c.componentRev >= 1) {
        c.navRight = r.str();
        c.navDown = r.str();
    }
}

void label(Rd& r, UILabel& l, bool ownVersion = false) {
    if (ownVersion) checkVersion(l.labelRev = l.version, "UILabel", 0, 1);
    else l.labelRev = version(r, "UILabel", 0, 1);
    component(r, l);
    l.localize = l.labelRev >= 1 ? r.b() : false;
    if (l.localize) l.textToken = r.str();
    else l.text = r.str();
}

void readUILabel(Rd& r, UILabel& o) { label(r, o, true); }

void bandText(Rd& r, BandText& t) {
    t.fitText = r.u32();
    t.width = r.f();
    t.height = r.f();
    t.leading = r.f();
    t.alignment = r.u32();
    t.jitterSeed = r.u32();
    t.deltaX = r.f();
    t.deltaZ = r.f();
    t.deltaRot = r.f();
    t.allCaps = r.b();
    t.kerning = r.f();
    t.textSize = r.f();
}

void readBandLabel(Rd& r, BandLabel& o) {
    checkVersion(o.version, "BandLabel", 11, 12);
    label(r, o);
    bandText(r, o.band);
    o.band.wrapWidth = o.version >= 12 ? r.f() : o.band.width;
    r.fs(o.color);
}

void readBandButton(Rd& r, BandButton& o) {
    checkVersion(o.version, "BandButton", 9, 10);
    o.buttonRev = version(r, "UIButton", 0, 0);
    label(r, o);
    bandText(r, o.band);
    o.band.wrapWidth = o.version >= 10 ? r.f() : o.band.width;
}

void readUIList(Rd& r, UIList& o) {
    checkVersion(o.version, "UIList", 1, 3);
    component(r, o);
    o.spacing = r.f();
    o.arrowOffset = r.f();
    o.fadeOffset = r.i32();
    o.displayNum = r.i32();
    o.circular = r.b();
    o.scrollTime = r.f();
    if (o.version > 2) o.paginate = r.b();
    if (o.version == 1) {
        r.u32();
        r.u32();
    }
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

void readUITrigger(Rd& r, UITrigger& o) {
    checkVersion(o.version, "UITrigger", 0, 0);
    component(r, o);
    o.event = r.str();
    o.anim = r.str();
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
        {"UIProxy", dec<UIProxy>(readPlain<UIProxy>)},
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
