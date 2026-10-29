// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/light_presets.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "game/world.h"
#include "scene/anim.h"
#include "scene/world_objects.h"
#include "script/tasks.h"

namespace {

std::string text(Interp& in, const Value& v) {
    const Value e = in.evalValue(v);
    return e.type == Value::Type::Object && e.object ? e.object->name() : e.str();
}

// A symbol, or the symbols of a list ("()" is an empty list).
bool symbols(Interp& in, const Value& raw, std::vector<std::string>& out, bool& isList) {
    const Value v = in.evalValue(raw);
    out.clear();
    isList = false;
    if (v.type == Value::Type::Array && v.array) {
        isList = true;
        for (const Value& e : *v.array) {
            const std::string s = text(in, e);
            if (!s.empty()) out.push_back(s);
        }
        return true;
    }
    if (v.isText()) {
        const std::string s = text(in, v);
        if (!s.empty()) out.push_back(s);
        return true;
    }
    return false;
}

float lerp(float a, float b, float t) { return a + (b - a) * t; }

template <size_t N> void lerpArr(std::array<float, N>& a, const std::array<float, N>& b, float t) {
    for (size_t i = 0; i < N; ++i) a[i] = lerp(a[i], b[i], t);
}

}  // namespace

namespace lighting {

bool parseEvent(const std::string& raw, std::vector<std::string>& adjectives) {
    std::string t;
    for (char c : raw) t += char(std::tolower(static_cast<unsigned char>(c)));
    size_t k = t.find_first_not_of(" \t[");
    if (k == std::string::npos || t.compare(k, 8, "lighting") != 0) return false;
    k += 8;
    adjectives.clear();
    std::string word;
    for (; k < t.size(); ++k) {
        const char c = t[k];
        if (c == ' ' || c == '\t' || c == '(' || c == ')' || c == ']') {
            if (!word.empty()) adjectives.push_back(word);
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) adjectives.push_back(word);
    return true;
}

}  // namespace lighting

LightPresets::LightPresets(World& world, uint32_t seed) : world_(world), rng_(seed ? seed : 1) {
    for (const scene::LightPreset* p : world_.venue().all<scene::LightPreset>()) {
        if (!p->decoded || p->keyframes.empty()) continue;
        all_.push_back(p);
        byCategory_[p->category].push_back(p);
    }
    world_.addNatives([this](Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) { return native(in, msg, args, r); });
}

LightPresets::~LightPresets() = default;

uint32_t LightPresets::random() {
    rng_ ^= rng_ << 13, rng_ ^= rng_ >> 17, rng_ ^= rng_ << 5;
    return rng_;
}

double LightPresets::now(const scene::LightPreset& p) const {
    return World::unitsOf(int(p.anim.rate)) == kTaskBeats ? world_.beats() : world_.seconds();
}

float LightPresets::frameOf(const scene::LightPreset& p, double start) const {
    return float((now(p) - start) * double(World::framesPerUnit(int(p.anim.rate))));
}

// ---- natives -----------------------------------------------------------------

bool LightPresets::native(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) {
    r = Value::integer(0);
    if (msg == "set_lighting") {
        if (ignoreEvents_) return true;
        std::vector<std::string> cats, adjs;
        bool catList = false, adjList = false;
        if (!args.empty() && symbols(in, args[0], cats, catList) && !cats.empty()) categories_ = cats;
        const bool hasAdj = args.size() > 1 && symbols(in, args[1], adjs, adjList) && adjList;
        const bool delayed = args.size() > 2 ? in.evalValue(args[2]).truthy() : true;
        setLighting(categories_, hasAdj ? &adjs : nullptr, delayed);
        return true;
    }
    if (msg == "set_excitement") {
        if (!ignoreEvents_ && !args.empty()) setExcitement(in.evalValue(args[0]).integerValue());
        return true;
    }
    if (msg == "lighting") {  // {world lighting (adjectives)}: the editor's tests
        std::vector<std::string> adjs;
        bool list = false;
        if (!args.empty()) symbols(in, args[0], adjs, list);
        setLighting(categories_, &adjs, false);
        return true;
    }
    if (msg == "cycle_lighting" || msg == "next_lighting") {
        if (!all_.empty()) start(all_[cycle_++ % all_.size()]);
        if (current_) r = Value::symbol(current_->name);
        return true;
    }
    if (msg == "current_lighting") {
        r = current_ ? Value::symbol(current_->name) : Value::integer(0);
        return true;
    }
    if (msg == "lighting_next_keyframe" || msg == "lighting_prev_keyframe" || msg == "lighting_first_keyframe") {
        if (!ignoreEvents_ && current_) queueStep(msg == "lighting_next_keyframe" ? 0 : msg == "lighting_prev_keyframe" ? 1 : 2);
        return true;
    }
    if (msg == "toggle_lighting_events") {
        ignoreEvents_ = !ignoreEvents_;
        r = Value::integer(ignoreEvents_ ? 1 : 0);
        return true;
    }
    if (msg == "ignore_lighting_events") {
        ignoreEvents_ = args.empty() || in.evalValue(args[0]).truthy();
        return true;
    }
    return false;
}

// 0x8223FC58: a step 4 beats from now (the note's time).
void LightPresets::queueStep(int cmd) {
    steps_.push_back({cmd, world_.beats() + 4.0});
    while (steps_.size() > 64) steps_.pop_front();
}

// ---- candidates ----------------------------------------------------------------

int LightPresets::score(const scene::LightPreset& p) const {
    const int n = int(categories_.size()), m = int(adjectives_.size());
    int s = 0;
    for (int i = 0; i < n; ++i)
        if (categories_[size_t(i)] == p.category) {
            s = n - i;
            break;
        }
    for (int j = 0; j < m; ++j)
        if (std::find(p.adjectives.begin(), p.adjectives.end(), adjectives_[size_t(j)]) != p.adjectives.end()) s += (m - j) * n;
    for (const std::string& a : p.adjectives)
        if (std::find(adjectives_.begin(), adjectives_.end(), a) == adjectives_.end()) --s;
    return s;
}

void LightPresets::rebuild() {
    candidates_.clear();
    for (const std::string& c : categories_) {
        auto it = byCategory_.find(c);
        if (it == byCategory_.end()) continue;
        for (const scene::LightPreset* p : it->second) {
            if (p->minExcitement > excitement_ || excitement_ > p->maxExcitement) continue;
            if (!adjectives_.empty() && !p->adjectives.empty()) {
                bool any = false;
                for (const std::string& a : p->adjectives) any = any || std::find(adjectives_.begin(), adjectives_.end(), a) != adjectives_.end();
                if (!any) continue;
            }
            candidates_.push_back(p);
        }
    }
    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [&](const scene::LightPreset* a, const scene::LightPreset* b) { return score(*a) > score(*b); });
    if (candidates_.empty()) return;
    const int best = score(*candidates_.front());
    size_t keep = 1;
    while (keep < candidates_.size() && score(*candidates_[keep]) >= best) ++keep;
    candidates_.resize(keep);
    for (size_t i = candidates_.size(); i > 1; --i) std::swap(candidates_[i - 1], candidates_[random() % i]);
}

