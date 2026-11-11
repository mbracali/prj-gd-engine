// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

#include "data/bytes.h"

namespace scene {
namespace {

constexpr uint32_t kNone = 0xFFFFFFFFu;

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("scene: " + what); }
void need(bool cond, const char* what) {
    if (!cond) fail(what);
}

size_t align(size_t x, size_t a) { return (x + a - 1) & ~(a - 1); }

// Big-endian random access on top of ByteReader (bounds-checked).
struct View {
    const uint8_t* data;
    size_t size;
    uint32_t u32(size_t o) const {
        ByteReader r(data, size);
        r.seek(o);
        return r.u32be();
    }
    uint16_t u16(size_t o) const {
        ByteReader r(data, size);
        r.seek(o);
        return r.u16be();
    }
    float f32(size_t o) const {
        uint32_t u = u32(o);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    void check(size_t a, size_t b) const {
        if (a > b || b > size) fail("range past the end of the file");
    }
    bool allBytes(size_t a, size_t b, uint8_t v1, int v2 = -1) const {
        check(a, b);
        for (size_t i = a; i < b; ++i)
            if (data[i] != v1 && int(data[i]) != v2) return false;
        return true;
    }
    template <size_t N>
    std::array<float, N> floats(size_t o) const {
        std::array<float, N> r{};
        for (size_t i = 0; i < N; ++i) r[i] = f32(o + 4 * i);
        return r;
    }
};

float half(uint16_t h) {
    uint32_t s = (h >> 15) & 1, e = (h >> 10) & 0x1F, m = h & 0x3FF;
    float v;
    if (e == 0)
        v = std::ldexp(float(m), -24);
    else if (e == 31)
        v = m ? NAN : INFINITY;
    else
        v = std::ldexp(float(m | 0x400), int(e) - 25);
    return s ? -v : v;
}

// D3DDECLTYPE_HEND3N: x 11 bits, y 11 bits, z 10 bits, signed normalised.
std::array<float, 3> hend3n(uint32_t v) {
    auto s = [](uint32_t x, int bits) {
        int32_t i = int32_t(x);
        if (i & (1 << (bits - 1))) i -= 1 << bits;
        return std::max(-1.0f, float(i) / float((1 << (bits - 1)) - 1));
    };
    return {s(v & 0x7FF, 11), s((v >> 11) & 0x7FF, 11), s(v >> 22, 10)};
}

uint32_t declStride(const std::vector<VertexElement>& d, int stream) {
    uint32_t s = 0;
    for (const VertexElement& e : d)
        if (e.stream == stream) s = std::max<uint32_t>(s, e.offset + e.size);
    return s;
}

void decodeElement(const View& v, size_t o, const VertexElement& e, Mesh& m, size_t vi) {
    auto f3 = [&](std::vector<std::array<float, 3>>& out, std::array<float, 3> x) { out[vi] = x; };
    std::array<float, 4> f4{};
    switch (e.type) {
        case ElemType::Float1: f4[0] = v.f32(o); break;
        case ElemType::Float2: f4[0] = v.f32(o); f4[1] = v.f32(o + 4); break;
        case ElemType::Float3: f4 = {v.f32(o), v.f32(o + 4), v.f32(o + 8), 0}; break;
        case ElemType::Float4: f4 = v.floats<4>(o); break;
        case ElemType::Half2: f4[0] = half(v.u16(o)); f4[1] = half(v.u16(o + 2)); break;
        case ElemType::Half4:
            for (int k = 0; k < 4; ++k) f4[k] = half(v.u16(o + 2 * k));
            break;
        case ElemType::Hend3n: {
            auto n = hend3n(v.u32(o));
            f4 = {n[0], n[1], n[2], 0};
            break;
        }
        case ElemType::Udhen3n: {
            uint32_t w = v.u32(o);
            f4 = {float(w & 0x3FF) / 1023.f, float((w >> 10) & 0x7FF) / 2047.f, float(w >> 21) / 2047.f, 0};
            break;
        }
        case ElemType::Ubyte4:
        case ElemType::Color: {
            uint32_t w = v.u32(o);
            std::array<uint8_t, 4> b{uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w)};
            if (e.type == ElemType::Color)
                m.colors[vi] = {b[1], b[2], b[3], b[0]};  // ARGB word -> r, g, b, a
            else
                m.blendIndices[vi] = b;
            return;
        }
    }
    std::array<float, 3> xyz{f4[0], f4[1], f4[2]};
    switch (e.usage) {
        case 0: f3(e.index ? m.positions2 : m.positions, xyz); break;
        case 1: m.blendWeights[vi] = f4; break;
        case 3: f3(m.normals, xyz); break;
        case 5: m.texcoords[e.index & 7][vi] = f4; break;
        case 6: f3(m.tangents[e.index & 1], xyz); break;
        case 7: f3(m.binormals[e.index & 1], xyz); break;
        default: break;
    }
}

}  // namespace

