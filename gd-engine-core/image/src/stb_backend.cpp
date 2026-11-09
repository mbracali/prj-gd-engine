// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "stb" image backend: stb_image (load), stb_image_write (save),
// stb_image_resize2 (resize).
#include <cstring>

#include "backends.h"
#include "third_party_impl.h"

namespace gd::core::image::detail {

namespace {

class StbDecoder final : public Decoder {
public:
    std::string_view id() const override { return "stb"; }

    bool canDecode(const void* data, size_t size) const override {
        int w = 0, h = 0;
        return data && size && stbInfo(data, size, &w, &h);
    }

    bool decode(const void* data, size_t size, Image& out, std::string& error, const DecodeOptions& options) override {
        if (!data || !size) {
            error = "stb: no data";
            return false;
        }
        int w = 0, h = 0;
        if (stbIsHdr(data, size) && options.keepFloat) {
            float* px = stbLoadRgbaF(data, size, &w, &h);
            if (!px) {
                error = std::string("stb: ") + stbFailureReason();
                return false;
            }
            out.width = w;
            out.height = h;
            out.format = PixelFormat::RGBA32F;
            out.pixels.resize(size_t(w) * size_t(h) * 16);
            std::memcpy(out.pixels.data(), px, out.pixels.size());
            stbFree(px);
            return true;
        }
        unsigned char* px = stbLoadRgba8(data, size, &w, &h);
        if (!px) {
            error = std::string("stb: ") + stbFailureReason();
            return false;
        }
        out.width = w;
        out.height = h;
        out.format = PixelFormat::RGBA8;
        out.pixels.assign(px, px + size_t(w) * size_t(h) * 4);
        stbFree(px);
        return true;
    }

    bool encode(const Image& image, FileFormat format, std::vector<uint8_t>& out, std::string& error,
                int quality) override {
        if (image.format != PixelFormat::RGBA8 || image.empty() ||
            image.pixels.size() < size_t(image.width) * size_t(image.height) * 4) {
            error = "stb: encode needs a non-empty RGBA8 image";
            return false;
        }
        const int kind = format == FileFormat::PNG ? 0 : format == FileFormat::TGA ? 1 : format == FileFormat::BMP ? 2 : 3;
        if (quality < 1) quality = 1;
        if (quality > 100) quality = 100;
        if (!stbWrite(kind, image.width, image.height, image.pixels.data(), quality, out) || out.empty()) {
            error = "stb: encoding failed";
            return false;
        }
        return true;
    }

    bool resize(const Image& in, int width, int height, Image& out, std::string& error, Filter filter) override {
        if (in.empty() || in.pixels.size() < size_t(in.width) * size_t(in.height) * in.bytesPerPixel()) {
            error = "stb: resize needs a non-empty image";
            return false;
        }
        if (!validSize(width, height, in.bytesPerPixel(), error)) return false;
        Image r;
        r.width = width;
        r.height = height;
        r.format = in.format;
        r.pixels.resize(size_t(width) * size_t(height) * in.bytesPerPixel());
        const int f = filter == Filter::Linear ? 1 : filter == Filter::Nearest ? 2 : 0;
        if (!stbResize(in.pixels.data(), in.width, in.height, r.pixels.data(), width, height,
                       in.format == PixelFormat::RGBA32F, f)) {
            error = "stb: resize failed";
            return false;
        }
        out = std::move(r);
        return true;
    }
};

}  // namespace

std::unique_ptr<Decoder> createStb() { return std::make_unique<StbDecoder>(); }

}  // namespace gd::core::image::detail
