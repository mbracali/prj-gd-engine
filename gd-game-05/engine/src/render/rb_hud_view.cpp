// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/rb_hud_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>

#include "data/game_data.h"
#include "render/anim_rate.h"
#include "render/draw_order.h"
#include "render/renderer.h"
#include "render/text_layout.h"
#include "render/ui_view.h"
#include "scene/milo.h"
#include "scene/ui_objects.h"
#include "scene/world_objects.h"

namespace {

constexpr float kFps = 30;  // the triggers' anims (rate k30_fps) [C data]; every rate 30 [I]

bool isPropAnim(const scene::Object* o) { return o && o->cls == "PropAnim"; }

// A PropAnim's frame range end (its RndAnimatable part) [C data].
float propAnimEnd(const scene::Object& a) {
    const Bytes& r = a.record;
    size_t p = 8;
    auto u32 = [&](size_t at) { return at + 4 <= r.size() ? uint32_t(r[at]) << 24 | uint32_t(r[at + 1]) << 16 | uint32_t(r[at + 2]) << 8 | r[at + 3] : 0u; };
    p += 4 + u32(p);
    p += 4 + 1 + 4;
    const uint32_t b = u32(p);
    float f;
    std::memcpy(&f, &b, 4);
    return std::isfinite(f) ? f : 0.0f;
}

// {localize_separated_int n} (ui/global.dta fmt_score) [C data; the
// separator I: ","].
std::string fmtScore(int n) {
    const std::string d = std::to_string(std::max(0, n));
    std::string out;
    for (size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += ',';
        out += d[i];
    }
    return out;
}

// A property of an object's header (its type's fields: (stars (star1 ...)),
// (level_colors (...))) [C data].
// A plain Object's record (class "Object", not decoded by the loader) is
// its Hmx::Object part alone [C data: the configurations, rev 2].
const scene::DataNode* headerProp(const scene::Object* o, const char* key) {
    if (!o) return nullptr;
    const scene::ObjectHeader* h = &o->header;
    if (!h->hasProps && !o->typed && !o->record.empty()) {
        static std::map<const scene::Object*, scene::ObjectHeader> parsed;
        auto it = parsed.find(o);
        if (it == parsed.end()) {
            scene::ObjectHeader ph;
            try {
                size_t pos = 0;
                ph = scene::readObjectHeaderAt(o->record, pos);
            } catch (const std::exception&) {
            }
            it = parsed.emplace(o, ph).first;
        }
        h = &it->second;
    }
    if (!h->hasProps) return nullptr;
    const auto& c = h->props.children;
    for (size_t i = 0; i + 1 < c.size(); ++i)
        if (c[i].str == key) return &c[i + 1];
    return nullptr;
}

}  // namespace

RbHudView::RbHudView() = default;
RbHudView::~RbHudView() = default;

const scene::Object* RbHudView::panelObj(const std::string& name) const { return panel_ ? panel_->root().get(name) : nullptr; }

RbTrackView::Config RbHudView::trackConfig() const {
    RbTrackView::Config c;
    auto num = [](const scene::DataNode* n, float& out) {
        if (n) out = n->type == 1 ? n->asFloat() : float(int32_t(n->raw));
    };
    const scene::Object* cfg = panelObj("1_player_wide");
    num(headerProp(cfg, "pitch"), c.pitch), num(headerProp(cfg, "fov"), c.fov);
    num(headerProp(cfg, "y_pos"), c.yPos), num(headerProp(cfg, "z_pos"), c.zPos);
    num(headerProp(cfg, "fade_start"), c.fadeStart), num(headerProp(cfg, "fade_length"), c.fadeLength);
    const scene::Object* self = panel_ && panel_->root().self ? panel_->root().self.get() : nullptr;
    num(headerProp(self, "view_time_easy"), c.viewTimeEasy), num(headerProp(self, "view_time_expert"), c.viewTimeExpert);
    return c;
}

scene::Directory* RbHudView::subdir(const std::string& name) const {
    if (!panel_) return nullptr;
    for (const auto& [owner, d] : panel_->root().subdirs)
        if (owner == name) return d.get();
    return nullptr;
}

const scene::Object* RbHudView::in(const std::string& elem, const std::string& name) const {
    const scene::Directory* d = subdir(elem);
    return d ? d->get(name) : nullptr;
}

void RbHudView::load(const GameData& data, scene::MiloLibrary& library) {
    data_ = &data, lib_ = &library;
    try {
        styles_.load(data);
        locale_.load(data);
    } catch (const std::exception& e) {
        SDL_Log("RB HUD: the labels' styles / locale: %s", e.what());
    }
    panel_ = &library.load("track/gen/trackpanel.milo_xbox");
    // The HUD's materials name track_shared.milo's textures (each element
    // milo links ../track_shared.milo) [C data: the RndDir records' subdir].
    try {
        scene::Milo& shared = library.load("track/gen/track_shared.milo_xbox");
        std::function<void(scene::Directory&)> link = [&](scene::Directory& d) {
            if (std::find(d.linked.begin(), d.linked.end(), &shared.root()) == d.linked.end()) d.linked.push_back(&shared.root());
            for (auto& [o, s] : d.subdirs)
                if (s) link(*s);
        };
        link(panel_->root());
    } catch (const std::exception& e) {
        SDL_Log("RB HUD: track_shared.milo: %s", e.what());
    }
    auto* self = panel_->root().self ? panel_->root().self->as<scene::DirObj>() : nullptr;
    cam_ = self && self->hasPanel && !self->panel.cam.empty() ? panel_->root().get(self->panel.cam) : nullptr;
    if (!cam_) cam_ = panel_->root().get("Cam.cam");
    if (!cam_ || !cam_->as<scene::Cam>()) throw std::runtime_error("trackpanel: no camera");
    // Each inline element's loose objects hang from the panel object that
    // holds it (RndDir proxies, as qp_endgame's players).
    for (const auto& [owner, d] : panel_->root().subdirs)
        if (d)
            if (const scene::Object* o = panel_->root().get(owner)) pose_.setDirBase(d.get(), o);
    // Drawn elsewhere: the gem tracks (RbTrackView), the vocals' HUD
    // (VocalTrackView), the guitar / bass effect selectors, the tutorials'
    // pop-ups and the vocalist volume [I: not ported].
    if (SDL_getenv("GD05_HUDLOG"))
        for (const auto& [owner, d] : panel_->root().subdirs) {
            auto* ds = d && d->self ? d->self->as<scene::DirObj>() : nullptr;
            SDL_Log("RB HUD: element %s environ '%s'", owner.c_str(), ds && ds->hasRnd ? ds->rnd.environment.c_str() : "-");
        }
    // The one-player meter, the tracks' reference (trackRects).
    applyConfiguration("1_player_wide");
    setCamera();
    if (float cx, cy, w; meterBox(0, cx, cy, w)) refMeter_ = {cx, cy, w};
    hiddenElems_ = {"guitar", "bass", "drum", "vocals", "guitar_effects", "bass_effects", "vocalist_volume",
                    "popup_help_01", "popup_help_02", "popup_help_03"};
    reset(-1e30f);
}

