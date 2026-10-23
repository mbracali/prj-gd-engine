#include "scene/scene.h"

#include "data/game_data.h"
#include "data/inflate.h"

#include <SDL3/SDL.h>

#include <cstring>
#include <stdexcept>
#include <type_traits>

namespace {

constexpr uint32_t kCompressedMagic = 0xCBBEDEAF;
constexpr uint8_t kEndMarker[4] = {0xAD, 0xDE, 0xAD, 0xDE};

// Chunked raw-deflate wrapper (ChunkStream).
Bytes decompress(const Bytes& file) {
    ByteReader r(file);
    if (file.size() < 16 || r.u32le() != kCompressedMagic) return file;
    uint32_t header = r.u32le(), blocks = r.u32le();
    r.u32le();
    std::vector<uint32_t> sizes(blocks);
    for (auto& s : sizes) s = r.u32le();
    Bytes out;
    size_t pos = header;
    for (uint32_t s : sizes) {
        if (pos + s > file.size()) throw std::runtime_error("truncated compressed scene");
        Bytes chunk = inflateRaw(file.data() + pos, s);
        out.insert(out.end(), chunk.begin(), chunk.end());
        pos += s;
    }
    return out;
}

const uint8_t* findMarker(const uint8_t* p, size_t n) {
    for (size_t i = 0; i + 4 <= n; ++i)
        if (p[i] == 0xAD && std::memcmp(p + i, kEndMarker, 4) == 0) return p + i;
    return nullptr;
}

class Rec {
public:
    Rec(const uint8_t* d, size_t n) : r_(d, n) {}
    uint32_t u32() { return r_.u32le(); }
    float f32() { return r_.f32le(); }
    bool b8() { return r_.u8() != 0; }
    std::string str() { return r_.str(r_.u32le()); }
    std::vector<std::string> refs() {
        std::vector<std::string> v(u32());
        for (auto& s : v) s = str();
        return v;
    }
    template <size_t N> std::array<float, N> floats() {
        std::array<float, N> a;
        for (auto& f : a) f = f32();
        return a;
    }
    Xfm xfm() {
        Xfm x;
        for (auto& f : x.rot) f = f32();
        for (auto& f : x.pos) f = f32();
        return x;
    }
    size_t remaining() const { return r_.remaining(); }
    ByteReader& raw() { return r_; }

private:
    ByteReader r_;
};

void expectVersion(Rec& r, uint32_t v, const char* cls) {
    uint32_t got = r.u32();
    if (got != v) throw std::runtime_error(std::string(cls) + " version " + std::to_string(got));
}

// RndTransformable_t1_v8 @ 0x2eadc8 (data version 8).
Transformable readTransformable(Rec& r) {
    Transformable t;
    expectVersion(r, 8, "Transformable");
    t.local = r.xfm();
    t.world = r.xfm();
    t.children = r.refs();
    t.constraint = static_cast<int>(r.u32());
    r.str();        // target
    t.preserveScale = r.b8();
    t.parent = r.str();
    return t;
}

// RndDrawable_t1_v8 @ 0x2e89c8 (data version 1).
Drawable readDrawable(Rec& r) {
    Drawable d;
    expectVersion(r, 1, "Drawable");
    d.showing = r.b8();
    d.draws = r.refs();
    d.sphere = r.floats<4>();
    return d;
}

// RndAnimatable_t1_v8 @ 0x1ac0c0: filters (u32 type + fields), then anims.
Animatable readAnimatable(Rec& r) {
    Animatable a;
    r.u32();
    for (uint32_t n = r.u32(); n; --n) {
        AnimFilter f;
        f.type = static_cast<int>(r.u32());
        if (f.type > 4) throw std::runtime_error("unknown animation filter");
        f.a = r.f32();
        f.b = r.f32();
        if (f.type == 4) f.c = r.f32();
        if (f.type == 1) f.loop = r.b8();
        a.filters.push_back(f);
    }
    a.anims = r.refs();
    return a;
}

// Key lists: u32 count, then per key the values and an f32 frame.
template <size_t N> std::vector<AnimKey<N>> readKeys(Rec& r) {
    std::vector<AnimKey<N>> keys(r.u32());
    for (auto& k : keys) {
        k.v = r.floats<N>();
        k.frame = r.f32();
    }
    return keys;
}

// RndBitmap load (FUN_001b0f10), returning the top level as RGBA.
Image readBitmap(Rec& r) {
    ByteReader& br = r.raw();
    size_t consumed = 0;
    const uint8_t* p = br.ptr();
    uint8_t bpp = p[1];
    uint16_t w = p[7] | p[8] << 8, h = p[9] | p[10] << 8;
    Image img;
    if (w == 0 || h == 0 || bpp == 32) {  // empty render-target bitmaps
        br.skip(32);
        return img;
    }
    img = decodeBitmap(p, br.remaining(), &consumed);
    br.skip(consumed);
    return img;
}

std::unique_ptr<TexObj> decodeTex(Rec& r) {
    auto t = std::make_unique<TexObj>();
    expectVersion(r, 8, "Tex");
    r.u32(), r.u32(), r.u32();  // width, height, bpp
    t->file = r.str();
    r.f32();                    // mipMapK
    t->type = static_cast<int>(r.u32());
    r.b8();
    if (r.remaining()) t->image = readBitmap(r);
    return t;
}

std::unique_ptr<MatObj> decodeMat(Rec& r) {
    auto m = std::make_unique<MatObj>();
    expectVersion(r, 21, "Mat");
    for (uint32_t n = r.u32(); n; --n) {
        MatStage s;
        s.blend = static_cast<int>(r.u32());
        s.texgen = static_cast<int>(r.u32());
        s.xfm = r.floats<12>();
        s.wrap = static_cast<int>(r.u32());
        s.tex = r.str();
        m->stages.push_back(std::move(s));
    }
    m->blend = static_cast<int>(r.u32());
    m->color = r.floats<4>();
    m->useEnviron = r.b8();
    m->vertAmb = r.b8();
    m->vertDyn = r.b8();
    m->cull = r.b8();
    m->multipass = static_cast<int>(r.u32());
    r.b8();          // normalize
    m->zmode = static_cast<int>(r.u32());
    m->alphaCutout = r.b8();
    m->alphaWrite = r.b8();
    return m;
}

// Recursive BSP node (FUN_00244af8).
void skipBsp(Rec& r) {
    if (!r.b8()) return;
    r.floats<4>();
    skipBsp(r);
    skipBsp(r);
}

std::unique_ptr<MeshObj> decodeMesh(Rec& r) {
    auto m = std::make_unique<MeshObj>();
    expectVersion(r, 25, "Mesh");
    m->trans = readTransformable(r);
    m->draw = readDrawable(r);
    m->mat = r.str();
    m->geomOwner = r.str();
    r.u32(), r.u32();  // mutable, volume
    skipBsp(r);
    m->verts.resize(r.u32());
    for (MeshVertex& v : m->verts) {
        for (float& f : v.pos) f = r.f32();
        for (float& f : v.normal) f = r.f32();
        for (float& f : v.extra) f = r.f32();
        for (float& f : v.uv) f = r.f32();
    }
    m->faces.resize(r.u32());
    for (auto& f : m->faces)
        for (auto& i : f) i = r.raw().u16le();
    uint32_t groups = r.u32();
    r.raw().skip(groups);
    std::string bone0 = r.str();
    if (!bone0.empty()) {
        std::string names[4] = {bone0, r.str(), r.str(), r.str()};
        for (const auto& n : names) m->bones.push_back({n, {}});
        for (auto& b : m->bones) b.offset = r.xfm();
    }
    // Precomputed strip data per group (present when the mesh owns geometry).
    if (r.remaining()) {
        for (uint32_t g = 0; g < groups; ++g) {
            uint32_t n = r.u32(), k = r.u32();
            r.raw().skip(size_t(n) * 4 + size_t(k) * 2);
        }
    }
    return m;
}

std::unique_ptr<ViewObj> decodeView(Rec& r) {
    auto v = std::make_unique<ViewObj>();
    expectVersion(r, 7, "View");
    v->anim = readAnimatable(r);
    v->trans = readTransformable(r);
    v->draw = readDrawable(r);
    v->env = r.str();
    r.f32(), r.f32();  // LOD sizes
    return v;
}

std::unique_ptr<CamObj> decodeCam(Rec& r) {
    auto c = std::make_unique<CamObj>();
    expectVersion(r, 9, "Cam");
    c->trans = readTransformable(r);
    readDrawable(r);
    c->nearPlane = r.f32();
    c->farPlane = r.f32();
    c->fov = r.f32();
    r.floats<4>();  // screen rect
    r.floats<2>();  // z range
    r.str();        // target texture
    return c;
}

std::unique_ptr<EnvironObj> decodeEnviron(Rec& r) {
    auto e = std::make_unique<EnvironObj>();
    expectVersion(r, 1, "Environ");
    readDrawable(r);
    e->lights = r.refs();
    e->ambient = r.floats<4>();
    e->fogStart = r.f32();
    e->fogEnd = r.f32();
    e->fogColor = r.floats<4>();
    e->fogEnable = r.b8();
    return e;
}

std::unique_ptr<LightObj> decodeLight(Rec& r) {
    auto l = std::make_unique<LightObj>();
    expectVersion(r, 3, "Light");
    l->trans = readTransformable(r);
    l->color = r.floats<4>();
    l->range = r.f32();
    l->type = static_cast<int>(r.u32());
    return l;
}

std::unique_ptr<MultiMeshObj> decodeMultiMesh(Rec& r) {
    auto m = std::make_unique<MultiMeshObj>();
    expectVersion(r, 0, "MultiMesh");
    m->draw = readDrawable(r);
    m->mesh = r.str();
    m->instances.resize(r.u32());
    for (auto& x : m->instances) x = r.xfm();
    return m;
}

// RndTransAnim_t2_v8 @ 0x1dd538, version 4.
std::unique_ptr<AnimObj> decodeTransAnim(Rec& r) {
    auto a = std::make_unique<AnimObj>();
    expectVersion(r, 4, "TransAnim");
    a->kind = AnimObj::Kind::Trans;
    a->anim = readAnimatable(r);
    readDrawable(r);
    a->target = r.str();
    a->rotKeys = readKeys<4>(r);
    a->transKeys = readKeys<3>(r);
    a->owner = r.str();
    a->transSpline = r.b8();
    a->repeatTrans = r.b8();
    a->scaleKeys = readKeys<3>(r);
    a->scaleSpline = r.b8();
    a->followPath = r.b8();
    a->rotSlerp = r.b8();
    return a;
}

// RndMeshAnim_v8 @ 0x1f9940, version 0 (tools/rnd_classes/geometry.py decode_meshanim).
std::unique_ptr<AnimObj> decodeMeshAnim(Rec& r) {
    auto a = std::make_unique<AnimObj>();
    expectVersion(r, 0, "MeshAnim");
    a->kind = AnimObj::Kind::Mesh;
    a->anim = readAnimatable(r);
    a->target = r.str();
    auto keys = [&](auto& values, std::vector<float>& frames, auto width) {
        constexpr size_t W = decltype(width)::value;
        for (uint32_t n = r.u32(); n; --n) {
            std::vector<std::array<float, W>> v(r.u32());
            for (auto& e : v) e = r.floats<W>();
            values.push_back(std::move(v));
            frames.push_back(r.f32());
        }
    };
    keys(a->vert.points, a->vert.pointFrames, std::integral_constant<size_t, 3>{});
    keys(a->vert.uvs, a->vert.uvFrames, std::integral_constant<size_t, 2>{});
    keys(a->vert.colors, a->vert.colorFrames, std::integral_constant<size_t, 4>{});
    a->owner = r.str();
    return a;
}

// RndFlare_t2_v8 @ 0x1f4710, version 3.
std::unique_ptr<FlareObj> decodeFlare(Rec& r) {
    auto f = std::make_unique<FlareObj>();
    expectVersion(r, 3, "Flare");
    f->trans = readTransformable(r);
    f->draw = readDrawable(r);
    f->mat = r.str();
    f->sizes = r.floats<2>();
    f->range = r.floats<2>();
    f->steps = std::max(1, int(r.u32()));
    return f;
}

// RndMorph Load (tools/rnd_classes/geometry.py decode_morph), version 3.
std::unique_ptr<MorphObj> decodeMorph(Rec& r) {
    auto m = std::make_unique<MorphObj>();
    expectVersion(r, 3, "Morph");
    readAnimatable(r);
    m->poses.resize(r.u32());
    for (auto& p : m->poses) {
        p.mesh = r.str();
        p.keys.resize(r.u32());
        for (auto& k : p.keys) {
            k.v = {r.f32(), 0};
            k.frame = r.f32();
        }
    }
    m->target = r.str();
    r.b8();  // normals
    r.b8();  // spline
    m->intensity = r.f32();
    return m;
}

// RndParticleSys_t3_v8 @ 0x1cedc0, version 22 (every particle record in the game).
std::unique_ptr<ParticleSysObj> decodeParticleSys(Rec& r) {
    auto p = std::make_unique<ParticleSysObj>();
    expectVersion(r, 22, "ParticleSys");
    p->anim = readAnimatable(r);
    p->trans = readTransformable(r);
    p->draw = readDrawable(r);
    p->life = r.floats<2>();
    p->posLow = r.floats<3>();
    p->posHigh = r.floats<3>();
    p->speed = r.floats<2>();
    p->pitch = r.floats<2>();
    p->yaw = r.floats<2>();
    p->emitRate = r.floats<2>();
    p->startSize = r.floats<2>();
    p->endSize = r.floats<2>();
    p->startLow = r.floats<4>();
    p->startHigh = r.floats<4>();
    p->endLow = r.floats<4>();
    p->endHigh = r.floats<4>();
    p->collide = r.b8();
    p->plane = r.floats<4>();
    p->force = r.floats<3>();
    p->mat = r.str();
    p->type = int(r.u32());
    p->growRatio = r.f32();
    p->shrinkRatio = r.f32();
    p->midRatio = r.f32();
    p->midLow = r.floats<4>();
    p->midHigh = r.floats<4>();
    p->maxParticles = int(r.u32());
    p->bubblePeriod = r.floats<2>();
    p->bubbleSize = r.floats<2>();
    p->bubble = r.b8();
    p->relativeMotion = r.f32();
    p->mesh = r.str();
    if (r.b8())  // preserve_particles: pos, colour, size per particle (FUN_001d24e0)
        for (uint32_t n = r.u32(); n; --n) {
            ParticleSysObj::Static q;
            q.pos = r.floats<3>();
            q.color = r.floats<4>();
            q.size = r.f32();
            p->preserved.push_back(q);
        }
    return p;
}

// RndParticleSysAnim_v8 @ 0x1fe528, versions 0..2.
std::unique_ptr<AnimObj> decodeParticleSysAnim(Rec& r) {
    auto a = std::make_unique<AnimObj>();
    uint32_t v = r.u32();
    if (v > 2) throw std::runtime_error("ParticleSysAnim version " + std::to_string(v));
    a->kind = AnimObj::Kind::Particle;
    a->anim = readAnimatable(r);
    a->target = r.str();
    a->startColorKeys = readKeys<4>(r);
    a->endColorKeys = readKeys<4>(r);
    if (v < 2) {
        auto fk = readKeys<1>(r);
        a->owner = r.str();
        float scale = v == 1 ? r.f32() : 1.0f;
        for (const auto& k : fk) a->emitKeys.push_back({{k.v[0], k.v[0] * scale}, k.frame});
    } else {
        a->emitKeys = readKeys<2>(r);
        a->owner = r.str();
        a->speedKeys = readKeys<2>(r);
        a->lifeKeys = readKeys<2>(r);
        a->startSizeKeys = readKeys<2>(r);
    }
    return a;
}

// RndMatAnim_v8 @ 0x1c1950, version 5.
std::unique_ptr<AnimObj> decodeMatAnim(Rec& r) {
    auto a = std::make_unique<AnimObj>();
    expectVersion(r, 5, "MatAnim");
    a->kind = AnimObj::Kind::Mat;
    a->anim = readAnimatable(r);
    a->target = r.str();
    a->stages.resize(r.u32());
    for (auto& st : a->stages) {
        st.trans = readKeys<3>(r);
        st.scale = readKeys<3>(r);
        st.rot = readKeys<3>(r);
        st.tex.resize(r.u32());
        for (auto& k : st.tex) {
            k.first = r.str();
            k.second = r.f32();
        }
    }
    a->owner = r.str();
    a->colorKeys = readKeys<4>(r);
    a->floatKeys = readKeys<1>(r);
    return a;
}

// RndLightAnim_v8 @ 0x1f8390, version 1.
std::unique_ptr<AnimObj> decodeLightAnim(Rec& r) {
    auto a = std::make_unique<AnimObj>();
    expectVersion(r, 1, "LightAnim");
    a->kind = AnimObj::Kind::Light;
    a->anim = readAnimatable(r);
    a->target = r.str();
    a->colorKeys = readKeys<4>(r);
    a->owner = r.str();
    return a;
}

// RndEnvAnim_v8 @ 0x1b5be8, version 3.
std::unique_ptr<AnimObj> decodeEnvAnim(Rec& r) {
    auto a = std::make_unique<AnimObj>();
    expectVersion(r, 3, "EnvAnim");
    a->kind = AnimObj::Kind::Env;
    a->anim = readAnimatable(r);
    a->target = r.str();
    a->colorKeys = readKeys<4>(r);
    a->owner = r.str();
    a->fogColorKeys = readKeys<4>(r);
    a->fogRangeKeys = readKeys<2>(r);
    return a;
}

// RndCamAnim_v8 @ 0x1f15b0, version 0.
std::unique_ptr<AnimObj> decodeCamAnim(Rec& r) {
    auto a = std::make_unique<AnimObj>();
    expectVersion(r, 0, "CamAnim");
    a->kind = AnimObj::Kind::Cam;
    a->anim = readAnimatable(r);
    a->target = r.str();
    a->floatKeys = readKeys<1>(r);
    a->owner = r.str();
    return a;
}

// RndFont_v8 @ 0x1b7600, version 7.
std::unique_ptr<FontObj> decodeFont(Rec& r) {
    auto f = std::make_unique<FontObj>();
    expectVersion(r, 7, "Font");
    f->mat = r.str();
    f->cellWidth = r.f32();
    f->cellHeight = r.f32();
    f->deprecatedSize = r.f32();
    f->space = r.f32();
    f->chars = r.str();
    if (r.b8()) {
        for (uint32_t n = r.u32(); n; --n) {
            uint32_t packed = r.u32();
            float amount = r.f32();
            f->kerning[{char(packed & 0xff), char((packed >> 8) & 0xff)}] = amount;
        }
    }
    return f;
}

// RndText_t2_v8 @ 0x1d75c8, version 15.
std::unique_ptr<TextObj> decodeText(Rec& r) {
    auto t = std::make_unique<TextObj>();
    expectVersion(r, 15, "Text");
    t->draw = readDrawable(r);
    t->trans = readTransformable(r);
    t->font = r.str();
    t->align = r.u32();
    t->text = r.str();
    t->color = r.floats<4>();
    t->wrapWidth = r.f32();
    t->leading = r.f32();
    r.u32();  // fixed length
    t->italics = r.f32();
    t->size = r.f32();
    t->markup = r.b8();
    t->capsMode = static_cast<int>(r.u32());
    return t;
}

// UI components (tools/ui_panels.py has the annotated versions).
std::unique_ptr<UiObj> decodeUi(Rec& r, const std::string& type) {
    auto u = std::make_unique<UiObj>();
    uint32_t v = r.u32();
    auto base = [&](bool drawFirst) {
        if (drawFirst) {
            u->draw = readDrawable(r);
            u->trans = readTransformable(r);
        } else {
            u->trans = readTransformable(r);
            u->draw = readDrawable(r);
        }
    };
    if (type == "LabelEx") {
        u->kind = UiObj::Kind::Label;
        base(false);
        if (v >= 6) {
            u->fit = r.b8();
            u->fitWidth = r.f32();
            u->fitHeight = r.f32();
        }
        if (v > 4) u->leading = r.f32();
        if (v > 3) u->align = r.u32();
        if (v > 2) {
            u->jitterSeed = r.u32();
            u->jitter = r.floats<3>();
        }
        u->style = v >= 2 ? r.str() : "labelex";
        if (v >= 1) u->caps = r.b8();
        u->text = r.str();
        u->width = r.f32();
    } else if (type == "ButtonEx") {
        u->kind = UiObj::Kind::Button;
        base(false);
        if (v > 2) {
            u->jitterSeed = r.u32();
            u->jitter = r.floats<3>();
        }
        if (v >= 2) {
            u->style = r.str();
            u->fit = r.b8();  // +0x134: stretch the text to box_width (FUN_0012c408)
            if (v < 4) r.b8();
        } else {
            u->style = "buttonex";
            u->fit = true;
        }
        u->caps = v >= 1 && r.b8();  // +0x12c: the text's caps_mode 2 / 0 (FUN_0012c3c8)
        u->fitWidth = r.f32();       // +0x120 box width
        u->fitHeight = r.f32();      // +0x124 box height (the focus box only)
        u->text = r.str();
    } else if (type == "PictureEx" || type == "PictureToggle" || type == "TextEntry") {
        u->kind = type == "PictureEx" ? UiObj::Kind::Picture
                : type == "PictureToggle" ? UiObj::Kind::PictureToggle : UiObj::Kind::TextEntry;
        base(false);
        u->style = (type != "PictureEx" || v >= 1) ? r.str() : "pictureex";
    } else if (type == "List") {
        u->kind = UiObj::Kind::List;
        if (v < 1) r.u32(), r.u32();
        base(false);
        u->style = r.str();
        if (v < 1) u->object = r.str();
        u->count = static_cast<int>(r.u32());
        u->spacing = r.f32();
        u->mode = v < 2 ? (r.b8() ? 0 : 2) : static_cast<int>(r.u32());
    } else if (type == "Slider") {
        u->kind = UiObj::Kind::Slider;
        base(false);
        u->style = r.str();
        u->symbol = r.str();
        u->object = r.str();
    } else if (type == "CreditList") {
        u->kind = UiObj::Kind::CreditList;
        base(true);
        u->count = static_cast<int>(r.u32());
        u->spacing = r.f32();
    } else {  // Placer
        u->kind = UiObj::Kind::Placer;
        base(true);
        u->symbol = r.str();
    }
    return u;
}

}  // namespace

