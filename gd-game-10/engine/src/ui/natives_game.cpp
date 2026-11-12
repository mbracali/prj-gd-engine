// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_game.h"

#include "data/band.h"
#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "script/globals.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <exception>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include "audio/sound_buss.h"
#include "render/texture_cache.h"
#include "ui/front_end.h"

namespace {
uint32_t K(const char* s) { return qbkey::key(s); }

// Reads paks into Pak::shared's cache on one worker thread, the newest
// wishes first (the character select's neighbours, see LoadPak).
class PakPrefetch {
public:
    explicit PakPrefetch(const Disc& disc) : disc_(disc) {}
    ~PakPrefetch() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            queue_.clear();
        }
        if (thread_.joinable()) thread_.join();
    }
    void want(const std::vector<std::string>& paths) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.assign(paths.begin(), paths.end());
        if (running_ || queue_.empty()) return;
        if (thread_.joinable()) thread_.join();  // ended (running_ false)
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

private:
    void run() {
        for (;;) {
            std::string p;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stop_ || queue_.empty()) {
                    running_ = false;
                    return;
                }
                p = queue_.front();
                queue_.pop_front();
            }
            try {
                if (disc_.exists(p)) Pak::shared(disc_, p);
            } catch (const std::exception& ex) {
                SDL_Log("prefetch %s: %s", p.c_str(), ex.what());
            }
        }
    }
    const Disc& disc_;
    std::mutex mutex_;
    std::deque<std::string> queue_;
    std::thread thread_;
    bool running_ = false, stop_ = false;
};

