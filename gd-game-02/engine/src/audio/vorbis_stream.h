// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>

#include "data/bytes.h"

struct stb_vorbis;

// A multichannel Ogg Vorbis stream decoded with stb_vorbis
// (third_party/stb_vorbis), from a .mogg held in memory. GH2's song moggs are
// 4-6 channels at 32000, 32250 or 44100 Hz [C data]. Not thread-safe: one
// thread decodes (the mixer's worker).
class VorbisStream {
public:
    // Takes the whole .mogg file; throws std::runtime_error when it is not a
    // plain mogg or its Vorbis headers do not decode.
    explicit VorbisStream(Bytes mogg);
    ~VorbisStream();
    VorbisStream(const VorbisStream&) = delete;
    VorbisStream& operator=(const VorbisStream&) = delete;

    int channels() const { return channels_; }
    int sampleRate() const { return rate_; }
    uint64_t lengthFrames() const { return length_; }
    // Decodes up to `frames` frames into one buffer per channel; returns the
    // count decoded (less than asked only at the end of the stream).
    int decode(float* const* channelBuffers, int frames);
    bool seek(uint64_t frame);

private:
    Bytes data_;
    stb_vorbis* v_ = nullptr;
    int channels_ = 0, rate_ = 0;
    uint64_t length_ = 0;
};

// Reads only the Vorbis identification header of a .mogg's first bytes:
// channels and sample rate (for the self-test). Throws on a bad file.
void probeMogg(const uint8_t* data, size_t size, int& channels, int& rate);
