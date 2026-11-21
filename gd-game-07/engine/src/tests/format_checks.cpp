// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "tests/format_checks.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <vector>

#include "data/qb.h"
#include "data/qbkey.h"
#include "data/anim.h"
#include "data/font.h"
#include "data/havok.h"
#include "data/scene.h"
#include "data/texture.h"

namespace {

constexpr uint32_t kQb = qbkey::key(".qb");
constexpr uint32_t kMqb = qbkey::key(".mqb");
constexpr uint32_t kSqb = qbkey::key(".sqb");

bool checkQb(const Pak& pak, const PakEntry& e, FormatStats& s, std::string& what) {
    auto span = pak.data(e);
    size_t unused = 0;
    try {
        qb::File f = qb::decode(span.first, span.second, &unused);
        ++s.qbFiles;
        s.qbSections += f.sections.size();
        for (const qb::Section& sec : f.sections)
            if (sec.value.type == qb::Type::Script) ++s.qbScripts;
        s.qbUnused += unused;
        if (f.fileKey != e.fullName && !f.sections.empty()) {
            what = "qb file key differs from the pak entry";
            return false;
        }
        if (unused) {
            what = std::to_string(unused) + " bytes not accounted for";
            return false;
        }
        return true;
    } catch (const std::exception& ex) {
        what = ex.what();
        return false;
    }
}

// Havok packfiles (.hkc, .rag): the packfile checks (data/scene.h) and the
// reflection decode (data/havok.h, GD10-015).
void checkHavok(const uint8_t* p, size_t n, FormatStats& s) {
    // gh4's .rag may be "LZSS", u32 size, Neversoft's LZSS of the packfile, as
    // the band unpacks it (render/band.cpp) [C data].
    Bytes unpacked;
    if (n >= 8 && std::memcmp(p, "LZSS", 4) == 0) {
        const size_t size = size_t(p[4]) << 24 | size_t(p[5]) << 16 | size_t(p[6]) << 8 | size_t(p[7]);
        unpacked = qb::lzss(p + 8, n - 8, size);
        p = unpacked.data(), n = unpacked.size();
    }
    // Havok 5.5.0 packfiles (file version 5: gh4's .hkc and .rag) are read by
    // havok::parse alone; GH3's packfile walk (scene::parseHavok) is version 4.
    const bool v55 = n >= 16 && p[12] == 0 && p[13] == 0 && p[14] == 0 && p[15] == 5;
    if (!v55) scene::parseHavok(p, n);
    const havok::Physics ph = havok::parse(p, n);
    s.havokBodies += ph.bodies.size();
    s.havokConstraints += ph.constraints.size();
    for (const havok::Body& b : ph.bodies)
        if (b.name.compare(0, 8, "Bone_ACC") == 0) ++s.havokAccessoryBodies;
        else if (b.shape.kind == havok::Shape::MoppMesh) s.havokTriangles += b.shape.triangles;
}

// Scenes, models, skins (one format) and Havok collision (data/scene.h).
bool checkScene(const Pak& pak, const PakEntry& e, FormatStats& st, std::string& what) {
    auto [p, n] = pak.data(e);
    try {
        if (e.type == qbkey::key(".hkc")) checkHavok(p, n, st);
        else
            scene::parse(p, n, e.type == qbkey::key(".skin") ? scene::Kind::Skin
                               : e.type == qbkey::key(".mdl") ? scene::Kind::Mdl
                                                              : scene::Kind::Scn);
        return true;
    } catch (const std::exception& ex) {
        what = ex.what();
        return false;
    }
}

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// .wav: RIFF/WAVE (little-endian) with fmt (XMA, tag 0x165) or XMA2, data
// (2048-byte packets) and seek chunks, read by FMOD's XMA codec [C data, docs/formats.md].
bool checkWav(const uint8_t* d, size_t n, std::string& what) {
    if (n < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) return what = "not RIFF/WAVE", false;
    if (le32(d + 4) + 8 != n) return what = "RIFF size", false;
    bool fmt = false, data = false;
    for (size_t o = 12; o < n;) {
        if (o + 8 > n) return what = "chunk header past the end", false;
        const uint32_t size = le32(d + o + 4);
        if (o + 8 + size > n) return what = "chunk past the end", false;
        if (std::memcmp(d + o, "fmt ", 4) == 0) {
            fmt = true;
            if (size < 2 || (d[o + 8] | d[o + 9] << 8) != 0x165) return what = "fmt is not XMA (0x165)", false;
        } else if (std::memcmp(d + o, "XMA2", 4) == 0) {
            // gh4: a big-endian XMA2WAVEFORMAT (audio/sfx.cpp riffData): version 4,
            // one stream, rate at +12, channels at +40 [C data: World Tour's 3,106 .wav].
            fmt = true;
            const uint8_t* f = d + o + 8;
            const uint32_t rate = be32(f + 12);
            if (size != 44 || f[0] != 4 || f[1] != 1 || rate < 8000 || rate > 96000 || f[40] < 1 || f[40] > 2)
                return what = "XMA2 chunk", false;
        } else if (std::memcmp(d + o, "data", 4) == 0) {
            data = true;
            if (size % 2048) return what = "data is not whole 2048-byte packets", false;
        } else if (std::memcmp(d + o, "seek", 4) != 0) {
            return what = "unknown chunk", false;
        }
        o += 8 + size + (size & 1);
    }
    if (!fmt || !data) return what = "fmt or data chunk missing", false;
    return true;
}

// .pfx: node-array prefix index: u32 node array key, u32 count, count x
// {u32 prefix key, u32 offset, u32 count}, then the u16 node indices [C data].
bool checkPfx(const uint8_t* d, size_t n, std::string& what) {
    if (n < 8) return what = "too short", false;
    const uint32_t count = be32(d + 4);
    if (8 + size_t(count) * 12 > n) return what = "table past the end", false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = d + 8 + 12 * size_t(i);
        if (be32(e + 4) + size_t(be32(e + 8)) * 2 > n) return what = "indices past the end", false;
    }
    return true;
}

}  // namespace