std::string lowerCase(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

const qb::Struct* descIn(const Globals& g, const char* array, uint32_t id) {
    if (const qb::Value* a = g.find(qbkey::key(array)); a && a->arr)
        for (const qb::Value& e : a->arr->values)
            if (e.st)
                if (const qb::Value* d = e.st->find(qbkey::key("desc_id")); d && d->key == id) return e.st.get();
    return nullptr;
}

// The paks of the characters next to the one whose body pak `discPath` is,
// in the character select's order: musician_profiles' guitarists, not
// debug_only, not locked (their body's tag unlocked = 0) [C script
// get_valid_character_index]; the nearest first: body, anims, instrument.
std::vector<std::string> neighbourPaks(qs::Vm& v, const std::string& discPath) {
    const Globals& g = v.globals();
    auto strOf = [](const qb::Value* x) { return x && (x->type == qb::Type::String || x->type == qb::Type::WString) ? x->str : std::string(); };
    auto descOf = [](const qb::Struct& prof, const char* field) -> uint32_t {
        const qb::Value* s = prof.find(qbkey::key(field));
        const qb::Value* d = s && s->st ? s->st->find(qbkey::key("desc_id")) : nullptr;
        return d ? d->key : 0;
    };
    std::vector<const qb::Struct*> valid;
    const qb::Value* profs = g.find(qbkey::key("musician_profiles"));
    if (!profs || !profs->arr) return {};
    for (const qb::Value& e : profs->arr->values) {
        if (!e.st) continue;
        const qb::Value* type = e.st->find(qbkey::key("Type"));
        if (!type || type->key != qbkey::key("Guitarist") || e.st->find(qbkey::key("debug_only"))) continue;
        const uint32_t body = descOf(*e.st, "musician_body");
        if (auto t = v.globalTags.find(body); t != v.globalTags.end())
            if (const qb::Value* u = qs::Vm::param(t->second, qbkey::key("unlocked")); u && u->number() == 0) continue;
        valid.push_back(e.st.get());
    }
    auto bodyPak = [&](const qb::Struct& prof) -> const qb::Struct* { return descIn(g, "musician_body", descOf(prof, "musician_body")); };
    const std::string want = lowerCase(discPath);
    int at = -1;
    for (size_t i = 0; i < valid.size() && at < 0; ++i)
        if (const qb::Struct* b = bodyPak(*valid[i]); b && lowerCase(band::pakPath(strOf(b->find(qbkey::key("pak"))))) == want) at = int(i);
    if (at < 0) return {};
    std::vector<std::string> out;
    const int n = int(valid.size());
    for (int d : {1, -1, 2, -2}) {
        if (at + d < 0 || at + d >= n) continue;  // the list does not wrap (cs_scroll_up_or_down) [C script]
        const qb::Struct& prof = *valid[size_t(at + d)];
        if (const qb::Struct* b = bodyPak(prof)) {
            if (std::string p = strOf(b->find(qbkey::key("pak"))); !p.empty()) out.push_back(band::pakPath(p));
            if (std::string p = strOf(b->find(qbkey::key("anims_pak"))); !p.empty()) out.push_back(band::pakPath(p));
        }
        if (const qb::Struct* in = descIn(g, "musician_instrument", descOf(prof, "musician_instrument")))
            if (std::string p = strOf(in->find(qbkey::key("pak"))); !p.empty()) out.push_back(band::pakPath(p));
    }
    return out;
}
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
    // A character's pak (pak/models/...): its textures are the model's own
    // (MenuObjects, the band) and the results art's (PushAssetContext), not
    // the global cache's. GH3 reads it async (preload_band_member) but its
    // LoadPakAsync runs inside a condition (load_musician_pak_file), which
    // the VM runs to its end: instead, a body pak loaded has the character
    // select's neighbours (their body, anims and instrument paks) read ahead
    // on a worker, so the next scroll finds its paks read (owner: character
    // select stutters).
    auto loaded = std::make_shared<std::set<std::string>>();
    auto prefetch = std::make_shared<PakPrefetch>(disc);
    vm.addNative("LoadPak", [&disc, &textures, loaded, prefetch](Vm& v, ScriptRun&, qb::Struct& p) {
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
        const bool modelPak = src.rfind("pak/models/", 0) == 0;
        bool ok = disc.exists(discPath);
        if (ok && !loaded->count(discPath)) {
            loaded->insert(discPath);
            try {
                if (!modelPak) textures.loadPak(disc, discPath);
                const auto pakShared = Pak::shared(disc, discPath);
                const Pak& pak = *pakShared;
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
        if (ok && modelPak)
            if (std::vector<std::string> next = neighbourPaks(v, discPath); !next.empty()) prefetch->want(next);
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
    // their C functions are replaced as one unit; backlog GD10-021].
    auto startScroller = [&fe](Vm& v, ScriptRun& run, qb::Struct& p) -> bool {
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
        fe.songSync = {};
        r.pending = true;
        // starttime; devil_finish_restart keeps devil_finish, any other start
        // clears it (and BossDevil then starts at 0) [C script start_gem_scroller].
        r.startMs = std::max(0.0, double(Vm::number(Vm::param(p, K("starttime")), 0)));
        // endtime (practice_start_song: practice_end_time; default 99999999) [C script start_gem_scroller].
        if (const qb::Value* e = Vm::param(p, K("endtime")); e && e->number() > 0 && e->number() < 99999999.0f) r.endMs = e->number();
        // setslomo current_speedfactor once loaded; the streams at pitch =
        // current_speedfactor * 100, player 1's guitar with
        // Player1PracticeEffects (PitchShiftSlow1) in training outside the
        // section menu [C scripts start_gem_scroller, startpreloadpaused_song].
        r.speed = std::clamp(Vm::number(v.global(K("current_speedfactor")), 1), 0.1f, 1.0f);
        if (v.global(K("game_mode")) && v.global(K("game_mode"))->key == K("training") &&
            Vm::number(v.global(K("in_menu_choose_practice_section")), 0) == 0)
            if (const qb::Value* ps = v.global(K("PitchShiftSlow1")); ps && ps->st)
                r.slowPitch = Vm::number(v.globals().field(*ps->st, K("pitch")), 1);
        if (Vm::number(Vm::param(p, K("devil_finish_restart")), 0) == 1) {
            r.devilFinish = Vm::number(v.global(K("devil_finish")), 0) != 0;
        } else {
            v.setGlobal(K("devil_finish"), Vm::makeInt(0));
            if (song && song->key == K("bossdevil")) r.startMs = 0;
        }
        v.setGlobal(K("current_starttime"), Vm::makeInt(int32_t(r.startMs)));
        // change current_endtime = <endtime> (default 99999999; practicemode_section fades its text out past it) [C script start_gem_scroller].
        v.setGlobal(K("current_endtime"), Vm::makeInt(int32_t(r.endMs > 0 ? r.endMs : 99999999.0)));
        // change playing_song = 1 (the script's, before the venue enters) [C
        // script start_gem_scroller]: controller_unplugged, sysnotify and
        // destroy_loading_screen read it; kill_gem_scroller sets it back to 0.
        v.setGlobal(K("playing_song"), Vm::makeInt(1));
        // preload_song's busses (the native core loads the streams): Band_Balance
        // = the song's band_playback_volume - 1.5 dB, Guitar_Balance its
        // guitar_playback_volume - 1.5 (0 when absent), unlocked, set at once
        // (start_gem_scroller passes no fadeintime) and locked again [C script preload_song].
        {
            float band = 0, guitar = 0;
            if (const qb::Value* props = v.global(K("gh3_songlist_props")); props && props->st && song)
                if (const qb::Value* ss = v.globals().field(*props->st, song->key); ss && ss->st) {
                    if (const qb::Value* b = ss->st->find(K("band_playback_volume"))) band = b->number();
                    if (const qb::Value* g = ss->st->find(K("guitar_playback_volume"))) guitar = g->number();
                }
            SoundBusses& sb = soundBusses();
            const float bv = band - 1.5f, gv = guitar - 1.5f;
            for (const char* b : {"Band_Balance", "Guitar_Balance"}) sb.lock(K(b), false);
            sb.set(K("Band_Balance"), &bv, nullptr, 0);
            sb.set(K("Guitar_Balance"), &gv, nullptr, 0);
            for (const char* b : {"Band_Balance", "Guitar_Balance"}) sb.lock(K(b), true);
        }
        // ... and, once the song has started (its venue loaded: App), the
        // loading screen goes (destroy_loading_screen, near the script's end)
        // [C script start_gem_scroller; I: spawned by App with the song].
        if (r.devilFinish || r.startMs > 0) SDL_Log("song: starts at %.0f ms%s", r.startMs, r.devilFinish ? " (Lou's finish)" : "");
        if (r.endMs > 0) SDL_Log("song: ends at %.0f ms (endtime)", r.endMs);
        // end_credits: cleared unless end_credits_restart = 1 or the song is
        // thrufireandflames [C script start_gem_scroller]; with it set,
        // CrowdDecrease takes no health [C 0x8210FD18].
        if (Vm::number(Vm::param(p, K("end_credits_restart")), 0) != 1 && !(song && song->key == K("thrufireandflames")))
            v.setGlobal(K("end_credits"), Vm::makeInt(0));
        r.endCredits = Vm::number(v.global(K("end_credits")), 0) == 1;
        // current_transition (none: fastintro), set by Transition_SelectTransition
        // or the restart / practice scripts; Transition_Play plays it, then
        // change current_transition = none [C script start_gem_scroller].
        if (const qb::Value* t = v.global(K("current_transition")); t && t->key && t->key != K("none")) r.transition = t->key;
        v.setGlobal(K("current_transition"), Vm::makeKey(K("none")));
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
                    ++fe.preloadRequest.serial;  // the boss is player 2's character: the venue's band loads again
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
        // The boss after the song that leads to it (GuitarEvent_SongWon_Spawned's
        // preboss: show_boss_helper_screen = 1): the boss song is set up, then
        // its intro shown when the venue has one (should_play_boss_intro:
        // wait_and_show_boss_helper_after_intro, spawned now) or at once
        // (show_boss_helper_now, spawned later); both kill the gem scroller and
        // go to battle_mode_helper, whose BATTLE starts the song again with the
        // flag cleared [C script start_gem_scroller's end; battlemode_container's
        // alpha: the port's battle HUD is native].
        if (r.boss && Vm::number(v.global(K("show_boss_helper_screen")), 0) == 1) {
            qb::Struct q;
            if (v.condition(run, K("should_play_boss_intro"), q)) {
                if (const uint32_t id = v.spawn(K("wait_and_show_boss_helper_after_intro"), qb::Struct{})) v.runNow(id);
            } else {
                v.spawn(K("show_boss_helper_now"), qb::Struct{});
            }
        }
        // Battle's sudden death: the saved health carries over [C scripts GuitarEvent_SongWon, restart_song].
        r.suddenDeath = r.gameMode == K("p2_battle") && Vm::number(v.global(K("battle_sudden_death")), 0) != 0;
        if (r.suddenDeath)
            for (int i = 0; i < 2; ++i) {
                const qb::Value* ps = v.global(K(i ? "player2_status" : "player1_status"));
                const qb::Value* h = ps && ps->st ? v.globals().field(*ps->st, K("save_health")) : nullptr;
                r.saveHealth[i] = h ? h->number() : 1.0f;
            }
        return true;
    };
    // A loading screen up for less than Vm::kLoadingHoldMs (0 since D23: the
    // original has no minimum): the start waits for the rest,
    // whichever script starts the song (restart_gem_scroller,
    // create_attract_mode, training) [I].
    vm.addNative("start_gem_scroller", [startScroller, &fe](Vm& v, ScriptRun& run, qb::Struct& p) {
        // The script's Menu_Music_Off first: the menu music stops as the song
        // loads (it played on into the song once the port's sounds were heard) [C script start_gem_scroller].
        v.run(K("Menu_Music_Off"), qb::Struct{});
        v.run(K("SongUnLoadFSB"), qb::Struct{});  // the setlist's preview stops as the song loads
        // The venue starts loading now, under the loading screen (VenuePreload).
        {
            FrontEnd::PreloadRequest& pr = fe.preloadRequest;
            const qb::Value* song = Vm::param(p, K("song_name"));
            if (!song) song = v.global(K("current_song"));
            pr.song = song ? song->key : 0;
            const qb::Value* t = v.global(K("current_transition"));
            pr.transition = t && t->key && t->key != K("none") ? t->key : 0;
            pr.endMs = -1;
            if (const qb::Value* e = Vm::param(p, K("endtime")); e && e->number() > 0 && e->number() < 99999999.0f) pr.endMs = e->number();
            // starttime, as startScroller reads it (bossdevil without devil_finish_restart: 0).
            pr.startMs = std::max(0.0, double(Vm::number(Vm::param(p, K("starttime")), 0)));
            if (song && song->key == K("bossdevil") && Vm::number(Vm::param(p, K("devil_finish_restart")), 0) != 1) pr.startMs = 0;
            ++pr.serial;
        }
        // The script goes on once the song has started (App: its venue
        // loaded on a worker), as GH3's start_gem_scroller returns after its
        // loads: the calls after it (Lou's finish's cameras_boss_dead, the
        // boss intro's) reach the new song [C script start_gem_scroller].
        const double until = v.loadingHoldUntil();
        FrontEnd* fp = &fe;
        if (v.clockMs() >= until) {
            if (!startScroller(v, run, p)) return false;
            if (fe.songRequest.pending) run.waitFor = [fp] { return !fp->songRequest.pending; };
            return true;
        }
        auto params = std::make_shared<qb::Struct>(p);
        Vm* vp = &v;
        ScriptRun* rp = &run;
        auto started = std::make_shared<bool>(false);
        run.waitFor = [startScroller, vp, rp, params, until, fp, started] {
            if (!*started) {
                if (vp->clockMs() < until) return false;
                startScroller(*vp, *rp, *params);
                *started = true;
            }
            return !fp->songRequest.pending;
        };
        return true;
    });
    // SongUnLoadFSBIfDownloaded: kill_gem_scroller's (quit / restart from the
    // pause menu, the attract mode's end) -- a playing song is abandoned [C script kill_gem_scroller].
    // gh4's kill_gem_scroller calls SongUnLoad instead (not on a restart);
    // unhandled, a quit song played on under the menus, unpaused by
    // ui_deinit_gameplay's UnpauseGame, and its end (GuitarEvent_SongWon)
    // pulled the menus back to gameplay (owner, GD10-060) [C script
    // kill_gem_scroller (gh4); I: the native unloads the song's streams].
    auto songUnload = [&fe](Vm&, ScriptRun&, qb::Struct&) {
        if (fe.songPlaying) fe.songKill = true;
        fe.songRequest.pending = false;  // a song asked for but not started yet goes too (show_boss_helper_now)
        return true;
    };
    vm.addNative("SongUnLoadFSBIfDownloaded", songUnload);
    vm.addNative("SongUnLoad", songUnload);
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
    // gh4: a pak's QB files join the globals (zone scripts, the screens'
    // uidesc_* descs), once per pak [C data: z_frontend's uidesc_main_menu_layout].
    auto qbLoaded = std::make_shared<std::set<std::string>>();
    // The pak is read and its QB files decoded on a worker; the script waits
    // for them (its frames go on: reading z_tushino here had held the loading
    // screen ~260 ms, no-stutter rule). They join the globals in the order
    // asked (a zone, then its links), when ready: polled by the waiting
    // script and by every later SetPakManCurrent (a script killed while it
    // waited). A script run as a condition reads them at once.
    struct QbLoad {
        std::future<std::vector<qb::File>> work;
        bool added = false;
    };
    auto qbLoads = std::make_shared<std::deque<std::shared_ptr<QbLoad>>>();
    auto drainQbs = [qbLoads](Vm& v) {
        while (!qbLoads->empty()) {
            QbLoad& l = *qbLoads->front();
            if (l.work.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
            for (const qb::File& f : l.work.get()) v.globals().add(f);
            l.added = true;
            qbLoads->pop_front();
        }
    };
    auto addQbs = [&disc, qbLoaded, qbLoads, drainQbs](Vm& v, ScriptRun& run, const std::string& path, bool now) {
        drainQbs(v);
        if (!qbLoaded->insert(path).second) return;
        const Disc* d = &disc;
        auto read = [d, path] {
            std::vector<qb::File> files;
            try {
                const auto pak = Pak::shared(*d, path);
                for (const PakEntry& e : pak->entries())
                    if (e.type == qbkey::key(".qb")) {
                        auto [p, n] = pak->data(e);
                        files.push_back(qb::decode(p, n));
                    }
            } catch (const std::exception& ex) {
                SDL_Log("pak %s: %s", path.c_str(), ex.what());
            }
            return files;
        };
        if ((run.id == 0 || now) && qbLoads->empty()) {
            for (const qb::File& f : read()) v.globals().add(f);
            return;
        }
        auto load = std::make_shared<QbLoad>();
        load->work = std::async(std::launch::async, read);
        qbLoads->push_back(load);
        if (run.id == 0 || now) {  // (a condition's run cannot wait: in now, after the ones before)
            while (!load->added) {
                qbLoads->front()->work.wait();
                drainQbs(v);
            }
            return;
        }
        run.waitFor = [&v, load, drainQbs] {
            drainQbs(v);
            return load->added;
        };
    };
    auto setCurrent = [&disc, &textures, &fe, current, addQbs](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* map = Vm::param(p, K("map"));
        const qb::Value* pak = Vm::param(p, K("pak"));
        const qb::Value* pakname = Vm::param(p, K("pakname"));
        if (!map || (!pak && !pakname)) return false;
        const uint32_t pk = pak ? pak->key : qbkey::key(pakname->str);
        (*current)[map->key] = pk;
        if (map->key == K("zones")) fe.setZone(pk);
        if (pk == K("none")) return true;
        auto load = [&](const std::string& path) {
            static const bool log = SDL_getenv("GD10_FLOWLOG") != nullptr;
            if (log) SDL_Log("pakman: %s %s", qbkey::name(map->key).c_str(), path.c_str());
            if (!disc.exists(path)) {
                v.logOnce("SetPakManCurrent: no " + path);
                return;
            }
            // Under a loading screen (a song's zone, chosen in setlist_choose_song
            // after create_loading_screen) the pak is read and decoded on a
            // worker and uploaded when ready; the song starts after it (App).
            // Read here it had stopped the game for half a second under the
            // loading screen's movie [I: GH3 loads it asynchronously there too].
            if (v.loadingHoldUntil() >= 0) textures.loadPakAsync(disc, path);
            else textures.loadPak(disc, path);
            // A screen's ui_paks (ui_state_pak_load, run by the UI state
            // machine just before ui_create_<state>): in now, the screen's
            // descs are in it (read on a worker, the options, backstage and
            // head-to-head screens were made without them and had no focus:
            // the menus and fuzz walks, GD10-020) [C uistatemachine; I].
            addQbs(v, run, path, map->key == K("ui_paks"));
        };
        if (map->key == K("ui_paks")) {
            // gh4 UI paks (UIPaks, folder 'Pak/ui/'): DATA/COMPRESSED/PAK/<name>.pak.xen [C data].
            const std::string name = pakname && !pakname->str.empty() ? pakname->str : qbkey::name(pk);
            load("DATA/COMPRESSED/PAK/" + name + ".pak.xen");
            return true;
        }
        // Zone keys name the zone (z_soundcheck); its pak is ZONES/<zone>.pak.xen,
        // with the zones GHZones.<zone>.zones links (z_soundcheck: z_frontend) [C data].
        load("DATA/COMPRESSED/ZONES/" + qbkey::name(pk) + ".pak.xen");
        if (map->key == K("zones"))
            if (const qb::Value* gz = v.global(K("GHZones")); gz && gz->st)
                if (const qb::Value* z = gz->st->find(pk); z && z->st)
                    if (const qb::Value* links = z->st->find(K("zones")); links && links->arr)
                        for (const qb::Value& l : links->arr->values) load("DATA/COMPRESSED/ZONES/" + qbkey::name(l.key) + ".pak.xen");
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
        // gh4: true with pak = none when nothing is current (frontend_load_soundcheck
        // then loads z_soundcheck) [I: GH3 returned false; the gh4 script needs true].
        const uint32_t pk = it == current->end() ? K("none") : it->second;
        if (qb::Struct* l = locals(run)) {
            Vm::set(*l, K("pak"), Vm::makeKey(pk));
            Vm::set(*l, K("pakname"), Vm::makeString(qbkey::name(pk)));
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
