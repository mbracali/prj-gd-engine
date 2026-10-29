// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "video/ipu.h"

#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
int le16(const uint8_t* p) { return int(p[0]) | int(p[1]) << 8; }

// Offset of the next 00 00 01 xx code at or after `from`, or SIZE_MAX. The
// macroblock data cannot hold 23 zero bits in a row, so the first code after
// a frame's flags byte is its own end.
size_t findCode(const std::vector<uint8_t>& d, size_t from) {
    for (size_t i = from; i + 3 < d.size(); ++i)
        if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) return i;
    return SIZE_MAX;
}

inline int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

}  // namespace

IpuMovie::IpuMovie(std::vector<uint8_t> file) : file_(std::move(file)) {
    if (file_.size() < 16 || std::memcmp(file_.data(), "ipum", 4) != 0) throw std::runtime_error("not an IPU movie (no 'ipum')");
    width_ = le16(&file_[8]);
    height_ = le16(&file_[10]);
    const uint32_t count = le32(&file_[12]);
    // The PS2 texture sizes are powers of two (milo/rnd_objects.dta's Movie help) and the IPU works in macroblocks.
    if (width_ < 16 || height_ < 16 || width_ % 16 || height_ % 16 || width_ > 1024 || height_ > 1024)
        throw std::runtime_error("IPU movie size " + std::to_string(width_) + "x" + std::to_string(height_));
    if (count == 0 || count > 100000) throw std::runtime_error("IPU movie frame count " + std::to_string(count));
    size_t pos = 16;
    for (uint32_t i = 0; i < count; ++i) {
        if (pos >= file_.size()) throw std::runtime_error("IPU movie ends before frame " + std::to_string(i));
        Frame f;
        f.flags = file_[pos];
        f.offset = pos + 1;
        const size_t end = findCode(file_, f.offset);
        if (end == SIZE_MAX || file_[end + 3] != 0xB0)
            throw std::runtime_error("IPU movie frame " + std::to_string(i) + " has no 00 00 01 B0 end");
        f.size = end - f.offset;
        frames_.push_back(f);
        pos = end + 4;
    }
    if (pos + 4 > file_.size() || findCode(file_, pos) != pos || file_[pos + 3] != 0xB1)
        throw std::runtime_error("IPU movie without 00 00 01 B1 after its last frame");
}

const Mpeg2Frame& IpuMovie::decodeYuv(int i) {
    if (i < 0 || i >= frameCount()) throw std::runtime_error("IPU frame " + std::to_string(i) + " out of range");
    const Frame& f = frames_[size_t(i)];
    size_t bits = 0;
    const Mpeg2Frame& pic = decoder_.decodeIdec(&file_[f.offset], f.size, width_, height_, f.flags, bits);
    // Whatever follows the last macroblock up to the code must be the zero
    // padding to the byte boundary (the IPU's FDEC 32 at 0x19ec60 then skips
    // the 00 00 01 B0).
    if ((bits + 7) / 8 != f.size) throw std::runtime_error("IPU frame " + std::to_string(i) + " has data after its macroblocks");
    return pic;
}

void IpuMovie::decodeRgba(int i, std::vector<uint8_t>& rgba) {
    const Mpeg2Frame& p = decodeYuv(i);
    rgba.resize(size_t(width_) * height_ * 4);
    uint8_t* d = rgba.data();
    for (int y = 0; y < height_; ++y) {
        const uint8_t* ly = &p.y[size_t(y) * p.lumaStride];
        const uint8_t* lu = &p.cb[size_t(y >> 1) * p.chromaStride];
        const uint8_t* lv = &p.cr[size_t(y >> 1) * p.chromaStride];
        for (int x = 0; x < width_; ++x, d += 4) {
            // IPU CSC: BT.601 limited range in 7-bit fixed point [I: the
            // coefficients and rounding PS2 emulators use; no Sony table here].
            const int lum = (ly[x] - 16) * 149, u = lu[x >> 1] - 128, v = lv[x >> 1] - 128;
            const int r = clamp255((lum + 204 * v + 64) >> 7);
            const int g = clamp255((lum - 104 * v - 50 * u + 64) >> 7);
            const int b = clamp255((lum + 258 * u + 64) >> 7);
            // RGB16 (IDEC OFM=1, DTE=0): the top 5 bits; the alpha bit from
            // SETTH 0x91000001 sent at 0x19ecf0 (TH0 = 1) [C], with the
            // threshold applied to the 8-bit values [I].
            const bool opaque = r >= 1 || g >= 1 || b >= 1;
            d[0] = uint8_t((r >> 3) << 3);
            d[1] = uint8_t((g >> 3) << 3);
            d[2] = uint8_t((b >> 3) << 3);
            d[3] = opaque ? 0x80 : 0;
        }
    }
}

bool IpuPlayer::setFrame(float frame) {
    const int n = movie_.frameCount();
    int target = int(frame);  // the PS2 truncates (cvt.w.s)
    if (loop_) {
        target %= n;
        if (target < 0) target += n;
    } else {
        target = target > n - 1 ? n - 1 : target < 0 ? 0 : target;
    }
    if (target == current_) return false;
    if (target < current_) {
        current_ = 0;  // the IPU and the DMA chain restart at the file's first frame
        ++rewinds_;
    } else {
        ++current_;
    }
    movie_.decodeRgba(current_, rgba_);
    ++decodes_;
    return true;
}

uint64_t ipuHash(const uint8_t* data, size_t size, uint64_t h) {
    for (size_t i = 0; i < size; ++i) h = (h ^ data[i]) * 0x100000001b3ull;
    return h;
}
