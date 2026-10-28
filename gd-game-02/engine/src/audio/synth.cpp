// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/synth.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "audio/sfx_dsp.h"
#include "audio/xaudio_reverb.h"
#include "audio/xma_decoder.h"
#include "data/dta.h"
#include "data/game_data.h"
#include "scene/milo.h"
#include "scene/synth_objects.h"

namespace {

using sfxdsp::dbToLin;

// 2^(semitones / 12) as 0x822FFAB8 computes it [C].
float transposeRatio(float semitones) { return float(std::pow(2.0, double(float(double(semitones) * 0.0833333358168602)))); }

// An object a sound object names: its directory first, then the parents'.
const scene::Object* resolveIn(const scene::Object& from, const std::string& name) {
    if (name.empty()) return nullptr;
    if (from.dir)
        if (const scene::Object* o = from.dir->resolve(name)) return o;
    return nullptr;
}

}  // namespace

// One playing sound object and, for sequences, what it plays.
struct Synth::Inst {
    const scene::Object* obj = nullptr;
    float volume = 0, pan = 0, transpose = 0;
    std::vector<SfxPlayer::VoiceId> voices;
    double end = 0;                              // Sfx / WaitSeq: when done
    std::vector<std::unique_ptr<Inst>> children; // SfxSeq / groups
    std::vector<const scene::Object*> order;     // SerialGroupSeq: the children to come
    size_t next = 0;
};

Synth::Synth() : rng_(uint32_t(SDL_GetTicksNS() & 0x7fffffff) | 1) {}

Synth::~Synth() = default;

double Synth::now() const { return double(SDL_GetTicksNS()) / 1e9; }

float Synth::random01() { return float(rng_() & 0xffff) * 1.5258789e-05f; }

int Synth::randomInt(int lo, int hi) { return hi > lo ? lo + int((rng_() & 0xffff) % uint32_t(hi - lo)) : lo; }

// RandomFloat(avg - spread, avg + spread) when the spread is not 0
// (0x822F0560, 0x8226DDA0) [C].
float Synth::spread(float avg, float s) {
    if (s == 0) return avg;
    const float lo = avg - s, hi = avg + s;
    return float(double(random01()) * double(float(hi - lo)) + lo);
}

namespace {

DataNode soundConfig(const GameData& data) {
    DataNode root = parseDtb(data.read("config/gen/sound.dtb"));
    resolveConditionals(root, {"HX_XBOX", "_SHIP"});
    return root;
}

}  // namespace

std::vector<std::string> Synth::loadBanks(const GameData& data) {
    std::vector<std::string> problems;
    try {
        const DataNode root = soundConfig(data);
        const DataNode* banks = root.find("banks");
        for (size_t k = 1; banks && k < banks->children.size(); ++k) {
            const DataNode& b = banks->children[k];
            if (!b.isList() || b.children.size() < 2) continue;
            const std::string key = b.textAt(0), path = scene::miloArkPath(b.textAt(1) + ".milo");
            try {
                milos_.push_back(scene::loadMilo(data, path));
                preload(milos_.back()->root());
                addBank(key, &milos_.back()->root());
            } catch (const std::exception& e) {
                problems.push_back(path + ": " + e.what());
            }
        }
    } catch (const std::exception& e) {
        problems.push_back(std::string("config/sound.dta: ") + e.what());
    }
    return problems;
}

float Synth::optionsVolumeDb(const GameData& data, int index) {
    try {
        const DataNode root = soundConfig(data);
        if (const DataNode* o = root.find("options_screen"))
            if (const DataNode* sl = o->find("slider"); sl && index >= 0 && size_t(index) + 1 < sl->children.size())
                return sl->floatAt(size_t(index) + 1);
    } catch (const std::exception&) {
    }
    return index <= 0 ? -96.0f : 20.0f * std::log10(float(index) / 11.0f);
}

void Synth::removeBank(const std::string& key) {
    banks_.erase(std::remove_if(banks_.begin(), banks_.end(), [&](const auto& b) { return b.first == key; }), banks_.end());
}

void Synth::addBank(const std::string& key, const scene::Directory* dir) {
    if (!dir) return;
    for (auto& b : banks_)
        if (b.first == key) return void(b.second = dir);
    banks_.emplace_back(key, dir);
}

int Synth::preload(const scene::Directory& dir) {
    int n = 0;
    dir.forEach([&](scene::Object& o) {
        if (auto* s = o.as<scene::SynthSample>(); s && pcm(*s)) ++n;
    });
    return n;
}

bool Synth::refersTo(const Inst& in, const scene::Directory& dir) {
    for (const scene::Directory* d = in.obj ? in.obj->dir : nullptr; d; d = d->parent)
        if (d == &dir) return true;
    for (const auto& c : in.children)
        if (refersTo(*c, dir)) return true;
    for (const scene::Object* o : in.order)
        for (const scene::Directory* d = o->dir; d; d = d->parent)
            if (d == &dir) return true;
    return false;
}