void LightPresets::setLighting(const std::vector<std::string>& categories, const std::vector<std::string>* adjectives, bool delayed) {
    if (&categories != &categories_ && !categories.empty()) categories_ = categories;
    if (adjectives) adjectives_ = *adjectives;
    rebuild();
    choose(delayed);
}

void LightPresets::setExcitement(int ex) {
    excitement_ = ex;
    rebuild();
    if (!candidates_.empty() && candidates_.front() != current_) switchTo(candidates_.front());
}

// 0x822629F8 / 0x822627E8: delayed so that the fade ends 4 beats from now.
void LightPresets::choose(bool delayed) {
    if (candidates_.empty()) return;
    const scene::LightPreset* p = candidates_.front();
    if (!delayed && p == current_) return;
    const double delay = delayed ? 4.0 - double(p->fadeIn) / 480.0 : 0.0;
    if (delay <= 0) {
        switchTo(p);
        return;
    }
    double at = world_.beats() + delay;
    // A later pending switch keeps its place; this one lands after it [I].
    if (!pending_.empty() && at < pending_.back().beat) at = std::min(pending_.back().beat, at + double(p->fadeIn) / 480.0);
    pending_.push_back({p, at});
}

void LightPresets::start(const scene::LightPreset* p) {
    pending_.clear();
    if (p) switchTo(p);
}

