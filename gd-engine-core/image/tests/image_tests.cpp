// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gdcore_image checks. Every sample is made here, at run time: images drawn in
// memory, encoded by one backend and decoded by another; BCn / ASTC blocks and
// DDS / KTX / KTX2 containers written byte by byte from their specifications.
// Never game data.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "gd/core/image/decoder.h"
#include "selftest.h"

namespace img = gd::core::image;

namespace {

img::Image gradient(int w, int h) {
    img::Image im;
    im.width = w;
    im.height = h;
    im.pixels.resize(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &im.pixels[(size_t(y) * size_t(w) + size_t(x)) * 4];
            p[0] = uint8_t(x * 255 / (w - 1));
            p[1] = uint8_t(y * 255 / (h - 1));
            p[2] = uint8_t((x * 7 + y * 13) & 255);
            p[3] = uint8_t(255 - (x + y) % 64);
        }
    return im;
}

img::Image solid(int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    img::Image im;
    im.width = w;
    im.height = h;
    im.pixels.resize(size_t(w) * size_t(h) * 4);
    for (size_t i = 0; i < im.pixels.size(); i += 4) {
        im.pixels[i] = r;
        im.pixels[i + 1] = g;
        im.pixels[i + 2] = b;
        im.pixels[i + 3] = a;
    }
    return im;
}

bool samePixels(const img::Image& a, const img::Image& b, bool rgbOnly = false) {
    if (a.width != b.width || a.height != b.height || a.format != b.format || a.pixels.size() != b.pixels.size())
        return false;
    for (size_t i = 0; i < a.pixels.size(); ++i)
        if ((!rgbOnly || i % 4 != 3) && a.pixels[i] != b.pixels[i]) return false;
    return true;
}

double meanAbsDiff(const img::Image& a, const img::Image& b) {
    if (a.pixels.size() != b.pixels.size() || a.pixels.empty()) return 1e9;
    double s = 0;
    for (size_t i = 0; i < a.pixels.size(); ++i) s += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
    return s / double(a.pixels.size());
}

const uint8_t* px(const img::Image& im, int x, int y) { return &im.pixels[(size_t(y) * size_t(im.width) + size_t(x)) * 4]; }
bool near(int a, int b, int tol = 1) { return std::abs(a - b) <= tol; }

void put16(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    b[at] = uint8_t(v);
    b[at + 1] = uint8_t(v >> 8);
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i));
}
void put64(std::vector<uint8_t>& b, size_t at, uint64_t v) {
    for (int i = 0; i < 8; ++i) b[at + size_t(i)] = uint8_t(v >> (8 * i));
}

// A BC1 block: color0 red (0xF800), color1 blue (0x001F) (4-colour mode),
// rows of indices 0, 1, 2, 3.
std::vector<uint8_t> bc1Rows() {
    std::vector<uint8_t> b(8, 0);
    put16(b, 0, 0xF800);
    put16(b, 2, 0x001F);
    b[4] = 0x00;  // row 0: index 0
    b[5] = 0x55;  // row 1: index 1
    b[6] = 0xAA;  // row 2: index 2
    b[7] = 0xFF;  // row 3: index 3
    return b;
}

