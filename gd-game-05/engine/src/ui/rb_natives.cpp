// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Rock Band's front-end host objects (the natives RB's ui/*.dta scripts call):
// gamemode, gamecfg and its player configs, session, platform_mgr,
// profile_mgr, memcardmgr, band_ui_sink, tour, char_cache, song_mgr.
// Wave 01 (GD05-005): enough of each for the quickplay path, every message
// it does not know logged once (GD05_UILOG, lesson L1); what each answers
// without having read the original's handler is [I] and listed in
// docs/audit.md. Values RB keeps in the data (modes.dtb) are read from it.
#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/dta.h"
#include "game/song_db.h"
#include "render/ui_view.h"
#include "ui/natives.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"

namespace ui {

namespace {

std::string strArg(const std::vector<Value>& a, size_t i) { return i < a.size() ? a[i].str() : std::string(); }

// A host object with a property map (get / set), add_sink / remove_sink,
// a table of native messages, and a logged default (0) for the others.
class RbHost : public ScriptObject {
public:
    using Fn = std::function<Value(std::vector<Value>&)>;
    RbHost(UiManager& ui, const std::string& name) : ScriptObject(name, "RbHost"), ui_(ui) {}
    void on(const std::string& msg, Fn fn) { fns_[msg] = std::move(fn); }
    void quiet(std::initializer_list<const char*> msgs) {
        for (const char* m : msgs) fns_[m] = [](std::vector<Value>&) { return Value::integer(0); };
    }
    // Messages the port does not have yet: 0, logged as STUBs (tests/ui_stubs.txt says why).
    void stub(std::initializer_list<const char*> msgs) {
        for (const char* m : msgs) {
            const std::string msg = m;
            fns_[msg] = [this, msg](std::vector<Value>&) { return ui_.interp().onStub(name(), msg, Value::integer(0)), Value::integer(0); };
        }
    }
    std::map<std::string, Value> props;

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        (void)in;
        if (auto it = fns_.find(msg); it != fns_.end()) return r = it->second(args), true;
        if (msg == "add_sink" || msg == "remove_sink") return r = Value::integer(0), true;
        if (msg == "get" && !args.empty()) {
            auto it = props.find(args[0].str());
            r = it != props.end() ? it->second : Value::integer(0);
            if (it == props.end()) ui_.logOnce("STUB " + name() + " get " + args[0].str());
            return true;
        }
        if (msg == "set" && args.size() >= 2) return props[args[0].str()] = args[1], r = Value::integer(0), true;
        // Unknown messages stay unhandled (logged by the interpreter as
        // UNHANDLED), so event messages sent to sinks (button_down ...) pass on.
        return false;
    }

protected:
    UiManager& ui_;
    std::map<std::string, Fn> fns_;
};


// song_provider, Rock Band's (ui/selsong.dta): the songs of the disc that
// have a rank (songs.dtb), shown by the display RB's song select cycles
// (rank first: band_ui / song_groupings in band.dtb), each group under a
// heading, "random_song" first. Grouping by rank: the songs sorted by the
// player's instrument's rank (the band's for a band), split into the nine
// generic_tier groups by count [I: the original's grouping, RB 0x82404A10
// -> 0x824024F0, is not read yet; DECISIONS D5].
class RbSongProvider : public ListProvider {
public:
    explicit RbSongProvider(UiManager& ui) : ListProvider("song_provider", "SongProvider"), ui_(ui) {}

