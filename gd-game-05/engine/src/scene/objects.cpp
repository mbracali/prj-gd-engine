// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/objects.h"

#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <stdexcept>

#include "char/char_objects.h"
#include "scene/synth_objects.h"
#include "scene/ui_objects.h"
#include "scene/world_objects.h"

namespace scene {

float DataNode::asFloat() const {
    if (type == 0) return float(int32_t(raw));
    float f;
    std::memcpy(&f, &raw, 4);
    return f;
}

Xfm compose(const Xfm& a, const Xfm& b) {
    Xfm r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0;
            for (int k = 0; k < 3; ++k) s += a.rot[i * 3 + k] * b.rot[k * 3 + j];
            r.rot[i * 3 + j] = s;
        }
    for (int j = 0; j < 3; ++j) {
        float s = b.pos[j];
        for (int k = 0; k < 3; ++k) s += a.pos[k] * b.rot[k * 3 + j];
        r.pos[j] = s;
    }
    return r;
}

namespace {

// BinStream reads in Rock Band's milo files, in the file's payload order
// (milo_order: big-endian in most RB files, little-endian in GH2's and in
// RB's older ones; docs/formats.md "Milo").
class In {
public:
    In(const uint8_t* d, size_t n) : r_(d, n) {}
    In(const Bytes& d, size_t pos) : r_(d) { r_.seek(pos); }
    size_t pos() const { return r_.pos(); }
    void seek(size_t p) { r_.seek(p); }
    ByteReader& raw() { return r_; }
    size_t remaining() const { return r_.remaining(); }