// track_configuration's update (track_paneldir.dta) [C data]: each object
// of `objects` gets its visible and nine xfm values (position, rotation in
// degrees, scale).
void RbHudView::applyConfiguration(const std::string& name) {
    const scene::Object* cfg = panelObj(name);
    if (!cfg) return;
    config_ = name;
    configured_.clear();
    const scene::DataNode* objs = headerProp(cfg, "objects");
    const scene::DataNode* vis = headerProp(cfg, "visibles");
    const scene::DataNode* xf = headerProp(cfg, "xfms");
    if (!objs || !vis || !xf) return;
    for (size_t i = 0; i < objs->children.size(); ++i) {
        const scene::Object* o = panelObj(objs->children[i].str);
        if (!o) continue;
        configured_.push_back(o->name);
        if (i < vis->children.size()) {
            const scene::DataNode& v = vis->children[i];
            pose_.setShowing(o, v.type == 1 ? v.asFloat() != 0 : int32_t(v.raw) != 0);
        }
        if (xf->children.size() < (i + 1) * 9) continue;
        float f[9];
        for (int k = 0; k < 9; ++k) {
            const scene::DataNode& n = xf->children[i * 9 + size_t(k)];
            f[k] = n.type == 1 ? n.asFloat() : float(int32_t(n.raw));
        }
        // set_local_rot x y z (degrees), set_local_scale [I: the Euler order
        // as Hmx's MakeRotMatrix: z, then x, then y].
        const float d = 3.14159265f / 180.0f;
        const float cx = std::cos(f[3] * d), sx = std::sin(f[3] * d), cy = std::cos(f[4] * d), sy = std::sin(f[4] * d),
                    cz = std::cos(f[5] * d), sz = std::sin(f[5] * d);
        // Rows are the local axes (scene::Xfm): R = Rz * Rx * Ry in row-vector form [I].
        const float rz[9] = {cz, sz, 0, -sz, cz, 0, 0, 0, 1};
        const float rx[9] = {1, 0, 0, 0, cx, sx, 0, -sx, cx};
        const float ry[9] = {cy, 0, -sy, 0, 1, 0, sy, 0, cy};
        auto mul = [](const float* a, const float* b, float* o3) {
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) o3[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
        };
        float t[9], m[9];
        mul(ry, rx, t);
        mul(t, rz, m);
        scene::Xfm x;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c) x.rot[size_t(r * 3 + c)] = m[r * 3 + c] * f[6 + r];
        x.pos = {f[0], f[1], f[2]};
        pose_.setLocal(o, x);
    }
}

void RbHudView::reset(float introAtMs) {
    if (!panel_) return;
    pose_.clear();
    for (const auto& [owner, d] : panel_->root().subdirs)
        if (d)
            if (const scene::Object* o = panel_->root().get(owner)) pose_.setDirBase(d.get(), o);
    tasks_.clear();
    animFrame_.clear();
    texts_.clear();
    introAtMs_ = introAtMs;
    introPlayed_ = false;
    lastMs_ = -1e30f;
    seen_ = {};
    scoreShown_.clear();
    bandMultShown_ = 0;
    wonSeen_ = lostSeen_ = codaSeen_ = false;
    excitementSeen_ = -1, starsSeen_ = -1;
    config_.clear();
    // The panel's reset: reset_all.trig, then each element's (track_paneldir.dta
    // reset, turn_everything_off) [C data].
    trigger(&panel_->root(), "reset_all.trig", 0);
    for (const auto& [owner, d] : panel_->root().subdirs)
        if (d && std::find(hiddenElems_.begin(), hiddenElems_.end(), owner) == hiddenElems_.end()) trigger(d.get(), "reset.trig", 0);
    runTasks(0);
}

// ---- EventTriggers, anims, PropAnims (as RbTrackView's, GD05-045) --------

bool RbHudView::trigger(const scene::Directory* d, const std::string& name, float nowMs) {
    const scene::Object* o = d ? d->get(name) : nullptr;
    const scene::EventTrigger* t = o ? o->as<scene::EventTrigger>() : nullptr;
    if (!t) return false;
    trigger(*t, nowMs);
    return true;
}

void RbHudView::trigger(const scene::EventTrigger& t, float nowMs) {
    const scene::Directory* d = t.dir;
    if (!d) return;
    static const bool log = SDL_getenv("GD05_HUDLOG") != nullptr;
    if (log) SDL_Log("RB HUD: %.0f ms %s %s (%d anims, %d shows, %d hides)", double(nowMs), d->name.c_str(), t.name.c_str(), int(t.anims.size()),
                     int(t.shows.size()), int(t.hideDelays.size()));
    for (const auto& a : t.anims) {
        const scene::Object* anim = a.anim.empty() ? nullptr : d->resolve(a.anim);
        if (!anim) continue;
        float from = 0, to = isPropAnim(anim) ? propAnimEnd(*anim) : gfx::ScenePose::endFrame(anim);
        int type = 0;
        if (a.enable) {
            from = a.start, to = a.end;
            if (a.type == "loop") type = 1;
            if (a.type == "dest") {
                auto it = animFrame_.find(anim);
                from = it != animFrame_.end() ? it->second : a.end;
            }
        }
        const float len = std::fabs(to - from);
        const float scale = a.enable && a.period > 0 ? (len > 0 ? len / (a.period * kFps) : 1.0f) : (a.scale != 0 ? std::fabs(a.scale) : 1.0f);
        // The entry's rate (GD05-070): be_ready.trig's be_color_pulse.anim
        // loops 0..1 at k1_fpb, one pulse a beat; at 30 a second it pulsed
        // 30 times a second and the Overdrive meter flickered.
        animate(anim, from, to, type, scale, nowMs + std::max(0.0f, a.delay) * 1000.0f, a.period > 0 || gfx::highwayLegacy() ? gfx::k30Fps : int(a.rate));
    }
    for (const auto& s : t.shows)
        if (const scene::Object* o = d->resolve(s)) pose_.setShowing(o, true);
    for (const auto& [name, delay] : t.hideDelays)
        if (const scene::Object* o = d->resolve(name)) {
            if (delay <= 0) pose_.setShowing(o, false);
            else animate(o, 0, 0, 2, 1, nowMs + delay * 1000.0f);
        }
    for (const std::string& e : t.proxyEvents)
        if (const scene::Object* o = !e.empty() ? d->get(e) : nullptr)
            if (auto* next = o->as<scene::EventTrigger>(); next && next != &t) trigger(*next, nowMs);
}

