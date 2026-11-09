// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// Images and textures: one interface, gd::core::image::Decoder, with a backend
// per library, chosen by id (docs/guidelines/engine-backend-guidelines.md §1):
//
//   "stb"    stb_image / stb_image_write / stb_image_resize2: PNG, JPEG, TGA,
//            BMP, PSD, GIF, HDR, PIC, PNM load; PNG, TGA, BMP, JPEG save; resize
//   "bcdec"  bcdec: raw BC1-BC7 blocks and DDS files; KTX-Software (libktx):
//            KTX 1 / KTX 2 files (BCn levels through bcdec, Basis Universal
//            ETC1S / UASTC transcoded, ASTC decoded, plain RGBA copied)
//   "sdl3"   SDL3_image (built with its own stb, no system libraries): PNG,
//            JPEG, BMP, GIF, TGA, QOI, PCX, PNM, XPM, XCF, XV, LBM, SVG load;
//            PNG, BMP, TGA, JPEG save; resize through SDL
//
// "local" (the game's own loaders) is not a core backend: a game keeps its
// path and asks the factory only when the player picked another id; when the
// factory returns nullptr the game stays on local and logs why.
//
// Plain C++17, no exceptions: a call that fails returns false and says why.
// Decoders are not thread-safe objects; make one per thread (they are cheap).
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace gd::core::image {

enum class PixelFormat {
    RGBA8,    // 4 bytes per pixel, R G B A, straight alpha
    RGBA32F,  // 16 bytes per pixel, linear float (HDR files, BC6H)
};

// A decoded image: rows top to bottom, tightly packed.
struct Image {
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    std::vector<uint8_t> pixels;

    size_t bytesPerPixel() const { return format == PixelFormat::RGBA8 ? 4 : 16; }
    bool empty() const { return width <= 0 || height <= 0 || pixels.empty(); }
};

// Block-compressed texture data without a container (ripped textures).
enum class BlockFormat {
    BC1,         // DXT1 (with its 1-bit alpha)
    BC2,         // DXT3
    BC3,         // DXT5
    BC4,         // ATI1 / RGTC1: R, decoded as (R, 0, 0, 255) like GL
    BC4_SNORM,   // signed R, mapped to 0..255
    BC5,         // ATI2 / RGTC2: RG, decoded as (R, G, 0, 255) like GL
    BC5_SNORM,
    BC6H_UF16,   // HDR, decoded to RGBA32F
    BC6H_SF16,
    BC7,
};

// Files the encoders write.
enum class FileFormat { PNG, TGA, BMP, JPEG };

enum class Filter {
    Default,  // the library's own choice for the direction (down: box / Mitchell; up: cubic)
    Linear,
    Nearest,
};

struct DecodeOptions {
    // Containers with several images (KTX, DDS): which one.
    uint32_t level = 0;  // mip level
    uint32_t layer = 0;  // array layer
    uint32_t face = 0;   // cube face (0..5)
    // HDR sources (.hdr, BC6H): true keeps RGBA32F, false tone-clamps to RGBA8.
    bool keepFloat = false;
};

class Decoder {
public:
    virtual ~Decoder() = default;

    virtual std::string_view id() const = 0;  // "stb", "bcdec", "sdl3"

    // Whether these bytes look like something this backend decodes (by
    // content, never by file name).
    virtual bool canDecode(const void* data, size_t size) const = 0;

    // An image file (or texture container) in memory to pixels.
    virtual bool decode(const void* data, size_t size, Image& out, std::string& error,
                        const DecodeOptions& options = {}) = 0;

    // Raw blocks of `format`, `width` x `height` pixels (any size: the last
    // row / column of blocks is cut). Only "bcdec" has it.
    virtual bool decodeBlocks(BlockFormat format, int width, int height, const void* data, size_t size, Image& out,
                              std::string& error, const DecodeOptions& options = {});

    // RGBA8 pixels to a file in memory. `quality` 1..100 for JPEG.
    virtual bool encode(const Image& image, FileFormat format, std::vector<uint8_t>& out, std::string& error,
                        int quality = 90);

    // Scales an RGBA8 or RGBA32F image to width x height.
    virtual bool resize(const Image& in, int width, int height, Image& out, std::string& error,
                        Filter filter = Filter::Default);
};

// The backend with that id, or nullptr (and `error`) when the id is unknown or
// that backend was not built for this platform. Ids are lower case.
std::unique_ptr<Decoder> createDecoder(std::string_view id, std::string* error = nullptr);

// The ids this build has, in the menus' order: {"stb", "bcdec", "sdl3"}.
std::vector<std::string> decoderIds();

// Size in bytes of `width` x `height` pixels of `format` (whole 4x4 blocks).
size_t blockDataSize(BlockFormat format, int width, int height);

}  // namespace gd::core::image
