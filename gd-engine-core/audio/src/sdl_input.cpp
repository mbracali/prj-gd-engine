// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The "sdl3" capture backend: an SDL3 audio stream bound to a recording
// device; SDL's device thread fills it and its put callback pushes the
// frames on, converted to the asked float format.
#include <SDL3/SDL.h>

#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

#include "input_backends.h"

namespace gd::core::audio::detail {
namespace {

bool ensureSdlAudio(std::string& why) {
    if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)) return true;
    why = std::string("SDL audio: ") + SDL_GetError();
    return false;
}

class SdlInput final : public Input {
public:
    ~SdlInput() override { close(); }

    bool open(const InputSpec& want, PushCallback push, std::string& error) override {
        close();
        if (want.nullDevice && !SDL_WasInit(SDL_INIT_AUDIO)) SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
        if (!ensureSdlAudio(error)) return false;
        push_ = std::move(push);
        spec_ = want;
        SDL_AudioSpec as{SDL_AUDIO_F32, want.channels, want.sampleRate};
        SDL_AudioDeviceID dev = SDL_AUDIO_DEVICE_DEFAULT_RECORDING;
        if (!want.device.empty()) dev = SDL_AudioDeviceID(std::strtoul(want.device.c_str(), nullptr, 10));
        stream_ = SDL_OpenAudioDeviceStream(dev, &as, &SdlInput::onData, this);
        if (!stream_) {
            error = std::string("SDL recording device: ") + SDL_GetError();
            return false;
        }
        const SDL_AudioDeviceID real = SDL_GetAudioStreamDevice(stream_);
        int frames = 0;
        SDL_AudioSpec devSpec{};
        if (SDL_GetAudioDeviceFormat(real, &devSpec, &frames)) latency_ = frames > 0 && devSpec.freq > 0 ? 1000.0 * frames / devSpec.freq : 0;
        const char* n = SDL_GetAudioDeviceName(real);
        name_ = std::string("SDL3 ") + (SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?") + " (" + (n ? n : "recording") + ")";
        return true;
    }
    bool start(std::string& error) override {
        if (!stream_ || !SDL_ResumeAudioStreamDevice(stream_)) {
            error = stream_ ? SDL_GetError() : "not open";
            return false;
        }
        return true;
    }
    void pause() override {
        if (stream_) SDL_PauseAudioStreamDevice(stream_);
    }
    void close() override {
        if (stream_) SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    InputSpec spec() const override { return spec_; }
    double latencyMs() const override { return latency_; }
    const char* id() const override { return "sdl3"; }
    std::string name() const override { return name_; }

private:
    // SDL's recording stream: `additional` bytes became available.
    static void SDLCALL onData(void* user, SDL_AudioStream* s, int additional, int /*total*/) {
        auto* self = static_cast<SdlInput*>(user);
        const int frameBytes = int(sizeof(float)) * self->spec_.channels;
        if (additional <= 0 || frameBytes <= 0) return;
        self->buf_.resize(size_t(additional / frameBytes + 1) * size_t(self->spec_.channels));
        const int got = SDL_GetAudioStreamData(s, self->buf_.data(), int(self->buf_.size() * sizeof(float)));
        if (got > 0 && self->push_) self->push_(self->buf_.data(), got / frameBytes);
    }

    SDL_AudioStream* stream_ = nullptr;
    PushCallback push_;
    InputSpec spec_;
    std::vector<float> buf_;
    double latency_ = 0;
    std::string name_;
};

std::vector<InputDevice> sdlDevices() {
    std::vector<InputDevice> out;
    std::string why;
    if (!ensureSdlAudio(why)) return out;
    int n = 0;
    SDL_AudioDeviceID* ids = SDL_GetAudioRecordingDevices(&n);
    for (int i = 0; ids && i < n; ++i) {
        const char* name = SDL_GetAudioDeviceName(ids[i]);
        out.push_back({std::to_string(ids[i]), name ? name : "recording device"});
    }
    SDL_free(ids);
    return out;
}

}  // namespace

InputBackend sdlInputBackend() {
    return {"sdl3", "SDL3", [](std::string&) { return true; }, [] { return std::unique_ptr<Input>(new SdlInput); }, sdlDevices};
}

}  // namespace gd::core::audio::detail
