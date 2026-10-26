// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/song_setup.h"

#include "data/game_data.h"
#include "data/midi.h"

int hopoThreshold(const SongAudio& audio, const Rules& rules) {
    return audio.hopoThresholdTicks > 0 ? audio.hopoThresholdTicks : rules.hopoThresholdTicks;
}

Song loadChart(const GameData& data, const SongInfo& info, const Rules& rules) {
    return buildSong(parseMidi(data.read(info.audio.midiPath)), hopoThreshold(info.audio, rules));
}
