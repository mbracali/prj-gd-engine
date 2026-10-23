// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// TutorialPanel (new_tutorial_panel; vtable 0x2f8a20, handler 0x153f60):
// the `tutorial` object the lesson scripts talk to. It owns the voice-over
// player (VoiceMgr / StreamPlayer 0x1543a0..0x155890) and the two trainers:
// TutorialGuitarTune (lesson T1.1, 0x155990..0x156390) and TutorialHopoTrain
// (T3.1 / T3.3a, 0x156530..0x1576c0). See docs/tutorials_plan.md §4-§6.

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <vector>

#include "audio/synth.h"
#include "data/dta.h"
#include "data/game_data.h"
#include "ui/panel.h"
#include "ui/select_panels.h"

namespace {

constexpr const char* kSceneOwner = "tut_script";  // the overlay views live in its scene

// VoiceMgr: config/sound.dta (tutorial (streaming_vo (volume v) (dir d)
// (events #include ../tutorial/tutorial_vo.dta))).
class VoiceMgr {
public:
    explicit VoiceMgr(SelectHost& host) : host_(host) {
        try {
            DataNode sound = parseDtb(host.data().read(dtbPath("config/sound.dta")));
            if (const DataNode* t = sound.find("tutorial"))
                if (const DataNode* vo = t->find("streaming_vo")) {
                    if (const DataNode* v = vo->find("volume"); v && v->children.size() > 1) volume_ = v->floatAt(1);
                    if (const DataNode* d = vo->find("dir"); d && d->children.size() > 1) dir_ = d->textAt(1);
                }
            DataNode events = parseDtb(host.data().read(dtbPath("tutorial/tutorial_vo.dta")));
            for (const DataNode& e : events.children) {
                if (!e.isList() || e.children.empty() || !e.children[0].isText()) continue;
                Event ev{e.children[0].s, 0, {}};
                for (size_t k = 1; k < e.children.size(); ++k)
                    if (e.children[k].isText()) ev.files.push_back(e.children[k].s);
                events_.push_back(std::move(ev));
            }
        } catch (const std::exception& ex) {
            SDL_Log("VoiceMgr: no streaming vo for tutorial (%s)", ex.what());
        }
    }
    // FUN_00154b78: dropped while a VO plays; a multi-file event advances
    // before use (its first play is the 2nd file), then round-robins.
    void play(const std::string& event) {
        if (busy()) {
            if (SDL_getenv("GD01_UILOG")) SDL_Log("VoiceMgr::Play - dropping voice over %s", event.c_str());
            return;
        }
        for (Event& e : events_)
            if (e.name == event && !e.files.empty()) {
                e.idx = (e.idx + 1) % int(e.files.size());
                host_.voicePlay(dir_ + "/" + e.files[size_t(e.idx)] + ".vgs", volume_);
                requested_ = true;
                return;
            }
        SDL_Log("Streaming voice-over event %s does not exist", event.c_str());
    }
    // True from the request until the stream has ended (the poll deletes it).
    bool busy() const { return requested_; }
    void poll() {
        if (requested_ && !paused_ && !host_.voicePlaying()) requested_ = false;
    }
    void reset() {
        host_.voiceStop();
        requested_ = false;
    }
    void setPaused(bool p) {
        paused_ = p;
        host_.voicePause(p);
    }

private:
    struct Event {
        std::string name;
        int idx;
        std::vector<std::string> files;
    };
    SelectHost& host_;
    std::vector<Event> events_;
    std::string dir_ = "tutorial/streams";
    float volume_ = 1;
    bool requested_ = false, paused_ = false;
};

Panel* overlay(SelectHost& host) { return host.panelScene(kSceneOwner); }

std::string fretView(const char* fmt, int f) {
    char buf[48];
    std::snprintf(buf, sizeof buf, fmt, f + 1);
    return buf;
}

// TutorialGuitarTune: strum each fret 3 times with only that fret held.
class GuitarTune {
public:
    explicit GuitarTune(SelectHost& host) : host_(host) {}
    // FUN_00155ae8: the fretchecks hidden, the counts zeroed.
    void enter() {
        counts_.fill(0);
        if (Panel* p = overlay(host_))
            for (int f = 0; f < 5; ++f) p->setShowing(fretView("t1_fretcheck%d.view", f), false);
    }
    // FUN_00155f08: a held strum does not count. (The checks are hidden here
    // too: on Enter the overlay scene may not be loaded yet.)
    void start(bool strum) {
        prevStrum_ = strum;
        if (Panel* p = overlay(host_))
            for (int f = 0; f < 5; ++f)
                if (counts_[size_t(f)] < 3) p->setShowing(fretView("t1_fretcheck%d.view", f), false);
    }
    // FUN_00156390; true once every fret reached 3.
    bool poll(uint32_t frets, bool strum, VoiceMgr& vo) {
        Panel* p = overlay(host_);
        int held = 0, last = -1;
        for (int f = 0; f < 5; ++f) {
            bool on = frets & (1u << f);
            if (p) p->setFrame(fretView("t1_fret%d.view", f), on ? 1.0f : 0.0f);
            if (on) ++held, last = f;
        }
        if (strum && !prevStrum_) {
            if (held == 1) fretHit(last, vo);
            else if (held >= 2 && host_.synth()) host_.synth()->playSequence("miss_chink");
        }
        prevStrum_ = strum;
        bool done = std::all_of(counts_.begin(), counts_.end(), [](int c) { return c >= 3; });
        if (done && p)
            for (int f = 0; f < 5; ++f) p->setFrame(fretView("t1_fret%d.view", f), 0);
        return done;
    }

private:
    // FUN_00156118: three pitch steps per fret (s101xx), fret_played, the check at 3.
    void fretHit(int f, VoiceMgr& vo) {
        ++counts_[size_t(f)];
        if (Synth* s = host_.synth()) {
            for (int k = 0; k < 3; ++k) s->stopSfx(sfxName(f * 3 + k));
            s->playSfx(sfxName(f * 3 + std::min(counts_[size_t(f)], 3) - 1));
        }
        host_.callFunc("fret_played", {Value::integer(f + 1)});
        if (counts_[size_t(f)] == 3) {
            if (Panel* p = overlay(host_)) p->setShowing(fretView("t1_fretcheck%d.view", f), true);
            vo.play("tuning_hit_vo");
        }
        counts_[size_t(f)] = std::min(counts_[size_t(f)], 3);
    }
    static std::string sfxName(int i) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "s101%02d", i + 1);
        return buf;
    }
    SelectHost& host_;
    std::array<int, 5> counts_{};
    bool prevStrum_ = false;
};