    uint32_t u32() { return r_.u32m(); }
    int32_t i32() { return int32_t(r_.u32m()); }
    uint16_t u16() { return r_.u16m(); }
    uint8_t u8() { return r_.u8(); }
    bool b() { return r_.u8() != 0; }
    float f() { return r_.f32m(); }
    // u32 length + bytes, no terminator (BinStream::ReadString @ 0x823595e0).
    std::string str() {
        uint32_t n = r_.u32m();
        if (n > r_.remaining()) throw std::runtime_error("string runs past the record");
        return r_.str(n);
    }
    template <size_t N> void fs(std::array<float, N>& a) {
        for (float& v : a) v = f();
    }
    // Transform: 3 rows then the position (Vector3 x 4, @ 0x8213d668).
    void xfm(Xfm& x) {
        fs(x.rot);
        fs(x.pos);
    }
    std::vector<std::string> strList() {
        uint32_t n = u32();
        if (n > r_.remaining() / 4) throw std::runtime_error("implausible list size");
        std::vector<std::string> v(n);
        for (auto& s : v) s = str();
        return v;
    }
    template <size_t N> std::vector<Key<N>> keys() {
        uint32_t n = u32();
        if (n > r_.remaining() / ((N + 1) * 4)) throw std::runtime_error("implausible key count");
        std::vector<Key<N>> v(n);
        for (auto& k : v) {
            fs(k.v);
            k.frame = f();
        }
        return v;
    }

private:
    ByteReader r_;
};

[[noreturn]] void bad(const std::string& what) { throw std::runtime_error(what); }

void expectVersion(uint32_t got, uint32_t want, const char* cls) {
    if (got != want) bad(std::string(cls) + " version " + std::to_string(got) + " (decoder reads " + std::to_string(want) + ")");
}

// DataNode::Load @ 0x82318c28 / DataArray::Load @ 0x8231aa20.
DataNode readArray(In& in, int depth);
DataNode readNode(In& in, int depth) {
    DataNode n;
    n.type = int(in.u32());  // a u32 (GH2's little-endian u16 + u16 read the same; RB is big-endian)
    switch (n.type) {
        case 0x10:
        case 0x11:
        case 0x13: {
            DataNode a = readArray(in, depth + 1);
            a.type = n.type;
            return a;
        }
        case 0x12:  // string
        case 2:     // variable
        case 3:     // func
        case 4:     // object
        case 5:     // symbol
        case 7:     // ifdef
        case 0x20:
        case 0x21:
        case 0x22:
        case 0x23:
            n.str = in.str();
            return n;
        default:
            n.raw = in.u32();
            return n;
    }
}

DataNode readArray(In& in, int depth) {
    if (depth > 64) bad("property arrays nested too deep");
    DataNode a;
    a.type = 0x10;
    int count = int16_t(in.u16());
    a.line = int16_t(in.u16());
    in.u16();  // deprecated
    if (count < 0) bad("negative array size");
    for (int i = 0; i < count; ++i) a.children.push_back(readNode(in, depth));
    return a;
}

ObjectHeader readObjectHeader(In& in) {
    ObjectHeader h;
    h.revision = in.i32();
    h.type = in.str();
    h.hasProps = in.b();
    if (h.hasProps) h.props = readArray(in, 0);
    if (h.revision > 0) h.note = in.str();
    return h;
}

// RndTransformable::Load @ 0x821b0a28, revision 9.
void readTrans(In& in, Trans& t) {
    uint32_t v = in.u32();
    expectVersion(v, 9, "Trans");
    in.xfm(t.local);
    in.xfm(t.world);
    t.constraint = in.i32();
    t.target = in.str();
    t.preserveScale = in.b();
    t.parent = in.str();
}

// RndDrawable::Load @ 0x821b36c0, revision 3.
void readDraw(In& in, Draw& d) {
    uint32_t v = in.u32();
    expectVersion(v, 3, "Draw");
    d.showing = in.b();
    in.fs(d.sphere);
    d.order = in.f();
}

// RndAnimatable::Load @ 0x821e96c0, revision 4.
void readAnimatable(In& in, Animatable& a) {
    uint32_t v = in.u32();
    expectVersion(v, 4, "Anim");
    a.unknown = in.u32();
    a.rate = in.u32();
}

void readTex(In& in, Tex& t) {
    expectVersion(t.version, 10, "Tex");
    t.header = readObjectHeader(in);
    t.width = in.i32();
    t.height = in.i32();
    t.bpp = in.i32();
    t.file = in.str();
    t.mipMapK = in.f();
    t.type = in.i32();
    t.useExtPath = in.b();
    // RB: every Tex record carries its bitmap, use_ext_path or not [C data].
    t.bitmap = readBitmap(in.raw());
}

// RndMat::Load RB 0x821FDB20 (rev 41) [C]: GH2's v28 with alpha_threshold
// after alpha_cut, then after stencil_mode: fur, two_color, color2,
// two_color_mask, de_normal, anisotropy, fleck_shaded, norm_detail_tiling,
// norm_detail_strength, substrate_depth, substrate_color, norm_detail_map,
// fleck_shaded_mask (names: SyncProperty RB 0x821FBC10).
void readMat(In& in, Mat& m) {
    expectVersion(m.version, 41, "Mat");
    m.header = readObjectHeader(in);
    m.blend = in.i32();
    in.fs(m.color);
    m.useEnviron = in.b();
    m.prelit = in.b();
    m.zMode = in.i32();
    m.alphaCut = in.b();
    m.alphaThreshold = in.i32();
    m.alphaWrite = in.b();
    m.texGen = in.i32();
    m.texWrap = in.i32();
    in.xfm(m.texXfm);
    m.diffuseTex = in.str();
    m.nextPass = in.str();
    m.intensify = in.b();
    m.cull = in.b();
    m.emissiveMultiplier = in.f();
    in.fs(m.specularRgb);
    m.specularPower = in.f();
    m.normalMap = in.str();
    m.emissiveMap = in.str();
    m.specularMap = in.str();
    m.customDiffuseMap = in.str();
    m.environMap = in.str();
    m.perPixelLit = in.b();
    m.normalize = in.b();
    m.stencilMode = in.i32();
    m.fur = in.str();
    m.twoColor = in.b();
    in.fs(m.color2);
    m.twoColorMask = in.str();
    m.deNormal = in.f();
    m.anisotropy = in.f();
    m.fleckShaded = in.b();
    m.normDetailTiling = in.f();
    m.normDetailStrength = in.f();
    m.substrateDepth = in.f();
    in.fs(m.substrateColor);
    m.normDetailMap = in.str();
    m.fleckShadedMask = in.str();
}

// The mesh's BSP tree (@ 0x8226ab80): u8 present, plane (4 f32), front, back.
void skipBsp(In& in, int depth) {
    if (depth > 256) bad("BSP tree too deep");
    if (!in.b()) return;
    for (int i = 0; i < 4; ++i) in.f();
    skipBsp(in, depth + 1);
    skipBsp(in, depth + 1);
}

// RndMesh::Load RB 0x821A02F8 (rev 34) [C]: the vertices are the console's
// 80-byte in-memory vertex read raw (pos 4 f32, normal 4 f32, 4 bone
// weights, uv, 4 u16 bone indices, tangent 4 f32); the bones a counted list
// of (name, offset transform); a per-group block never present on RB's disc.
void readMesh(In& in, Mesh& m) {
    expectVersion(m.version, 34, "Mesh");
    m.header = readObjectHeader(in);
    readTrans(in, m.trans);
    readDraw(in, m.draw);
    m.mat = in.str();
    m.geomOwner = in.str();
    m.mutableParts = in.u32();
    m.volume = in.u32();
    skipBsp(in, 0);
    uint32_t nv = in.u32();
    if (nv > in.remaining() / 80) bad("implausible vertex count");
    m.verts.resize(nv);
    for (auto& v : m.verts) {
        for (float& x : v.pos) x = in.f();
        in.f();
        for (float& x : v.normal) x = in.f();
        in.f();
        for (float& x : v.color) x = in.f();  // RB: the bone weights (no vertex colour) [C]
        for (float& x : v.uv) x = in.f();
        for (uint16_t& b : v.bones) b = in.u16();
        for (float& x : v.tangent) x = in.f();
    }
    m.rbVertices = true;
    uint32_t nf = in.u32();
    if (nf > in.remaining() / 6) bad("implausible face count");
    m.faces.resize(nf);
    for (auto& f : m.faces) f = {in.u16(), in.u16(), in.u16()};
    uint32_t ng = in.u32();  // @ 0x821bf6c0
    if (ng > in.remaining()) bad("implausible group count");
    m.groupSizes.resize(ng);
    for (auto& g : m.groupSizes) g = in.u8();
    // Bone 0; when set, bones 1..3 and the four offset transforms
    // (disassembly at 0x821c3278..0x821c33f4).
    const uint32_t nb = in.u32();
    if (nb > in.remaining() / 52) bad("implausible bone count");
    m.bones.resize(nb);
    for (auto& b : m.bones) {
        b.name = in.str();
        in.xfm(b.offset);
    }
    // A per-group vertex/index block follows only for meshes the loader keeps
    // in a platform-specific form (FUN_821bd000); GH2's data never has it.
}

// RndGroup::Load RB 0x821EB800 (rev 13) [C]: + draw_only after the environ.
void readGroup(In& in, Group& g) {
    expectVersion(g.version, 13, "Group");
    g.header = readObjectHeader(in);
    readAnimatable(in, g.anim);
    readTrans(in, g.trans);
    readDraw(in, g.draw);
    g.objects = in.strList();
    g.environment = in.str();
    g.drawOnly = in.str();
    g.lod = in.str();
    g.lodScreenSize = in.f();
}

void readCam(In& in, Cam& c) {
    expectVersion(c.version, 12, "Cam");
    c.header = readObjectHeader(in);
    readTrans(in, c.trans);
    c.nearPlane = in.f();
    c.farPlane = in.f();
    c.yFov = in.f();
    in.fs(c.screenRect);
    in.fs(c.zRange);
    c.targetTex = in.str();
}

// RndEnviron::Load RB 0x821ADBB8 (rev 9) [C]: GH2's v5 plus fade_max,
// fade_ref, the four fog-plane distances, the ambient fog owner and the
// colour adjustment (use_color_adjust, ColorXfm RB 0x8221A410).
void readEnviron(In& in, Environ& e) {
    expectVersion(e.version, 9, "Environ");
    e.header = readObjectHeader(in);
    e.lights = in.strList();
    in.fs(e.ambientColor);
    e.fogStart = in.f();
    e.fogEnd = in.f();
    in.fs(e.fogColor);
    e.fogEnable = in.b();
    e.animateFromPreset = in.b();
    e.fadeOut = in.b();
    e.fadeStart = in.f();
    e.fadeEnd = in.f();
    e.fadeMax = in.f();
    e.fadeRef = in.str();
    in.fs(e.fogPlanes);
    e.ambientFogOwner = in.str();
    e.useColorAdjust = in.b();
    expectVersion(in.u32(), 0, "ColorXfm");
    in.xfm(e.colorXfm.xfm);
    e.colorXfm.hue = in.f();
    e.colorXfm.saturation = in.f();
    e.colorXfm.lightness = in.f();
    e.colorXfm.contrast = in.f();
    e.colorXfm.brightness = in.f();
    in.fs(e.colorXfm.inLo);
    in.fs(e.colorXfm.inHi);
    in.fs(e.colorXfm.outLo);
    in.fs(e.colorXfm.outHi);
}

// RndLight::Load RB 0x821AACC8 (rev 12) [C]: colour, range, type,
// falloff_start (the object's +0xD0 range, +0xD8 type, +0xD4 falloff_start:
// the light-kind switches test +0xD8, 0x821A8900 uses +0xD0 as the range;
// every RB light's second field is 0..4 [C data]), the preset flags, the
// cone, the texture, the colour owner.
void readLight(In& in, Light& l) {
    expectVersion(l.version, 12, "Light");
    l.header = readObjectHeader(in);
    readTrans(in, l.trans);
    in.fs(l.color);
    l.range = in.f();
    l.type = in.i32();
    l.falloffStart = in.f();
    l.animateColorFromPreset = in.b();
    l.animatePositionFromPreset = in.b();
    l.topRadius = in.f();
    l.botRadius = in.f();
    l.softness = in.f();
    l.displacement = in.f();
    l.texture = in.str();
    l.colorOwner = in.str();
}

void readTransObj(In& in, TransObj& t) {
    // The record's version is Trans's own; the object header follows it
    // (Trans::Load reads it only when the object's class is Trans).
    expectVersion(t.version, 9, "Trans");
    t.header = readObjectHeader(in);
    in.xfm(t.trans.local);
    in.xfm(t.trans.world);
    t.trans.constraint = in.i32();
    t.trans.target = in.str();
    t.trans.preserveScale = in.b();
    t.trans.parent = in.str();
}

void readTransAnim(In& in, TransAnim& a) {
    expectVersion(a.version, 6, "TransAnim");
    a.header = readObjectHeader(in);
    readAnimatable(in, a.anim);
    a.trans = in.str();
    a.rotKeys = in.keys<4>();
    a.transKeys = in.keys<3>();
    a.keysOwner = in.str();
    a.transSpline = in.b();
    a.repeatTrans = in.b();
    a.scaleKeys = in.keys<3>();
    a.scaleSpline = in.b();
    a.followPath = in.b();
    a.rotSlerp = in.b();
}

void readMatAnim(In& in, MatAnim& a) {
    expectVersion(a.version, 7, "MatAnim");
    a.header = readObjectHeader(in);
    readAnimatable(in, a.anim);
    a.mat = in.str();
    a.keysOwner = in.str();
    a.colorKeys = in.keys<4>();
    a.alphaKeys = in.keys<1>();
    a.transKeys = in.keys<3>();
    a.scaleKeys = in.keys<3>();
    a.rotKeys = in.keys<3>();
    uint32_t n = in.u32();
    if (n > in.remaining() / 8) bad("implausible key count");
    a.texKeys.resize(n);
    for (auto& k : a.texKeys) {
        k.tex = in.str();
        k.frame = in.f();
    }
}

// RndAnimFilter::Load RB 0x821F1DC8 (rev 2) [C]: + snap, jitter.
void readAnimFilter(In& in, AnimFilter& a) {
    expectVersion(a.version, 2, "AnimFilter");
    a.header = readObjectHeader(in);
    readAnimatable(in, a.animatable);
    a.anim = in.str();
    a.scale = in.f();
    a.offset = in.f();
    a.start = in.f();
    a.end = in.f();
    a.type = in.i32();
    a.period = in.f();
    a.snap = in.f();
    a.jitter = in.f();
}

// RndParticleSys::Load RB 0x821D7870 (rev 36) [C]: GH2's v30 plus
// screen_aspect, max_burst, time_between, peak_rate, duration, the start /
// end offsets, the velocity-alignment flags, stretch_scale, perspective,
// subsamples and pause_offscreen; shrink_ratio before grow_ratio.
void readParticleSys(In& in, ParticleSys& p) {
    expectVersion(p.version, 36, "ParticleSys");
    p.header = readObjectHeader(in);
    readObjectHeader(in);  // read a second time (RB 0x821D7870)
    readAnimatable(in, p.anim);
    readTrans(in, p.trans);
    readDraw(in, p.draw);
    in.fs(p.life);
    p.screenAspect = in.f();
    in.fs(p.posLow);
    in.fs(p.posHigh);
    in.fs(p.speed);
    in.fs(p.pitch);
    in.fs(p.yaw);
    in.fs(p.emitRate);
    p.maxBurst = in.i32();
    in.fs(p.timeBetween);
    in.fs(p.peakRate);
    in.fs(p.duration);
    in.fs(p.startSize);
    in.fs(p.deltaSize);
    in.fs(p.startColorLow);
    in.fs(p.startColorHigh);
    in.fs(p.endColorLow);
    in.fs(p.endColorHigh);
    p.bounce = in.str();
    in.fs(p.force);
    p.mat = in.str();
    p.type = in.i32();
    p.shrinkRatio = in.f();
    p.growRatio = in.f();
    p.midColorRatio = in.f();
    in.fs(p.midColorLow);
    in.fs(p.midColorHigh);
    p.maxParticles = in.i32();
    in.fs(p.bubblePeriod);
    in.fs(p.bubbleSize);
    p.bubble = in.b();
    p.spin = in.b();
    in.fs(p.spinSpeed);
    p.spinDamping = in.f();
    p.drag = in.f();
    in.fs(p.startOffset);
    in.fs(p.endOffset);
    p.velocityAlign = in.b();
    p.stretchWithVelocity = in.b();
    p.constantArea = in.b();
    p.stretchScale = in.f();
    p.perspective = in.b();
    p.relativeMotion = in.f();
    p.relativeParent = in.str();
    p.mesh = in.str();
    p.subsamples = in.i32();
    p.frameDrive = in.b();
    p.pauseOffscreen = in.b();
    p.preSpawn = in.b();
    if (in.b()) {  // preserved particles: pos (4), colour (4), size
        uint32_t n = in.u32();
        if (n > in.remaining() / 36) bad("implausible particle count");
        p.preserved.resize(n);
        for (auto& q : p.preserved) {
            in.fs(q.pos);
            in.fs(q.color);
            q.size = in.f();
        }
    }
}

void readParticleSysAnim(In& in, ParticleSysAnim& a) {
    expectVersion(a.version, 3, "ParticleSysAnim");
    a.header = readObjectHeader(in);
    readAnimatable(in, a.anim);
    a.partSys = in.str();
    a.startColorKeys = in.keys<4>();
    a.endColorKeys = in.keys<4>();
    a.emitRateKeys = in.keys<2>();
    a.keysOwner = in.str();
    a.speedKeys = in.keys<2>();
    a.lifeKeys = in.keys<2>();
    a.startSizeKeys = in.keys<2>();
}

template <size_t N> void readVertKeys(In& in, std::vector<MeshAnim::VertKey<std::array<float, N>>>& keys) {
    uint32_t n = in.u32();
    if (n > in.remaining() / 8) bad("implausible key count");
    keys.resize(n);
    for (auto& k : keys) {
        uint32_t m = in.u32();
        if (m > in.remaining() / (N * 4)) bad("implausible vertex count");
        k.values.resize(m);
        for (auto& v : k.values) in.fs(v);
        k.frame = in.f();
    }
}

void readMeshAnim(In& in, MeshAnim& a) {
    expectVersion(a.version, 1, "MeshAnim");
    a.header = readObjectHeader(in);
    readAnimatable(in, a.anim);
    a.mesh = in.str();
    readVertKeys<3>(in, a.pointKeys);
    readVertKeys<2>(in, a.uvKeys);
    readVertKeys<4>(in, a.colorKeys);
    a.keysOwner = in.str();
}

void readFont(In& in, Font& f) {
    // RndFont::Load RB 0x821A4A80 (rev 16) [C]: the same fields as GH2's v15
    // (the two bools keep GH2's order: monospace, then the other [C data: a
    // proportional font such as fakir_regular reads monospace FALSE]).
    expectVersion(f.version, 16, "Font");
    f.header = readObjectHeader(in);
    f.mat = in.str();
    in.fs(f.cellSize);
    f.deprecatedSize = in.f();
    f.spacing = in.f();
    f.chars = in.str();
    if (in.b()) {  // kerning table (@ 0x821d4a00): u32 count, (u32 pair, f32 amount)
        uint32_t n = in.u32();
        if (n > in.remaining() / 8) bad("implausible kerning count");
        f.kerning.resize(n);
        for (auto& k : f.kerning) {
            k.first = in.u32();
            k.second = in.f();
        }
    }
    f.textureOwner = in.str();
    f.monospace = in.b();
    f.unknown1074 = in.b();
    f.texWidth = in.u32();
    f.texHeight = in.u32();
    in.fs(f.cellUv);
    for (auto& g : f.glyphs) {
        g.x = in.f();
        g.y = in.f();
        g.width = in.f();
        g.advance = in.f();
    }
}

void readText(In& in, Text& t) {
    expectVersion(t.version, 17, "Text");
    t.header = readObjectHeader(in);
    readDraw(in, t.draw);
    readTrans(in, t.trans);
    t.font = in.str();
    t.align = in.u32();
    t.text = in.str();
    in.fs(t.color);
    t.wrapWidth = in.f();
    t.leading = in.f();
    t.fixedLength = in.u32();
    t.italics = in.f();
    t.size = in.f();
    t.markup = in.b();
    t.capsMode = in.u32();
}

void readTrackWidget(In& in, TrackWidget& w) {
    expectVersion(w.version, 4, "TrackWidget");
    w.header = readObjectHeader(in);
    readDraw(in, w.draw);
    w.meshes = in.strList();
    w.environment = in.str();
    w.baseLength = in.f();
    w.multiMesh = in.b();
    w.allowRotation = in.b();
}

struct Decoder {
    std::function<std::unique_ptr<Object>()> make;
    std::function<void(In&, Object&)> read;
};

template <class T> Decoder dec(void (*fn)(In&, T&)) {
    return {[] { return std::unique_ptr<Object>(new T); }, [fn](In& in, Object& o) { fn(in, static_cast<T&>(o)); }};
}

const std::map<std::string, Decoder>& decoders() {
    static const std::map<std::string, Decoder> m = {
        {"Tex", dec<Tex>(readTex)},
        {"Mat", dec<Mat>(readMat)},
        {"Mesh", dec<Mesh>(readMesh)},
        {"Trans", dec<TransObj>(readTransObj)},
        {"Group", dec<Group>(readGroup)},
        {"Cam", dec<Cam>(readCam)},
        {"Environ", dec<Environ>(readEnviron)},
        {"Light", dec<Light>(readLight)},
        {"TransAnim", dec<TransAnim>(readTransAnim)},
        {"MatAnim", dec<MatAnim>(readMatAnim)},
        {"AnimFilter", dec<AnimFilter>(readAnimFilter)},
        {"TrackWidget", dec<TrackWidget>(readTrackWidget)},
        {"ParticleSys", dec<ParticleSys>(readParticleSys)},
        {"ParticleSysAnim", dec<ParticleSysAnim>(readParticleSysAnim)},
        {"MeshAnim", dec<MeshAnim>(readMeshAnim)},
        {"Font", dec<Font>(readFont)},
        {"Text", dec<Text>(readText)},
    };
    return m;
}

}  // namespace

