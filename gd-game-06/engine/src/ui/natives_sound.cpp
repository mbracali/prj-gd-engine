// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/natives_sound.h"
#include "data/disc.h"
#include <set>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>
#include <vector>
#include <algorithm>

#include <memory>
#include <random>
#include <unordered_map>

#include "audio/sfx.h"
#include "audio/sound_buss.h"
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

void applySoundBussParams(qs::Vm& vm, const qb::Struct& p) {
    using qs::Vm;
    SoundBusses& sb = soundBusses();
    const float time = Vm::number(Vm::param(p, K("time")), 0);
    auto one = [&](uint32_t buss, const qb::Value& v) {
        if (v.type != qb::Type::Struct || !v.st) return;
        const qb::Value* vol = v.st->find(K("vol"));
        const qb::Value* pitch = v.st->find(K("pitch"));
        const float vv = vol ? vol->number() : 0, pv = pitch ? pitch->number() : 0;
        sb.set(buss, vol ? &vv : nullptr, pitch ? &pv : nullptr, time);
    };
    for (const qb::Item& it : p.items) {
        const qb::Value* val = &it.value;
        if (it.name == K("time")) continue;
        if (it.name == 0) {
            // A BussSet: its own items are the busses.
            if (val->type == qb::Type::Key || val->type == qb::Type::KeyPtr) val = vm.global(val->key);
            if (!val || val->type != qb::Type::Struct || !val->st) continue;
            for (const qb::Item& b : val->st->items) one(b.name, b.value);
        } else {
            one(it.name, *val);
        }
    }
}

void addSoundNatives(qs::Vm& vm, Sfx& sfx, const Disc& disc) {
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
        uint32_t buss = K("default");  // no default_buss: FMOD's default group [I]
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
        if (!sfx.play(s->key, vol / 100.0f, pitch / 100.0f, b ? b->key : K("default"))) v.logOnce("PlaySound: no sample " + qbkey::name(s->key));
        return true;
    });
    // SongLoadFSB song_prefix = p: the song's bank (DATA/MUSIC/<p>.fsb.xen +
    // .dat.xen) for its <p>_preview, played by the setlist's
    // setlist_songpreview_monitor (indexed once a song; decoded on play) [C scripts].
    auto loadedBanks = std::make_shared<std::set<std::string>>();
    vm.addNative("SongLoadFSB", [&sfx, &disc, loadedBanks](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* pre = Vm::param(p, K("song_prefix"));
        if (!pre) return false;
        const std::string name = pre->type == qb::Type::String || pre->type == qb::Type::WString ? pre->str : qbkey::name(pre->key);
        if (loadedBanks->count(name)) return true;
        const std::string fsb = "DATA/MUSIC/" + name + ".fsb.xen", dat = "DATA/MUSIC/" + name + ".dat.xen";
        if (!disc.exists(fsb)) {
            v.logOnce("SongLoadFSB: no " + fsb);
            return false;
        }
        sfx.addFsb(disc, fsb, dat);
        loadedBanks->insert(name);
        return true;
    });
    // SongUnLoadFSB: the bank goes, and with it the preview playing from it
    // (the setlist's buss Music_Setlist): a preview never outlives its song's
    // bank (it played on into the next song) [C script setlist_songpreview_monitor; I: the buss].
    vm.addNative("SongUnLoadFSB", [&sfx](Vm&, ScriptRun&, qb::Struct&) {
        sfx.stopBuss(K("Music_Setlist"));
        return true;
    });
    // IsSoundPlaying <sample>: a voice of it still sounding (the preview's loop).
    vm.addNative("IsSoundPlaying", [&sfx](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* s = firstUnnamed(p);
        return s && sfx.samplePlaying(s->key);
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
    vm.addNative("SetSoundParams", [](Vm&, ScriptRun&, qb::Struct&) { return true; });
    // The busses (audio/sound_buss.h): CreateBussSystem {<BussTree>} builds
    // the tree (a struct of busses, each a struct of its children and
    // LeafNodes = [...]) [C script Master_SFX_Adding_Sound_Busses, data sound_buss.qb].
    vm.addNative("CreateBussSystem", [](Vm& v, ScriptRun&, qb::Struct& p) {
        // {<BussTree>} reaches here as the tree's own items (master = {...}),
        // or as one unnamed struct / key.
        const qb::Value* tree = firstUnnamed(p);
        if (tree && (tree->type == qb::Type::Key || tree->type == qb::Type::KeyPtr)) tree = v.global(tree->key);
        const qb::Struct& root = tree && tree->type == qb::Type::Struct && tree->st ? *tree->st : p;
        soundBusses().create([&root](auto add) {
            std::function<void(const qb::Struct&, uint32_t)> walk = [&](const qb::Struct& s, uint32_t parent) {
                for (const qb::Item& it : s.items) {
                    if (it.name == K("LeafNodes") && it.value.arr) {
                        for (const qb::Value& l : it.value.arr->values) add(l.key, parent);
                    } else if (it.value.type == qb::Type::Struct && it.value.st) {
                        add(it.name, parent);
                        walk(*it.value.st, it.name);
                    }
                }
            };
            walk(root, 0);
        });
        SDL_Log("sound busses: %d in the tree", soundBusses().count());
        return true;
    });
    vm.addNative("SetSoundBussParams", [](Vm& v, ScriptRun&, qb::Struct& p) {
        applySoundBussParams(v, p);
        return true;
    });
    for (const char* n : {"SoundBussLock", "SoundBussUnLock"}) {
        const bool lock = std::string(n) == "SoundBussLock";
        vm.addNative(n, [lock](Vm&, ScriptRun&, qb::Struct& p) {
            if (const qb::Value* b = firstUnnamed(p)) soundBusses().lock(b->key, lock);
            return true;
        });
    }
    // ChangeSpinalTapVolume spinal_tap_volume = v change = +-1 -> volume:
    // the audio settings' knob, 0 to 11 (menu_audio_settings_get_buss_volume
    // maps 11 to 0 dB ... 0 to -100) [C scripts menu_audio_settings_*_selection_volume;
    // I: the sum, kept within 0..11]. Unhandled, the knobs never moved and
    // the busses stayed at 0 dB.
    vm.addNative("ChangeSpinalTapVolume", [](Vm&, ScriptRun& run, qb::Struct& p) {
        const int v = int(Vm::number(Vm::param(p, K("spinal_tap_volume")), 11)) + int(Vm::number(Vm::param(p, K("change")), 0));
        if (!run.frames.empty() && run.frames.back().locals)
            Vm::set(*run.frames.back().locals, K("volume"), Vm::makeInt(std::clamp(v, 0, 11)));
        return true;
    });
    vm.addNative("PushSoundBussParams", [](Vm&, ScriptRun&, qb::Struct&) {
        soundBusses().push();
        return true;
    });
    vm.addNative("PopSoundBussParams", [](Vm&, ScriptRun&, qb::Struct&) {
        soundBusses().pop();
        return true;
    });
}
