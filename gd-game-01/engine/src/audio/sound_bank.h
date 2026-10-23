// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/adpcm.h"
#include "data/dta.h"

class GameData;

// One Synth bank (docs/audio_sfx.md §1): <dir>/gen/<name>.bnk (descs, sfx
// map, sample table), its .nse PS-ADPCM samples (decoded at load) and its
// .seq sequences.
class SoundBank {
public:
    struct Desc {  // SampleDesc (FUN_00218130)
        uint32_t sample = 0;
        int core = -1;      // fx_core: -1 dry, 0 echo send, 1 large-studio send
        float ratio = 1;    // 2^((transpose / 128) / 12)
        uint16_t adsr1 = 0x8F1F, adsr2 = 0x03C7;
        float volume = 1;   // / 32767
        float pan = 0;      // / 127
    };
    struct Sample {
        std::shared_ptr<const AdpcmSample> pcm;
        int rate = 22050;
    };

    // `path` is "sfx/ingame" or "venues/basement/basement"; throws std::runtime_error.
    SoundBank(const GameData& data, const std::string& path);

    const std::string& path() const { return path_; }
    // Every desc an sfx name plays (its layers), in file order.
    std::vector<const Desc*> layers(const std::string& sfx) const;
    const Sample* sample(uint32_t i) const { return i < samples_.size() ? &samples_[i] : nullptr; }
    // A sequence definition from the .seq, or null.
    const DataNode* sequence(const std::string& name) const;

private:
    std::string path_;
    std::vector<Desc> descs_;
    std::multimap<std::string, int> sfx_;
    std::vector<Sample> samples_;
    DataNode seqRoot_;
    std::map<std::string, const DataNode*> sequences_;
};
