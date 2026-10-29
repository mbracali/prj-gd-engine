// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "video/movie_player.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <stdexcept>

#include "app_ids.h"
#include "audio/mixer.h"
#include "data/disc.h"
#include "input/input.h"
#include "render/canvas.h"

#if GD04_VIDEO

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "audio/audio_source.h"
#include "devaids/screenshot.h"
#include "render/rmath.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace {

std::string avError(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return text;
}

// One decoded picture: tightly packed YUV 4:2:0 planes.
struct Frame {
    double ms = 0;  // presentation time from the start of the video
    int w = 0, h = 0;
    std::vector<uint8_t> y, u, v;
};

void copyPlane(std::vector<uint8_t>& dst, const uint8_t* src, int pitch, int w, int h) {
    dst.resize(size_t(w) * size_t(h));
    for (int row = 0; row < h; ++row) std::memcpy(&dst[size_t(row) * size_t(w)], src + ptrdiff_t(row) * pitch, size_t(w));
}

// The soundtrack between the decode thread (producer) and the mixer's
// worker (AudioSource::decode): interleaved float samples.
struct SoundQueue {
    std::mutex m;
    std::condition_variable cv;
    std::deque<float> samples;
    int channels = 2;
    bool ended = false;
    std::atomic<bool> abort{false};
};

class SoundSource final : public AudioSource {
public:
    SoundSource(std::shared_ptr<SoundQueue> q, int rate) : q_(std::move(q)), rate_(rate) {}
    int channels() const override { return q_->channels; }
    int sampleRate() const override { return rate_; }
    int decode(float* const* out, int frames) override {
        std::unique_lock lock(q_->m);
        const size_t ch = size_t(q_->channels);
        while (!q_->abort && !q_->ended && q_->samples.size() < size_t(frames) * ch)
            q_->cv.wait_for(lock, std::chrono::milliseconds(20));
        const int n = int(std::min<size_t>(size_t(frames), q_->samples.size() / ch));
        for (int i = 0; i < n; ++i)
            for (size_t c = 0; c < ch; ++c) {
                out[c][i] = q_->samples.front();
                q_->samples.pop_front();
            }
        lock.unlock();
        q_->cv.notify_all();
        return n;
    }

private:
    std::shared_ptr<SoundQueue> q_;
    int rate_;
};

// A file on the disc for FFmpeg's custom I/O.
struct DiscFile {
    const Disc* disc;
    std::string path;
    uint64_t length;
    uint64_t size() const { return length; }
    size_t read(uint64_t at, uint8_t* buf, size_t n) const {
        if (at >= length) return 0;
        n = size_t(std::min<uint64_t>(n, length - at));
        disc->readRange(path, at, buf, n);
        return n;
    }
};

// Disc file reads for FFmpeg's custom I/O.
int readPacket(void* opaque, uint8_t* buf, int size) {
    auto* f = static_cast<std::pair<DiscFile*, uint64_t>*>(opaque);
    size_t n = 0;
    try {
        n = f->first->read(f->second, buf, size_t(size));
    } catch (const std::exception&) {
        return AVERROR(EIO);
    }
    f->second += n;
    return n ? int(n) : AVERROR_EOF;
}

int64_t seekPacket(void* opaque, int64_t offset, int whence) {
    auto* f = static_cast<std::pair<DiscFile*, uint64_t>*>(opaque);
    const int64_t size = int64_t(f->first->size());
    switch (whence & ~AVSEEK_FORCE) {
    case AVSEEK_SIZE: return size;
    case SEEK_SET: break;
    case SEEK_CUR: offset += int64_t(f->second); break;
    case SEEK_END: offset += size; break;
    default: return -1;
    }
    if (offset < 0) return -1;
    f->second = uint64_t(offset);
    return offset;
}

constexpr size_t kQueueFrames = 24;     // decoded pictures kept ahead (~0.4 s at 59.94 fps)
constexpr size_t kQueueFramesMax = 240;  // ... more while the sound runs short (an uneven interleave)

}  // namespace

