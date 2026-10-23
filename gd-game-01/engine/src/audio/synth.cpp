// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "audio/synth.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "audio/mixer.h"
#include "data/game_data.h"

struct Synth::Sound {
    std::string name;
    std::vector<int> voices;
    std::vector<const SoundBank::Desc*> layers;
    bool playing(Spu& spu) const {
        for (int v : voices)
            if (spu.playing(v)) return true;
        return false;
    }
    void stop(Spu& spu) const {
        for (int v : voices) spu.keyOff(v);
    }
};

// ------------------------------------------------------------------ sequences

// Sequence (vtable slots: Stop, IsPlaying, SetVolume, SetPan, SetTranspose,
// Poll, Play); docs/audio_sfx.md §2.
class Synth::Seq {
public:
    virtual ~Seq() = default;
    virtual void play() = 0;
    virtual void stop() = 0;
    virtual bool playing() = 0;
    virtual void poll(float) {}
    virtual void setVolume(float v) { volume_ = v; }
    virtual void setPan(float p) { pan_ = p; }
    virtual void setTranspose(float t) { transpose_ = t; }

protected:
    float volume_ = 1, pan_ = 0, transpose_ = 0;
};

namespace {

// A sequence parameter: a number, or (rand lo hi) (FUN_0020f5a8).
float evalParam(const DataNode* p, float def, std::mt19937& rng) {
    if (!p || p->children.size() < 2) return def;
    const DataNode& v = p->children[1];
    if (v.type == DataNode::Type::Float) return v.f;
    if (v.type == DataNode::Type::Int) return float(v.i);
    if (v.isList() && v.children.size() >= 3 && v.children[0].isText() && v.children[0].s == "rand") {
        float lo = v.children[1].type == DataNode::Type::Float ? v.children[1].f : float(v.children[1].i);
        float hi = v.children[2].type == DataNode::Type::Float ? v.children[2].f : float(v.children[2].i);
        return lo + (hi - lo) * float(rng() & 0xFFFF) / 65536.0f;
    }
    return def;
}

}  // namespace

// sfx_seq (SfxSeq_v8): one sfx with volume * runtime volume, pan + runtime pan,
// transpose + runtime transpose (semitones).
class SfxSeq : public Synth::Seq {
public:
    SfxSeq(Synth& synth, const DataNode& def) : synth_(synth), def_(def) {
        if (const DataNode* s = def.find("sfx")) sfx_ = s->textAt(1);
    }
    void play() override {
        stop();
        float vol = evalParam(def_.find("volume"), 1, synth_.rng_) * volume_;
        float pan = evalParam(def_.find("pan"), 0, synth_.rng_) + pan_;
        const DataNode* t = def_.find("transpose");
        float tr = (t && t->children.size() > 1 ? (t->children[1].type == DataNode::Type::Float ? t->children[1].f : float(t->children[1].i)) : 0) + transpose_;
        sound_ = synth_.start(sfx_, vol, tr, pan);
    }
    void stop() override {
        if (sound_) sound_->stop(synth_.mixer_.spu());
    }
    bool playing() override { return sound_ && sound_->playing(synth_.mixer_.spu()); }

private:
    Synth& synth_;
    const DataNode& def_;
    std::string sfx_;
    std::unique_ptr<Synth::Sound> sound_;
};

