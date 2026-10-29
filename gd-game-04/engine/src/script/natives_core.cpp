// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "script/natives_core.h"

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <string>

#include "data/qbkey.h"

// The engine-independent C functions every script uses: waits, globals,
// spawning, structures and arrays, text formatting. Each lists the
// cfuncs.tsv address it reimplements; the behaviour is read from the
// scripts' use [I] until each is confirmed in its code (docs/qscript.md).
namespace qs {

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

// printf-style text with %s %d %f %i and %% from the params (FormatText,
// printf): `%s` takes the next named param s, d, ... in order of the letters
// QScript uses: s=, d=, f=, a=, b= ... [I].
std::string format(const std::string& fmt, const qb::Struct& p) {
    std::string out;
    for (size_t i = 0; i < fmt.size(); ++i) {
        if (fmt[i] != '%' || i + 1 >= fmt.size()) {
            out += fmt[i];
            continue;
        }
        const char c = fmt[i + 1];
        if (c == '%') {
            out += '%';
            ++i;
            continue;
        }
        // %<letter>: the parameter named by that letter (%s -> s, %d -> d).
        const qb::Value* v = Vm::param(p, qbkey::key(std::string(1, c)));
        ++i;
        if (!v) continue;
        char buf[64];
        switch (v->type) {
        case qb::Type::Int: std::snprintf(buf, sizeof buf, "%d", v->i), out += buf; break;
        case qb::Type::Float: std::snprintf(buf, sizeof buf, "%g", double(v->f[0])), out += buf; break;
        case qb::Type::String:
        case qb::Type::WString: out += v->str; break;
        case qb::Type::Key:
        case qb::Type::KeyPtr: out += qbkey::name(v->key); break;
        default: break;
        }
    }
    return out;
}

const qb::Value* first(const qb::Struct& p) {
    for (const qb::Item& it : p.items)
        if (it.name == 0) return &it.value;
    return nullptr;
}

}  // namespace

