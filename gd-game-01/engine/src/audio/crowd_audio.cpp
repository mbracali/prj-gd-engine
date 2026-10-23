// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/crowd_audio.h"

#include <SDL3/SDL.h>

#include "audio/mixer.h"
#include "data/dta.h"
#include "data/game_data.h"

CrowdAudio::CrowdAudio(Mixer& mixer, const GameData& data) : mixer_(mixer), data_(data) {}

void CrowdAudio::setVenue(const std::string& venue) {
    venue_ = venue;
    bank_.clear();
    intro_ = {};
    levels_ = {};
    try {
        DataNode venues = parseDtb(data_.read(dtbPath("arena/venues.dta")));
        const DataNode* v = venues.find(venue);
        const DataNode* sound = v ? v->find("sound") : nullptr;
        if (!sound) return;
        if (const DataNode* b = sound->find("bank")) bank_ = "venues/" + venue + "/" + b->textAt(1);
        const DataNode* crowd = sound->find("crowd");
        if (!crowd) return;
        auto entry = [](const DataNode& e) -> std::pair<float, std::string> {
            if (e.children.size() < 3) return {};
            return {e.floatAt(1), e.textAt(2)};
        };
        if (const DataNode* i = crowd->find("intro")) intro_ = entry(*i);
        if (const DataNode* lv = crowd->find("levels")) {
            static const char* const kNames[] = {"kExcitementBoot", "kExcitementBad", "kExcitementOkay", "kExcitementGreat",
                                                 "kExcitementPeak"};
            for (size_t k = 1; k < lv->children.size(); ++k) {
                const DataNode& e = lv->children[k];
                if (!e.isList() || e.children.empty()) continue;
                for (size_t n = 0; n < 5; ++n)
                    if ((e.children[0].isText() && e.children[0].s == kNames[n]) ||
                        (e.children[0].type == DataNode::Type::Int && e.children[0].i == int(n)))
                        levels_[n] = entry(e);
            }
        }
    } catch (const std::exception& ex) {
        SDL_Log("crowd audio %s: %s", venue.c_str(), ex.what());
    }
}

// A looping stereo stream, ch0 left and ch1 right.
void CrowdAudio::play(float volume, const std::string& file) {
    if (file.empty() || !enabled_) return;
    std::string path = "venues/" + venue_ + "/streams/" + file + ".vgs";
    if (!data_.exists(path)) path = "venues/" + venue_ + "/streams/gen/" + file + ".vgs";
    if (!data_.exists(path)) {
        SDL_Log("crowd audio: no %s", path.c_str());
        return;
    }
    try {
        auto vgs = std::make_unique<VgsStream>(data_.read(path));
        std::vector<Mixer::Track> tracks(size_t(vgs->channels()));
        for (size_t c = 0; c < tracks.size(); ++c) tracks[c].pan = tracks.size() == 2 ? (c == 0 ? -1.0f : 1.0f) : 0.0f;
        mixer_.playStream(Mixer::kCrowd, std::move(vgs), std::move(tracks), 0, 0, true, 0, volume * globalVol_ * fx());
        playing_ = file;
        levelVol_ = volume;
    } catch (const std::exception& ex) {
        SDL_Log("crowd audio %s: %s", path.c_str(), ex.what());
    }
}

void CrowdAudio::playIntro() { play(intro_.first, intro_.second); }

void CrowdAudio::setExcitement(int level) {
    if (level < 0 || level >= 5) return;
    // Every set_excitement restarts the file, even the same one (FUN_00112570).
    excitement_ = level;
    const auto& [vol, file] = levels_[size_t(level)];
    play(vol, file);
}

void CrowdAudio::playStream(float volume, const std::string& file) { play(volume, file); }

void CrowdAudio::turnOff() {
    mixer_.stopStream(Mixer::kCrowd, 0);
    playing_.clear();
}

void CrowdAudio::setPaused(bool paused) {
    if (!paused) mixer_.setStreamVolume(Mixer::kCrowd, levelVol_ * globalVol_ * fx());
    mixer_.setStreamPaused(Mixer::kCrowd, paused);
}

void CrowdAudio::setVolume(float v) {
    globalVol_ = v;
    mixer_.setStreamVolume(Mixer::kCrowd, levelVol_ * globalVol_ * fx());
}

void CrowdAudio::setEnabled(bool on) {
    if (on == enabled_) return;
    enabled_ = on;
    if (!on) turnOff();
    else setExcitement(excitement_);
}
