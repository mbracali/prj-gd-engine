// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL.h>

// The one output every gd03 sound goes through, like GH2's XAudio
// mastering voice: the song (audio/mixer.h), the crowd (audio/crowd_audio.h)
// and the sound effects (audio/sfx_player.h) are streams bound to one
// logical playback device, and the final mix passes GH2's master limiter
// (sfxdsp::Limiter: default.dta (synth (limiter (attack_ms 1) (release_ms
// 50) (threshold -2))), on from the start: Synth360::Init 0x82682558 sets
// +0x61 [C]). On the 360 the limiter sees the 48 kHz 5.1 master before the
// console folds it to stereo; here it sees the device's own mix (its rate
// and channels) [I].
namespace audiodev {

// Opens a stream of `spec` bound to the shared device (opening the device
// the first time), its data pulled through `callback` (as
// SDL_OpenAudioDeviceStream's). nullptr when there is no device.
SDL_AudioStream* openStream(const SDL_AudioSpec& spec, SDL_AudioStreamCallback callback, void* user);
// Destroys a stream from openStream; the device closes with the last one.
void closeStream(SDL_AudioStream* stream);

// `synth toggle_limiter` (Synth360 0x826822C8: flips the limiter effect on
// the mastering voice) [C].
void toggleLimiter();
bool limiterEnabled();
// The options' stereo / mono choice (options set_stereo, 0x0010dbc0 ->
// 0x0010dc00) [C]: mono is no fold of the mix but every source panned to the
// centre: the synth's voices (0x00225cd8 -> 0x0021e490 marks each voice for
// re-panning) and the song's channels (0x00123110 -> 0x0023ab90: pan 0
// instead of the channel's own). The mixer and the sfx player read it.
void setMono(bool mono);
bool mono();

}  // namespace audiodev
