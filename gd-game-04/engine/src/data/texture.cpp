// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/texture.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gh3tex {
namespace {

constexpr uint32_t kTexMagic = 0xFACECAA7;
constexpr uint16_t kImgVersion = 0x0A28;
constexpr size_t kImgHeader = 0x28, kD3dHeader = 0x34, kImgData = 0x1000;
constexpr uint32_t kD3dFixed[7] = {0x00200003, 1, 0, 0, 0, 0xFFFF0000, 0xFFFF0000};
enum Dim { Dim2D = 1, Dim3D = 2, DimCube = 3 };

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error("gh3 texture: " + what); }

struct FormatInfo {
    uint8_t gpu;           // GPUTEXTUREFORMAT
    const char* name;
    int blockW, blockBytes;
    uint8_t bits;          // image header byte 0x15
    uint8_t code, code2;   // image header byte 0x16 (DXT1 also 2 on 22 textures)
};
constexpr FormatInfo kFormats[] = {
    {0x06, "8_8_8_8", 1, 4, 32, 0, 0},  {0x12, "DXT1", 4, 8, 4, 1, 2},  {0x13, "DXT3", 4, 16, 4, 2, 2},
    {0x14, "DXT5", 4, 16, 8, 5, 5},     {0x1F, "16_16_FLOAT", 1, 4, 32, 0, 0}, {0x31, "DXN", 4, 16, 8, 6, 6},
};

const FormatInfo& formatInfo(uint8_t gpu) {
    for (const auto& f : kFormats)
        if (f.gpu == gpu) return f;
    fail("texture format " + std::to_string(gpu));
}

size_t align(size_t x, size_t a) { return (x + a - 1) / a * a; }
int npot(int x) { int p = 1; while (p < x) p *= 2; return p; }

// GPUTEXTURE_FETCH_CONSTANT (the D3D header's last six words).
struct Fetch {
    bool tiled = false;
    int pitch = 0;  // texels
    uint8_t fmt = 0, endian = 0;
    int width = 0, height = 0, depth = 1, dim = 0, levels = 1;
    int swizzle[4] = {0, 1, 2, 3};
    uint32_t mipAddress = 0;  // bytes
};

Fetch parseFetch(const uint32_t w[6]) {
    Fetch f;
    if ((w[0] & 3) != 2) fail("fetch constant type");
    const uint32_t sign = (w[0] >> 2) & 0xFF;
    if (((w[0] >> 10) & 0x1FF) || ((w[0] >> 19) & 7)) fail("clamp / dim bits");
    f.pitch = int((w[0] >> 22) & 0x1FF) * 32;
    f.tiled = (w[0] >> 31) != 0;
    f.fmt = uint8_t(w[1] & 0x3F);
    f.endian = uint8_t((w[1] >> 6) & 3);
    if (w[1] >> 8) fail("base address / request bits");
    f.dim = int((w[5] >> 9) & 3);
    if (f.dim == Dim3D) {
        f.width = int(w[2] & 0x7FF) + 1;
        f.height = int((w[2] >> 11) & 0x7FF) + 1;
        f.depth = int(w[2] >> 22) + 1;
    } else {
        f.width = int(w[2] & 0x1FFF) + 1;
        f.height = int((w[2] >> 13) & 0x1FFF) + 1;
        f.depth = int(w[2] >> 26) + 1;
    }
    for (int i = 0; i < 4; ++i) {
        f.swizzle[i] = int((w[3] >> (1 + 3 * i)) & 7);
        if (f.swizzle[i] > 5) fail("swizzle");
    }
    if ((w[3] >> 13) || (sign != 0 && sign != 0x55)) fail("filter / sign bits");
    if ((w[4] & 3) || (w[4] >> 10) || ((w[4] >> 2) & 0xF)) fail("lod / min mip bits");
    f.levels = int((w[4] >> 6) & 0xF) + 1;
    if (w[5] & 0x1FF) fail("dword5 low bits");
    if (!((w[5] >> 11) & 1)) fail("mips not packed");
    f.mipAddress = (w[5] >> 12) << 12;
    const FormatInfo& fi = formatInfo(f.fmt);
    if (f.endian != (fi.blockW == 4 ? 1 : 2)) fail("endian");
    if (f.dim == 0 || (f.dim == Dim2D && f.depth != 1) || (f.dim == DimCube && f.depth != 6)) fail("dimension");
    return f;
}

