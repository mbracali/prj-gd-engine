// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/xma_decoder.h"

#if GD04_VIDEO

#include <algorithm>
#include <cstring>
#include <memory>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
}

namespace xma {

namespace {

constexpr int kPacket = 2048;

std::string avError(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

void put16(uint8_t* p, uint32_t v) { p[0] = uint8_t(v), p[1] = uint8_t(v >> 8); }
void put32(uint8_t* p, uint32_t v) { put16(p, v), put16(p + 2, v >> 16); }

struct CodecFree {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct PacketFree {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct FrameFree {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};

}  // namespace

bool available() { return avcodec_find_decoder(AV_CODEC_ID_XMA2) != nullptr; }

bool decode(const uint8_t* data, size_t size, int rate, int channels, uint32_t frames, std::vector<float>& out,
            std::string& error, uint32_t* decodedFrames) {
    out.clear();
    if (decodedFrames) *decodedFrames = 0;
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_XMA2);
    if (!codec) return error = "this FFmpeg has no xma2 decoder", false;
    if (channels < 1 || channels > 2 || rate <= 0) return error = "unsupported XMA layout", false;
    std::unique_ptr<AVCodecContext, CodecFree> ctx(avcodec_alloc_context3(codec));
    if (!ctx) return error = "out of memory", false;
    ctx->sample_rate = rate;
    ctx->pkt_timebase = AVRational{1, rate};
    ctx->block_align = kPacket;
    av_channel_layout_default(&ctx->ch_layout, channels);
    // The XMA2WAVEFORMATEX extension (after cbSize, 34 bytes): one stream,
    // its channel mask; the rest (sample counts, block size) is not needed.
    ctx->extradata = static_cast<uint8_t*>(av_mallocz(34 + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!ctx->extradata) return error = "out of memory", false;
    ctx->extradata_size = 34;
    put16(ctx->extradata, 1);
    put32(ctx->extradata + 2, channels == 1 ? AV_CH_FRONT_CENTER : AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT);
    put32(ctx->extradata + 10, kPacket * 16);  // BytesPerBlock (XMA2's default block)
    put16(ctx->extradata + 32, uint32_t((size + kPacket - 1) / kPacket));
    if (int e = avcodec_open2(ctx.get(), codec, nullptr); e < 0) return error = "xma2: " + avError(e), false;

    std::unique_ptr<AVPacket, PacketFree> pkt(av_packet_alloc());
    std::unique_ptr<AVFrame, FrameFree> frame(av_frame_alloc());
    if (!pkt || !frame) return error = "out of memory", false;
    if (frames) out.reserve(size_t(frames) * size_t(channels));
    auto drain = [&]() -> bool {
        for (;;) {
            int e = avcodec_receive_frame(ctx.get(), frame.get());
            if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) return true;
            if (e < 0) return error = "xma2: " + avError(e), false;
            const int n = frame->nb_samples, ch = frame->ch_layout.nb_channels;
            if (frame->format != AV_SAMPLE_FMT_FLTP || ch != channels) return error = "xma2: unexpected output format", false;
            const size_t at = out.size();
            out.resize(at + size_t(n) * size_t(ch));
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c) out[at + size_t(i) * size_t(ch) + size_t(c)] = reinterpret_cast<const float*>(frame->data[c])[i];
            av_frame_unref(frame.get());
        }
    };
    for (size_t at = 0; at < size; at += kPacket) {
        const int n = int(std::min<size_t>(kPacket, size - at));
        if (av_new_packet(pkt.get(), n) < 0) return error = "out of memory", false;
        std::memcpy(pkt->data, data + at, size_t(n));
        // A timestamp (frames so far) lets the decoder trim its start skip
        // without complaining.
        pkt->pts = pkt->dts = int64_t(out.size() / size_t(channels));
        int e = avcodec_send_packet(ctx.get(), pkt.get());
        av_packet_unref(pkt.get());
        if (e < 0) return error = "xma2: " + avError(e) + " at packet " + std::to_string(at / kPacket), false;
        if (!drain()) return false;
    }
    if (int e = avcodec_send_packet(ctx.get(), nullptr); e < 0 && e != AVERROR_EOF) return error = "xma2: " + avError(e), false;
    if (!drain()) return false;
    const size_t got = out.size() / size_t(channels);
    if (decodedFrames) *decodedFrames = uint32_t(got);
    if (frames) out.resize(size_t(frames) * size_t(channels), 0.0f);
    return true;
}

}  // namespace xma

#else  // no FFmpeg

namespace xma {

bool available() { return false; }

bool decode(const uint8_t*, size_t, int, int, uint32_t, std::vector<float>& out, std::string& error, uint32_t* decodedFrames) {
    out.clear();
    if (decodedFrames) *decodedFrames = 0;
    error = "built without FFmpeg (GD04_VIDEO OFF): no XMA decoder";
    return false;
}

}  // namespace xma

#endif