// A KTX 2 file (KTX 2.0 specification): one level, no supercompression, a
// Khronos basic data format descriptor with the given samples.
struct DfdSample {
    uint32_t bitOffset, bitLength, channel, lower, upper;
};
std::vector<uint8_t> ktx2(uint32_t vkFormat, uint32_t typeSize, int w, int h, uint8_t colorModel, uint8_t transfer,
                          uint8_t blockW, uint8_t blockH, uint8_t bytesPlane0, const std::vector<DfdSample>& samples,
                          const std::vector<uint8_t>& level, size_t align) {
    static const uint8_t id[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};
    const size_t dfdOff = 80 + 24, dfdLen = 4 + 24 + 16 * samples.size();
    size_t dataOff = dfdOff + dfdLen;
    dataOff = (dataOff + align - 1) / align * align;
    std::vector<uint8_t> f(dataOff + level.size(), 0);
    std::memcpy(f.data(), id, 12);
    put32(f, 12, vkFormat);
    put32(f, 16, typeSize);
    put32(f, 20, uint32_t(w));
    put32(f, 24, uint32_t(h));
    put32(f, 28, 0);  // depth
    put32(f, 32, 0);  // layers
    put32(f, 36, 1);  // faces
    put32(f, 40, 1);  // levels
    put32(f, 44, 0);  // supercompression
    put32(f, 48, uint32_t(dfdOff));
    put32(f, 52, uint32_t(dfdLen));
    put64(f, 80, dataOff);
    put64(f, 88, level.size());
    put64(f, 96, level.size());
    size_t d = dfdOff;
    put32(f, d, uint32_t(dfdLen));
    put32(f, d + 4, 0);                                                  // vendor 0, type 0
    put32(f, d + 8, 2u | uint32_t(24 + 16 * samples.size()) << 16);    // version 1.3, block size
    f[d + 12] = colorModel;
    f[d + 13] = 1;  // BT.709 primaries
    f[d + 14] = transfer;
    f[d + 15] = 0;
    f[d + 16] = blockW;
    f[d + 17] = blockH;
    f[d + 20] = bytesPlane0;
    for (size_t i = 0; i < samples.size(); ++i) {
        const size_t s = d + 28 + 16 * i;
        put32(f, s, samples[i].bitOffset | (samples[i].bitLength - 1) << 16 | samples[i].channel << 24);
        put32(f, s + 8, samples[i].lower);
        put32(f, s + 12, samples[i].upper);
    }
    std::memcpy(&f[dataOff], level.data(), level.size());
    return f;
}

// A KTX 1 file (KTX 1.1 specification), one level.
std::vector<uint8_t> ktx1(uint32_t glType, uint32_t glTypeSize, uint32_t glFormat, uint32_t glInternal, uint32_t glBase,
                          int w, int h, const std::vector<uint8_t>& level) {
    static const uint8_t id[12] = {0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> f(64 + 4 + level.size(), 0);
    std::memcpy(f.data(), id, 12);
    const uint32_t v[] = {0x04030201, glType, glTypeSize, glFormat, glInternal, glBase, uint32_t(w), uint32_t(h), 0, 0, 1, 1, 0};
    for (size_t i = 0; i < 13; ++i) put32(f, 12 + 4 * i, v[i]);
    put32(f, 64, uint32_t(level.size()));
    std::memcpy(&f[68], level.data(), level.size());
    return f;
}

// A DDS file: legacy fourCC or DX10 header, then the given level data.
std::vector<uint8_t> dds(int w, int h, uint32_t mips, const char* fourcc, uint32_t dxgi, const std::vector<uint8_t>& data,
                         uint32_t rgbBits = 0) {
    const bool dx10 = fourcc && std::strcmp(fourcc, "DX10") == 0;
    std::vector<uint8_t> f((dx10 ? 148 : 128) + data.size(), 0);
    std::memcpy(f.data(), "DDS ", 4);
    put32(f, 4, 124);
    put32(f, 8, 0x1007 | (mips > 1 ? 0x20000 : 0));
    put32(f, 12, uint32_t(h));
    put32(f, 16, uint32_t(w));
    put32(f, 28, mips);
    put32(f, 76, 32);
    if (fourcc) {
        put32(f, 80, 0x4);
        std::memcpy(&f[84], fourcc, 4);
    } else {
        put32(f, 80, 0x40 | 0x1);  // RGB + alpha
        put32(f, 88, rgbBits);
        put32(f, 92, 0x00FF0000);
        put32(f, 96, 0x0000FF00);
        put32(f, 100, 0x000000FF);
        put32(f, 104, 0xFF000000);
    }
    put32(f, 108, 0x1000);
    if (dx10) {
        put32(f, 128, dxgi);
        put32(f, 132, 3);  // 2D
        put32(f, 140, 1);  // one element
    }
    std::memcpy(&f[dx10 ? 148 : 128], data.data(), data.size());
    return f;
}

}  // namespace