bool checkPakFile(const Pak& pak, const PakEntry& e, FormatStats& s, std::string& what) {
    const std::string type = qbkey::name(e.type);
    bool ok = true;
    if (e.type == kQb || e.type == kMqb || e.type == kSqb) ok = checkQb(pak, e, s, what);
    else if (e.type == qbkey::key(".scn") || e.type == qbkey::key(".mdl") || e.type == qbkey::key(".skin") ||
             e.type == qbkey::key(".hkc"))
        ok = checkScene(pak, e, s, what);
    else if (e.type == qbkey::key(".rag")) {  // Havok packfile, like .hkc [C data]
        auto [p, n] = pak.data(e);
        try {
            checkHavok(p, n, s);
        } catch (const std::exception& ex) {
            what = ex.what(), ok = false;
        }
    } else if (e.type == qbkey::key(".ska") || e.type == qbkey::key(".ske") || e.type == qbkey::key(".fnt")) {
        auto [p, n] = pak.data(e);
        try {
            if (e.type == qbkey::key(".ska")) anim::parseClip(p, n);
            else if (e.type == qbkey::key(".ske")) {
                // global_rag_skel's ragdoll skeletons: "LZSS", u32 size, Neversoft's
                // LZSS of an ordinary .ske (docs/formats.md, gd-game-10) [C data].
                if (n >= 8 && std::memcmp(p, "LZSS", 4) == 0) {
                    const size_t size = size_t(p[4]) << 24 | size_t(p[5]) << 16 | size_t(p[6]) << 8 | size_t(p[7]);
                    const Bytes u = qb::lzss(p + 8, n - 8, size);
                    anim::parseSkeleton(u.data(), u.size());
                } else {
                    anim::parseSkeleton(p, n);
                }
            }
            else {
                const font::Font f = font::parseFont(p, n);
                const gh3tex::Texture t = font::decodeFontTexture(f, p, n);  // its glyph page decodes too
                if (t.width != f.texWidth || t.height != f.texHeight) throw std::runtime_error("font page size");
            }
        } catch (const std::exception& ex) {
            what = ex.what(), ok = false;
        }
    } else if (e.type == qbkey::key(".nqb")) {  // zone node arrays: ordinary QB [C data]
        auto [p, n] = pak.data(e);
        size_t unused = 0;
        try {
            qb::decode(p, n, &unused);
            if (unused) what = "unused bytes", ok = false;
        } catch (const std::exception& ex) {
            what = ex.what(), ok = false;
        }
    } else if (e.type == qbkey::key(".img") || e.type == qbkey::key(".tex")) {  // textures (data/texture.h)
        auto [p, n] = pak.data(e);
        try {
            std::vector<gh3tex::Texture> ts;
            if (e.type == qbkey::key(".img")) ts.push_back(gh3tex::decodeImg(p, n));
            else ts = gh3tex::decodeTexDict(p, n);
            for (const gh3tex::Texture& t : ts) {
                if (t.depth <= 1) continue;  // volumes: every slice untiled (3D tiling, GD10-014)
                if (int(t.levels.size()) != t.depth) throw std::runtime_error("volume slices");
                for (const Bytes& sl : t.levels) {
                    // One slice of the decoded format: RGBA8 / R8 texels, BC1 8-byte or
                    // BC3 / BC5 16-byte blocks (World Tour: 8-bit and DXT1 volumes).
                    const size_t blocks = size_t((t.width + 3) / 4) * size_t((t.height + 3) / 4),
                                 texels = size_t(t.width) * size_t(t.height);
                    const size_t want = t.format == gfx::TexFormat::RGBA8 ? texels * 4
                                        : t.format == gfx::TexFormat::R8  ? texels
                                        : t.format == gfx::TexFormat::BC1 ? blocks * 8
                                                                          : blocks * 16;
                    if (sl.size() != want) throw std::runtime_error("volume slice size (" + t.sourceFormat + " " + std::to_string(t.width) + "x" +
                                                 std::to_string(t.height) + "x" + std::to_string(t.depth) + ", " +
                                                 std::to_string(sl.size()) + " bytes)");
                    for (uint8_t c : sl) s.volumeHash = (s.volumeHash ^ c) * 0x01000193u;  // FNV-1a, vs tools/img.py
                }
                ++s.volumes;
                s.volumeSlices += size_t(t.depth);
            }
        } catch (const std::exception& ex) {
            what = ex.what(), ok = false;
        }
    } else if (e.type == qbkey::key(".wav")) {
        auto [p, n] = pak.data(e);
        ok = checkWav(p, n, what);
    } else if (e.type == qbkey::key(".pfx")) {
        auto [p, n] = pak.data(e);
        ok = checkPfx(p, n, what);
    } else if (e.type == qbkey::key(".table")) {
        // A list of u32 words, each 0xE5F50254 or 0x21775481 (World Tour: 70 tables of
        // one or two words) [C data; meaning and names I, not used by the port].
        auto [p, n] = pak.data(e);
        if (n == 0 || n % 4) what = "unexpected .table", ok = false;
        for (size_t i = 0; ok && i < n; i += 4)
            if (be32(p + i) != 0xE5F50254u && be32(p + i) != 0x21775481u) what = "unexpected .table word", ok = false;
    } else if (e.type == qbkey::key(".dbg")) {  // the debug name text (Disc::loadNames)
        auto [p, n] = pak.data(e);
        const std::string text(reinterpret_cast<const char*>(p), n);
        if (text.find("[LineNumbers]") == std::string::npos && text.find("[Checksums]") == std::string::npos)
            what = "not a debug text", ok = false;
    } else {
        ++s.notDecoded[type];
        return true;
    }
    if (ok) ++s.decoded[type];
    return ok;
}

void printFormatStats(const FormatStats& s) {
    for (auto& [t, n] : s.decoded) std::printf("decoded      %-8s %6zu\n", t.c_str(), n);
    std::printf("havok: %zu rigid bodies (%zu accessory bones), %zu constraints, %zu collision triangles\n", s.havokBodies,
                s.havokAccessoryBodies, s.havokConstraints, s.havokTriangles);
    std::printf("volume textures %zu (%zu slices, 3D tiled, FNV-1a %08x)\n", s.volumes, s.volumeSlices, unsigned(s.volumeHash));
    for (auto& [t, n] : s.notDecoded) std::printf("not decoded  %-8s %6zu (no decoder yet)\n", t.c_str(), n);
}
