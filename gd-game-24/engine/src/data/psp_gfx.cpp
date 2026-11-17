// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/psp_gfx.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace gd24 {

namespace {
uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
float f32(const uint8_t* p) {
    const uint32_t u = le32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
bool nameChar(uint8_t c) { return std::isalnum(c) || c == '_'; }
}  // namespace

bool decodeTexture(const HeapChunk& c, uint32_t address, PspTexture& tex) {
    if (!c.contains(address, 0x60)) return false;
    const uint32_t o = address - c.base;
    const uint8_t* t = c.data + o;
    if (le32(t) != 0x54) return false;
    const int w = le16(t + 4), h = le16(t + 6), bpp = t[0x0E];
    if ((bpp != 4 && bpp != 8) || w < 8 || h < 8 || w > 512 || h > 512 || (w & (w - 1)) || (h & (h - 1))) return false;
    if (t[0x20] != 1 || (1 << t[0x21]) != w || (1 << t[0x22]) != h) return false;
    if (!nameChar(t[0x3D])) return false;
    const uint32_t stride = uint32_t(w * bpp / 8), pixels = stride * uint32_t(h), colours = 1u << bpp;
    if (o + 0x50 + pixels + colours * 4 > c.size) return false;
    tex = PspTexture{};
    tex.address = address;
    tex.width = w, tex.height = h, tex.bpp = bpp;
    for (const uint8_t* p = t + 0x3D; p < c.data + c.size && nameChar(*p); ++p) tex.name.push_back(char(*p));
    const uint8_t* px = t + 0x50;
    const uint8_t* pal = px + pixels;
    tex.rgba.resize(size_t(w) * size_t(h) * 4);
    const uint32_t blocksX = stride / 16;
    for (int y = 0; y < h; ++y)
        for (uint32_t bx = 0; bx < stride; ++bx) {
            // Swizzled: 16-byte x 8-row blocks, row-major blocks.
            const uint32_t blk = uint32_t(y / 8) * blocksX + bx / 16;
            const uint8_t b = px[(blk * 8 + uint32_t(y % 8)) * 16 + bx % 16];
            for (int k = 0; k < (bpp == 4 ? 2 : 1); ++k) {
                const int idx = bpp == 4 ? (k ? b >> 4 : b & 15) : b;
                const int x = int(bx) * (bpp == 4 ? 2 : 1) + k;
                uint8_t* d = &tex.rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
                std::memcpy(d, pal + idx * 4, 4);
                d[3] = uint8_t(std::min(255, d[3] * 2));  // PSP alpha 0x80 = opaque
            }
        }
    return true;
}

std::vector<PspTexture> findTextures(const HeapChunk& c) {
    std::vector<PspTexture> out;
    PspTexture tex;
    for (uint32_t o = 0; o + 0x60 <= c.size; o += 4)
        if (decodeTexture(c, c.base + o, tex)) out.push_back(std::move(tex));
    return out;
}

bool decodeMesh(const HeapChunk& c, uint32_t address, PspMesh& m, bool strict) {
    if (!c.contains(address, 0x40)) return false;
    const uint32_t o = address - c.base;
    const uint8_t* h = c.data + o;
    const uint32_t size = le32(h), nv = le32(h + 4), np = le32(h + 8), voff = le32(h + 16);
    if (!nv || nv > 20000 || !np || np > 256 || voff < 0x20 || voff > 0x400 || size > 0x100000 || o + size > c.size) return false;
    // The words between vary ({0, 1, -1} on most props; {1, 1, -1, 0x20} on
    // the track plane's pieces; skinned meshes keep bone data there): a
    // search over raw memory wants the common form only.
    if (strict && (le32(h + 12) != 0 || le32(h + 20) != 1 || (le32(h + 24) != 0xFFFFFFFF && le32(h + 28) != 0xFFFFFFFF))) return false;
    // The display list: VTYPE, PRIM..., RET.
    uint32_t p = o + 0x1C;
    while (p < o + voff && (le32(c.data + p) >> 24) != 0x12) p += 4;
    if (p >= o + voff) return false;
    m = PspMesh{};
    m.address = address;
    m.vtype = le32(c.data + p) & 0xFFFFFF;
    // The material colour before the list: 0x58 ambient alpha, 0x55 ambient
    // colour (BGR), the GE's colour for vertices without one [C data: beat bars
    // c4c4c4, the track icon alpha c0, most others ffffff].
    for (uint32_t q = p; q >= o + 0x24 && q + 8 > p; q -= 4) {
        const uint32_t cmd = le32(c.data + q - 4);
        if (cmd >> 24 == 0x55) m.material = (m.material & 0xFF000000u) | (cmd & 0xFFFFFF);
        else if (cmd >> 24 == 0x58) m.material = (m.material & 0xFFFFFFu) | (cmd & 0xFF) << 24;
    }
    int total = 0;
    // Skinned meshes have one list per bone group ({VTYPE, PRIM..., RET}
    // again) [C data: plate_4a's five strips in two lists]: read up to the vertices.
    for (p += 4; p + 4 <= o + voff; p += 4) {
        const uint32_t cmd = le32(c.data + p);
        if (cmd >> 24 == 0x04) m.prims.push_back({int((cmd >> 16) & 7), int(cmd & 0xFFFF)}), total += int(cmd & 0xFFFF);
        else if (cmd >> 24 == 0x12 && (cmd & 0xFFFFFF) != m.vtype) return false;
    }
    if (total != int(nv) || m.prims.size() != np) return false;
    // The GE vertex layout.
    const uint32_t vt = m.vtype;
    const int tc = vt & 3, col = (vt >> 2) & 7, nrm = (vt >> 5) & 3, pos = (vt >> 7) & 3, wt = (vt >> 9) & 3,
              wc = int((vt >> 14) & 7) + 1;
    static const int kSize[4] = {0, 1, 2, 4};
    int off = 0, maxs = 1, toff = -1, coff = -1, poff = -1;
    auto align = [&](int s) { off = (off + s - 1) / s * s, maxs = std::max(maxs, s); };
    if (wt) align(kSize[wt]), off += kSize[wt] * wc;
    if (tc) align(kSize[tc]), toff = off, off += 2 * kSize[tc];
    if (col) {
        const int s = col == 7 ? 4 : 2;
        align(s), coff = off, off += s;
    }
    if (nrm) align(kSize[nrm]), off += 3 * kSize[nrm];
    if (!pos) return false;
    align(kSize[pos]), poff = off, off += 3 * kSize[pos];
    const int stride = (off + maxs - 1) / maxs * maxs;
    if (o + voff + nv * uint32_t(stride) > c.size) return false;
    auto comp = [&](const uint8_t* q, int type, bool sign) -> float {
        if (type == 1) return sign ? float(int8_t(*q)) / 127.0f : float(*q) / 128.0f;
        if (type == 2) return sign ? float(int16_t(le16(q))) / 32767.0f : float(le16(q)) / 32768.0f;
        return f32(q);
    };
    for (uint32_t i = 0; i < nv; ++i) {
        const uint8_t* q = c.data + o + voff + i * uint32_t(stride);
        PspVertex v;
        v.color = m.material;
        if (toff >= 0) v.u = comp(q + toff, tc, false), v.v = comp(q + toff + kSize[tc], tc, false);
        if (coff >= 0) {
            // GE colours: 4 BGR565, 5 ABGR1555, 6 ABGR4444, 7 ABGR8888 (R in the low bits).
            const uint32_t c = col == 7 ? le32(q + coff) : le16(q + coff);
            auto x = [](uint32_t v, int bits) { return (v * 255u + ((1u << bits) - 1) / 2) / ((1u << bits) - 1); };
            if (col == 7) v.color = c;
            else if (col == 4) v.color = x(c & 31, 5) | x((c >> 5) & 63, 6) << 8 | x((c >> 11) & 31, 5) << 16 | 0xFF000000u;
            else if (col == 5) v.color = x(c & 31, 5) | x((c >> 5) & 31, 5) << 8 | x((c >> 10) & 31, 5) << 16 | (c >> 15 ? 0xFF000000u : 0);
            else if (col == 6) v.color = x(c & 15, 4) | x((c >> 4) & 15, 4) << 8 | x((c >> 8) & 15, 4) << 16 | x(c >> 12, 4) << 24;
        }
        v.x = comp(q + poff, pos, true), v.y = comp(q + poff + kSize[pos], pos, true), v.z = comp(q + poff + 2 * kSize[pos], pos, true);
        m.vertices.push_back(v);
    }
    // The name: the last identifier in the 0x60 bytes before the block [I].
    for (uint32_t k = std::min<uint32_t>(o, 0x60); k > 0; --k) {
        const uint8_t* s = c.data + o - k;
        if (nameChar(*s) && (o - k == 0 || !nameChar(s[-1]))) {
            std::string n;
            for (const uint8_t* e = s; e < h && nameChar(*e); ++e) n.push_back(char(*e));
            if (n.size() >= 3 && std::isalpha(static_cast<unsigned char>(n[0]))) m.name = n;
        }
    }
    return true;
}

std::vector<PspMesh> findMeshes(const HeapChunk& c) {
    std::vector<PspMesh> out;
    PspMesh m;
    for (uint32_t o = 0; o + 0x40 <= c.size; o += 4)
        if (decodeMesh(c, c.base + o, m, true)) out.push_back(std::move(m));
    return out;
}

}  // namespace gd24
