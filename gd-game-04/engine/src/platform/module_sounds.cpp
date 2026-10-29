// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd_game_ui_sounds: GH3's own menu sounds for the launcher's menus
// (docs/adding-a-game.md "Menu sounds"): the samples of the scripts' sound
// events ui_sfx_scroll (move), ui_sfx_select (select, play) and
// Generic_Menu_Back_SFX (back, error) [C data: global_sound_events.qb].
#include <exception>
#include <string>
#include <vector>

#include "audio/sfx.h"
#include "data/disc.h"
#include "data/qbkey.h"
#include "gd_host.h"
#include "script/globals.h"

extern "C" GD_MODULE_EXPORT int gd_game_ui_sounds(const GdHost* host, GdUiSoundFn sound, void* user) {
    if (!host || host->argc < 1 || !sound) return 0;
    std::string error;
    auto disc = Disc::open(host->argv[host->argc - 1], error);
    if (!disc) return 0;
    int given = 0;
    try {
        disc->loadNames();
        Globals g;
        g.load(*disc);
        Sfx sfx;  // not opened: decoding only
        sfx.addPak(*disc, "DATA/COMPRESSED/ZONES/global_sfx.pak.xen");
        const struct {
            const char* role;
            const char* event;
        } roles[] = {{"move", "ui_sfx_scroll"}, {"select", "ui_sfx_select"}, {"play", "ui_sfx_select"},
                     {"back", "Generic_Menu_Back_SFX"}, {"error", "Generic_Menu_Back_SFX"}};
        for (const auto& r : roles) {
            const qb::Value* c = g.find(qbkey::key(std::string(r.event) + "_container"));
            const qb::Value* sounds = c && c->st ? c->st->find(qbkey::key("sounds")) : nullptr;
            if (!sounds || !sounds->st) continue;
            for (const qb::Item& it : sounds->st->items) {
                if (!it.value.st) continue;
                uint32_t sample = 0;
                for (const qb::Item& si : it.value.st->items)
                    if (si.name == 0 && (si.value.type == qb::Type::Key || si.value.type == qb::Type::KeyPtr)) sample = si.value.key;
                std::vector<float> pcm;
                int rate = 0, channels = 0;
                if (!sample || !sfx.pcm(sample, pcm, rate, channels)) continue;
                std::vector<float> stereo;
                if (channels == 1) {
                    stereo.reserve(pcm.size() * 2);
                    for (float v : pcm) stereo.push_back(v), stereo.push_back(v);
                } else {
                    stereo = std::move(pcm);
                }
                sound(user, r.role, stereo.data(), int(stereo.size() / 2), rate);
                ++given;
            }
        }
    } catch (const std::exception&) {
        return given;
    }
    return given;
}