void addCoreNatives(Vm& vm) {
    // Wait <n> [seconds|frames|gameframes]: suspends the script [I: units
    // default to frames when no unit flag].
    vm.addNative("Wait", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* n = Vm::positional(p);
        float amount = Vm::number(n, 1);
        if (Vm::flag(p, K("seconds")) || Vm::flag(p, K("second"))) run.waitUntilMs = v.clockMs() + double(amount) * 1000.0;
        else run.waitFrames = std::max(1, int(amount + 0.5f));
        return true;
    });
    // change <global> = value [C: the scripts' `change name = value`].
    vm.addNative("change", [](Vm& v, ScriptRun&, qb::Struct& p) {
        for (const qb::Item& it : p.items)
            if (it.name) v.setGlobal(it.name, it.value);
        return true;
    });
    vm.addNative("printf", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    vm.addNative("printstruct", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    vm.addNative("ScriptAssert", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* s = first(p);
        v.logOnce("ScriptAssert in " + qbkey::name(run.name) + ": " + (s ? format(s->str, p) : std::string()));
        return true;
    });
    // GetPlatform: platform = Xenon (the version this port follows).
    vm.addNative("GetPlatform", [](Vm&, ScriptRun& run, qb::Struct&) {
        if (!run.frames.empty() && run.frames.back().locals)
            Vm::set(*run.frames.back().locals, K("platform"), Vm::makeKey(K("Xenon")));
        return true;
    });
    vm.addNative("IsXenon", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    vm.addNative("IsPS3", [](Vm&, ScriptRun&, qb::Struct&) { return false; });
    vm.addNative("IsPS2", [](Vm&, ScriptRun&, qb::Struct&) { return false; });
    vm.addNative("IsFmodEnabled", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    // SpawnScriptNow / SpawnScriptLater <script> params = {...} id = x.
    auto spawn = [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* s = first(p);
        if (!s || s->type != qb::Type::Key) return false;
        qb::Struct params;
        if (const qb::Value* ps = Vm::param(p, K("params")); ps && ps->st) params = *ps->st;
        const uint32_t id = v.spawn(s->key, params, run.id);
        if (const qb::Value* sid = Vm::param(p, K("id")))
            for (auto& r : v.runs())
                if (r->id == id) r->type = sid->key;
        return id != 0;
    };
    vm.addNative("SpawnScriptNow", spawn);
    vm.addNative("SpawnScriptLater", spawn);
    vm.addNative("SpawnScript", spawn);
    vm.addNative("KillSpawnedScript", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        if (const qb::Value* n = Vm::param(p, K("name"))) v.killByName(n->key);
        if (const qb::Value* id = Vm::param(p, K("id")))
            for (auto& r : v.runs())
                if (r->type == id->key && r.get() != &run) r->finished = true;
        return true;
    });
    vm.addNative("ScriptIsRunning", [](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* s = first(p);
        return s && v.running(s->key);
    });
    // GotParam <name>: the calling script has a parameter of that name.
    vm.addNative("GotParam", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* n = first(p);
        if (!n || run.frames.empty() || !run.frames.back().locals) return false;
        for (const qb::Item& it : run.frames.back().locals->items)
            if (it.name == n->key || (it.name == 0 && it.value.type == qb::Type::Key && it.value.key == n->key)) return true;
        return false;
    });
    // StructureContains structure = s name = n.
    vm.addNative("StructureContains", [](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* s = Vm::param(p, K("structure"));
        const qb::Value* n = Vm::param(p, K("name"));
        if (!n) n = first(p);
        // A key names the global struct [C data: start_flow_manager passes the flow state's key].
        if (s && (s->type == qb::Type::Key || s->type == qb::Type::KeyPtr)) s = v.global(s->key);
        if (!s || !n || s->type != qb::Type::Struct || !s->st) return false;
        return v.globals().field(*s->st, n->key) != nullptr;
    });
    // GetArraySize <array> -> array_size.
    vm.addNative("GetArraySize", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* a = first(p);
        const int n = a && a->type == qb::Type::Array && a->arr ? int(a->arr->values.size()) : 0;
        if (!run.frames.empty() && run.frames.back().locals) Vm::set(*run.frames.back().locals, K("array_size"), Vm::makeInt(n));
        return true;
    });
    // FormatText checksumName = x / TextName = x 'fmt' params.
    vm.addNative("FormatText", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* f = nullptr;
        for (const qb::Item& it : p.items)
            if (it.name == 0 && (it.value.type == qb::Type::String || it.value.type == qb::Type::WString)) f = &it.value;
        if (!f || run.frames.empty()) return false;
        const std::string text = format(f->str, p);
        if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
        if (const qb::Value* c = Vm::param(p, K("checksumName"))) {
            qbkey::names().add(text);
            Vm::set(*run.frames.back().locals, c->key, Vm::makeKey(qbkey::key(text)));
        }
        if (const qb::Value* t = Vm::param(p, K("TextName"))) {
            qb::Value s = Vm::makeString(text);
            s.type = qb::Type::WString;
            Vm::set(*run.frames.back().locals, t->key, s);
        }
        return true;
    });
    // GetTrueStartTime -> starttime (ms since boot) [I].
    vm.addNative("GetTrueStartTime", [](Vm& v, ScriptRun& run, qb::Struct&) {
        if (!run.frames.empty() && run.frames.back().locals)
            Vm::set(*run.frames.back().locals, K("starttime"), Vm::makeInt(int32_t(v.clockMs())));
        return true;
    });
    vm.addNative("GetStartTime", [](Vm& v, ScriptRun& run, qb::Struct&) {
        if (!run.frames.empty() && run.frames.back().locals)
            Vm::set(*run.frames.back().locals, K("starttime"), Vm::makeInt(int32_t(v.clockMs())));
        return true;
    });
    vm.addNative("GetTrueElapsedTime", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const float start = Vm::number(Vm::param(p, K("starttime")), 0);
        if (!run.frames.empty() && run.frames.back().locals)
            Vm::set(*run.frames.back().locals, K("elapsedtime"), Vm::makeInt(int32_t(v.clockMs() - start)));
        return true;
    });
    vm.addNative("GetElapsedTime", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const float start = Vm::number(Vm::param(p, K("starttime")), 0);
        if (!run.frames.empty() && run.frames.back().locals)
            Vm::set(*run.frames.back().locals, K("elapsedtime"), Vm::makeInt(int32_t(v.clockMs() - start)));
        return true;
    });
    // Random numbers: GetRandomValue name = n a = lo b = hi [integer].
    vm.addNative("GetRandomValue", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* n = Vm::param(p, K("name"));
        const float a = Vm::number(Vm::param(p, K("a")), 0), b = Vm::number(Vm::param(p, K("b")), 1);
        if (!n || run.frames.empty() || !run.frames.back().locals) return false;
        const float r = a + (b - a) * float(v.rng()() % 100000) / 100000.0f;
        Vm::set(*run.frames.back().locals, n->key, Vm::flag(p, K("integer")) ? Vm::makeInt(int32_t(r + 0.5f)) : Vm::makeFloat(r));
        return true;
    });
    // SetArrayElement arrayname = a [globalarray] index = i newvalue = v: one
    // element of a global (or the caller's local) array, copied on write [I].
    vm.addNative("SetArrayElement", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* name = Vm::param(p, K("arrayname"));
        const qb::Value* idx = Vm::param(p, K("index"));
        const qb::Value* nv = Vm::param(p, K("newvalue"));
        if (!name || !idx || !nv) return false;
        const int i = int(Vm::number(idx, -1));
        qb::Value arr;
        if (Vm::flag(p, K("globalarray"))) {
            if (const qb::Value* g = v.global(name->key)) arr = *g;
        } else if (!run.frames.empty() && run.frames.back().locals) {
            if (const qb::Value* l = Vm::param(*run.frames.back().locals, name->key)) arr = *l;
        }
        if (arr.type != qb::Type::Array || !arr.arr || i < 0 || size_t(i) >= arr.arr->values.size()) return false;
        arr.arr = std::make_shared<qb::Array>(*arr.arr);
        arr.arr->values[size_t(i)] = *nv;
        if (Vm::flag(p, K("globalarray"))) v.setGlobal(name->key, arr);
        else Vm::set(*run.frames.back().locals, name->key, arr);
        return true;
    });
    // GetGlobalTags <tag>: the tag's fields into the caller's parameters;
    // false when the tag was never set. SetGlobalTags <tag> params = {...}.
    vm.addNative("GetGlobalTags", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const qb::Value* tag = first(p);
        if (!tag) return false;
        auto it = v.globalTags.find(tag->key);
        if (it == v.globalTags.end()) return false;
        if (!run.frames.empty()) {
            if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
            const qb::Value* only = Vm::param(p, K("param"));
            for (const qb::Item& item : it->second.items)
                if (!only || item.name == only->key) Vm::set(*run.frames.back().locals, item.name, item.value);
        }
        return true;
    });
    vm.addNative("SetGlobalTags", [](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* tag = first(p);
        if (!tag) return false;
        qb::Struct& t = v.globalTags[tag->key];
        if (const qb::Value* ps = Vm::param(p, K("params")); ps && ps->st)
            for (const qb::Item& item : v.globals().expand(*ps->st)) Vm::set(t, item.name, item.value);
        return true;
    });
    // PushGlobalTags <tag> section = S: the tag's current fields kept in
    // section S; PopGlobalTags section = S: every tag kept there restored
    // (push_bandtags / pop_bandtags swap a band's career in and out)
    // [I: from the scripts; natives 0x821239D0 / 0x82123A98].
    vm.addNative("PushGlobalTags", [](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* tag = first(p);
        const qb::Value* sec = Vm::param(p, K("section"));
        if (!tag || !sec) return false;
        auto it = v.globalTags.find(tag->key);
        v.globalTagSections[sec->key][tag->key] = it == v.globalTags.end() ? qb::Struct{} : it->second;
        return true;
    });
    vm.addNative("PopGlobalTags", [](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* sec = Vm::param(p, K("section"));
        if (!sec) return false;
        auto it = v.globalTagSections.find(sec->key);
        if (it == v.globalTagSections.end()) return false;
        for (const auto& [k, st] : it->second) v.globalTags[k] = st;
        return true;
    });
    // AddParams <struct>: merged into the caller's parameters [I].
    vm.addNative("AddParams", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        if (run.frames.empty()) return false;
        if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
        for (const qb::Item& it : p.items) {
            if (it.name == 0 && it.value.type == qb::Type::Struct && it.value.st)
                for (const qb::Item& s2 : v.globals().expand(*it.value.st)) Vm::set(*run.frames.back().locals, s2.name, s2.value);
            else if (it.name)
                Vm::set(*run.frames.back().locals, it.name, it.value);
        }
        return true;
    });
    // ExtendCRC <key> <text|int> out = name: the key's CRC continued with the
    // text (QB keys are the running CRC, so this is the key of name + text) [I].
    vm.addNative("ExtendCRC", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* base = nullptr;
        std::string suffix;
        for (const qb::Item& it : p.items) {
            if (it.name) continue;
            if (!base && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr)) base = &it.value;
            else if (it.value.type == qb::Type::String || it.value.type == qb::Type::WString) suffix = it.value.str;
            else if (it.value.type == qb::Type::Int) suffix = std::to_string(it.value.i);
        }
        const qb::Value* out = Vm::param(p, K("out"));
        if (!base || !out || run.frames.empty()) return false;
        uint32_t c = base->key;
        for (char ch : suffix) c = qbkey::detail::kTable[(c ^ uint8_t(qbkey::detail::lower(ch))) & 0xFF] ^ (c >> 8);
        if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
        Vm::set(*run.frames.back().locals, out->key, Vm::makeKey(c));
        return true;
    });
    vm.addNative("AppendSuffixToChecksum", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const qb::Value* base = Vm::param(p, K("base"));
        const qb::Value* suf = Vm::param(p, K("suffixstring"));
        if (!base || !suf || run.frames.empty()) return false;
        uint32_t c = base->key;
        for (char ch : suf->str) c = qbkey::detail::kTable[(c ^ uint8_t(qbkey::detail::lower(ch))) & 0xFF] ^ (c >> 8);
        if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
        Vm::set(*run.frames.back().locals, K("appended_id"), Vm::makeKey(c));
        return true;
    });
    // Block: the script waits for an event that nothing sends yet [I].
    vm.addNative("Block", [](Vm&, ScriptRun& run, qb::Struct&) {
        run.waitFor = [] { return false; };
        return true;
    });
    // Memory and profiling calls of the console build: nothing to do on PC.
    for (const char* n : {"MemInitHeap", "PushMemProfile", "PopMemProfile", "StopRendering", "StartRendering",
                          "MemPushContext", "MemPopContext", "AddToStringLookup", "DisplayLoadingScreen", "hideloadingscreen",
                          "SetButtonEventMappings", "ToggleMetrics", "SetScreenMode", "InitCOIM", "ProcessorMgr_Init", "PassMgr_Init",
                          "RegisterProcessorGroupDesc", "RegisterPassGroupDesc", "RegisterCompositeObjectDesc", "InitAnimCompressTable",
                          "InitAnimSystem", "InitLightManager", "engineconfig", "LaunchViewer", "AllocateDataBuffer", "InitAtoms",
                          "ScreenElementSystemInit", "SetShadowRenderingFlags", "SetShadowMapParams", "SetShadowProjectionTexture",
                          "SetDynamicShadowIntensity", "SetModelLightsFadeTime", "MemCardSystemInitialize", "AddEditableList"})
        vm.addNative(n, [](Vm&, ScriptRun&, qb::Struct&) { return true; });
}

}  // namespace qs
