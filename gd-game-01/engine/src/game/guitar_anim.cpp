// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/guitar_anim.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "scene/character.h"

namespace {

const DataNode* entry(const DataNode& list, const std::string& key) {
    for (const DataNode& c : list.children)
        if (c.isList() && !c.children.empty() && c.children[0].isText() && c.children[0].s == key) return &c;
    return nullptr;
}

// An int, or an array picked with RandomInt(1, size) (FUN_00115118: element 0
// is never chosen).
int pickValue(const DataNode& v, std::mt19937& rng) {
    if (v.type == DataNode::Type::Int) return v.i;
    if (v.isList() && !v.children.empty()) {
        size_t n = v.children.size();
        size_t i = n > 1 ? std::uniform_int_distribution<size_t>(1, n - 1)(rng) : 0;
        return v.children[i].type == DataNode::Type::Int ? v.children[i].i : 0;
    }
    return 0;
}

// FUN_00116018: an event ends `gap` beats before the next one starts when they touch.
void trim(std::vector<GuitarHands::Event>& list, float gap) {
    for (size_t i = 1; i < list.size(); ++i) {
        GuitarHands::Event& prev = list[i - 1];
        const GuitarHands::Event& next = list[i];
        if (next.start - gap <= prev.end && prev.end - prev.start > gap) prev.end = next.start - gap;
    }
}

// The map symbol in effect at `beat` (HandMap_ / StrumMap_ text events).
const std::string* mapAt(const std::vector<HandMapEvent>& maps, float beat) {
    const std::string* out = nullptr;
    for (const HandMapEvent& m : maps)
        if (m.beat <= beat) out = &m.symbol;
    return out;
}

float smooth(float t) { return 0.5f - 0.5f * std::cos(3.14159265f * t); }

}  // namespace

GuitarHands::GuitarHands(const Song& song, const Chart& chart, const DataNode& charsys) {
    // hand_events: note -> clip, ((40 60) fret_pos 40) handled by the chart.
    std::map<int, std::string> events;
    if (const DataNode* he = charsys.find("hand_events"))
        for (size_t k = 1; k < he->children.size(); ++k) {
            const DataNode& e = he->children[k];
            if (e.isList() && e.children.size() >= 2 && e.children[0].type == DataNode::Type::Int && e.children[1].isText())
                events[e.children[0].i] = e.children[1].s;
        }
    const DataNode* fingerMaps = charsys.find("hand_finger_mapping");
    const DataNode* strumMaps = charsys.find("hand_strum_mapping");

    for (const FretPosEvent& f : song.fretPositions) fret_.push_back({f.start, f.end, f.value, {}});
    bool genFret = fret_.empty();

    // FUN_00114dd8: one finger and one strum event per gem (the last gem is skipped).
    std::string handMap = "HandMap_Default", strumMap = "StrumMap_Default";
    const auto& gems = chart.gems;
    for (size_t i = 0; i + 1 < gems.size(); ++i) {
        const Gem& g = gems[i];
        float start = float(g.tick) / 480.0f, end = start + float(g.durationTicks) / 480.0f;
        bool chord = g.fretCount() > 1;
        int low = std::countr_zero(unsigned(g.frets));
        if (const std::string* m = mapAt(song.handMaps, start)) handMap = *m;
        if (const std::string* m = mapAt(song.strumMaps, start)) strumMap = *m;
        bool isLong = end - start > 0.25f;
        // hand_strum_mapping: (map (single (short n) (long n)) (chord ...)), positional.
        int strumNote = 26;
        if (const DataNode* sm = strumMaps ? entry(*strumMaps, strumMap) : nullptr)
            if (const DataNode* kind = entry(*sm, chord ? "chord" : "single"); kind && kind->children.size() > (isLong ? 2u : 1u)) {
                const DataNode& e = kind->children[isLong ? 2 : 1];
                if (e.isList() && e.children.size() > 1) strumNote = pickValue(e.children[1], rng_);
            }
        // hand_finger_mapping: (map (short (gems g anim n) (gems (a b) anim n) ...) (long ...)).
        int fingerNote = 83;
        if (const DataNode* fm = fingerMaps ? entry(*fingerMaps, handMap) : nullptr)
            if (const DataNode* len = entry(*fm, isLong ? "long" : "short"))
                for (size_t k = 1; k < len->children.size(); ++k) {
                    // (gems g anim n) / (gems (a b) anim n): flat symbol-value pairs.
                    const DataNode& e = len->children[k];
                    const DataNode *gv = nullptr, *av = nullptr;
                    for (size_t n = 0; n + 1 < e.children.size(); ++n) {
                        if (e.children[n].isText() && e.children[n].s == "gems") gv = &e.children[n + 1];
                        if (e.children[n].isText() && e.children[n].s == "anim") av = &e.children[n + 1];
                    }
                    if (!gv || !av) continue;
                    bool match = false;
                    if (gv->type == DataNode::Type::Int) match = g.frets == (1u << (gv->i - 1));  // FUN_001154c0
                    else if (gv->isList() && gv->children.size() >= 2)
                        match = (g.frets >> (gv->children[0].i - 1) & 1) && (g.frets >> (gv->children[1].i - 1) & 1);
                    if (match) {
                        fingerNote = pickValue(*av, rng_);
                        break;
                    }
                }
        finger_.push_back({start, end, 0, events.count(fingerNote) ? events[fingerNote] : "finger_open"});
        strum_.push_back({start - 0.0625f, start + 0.1875f, 0, events.count(strumNote) ? events[strumNote] : "strum_pluck_short"});
        if (genFret && chord) fret_.push_back({start, end, low * 3, {}});
    }
    trim(fret_, 0.22f);
    trim(finger_, 0.12f);
}