void LightPresets::switchTo(const scene::LightPreset* p) {
    previous_ = current_;
    previousStart_ = currentStart_;
    current_ = p;
    currentStart_ = now(*p);
    currentStartSeconds_ = world_.seconds();
    Manual& m = manual_[p];
    m = Manual{};
    m.startBeat = world_.beats();
    ++switches_;
    // StartAnim fires the preset's trigger, an EventTrigger (small2's neon
    // presets: neon_all_on.trig...) [I: fired directly, not as a message].
    if (!p->trigger.empty())
        if (auto* t = world_.venue().find<scene::EventTrigger>(p->trigger)) world_.trigger(t);
    if (onSwitch) onSwitch("preset " + p->name + " (" + p->category + ")");
    if (world_.log) SDL_Log("world: lighting %s (%s)", p->name.c_str(), p->category.c_str());
}

// ---- playback --------------------------------------------------------------------

void LightPresets::keyframeAt(const scene::LightPreset& p, float f, int& a, int& b, float& t) {
    const int n = int(p.keyframes.size());
    a = b = 0, t = 0;
    if (n == 0) return;
    if (p.manual) {
        Manual& m = manual_[&p];
        // Steps queued before the preset started are dropped; a step starts
        // its fade at its beat - fade_out / 480 (the current keyframe's).
        while (!steps_.empty() && steps_.front().beat <= m.startBeat) steps_.pop_front();
        while (!steps_.empty()) {
            const Step s = steps_.front();
            const float fade = p.keyframes[size_t(std::clamp(m.index, 0, n - 1))].fadeOut;
            if (world_.beats() < s.beat - double(fade) / 480.0) break;
            m.prev = m.index;
            if (s.cmd == 2) m.index = 0;
            else if (s.cmd == 0) m.index = m.index + 1 >= n ? (p.looping ? 0 : n - 1) : m.index + 1;
            else m.index = m.index - 1 < 0 ? (p.looping ? n - 1 : 0) : m.index - 1;
            m.blendStart = f;
            m.blendLen = world_.beats() >= s.beat ? 0.0f : float((s.beat - world_.beats()) * 480.0);
            steps_.pop_front();
        }
        a = std::clamp(m.index, 0, n - 1);
        b = a;
        t = 0;
        if (m.prev >= 0 && m.prev < n && m.blendLen > 0) {
            const float w = std::clamp((f - m.blendStart) / m.blendLen, 0.0f, 1.0f);
            if (w < 1) a = m.prev, t = w;
        }
        return;
    }
    // Automatic (0x822399A8): keyframe i starts at the sum of the earlier
    // (duration + fade_out), holds its duration, then fades to the next.
    float total = 0;
    for (const auto& k : p.keyframes) total += k.duration + k.fadeOut;
    if (f <= 0 || total <= 0) return;
    if (p.looping) f = std::fmod(f, total);
    float s = 0;
    for (int i = 0; i < n; ++i) {
        const auto& k = p.keyframes[size_t(i)];
        const bool last = i == n - 1;
        if (f <= s + k.duration || (last && (!p.looping || k.fadeOut <= 0))) {
            a = b = i;
            return;
        }
        if (f <= s + k.duration + k.fadeOut || last) {
            a = i, b = last ? 0 : i + 1;
            t = k.fadeOut > 0 ? std::clamp((f - s - k.duration) / k.fadeOut, 0.0f, 1.0f) : 1.0f;
            return;
        }
        s += k.duration + k.fadeOut;
    }
}

void LightPresets::update() {
    // Due switches (Poll's queue, in beats).
    const scene::LightPreset* due = nullptr;
    while (!pending_.empty() && pending_.front().beat <= world_.beats()) {
        due = pending_.front().preset;
        pending_.pop_front();
    }
    if (due) switchTo(due);
    if (!current_) return;
    // The current preset, then the previous one over it while fading in.
    int a, b;
    float t;
    keyframeAt(*current_, frameOf(*current_, currentStart_), a, b, t);
    apply(*current_, a, b, t, 1.0f);
    const float fadeUnits = current_->fadeIn / World::framesPerUnit(int(current_->anim.rate));
    if (previous_ && previous_ != current_ && fadeUnits > 0) {
        const float u = float((now(*current_) - currentStart_) / double(fadeUnits));
        if (u < 1) {
            keyframeAt(*previous_, frameOf(*previous_, previousStart_), a, b, t);
            apply(*previous_, a, b, t, 1.0f - std::max(0.0f, u));
        } else {
            previous_ = nullptr;
        }
    }
}