GDCORE_TEST(image_factory) {
    auto ids = img::decoderIds();
    GDCORE_CHECK(ids.size() == 3 && ids[0] == "stb" && ids[1] == "bcdec" && ids[2] == "sdl3");
    for (const auto& id : ids) {
        std::string err;
        auto d = img::createDecoder(id, &err);
        GDCORE_CHECK_MSG(d != nullptr, id + ": " + err);
        if (d) GDCORE_CHECK(d->id() == id);
    }
    std::string err;
    GDCORE_CHECK(img::createDecoder("local", &err) == nullptr && !err.empty());
    GDCORE_CHECK(img::createDecoder("STB") == nullptr);  // ids are lower case
    GDCORE_CHECK(img::blockDataSize(img::BlockFormat::BC1, 5, 5) == 4 * 8);
    GDCORE_CHECK(img::blockDataSize(img::BlockFormat::BC7, 8, 4) == 2 * 16);
}

// Every encoder's PNG / TGA / BMP read back exactly by every decoder.
GDCORE_TEST(image_roundtrip_stb_sdl3) {
    auto stb = img::createDecoder("stb");
    auto sdl = img::createDecoder("sdl3");
    auto bc = img::createDecoder("bcdec");
    if (!stb || !sdl || !bc) {
        GDCORE_CHECK(false);
        return;
    }
    const img::Image src = gradient(37, 23);
    for (auto* enc : {stb.get(), sdl.get()}) {
        for (auto fmt : {img::FileFormat::PNG, img::FileFormat::TGA, img::FileFormat::BMP}) {
            const std::string what = std::string(enc->id()) + " " +
                                     (fmt == img::FileFormat::PNG ? "PNG" : fmt == img::FileFormat::TGA ? "TGA" : "BMP");
            std::vector<uint8_t> file;
            std::string err;
            GDCORE_CHECK_MSG(enc->encode(src, fmt, file, err) && !file.empty(), what + ": " + err);
            for (auto* dec : {stb.get(), sdl.get()}) {
                img::Image back;
                GDCORE_CHECK_MSG(dec->decode(file.data(), file.size(), back, err),
                                 what + " -> " + std::string(dec->id()) + ": " + err);
                // BMP's alpha is not read back the same everywhere [I]: colour only.
                GDCORE_CHECK_MSG(samePixels(src, back, fmt == img::FileFormat::BMP),
                                 what + " -> " + std::string(dec->id()) + ": pixels differ");
            }
            if (fmt == img::FileFormat::PNG) {
                GDCORE_CHECK(stb->canDecode(file.data(), file.size()));
                GDCORE_CHECK(sdl->canDecode(file.data(), file.size()));
                GDCORE_CHECK(!bc->canDecode(file.data(), file.size()));
            }
        }
        // JPEG: lossy, so close (a smooth image at quality 95).
        img::Image smooth = solid(64, 32, 0, 0, 0, 255);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 64; ++x) {
                uint8_t* p = &smooth.pixels[(size_t(y) * 64 + size_t(x)) * 4];
                p[0] = uint8_t(x * 4);
                p[1] = uint8_t(y * 8);
                p[2] = 128;
            }
        std::vector<uint8_t> jpg;
        std::string err;
        GDCORE_CHECK_MSG(enc->encode(smooth, img::FileFormat::JPEG, jpg, err, 95), err);
        for (auto* dec : {stb.get(), sdl.get()}) {
            img::Image back;
            GDCORE_CHECK_MSG(dec->decode(jpg.data(), jpg.size(), back, err), err);
            const double diff = meanAbsDiff(smooth, back);
            GDCORE_CHECK_MSG(diff < 3.0, std::string(enc->id()) + " JPEG -> " + std::string(dec->id()) +
                                             ": mean difference " + std::to_string(diff));
        }
    }
    img::Image none;
    std::string err;
    GDCORE_CHECK(!stb->decode("not an image", 12, none, err) && !err.empty());
    GDCORE_CHECK(!sdl->decode("not an image", 12, none, err));
}

