// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_game.h"

#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "script/globals.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
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
    vm.addNative("start_gem_scroller", [&fe](Vm& v, ScriptRun&, qb::Struct& p) {
        auto diffOf = [](const qb::Value* diff) {
            if (diff) {
                if (diff->key == K("easy")) return 0;
                if (diff->key == K("medium")) return 1;
                if (diff->key == K("hard")) return 2;
            }
            return 3;
        };
        // The script's song_name / difficulty / difficulty2 params, else the globals.
        const qb::Value* song = Vm::param(p, K("song_name"));
        if (!song) song = v.global(K("current_song"));
        const qb::Value* diff = Vm::param(p, K("difficulty"));
        if (!diff) diff = v.global(K("current_difficulty"));
        const qb::Value* diff2 = Vm::param(p, K("difficulty2"));
        if (!diff2) diff2 = v.global(K("current_difficulty2"));
        FrontEnd::SongRequest& r = fe.songRequest;
        r = FrontEnd::SongRequest{};
        r.pending = true;
        // starttime; devil_finish_restart keeps devil_finish, any other start
        // clears it (and BossDevil then starts at 0) [C script start_gem_scroller].
        r.startMs = std::max(0.0, double(Vm::number(Vm::param(p, K("starttime")), 0)));
        if (Vm::number(Vm::param(p, K("devil_finish_restart")), 0) == 1) {
            r.devilFinish = Vm::number(v.global(K("devil_finish")), 0) != 0;
        } else {
            v.setGlobal(K("devil_finish"), Vm::makeInt(0));
            if (song && song->key == K("bossdevil")) r.startMs = 0;
        }
        v.setGlobal(K("current_starttime"), Vm::makeInt(int32_t(r.startMs)));
        if (auto it = v.globalTags.find(K("user_options")); it != v.globalTags.end())
            for (int i = 0; i < 2; ++i)
                if (const qb::Value* l = it->second.find(K(i ? "lefty_flip_p2" : "lefty_flip_p1"))) r.lefty[i] = l->number() != 0;
        if (auto it = v.globalTags.find(K("user_options")); it != v.globalTags.end())
            if (const qb::Value* l = it->second.find(K("lag_calibration"))) r.lagMs = l->number();
        r.song = song ? song->key : 0;
        r.difficulty = diffOf(diff);
        r.difficulty2 = diffOf(diff2);
        if (song) v.setGlobal(K("current_song"), Vm::makeKey(song->key));
        if (diff) v.setGlobal(K("current_difficulty"), Vm::makeKey(diff->key));
        if (diff2) v.setGlobal(K("current_difficulty2"), Vm::makeKey(diff2->key));
        // A boss song (its props carry `boss`): boss_battle = 1, current_boss,
        // two players, player 2 the bot (GetInputHandlerBotIndex player = 2)
        // playing the rhythm part (bossbattle_init) [C scripts start_gem_scroller, bossbattle_init].
        v.setGlobal(K("boss_battle"), Vm::makeInt(0));
        if (const qb::Value* props = v.global(K("gh3_songlist_props")); props && props->st && song)
            if (const qb::Value* ss = v.globals().field(*props->st, song->key); ss && ss->st)
                if (const qb::Value* boss = ss->st->find(K("boss"))) {
                    r.boss = boss->key;
                    v.setGlobal(K("current_boss"), *boss);
                    v.setGlobal(K("boss_battle"), Vm::makeInt(1));
                    v.setGlobal(K("current_num_players"), Vm::makeInt(2));
                    // player2_status.character_id = the boss's character_profile, outfit 1, style 1 [C script].
                    const qb::Value* bs = v.global(boss->key);
                    const qb::Value* cp = bs && bs->st ? v.globals().field(*bs->st, K("character_profile")) : nullptr;
                    if (const qb::Value* p2 = v.global(K("player2_status")); cp && p2 && p2->st) {
                        qb::Value nv = *p2;
                        nv.st = std::make_shared<qb::Struct>(*p2->st);
                        Vm::set(*nv.st, K("character_id"), *cp);
                        Vm::set(*nv.st, K("outfit"), Vm::makeInt(1));
                        Vm::set(*nv.st, K("style"), Vm::makeInt(1));
                        v.setGlobal(K("player2_status"), nv);
                    }
                }
        const qb::Value* mode = v.global(K("game_mode"));
        r.gameMode = mode ? mode->key : 0;
        r.players = int(Vm::number(v.global(K("current_num_players")), 1)) >= 2 ? 2 : 1;
        for (int i = 0; i < 2; ++i) {
            const qb::Value* ps = v.global(K(i ? "player2_status" : "player1_status"));
            const qb::Value* part = ps && ps->st ? v.globals().field(*ps->st, K("part")) : nullptr;
            r.part2Rhythm[i] = part && part->key == K("rhythm");
        }
        if (r.boss) r.part2Rhythm[1] = true;
        // Battle's sudden death: the saved health carries over [C scripts GuitarEvent_SongWon, restart_song].
        r.suddenDeath = r.gameMode == K("p2_battle") && Vm::number(v.global(K("battle_sudden_death")), 0) != 0;
        if (r.suddenDeath)
            for (int i = 0; i < 2; ++i) {
                const qb::Value* ps = v.global(K(i ? "player2_status" : "player1_status"));
                const qb::Value* h = ps && ps->st ? v.globals().field(*ps->st, K("save_health")) : nullptr;
                r.saveHealth[i] = h ? h->number() : 1.0f;
            }
        return true;
    });
    // SongUnLoadFSBIfDownloaded: kill_gem_scroller's (quit / restart from the
    // pause menu, the attract mode's end) -- a playing song is abandoned [C script kill_gem_scroller].
    vm.addNative("SongUnLoadFSBIfDownloaded", [&fe](Vm&, ScriptRun&, qb::Struct&) {
        if (fe.songPlaying) fe.songKill = true;
        return true;
    });
    // GetHeldPattern [controller = n] -> hold_pattern: the frets held, green
    // 0x10000 .. orange 0x1 [C use: the cheat entry, create_cheat_guitar_strum].
    vm.addNative("GetHeldPattern", [&fe](Vm&, ScriptRun& run, qb::Struct&) {
        if (!run.frames.empty()) {
            if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
            Vm::set(*run.frames.back().locals, K("hold_pattern"), Vm::makeInt(int32_t(fe.heldPattern)));
        }
        return true;
    });
    // GetSongTimeMs [time_offset = n]: time = the song clock + the offset (ms) [C use: Transition_Play_Spawned].
    vm.addNative("GetSongTimeMs", [&fe](Vm&, ScriptRun& run, qb::Struct& p) {
        const double t = fe.songClockMs + Vm::number(Vm::param(p, K("time_offset")), 0);
        if (!run.frames.empty()) {
            if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
            Vm::set(*run.frames.back().locals, K("time"), Vm::makeFloat(float(t)));
        }
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
    // PlayMovie {movie = 'atvi' ...}: played by the app at once (modal,
    // skippable), so IsMoviePlaying is false afterwards [I: GH3 plays
    // movies asynchronously; PlayMovieAndWait polls IsMoviePlaying].
    vm.addNative("PlayMovie", [&fe](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* m = Vm::param(p, K("movie"));
        if (!m) return false;
        fe.movieRequest = m->str;
        return true;
    });
    for (const char* n : {"PlayMovieFromBuffer", "StartPreLoadedMovie"})
        vm.addNative(n, [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    for (const char* n : {"IsMoviePlaying", "KillMovie", "PreLoadMovie", "AllocateMovieBuffer", "FreeMovieBuffer", "LoadMovieIntoBuffer",
                          "PauseMovie", "ResumeMovie"})
        vm.addNative(n, [](Vm&, ScriptRun&, qb::Struct&) { return false; });
    // StringToCharArray string = s: char_array = its characters as strings
    // (split_text_into_array_elements: "You Rock!", "Player One Rocks!", "TIE!") [C 0x82268BF0; I: one string per character].
    vm.addNative("StringToCharArray", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* str = Vm::param(p, K("string"));
        if (!str) return false;
        qb::Value a;
        a.type = qb::Type::Array;
        a.arr = std::make_shared<qb::Array>();
        for (char ch : str->str) a.arr->values.push_back(Vm::makeString(std::string(1, ch)));
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("char_array"), a);
        return true;
    });
    // ArrayContains array = a contains = x: whether a holds x [C 0x82267808; I: compares keys / numbers].
    vm.addNative("ArrayContains", [](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* a = Vm::param(p, K("array"));
        const qb::Value* x = Vm::param(p, K("contains"));
        if (!a || !x || a->type != qb::Type::Array || !a->arr) return false;
        for (const qb::Value& e : a->arr->values) {
            if ((e.type == qb::Type::Key || e.type == qb::Type::KeyPtr) && (x->type == qb::Type::Key || x->type == qb::Type::KeyPtr)) {
                if (e.key == x->key) return true;
            } else if ((e.type == qb::Type::Int || e.type == qb::Type::Float) && (x->type == qb::Type::Int || x->type == qb::Type::Float)) {
                if (e.number() == x->number()) return true;
            }
        }
        return false;
    });
    // GetActiveControllers: active_controllers = 4 flags, 1 for a connected
    // controller [C 0x82103AC8]. The port's controllers are its player slots.
    vm.addNative("GetActiveControllers", [&fe](Vm&, ScriptRun& run, qb::Struct&) {
        if (qb::Struct* l = locals(run)) {
            qb::Value a;
            a.type = qb::Type::Array;
            a.arr = std::make_shared<qb::Array>();
            for (int i = 0; i < 4; ++i) a.arr->values.push_back(Vm::makeInt((fe.activeControllers >> i) & 1));
            Vm::set(*l, K("active_controllers"), a);
        }
        return true;
    });
    // GetInputHandlerBotIndex {player = n}: controller = n - 1 + 4, the bot's
    // input handler (after the four pads) [C 0x82104468, 0x820E7D08].
    vm.addNative("GetInputHandlerBotIndex", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const int player = int(Vm::number(Vm::param(p, K("player")), 1));
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("controller"), Vm::makeInt(player - 1 + 4));
        return true;
    });
    // IsGuitarController {controller = n}: a bot (n > 3) is; a pad is when
    // its type is a guitar [C 0x82100D00]. The port: by the slot's device.
    vm.addNative("IsGuitarController", [&fe](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* c = Vm::param(p, K("controller"));
        if (!c) return false;
        const int i = int(c->number());
        return i > 3 || (i >= 0 && ((fe.guitarControllers >> i) & 1));
    });
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
