// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

#include <string>

// The one output every gd04 sound goes through, like GH2's XAudio
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
// "local" (the default) is SDL's devices, as always; "miniaudio", "openal" or
// "faudio" plays the same streams (the song's and the sound effects') through
// that gd-engine-core output (mixed and limited here as SDL's devices do;
// gd02's). Set once at start-up, before the first openStream / openFx. A
// backend that cannot start leaves the game on local for the run, logged
// (engine-backend-guidelines.md rule 2).
void setBackend(const std::string& id);
// The backend that plays now: "local", a core id, or "" while nothing is
// open (then "local" when the asked one failed).
std::string backend();
// The one asked for with setBackend ("local" by default).
std::string askedBackend();

// Opens a stream of `spec` bound to the shared device (opening the device
// the first time), its data pulled through `callback` (as
// SDL_OpenAudioDeviceStream's). nullptr when there is no device.
SDL_AudioStream* openStream(const SDL_AudioSpec& spec, SDL_AudioStreamCallback callback, void* user);
// Destroys a stream from openStream; the device closes with the last one.
void closeStream(SDL_AudioStream* stream);

// The sound effects' own logical device (audio/sfx.h; GH3's sound effects
// are not under the song's limiter): opened once, its streams bound and
// unbound (paused) here; a stream is unbound before it is destroyed. On a
// core backend the streams join its output (after the limiter).
bool openFx(std::string& error);
void closeFx();
bool bindFx(SDL_AudioStream* stream);
void unbindFx(SDL_AudioStream* stream);

// `synth toggle_limiter` (Synth360 0x826822C8: flips the limiter effect on
// the mastering voice) [C].
void toggleLimiter();
bool limiterEnabled();

// GD04_AUDIODUMP=<prefix> (development aid): a logical device's final mix
// (after its postmix: the song's limiter) written to <prefix>.<tag>.wav
// (float, the device's rate and channels), so a sound's level can be
// measured against the song's at the moment it plays. `dumpPostmix` is
// called from that device's postmix callback; `dumpFrames` is how many
// frames the dump holds now (the sound effects log where they start).
bool dumpEnabled();
void dumpPostmix(int slot, const char* tag, const SDL_AudioSpec* spec, const float* buffer, int bytes);
long long dumpFrames(int slot);

}  // namespace audiodev
