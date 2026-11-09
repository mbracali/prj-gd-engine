// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The image backends (private): one factory function each, and helpers they share.
#pragma once

#include <memory>

#include "gd/core/image/decoder.h"

namespace gd::core::image::detail {

std::unique_ptr<Decoder> createStb();
std::unique_ptr<Decoder> createBcdec();
std::unique_ptr<Decoder> createSdl3();

// RGBA32F to RGBA8: colour clamped to [0, 1] with a 1/2.2 gamma (what
// stb_image does for HDR files loaded as 8-bit [C: stbi__hdr_to_ldr]),
// alpha clamped linearly.
void floatToRgba8(const Image& in, Image& out);

// Checks a requested size: positive and small enough for memory.
bool validSize(int width, int height, size_t bytesPerPixel, std::string& error);

}  // namespace gd::core::image::detail
