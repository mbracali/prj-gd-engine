// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "data/bytes.h"

// Streaming decoder for .vgs multitrack audio: 16-byte PS-ADPCM blocks whose
// second byte holds the channel ID (VAGFileReader_New, FUN_0021d7c0).
class VgsStream {
public:
    // Throws std::runtime_error on malformed input.
    explicit VgsStream(Bytes data);

    int channels() const { return static_cast<int>(channels_.size()); }
    int sampleRate() const { return sampleRate_; }
    uint64_t lengthFrames() const { return lengthFrames_; }

    // Decodes the next `frames` samples of every channel into out[ch][0..frames).
    // Channels that run out are padded with silence.
    void decode(float* const* out, int frames);
    // Restarts decoding at `frame` (block-aligned decoder state is reset).
    void seek(uint64_t frame);

private:
    struct Channel {
        std::vector<uint32_t> blocks;  // byte offsets of this channel's blocks
        size_t nextBlock = 0;
        std::array<int16_t, 28> samples{};
        int pos = 28;  // index into samples; 28 = need a new block
        int s1 = 0, s2 = 0;
    };
    void decodeBlock(Channel& ch);

    Bytes data_;
    std::vector<Channel> channels_;
    int sampleRate_ = 44100;
    uint64_t lengthFrames_ = 0;
};
