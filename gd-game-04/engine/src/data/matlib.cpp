// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/matlib.h"

#include <cstring>
#include <stdexcept>

#include "data/qbkey.h"

namespace matlib {
namespace {

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("material library: " + what); }

struct View {
    const uint8_t* d;
    size_t n;
    uint32_t u32(size_t o) const {
        if (o + 4 > n) fail("read past end");
        return uint32_t(d[o]) << 24 | uint32_t(d[o + 1]) << 16 | uint32_t(d[o + 2]) << 8 | d[o + 3];
    }
    uint16_t u16(size_t o) const {
        if (o + 2 > n) fail("read past end");
        return uint16_t(d[o] << 8 | d[o + 1]);
    }
    float f32(size_t o) const {
        const uint32_t u = u32(o);
        float f;
        std::memcpy(&f, &u, 4);
        return f;
    }
    std::string str(size_t o) const {
        std::string s;
        while (o < n && d[o]) s += char(d[o++]);
        if (o >= n) fail("unterminated string");
        return s;
    }
};

const char* const kTechniqueNames[kTechniques] = {"Default", "DepthOnly", "AlphaOnly", "DynamicShadowReciever", "ReducedLOD"};

// One compiled shader (XDK layout) at `o`; returns its size, or 0 when the
// words there are not a shader header.
size_t parseShader(const View& v, size_t o, Shader& s) {
    const uint32_t magic = v.u32(o);
    const uint32_t region = v.u32(o + 4), regionSize = v.u32(o + 8);
    const uint32_t ctabAt = v.u32(o + 0x10), litAt = v.u32(o + 0x14), infoAt = v.u32(o + 0x18);
    if (v.u32(o + 0x0c) != 0x24 || !region || region > 0x10000 || regionSize > 0x40000 || infoAt >= region || ctabAt >= region ||
        (litAt && (litAt >= infoAt || litAt <= ctabAt)))
        return 0;
    s.pixel = magic == 0x102A1100;
    // Debug info: a hash, the path's length and the path of the .updb.
    const uint32_t pathLen = v.u32(o + 0x28);
    if (pathLen == 0 || 0x2c + pathLen > ctabAt) return 0;
    std::string path(reinterpret_cast<const char*>(v.d + o + 0x2c), pathLen);
    while (!path.empty() && path.back() == 0) path.pop_back();
    const size_t slash = path.find_last_of('\\');
    s.name = slash == std::string::npos ? path : path.substr(slash + 1);
    if (s.name.size() > 5 && s.name.compare(s.name.size() - 5, 5, ".updb") == 0) s.name.resize(s.name.size() - 5);

    // The constant table (D3DXSHADER_CONSTANTTABLE, big-endian) after its size word.
    const size_t ct = o + ctabAt + 4;
    const uint32_t version = v.u32(ct + 8);
    if (v.u32(ct) != 0x1c || (version != 0xFFFE0300 && version != 0xFFFF0300)) return 0;
    if ((version == 0xFFFF0300) != s.pixel) fail(s.name + ": shader magic and constant table disagree");
    const uint32_t nConst = v.u32(ct + 12), info = v.u32(ct + 16);
    for (uint32_t k = 0; k < nConst; ++k) {
        const size_t e = ct + info + 20 * k;
        Constant c;
        c.name = v.str(ct + v.u32(e));
        c.set = uint8_t(v.u16(e + 4));
        c.reg = v.u16(e + 6);
        c.count = v.u16(e + 8);
        s.constants.push_back(std::move(c));
    }

    // Shader info: where the microcode starts in the region and how long it is,
    // then the vertex fetch and interpolator tables.
    const size_t in = o + infoAt;
    const uint32_t prefix = v.u32(in), ucodeSize = v.u32(in + 4);
    if (prefix + ucodeSize != regionSize || ucodeSize % 12) fail(s.name + ": microcode size");
    const size_t words = (region - infoAt) / 4;
    if (s.pixel) {
        if (words < 8) fail(s.name + ": pixel shader info");
        for (size_t k = 8; k < words; ++k) {
            const uint32_t w = v.u32(in + 4 * k);
            if (!w) break;
            s.links.push_back({uint8_t((w >> 8) & 0xF), uint8_t((w >> 12) & 0xF), uint8_t(w & 0xFF)});
        }
    } else {
        if (words < 10) fail(s.name + ": vertex shader info");
        const uint32_t nFetch = v.u32(in + 28), nPatch = v.u32(in + 32);
        if (10 + nFetch + nPatch > words) fail(s.name + ": vertex shader tables");
        const size_t nOut = words - 10 - nFetch - nPatch;
        for (uint32_t k = 0; k < nFetch; ++k) {
            const uint32_t w = v.u32(in + 40 + 4 * k);
            s.fetches.push_back({uint16_t(w & 0xFFF), uint8_t((w >> 12) & 0xF), uint8_t((w >> 16) & 0xF)});
        }
        for (size_t k = 0; k < nOut; ++k) {
            const uint32_t w = v.u32(in + 40 + 4 * (nFetch + k));
            s.links.push_back({uint8_t((w >> 8) & 0xF), uint8_t((w >> 12) & 0xF), uint8_t(w & 0xFF)});
        }
    }
    // Literal constants: (register << 16 | float count, byte offset in the
    // region's prefix) pairs [C: e.g. D_3ChkBox_Pass0's pixel shader reads
    // c255 = (1, -1, 0, 0) to blend its shadow map].
    if (litAt) {
        size_t p = o + litAt + 20;
        for (int r = 0; r < 4 && p + 8 <= o + infoAt; ++r, p += 8) {
            const uint32_t w = v.u32(p), off = v.u32(p + 4);
            if (!w) break;
            const uint32_t reg = (w >> 16) & 0x1FF, count = w & 0xFFFF;
            if (off + 4 * count > prefix) fail(s.name + ": literal range");
            if (s.literals.empty()) s.literalReg = uint16_t(reg & 0xFF);
            const size_t at = s.literalReg + s.literals.size() / 4;
            if ((reg & 0xFF) > at) s.literals.resize(size_t((reg & 0xFF) - s.literalReg) * 4, 0.0f);
            for (uint32_t k = 0; k < count; ++k) s.literals.push_back(v.f32(o + region + off + 4 * k));
        }
    }
    s.ucode.resize(ucodeSize / 4);
    for (size_t k = 0; k < s.ucode.size(); ++k) s.ucode[k] = v.u32(o + region + prefix + 4 * k);
    return region + regionSize;
}

}  // namespace

