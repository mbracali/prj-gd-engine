// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_game.h"

#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "script/globals.h"

#include <SDL3/SDL_log.h>

#include <cctype>
#include <exception>
#include <set>
#include <unordered_map>
#include "render/texture_cache.h"
#include "ui/front_end.h"

namespace {
uint32_t K(const char* s) { return qbkey::key(s); }
qb::Struct* locals(qs::ScriptRun& run) {
    if (run.frames.empty()) return nullptr;
    if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
    return run.frames.back().locals.get();
}
}  // namespace

void addGameNatives(FrontEnd& fe, const Disc& disc, TextureCache& textures) {
    using qs::ScriptRun;
    using qs::Vm;
    qs::Vm& vm = fe.vm();
    // LoadPak '<folder>/.../<name>.pak' [load_callback = s callback_data = d]:
    // the pak's textures to the GPU and its scripts into the globals; the
    // callback runs with `end` and result = 0 (LoadPakAsync waits for it)
    // [C 0x8218B378; behaviour from LoadPakAsync / LoadPakAsync_callback].
    auto loaded = std::make_shared<std::set<std::string>>();
    vm.addNative("LoadPak", [&disc, &textures, loaded](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* path = nullptr;
        for (const qb::Item& it : p.items)
            if (it.name == 0 && (it.value.type == qb::Type::String || it.value.type == qb::Type::WString)) path = &it.value;
        if (!path) return false;
        std::string src = path->str;
        for (char& c : src) c = (c == '\\') ? '/' : char(std::tolower(static_cast<unsigned char>(c)));
        const size_t slash = src.find('/'), last = src.rfind('/');
        std::string folder = slash == std::string::npos ? "pak" : src.substr(0, slash);
        for (char& c : folder) c = char(std::toupper(static_cast<unsigned char>(c)));
        const std::string file = last == std::string::npos ? src : src.substr(last + 1);
        std::string discPath = "DATA/COMPRESSED/" + folder + "/" + file + ".xen";
        bool ok = disc.exists(discPath);
        if (ok && !loaded->count(discPath)) {
            loaded->insert(discPath);
            try {
                textures.loadPak(disc, discPath);
                Pak pak = Pak::load(disc, discPath);
                for (const PakEntry& e : pak.entries())
                    if (e.type == qbkey::key(".qb")) {
                        auto [d, n] = pak.data(e);
                        v.globals().add(qb::decode(d, n));
                    }
            } catch (const std::exception& ex) {
                SDL_Log("LoadPak %s: %s", discPath.c_str(), ex.what());
                ok = false;
            }
        }
        if (!ok) v.logOnce("LoadPak: no " + discPath);
        if (const qb::Value* cb = Vm::param(p, K("load_callback"))) {
            qb::Struct cp;
            cp.items.push_back(qb::Item{0, Vm::makeKey(K("end"))});
            Vm::set(cp, K("result"), Vm::makeInt(ok ? 0 : 1));
            if (const qb::Value* cd = Vm::param(p, K("callback_data"))) Vm::set(cp, K("callback_data"), *cd);
            v.spawn(cb->key, cp);
        }
        return ok;
    });
    // start_gem_scroller (a script in the original): the song starts with the
    // native game core (game/player.h, ui/song_screen.h) on $current_song at
    // $current_difficulty [approximation: GH3's own gem scroller scripts and
    // their C functions are replaced as one unit; backlog GD04-021].
    vm.addNative("start_gem_scroller", [&fe](Vm& v, ScriptRun&, qb::Struct&) {
        const qb::Value* song = v.global(K("current_song"));
        const qb::Value* diff = v.global(K("current_difficulty"));
        fe.songRequest.pending = true;
        fe.songRequest.song = song ? song->key : 0;
        int d = 3;
        if (diff) {
            if (diff->key == K("easy")) d = 0;
            else if (diff->key == K("medium")) d = 1;
            else if (diff->key == K("hard")) d = 2;
        }
        fe.songRequest.difficulty = d;
        return true;
    });
    // The pak manager (0x82182D18 / 0x82182AF8 / 0x82182DF8 / 0x821844A0): one
    // current pak per map (zones: the venue). Setting it loads the zone's
    // pak (textures now; the venue itself is M5); loads finish at once.
    auto current = std::make_shared<std::unordered_map<uint32_t, uint32_t>>();
    auto setCurrent = [&disc, &textures, current](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* map = Vm::param(p, K("map"));
        const qb::Value* pak = Vm::param(p, K("pak"));
        if (!map || !pak) return false;
        (*current)[map->key] = pak->key;
        if (pak->key == K("none")) return true;
        // Zone keys name the zone (z_budokan); its pak is ZONES/<zone>.pak.xen [C data].
        const std::string zone = qbkey::name(pak->key);
        const std::string path = "DATA/COMPRESSED/ZONES/" + zone + ".pak.xen";
        if (disc.exists(path)) textures.loadPak(disc, path);
        else v.logOnce("SetPakManCurrent: no " + path);
        return true;
    };
    vm.addNative("SetPakManCurrentBlock", setCurrent);
    vm.addNative("SetPakManCurrent", setCurrent);
    vm.addNative("PendingPakManLoads", [](Vm&, ScriptRun&, qb::Struct&) { return false; });
    vm.addNative("DestroyPakManMap", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    auto getCurrent = [current](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* map = Vm::param(p, K("map"));
        if (!map) return false;
        auto it = current->find(map->key);
        if (it == current->end() || it->second == K("none")) return false;
        if (qb::Struct* l = locals(run)) {
            Vm::set(*l, K("pak"), Vm::makeKey(it->second));
            Vm::set(*l, K("pakname"), Vm::makeString(qbkey::name(it->second)));
        }
        return true;
    };
    vm.addNative("GetPakManCurrent", getCurrent);
    vm.addNative("GetPakManCurrentName", getCurrent);
    for (const char* n : {"ResetWaypoints", "ResetPulseEvents"}) vm.addNative(n, [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    // GetButtonsPressed: makes = buttons pressed since the last call, device_num [I].
    vm.addNative("GetButtonsPressed", [&fe](Vm&, ScriptRun& run, qb::Struct&) {
        if (qb::Struct* l = locals(run)) {
            Vm::set(*l, K("makes"), Vm::makeInt(fe.pad.makes));
            Vm::set(*l, K("device_num"), Vm::makeInt(fe.pad.device));
        }
        fe.pad.makes = 0;
        return true;
    });
}