void Synth::forget(const scene::Directory& dir) {
    for (auto it = playing_.begin(); it != playing_.end();) {
        if (refersTo(**it, dir)) {
            stopInst(**it);
            it = playing_.erase(it);
        } else {
            ++it;
        }
    }
    dir.forEach([&](scene::Object& o) {
        if (auto* s = o.as<scene::SynthSample>()) cache_.erase(s);
        buses_.erase(&o);
    });
    banks_.erase(std::remove_if(banks_.begin(), banks_.end(), [&](const auto& b) { return b.second == &dir; }), banks_.end());
}

std::shared_ptr<const SfxPcm> Synth::pcm(const scene::SynthSample& s, std::string* error) {
    if (auto it = cache_.find(&s); it != cache_.end()) return it->second;
    auto p = std::make_shared<SfxPcm>();
    std::string err;
    const uint64_t t0 = SDL_GetTicksNS();
    bool ok = false;
    if (!s.decoded) err = "not decoded: " + s.error;
    else if (!s.sample.hasData) err = "no sample data";
    else if (s.sample.format != scene::SampleData::kXma) err = "sample format " + std::to_string(s.sample.format) + " (only XMA is decoded)";
    else ok = xma::decode(s.data(), s.sample.size, int(s.sample.rate), 1, s.sample.sampleCount, p->samples, err);
    stats_.decodeMs += double(SDL_GetTicksNS() - t0) / 1e6;
    if (!ok) {
        ++stats_.decodeFailures;
        if (error) *error = err;
        // Without an XMA decoder (no FFmpeg) every sample fails: said once.
        static bool noDecoderLogged = false;
        if (xma::available()) SDL_Log("synth: %s: %s", s.name.c_str(), err.c_str());
        else if (!noDecoderLogged) noDecoderLogged = true, SDL_Log("synth: the sound effects are silent: %s", err.c_str());
        cache_[&s] = nullptr;  // reported once
        return nullptr;
    }
    ++stats_.decoded;
    p->rate = int(s.sample.rate);
    const uint32_t count = uint32_t(p->samples.size());
    const uint32_t loopEnd = s.loopEnd < 0 ? count : std::min(uint32_t(s.loopEnd), count);
    p->looping = s.looping && s.loopStart >= 0 && uint32_t(s.loopStart) < loopEnd;
    p->loopStart = p->looping ? uint32_t(s.loopStart) : 0;
    p->loopEnd = p->looping ? loopEnd : 0;
    cache_[&s] = p;
    return p;
}

std::vector<int> Synth::randomGroupPick(int count, uint32_t numSimul, const std::function<float()>& random01,
                                        const std::function<int(int, int)>& randomInt) {
    // 0x822F4F18 [C]: n = min(num_simul, children); a child whose object is
    // missing counts but plays nothing (the caller skips it).
    std::vector<int> picked;
    int need = int(std::min<uint32_t>(numSimul, uint32_t(count)));
    if (need == 1) return {randomInt(0, count)};
    for (int k = 0, left = count; need > 0 && k < count; ++k, --left)
        if (need == left || double(random01()) <= double(float(need) / float(left))) picked.push_back(k), --need;
    return picked;
}

std::shared_ptr<SfxPlayer::Bus> Synth::bus(const scene::Object& send) {
    if (auto it = buses_.find(&send); it != buses_.end()) return it->second;
    std::shared_ptr<SfxPlayer::Bus> b;
    if (const auto* r = send.as<scene::FxSendReverb>()) {
        I3DL2Params p{};
        i3dl2Preset(r->environment, p);
        b = player_.reverbBus(p);
    }
    buses_[&send] = b;
    return b;
}

const scene::Object* Synth::find(const std::string& name) const {
    for (const auto& b : banks_)
        if (const scene::Object* o = b.second->find(name)) return o;
    return nullptr;
}

bool Synth::play(const std::string& name, float volumeDb, float pan, float transpose) {
    const scene::Object* o = find(name);
    return o && play(*o, volumeDb, pan, transpose);
}

bool Synth::play(const scene::Object& o, float volumeDb, float pan, float transpose) {
    if (!o.as<scene::Sfx>() && !o.as<scene::SequenceObj>()) return false;
    auto in = std::make_unique<Inst>();
    in->obj = &o;
    in->volume = volumeDb, in->pan = pan, in->transpose = transpose;
    ++stats_.plays;
    const double t = now();
    start(*in, t);
    if (!step(*in, t)) playing_.push_back(std::move(in));
    return true;
}