struct MoviePlayer::Impl {
    std::unique_ptr<DiscFile> file;
    std::pair<DiscFile*, uint64_t> io{nullptr, 0};
    std::string name;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* vctx = nullptr;
    AVCodecContext* actx = nullptr;
    int vIndex = -1, aIndex = -1;
    double startMs = 0;  // the earliest stream start
    AVPacket* pkt = nullptr;
    AVFrame* frame = nullptr;
    bool decodeErrorLogged = false;

    // Between the decode thread and the player.
    std::mutex m;
    std::condition_variable cv;
    std::deque<Frame> frames;
    bool videoEnded = false;
    std::atomic<bool> abort{false};
    std::shared_ptr<SoundQueue> sound = std::make_shared<SoundQueue>();
    std::thread thread;

    ~Impl() { close(); }

    void close() {
        stopThread();
        av_frame_free(&frame);
        av_packet_free(&pkt);
        avcodec_free_context(&vctx);
        avcodec_free_context(&actx);
        if (fmt) {
            AVIOContext* pb = fmt->pb;
            avformat_close_input(&fmt);
            if (pb) av_freep(&pb->buffer);
            avio_context_free(&pb);
        }
        file.reset();
    }

    void stopThread() {
        abort = true;
        sound->abort = true;
        cv.notify_all();
        sound->cv.notify_all();
        if (thread.joinable()) thread.join();
    }

    double toMs(int64_t pts, int stream) const {
        if (pts == AV_NOPTS_VALUE) return -1;
        return double(pts) * av_q2d(fmt->streams[stream]->time_base) * 1000.0 - startMs;
    }

    AVCodecContext* openDecoder(int stream, std::string& error) {
        const AVCodec* codec = avcodec_find_decoder(fmt->streams[stream]->codecpar->codec_id);
        if (!codec) {
            error = std::string("no decoder for ") + avcodec_get_name(fmt->streams[stream]->codecpar->codec_id);
            return nullptr;
        }
        AVCodecContext* c = avcodec_alloc_context3(codec);
        int r = c ? avcodec_parameters_to_context(c, fmt->streams[stream]->codecpar) : AVERROR(ENOMEM);
        if (r >= 0) {
            c->pkt_timebase = fmt->streams[stream]->time_base;
            r = avcodec_open2(c, codec, nullptr);
        }
        if (r < 0) {
            error = std::string(codec->name) + ": " + avError(r);
            avcodec_free_context(&c);
        }
        return c;
    }

    // Pushes one picture, waiting while the queue is full (it may grow while
    // the sound queue runs short, so a sound that trails the pictures in the
    // file never stalls). Returns false when aborted.
    bool pushFrame(Frame&& f) {
        std::unique_lock lock(m);
        for (;;) {
            if (abort) return false;
            size_t limit = kQueueFrames;
            if (aIndex >= 0) {
                std::lock_guard sl(sound->m);
                const size_t lowSound = size_t(actx->sample_rate) * size_t(sound->channels) / 4;  // 0.25 s
                if (!sound->ended && sound->samples.size() < lowSound) limit = kQueueFramesMax;
            }
            if (frames.size() < limit) break;
            cv.wait_for(lock, std::chrono::milliseconds(20));
        }
        frames.push_back(std::move(f));
        return true;
    }

    // Receives the decoded pictures of the packets sent so far into `out`
    // (the player's queue, or a dump); false to stop.
    template <typename Sink>
    bool receiveVideo(Sink&& sink) {
        for (;;) {
            const int r = avcodec_receive_frame(vctx, frame);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return true;
            if (r < 0) {
                logDecodeError(r);
                return true;
            }
            if (frame->format != AV_PIX_FMT_YUV420P && frame->format != AV_PIX_FMT_YUVJ420P) {
                SDL_Log("video %s: unexpected picture format %d", name.c_str(), frame->format);
                av_frame_unref(frame);
                return false;
            }
            Frame f;
            f.ms = toMs(frame->best_effort_timestamp, vIndex);
            f.w = frame->width, f.h = frame->height;
            const int cw = (f.w + 1) / 2, ch = (f.h + 1) / 2;
            copyPlane(f.y, frame->data[0], frame->linesize[0], f.w, f.h);
            copyPlane(f.u, frame->data[1], frame->linesize[1], cw, ch);
            copyPlane(f.v, frame->data[2], frame->linesize[2], cw, ch);
            av_frame_unref(frame);
            if (!sink(std::move(f))) return false;
        }
    }

