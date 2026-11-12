// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Small natives the scripts call (docs/audit.md): maths, checks, the
// single-player Xbox 360 answers to the network / split-screen questions,
// and the asset-context bookkeeping that has nothing to load in the port.
// Return names are the ones the scripts read [C use: tools/qbusage.py];
// behaviour [I] unless marked.
#include "script/natives_misc.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "data/qbkey.h"
#include "script/atoms.h"
#include "script/vm.h"

namespace {

using qs::ScriptRun;
using qs::Vm;
uint32_t K(const char* s) { return qbkey::key(s); }

qb::Struct* locals(ScriptRun& run) {
    if (run.frames.empty()) return nullptr;
    if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
    return run.frames.back().locals.get();
}

const qb::Value* firstUnnamed(const qb::Struct& p) {
    for (const qb::Item& it : p.items)
        if (it.name == 0) return &it.value;
    return nullptr;
}

bool truthy(const qb::Value* v) {
    if (!v) return false;
    switch (v->type) {
    case qb::Type::Int: return v->i != 0;
    case qb::Type::Float: return v->f[0] != 0;
    case qb::Type::Key:
    case qb::Type::KeyPtr: return v->key != 0 && v->key != K("false");
    case qb::Type::None: return false;
    default: return true;
    }
}

}  // namespace

