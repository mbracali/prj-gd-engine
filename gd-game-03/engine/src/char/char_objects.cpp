// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/char_objects.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>

namespace scene {

namespace {

[[noreturn]] void bad(const std::string& what) { throw std::runtime_error(what); }

// Little-endian reads at a position of one record.
class Rd {
public:
    Rd(const Bytes& b, size_t& pos) : b_(b), p_(pos) {}
    uint32_t u32() {
        need(4);
        const uint8_t* q = b_.data() + p_;
        p_ += 4;
        return uint32_t(q[0]) | uint32_t(q[1]) << 8 | uint32_t(q[2]) << 16 | uint32_t(q[3]) << 24;
    }
    uint16_t u16() {
        need(2);
        const uint8_t* q = b_.data() + p_;
        p_ += 2;
        return uint16_t(q[0] | q[1] << 8);
    }
    int16_t s16() { return int16_t(u16()); }
    uint8_t u8() {
        need(1);
        return b_[p_++];
    }
    bool b() { return u8() != 0; }
    float f() {
        uint32_t u = u32();
        float v;
        std::memcpy(&v, &u, 4);
        return v;
    }
    template <size_t N> void fs(std::array<float, N>& a) {
        for (float& v : a) v = f();
    }
    std::string str() {
        uint32_t n = u32();
        if (n > b_.size() - p_) bad("string past the end");
        std::string s(reinterpret_cast<const char*>(b_.data() + p_), n);
        p_ += n;
        return s;
    }
    uint32_t count(size_t minBytes) {
        uint32_t n = u32();
        if (n > (b_.size() - p_) / minBytes) bad("implausible list size");
        return n;
    }
    std::vector<std::string> strs() {
        std::vector<std::string> v(count(4));
        for (auto& s : v) s = str();
        return v;
    }
    uint32_t version(std::initializer_list<uint32_t> ok, const char* what) {
        uint32_t v = u32();
        for (uint32_t x : ok)
            if (v == x) return v;
        bad(std::string(what) + " version " + std::to_string(v));
    }
    ObjectHeader header() { return readObjectHeaderAt(b_, p_); }
    void trans(Trans& t) { readTransAt(b_, p_, t); }
    void draw(Draw& d) { readDrawAt(b_, p_, d); }
    void xfm(Xfm& x) {
        fs(x.rot);
        fs(x.pos);
    }
    size_t pos() const { return p_; }
    size_t left() const { return b_.size() - p_; }

private:
    void need(size_t n) {
        if (n > b_.size() - p_) bad("unexpected end of data");
    }
    const Bytes& b_;
    size_t& p_;
};

// CharWeightable::Load 0x8216A190.
CharWeightable weightable(Rd& r) {
    CharWeightable w;
    w.revision = r.version({1, 2, 3}, "CharWeightable");
    w.weight = r.f();
    if (w.revision > 1) w.weightOwner = r.str();
    return w;
}

// ObjectDir PreLoad 0x82322598 (v17) / PostLoad 0x8231ff08; RndDir v9
// (PreLoad 0x821ba438, PostLoad 0x821ba4c8): mirrors scene/objects.cpp.
void objectDirPre(Rd& r, ObjectDirData& o) {
    r.version({17}, "ObjectDir");
    uint32_t n = r.count(52);
    o.viewports.resize(n);
    for (auto& v : o.viewports) {
        r.xfm(v);
        r.u32();
    }
    o.currentViewport = r.u32();
    o.proxyFile = r.str();
    o.subdirs = r.strs();
    o.inlineProxy = r.u32();
}

void objectDirPost(Rd& r, ObjectDirData& o, ObjectHeader& h) {
    o.postRef0 = r.str();
    o.postRef1 = r.str();
    h = r.header();
}

void rndDirPost(Rd& r, ObjectDirData& o, ObjectHeader& h, RndDirData& d) {
    objectDirPost(r, o, h);
    r.version({4}, "Anim");
    d.anim.unknown = r.u32();
    d.anim.rate = r.u32();
    r.draw(d.draw);
    r.trans(d.trans);
    d.environment = r.str();
}

// Character PreLoad 0x82153CF8 (rev 10; BandCharacter PreLoad 0x82126048 first: u32 1).
void characterPre(Rd& r, CharacterObj& c, bool readBandVersion) {
    if (readBandVersion) r.version({1}, "BandCharacter");
    r.version({10}, "Character");
    r.version({9}, "RndDir");
    objectDirPre(r, c.objectDir);
}

// ---- clips

void bonesHeader(Rd& r, CharBonesSamples& s) {
    uint32_t n = r.count(8);
    s.bones.resize(n);
    for (auto& b : s.bones) {
        b.name = r.str();
        b.weight = r.f();
        b.type = CharBonesSamples::typeOf(b.name);
    }
    for (auto& o : s.offs) o = r.u32();
    s.compression = r.u32();
    s.count = r.u32();
    // Bones sorted by type, offsets agreeing with the names [C data].
    for (size_t i = 1; i < s.bones.size(); ++i)
        if (s.bones[i].type < s.bones[i - 1].type) bad("bone channels not sorted by type");
    for (int t = 0; t < 9; ++t) {
        uint32_t below = 0;
        for (const auto& b : s.bones) below += b.type < t ? 1 : 0;
        if (s.offs[size_t(t)] != below) bad("bone channel offsets disagree with the names");
    }
    if (s.offs[9] != n) bad("bone channel total disagrees");
    s.offset.resize(n);
    int w = 0;
    for (size_t i = 0; i < n; ++i) {
        s.offset[i] = w;
        const int t = s.bones[i].type;
        w += t <= CharBonesSamples::kScale ? 3 : t == CharBonesSamples::kQuat ? 4 : 1;
    }
    s.width = w;
}

constexpr float kQuatScale = 3.051851e-05f;      // s16 / 32767 (0x8215d338)
constexpr float kRotScale = 0.0006103515625f;    // s16 -> radians (0x821a1a58)

// CharBonesSamples data 0x821A1500.
void bonesData(Rd& r, CharBonesSamples& s) {
    const bool c = s.compression != 0;
    if (s.width > 0 && s.count > r.left()) bad("implausible sample count");  // a set without channels keeps its count
    s.values.resize(size_t(s.count) * size_t(s.width));
    float* out = s.values.data();
    for (uint32_t k = 0; k < s.count; ++k)
        for (const auto& b : s.bones) {
            if (b.type <= CharBonesSamples::kScale) {
                for (int i = 0; i < 3; ++i) *out++ = r.f();
            } else if (b.type == CharBonesSamples::kQuat) {
                for (int i = 0; i < 4; ++i) *out++ = c ? float(r.s16()) * kQuatScale : r.f();
            } else {
                *out++ = c ? float(r.s16()) * kRotScale : r.f();
            }
        }
}

// CharClip::Load 0x821A0BF0 (rev 5).
void charClip(Rd& r, CharClipSamples& k) {
    k.clipRevision = r.version({5}, "CharClip");
    k.header = r.header();
    k.startFrame = r.f();
    k.endFrame = r.f();
    k.framesPerSec = r.f();
    k.flags = r.u32();
    k.playFlags = r.u32();
    k.blendWidth = r.f();
    k.range = r.f();
    k.relative = r.b();
    k.transitions.resize(r.count(8));
    for (auto& t : k.transitions) {
        t.dest = r.str();
        t.nodes.resize(r.count(8));
        for (auto& n : t.nodes) n.first = r.f(), n.second = r.f();
    }
    k.enterEvent = r.str();
    k.exitEvent = r.str();
    k.events.resize(r.count(8));
    for (auto& e : k.events) {
        e.frame = r.f();
        e.script = r.str();
    }
}

void readClipSamples(Rd& r, CharClipSamples& k) {
    charClip(r, k);
    CharBonesSamples* sets[3] = {&k.full, &k.one, &k.third};
    for (auto* s : sets) bonesHeader(r, *s);
    for (auto* s : sets) bonesData(r, *s);
}

void readCharDriverBody(Rd& r, CharDriver& d) {
    r.version({5}, "CharDriver");
    d.header = r.header();
    d.weightable = weightable(r);
    d.bones = r.str();
    d.clips = r.str();
    d.realign = r.b();
    d.unknown55 = r.b();
    d.starvedClip = r.str();
}

void readIKHand(Rd& r, CharIKHand& h) {
    h.header = r.header();
    h.weightable = weightable(r);
    h.hand = r.str();
    h.finger = r.str();
    h.targets = r.strs();
    h.orient = r.b();
    h.stretch = r.b();
    h.scaleX = r.b();
    h.moveElbow = r.b();
}

struct Decoder {
    uint32_t version;
    std::function<std::unique_ptr<Object>()> make;
    std::function<void(Rd&, Object&)> read;
};

template <class T> Decoder dec(uint32_t version, std::function<void(Rd&, T&)> fn) {
    return {version, [] { return std::make_unique<T>(); }, [fn](Rd& r, Object& o) { fn(r, static_cast<T&>(o)); }};
}

const std::map<std::string, Decoder>& charDecoders() {
    static const std::map<std::string, Decoder> m = {
        // CharClipSamples::Load 0x82162C68 (rev 11).
        {"CharClipSamples", dec<CharClipSamples>(11, readClipSamples)},
        // CharBone::Load 0x8215FDF0 (rev 3).
        {"CharBone", dec<CharBone>(3, [](Rd& r, CharBone& b) {
             b.header = r.header();
             r.trans(b.trans);
             b.position = r.b();
             b.scale = r.b();
             b.rotation = r.u32();
             b.deltaRotation = r.u32();
             b.weight = r.f();
         })},
        // CharClipGroup::Load 0x821775A8 (rev 1).
        {"CharClipGroup", dec<CharClipGroup>(1, [](Rd& r, CharClipGroup& g) {
             g.header = r.header();
             g.clips = r.strs();
             g.flags = r.u32();
         })},
        // CharClipFilter 0x82163F20: the object header only (its u32 1 is the header's revision).
        {"CharClipFilter", {1, [] { return std::make_unique<CharClipFilter>(); }, [](Rd&, Object&) {}}},
        // CharDriver Load 0x8216A728 / PreLoad 0x8216BE80 / PostLoad 0x8216C800 (rev 5).
        {"CharDriver", {5, [] { return std::make_unique<CharDriver>(); }, [](Rd& r, Object& o) {
             // The version was read by the dispatcher: re-read the body from it.
             auto& d = static_cast<CharDriver&>(o);
             d.header = r.header();
             d.weightable = weightable(r);
             d.bones = r.str();
             d.clips = r.str();
             d.realign = r.b();
             d.unknown55 = r.b();
             d.starvedClip = r.str();
         }}},
        // CharDriverMidi Pre 0x82185590 (u32 3, CharDriver Pre), Post 0x82185600 (CharDriver Post, ObjPtr<CharClip>).
        {"CharDriverMidi", dec<CharDriverMidi>(3, [](Rd& r, CharDriverMidi& d) {
             readCharDriverBody(r, d);
             d.inactiveClip = r.str();
         })},
        // CharIKHand::Load 0x82174470 (rev 5).
        {"CharIKHand", dec<CharIKHand>(5, readIKHand)},
        // CharIKMidi::Load 0x82184950 (rev 4).
        {"CharIKMidi", dec<CharIKMidi>(4, [](Rd& r, CharIKMidi& k) {
             k.header = r.header();
             k.bone = r.str();
         })},
        // CharHair::Load 0x8217FAF0 (rev 2).
        {"CharHair", dec<CharHair>(2, [](Rd& r, CharHair& h) {
             h.header = r.header();
             r.fs(h.params);
             h.strands.resize(r.count(8));
             for (auto& s : h.strands) {
                 s.root = r.str();
                 s.angle = r.f();
                 s.points.resize(r.count(8));
                 for (auto& p : s.points) {  // 0x8217C0F8
                     r.fs(p.pos);
                     p.bone = r.str();
                     p.length = r.f();
                     p.f50 = r.f();
                     p.collide = r.str();
                     p.f60 = r.f();
                     p.f64 = r.f();
                 }
                 r.fs(s.baseMat);
                 r.fs(s.baseMat2);
             }
             h.simulate = r.b();
         })},
        // CharLookAt::Load 0x8216EBB0 (rev 3).
        {"CharLookAt", dec<CharLookAt>(3, [](Rd& r, CharLookAt& l) {
             l.header = r.header();
             l.weightable = weightable(r);
             l.source = r.str();
             l.pivot = r.str();
             l.dest = r.str();
             r.fs(l.values);
             l.allowRoll = r.b();
         })},
        // CharEyes::Load 0x82172708 (rev 4).
        {"CharEyes", dec<CharEyes>(4, [](Rd& r, CharEyes& e) {
             e.header = r.header();
             e.eyes = r.strs();
             e.viewDirection = r.str();
             e.interests.resize(r.count(8));
             for (auto& i : e.interests) {
                 i.first = r.str();
                 i.second = r.u32();
             }
         })},
        // CharWeightSetter::Load 0x8218DFC0 (rev 3).
        {"CharWeightSetter", dec<CharWeightSetter>(3, [](Rd& r, CharWeightSetter& w) {
             w.header = r.header();
             w.weightable = weightable(r);
             w.driver = r.str();
             w.flags = r.u32();
             w.invert = r.b();
         })},
        // CharUpperTwist::Load 0x82182C00 (rev 1).
        {"CharUpperTwist", dec<CharUpperTwist>(1, [](Rd& r, CharUpperTwist& t) {
             t.header = r.header();
             t.upperArm = r.str();
             t.twist1 = r.str();
             t.twist2 = r.str();
         })},
        // CharForeTwist::Load 0x82182018 (rev 1).
        {"CharForeTwist", dec<CharForeTwist>(1, [](Rd& r, CharForeTwist& t) {
             t.header = r.header();
             t.offset = r.f();
             t.hand = r.str();
             t.twist2 = r.str();
         })},
        // CharServoBone::Load 0x82188870, CharWalk::Load 0x82189F80 (rev 1): header only.
        {"CharServoBone", dec<CharServoBone>(1, [](Rd& r, CharServoBone& s) { s.header = r.header(); })},
        {"CharWalk", dec<CharWalk>(1, [](Rd& r, CharWalk& s) { s.header = r.header(); })},
        // CharPosConstraint::Load 0x82179178 (rev 2).
        {"CharPosConstraint", dec<CharPosConstraint>(2, [](Rd& r, CharPosConstraint& c) {
             c.header = r.header();
             c.targets = r.strs();
             c.src = r.str();
             r.fs(c.boxMin);
             r.fs(c.boxMax);
         })},
        // CharIKRod::Load 0x821806F8 (rev 2).
        {"CharIKRod", dec<CharIKRod>(2, [](Rd& r, CharIKRod& k) {
             k.header = r.header();
             k.leftEnd = r.str();
             k.rightEnd = r.str();
             k.destPos = r.f();
             k.sideAxis = r.str();
             k.vertical = r.b();
             k.dest = r.str();
             r.fs(k.xfm);
         })},
        // OutfitLoader PreLoad 0x8215B528 (rev 2).
        {"OutfitLoader", dec<OutfitLoader>(2, [](Rd& r, OutfitLoader& o) {
             o.header = r.header();
             o.path = r.str();
             o.categories.resize(r.u16());
             for (auto& c : o.categories) {
                 c.current = r.u8();
                 c.next = r.u8();
                 c.name = r.str();
                 c.items.resize(r.u16());
                 for (auto& i : c.items) i.first = r.u8(), i.second = r.u8();
             }
         })},
        // FaceFxLipSyncServo v5 (Load 0x8218E378, PreLoad 0x8218FAF0, PostLoad 0x82192470).
        {"FaceFxLipSyncServo", dec<FaceFxLipSyncServo>(5, [](Rd& r, FaceFxLipSyncServo& s) {
             s.header = r.header();
             s.weightable = weightable(r);
             s.faceGraph = r.str();
             s.visemeSet = r.str();
             s.entries.resize(r.count(12));
             for (auto& e : s.entries) {
                 e.object = r.str();
                 e.axis = r.u32();
                 e.node = r.str();
             }
         })},
        // Character / BandCharacter in a venue's <v>_chars milo: Pre + RndDir PostLoad
        // (Character PostLoad 0x82157678 reads its own fields only for its own record).
        {"Character", dec<CharacterObj>(10, [](Rd& r, CharacterObj& c) {
             r.version({9}, "RndDir");
             objectDirPre(r, c.objectDir);
             rndDirPost(r, c.objectDir, c.header, c.rnd);
         })},
        {"BandCharacter", dec<CharacterObj>(1, [](Rd& r, CharacterObj& c) {
             c.band = true;
             r.version({10}, "Character");
             r.version({9}, "RndDir");
             objectDirPre(r, c.objectDir);
             rndDirPost(r, c.objectDir, c.header, c.rnd);
         })},
    };
    return m;
}

}  // namespace

std::string CharBonesSamples::boneOf(const std::string& channel) {
    const size_t dot = channel.rfind('.');
    return dot == std::string::npos ? channel : channel.substr(0, dot);
}

int CharBonesSamples::typeOf(const std::string& channel) {
    static const char* kTypes[] = {"pos", "scale", "quat", "rotx", "roty", "rotz", "drotx", "droty", "drotz"};
    const size_t dot = channel.rfind('.');
    const std::string suf = dot == std::string::npos ? channel : channel.substr(dot + 1);
    for (int i = 0; i < 9; ++i)
        if (suf == kTypes[i]) return i;
    return kNone;
}

CharClipSetData decodeClipSetDir(const Object& self, size_t clipCount) {
    CharClipSetData s;
    try {
        size_t pos = 0;
        Rd r(self.record, pos);
        r.version({16}, "CharClipSet");
        objectDirPre(r, s.objectDir);
        objectDirPost(r, s.objectDir, s.header);
        s.blendWidth = r.f();
        s.playFlags = r.u32();
        s.defaultFlags = r.u32();
        s.clips.resize(clipCount);
        for (auto& e : s.clips) {
            e.name = r.str();
            e.flags = r.u32();
            e.unknown = r.u32();
        }
        s.moveSelf = r.b();
        s.recenter.targets = r.strs();
        s.recenter.average = r.strs();
        s.recenter.slide = r.b();
        s.type = r.str();
        s.typeVersion = r.u32();
        if (r.left() != 0) bad(std::to_string(r.left()) + " bytes left before the end marker");
        s.decoded = true;
    } catch (const std::exception& e) {
        s.error = e.what();
    }
    return s;
}

std::unique_ptr<CharacterObj> decodeCharacterDir(const Object& self) {
    auto c = std::make_unique<CharacterObj>();
    c->cls = self.cls;
    c->name = self.name;
    c->version = self.version;
    c->record = self.record;
    c->typed = true;
    c->own = true;
    c->band = self.cls == "BandCharacter";
    size_t pos = 0;
    Rd r(c->record, pos);
    characterPre(r, *c, c->band);
    rndDirPost(r, c->objectDir, c->header, c->rnd);
    c->lods.resize(r.count(8));
    for (auto& l : c->lods) {
        l.screenSize = r.f();
        l.group = r.str();
    }
    c->shadow = r.str();
    c->selfShadow = r.b();
    c->sphereBase = r.str();
    // CharacterTest::Load 0x8219B7E0 (rev 6), the editor's block.
    auto& t = c->test;
    r.version({6}, "CharacterTest");
    t.driver = r.str();
    t.clip1 = r.str();
    t.clip2 = r.str();
    t.teleportTo = r.str();
    t.walkPath = r.strs();
    t.showDistMap = r.str();
    t.transition = r.u32();
    t.cycleTransition = r.b();
    t.internalTransition = r.u32();
    t.showFootsteps = r.u32();
    t.b69 = r.b();
    t.b6a = r.b();
    t.b6b = r.b();
    t.sym6c = r.str();
    t.b70 = r.b();
    t.b71 = r.b();
    t.u74 = r.u32();
    if (r.left() != 0) bad(std::to_string(r.left()) + " bytes left in the Character record");
    c->decoded = true;
    return c;
}

std::map<std::string, std::vector<float>> referencePose(const CharClipSamples& clip, float frame) {
    std::map<std::string, std::vector<float>> out;
    auto sampleSet = [&](const CharBonesSamples& h, float t) {
        const uint32_t n = h.count;
        if (n == 0) return;
        uint32_t i = 0;
        float f = 0;
        if (n >= 2) {
            const float x = std::min(std::max(t, 0.0f), 1.0f) * float(n - 1);
            i = std::min(uint32_t(x), n - 1);
            f = i < n - 1 ? x - float(i) : 0.0f;
        }
        const float* a = h.sample(i);
        const float* b = f > 0 ? h.sample(i + 1) : a;
        for (size_t k = 0; k < h.bones.size(); ++k) {
            const int t2 = h.bones[k].type;
            const int w = t2 <= CharBonesSamples::kScale ? 3 : t2 == CharBonesSamples::kQuat ? 4 : 1;
            std::vector<float> v(static_cast<size_t>(w), 0.0f);
            for (int j = 0; j < w; ++j) {
                const float p = a[h.offset[k] + j], q = b[h.offset[k] + j];
                v[size_t(j)] = p + (q - p) * f;
            }
            out[h.bones[k].name] = v;
        }
    };
    const float span = clip.endFrame - clip.startFrame;
    const float t = (frame - clip.startFrame) / (span != 0 ? span : 1.0f);
    sampleSet(clip.one, 0.0f);
    sampleSet(clip.full, t);
    for (auto& [name, v] : out)
        if (CharBonesSamples::typeOf(name) == CharBonesSamples::kQuat) {
            float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2] + v[3] * v[3]);
            if (l == 0) l = 1;
            for (float& x : v) x /= l;
        }
    return out;
}

bool isCharClass(const std::string& cls) { return charDecoders().count(cls) != 0; }

std::unique_ptr<Object> makeCharObject(const std::string& cls) {
    auto it = charDecoders().find(cls);
    return it != charDecoders().end() ? it->second.make() : std::make_unique<Object>();
}

void readCharObject(Object& o, const Bytes& record, size_t& pos) {
    auto it = charDecoders().find(o.cls);
    if (it == charDecoders().end()) bad("no character decoder for " + o.cls);
    if (o.cls == "CharClipFilter") {
        // Only the object header: its revision is the record's first u32.
        pos = 0;
        o.header = readObjectHeaderAt(record, pos);
        return;
    }
    if (o.version != it->second.version)
        bad(o.cls + " version " + std::to_string(o.version) + " (decoder reads " + std::to_string(it->second.version) + ")");
    Rd r(record, pos);
    it->second.read(r, o);
}

}  // namespace scene
