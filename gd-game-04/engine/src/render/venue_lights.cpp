// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_lights.h"

#include <algorithm>
#include <cmath>

#include "data/qbkey.h"
#include "script/globals.h"

namespace venue {
namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }

const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct ? v->st.get() : nullptr; }
uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }
float num(const qb::Value* v, float f) { return v && (v->type == qb::Type::Float || v->type == qb::Type::Int) ? v->number() : f; }
bool hasFlag(const qb::Struct* st, uint32_t flag) {
    if (!st) return false;
    for (const qb::Item& it : st->items)
        if (it.name == 0 && it.value.type == qb::Type::Key && it.value.key == flag) return true;
    return false;
}
const char* perfName(Performance p) { return p == Performance::Poor ? "poor" : p == Performance::Good ? "good" : "medium"; }

}  // namespace

void LightShow::init(const Venue& v, const Globals& g, const Chart& chart) {
    v_ = &v, g_ = &g;
    notes_ = chart.lightshow;
    std::stable_sort(notes_.begin(), notes_.end(), [](const ChartEvent& a, const ChartEvent& b) { return a.time < b.time; });
    offsetMs_ = qbNumber(g, K("lightshow_offset_ms"), 100);
    defaultBlend_ = blend_ = qbNumber(g, K("lightshow_defaultblendtime"), 0.15f);
    overrideMix_ = qbNumber(g, K("lightshow_coloroverrideblend"), 0.4f);
    if (const qb::Value* ex = g.find(K("LightShow_ColorOverrideExcludeLights")); ex && ex->type == qb::Type::Array && ex->arr)
        for (const qb::Value& k : ex->arr->values) overrideExclude_.insert(k.key);
    mapping_ = structOf(v.find(v.key("_lightshow_mapping")));
    snapshots_ = structOf(v.find(v.key("_snapshots")));
    lights_.resize(v.lights.size());
    for (size_t i = 0; i < v.lights.size(); ++i) {
        const Light& l = v.lights[i];
        LightState s{l.color, l.intensity, l.attenStart, l.attenEnd};
        lights_[i].from = lights_[i].to = s;
    }
    // Progression_SetProgressionNodeFlags outside an encore / the 3.5 tier
    // change: LS_ALWAYS, LS_3_5_PRE and LS_ENCORE_PRE on [C scripts; I: the
    // quickplay values]; LightShow_InitEventMappings: LS_SPOTLIGHT_GUITARIST on.
    for (const char* f : {"LS_ALWAYS", "LS_3_5_PRE", "LS_ENCORE_PRE", "LS_SPOTLIGHT_GUITARIST"}) flags_.insert(K(f));
    // LightShow_InitEventMappings: LightShow_SetParams performance=medium mood=intro [C].
    perf_ = Performance::Medium;
    setStateFlags();
    enterMood(K("intro"), -1e9);
}

const qb::Value* LightShow::modeEntry() const {
    if (!mapping_) return nullptr;
    const qb::Struct* mood = structOf(mapping_->find(mood_));
    if (!mood) return nullptr;
    if (const qb::Value* e = mood->find(K(perfName(perf_)))) return e;
    return mood->find(K("default"));
}

void LightShow::setStateFlags() {
    // lightshow_statenodeflagmapping: the performance's flags, then the mood's [C].
    const qb::Value* m = g_->find(K("LightShow_StateNodeFlags"));
    if (m && m->type == qb::Type::Array && m->arr)
        for (const qb::Value& f : m->arr->values) flags_.erase(f.key);
    const qb::Value* map = g_->find(K("lightshow_statenodeflagmapping"));
    const qb::Struct* ms = structOf(map);
    if (!ms) return;
    auto apply = [&](const qb::Struct* group, uint32_t key) {
        if (!group) return;
        const qb::Value* list = group->find(key);
        if (!list || list->type != qb::Type::Array || !list->arr) return;
        for (const qb::Value& pair : list->arr->values) {
            const qb::Struct* p = structOf(&pair);
            if (!p || p->items.size() < 2) continue;
            const uint32_t flag = p->items[0].value.key;
            if (p->items[1].value.number() != 0) flags_.insert(flag);
            else flags_.erase(flag);
        }
    };
    apply(structOf(ms->find(K("performance"))), K(perfName(perf_)));
    apply(structOf(ms->find(K("mood"))), mood_);
}

