// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "sdl3" image backend: SDL3_image, built into the core as a static
// library with its own stb_image for PNG / JPEG and its built-in loaders, no
// system image libraries (cmake: image/CMakeLists.txt). Resizing through
// SDL_ScaleSurface.
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>

#include <cstring>

#include "backends.h"

namespace gd::core::image::detail {

namespace {

struct SurfaceFree {
    void operator()(SDL_Surface* s) const { SDL_DestroySurface(s); }
};
using Surface = std::unique_ptr<SDL_Surface, SurfaceFree>;

bool toImage(SDL_Surface* s, Image& out, std::string& error) {
    Surface rgba(SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32));  // R, G, B, A bytes in memory
    if (!rgba) {
        error = std::string("sdl3: ") + SDL_GetError();
        return false;
    }
    out.width = rgba->w;
    out.height = rgba->h;
    out.format = PixelFormat::RGBA8;
    out.pixels.resize(size_t(rgba->w) * size_t(rgba->h) * 4);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(&out.pixels[size_t(y) * size_t(rgba->w) * 4],
                    static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch), size_t(rgba->w) * 4);
    return true;
}

// A surface over an RGBA8 image's pixels (not copied).
Surface wrap(const Image& image) {
    return Surface(SDL_CreateSurfaceFrom(image.width, image.height, SDL_PIXELFORMAT_RGBA32,
                                         const_cast<uint8_t*>(image.pixels.data()), image.width * 4));
}

bool rgba8Ok(const Image& image) {
    return image.format == PixelFormat::RGBA8 && !image.empty() &&
           image.pixels.size() >= size_t(image.width) * size_t(image.height) * 4;
}

class Sdl3Decoder final : public Decoder {
public:
    std::string_view id() const override { return "sdl3"; }

    bool canDecode(const void* data, size_t size) const override {
        if (!data || !size) return false;
        SDL_IOStream* io = SDL_IOFromConstMem(data, size);
        if (!io) return false;
        // Formats with a signature; TGA has none (decode() still reads it).
        const bool yes = IMG_isPNG(io) || IMG_isJPG(io) || IMG_isBMP(io) || IMG_isGIF(io) || IMG_isQOI(io) ||
                         IMG_isPCX(io) || IMG_isPNM(io) || IMG_isXPM(io) || IMG_isXCF(io) || IMG_isXV(io) ||
                         IMG_isLBM(io) || IMG_isSVG(io);
        SDL_CloseIO(io);
        return yes;
    }

    bool decode(const void* data, size_t size, Image& out, std::string& error, const DecodeOptions&) override {
        if (!data || !size) {
            error = "sdl3: no data";
            return false;
        }
        SDL_IOStream* io = SDL_IOFromConstMem(data, size);
        if (!io) {
            error = std::string("sdl3: ") + SDL_GetError();
            return false;
        }
        Surface s(IMG_Load_IO(io, false));
        if (!s) {  // no signature: TGA by its header
            SDL_SeekIO(io, 0, SDL_IO_SEEK_SET);
            s.reset(IMG_LoadTyped_IO(io, false, "TGA"));
        }
        if (!s) error = std::string("sdl3: ") + SDL_GetError();
        SDL_CloseIO(io);
        return s && toImage(s.get(), out, error);
    }

    bool encode(const Image& image, FileFormat format, std::vector<uint8_t>& out, std::string& error,
                int quality) override {
        if (!rgba8Ok(image)) {
            error = "sdl3: encode needs a non-empty RGBA8 image";
            return false;
        }
        Surface s = wrap(image);
        SDL_IOStream* io = SDL_IOFromDynamicMem();
        if (!s || !io) {
            error = std::string("sdl3: ") + SDL_GetError();
            if (io) SDL_CloseIO(io);
            return false;
        }
        if (quality < 1) quality = 1;
        if (quality > 100) quality = 100;
        bool ok = false;
        switch (format) {
            case FileFormat::PNG: ok = IMG_SavePNG_IO(s.get(), io, false); break;
            case FileFormat::TGA: ok = IMG_SaveTGA_IO(s.get(), io, false); break;
            case FileFormat::BMP: ok = SDL_SaveBMP_IO(s.get(), io, false); break;
            case FileFormat::JPEG: ok = IMG_SaveJPG_IO(s.get(), io, false, quality); break;
        }
        if (ok) {
            const Sint64 n = SDL_TellIO(io);
            const void* mem = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, nullptr);
            ok = n > 0 && mem;
            if (ok) out.assign(static_cast<const uint8_t*>(mem), static_cast<const uint8_t*>(mem) + n);
        }
        if (!ok) error = std::string("sdl3: encoding failed: ") + SDL_GetError();
        SDL_CloseIO(io);
        return ok;
    }

    bool resize(const Image& in, int width, int height, Image& out, std::string& error, Filter filter) override {
        if (!rgba8Ok(in)) {
            error = "sdl3: resize needs a non-empty RGBA8 image";
            return false;
        }
        if (!validSize(width, height, 4, error)) return false;
        Surface s = wrap(in);
        Surface r(s ? SDL_ScaleSurface(s.get(), width, height,
                                       filter == Filter::Nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR)
                    : nullptr);
        if (!r) {
            error = std::string("sdl3: ") + SDL_GetError();
            return false;
        }
        Image tmp;
        if (!toImage(r.get(), tmp, error)) return false;
        out = std::move(tmp);
        return true;
    }
};

}  // namespace

std::unique_ptr<Decoder> createSdl3() { return std::make_unique<Sdl3Decoder>(); }

}  // namespace gd::core::image::detail