// TutorialHopoTrain: 3 runs of hammer-ons (green -> red -> yellow) or
// pull-offs (yellow -> red -> green), each step within 2 s, no strum after the first.
class HopoTrain {
public:
    explicit HopoTrain(SelectHost& host) : host_(host) {}
    void enter() {
        if (Panel* p = overlay(host_))
            for (int f = 0; f < 5; ++f) p->setShowing(fretView("t1_fretcheck%d.view", f), false);
        fading_.fill(-1);
    }
    // FUN_00156b98: kHammerOnTutorial (1) or kPullOffTutorial (2).
    void start(int type, bool strum, float now) {
        completed_ = 0;
        prevStrum_ = strum;
        seq_ = type == 1 ? std::vector<int>{0, 1, 2} : std::vector<int>{2, 1, 0};
        active_ = valid_ = false;
        progress_ = 0;
        prevTop_ = -2;
        lastEvent_ = now;
    }
    void stop() {
        if (Synth* s = host_.synth())
            for (int f = 0; f < 5; ++f) s->stopSfx(sfxName(f));
    }
    // FUN_001576c0; true once 3 runs are complete. `dt60` = polls at 60 Hz this frame.
    bool poll(uint32_t frets, bool strum, float now, float dt60) {
        Panel* p = overlay(host_);
        int top = -1;
        for (int f = 0; f < 5; ++f) {
            bool on = frets & (1u << f);
            if (p) p->setFrame(fretView("t1_fret%d.view", f), on ? 1.0f : 0.0f);
            if (on) top = f;
        }
        bool topChanged = top != prevTop_;
        if (strum && !prevStrum_) {
            fadeAll();
            if (top == -1) {
                active_ = valid_ = false, progress_ = 0;
            } else if (top == seq_[0]) {
                progress_ = 1, active_ = valid_ = true;
                check(top);
            } else {
                active_ = true, valid_ = false;
            }
            if (top >= 0) fretEvent(top);
            lastEvent_ = now;
        } else if (active_ && topChanged) {
            if (top < 0) {
                active_ = valid_ = false, progress_ = 0;
                fadeAll();
            } else if (now - lastEvent_ <= 2000) {
                fretEvent(top);
                if (valid_) {
                    if (progress_ < int(seq_.size()) && top == seq_[size_t(progress_)]) check(top), ++progress_;
                    else valid_ = false, fadeAll();
                }
            } else {
                active_ = valid_ = false, progress_ = 0;
                fadeAll();
            }
            lastEvent_ = now;
        }
        if (valid_ && now - lastEvent_ > 2000) {
            fadeAll();
            active_ = valid_ = false, progress_ = 0;
        }
        prevStrum_ = strum;
        prevTop_ = top;
        // FUN_001575f0: a fading check's mnm advances 4 frames a poll; past 100 it hides.
        for (int f = 0; f < 5; ++f)
            if (fading_[size_t(f)] >= 0) {
                fading_[size_t(f)] += 4 * dt60;
                if (p) p->setFrame(fretView("t1_fret_check%d.mnm", f), std::min(fading_[size_t(f)], 100.0f));
                if (fading_[size_t(f)] > 100) {
                    if (p) p->setShowing(fretView("t1_fretcheck%d.view", f), false), p->setFrame(fretView("t1_fret_check%d.mnm", f), 0);
                    fading_[size_t(f)] = -1;
                }
            }
        if (valid_ && progress_ == int(seq_.size())) {
            ++completed_;
            fadeAll();
            valid_ = false;
            host_.callFunc("hopo_hit", {});
        }
        bool done = completed_ == int(seq_.size());
        if (done && p)
            for (int f = 0; f < 5; ++f) p->setFrame(fretView("t1_fret%d.view", f), 0);
        return done;
    }

private:
    void check(int f) {  // FUN_00157548
        if (Panel* p = overlay(host_)) {
            p->setFrame(fretView("t1_fret_check%d.mnm", f), 0);
            p->setShowing(fretView("t1_fretcheck%d.view", f), true);
        }
        fading_[size_t(f)] = -1;
    }
    void fadeAll() {
        Panel* p = overlay(host_);
        for (int f = 0; f < 5; ++f) {
            const SceneObject* v = p ? p->scene().find(fretView("t1_fretcheck%d.view", f)) : nullptr;
            if (v && v->drawable() && v->drawable()->showing && fading_[size_t(f)] < 0) fading_[size_t(f)] = 0;
        }
    }
    void fretEvent(int f) {  // FUN_00157320
        host_.callFunc("fret_played", {Value::integer(f + 1)});
        if (Synth* s = host_.synth()) {
            for (int k = 0; k < 5; ++k) s->stopSfx(sfxName(k));
            s->playSfx(sfxName(f));
        }
    }
    static std::string sfxName(int f) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "s2060%d", f + 1);
        return buf;
    }
    SelectHost& host_;
    std::vector<int> seq_{0, 1, 2};
    std::array<float, 5> fading_{};
    bool prevStrum_ = false, active_ = false, valid_ = false;
    int progress_ = 0, completed_ = 0, prevTop_ = -2;
    float lastEvent_ = 0;
};

