// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/highway_assets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "data/str_file.h"
#include "data/umd.h"

namespace gd24 {

namespace {

// COMMON.STR's chunk load addresses (highwaytest.cpp).
constexpr uint32_t kCommonBase[2] = {0x08F36600, 0x091C32E0};
constexpr uint32_t kPtrBias = 0x80;  // pointers land 0x80 past the object

struct Heap {
    std::vector<HeapChunk> chunks;
    const HeapChunk* at(uint32_t a, uint32_t n = 4) const {
        for (const HeapChunk& c : chunks)
            if (c.contains(a, n)) return &c;
        return nullptr;
    }
    bool u32(uint32_t a, uint32_t& v) const {
        const HeapChunk* c = at(a);
        if (!c) return false;
        const uint8_t* p = c->at(a);
        v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
        return true;
    }
    float f32(uint32_t a) const {
        uint32_t v = 0;
        u32(a, v);
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    std::string name(uint32_t a) const {
        std::string s;
        for (const HeapChunk* c = at(a, 1); c && c->contains(a, 1) && s.size() < 64; ++a) {
            const char ch = char(*c->at(a));
            if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_')) break;
            s.push_back(ch);
        }
        return s;
    }
};

}  // namespace

bool HighwayAssets::load(const Umd& umd, std::string* error) {
    auto fail = [&](const std::string& why) {
        if (error) *error = why;
        return false;
    };
    std::vector<uint8_t> file;
    StrFile f;
    std::string why;
    if (!umd.read("PSP_GAME/USRDIR/STREAMS/COMMON.STR", file)) return fail("cannot read STREAMS/COMMON.STR");
    if (!f.parse(file.data(), file.size(), file.size(), &why)) return fail("COMMON.STR: " + why);
    if (f.chunks.size() < 2) return fail("COMMON.STR: fewer than two chunks");
    Heap heap;
    for (size_t i = 0; i < 2; ++i) heap.chunks.push_back({file.data() + f.chunks[i].offset, f.chunks[i].size, kCommonBase[i]});
    textures.clear(), models.clear();
    std::map<uint32_t, int> texAt;
    for (const HeapChunk& c : heap.chunks) {
        for (uint32_t o = 0; o + 0x100 <= c.size; o += 4) {
            const uint32_t a = c.base + o;
            uint32_t tag = 0, mark = 0;
            heap.u32(a, tag), heap.u32(a + 0x10, mark);
            if (tag != 0x4D || mark != 0x77359400) continue;
            HwModel m;
            uint32_t namePtr = 0, n = 0, type = 0;
            heap.u32(a + 0x68, namePtr), heap.u32(a + 0x6C, type), heap.u32(a + 0x7C, n);
            m.name = heap.name(namePtr - kPtrBias);
            m.type = int(type);
            if (m.name.empty() || n == 0 || n > 16) continue;
            m.scale = 0;
            for (int k = 0; k < 3; ++k) m.min[k] = heap.f32(a + 0x30 + 4 * uint32_t(k)), m.max[k] = heap.f32(a + 0x40 + 4 * uint32_t(k));
            for (int k = 0; k < 3; ++k) m.scale = std::max({m.scale, std::fabs(m.min[k]), std::fabs(m.max[k])});
            if (!(m.scale > 0) || !std::isfinite(m.scale)) continue;
            for (uint32_t s = 0; s < n; ++s) {
                uint32_t matPtr = 0, blockPtr = 0, flags = 0, texPtr = 0;
                heap.u32(a + 0x80 + 8 * s, matPtr), heap.u32(a + 0x84 + 8 * s, blockPtr);
                const uint32_t mat = matPtr - kPtrBias, block = blockPtr - kPtrBias;
                HwSubMesh sub;
                const HeapChunk* bc = heap.at(block, 0x40);
                if (!bc || !decodeMesh(*bc, block, sub.mesh)) continue;
                heap.u32(mat + 0x0C, flags), heap.u32(mat + 0x1C, texPtr);
                sub.flags = flags;
                sub.material = heap.name(matPtr);
                if (texPtr) {
                    const uint32_t t = texPtr - kPtrBias;
                    auto it = texAt.find(t);
                    if (it == texAt.end()) {
                        PspTexture tex;
                        const HeapChunk* tc = heap.at(t, 0x60);
                        if (tc && decodeTexture(*tc, t, tex)) {
                            textures.push_back(std::move(tex));
                            it = texAt.emplace(t, int(textures.size()) - 1).first;
                        }
                    }
                    if (it != texAt.end()) sub.textureIndex = it->second, sub.texture = textures[size_t(it->second)].name;
                }
                for (PspVertex& v : sub.mesh.vertices) v.x *= m.scale, v.y *= m.scale, v.z *= m.scale;
                m.subs.push_back(std::move(sub));
            }
            if (!m.subs.empty() && !models.count(m.name)) models.emplace(m.name, std::move(m));
        }
    }
    // The other textures (the instrument icons, Overdrive layers): every
    // texture object of the chunks, by name.
    for (const HeapChunk& c : heap.chunks)
        for (PspTexture& t : findTextures(c))
            if (!texAt.count(t.address)) texAt.emplace(t.address, int(textures.size())), textures.push_back(std::move(t));
    if (!model("track_plane_A") || !model("gem_4a")) return fail("COMMON.STR: the highway models were not found");
    return true;
}

const HwModel* HighwayAssets::model(const std::string& name) const {
    auto it = models.find(name);
    return it == models.end() ? nullptr : &it->second;
}

// The strike line's four lane marks on the track plane [C data]: the
// TrackPlaneEdge quads textured from the atlas' red / yellow / green / blue
// row (v 0.11..0.19), at y -4.55..-4.42, x centres -1.10 -0.36 0.38 1.13.
// Their centres are the lanes, their y the strike line.
bool HighwayAssets::laneMarks(float laneX[4], float& strikeY) const {
    const HwModel* m = model("track_plane_A");
    if (!m) return false;
    for (const HwSubMesh& s : m->subs) {
        if (s.material != "TrackPlaneEdge") continue;
        std::vector<std::pair<float, float>> marks;
        size_t first = 0;
        for (const PspMesh::Prim& p : s.mesh.prims) {
            if (p.count == 4 && first + 4 <= s.mesh.vertices.size()) {
                bool row = true;
                float cx = 0, cy = 0;
                for (size_t i = first; i < first + 4; ++i) {
                    const PspVertex& v = s.mesh.vertices[i];
                    row = row && v.v > 0.10f && v.v < 0.20f;
                    cx += v.x / 4, cy += v.y / 4;
                }
                if (row) marks.push_back({cx, cy});
            }
            first += size_t(p.count);
        }
        if (marks.size() != 4) return false;
        std::sort(marks.begin(), marks.end());
        strikeY = 0;
        for (int l = 0; l < 4; ++l) laneX[l] = marks[size_t(l)].first, strikeY += marks[size_t(l)].second / 4;
        return true;
    }
    return false;
}

int HighwayAssets::texture(const std::string& name) const {
    for (size_t i = 0; i < textures.size(); ++i)
        if (textures[i].name == name) return int(i);
    return -1;
}

}  // namespace gd24