// 0x82239BB8: each list entry blended into its object's state by `weight`.
void LightPresets::apply(const scene::LightPreset& p, int a, int b, float t, float weight) {
    if (p.keyframes.empty()) return;
    const auto& ka = p.keyframes[size_t(a)];
    const auto& kb = p.keyframes[size_t(b)];
    Venue& v = world_.venue();
    gfx::ScenePose& pose = world_.pose();
    for (size_t i = 0; i < p.spotlights.size() && i < ka.spots.size() && i < kb.spots.size(); ++i) {
        auto* s = v.find<scene::Spotlight>(p.spotlights[i]);
        if (!s || !s->animateFromPreset) continue;
        const auto& ea = ka.spots[i];
        const auto& eb = kb.spots[i];
        World::SpotState& st = world_.spotState(s);
        if (st.rotation == std::array<float, 4>{}) {  // first touch: the spotlight's own
            st.intensity = s->intensity, st.color = s->color;
            st.rotation = {0, 0, 0, 1};
            st.target = s->target;
            st.flare = s->flareEnabled;
        }
        const float in = lerp(ea.intensity, eb.intensity, t);
        std::array<float, 4> col = ea.color;
        lerpArr(col, eb.color, t);
        const scene::Quat q = scene::slerp(ea.rotation, eb.rotation, t);
        st.intensity = lerp(st.intensity, in, weight);
        lerpArr(st.color, col, weight);
        st.rotation = scene::slerp(st.rotation, q, weight);
        if (weight >= 1) {
            const auto& e = t < 1 ? ea : eb;
            st.target = e.target;
            st.flare = e.flare;
        }
    }
    for (size_t i = 0; i < p.environs.size() && i < ka.environs.size() && i < kb.environs.size(); ++i) {
        auto* env = v.find<scene::Environ>(p.environs[i]);
        if (!env || !env->animateFromPreset) continue;
        const auto& ea = ka.environs[i];
        const auto& eb = kb.environs[i];
        auto& st = pose.envState(env);
        if (!st.hasAmbient) st.ambient = env->ambientColor;
        if (!st.hasFogColor) st.fogColor = env->fogColor;
        if (!st.hasFogRange) st.fogStart = env->fogStart, st.fogEnd = env->fogEnd;
        // Fog off pushes the range out (0x82238920) [I: far enough to vanish].
        auto range = [](const scene::LightPreset::EnvEntry& e, float& s0, float& s1) {
            s0 = e.fogEnable ? e.fogStart : 1e5f;
            s1 = e.fogEnable ? e.fogEnd : 1e5f + 1;
        };
        float as, ae, bs, be;
        range(ea, as, ae);
        range(eb, bs, be);
        std::array<float, 4> amb = ea.ambient, fog = ea.fogColor;
        lerpArr(amb, eb.ambient, t);
        lerpArr(fog, eb.fogColor, t);
        st.hasAmbient = st.hasFogColor = st.hasFogRange = st.hasFogEnable = true;
        lerpArr(st.ambient, amb, weight);
        lerpArr(st.fogColor, fog, weight);
        st.fogStart = lerp(st.fogStart, lerp(as, bs, t), weight);
        st.fogEnd = lerp(st.fogEnd, lerp(ae, be, t), weight);
        st.fogEnable = st.fogStart < 1e5f * 0.99f || ea.fogEnable || eb.fogEnable;
    }
    for (size_t i = 0; i < p.lights.size() && i < ka.lights.size() && i < kb.lights.size(); ++i) {
        auto* l = v.find<scene::Light>(p.lights[i]);
        if (!l) continue;
        const auto& ea = ka.lights[i];
        const auto& eb = kb.lights[i];
        if (l->animateColorFromPreset) {
            auto& st = pose.lightState(l);
            if (!st.hasColor) st.color = l->color, st.range = l->range;
            std::array<float, 4> c = ea.color;
            lerpArr(c, eb.color, t);
            st.hasColor = st.hasRange = true;
            lerpArr(st.color, c, weight);
            st.range = lerp(st.range, lerp(ea.range, eb.range, t), weight);
        }
        if (l->animatePositionFromPreset) {
            scene::Xfm x = pose.local(l);
            std::array<float, 3> pos = ea.position;
            lerpArr(pos, eb.position, t);
            lerpArr(x.pos, pos, weight);
            scene::Quat cur = scene::slerp(ea.rotation, eb.rotation, t);
            if (weight < 1) {
                // Blend from the current rotation [I: slerp as the spots].
                std::array<float, 9> m{};
                scene::quatToRot(cur, m);
                for (size_t k = 0; k < 9; ++k) x.rot[k] = lerp(x.rot[k], m[k], weight);
            } else {
                scene::quatToRot(cur, x.rot);
            }
            pose.setLocal(l, x);
        }
    }
}
