// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/havok.h"

#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace havok {
namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("havok: " + what); }
void need(bool c, const char* what) {
    if (!c) fail(what);
}

constexpr uint32_t kNone = 0xFFFFFFFFu;

// hkClassMember::Type (Havok 4.6)
enum Type : uint8_t {
    VOID, BOOL, CHAR, INT8, UINT8, INT16, UINT16, INT32, UINT32, INT64, UINT64, REAL, VECTOR4, QUATERNION, MATRIX3,
    ROTATION, QSTRANSFORM, MATRIX4, TRANSFORM, ZERO, POINTER, FUNCTIONPOINTER, ARRAY, INPLACEARRAY, ENUM, STRUCT,
    SIMPLEARRAY, HOMOGENEOUSARRAY, VARIANT, CSTRING, ULONG, FLAGS, TYPE_COUNT
};

size_t fixedSize(uint8_t t) {
    switch (t) {
    case BOOL: case CHAR: case INT8: case UINT8: return 1;
    case INT16: case UINT16: return 2;
    case INT32: case UINT32: case REAL: case POINTER: case FUNCTIONPOINTER: case CSTRING: case ULONG: return 4;
    case INT64: case UINT64: case SIMPLEARRAY: case VARIANT: return 8;
    case ARRAY: case HOMOGENEOUSARRAY: return 12;
    case VECTOR4: case QUATERNION: return 16;
    case MATRIX3: case ROTATION: case QSTRANSFORM: return 48;
    case MATRIX4: case TRANSFORM: return 64;
    default: return 0;
    }
}

using Addr = uint64_t;  // section << 32 | offset
constexpr Addr addr(uint32_t sec, uint32_t off) { return Addr(sec) << 32 | off; }
constexpr uint32_t secOf(Addr a) { return uint32_t(a >> 32); }
constexpr uint32_t offOf(Addr a) { return uint32_t(a); }

struct Class;
struct Enum {
    std::string name;
    std::map<int32_t, std::string> items;
};
struct Member {
    std::string name;
    const Class* cls = nullptr;
    const Enum* en = nullptr;
    uint8_t type = 0, subtype = 0;
    int16_t carray = 0;
    uint16_t flags = 0, offset = 0;
};
struct Class {
    std::string name;
    const Class* parent = nullptr;
    uint32_t size = 0;
    std::vector<Member> members;
    void all(std::vector<const Member*>& out) const {
        if (parent) parent->all(out);
        for (const Member& m : members) out.push_back(&m);
    }
    const Member* find(const std::string& n) const {
        for (const Member& m : members)
            if (m.name == n) return &m;
        return parent ? parent->find(n) : nullptr;
    }
};

class Pack {
public:
    Pack(const uint8_t* d, size_t n) : d_(d), n_(n) {}

    void load() {
        static const uint8_t magic[8] = {0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10};
        need(n_ >= 0x40 && std::memcmp(d_, magic, 8) == 0, "no packfile magic");
        need(be(8) == 0x786E6B68u && be(12) == 4 && be(16) == 0x04000001u, "packfile header");
        need(std::memcmp(d_ + 40, "Havok-4.6.0-r1", 15) == 0, "packfile version");
        const uint32_t nsec = be(20);
        csec_ = be(24), coff_ = be(28);
        const uint32_t cnsec = be(32);
        cnoff_ = be(36);
        need(nsec == 3 && csec_ == 1 && cnsec == 0, "section indices");
        for (uint32_t k = 0; k < nsec; ++k) {
            const size_t o = 0x40 + 0x30 * size_t(k);
            Sec s;
            s.start = be(o + 20);
            for (int j = 0; j < 6; ++j) s.off[j] = be(o + 24 + 4 * size_t(j));
            need(size_t(s.start) + s.off[5] <= n_, "section past the end");
            secs_.push_back(s);
        }
        for (uint32_t si = 0; si < nsec; ++si) {
            const Sec& s = secs_[si];
            for (int kind = 0; kind < 3; ++kind) {
                const uint32_t w = kind == 0 ? 2 : 3, words = (s.off[kind + 1] - s.off[kind]) / 4;
                for (uint32_t i = 0; i + w <= words; i += w) {
                    const size_t o = s.start + s.off[kind] + 4 * size_t(i);
                    const uint32_t r0 = be(o), r1 = be(o + 4), r2 = w == 3 ? be(o + 8) : 0;
                    if (r0 == kNone) continue;
                    if (kind == 0) ptr_[addr(si, r0)] = addr(si, r1);
                    else if (kind == 1) need(r1 < nsec, "global fixup section"), ptr_[addr(si, r0)] = addr(r1, r2);
                    else obj_[addr(si, r0)] = cstrAt(addr(0, r2));
                }
            }
        }
        readTypes();
    }