void Synth::start(Inst& in, double t) {
    const scene::Object& o = *in.obj;
    if (const auto* sfx = o.as<scene::Sfx>()) {
        // The send: an FxSend360 (only FxSendReverbs are in the data) [C].
        std::shared_ptr<SfxPlayer::Bus> send;
        if (const scene::Object* so = resolveIn(o, sfx->send)) send = bus(*so);
        else if (!sfx->send.empty()) ++stats_.unresolved;
        for (const auto& m : sfx->maps) {
            const auto* s = dynamic_cast<const scene::SynthSample*>(resolveIn(o, m.sample));
            if (!s) {
                if (!m.sample.empty()) ++stats_.unresolved;
                continue;
            }
            std::shared_ptr<const SfxPcm> p = pcm(*s);
            if (!p) continue;
            SfxPlayer::Params pp;
            pp.gain = dbToLin(in.volume) * dbToLin(m.volume);
            pp.pan = in.pan + m.pan;
            pp.speed = transposeRatio(in.transpose) * transposeRatio(m.transpose);
            pp.attack = m.adsr.attackRate;
            pp.release = m.adsr.releaseRate;
            pp.send = send;
            pp.sendVolume = sfx->sendVolume;
            if (SfxPlayer::VoiceId id = player_.play(p, pp)) {
                in.voices.push_back(id);
                ++stats_.voices;
            }
        }
        return;
    }
    if (!o.as<scene::SequenceObj>()) return;
    auto child = [&](const scene::Object* c) { startChild(in, c, t); };
    auto resolved = [&](const std::string& name) -> const scene::Object* {
        if (name.empty()) return nullptr;  // an empty ObjPtr plays nothing
        const scene::Object* c = resolveIn(o, name);
        if (!c || (!c->as<scene::Sfx>() && !c->as<scene::SequenceObj>())) {
            ++stats_.unresolved;
            return nullptr;
        }
        return c;
    };
    if (const auto* w = o.as<scene::WaitSeq>()) {
        in.end = t + std::max(0.0f, spread(w->avgWaitSeconds, w->waitSpread));
    } else if (const auto* s = o.as<scene::SfxSeq>()) {
        if (const scene::Object* c = resolved(s->child)) child(c);
    } else if (const auto* r = o.as<scene::RandomGroupSeq>()) {
        for (int k : randomGroupPick(int(r->children.size()), r->numSimul, [this] { return random01(); },
                                     [this](int lo, int hi) { return randomInt(lo, hi); }))
            if (const scene::Object* c = resolved(r->children[size_t(k)])) child(c);
    } else if (const auto* g = o.as<scene::GroupSeq>()) {  // SerialGroupSeq
        for (const auto& n : g->children)
            if (const scene::Object* c = resolved(n)) in.order.push_back(c);
        if (!in.order.empty()) child(in.order[in.next++]);
    }
}

void Synth::startChild(Inst& in, const scene::Object* c, double t) {
    const scene::SequenceParams& sp = in.obj->as<scene::SequenceObj>()->params;
    auto ci = std::make_unique<Inst>();
    ci->obj = c;
    ci->volume = in.volume + spread(sp.avgVolume, sp.volumeSpread);
    ci->transpose = in.transpose + spread(sp.avgTranspose, sp.transposeSpread);
    ci->pan = in.pan + spread(sp.avgPan, sp.panSpread);
    start(*ci, t);
    in.children.push_back(std::move(ci));
}

bool Synth::step(Inst& in, double t) {
    if (in.obj->as<scene::Sfx>()) {
        for (SfxPlayer::VoiceId id : in.voices)
            if (player_.playing(id)) return false;
        return true;
    }
    if (in.obj->as<scene::WaitSeq>()) return t >= in.end;
    bool done = true;
    for (auto& c : in.children)
        if (!step(*c, t)) done = false;
    // SerialGroupSeq: the next child when the current one has ended.
    while (done && in.next < in.order.size()) {
        in.children.clear();
        startChild(in, in.order[in.next++], t);
        done = step(*in.children.back(), t);
    }
    return done;
}

void Synth::stopInst(Inst& in) {
    for (SfxPlayer::VoiceId id : in.voices) player_.stop(id);
    in.voices.clear();
    for (auto& c : in.children) stopInst(*c);
    in.children.clear();
    in.order.clear();
    in.end = 0;
}

void Synth::stop(const scene::Object& o) {
    for (auto it = playing_.begin(); it != playing_.end();) {
        if ((*it)->obj == &o) {
            stopInst(**it);
            it = playing_.erase(it);
        } else {
            ++it;
        }
    }
}

bool Synth::stop(const std::string& name) {
    const scene::Object* o = find(name);
    if (o) stop(*o);
    return o != nullptr;
}

void Synth::stopAll() {
    for (auto& in : playing_) stopInst(*in);
    playing_.clear();
    player_.stopAll();
}

// pause_all_sfx / Game::SetPaused (0x822E7C68): every Sfx pauses the
// instances it is playing now (0x822ECB40); the sequences and the sounds
// started afterwards go on [C]. A Synth-wide pause kept the menus silent
// after a song (game_outro_complete pauses the game for good, gd-backlog
// GD02-073).
void Synth::setPaused(bool paused) { player_.setPaused(paused); }

void Synth::update() {
    if (playing_.empty()) return;
    const double t = now();
    playing_.erase(std::remove_if(playing_.begin(), playing_.end(), [&](const std::unique_ptr<Inst>& in) { return step(*in, t); }),
                   playing_.end());
}
