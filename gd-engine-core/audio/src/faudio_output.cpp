// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
//
// The FAudio backend (the XAudio2 reimplementation, zlib, built in statically
// on its SDL3 platform): an engine, a mastering voice at the asked format and
// one float source voice fed from a ring of buffers. Each OnBufferEnd (FAudio's
// mixing thread, [C] called with FAudio's voice locks released, so a buffer may
// be submitted from it, as XAudio2 allows) pulls the next buffer from the game.
// Headless, FAudio plays through SDL's dummy audio driver.
#include <SDL3/SDL.h>

#include <FAudio.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>

#include "backends.h"

namespace gd::core::audio::detail {
namespace {

class FAudioOutput final : public Output {
public:
    FAudioOutput() {
        std::memset(&callbacks_.cb, 0, sizeof callbacks_.cb);
        callbacks_.cb.OnBufferEnd = &FAudioOutput::onBufferEnd;
        callbacks_.self = this;
    }
    ~FAudioOutput() override { close(); }

    bool open(const Spec& want, PullCallback pull, std::string& error) override {
        close();
        if (!pull) {
            error = "FAudio: no pull callback";
            return false;
        }
        pull_ = std::move(pull);
        // FAudio starts SDL's audio itself; headless, on the dummy driver
        // (only a hint: a game that already started SDL audio keeps its driver).
        if (want.nullDevice && !SDL_WasInit(SDL_INIT_AUDIO)) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        if (FAudioCreate(&audio_, 0, FAUDIO_DEFAULT_PROCESSOR) != 0 || !audio_) {
            error = "FAudio: cannot create the engine";
            audio_ = nullptr;
            return false;
        }
        const int channels = std::clamp(want.channels, 1, 2);
        const int rate = std::max(want.sampleRate, 8000);
        if (FAudio_CreateMasteringVoice(audio_, &master_, uint32_t(channels), uint32_t(rate), 0, 0, nullptr) != 0 || !master_) {
            error = std::string("FAudio: cannot open the playback device (") + SDL_GetError() + ")";
            master_ = nullptr;
            close();
            return false;
        }
        FAudioWaveFormatEx fmt{};
        fmt.wFormatTag = FAUDIO_FORMAT_IEEE_FLOAT;
        fmt.nChannels = uint16_t(channels);
        fmt.nSamplesPerSec = uint32_t(rate);
        fmt.wBitsPerSample = 32;
        fmt.nBlockAlign = uint16_t(channels * 4);
        fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        fmt.cbSize = 0;
        if (FAudio_CreateSourceVoice(audio_, &source_, &fmt, FAUDIO_VOICE_NOPITCH, 1.0f, &callbacks_.cb, nullptr, nullptr) != 0 ||
            !source_) {
            error = "FAudio: cannot create the source voice";
            source_ = nullptr;
            close();
            return false;
        }
        channels_ = channels;
        // [I] A period of ~10 ms by default (FAudio's own update on SDL is
        // rate / 100); kBuffers of them queued.
        period_ = want.framesHint > 0 ? std::clamp(want.framesHint, 64, 16384) : std::max(64, rate / 100);
        for (auto& b : buffers_) b.assign(size_t(period_) * size_t(channels), 0.0f);
        spec_.sampleRate = rate;
        spec_.channels = channels;
        spec_.framesHint = period_;
        FAudioPerformanceData perf{};
        FAudio_GetPerformanceData(audio_, &perf);
        latencyMs_ = std::min(250.0, (double(kBuffers) * period_ + perf.CurrentLatencyInSamples) * 1000.0 / rate);
        const uint32_t v = FAudioLinkedVersion();
        const char* drv = SDL_GetCurrentAudioDriver();
        name_ = "FAudio " + std::to_string(v / 10000) + "." + std::to_string(v / 100 % 100) + " (SDL " +
                (drv ? drv : "audio") + ")";
        return true;
    }

