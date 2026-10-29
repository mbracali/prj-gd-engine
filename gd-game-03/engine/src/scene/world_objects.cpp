// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/world_objects.h"

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
    void xfm(Xfm& x) {
        fs(x.rot);
        fs(x.pos);
    }
    // A count followed by that many items of at least `minBytes` each.
    uint32_t count(size_t minBytes) {
        uint32_t n = u32();
        if (minBytes && n > (b_.size() - p_) / minBytes) throw std::runtime_error("implausible list size");
        return n;
    }
    std::vector<std::string> strs() {
        std::vector<std::string> v(count(4));
        for (auto& s : v) s = str();
        return v;
    }
    template <size_t N> std::vector<Key<N>> keys() {
        std::vector<Key<N>> v(count((N + 1) * 4));
        for (auto& k : v) {
            fs(k.v);
            k.frame = f();
        }
        return v;
    }
    ObjectHeader header() { return readObjectHeaderAt(b_, p_); }
    void trans(Trans& t) { readTransAt(b_, p_, t); }
    void draw(Draw& d) { readDrawAt(b_, p_, d); }
    // RndAnimatable v4 (@ 0x821e96c0).
    void anim(Animatable& a) {
        if (u32() != 4) throw std::runtime_error("Anim version (decoder reads 4)");
        a.unknown = u32();
        a.rate = u32();
    }
    Bitmap bitmap() {
        ByteReader r(b_);
        r.seek(p_);
        Bitmap bm = readBitmap(r);
        p_ = r.pos();
        return bm;
    }

private:
    void need(size_t n) const {
        if (n > b_.size() || p_ > b_.size() - n) throw std::runtime_error("record ends early");
    }
    const Bytes& b_;
    size_t& p_;
};

[[noreturn]] void bad(const std::string& what) { throw std::runtime_error(what); }

void checkVersion(uint32_t v, const char* cls, uint32_t want) {
    if (v != want) bad(std::string(cls) + " version " + std::to_string(v) + " (decoder reads " + std::to_string(want) + ")");
}

CamTarget camTarget(Rd& r) {
    CamTarget t;
    t.rev = r.u32();
    if (t.rev >= 0x1c) bad("CamShot target revision " + std::to_string(t.rev));
    t.object = r.str();
    t.subpart = r.str();
    return t;
}

// CamShot::Load @ 0x8222de20; keyframe @ 0x8222a5b8 (the field offsets in
// comments are the keyframe's in memory).
void readCamShot(Rd& r, CamShot& c) {
    checkVersion(c.version, "CamShot", 27);
    c.header = r.header();
    r.anim(c.anim);
    c.keyframes.resize(r.count(100));
    for (auto& k : c.keyframes) {
        k.duration = r.f();
        k.blend = r.f();
        k.blendEase = r.f();
        k.fieldOfView = r.f();
        r.xfm(k.xfm);              // +0x18
        r.fs(k.screenOffset);      // +0x58
        k.blurDepth = r.f();       // +0x70
        k.maxBlur = r.f();         // +0x74 (rev >= 24)
        k.focusBlurMultiplier = r.f();  // +0x78 (rev >= 21)
        k.targets.resize(r.count(12));  // +0x7c
        for (auto& t : k.targets) t = camTarget(r);
        k.focalTarget = camTarget(r);   // +0xec (rev >= 27)
        k.parent = camTarget(r);        // +0x9c
        k.useParentRotation = r.b();    // +0xfc
        k.shakeNoiseAmp = r.f();        // +0x64
        k.shakeNoiseFreq = r.f();       // +0x60
        r.fs(k.shakeMaxAngle);          // +0x68
        k.zoomFov = r.f();              // +0x14 (rev >= 22)
    }
    c.looping = r.b();
    c.pathEase = r.f();
    c.nearPlane = r.f();
    c.farPlane = r.f();
    c.useDepthOfField = r.b();
    c.filter = r.f();
    c.clampHeight = r.f();
    c.path = r.str();
    c.fadeTime = r.f();
    c.category = r.str();
    c.selectionWeight = r.f();
    c.crowd3d.resize(r.count(8));  // @ 0x8222a5b8
    for (auto& p : c.crowd3d) p.first = r.u32(), p.second = r.u32();
    c.unused = r.u32();
    c.hideList = r.strs();         // @ 0x82227ad8
    c.crowd = r.str();             // @ 0x82225e68
    c.glowSpot = r.str();          // @ 0x82225ef0
}

