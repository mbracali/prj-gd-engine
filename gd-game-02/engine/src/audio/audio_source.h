// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>

// What the mixer plays (audio/mixer.h): a song's multichannel Vorbis
// (audio/vorbis_stream.h) or a video's soundtrack (video/wmv_player.cpp).
// decode() runs on the mixer's worker thread only.
class AudioSource {
public:
    virtual ~AudioSource() = default;
    virtual int channels() const = 0;
    virtual int sampleRate() const = 0;
    // Decodes up to `frames` frames into one float buffer per channel;
    // returns the count decoded (less than asked only at the end).
    virtual int decode(float* const* channelBuffers, int frames) = 0;
    // Seeks to a frame (for a start offset / practice). Sources that cannot
    // seek return false and keep playing from where they are.
    virtual bool seek(uint64_t frame) { return false; }
};