std::vector<VertexElement> vertexDeclaration(uint32_t fl) {
    std::vector<VertexElement> out;
    uint16_t off = 0;
    uint8_t st = 0;
    auto add = [&](ElemType t, uint8_t size, uint8_t usage, uint8_t index) {
        out.push_back({st, off, t, size, usage, index});
        off = uint16_t(off + size);
    };
    for (uint8_t i = 0; i < ((fl >> 12) & 3) + 1; ++i) {
        if (fl & 1)
            add(ElemType::Float4, 16, 0, i);
        else
            add(ElemType::Float3, 12, 0, i);
    }
    if (fl & 8) {
        if ((fl & 0x28) == 0x28)
            add(ElemType::Hend3n, 4, 3, 0);
        else
            add(ElemType::Float3, 12, 3, 0);
    }
    for (uint8_t i = 0; i < ((fl >> 10) & 3); ++i) add(ElemType::Hend3n, 4, 7, i);
    for (uint8_t i = 0; i < ((fl >> 8) & 3); ++i) add(ElemType::Hend3n, 4, 6, i);
    if (!(fl & 0x80)) {
        if (fl & 2) add(ElemType::Udhen3n, 4, 1, 0);
        if (fl & 4) add(ElemType::Ubyte4, 4, 2, 0);
    } else {
        st = 1;
        off = 0;
    }
    if (fl & 0x10) add(ElemType::Color, 4, 10, 0);
    if (fl & 0x4000) add(ElemType::Ubyte4, 4, 2, 0);
    uint32_t t = fl >> 16;
    for (uint8_t i = 0; t; t >>= 2, ++i) {
        uint32_t c = t & 3;
        if (!c) continue;
        if (fl & 0x40) {
            if (c < 3)
                add(ElemType::Half2, 4, 5, i);
            else
                add(ElemType::Half4, 8, 5, i);
        } else if (c == 1) {
            add(ElemType::Float1, 4, 5, i);
        } else if (c == 2) {
            add(ElemType::Float2, 8, 5, i);
        } else {
            add(ElemType::Float3, 12, 5, i);
        }
    }
    return out;
}

std::vector<std::array<uint32_t, 3>> Mesh::triangles() const {
    std::vector<std::array<uint32_t, 3>> tris;
    std::vector<uint32_t> run;
    auto flush = [&] {
        for (size_t k = 2; k < run.size(); ++k) {
            uint32_t a = run[k - 2], b = run[k - 1], c = run[k];
            if (k % 2) std::swap(a, b);
            if (a != b && b != c && a != c) tris.push_back({a, b, c});
        }
        run.clear();
    };
    for (uint16_t x : indices) {
        if (x == 0x7FFF)
            flush();
        else
            run.push_back(x);
    }
    flush();
    return tris;
}

