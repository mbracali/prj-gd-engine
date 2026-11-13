// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/texture_codec.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include "gd/core/image/decoder.h"

namespace gfx {
namespace {

size_t blockBytes(TexFormat f) { return f == TexFormat::BC1 ? 8 : 16; }

}  // namespace

size_t levelSize(TexFormat format, int w, int h) {
    if (format == TexFormat::RGBA8) return size_t(w) * size_t(h) * 4;
    if (format == TexFormat::R8) return size_t(w) * size_t(h);
    return size_t((w + 3) / 4) * size_t((h + 3) / 4) * blockBytes(format);
}

void swapBytes16(uint8_t* data, size_t size) {
    for (size_t i = 0; i + 1 < size; i += 2) std::swap(data[i], data[i + 1]);
}

// gd-engine-core's bcdec backend (BC5 as GL's RG: x, y, 0, 255).
bool decodeToRgba8(TexFormat format, const uint8_t* data, size_t size, int w, int h, std::vector<uint8_t>& out) {
    out.assign(size_t(w) * size_t(h) * 4, 0);
    if (format == TexFormat::RGBA8) {
        if (size < out.size()) return false;
        std::copy_n(data, out.size(), out.begin());
        return true;
    }
    if (format == TexFormat::R8 || size < levelSize(format, w, h)) return false;
    namespace img = gd::core::image;
    thread_local std::unique_ptr<img::Decoder> decoder;  // loader threads decode too
    if (!decoder && !(decoder = img::createDecoder("bcdec"))) return false;
    std::string error;
    const img::BlockFormat bf = format == TexFormat::BC1 ? img::BlockFormat::BC1 : format == TexFormat::BC3 ? img::BlockFormat::BC3 : img::BlockFormat::BC5;
    img::Image image;
    if (!decoder->decodeBlocks(bf, w, h, data, size, image, error) || image.pixels.size() != out.size()) return false;
    out = std::move(image.pixels);
    return true;
}

}  // namespace gfx
