// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Xbox 360 XMA2 (GH2's SynthSample data, docs/audio_and_charts.md "Sound
// effects") -> float PCM, through FFmpeg's xma2 decoder (LGPL, the shared
// libraries the videos use). The data: one stream, 2048-byte packets in
// stream order, each with the XMA2 header (6 bits frame count, 15 bits
// first-frame bit offset, 3 bits metadata = 1, 8 bits packet skip = 0); a
// last padding packet (0x03FFF800) may follow.
//
// Without FFmpeg (GD04_VIDEO OFF) available() is false and decode fails.
namespace xma {

bool available();

// Decodes `size` bytes of `channels`-channel XMA2 at `rate` into
// interleaved float samples, trimmed (or zero-padded) to `frames` frames
// when `frames` > 0. Returns false with `error` on a decoder error.
// `decodedFrames` (optional) gets what the decoder produced before the trim.
bool decode(const uint8_t* data, size_t size, int rate, int channels, uint32_t frames, std::vector<float>& out,
            std::string& error, uint32_t* decodedFrames = nullptr);

}  // namespace xma
