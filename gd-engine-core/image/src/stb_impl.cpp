// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// stb_image, stb_image_write and stb_image_resize2 (third_party/stb), internal
// to this file; compiled without warnings (code we do not change).
#include "third_party_impl.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO  // memory only: files are read by the caller (platform/file.h)
#include "stb_image.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"

#define STB_IMAGE_RESIZE_STATIC
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#include <climits>

namespace gd::core::image::detail {

namespace {
bool fitsInt(size_t n) { return n <= size_t(INT_MAX); }
void append(void* ctx, void* data, int size) {
    auto* out = static_cast<std::vector<uint8_t>*>(ctx);
    const auto* p = static_cast<const uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}
}  // namespace

bool stbInfo(const void* data, size_t size, int* w, int* h) {
    int comp = 0;
    return fitsInt(size) && stbi_info_from_memory(static_cast<const stbi_uc*>(data), int(size), w, h, &comp) != 0;
}

bool stbIsHdr(const void* data, size_t size) {
    return fitsInt(size) && stbi_is_hdr_from_memory(static_cast<const stbi_uc*>(data), int(size)) != 0;
}

unsigned char* stbLoadRgba8(const void* data, size_t size, int* w, int* h) {
    int comp = 0;
    if (!fitsInt(size)) return nullptr;
    return stbi_load_from_memory(static_cast<const stbi_uc*>(data), int(size), w, h, &comp, 4);
}

float* stbLoadRgbaF(const void* data, size_t size, int* w, int* h) {
    int comp = 0;
    if (!fitsInt(size)) return nullptr;
    return stbi_loadf_from_memory(static_cast<const stbi_uc*>(data), int(size), w, h, &comp, 4);
}

void stbFree(void* p) { stbi_image_free(p); }

const char* stbFailureReason() {
    const char* r = stbi_failure_reason();
    return r ? r : "unknown error";
}

bool stbWrite(int kind, int w, int h, const uint8_t* rgba, int quality, std::vector<uint8_t>& out) {
    out.clear();
    switch (kind) {
        case 0: return stbi_write_png_to_func(&append, &out, w, h, 4, rgba, w * 4) != 0;
        case 1: return stbi_write_tga_to_func(&append, &out, w, h, 4, rgba) != 0;
        case 2: return stbi_write_bmp_to_func(&append, &out, w, h, 4, rgba) != 0;
        case 3: return stbi_write_jpg_to_func(&append, &out, w, h, 4, rgba, quality) != 0;
        default: return false;
    }
}

bool stbResize(const void* in, int w, int h, void* out, int ow, int oh, bool isFloat, int filter) {
    const stbir_filter f = filter == 1 ? STBIR_FILTER_TRIANGLE : filter == 2 ? STBIR_FILTER_POINT_SAMPLE
                                                                               : STBIR_FILTER_DEFAULT;
    const int px = isFloat ? 16 : 4;
    return stbir_resize(in, w, h, w * px, out, ow, oh, ow * px, STBIR_RGBA, isFloat ? STBIR_TYPE_FLOAT : STBIR_TYPE_UINT8,
                        STBIR_EDGE_CLAMP, f) != nullptr;
}

}  // namespace gd::core::image::detail