class TutorialPanel : public NativePanel {
public:
    explicit TutorialPanel(SelectHost& host) : host_(host), vo_(host), tune_(host), train_(host) {}

    bool handle(const std::string& msg, std::vector<Value>& a, Value& r) override {
        auto arg = [&](size_t k) { return k < a.size() ? a[k] : Value::integer(0); };
        r = Value::integer(0);
        static const bool log = SDL_getenv("GD01_TUTLOG") != nullptr;  // development aid
        if (log && msg != "is_vo_playing" && msg != "is_tuning" && msg != "is_hopo_training")
            SDL_Log("tutorial %.1f s: %s %s", SDL_GetTicks() / 1000.0, msg.c_str(), a.empty() ? "" : a[0].str().c_str());
        if (msg == "play_vo") return vo_.play(arg(0).str()), true;
        if (msg == "is_vo_playing") return r = Value::integer(vo_.busy() ? 1 : 0), true;
        if (msg == "reset_vo") return vo_.reset(), true;
        if (msg == "start_tuning_mode") {
            if (!tuning_) {
                uint32_t f;
                bool s;
                host_.padState(f, s);
                tune_.start(s);
                tuning_ = true;
            }
            return true;
        }
        if (msg == "end_tuning_mode") return tuning_ = false, true;
        if (msg == "is_tuning") return r = Value::integer(tuning_ ? 1 : 0), true;
        if (msg == "start_hopo_train") {
            if (!hopo_) {
                uint32_t f;
                bool s;
                host_.padState(f, s);
                train_.start(arg(0).integerValue(), s, clock_);
                hopo_ = true;
            }
            return true;
        }
        if (msg == "end_hopo_train") {
            if (hopo_) train_.stop(), hopo_ = false;
            return true;
        }
        if (msg == "is_hopo_training") return r = Value::integer(hopo_ ? 1 : 0), true;
        if (msg == "is_tutorial_running") return r = Value::integer(running_ ? 1 : 0), true;
        if (msg == "set_tutorial_running") return running_ = arg(0).truthy(), host_.setTutorialRunning(running_), true;
        // The port lets keyboards and pads in guitar mode play tutorials (docs/tutorials_plan.md §2.5).
        if (msg == "is_missing_guitar") return r = Value::integer(0), true;
        return false;
    }
    void enter() override {  // TutorialPanel_v3
        tuning_ = hopo_ = false;
        tune_.enter();
        train_.enter();
    }
    void exit() override {  // TutorialPanel_v4
        tuning_ = hopo_ = false;
        train_.stop();
        vo_.reset();
    }
    void setPaused(bool paused) override { vo_.setPaused(paused); }
    // TutorialPanel_v10 (not called while the panel is paused).
    void poll(float dtMs) override {
        clock_ += dtMs;
        vo_.poll();
        uint32_t frets = 0;
        bool strum = false;
        host_.padState(frets, strum);
        if (tuning_ && tune_.poll(frets, strum, vo_)) tuning_ = false;
        if (hopo_ && train_.poll(frets, strum, clock_, dtMs / (1000.0f / 60.0f))) hopo_ = false;
    }

private:
    SelectHost& host_;
    VoiceMgr vo_;
    GuitarTune tune_;
    HopoTrain train_;
    bool tuning_ = false, hopo_ = false, running_ = false;
    float clock_ = 0;
};

}  // namespace

std::unique_ptr<NativePanel> makeTutorialPanel(SelectHost& host) { return std::make_unique<TutorialPanel>(host); }
