// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// One decoded picture in YUV 4:2:0. The planes are macroblock aligned; only
// the top-left width x height (chroma (width/2) x (height/2)) is picture.
struct Mpeg2Frame {
    int width = 0, height = 0;
    int lumaStride = 0, chromaStride = 0;
    std::vector<uint8_t> y, cb, cr;
    char type = 'I';            // 'I', 'P' or 'B'
    bool progressive = true;    // progressive_frame
    bool topFieldFirst = true;  // field order when !progressive
    uint64_t index = 0;         // display order number, from 0
};

// MPEG-2 Main@Main video elementary stream decoder (ISO/IEC 13818-2), frame
// pictures only (every picture of the game's .pss movies, docs/video.md §2.2):
// I/P/B, frame and field motion compensation, dual prime, field DCT, both
// scans, linear and non-linear quantiser, tables B.14/B.15, custom matrices.
// Field pictures and MPEG-1 streams are rejected with std::runtime_error.
class Mpeg2Decoder {
public:
    Mpeg2Decoder();
    ~Mpeg2Decoder();
    Mpeg2Decoder(const Mpeg2Decoder&) = delete;
    Mpeg2Decoder& operator=(const Mpeg2Decoder&) = delete;

    // Appends elementary stream bytes.
    void push(const uint8_t* data, size_t size);
    // Marks the end of the stream so the last pictures get flushed.
    void pushEnd();
    // Decodes until the next picture in display order is ready. Returns
    // nullptr when more data is needed (or, after pushEnd, at the end). The
    // frame stays valid until the next call. Throws std::runtime_error on
    // unsupported or malformed headers; damaged slices are concealed and
    // counted in sliceErrors().
    const Mpeg2Frame* decode();
    // Drops buffered data and references (e.g. before restarting a stream).
    void reset();

    int width() const;
    int height() const;
    double frameRate() const;  // from the sequence header, 0 before it
    uint64_t sliceErrors() const;
    uint64_t dualPrimeMacroblocks() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// BT.601 limited-range YUV -> RGBA8 (alpha 255) into dst (dstStride bytes per
// row, width*height pixels). With `deinterlace`, interlaced frames are
// linear-blended (each output row averages two adjacent rows).
void mpeg2ToRgba(const Mpeg2Frame& frame, uint8_t* dst, size_t dstStride, bool deinterlace = false);