void readLightPreset(Rd& r, LightPreset& p) {
    checkVersion(p.version, "LightPreset", 9);
    p.header = r.header();
    r.anim(p.anim);
    p.keyframes.resize(r.count(16));
    for (auto& k : p.keyframes) {
        k.duration = r.f();
        k.fadeOut = r.f();
        k.spots.resize(r.count(45));
        for (auto& s : k.spots) {
            s.intensity = r.f();
            r.fs(s.rotation);
            r.fs(s.color);
            s.target = r.str();
            s.subpart = r.str();
            s.flare = r.b();
        }
        k.environs.resize(r.count(41));
        for (auto& e : k.environs) {
            r.fs(e.ambient);
            e.fogEnable = r.b();
            e.fogStart = r.f();
            e.fogEnd = r.f();
            r.fs(e.fogColor);
        }
        k.lights.resize(r.count(60));
        for (auto& l : k.lights) {
            r.fs(l.rotation);
            r.fs(l.position);
            r.fs(l.color);
            l.range = r.f();
            l.falloff = r.f();
        }
        k.description = r.str();
    }
    p.spotlights = r.strs();
    p.environs = r.strs();
    p.lights = r.strs();
    p.looping = r.b();
    p.category = r.str();
    p.adjectives = r.strs();
    p.trigger = r.str();
    p.fadeIn = r.f();
    p.minExcitement = r.i32();
    p.maxExcitement = r.i32();
    p.manual = r.b();
    p.locked = r.b();
}

void readSpotlight(Rd& r, Spotlight& s) {
    checkVersion(s.version, "Spotlight", 25);
    s.header = r.header();
    r.draw(s.draw);
    r.trans(s.trans);
    s.spotScale = r.f();
    s.spotHeight = r.f();
    auto& b = s.beam;
    b.isCone = r.b();
    b.length = r.f();
    b.bottomRadius = r.f();
    b.topRadius = r.f();
    b.topSideBorder = r.f();
    b.bottomSideBorder = r.f();
    b.bottomBorder = r.f();
    b.material = r.str();
    b.offset = r.f();
    r.fs(b.angleOffset);
    b.brighten = r.f();
    b.xsection = r.str();
    b.expand = r.f();
    b.cutouts = r.strs();
    s.lightCan = r.str();
    s.target = r.str();
    s.lightCanOffset = r.f();
    r.fs(s.color);
    s.intensity = r.f();
    s.spotMaterial = r.str();
    s.dampingConstant = r.f();
    s.targetSubpart = r.str();
    s.flareMaterial = r.str();
    r.fs(s.flareSize);
    r.fs(s.flareRange);
    s.flareSteps = r.u32();
    s.flareOffset = r.f();
    s.flareEnabled = r.b();
    s.flareVisibilityTest = r.b();
    s.lensSize = r.f();
    s.lensOffset = r.f();
    s.lensMaterial = r.str();
    s.additionalObjects = r.strs();
    s.targetShadow = r.b();
    s.animateFromPreset = r.b();
}

void readWorldCrowd(Rd& r, WorldCrowd& c) {
    checkVersion(c.version, "WorldCrowd", 6);
    r.draw(c.draw);
    c.placementMesh = r.str();
    c.num = r.u32();
    c.rotate = r.b();
    c.characters.resize(r.count(16));
    for (auto& ch : c.characters) {
        ch.character = r.str();
        ch.height = r.f();
        ch.density = r.f();
        ch.radius = r.f();
    }
    for (auto& ch : c.characters) {
        ch.placements.resize(r.count(48));
        for (auto& x : ch.placements) r.xfm(x);
    }
    c.unknown = r.f();
    c.show3dOnly = r.b();
    c.header = r.header();  // the object block comes last in this class
}

void readEventTrigger(Rd& r, EventTrigger& e) {
    checkVersion(e.version, "EventTrigger", 10);
    e.header = r.header();
    e.triggerEvents = r.strs();
    e.anims.resize(r.count(30));
    for (auto& a : e.anims) {
        a.anim = r.str();
        a.blend = r.f();
        a.wait = r.b();
        a.delay = r.f();
        a.enable = r.b();
        a.rate = r.u32();
        a.start = r.f();
        a.end = r.f();
        a.scale = r.f();
        a.type = r.str();
        a.period = r.f();
    }
    e.sounds = r.strs();
    e.shows = r.strs();
    e.hideDelays.resize(r.count(8));
    for (auto& h : e.hideDelays) h.first = r.str(), h.second = r.f();
    e.enableEvents = r.strs();
    e.disableEvents = r.strs();
    e.waitForEvents = r.strs();
    e.nextRandom = r.str();
    e.proxyCalls.resize(r.count(8));
    for (auto& p : e.proxyCalls) p.first = r.str(), p.second = r.str();
}

void readPostProc(Rd& r, PostProc& p) {
    checkVersion(p.version, "PostProc", 9);
    p.header = r.header();
    r.fs(p.bloomThreshold);
    p.bloomScale = r.f();
    p.bloomIntensity = r.f();
    p.bloomKernelMult = r.f();
    p.luminanceMap = r.str();
    r.fs(p.colorXfm);
    r.fs(p.intensity);
    r.fs(p.secsRange);
    r.fs(p.noiseBaseScale);
    p.unknownD0 = r.f();
    p.noiseIntensity = r.f();
    p.noiseMap = r.str();
    p.hue = r.f();
    p.saturation = r.f();
    p.lightness = r.f();
    p.contrast = r.f();
    p.brightness = r.f();
    p.threshold = r.f();
    p.duration = r.f();
    p.emulateFps = r.f();
}

