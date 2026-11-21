// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// An XMA1 / XMA2 decoder fed one 2048-byte packet at a time (the song
// stems are decoded while they play; audio/xma_decoder.h decodes whole
// sounds). FFmpeg's xma1 / xma2 decoders; without FFmpeg open() fails.
class XmaStream {
public:
    XmaStream();
    ~XmaStream();
    XmaStream(const XmaStream&) = delete;
    XmaStream& operator=(const XmaStream&) = delete;

    bool open(int version, int rate, int channels, std::string& error);
    // Decodes one packet; appends interleaved float frames to `out`.
    bool feed(const uint8_t* packet, size_t size, std::vector<float>& out, std::string& error);
    // The decoder's last frames, at the end of the stream.
    bool flush(std::vector<float>& out, std::string& error);
    int channels() const { return channels_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int channels_ = 0;
    int64_t frames_ = 0;
};
