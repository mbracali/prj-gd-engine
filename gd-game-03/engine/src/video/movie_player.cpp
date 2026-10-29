// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "video/movie_player.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "app_ids.h"
#include "audio/audio_source.h"
#include "audio/mixer.h"
#include "data/game_data.h"
#include "devaids/screenshot.h"
#include "input/input.h"
#include "render/canvas.h"
#include "render/renderer.h"
#include "render/rmath.h"
#include "video/pss.h"

namespace {

// The movie's PCM between the main thread (which demuxes) and the mixer's
// worker: interleaved stereo s16. Underruns play silence while the movie runs.
struct SoundQueue {
    std::mutex m;
    std::deque<int16_t> samples;
    bool ended = false;
};

class QueueSource final : public AudioSource {
public:
    explicit QueueSource(std::shared_ptr<SoundQueue> q) : q_(std::move(q)) {}
    int channels() const override { return PssMovie::kAudioChannels; }
    int sampleRate() const override { return PssMovie::kAudioRate; }
    int decode(float* const* out, int frames) override {
        std::lock_guard lock(q_->m);
        if (q_->ended && q_->samples.empty()) return 0;
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c) {
                int16_t v = 0;
                if (!q_->samples.empty()) v = q_->samples.front(), q_->samples.pop_front();
                out[c][i] = float(v) * (1.0f / 32768.0f);
            }
        return frames;
    }

private:
    std::shared_ptr<SoundQueue> q_;
};

// One plane, tightly packed. An interlaced picture's two fields are blended
// line with next line (as gd-game-01's mpeg2ToRgba does on the PS2 movies).
void copyPlane(std::vector<uint8_t>& dst, const uint8_t* src, int stride, int w, int h, bool blend) {
    dst.resize(size_t(w) * size_t(h));
    for (int row = 0; row < h; ++row) {
        uint8_t* out = &dst[size_t(row) * size_t(w)];
        const uint8_t* a = src + ptrdiff_t(row) * stride;
        if (!blend || row + 1 >= h) {
            std::memcpy(out, a, size_t(w));
            continue;
        }
        const uint8_t* b = a + stride;
        for (int x = 0; x < w; ++x) out[x] = uint8_t((a[x] + b[x] + 1) >> 1);
    }
}

}  // namespace

struct MoviePlayer::Impl {
    std::string name;
    std::unique_ptr<DiscFile> file;
    uint64_t offset = 0;
    std::unique_ptr<PssMovie> movie;
};

MoviePlayer::MoviePlayer() : impl_(std::make_unique<Impl>()) {}
MoviePlayer::~MoviePlayer() = default;

bool MoviePlayer::open(const GameData& data, const std::string& discPath, std::string& error) {
    Impl& d = *impl_;
    d.name = discPath;
    d.file = data.openDiscFile(discPath);
    if (!d.file) return error = "not on the disc", false;
    d.offset = 0;
    DiscFile* f = d.file.get();
    uint64_t* off = &d.offset;
    d.movie = std::make_unique<PssMovie>([f, off](uint8_t* dst, size_t n) {
        const size_t got = f->read(*off, dst, n);
        *off += got;
        return got;
    });
    return true;
}

int MoviePlayer::decodeFrames(int frames) {
    Impl& d = *impl_;
    if (!d.movie) return -1;
    int n = 0;
    try {
        std::vector<int16_t> pcm;
        while (n < frames && d.movie->nextFrame()) ++n, d.movie->takeAudio(pcm);
    } catch (const std::exception& e) {
        SDL_Log("video %s: %s", d.name.c_str(), e.what());
        return -1;
    }
    return n;
}