Scene parse(const uint8_t* data, size_t size, Kind kind) {
    View v{data, size};
    need(size >= 0x30, "file too small");
    Scene sc;
    sc.kind = kind;
    for (int i = 1; i < 8; ++i) need(v.u32(4 * i) == 0xFAAABACAu, "bad 0xFAAABACA header");
    size_t end = size;
    if (kind == Kind::Skin) {
        static const uint8_t trailer[16] = {1, 0x10, 0, 0xFE, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10};
        need(v.u32(0) == size - 0x30, "skin trailer offset");
        need(std::memcmp(data + size - 16, trailer, 16) == 0, "bad skin trailer");
        end = size - 16;
    } else {
        need(v.u32(0) == 0, "first word not 0");
    }

    // Materials (Function_822B27E8, Function_822B2068).
    need(data[0x20] == 4 && data[0x21] == 0x10, "material header");
    const uint32_t nmat = v.u16(0x22);
    const size_t mend = 0x20 + size_t(v.u32(0x24));
    need(v.u32(0x28) == 0x10 && v.u32(0x2C) == kNone, "material header words");
    need(mend <= end, "material section past the end");
    size_t p = 0x30;
    for (uint32_t i = 0; i < nmat; ++i) {
        need(p + 0x100 <= mend, "material past its section");
        auto W = [&](size_t o) { return v.u32(p + o); };
        need(v.allBytes(p + 4, p + 0xA0, 0), "material name area not zero");
        const uint32_t nA = W(0xA8), oA = W(0xAC), nB = W(0xB0), oB = W(0xB4), nT = W(0xB8), oT = W(0xBC);
        const uint32_t msize = W(0xC4);
        need(W(0xA4) == 0 && W(0xE0) == 0 && W(0xF4) == 0 && W(0xF8) == 0 && W(0xFC) == 0, "material reserved words");
        need(nA < 0x1000 && nB < 0x1000 && nT < 0x1000, "material counts");
        need(oA == 0x100 && oB == oA + 16 * nA && oT == oB + 16 * nB, "material array offsets");
        need(W(0xC0) == 0 && msize == align(oT + 4 * nT, 16) && W(0xE4) == msize, "material size");
        need(p + msize <= mend, "material past its section");
        need(v.allBytes(p + oT + 4 * nT, p + msize, 0), "material padding");
        Material m;
        m.key = W(0);
        m.shader = W(0xA0);
        for (uint32_t k = 0; k < nA; ++k) m.constsA.push_back(v.floats<4>(p + oA + 16 * k));
        for (uint32_t k = 0; k < nB; ++k) m.constsB.push_back(v.floats<4>(p + oB + 16 * k));
        for (uint32_t k = 0; k < nT; ++k) m.textures.push_back(W(oT + 4 * k));
        m.c8 = W(0xC8), m.cc = W(0xCC), m.d0 = W(0xD0), m.d4 = v.f32(p + 0xD4), m.d8 = W(0xD8), m.dc = W(0xDC);
        m.e8 = W(0xE8), m.ec = W(0xEC), m.f0 = W(0xF0);
        sc.materials.push_back(std::move(m));
        p += msize;
    }
    need(p == mend, "materials do not fill their section");

    // Scene header (Function_822D7E78, FUN_822d6d48).
    const size_t s = mend;
    need(v.u32(s) == 0xBABEFACEu, "no 0xBABEFACE");
    const uint32_t pad = v.u32(s + 4);
    need(pad >= 4 && pad % 4 == 0 && s + 8 + pad <= end, "scene padding");
    need(v.allBytes(s + 8, s + 4 + pad, 0) && v.u32(s + 4 + pad) == 0xEA, "scene padding bytes");
    const size_t B = s + 8 + pad;
    need(B + 0x90 <= end, "scene header past the end");
    auto H = [&](size_t o) { return v.u32(B + o); };
    sc.bmin = v.floats<3>(B);
    sc.bmax = v.floats<3>(B + 0x10);
    sc.sphere = v.floats<4>(B + 0x20);
    need(H(0x0C) == 0 && H(0x1C) == 0, "scene bbox w");
    need(H(0x30) == 0x000E0090 && H(0x3C) == end - 0x20, "scene header words");
    const uint32_t nm = H(0x4C), ns = H(0x6C);
    need(nm < 0x10000 && ns < 0x10000, "scene counts");
    const size_t oGeom = 0x90 + 0x60 * size_t(ns), oT14 = oGeom + 0x60 * size_t(ns);
    const size_t oMesh = align(oT14 + 0x14 * size_t(ns), 16), oPtr = oMesh + 0x80 * size_t(nm);
    const size_t oOrder = oPtr + 4 * size_t(nm), oT50 = oOrder + 4 * size_t(nm), tablesEnd = oT50 + 4 * size_t(nm);
    const std::pair<size_t, size_t> expect[] = {
        {0x34, 0}, {0x38, 0}, {0x40, 0}, {0x44, kNone}, {0x48, oOrder}, {0x50, oT50}, {0x54, 0}, {0x58, 0},
        {0x5C, 0}, {0x60, 0}, {0x64, kNone}, {0x68, 0}, {0x70, 0x90}, {0x74, oGeom}, {0x78, kNone},
        {0x7C, oT14}, {0x80, oMesh}, {0x84, 0}, {0x88, 0}, {0x8C, oPtr}};
    for (const auto& [o, val] : expect) need(H(o) == val, "scene header offsets");
    need(B + tablesEnd <= end, "scene tables past the end");

    uint32_t total = 0;
    for (uint32_t k = 0; k < ns; ++k) {
        const size_t so = B + 0x90 + 0x60 * size_t(k), go = B + oGeom + 0x60 * size_t(k);
        need(v.u32(so) == 0 && v.u32(so + 0x10) == kNone && (v.u32(so + 0xC) == k || v.u32(so + 0xC) == kNone),
             "sector words");
        need(v.allBytes(so + 0x14, so + 0x40, 0) && v.allBytes(so + 0x50, so + 0x60, 0), "sector padding");
        need(v.u32(go) == 0x60 * ns && v.u32(go + 4) == 0 && v.u32(go + 8) == 0 && v.u32(go + 0xC) == 0x100,
             "geom header");
        need(v.u32(go + 0x1C) == 0 && v.u32(go + 0x2C) == 0, "geom bbox w");
        need(v.allBytes(go + 0x30, go + 0x48, 0) && v.allBytes(go + 0x50, go + 0x5C, 0), "geom padding");
        Sector sec;
        sec.key = v.u32(so + 4), sec.flags = v.u32(so + 8), sec.index = v.u32(so + 0xC);
        sec.sphere = v.floats<4>(so + 0x40);
        sec.bmin = v.floats<3>(go + 0x10), sec.bmax = v.floats<3>(go + 0x20);
        sec.firstMesh = v.u32(go + 0x48), sec.meshCount = v.u32(go + 0x4C), sec.geom5c = v.u32(go + 0x5C);
        need(sec.firstMesh == total && sec.meshCount <= nm - total, "geom mesh range");
        total += sec.meshCount;
        sc.sectors.push_back(sec);
    }
    need(total == nm, "geoms do not cover the meshes");
    need(v.allBytes(B + oT14, B + oMesh, 0), "per-sector records not zero");
    std::vector<bool> seen(nm, false);
    for (uint32_t i = 0; i < nm; ++i) {
        need(v.u32(B + oPtr + 4 * i) == 0xEAEAEAEAu, "mesh pointer table");
        need(v.u32(B + oT50 + 4 * i) == kNone, "second mesh table");
        uint32_t o = v.u32(B + oOrder + 4 * i);
        need(o < nm && !seen[o], "mesh order table not a permutation");
        seen[o] = true;
        sc.meshOrder.push_back(o);
    }

    struct Block {
        size_t a, b;
        bool skin;
        uint32_t mesh;
    };
    std::vector<Block> blocks;
    for (uint32_t i = 0; i < nm; ++i) {
        const size_t mo = B + oMesh + 0x80 * size_t(i);
        auto M = [&](size_t o) { return v.u32(mo + o); };
        Mesh m;
        m.sphere = v.floats<4>(mo);
        m.word10 = M(0x10), m.material = M(0x14), m.word18 = M(0x18), m.vertexFlags = M(0x1C);
        m.word58 = M(0x58), m.word68 = M(0x68), m.word78 = M(0x78);
        const uint32_t ni = M(0x24) >> 16, nv = M(0x24) & 0xFFFF;
        need(M(0x20) == 0xFF000000u && M(0x28) == 6, "mesh words 0x20/0x28");
        need(v.allBytes(mo + 0x2C, mo + 0x40, 0), "mesh padding");
        need(M(0x44) == kNone && M(0x48) == kNone && M(0x50) == 0 && M(0x54) == 0 && M(0x64) == 0 && M(0x6C) == 0 &&
                 M(0x70) == 0 && M(0x74) == 0 && M(0x7C) == 0,
             "mesh reserved words");
        m.decl = vertexDeclaration(m.vertexFlags);
        const bool skinned = M(0x60) != kNone;
        need(skinned == (kind == Kind::Skin), "skin stream presence");
        if (skinned) {
            need((m.vertexFlags & 0x80) && declStride(m.decl, 0) == 24 && m.word78 == 0x05000000 &&
                     (m.word68 & 0xFFFF) == 0,
                 "skin mesh vertex flags");
            m.stride = declStride(m.decl, 1);
        } else {
            need(!(m.vertexFlags & 0x80) && m.word68 == 0, "mesh vertex flags");
            m.stride = declStride(m.decl, 0);
        }
        need(M(0x4C) == m.stride * nv, "vertex block size");
        const size_t vo = B + M(0x40), io = B + M(0x5C);
        need(M(0x40) < end && io == vo + 0x20 + M(0x4C), "index block placement");
        v.check(io, io + 0x20 + 2 * size_t(ni));
        for (int k = 0; k < 16; ++k) need(data[io + k] == data[vo + 31 - k], "index descriptor mirror");
        for (uint32_t k = 0; k < ni; ++k) {
            uint16_t x = v.u16(io + 0x20 + 2 * k);
            need(x < nv || x == 0x7FFF, "index out of range");
            m.indices.push_back(x);
        }
        blocks.push_back({vo, io + 0x20 + 2 * size_t(ni), false, i});

        // Vertex attributes.
        m.positions.resize(nv);
        auto has = [&](uint8_t usage, uint8_t index) {
            for (const VertexElement& e : m.decl)
                if (e.usage == usage && e.index == index) return true;
            return false;
        };
        if (has(0, 1)) m.positions2.resize(nv);
        if (has(3, 0)) m.normals.resize(nv);
        for (uint8_t k = 0; k < 2; ++k) {
            if (has(6, k)) m.tangents[k].resize(nv);
            if (has(7, k)) m.binormals[k].resize(nv);
        }
        if (has(10, 0)) m.colors.resize(nv);
        if (has(1, 0) || skinned) m.blendWeights.resize(nv);
        if (has(2, 0) || skinned) m.blendIndices.resize(nv);
        for (uint8_t k = 0; k < 8; ++k)
            if (has(5, k)) m.texcoords[k].resize(nv);
        for (uint32_t vi = 0; vi < nv; ++vi)
            for (const VertexElement& e : m.decl)
                if (!(skinned && e.stream == 0)) decodeElement(v, vo + 0x20 + size_t(vi) * m.stride + e.offset, e, m, vi);

        if (skinned) {  // CPU-skinning block: groups of vertices by bone count
            const size_t sk = B + M(0x60);
            for (int k = 0; k < 4; ++k) need(v.u32(sk + 4 * k) == 0xCAFEBAB4u, "no 0xCAFEBAB4");
            uint32_t counts[4];
            for (int k = 0; k < 4; ++k) counts[k] = v.u32(sk + 16 + 4 * k);
            need(counts[0] == 0, "skin group count 0");
            for (int k = 0; k < 4; ++k) need(v.u32(io + 16 + 4 * k) == counts[3 - k], "skin index descriptor");
            size_t q = sk + 0x20;
            uint32_t vi = 0;
            for (uint32_t nb = 1; nb <= 3; ++nb) {
                for (uint32_t g = 0; g < counts[nb]; ++g) {
                    const uint32_t gn = v.u32(q);
                    need(gn > 0 && gn <= nv - vi, "skin group size");
                    need(v.u32(q + 8) == 0xFACEF000u && v.u32(q + 12) == 0xFACEF001u, "skin group markers");
                    SkinGroup sg;
                    sg.count = gn;
                    std::array<uint8_t, 4> bones{data[q + 4], data[q + 5], data[q + 6], data[q + 7]};
                    for (uint32_t b = nb; b < 4; ++b) need(bones[b] == 0, "skin group bone padding");
                    sg.bones.assign(bones.begin(), bones.begin() + nb);
                    q += 16;
                    for (uint32_t k = 0; k < gn; ++k, ++vi, q += 32) {
                        need(v.u32(q + 28) == 0xBAADF00Du, "skin vertex marker");
                        m.positions[vi] = v.floats<3>(q);
                        const uint32_t w = v.u32(q + 12);
                        const float w0 = float(w & 0xFFFF) / 65535.f, w1 = float(w >> 16) / 65535.f;
                        m.blendWeights[vi] = nb == 1   ? std::array<float, 4>{1, 0, 0, 0}
                                             : nb == 2 ? std::array<float, 4>{w0, w1, 0, 0}
                                                       : std::array<float, 4>{w0, w1, std::max(0.f, 1.f - w0 - w1), 0};
                        m.blendIndices[vi] = bones;
                        // Stream-0 elements after the position sit 4 bytes later (the weights word).
                        for (const VertexElement& e : m.decl)
                            if (e.stream == 0 && e.usage != 0) decodeElement(v, q + e.offset + 4, e, m, vi);
                    }
                    m.skinGroups.push_back(std::move(sg));
                }
            }
            need(vi == nv, "skin groups do not cover the vertices");
            blocks.push_back({sk, q, true, i});
        }
        for (const auto& p3 : m.positions)
            for (float x : p3) need(std::isfinite(x), "non-finite position");
        sc.meshes.push_back(std::move(m));
    }

    // Every byte between the blocks is padding.
    std::sort(blocks.begin(), blocks.end(), [](const Block& a, const Block& b) { return a.a < b.a; });
    size_t pos = B + tablesEnd;
    bool first = true;
    for (const Block& bl : blocks) {
        need(bl.a >= pos, "blocks overlap");
        if (bl.skin) {
            const size_t zl = 2 * size_t(sc.meshes[bl.mesh].word68 >> 16);
            need(bl.a == B + align(pos + zl - B, 0x1000), "skin block alignment");
            need(v.allBytes(pos, pos + zl, 0) && v.allBytes(pos + zl, bl.a, 0xEE), "padding before a skin block");
        } else if (first) {
            need(bl.a == B + align(pos - B, 32) && v.allBytes(pos, bl.a, 0xAA), "padding after the tables");
        } else {
            need(bl.a == align(pos, 16) && v.allBytes(pos, bl.a, 0), "padding before a vertex block");
        }
        first = false;
        pos = bl.b;
    }
    if (blocks.empty())
        need(end == B + align(pos - B, 32) && v.allBytes(pos, end, 0xAA), "empty scene padding");
    else
        need(end == align(pos, 16) && v.allBytes(pos, end, 0), "tail padding");
    return sc;
}

