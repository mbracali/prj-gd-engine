// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_lights.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

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

void LightShow::init(const Venue& v, const Globals& g, const Chart& chart, uint32_t songKey) {
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
        LightState s{l.color, l.intensity, l.attenStart, l.attenEnd, l.specular, l.shadow};
        lights_[i].from = lights_[i].to = s;
    }
    // The light housings: game objects with a lightvolumetype [C data: the _gfx node arrays].
    housings_.clear();
    for (const Node& n : v.nodes) {
        if (!n.st || !n.st->find(K("lightvolumetype"))) continue;
        HousingState h;
        h.pos = n.pos, h.rot = n.rot;
        auto rgb = [&](uint32_t key, Vec3 fallback) {
            const qb::Value* c = n.st->find(key);
            if (!c || c->type != qb::Type::Array || !c->arr || c->arr->values.size() < 3) return fallback;
            return Vec3{c->arr->values[0].number() / 255.0f, c->arr->values[1].number() / 255.0f, c->arr->values[2].number() / 255.0f};
        };
        h.volumeColor = rgb(K("volumecolor"), {1, 1, 1});
        h.projectorColor = rgb(K("projectorcolor"), {0, 0, 0});
        h.startRadius = num(n.st->find(K("startradius")), 0), h.endRadius = num(n.st->find(K("endradius")), 0);
        h.innerRadius = num(n.st->find(K("innerradius")), 0), h.range = num(n.st->find(K("range")), 0);
        h.density = num(n.st->find(K("volumedensity")), 0);
        housings_[n.name] = HousingTarget{h, h, 0, 0};
    }
    // Progression_SetProgressionNodeFlags outside an encore / the 3.5 tier
    // change: LS_ALWAYS, LS_3_5_PRE and LS_ENCORE_PRE on [C scripts; I: the
    // quickplay values]; LightShow_InitEventMappings: LS_SPOTLIGHT_GUITARIST on.
    for (const char* f : {"LS_ALWAYS", "LS_3_5_PRE", "LS_ENCORE_PRE", "LS_SPOTLIGHT_GUITARIST"}) flags_.insert(K(f));
    // GD10 in-the-round bug (The Unforgiven): "in the round" songs place extra
    // Forum geometry -- James's static guitar on its stand
    // (Z_Forum_GTR65_Var_stand, levelgeometry createdfromvariable=ls_perf_round_v)
    // -- that only draws while the ls_perf_round node flags are up.
    // Progression_SetProgressionNodeFlags (all_dis.txt ~89185-89210) sets
    // ls_perf_standard=1 / ls_perf_round*=0 by default and, when
    // gh_songlist_props.<current_song>.in_the_round == 1, flips
    // ls_perf_round + ls_perf_round_m1..m8 (and, via the per-zone
    // z_forum_SetupNodeflags, ls_perf_round_v) on and ls_perf_standard off. The
    // port set none of these, so every in-the-round prop was culled at
    // venue_view.cpp. Port the flag block here; raise ls_perf_round_v too (the
    // flag the stand's createdfromvariable names) so the stand is no longer
    // culled. GD10_INROUND_LEGACY=1: skip it (no round flags, as before).
    static const bool inRoundLegacy = SDL_getenv("GD10_INROUND_LEGACY") != nullptr;
    bool inRound = false;
    if (!inRoundLegacy) {
        // Prefer the song actually being loaded (songKey, the chart's song =
        // the original script's `<arg> song`) over the current_song global,
        // which at lights-init time is still the stale default (e.g. battery),
        // so the lookup used the wrong song and never set the round flags.
        // gh_songlist_props is only a container of three KeyPtr references --
        // { <download_songlist_props> <permanent_songlist_props>
        // <jamsession_songlist_props> } -- which the port keeps unexpanded, so
        // find(song) on it never reaches a song. Look the song up in the three
        // real sub-lists directly (the main setlist lives in
        // permanent_songlist_props).
        const uint32_t cs = keyOf(g.find(K("current_song")));
        const uint32_t song = songKey ? songKey : cs;
        auto irOf = [&](const char* listName) -> int {  // -1 song not in this list, else its in_the_round (0/1)
            if (const qb::Struct* list = structOf(g.find(K(listName))))
                if (const qb::Struct* sp = structOf(list->find(song)))
                    if (const qb::Value* ir = sp->find(K("in_the_round"))) return ir->number() != 0 ? 1 : 0;
            return -1;
        };
        int r = -1;
        if (song)
            for (const char* list : {"permanent_songlist_props", "download_songlist_props", "jamsession_songlist_props"})
                if ((r = irOf(list)) >= 0) break;
        inRound = r == 1;
    }
    if (!inRoundLegacy) {
        if (inRound)
            for (const char* f : {"ls_perf_round", "ls_perf_round_m1", "ls_perf_round_m2", "ls_perf_round_m3", "ls_perf_round_m4",
                                  "ls_perf_round_m5", "ls_perf_round_m6", "ls_perf_round_m7", "ls_perf_round_m8", "ls_perf_round_v"})
                flags_.insert(K(f));
        else
            flags_.insert(K("ls_perf_standard"));
        if (SDL_getenv("GD10_GTRLOG")) {
            const uint32_t cs = keyOf(g.find(K("current_song")));
            SDL_Log("GTRLOG lights init: current_song=%s songKey=%s inRound=%d round_v_flag=%d", cs ? qbkey::name(cs).c_str() : "-",
                    songKey ? qbkey::name(songKey).c_str() : "-", int(inRound), int(flags_.count(K("ls_perf_round_v")) > 0));
        }
    }
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
    r.specular = tg.from.specular + (tg.to.specular - tg.from.specular) * s;
    r.shadow = tg.from.shadow + (tg.to.shadow - tg.from.shadow) * s;
    return r;
}

