// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "video/mpeg2.h"

// PS2 IPU movies (.ipu), the files a RndMovie animates through a texture
// (docs/venues.md "fest_1.ipu"). The disc's only one is
// world/shared/fest_1.ipu, which nothing in SLUS_215.86 loads [C].
//
// File [C data, the PsMovie loader at 0x19e690 reads +8, +0xA, +0xC]:
//   'ipum', u32 size (not trusted), u16 width, u16 height, u32 frame count,
//   then per frame: one IPU_CTRL flags byte, the IDEC macroblocks, zero bits
//   to a byte boundary and 00 00 01 B0; 00 00 01 B1 ends the stream.
class IpuMovie {
public:
    // Parses the header and indexes the frames; throws std::runtime_error.
    explicit IpuMovie(std::vector<uint8_t> file);

    int width() const { return width_; }
    int height() const { return height_; }
    int frameCount() const { return int(frames_.size()); }
    unsigned frameFlags(int i) const { return frames_[size_t(i)].flags; }
    size_t frameBytes(int i) const { return frames_[size_t(i)].size; }

    // Decodes frame i (every frame is intra, so any order works) as the IPU
    // outputs it with this game's settings, in RGBA8, width*height*4 bytes:
    // IDEC to RGB16 5:5:5:1 without dithering, A = 0 for pure black (SETTH
    // TH0 = 1), and each 5-bit channel widened as the GS reads a 16-bit
    // texel (c << 3; A 0 / 0x80 as TEXA TA0 / TA1). Throws on bad data.
    void decodeRgba(int i, std::vector<uint8_t>& rgba);
    // The YUV picture of frame i (for checks); valid until the next decode.
    const Mpeg2Frame& decodeYuv(int i);

private:
    struct Frame {
        size_t offset = 0;  // the macroblock data, after the flags byte
        size_t size = 0;    // up to the 00 00 01 B0 code
        unsigned flags = 0;
    };
    std::vector<uint8_t> file_;
    int width_ = 0, height_ = 0;
    std::vector<Frame> frames_;
    Mpeg2Decoder decoder_;
};

// The PS2 RndMovie's SetFrame (PsMovie at 0x19eef0) [C]: the movie only
// decodes forward, one frame per call. The requested frame is wrapped
// (loop) or clamped to [0, n-1]; when it differs from the current one, a
// smaller one rewinds the stream to frame 0, otherwise the next frame
// (current + 1) is decoded. Before the first call the current frame is n,
// so the first call decodes frame 0. The RndMovie's ScaleOffset filter
// turns the driving clock into the frame (fest_1 has none: no Movie names
// it, docs/venues.md).
class IpuPlayer {
public:
    IpuPlayer(IpuMovie& movie, bool loop) : movie_(movie), loop_(loop), current_(movie.frameCount()) {}

    // Returns true when a new picture was decoded into rgba().
    bool setFrame(float frame);
    int current() const { return current_; }
    const std::vector<uint8_t>& rgba() const { return rgba_; }
    uint64_t decodes() const { return decodes_; }
    uint64_t rewinds() const { return rewinds_; }

private:
    IpuMovie& movie_;
    bool loop_;
    int current_;
    std::vector<uint8_t> rgba_;
    uint64_t decodes_ = 0, rewinds_ = 0;
};

// 64-bit FNV-1a, for the decode checks (tools/ipu.py prints the same).
uint64_t ipuHash(const uint8_t* data, size_t size, uint64_t h = 0xcbf29ce484222325ull);