void RbHudView::animate(const scene::Object* anim, float from, float to, int type, float scale, float startMs, int rate) {
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [&](const Task& k) { return k.anim == anim && k.type != 2; }), tasks_.end());
    Task k;
    k.anim = anim, k.from = from, k.to = to, k.type = type, k.scale = scale, k.t0 = startMs, k.rate = rate;
    tasks_.push_back(k);
    if (type != 2 && startMs <= lastMs_ + 0.5f) setFrame(anim, from);
}

void RbHudView::setFrame(const scene::Object* anim, float frame) {
    if (!anim) return;
    animFrame_[anim] = frame;
    if (isPropAnim(anim)) {
        applyPropAnim(*anim, frame);
    } else if (auto* g = anim->as<scene::Group>()) {
        for (const std::string& n : g->objects)
            if (const scene::Object* o = g->dir ? g->dir->resolve(n) : nullptr; o && o != g)
                if (isPropAnim(o) || o->as<scene::Group>() || o->as<scene::TransAnim>() || o->as<scene::MatAnim>() || o->as<scene::AnimFilter>() ||
                    o->as<scene::MeshAnim>())
                    setFrame(o, frame);
    } else {
        pose_.setFrame(anim, frame);
    }
}

void RbHudView::runTasks(float nowMs) {
    for (size_t k = 0; k < tasks_.size();) {
        const Task t = tasks_[k];
        if (nowMs < t.t0) {
            ++k;
            continue;
        }
        if (t.type == 2) {
            pose_.setShowing(t.anim, false);
            tasks_.erase(tasks_.begin() + long(k));
            continue;
        }
        const float run = float(gfx::animFrames(t.rate, t.t0, nowMs, tempo_)) * t.scale;
        const float len = t.to - t.from;
        bool done = false;
        float f;
        if (t.type == 1 && std::fabs(len) > 1e-4f) f = t.from + std::fmod(run, std::fabs(len)) * (len < 0 ? -1.0f : 1.0f);
        else if (run >= std::fabs(len)) f = t.to, done = true;
        else f = t.from + run * (len < 0 ? -1.0f : 1.0f);
        setFrame(t.anim, f);
        if (done) tasks_.erase(tasks_.begin() + long(k));
        else ++k;
    }
}

// PropAnim rev 5, read as RbTrackView::applyPropAnimFx reads it [C data]:
// a Mat's color / alpha / diffuse_tex, an object's showing; other keys
// are skipped.
bool RbHudView::applyPropAnim(const scene::Object& anim, float frame) {
    const Bytes& r = anim.record;
    size_t p = 0;
    auto u32 = [&](uint32_t& v) {
        if (p + 4 > r.size()) return false;
        v = uint32_t(r[p]) << 24 | uint32_t(r[p + 1]) << 16 | uint32_t(r[p + 2]) << 8 | r[p + 3];
        p += 4;
        return true;
    };
    auto f32 = [&](float& v) {
        uint32_t b;
        if (!u32(b)) return false;
        std::memcpy(&v, &b, 4);
        return true;
    };
    auto str = [&](std::string& out) {
        uint32_t n;
        if (!u32(n) || n > 256 || p + n > r.size()) return false;
        out.assign(reinterpret_cast<const char*>(r.data() + p), n);
        p += n;
        return true;
    };
    uint32_t v, sets;
    std::string type;
    if (!u32(v) || v != 5 || !u32(v) || !str(type) || !u32(v)) return false;
    ++p;
    float fr;
    if (!u32(v) || !f32(fr) || !u32(v) || !u32(sets) || sets > 256) return false;
    for (uint32_t k = 0; k < sets; ++k) {
        std::string target, prop, sub;
        uint32_t ktype, count;
        if (!str(target) || p + 7 > r.size()) return false;
        const uint32_t nodes = uint32_t(r[p + 1]) << 8 | r[p + 2];
        p += 7;
        for (uint32_t i = 0; i < nodes; ++i) {
            uint32_t nt;
            std::string s;
            if (!u32(nt)) return false;
            if (nt == 5 || nt == 0x12) {
                if (!str(s)) return false;
            } else if (!u32(v)) {
                return false;
            }
            if (i == 0) prop = s;
            else if (i == 1) sub = s;
        }
        if (!u32(ktype) || ktype > 3) return true;
        p += 4 * ktype;
        if (!u32(count) || count > 4096) return false;
        const scene::Object* obj = anim.dir ? anim.dir->resolve(target) : nullptr;
        const scene::Mat* mat = obj ? obj->as<scene::Mat>() : nullptr;
        if (ktype == 2) {
            std::string pick;
            for (uint32_t i = 0; i < count; ++i) {
                std::string name;
                float kf;
                if (!str(name) || !f32(kf)) return false;
                if (i == 0 || kf <= frame + 1e-3f) pick = name;
            }
            if (mat && prop == "diffuse_tex")
                if (const scene::Tex* t = anim.dir->resolve<scene::Tex>(pick)) pose_.matState(mat).tex = t;
        } else if (ktype == 1) {
            std::array<float, 4> out{};
            bool have = false;
            for (uint32_t i = 0; i < count; ++i) {
                std::array<float, 5> cur{};
                for (float& x : cur)
                    if (!f32(x)) return false;
                if (!have || cur[4] <= frame + 1e-3f) out = {cur[0], cur[1], cur[2], cur[3]};
                have = true;
            }
            if (have && mat && prop == "color") {
                gfx::ScenePose::MatState& st = pose_.matState(mat);
                st.hasColor = true;
                st.rgb[0] = out[0], st.rgb[1] = out[1], st.rgb[2] = out[2];
                st.hasAlpha = true;
                st.alpha = out[3];
            }
        } else if (ktype == 0) {
            float out = 0, pv = 0, pf = 0;
            bool have = false;
            for (uint32_t i = 0; i < count; ++i) {
                float kv, kf;
                if (!f32(kv) || !f32(kf)) return false;
                if (!have || kf <= frame) out = kv;
                else if (pf <= frame && kf > frame) out = pv + (kv - pv) * (frame - pf) / (kf - pf);
                pv = kv, pf = kf, have = true;
            }
            if (have && mat && prop == "alpha") {
                gfx::ScenePose::MatState& st = pose_.matState(mat);
                st.hasAlpha = true;
                st.alpha = std::clamp(out, 0.0f, 1.0f);
            } else if (have && mat && prop == "tex_xfm") {
                // band_meter_fill.anim: the Overdrive bar's texture slides
                // along x with the energy [C data].
                gfx::ScenePose::MatState& st = pose_.matState(mat);
                if (!st.hasXfm) st.texXfm = mat->texXfm;
                st.hasXfm = true;
                const int axis = sub == "y" ? 1 : sub == "z" ? 2 : 0;
                st.texXfm.pos[size_t(axis)] = out;
            }
        } else {
            bool out = false, have = false;
            for (uint32_t i = 0; i < count; ++i) {
                float kf;
                if (p >= r.size()) return false;
                const bool kv = r[p++] != 0;
                if (!f32(kf)) return false;
                if (!have || kf <= frame + 1e-3f) out = kv;
                have = true;
            }
            if (have && obj && prop == "showing") pose_.setShowing(obj, out);
        }
        p += 12 - 4 * ktype;
        if (p > r.size()) return false;
    }
    return true;
}