void LightShow::play(uint32_t snapshot, double t, bool setCurrent) {
    // LightShow_PlaySnapshot: each entry sets one light (lightchecksum) to
    // light_setparams (821B4950): r g b / 255, intensity, specularintensity,
    // shadowintensity; FarAttenStart / End are not applied (the scripts'
    // ApplyLightFarAtten*Change are empty) [C]; blended over the current time
    // (LightShow_SetTime; 0 while disabled) [I: the blend].
    if (!snapshots_) return;
    const qb::Value* list = snapshots_->find(snapshot);
    if (!list || list->type != qb::Type::Array || !list->arr) return;
    // A spotlight snapshot touches only the follow-spot lights; it must not
    // become the mood's current snapshot (which cycle/strobe track), so the
    // caller passes setCurrent=false and the mood snapshot is left in place.
    if (setCurrent) snapshot_ = snapshot;
    ++changes_;
    for (const qb::Value& ev : list->arr->values) {
        const qb::Struct* e = structOf(&ev);
        if (!e) continue;
        // A housing entry: pos, Quat, radii, range, volume and projector
        // colours (0..255) [C data: the zones' snapshots; I: blended like the lights].
        if (const uint32_t hs = keyOf(e->find(K("housingchecksum")))) {
            auto it = housings_.find(hs);
            if (it == housings_.end()) continue;
            HousingTarget& h = it->second;
            h.from = housingAt(h, t);
            if (const qb::Value* p = e->find(K("pos")); p && p->type == qb::Type::Vector) h.to.pos = {p->f[0], p->f[1], p->f[2]};
            if (const qb::Value* q = e->find(K("Quat")); q && q->type == qb::Type::Vector) {
                const float w2 = 1.0f - q->f[0] * q->f[0] - q->f[1] * q->f[1] - q->f[2] * q->f[2];
                h.to.rot = {q->f[0], q->f[1], q->f[2], std::sqrt(std::max(0.0f, w2))};
            }
            h.to.startRadius = num(e->find(K("startradius")), h.to.startRadius);
            h.to.endRadius = num(e->find(K("endradius")), h.to.endRadius);
            h.to.innerRadius = num(e->find(K("innerradius")), h.to.innerRadius);
            h.to.range = num(e->find(K("range")), h.to.range);
            h.to.density = num(e->find(K("volumedensity")), h.to.density);
            h.to.volumeColor = {num(e->find(K("volumecolorred")), h.to.volumeColor[0] * 255) / 255.0f,
                                num(e->find(K("volumecolorgreen")), h.to.volumeColor[1] * 255) / 255.0f,
                                num(e->find(K("volumecolorblue")), h.to.volumeColor[2] * 255) / 255.0f};
            h.to.projectorColor = {num(e->find(K("projectorcolorred")), h.to.projectorColor[0] * 255) / 255.0f,
                                   num(e->find(K("projectorcolorgreen")), h.to.projectorColor[1] * 255) / 255.0f,
                                   num(e->find(K("projectorcolorblue")), h.to.projectorColor[2] * 255) / 255.0f};
            h.start = t;
            h.blend = timeEnabled_ ? blend_ : 0.0;
            continue;
        }
        const uint32_t light = keyOf(e->find(K("lightchecksum")));
        if (!light) continue;
        for (size_t i = 0; i < v_->lights.size(); ++i) {
            if (v_->lights[i].name != light) continue;
            Target& tg = lights_[i];
            tg.from = at(i, t);
            tg.to.color = {num(e->find(K("r")), tg.to.color[0] * 255) / 255.0f, num(e->find(K("g")), tg.to.color[1] * 255) / 255.0f,
                           num(e->find(K("b")), tg.to.color[2] * 255) / 255.0f};
            tg.to.intensity = num(e->find(K("intensity")), tg.to.intensity);
            tg.to.specular = num(e->find(K("specularintensity")), tg.to.specular);
            tg.to.shadow = num(e->find(K("shadowintensity")), tg.to.shadow);
            if (strobeOn_ && mood_ == K("strobe")) tg.to.intensity = 1.0f;  // LightShow_AppendSnapshotParams intensity=1.0
            tg.start = t;
            tg.blend = timeEnabled_ ? blend_ : 0.0;
        }
    }
}

