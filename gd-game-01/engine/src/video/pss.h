// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "video/mpeg2.h"

// Streaming player core for Sony .pss movies (docs/video.md §2): an MPEG-2
// program stream with MPEG-2 video in PES 0xE0 and 16-bit PCM in private
// stream 1 (0xBD, sub-header FF A0 00 00, then an SShd/SSbd header and
// stereo data interleaved in 0x200-byte L/R blocks). The file is pulled
// through a reader callback in small chunks, never loaded whole.
class PssMovie {
public:
    // Reads up to n bytes into dst; returns the count, 0 at end of file.
    using Reader = std::function<size_t(uint8_t* dst, size_t n)>;

    // The PS2 plays the PCM through SPU2 AutoDMA at 48 kHz whatever SShd says.
    static constexpr int kAudioRate = 48000;
    static constexpr int kAudioChannels = 2;
    // One picture per NTSC TV frame.
    static constexpr double kFrameRate = 30000.0 / 1001.0;

    explicit PssMovie(Reader reader);
    // Reader over a file on disk; throws std::runtime_error if it can't be opened.
    static Reader fileReader(const std::string& path);

    // Decodes the next picture in display order (Mpeg2Frame::index counts
    // from 0), or nullptr at the end of the movie. The frame stays valid until
    // the next call. Throws std::runtime_error on malformed input.
    const Mpeg2Frame* nextFrame();

    // Appends the PCM demuxed so far (interleaved stereo s16, L R L R ...)
    // to out and drops it from the movie's buffer. Returns the stereo frames moved.
    size_t takeAudio(std::vector<int16_t>& out);
    // Stereo frames waiting in takeAudio's buffer.
    size_t audioFramesBuffered() const { return audio_.size() / 2; }
    // Demuxes ahead (buffering video too) until at least `frames` stereo
    // frames of audio are buffered or the file ends.
    void prefetchAudio(size_t frames);

    // Sample rate written in the SShd header (0 until seen); informational only.
    int headerAudioRate() const { return headerRate_; }
    // True once nextFrame() has returned nullptr.
    bool ended() const { return done_; }
    const Mpeg2Decoder& decoder() const { return video_; }

private:
    bool demuxPacket();  // false at end of file
    bool fill(size_t n);  // makes n bytes available at in_[pos_]
    void onAudio(const uint8_t* p, size_t n);

    Reader reader_;
    std::vector<uint8_t> in_;
    size_t pos_ = 0;
    bool eof_ = false, finished_ = false, done_ = false;
    Mpeg2Decoder video_;

    // Audio stream state.
    std::vector<uint8_t> audioHeader_;  // SShd + SSbd, 0x28 bytes
    uint64_t audioLeft_ = 0;             // SSbd bytes still to come
    size_t interleave_ = 0x200;
    std::vector<uint8_t> block_;        // partial L/R block pair
    std::vector<int16_t> audio_;
    int headerRate_ = 0;
};
