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

// A song's audio as three stereo stems (guitar, rhythm, the rest) for the
// mixer. GH3: one bank, a stereo XMA sample per stem (<song>_guitar,
// _rhythm, _song). gh4 (Metallica): three banks <song>_1 .. _3, each one
// multichannel XMA sample of interleaved stereo streams (4 + 3 + 2 pairs),
// every pair routed to a stem (kGh4Pairs, docs/audio.md) [C 0x820E6948:
// the three banks; the pairs' instruments I].
class SongStream : public AudioSource {
public:
    enum Stem { Guitar = 0, Rhythm = 1, Song = 2, StemCount = 3 };
    static std::unique_ptr<SongStream> open(const Disc& disc, const std::string& song, std::string& error);
    // gh4's set list preview: DATA/MUSIC/<song>_preview.fsb.xen, one stereo
    // XMA sample (about 21 s), its key made from "<song>_preview" as the
    // song banks' [C 0x820E45E8: "%s_preview", 0x820E24F8]; the pair is the
    // Song stem.
    static std::unique_ptr<SongStream> openPreview(const Disc& disc, const std::string& song, std::string& error);

    int channels() const override { return 2 * StemCount; }
    int sampleRate() const override { return rate_; }
    int decode(float* const* channelBuffers, int frames) override;
    // Frames of the longest stem.
    uint32_t length() const { return length_; }
    // Starts the stems at song time `ms` instead of 0 (a mid-song start:
    // GH3's starttime) before the first decode.
    bool seek(double ms);
    const std::string& error() const { return error_; }
    // GD10_PAIRGAIN-free check (tests): the RMS of each source pair over the
    // next `frames` frames, decoding them (the stream moves on).
    std::vector<float> pairRms(int frames);

private:
    struct Source {
        FsbBank bank;
        const FsbSample* sample = nullptr;
        XmaStream xma;
        int channels = 2;
        std::vector<int> pairStem;   // each stereo pair's stem (-1: dropped)
        uint64_t next = 0;           // next packet's byte offset in the sample data
        std::vector<float> pending;  // decoded, not yet handed out (interleaved)
        size_t pendingAt = 0;
        bool ended = false;
        uint32_t produced = 0;       // frames handed out
        uint32_t skip = 0;           // decoded frames still to drop (seek)
    };
    bool refill(Source& s, size_t frames);
    size_t avail(const Source& s) const { return (s.pending.size() - s.pendingAt) / size_t(s.channels); }

    const Disc* disc_ = nullptr;
    std::vector<std::unique_ptr<Source>> sources_;
    int rate_ = 44100;
    uint32_t length_ = 0;
    bool gh4_ = false;
    std::string error_;
};