// Byte offset of block (x, y) in a Xenos-tiled surface `pitch` blocks wide (a
// multiple of 32): 32x32-block macro tiles, the bank / pipe interleave inside.
uint32_t tiledOffset(uint32_t x, uint32_t y, uint32_t pitch, uint32_t lb) {
    const uint32_t macro = ((x >> 5) + (y >> 5) * (pitch >> 5)) << (lb + 7);
    const uint32_t micro = ((x & 7) + ((y & 6) << 2)) << lb;
    const uint32_t off = macro + ((micro & ~15u) << 1) + (micro & 15) + ((y & 8) << (3 + lb)) + ((y & 1) << 4);
    return ((off & ~511u) << 3) + ((off & 448) << 2) + (off & 63) + ((y & 16) << 7) +
           (((((y & 8) >> 2) + (x >> 3)) & 3) << 6);
}

// Byte offset of block (x, y, z) in a Xenos-tiled volume of `pitch` x `height`
// blocks per slice (multiples of 32): 32x16x4-block macro tiles laid out x,
// then y, then z; 8x8x4-block micro tiles inside, the bank flipped by z bit 2
// (XGAddress3DTiledOffset as Xenia's texture_address.h Tiled3D writes it,
// github.com/xenia-project/xenia src/xenia/gpu/texture_address.h at 95a5c3e;
// checked on the disc's one volume: its 32 slices decode seamless) [C data].
uint32_t tiled3dOffset(uint32_t x, uint32_t y, uint32_t z, uint32_t pitch, uint32_t height, uint32_t lb) {
    const uint32_t outer = ((((z >> 2) * (height >> 4) + (y >> 4)) * (pitch >> 5)) + (x >> 5)) << 7;
    const uint32_t inner = ((z & 3) << 5) | (((y >> 1) & 3) << 3) | (x & 7);
    const uint32_t oib = (outer | inner) << lb;
    const uint32_t bank = ((y >> 3) ^ (z >> 2)) & 1;
    const uint32_t pipe = ((x >> 3) & 3) ^ (bank << 1);
    return ((y & 1) << 4) | (pipe << 6) | (bank << 11) | (oib & 15) | (((oib >> 4) & 1) << 5) | (((oib >> 5) & 7) << 8) |
           ((oib >> 8) << 12);
}

struct Level {
    int w = 0, h = 0, wb = 0, hb = 0;
    size_t addr = 0;
    int pitch = 0;  // blocks
    bool tiled = true;
    int ox = 0, oy = 0;  // blocks, inside the packed tail
    bool derived = false;  // cube 1x1: not stored, box-filtered from 2x2
    int z = -1;            // a volume's slice (3D tiled), -1 for 2D / cube levels
    int height3d = 0;      // a volume's slice height in blocks, aligned to 32
};

// Level offsets in the packed mip tail (texels): wide textures stack down, the
// others across; levels under 4 texels on that side go the other way at 4x.
void tailOffset(int w, int h, bool wide, int& ox, int& oy) {
    if (wide) {
        if (h >= 4) ox = 0, oy = h; else ox = 4 * w, oy = 0;
    } else {
        if (w >= 4) ox = w, oy = 0; else ox = 0, oy = 4 * h;
    }
}

// levels[level * faces + face]
std::vector<Level> layout(const Fetch& f, size_t base, size_t mips) {
    const FormatInfo& fi = formatInfo(f.fmt);
    const int bw = fi.blockW, bpb = fi.blockBytes;
    const int faces = f.dim == DimCube ? 6 : 1;
    std::vector<Level> out;
    size_t addr = mips;
    bool haveTail = false;
    size_t tailAddr = 0;
    int tailPitch = 0;
    int tailK = 1;  // the tail's levels sit where a 16-texel first level's would [C data: white, 4x4 at x 16]
    for (int i = 0; i < f.levels; ++i) {
        const int w = i == 0 ? f.width : std::max(1, npot(f.width) >> i);
        const int h = i == 0 ? f.height : std::max(1, npot(f.height) >> i);
        const int wb = (w + bw - 1) / bw, hb = (h + bw - 1) / bw;
        if (i == 0 && f.tiled && f.pitch != int(align(size_t(wb), 32)) * bw) fail("pitch");
        if (f.tiled && std::min(w, h) <= 16) {
            if (!haveTail) {
                haveTail = true;
                tailAddr = i == 0 ? base : addr;
                tailPitch = int(align(size_t(wb), 32));
                tailK = std::max(1, 16 / npot(std::min(w, h)));
            }
            for (int face = 0; face < faces; ++face) {
                Level L{w, h, wb, hb, tailAddr, tailPitch, true, 0, 0, false};
                if (faces == 1) {
                    tailOffset(w * tailK, h * tailK, f.width > f.height, L.ox, L.oy);
                    L.ox /= bw, L.oy /= bw;
                } else {
                    // cube tails: each level's faces stacked down one column;
                    // 2x2 faces at (16, 8) over the 16x16 faces' first columns;
                    // 1x1 faces not found, made from the 2x2 ones [C data]
                    if (bw != 1) fail("compressed cube map");
                    if (w >= 4) L.ox = w, L.oy = face * h;
                    else if (w == 2) L.ox = 16, L.oy = 8 + face * h;
                    else L.derived = true;
                }
                out.push_back(L);
            }
        } else {
            int pitch;
            if (!f.tiled) {
                if (i) fail("mips of a linear texture");
                pitch = f.pitch / bw;
            } else {
                pitch = int(align(size_t(wb), 32));
            }
            const size_t fsize = align(size_t(pitch) * align(size_t(hb), 32) * size_t(bpb), 4096);
            const size_t a = i == 0 ? base : addr;
            for (int face = 0; face < faces; ++face)
                out.push_back(Level{w, h, wb, hb, a + size_t(face) * fsize, pitch, f.tiled, 0, 0, false});
            if (i) addr += fsize * size_t(faces);
        }
    }
    return out;
}

