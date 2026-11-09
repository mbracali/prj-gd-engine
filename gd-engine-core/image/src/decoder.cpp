// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "gd/core/image/decoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "backends.h"

namespace gd::core::image {

namespace {
std::string unsupported(std::string_view id, const char* what) {
    return std::string(id) + ": " + what + " is not supported by this backend";
}
}  // namespace

bool Decoder::decodeBlocks(BlockFormat, int, int, const void*, size_t, Image&, std::string& error, const DecodeOptions&) {
    error = unsupported(id(), "block-compressed data");
    return false;
}

bool Decoder::encode(const Image&, FileFormat, std::vector<uint8_t>&, std::string& error, int) {
    error = unsupported(id(), "encoding");
    return false;
}

bool Decoder::resize(const Image&, int, int, Image&, std::string& error, Filter) {
    error = unsupported(id(), "resizing");
    return false;
}

std::vector<std::string> decoderIds() {
    std::vector<std::string> ids{"stb", "bcdec"};
#if GDCORE_IMAGE_SDL3
    ids.push_back("sdl3");
#endif
    return ids;
}

std::unique_ptr<Decoder> createDecoder(std::string_view id, std::string* error) {
    if (id == "stb") return detail::createStb();
    if (id == "bcdec") return detail::createBcdec();
#if GDCORE_IMAGE_SDL3
    if (id == "sdl3") return detail::createSdl3();
#else
    if (id == "sdl3") {
        if (error) *error = "image backend sdl3: not built for this platform";
        return nullptr;
    }
#endif
    if (error) *error = "image backend \"" + std::string(id) + "\": unknown (known: stb, bcdec, sdl3)";
    return nullptr;
}

size_t blockDataSize(BlockFormat format, int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    const size_t blocks = size_t((width + 3) / 4) * size_t((height + 3) / 4);
    switch (format) {
        case BlockFormat::BC1:
        case BlockFormat::BC4:
        case BlockFormat::BC4_SNORM: return blocks * 8;
        default: return blocks * 16;
    }
}

namespace detail {

void floatToRgba8(const Image& in, Image& out) {
    out.width = in.width;
    out.height = in.height;
    out.format = PixelFormat::RGBA8;
    const size_t n = size_t(in.width) * size_t(in.height);
    out.pixels.resize(n * 4);
    const float* src = reinterpret_cast<const float*>(in.pixels.data());
    for (size_t i = 0; i < n * 4; ++i) {
        float v = src[i];
        if (!(v > 0.0f)) v = 0.0f;  // NaN too
        if (v > 1.0f) v = 1.0f;
        if (i % 4 != 3) v = std::pow(v, 1.0f / 2.2f);
        out.pixels[i] = uint8_t(v * 255.0f + 0.5f);
    }
}

bool validSize(int width, int height, size_t bytesPerPixel, std::string& error) {
    // 32768 x 32768 is beyond any texture a game of ours loads [I].
    if (width <= 0 || height <= 0 || width > 32768 || height > 32768) {
        error = "invalid size " + std::to_string(width) + "x" + std::to_string(height);
        return false;
    }
    (void)bytesPerPixel;
    return true;
}

}  // namespace detail

}  // namespace gd::core::image