    // --- raw access
    uint32_t be(size_t o) const {
        need(o + 4 <= n_, "read past the end");
        return uint32_t(d_[o]) << 24 | uint32_t(d_[o + 1]) << 16 | uint32_t(d_[o + 2]) << 8 | d_[o + 3];
    }
    const uint8_t* at(Addr a, size_t n) const {
        need(secOf(a) < secs_.size(), "section index");
        const Sec& s = secs_[secOf(a)];
        need(size_t(offOf(a)) + n <= s.off[0], "read past a section");
        return d_ + s.start + offOf(a);
    }
    uint32_t u32(Addr a) const { const uint8_t* p = at(a, 4); return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
    int32_t s32(Addr a) const { return int32_t(u32(a)); }
    uint16_t u16(Addr a) const { const uint8_t* p = at(a, 2); return uint16_t(p[0] << 8 | p[1]); }
    float f32(Addr a) const { uint32_t u = u32(a); float f; std::memcpy(&f, &u, 4); return f; }
    std::string cstrAt(Addr a) const {
        const Sec& s = secs_.at(secOf(a));
        size_t b = s.start + offOf(a), e = b;
        const size_t end = s.start + s.off[0];
        while (e < end && d_[e]) ++e;
        need(e < end, "unterminated string");
        return std::string(reinterpret_cast<const char*>(d_ + b), e - b);
    }
    // The pointer stored at a (0 in the file, set by a fixup): its target, or 0 when null.
    bool pointer(Addr a, Addr& out) {
        auto it = ptr_.find(a);
        need(u32(a) == 0, "pointer with a stored value");
        if (it == ptr_.end()) return false;
        used_.insert(a);
        out = it->second;
        return true;
    }
    Addr pointerOrFail(Addr a) {
        Addr t = 0;
        need(pointer(a, t), "null pointer");
        return t;
    }

    // --- reflection
    void readTypes() {
        for (auto& [a, cname] : obj_) {
            if (secOf(a) != 2) continue;
            if (cname == "hkClassEnum") enumAt(a);
            else need(cname == "hkClass", "__types__ object class"), classes_[a];
        }
        for (auto& [a, k] : classes_) {
            k.name = cstrAt(pointerOrFail(a));
            k.size = u32(a + 8);
            for (int w = 8; w < 12; ++w) need(u32(a + 4 * Addr(w)) == 0, "hkClass tail words");
            byName_[k.name] = &k;
        }
        for (auto& [a, k] : classes_) {
            Addr p = 0;
            if (pointer(a + 4, p)) k.parent = &classes_.at(p);
            const int32_t ne = s32(a + 0x14);
            if (pointer(a + 0x10, p))
                for (int32_t i = 0; i < ne; ++i) enumAt(p + 0x0C * Addr(i));
            Addr mp = 0;
            const int32_t nm = s32(a + 0x1C);
            need(nm == 0 || pointer(a + 0x18, mp), "member table");
            Addr dflt = 0;
            need(!pointer(a + 0x20, dflt), "class defaults");
            for (int32_t i = 0; i < nm; ++i) {
                const Addr q = mp + 0x14 * Addr(i);
                Member m;
                m.name = cstrAt(pointerOrFail(q));
                if (pointer(q + 4, p)) m.cls = &classes_.at(p);
                if (pointer(q + 8, p)) m.en = &enumAt(p);
                const uint8_t* b = at(q + 12, 8);
                m.type = b[0], m.subtype = b[1];
                m.carray = int16_t(b[2] << 8 | b[3]);
                m.flags = uint16_t(b[4] << 8 | b[5]);
                m.offset = uint16_t(b[6] << 8 | b[7]);
                need(m.type < TYPE_COUNT && m.subtype < TYPE_COUNT, "member type");
                k.members.push_back(std::move(m));
            }
        }
    }
    const Enum& enumAt(Addr a) {
        auto it = enums_.find(a);
        if (it != enums_.end()) return it->second;
        Enum e;
        e.name = cstrAt(pointerOrFail(a));
        const int32_t n = s32(a + 8);
        Addr items = 0;
        need(n == 0 || pointer(a + 4, items), "enum items");
        for (int32_t i = 0; i < n; ++i) e.items[s32(items + 8 * Addr(i))] = cstrAt(pointerOrFail(items + 8 * Addr(i) + 4));
        return enums_[a] = std::move(e);
    }
    const Class* cls(const std::string& n) const {
        auto it = byName_.find(n);
        return it == byName_.end() ? nullptr : it->second;
    }

    static size_t enumSize(const Member& m) {
        switch (m.flags & 0x38) {
        case 8: return 1;
        case 16: return 2;
        case 32: return 4;
        default: fail("enum storage");
        }
    }
    static size_t elemSize(uint8_t t, const Member& m) {
        if (t == STRUCT) return m.cls ? m.cls->size : 0;
        if (t == ENUM || t == FLAGS) return enumSize(m);
        const size_t s = fixedSize(t);
        need(s != 0, "member element type");
        return s;
    }

    // --- the generic walk: every member of every object, marking the pointer
    // fixups it reads and the objects it reaches.
    void walkObject(Addr a, const Class* k) {
        auto o = obj_.find(a);
        if (o != obj_.end()) {
            k = cls(o->second);
            need(k != nullptr, "object of an unreflected class");
        }
        need(k != nullptr, "untyped object");
        if (!visited_.insert({a, k}).second) return;
        if (o != obj_.end()) reached_.insert(a);
        need(size_t(offOf(a)) + k->size <= secs_.at(secOf(a)).off[0], "object past its section");
        walkStruct(a, k);
    }
    void walkStruct(Addr a, const Class* k) {
        std::vector<const Member*> ms;
        k->all(ms);
        for (const Member* m : ms) {
            const Addr q = a + m->offset;
            const int n = m->carray ? m->carray : 1;
            switch (m->type) {
            case ZERO: break;
            case ARRAY:
            case SIMPLEARRAY: {
                const int32_t cnt = s32(q + 4);
                Addr p = 0;
                const bool has = pointer(q, p);
                need(has == (cnt != 0), "array pointer vs count");
                if (m->type == ARRAY) {
                    const uint32_t cap = u32(q + 8);
                    need((cap & 0x80000000u) && ((cap & 0x3FFFFFFFu) == 0 || (cap & 0x3FFFFFFFu) == uint32_t(cnt)), "array capacity");
                }
                const size_t es = elemSize(m->subtype, *m);
                for (int32_t i = 0; i < cnt; ++i) walkValue(p + es * Addr(i), m->subtype, *m);
                break;
            }
            case HOMOGENEOUSARRAY: case INPLACEARRAY: fail("array kind");
            default: {
                const size_t es = m->type == STRUCT ? m->cls->size : m->type == ENUM || m->type == FLAGS ? enumSize(*m) : fixedSize(m->type);
                need(es != 0, "member type");
                for (int i = 0; i < n; ++i) walkValue(q + es * Addr(i), m->type, *m);
            }
            }
        }
    }
    void walkValue(Addr a, uint8_t t, const Member& m) {
        Addr p = 0;
        switch (t) {
        case STRUCT: walkStruct(a, m.cls); break;
        case CSTRING: if (pointer(a, p)) (void)cstrAt(p); break;
        case POINTER: if (pointer(a, p)) walkObject(p, m.cls); break;
        case VARIANT: {
            Addr c = 0;
            if (pointer(a, p)) walkObject(p, nullptr);
            (void)pointer(a + 4, c);
            break;
        }
        default: (void)at(a, elemSize(t, m));
        }
    }

    void checkAll() {
        const Addr root = addr(csec_, coff_);
        auto it = obj_.find(root);
        need(it != obj_.end() && it->second == cstrAt(addr(0, cnoff_)), "contents object");
        walkObject(root, nullptr);
        for (auto& [a, c] : obj_)
            if (secOf(a) == 1) need(reached_.count(a) != 0, "object not reached from the contents");
        for (auto& [a, t] : ptr_)
            if (secOf(a) == 1) need(used_.count(a) != 0, "pointer fixup no member reads");
    }

    // --- typed views for the summary
    struct Obj {
        Pack* pk;
        Addr a;
        const Class* k;
        const Member& m(const char* n) const {
            const Member* r = k->find(n);
            if (!r) fail(std::string("no member ") + n + " in " + k->name);
            return *r;
        }
        Addr at(const char* n) const { return a + m(n).offset; }
        float real(const char* n) const { return pk->f32(at(n)); }
        int32_t i32(const char* n) const { return pk->s32(at(n)); }
        std::array<float, 4> vec(const char* n) const {
            std::array<float, 4> v{};
            for (int i = 0; i < 4; ++i) v[size_t(i)] = pk->f32(at(n) + 4 * Addr(i));
            return v;
        }
        std::string str(const char* n) const {
            Addr p = 0;
            return pk->peek(at(n), p) ? pk->cstrAt(p) : std::string();
        }
        std::string enumName(const char* n) const {
            const Member& e = m(n);
            const uint8_t* b = pk->at(at(n), enumSize(e));
            int32_t v = 0;
            for (size_t i = 0; i < enumSize(e); ++i) v = v << 8 | b[i];
            if (e.en) {
                auto it = e.en->items.find(v);
                if (it != e.en->items.end()) return it->second;
            }
            return std::to_string(v);
        }
        Obj sub(const char* n) const { return {pk, at(n), m(n).cls}; }
        bool ptr(const char* n, Obj& out, Addr index = 0) const {
            Addr p = 0;
            if (!pk->peek(at(n) + 4 * index, p)) return false;
            out = pk->objAt(p, m(n).cls);
            return true;
        }
        std::vector<Obj> arrayOfPtrs(const char* n) const {
            std::vector<Obj> v;
            const Member& am = m(n);
            const int32_t cnt = pk->s32(at(n) + 4);
            Addr p = 0;
            if (cnt && pk->peek(at(n), p))
                for (int32_t i = 0; i < cnt; ++i) {
                    Addr q = 0;
                    if (pk->peek(p + 4 * Addr(i), q)) v.push_back(pk->objAt(q, am.cls));
                }
            return v;
        }
        std::vector<Obj> arrayOfStructs(const char* n) const {
            std::vector<Obj> v;
            const Member& am = m(n);
            const int32_t cnt = pk->s32(at(n) + 4);
            Addr p = 0;
            if (cnt && pk->peek(at(n), p))
                for (int32_t i = 0; i < cnt; ++i) v.push_back({pk, p + am.cls->size * Addr(i), am.cls});
            return v;
        }
    };
    bool peek(Addr a, Addr& out) const {
        auto it = ptr_.find(a);
        if (it == ptr_.end()) return false;
        out = it->second;
        return true;
    }
    Obj objAt(Addr a, const Class* fallback) {
        auto o = obj_.find(a);
        const Class* k = o != obj_.end() ? cls(o->second) : fallback;
        need(k != nullptr, "object class");
        return {this, a, k};
    }
    Obj root() { return objAt(addr(csec_, coff_), nullptr); }

    const std::map<Addr, std::string>& objects() const { return obj_; }
    size_t fixups() const { return ptr_.size(); }
    size_t classCount() const { return classes_.size(); }

private:
    struct Sec {
        uint32_t start = 0, off[6] = {};  // data size (= local fixups), global, virtual, exports, imports, end
    };
    const uint8_t* d_;
    size_t n_;
    uint32_t csec_ = 0, coff_ = 0, cnoff_ = 0;
    std::vector<Sec> secs_;
    std::unordered_map<Addr, Addr> ptr_;
    std::map<Addr, std::string> obj_;
    std::map<Addr, Class> classes_;
    std::map<Addr, Enum> enums_;
    std::unordered_map<std::string, const Class*> byName_;
    std::set<Addr> used_, reached_;
    std::set<std::pair<Addr, const Class*>> visited_;
};

std::array<float, 3> xyz(const std::array<float, 4>& v) { return {v[0], v[1], v[2]}; }

Shape readShape(const Pack::Obj& s) {
    Shape out;
    out.className = s.k->name;
    Pack::Obj cur = s;
    if (cur.k->name == "hkConvexTranslateShape") {
        out.translation = xyz(cur.vec("translation"));
        Pack::Obj child{};
        need(cur.sub("childShape").ptr("childShape", child), "translate shape child");
        cur = child;
    }
    if (cur.k->name == "hkCapsuleShape") {
        out.kind = Shape::Capsule;
        out.radius = cur.real("radius");
        out.a = xyz(cur.vec("vertexA"));
        out.b = xyz(cur.vec("vertexB"));
    } else if (cur.k->name == "hkBoxShape") {
        out.kind = Shape::Box;
        out.radius = cur.real("radius");
        out.a = xyz(cur.vec("halfExtents"));
    } else if (cur.k->name == "hkMoppBvTreeShape") {
        out.kind = Shape::MoppMesh;
        Pack::Obj mesh{};
        need(cur.sub("child").ptr("childShape", mesh), "MOPP child");
        for (const Pack::Obj& sp : mesh.arrayOfStructs("trianglesSubparts")) {
            out.triangles += size_t(sp.i32("numTriangleShapes"));
            out.vertices += size_t(sp.i32("numVertices"));
        }
    } else {
        out.kind = Shape::Other;
    }
    return out;
}

}  // namespace