float halfToFloat(uint16_t h) {
    const int e = (h >> 10) & 31, m = h & 1023;
    float v = e == 0 ? std::ldexp(float(m), -24) : e == 31 ? INFINITY : std::ldexp(float(m + 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}

uint8_t unorm(float v) { return uint8_t(std::clamp(std::lround(v * 255.0f), 0L, 255L)); }

// Untiles one level and converts it to the device format.
Bytes readLevel(const uint8_t* d, size_t size, const Level& L, const Fetch& f, gfx::TexFormat& outFmt) {
    const FormatInfo& fi = formatInfo(f.fmt);
    const size_t bpb = size_t(fi.blockBytes);
    const uint32_t lb = fi.blockBytes == 4 ? 2 : fi.blockBytes == 8 ? 3 : 4;
    Bytes raw(size_t(L.wb) * size_t(L.hb) * bpb);
    for (int y = 0; y < L.hb; ++y)
        for (int x = 0; x < L.wb; ++x) {
            const size_t o =
                L.addr + (L.z >= 0 ? tiled3dOffset(uint32_t(x), uint32_t(y), uint32_t(L.z), uint32_t(L.pitch), uint32_t(L.height3d), lb)
                          : L.tiled ? tiledOffset(uint32_t(x + L.ox), uint32_t(y + L.oy), uint32_t(L.pitch), lb)
                                    : (size_t(y + L.oy) * size_t(L.pitch) + size_t(x + L.ox)) * bpb);
            if (o + bpb > size) fail("texel data outside the file");
            uint8_t* dst = &raw[(size_t(y) * size_t(L.wb) + size_t(x)) * bpb];
            if (fi.blockW == 4)  // 16-bit big-endian words
                for (size_t k = 0; k < bpb; k += 2) dst[k] = d[o + k + 1], dst[k + 1] = d[o + k];
            else  // one 32-bit big-endian word
                for (size_t k = 0; k < 4; ++k) dst[k] = d[o + 3 - k];
        }
    switch (f.fmt) {
    case 0x12: outFmt = gfx::TexFormat::BC1; return raw;
    case 0x14: outFmt = gfx::TexFormat::BC3; return raw;
    case 0x31: outFmt = gfx::TexFormat::BC5; return raw;
    default: break;
    }
    outFmt = gfx::TexFormat::RGBA8;
    const size_t n = size_t(L.wb) * size_t(L.hb);
    Bytes rgba(size_t(L.w) * size_t(L.h) * 4);
    if (f.fmt == 0x13) {  // DXT3: explicit 4-bit alpha + BC1 colour (no BC2 on the device)
        const int W = L.wb * 4;
        Bytes full(size_t(W) * size_t(L.hb) * 16);
        for (size_t b = 0; b < n; ++b) {
            const uint8_t* blk = &raw[b * 16];
            const uint16_t c0 = uint16_t(blk[8] | blk[9] << 8), c1 = uint16_t(blk[10] | blk[11] << 8);
            uint8_t pal[4][3];
            auto ex = [](uint16_t c, uint8_t* o) {
                o[0] = uint8_t(((c >> 11) * 255 + 15) / 31);
                o[1] = uint8_t((((c >> 5) & 63) * 255 + 31) / 63);
                o[2] = uint8_t(((c & 31) * 255 + 15) / 31);
            };
            ex(c0, pal[0]);
            ex(c1, pal[1]);
            for (int k = 0; k < 3; ++k) {
                pal[2][k] = uint8_t((2 * pal[0][k] + pal[1][k] + 1) / 3);
                pal[3][k] = uint8_t((pal[0][k] + 2 * pal[1][k] + 1) / 3);
            }
            const uint32_t idx = uint32_t(blk[12]) | uint32_t(blk[13]) << 8 | uint32_t(blk[14]) << 16 | uint32_t(blk[15]) << 24;
            const size_t bx = b % size_t(L.wb), by = b / size_t(L.wb);
            for (int i = 0; i < 16; ++i) {
                uint8_t* p = &full[((by * 4 + size_t(i / 4)) * size_t(W) + bx * 4 + size_t(i % 4)) * 4];
                std::copy_n(pal[(idx >> (2 * i)) & 3], 3, p);
                p[3] = uint8_t(((blk[i / 2] >> (4 * (i & 1))) & 15) * 17);
            }
        }
        for (int y = 0; y < L.h; ++y) std::copy_n(&full[size_t(y) * size_t(W) * 4], size_t(L.w) * 4, &rgba[size_t(y) * size_t(L.w) * 4]);
        return rgba;
    }
    for (size_t i = 0; i < size_t(L.w) * size_t(L.h); ++i) {
        const size_t src = (i / size_t(L.w)) * size_t(L.wb) + i % size_t(L.w);
        const uint8_t* t = &raw[src * 4];  // little-endian word now: component k = byte k
        uint8_t comp[4];
        if (f.fmt == 0x06) {
            std::copy_n(t, 4, comp);
        } else {  // 16_16_FLOAT, clamped to [0, 1]
            comp[0] = unorm(halfToFloat(uint16_t(t[0] | t[1] << 8)));
            comp[1] = unorm(halfToFloat(uint16_t(t[2] | t[3] << 8)));
            comp[2] = comp[3] = 0;
        }
        for (int c = 0; c < 4; ++c) {
            const int s = f.swizzle[c];
            rgba[i * 4 + size_t(c)] = s < 4 ? comp[s] : s == 4 ? 0 : 255;
        }
    }
    return rgba;
}

Bytes boxFilter(const Bytes& src, int w, int h, int w2, int h2) {
    const int fx = w / w2, fy = h / h2, n = fx * fy;
    Bytes out(size_t(w2) * size_t(h2) * 4);
    for (int y = 0; y < h2; ++y)
        for (int x = 0; x < w2; ++x)
            for (int c = 0; c < 4; ++c) {
                int t = 0;
                for (int j = 0; j < fy; ++j)
                    for (int i = 0; i < fx; ++i) t += src[(size_t(y * fy + j) * size_t(w) + size_t(x * fx + i)) * 4 + size_t(c)];
                out[(size_t(y) * size_t(w2) + size_t(x)) * 4 + size_t(c)] = uint8_t((t + n / 2) / n);
            }
    return out;
}

// One image header at `pos`; data offsets count from `rel`. `img`: a lone .img.
Texture readImage(const uint8_t* d, size_t size, size_t pos, bool img, size_t rel) {
    ByteReader r(d, size);
    r.seek(pos);
    const uint16_t ver = r.u16be();
    const uint8_t flags = r.u8(), usage = r.u8();
    const uint32_t key = r.u32be();
    uint16_t dims[6];
    for (auto& v : dims) v = r.u16be();
    const uint8_t levels = r.u8(), bits = r.u8(), code = r.u8(), pad = r.u8();
    const uint32_t mipOff = r.u32be(), hdr = r.u32be(), dataSize = r.u32be(), baseOff = r.u32be();
    if (ver != kImgVersion) fail("image version");
    if (img ? flags != 0x11 : (flags != 0x02 && flags != 0x12)) fail("image flags");
    // Class 2: the images fonts embed (.fnt) [C data] are laid out like an .img.
    if ((img && usage && usage != 2) || usage > 5 || pad) fail("image class");
    if (dims[0] != dims[3] || dims[1] != dims[4] || dims[2] != dims[5]) fail("size fields differ");
    if (size_t(hdr) + kD3dHeader > size || (img && hdr != kImgHeader)) fail("D3D header offset");
    r.seek(hdr);
    uint32_t words[13];
    for (auto& w : words) w = r.u32be();
    for (int i = 0; i < 7; ++i)
        if (words[i] != kD3dFixed[i]) fail("D3D header");
    const Fetch f = parseFetch(words + 7);
    const FormatInfo& fi = formatInfo(f.fmt);
    if (f.width != dims[0] || f.height != dims[1] || (f.dim == Dim3D ? f.depth : 1) != dims[2]) fail("size vs fetch constant");
    if (levels != f.levels || bits != fi.bits || (code != fi.code && code != fi.code2)) fail("levels / bits / compression");
    if (baseOff == 0xFFFFFFFF) fail("no texel data");
    const size_t base = rel + baseOff;
    size_t mips;
    if (img) {
        // [C 0x82415fd8] data page + 0; mips = base + the fetch constant's mip address
        if (mipOff || baseOff) fail("img data offsets");
        mips = base + f.mipAddress;
    } else {
        // [C 0x82382d88] offsets from the dictionary's start
        if (mipOff == 0xFFFFFFFF) fail("no mip offset");
        mips = rel + mipOff;
        if (f.mipAddress && (f.dim != DimCube || mips != base + f.mipAddress)) fail("mip address");
    }
    (void)dataSize;  // .img: data size x levels; .tex: allocator growth (docs)

    Texture t;
    t.key = key;
    t.width = f.width;
    t.height = f.height;
    t.mips = f.levels;
    t.sourceFormat = fi.name;
    t.usage = usage;
    if (f.dim == Dim3D) {
        // The disc's one volume (global_gfx.tex #49, DXT5 128x128x32, one
        // level, VolumeMap material only): its slices, 3D tiled. The device
        // has no volume kind, so it stays unsupported for upload.
        if (!f.tiled || f.levels != 1) fail("volume layout");
        t.depth = f.depth;
        t.supported = false;
        const int wb = (f.width + fi.blockW - 1) / fi.blockW, hb = (f.height + fi.blockW - 1) / fi.blockW;
        for (int z = 0; z < f.depth; ++z) {
            Level L{f.width, f.height, wb, hb, base, int(align(size_t(wb), 32)), true, 0, 0, false, z, int(align(size_t(hb), 32))};
            gfx::TexFormat fmt;
            t.levels.push_back(readLevel(d, size, L, f, fmt));
            t.format = fmt;
        }
        return t;
    }
    t.kind = f.dim == DimCube ? gfx::TexKind::Cube : gfx::TexKind::Tex2D;
    const int faces = f.dim == DimCube ? 6 : 1;
    const std::vector<Level> lv = layout(f, base, mips);
    std::vector<Bytes> byLevel(lv.size());  // [level * faces + face]
    for (size_t i = 0; i < lv.size(); ++i) {
        gfx::TexFormat fmt;
        if (lv[i].derived) {
            const Level& up = lv[i - size_t(faces)];
            byLevel[i] = boxFilter(byLevel[i - size_t(faces)], up.w, up.h, lv[i].w, lv[i].h);
            fmt = gfx::TexFormat::RGBA8;
        } else {
            byLevel[i] = readLevel(d, size, lv[i], f, fmt);
        }
        t.format = fmt;
    }
    t.levels.resize(lv.size());
    for (int face = 0; face < faces; ++face)
        for (int l = 0; l < f.levels; ++l)
            t.levels[size_t(face * f.levels + l)] = std::move(byLevel[size_t(l * faces + face)]);
    return t;
}

int hashBits(uint32_t n) {
    int b = 1;
    while ((1u << (b - 1)) < n && b < 8) ++b;
    return b;
}

}  // namespace

std::vector<gfx::TexLevel> Texture::texLevels() const {
    std::vector<gfx::TexLevel> v;
    v.reserve(levels.size());
    for (const Bytes& b : levels) v.push_back({b.data(), b.size()});
    return v;
}

Texture decodeImg(const uint8_t* data, size_t size) {
    if (size < kImgData) fail("short .img");
    return readImage(data, size, 0, true, kImgData);
}

std::vector<Texture> decodeTexDict(const uint8_t* data, size_t size) {
    ByteReader r(data, size);
    const uint32_t magic = r.u32be();
    const uint8_t ver = r.u8(), hsize = r.u8();
    const uint16_t count = r.u16be();
    const uint32_t table = r.u32be(), objects = r.u32be(), unused = r.u32be(), bits = r.u32be(), hash = r.u32be();
    if (magic != kTexMagic || ver != 1 || hsize != 0x1C) fail("tex header");
    if (unused != 0xFFFFFFFF || hash != 0x1C || bits != uint32_t(hashBits(count))) fail("tex hash table");
    if (table != hash + 0x1C + 12u * (1u << bits) || objects != table + count * (kImgHeader + kD3dHeader)) fail("tex table");
    std::vector<Texture> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const size_t pos = table + i * kImgHeader;
        ByteReader h(data, size);
        h.seek(pos + 0x1C);
        if (h.u32be() != table + count * kImgHeader + i * kD3dHeader) fail("tex D3D header order");
        out.push_back(readImage(data, size, pos, false, 0));
    }
    return out;
}

}  // namespace gh3tex
