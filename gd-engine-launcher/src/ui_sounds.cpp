// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui_sounds.h"

namespace {

const char* const kRoles[] = {"move", "select", "play", "back", "error"};
constexpr float kGain = 0.6f;  // the menu sounds under the games' own levels

}  // namespace

void UiSounds::add(const std::string& role, const float* stereo, int frames, int rate) {
    if (!stereo || frames <= 0 || rate <= 0) return;
    for (size_t i = 0; i < clips_.size(); ++i)
        if (role == kRoles[i]) clips_[i].push_back({std::vector<float>(stereo, stereo + size_t(frames) * 2), rate});
}

void UiSounds::clear() {
    close();
    for (auto& c : clips_) c.clear();
}

int UiSounds::count() const {
    int n = 0;
    for (const auto& c : clips_) n += int(c.size());
    return n;
}

bool UiSounds::open() {
    if (device_ || count() == 0) return device_ != 0;
    if (!audioInit_) {
        if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            SDL_Log("gdul: menu sounds: no audio (%s)", SDL_GetError());
            return false;
        }
        audioInit_ = true;
    }
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!device_) {
        SDL_Log("gdul: menu sounds: cannot open the audio device (%s)", SDL_GetError());
        close();
        return false;
    }
    // A few streams on the one device (SDL mixes them): quick moves overlap
    // as in the game's menus instead of queueing up.
    for (SDL_AudioStream*& s : streams_) {
        s = SDL_CreateAudioStream(&spec, nullptr);
        if (s) {
            SDL_SetAudioStreamGain(s, kGain);
            SDL_BindAudioStream(device_, s);
        }
    }
    return true;
}

void UiSounds::close() {
    for (SDL_AudioStream*& s : streams_) {
        if (s) SDL_DestroyAudioStream(s);
        s = nullptr;
    }
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
    if (audioInit_) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audioInit_ = false;
}

void UiSounds::play(UiSound s) {
    const auto& set = clips_[size_t(s)];
    if (set.empty() || (!device_ && !open())) return;
    const Clip& c = set[std::uniform_int_distribution<size_t>(0, set.size() - 1)(rng_)];
    // The stream that is free (else the one used longest ago).
    SDL_AudioStream* stream = nullptr;
    for (size_t k = 0; k < streams_.size() && !stream; ++k) {
        SDL_AudioStream* t = streams_[(next_ + k) % streams_.size()];
        if (t && SDL_GetAudioStreamQueued(t) == 0) stream = t, next_ = (next_ + k + 1) % streams_.size();
    }
    if (!stream) {
        stream = streams_[next_];
        next_ = (next_ + 1) % streams_.size();
        if (stream) SDL_ClearAudioStream(stream);
    }
    if (!stream) return;
    const SDL_AudioSpec src{SDL_AUDIO_F32, 2, c.rate};
    SDL_SetAudioStreamFormat(stream, &src, nullptr);
    SDL_PutAudioStreamData(stream, c.pcm.data(), int(c.pcm.size() * sizeof(float)));
    if (logging_) SDL_Log("gdul: menu sound %s (%d ms)", kRoles[size_t(s)], int(c.pcm.size() / 2 * 1000 / size_t(c.rate)));
}