Physics parse(const uint8_t* data, size_t size) {
    Pack pk(data, size);
    pk.load();
    pk.checkAll();
    Physics ph;
    ph.objects = 0;
    for (auto& [a, c] : pk.objects())
        if (secOf(a) == 1) ++ph.objects, ph.objectClasses.push_back(c);
    ph.fixups = pk.fixups();
    ph.classes = pk.classCount();
    Pack::Obj root = pk.root();
    ph.contentsClass = root.k->name;
    need(ph.contentsClass == "hkPhysicsData", "contents class");
    std::map<Addr, int> bodyIndex;
    for (const Pack::Obj& sys : root.arrayOfPtrs("systems")) {
        for (const Pack::Obj& rb : sys.arrayOfPtrs("rigidBodies")) {
            Body b;
            b.name = rb.str("name");
            const Pack::Obj mo = rb.sub("motion");
            b.motion = mo.enumName("type");
            const float invMass = mo.vec("inertiaAndMassInv")[3];
            b.mass = invMass != 0 ? 1.0f / invMass : 0.0f;
            const Pack::Obj ms = mo.sub("motionState");
            for (int i = 0; i < 16; ++i) b.transform[size_t(i)] = pk.f32(ms.at("transform") + 4 * Addr(i));
            b.linearDamping = ms.real("linearDamping");
            b.angularDamping = ms.real("angularDamping");
            const Pack::Obj mat = rb.sub("material");
            b.friction = mat.real("friction");
            b.restitution = mat.real("restitution");
            Pack::Obj shape{};
            if (rb.sub("collidable").ptr("shape", shape)) b.shape = readShape(shape);
            bodyIndex[rb.a] = int(ph.bodies.size());
            ph.bodies.push_back(std::move(b));
        }
        for (const Pack::Obj& ci : sys.arrayOfPtrs("constraints")) {
            Constraint c;
            c.name = ci.str("name");
            Pack::Obj d{}, ea{}, eb{};
            need(ci.ptr("data", d), "constraint data");
            c.type = d.k->name;
            if (ci.ptr("entities", ea, 0)) c.bodyA = bodyIndex.count(ea.a) ? bodyIndex[ea.a] : -1;
            if (ci.ptr("entities", eb, 1)) c.bodyB = bodyIndex.count(eb.a) ? bodyIndex[eb.a] : -1;
            const Pack::Obj atoms = d.sub("atoms");
            if (atoms.k->find("transforms")) {
                const Pack::Obj tr = atoms.sub("transforms");
                for (int i = 0; i < 3; ++i) {
                    c.pivotA[size_t(i)] = pk.f32(tr.at("transformA") + 4 * Addr(12 + i));
                    c.pivotB[size_t(i)] = pk.f32(tr.at("transformB") + 4 * Addr(12 + i));
                }
            }
            if (c.type == "hkRagdollConstraintData") {
                c.coneMax = atoms.sub("coneLimit").real("maxAngle");
                c.twistMin = atoms.sub("twistLimit").real("minAngle");
                c.twistMax = atoms.sub("twistLimit").real("maxAngle");
            } else if (c.type == "hkLimitedHingeConstraintData") {
                c.twistMin = atoms.sub("angLimit").real("minAngle");
                c.twistMax = atoms.sub("angLimit").real("maxAngle");
            }
            ph.constraints.push_back(std::move(c));
        }
    }
    return ph;
}

}  // namespace havok