void RbHudView::setText(const std::string& elem, const std::string& text, const std::string& value) {
    if (const scene::Object* o = in(elem, text)) texts_[o] = value;
}

// ---- What drives each element ------------------------------------------

void RbHudView::drive(const State& s, float nowMs) {
    // track_paneldir.dta configure_tracks: the configuration by the gem
    // tracks' count (widescreen: the port draws 16:9) [C data].
    const size_t n = std::min<size_t>(s.tracks.size(), 3);
    const std::string want = n == 0 ? "vocal_only_wide" : std::to_string(n) + "_player_wide";
    if (config_ != want) {
        applyConfiguration(want);
        // set_showing per instrument: the crowd meter's icons (configure_tracks) [C data].
        if (scene::Directory* cm = subdir("crowd_meter"))
            for (const char* inst : {"guitar", "bass", "drum", "vocals"}) {
                bool on = std::string(inst) == "vocals" ? s.vocals : false;
                for (const Track& t : s.tracks)
                    if (t.part == inst) on = true;
                if (const scene::Object* o = cm->get(inst)) pose_.setShowing(o, on);
                if (const scene::Object* g = cm->get(std::string(inst) + ".grp")) pose_.setShowing(g, on);
            }
        trackRects_.clear();
        // No vocals: scoreboard_to_top.tnm at frame 1 [C data].
        setFrame(panelObj("scoreboard_to_top.tnm"), s.vocals && n > 0 ? 0.0f : 1.0f);
    }
    // The tracks' places once the meters show (reset_all.trig hides
    // player_hud.grp until the intro).
    if (n > 1 && trackRects_.empty()) {
        setCamera();
        for (size_t i = 0; n > 1 && i < n; ++i) {
            float cx, cy, w;
            if (!meterBox(int(i), cx, cy, w)) {
                trackRects_.clear();
                break;
            }
            const float k = w / refMeter_[2];
            trackRects_.push_back({(cx - refMeter_[0] * k) / 1280.0f, (cy - refMeter_[1] * k) / 720.0f, k, k});
        }
        if (SDL_getenv("GD05_HUDLOG"))
            for (const auto& r : trackRects_)
                SDL_Log("RB HUD: %s track rect %.3f %.3f %.3f %.3f (meter ref %.0f %.0f %.0f)", want.c_str(), double(r[0]), double(r[1]), double(r[2]), double(r[3]),
                        double(refMeter_[0]), double(refMeter_[1]), double(refMeter_[2]));
    }
    // on_extend: play_intro (intro.trig) [C data].
    if (!introPlayed_ && nowMs >= introAtMs_) {
        introPlayed_ = true;
        trigger(&panel_->root(), "intro.trig", std::max(nowMs, introAtMs_));
    }

    for (size_t i = 0; i < n; ++i) {
        const Track& t = s.tracks[i];
        TrackSeen& w = seen_[i];
        char nm[32];
        // The instrument's icon letter in the icon font (player_intro.dta
        // guitar_intro "G", player_feedback.dta setup_player B / D / G / V) [C data].
        const std::string icon = t.part == "bass" ? "B" : t.part == "drum" ? "D" : t.part == "vocals" ? "V" : "G";
        // gem_track.dta play_intro: player_intro intro (intro.trig 0.01 s
        // later), hide 2 s later; setup_player sets icon.lbl and
        // player_name.lbl (configure_tracks' setup_player_intro) [C data].
        std::snprintf(nm, sizeof nm, "player_intro_%02d", int(i + 1));
        if (scene::Directory* pi = subdir(nm)) {
            if (introSet_[i] != t.part + "/" + t.name) {
                introSet_[i] = t.part + "/" + t.name;
                if (const scene::Object* o = pi->get("icon.lbl")) texts_[o] = icon;
                if (const scene::Object* o = pi->get("player_name.lbl")) texts_[o] = t.name;
            }
            if (!w.intro && nowMs >= t.introAtMs) {
                w.intro = true;
                trigger(pi, "intro.trig", t.introAtMs + 10);
                trigger(pi, "hide.trig", t.introAtMs + 2000);
            }
        }
        // crowd_meter.dta send_icon: each instrument's icon.lbl [C data].
        if (scene::Directory* cm = subdir("crowd_meter"))
            for (const auto& [o2, d2] : cm->subdirs)
                if (o2 == t.part && d2)
                    if (const scene::Object* o = d2->get("icon.lbl")) texts_[o] = icon;
        // band_power_meter.dta set_energy: extend_anim.grp at the energy;
        // the state none / ready / deploying fires be_filling / be_ready /
        // be_deploying [C data; ready at 0.5 (scoring.dtb) I].
        std::snprintf(nm, sizeof nm, "star_power_%02d", int(i + 1));
        if (scene::Directory* sp = subdir(nm)) {
            // gem_track.dta energy_added (a phrase captured: a quarter of
            // the meter at once) -> spotlight_phrase_success.trig [C data;
            // the jump as the sign I].
            if (w.energy >= 0 && t.energy - w.energy > 0.1f && !t.deploying) trigger(sp, "spotlight_phrase_success.trig", nowMs);
            if (t.energy != w.energy) setFrame(sp->get("extend_anim.grp"), t.energy), w.energy = t.energy;
            const int st = t.deploying ? 2 : t.energy >= 0.5f ? 1 : 0;
            if (st != w.state) {
                trigger(sp, st == 2 ? "be_deploying.trig" : st == 1 ? "be_ready.trig" : "be_filling.trig", nowMs);
                if (st == 0) setFrame(sp->get("extend_anim.grp"), t.energy);
                w.state = st;
            }
        }
        // streak_meter.dta: set_multiplier (super streak at the max:
        // show_blue / hide_blue), multiplier_changed (multiplier.txt "Nx"
        // over 1, multi-meter_anim.tnm, new_streak.trig), set_hit (the wipe:
        // the streak's share of the next multiplier) and break [C data; the
        // wipe's share I: ten notes a multiplier].
        std::snprintf(nm, sizeof nm, "streak_meter_%02d", int(i + 1));
        if (scene::Directory* sm = subdir(nm)) {
            const int mult = std::max(1, t.streakMultiplier), total = std::max(1, t.totalMultiplier);
            if (mult < w.mult && w.mult > 1) trigger(sm, "broken_glow.trig", nowMs);
            if (mult != w.mult) trigger(sm, mult >= t.maxMultiplier ? "show_blue.trig" : "hide_blue.trig", nowMs);
            w.mult = mult;
            if (total != w.total) {
                const scene::Object* mm = sm->get("multi-meter_anim.tnm");
                switch (total) {
                case 2: animate(mm, 0, 8, 0, 1, nowMs); break;
                case 5: case 6: setFrame(mm, 8); break;
                case 10: setFrame(mm, 10); break;
                case 12: setFrame(mm, 9); break;
                default: break;
                }
                if (const scene::Object* txt = sm->get("multiplier.txt")) {
                    pose_.setShowing(txt, total > 1);
                    texts_[txt] = std::to_string(total) + "x";
                }
                if (total > 1 && w.total > 0) trigger(sm, "new_streak.trig", nowMs);
                w.total = total;
            }
            const float hit = mult >= t.maxMultiplier ? 1.0f : float(t.streak % 10) / 10.0f;
            setFrame(sm->get("meter_wipe.mnm"), hit);
        }
        // failed_feedback.dta failed: strikes, failed.lbl, bfb_failed.trig;
        // saved: bfb_failed_end.trig, bfb_saved.trig 0.4 s later [C data].
        std::snprintf(nm, sizeof nm, "failed_feedback_%02d", int(i + 1));
        if (scene::Directory* ff = subdir(nm); ff && t.failed != w.failed) {
            if (t.failed) {
                trigger(ff, "reset.trig", nowMs);
                if (const scene::Object* o = ff->get("failed.lbl")) texts_[o] = locale_.text("Failed!");
                if (const scene::Object* o = ff->get("strikes_bg.grp")) pose_.setShowing(o, true);
                trigger(ff, "bfb_failed.trig", nowMs);
            } else {
                trigger(ff, "bfb_failed_end.trig", nowMs);
                trigger(ff, "bfb_saved.trig", nowMs + 400);
            }
            w.failed = t.failed;
        }
        // player_feedback.dta start_solo / set_percent / end_solo [C data].
        std::snprintf(nm, sizeof nm, "player_feedback_%02d", int(i + 1));
        if (scene::Directory* pf = subdir(nm)) {
            if (t.soloActive && !w.solo) {
                trigger(pf, "reset.trig", nowMs), trigger(pf, "solo_start.trig", nowMs);
                if (const scene::Object* o = pf->get("icon.lbl")) texts_[o] = icon;
                if (const scene::Object* o = pf->get("player_name.lbl")) texts_[o] = t.name;
            }
            if (!t.soloActive && w.solo) trigger(pf, "solo_end.trig", nowMs);
            if (t.soloActive) {
                const std::string pct = std::to_string(t.soloPercent) + "%";
                if (const scene::Object* txt = pf->get("solo_percent.txt")) texts_[txt] = pct;
                if (const scene::Object* g = pf->get("percent_growth.tnm"))
                    setFrame(g, float(t.soloPercent) / 100.0f * gfx::ScenePose::endFrame(g));
            }
            w.solo = t.soloActive;
        }
        // crowd_meter.dta: each instrument's icon group at its health [I:
        // the rating 0..1 as the frame; the editor's range is -1..1].
        if (scene::Directory* cm = subdir("crowd_meter"))
            if (const scene::Object* g = cm->get(t.part + ".grp")) setFrame(g, t.rating);
    }
    // TrackPanel poll (0x82803750) [C]: the scoreboard's set_score.
    const std::string score = fmtScore(s.bandScore);
    if (score != scoreShown_) setText("scoreboard", "score.txt", score), scoreShown_ = score;
    // scoreboard.dta set_num_stars: star N shows and sweeps as the stars
    // fill (sweep.mnm 0..1), full.trig when full [C data; whole stars here I].
    if (scene::Directory* sb = subdir("scoreboard")) {
        const scene::DataNode* stars = headerProp(sb->self.get(), "stars");
        std::vector<const scene::Object*> st;
        if (stars)
            for (const auto& c : stars->children)
                if (const scene::Object* o = sb->get(c.str)) st.push_back(o);
        if (st.empty())
            for (int k = 1; k <= 5; ++k)
                if (const scene::Object* o = sb->get("star" + std::to_string(k))) st.push_back(o);
        const int whole = int(std::floor(s.stars));
        if (whole != starsSeen_) {
            for (size_t k = 0; k < st.size(); ++k) {
                pose_.setShowing(st[k], int(k) <= whole || k == 0);
                for (const auto& [o2, d2] : sb->subdirs)
                    if (o2 == st[k]->name && d2) {
                        if (int(k) < whole) {
                            setFrame(d2->get("sweep.mnm"), 1);
                            if (int(k) >= starsSeen_) trigger(d2.get(), "full.trig", nowMs);
                        } else {
                            setFrame(d2->get("sweep.mnm"), s.stars - float(whole));
                        }
                    }
            }
            starsSeen_ = whole;
        }
    }
    // crowd_meter.dta set_crowd: crowd_meter.mnm at the value;
    // update_excitement: crowd_meter.mat's colour from level_colors [C data].
    if (scene::Directory* cm = subdir("crowd_meter")) {
        setFrame(cm->get("crowd_meter.mnm"), s.crowd);
        if (s.excitement != excitementSeen_) {
            excitementSeen_ = s.excitement;
            const scene::DataNode* lc = headerProp(cm->self.get(), "level_colors");
            if (!lc) lc = headerProp(panelObj("crowd_meter"), "level_colors");
            const scene::Mat* m = cm->get("crowd_meter.mat") ? cm->get("crowd_meter.mat")->as<scene::Mat>() : nullptr;
            if (lc && m && size_t(s.excitement) < lc->children.size()) {
                const uint32_t c = lc->children[size_t(s.excitement)].raw;  // packed r | g << 8 | b << 16 [I]
                gfx::ScenePose::MatState& ms = pose_.matState(m);
                ms.hasColor = true;
                ms.rgb[0] = float(c & 0xff) / 255.0f, ms.rgb[1] = float((c >> 8) & 0xff) / 255.0f, ms.rgb[2] = float((c >> 16) & 0xff) / 255.0f;
            }
        }
    }
    // track_paneldir.dta set_multiplier: star_meter_multiplier shows over 1
    // with more than one participant; new_multiplier.trig [C data].
    if (s.bandMultiplier != bandMultShown_) {
        const bool visible = s.bandMultiplier > 1 && s.participants > 1;
        if (const scene::Object* o = panelObj("star_meter_multiplier")) pose_.setShowing(o, visible);
        setText("star_meter_multiplier", "multiplier.txt", std::to_string(s.bandMultiplier));
        if (s.bandMultiplier > 1) trigger(&panel_->root(), "star_multiplier_number.trig", nowMs), trigger(subdir("star_meter_multiplier"), "new_multiplier.trig", nowMs);
        bandMultShown_ = s.bandMultiplier;
    }
    // game_won / game_lost / coda (bre_start.trig) [C data].
    if (s.won && !wonSeen_) trigger(&panel_->root(), "game_won.trig", nowMs), wonSeen_ = true;
    if (s.lost && !lostSeen_) trigger(&panel_->root(), "game_lost.trig", nowMs), lostSeen_ = true;
    if (s.coda && !codaSeen_) trigger(&panel_->root(), "bre_start.trig", nowMs), codaSeen_ = true;
    if (s.coda) setText("ending_bonus", "score.txt", fmtScore(s.codaScore));
}