const GuitarHands::Event* GuitarHands::next(float beat, const std::vector<Event>& list) {
    for (const Event& e : list)
        if (e.start >= beat) return &e;
    return nullptr;
}

// FUN_002868c8: bone_fret glides on an S curve from the previous spot to the
// next event's spot, arriving at its start, and holds until its end.
void GuitarHands::place(float beat, bool active, Character& c) {
    if (active && beat >= holdEnd_)
        if (const Event* ev = next(beat, fret_)) {
            holdEnd_ = ev->end;
            prev_ = cur_;
            cur_ = std::clamp(ev->value, 0, 19);
            moveStart_ = beat;
            moveTarget_ = ev->start;
        }
    float t = moveTarget_ > moveStart_ ? std::clamp((beat - moveStart_) / (moveTarget_ - moveStart_), 0.0f, 1.0f) : 1.0f;
    float s = smooth(t);
    auto spot = [&](int i) -> const Transformable* {
        char name[32];
        std::snprintf(name, sizeof name, "spot_neck_fret%02d.mesh", i + 1);
        const SceneObject* o = c.find(name);
        return o ? o->transformable() : nullptr;
    };
    const Transformable* a = spot(prev_);
    const Transformable* b = spot(cur_);
    if (!a || !b) return;
    Xfm x;
    for (int k = 0; k < 3; ++k) x.pos[size_t(k)] = (1 - s) * a->world.pos[size_t(k)] + s * b->world.pos[size_t(k)];
    // Rows from the previous spot: Y normalised, X = Y x Z, Z = X x Y.
    const auto& r = a->world.rot;
    float y[3] = {r[3], r[4], r[5]}, z[3] = {r[6], r[7], r[8]};
    auto norm = [](float* v) {
        float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 0)
            for (int k = 0; k < 3; ++k) v[k] /= l;
    };
    norm(y);
    float xr[3] = {y[1] * z[2] - y[2] * z[1], y[2] * z[0] - y[0] * z[2], y[0] * z[1] - y[1] * z[0]};
    norm(xr);
    float zr[3] = {xr[1] * y[2] - xr[2] * y[1], xr[2] * y[0] - xr[0] * y[2], xr[0] * y[1] - xr[1] * y[0]};
    x.rot = {xr[0], xr[1], xr[2], y[0], y[1], y[2], zr[0], zr[1], zr[2]};
    c.setFretBone(x);
}

void GuitarHands::update(float beat, bool active, Character& c) {
    if (!c.hasHands()) return;
    if (!fret_.empty()) place(beat, active, c);
    using C = Character;
    // FUN_00286d88: the finger clip blends in over its blend width before the
    // note and loops; finger_open when the next note is over a beat away.
    if (!finger_.empty() && beat >= nextL_)
        if (const Event* ev = next(beat, finger_)) {
            if (!active || beat <= ev->start - 1.0f) {
                if (c.handHead(C::kLeftHand) != "finger_open") c.handPlay(C::kLeftHand, "finger_open", C::kAnimNow | C::kAnimLoop, 0, false);
                nextL_ = ev->start - 0.25f;
            } else {
                nextL_ = ev->end;
                if (c.handHead(C::kLeftHand) != ev->clip)
                    c.handPlay(C::kLeftHand, ev->clip, C::kAnimLoop,
                               std::max(0.0f, ev->start - c.handBlendWidth(ev->clip) - beat), true);
            }
        }
    // FUN_00286bc8: every strum re-triggers; strum_open when the next one is over 4 beats away.
    if (!strum_.empty() && beat >= nextR_)
        if (const Event* ev = next(beat, strum_)) {
            if (!active || beat <= ev->start - 4.0f) {
                if (c.handHead(C::kRightHand) != "strum_open") c.handPlay(C::kRightHand, "strum_open", C::kAnimNow, 0, false);
                nextR_ = ev->start - 0.25f;
            } else {
                nextR_ = ev->end;
                c.handPlay(C::kRightHand, ev->clip, 0, std::max(0.0f, ev->start - c.handBlendWidth(ev->clip) - beat), true);
            }
        }
}