// A Radiance HDR file (flat scanlines) kept as floats or brought to 8 bits.
GDCORE_TEST(image_stb_hdr) {
    std::string h = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    std::vector<uint8_t> file(h.begin(), h.end());
    // RGBE: (128, 64, 32, e=129) = (1.0, 0.5, 0.25); (128, 128, 128, e=130) = 2.0
    for (uint8_t b : {128, 64, 32, 129, 128, 128, 128, 130}) file.push_back(b);
    auto stb = img::createDecoder("stb");
    img::Image f, b;
    std::string err;
    img::DecodeOptions keep;
    keep.keepFloat = true;
    GDCORE_CHECK_MSG(stb->decode(file.data(), file.size(), f, err, keep), err);
    GDCORE_CHECK(f.format == img::PixelFormat::RGBA32F && f.width == 2 && f.height == 1);
    if (f.pixels.size() == 32) {
        float v[8];
        std::memcpy(v, f.pixels.data(), 32);
        GDCORE_CHECK(std::fabs(v[0] - 1.0f) < 0.01f && std::fabs(v[1] - 0.5f) < 0.01f && std::fabs(v[2] - 0.25f) < 0.01f);
        GDCORE_CHECK(std::fabs(v[4] - 2.0f) < 0.02f && v[7] == 1.0f);
    }
    GDCORE_CHECK_MSG(stb->decode(file.data(), file.size(), b, err), err);
    GDCORE_CHECK(b.format == img::PixelFormat::RGBA8 && b.pixels.size() == 8);
    if (b.pixels.size() == 8) GDCORE_CHECK(b.pixels[0] == 255 && b.pixels[4] == 255);  // 1.0 and 2.0 clamp to white
}

GDCORE_TEST(image_resize) {
    for (const char* id : {"stb", "sdl3"}) {
        auto d = img::createDecoder(id);
        const img::Image flat = solid(17, 9, 10, 200, 30, 255);
        for (auto filter : {img::Filter::Default, img::Filter::Linear, img::Filter::Nearest}) {
            img::Image up, down;
            std::string err;
            GDCORE_CHECK_MSG(d->resize(flat, 40, 31, up, err, filter), std::string(id) + ": " + err);
            GDCORE_CHECK(up.width == 40 && up.height == 31 && samePixels(up, solid(40, 31, 10, 200, 30, 255)));
            GDCORE_CHECK_MSG(d->resize(flat, 5, 3, down, err, filter), std::string(id) + ": " + err);
            GDCORE_CHECK(samePixels(down, solid(5, 3, 10, 200, 30, 255)));
        }
        // A 2x2 checker halved with a box-like filter is the average.
        img::Image check = solid(2, 2, 0, 0, 0, 255);
        check.pixels[0] = check.pixels[12] = 200;
        img::Image one;
        std::string err;
        GDCORE_CHECK(d->resize(check, 1, 1, one, err, img::Filter::Linear));
        if (one.pixels.size() == 4) GDCORE_CHECK_MSG(near(one.pixels[0], 100, 2), std::to_string(one.pixels[0]));
        GDCORE_CHECK(!d->resize(check, 0, 5, one, err));
    }
}