// ---- Drawing ------------------------------------------------------------

void RbHudView::setCamera() {
    const scene::Cam* cam = cam_->as<scene::Cam>();
    // track_paneldir.dta reset: Cam.cam at the origin in widescreen [C data].
    scene::Xfm x = pose_.world(cam_);
    x.pos = {0, 0, 0};
    camera_.world = gfx::toMat4(x);
    camera_.yFov = cam->yFov, camera_.nearPlane = cam->nearPlane, camera_.farPlane = cam->farPlane;
    camera_.aspect = float(viewW_) / float(viewH_);
    pose_.setCamera(x);
}

void RbHudView::setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env) {
    if (env == currentEnv_ && frameSet_) return;
    currentEnv_ = env;
    frameSet_ = true;
    // Without an environ of its own an element draws in the UI's (the
    // TrackPanel is a UI panel: config/default.dta (ui (env (ambient 1 1 1))))
    // [C data; I: which environ RB's panel draw leaves current].
    gfx::LightEnv le = gfx::UiView::defaultEnvironment();
    if (env) le = cache.lightEnv(*env, &pose_);
    mr.setFrame(camera_, le, viewW_, viewH_);
}

void RbHudView::drawObject(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object* o, const scene::Environ* env, int depth) {
    if (!o || depth > 20 || !pose_.showing(o)) return;
    if (auto* m = o->as<scene::Mesh>()) {
        if (suppress_) return;
        setEnv(mr, cache, env);
        gfx::drawMesh(mr, cache, *m, gfx::toMat4(pose_.world(m)), &pose_);
    } else if (auto* g = o->as<scene::Group>()) {
        if (!g->environment.empty() && g->dir)
            if (auto* e = g->dir->resolve<scene::Environ>(g->environment)) env = e;
        for (const std::string& n : g->objects)
            if (const scene::Object* c = g->dir ? g->dir->resolve(n) : nullptr; c && c != g) drawObject(mr, cache, c, env, depth + 1);
    } else if (auto* t = o->as<scene::Text>()) {
        const scene::Font* font = t->dir && !t->font.empty() ? t->dir->resolve<scene::Font>(t->font) : nullptr;
        std::string str = t->text;
        if (auto it = texts_.find(t); it != texts_.end()) str = it->second;
        if (!font || str.empty() || suppress_) return;
        setEnv(mr, cache, env);
        gfx::FontMetrics fm(*font);
        gfx::TextStyle st;
        st.size = t->size, st.leading = t->leading, st.wrapWidth = t->wrapWidth, st.italics = t->italics;
        st.align = t->align, st.capsMode = t->capsMode, st.markup = t->markup, st.color = t->color;
        const gfx::TextLayout layout = gfx::layoutText(&fm, str, st);
        if (!textDrawer_) textDrawer_ = std::make_unique<gfx::TextDrawer>(cache);
        textDrawer_->draw(mr, t, *font, layout, gfx::toMat4(pose_.world(t)));
        keepQuads(t, layout, pose_.world(t));
    } else if (o->as<scene::BandLabel>()) {
        if (!suppress_) drawLabel(mr, cache, *o, env);
    } else if (auto* d = o->as<scene::DirObj>()) {
        // An inline element (a RndDir proxy): its directory under the object.
        if (!o->dir) return;
        // The probe: a nested directory alone, or the element without them.
        const bool nested = !onlyElem_.empty() && depth >= 1;
        if (nested && onlySub_ != o->name) return;
        const bool was = suppress_;
        if (nested) suppress_ = false;
        for (const auto& [owner, sd] : o->dir->subdirs)
            if (owner == o->name && sd) drawDir(mr, cache, *sd, env, depth + 1);
        suppress_ = was;
        (void)d;
    }
}

