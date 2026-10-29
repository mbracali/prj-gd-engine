// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/xma_stream.h"

#if GD04_VIDEO

#include <cstring>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mem.h>
}

namespace {

std::string avError(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

}  // namespace

struct XmaStream::Impl {
    AVCodecContext* ctx = nullptr;
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    ~Impl() {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&ctx);
    }
    bool drain(int channels, std::vector<float>& out, std::string& error) {
        for (;;) {
            int e = avcodec_receive_frame(ctx, frame);
            if (e == AVERROR(EAGAIN) || e == AVERROR_EOF) return true;
            if (e < 0) return error = "xma: " + avError(e), false;
            const int n = frame->nb_samples, ch = frame->ch_layout.nb_channels;
            if (frame->format != AV_SAMPLE_FMT_FLTP || ch != channels) return error = "xma: unexpected output format", false;
            const size_t at = out.size();
            out.resize(at + size_t(n) * size_t(ch));
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c)
                    out[at + size_t(i) * size_t(ch) + size_t(c)] = reinterpret_cast<const float*>(frame->data[c])[i];
            av_frame_unref(frame);
        }
    }
};

XmaStream::XmaStream() = default;
XmaStream::~XmaStream() = default;

bool XmaStream::open(int version, int rate, int channels, std::string& error) {
    impl_ = std::make_unique<Impl>();
    channels_ = channels;
    frames_ = 0;
    const AVCodec* codec = avcodec_find_decoder(version == 1 ? AV_CODEC_ID_XMA1 : AV_CODEC_ID_XMA2);
    if (!codec) return error = "this FFmpeg has no XMA decoder", false;
    if (channels < 1 || channels > 2 || rate <= 0) return error = "unsupported XMA layout", false;
    Impl& m = *impl_;
    m.ctx = avcodec_alloc_context3(codec);
    m.pkt = av_packet_alloc();
    m.frame = av_frame_alloc();
    if (!m.ctx || !m.pkt || !m.frame) return error = "out of memory", false;
    m.ctx->sample_rate = rate;
    m.ctx->pkt_timebase = AVRational{1, rate};
    m.ctx->block_align = 2048;
    av_channel_layout_default(&m.ctx->ch_layout, channels);
    const int extra = version == 1 ? 28 : 34;
    m.ctx->extradata = static_cast<uint8_t*>(av_mallocz(size_t(extra) + AV_INPUT_BUFFER_PADDING_SIZE));
    if (!m.ctx->extradata) return error = "out of memory", false;
    m.ctx->extradata_size = extra;
    uint8_t* x = m.ctx->extradata;
    if (version == 1) {
        x[4] = 1;  // XMAWAVEFORMAT: one stream ...
        x[8 + 17] = uint8_t(channels);  // ... of this many channels (FFmpeg's reading of it)
    } else {
        x[0] = 1;  // XMA2WAVEFORMATEX extension: one stream, its channel mask
        const uint32_t mask = channels == 1 ? AV_CH_FRONT_CENTER : AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT;
        for (int i = 0; i < 4; ++i) x[2 + i] = uint8_t(mask >> (8 * i));
    }
    if (int e = avcodec_open2(m.ctx, codec, nullptr); e < 0) return error = "xma: " + avError(e), false;
    return true;
}

bool XmaStream::feed(const uint8_t* packet, size_t size, std::vector<float>& out, std::string& error) {
    if (!impl_ || !impl_->ctx) return error = "xma: not open", false;
    Impl& m = *impl_;
    if (av_new_packet(m.pkt, int(size)) < 0) return error = "out of memory", false;
    std::memcpy(m.pkt->data, packet, size);
    m.pkt->pts = m.pkt->dts = frames_;
    const size_t before = out.size();
    int e = avcodec_send_packet(m.ctx, m.pkt);
    av_packet_unref(m.pkt);
    if (e < 0) return error = "xma: " + avError(e), false;
    if (!m.drain(channels_, out, error)) return false;
    frames_ += int64_t((out.size() - before) / size_t(channels_));
    return true;
}

bool XmaStream::flush(std::vector<float>& out, std::string& error) {
    if (!impl_ || !impl_->ctx) return true;
    int e = avcodec_send_packet(impl_->ctx, nullptr);
    if (e < 0 && e != AVERROR_EOF) return error = "xma: " + avError(e), false;
    return impl_->drain(channels_, out, error);
}

#else

struct XmaStream::Impl {};
XmaStream::XmaStream() = default;
XmaStream::~XmaStream() = default;
bool XmaStream::open(int, int, int channels, std::string& error) {
    channels_ = channels;
    error = "built without FFmpeg (GD04_VIDEO OFF): no XMA decoder";
    return false;
}
bool XmaStream::feed(const uint8_t*, size_t, std::vector<float>&, std::string& error) {
    error = "no XMA decoder";
    return false;
}
bool XmaStream::flush(std::vector<float>&, std::string&) { return true; }

#endif