MoviePlayer::Result MoviePlayer::play(Canvas& canvas, Mixer& mixer, Input& input) {
    Impl& d = *impl_;
    if (!d.movie) return Result::Failed;
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
    std::vector<uint8_t> y, u, v;

    // The sound: half a second buffered first, then fed as the pictures go.
    auto queue = std::make_shared<SoundQueue>();
    std::vector<int16_t> pcm;
    Result result = Result::Finished;
    try {
        d.movie->prefetchAudio(PssMovie::kAudioRate / 2);
        d.movie->takeAudio(pcm);
    } catch (const std::exception& e) {
        SDL_Log("video %s: %s", d.name.c_str(), e.what());
        result = Result::Failed;
    }
    {
        std::lock_guard lock(queue->m);
        queue->samples.insert(queue->samples.end(), pcm.begin(), pcm.end());
    }
    std::vector<Mixer::Track> tracks(2);
    tracks[0].pan = -1, tracks[1].pan = 1;
    if (result != Result::Failed) mixer.play(std::make_unique<QueueSource>(queue), std::move(tracks), 0.0);

    std::vector<InputEvent> actions;
    bool shown = false, running = result != Result::Failed, ended = false;
    int afterEnd = 0;
    uint64_t pictures = 0;
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
                result = Result::Skipped, running = false;
        if (!running) break;

        // The pictures due by the sound's clock (one per NTSC frame).
        const double now = mixer.frameTimeMs();
        const Mpeg2Frame* due = nullptr;
        try {
            while (!ended && double(pictures) * 1000.0 / PssMovie::kFrameRate <= now) {
                const Mpeg2Frame* f = d.movie->nextFrame();
                if (!f) {
                    ended = true;
                    break;
                }
                due = f;
                ++pictures;
            }
            pcm.clear();
            d.movie->takeAudio(pcm);
        } catch (const std::exception& ex) {
            SDL_Log("video %s: %s", d.name.c_str(), ex.what());
            result = Result::Failed;
            break;
        }
        {
            std::lock_guard lock(queue->m);
            queue->samples.insert(queue->samples.end(), pcm.begin(), pcm.end());
            queue->ended = ended;
        }
        if (due) {
            const int w = due->width, h = due->height, cw = w / 2, ch = h / 2;
            const bool blend = !due->progressive;
            copyPlane(y, due->y.data(), due->lumaStride, w, h, blend);
            copyPlane(u, due->cb.data(), due->chromaStride, cw, ch, false);
            copyPlane(v, due->cr.data(), due->chromaStride, cw, ch, false);
            if (!planes[0] || texW != w || texH != h) {
                for (gfx::TextureId& t : planes) dev.destroyTexture(t), t = {};
                texW = w, texH = h;
                const gfx::TexLevel ly{y.data(), y.size()}, lu{u.data(), u.size()}, lv{v.data(), v.size()};
                planes[0] = dev.createTexture({w, h, 1, gfx::TexFormat::R8, gfx::TexKind::Tex2D}, &ly, error);
                planes[1] = dev.createTexture({cw, ch, 1, gfx::TexFormat::R8, gfx::TexKind::Tex2D}, &lu, error);
                planes[2] = dev.createTexture({cw, ch, 1, gfx::TexFormat::R8, gfx::TexKind::Tex2D}, &lv, error);
                if (!planes[0] || !planes[1] || !planes[2]) {
                    SDL_Log("video %s: cannot create the picture textures: %s", d.name.c_str(), error.c_str());
                    result = Result::Failed;
                    break;
                }
            } else {
                dev.updateTexture(planes[0], y.data(), size_t(w));
                dev.updateTexture(planes[1], u.data(), size_t(cw));
                dev.updateTexture(planes[2], v.data(), size_t(cw));
            }
            shown = true;
        }
        // Two more frames after the last picture, as gd02's player.
        if (ended && ++afterEnd > 2) break;

        canvas.begin(SDL_Color{0, 0, 0, 255});
        canvas.flush();
        if (shown) {
            // 4:3 in the middle of the 1280x720 frame: 960x720, pillarboxed
            // (the whole picture when it is 4:3).
            constexpr float w = kScreenH * 4 / 3, x0 = (kScreenW - w) / 2, x1 = x0 + w, y1 = kScreenH;
            const Vertex a{x0, 0, 0, 0, 0, 255, 255, 255, 255}, b{x1, 0, 0, 1, 0, 255, 255, 255, 255};
            const Vertex c{x1, y1, 0, 1, 1, 255, 255, 255, 255}, e2{x0, y1, 0, 0, 1, 255, 255, 255, 255};
            const Vertex quad[6] = {a, b, c, a, c, e2};
            dev.updateBuffer(vbuf, 0, quad, sizeof quad);
            gfx::DrawCall call;
            call.program = program;
            call.layout = &layout;
            call.vertices = vbuf;
            call.count = 6;
            for (int i = 0; i < 3; ++i) call.textures[i] = planes[i], call.samplers[i] = {gfx::Filter::Linear, gfx::Wrap::Clamp, 0};
            const gfx::Mat4 proj = canvas.projection();
            call.uniforms[0] = {proj.m, sizeof proj.m};
            dev.draw(call);
            if (captureDebugScreenshot(canvas, true, now)) result = Result::Quit, running = false;
        }
        canvas.end();
        const uint64_t spent = SDL_GetTicksNS() - frameStart;
        if (spent < 4'000'000) SDL_DelayNS(4'000'000 - spent);
    }

    mixer.stop();
    for (gfx::TextureId t : planes) dev.destroyTexture(t);
    dev.destroyBuffer(vbuf);
    dev.destroyProgram(program);
    SDL_Log("video %s: %s (%llu pictures)", d.name.c_str(),
            result == Result::Finished ? "played to the end" : result == Result::Skipped ? "skipped" : result == Result::Quit ? "stopped (quit)" : "failed",
            static_cast<unsigned long long>(pictures));
    return result;
}

MoviePlayer::Result playMovie(const GameData& data, const std::string& name, Canvas& canvas, Mixer& mixer, Input& input) {
    if (const char* skip = SDL_getenv("GD03_SKIP_MOVIES"); skip && *skip && *skip != '0') {
        SDL_Log("video %s: skipped (GD03_SKIP_MOVIES)", name.c_str());
        return MoviePlayer::Result::Unavailable;
    }
    MoviePlayer player;
    std::string error;
    if (!player.open(data, "videos/" + name, error)) {
        SDL_Log("video %s: cannot play: %s", name.c_str(), error.c_str());
        return MoviePlayer::Result::Failed;
    }
    return player.play(canvas, mixer, input);
}

int runPlayMovie(const std::string& dataPath, const std::string& name) {
    std::unique_ptr<GameData> data;
    try {
        data = std::make_unique<GameData>(dataPath);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "%s: %s\n", app::kProgram, ex.what());
        return 2;
    }
    if (SDL_getenv("GD03_MOVIE_DUMP")) {
        const char* n = SDL_getenv("GD03_MOVIE_FRAMES");
        const int frames = n ? std::atoi(n) : 120;
        MoviePlayer player;
        std::string error;
        if (!player.open(*data, "videos/" + name, error)) {
            std::fprintf(stderr, "%s: videos/%s: %s\n", app::kProgram, name.c_str(), error.c_str());
            return 1;
        }
        const uint64_t t0 = SDL_GetTicksNS();
        const int done = player.decodeFrames(frames);
        std::printf("decoded %d pictures of videos/%s (%.0f ms)\n", done, name.c_str(), double(SDL_GetTicksNS() - t0) / 1e6);
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
            const MoviePlayer::Result r = playMovie(*data, name, canvas, mixer, input);
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
