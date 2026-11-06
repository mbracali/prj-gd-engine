// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "bcdec" image backend: block-compressed textures. Raw BC1-BC7 blocks
// and DDS files through bcdec; KTX 1 and KTX 2 files through KTX-Software
// (libktx, the read-only library): BCn levels decoded by bcdec, Basis
// Universal (ETC1S / UASTC) transcoded to RGBA, ASTC decoded by libktx's
// astc-encoder, plain RGBA / BGRA / RGB copied. Not supported: raw ETC1 / ETC2
// levels (libktx's ETC decoder, lib/etcdec.cxx, is not open source and is left
// out of our build [C: KTX-Software LICENSE.md, "Special Cases"]).
#include <algorithm>
#include <cstdio>
#include <cstring>

#include "backends.h"
#include "third_party_impl.h"

#if GDCORE_IMAGE_KTX
#include <ktx.h>
#endif

namespace gd::core::image::detail {

namespace {

uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

bool startsWith(const void* data, size_t size, const void* magic, size_t n) {
    return size >= n && std::memcmp(data, magic, n) == 0;
}
const uint8_t kKtx1[12] = {0xAB, 'K', 'T', 'X', ' ', '1', '1', 0xBB, '\r', '\n', 0x1A, '\n'};
const uint8_t kKtx2[12] = {0xAB, 'K', 'T', 'X', ' ', '2', '0', 0xBB, '\r', '\n', 0x1A, '\n'};

// Raw pixel layouts a container may hold uncompressed.
enum class Plain { RGBA8, BGRA8, RGB8, BGR8 };

void decodePlain(Plain layout, int w, int h, const uint8_t* src, size_t rowPitch, Image& out) {
    out.width = w;
    out.height = h;
    out.format = PixelFormat::RGBA8;
    out.pixels.resize(size_t(w) * size_t(h) * 4);
    const size_t in = (layout == Plain::RGB8 || layout == Plain::BGR8) ? 3 : 4;
    const bool swap = layout == Plain::BGRA8 || layout == Plain::BGR8;
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + size_t(y) * rowPitch;
        uint8_t* d = &out.pixels[size_t(y) * size_t(w) * 4];
        for (int x = 0; x < w; ++x, s += in, d += 4) {
            d[0] = swap ? s[2] : s[0];
            d[1] = s[1];
            d[2] = swap ? s[0] : s[2];
            d[3] = in == 4 ? s[3] : 255;
        }
    }
}

size_t plainSize(Plain layout, int w, int h) {
    return size_t(w) * size_t(h) * ((layout == Plain::RGB8 || layout == Plain::BGR8) ? 3 : 4);
}

class BcdecDecoder final : public Decoder {
public:
    std::string_view id() const override { return "bcdec"; }

    bool canDecode(const void* data, size_t size) const override {
        if (!data) return false;
        if (startsWith(data, size, "DDS ", 4)) return true;
#if GDCORE_IMAGE_KTX
        if (startsWith(data, size, kKtx1, 12) || startsWith(data, size, kKtx2, 12)) return true;
#endif
        return false;
    }

    bool decode(const void* data, size_t size, Image& out, std::string& error, const DecodeOptions& options) override {
        if (!data || !size) {
            error = "bcdec: no data";
            return false;
        }
        if (startsWith(data, size, "DDS ", 4)) return decodeDds(static_cast<const uint8_t*>(data), size, out, error, options);
        if (startsWith(data, size, kKtx1, 12) || startsWith(data, size, kKtx2, 12)) {
#if GDCORE_IMAGE_KTX
            return decodeKtx(static_cast<const uint8_t*>(data), size, out, error, options);
#else
            error = "bcdec: KTX support (libktx) was not built for this platform";
            return false;
#endif
        }
        error = "bcdec: not a DDS or KTX file";
        return false;
    }