// RndAnimFilter::Scale (0x822078D8) [C]: without a period the scale, negated
// when the range runs backwards (end < start: metacam's in_backward.filt
// plays meta_cam_right_out.tnm from 500 down to 0); with one, the range over
// the period.
float AnimFilter::scaleOf(float framesPerUnit) const {
    if (period == 0) return end < start ? -scale : scale;
    return (end - start) / (framesPerUnit * period);
}

// RndAnimFilter::SetFrame (0x822079B0) [C]: scale x frame + offset (+ the
// range's length when it runs backwards), then kept in the range (0), looped
// (1) or shuttled (2 [I: the later Harmonix code's]).
float AnimFilter::filter(float frame, float framesPerUnit) const {
    const float base = offset + (end < start ? start - end : 0.0f);
    float f = scaleOf(framesPerUnit) * frame + base;
    const float lo = std::min(start, end), hi = std::max(start, end);
    const float len = hi - lo;
    switch (type) {
        case 1:  // loop
            if (len > 0) f = lo + std::fmod(std::fmod(f - lo, len) + len, len);
            break;
        case 2:  // shuttle
            if (len > 0) {
                float p = std::fmod(std::fmod(f - lo, 2 * len) + 2 * len, 2 * len);
                f = lo + (p <= len ? p : 2 * len - p);
            }
            break;
        default:  // range
            f = std::max(lo, std::min(hi, f));
            break;
    }
    return f;
}