Transformable* SceneObject::transformable() {
    if (mesh) return &mesh->trans;
    if (view) return &view->trans;
    if (cam) return &cam->trans;
    if (light) return &light->trans;
    if (text) return &text->trans;
    if (ui) return &ui->trans;
    if (particles) return &particles->trans;
    if (flare) return &flare->trans;
    return nullptr;
}

Drawable* SceneObject::drawable() {
    if (mesh) return &mesh->draw;
    if (view) return &view->draw;
    if (multiMesh) return &multiMesh->draw;
    if (text) return &text->draw;
    if (ui) return &ui->draw;
    if (particles) return &particles->draw;
    if (flare) return &flare->draw;
    return nullptr;
}

SceneObject* Scene::add(SceneObject&& o) {
    if (objects_.size() >= objects_.capacity() || byName_.count(o.name)) return nullptr;
    byName_[o.name] = objects_.size();
    objects_.push_back(std::move(o));
    return &objects_.back();
}

Scene::Scene(const Bytes& file) {
    Bytes data = decompress(file);
    ByteReader r(data);
    if (r.u32le() != 10) throw std::runtime_error("unsupported scene version");
    uint32_t count = r.u32le();
    objects_.reserve(count + kSpareObjects);
    objects_.resize(count);
    for (auto& o : objects_) {
        o.type = r.str(r.u32le());
        o.name = r.str(r.u32le());
    }
    for (uint32_t n = r.u32le(); n; --n) r.skip(r.u32le());  // external file paths

    for (size_t i = 0; i < objects_.size(); ++i) {
        SceneObject& o = objects_[i];
        const uint8_t* start = r.ptr();
        const uint8_t* end = findMarker(start, r.remaining());
        if (!end) throw std::runtime_error("scene object without end marker: " + o.name);
        size_t len = size_t(end - start);
        Rec rec(start, len);
        try {
            if (o.type == "Tex") o.tex = decodeTex(rec);
            else if (o.type == "Mat") o.mat = decodeMat(rec);
            else if (o.type == "Mesh") o.mesh = decodeMesh(rec);
            else if (o.type == "View") o.view = decodeView(rec);
            else if (o.type == "Cam") o.cam = decodeCam(rec);
            else if (o.type == "Environ") o.environment = decodeEnviron(rec);
            else if (o.type == "Light") o.light = decodeLight(rec);
            else if (o.type == "MultiMesh") o.multiMesh = decodeMultiMesh(rec);
            else if (o.type == "TransAnim") o.anim = decodeTransAnim(rec);
            else if (o.type == "MatAnim") o.anim = decodeMatAnim(rec);
            else if (o.type == "LightAnim") o.anim = decodeLightAnim(rec);
            else if (o.type == "EnvAnim") o.anim = decodeEnvAnim(rec);
            else if (o.type == "CamAnim") o.anim = decodeCamAnim(rec);
            else if (o.type == "MeshAnim") o.anim = decodeMeshAnim(rec);
            else if (o.type == "ParticleSys") o.particles = decodeParticleSys(rec);
            else if (o.type == "Morph") o.morph = decodeMorph(rec);
            else if (o.type == "Flare") o.flare = decodeFlare(rec);
            else if (o.type == "ParticleSysAnim") o.anim = decodeParticleSysAnim(rec);
            else if (o.type == "Font") o.font = decodeFont(rec);
            else if (o.type == "Text") o.text = decodeText(rec);
            else if (o.type == "LabelEx" || o.type == "ButtonEx" || o.type == "PictureEx" || o.type == "PictureToggle" ||
                     o.type == "List" || o.type == "Slider" || o.type == "TextEntry" || o.type == "CreditList" ||
                     o.type == "Placer")
                o.ui = decodeUi(rec, o.type);
            else ++undecoded_;
            if ((o.tex || o.mat || o.mesh || o.view || o.cam || o.environment || o.light || o.multiMesh || o.anim ||
                 o.font || o.text || o.ui || o.particles || o.morph || o.flare) &&
                rec.remaining())
                throw std::runtime_error(std::to_string(rec.remaining()) + " unread bytes");
        } catch (const std::exception& ex) {
            SDL_Log("scene: %s %s: %s", o.type.c_str(), o.name.c_str(), ex.what());
            o.tex.reset(), o.mat.reset(), o.mesh.reset(), o.view.reset();
            o.cam.reset(), o.environment.reset(), o.light.reset(), o.multiMesh.reset(), o.anim.reset();
            o.font.reset(), o.text.reset(), o.ui.reset(), o.particles.reset(), o.morph.reset(), o.flare.reset();
            ++undecoded_;
        }
        byName_[o.name] = i;
        r.skip(len + sizeof kEndMarker);
    }
}

void Scene::loadExternalTextures(const GameData& data, const std::string& dir) {
    for (SceneObject& o : objects_) {
        if (!o.tex || o.tex->image.width > 0 || o.tex->file.empty()) continue;
        // "image/brick03.bmp" -> <dir>/image/gen/brick03.bmp_ps2
        std::string f = o.tex->file;
        size_t slash = f.rfind('/');
        std::string sub = slash == std::string::npos ? "" : f.substr(0, slash + 1);
        std::string path = (dir.empty() ? "" : dir + "/") + sub + "gen/" + f.substr(slash + 1) + "_ps2";
        if (!data.exists(path)) continue;
        try {
            Bytes b = data.read(path);
            o.tex->image = decodeBitmap(b.data(), b.size(), nullptr);
        } catch (const std::exception& ex) {
            SDL_Log("scene: texture %s: %s", path.c_str(), ex.what());
        }
    }
}

const SceneObject* Scene::find(const std::string& name) const {
    auto it = byName_.find(name);
    return it == byName_.end() ? nullptr : &objects_[it->second];
}
