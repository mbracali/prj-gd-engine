// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_gh4.h"

#include <memory>
#include <string>

#include "data/qbkey.h"
#include "script/globals.h"
#include "script/vm.h"

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

// The player number (first unnamed int) and the field (first unnamed key).
void args(const qb::Struct& p, int& player, uint32_t& field) {
    player = 0, field = 0;
    for (const qb::Item& it : p.items) {
        if (it.name) continue;
        if (it.value.type == qb::Type::Int && !player) player = it.value.i;
        else if ((it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && !field) field = it.value.key;
    }
}

// player<n>_status, filled once from player_shared_defaults and
// player<n>_defaults (playerinfo.qb: the statuses start empty) [C data; I:
// when the C fills them].
qb::Struct* status(qs::Vm& vm, int player) {
    if (player < 1 || player > 8) return nullptr;
    const uint32_t name = qbkey::key("player" + std::to_string(player) + "_status");
    qb::Value* v = vm.globals().ensure(name);
    if (v->type != qb::Type::Struct || !v->st) {
        v->type = qb::Type::Struct;
        v->st = std::make_shared<qb::Struct>();
    }
    if (v->st->items.empty()) {
        for (const char* d : {"player_shared_defaults", ""}) {
            const std::string dn = *d ? d : "player" + std::to_string(player) + "_defaults";
            if (const qb::Value* def = vm.global(qbkey::key(dn)); def && def->st)
                for (const qb::Item& it : vm.globals().expand(*def->st)) qs::Vm::set(*v->st, it.name, it.value);
        }
    }
    return v->st.get();
}

}  // namespace

void addGh4Natives(qs::Vm& vm) {
    using qs::ScriptRun;
    using qs::Vm;
    // GetPlayerInfo <n> <field>: the field of player n's info into a local
    // of its name [C scripts: 1,296 calls; I: the info lives in player<n>_status].
    vm.addNative("GetPlayerInfo", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        int player;
        uint32_t field;
        args(p, player, field);
        qb::Struct* st = status(v, player);
        if (!st || !field) return false;
        const qb::Value* val = st->find(field);
        if (!val) return false;
        if (!run.frames.empty()) {
            if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
            Vm::set(*run.frames.back().locals, field, *val);
        }
        return true;
    });
    // SetPlayerInfo <n> <field> = <value> (named items set those fields).
    vm.addNative("SetPlayerInfo", [](Vm& v, ScriptRun&, qb::Struct& p) {
        int player;
        uint32_t field;
        args(p, player, field);
        qb::Struct* st = status(v, player);
        if (!st) return false;
        for (const qb::Item& it : p.items)
            if (it.name) Vm::set(*st, it.name, it.value);
        return true;
    });
    // The game mode (game_mode = p1_quickplay ...): <mode>_props over
    // default_gamemode_props [C data gamemodes.qb; I: the merge].
    auto modeProp = [](Vm& v, uint32_t prop) -> qb::Value {
        const qb::Value* gm = v.global(K("game_mode"));
        const uint32_t mode = gm ? gm->key : 0;
        for (const std::string& name : {qbkey::name(mode) + "_props", std::string("default_gamemode_props")})
            if (const qb::Value* ps = v.global(qbkey::key(name)); ps && ps->st)
                if (const qb::Value* x = v.globals().field(*ps->st, prop)) return *x;
        return qb::Value{};
    };
    auto setLocal = [](ScriptRun& run, uint32_t name, const qb::Value& val) {
        if (run.frames.empty()) return;
        if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
        Vm::set(*run.frames.back().locals, name, val);
    };
    vm.addNative("GameMode_GetType", [modeProp, setLocal](Vm& v, ScriptRun& run, qb::Struct&) {
        setLocal(run, K("type"), modeProp(v, K("type")));
        return true;
    });
    vm.addNative("GameMode_GetProperty", [modeProp, setLocal](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* pr = Vm::param(p, K("prop"));
        if (!pr) return false;
        qb::Value val = modeProp(v, pr->key);
        if (val.type == qb::Type::None) return false;
        setLocal(run, pr->key, val);
        return Vm::truth(val);
    });
    // num_players (and num_players_shown: the same outside online play) [I].
    vm.addNative("GameMode_GetNumPlayers", [modeProp, setLocal](Vm& v, ScriptRun& run, qb::Struct&) {
        qb::Value n = modeProp(v, K("num_players"));
        if (n.type == qb::Type::None) n = Vm::makeInt(1);
        setLocal(run, K("num_players"), n);
        return true;
    });
    vm.addNative("GameMode_GetNumPlayersShown", [modeProp, setLocal](Vm& v, ScriptRun& run, qb::Struct&) {
        qb::Value n = modeProp(v, K("num_players"));
        if (n.type == qb::Type::None) n = Vm::makeInt(1);
        setLocal(run, K("num_players_shown"), n);
        return true;
    });
    vm.addNative("GameMode_GetMinPlayers", [modeProp, setLocal](Vm& v, ScriptRun& run, qb::Struct&) {
        qb::Value n = modeProp(v, K("min_players"));
        if (n.type == qb::Type::None) n = modeProp(v, K("num_players"));
        setLocal(run, K("min_players"), n);
        return true;
    });
    // SearchMusicianProfileArray array_name = <global array> id = <name>:
    // the index of the profile of that name -> index; false if none [C
    // 0x820044EC's native, by its use in get_musician_profile_struct_by_id; I].
    vm.addNative("SearchMusicianProfileArray", [setLocal](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* an = Vm::param(p, K("array_name"));
        const qb::Value* id = Vm::param(p, K("id"));
        if (!an || !id) return false;
        const qb::Value* arr = v.global(an->key);
        if (!arr || !arr->arr) return false;
        for (size_t i = 0; i < arr->arr->values.size(); ++i) {
            const qb::Value& e = arr->arr->values[i];
            if (!e.st) continue;
            const qb::Value* n = v.globals().field(*e.st, K("name"));
            if (n && n->key == id->key) {
                setLocal(run, K("index"), Vm::makeInt(int32_t(i)));
                return true;
            }
        }
        return false;
    });
    // obj_getid: the element (object) the script runs on, as objid [C scripts].
    vm.addNative("obj_getid", [setLocal](Vm&, ScriptRun& run, qb::Struct&) {
        if (!run.object) return false;
        setLocal(run, K("objid"), Vm::makeKey(run.object));
        return true;
    });
}