// RndAnimFilter::EndFrame (0x82207B80) [C].
float AnimFilter::endFrame(float framesPerUnit) const {
    float sc = scaleOf(framesPerUnit);
    if (sc == 0) sc = 1;
    const float f = (end - (offset + (end < start ? start - end : 0.0f))) / sc;
    return type == 2 ? f * 2 : f;
}

namespace {

// ---- The ObjectDir family ------------------------------------------------

// RB: ObjectDir PreLoad RB 0x8247FB78 (rev 19), RndDir RB 0x821B1B28 /
// 0x821B1278 (rev 10), PanelDir RB 0x82260A90 / 0x82260B08 (rev 4), TrackDir
// RB 0x82250E88 / 0x82252610 (rev 5), WorldDir RB 0x8228C468 / 0x8228B6C8
// (rev 17) [C].
constexpr uint32_t kObjectDirRev = 19, kRndDirRev = 10, kPanelDirRev = 4, kTrackDirRev = 5, kGHTrackDirRev = 3, kWorldDirRev = 17;

void objectDirPre(In& in, DirObj& d) {
    expectVersion(in.u32(), kObjectDirRev, "ObjectDir");
    auto& o = d.objectDir;
    uint32_t n = in.u32();  // viewports: a transform each (RB: no u32 after it)
    if (n > in.remaining() / 48) bad("implausible viewport count");
    o.viewports.resize(n);
    for (auto& v : o.viewports) in.xfm(v);
    o.currentViewport = in.u32();
    o.proxyFile = in.str();
    o.subdirs = in.strList();
    o.inlineProxy = 0;  // RB: an inline directory follows the record's end marker (scene/milo.cpp)
}

void objectDirPost(In& in, DirObj& d) {
    d.objectDir.postRef0 = in.str();
    d.objectDir.postRef1 = in.str();
    d.header = readObjectHeader(in);
}

void rndDirPost(In& in, DirObj& d) {
    objectDirPost(in, d);
    readAnimatable(in, d.rnd.anim);
    readDraw(in, d.rnd.draw);
    readTrans(in, d.rnd.trans);
    d.rnd.environment = in.str();
    d.rnd.testEvent = in.str();
    d.hasRnd = true;
}

void panelDirPost(In& in, DirObj& d) {
    rndDirPost(in, d);
    d.hasPanel = true;
    if (d.own) d.panel.cam = in.str();  // the cam only in a file's own record [C data]
    d.panel.canEndWorld = in.b();
}

// WorldDir PostLoad (0x8224de60): PanelDir's, then (own record) the default
// camera, the sub-directory hides and bitmaps (pairs of names).
void worldDirPost(In& in, DirObj& d) {
    panelDirPost(in, d);
    if (!d.own) return;
    auto& w = d.world;
    in.xfm(w.camXfm);
    w.subdirHides = in.strList();
    uint32_t n = in.u32();
    if (n > in.remaining() / 8) bad("implausible bitmap count");
    w.subdirBitmaps.resize(n);
    for (auto& b : w.subdirBitmaps) b.first = in.str(), b.second = in.str();
    // RB rev 17: material and preset overrides (name pairs), the camshot
    // list and two more name lists [C order; names I].
    for (auto* pairs : {&w.matOverrides, &w.presetOverrides}) {
        uint32_t k = in.u32();
        if (k > in.remaining() / 8) bad("implausible override count");
        pairs->resize(k);
        for (auto& b : *pairs) b.first = in.str(), b.second = in.str();
    }
    w.camshots = in.strList();
    w.list160 = in.strList();
    w.list174 = in.strList();
}

void trackDirPost(In& in, DirObj& d) {
    panelDirPost(in, d);
    d.hasTrack = true;
    if (!d.own) return;
    auto& t = d.track;
    t.drawGroup = in.str();
    t.animGroup = in.str();
    t.yPerSecond = in.f();
    t.topY = in.f();
    t.bottomY = in.f();
    uint32_t n = in.u32();  // slots (@ 0x823651e0): two f32 each (@ 0x82363200)
    if (n > in.remaining() / 8) bad("implausible slot count");
    t.slots.resize(n);
    for (auto& sl : t.slots) in.fs(sl);
    t.warnOnResort = in.b();
    t.legacyWidget = in.str();
    t.legacyValue = in.u32();
}

// The Band* HUD directories' own fields (see HudDirData); false if `cls` is not one.
bool hudPre(In& in, DirObj& d, bool own) {
    auto& h = d.hud;
    auto ref = [&] { h.refs.push_back(in.str()); };
    auto list = [&] {
        auto v = in.strList();
        h.values.push_back(uint32_t(v.size()));
        h.refs.insert(h.refs.end(), v.begin(), v.end());
    };
    auto u32 = [&] { h.values.push_back(in.u32()); };
    auto u8 = [&] { h.values.push_back(in.u8()); };
    const std::string& cls = d.cls;
    if (cls == "BandCrowdMeterDir") {
        // RB's BandCrowdMeter (PreLoad RB 0x822ED498, rev 1) [C]: the slot
        // anims (a name list, own record only), f32 peak_value, then RndDir.
        expectVersion(d.version, 1, cls.c_str());
        if (own) list();
        u32();
    } else if (cls == "BandScoreDisplay") {
        expectVersion(d.version, 3, cls.c_str());
        if (own) {
            list();
            list();
            ref();
        }
        u32();
        u32();
    } else if (cls == "BandStarMeterDir") {
        expectVersion(d.version, 5, cls.c_str());
        if (own)
            for (int i = 0; i < 6; ++i) ref();
        u32();
        u32();
        u8();
        u8();
    } else if (cls == "BandStreakDisplay") {
        expectVersion(d.version, 4, cls.c_str());
        if (own) {
            for (int i = 0; i < 4; ++i) ref();
            list();
            list();
            ref();
            list();
            for (int i = 0; i < 4; ++i) ref();
            for (int i = 0; i < 3; ++i) ref();
            u32();
        }
        u32();
        u8();
        u32();
        u32();
    } else if (cls == "BandLeadMeter") {
        expectVersion(d.version, 8, cls.c_str());
        u32();
        if (own)
            for (int i = 0; i < 10; ++i) ref();
    } else {
        return false;
    }
    d.hasHud = true;
    return true;
}

bool isHudDirClass(const std::string& cls) {
    return cls == "BandCrowdMeterDir" || cls == "BandScoreDisplay" || cls == "BandStarMeterDir" ||
           cls == "BandStreakDisplay" || cls == "BandLeadMeter";
}

}  // namespace