namespace {

class GroupSeq : public Synth::Seq {
public:
    std::vector<std::unique_ptr<Synth::Seq>> children;
    void setVolume(float v) override {
        Seq::setVolume(v);
        for (auto& c : children) c->setVolume(v);
    }
    void setPan(float p) override {
        Seq::setPan(p);
        for (auto& c : children) c->setPan(p);
    }
    void setTranspose(float t) override {
        Seq::setTranspose(t);
        for (auto& c : children) c->setTranspose(t);
    }
    void stop() override {
        for (auto& c : children) c->stop();
    }
};

// random_group_seq (RandomGroupSeq_v8): num_seqs random children at once, no repeat rule.
class RandomGroupSeq : public GroupSeq {
public:
    RandomGroupSeq(std::mt19937& rng, int num) : rng_(rng), num_(std::max(1, num)) {}
    void play() override {
        stop();
        chosen_.clear();
        size_t count = children.size();
        if (count == 0) return;
        size_t n = std::min(size_t(num_), count);
        std::vector<size_t> pick(count);
        for (size_t i = 0; i < count; ++i) pick[i] = i;
        if (n == 1) pick = {size_t(rng_() % count)};
        else
            while (pick.size() > n) pick.erase(pick.begin() + long(rng_() % pick.size()));
        for (size_t i : pick) {
            children[i]->play();
            chosen_.push_back(i);
        }
    }
    bool playing() override {
        for (size_t i : chosen_)
            if (children[i]->playing()) return true;
        return false;
    }

private:
    std::mt19937& rng_;
    int num_;
    std::vector<size_t> chosen_;
};

// serial_group_seq / loop_group_seq: one child after another (loop wraps forever).
class SerialGroupSeq : public GroupSeq {
public:
    explicit SerialGroupSeq(bool loop) : loop_(loop) {}
    void play() override {
        stop();
        index_ = 0;
        active_ = !children.empty();
        if (active_) children[0]->play();
    }
    void stop() override {
        GroupSeq::stop();
        active_ = false;
    }
    void poll(float dt) override {
        if (!active_) return;
        children[index_]->poll(dt);
        if (children[index_]->playing()) return;
        if (++index_ >= children.size()) {
            if (!loop_) {
                active_ = false;
                return;
            }
            index_ = 0;
        }
        children[index_]->play();
    }
    bool playing() override { return active_; }

private:
    bool loop_, active_ = false;
    size_t index_ = 0;
};

class ParallelGroupSeq : public GroupSeq {
public:
    void play() override {
        for (auto& c : children) c->play();
    }
    void poll(float dt) override {
        for (auto& c : children) c->poll(dt);
    }
    bool playing() override {
        for (auto& c : children)
            if (c->playing()) return true;
        return false;
    }
};

// wait_seq: playing for `secs`.
class WaitSeq : public Synth::Seq {
public:
    WaitSeq(float secs) : secs_(secs) {}
    void play() override { left_ = secs_ * 1000; }
    void stop() override { left_ = 0; }
    void poll(float dt) override { left_ -= dt; }
    bool playing() override { return left_ > 0; }

private:
    float secs_, left_ = 0;
};

// vol_mod_seq / pan_mod_seq / transpose_mod_seq / loop_mod_seq wrap one sequence.
class ModSeq : public Synth::Seq {
public:
    enum Kind { Vol, Pan, Transpose, Loop };
    ModSeq(Kind kind, float value, std::unique_ptr<Synth::Seq> child) : kind_(kind), value_(value), child_(std::move(child)) {
        apply();
    }
    void play() override {
        looping_ = kind_ == Loop;
        if (child_) child_->play();
    }
    void stop() override {
        looping_ = false;
        if (child_) child_->stop();
    }
    void poll(float dt) override {
        if (!child_) return;
        child_->poll(dt);
        if (looping_ && !child_->playing()) child_->play();
    }
    bool playing() override { return kind_ == Loop ? looping_ : child_ && child_->playing(); }
    void setVolume(float v) override { Seq::setVolume(v), apply(); }
    void setPan(float p) override { Seq::setPan(p), apply(); }
    void setTranspose(float t) override { Seq::setTranspose(t), apply(); }

private:
    void apply() {
        if (!child_) return;
        child_->setVolume(kind_ == Vol ? volume_ * value_ : volume_);
        child_->setPan(kind_ == Pan ? pan_ + value_ : pan_);
        child_->setTranspose(kind_ == Transpose ? transpose_ + value_ : transpose_);
    }
    Kind kind_;
    float value_;
    bool looping_ = false;
    std::unique_ptr<Synth::Seq> child_;
};

}  // namespace

// FUN_0020ed38: the class factory.
std::unique_ptr<Synth::Seq> Synth::build(const DataNode& def) {
    if (def.children.empty() || !def.children[0].isText()) return nullptr;
    const std::string& cls = def.children[0].s;
    auto group = [&](GroupSeq& g) {
        if (const DataNode* ch = def.find("children"))
            for (size_t k = 1; k < ch->children.size(); ++k)
                if (ch->children[k].isList())
                    if (auto c = build(ch->children[k])) g.children.push_back(std::move(c));
    };
    if (cls == "sfx_seq") return std::make_unique<SfxSeq>(*this, def);
    if (cls == "random_group_seq") {
        const DataNode* n = def.find("num_seqs");
        auto g = std::make_unique<RandomGroupSeq>(rng_, n && n->children.size() > 1 ? n->children[1].i : 1);
        group(*g);
        return g;
    }
    if (cls == "serial_group_seq" || cls == "loop_group_seq") {
        auto g = std::make_unique<SerialGroupSeq>(cls == "loop_group_seq");
        group(*g);
        return g;
    }
    if (cls == "parallel_group_seq") {
        auto g = std::make_unique<ParallelGroupSeq>();
        group(*g);
        return g;
    }
    if (cls == "wait_seq") return std::make_unique<WaitSeq>(evalParam(def.find("secs"), 0, rng_));
    ModSeq::Kind kind = cls == "vol_mod_seq" ? ModSeq::Vol : cls == "pan_mod_seq" ? ModSeq::Pan
                      : cls == "transpose_mod_seq" ? ModSeq::Transpose : ModSeq::Loop;
    if (cls == "vol_mod_seq" || cls == "pan_mod_seq" || cls == "transpose_mod_seq" || cls == "loop_mod_seq") {
        std::unique_ptr<Seq> child;
        for (size_t k = 1; k < def.children.size() && !child; ++k)
            if (def.children[k].isList() && !def.children[k].children.empty() && def.children[k].children[0].isText() &&
                def.children[k].children[0].s.find("_seq") != std::string::npos)
                child = build(def.children[k]);
        return std::make_unique<ModSeq>(kind, evalParam(def.find("value"), kind == ModSeq::Vol ? 1.0f : 0.0f, rng_), std::move(child));
    }
    return nullptr;  // midi_seq: unused in GH1
}

