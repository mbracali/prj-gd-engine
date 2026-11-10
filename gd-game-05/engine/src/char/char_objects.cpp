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
        return miloU32(q);
    }
    uint16_t u16() {
        need(2);
        const uint8_t* q = b_.data() + p_;
        p_ += 2;
        return miloU16(q);
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
// RB: ObjectDir PreLoad RB 0x8247FB78 (rev 19) [C]: the viewports are four
// vec3 each (GH2's extra u32 per viewport is gone), then the current
// viewport, the proxy file and the subdirs; no inline-proxy u32.
void objectDirPre(Rd& r, ObjectDirData& o) {
    r.version({19}, "ObjectDir");
    uint32_t n = r.count(48);
    o.viewports.resize(n);
    for (auto& v : o.viewports) r.xfm(v);
    o.currentViewport = r.u32();
    o.proxyFile = r.str();
    o.subdirs = r.strs();
    o.inlineProxy = 0;
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
    d.testEvent = r.str();  // RB RndDir PostLoad RB 0x821B1278 (rev 10): the test event symbol [C]
}

// Character PreLoad RB 0x82307140 (rev 12) then RndDir PreLoad RB 0x821B1B28
// (rev 10) [C]; CompositeCharacter PreLoad RB 0x822E4260 reads its u32 9 first.
void characterPre(Rd& r, CharacterObj& c, bool readCompositeVersion) {
    if (readCompositeVersion) r.version({9}, "CompositeCharacter");
    r.version({12}, "Character");
    r.version({10}, "RndDir");
    objectDirPre(r, c.objectDir);
}

// ---- clips

void bonesHeader(Rd& r, CharBonesSamples& s) {
    s.revision = r.version({11, 12, 13, 14, 15, 16}, "CharBonesSamples");
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
    // RB CharBonesSamples header RB 0x82366080: (rev >= 12) a float list
    // (its use is not read yet [I]); (rev 14) a bool [C].
    if (s.revision >= 12) {
        s.frames.resize(r.count(4));
        for (float& f : s.frames) f = r.f();
    }
    if (s.revision == 14) r.b();
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

// CharBonesSamples data RB 0x82364D10 (rev >= 15, the 360's native data):
// one raw block of count samples in the in-memory layout of RB 0x8231E378
// [C]: a position or scale is a vec3 padded to 16 bytes, a quaternion 4 s16
// (compressed) or 4 f32, a one-axis rotation s16 or f32, each sample padded
// to 16 bytes. The s16 scales are GH2's [I for RB].
void bonesData(Rd& r, CharBonesSamples& s) {
    const bool c = s.compression != 0;
    size_t mem = 0;
    for (const auto& b : s.bones)
        mem += b.type <= CharBonesSamples::kScale ? 16 : b.type == CharBonesSamples::kQuat ? (c ? 8 : 16) : (c ? 2 : 4);
    const size_t sampleSize = (mem + 15) & ~size_t(15);
    if (sampleSize * s.count > r.left()) bad("sample data past the end");
    s.values.resize(size_t(s.count) * size_t(s.width));
    float* out = s.values.data();
    for (uint32_t k = 0; k < s.count; ++k) {
        const size_t start = r.pos();
        for (const auto& b : s.bones) {
            if (b.type <= CharBonesSamples::kScale) {
                for (int i = 0; i < 3; ++i) *out++ = r.f();
                r.f();  // padding
            } else if (b.type == CharBonesSamples::kQuat) {
                for (int i = 0; i < 4; ++i) *out++ = c ? float(r.s16()) * kQuatScale : r.f();
            } else {
                *out++ = c ? float(r.s16()) * kRotScale : r.f();
            }
        }
        for (size_t used = r.pos() - start; used < sampleSize; ++used) r.u8();
    }
}

// CharClip::Load RB 0x8231A480 (rev 9) [C]: GH2's rev 5 with the
// "relative" bool turned into an object name, a do_not_compress bool, a u32
// before the transitions, no enter / exit event strings, and each event's
// name before its frame.
void charClip(Rd& r, CharClipSamples& k) {
    k.clipRevision = r.version({9}, "CharClip");
    k.header = r.header();
    k.startFrame = r.f();
    k.endFrame = r.f();
    k.framesPerSec = r.f();
    k.flags = r.u32();
    k.playFlags = r.u32();
    k.blendWidth = r.f();
    k.range = r.f();
    k.relativeTo = r.str();
    k.relative = !k.relativeTo.empty();
    k.doNotCompress = r.b();
    r.u32();  // the transitions' size [C RB 0x8231A480; meaning I]
    k.transitions.resize(r.count(4));
    for (auto& t : k.transitions) {
        t.dest = r.str();
        t.nodes.resize(r.count(8));
        for (auto& n : t.nodes) n.first = r.f(), n.second = r.f();
    }
    k.events.resize(r.count(8));
    for (auto& e : k.events) {
        e.script = r.str();
        e.frame = r.f();
    }
}

// CharClipSamples::Load RB 0x8234F8E0 (rev 13) [C]: the clip, then the full,
// one and third CharBonesSamples, each complete (version, header, data);
// GH2 stored the three headers first.
void readClipSamples(Rd& r, CharClipSamples& k) {
    charClip(r, k);
    CharBonesSamples* sets[3] = {&k.full, &k.one, &k.third};
    for (auto* s : sets) {
        bonesHeader(r, *s);
        bonesData(r, *s);
    }
}

// CharDriver PreLoad RB 0x82314B48 + PostLoad RB 0x823154E8 (rev 7) [C]:
// the clips path, and when it is empty the clips' object name; the u32
// "apply" where GH2 had a bool; then the first clip.
void readCharDriverBody(Rd& r, CharDriver& d, bool readVersion = true) {
    if (readVersion) r.version({7}, "CharDriver");
    d.header = r.header();
    d.weightable = weightable(r);
    d.bones = r.str();
    d.clips = r.str();
    if (d.clips.empty()) d.clipsObject = r.str();
    d.realign = r.b();
    d.apply = r.u32();
    d.unknown55 = d.apply != 0;
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
    h.elbowSwing = r.f();  // RB rev 6 (RB 0x8232F9C0) [C order; name I]
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
        {"CharClipSamples", dec<CharClipSamples>(13, readClipSamples)},
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
        {"CharDriver", {7, [] { return std::make_unique<CharDriver>(); }, [](Rd& r, Object& o) {
             readCharDriverBody(r, static_cast<CharDriver&>(o), false);  // the dispatcher read the version
         }}},
        // CharDriverMidi Pre 0x82185590 (u32 3, CharDriver Pre), Post 0x82185600 (CharDriver Post, ObjPtr<CharClip>).
        {"CharDriverMidi", dec<CharDriverMidi>(3, [](Rd& r, CharDriverMidi& d) {
             readCharDriverBody(r, d);
             d.inactiveClip = r.str();  // RB PostLoad RB 0x823425A0: the MIDI clip [C]
         })},
        // CharIKHand::Load 0x82174470 (rev 5).
        {"CharIKHand", dec<CharIKHand>(6, readIKHand)},
        // CharIKMidi::Load 0x82184950 (rev 4).
        {"CharIKMidi", dec<CharIKMidi>(4, [](Rd& r, CharIKMidi& k) {
             k.header = r.header();
             k.bone = r.str();
         })},
        // CharHair::Load 0x8217FAF0 (rev 2).
        // CharHair::Load RB 0x8233BF30 (rev 9) [C]: eight parameters
        // (stiffness .. max_slack), strands (RB 0x8233A868: root, angle,
        // points RB 0x823372A0: pos, bone, length, radius, outer_radius,
        // side_length; two 3x3 matrices, a u32 hookup flag), simulate.
        {"CharHair", dec<CharHair>(9, [](Rd& r, CharHair& h) {
             h.header = r.header();
             r.fs(h.params);
             h.strands.resize(r.count(8));
             for (auto& s : h.strands) {
                 s.root = r.str();
                 s.angle = r.f();
                 s.points.resize(r.count(20));
                 for (auto& p : s.points) {
                     r.fs(p.pos);
                     p.bone = r.str();
                     p.length = r.f();
                     p.radius = r.f();
                     p.f60 = r.f();  // outer_radius
                     p.f64 = r.f();  // side_length
                 }
                 r.fs(s.baseMat);
                 r.fs(s.baseMat2);
                 s.hookupFlags = r.u32();
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
        // CharEyes::Load RB 0x8232CB58 (rev 7) [C]: weightable, eyes as
        // (eye, upper lid, lower lid), interests, the face servo.
        {"CharEyes", dec<CharEyes>(7, [](Rd& r, CharEyes& e) {
             e.header = r.header();
             e.weightable = weightable(r);
             const uint32_t n = r.count(12);
             for (uint32_t i = 0; i < n; ++i) {
                 e.eyes.push_back(r.str());
                 e.upperLids.push_back(r.str());
                 e.lowerLids.push_back(r.str());
             }
             e.interests.resize(r.count(8));
             for (auto& i : e.interests) {
                 i.first = r.str();
                 i.second = r.u32();
             }
             e.faceServo = r.str();
         })},
        // CharWeightSetter::Load 0x8218DFC0 (rev 3).
        // CharWeightSetter::Load RB 0x8234B6E0 (rev 5) [C]: offset, scale,
        // base_weight, beats_per_weight after the flags (GH2's invert bool is gone).
        {"CharWeightSetter", dec<CharWeightSetter>(5, [](Rd& r, CharWeightSetter& w) {
             w.header = r.header();
             w.weightable = weightable(r);
             w.driver = r.str();
             w.flags = r.u32();
             w.offset = r.f();
             w.scale = r.f();
             w.baseWeight = r.f();
             w.beatsPerWeight = r.f();
         })},
        // CharUpperTwist::Load 0x82182C00 (rev 1).
        {"CharUpperTwist", dec<CharUpperTwist>(1, [](Rd& r, CharUpperTwist& t) {
             t.header = r.header();
             t.upperArm = r.str();
             t.twist1 = r.str();
             t.twist2 = r.str();
         })},
        // CharForeTwist::Load 0x82182018 (rev 1).
        // CharForeTwist::Load RB 0x8233E670 (rev 2): + length_scale [C].
        {"CharForeTwist", dec<CharForeTwist>(2, [](Rd& r, CharForeTwist& t) {
             t.header = r.header();
             t.offset = r.f();
             t.hand = r.str();
             t.twist2 = r.str();
             t.lengthScale = r.f();
         })},
        // RB's small helpers [C: RB Load addresses in docs/formats.md].
        {"CharNeckTwist", dec<CharNeckTwist>(1, [](Rd& r, CharNeckTwist& t) {
             t.header = r.header();
             t.head = r.str();
             t.twist = r.str();
         })},
        {"CharTransCopy", dec<CharTransCopy>(1, [](Rd& r, CharTransCopy& t) {
             t.header = r.header();
             t.src = r.str();
             t.dest = r.str();
         })},
        {"CharIKScale", dec<CharIKScale>(1, [](Rd& r, CharIKScale& t) {
             t.header = r.header();
             t.weightable = weightable(r);
             t.dest = r.str();
             t.scale = r.f();
         })},
        {"CharGuitarString", dec<CharGuitarString>(0, [](Rd& r, CharGuitarString& t) {
             t.header = r.header();
             t.nut = r.str();
             t.bridge = r.str();
             t.bend = r.str();
             t.target = r.str();
         })},
        {"CharBonesBlender", dec<CharBonesBlender>(1, [](Rd& r, CharBonesBlender& t) {
             t.header = r.header();
             t.dest = r.str();
         })},
        {"CharPollGroup", dec<CharPollGroup>(1, [](Rd& r, CharPollGroup& t) {
             t.header = r.header();
             t.polls = r.strs();
         })},
        // CharCollide::Load RB 0x8232D9D0 (rev 3): trans, shape, radius, length, flags [C].
        {"CharCollide", dec<CharCollide>(3, [](Rd& r, CharCollide& c) {
             c.header = r.header();
             r.trans(c.trans);
             c.shape = r.u32();
             c.radius = r.f();
             c.length = r.f();
             c.flags = r.u32();
         })},
        // CharCuff::Load RB 0x82334660 (rev 7) [C].
        {"CharCuff", dec<CharCuff>(7, [](Rd& r, CharCuff& c) {
             c.header = r.header();
             r.trans(c.trans);
             for (int i = 0; i < 3; ++i) c.radius[size_t(i)] = r.f(), c.offset[size_t(i)] = r.f();
             c.outerRadius = r.f();
             c.openEnd = r.b();
             c.bone = r.str();
             c.eccentricity = r.f();
             c.category = r.str();
         })},
        // CharMeshHide::Load RB 0x8235CAE8 (rev 1) [C].
        {"CharMeshHide", dec<CharMeshHide>(1, [](Rd& r, CharMeshHide& c) {
             c.header = r.header();
             c.flags = r.u32();
             c.hides.resize(r.count(8));
             for (auto& h : c.hides) h.first = r.str(), h.second = r.u32();
         })},
        // OutfitConfig::Load RB 0x822F9DD0 (rev 3) [C]: colours (index,
        // materials: mat, primary / secondary palette, textures).
        {"OutfitConfig", dec<OutfitConfig>(3, [](Rd& r, OutfitConfig& c) {
             c.header = r.header();
             c.colors.resize(r.count(8));
             for (auto& col : c.colors) {
                 col.index = r.u32();
                 col.mats.resize(r.count(16));
                 for (auto& m : col.mats) {
                     m.mat = r.str();
                     m.primaryPalette = r.str();
                     m.secondaryPalette = r.str();
                     m.textures = r.strs();
                 }
             }
         })},
        // ColorPalette::Load RB 0x822F8038 (rev 1): RGBA colours [C].
        {"ColorPalette", dec<ColorPalette>(1, [](Rd& r, ColorPalette& c) {
             c.header = r.header();
             c.colors.resize(r.count(16));
             for (auto& col : c.colors) r.fs(col);
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
        // OutfitLoader PreLoad RB 0x8230F488 (rev 3): GH2's without the category name [C].
        {"OutfitLoader", dec<OutfitLoader>(3, [](Rd& r, OutfitLoader& o) {
             o.header = r.header();
             o.path = r.str();
             o.categories.resize(r.u16());
             for (auto& c : o.categories) {
                 c.current = r.u8();
                 c.next = r.u8();
                 c.items.resize(r.u16());
                 for (auto& i : c.items) i.first = r.u8(), i.second = r.u8();
             }
         })},
        // FaceFxLipSyncServo v5 (Load 0x8218E378, PreLoad 0x8218FAF0, PostLoad 0x82192470).
        {"FaceFxLipSyncServo", dec<FaceFxLipSyncServo>(7, [](Rd& r, FaceFxLipSyncServo& s) {
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
        {"Character", dec<CharacterObj>(12, [](Rd& r, CharacterObj& c) {
             r.version({10}, "RndDir");
             objectDirPre(r, c.objectDir);
             rndDirPost(r, c.objectDir, c.header, c.rnd);
         })},
        {"CompositeCharacter", dec<CharacterObj>(9, [](Rd& r, CharacterObj& c) {
             c.composite = true;
             r.version({12}, "Character");
             r.version({10}, "RndDir");
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
        milo_order::Scope order(self.bigEndian);
        Rd r(self.record, pos);
        // CharClipSet PreLoad RB 0x82352AE8 / PostLoad RB 0x82354500 (rev 17)
        // [C]: no blend width / play flags / default flags before the clips.
        r.version({17}, "CharClipSet");
        objectDirPre(r, s.objectDir);
        objectDirPost(r, s.objectDir, s.header);
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
    c->composite = self.cls == "CompositeCharacter";
    size_t pos = 0;
    milo_order::Scope order(self.bigEndian);
    c->bigEndian = self.bigEndian;
    Rd r(c->record, pos);
    characterPre(r, *c, c->composite);
    rndDirPost(r, c->objectDir, c->header, c->rnd);
    c->lods.resize(r.count(8));
    for (auto& l : c->lods) {
        l.screenSize = r.f();
        l.group = r.str();
    }
    c->shadow = r.str();
    c->selfShadow = r.b();
    c->sphereBase = r.str();
    // RB rev 12 (PostLoad RB 0x8230AE00): the bounding sphere [C].
    for (float& v : c->bounding) v = r.f();
    // CharacterTest::Load RB 0x823607C0 (rev 8), the editor's block [C].
    auto& t = c->test;
    r.version({8}, "CharacterTest");
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
    t.sym80 = r.str();
    t.f84 = r.f();
    if (c->composite) {  // CompositeCharacter PostLoad RB 0x822E6508's own fields [C order; names I]
        r.u32(), r.str(), r.u32(), r.str(), r.u32(), r.u32(), r.str(), r.str();
    }
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