Havok parseHavok(const uint8_t* data, size_t size) {
    View v{data, size};
    static const uint8_t magic[8] = {0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10};
    need(size >= 0x40 && std::memcmp(data, magic, 8) == 0, "hkc: no Havok packfile magic");
    need(v.u32(8) == 0x786E6B68 && v.u32(12) == 4 && v.u32(16) == 0x04000001, "hkc: packfile header");
    static const char ver[] = "Havok-4.6.0-r1";
    need(std::memcmp(data + 40, ver, sizeof ver) == 0 && v.allBytes(40 + sizeof ver, 64, 0xFF), "hkc: version");
    const uint32_t nsec = v.u32(20), csec = v.u32(24), coff = v.u32(28), cnsec = v.u32(32), cnoff = v.u32(36);
    need(nsec == 3 && csec == 1 && cnsec == 0, "hkc: section indices");
    Havok hk;
    struct Sec {
        uint32_t start, off[6];
    };
    std::vector<Sec> secs;
    size_t pos = 0x40 + 0x30 * size_t(nsec);
    static const char* const names[3] = {"__classnames__", "__data__", "__types__"};
    for (uint32_t k = 0; k < nsec; ++k) {
        const size_t o = 0x40 + 0x30 * size_t(k);
        const size_t n = std::strlen(names[k]);
        need(std::memcmp(data + o, names[k], n) == 0 && v.allBytes(o + n, o + 19, 0) && data[o + 19] == 0xFF,
             "hkc: section name");
        Sec s;
        s.start = v.u32(o + 20);
        for (int j = 0; j < 6; ++j) s.off[j] = v.u32(o + 24 + 4 * j);  // data (= local fixups), global, virtual, exports, imports, end
        need(s.start >= pos && v.allBytes(pos, s.start, 0xFF), "hkc: padding before a section");
        for (int j = 0; j < 5; ++j) need(s.off[j] <= s.off[j + 1], "hkc: section offsets");
        need(size_t(s.start) + s.off[5] <= size, "hkc: section past the end");
        secs.push_back(s);
        hk.sections.push_back({names[k], s.start, s.off[0], s.off[5]});
        pos = size_t(s.start) + s.off[5];
    }
    need(v.allBytes(pos, size, 0xFF), "hkc: file tail");
    // Class names: u32 signature, 0x09, name, NUL.
    std::vector<std::pair<uint32_t, std::string>> classes;
    {
        const size_t a = secs[0].start, b = a + secs[0].off[0];
        size_t p = a;
        while (p < b && data[p] != 0xFF) {
            need(p + 5 <= b && data[p + 4] == 0x09, "hkc: class name entry");
            size_t z = p + 5;
            while (z < b && data[z]) ++z;
            need(z < b, "hkc: class name not terminated");
            classes.push_back({uint32_t(p + 5 - a), std::string(reinterpret_cast<const char*>(data + p + 5), z - p - 5)});
            p = z + 1;
        }
        need(v.allBytes(p, b, 0xFF), "hkc: class name padding");
    }
    auto className = [&](uint32_t off) -> const std::string* {
        for (const auto& c : classes)
            if (c.first == off) return &c.second;
        return nullptr;
    };
    need(className(cnoff) != nullptr, "hkc: contents class");
    hk.contentsClass = *className(cnoff);
    need(coff < secs[1].off[0], "hkc: contents offset");
    for (uint32_t si = 0; si < nsec; ++si) {
        const Sec& s = secs[si];
        const uint32_t dsize = s.off[0];
        for (int kind = 0; kind < 3; ++kind) {  // local (2 words), global (3), virtual (3)
            const uint32_t lo = s.off[kind], hi = s.off[kind + 1], width = kind == 0 ? 2 : 3;
            need((hi - lo) % 4 == 0 && hi % 16 == 0, "hkc: fixup table size");
            const uint32_t words = (hi - lo) / 4, full = words - words % width;
            for (uint32_t w = full; w < words; ++w) need(v.u32(s.start + lo + 4 * w) == kNone, "hkc: fixup tail");
            bool done = false;
            for (uint32_t w = 0; w < full; w += width) {
                const size_t o = s.start + lo + 4 * size_t(w);
                const uint32_t r0 = v.u32(o), r1 = v.u32(o + 4), r2 = width == 3 ? v.u32(o + 8) : kNone;
                if (r0 == kNone) {
                    need(r1 == kNone && r2 == kNone, "hkc: fixup padding");
                    done = true;
                    continue;
                }
                need(!done && r0 + 4 <= dsize, "hkc: fixup source");
                if (kind == 0) {
                    need(r1 < dsize, "hkc: local fixup target");
                } else if (kind == 1) {
                    need(r1 < nsec && r2 < secs[r1].off[0], "hkc: global fixup target");
                } else {
                    need(r1 == 0 && className(r2) != nullptr, "hkc: virtual fixup class");
                    if (si == 1) hk.objectClasses.push_back(*className(r2));
                }
            }
        }
        need(s.off[3] == s.off[4] && s.off[4] == s.off[5], "hkc: exports / imports present");
    }
    return hk;
}

}  // namespace scene
