// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <utility>

class GameData;
class Mixer;

// `crowd_audio` (CrowdAudio_v1 0x112570): the venue's looping crowd streams,
// venues/<v>/streams/<file>.vgs, from arena/venues.dta
// (sound (bank b) (crowd (intro vol file) (levels (kExcitementBoot vol file) ...))).
class CrowdAudio {
public:
    CrowdAudio(Mixer& mixer, const GameData& data);

    // Reads the venue's sound config; also gives its bank path ("venues/basement/basement").
    void setVenue(const std::string& venue);
    const std::string& bankPath() const { return bank_; }

    void playIntro();
    void setExcitement(int level);  // kExcitementBoot 0 .. kExcitementPeak 4
    void playStream(float volume, const std::string& file);
    void turnOff();
    void setPaused(bool paused);
    // The options' sound effects volume (FUN_00112470), read when a stream
    // starts or un-pauses.
    std::function<float()> fxVolume;
    // crowd_audio set_volume / set_enabled / enabled (the cheats).
    void setVolume(float v);
    void setEnabled(bool on);
    bool enabled() const { return enabled_; }

private:
    void play(float volume, const std::string& file);

    Mixer& mixer_;
    const GameData& data_;
    std::string venue_, bank_;
    std::pair<float, std::string> intro_;
    std::array<std::pair<float, std::string>, 5> levels_;
    float fx() const { return fxVolume ? fxVolume() : 1.0f; }
    std::string playing_;
    float levelVol_ = 0, globalVol_ = 1;
    int excitement_ = 2;
    bool enabled_ = true;
};