    void receiveSound() {
        while (avcodec_receive_frame(actx, frame) == 0) {
            const int n = frame->nb_samples, ch = sound->channels;
            const auto sf = AVSampleFormat(frame->format);
            const bool planar = av_sample_fmt_is_planar(sf);
            std::vector<float> buf(size_t(n) * size_t(ch));
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < ch; ++c) {
                    const int plane = planar ? c : 0, at = planar ? i : i * ch + c;
                    float v = 0;
                    switch (av_get_packed_sample_fmt(sf)) {
                    case AV_SAMPLE_FMT_FLT: v = reinterpret_cast<const float*>(frame->extended_data[plane])[at]; break;
                    case AV_SAMPLE_FMT_S16: v = reinterpret_cast<const int16_t*>(frame->extended_data[plane])[at] / 32768.0f; break;
                    case AV_SAMPLE_FMT_S32: v = float(reinterpret_cast<const int32_t*>(frame->extended_data[plane])[at] / 2147483648.0); break;
                    default: break;
                    }
                    buf[size_t(i) * size_t(ch) + size_t(c)] = v;
                }
            av_frame_unref(frame);
            {
                std::lock_guard lock(sound->m);
                sound->samples.insert(sound->samples.end(), buf.begin(), buf.end());
            }
            sound->cv.notify_all();
        }
    }

    void logDecodeError(int r) {
        if (!decodeErrorLogged) SDL_Log("video %s: decode error: %s (later ones not logged)", name.c_str(), avError(r).c_str());
        decodeErrorLogged = true;
    }

    // The decode thread: demuxes and decodes both streams ahead of playback.
    void run() {
        auto sink = [this](Frame&& f) { return pushFrame(std::move(f)); };
        bool more = true;
        while (more && !abort) {
            const int r = av_read_frame(fmt, pkt);
            if (r < 0) break;
            if (pkt->stream_index == vIndex) {
                if (const int s = avcodec_send_packet(vctx, pkt); s < 0) logDecodeError(s);
                more = receiveVideo(sink);
            } else if (pkt->stream_index == aIndex) {
                if (const int s = avcodec_send_packet(actx, pkt); s < 0) logDecodeError(s);
                receiveSound();
            }
            av_packet_unref(pkt);
        }
        if (more && !abort) {  // the decoders' last pictures and samples
            avcodec_send_packet(vctx, nullptr);
            receiveVideo(sink);
            if (actx) {
                avcodec_send_packet(actx, nullptr);
                receiveSound();
            }
        }
        {
            std::lock_guard lock(m);
            videoEnded = true;
        }
        {
            std::lock_guard lock(sound->m);
            sound->ended = true;
        }
        cv.notify_all();
        sound->cv.notify_all();
    }
};

MoviePlayer::MoviePlayer() : impl_(std::make_unique<Impl>()) {}
MoviePlayer::~MoviePlayer() = default;

bool MoviePlayer::available() {
    static const bool ok = [] {
        const bool decoders = avcodec_find_decoder(AV_CODEC_ID_BINKVIDEO) && av_find_input_format("bink");
        const unsigned v = avcodec_version();
        SDL_Log("video: FFmpeg libavcodec %u.%u.%u (%s)%s", v >> 16, (v >> 8) & 0xff, v & 0xff, avcodec_license(),
                decoders ? "" : ": no Bink support in it, videos are skipped");
        return decoders;
    }();
    return ok;
}

