// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "audio/audio_source.h"
#include "audio/xma_stream.h"
#include "data/fsb.h"

class Disc;

// A song's audio for the mixer: DATA/MUSIC/<song>.fsb.xen's three stereo
// XMA1 stems, decoded while they play, as six channels: guitar L R,
// rhythm L R, song L R (Stem below). The stems come from the bank's .dat
// ("<song>_guitar", "<song>_rhythm", "<song>_song") [C data]; a missing
// stem plays silence. Decoding runs on the mixer's worker thread.
class SongStream : public AudioSource {
public:
    enum Stem { Guitar = 0, Rhythm = 1, Song = 2, StemCount = 3 };
    static std::unique_ptr<SongStream> open(const Disc& disc, const std::string& song, std::string& error);

    int channels() const override { return 2 * StemCount; }
    int sampleRate() const override { return rate_; }
    int decode(float* const* channelBuffers, int frames) override;
    // Frames of the longest stem.
    uint32_t length() const { return length_; }
    const std::string& error() const { return error_; }

private:
    struct StemState {
        const FsbSample* sample = nullptr;
        XmaStream xma;
        uint64_t next = 0;           // next packet's byte offset in the sample data
        std::vector<float> pending;  // decoded, not yet handed out (interleaved stereo)
        size_t pendingAt = 0;
        bool ended = false;
        uint32_t produced = 0;       // frames handed out
    };
    bool refill(StemState& s, size_t frames);

    const Disc* disc_ = nullptr;
    FsbBank bank_;
    std::array<StemState, StemCount> stems_;
    int rate_ = 44100;
    uint32_t length_ = 0;
    std::string error_;
};