LightState LightShow::at(size_t i, double t) const {
    const Target& tg = lights_[i];
    if (tg.blend <= 0 || t >= tg.start + tg.blend * 1000.0) return tg.to;
    const float s = float(std::clamp((t - tg.start) / (tg.blend * 1000.0), 0.0, 1.0));
    LightState r;
    for (size_t k = 0; k < 3; ++k) r.color[k] = tg.from.color[k] + (tg.to.color[k] - tg.from.color[k]) * s;
    r.intensity = tg.from.intensity + (tg.to.intensity - tg.from.intensity) * s;
    r.attenStart = tg.from.attenStart + (tg.to.attenStart - tg.from.attenStart) * s;
    r.attenEnd = tg.from.attenEnd + (tg.to.attenEnd - tg.from.attenEnd) * s;
    return r;
}

void LightShow::play(uint32_t snapshot, double t) {
    // LightShow_PlaySnapshot: each entry sets one light (lightchecksum) to
    // r g b / 255, intensity, FarAttenStart / End, blended over the current
    // time (LightShow_SetTime; 0 while disabled) [C data; I: the blend].
    if (!snapshots_) return;
    const qb::Value* list = snapshots_->find(snapshot);
    if (!list || list->type != qb::Type::Array || !list->arr) return;
    snapshot_ = snapshot;
    ++changes_;
    for (const qb::Value& ev : list->arr->values) {
        const qb::Struct* e = structOf(&ev);
        if (!e) continue;
        const uint32_t light = keyOf(e->find(K("lightchecksum")));
        if (!light) continue;
        for (size_t i = 0; i < v_->lights.size(); ++i) {
            if (v_->lights[i].name != light) continue;
            Target& tg = lights_[i];
            tg.from = at(i, t);
            tg.to.color = {num(e->find(K("r")), 255) / 255.0f, num(e->find(K("g")), 255) / 255.0f, num(e->find(K("b")), 255) / 255.0f};
            tg.to.intensity = num(e->find(K("intensity")), tg.to.intensity);
            tg.to.attenStart = num(e->find(K("FarAttenStart")), tg.to.attenStart);
            tg.to.attenEnd = num(e->find(K("FarAttenEnd")), tg.to.attenEnd);
            if (strobeOn_ && mood_ == K("strobe")) tg.to.intensity = 1.0f;  // LightShow_AppendSnapshotParams intensity=1.0
            tg.start = t;
            tg.blend = timeEnabled_ ? blend_ : 0.0;
        }
    }
}

void LightShow::cycle(double t, bool save) {
    // LightShow_CycleNextSnapshot: the next snapshot of the mood's list for
    // the performance [I: in list order, from the one showing].
    const qb::Struct* e = structOf(modeEntry());
    if (!e) return;
    const qb::Value* list = e->find(K("snapshots"));
    if (list && list->type == qb::Type::KeyPtr) list = v_->find(list->key);
    if (!list || list->type != qb::Type::Array || !list->arr || list->arr->values.empty()) return;
    const auto& vals = list->arr->values;
    size_t next = 0;
    for (size_t i = 0; i < vals.size(); ++i)
        if (vals[i].key == snapshot_) next = (i + 1) % vals.size();
    play(vals[next].key, t);
    if (save) savedSnapshot_ = vals[next].key;
}

void LightShow::enterMood(uint32_t mood, double t) {
    const uint32_t oldProc = processor_;
    // The old processor's exit script.
    if (oldProc == K("Strobe_Generic")) timeEnabled_ = true;  // LightShow_Strobe_Exit: SetTime enable [C]
    mood_ = mood;
    setStateFlags();
    const qb::Struct* e = structOf(modeEntry());
    processor_ = e ? keyOf(e->find(K("processor"))) : 0;
    strobeOn_ = false;
    if (processor_ == K("Strobe_Generic")) {
        timeEnabled_ = false;  // LightShow_Strobe_Enter: SetTime enable=false
        cycle(t, false);       // LightShow_Strobe_Event's first CycleNextSnapshot (the blackout)
    } else if (processor_ == K("Blackout_Generic") || processor_ == K("Flare_Generic")) {
        cycle(t, false);
    } else if (processor_) {
        cycle(t, true);  // LightShow_GenericMood_Enter / LightShow_Poor_Enter
    }
}