// A BandLabel (BandLabel 0x8214B0A0 [C], as UiView draws them): its
// type's template Text (ui_objects.dta resource_file / texts), its own
// text fields, the label's colour on the font material; the string is a
// set_text, the localized token, or its own text.
void RbHudView::drawLabel(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object& o, const scene::Environ* env) {
    const auto& l = *o.as<scene::BandLabel>();
    std::string str = l.localize ? (l.textToken.empty() ? std::string() : locale_.text(l.textToken)) : l.text;
    if (auto it = texts_.find(&o); it != texts_.end()) str = it->second;
    if (str.empty() || !data_ || !lib_ || !styles_.loaded()) return;
    const scene::UiStyle style = styles_.style(l.cls, l.header.type);
    const std::string file = style.resourceFile.empty() ? style.classResourceFile : style.resourceFile;
    scene::Milo* res = nullptr;
    if (auto it = resources_.find(file); it != resources_.end()) res = it->second;
    else {
        try {
            const std::string path = scene::UiStyles::resourcePath(*data_, file);
            if (data_->exists(path)) res = &lib_->load(path);
        } catch (const std::exception& e) {
            SDL_Log("RB HUD: %s: %s", file.c_str(), e.what());
        }
        resources_[file] = res;
    }
    const scene::Text* tmpl = res && !style.textTemplate.empty() ? res->find<scene::Text>(style.textTemplate) : nullptr;
    const scene::Font* font = tmpl && tmpl->dir ? tmpl->dir->resolve<scene::Font>(tmpl->font) : nullptr;
    if (!font) return;
    gfx::FontMetrics fm(*font);
    gfx::TextStyle st;
    st.italics = tmpl->italics, st.markup = tmpl->markup || l.markup;
    st.size = l.band.textSize, st.leading = l.band.leading, st.wrapWidth = l.band.wrapWidth;
    st.align = l.band.alignment, st.capsMode = l.band.allCaps ? 2 : 0;
    st.overrideKerning = true, st.kerning = l.band.kerning;
    gfx::TextLayout layout = gfx::layoutText(&fm, str, st);
    const std::array<float, 2> fit = gfx::fitScale(layout, st, l.band.fitText, l.band.width, l.band.height);
    scene::Xfm sx;
    sx.rot = {fit[0], 0, 0, 0, 1, 0, 0, 0, fit[1]};
    const scene::Xfm world = scene::compose(sx, pose_.world(&o));
    setEnv(mr, cache, env);
    if (!textDrawer_) textDrawer_ = std::make_unique<gfx::TextDrawer>(cache);
    const float rgb[3] = {l.color[0], l.color[1], l.color[2]};
    textDrawer_->draw(mr, &o, *font, layout, gfx::toMat4(world), rgb);
    keepQuads(&o, layout, world);
}

