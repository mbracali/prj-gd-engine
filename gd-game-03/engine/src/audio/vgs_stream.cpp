// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/vgs_stream.h"

#include <algorithm>
#include <stdexcept>

namespace {

constexpr int kFilters[5][2] = {{0, 0}, {60, 0}, {115, -52}, {98, -55}, {122, -60}};
constexpr size_t kHeaderSize = 0x80;
constexpr size_t kBlockSize = 16;

}  // namespace

void probeVgs(const uint8_t* data, size_t size, int& channels, int& rate) {
    ByteReader r(data, size);
    if (r.str(4) != "VgS!") throw std::runtime_error("not a VGS file");
    if (r.u32le() != 2) throw std::runtime_error("unsupported VGS version");
    channels = 0;
    rate = 0;
    for (int i = 0; i < 15; ++i) {
        uint32_t chRate = r.u32le();
        r.u32le();
        if (chRate == 0) break;
        if (rate == 0) rate = static_cast<int>(chRate);
        else if (static_cast<int>(chRate) != rate) throw std::runtime_error("VGS channels differ in sample rate");
        ++channels;
    }
    if (channels == 0) throw std::runtime_error("VGS file has no channels");
}

VgsStream::VgsStream(Bytes data) : data_(std::move(data)) {
    ByteReader r(data_);
    if (r.str(4) != "VgS!") throw std::runtime_error("not a VGS file");
    if (r.u32le() != 2) throw std::runtime_error("unsupported VGS version");
    std::vector<uint32_t> declared;
    for (int i = 0; i < 15; ++i) {
        uint32_t rate = r.u32le(), blocks = r.u32le();
        if (rate == 0) break;
        if (channels_.empty()) sampleRate_ = static_cast<int>(rate);
        else if (static_cast<int>(rate) != sampleRate_) throw std::runtime_error("VGS channels differ in sample rate");
        channels_.emplace_back();
        declared.push_back(blocks);
    }
    if (channels_.empty()) throw std::runtime_error("VGS file has no channels");
    for (size_t i = 0; i < channels_.size(); ++i) channels_[i].blocks.reserve(declared[i]);

    for (size_t off = kHeaderSize; off + kBlockSize <= data_.size(); off += kBlockSize) {
        size_t ch = data_[off + 1] & 0x7F;
        if (ch < channels_.size()) channels_[ch].blocks.push_back(static_cast<uint32_t>(off));
    }
    for (const auto& ch : channels_) lengthFrames_ = std::max<uint64_t>(lengthFrames_, ch.blocks.size() * 28);
}

void VgsStream::decodeBlock(Channel& ch) {
    ch.pos = 0;
    if (ch.nextBlock >= ch.blocks.size()) {
        ch.samples.fill(0);
        return;
    }
    const uint8_t* b = &data_[ch.blocks[ch.nextBlock++]];
    int shift = b[0] & 0x0F;
    int filter = std::min(b[0] >> 4, 4);
    int f0 = kFilters[filter][0], f1 = kFilters[filter][1];
    for (int i = 0; i < 28; ++i) {
        int nibble = (b[2 + i / 2] >> ((i & 1) * 4)) & 0x0F;
        int s = static_cast<int16_t>(nibble << 12) >> shift;
        s += (ch.s1 * f0 + ch.s2 * f1 + 32) >> 6;
        s = std::clamp(s, -32768, 32767);
        ch.samples[i] = static_cast<int16_t>(s);
        ch.s2 = ch.s1;
        ch.s1 = s;
    }
}

int VgsStream::decode(float* const* out, int frames) {
    const int n = static_cast<int>(std::min<uint64_t>(frames, lengthFrames_ - std::min(played_, lengthFrames_)));
    for (size_t c = 0; c < channels_.size(); ++c) {
        Channel& ch = channels_[c];
        for (int i = 0; i < n; ++i) {
            if (ch.pos == 28) decodeBlock(ch);
            out[c][i] = ch.samples[ch.pos++] * (1.0f / 32768.0f);
        }
    }
    played_ += n;
    return n;
}

bool VgsStream::seek(uint64_t frame) {
    if (frame > lengthFrames_) return false;
    for (Channel& ch : channels_) {
        ch.nextBlock = std::min<size_t>(frame / 28, ch.blocks.size());
        ch.s1 = ch.s2 = 0;
        ch.pos = 28;
        if (frame % 28 && ch.nextBlock < ch.blocks.size()) {
            decodeBlock(ch);
            ch.pos = static_cast<int>(frame % 28);
        }
    }
    played_ = frame;
    return true;
}