bool isDirClass(const std::string& cls) {
    return cls == "ObjectDir" || cls == "RndDir" || cls == "PanelDir" || cls == "GHTrackDir" || cls == "TrackDir" || cls == "WorldDir" ||
           cls == "WorldInstance" || isHudDirClass(cls);
}

std::unique_ptr<DirObj> decodeDir(const std::string& cls, const std::string& name, const Bytes& stream, size_t start,
                                  bool own, const std::function<size_t(size_t)>& inlineDir, size_t& end) {
    auto d = std::make_unique<DirObj>();
    d->cls = cls;
    d->name = name;
    d->own = own;
    d->typed = true;
    In in(stream, start);
    d->version = in.u32();
    // PreLoad chain, most derived class first.
    if (cls == "GHTrackDir") {
        expectVersion(d->version, kGHTrackDirRev, "GHTrackDir");
        auto& g = d->ghTrack;
        g.unknown = in.i32();
        if (own) {
            g.spCamera = in.str();
            g.mpCameras = in.strList();
            g.gemWidgets = in.strList();
            g.hopoWidgets = in.strList();
            g.starWidgets = in.strList();
            g.starHopoWidgets = in.strList();
        }
        d->hasGHTrack = true;
        expectVersion(in.u32(), kTrackDirRev, "TrackDir");
    }
    if (cls == "TrackDir") expectVersion(d->version, kTrackDirRev, "TrackDir");  // RB's track: TrackDir itself
    // RB's WorldInstance v0 (PreLoad RB 0x822CC990, PostLoad RB 0x822CDF98)
    // [C]: the shared directory's file, then a whole RndDir record (the
    // instance's transform, draw and environ). Drawn as a proxy of that file.
    std::string sharedFile;
    if (cls == "WorldInstance") {
        expectVersion(d->version, 0, "WorldInstance");
        sharedFile = in.str();
    }
    if (cls == "WorldDir") {
        // WorldDir PreLoad (0x8224e5c0): its fields, then PanelDir's chain.
        expectVersion(d->version, kWorldDirRev, "WorldDir");
        d->world.unknown0 = in.u32();
        d->world.unknown1 = in.u32();
        d->world.hudFile = in.str();
        d->hasWorld = true;
    }
    if (isHudDirClass(cls)) hudPre(in, *d, own);
    if (cls == "GHTrackDir" || cls == "TrackDir" || cls == "PanelDir" || cls == "WorldDir") {
        if (cls == "PanelDir") expectVersion(d->version, kPanelDirRev, "PanelDir");
        else expectVersion(in.u32(), kPanelDirRev, "PanelDir");
    }
    if (cls != "ObjectDir") {
        if (cls == "RndDir") expectVersion(d->version, kRndDirRev, "RndDir");  // HUD dirs: RndDir follows their fields
        else expectVersion(in.u32(), kRndDirRev, "RndDir");
        objectDirPre(in, *d);
    } else {
        in.seek(start);
        objectDirPre(in, *d);
    }
    // The proxied directory, stored inline (ObjectDir PreLoad, 0x82322a38).
    const auto& od = d->objectDir;
    if (!own && (od.inlineProxy == 1 || od.inlineProxy == 2) && !od.proxyFile.empty()) in.seek(inlineDir(in.pos()));
    // PostLoad chain, base class first.
    if (cls == "GHTrackDir" || cls == "TrackDir") trackDirPost(in, *d);
    else if (cls == "PanelDir") panelDirPost(in, *d);
    else if (cls == "WorldDir") worldDirPost(in, *d);
    else if (cls == "RndDir" || cls == "WorldInstance" || isHudDirClass(cls)) rndDirPost(in, *d);
    else objectDirPost(in, *d);
    end = in.pos();
    if (in.remaining() < 4 || stream[end] != 0xAD || stream[end + 1] != 0xDE || stream[end + 2] != 0xAD ||
        stream[end + 3] != 0xDE)
        bad("directory record does not end at its end marker");
    if (cls == "WorldInstance" && d->objectDir.proxyFile.empty()) d->objectDir.proxyFile = sharedFile;
    d->decoded = true;
    return d;
}