    bool decodeBlocks(BlockFormat format, int width, int height, const void* data, size_t size, Image& out,
                      std::string& error, const DecodeOptions& options) override {
        if (!validSize(width, height, 4, error)) {
            error = "bcdec: " + error;
            return false;
        }
        if (!data || size < blockDataSize(format, width, height)) {
            error = "bcdec: " + std::to_string(size) + " bytes, " + std::to_string(blockDataSize(format, width, height)) +
                    " needed for " + std::to_string(width) + "x" + std::to_string(height);
            return false;
        }
        const auto* src = static_cast<const uint8_t*>(data);
        const size_t blockBytes = blockDataSize(format, 4, 4);
        const int bw = (width + 3) / 4, bh = (height + 3) / 4;
        const bool hdr = format == BlockFormat::BC6H_UF16 || format == BlockFormat::BC6H_SF16;
        Image img;
        img.width = width;
        img.height = height;
        img.format = hdr ? PixelFormat::RGBA32F : PixelFormat::RGBA8;
        img.pixels.assign(size_t(width) * size_t(height) * img.bytesPerPixel(), 0);
        uint8_t px8[16 * 4];
        float pxf[16 * 3];
        for (int by = 0; by < bh; ++by) {
            for (int bx = 0; bx < bw; ++bx, src += blockBytes) {
                // One block into px8 (RGBA8, 4x4) or pxf (RGB / RG / R floats).
                switch (format) {
                    case BlockFormat::BC1: bcdecRgba8(1, src, px8, 16); break;
                    case BlockFormat::BC2: bcdecRgba8(2, src, px8, 16); break;
                    case BlockFormat::BC3: bcdecRgba8(3, src, px8, 16); break;
                    case BlockFormat::BC7: bcdecRgba8(7, src, px8, 16); break;
                    case BlockFormat::BC4: {
                        uint8_t r[16];
                        bcdecBc4(src, r, 4);
                        for (int i = 0; i < 16; ++i) setPx(px8, i, r[i], 0, 0, 255);
                        break;
                    }
                    case BlockFormat::BC5: {
                        uint8_t rg[32];
                        bcdecBc5(src, rg, 8);
                        for (int i = 0; i < 16; ++i) setPx(px8, i, rg[i * 2], rg[i * 2 + 1], 0, 255);
                        break;
                    }
                    case BlockFormat::BC4_SNORM: {
                        float r[16];
                        bcdecBc4Snorm(src, r, 4);
                        for (int i = 0; i < 16; ++i) setPx(px8, i, snorm8(r[i]), 0, 0, 255);
                        break;
                    }
                    case BlockFormat::BC5_SNORM: {
                        float rg[32];
                        bcdecBc5Snorm(src, rg, 8);
                        for (int i = 0; i < 16; ++i) setPx(px8, i, snorm8(rg[i * 2]), snorm8(rg[i * 2 + 1]), 0, 255);
                        break;
                    }
                    case BlockFormat::BC6H_UF16:
                    case BlockFormat::BC6H_SF16:
                        bcdecBc6h(src, pxf, 12, format == BlockFormat::BC6H_SF16);
                        break;
                }
                for (int y = 0; y < 4 && by * 4 + y < height; ++y) {
                    for (int x = 0; x < 4 && bx * 4 + x < width; ++x) {
                        const size_t at = size_t(by * 4 + y) * size_t(width) + size_t(bx * 4 + x);
                        if (hdr) {
                            float rgba[4] = {pxf[(y * 4 + x) * 3], pxf[(y * 4 + x) * 3 + 1], pxf[(y * 4 + x) * 3 + 2], 1.0f};
                            std::memcpy(&img.pixels[at * 16], rgba, 16);
                        } else {
                            std::memcpy(&img.pixels[at * 4], &px8[(y * 4 + x) * 4], 4);
                        }
                    }
                }
            }
        }
        if (hdr && !options.keepFloat) floatToRgba8(img, out);
        else out = std::move(img);
        return true;
    }

private:
    static void setPx(uint8_t* px, int i, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        px[i * 4] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = a;
    }
    static uint8_t snorm8(float v) {
        if (!(v > -1.0f)) v = -1.0f;
        if (v > 1.0f) v = 1.0f;
        return uint8_t((v + 1.0f) * 127.5f + 0.5f);
    }

