// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/vorbis_stream.h"

#include <stdexcept>
#include <string>

#include "data/mogg.h"

// The declarations only; the decoder itself is compiled from
// third_party/stb_vorbis/stb_vorbis.c (CMakeLists.txt, which also defines
// STB_VORBIS_NO_STDIO for both).
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

namespace {
// GH2's song moggs have at most 6 channels [C data]; stb_vorbis's own limit is 16.
constexpr int kMaxChannels = 16;
}  // namespace

VorbisStream::VorbisStream(Bytes mogg) : data_(std::move(mogg)) {
    MoggHeader h = parseMoggHeader(data_.data(), data_.size());
    int error = 0;
    v_ = stb_vorbis_open_memory(data_.data() + h.oggOffset, int(data_.size() - h.oggOffset), &error, nullptr);
    if (!v_) throw std::runtime_error("cannot decode the mogg's Ogg Vorbis stream (stb_vorbis error " + std::to_string(error) + ")");
    stb_vorbis_info info = stb_vorbis_get_info(v_);
    channels_ = info.channels;
    rate_ = int(info.sample_rate);
    length_ = stb_vorbis_stream_length_in_samples(v_);
    if (channels_ < 1 || channels_ > kMaxChannels) {
        stb_vorbis_close(v_);
        v_ = nullptr;
        throw std::runtime_error("unsupported channel count in the mogg");
    }
}

VorbisStream::~VorbisStream() {
    if (v_) stb_vorbis_close(v_);
}

int VorbisStream::decode(float* const* channelBuffers, int frames) {
    int done = 0;
    while (done < frames) {
        float* at[kMaxChannels];
        for (int c = 0; c < channels_; ++c) at[c] = channelBuffers[c] + done;
        int n = stb_vorbis_get_samples_float(v_, channels_, at, frames - done);
        if (n <= 0) break;
        done += n;
    }
    return done;
}

bool VorbisStream::seek(uint64_t frame) { return stb_vorbis_seek(v_, static_cast<unsigned int>(frame)) != 0; }

void probeMogg(const uint8_t* data, size_t size, int& channels, int& rate) {
    MoggHeader h = parseMoggHeader(data, size);
    if (h.oggOffset >= size) throw std::runtime_error("mogg header longer than the bytes read");
    // The pushdata API reads the three Vorbis headers from the bytes given,
    // without needing the rest of the stream.
    int used = 0, error = 0;
    stb_vorbis* v = stb_vorbis_open_pushdata(data + h.oggOffset, int(size - h.oggOffset), &used, &error, nullptr);
    if (!v) throw std::runtime_error("cannot read the Vorbis headers (stb_vorbis error " + std::to_string(error) + ")");
    stb_vorbis_info info = stb_vorbis_get_info(v);
    channels = info.channels;
    rate = int(info.sample_rate);
    stb_vorbis_close(v);
}