GDCORE_TEST(image_bcn_blocks) {
    auto bc = img::createDecoder("bcdec");
    img::Image out;
    std::string err;
    // BC1: rows red, blue, 2/3 red + 1/3 blue, 1/3 red + 2/3 blue.
    auto b1 = bc1Rows();
    GDCORE_CHECK_MSG(bc->decodeBlocks(img::BlockFormat::BC1, 4, 4, b1.data(), b1.size(), out, err), err);
    if (out.width == 4) {
        GDCORE_CHECK(px(out, 0, 0)[0] == 255 && px(out, 0, 0)[2] == 0 && px(out, 0, 0)[3] == 255);
        GDCORE_CHECK(px(out, 3, 1)[0] == 0 && px(out, 3, 1)[2] == 255);
        GDCORE_CHECK(near(px(out, 1, 2)[0], 170) && near(px(out, 1, 2)[2], 85));
        GDCORE_CHECK(near(px(out, 2, 3)[0], 85) && near(px(out, 2, 3)[2], 170));
    }
    // Not a multiple of 4: 6x5 is 2x2 blocks, cut.
    std::vector<uint8_t> four;
    for (int i = 0; i < 4; ++i) four.insert(four.end(), b1.begin(), b1.end());
    GDCORE_CHECK(bc->decodeBlocks(img::BlockFormat::BC1, 6, 5, four.data(), four.size(), out, err));
    GDCORE_CHECK(out.width == 6 && out.height == 5 && out.pixels.size() == 6 * 5 * 4);
    if (out.width == 6) GDCORE_CHECK(px(out, 5, 4)[0] == 255);  // row 0 of the lower right block
    GDCORE_CHECK(!bc->decodeBlocks(img::BlockFormat::BC1, 6, 5, four.data(), four.size() - 1, out, err));
    // BC3: alpha 255 / 0 endpoints, index 1 everywhere -> alpha 0; colour as BC1.
    std::vector<uint8_t> b3(16, 0);
    b3[0] = 255;
    b3[1] = 0;
    for (int i = 2; i < 8; ++i) b3[size_t(i)] = 0x49;  // 3-bit indices 1,1,1,... (001 001 001 ...)
    b3[2] = 0x49, b3[3] = 0x92, b3[4] = 0x24, b3[5] = 0x49, b3[6] = 0x92, b3[7] = 0x24;
    std::memcpy(&b3[8], b1.data(), 8);
    GDCORE_CHECK(bc->decodeBlocks(img::BlockFormat::BC3, 4, 4, b3.data(), b3.size(), out, err));
    if (out.width == 4) GDCORE_CHECK(px(out, 2, 2)[3] == 0 && px(out, 0, 0)[0] == 255);
    // BC4: endpoints 200 / 100, index 0 -> (200, 0, 0, 255).
    std::vector<uint8_t> b4(8, 0);
    b4[0] = 200;
    b4[1] = 100;
    GDCORE_CHECK(bc->decodeBlocks(img::BlockFormat::BC4, 4, 4, b4.data(), 8, out, err));
    if (out.width == 4) GDCORE_CHECK(px(out, 1, 1)[0] == 200 && px(out, 1, 1)[1] == 0 && px(out, 1, 1)[3] == 255);
    // BC5: two BC4 halves -> (200, 50, 0, 255).
    std::vector<uint8_t> b5(16, 0);
    b5[0] = 200, b5[1] = 100, b5[8] = 50, b5[9] = 10;
    GDCORE_CHECK(bc->decodeBlocks(img::BlockFormat::BC5, 4, 4, b5.data(), 16, out, err));
    if (out.width == 4) GDCORE_CHECK(px(out, 3, 3)[0] == 200 && px(out, 3, 3)[1] == 50 && px(out, 3, 3)[2] == 0);
    // BC7 mode 6: all bits after the mode set -> endpoints 255, index 15: white.
    std::vector<uint8_t> b7(16, 0xFF);
    b7[0] = 0xC0;
    GDCORE_CHECK(bc->decodeBlocks(img::BlockFormat::BC7, 4, 4, b7.data(), 16, out, err));
    if (out.width == 4) GDCORE_CHECK(px(out, 1, 2)[0] == 255 && px(out, 1, 2)[1] == 255 && px(out, 1, 2)[3] == 255);
    // BC7 mode 6 with everything zero -> transparent black.
    std::fill(b7.begin(), b7.end(), 0);
    b7[0] = 0x40;
    GDCORE_CHECK(bc->decodeBlocks(img::BlockFormat::BC7, 4, 4, b7.data(), 16, out, err));
    if (out.width == 4) GDCORE_CHECK(px(out, 0, 3)[0] == 0 && px(out, 0, 3)[3] == 0);
    // BC6H all-zero block: black, alpha 1, kept as floats on request.
    std::vector<uint8_t> b6(16, 0);
    img::DecodeOptions keep;
    keep.keepFloat = true;
    GDCORE_CHECK(bc->decodeBlocks(img::BlockFormat::BC6H_UF16, 4, 4, b6.data(), 16, out, err, keep));
    GDCORE_CHECK(out.format == img::PixelFormat::RGBA32F && out.pixels.size() == 16 * 16);
    if (out.pixels.size() == 256) {
        float v[4];
        std::memcpy(v, out.pixels.data(), 16);
        GDCORE_CHECK(v[0] == 0.0f && v[3] == 1.0f);
    }
    // Only bcdec decodes blocks.
    GDCORE_CHECK(!img::createDecoder("stb")->decodeBlocks(img::BlockFormat::BC1, 4, 4, b1.data(), 8, out, err));
}

