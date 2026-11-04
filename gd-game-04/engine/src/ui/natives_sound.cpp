// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_sound.h"
#include <SDL3/SDL_timer.h>
#include <vector>
#include <algorithm>

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
std::function<void(uint32_t)>& listener() {
    static std::function<void(uint32_t)> f;
    return f;
}

const qb::Value* firstUnnamed(const qb::Struct& p) {
    for (const qb::Item& it : p.items)
        if (it.name == 0) return &it.value;
    return nullptr;
}
}  // namespace

void setSoundEventListener(std::function<void(uint32_t)> f) { listener() = std::move(f); }

void addSoundNatives(qs::Vm& vm, Sfx& sfx) {
    using qs::ScriptRun;
    using qs::Vm;
    // Per sound event: its voices, when it last started, its last picks
    // (IsSoundEventPlaying / StopSoundEvent, the struct's limits).
    struct EventState {
        std::vector<uint32_t> voices;
        uint64_t lastNs = 0;
        int last[2] = {-1, -1};
    };
    auto events = std::make_shared<std::unordered_map<uint32_t, EventState>>();
    // SoundEventFast event = E: <E>_container's sounds (randomness: one at
    // random, randomnorepeatlasttwotype: not one of the last two picks) under
    // <E>_struct's rules -- norepeatfor (seconds since the last start: none),
    // instancelimit with instancemanagement (ignore / ignore_spawn_request:
    // the new one is dropped; stop_oldest: the oldest stops), default_buss
    // (the buss calls) [C data <E>_struct; I: the engine's FMOD-side rules].
    vm.addNative("SoundEventFast", [&sfx, events](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* ev = Vm::param(p, K("event"));
        if (!ev) ev = firstUnnamed(p);
        if (!ev || (ev->type != qb::Type::Key && ev->type != qb::Type::KeyPtr)) return false;
        if (listener()) listener()(ev->key);
        const std::string base = qbkey::name(ev->key);
        const qb::Value* c = v.global(qbkey::key(base + "_container"));
        if (!c || !c->st) {
            v.logOnce("SoundEvent: no container for " + base);
            return false;
        }
        const qb::Value* sounds = c->st->find(K("sounds"));
        if (!sounds || !sounds->st || sounds->st->items.empty()) return false;
        EventState& es = (*events)[ev->key];
        es.voices.erase(std::remove_if(es.voices.begin(), es.voices.end(), [&](uint32_t id) { return !sfx.playing(id); }), es.voices.end());
        uint32_t buss = 0;
        if (const qb::Value* st = v.global(qbkey::key(base + "_struct")); st && st->st) {
            const uint64_t now = SDL_GetTicksNS();
            const float norepeat = Vm::number(st->st->find(K("norepeatfor")), 0);
            if (norepeat > 0 && es.lastNs && double(now - es.lastNs) / 1e9 < double(norepeat)) return true;
            const int limit = int(Vm::number(st->st->find(K("instancelimit")), 0));
            if (limit > 0 && int(es.voices.size()) >= limit) {
                const qb::Value* m = st->st->find(K("instancemanagement"));
                if (m && m->key == K("stop_oldest")) {
                    sfx.stop(es.voices.front());
                    es.voices.erase(es.voices.begin());
                } else {
                    return true;  // ignore: the playing one stays
                }
            }
            if (const qb::Value* b = st->st->find(K("default_buss"))) buss = b->key;
        }
        const size_t n = sounds->st->items.size();
        size_t idx = size_t(v.rng()() % n);
        const qb::Value* rnd = c->st->find(K("randomness"));
        if (rnd && rnd->key == K("randomnorepeatlasttwotype") && n > 2)
            for (int tries = 0; tries < 8 && (int(idx) == es.last[0] || int(idx) == es.last[1]); ++tries) idx = size_t(v.rng()() % n);
        es.last[1] = es.last[0], es.last[0] = int(idx);
        const qb::Item& pick = sounds->st->items[idx];
        if (!pick.value.st) return false;
        uint32_t sample = 0;
        float vol = 100, pitch = 100;
        for (const qb::Item& it : pick.value.st->items) {
            if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr)) sample = it.value.key;
            else if (it.name == K("vol")) vol = it.value.number();
            else if (it.name == K("pitch")) pitch = it.value.number();
        }
        if (!sample) return false;
        const uint32_t voice = sfx.play(sample, vol / 100.0f, pitch / 100.0f, buss);
        if (!voice) {
            v.logOnce("SoundEvent: no sample " + qbkey::name(sample));
        } else {
            es.voices.push_back(voice);
            es.lastNs = SDL_GetTicksNS();
        }
        return true;
    });
    vm.addNative("IsSoundEventPlaying", [&sfx, events](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* ev = firstUnnamed(p);
        if (!ev) return false;
        auto it = events->find(ev->key);
        if (it == events->end()) return false;
        for (uint32_t id : it->second.voices)
            if (sfx.playing(id)) return true;
        return false;
    });
    vm.addNative("StopSoundEvent", [&sfx, events](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* ev = firstUnnamed(p);
        if (!ev) return false;
        auto it = events->find(ev->key);
        if (it != events->end()) {
            for (uint32_t id : it->second.voices) sfx.stop(id);
            it->second.voices.clear();
        }
        return true;
    });
    // PlaySound <sample> vol pitch buss (the song previews, the intro crowd).
    vm.addNative("PlaySound", [&sfx](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* s = firstUnnamed(p);
        if (!s) return false;
        const float vol = Vm::number(Vm::param(p, K("vol")), 100), pitch = Vm::number(Vm::param(p, K("pitch")), 100);
        const qb::Value* b = Vm::param(p, K("buss"));
        if (!sfx.play(s->key, vol / 100.0f, pitch / 100.0f, b ? b->key : 0)) v.logOnce("PlaySound: no sample " + qbkey::name(s->key));
        return true;
    });
    // StopSound <sample> (the song preview leaving the setlist); the buss
    // calls on the voices played on that buss [C scripts].
    vm.addNative("StopSound", [&sfx](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* s = firstUnnamed(p)) sfx.stopSample(s->key);
        return true;
    });
    vm.addNative("StopSoundsByBuss", [&sfx](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* b = firstUnnamed(p)) sfx.stopBuss(b->key);
        return true;
    });
    vm.addNative("PauseSoundsByBuss", [&sfx](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* b = firstUnnamed(p)) sfx.pauseBuss(b->key, true);
        return true;
    });
    vm.addNative("UnPauseSoundsByBuss", [&sfx](Vm&, ScriptRun&, qb::Struct& p) {
        if (const qb::Value* b = firstUnnamed(p)) sfx.pauseBuss(b->key, false);
        return true;
    });
    // StopAllSounds (destroy_attract_mode_text): every sound effect playing stops [C script; I: the streams are the native core's].
    vm.addNative("StopAllSounds", [&sfx](Vm&, ScriptRun&, qb::Struct&) {
        sfx.stopAll();
        return true;
    });
    for (const char* n : {"SetSoundParams", "SetSoundBussParams"})
        vm.addNative(n, [](Vm&, ScriptRun&, qb::Struct&) { return true; });
}