void addMiscNatives(qs::Vm& vm) {
    // A pad's confirm is always A here, never the Japanese circle [C 0x820EBE00].
    vm.addNative("GetEnterButtonAssignment", [](Vm&, ScriptRun& run, qb::Struct&) {
        if (qb::Struct* l = locals(run)) Vm::set(*l, qbkey::key("assignment"), Vm::makeKey(qbkey::key("none")));
        return true;
    });
    // The career's difficulty for its progression atoms [C 0x82270C90, 0x82270CD0].
    auto difficulty = std::make_shared<int>(0);
    vm.addNative("SetProgressionDifficulty", [difficulty](Vm&, ScriptRun&, qb::Struct& p) {
        *difficulty = int(Vm::number(Vm::param(p, qbkey::key("difficulty")), 0));
        return true;
    });
    vm.addNative("GetProgressionDifficulty", [difficulty](Vm&, ScriptRun& run, qb::Struct&) {
        if (qb::Struct* l = locals(run)) Vm::set(*l, qbkey::key("difficulty"), Vm::makeInt(*difficulty));
        return true;
    });
    // The career's atoms (GD10-116): they read the same difficulty.
    addAtomNatives(vm, difficulty);
    // mod a = x b = y -> mod [C use: TileSprite].
    vm.addNative("mod", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const int a = int(Vm::number(Vm::param(p, K("a")), 0)), b = int(Vm::number(Vm::param(p, K("b")), 1));
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("mod"), Vm::makeInt(b ? a % b : 0));
        return true;
    });
    // Atan2 x = .. y = .. -> atan (degrees [I]).
    vm.addNative("Atan2", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const double x = Vm::number(Vm::param(p, K("x")), 0), y = Vm::number(Vm::param(p, K("y")), 0);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("atan"), Vm::makeFloat(float(std::atan2(y, x) * 180.0 / M_PI)));
        return true;
    });
    // GetDistance posa = .. posb = .. -> dist_atob.
    vm.addNative("GetDistance", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* a = Vm::param(p, K("posa"));
        const qb::Value* b = Vm::param(p, K("posb"));
        double d = 0;
        if (a && b)
            for (int k = 0; k < 3; ++k) d += double(a->f[k] - b->f[k]) * double(a->f[k] - b->f[k]);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("dist_atob"), Vm::makeFloat(float(std::sqrt(d))));
        return true;
    });
    // NormalizeVector <v> -> norm, length.
    vm.addNative("NormalizeVector", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* v = firstUnnamed(p);
        if (!v) return false;
        qb::Value n = *v;
        const double len = std::sqrt(double(v->f[0]) * v->f[0] + double(v->f[1]) * v->f[1] + double(v->f[2]) * v->f[2]);
        if (len > 0)
            for (int k = 0; k < 3; ++k) n.f[k] = float(v->f[k] / len);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("norm"), n), Vm::set(*l, K("length"), Vm::makeFloat(float(len)));
        return true;
    });
    // IsTrue { <x> }: the value is set and not 0 / false.
    vm.addNative("IsTrue", [](Vm&, ScriptRun&, qb::Struct& p) { return truthy(firstUnnamed(p)); });
    // ChecksumEquals a = .. b = .. [C use: handle_start_pressed].
    vm.addNative("ChecksumEquals", [](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* a = Vm::param(p, K("a"));
        const qb::Value* b = Vm::param(p, K("b"));
        return a && b && a->key == b->key;
    });
    // MangleChecksums a = .. b = .. -> mangled_id: one key from two [I: the
    // CRC of the second continued with the first's bytes].
    vm.addNative("MangleChecksums", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* a = Vm::param(p, K("a"));
        const qb::Value* b = Vm::param(p, K("b"));
        const uint32_t m = (a ? a->key : 0) ^ ((b ? b->key : 0) * 0x9E3779B1u + 0x7F4A7C15u);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("mangled_id"), Vm::makeKey(m));
        return true;
    });
    // GetDeltaTime -> delta_time (the frame's seconds; the VM runs at the frame rate).
    vm.addNative("GetDeltaTime", [](Vm& v, ScriptRun& run, qb::Struct&) {
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("delta_time"), Vm::makeFloat(float(v.frameMs() / 1000.0)));
        return true;
    });
    // The single-player Xbox 360 answers [C data: the port plays offline, NTSC].
    for (const char* n : {"IsPal", "InNetGame", "InSplitScreenGame", "IsObserving", "GMan_HasActiveGoals", "gotextramemory",
                          "ViewportExists", "ScreenFX_FXInstanceExists", "DoesWayPointExist"})
        vm.addNative(n, [](Vm&, ScriptRun&, qb::Struct&) { return false; });
    // GameModeEquals { $is_classic }: the skate engine's game types; none apply [I].
    vm.addNative("GameModeEquals", [](Vm&, ScriptRun&, qb::Struct&) { return false; });
    // DeBounce { $x time = t }: ignores the button for t [I: the dialogs' double presses]; the menus already debounce.
    // Bookkeeping with nothing to do in the port (assets come from the paks as they load).
    for (const char* n : {"DeBounce", "SetSearchAllAssetContexts", "PushAssetContext", "PopAssetContext", "TextOutput", "TextOutputStart",
                          "TextOutputEnd", "EnableInput", "UpdateGuitarVolume", "EnableDuplicateSymbolWarning",
                          "SetNetworkPreference", "WriteAchievements", "LoadSound", "ClearEventHandler"})
        vm.addNative(n, [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    // RemoveParameter {$name}: the calling script's parameter goes (named, or
    // an unnamed flag), so GotParam no longer finds it [C 0x823946C0: the
    // unnamed key, then a remove from the script's params (0x822B7140); I
    // from its callers]. A no-op until GD10-051: user_control_helper_get_buttonchar
    // kept all_buttons on the Xbox 360 and the help bar drew A + the green fret.
    vm.addNative("RemoveParameter", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* n = firstUnnamed(p);
        if (!n || run.frames.empty() || !run.frames.back().locals) return true;
        if (n->type != qb::Type::Key && n->type != qb::Type::KeyPtr) return true;
        auto& items = run.frames.back().locals->items;
        items.erase(std::remove_if(items.begin(), items.end(),
                                   [&](const qb::Item& it) {
                                       return it.name == n->key ||
                                              (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && it.value.key == n->key);
                                   }),
                    items.end());
        return true;
    });
    // AddParam name = n value = v [structure_name = s]: n = v joins the
    // calling script's parameters, or its struct s (made if missing), so
    // `se_setprops <s>` sets every collected prop at once [C 0x823980E8
    // reads name (0xA1DC81F9); I the rest from its callers:
    // ui_setlist_scribbles cycles the difficulty pop-up's highlight
    // scribbles this way; a stub until GD10-052 left them all at alpha 0].
    vm.addNative("AddParam", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* n = Vm::param(p, K("name"));
        const qb::Value* v = Vm::param(p, K("value"));
        if (!n || !v || run.frames.empty()) return true;
        if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
        qb::Struct& l = *run.frames.back().locals;
        const qb::Value* sn = Vm::param(p, K("structure_name"));
        if (!sn) {
            Vm::set(l, n->key, *v);
            return true;
        }
        qb::Value st;
        for (const qb::Item& it : l.items)
            if (it.name == sn->key && it.value.type == qb::Type::Struct && it.value.st) st = it.value;
        auto copy = std::make_shared<qb::Struct>(st.st ? *st.st : qb::Struct{});
        Vm::set(*copy, n->key, *v);
        st.type = qb::Type::Struct, st.st = copy;
        Vm::set(l, sn->key, st);
        return true;
    });
    // ForEachIn <array> do = script params = {...}: the script once per element,
    // with the element's fields and the params [C use: create_snazzy_dialog_box].
    vm.addNative("ForEachIn", [](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* a = firstUnnamed(p);
        const qb::Value* fn = Vm::param(p, K("do"));
        if (!a || !fn || a->type != qb::Type::Array || !a->arr) return false;
        const qb::Value* extra = Vm::param(p, K("params"));
        for (const qb::Value& e : a->arr->values) {
            qb::Struct args;
            if (e.st)
                for (const qb::Item& it : v.globals().expand(*e.st)) Vm::set(args, it.name, it.value);
            if (extra && extra->st)
                for (const qb::Item& it : v.globals().expand(*extra->st)) Vm::set(args, it.name, it.value);
            v.run(fn->key, args);
        }
        return true;
    });
}