void LightShow::applyNote(const ChartEvent& e, double t) {
    // LightShow_NoteMapping [C]: 70..76 moods, 61..69 colour overrides,
    // 60 strobe toggle, 57 / 58 snapshot change, 56 pyro, 39..53 blend time.
    const qb::Value* map = g_->find(K("LightShow_NoteMapping"));
    if (!map || map->type != qb::Type::Array || !map->arr) return;
    for (const qb::Value& m : map->arr->values) {
        const qb::Struct* st = structOf(&m);
        if (!st || int(num(st->find(K("midinote")), -1)) != e.note) continue;
        const qb::Struct* params = structOf(st->find(K("params")));
        const uint32_t scr = keyOf(st->find(K("scr"))), event = keyOf(st->find(K("event")));
        if (scr == K("LightShow_SetParams")) {
            const uint32_t mood = params ? keyOf(params->find(K("mood"))) : 0;
            if (mood && mood != mood_) enterMood(mood, t);
        } else if (scr == K("LightShow_OverrideColor")) {
            override_ = !hasFlag(params, K("OFF"));
            if (override_) {
                const uint32_t c = params ? keyOf(params->find(K("color"))) : 0;
                const qb::Struct* table = structOf(g_->find(K("lightshow_coloroverrides")));
                const qb::Value* col = table ? table->find(c) : nullptr;
                if (col && col->type == qb::Type::Vector) overrideColor_ = {col->f[0] / 255, col->f[1] / 255, col->f[2] / 255};
                else override_ = false;
            }
        } else if (scr == K("LightShow_SetTime")) {
            if (hasFlag(params, K("default"))) blend_ = defaultBlend_;
            else if (params) blend_ = num(params->find(K("time")), float(defaultBlend_));
        } else if (event == K("snapshotchange")) {
            // GenericMood_Event cycles on it; Poor_Event and the others wait [C].
            if (processor_ && processor_ != K("Poor_Generic") && processor_ != K("Blackout_Generic") && processor_ != K("Flare_Generic") &&
                processor_ != K("Strobe_Generic"))
                cycle(t, true);
        } else if (event == K("strobetoggle")) {
            if (processor_ == K("Strobe_Generic")) {
                strobeOn_ = !strobeOn_;
                if (strobeOn_ && savedSnapshot_) play(savedSnapshot_, t);
                else cycle(t, false);
            }
        }
        return;
    }
}

void LightShow::update(double ms, const PerfTimeline& perf) {
    // lightshow_iterator runs with time_offset = gem offset + lightshow_offset_ms:
    // a note fires lightshow_offset_ms early [I: the sign].
    for (;;) {
        const double tNote = noteAt_ < notes_.size() ? double(notes_[noteAt_].time) - offsetMs_ : 1e300;
        const double tPerf = perfAt_ < perf.changes.size() ? perf.changes[perfAt_].first : 1e300;
        const double t = std::min(tNote, tPerf);
        if (t > ms) break;
        now_ = std::max(now_, t);
        if (tPerf <= tNote) {
            const Performance p = perf.changes[perfAt_++].second;
            if (p == perf_) continue;
            // The crowd's performance changed: the state flags and the mood's
            // snapshots for it [I: re-entering the mood's processor].
            perf_ = p;
            const uint32_t m = mood_;
            mood_ = 0;
            enterMood(m, now_);
        } else {
            applyNote(notes_[noteAt_++], now_);
        }
    }
    now_ = std::max(now_, ms);
}

void LightShow::evaluate(double ms, std::vector<LightState>& out) const {
    out.resize(lights_.size());
    for (size_t i = 0; i < lights_.size(); ++i) {
        out[i] = at(i, ms);
        if (override_ && !overrideExclude_.count(v_->lights[i].name))
            for (size_t k = 0; k < 3; ++k) out[i].color[k] += (overrideColor_[k] - out[i].color[k]) * overrideMix_;
    }
}

}  // namespace venue
