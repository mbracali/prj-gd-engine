// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd01's menu sounds for the launcher's own menus (gd_game_ui_sounds,
// sdk/gd_host.h): Guitar Hero's metagame bank (sfx/metagame) read from the
// player's disc with gd01's loaders, and each sound rendered once through
// gd01's software SPU2, as the game's menus play them (gd-game-01
// docs/audio_sfx.md section 4, ghui/sfx.dta):
//
//   move    FOCUS_MSG / SCROLL_MSG  seq button_toggle (random of 3)
//   select  SELECT_MSG              seq button_select (random of 3)
//   play    SELECT_MSG on a song    seq button_play   (random of 8)
//   back    SCREEN_BACK_MSG         seq button_back   (random of 3)
//   error   BAD_SELECT_MSG          sfx button_error
//
// Each variation of a sequence is given to the launcher, which picks one at
// random as the random_group_seq does.
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <vector>

#include "audio/sound_bank.h"
#include "audio/spu.h"
#include "data/game_data.h"
#include "gd_host.h"
#include "gd_module_shim.h"

namespace {

constexpr int kRate = 48000;

// The sfx names a sequence plays: its own (sfx_seq) or its children's.
void sfxOf(const DataNode& def, std::vector<std::string>& out) {
    if (def.children.empty() || !def.children[0].isText()) return;
    if (def.children[0].s == "sfx_seq") {
        if (const DataNode* s = def.find("sfx"); s && s->children.size() > 1 && s->children[1].isText()) out.push_back(s->children[1].s);
        return;
    }
    if (const DataNode* ch = def.find("children"))
        for (size_t k = 1; k < ch->children.size(); ++k)
            if (ch->children[k].isList()) sfxOf(ch->children[k], out);
}

// One sfx (all its layers at once) through a fresh SPU, until its voices
// and the effect tails are silent: interleaved stereo at kRate.
std::vector<float> render(const SoundBank& bank, const std::string& name) {
    Spu spu;
    spu.setEffects(0.3f, 0.5f, 0.3f, 0.4f);  // config/synth.dta, as gd01's Synth sets them
    std::vector<int> voices;
    for (const SoundBank::Desc* d : bank.layers(name)) {
        const SoundBank::Sample* s = bank.sample(d->sample);
        if (!s) continue;
        Spu::KeyOn k;
        k.sample = s->pcm;
        k.rate = s->rate;
        k.speed = d->ratio;
        k.volume = std::clamp(d->volume, 0.0f, 1.0f);
        k.pan = std::clamp(d->pan, -1.0f, 1.0f);
        k.adsr1 = d->adsr1, k.adsr2 = d->adsr2;
        k.core = d->core;
        if (int id = spu.keyOn(k)) voices.push_back(id);
    }
    std::vector<float> out;
    if (voices.empty()) return out;
    constexpr int kBlock = 512;
    std::vector<float> block(size_t(kBlock) * 2);
    int quiet = 0;
    for (int frames = 0; frames < kRate * 4; frames += kBlock) {  // at most 4 s
        std::fill(block.begin(), block.end(), 0.0f);
        spu.mix(block.data(), kBlock, kRate);
        out.insert(out.end(), block.begin(), block.end());
        bool playing = false;
        for (int id : voices) playing = playing || spu.playing(id);
        float peak = 0;
        for (float v : block) peak = std::max(peak, std::abs(v));
        quiet = !playing && peak < 1e-4f ? quiet + 1 : 0;
        if (quiet >= 4) break;
    }
    // Trim the silent end.
    size_t end = out.size();
    while (end >= 2 && std::abs(out[end - 1]) < 1e-4f && std::abs(out[end - 2]) < 1e-4f) end -= 2;
    out.resize(end);
    return out;
}

}  // namespace

extern "C" GD_MODULE_EXPORT int gd_game_ui_sounds(const GdHost* host, GdUiSoundFn sound, void* user) {
    if (!host || host->abi_version != GD_HOST_ABI_VERSION || !sound || host->argc < 2) return 0;
    gdshim_begin(host);
    int given = 0;
    try {
        GameData data(host->argv[host->argc - 1]);
        SoundBank bank(data, "sfx/metagame");
        struct Role {
            const char* role;
            const char* name;
            bool sequence;
        };
        static const Role kRoles[] = {{"move", "button_toggle", true},  {"select", "button_select", true}, {"play", "button_play", true},
                                      {"back", "button_back", true},    {"error", "button_error", false}};
        for (const Role& r : kRoles) {
            std::vector<std::string> names;
            if (!r.sequence) names.push_back(r.name);
            else if (const DataNode* def = bank.sequence(r.name)) sfxOf(*def, names);
            for (const std::string& n : names) {
                std::vector<float> pcm = render(bank, n);
                if (pcm.empty()) continue;
                sound(user, r.role, pcm.data(), int(pcm.size() / 2), kRate);
                ++given;
            }
        }
    } catch (const std::exception& e) {
        SDL_Log("gd01 menu sounds: %s", e.what());
    }
    gdshim_end();
    return given;
}
