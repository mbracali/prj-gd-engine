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
    sink->quiet({"set_net_sync", "net_sync", "set_block_mode", "force_reset_screen", "sync_screen", "dismiss_event", "hide_network_busy_icon",
                 "show_network_busy_icon", "check_controllers", "set_controller_level"});
    RbHost* tour = add(std::make_unique<RbHost>(ui, "tour"));
    tour->on("performer", no);
    tour->quiet({"clear_performer"});
    RbHost* charCache = add(std::make_unique<RbHost>(ui, "char_cache"));
    charCache->quiet({"cache_update", "cache_enable", "cache_clear", "iterate", "meta_configure_done"});
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
    sessionMgr->quiet({"remove_local_player", "cancel_find", "update_band_leader"});
    RbHost* songMgr = add(std::make_unique<RbHost>(ui, "song_mgr"));
    songMgr->on("has_song", [&ui](std::vector<Value>& a) { return Value::integer(songInfoIndex(ui, strArg(a, 0)) >= 0 ? 1 : 0); });
    songMgr->on("is_song_shared", yes);
    songMgr->quiet({"reset_shared_songs"});
}

}  // namespace ui