GDCORE_TEST(image_dds) {
    auto bc = img::createDecoder("bcdec");
    img::Image out;
    std::string err;
    // DXT1, 8x8 with 2 mips: level 0 all red blocks, level 1 the row block.
    auto b1 = bc1Rows();
    std::vector<uint8_t> red(8, 0);
    put16(red, 0, 0xF800);
    put16(red, 2, 0x001F);
    std::vector<uint8_t> data;
    for (int i = 0; i < 4; ++i) data.insert(data.end(), red.begin(), red.end());
    data.insert(data.end(), b1.begin(), b1.end());
    auto file = dds(8, 8, 2, "DXT1", 0, data);
    GDCORE_CHECK(bc->canDecode(file.data(), file.size()));
    GDCORE_CHECK_MSG(bc->decode(file.data(), file.size(), out, err), err);
    GDCORE_CHECK(out.width == 8 && out.height == 8);
    if (out.width == 8) GDCORE_CHECK(px(out, 7, 7)[0] == 255 && px(out, 7, 7)[2] == 0);
    img::DecodeOptions l1;
    l1.level = 1;
    GDCORE_CHECK_MSG(bc->decode(file.data(), file.size(), out, err, l1), err);
    GDCORE_CHECK(out.width == 4 && out.height == 4);
    if (out.width == 4) GDCORE_CHECK(px(out, 0, 1)[2] == 255);
    l1.level = 2;
    GDCORE_CHECK(!bc->decode(file.data(), file.size(), out, err, l1));
    file.resize(file.size() - 1);  // short
    l1.level = 1;
    GDCORE_CHECK(!bc->decode(file.data(), file.size(), out, err, l1));
    // DX10 header, BC7 (DXGI 98).
    std::vector<uint8_t> b7(16, 0xFF);
    b7[0] = 0xC0;
    file = dds(4, 4, 1, "DX10", 98, b7);
    GDCORE_CHECK_MSG(bc->decode(file.data(), file.size(), out, err), err);
    if (out.width == 4) GDCORE_CHECK(px(out, 2, 2)[0] == 255 && px(out, 2, 2)[3] == 255);
    // Uncompressed 32-bit BGRA (masks), 2x1.
    file = dds(2, 1, 1, nullptr, 0, {30, 20, 10, 255, 3, 2, 1, 128}, 32);
    GDCORE_CHECK_MSG(bc->decode(file.data(), file.size(), out, err), err);
    if (out.width == 2) GDCORE_CHECK(px(out, 0, 0)[0] == 10 && px(out, 0, 0)[2] == 30 && px(out, 1, 0)[3] == 128);
}

