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
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/dta.h"
#include "game/song_db.h"
#include "ui/natives.h"
#include "ui/ui_host.h"
#include "ui/ui_manager.h"

namespace ui {

namespace {

int intArg(const std::vector<Value>& a, size_t i, int def = 0) { return i < a.size() && a[i].isNumber() ? a[i].integerValue() : def; }
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

// A player's configuration (gamecfg get_player_config n) [I: the fields
// the scripts read, set by the menus].
class PlayerConfigObj : public RbHost {
public:
    PlayerConfigObj(UiManager& ui, int n) : RbHost(ui, "player_config" + std::to_string(n)), num(n) {
        static const char* const kTracks[] = {"drum", "bass", "guitar", "vocals", "none"};
        on("player", [this](std::vector<Value>&) { return Value::integer(num); });
        on("player_num", [this](std::vector<Value>&) { return Value::integer(num); });
        on("is_local_player", [this](std::vector<Value>&) { return Value::integer(num == 0 ? 1 : 0); });
        on("is_participating", [this](std::vector<Value>&) { return Value::integer(participating ? 1 : 0); });
        on("set_participating", [this](std::vector<Value>& a) { return participating = intArg(a, 0) != 0, Value::integer(0); });
        on("get_track_type", [this](std::vector<Value>&) { return Value::integer(track); });
        on("set_track_type", [this](std::vector<Value>& a) { return track = intArg(a, 0, track), Value::integer(0); });
        on("get_track_sym", [this](std::vector<Value>&) { return Value::symbol(kTracks[std::clamp(track, 0, 4)]); });
        on("get_controller_type", [this](std::vector<Value>&) { return Value::integer(track == 2 || track == 1 ? 1 : 0); });
        on("get_controller_sym", [this](std::vector<Value>&) { return Value::symbol(track == 0 ? "drum" : track == 3 ? "vocals" : "guitar"); });
        on("get_difficulty", [this](std::vector<Value>&) { return Value::integer(difficulty); });
        on("set_difficulty", [this](std::vector<Value>& a) { return difficulty = intArg(a, 0, difficulty), Value::integer(0); });
        on("get_difficulty_sym", [this](std::vector<Value>&) {
            static const char* const kDiff[] = {"easy", "medium", "hard", "expert"};
            return Value::symbol(kDiff[std::clamp(difficulty, 0, 3)]);
        });
        on("get_char", [this](std::vector<Value>&) { return character; });
        on("set_char", [this](std::vector<Value>& a) { return character = a.empty() ? Value::integer(0) : a[0], Value::integer(0); });
        quiet({"set_ui_waiting", "reset"});
    }
    int num;
    int track = 2;          // kTrackGuitar
    int difficulty = 3;     // kDifficultyExpert
    bool participating = false;
    Value character = Value::integer(0);
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

    // The four player configs and gamecfg (GameConfig).
    std::array<PlayerConfigObj*, 4> cfg{};
    for (int p = 0; p < 4; ++p) {
        auto o = std::make_unique<PlayerConfigObj>(ui, p);
        cfg[size_t(p)] = o.get();
        add(std::move(o));
    }
    cfg[0]->participating = true;
    RbHost* gamecfg = add(std::make_unique<RbHost>(ui, "gamecfg"));
    gamecfg->on("get_player_config", [cfg](std::vector<Value>& a) { return Value::obj(cfg[size_t(std::clamp(intArg(a, 0), 0, 3))]); });
    gamecfg->on("get_player", [cfg](std::vector<Value>& a) { return Value::obj(cfg[size_t(std::clamp(intArg(a, 0), 0, 3))]); });
    gamecfg->on("get_player_num_from_slot_num", [](std::vector<Value>& a) { return Value::integer(intArg(a, 0)); });
    gamecfg->on("is_player_local", [](std::vector<Value>& a) { return Value::integer(intArg(a, 0) == 0 ? 1 : 0); });
    gamecfg->on("get_instrument_from_controller", [](std::vector<Value>&) { return Value::symbol("guitar"); });
    gamecfg->on("get_controller_type_from_controller", [](std::vector<Value>&) { return Value::integer(1); });
    gamecfg->quiet({"auto_assign_missing_slots", "debug_get_force_controller", "foreach_player_config"});

