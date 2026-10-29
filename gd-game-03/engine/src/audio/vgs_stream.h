// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "audio/audio_source.h"
#include "data/bytes.h"

// A song's multitrack .vgs (docs/formats.md "vgs"), copied from gd-game-01's
// data/vgs and made an AudioSource:
//   "VgS!", u32 version 2, then up to 15 (u32 sample rate, u32 block count)
//   channel entries (rate 0 ends the list), data from 0x80: 16-byte SPU
//   PS-ADPCM blocks whose second byte holds the channel (0x80 set on a
//   channel's last block).
// The whole file is held in memory. Not thread-safe: one thread decodes.
class VgsStream final : public AudioSource {
public:
    // Throws std::runtime_error on malformed input.
    explicit VgsStream(Bytes data);

    int channels() const override { return static_cast<int>(channels_.size()); }
    int sampleRate() const override { return sampleRate_; }
    uint64_t lengthFrames() const { return lengthFrames_; }
    // Decodes up to `frames` frames into one buffer per channel; returns the
    // count decoded (less than asked only at the end). Channels that end
    // earlier than the longest are padded with silence.
    int decode(float* const* channelBuffers, int frames) override;
    // Restarts decoding at `frame` (block-aligned decoder state is reset).
    bool seek(uint64_t frame);

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
    uint64_t lengthFrames_ = 0, played_ = 0;
};

// Reads only a .vgs header (its first 0x80 bytes): channel count and sample
// rate, for the self-test. Throws on a bad header.
void probeVgs(const uint8_t* data, size_t size, int& channels, int& rate);