    void initData() override { build(); }
    int numData() override {
        build();
        return int(data_.size());
    }
    std::string symbol(int i) override {
        build();
        return i >= 0 && i < int(data_.size()) ? data_[size_t(i)] : std::string();
    }
    bool active(int i) override { return i >= 0 && i < int(data_.size()); }
    void rows(std::vector<gfx::UiView::ListRow>& out, int selected, int& selectedRow) override {
        build();
        out.clear();
        selectedRow = 0;
        int headersSoFar = 0;
        for (int i = 0; i < int(data_.size()); ++i) {
            gfx::UiView::ListRow r;
            for (const auto& [idx, text] : headers_)
                if (idx == i) r.header = true, r.texts["heading.lbl"] = text, ++headersSoFar;
            r.slot = float(i + headersSoFar);
            const std::string& sym = data_[size_t(i)];
            if (sym == "random_song") {
                r.texts["list.lbl"] = ui_.localize("random_song");
            } else {
                const int info = songInfoIndex(ui_, sym);
                r.texts["list.lbl"] = info >= 0 ? ui_.host().songs()[size_t(info)].name : sym;
            }
            if (i == selected) selectedRow = int(out.size());
            out.push_back(std::move(r));
        }
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        (void)in;
        r = Value::integer(0);
        auto ix = [&] { return args.empty() ? -1 : args[0].integerValue(); };
        if (msg == "set_song_mode") return mode_ = strArg(args, 0), built_ = false, true;
        if (msg == "get_display") return r = Value::integer(display_), true;
        // The instrument the list ranks by: player 0's controller [I: one local player].
        if (msg == "get_instrument") {
            const Value cfg = in.send("game", "get_player_config", {Value::integer(0)});
            r = cfg.type == Value::Type::Object ? in.send(cfg.object, "get_controller_sym", {}) : Value::symbol("guitar");
            return true;
        }
        if (msg == "set_display") return display_ = args.empty() ? 0 : args[0].integerValue(), built_ = false, true;
        if (msg == "cycle_display") return display_ = (display_ + 1) % 4, built_ = false, true;
        if (msg == "is_active") return r = Value::integer(active(ix()) ? 1 : 0), true;
        if (msg == "is_random") return r = Value::integer(symbol(ix()) == "random_song" ? 1 : 0), true;
        if (msg == "is_invalid_song") return r = Value::integer(symbol(ix()).empty() ? 1 : 0), true;
        if (msg == "get_song_to_set") {
            std::string sym = symbol(ix());
            if (sym == "random_song" && data_.size() > 1) sym = data_[1 + size_t(std::rand()) % (data_.size() - 1)];
            return r = Value::symbol(sym), true;
        }
        if (msg == "num_data") return r = Value::integer(numData()), true;
        if (msg == "get_random_song") return r = Value::symbol(data_.size() > 1 ? data_[1] : std::string()), true;
        if (msg == "add_recent_song") return true;
        return false;
    }

private:
    void build() {
        if (built_) return;
        built_ = true;
        data_ = {"random_song"};
        headers_.clear();
        std::vector<const SongInfo*> songs;
        for (const SongInfo& s : ui_.host().songs())
            if (!s.tutorial) songs.push_back(&s);
        const std::string inst = "band";
        std::stable_sort(songs.begin(), songs.end(), [&](const SongInfo* a, const SongInfo* b) {
            auto ra = a->rank.count(inst) ? a->rank.at(inst) : 0, rb = b->rank.count(inst) ? b->rank.at(inst) : 0;
            return ra < rb;
        });
        const int tiers = 9, n = int(songs.size());
        for (int i = 0; i < n; ++i) {
            const int tier = n ? i * tiers / n : 0;
            if (i == 0 || tier != (i - 1) * tiers / n)
                headers_.push_back({int(data_.size()), ui_.localize("generic_tier" + std::to_string(tier + 1))});
            data_.push_back(songs[size_t(i)]->id);
        }
    }

    UiManager& ui_;
    std::string mode_ = "game";
    int display_ = 0;
    bool built_ = false;
    std::vector<std::string> data_;
    std::vector<std::pair<int, std::string>> headers_;
};

}  // namespace

void installRbNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned) {
    auto add = [&](std::unique_ptr<RbHost> o) -> RbHost* {
        RbHost* raw = o.get();
        in.registerObject(raw);
        owned.push_back(std::move(o));
        return raw;
    };
    auto yes = [](std::vector<Value>&) { return Value::integer(1); };
    auto no = [](std::vector<Value>&) { return Value::integer(0); };

    // A stand-in character per instrument until the characters come (M6,
    // GD05-008): what the menus ask a character [I]. The icons are
    // config/tour_solo.dta's track_icons, first row [C data].
    static const std::map<std::string, std::string> kIcon = {{"bass", "B"}, {"guitar", "G"}, {"drum", "D"}, {"vocals", "V"}};
    for (const char* inst : {"guitar", "bass", "drum", "vocals"}) {
        RbHost* c = add(std::make_unique<RbHost>(ui, std::string("prefab_") + inst));
        const std::string instrument = std::string(inst) == "bass" ? "guitar" : inst;
        c->props["instrument"] = Value::symbol(instrument);
        c->on("get_instrument", [instrument](std::vector<Value>&) { return Value::symbol(instrument); });
        c->on("instrument_icon", [instrument](std::vector<Value>&) { return Value::string(kIcon.at(instrument)); });
        c->on("track_icon", [instrument](std::vector<Value>& a) {
            const std::string t = strArg(a, 0);
            auto it = kIcon.find(t.empty() ? instrument : t);
            return Value::string(it != kIcon.end() ? it->second : kIcon.at(instrument));
        });
        c->on("get_char_name", [inst](std::vector<Value>&) { return Value::string(std::string(inst) == "drum" ? "Drummer" : std::string(inst) == "vocals" ? "Singer" : "Guitarist"); });
        c->on("get_gender", [](std::vector<Value>&) { return Value::symbol("male"); });
        c->quiet({"money", "deform", "world_fame", "tour_score", "total_world_fame"});
    }

    // gamemode: the mode's settings from config/gen/modes.dtb (defaults,
    // then the mode's own block) [C data].
    // gamemode: RB's mode switch over the same table gamecfg reads (the
    // session's mode in config/gen/modes.dtb, then (defaults)) [C data].
    RbHost* gamemode = add(std::make_unique<RbHost>(ui, "gamemode"));
    auto modeValue = [&ui](const std::string& key) { return gameCfgValue(ui, key); };
    gamemode->on("set_mode", [&ui](std::vector<Value>& a) {
        Session& s = ui.session();
        s.mode = strArg(a, 0);
        s.cfgSet.clear();
        if (a.size() > 1) s.cfgSet["player_num"] = a[1];
        return Value::integer(0);
    });
    gamemode->on("get_mode", [&ui](std::vector<Value>&) { return Value::symbol(ui.session().mode); });
    gamemode->on("get", [modeValue](std::vector<Value>& a) { return modeValue(strArg(a, 0)); });
    // {gamemode intro_category}: the mode's (intro_category ...) (modes.dta) [C data].
    gamemode->on("intro_category", [modeValue](std::vector<Value>&) { return modeValue("intro_category"); });
    gamemode->on("is_solo", [modeValue](std::vector<Value>&) { return Value::integer(modeValue("max_players").integerValue() == 1 ? 1 : 0); });
    gamemode->on("is_hth_arrangement", [modeValue](std::vector<Value>&) { return Value::integer(modeValue("player_arrangement").integerValue() == 1 ? 1 : 0); });
    gamemode->on("is_band_arrangement", [modeValue](std::vector<Value>&) { return Value::integer(modeValue("player_arrangement").integerValue() == 0 ? 1 : 0); });
    gamemode->on("is_bwt", [&ui](std::vector<Value>&) { return Value::integer(ui.session().mode == "tour_world_local" ? 1 : 0); });
    gamemode->on("is_solo_tour", [&ui](std::vector<Value>&) { return Value::integer(ui.session().mode == "tour_solo" ? 1 : 0); });
    gamemode->on("separate_parts", [modeValue](std::vector<Value>&) { return modeValue("separate_parts"); });

    // Online play is not in the port (GD05-013): a local session, signed out.
    RbHost* session = add(std::make_unique<RbHost>(ui, "session"));
    session->on("mode", no);  // kNetLocal
    session->on("num_players", [](std::vector<Value>&) { return Value::integer(1); });  // [I] solo until M7's band modes
    session->on("is_busy", no);
    session->on("disabled", no);
    // {session state}: kNetInGame (the .dtb's 2) while a song is loaded
    // (ui/game.dta on_pause) [C data], else 0 [I: the other states' values].
    session->on("state", [&ui](std::vector<Value>&) { return Value::integer(ui.host().gameLoaded() ? 2 : 0); });
    session->on("num_open_slots", no);
    session->quiet({"clear", "send_to_all", "send_to_player", "set_all_local_ready_status", "set_invites_allowed", "ignore_remote_messages", "done_loading"});
    RbHost* platform = add(std::make_unique<RbHost>(ui, "platform_mgr"));
    platform->on("is_player_signed_in", no);
    platform->on("guide_showing", no);
    platform->on("has_player_signin_changed", no);
    platform->on("is_enter_cancel_swapped", no);
    platform->on("is_player_a_guest", no);
    platform->quiet({"set_notify_ui_location", "disable_xmp", "enable_xmp", "signin"});
    RbHost* profiles = add(std::make_unique<RbHost>(ui, "profile_mgr"));
    // The profiles' save data: RB's saves are M7 (GD05-009; DECISIONS D4).
    profiles->stub({"get_save_data_from_player", "get_char_options_from_player"});
    profiles->on("first_timer", no);
    profiles->on("get_sync_offset", no);
    profiles->on("get_default_sync_offset_ms", no);
    profiles->on("has_profile_map", no);
    profiles->quiet({"set_sync_offset", "update_mic_gain", "update_vocal_volume", "purge_old_data"});
    RbHost* memcard = add(std::make_unique<RbHost>(ui, "memcardmgr"));
    memcard->on("is_device_valid", no);
    // No profile signs in on PC: the Xbox save flow is skipped (-1 = nobody),
    // the port keeps its own JSON save (lesson L12) [I] (DECISIONS D4).
    auto nobody = [](std::vector<Value>&) { return Value::integer(-1); };
    memcard->on("get_new_signin_player_num", nobody);
    memcard->on("get_autosavable_player_num", nobody);
    memcard->quiet({"saveload_profile_complete", "saveload_all_complete", "select_device"});
    RbHost* sink = add(std::make_unique<RbHost>(ui, "band_ui_sink"));
    sink->on("has_event", no);
    sink->on("is_forced_transition_active", no);
    // sync_screen <screen> [depth]: the band's screen change, local only here [I: the network sync].
    sink->on("sync_screen", [&ui](std::vector<Value>& a) {
        if (!a.empty()) ui.gotoScreen(a[0].type == Value::Type::Object && a[0].object ? a[0].object->name() : a[0].str(), false);
        return Value::integer(0);
    });
    sink->quiet({"set_net_sync", "net_sync", "set_block_mode", "force_reset_screen", "dismiss_event", "hide_network_busy_icon",
                 "show_network_busy_icon", "set_controller_level"});
    // check_controllers: every player's controller is there (ui/game.dta: FALSE
    // pushes pause_controller_screen) [I: the port's own disconnect pause is the input layer's].
    sink->on("check_controllers", yes);
    RbHost* tour = add(std::make_unique<RbHost>(ui, "tour"));
    tour->on("performer", no);
    tour->quiet({"clear_performer"});
    RbHost* charCache = add(std::make_unique<RbHost>(ui, "char_cache"));
    // The characters' cache: the band's loading is M6's (GD05-008) [I].
    charCache->quiet({"cache_update", "cache_enable", "cache_clear", "iterate", "meta_configure_done", "light_characters", "load_instruments",
                      "world_configure_characters", "reclaim_characters"});
    charCache->on("loaded", yes);
    // content_mgr: the Music Store / downloads are not in the port (GD05-013):
    // the disc's content is all there is, its refresh always done.
    RbHost* content = add(std::make_unique<RbHost>(ui, "content_mgr"));
    content->on("refresh_done", yes);
    content->quiet({"start_refresh"});
    // session_mgr add_local_player: the local session is ready at once; the
    // panel that asked (wait_for_session_ready) gets session_ready_complete_msg
    // [I: the original's SessionMgr completes asynchronously].
    RbHost* sessionMgr = add(std::make_unique<RbHost>(ui, "session_mgr"));
    sessionMgr->on("add_local_player", [&ui](std::vector<Value>&) {
        if (ScriptObject* w = ui.sessionWaiter()) {
            ui.setSessionWaiter(nullptr);
            ui.interp().send(w, "session_ready_complete_msg", {});
        }
        return Value::integer(0);
    });
    sessionMgr->on("is_finding", no);
    sessionMgr->quiet({"remove_local_player", "cancel_find", "update_band_leader", "send_to_all"});
    {
        auto sp = std::make_unique<RbSongProvider>(ui);
        in.registerObject(sp.get());
        owned.push_back(std::move(sp));
    }
    RbHost* songMgr = add(std::make_unique<RbHost>(ui, "song_mgr"));
    songMgr->on("has_song", [&ui](std::vector<Value>& a) { return Value::integer(songInfoIndex(ui, strArg(a, 0)) >= 0 ? 1 : 0); });
    songMgr->on("is_song_shared", yes);
    songMgr->quiet({"reset_shared_songs"});
    // song_mgr song_name / by_line: the song's name, and "by <artist>" [I: the by-line's wording is the locale's].
    songMgr->on("song_name", [&ui](std::vector<Value>& a) {
        const int i = songInfoIndex(ui, strArg(a, 0));
        return Value::string(i >= 0 ? ui.host().songs()[size_t(i)].name : strArg(a, 0));
    });
    songMgr->on("song_artist", [&ui](std::vector<Value>& a) {
        const int i = songInfoIndex(ui, strArg(a, 0));
        return Value::string(i >= 0 ? ui.host().songs()[size_t(i)].artist : std::string());
    });
    songMgr->on("by_line", [&ui](std::vector<Value>& a) {
        const int i = songInfoIndex(ui, strArg(a, 0));
        return Value::string(i >= 0 ? ui.host().songs()[size_t(i)].artist : std::string());
    });
}

}  // namespace ui
