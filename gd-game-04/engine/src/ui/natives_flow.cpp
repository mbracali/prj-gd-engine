// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_flow.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <ctime>
#include <string>
#include <unordered_map>

#include "data/qbkey.h"
#include "game/save.h"
#include "script/globals.h"
#include "script/vm.h"

// The C functions the front-end flow calls (agents/scratch/flow_path.md
// section 15 lists them with their XEX addresses): the platform answers a
// PC port gives (a retail disc, a local profile signed in, a save device),
// script utilities, the event system and the pause state. Each is
// described with what the scripts need from it [C: the scripts' use;
// details I unless an address is given].
namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

qb::Struct* locals(qs::ScriptRun& run) {
    if (run.frames.empty()) return nullptr;
    if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
    return run.frames.back().locals.get();
}

const qb::Value* firstUnnamed(const qb::Struct& p) {
    for (const qb::Item& it : p.items)
        if (it.name == 0) return &it.value;
    return nullptr;
}

}  // namespace

void addFlowNatives(qs::Vm& vm, FlowState& st) {
    using qs::ScriptRun;
    using qs::Vm;
    auto yes = [](Vm&, ScriptRun&, qb::Struct&) { return true; };
    auto no = [](Vm&, ScriptRun&, qb::Struct&) { return false; };
    // A retail disc: NotCD false, CD true (0x822873C8 / 0x822873B8) [C addresses; values I].
    vm.addNative("NotCD", no);
    vm.addNative("CD", yes);
    // A local profile is signed in; Xbox Live is not (not in the port).
    vm.addNative("CheckForSignIn", [](Vm&, ScriptRun&, qb::Struct& p) { return Vm::flag(p, K("local")); });
    for (const char* n : {"GameProfileSettingsFinished", "StartGameProfileSettingsRead", "RefreshSigninStatus", "ReAcquireControllers",
                          "ResumeControllerChecking", "UnBindGuitarControllerStatus", "SetInput", "SetMenuAutoRepeatTimes",
                          "StartRendering_C", "StopRendering_C", "SetScreen", "RegisterAtoms", "DeRegisterAtoms", "UpdateAtoms",
                          "PrintScriptInfo", "UnPauseSpawnedScript", "OnExitRun", "SetSpawnInstanceLimits", "ClearEventHandlerGroup",
                          "ResetEventHandlersFromTable", "RequireParams", "EnableUserMusic", "MassiveInit", "English",
                          "ClearDataBuffer", "SetRandomSeeds", "SetSlomo", "LockDSP", "UnLockDSP", "KillCamAnim", "PlayIGCCam",
                          "SetViewportProperties", "ScreenFX_ClearFXInstances", "ScreenFX_UpdateFXInstanceParams",
                          "ScreenFX_AddFXInstance", "HideObjectByType", "SetProgressionMaxDifficulty", "Init2DParticles",
                          "AddToMaterialLibrary", "SetFontProperties", "SetFontNonProportionalNumbers", "EnableRemoveSoundEntry",
                          "SetScenePermanent", "ParseNodeArray", "startup", "CreateBussSystem", "SetSoundBussParams", "SoundBussLock",
                          "SoundBussUnLock", "createsoundbusseffects", "SetSoundBussEffects", "createnodeflag", "LightShow_Init",
                          "LightShow_SetProcessors", "CreatePakManMap", "UnloadPak", "WaitUnloadPak"})
        vm.addNative(n, yes);
    for (const char* n : {"NetSessionFunc", "IsHost", "EnumContentFiles", "IsEnumContentFilesDamaged", "GetLatestContentIndexFile",
                          "GetContentFolderIndexFromFile", "French", "German", "Spanish", "Italian", "IsCreated", "CompositeObjectExists",
                          "IsInNodeArray", "IsSoundPlaying", "IsMovieInBuffer", "IsMoviePreLoaded", "GetGameProfileSetting"})
        vm.addNative(n, no);
    vm.addNative("EnumContentFilesFinished", yes);
    // GetDisplaySettings: a 16:9 frame (widescreen = 1) [I].
    vm.addNative("GetDisplaySettings", [](Vm&, ScriptRun& run, qb::Struct&) {
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("widescreen"), Vm::makeInt(1));
        return true;
    });
    // The save device: always present (the save folder); the folder and
    // file checks follow what is saved (M7) [I].
    vm.addNative("CardIsInSlot", yes);
    vm.addNative("NewShowStorageSelector", yes);
    vm.addNative("StorageSelectorFinished", yes);
    vm.addNative("MC_WaitAsyncOpsFinished", yes);
    // SaveToMemoryCard / LoadFromMemoryCard: the global tags as save.json
    // in the save folder (game/save.h); result = 1 on success [I: result use from the memcard scripts].
    vm.addNative("SaveToMemoryCard", [&st](Vm& v, ScriptRun& run, qb::Struct&) {
        std::string err;
        const bool ok = save::write(v.globalTags, v.globalTagSections, err);
        if (!ok) SDL_Log("save: %s", err.c_str());
        else st.saveExists = true, ++st.saves;
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("result"), Vm::makeInt(ok ? 1 : 0));
        return ok;
    });
    vm.addNative("LoadFromMemoryCard", [&st](Vm& v, ScriptRun& run, qb::Struct&) {
        std::string err;
        save::Tags tags;
        save::Sections sections;
        const bool ok = save::read(tags, sections, err);
        if (ok) {
            for (auto& [k, t] : tags) v.globalTags[k] = std::move(t);
            for (auto& [k, t] : sections) v.globalTagSections[k] = std::move(t);
            ++st.loads;
        } else {
            SDL_Log("save: %s", err.c_str());
        }
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("result"), Vm::makeInt(ok ? 1 : 0));
        return ok;
    });
    for (const char* n : {"MC_EnumerateFolders", "MC_SetActiveFolder", "MC_LoadTOCInActiveFolder", "MC_CreateFolder", "MC_DeleteFolder"})
        vm.addNative(n, [](Vm&, ScriptRun& run, qb::Struct&) {
            if (qb::Struct* l = locals(run)) Vm::set(*l, K("result"), Vm::makeInt(1));
            return true;
        });
    vm.addNative("MC_FolderExists", [&st](Vm&, ScriptRun&, qb::Struct&) { return st.saveExists; });
    vm.addNative("MC_HasActiveFolder", [&st](Vm&, ScriptRun&, qb::Struct&) { return st.saveExists; });
    vm.addNative("MemCardFileExists", [&st](Vm&, ScriptRun&, qb::Struct&) { return st.saveExists; });
    // ResetTimer / TimeGreaterThan <seconds>: a script timer [I: the 3 s popup].
    vm.addNative("ResetTimer", [&st](Vm& v, ScriptRun&, qb::Struct&) {
        st.timerMs = v.clockMs();
        return true;
    });
    vm.addNative("TimeGreaterThan", [&st](Vm& v, ScriptRun&, qb::Struct& p) {
        return v.clockMs() - st.timerMs >= double(Vm::number(Vm::positional(p), 0)) * 1000.0;
    });
    // Pause state (0x820EABD8 / 0x820EAC20 / 0x820EAC50).
    vm.addNative("PauseGame", [&st](Vm&, ScriptRun&, qb::Struct&) {
        st.paused = true;
        return true;
    });
    vm.addNative("UnPauseGame", [&st](Vm&, ScriptRun&, qb::Struct&) {
        st.paused = false;
        return true;
    });
    vm.addNative("GameIsPaused", [&st](Vm&, ScriptRun&, qb::Struct&) { return st.paused; });
    // Script utilities.
    vm.addNative("ScriptExists", [](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* n = firstUnnamed(p);
        const qb::Value* g = n ? v.globals().find(n->key) : nullptr;
        return (g && g->type == qb::Type::Script) || (n && v.hasNative(n->key));
    });
    vm.addNative("GlobalExists", [](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* n = Vm::param(p, K("name"));
        if (!n) n = firstUnnamed(p);
        return n && v.globals().find(n->key) != nullptr;
    });
    // CastToInteger <local>: the local made an integer.
    vm.addNative("CastToInteger", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* n = firstUnnamed(p);
        qb::Struct* l = locals(run);
        if (!n || !l) return false;
        if (const qb::Value* v = Vm::param(*l, n->key)) Vm::set(*l, n->key, Vm::makeInt(int32_t(Vm::number(v, 0))));
        return true;
    });
    vm.addNative("MathMin", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const float a = Vm::number(Vm::param(p, K("a")), 0), b = Vm::number(Vm::param(p, K("b")), 0);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("min"), Vm::makeFloat(std::min(a, b)));
        return true;
    });
    vm.addNative("MathMax", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const float a = Vm::number(Vm::param(p, K("a")), 0), b = Vm::number(Vm::param(p, K("b")), 0);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("max"), Vm::makeFloat(std::max(a, b)));
        return true;
    });
    // StringRemoveTrailingWhitespace string = s -> new_string [C use: confirm_band_name].
    vm.addNative("StringRemoveTrailingWhitespace", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* s = Vm::param(p, K("string"));
        if (!s) s = firstUnnamed(p);
        if (!s) return false;
        qb::Value out = *s;
        while (!out.str.empty() && (out.str.back() == ' ' || out.str.back() == '\t')) out.str.pop_back();
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("new_string"), out);
        return true;
    });
    // GetUpperCaseString <text> -> uppercasestring [C use: intro_song_info, the band-name date].
    vm.addNative("GetUpperCaseString", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* s = firstUnnamed(p);
        if (!s) return false;
        qb::Value out = *s;
        if (out.type != qb::Type::String && out.type != qb::Type::WString) out.type = qb::Type::WString;
        for (char& c : out.str)
            if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("uppercasestring"), out);
        return true;
    });
    // GetLocalSystemTime -> localsystemtime {year month (0..11, us_month_names'
    // index) dayofmonth hour minute second} [I: the field names from the scripts].
    vm.addNative("GetLocalSystemTime", [](Vm&, ScriptRun& run, qb::Struct&) {
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        qb::Value t;
        t.type = qb::Type::Struct;
        t.st = std::make_shared<qb::Struct>();
        Vm::set(*t.st, K("year"), Vm::makeInt(tm.tm_year + 1900));
        Vm::set(*t.st, K("month"), Vm::makeInt(tm.tm_mon));
        Vm::set(*t.st, K("dayofmonth"), Vm::makeInt(tm.tm_mday));
        Vm::set(*t.st, K("hour"), Vm::makeInt(tm.tm_hour));
        Vm::set(*t.st, K("minute"), Vm::makeInt(tm.tm_min));
        Vm::set(*t.st, K("second"), Vm::makeInt(tm.tm_sec));
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("localsystemtime"), t);
        return true;
    });
    vm.addNative("StringLength", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* s = Vm::param(p, K("string"));
        if (!s) s = firstUnnamed(p);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("str_len"), Vm::makeInt(s ? int32_t(s->str.size()) : 0));
        return true;
    });
    // AddArrayElement array = a element = e -> array (the new array).
    vm.addNative("AddArrayElement", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* a = Vm::param(p, K("array"));
        const qb::Value* e = Vm::param(p, K("element"));
        if (!e) return false;
        qb::Value out;
        out.type = qb::Type::Array;
        out.arr = a && a->arr ? std::make_shared<qb::Array>(*a->arr) : std::make_shared<qb::Array>();
        if (out.arr->type == qb::Type::None) out.arr->type = e->type;
        out.arr->values.push_back(*e);
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("array"), out);
        return true;
    });
    // Goto <script> params: runs it in place of the rest of the caller [I].
    vm.addNative("Goto", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* s = firstUnnamed(p);
        if (!s || run.frames.empty()) return false;
        qb::Struct params;
        if (const qb::Value* ps = Vm::param(p, K("params")); ps && ps->st) params = *ps->st;
        auto c = v.code(s->key);
        if (!c && run.frames.back().locals) {
            // A local naming the script (memcard_sequence_quit's `goto MemcardDoneScript`) [C data].
            if (const qb::Value* lv = Vm::param(*run.frames.back().locals, s->key))
                if (lv->type == qb::Type::Key || lv->type == qb::Type::KeyPtr) c = v.code(lv->key);
        }
        if (!c)
            if (const qb::Value* gv = v.globals().find(s->key); gv && gv->type == qb::Type::Key) c = v.code(gv->key);
        if (!c) {
            v.logOnce("Goto: no script " + qbkey::name(s->key));
            return false;
        }
        qs::Frame f;
        f.code = c;
        f.locals = std::make_shared<qb::Struct>(params);
        run.frames.back() = std::move(f);
        return true;
    });
    // The event system: SetEventHandler event = e scr = s [params]; BroadcastEvent type = e data = {..}
    // runs every handler (0x82266088, 0x82158160) [I: handlers per script object, here global].
    vm.addNative("SetEventHandler", [&st](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* e = Vm::param(p, K("event"));
        const qb::Value* s = Vm::param(p, K("scr"));
        if (!e || !s) return false;
        FlowState::Handler h{e->key, s->key, {}};
        if (const qb::Value* ps = Vm::param(p, K("params")); ps && ps->st) h.params = *ps->st;
        st.handlers.push_back(std::move(h));
        return true;
    });
    vm.addNative("BroadcastEvent", [&st](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* t = Vm::param(p, K("type"));
        if (!t) return false;
        qb::Struct data;
        if (const qb::Value* d = Vm::param(p, K("data")); d && d->st) data = *d->st;
        ++st.broadcasts;
        st.lastBroadcast = t->key;
        if (st.onBroadcast) st.onBroadcast(t->key);
        for (const FlowState::Handler& h : st.handlers) {
            if (h.event != t->key) continue;
            qb::Struct params = h.params;
            for (const qb::Item& it : data.items) Vm::set(params, it.name, it.value);
            v.spawn(h.script, params);
        }
        return true;
    });
}