GDCORE_TEST(image_ktx) {
    auto bc = img::createDecoder("bcdec");
    img::Image out;
    std::string err;
    const auto b1 = bc1Rows();
    // KTX 1, BC1 RGBA (GL_COMPRESSED_RGBA_S3TC_DXT1_EXT).
    auto k1 = ktx1(0, 1, 0, 0x83F1, 0x1908, 4, 4, b1);
    GDCORE_CHECK(bc->canDecode(k1.data(), k1.size()));
    GDCORE_CHECK_MSG(bc->decode(k1.data(), k1.size(), out, err), err);
    if (out.width == 4) GDCORE_CHECK(px(out, 0, 1)[2] == 255 && px(out, 0, 0)[0] == 255);
    // KTX 1, RGBA8.
    auto k1p = ktx1(0x1401, 1, 0x1908, 0x8058, 0x1908, 2, 1, {1, 2, 3, 4, 5, 6, 7, 8});
    GDCORE_CHECK_MSG(bc->decode(k1p.data(), k1p.size(), out, err), err);
    if (out.width == 2) GDCORE_CHECK(px(out, 1, 0)[0] == 5 && px(out, 1, 0)[3] == 8);
    // KTX 2, VK_FORMAT_R8G8B8A8_UNORM.
    auto k2p = ktx2(37, 1, 2, 1, 1, 1, 0, 0, 4,
                    {{0, 8, 0, 0, 255}, {8, 8, 1, 0, 255}, {16, 8, 2, 0, 255}, {24, 8, 15, 0, 255}},
                    {9, 8, 7, 6, 5, 4, 3, 2}, 4);
    GDCORE_CHECK_MSG(bc->decode(k2p.data(), k2p.size(), out, err), err);
    if (out.width == 2) GDCORE_CHECK(px(out, 0, 0)[0] == 9 && px(out, 1, 0)[3] == 2);
    // KTX 2, VK_FORMAT_BC1_RGBA_UNORM_BLOCK.
    auto k2 = ktx2(133, 1, 4, 4, 128, 1, 3, 3, 8, {{0, 64, 15, 0, 0xFFFFFFFFu}}, b1, 8);
    GDCORE_CHECK_MSG(bc->decode(k2.data(), k2.size(), out, err), err);
    if (out.width == 4) GDCORE_CHECK(px(out, 0, 1)[2] == 255 && px(out, 3, 0)[0] == 255);
    // KTX 2, VK_FORMAT_ASTC_4x4_UNORM_BLOCK: a void-extent (constant colour)
    // block, red, decoded by libktx's astc-encoder.
    std::vector<uint8_t> astc(16, 0);
    put64(astc, 0, 0xFFFFFFFFFFFFFDFCull);  // void extent, LDR, no extents
    put16(astc, 8, 0xFFFF);                 // R
    put16(astc, 10, 0x0000);                // G
    put16(astc, 12, 0x0000);                // B
    put16(astc, 14, 0xFFFF);                // A
    auto k2a = ktx2(157, 1, 4, 4, 162, 1, 3, 3, 16, {{0, 128, 0, 0, 0xFFFFFFFFu}}, astc, 16);
    GDCORE_CHECK_MSG(bc->decode(k2a.data(), k2a.size(), out, err), err);
    if (out.width == 4) GDCORE_CHECK(px(out, 2, 3)[0] == 255 && px(out, 2, 3)[1] == 0 && px(out, 2, 3)[3] == 255);
    // KTX 2 ETC2 (VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK): refused, saying why.
    auto k2e = ktx2(147, 1, 4, 4, 161, 1, 3, 3, 8, {{0, 64, 0, 0, 0xFFFFFFFFu}}, std::vector<uint8_t>(8, 0), 8);
    GDCORE_CHECK(!bc->decode(k2e.data(), k2e.size(), out, err));
    ctx.note("ETC2: " + err);
}