    // ---- DDS ----
    // "DDS ", DDS_HEADER (124 bytes: height at 12, width at 16, mip count at
    // 28, pixel format at 76 (flags 80, fourCC 84, bit count 88, masks
    // 92..104), caps2 at 112), then DDS_HEADER_DXT10 (20 bytes) when the
    // fourCC is "DX10". Data: per array element (per cube face), every mip
    // level in turn [C: Microsoft's DDS reference].
    bool decodeDds(const uint8_t* d, size_t size, Image& out, std::string& error, const DecodeOptions& o) {
        if (size < 128 || le32(d + 4) != 124) {
            error = "bcdec: DDS header damaged";
            return false;
        }
        const int h = int(le32(d + 12)), w = int(le32(d + 16));
        const uint32_t mips = std::max<uint32_t>(1, le32(d + 28));
        const uint32_t pfFlags = le32(d + 80), bits = le32(d + 88);
        const uint32_t caps2 = le32(d + 112);
        size_t offset = 128;
        uint32_t faces = (caps2 & 0x200) ? 6 : 1, layers = 1;
        bool compressed = true, opaque = false;
        BlockFormat bf = BlockFormat::BC1;
        Plain plain = Plain::RGBA8;
        if (le32(d + 24) > 1 && (caps2 & 0x200000)) {
            error = "bcdec: DDS volume textures are not supported";
            return false;
        }
        if (pfFlags & 0x4) {  // DDPF_FOURCC
            const std::string cc(reinterpret_cast<const char*>(d + 84), 4);
            if (cc == "DX10") {
                if (size < 148) {
                    error = "bcdec: DDS DX10 header damaged";
                    return false;
                }
                offset = 148;
                const uint32_t dxgi = le32(d + 128);
                if (le32(d + 136) & 0x4) faces = 6;  // a cube map
                layers = std::max<uint32_t>(1, le32(d + 140));
                if (!fromDxgi(dxgi, bf, plain, compressed)) {
                    error = "bcdec: DDS DXGI format " + std::to_string(dxgi) + " is not supported";
                    return false;
                }
            } else if (cc == "DXT1") bf = BlockFormat::BC1;
            else if (cc == "DXT2" || cc == "DXT3") bf = BlockFormat::BC2;
            else if (cc == "DXT4" || cc == "DXT5") bf = BlockFormat::BC3;
            else if (cc == "ATI1" || cc == "BC4U") bf = BlockFormat::BC4;
            else if (cc == "BC4S") bf = BlockFormat::BC4_SNORM;
            else if (cc == "ATI2" || cc == "BC5U") bf = BlockFormat::BC5;
            else if (cc == "BC5S") bf = BlockFormat::BC5_SNORM;
            else {
                error = "bcdec: DDS fourCC \"" + cc + "\" is not supported";
                return false;
            }
        } else if ((pfFlags & 0x40) && (bits == 32 || bits == 24)) {  // DDPF_RGB
            compressed = false;
            const uint32_t rmask = le32(d + 92);
            const bool rFirst = rmask == 0x000000FF;  // else 0x00FF0000: BGR(A) in memory
            if (bits == 32) plain = rFirst ? Plain::RGBA8 : Plain::BGRA8;
            else plain = rFirst ? Plain::RGB8 : Plain::BGR8;
            opaque = bits == 32 && !(pfFlags & 0x1);  // no DDPF_ALPHAPIXELS: the 4th byte is padding
        } else {
            error = "bcdec: DDS pixel format not supported";
            return false;
        }
        if (w <= 0 || h <= 0 || o.level >= mips || o.face >= faces || o.layer >= layers) {
            error = "bcdec: DDS has no image at level " + std::to_string(o.level) + ", layer " + std::to_string(o.layer) +
                    ", face " + std::to_string(o.face);
            return false;
        }
        auto levelSize = [&](uint32_t l) {
            const int lw = std::max(1, w >> l), lh = std::max(1, h >> l);
            return compressed ? blockDataSize(bf, lw, lh) : plainSize(plain, lw, lh);
        };
        size_t chain = 0;
        for (uint32_t l = 0; l < mips; ++l) chain += levelSize(l);
        offset += chain * (size_t(o.layer) * faces + o.face);
        for (uint32_t l = 0; l < o.level; ++l) offset += levelSize(l);
        const size_t need = levelSize(o.level);
        if (offset > size || need > size - offset) {
            error = "bcdec: DDS is shorter than its header says";
            return false;
        }
        const int lw = std::max(1, w >> o.level), lh = std::max(1, h >> o.level);
        if (compressed) return decodeBlocks(bf, lw, lh, d + offset, need, out, error, o);
        const size_t px = (plain == Plain::RGB8 || plain == Plain::BGR8) ? 3 : 4;
        decodePlain(plain, lw, lh, d + offset, size_t(lw) * px, out);
        if (opaque)
            for (size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 255;
        return true;
    }

    static bool fromDxgi(uint32_t dxgi, BlockFormat& bf, Plain& plain, bool& compressed) {
        compressed = true;
        switch (dxgi) {
            case 70: case 71: case 72: bf = BlockFormat::BC1; return true;
            case 73: case 74: case 75: bf = BlockFormat::BC2; return true;
            case 76: case 77: case 78: bf = BlockFormat::BC3; return true;
            case 79: case 80: bf = BlockFormat::BC4; return true;
            case 81: bf = BlockFormat::BC4_SNORM; return true;
            case 82: case 83: bf = BlockFormat::BC5; return true;
            case 84: bf = BlockFormat::BC5_SNORM; return true;
            case 94: case 95: bf = BlockFormat::BC6H_UF16; return true;
            case 96: bf = BlockFormat::BC6H_SF16; return true;
            case 97: case 98: case 99: bf = BlockFormat::BC7; return true;
            case 27: case 28: case 29: compressed = false; plain = Plain::RGBA8; return true;   // R8G8B8A8
            case 87: case 90: case 91: compressed = false; plain = Plain::BGRA8; return true;   // B8G8R8A8
            default: return false;
        }
    }

#if GDCORE_IMAGE_KTX
    // ---- KTX 1 / KTX 2 (libktx) ----
    struct KtxFree {
        void operator()(ktxTexture* t) const {
            if (t) ktxTexture_Destroy(t);
        }
    };

    // Vulkan formats (KTX 2) [C: vulkan_core.h VkFormat values].
    static bool fromVk(uint32_t vk, BlockFormat& bf, Plain& plain, bool& compressed) {
        compressed = true;
        switch (vk) {
            case 131: case 132: case 133: case 134: bf = BlockFormat::BC1; return true;
            case 135: case 136: bf = BlockFormat::BC2; return true;
            case 137: case 138: bf = BlockFormat::BC3; return true;
            case 139: bf = BlockFormat::BC4; return true;
            case 140: bf = BlockFormat::BC4_SNORM; return true;
            case 141: bf = BlockFormat::BC5; return true;
            case 142: bf = BlockFormat::BC5_SNORM; return true;
            case 143: bf = BlockFormat::BC6H_UF16; return true;
            case 144: bf = BlockFormat::BC6H_SF16; return true;
            case 145: case 146: bf = BlockFormat::BC7; return true;
            case 37: case 43: compressed = false; plain = Plain::RGBA8; return true;
            case 44: case 50: compressed = false; plain = Plain::BGRA8; return true;
            case 23: case 29: compressed = false; plain = Plain::RGB8; return true;
            case 30: case 36: compressed = false; plain = Plain::BGR8; return true;
            default: return false;
        }
    }
    // OpenGL internal formats (KTX 1) [C: GL_EXT_texture_compression_s3tc,
    // GL_EXT_texture_sRGB, ARB_texture_compression_rgtc / bptc].
    static bool fromGl(uint32_t gl, BlockFormat& bf, Plain& plain, bool& compressed) {
        compressed = true;
        switch (gl) {
            case 0x83F0: case 0x83F1: case 0x8C4C: case 0x8C4D: bf = BlockFormat::BC1; return true;
            case 0x83F2: case 0x8C4E: bf = BlockFormat::BC2; return true;
            case 0x83F3: case 0x8C4F: bf = BlockFormat::BC3; return true;
            case 0x8DBB: bf = BlockFormat::BC4; return true;
            case 0x8DBC: bf = BlockFormat::BC4_SNORM; return true;
            case 0x8DBD: bf = BlockFormat::BC5; return true;
            case 0x8DBE: bf = BlockFormat::BC5_SNORM; return true;
            case 0x8E8F: bf = BlockFormat::BC6H_UF16; return true;
            case 0x8E8E: bf = BlockFormat::BC6H_SF16; return true;
            case 0x8E8C: case 0x8E8D: bf = BlockFormat::BC7; return true;
            case 0x8058: case 0x8C43: case 0x1908: compressed = false; plain = Plain::RGBA8; return true;  // RGBA8, SRGB8_ALPHA8, RGBA
            case 0x8051: case 0x8C41: case 0x1907: compressed = false; plain = Plain::RGB8; return true;   // RGB8, SRGB8, RGB
            default: return false;
        }
    }
    static bool isEtc(uint32_t vkOrGl, bool isVk) {
        return isVk ? (vkOrGl >= 147 && vkOrGl <= 156) : ((vkOrGl >= 0x9270 && vkOrGl <= 0x9279) || vkOrGl == 0x8D64);
    }
    static bool isAstcVk(uint32_t vk) { return vk >= 157 && vk <= 184; }

    bool decodeKtx(const uint8_t* d, size_t size, Image& out, std::string& error, const DecodeOptions& o) {
        ktxTexture* raw = nullptr;
        KTX_error_code rc = ktxTexture_CreateFromMemory(d, size, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &raw);
        std::unique_ptr<ktxTexture, KtxFree> tex(raw);
        if (rc != KTX_SUCCESS || !tex) {
            error = std::string("bcdec: KTX: ") + ktxErrorString(rc);
            return false;
        }
        uint32_t format = 0;
        const bool isVk = tex->classId == ktxTexture2_c;
        if (isVk) {
            auto* t2 = reinterpret_cast<ktxTexture2*>(tex.get());
            if (ktxTexture2_NeedsTranscoding(t2)) {  // Basis Universal ETC1S / UASTC
                rc = ktxTexture2_TranscodeBasis(t2, KTX_TTF_RGBA32, 0);
                if (rc != KTX_SUCCESS) {
                    error = std::string("bcdec: KTX 2 Basis transcoding: ") + ktxErrorString(rc);
                    return false;
                }
            } else if (isAstcVk(t2->vkFormat)) {
                rc = ktxTexture2_DecodeAstc(t2);
                if (rc != KTX_SUCCESS) {
                    error = std::string("bcdec: KTX 2 ASTC decoding: ") + ktxErrorString(rc);
                    return false;
                }
            }
            format = t2->vkFormat;
        } else {
            format = reinterpret_cast<ktxTexture1*>(tex.get())->glInternalformat;
        }
        BlockFormat bf = BlockFormat::BC1;
        Plain plain = Plain::RGBA8;
        bool compressed = true;
        if (!(isVk ? fromVk(format, bf, plain, compressed) : fromGl(format, bf, plain, compressed))) {
            error = std::string("bcdec: KTX ") + (isVk ? "VkFormat " : "GL format 0x");
            if (isVk) error += std::to_string(format);
            else {
                char hex[16];
                std::snprintf(hex, sizeof hex, "%X", format);
                error += hex;
            }
            error += isEtc(format, isVk) ? ": ETC is not supported (libktx's ETC decoder is not open source)"
                                         : " is not supported";
            return false;
        }
        if (tex->baseDepth > 1) {
            error = "bcdec: KTX 3D textures are not supported";
            return false;
        }
        if (o.level >= tex->numLevels || o.layer >= tex->numLayers || o.face >= tex->numFaces) {
            error = "bcdec: KTX has no image at level " + std::to_string(o.level) + ", layer " + std::to_string(o.layer) +
                    ", face " + std::to_string(o.face);
            return false;
        }
        ktx_size_t offset = 0;
        rc = ktxTexture_GetImageOffset(tex.get(), o.level, o.layer, o.face, &offset);
        if (rc != KTX_SUCCESS) {
            error = std::string("bcdec: KTX: ") + ktxErrorString(rc);
            return false;
        }
        const int w = int(std::max<uint32_t>(1, tex->baseWidth >> o.level));
        const int h = int(std::max<uint32_t>(1, tex->baseHeight >> o.level));
        const size_t imageSize = ktxTexture_GetImageSize(tex.get(), o.level);
        const uint8_t* data = ktxTexture_GetData(tex.get());
        if (!data || offset > tex->dataSize || imageSize > tex->dataSize - offset) {
            error = "bcdec: KTX image data damaged";
            return false;
        }
        if (compressed) return decodeBlocks(bf, w, h, data + offset, imageSize, out, error, o);
        const size_t pitch = ktxTexture_GetRowPitch(tex.get(), o.level);  // KTX 1 rows are 4-byte aligned
        const size_t px = (plain == Plain::RGB8 || plain == Plain::BGR8) ? 3 : 4;
        if (pitch < size_t(w) * px || pitch * size_t(h - 1) + size_t(w) * px > imageSize) {
            error = "bcdec: KTX image data damaged";
            return false;
        }
        decodePlain(plain, w, h, data + offset, pitch, out);
        return true;
    }
#endif
};

}  // namespace

std::unique_ptr<Decoder> createBcdec() { return std::make_unique<BcdecDecoder>(); }

}  // namespace gd::core::image::detail