// ------------------------------------------------------------------ synth

Synth::Synth(Mixer& mixer, const GameData& data) : mixer_(mixer), data_(data) {
    // config/synth.dta fx: core_0 echo (volume 0.3, delay 0.5, feedback 0.3), core_1 large studio (0.4).
    mixer_.spu().setEffects(0.3f, 0.5f, 0.3f, 0.4f);
}

Synth::~Synth() = default;

void Synth::loadSlot(int slot, const std::string& path) {
    if (slot < 0 || slot >= int(slots_.size())) return;
    for (size_t i = size_t(slot); i < slots_.size(); ++i) slots_[i].reset();
    try {
        slots_[size_t(slot)] = std::make_unique<SoundBank>(data_, path);
    } catch (const std::exception& ex) {
        SDL_Log("synth: bank %s: %s", path.c_str(), ex.what());
    }
}

void Synth::clearSlots() {
    stopAll();
    sequences_.clear();
    fireAndForget_.clear();
    for (auto& s : slots_) s.reset();
}

void Synth::loadMenuBanks() {
    if (slots_[0] && slots_[0]->path() == "sfx/metagame" && !slots_[1]) return;
    clearSlots();
    loadSlot(0, "sfx/metagame");
}

void Synth::loadGameBanks(const std::string& venueBank, const std::string& songBank) {
    clearSlots();
    if (!venueBank.empty()) loadSlot(0, venueBank);
    loadSlot(1, songBank.empty() ? "sfx/song_default" : songBank);
    loadSlot(2, "sfx/ingame");
}

// FUN_00216898 + FUN_00215ff0: the first bank with the name; each layer's
// volume, pan and speed combine the call's and the desc's.
std::unique_ptr<Synth::Sound> Synth::start(const std::string& name, float volume, float transpose, float pan) {
    auto sound = std::make_unique<Sound>();
    sound->name = name;
    for (const auto& bank : slots_) {
        if (!bank) continue;
        std::vector<const SoundBank::Desc*> layers = bank->layers(name);
        if (layers.empty()) continue;
        if (SDL_getenv("GD01_SFXLOG"))  // development aid
            SDL_Log("sfx %s (%s, %d layers, vol %.2f pan %.2f)", name.c_str(), bank->path().c_str(), int(layers.size()), double(volume), double(pan));
        float ratio = std::pow(2.0f, transpose / 12.0f);
        for (const SoundBank::Desc* d : layers) {
            const SoundBank::Sample* s = bank->sample(d->sample);
            if (!s) continue;
            Spu::KeyOn k;
            k.sample = s->pcm;
            k.rate = s->rate;
            k.speed = ratio * d->ratio;
            k.volume = std::clamp(volume * d->volume, 0.0f, 1.0f);
            k.pan = std::clamp(pan + d->pan, -1.0f, 1.0f);
            k.adsr1 = d->adsr1, k.adsr2 = d->adsr2;
            k.core = d->core;
            if (int id = mixer_.spu().keyOn(k)) sound->voices.push_back(id);
        }
        return sound;
    }
    if (SDL_getenv("GD01_SFXLOG")) SDL_Log("sfx %s: not in any loaded bank", name.c_str());
    return sound;  // "couldn't find sfx": silent
}

void Synth::playSfx(const std::string& name, float volume, float transpose, float pan, bool stop) {
    if (stop) stopSfx(name);
    fireAndForget_.push_back(start(name, volume, transpose, pan));
}

void Synth::stopSfx(const std::string& name) {
    for (auto& s : fireAndForget_)
        if (s->name == name) s->stop(mixer_.spu());
}

bool Synth::playSequence(const std::string& name, float volume, float transpose, float pan) {
    for (const auto& bank : slots_) {
        if (!bank) continue;
        if (const DataNode* def = bank->sequence(name)) {
            std::unique_ptr<Seq> seq = build(*def);
            if (!seq) return false;
            seq->setVolume(volume);
            seq->setTranspose(transpose);
            seq->setPan(pan);
            seq->play();
            sequences_.push_back(std::move(seq));
            return true;
        }
    }
    return false;
}

void Synth::stopAll() {
    mixer_.spu().keyOffAll();
    fireAndForget_.clear();
}

void Synth::pauseAll(bool paused) { mixer_.spu().setPaused(paused); }

void Synth::poll(float dtMs) {
    clockMs_ += dtMs;
    for (auto& s : sequences_) s->poll(dtMs);
    std::erase_if(sequences_, [](const std::unique_ptr<Seq>& s) { return !s->playing(); });
    std::erase_if(fireAndForget_, [this](const std::unique_ptr<Sound>& s) { return !s->playing(mixer_.spu()); });
}