bool MoviePlayer::open(const Disc& disc, const std::string& discPath, std::string& error) {
    Impl& d = *impl_;
    d.close();
    d.abort = false;
    d.frames.clear();
    d.videoEnded = false;
    d.sound = std::make_shared<SoundQueue>();
    d.name = discPath;
    if (!available()) {
        error = "the FFmpeg libraries lack the decoders GH3's videos need";
        return false;
    }
    try {
        auto len = disc.size(discPath);
        if (len) d.file = std::make_unique<DiscFile>(DiscFile{&disc, discPath, *len});
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
    if (!d.file) {
        error = "not on the disc";
        return false;
    }
    d.io = {d.file.get(), 0};
    constexpr int kIoBuffer = 64 * 1024;
    auto* buffer = static_cast<uint8_t*>(av_malloc(kIoBuffer));
    AVIOContext* pb = buffer ? avio_alloc_context(buffer, kIoBuffer, 0, &d.io, &readPacket, nullptr, &seekPacket) : nullptr;
    d.fmt = avformat_alloc_context();
    if (!pb || !d.fmt) {
        if (pb) av_freep(&pb->buffer);
        else av_free(buffer);
        avio_context_free(&pb);
        error = "out of memory";
        return false;
    }
    d.fmt->pb = pb;
    d.fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (const int r = avformat_open_input(&d.fmt, discPath.c_str(), av_find_input_format("bink"), nullptr); r < 0) {
        // avformat_open_input frees the context, not the custom I/O.
        av_freep(&pb->buffer);
        avio_context_free(&pb);
        error = "not an ASF file: " + avError(r);
        return false;
    }
    if (const int r = avformat_find_stream_info(d.fmt, nullptr); r < 0) {
        error = "no stream information: " + avError(r);
        return false;
    }
    d.vIndex = av_find_best_stream(d.fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    d.aIndex = av_find_best_stream(d.fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (d.vIndex < 0) {
        error = "no video stream";
        return false;
    }
    if (!(d.vctx = d.openDecoder(d.vIndex, error))) return false;
    if (d.aIndex >= 0) {
        std::string audioError;
        d.actx = d.openDecoder(d.aIndex, audioError);
        if (!d.actx) SDL_Log("video %s: no sound (%s)", discPath.c_str(), audioError.c_str()), d.aIndex = -1;
        else d.sound->channels = std::clamp(d.actx->ch_layout.nb_channels, 1, 8);
    }
    d.startMs = 1e30;
    for (int s : {d.vIndex, d.aIndex}) {
        if (s < 0) continue;
        const AVStream* st = d.fmt->streams[s];
        d.startMs = std::min(d.startMs, st->start_time == AV_NOPTS_VALUE ? 0.0 : double(st->start_time) * av_q2d(st->time_base) * 1000.0);
    }
    d.pkt = av_packet_alloc();
    d.frame = av_frame_alloc();
    if (!d.pkt || !d.frame) {
        error = "out of memory";
        return false;
    }
    const AVStream* vs = d.fmt->streams[d.vIndex];
    SDL_Log("video %s: %s %dx%d %.2f fps, %s, %.1f s", discPath.c_str(), avcodec_get_name(vs->codecpar->codec_id), vs->codecpar->width,
            vs->codecpar->height, av_q2d(vs->avg_frame_rate.num ? vs->avg_frame_rate : vs->r_frame_rate),
            d.actx ? (std::string(avcodec_get_name(d.actx->codec_id)) + " " + std::to_string(d.actx->sample_rate) + " Hz " +
                      std::to_string(d.sound->channels) + " ch").c_str()
                   : "no sound",
            d.fmt->duration > 0 ? double(d.fmt->duration) / AV_TIME_BASE : 0.0);
    return true;
}

int MoviePlayer::dumpFrames(int count, std::FILE* out) {
    Impl& d = *impl_;
    if (!d.fmt) return -1;
    int done = 0;
    auto sink = [&](Frame&& f) {
        std::fwrite(f.y.data(), 1, f.y.size(), out);
        std::fwrite(f.u.data(), 1, f.u.size(), out);
        std::fwrite(f.v.data(), 1, f.v.size(), out);
        return ++done < count;
    };
    bool more = true;
    while (more && av_read_frame(d.fmt, d.pkt) >= 0) {
        if (d.pkt->stream_index == d.vIndex) {
            if (const int s = avcodec_send_packet(d.vctx, d.pkt); s < 0) d.logDecodeError(s);
            more = d.receiveVideo(sink);
        }
        av_packet_unref(d.pkt);
    }
    if (more) {
        avcodec_send_packet(d.vctx, nullptr);
        d.receiveVideo(sink);
    }
    return done;
}

MoviePlayer::Result MoviePlayer::play(Canvas& canvas, Mixer& mixer, Input& input) {
    Impl& d = *impl_;
    if (!d.fmt) return Result::Failed;
    gfx::RenderDevice& dev = canvas.renderer().device();
    std::string error;
    const gfx::ProgramId program = dev.createProgram(gfx::Renderer::videoProgramDesc(), error);
    if (!program) {
        SDL_Log("video %s: the video shader does not compile: %s", d.name.c_str(), error.c_str());
        return Result::Failed;
    }
    const gfx::BufferId vbuf = dev.createBuffer(gfx::BufferKind::Vertex, 6 * sizeof(Vertex), nullptr, true);
    gfx::VertexLayout layout;
    layout.stride = sizeof(Vertex);
    layout.attribs[0] = {gfx::kAttribPos, 3, gfx::AttribType::Float, uint16_t(offsetof(Vertex, x))};
    layout.attribs[1] = {gfx::kAttribUv, 2, gfx::AttribType::Float, uint16_t(offsetof(Vertex, u))};
    layout.attribs[2] = {gfx::kAttribColor, 4, gfx::AttribType::UNorm8, uint16_t(offsetof(Vertex, r))};
    gfx::TextureId planes[3];
    int texW = 0, texH = 0;

    d.thread = std::thread([&d] { d.run(); });
    const bool sound = d.actx != nullptr;
    if (sound) {
        std::vector<Mixer::Track> tracks(size_t(d.sound->channels));
        if (tracks.size() == 2) tracks[0].pan = -1, tracks[1].pan = 1;
        // The sound's first sample plays at its own start time.
        const AVStream* as = d.fmt->streams[d.aIndex];
        const double soundStart = as->start_time == AV_NOPTS_VALUE ? 0.0 : double(as->start_time) * av_q2d(as->time_base) * 1000.0 - d.startMs;
        mixer.play(std::make_unique<SoundSource>(d.sound, d.actx->sample_rate), std::move(tracks), std::max(0.0, soundStart));
    }
    const uint64_t startNs = SDL_GetTicksNS();
    auto clockMs = [&] { return sound ? mixer.frameTimeMs() : double(SDL_GetTicksNS() - startNs) / 1e6; };

    Result result = Result::Finished;
    std::vector<InputEvent> actions;
    bool shown = false, running = true;
    int afterEnd = 0;
    while (running) {
        const uint64_t frameStart = SDL_GetTicksNS();
        SDL_Event e;
        actions.clear();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) result = Result::Quit, running = false;
            input.handle(e, actions);
        }
        input.poll(SDL_GetTicksNS(), actions);
        for (const InputEvent& a : actions)
            if (a.down && (a.action == Action::Start || a.action == Action::Confirm || a.action == Action::Back || a.action == Action::Fret0))
                result = Result::Skipped, running = false;  // as gd01 skips its videos
        if (!running) break;

        // The newest picture due by the sound's clock.
        const double now = clockMs();
        std::optional<Frame> due;
        bool ended = false;
        {
            std::lock_guard lock(d.m);
            while (!d.frames.empty() && d.frames.front().ms <= now) {
                due = std::move(d.frames.front());
                d.frames.pop_front();
            }
            ended = d.videoEnded && d.frames.empty();
        }
        d.cv.notify_all();
        if (due) {
            if (!planes[0] || texW != due->w || texH != due->h) {
                for (gfx::TextureId& t : planes) dev.destroyTexture(t), t = {};
                texW = due->w, texH = due->h;
                const int cw = (texW + 1) / 2, ch = (texH + 1) / 2;
                const gfx::TexLevel ly{due->y.data(), due->y.size()}, lu{due->u.data(), due->u.size()}, lv{due->v.data(), due->v.size()};
                planes[0] = dev.createTexture({texW, texH, 1, gfx::TexFormat::R8, gfx::TexKind::Tex2D}, &ly, error);
                planes[1] = dev.createTexture({cw, ch, 1, gfx::TexFormat::R8, gfx::TexKind::Tex2D}, &lu, error);
                planes[2] = dev.createTexture({cw, ch, 1, gfx::TexFormat::R8, gfx::TexKind::Tex2D}, &lv, error);
                if (!planes[0] || !planes[1] || !planes[2]) {
                    SDL_Log("video %s: cannot create the picture textures: %s", d.name.c_str(), error.c_str());
                    result = Result::Failed;
                    break;
                }
            } else {
                dev.updateTexture(planes[0], due->y.data(), size_t(texW));
                dev.updateTexture(planes[1], due->u.data(), size_t((texW + 1) / 2));
                dev.updateTexture(planes[2], due->v.data(), size_t((texW + 1) / 2));
            }
            shown = true;
        }
        // The end: GH2 shows two more frames after the last picture.
        if (ended && ++afterEnd > 2) break;

        canvas.begin(SDL_Color{0, 0, 0, 255});
        canvas.flush();
        if (shown) {
            // GH3's Bink videos are 1280x720 (a few 1280x780) [C data]: fitted
            // into the 1280x720 picture keeping their aspect [I].
            const float aspect = texH > 0 ? float(texW) / float(texH) : 16.0f / 9.0f;
            const float w = std::min(kScreenW, kScreenH * aspect), h = w / aspect;
            const float x0 = (kScreenW - w) / 2, x1 = x0 + w, y0 = (kScreenH - h) / 2, y1 = y0 + h;
            const Vertex a{x0, y0, 0, 0, 0, 255, 255, 255, 255}, b{x1, y0, 0, 1, 0, 255, 255, 255, 255};
            const Vertex c{x1, y1, 0, 1, 1, 255, 255, 255, 255}, e2{x0, y1, 0, 0, 1, 255, 255, 255, 255};
            const Vertex quad[6] = {a, b, c, a, c, e2};
            dev.updateBuffer(vbuf, 0, quad, sizeof quad);
            gfx::DrawCall call;
            call.program = program;
            call.layout = &layout;
            call.vertices = vbuf;
            call.count = 6;
            for (int i = 0; i < 3; ++i) call.textures[i] = planes[i], call.samplers[i] = {gfx::Filter::Linear, gfx::Wrap::Clamp, 0};
            const gfx::Mat4 proj = gfx::Mat4::ortho2D(0, kScreenW, 0, kScreenH);
            call.uniforms[0] = {proj.m, sizeof proj.m};
            dev.draw(call);
            if (captureDebugScreenshot(canvas, true, now)) result = Result::Quit, running = false;
        }
        canvas.end();
        // Without vsync (headless) keep to about 250 frames a second, as the app does.
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        if (spent < 4'000'000) SDL_DelayNS(4'000'000 - spent);
    }

    d.stopThread();
    if (sound) mixer.stop();
    for (gfx::TextureId t : planes) dev.destroyTexture(t);
    dev.destroyBuffer(vbuf);
    dev.destroyProgram(program);
    SDL_Log("video %s: %s", d.name.c_str(),
            result == Result::Finished ? "played to the end" : result == Result::Skipped ? "skipped" : result == Result::Quit ? "stopped (quit)" : "failed");
    return result;
}

#else  // !GD04_VIDEO: built without FFmpeg

struct MoviePlayer::Impl {};

MoviePlayer::MoviePlayer() = default;
MoviePlayer::~MoviePlayer() = default;

bool MoviePlayer::available() {
    static const bool logged = (SDL_Log("video: this build has no video support (built without FFmpeg); videos are skipped"), true);
    (void)logged;
    return false;
}

bool MoviePlayer::open(const Disc&, const std::string&, std::string& error) {
    available();
    error = "no video support in this build";
    return false;
}

MoviePlayer::Result MoviePlayer::play(Canvas&, Mixer&, Input&) { return Result::Unavailable; }

int MoviePlayer::dumpFrames(int, std::FILE*) { return -1; }

#endif  // GD04_VIDEO

MoviePlayer::Result playMovie(const Disc& disc, const std::string& name, Canvas& canvas, Mixer& mixer, Input& input) {
    if (const char* skip = SDL_getenv("GD04_SKIP_MOVIES"); skip && *skip && *skip != '0') {
        SDL_Log("video %s: skipped (GD04_SKIP_MOVIES)", name.c_str());
        return MoviePlayer::Result::Unavailable;
    }
    if (!MoviePlayer::available()) {
        SDL_Log("video %s: skipped (no video support)", name.c_str());
        return MoviePlayer::Result::Unavailable;
    }
    MoviePlayer player;
    std::string error;
    if (!player.open(disc, "DATA/MOVIES/BIK/" + name + ".bik.xen", error)) {
        SDL_Log("video %s: cannot play: %s", name.c_str(), error.c_str());
        return MoviePlayer::Result::Failed;
    }
    return player.play(canvas, mixer, input);
}

int runPlayMovie(const std::string& dataPath, const std::string& name) {
    std::string openError;
    std::unique_ptr<Disc> disc = Disc::open(dataPath, openError);
    if (!disc) {
        std::fprintf(stderr, "%s: %s\n", app::kProgram, openError.c_str());
        return 2;
    }

    // Decode only (no window): the pictures' planes for comparisons.
    if (const char* dump = SDL_getenv("GD04_MOVIE_DUMP")) {
        const char* n = SDL_getenv("GD04_MOVIE_FRAMES");
        const int frames = n ? std::atoi(n) : 120;
        MoviePlayer player;
        std::string error;
        if (!player.open(*disc, "DATA/MOVIES/BIK/" + name + ".bik.xen", error)) {
            std::fprintf(stderr, "%s: videos/%s: %s\n", app::kProgram, name.c_str(), error.c_str());
            return 1;
        }
        std::FILE* out = std::fopen(dump, "wb");
        if (!out) {
            std::fprintf(stderr, "%s: cannot write %s\n", app::kProgram, dump);
            return 1;
        }
        const uint64_t t0 = SDL_GetTicksNS();
        const int done = player.dumpFrames(frames, out);
        std::fclose(out);
        std::printf("decoded %d pictures of videos/%s into %s (%.0f ms)\n", done, name.c_str(), dump, double(SDL_GetTicksNS() - t0) / 1e6);
        return done == frames ? 0 : 1;
    }

    Input::prepareSdl();
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "%s: cannot start SDL: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), SDL_WINDOW_RESIZABLE | gfx::prepareWindow());
    int code = 2;
    {
        Canvas canvas;
        Mixer mixer;
        Input input;
        std::string error;
        if (!window || !canvas.init(window, error)) {
            std::fprintf(stderr, "%s: cannot open the window: %s\n", app::kProgram, window ? error.c_str() : SDL_GetError());
        } else if (!mixer.open(error)) {
            std::fprintf(stderr, "%s: cannot open the audio device: %s\n", app::kProgram, error.c_str());
        } else {
            const MoviePlayer::Result r = playMovie(*disc, name, canvas, mixer, input);
            code = r == MoviePlayer::Result::Failed || r == MoviePlayer::Result::Unavailable ? 1 : 0;
        }
        input.close();
        mixer.close();
        canvas.shutdown();
    }
    if (window) SDL_DestroyWindow(window);
    SDL_Quit();
    return code;
}