void Library::parse(const uint8_t* data, size_t size) {
    templates_.clear(), byKey_.clear(), shaders_.clear();
    const View v{data, size};
    if (size < 0x18 || std::memcmp(data, "MATL", 4) != 0 || v.u32(4) != 2) fail("not a MATL v2 file");
    const uint32_t count = v.u32(8);
    const size_t base = 0x18 + 4 * size_t(count);
    std::unordered_map<std::string, size_t> byName;
    for (uint32_t i = 0; i < count; ++i) {
        const size_t e = base + 0x28 * size_t(i);
        Template t;
        t.name = v.str(base + v.u32(e + 4));
        t.key = v.u32(e + 8);
        if (t.key != qbkey::key(t.name)) fail(t.name + ": key is not the name's checksum");
        byKey_[t.key] = templates_.size();
        byName[t.name] = templates_.size();
        templates_.push_back(std::move(t));
    }
    // The shaders: scan 4-byte aligned words for the two magics from the end
    // of the template records, validating each header.
    size_t o = base + 0x28 * size_t(count);
    while (o + 0x40 <= size) {
        const uint32_t w = v.u32(o);
        if (w != 0x102A1100 && w != 0x102A1101) {
            o += 4;
            continue;
        }
        Shader s;
        const size_t len = parseShader(v, o, s);
        if (!len) {
            o += 4;
            continue;
        }
        // "<template>_<technique>"
        int tech = -1;
        size_t ti = 0;
        for (int k = 0; k < kTechniques && tech < 0; ++k) {
            const std::string suffix = std::string("_") + kTechniqueNames[k];
            if (s.name.size() > suffix.size() && s.name.compare(s.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                auto it = byName.find(s.name.substr(0, s.name.size() - suffix.size()));
                if (it != byName.end()) tech = k, ti = it->second;
            }
        }
        if (tech >= 0) {
            int& slot = s.pixel ? templates_[ti].ps[tech] : templates_[ti].vs[tech];
            if (slot < 0) slot = int(shaders_.size());
        }
        shaders_.push_back(std::move(s));
        o += (len + 3) & ~size_t(3);
    }
    if (shaders_.empty()) fail("no shaders");
}

}  // namespace matlib