void readFlare(Rd& r, Flare& f) {
    checkVersion(f.version, "Flare", 4);
    f.header = r.header();
    r.trans(f.trans);
    r.draw(f.draw);
    f.mat = r.str();
    r.fs(f.size);
    r.fs(f.range);
    f.steps = r.u32();
}

void readLightAnim(Rd& r, LightAnim& a) {
    checkVersion(a.version, "LightAnim", 2);
    a.header = r.header();
    r.anim(a.anim);
    a.light = r.str();
    a.colorKeys = r.keys<4>();
    a.keysOwner = r.str();
}

void readEnvAnim(Rd& r, EnvAnim& a) {
    checkVersion(a.version, "EnvAnim", 4);
    a.header = r.header();
    r.anim(a.anim);
    a.environName = r.str();
    a.ambientKeys = r.keys<4>();
    a.keysOwner = r.str();
    a.fogColorKeys = r.keys<4>();   // rev > 1
    a.fogRangeKeys = r.keys<2>();   // rev > 2
}

void readCamAnim(Rd& r, CamAnim& a) {
    checkVersion(a.version, "CamAnim", 2);
    a.header = r.header();
    r.anim(a.anim);
    a.cam = r.str();
    // Q: (fov, frame) pairs; none on the disc.
    a.fovKeys = r.keys<1>();
    a.keysOwner = r.str();
}

void readSet(Rd& r, SetObj& s) {
    checkVersion(s.version, "Set", 0);
    s.header = r.header();
    s.objects = r.strs();
}

void readCubeTex(Rd& r, CubeTex& c) {
    checkVersion(c.version, "CubeTex", 1);
    c.header = r.header();
    c.faceSize = r.u32();
    c.bpp = r.u32();
    for (auto& f : c.files) f = r.str();
    c.inlineBitmaps = r.b();  // PostLoad
    if (c.inlineBitmaps)
        for (auto& b : c.faces) b = r.bitmap();
}

void readWaypoint(Rd& r, Waypoint& w) {
    checkVersion(w.version, "Waypoint", 3);
    w.header = r.header();
    r.draw(w.draw);
    r.trans(w.trans);
    w.flags = r.u32();
    w.connections = r.strs();
    w.radius = r.f();
    r.fs(w.unknown);
}

struct WorldDecoder {
    std::function<std::unique_ptr<Object>()> make;
    std::function<void(Rd&, Object&)> read;
};

template <class T> WorldDecoder dec(void (*fn)(Rd&, T&)) {
    return {[] { return std::unique_ptr<Object>(new T); }, [fn](Rd& r, Object& o) { fn(r, static_cast<T&>(o)); }};
}

const std::map<std::string, WorldDecoder>& worldDecoders() {
    static const std::map<std::string, WorldDecoder> m = {
        {"CamShot", dec<CamShot>(readCamShot)},
        {"LightPreset", dec<LightPreset>(readLightPreset)},
        {"Spotlight", dec<Spotlight>(readSpotlight)},
        {"WorldCrowd", dec<WorldCrowd>(readWorldCrowd)},
        {"EventTrigger", dec<EventTrigger>(readEventTrigger)},
        {"PostProc", dec<PostProc>(readPostProc)},
        {"Flare", dec<Flare>(readFlare)},
        {"LightAnim", dec<LightAnim>(readLightAnim)},
        {"EnvAnim", dec<EnvAnim>(readEnvAnim)},
        {"CamAnim", dec<CamAnim>(readCamAnim)},
        {"Set", dec<SetObj>(readSet)},
        {"CubeTex", dec<CubeTex>(readCubeTex)},
        {"Waypoint", dec<Waypoint>(readWaypoint)},
    };
    return m;
}

std::string nodeText(const DataNode& n) {
    switch (n.type) {
    case 0: return std::to_string(int32_t(n.raw));
    case 1: {
        char b[32];
        std::snprintf(b, sizeof b, "%g", double(n.asFloat()));
        return b;
    }
    case 0x10: case 0x11: case 0x13: {
        std::string s;
        for (const auto& c : n.children) s += (s.empty() ? "" : " ") + nodeText(c);
        return s;
    }
    default: return n.str;
    }
}

}  // namespace

float CamShot::lengthFrames() const {
    float f = 0;
    for (const auto& k : keyframes) f += k.duration + k.blend;
    return f;
}

std::string CamShot::flag(const std::string& key) const {
    const auto& c = header.props.children;
    for (size_t i = 0; i + 1 < c.size(); i += 2)
        if (c[i].str == key) return nodeText(c[i + 1]);
    return {};
}

bool isWorldClass(const std::string& cls) { return worldDecoders().count(cls) != 0; }

std::unique_ptr<Object> makeWorldObject(const std::string& cls) {
    auto it = worldDecoders().find(cls);
    return it != worldDecoders().end() ? it->second.make() : std::make_unique<Object>();
}

void readWorldObject(Object& o, const Bytes& record, size_t& pos) {
    auto it = worldDecoders().find(o.cls);
    if (it == worldDecoders().end()) bad("no world decoder for " + o.cls);
    Rd r(record, pos);
    it->second.read(r, o);
}

}  // namespace scene
