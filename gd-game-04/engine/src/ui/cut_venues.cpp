// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/cut_venues.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>

#include "data/qbkey.h"
#include "script/vm.h"
#include "ui/front_end.h"
#include "ui/ui_system.h"

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

qb::Value keyVal(uint32_t k) { return qs::Vm::makeKey(k); }

qb::Value structVal(qb::Struct s) {
    qb::Value v;
    v.type = qb::Type::Struct;
    v.st = std::make_shared<qb::Struct>(std::move(s));
    return v;
}

void add(qb::Struct& s, uint32_t name, qb::Value v) { s.items.push_back({name, std::move(v)}); }

// LaunchEvent type = t target = id (the native, outside any script).
void launch(qs::Vm& vm, const char* type, const char* target) {
    qs::ScriptRun r;
    qb::Struct p;
    add(p, K("type"), keyVal(K(type)));
    add(p, K("target"), keyVal(K(target)));
    vm.callNative(K("LaunchEvent"), r, p);
}

bool isKey(const qb::Value* v, uint32_t k) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) && v->key == k; }

}  // namespace

CutVenues::CutVenues(FrontEnd& fe) : fe_(fe) {
    // gd04_cut_venue_pick level = <LevelZoneArray key> (0 / absent: cancel):
    // the popup's options' func [the port's].
    fe_.vm().addNative("gd04_cut_venue_pick", [this](qs::Vm&, qs::ScriptRun&, qb::Struct& p) {
        const qb::Value* lv = qs::Vm::param(p, K("level"));
        const uint32_t level = lv && (lv->type == qb::Type::Key || lv->type == qb::Type::KeyPtr) ? lv->key : 0;
        if (level) pending_ = level;
        SDL_Log("cut venues: %s", level ? ("the next song in " + qbkey::name(level)).c_str() : "cancelled");
        close();
        return true;
    });
}

bool CutVenues::available() const {
    if (!qs::Vm::cutContent || fe_.flowState() != "quickplay_setlist_fs") return false;
    return isKey(fe_.vm().global(K("current_tab")), K("tab_setlist"));
}

bool CutVenues::button(const InputEvent& e) {
    if (!e.down) return false;
    if (open_) {
        // Back closes it (GH3's popup has no pad_back handler of its own).
        if (e.action == Action::Back || e.action == Action::Fret1) {
            fe_.soundEvent("ui_sfx_back");
            close();
            return true;
        }
        // Only the popup's own buttons (up / down / select) reach GH3.
        return !(e.action == Action::Up || e.action == Action::Down || e.action == Action::Confirm || e.action == Action::Fret0);
    }
    if (e.action == Action::Fret2 && available()) {
        fe_.soundEvent("ui_sfx_select");
        open();
        return true;
    }
    return false;
}

void CutVenues::open() {
    qs::Vm& vm = fe_.vm();
    // The venues GH3's LevelZones marks $debug_only, but z_viewer (no scene)
    // [C data LevelZoneArray, LevelZones].
    qb::Value options;
    options.type = qb::Type::Array;
    options.arr = std::make_shared<qb::Array>();
    options.arr->type = qb::Type::Struct;
    const qb::Value* arr = vm.global(K("LevelZoneArray"));
    const qb::Value* zones = vm.global(K("LevelZones"));
    if (arr && arr->arr && zones && zones->st)
        for (const qb::Value& lk : arr->arr->values) {
            const qb::Value* z = zones->st->find(lk.key);
            if (!z || !z->st) continue;
            bool debugOnly = false;
            for (const qb::Item& it : z->st->items)
                if (!it.name && isKey(&it.value, K("debug_only"))) debugOnly = true;
            const qb::Value* nm = z->st->find(K("name"));
            const qb::Value* title = z->st->find(K("title"));
            if (!debugOnly || !nm || nm->str == "z_viewer") continue;
            std::string text = title ? title->str : nm->str;
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::toupper(c)); });
            qb::Struct fp, func, opt;
            add(fp, K("level"), keyVal(lk.key));
            add(func, 0, keyVal(K("gd04_cut_venue_pick")));
            add(func, K("params"), structVal(fp));
            add(opt, K("func"), structVal(func));
            add(opt, K("text"), qs::Vm::makeString(text));
            options.arr->values.push_back(structVal(opt));
        }
    // GH3's popup as its lefty flip warning makes it (over the set list: no
    // background) [C script create_lefty_flip_warning_menu].
    qb::Struct tb;
    add(tb, K("text"), qs::Vm::makeString("Pick the venue of the next song. The song after it is back to a random venue. BACK cancels."));
    add(tb, K("dims"), [] {
        qb::Value v;
        v.type = qb::Type::Pair;
        v.f[0] = 800, v.f[1] = 400;
        return v;
    }());
    add(tb, K("scale"), qs::Vm::makeFloat(0.55f));
    qb::Struct p;
    add(p, K("title"), qs::Vm::makeString("CUT VENUES"));
    add(p, K("textblock"), structVal(tb));
    add(p, K("player_device"), vm.global(K("primary_controller")) ? *vm.global(K("primary_controller")) : qs::Vm::makeInt(0));
    add(p, 0, keyVal(K("no_background")));
    add(p, K("options"), options);
    launch(vm, "unfocus", "vmenu_setlist");
    open_ = true;
    vm.run(K("create_popup_warning_menu"), p);
}

void CutVenues::close() {
    if (!open_) return;
    open_ = false;
    qs::Vm& vm = fe_.vm();
    vm.run(K("destroy_popup_warning_menu"), qb::Struct{});
    launch(vm, "focus", "vmenu_setlist");
}

void CutVenues::scriptEnter(uint32_t name) {
    static const uint32_t startSong = K("start_song"), helper = K("setlist_show_helperbar");
    if (name == helper) pillWanted_ = true;
    if (name != startSong || !pending_) return;
    // quickplay_start_song has drawn its random venue into current_level;
    // the picked one replaces it for this song [C script quickplay_start_song].
    qs::Vm& vm = fe_.vm();
    if (isKey(vm.global(K("game_mode")), K("p1_quickplay"))) {
        SDL_Log("cut venues: this song in %s", qbkey::name(pending_).c_str());
        vm.setGlobal(K("current_level"), keyVal(pending_));
    }
    pending_ = 0;
}

void CutVenues::update() {
    if (!pillWanted_) return;
    pillWanted_ = false;
    if (!available() || !fe_.ui().find(K("user_control_container"))) return;
    // GH3's own pill, after setlist_show_helperbar's (its colours stay set);
    // a sixth pill fits with a narrower gap than create_sl_assets' 100
    // (user_control_build_pills lays them out again) [C scripts; I: 50].
    fe_.vm().setGlobal(K("user_control_pill_gap"), qs::Vm::makeInt(50));
    qb::Struct p;
    add(p, K("text"), qs::Vm::makeString("VENUE"));
    add(p, K("button"), keyVal(K("yellow")));
    add(p, K("z"), qs::Vm::makeInt(100));
    fe_.vm().run(K("add_user_control_helper"), p);
}
