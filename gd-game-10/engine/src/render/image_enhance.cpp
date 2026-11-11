// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/image_enhance.h"

#include <algorithm>
#include <cmath>

namespace gfx {
namespace {

// Catmull-Rom weights for the four taps around a sample at fraction t.
void catmullRom(float t, float w[4]) {
    const float t2 = t * t, t3 = t2 * t;
    w[0] = 0.5f * (-t3 + 2 * t2 - t);
    w[1] = 0.5f * (3 * t3 - 5 * t2 + 2);
    w[2] = 0.5f * (-3 * t3 + 4 * t2 + t);
    w[3] = 0.5f * (t3 - t2);
}

// One separable pass: `src` (sw x sh, 4 floats a pixel) resampled by `f`
// along x (horizontal) or y.
std::vector<float> pass(const std::vector<float>& src, int sw, int sh, int f, bool horizontal) {
    const int dw = horizontal ? sw * f : sw, dh = horizontal ? sh : sh * f;
    std::vector<float> dst(size_t(dw) * size_t(dh) * 4);
    const int n = horizontal ? sw : sh;
    for (int y = 0; y < dh; ++y)
        for (int x = 0; x < dw; ++x) {
            const int d = horizontal ? x : y;
            const float pos = (float(d) + 0.5f) / float(f) - 0.5f;
            const int i0 = int(std::floor(pos));
            float w[4];
            catmullRom(pos - float(i0), w);
            float acc[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; ++k) {
                const int i = std::clamp(i0 - 1 + k, 0, n - 1);
                const size_t s = horizontal ? (size_t(y) * size_t(sw) + size_t(i)) * 4 : (size_t(i) * size_t(sw) + size_t(x)) * 4;
                for (int c = 0; c < 4; ++c) acc[c] += w[k] * src[s + size_t(c)];
            }
            const size_t o = (size_t(y) * size_t(dw) + size_t(x)) * 4;
            for (int c = 0; c < 4; ++c) dst[o + size_t(c)] = acc[c];
        }
    return dst;
}

}  // namespace

bool enhanceImage(std::vector<uint8_t>& rgba, int& width, int& height) {
    const int w = width, h = height;
    if (w <= 0 || h <= 0 || rgba.size() < size_t(w) * size_t(h) * 4) return false;
    const int maxDim = std::max(w, h);
    const int f = maxDim <= 256 ? 4 : maxDim <= 512 ? 2 : 1;
    if (f == 1) return false;
    // Premultiplied, so transparent texels add no dark fringe.
    std::vector<float> src(size_t(w) * size_t(h) * 4);
    int mid = 0, visible = 0;
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
        const float a = rgba[i * 4 + 3] / 255.0f;
        for (int c = 0; c < 3; ++c) src[i * 4 + size_t(c)] = rgba[i * 4 + size_t(c)] / 255.0f * a;
        src[i * 4 + 3] = a;
        if (a > 0.02f) ++visible;
        if (a > 0.1f && a < 0.9f) ++mid;
    }
    // Glyph sheets and cut-out sprites have mostly hard alpha: their upscaled
    // edges are tightened again instead of staying a soft ramp.
    const bool hardAlpha = visible > 0 && float(mid) / float(visible) < 0.25f;
    const std::vector<float> dst = pass(pass(src, w, h, f, true), w * f, h, f, false);
    width = w * f, height = h * f;
    rgba.assign(size_t(width) * size_t(height) * 4, 0);
    for (size_t i = 0; i < size_t(width) * size_t(height); ++i) {
        float a = std::clamp(dst[i * 4 + 3], 0.0f, 1.0f);
        float rgb[3];
        for (int c = 0; c < 3; ++c) rgb[c] = a > 1e-4f ? std::clamp(dst[i * 4 + size_t(c)] / a, 0.0f, 1.0f) : 0.0f;
        if (hardAlpha) {
            const float t = std::clamp((a - 0.3f) / 0.4f, 0.0f, 1.0f);
            a = t * t * (3 - 2 * t);
        }
        for (int c = 0; c < 3; ++c) rgba[i * 4 + size_t(c)] = uint8_t(std::lround(rgb[c] * 255));
        rgba[i * 4 + 3] = uint8_t(std::lround(a * 255));
    }
    return true;
}

std::vector<std::vector<uint8_t>> mipChain(const std::vector<uint8_t>& rgba, int width, int height) {
    std::vector<std::vector<uint8_t>> out;
    const std::vector<uint8_t>* src = &rgba;
    int w = width, h = height;
    while (w > 1 || h > 1) {
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<uint8_t> next(size_t(nw) * size_t(nh) * 4);
        for (int y = 0; y < nh; ++y)
            for (int x = 0; x < nw; ++x)
                for (int c = 0; c < 4; ++c) {
                    int sum = 0;
                    for (int dy = 0; dy < 2; ++dy)
                        for (int dx = 0; dx < 2; ++dx) {
                            const int sx = std::min(x * 2 + dx, w - 1), sy = std::min(y * 2 + dy, h - 1);
                            sum += (*src)[(size_t(sy) * size_t(w) + size_t(sx)) * 4 + size_t(c)];
                        }
                    next[(size_t(y) * size_t(nw) + size_t(x)) * 4 + size_t(c)] = uint8_t((sum + 2) / 4);
                }
        out.push_back(std::move(next));
        src = &out.back();
        w = nw, h = nh;
    }
    return out;
}

}  // namespace gfx