ObjectHeader readObjectHeaderAt(const Bytes& record, size_t& pos) {
    In in(record, pos);
    ObjectHeader h = readObjectHeader(in);
    pos = in.pos();
    return h;
}

void readTransAt(const Bytes& record, size_t& pos, Trans& t) {
    In in(record, pos);
    readTrans(in, t);
    pos = in.pos();
}

void readDrawAt(const Bytes& record, size_t& pos, Draw& d) {
    In in(record, pos);
    readDraw(in, d);
    pos = in.pos();
}

bool isTypedClass(const std::string& cls) {
    return decoders().count(cls) != 0 || isDirClass(cls) || isUiClass(cls) || isWorldClass(cls) || isSynthClass(cls) || isCharClass(cls);
}

std::unique_ptr<Object> decodeObject(const std::string& cls, const std::string& name, const uint8_t* data, size_t size) {
    const auto& m = decoders();
    auto it = m.find(cls);
    const bool ui = it == m.end() && isUiClass(cls);  // scene/ui_objects.h
    const bool world = it == m.end() && !ui && isWorldClass(cls);  // scene/world_objects.h
    const bool synth = it == m.end() && !ui && !world && isSynthClass(cls);  // scene/synth_objects.h
    const bool chr = it == m.end() && !ui && !world && !synth && isCharClass(cls);  // char/char_objects.h
    std::unique_ptr<Object> o = it != m.end() ? it->second.make()
                                : ui          ? makeUiObject(cls)
                                : world       ? makeWorldObject(cls)
                                : synth       ? makeSynthObject(cls)
                                : chr         ? makeCharObject(cls)
                                              : std::make_unique<Object>();
    o->cls = cls;
    o->name = name;
    o->record.assign(data, data + size);
    o->typed = it != m.end() || ui || world || synth || chr;
    if (size >= 4) o->version = miloU32(data);
    if (!o->typed) return o;
    try {
        size_t left = 0;
        if (ui || world || synth || chr) {
            size_t pos = 4;
            if (size < 4) bad("record too short");
            if (ui) readUiObject(*o, o->record, pos);
            else if (world) readWorldObject(*o, o->record, pos);
            else if (synth) readSynthObject(*o, o->record, pos);
            else readCharObject(*o, o->record, pos);
            left = size - pos;
        } else {
            In in(o->record.data(), o->record.size());
            in.u32();
            it->second.read(in, *o);
            left = in.remaining();
        }
        if (left != 0) bad(std::to_string(left) + " bytes left before the end marker");
        o->decoded = true;
    } catch (const std::exception& e) {
        o->error = e.what();
    }
    return o;
}

}  // namespace scene