void RbHudView::keepQuads(const scene::Object* o, const gfx::TextLayout& layout, const scene::Xfm& world) {
    auto& q = textQuads_[o];
    q.clear();
    const gfx::Mat4 w = gfx::toMat4(world);
    for (const auto& v : layout.verts) {
        const gfx::Vec3 p = w.transformPoint({v.pos[0], v.pos[1], v.pos[2]});
        q.push_back({p.x, p.y, p.z});
    }
}

// A directory draws draw.grp when it has one, else its drawables that no
// group of its own holds, in table order [I: RndDir's draw list is not read].
void RbHudView::drawDir(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Directory& dir, const scene::Environ* env, int depth) {
    if (auto* self = dir.self ? dir.self->as<scene::DirObj>() : nullptr; self && self->hasRnd && !self->rnd.environment.empty())
        if (auto* e = dir.get(self->rnd.environment) ? dir.get(self->rnd.environment)->as<scene::Environ>() : nullptr) env = e;
    // RB's RndDir draw list: by draw order (render/draw_order.h, GD05-070):
    // the streak meter's dial (streak_meter.grp, order 2) over its
    // multiplier tag and text (multi_meter_anim.grp, order 1). In the
    // file's order (GD05_HWY_LEGACY=1) the multiplier drew on top.
    std::vector<const scene::Object*> list;
    if (gfx::highwayLegacy()) {
        std::set<std::string> grouped;
        for (const auto& o : dir.objects)
            if (auto* g = o->as<scene::Group>()) grouped.insert(g->objects.begin(), g->objects.end());
        for (const auto& o : dir.objects)
            if (!grouped.count(o->name) && (o->as<scene::Mesh>() || o->as<scene::Group>() || o->as<scene::DirObj>() || o->as<scene::Text>() || o->as<scene::BandLabel>()))
                list.push_back(o.get());
    } else {
        list = gfx::dirDrawList(dir);
    }
    std::vector<const scene::Object*> drawn;
    for (const scene::Object* o : list) {
        if (pose_.showing(o)) drawn.push_back(o);
        drawObject(mr, cache, o, env, depth);
    }
    if (countOrder_) orderInversions_ += gfx::drawOrderInversions(drawn), ++orderDirs_;
}

void RbHudView::drawElem(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object* o) {
    if (!o) return;
    if (std::find(hiddenElems_.begin(), hiddenElems_.end(), o->name) != hiddenElems_.end()) return;
    if (!onlyElem_.empty() && o->as<scene::DirObj>() && o->name != onlyElem_) return;
    if (auto* g = o->as<scene::Group>()) {
        if (!pose_.showing(g)) return;
        for (const std::string& n : g->objects)
            if (const scene::Object* c = g->dir ? g->dir->resolve(n) : nullptr; c && c != g) drawElem(mr, cache, c);
        return;
    }
    if (!onlyElem_.empty() && o->name != onlyElem_) return;
    drawObject(mr, cache, o, nullptr, 0);
}

void RbHudView::begin(gfx::Renderer& r, gfx::SceneCache& cache) {
    cache_ = &cache;
    int vx, vy;
    r.pictureRect(vx, vy, viewW_, viewH_);
    r.device().clear(nullptr, true);
    frameSet_ = false;
    currentEnv_ = nullptr;
    setCamera();
}

void RbHudView::draw(gfx::Renderer& r, gfx::SceneCache& cache, const State& state, float nowMs) {
    if (!panel_) return;
    if (nowMs < lastMs_ - 1000.0f) reset(introAtMs_);
    tempo_ = state.tempo;
    drive(state, nowMs);
    lastMs_ = nowMs;
    runTasks(nowMs);
    // Before the intro the panel is not shown (track_panel.dta finish_load
    // set_showing FALSE, intro_start set_showing) [C data].
    if (!introPlayed_) return;
    begin(r, cache);
    onlyElem_.clear();
    const scene::Group* order = panel_->root().get("draw_order.grp") ? panel_->root().get("draw_order.grp")->as<scene::Group>() : nullptr;
    if (!order) return;
    gfx::MaterialRenderer& mr = r.materials();
    countOrder_ = true, orderInversions_ = orderDirs_ = 0;
    for (const std::string& n : order->objects) drawElem(mr, cache, panel_->root().get(n));
    countOrder_ = false;
    if (textDrawer_) textDrawer_->collect();
}

void RbHudView::drawOnly(gfx::Renderer& r, gfx::SceneCache& cache, const std::string& name) {
    if (!panel_ || !introPlayed_) return;
    begin(r, cache);
    const size_t slash = name.find('/');
    onlyElem_ = name.substr(0, slash);
    onlySub_ = slash == std::string::npos ? std::string() : name.substr(slash + 1);
    suppress_ = !onlySub_.empty();
    const scene::Group* order = panel_->root().get("draw_order.grp") ? panel_->root().get("draw_order.grp")->as<scene::Group>() : nullptr;
    if (order) {
        gfx::MaterialRenderer& mr = r.materials();
        for (const std::string& n : order->objects) drawElem(mr, cache, panel_->root().get(n));
    }
    onlyElem_.clear(), onlySub_.clear();
    suppress_ = false;
}

