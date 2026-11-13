// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <string>

// The one output every gd05 sound goes through, like GH2's XAudio
// mastering voice: the song (audio/mixer.h), the crowd (audio/crowd_audio.h)
// and the sound effects (audio/sfx_player.h) are streams bound to one
// logical playback device, and the final mix passes GH2's master limiter
// (sfxdsp::Limiter: default.dta (synth (limiter (attack_ms 1) (release_ms
// 50) (threshold -2))), on from the start: Synth360::Init 0x82682558 sets
// +0x61 [C]). On the 360 the limiter sees the 48 kHz 5.1 master before the
// console folds it to stereo; here it sees the device's own mix (its rate
// and channels) [I].
namespace audiodev {

// Enhanced > Audio backend (audio_backend; gd-engine-core/docs/audio.md):
// every stream plays through one gd-engine-core output, "miniaudio" by
// default, summed and limited here (DECISIONS.md D71). Set once at start-up,
// before the first openStream. A backend that cannot start hands over to the
// next one this build has (miniaudio, faudio, openal), logged
// (engine-backend-guidelines.md rule 2).
constexpr const char* kDefaultBackend = "miniaudio";
void setBackend(const std::string& id);
// The backend that plays now: a core id, "" while no stream is open, "none"
// when no backend could start.
std::string backend();
// The one asked for with setBackend (kDefaultBackend by default).
std::string askedBackend();

// Opens a stream of `spec` mixed into the shared output (opening it the first
// time), its data pulled through `callback` (as SDL_OpenAudioDeviceStream's,
// but the stream is bound to no SDL device). nullptr when there is no output.
SDL_AudioStream* openStream(const SDL_AudioSpec& spec, SDL_AudioStreamCallback callback, void* user);
// Destroys a stream from openStream; the output closes with the last one.
void closeStream(SDL_AudioStream* stream);

// `synth toggle_limiter` (Synth360 0x826822C8: flips the limiter effect on
// the mastering voice) [C].
void toggleLimiter();
bool limiterEnabled();

}  // namespace audiodev