    // gamemode: the mode's settings from config/gen/modes.dtb (defaults,
    // then the mode's own block) [C data].
    RbHost* gamemode = add(std::make_unique<RbHost>(ui, "gamemode"));
    auto mode = std::make_shared<std::string>("init");
    auto modeValue = [&ui, mode](const std::string& key) -> Value {
        const DataNode* modes = ui.modes();
        if (!modes) return Value::integer(0);
        for (const char* block : {mode->c_str(), "defaults"}) {
            const DataNode* b = modes->find(block);
            if (!b) continue;
            if (const DataNode* e = b->find(key); e && e->children.size() > 1) return ui.interp().toValue(e->children[1]);
        }
        return Value::integer(0);
    };
    gamemode->on("set_mode", [mode](std::vector<Value>& a) { return *mode = strArg(a, 0), Value::integer(0); });
    gamemode->on("get_mode", [mode](std::vector<Value>&) { return Value::symbol(*mode); });
    gamemode->on("get", [modeValue](std::vector<Value>& a) { return modeValue(strArg(a, 0)); });
    gamemode->on("is_solo", [modeValue](std::vector<Value>&) { return Value::integer(modeValue("max_players").integerValue() == 1 ? 1 : 0); });
    gamemode->on("is_hth_arrangement", [modeValue](std::vector<Value>&) { return Value::integer(modeValue("player_arrangement").integerValue() == 1 ? 1 : 0); });
    gamemode->on("is_band_arrangement", [modeValue](std::vector<Value>&) { return Value::integer(modeValue("player_arrangement").integerValue() == 0 ? 1 : 0); });
    gamemode->on("is_bwt", [mode](std::vector<Value>&) { return Value::integer(mode->find("bwt") != std::string::npos || *mode == "tour_band" ? 1 : 0); });
    gamemode->on("is_solo_tour", [mode](std::vector<Value>&) { return Value::integer(*mode == "tour_solo" ? 1 : 0); });
    gamemode->on("separate_parts", [modeValue](std::vector<Value>&) { return modeValue("separate_parts"); });

    // Online play is not in the port (GD05-013): a local session, signed out.
    RbHost* session = add(std::make_unique<RbHost>(ui, "session"));
    session->on("mode", no);  // kNetLocal
    session->on("num_players", [cfg](std::vector<Value>&) {
        int n = 0;
        for (auto* c : cfg) n += c->participating ? 1 : 0;
        return Value::integer(n);
    });
    session->on("is_busy", no);
    session->on("disabled", no);
    session->on("state", no);
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
    profiles->on("first_timer", no);
    profiles->on("get_sync_offset", no);
    profiles->on("get_default_sync_offset_ms", no);
    profiles->on("has_profile_map", no);
    profiles->quiet({"set_sync_offset", "update_mic_gain", "update_vocal_volume"});
    RbHost* memcard = add(std::make_unique<RbHost>(ui, "memcardmgr"));
    memcard->on("is_device_valid", no);
    RbHost* sink = add(std::make_unique<RbHost>(ui, "band_ui_sink"));
    sink->on("has_event", no);
    sink->on("is_forced_transition_active", no);
    sink->quiet({"set_net_sync", "set_block_mode", "force_reset_screen", "sync_screen", "dismiss_event", "hide_network_busy_icon",
                 "show_network_busy_icon", "check_controllers", "set_controller_level"});
    RbHost* tour = add(std::make_unique<RbHost>(ui, "tour"));
    tour->on("performer", no);
    tour->quiet({"clear_performer"});
    RbHost* charCache = add(std::make_unique<RbHost>(ui, "char_cache"));
    charCache->quiet({"cache_update", "cache_enable", "cache_clear", "iterate", "meta_configure_done"});
    RbHost* songMgr = add(std::make_unique<RbHost>(ui, "song_mgr"));
    songMgr->on("has_song", [&ui](std::vector<Value>& a) { return Value::integer(songInfoIndex(ui, strArg(a, 0)) >= 0 ? 1 : 0); });
    songMgr->on("is_song_shared", yes);
    songMgr->quiet({"reset_shared_songs"});
}

}  // namespace ui