    bool start(std::string& error) override {
        if (!source_) {
            error = "FAudio: not open";
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(pullMutex_);
            paused_ = false;
            // The first start queues every buffer; later ones resume the queue.
            if (!primed_) {
                for (size_t i = 0; i < buffers_.size(); ++i) {
                    pull_(buffers_[i].data(), period_);
                    submit(i);
                }
                primed_ = true;
            }
        }
        if (FAudioSourceVoice_Start(source_, 0, FAUDIO_COMMIT_NOW) != 0) {
            error = "FAudio: cannot start the source voice";
            return false;
        }
        return true;
    }

    void pause() override {
        if (!source_) return;
        FAudioSourceVoice_Stop(source_, 0, FAUDIO_COMMIT_NOW);
        // FAudio's thread may be in the callback: once this lock is held it
        // is out, and later buffers are silence.
        std::lock_guard<std::mutex> lock(pullMutex_);
        paused_ = true;
    }

    void close() override {
        if (source_) {
            FAudioSourceVoice_Stop(source_, 0, FAUDIO_COMMIT_NOW);
            {
                std::lock_guard<std::mutex> lock(pullMutex_);
                closing_ = true;
            }
            FAudioVoice_DestroyVoice(source_);  // waits for the mixing pass
            source_ = nullptr;
        }
        if (master_) FAudioVoice_DestroyVoice(master_);
        master_ = nullptr;
        if (audio_) FAudio_Release(audio_);  // stops the engine and its SDL device
        audio_ = nullptr;
        primed_ = false, closing_ = false, paused_ = true;
    }

    Spec spec() const override { return spec_; }
    double latencyMs() const override { return latencyMs_; }
    const char* id() const override { return "faudio"; }
    std::string name() const override { return name_; }

private:
    static constexpr size_t kBuffers = 3;

    // pullMutex_ held.
    void submit(size_t i) {
        FAudioBuffer b{};
        b.AudioBytes = uint32_t(buffers_[i].size() * sizeof(float));
        b.pAudioData = reinterpret_cast<const uint8_t*>(buffers_[i].data());
        b.pContext = reinterpret_cast<void*>(i);
        FAudioSourceVoice_SubmitSourceBuffer(source_, &b, nullptr);
    }

    static void FAUDIOCALL onBufferEnd(FAudioVoiceCallback* cb, void* context) {
        FAudioOutput* self = reinterpret_cast<Callbacks*>(cb)->self;
        const size_t i = reinterpret_cast<size_t>(context);
        std::lock_guard<std::mutex> lock(self->pullMutex_);
        if (self->closing_ || i >= kBuffers) return;
        std::vector<float>& buf = self->buffers_[i];
        if (self->paused_) std::fill(buf.begin(), buf.end(), 0.0f);
        else self->pull_(buf.data(), self->period_);
        self->submit(i);
    }

    // The callback table first, so FAudio's pointer to it finds this output.
    struct Callbacks {
        FAudioVoiceCallback cb;
        FAudioOutput* self;
    } callbacks_{};
    FAudio* audio_ = nullptr;
    FAudioMasteringVoice* master_ = nullptr;
    FAudioSourceVoice* source_ = nullptr;
    std::array<std::vector<float>, kBuffers> buffers_;
    int channels_ = 2, period_ = 480;
    PullCallback pull_;
    std::mutex pullMutex_;
    bool paused_ = true, primed_ = false, closing_ = false;
    Spec spec_;
    double latencyMs_ = 0;
    std::string name_;
};

}  // namespace

Backend faudioBackend() {
    Backend b;
    b.id = "faudio";
    b.displayName = "FAudio";
    // Built in; it needs SDL's audio, which every SDL target has. A missing
    // device shows at open().
    b.probe = [](std::string&) { return true; };
    b.make = [] { return std::unique_ptr<Output>(new FAudioOutput()); };
    return b;
}

}  // namespace gd::core::audio::detail
