// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// <SONG>_PREVIEW.AT3 (docs/FORMATS.md): a RIFF WAVE with a
// WAVEFORMATEXTENSIBLE ATRAC3plus format (stereo, 44.1 kHz, 2048-byte
// frames), decoded with FFmpeg's atrac3p decoder. Chunks [C data, all 41]:
//   fmt   0xFFFE, channels, rate, block align 0x800, ATRAC3plus GUID
//   fact  {u32 samples, u32 encoder delay}: e.g. ABC 1,293,970 and 3,070
//   smpl  one loop {start = delay, end = delay + samples - 1}
//   data  the frames, 2048 samples each
// The decoded PCM starts after the encoder delay and is `samples` long (the
// last frame may end a few hundred samples early: the rest is silence).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct PcmClip {
    int rate = 0, channels = 0;
    std::vector<float> samples;  // interleaved, `frames() * channels`
    uint32_t factSamples = 0, delay = 0;  // the fact chunk
    int packets = 0, errors = 0;          // frames decoded, frames FFmpeg refused
    size_t decodedFrames = 0;             // before the delay was cut
    size_t frames() const { return channels ? samples.size() / size_t(channels) : 0; }
    double seconds() const { return rate ? double(frames()) / rate : 0; }
};

// false + `error` when the file is not this format or FFmpeg is missing /
// cannot open the decoder. Decode errors in single frames are counted.
bool decodeAt3(const std::vector<uint8_t>& riff, PcmClip& out, std::string& error);
// Whether this build has FFmpeg (GD24_FFMPEG).
bool at3Available();
