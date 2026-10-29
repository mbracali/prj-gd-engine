// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_sound.h"

#include <memory>
#include <random>
#include <unordered_map>

#include "audio/sfx.h"
#include "data/qbkey.h"
#include "script/globals.h"
#include "script/vm.h"

// Sound events: SoundEventFast event = E plays <E>_container's command
// (playsound) on one of its `sounds` (Sound1, Sound2 ... each {$sample
// vol = percent}) [C data: global_sound_events.qb; choice rules I: a uniform
// pick, randomness types not modelled yet].
namespace {
uint32_t K(const char* s) { return qbkey::key(s); }

const qb::Value* firstUnnamed(const qb::Struct& p) {
    for (const qb::Item& it : p.items)
        if (it.name == 0) return &it.value;
    return nullptr;
}
}  // namespace

void addSoundNatives(qs::Vm& vm, Sfx& sfx) {
    using qs::ScriptRun;
    using qs::Vm;
    // event -> its last voice (IsSoundEventPlaying / StopSoundEvent).
    auto voices = std::make_shared<std::unordered_map<uint32_t, uint32_t>>();
    vm.addNative("SoundEventFast", [&sfx, voices](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* ev = Vm::param(p, K("event"));
        if (!ev) ev = firstUnnamed(p);
        if (!ev || (ev->type != qb::Type::Key && ev->type != qb::Type::KeyPtr)) return false;
        const qb::Value* c = v.global(qbkey::key(qbkey::name(ev->key) + "_container"));
        if (!c || !c->st) {
            v.logOnce("SoundEvent: no container for " + qbkey::name(ev->key));
            return false;
        }
        const qb::Value* sounds = c->st->find(K("sounds"));
        if (!sounds || !sounds->st || sounds->st->items.empty()) return false;
        const size_t n = sounds->st->items.size();
        const qb::Item& pick = sounds->st->items[size_t(v.rng()() % n)];
        if (!pick.value.st) return false;
        uint32_t sample = 0;
        float vol = 100, pitch = 100;
        for (const qb::Item& it : pick.value.st->items) {
            if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr)) sample = it.value.key;
            else if (it.name == K("vol")) vol = it.value.number();
            else if (it.name == K("pitch")) pitch = it.value.number();
        }
        if (!sample) return false;
        const uint32_t voice = sfx.play(sample, vol / 100.0f, pitch / 100.0f);
        if (!voice) v.logOnce("SoundEvent: no sample " + qbkey::name(sample));
        else (*voices)[ev->key] = voice;
        return true;
    });
    vm.addNative("IsSoundEventPlaying", [&sfx, voices](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* ev = firstUnnamed(p);
        if (!ev) return false;
        auto it = voices->find(ev->key);
        return it != voices->end() && sfx.playing(it->second);
    });
    vm.addNative("StopSoundEvent", [&sfx, voices](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* ev = firstUnnamed(p);
        if (!ev) return false;
        auto it = voices->find(ev->key);
        if (it != voices->end()) sfx.stop(it->second);
        return true;
    });
    vm.addNative("PlaySound", [&sfx](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* s = firstUnnamed(p);
        if (!s) return false;
        const float vol = Vm::number(Vm::param(p, K("vol")), 100), pitch = Vm::number(Vm::param(p, K("pitch")), 100);
        if (!sfx.play(s->key, vol / 100.0f, pitch / 100.0f)) v.logOnce("PlaySound: no sample " + qbkey::name(s->key));
        return true;
    });
    for (const char* n : {"StopSound", "StopSoundsByBuss", "PauseSoundsByBuss", "UnPauseSoundsByBuss",
                          "SetSoundParams", "SetSoundBussParams"})
        vm.addNative(n, [](Vm&, ScriptRun&, qb::Struct&) { return true; });
}