// The box each element's shown meshes cover through the panel camera,
// from the milo data and the configuration (1280 x 720) [C data].
std::vector<RbHudView::Box> RbHudView::expectedBoxes() const {
    std::vector<Box> out;
    if (!panel_) return out;
    const gfx::Mat4 vp = camera_.viewProj(false);
    auto project = [&](const gfx::Vec3& p, float& sx, float& sy) {
        const float cx = vp.at(0, 0) * p.x + vp.at(0, 1) * p.y + vp.at(0, 2) * p.z + vp.at(0, 3);
        const float cy = vp.at(1, 0) * p.x + vp.at(1, 1) * p.y + vp.at(1, 2) * p.z + vp.at(1, 3);
        const float cw = vp.at(3, 0) * p.x + vp.at(3, 1) * p.y + vp.at(3, 2) * p.z + vp.at(3, 3);
        if (cw <= 1e-4f) return false;
        sx = (cx / cw * 0.5f + 0.5f) * 1280.0f, sy = (0.5f - cy / cw * 0.5f) * 720.0f;
        return true;
    };
    for (const auto& [owner, d] : panel_->root().subdirs) {
        const scene::Object* o = panel_->root().get(owner);
        if (!d || !o || std::find(hiddenElems_.begin(), hiddenElems_.end(), owner) != hiddenElems_.end()) continue;
        // Shown: the object and every group above it.
        bool shown = pose_.showing(o);
        for (const scene::Trans* t = gfx::transOf(o); shown && t && !t->parent.empty();) {
            const scene::Object* p = panel_->root().get(t->parent);
            if (!p || p == o) break;
            shown = pose_.showing(p);
            t = gfx::transOf(p);
        }
        if (!shown) continue;
        Box b;
        std::vector<Box> nestedBoxes;
        std::function<void(const scene::Directory&, Box&, bool)> walk = [&](const scene::Directory& dir, Box& b, bool top) {
            for (const auto& ob : dir.objects) {
                const auto* m = ob->as<scene::Mesh>();
                if (!m || !pose_.showing(m) || m->verts.empty()) continue;
                bool vis = true;  // a hidden group holding it
                for (const auto& og : dir.objects)
                    if (auto* g = og->as<scene::Group>(); g && !pose_.showing(g) && std::find(g->objects.begin(), g->objects.end(), m->name) != g->objects.end()) vis = false;
                if (!vis) continue;
                const scene::Mat* mat = m->dir ? m->dir->resolve<scene::Mat>(m->mat) : nullptr;
                float alpha = mat ? mat->color[3] : 1.0f;
                if (const auto* ms = mat ? pose_.mat(mat) : nullptr; ms && ms->hasAlpha) alpha = ms->alpha;
                if (alpha <= 0.02f) continue;
                // A mesh a MatAnim's texture keys reveal in part (the streak
                // meter's meter_wipe.mnm at the hit share, streak_meter.dta
                // set_hit) covers only that part: left out until full.
                bool partial = false;
                for (const auto& oa : dir.objects)
                    if (const auto* ma = oa->as<scene::MatAnim>(); ma && mat && ma->mat == mat->name && (!ma->texKeys.empty() || !ma->transKeys.empty()))
                        if (auto it = animFrame_.find(ma); it != animFrame_.end() && it->second < gfx::ScenePose::endFrame(ma) - 1e-3f) partial = true;
                if (partial) continue;
                const gfx::Mat4 w = gfx::toMat4(pose_.world(m));
                float mx0 = 1e9f, mx1 = -1e9f, my0 = 1e9f, my1 = -1e9f;
                for (const auto& v : m->verts) {
                    float sx, sy;
                    if (!project(w.transformPoint({v.pos[0], v.pos[1], v.pos[2]}), sx, sy)) continue;
                    b.x0 = std::min(b.x0, sx), b.y0 = std::min(b.y0, sy), b.x1 = std::max(b.x1, sx), b.y1 = std::max(b.y1, sy);
                    mx0 = std::min(mx0, sx), my0 = std::min(my0, sy), mx1 = std::max(mx1, sx), my1 = std::max(my1, sy);
                    b.valid = true;
                }
                if (SDL_getenv("GD05_HUDLOG"))
                    SDL_Log("RB HUD:   %s mesh %s box %.0f %.0f - %.0f %.0f alpha %.2f mat %s", owner.c_str(), m->name.c_str(), double(mx0), double(my0), double(mx1),
                            double(my1), double(alpha), m->mat.c_str());
            }
            // Its texts and labels as last drawn (their glyph quads).
            for (const auto& ob : dir.objects) {
                if (!ob->as<scene::Text>() && !ob->as<scene::BandLabel>()) continue;
                auto it = textQuads_.find(ob.get());
                if (it == textQuads_.end() || !pose_.showing(ob.get())) continue;
                const scene::Trans* tt = gfx::transOf(ob.get());
                bool vis = true;
                for (const auto& og : dir.objects)
                    if (auto* g = og->as<scene::Group>(); g && !pose_.showing(g) && std::find(g->objects.begin(), g->objects.end(), ob->name) != g->objects.end()) vis = false;
                (void)tt;
                if (!vis) continue;
                for (const auto& p : it->second) {
                    float sx, sy;
                    if (!project({p[0], p[1], p[2]}, sx, sy)) continue;
                    b.x0 = std::min(b.x0, sx), b.y0 = std::min(b.y0, sy), b.x1 = std::max(b.x1, sx), b.y1 = std::max(b.y1, sy);
                    b.valid = true;
                }
            }
            for (const auto& [o2, sd] : dir.subdirs)
                if (sd)
                    if (const scene::Object* so = dir.get(o2); so && pose_.showing(so)) {
                        if (!top) {
                            walk(*sd, b, false);
                            continue;
                        }
                        Box nb;
                        nb.name = owner + "/" + o2, nb.strict = false;
                        nb.x0 = nb.y0 = 1e9f, nb.x1 = nb.y1 = -1e9f;
                        walk(*sd, nb, false);
                        if (nb.valid) nestedBoxes.push_back(nb);
                    }
        };
        b.name = owner;
        b.x0 = b.y0 = 1e9f, b.x1 = b.y1 = -1e9f;
        walk(*d, b, true);
        nestedBoxes.insert(nestedBoxes.begin(), b);
        for (Box& bx : nestedBoxes)
            if (bx.valid) {
                bx.x0 = std::clamp(bx.x0, 0.0f, 1280.0f), bx.x1 = std::clamp(bx.x1, 0.0f, 1280.0f);
                bx.y0 = std::clamp(bx.y0, 0.0f, 720.0f), bx.y1 = std::clamp(bx.y1, 0.0f, 720.0f);
                if (bx.x1 > bx.x0 + 2 && bx.y1 > bx.y0 + 2) out.push_back(bx);  // shrunk to a point (an intro's hide): not on screen
            }
    }
    return out;
}

// Track `index`'s Overdrive meter (its own meshes) on the picture.
bool RbHudView::meterBox(int index, float& cx, float& cy, float& w) const {
    char nm[32];
    std::snprintf(nm, sizeof nm, "star_power_%02d", index + 1);
    for (const Box& b : expectedBoxes())
        if (b.name == nm && b.strict) {
            cx = 0.5f * (b.x0 + b.x1), cy = 0.5f * (b.y0 + b.y1), w = b.x1 - b.x0;
            return w > 1;
        }
    return false;
}

RbHudView::Shown RbHudView::shown(int track) const {
    Shown s;
    if (const scene::Object* o = in("scoreboard", "score.txt"))
        if (auto it = texts_.find(o); it != texts_.end()) s.score = it->second;
    char nm[32];
    std::snprintf(nm, sizeof nm, "streak_meter_%02d", track + 1);
    if (const scene::Object* o = in(nm, "multiplier.txt"))
        if (auto it = texts_.find(o); it != texts_.end() && pose_.showing(o)) s.multiplier = it->second;
    if (const scene::Object* o = in(nm, "meter_wipe.mnm"))
        if (auto it = animFrame_.find(o); it != animFrame_.end()) s.wipeFrame = it->second;
    std::snprintf(nm, sizeof nm, "star_power_%02d", track + 1);
    if (const scene::Object* o = in(nm, "extend_anim.grp"))
        if (auto it = animFrame_.find(o); it != animFrame_.end()) s.energyFrame = it->second;
    if (const scene::Object* o = in("crowd_meter", "crowd_meter.mnm"))
        if (auto it = animFrame_.find(o); it != animFrame_.end()) s.crowdFrame = it->second;
    return s;
}