void LightShow::playSpotlight(uint32_t color, double t) {
    // mapping.spotlight_snapshots.<colour> -> a snapshot of just the follow-spot
    // lights; applied without disturbing the mood snapshot [C lightshow.qb].
    const qb::Struct* snaps = mapping_ ? structOf(mapping_->find(K("spotlight_snapshots"))) : nullptr;
    const qb::Value* snap = snaps && color ? snaps->find(color) : nullptr;
    if (snap) play(keyOf(snap), t, false);
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

// The processor each mode entry names is resolved through the processor table
// LightShow_SetProcessors registers. On this disc that is only
// LightShow_SharedProcessors (one call, main init); no zone ships a per-venue
// <zone>_lightshow_processors, so the shared table is authoritative [C qb.pak,
// all_dis.txt ~44584]. Per processor: screnter / screvent (screxit is a no-op
// for all) --
//   default_generic : GenericMood_Enter (cycle save=true) / GenericMood_Event (cycle on snapshotchange)
//   Poor_Generic    : Poor_Enter     (cycle save=true)    / Poor_Event     (waits, no cycle)
//   Blackout_Generic: Blackout_Enter (cycle save=false)   / Blackout_Event (empty)
//   Flare_Generic   : Flare_Enter    (cycle save=false)   / Flare_Event    (empty)
//   Strobe_Generic  : GenericMood_Enter / GenericMood_Event  <-- NOT the orphaned
//       LightShow_Strobe_* scripts (nothing references them); strobe is an
//       ordinary cycling mood driven by the snapshotchange notes 57/58, and
//       note 60 `strobetoggle` is consumed by no processor [C].
//   Proc_*Intro     : GenericMood_Enter / nullscript (cycle on enter, not on snapshotchange)
// A mode entry with no `processor` defaults to default_generic. The port read a
// `processor` inline from the mapping entry and did nothing when absent -- so
// the medium/good generic moods (intro, wash, prelude, exposition, verse,
// chorus, ...; ~40 % of every zone's entries carry snapshots but no processor)
// never played -- and modelled Strobe_Generic from the orphaned scripts.
// GD10_LIGHTSHOW_LEGACY=1 restores that behaviour.
bool LightShow::cyclesOnSnapshotChange() const {
    // screvent == LightShow_GenericMood_Event: default_generic (the no-processor
    // fallback) and Strobe_Generic only.
    return processor_ == 0 || processor_ == K("default_generic") || processor_ == K("Strobe_Generic");
}

void LightShow::enterMood(uint32_t mood, double t) {
    static const bool legacy = SDL_getenv("GD10_LIGHTSHOW_LEGACY") != nullptr;
    const uint32_t oldProc = processor_;
    mood_ = mood;
    setStateFlags();
    const qb::Struct* e = structOf(modeEntry());
    processor_ = e ? keyOf(e->find(K("processor"))) : 0;
    strobeOn_ = false;
    if (legacy) {
        if (oldProc == K("Strobe_Generic")) timeEnabled_ = true;  // LightShow_Strobe_Exit: SetTime enable
        if (processor_ == K("Strobe_Generic")) {
            timeEnabled_ = false;  // LightShow_Strobe_Enter: SetTime enable=false
            cycle(t, false);
        } else if (processor_ == K("Blackout_Generic") || processor_ == K("Flare_Generic")) {
            cycle(t, false);
        } else if (processor_) {
            cycle(t, true);
        }
        return;
    }
    // Every processor's enter cycles to a snapshot; Blackout / Flare keep it as
    // transient (save=false), the rest (GenericMood / Poor, and the
    // no-processor default_generic) save it as the mood's current snapshot.
    if (processor_ == K("Blackout_Generic") || processor_ == K("Flare_Generic"))
        cycle(t, false);  // Blackout_Enter / Flare_Enter
    else
        cycle(t, true);  // GenericMood_Enter / Poor_Enter (Strobe_Generic / Proc_*Intro map here too)
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
            // Notes 57/58. GenericMood_Event cycles on it; Poor_Event waits and
            // Blackout / Flare events are empty [C]. default_generic (the
            // no-processor fallback) and Strobe_Generic both have screvent =
            // LightShow_GenericMood_Event.
            static const bool legacy = SDL_getenv("GD10_LIGHTSHOW_LEGACY") != nullptr;
            if (legacy) {
                // Old guard: required an inline processor and excluded Strobe.
                if (processor_ && processor_ != K("Poor_Generic") && processor_ != K("Blackout_Generic") &&
                    processor_ != K("Flare_Generic") && processor_ != K("Strobe_Generic"))
                    cycle(t, true);
            } else if (cyclesOnSnapshotChange()) {
                cycle(t, true);
            }
        } else if (event == K("strobetoggle")) {
            // Note 60. No shipped processor consumes it (every screvent is
            // GenericMood / Poor / Blackout / Flare / nullscript; the
            // LightShow_Strobe_* scripts are orphaned), so the real game ignores
            // it [C]; kept only under GD10_LIGHTSHOW_LEGACY.
            static const bool legacy = SDL_getenv("GD10_LIGHTSHOW_LEGACY") != nullptr;
            if (legacy && processor_ == K("Strobe_Generic")) {
                strobeOn_ = !strobeOn_;
                if (strobeOn_ && savedSnapshot_) play(savedSnapshot_, t);
                else cycle(t, false);
            }
        } else if (scr == K("LightShow_SpotlightColor")) {
            // The follow-spots take the colour's spotlight snapshot (keyed
            // red..white/off in mapping.spotlight_snapshots), applied to the
            // spot lights only -- not the mood snapshot [C scripts\fx\lightshow.qb].
            spotColor_ = params ? keyOf(params->find(K("color"))) : 0;
            playSpotlight(spotColor_, t);
        } else if (scr == K("LightShow_EnableSpotlights")) {
            spotOn_ = params && keyOf(params->find(K("enabled"))) == K("true");
            // Enabling with no colour set yet defaults to white [I: spotlights_White].
            playSpotlight(spotOn_ ? (spotColor_ ? spotColor_ : K("white")) : K("off"), t);
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

HousingState LightShow::housingAt(const HousingTarget& h, double t) const {
    if (h.blend <= 0 || t >= h.start + h.blend * 1000.0) return h.to;
    const float s = float(std::clamp((t - h.start) / (h.blend * 1000.0), 0.0, 1.0));
    auto mix = [s](float a, float b) { return a + (b - a) * s; };
    HousingState r = h.to;
    for (size_t k = 0; k < 3; ++k) {
        r.pos[k] = mix(h.from.pos[k], h.to.pos[k]);
        r.volumeColor[k] = mix(h.from.volumeColor[k], h.to.volumeColor[k]);
        r.projectorColor[k] = mix(h.from.projectorColor[k], h.to.projectorColor[k]);
    }
    // nlerp of the rotation (shortest way).
    Quat a = h.from.rot, b = h.to.rot;
    const float d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (d < 0) b = {-b[0], -b[1], -b[2], -b[3]};
    float len = 0;
    for (size_t k = 0; k < 4; ++k) r.rot[k] = mix(a[k], b[k]), len += r.rot[k] * r.rot[k];
    len = std::sqrt(std::max(len, 1e-12f));
    for (float& c : r.rot) c /= len;
    r.startRadius = mix(h.from.startRadius, h.to.startRadius), r.endRadius = mix(h.from.endRadius, h.to.endRadius);
    r.innerRadius = mix(h.from.innerRadius, h.to.innerRadius), r.range = mix(h.from.range, h.to.range);
    r.density = mix(h.from.density, h.to.density);
    return r;
}

bool LightShow::housing(uint32_t node, double ms, HousingState& out) const {
    auto it = housings_.find(node);
    if (it == housings_.end()) return false;
    out = housingAt(it->second, ms);
    return true;
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
