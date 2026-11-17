// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/at3_preview.h"

#include <algorithm>
#include <cstring>

#ifdef GD24_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}
#endif

namespace {

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint16_t le16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }

// KSDATAFORMAT_SUBTYPE for ATRAC3plus, as stored in the file.
const uint8_t kAtrac3PlusGuid[16] = {0xBF, 0xAA, 0x23, 0xE9, 0x58, 0xCB, 0x71, 0x44,
                                     0xA1, 0x19, 0xFF, 0xFA, 0x01, 0xE4, 0xCE, 0x62};

}  // namespace

bool at3Available() {
#ifdef GD24_FFMPEG
    return true;
#else
    return false;
#endif
}

bool decodeAt3(const std::vector<uint8_t>& b, PcmClip& out, std::string& error) {
    out = PcmClip{};
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) || std::memcmp(&b[8], "WAVE", 4)) {
        error = "not a RIFF WAVE";
        return false;
    }
    int channels = 0, rate = 0, blockAlign = 0;
    bool atrac = false;
    size_t dataAt = 0, dataSize = 0;
    for (size_t p = 12; p + 8 <= b.size();) {
        const uint32_t n = le32(&b[p + 4]);
        const uint8_t* body = &b[p + 8];
        if (p + 8 + n > b.size()) break;
        if (!std::memcmp(&b[p], "fmt ", 4) && n >= 40) {
            channels = le16(body + 2), rate = int(le32(body + 4)), blockAlign = le16(body + 12);
            atrac = le16(body) == 0xFFFE && !std::memcmp(body + 24, kAtrac3PlusGuid, 16);
        } else if (!std::memcmp(&b[p], "fact", 4) && n >= 8) {
            out.factSamples = le32(body), out.delay = le32(body + 4);
        } else if (!std::memcmp(&b[p], "data", 4)) {
            dataAt = p + 8, dataSize = n;
        }
        p += 8 + n + (n & 1);
    }
    if (!atrac || channels < 1 || channels > 2 || rate <= 0 || blockAlign <= 0 || !dataAt) {
        error = "not an ATRAC3plus RIFF WAVE (fmt / data chunks)";
        return false;
    }
#ifndef GD24_FFMPEG
    error = "this build has no FFmpeg (GD24_FFMPEG_DIR)";
    return false;
#else
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_ATRAC3P);
    AVCodecContext* ctx = codec ? avcodec_alloc_context3(codec) : nullptr;
    if (!ctx) {
        error = "FFmpeg has no atrac3p decoder";
        return false;
    }
    av_channel_layout_default(&ctx->ch_layout, channels);
    ctx->sample_rate = rate;
    ctx->block_align = blockAlign;
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        avcodec_free_context(&ctx);
        error = "cannot open the atrac3p decoder";
        return false;
    }
    out.rate = rate, out.channels = channels;
    std::vector<float> pcm;
    pcm.reserve(size_t(dataSize / size_t(blockAlign) + 1) * 2048 * size_t(channels));
    AVPacket* pkt = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    std::vector<uint8_t> buf(size_t(blockAlign) + AV_INPUT_BUFFER_PADDING_SIZE, 0);
    auto drain = [&] {
        while (avcodec_receive_frame(ctx, frame) == 0) {
            const int n = frame->nb_samples, ch = frame->ch_layout.nb_channels;
            const bool planar = frame->format == AV_SAMPLE_FMT_FLTP;
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < channels; ++c) {
                    const int src = std::min(c, ch - 1);
                    pcm.push_back(planar ? reinterpret_cast<const float*>(frame->extended_data[src])[i]
                                         : reinterpret_cast<const float*>(frame->extended_data[0])[i * ch + src]);
                }
            av_frame_unref(frame);
        }
    };
    for (size_t at = dataAt; at + size_t(blockAlign) <= dataAt + dataSize; at += size_t(blockAlign)) {
        std::memcpy(buf.data(), &b[at], size_t(blockAlign));
        pkt->data = buf.data(), pkt->size = blockAlign;
        ++out.packets;
        if (avcodec_send_packet(ctx, pkt) < 0) ++out.errors;
        drain();
    }
    avcodec_send_packet(ctx, nullptr);
    drain();
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);

    out.decodedFrames = pcm.size() / size_t(channels);
    const size_t skip = std::min<size_t>(out.delay, out.decodedFrames);
    size_t keep = out.decodedFrames - skip;
    if (out.factSamples) keep = std::min<size_t>(keep, out.factSamples);
    out.samples.assign(pcm.begin() + std::ptrdiff_t(skip * size_t(channels)),
                       pcm.begin() + std::ptrdiff_t((skip + keep) * size_t(channels)));
    if (out.factSamples > keep) out.samples.resize(size_t(out.factSamples) * size_t(channels), 0.0f);
    return true;
#endif
}
