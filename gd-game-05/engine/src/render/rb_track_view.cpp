// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/rb_track_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "data/dta.h"
#include "data/game_data.h"
#include "render/renderer.h"
#include "scene/milo.h"

RbTrackView::RbTrackView() = default;
RbTrackView::~RbTrackView() = default;

const scene::Object* RbTrackView::obj(const std::string& name) const { return track_ ? track_->find(name) : nullptr; }

template <class T> const T* RbTrackView::get(const std::string& name) const {
    const scene::Object* o = obj(name);
    return o ? o->as<T>() : nullptr;
}

float RbTrackView::slotX(int slot) const {
    const float x = slotX_[size_t(std::clamp(slot, 0, 4))];
    return lefty_ ? -x : x;
}

void RbTrackView::load(const GameData& data, scene::MiloLibrary& library, const std::string& part) {
    part_ = part;
    drums_ = part == "drum";
    // ui/track_panel.dta: track_panel (file ../track/trackpanel.milo); its
    // TrackDirs guitar / bass / drum proxy track/tracksystem.milo [C data].
    scene::Milo& panel = library.load("track/gen/trackpanel.milo_xbox");
    scene::Milo* track = nullptr;
    for (const auto& o : panel.root().objects)
        if (o->cls == "TrackDir" && o->name == part)
            if ((track = library.proxy(panel, *o))) break;
    if (!track) track = &library.load("track/gen/tracksystem.milo_xbox");
    auto* self = track->root().self ? track->root().self->as<scene::DirObj>() : nullptr;
    if (!self || !self->hasTrack) throw std::runtime_error("tracksystem: not a TrackDir");
    track_ = track;
    // The textures the track's materials name (prism_gems.tex ...) are in
    // track/track_shared.milo [C data]; linked for the names [I: how the
    // original shares them, a sub-directory of the panel, is not read].
    try {
        scene::Milo& shared = library.load("track/gen/track_shared.milo_xbox");
        auto& linked = track_->root().linked;
        if (std::find(linked.begin(), linked.end(), &shared.root()) == linked.end()) linked.push_back(&shared.root());
    } catch (const std::exception& e) {
        SDL_Log("RB track: track_shared.milo: %s", e.what());
    }
    const scene::TrackDirData& td = self->track;
    yPerSecond_ = td.yPerSecond > 0 ? td.yPerSecond : 60;
    bottomY_ = td.bottomY, topY_ = td.topY > td.bottomY ? td.topY : 120;
    drawGroup_ = td.drawGroup, animGroup_ = td.animGroup;
    // The slots: five for the guitar and the drums (the kick's slot 0 is the
    // whole track's middle) [C data: TrackDir slots].
    for (size_t i = 0; i < slotX_.size() && i < td.slots.size(); ++i) slotX_[i] = td.slots[i][0];

    // config/track_graphics.dtb (gem (gems (guitar GUITAR_SET) (drum DRUM_SET)))
    // [C data]: per slot the widget of each kind.
    const DataNode cfg = parseDtb(data.read("config/gen/track_graphics.dtb"));
    if (const DataNode* sc = cfg.find("slot_colors"))
        for (size_t i = 0; i < 5 && i + 1 < sc->children.size(); ++i) slotColors_[i] = sc->textAt(i + 1);
    const DataNode* gem = cfg.find("gem");
    const DataNode* gems = gem ? gem->find("gems") : nullptr;
    const DataNode* set = gems ? gems->find(drums_ ? "drum" : part == "bass" ? "bass" : "guitar") : nullptr;
    auto widget = [&](const DataNode* slot, const char* kind) -> const scene::TrackWidget* {
        const DataNode* k = slot ? slot->find(kind) : nullptr;
        return k && k->children.size() > 1 ? get<scene::TrackWidget>(k->textAt(1)) : nullptr;
    };
    // (guitar GUITAR_SET): the .dtb keeps the #define; the macro's body
    // (the list after the Define node) holds the slots' lists.
    if (set && set->children.size() == 2 && set->children[1].isText()) {
        const std::string macro = set->children[1].s;
        set = nullptr;
        for (size_t k = 0; k + 1 < cfg.children.size(); ++k)
            if (cfg.children[k].type == DataNode::Type::Define && cfg.children[k].s == macro) set = &cfg.children[k + 1];
    } else if (set) {
        set = nullptr;  // an inline list is not what the disc has [C data]
    }
    for (size_t k = 0; set && k < set->children.size(); ++k) {
        const DataNode& s = set->children[k];
        if (!s.isList() || s.children.empty()) continue;
        const int i = s.children[0].type == DataNode::Type::Int ? s.children[0].i : -1;
        if (i < 0 || i > 4) continue;
        SlotSet& o = sets_[size_t(i)];
        o.normal = widget(&s, "normal"), o.hopo = widget(&s, "normal_hopo"), o.star = widget(&s, "star");
        o.starHopo = widget(&s, "star_hopo"), o.miss = widget(&s, "miss"), o.missHopo = widget(&s, "miss_hopo");
        if (!o.hopo) o.hopo = o.normal;
        if (!o.starHopo) o.starHopo = o.star;
        if (!o.missHopo) o.missHopo = o.miss;
    }
    if (!sets_[1].normal)
        throw std::runtime_error("tracksystem: no gem widgets for " + part + " (gems " + std::to_string(gems ? gems->children.size() : 0) +
                                 ", set " + std::to_string(set ? set->children.size() : 0) + ", gem_red.wid " +
                                 (obj("gem_red.wid") ? obj("gem_red.wid")->cls : std::string("missing")) + ")");
    barW_ = get<scene::TrackWidget>("bar_measure.wid");
    beatW_ = get<scene::TrackWidget>("bar_beat.wid");
    halfBeatW_ = get<scene::TrackWidget>("bar_half_beat.wid");

    // The smasher plate (the now bar's frets / pads): the inline directory
    // smasher_plate_guitar / _drum, its draw_order.grp [C data].
    for (const auto& [owner, d] : track_->root().subdirs)
        if (owner == (drums_ ? "smasher_plate_drum" : "smasher_plate_guitar")) smasherGroup_ = d->get("draw_order.grp") ? d->get("draw_order.grp")->as<scene::Group>() : nullptr;
    // The camera: the track's game.cam [C data; the choice is I].
    cam_ = get<scene::Cam>("game.cam");
    if (!cam_)
        for (const auto& o : track_->root().objects)
            if ((cam_ = o->as<scene::Cam>())) break;
    if (!cam_) throw std::runtime_error("tracksystem: no camera");
    screenRect_ = cam_->screenRect;
    if (const scene::Group* g = get<scene::Group>(drawGroup_))
        if (!g->environment.empty() && g->dir) trackEnv_ = g->dir->resolve<scene::Environ>(g->environment);
    if (SDL_getenv("GD05_TRACKLOG"))
        for (const scene::TrackWidget* w : {sets_[0].normal, sets_[0].star, barW_})
            for (const std::string& n : w ? w->meshes : std::vector<std::string>{}) {
                const scene::Mesh* m = w->dir->resolve<scene::Mesh>(n);
                const scene::Mat* mt = m && w->dir ? w->dir->resolve<scene::Mat>(m->mat) : nullptr;
                SDL_Log("  widget %s mesh %s (%s) mat %s (%s) tex %s color %.2f %.2f %.2f %.2f", w->name.c_str(), n.c_str(), m ? "ok" : "missing",
                        m ? m->mat.c_str() : "", mt ? "ok" : "missing", mt ? mt->diffuseTex.c_str() : "", mt ? double(mt->color[0]) : 0.0,
                        mt ? double(mt->color[1]) : 0.0, mt ? double(mt->color[2]) : 0.0, mt ? double(mt->color[3]) : 0.0);
            }
    if (SDL_getenv("GD05_TRACKLOG"))
        if (const scene::Group* g = get<scene::Group>(drawGroup_))
            for (const std::string& n : g->objects) {
                const scene::Object* o = g->dir->resolve(n);
                const scene::Mesh* m = o ? o->as<scene::Mesh>() : nullptr;
                const scene::Mat* mt = m ? g->dir->resolve<scene::Mat>(m->mat) : nullptr;
                const scene::Object* tx = mt && !mt->diffuseTex.empty() ? g->dir->resolve(mt->diffuseTex) : nullptr;
                SDL_Log("  draw %s %s mat %s tex %s (%s)", o ? o->cls.c_str() : "?", n.c_str(), m ? m->mat.c_str() : "", mt ? mt->diffuseTex.c_str() : "",
                        tx ? tx->cls.c_str() : "missing");
            }
    SDL_Log("RB track %s: y_per_second %.1f, y %.1f..%.1f, slots %.2f %.2f %.2f %.2f %.2f, draw %s, anim %s, camera %s", part.c_str(),
            double(yPerSecond_), double(bottomY_), double(topY_), double(slotX_[0]), double(slotX_[1]), double(slotX_[2]), double(slotX_[3]),
            double(slotX_[4]), drawGroup_.c_str(), animGroup_.c_str(), cam_->name.c_str());
}

void RbTrackView::setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env) {
    if (env == currentEnv_ && frameSet_) return;
    currentEnv_ = env;
    frameSet_ = true;
    gfx::LightEnv le;
    if (env) le = cache.lightEnv(*env, &pose_);
    mr.setFrame(camera_, le, viewW_, viewH_);
}

// The draw group's meshes and sub-groups; the widgets' own meshes (drawn
// per gem) and the effects' directories are not part of it.
void RbTrackView::drawGroup(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Group* g, const scene::Environ* env,
                            int depth) {
    if (!g || depth > 8 || !pose_.showing(g) || !g->dir) return;
    if (!g->environment.empty())
        if (auto* e = g->dir->resolve<scene::Environ>(g->environment)) env = e;
    for (const std::string& n : g->objects) {
        const scene::Object* o = g->dir->resolve(n);
        if (!o || o == g || !pose_.showing(o)) continue;
        if (auto* m = o->as<scene::Mesh>()) {
            setEnv(mr, cache, env);
            gfx::drawMesh(mr, cache, *m, gfx::toMat4(pose_.world(m)), &pose_);
        } else if (auto* sub = o->as<scene::Group>()) {
            drawGroup(mr, cache, sub, env, depth + 1);
        }
    }
}

// TrackWidget instances as GH2's (0x82363A88 there; RB's TrackWidget
// Load RB 0x82254720) [C GH2, I RB]: the widget's meshes at (x, y, 0).
// The gem meshes share prism_mat.mat / prism_glow_blue.mat; the slot's
// colour is the same material named for it (prism_mat_<colour>.mat,
// prism_glow_<colour>.mat), slot_colors from track_graphics [C data; the swap
// itself is I: RB's gem code is not read].
const scene::Mat* RbTrackView::slotMat(const scene::Mesh& m, int slot) const {
    if (slot < 0 || slot > 4 || !track_) return nullptr;
    const std::string& c = slotColors_[size_t(slot)];
    std::string name;
    if (m.mat.rfind("prism_mat", 0) == 0) name = "prism_mat_" + c + ".mat";
    else if (m.mat.rfind("prism_glow", 0) == 0) name = "prism_glow_" + c + ".mat";
    if (name.empty() || name == m.mat) return nullptr;
    return track_->root().resolve<scene::Mat>(name);
}

void RbTrackView::drawWidget(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::TrackWidget* w, float x, float y, int colorSlot) {
    if (!w || !w->dir) return;
    for (const std::string& n : w->meshes) {
        const scene::Mesh* m = w->dir->resolve<scene::Mesh>(n);
        if (!m || !pose_.showing(m)) continue;
        scene::Xfm x0;
        if (w->allowRotation) x0.rot = pose_.local(m).rot;
        x0.pos = {x, y, 0};
        gfx::drawMesh(mr, cache, *m, gfx::toMat4(x0), &pose_, slotMat(*m, colorSlot));
    }
}

void RbTrackView::drawBeatLines(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Song& song, float nowMs) {
    setEnv(mr, cache, trackEnv_);
    const int division = std::max(2, song.tempo.division());
    const long half = division / 2;
    const double fromMs = nowMs + (bottomY_ - 1) / (yPerSecond_ * speed_) * 1000.0f;
    const double toMs = nowMs + topY_ / (yPerSecond_ * speed_) * 1000.0f;
    long tick = std::max(0L, long(song.tempo.msToTick(std::max(0.0, fromMs))) / half * half);
    for (int guard = 0; guard < 4096; ++guard, tick += half) {
        const double ms = song.tempo.tickToMs(double(tick));
        if (ms > toMs || ms > song.endMs + 1) break;
        const float y = yOf(float(ms) - nowMs);
        if (y + 1 < bottomY_) continue;
        const scene::TrackWidget* w = halfBeatW_;
        if (tick % division == 0) {
            const double m = song.tempo.tickToMeasure(double(tick));
            w = std::abs(m - std::round(m)) < 1e-6 ? barW_ : beatW_;
        }
        drawWidget(mr, cache, w, 0, y);
    }
}

void RbTrackView::drawGems(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const PlayerView& player, float nowMs) {
    if (!player.chart) return;
    setEnv(mr, cache, trackEnv_);
    const auto& gems = player.chart->gems;
    const float aheadMs = topY_ / (yPerSecond_ * speed_) * 1000.0f;
    const float behindMs = (bottomY_ - 1) / (yPerSecond_ * speed_) * 1000.0f;
    auto first = std::lower_bound(gems.begin(), gems.end(), nowMs + behindMs, [](const Gem& g, float v) { return g.ms < v; });
    auto last = std::lower_bound(first, gems.end(), nowMs + aheadMs, [](const Gem& g, float v) { return g.ms < v; });
    // Far to near; the drums' kick first (under the pads' gems).
    for (auto it = last; it != first;) {
        --it;
        const int i = int(it - gems.begin());
        const Gem& g = *it;
        if (player.hit && player.hit(i)) continue;
        const float y = yOf(g.ms - nowMs);
        const bool missed = player.missed && player.missed(i);
        const bool hopo = !drums_ && g.hopo && i > 0;
        const bool star = !missed && !player.overdriveActive && g.phrase >= 0 && (!player.phraseIntact || player.phraseIntact(g.phrase));
        for (int f = 0; f < 5; ++f) {
            if (!(g.frets & (1u << f))) continue;
            const SlotSet& s = sets_[size_t(f)];
            const scene::TrackWidget* w = missed ? (hopo ? s.missHopo : s.miss) : star ? (hopo ? s.starHopo : s.star) : hopo ? s.hopo : s.normal;
            if (!w) w = s.normal;
            // The kick's widget spans the track from its middle [I].
            // Colour: the guitar's slot f; the drums' pads are red, yellow, blue, green (lanes 1-4), the kick orange [C data: slot_colors; I: the drums' order].
            const int color = missed || star ? -1 : drums_ ? (f == 0 ? 4 : f) : f;
            drawWidget(mr, cache, w, drums_ && f == 0 ? 0.0f : slotX(f), y, color);
        }
    }
}

void RbTrackView::draw(gfx::Renderer& r, gfx::SceneCache& cache, const Song& song, const PlayerView& player, float nowMs, float trackSpeed) {
    if (!track_) return;
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    const bool sub = screenRect_ != std::array<float, 4>{0, 0, 1, 1};
    viewW_ = std::max(1, int(float(vw) * screenRect_[2])), viewH_ = std::max(1, int(float(vh) * screenRect_[3]));
    if (sub) r.device().setViewport(vx + int(float(vw) * screenRect_[0]), vy + int(float(vh) * screenRect_[1]), viewW_, viewH_);
    frameSet_ = false;
    speed_ = trackSpeed;
    nowY_ = yOf(nowMs);
    gfx::MaterialRenderer& mr = r.materials();
    // The anim group at frame now_y (TrackDir::Poll, as GH2's): the surface's scroll [C GH2; I RB].
    if (!animGroup_.empty()) pose_.setFrame(obj(animGroup_), nowY_);
    // static.grp holds every instrument's parts; the other instrument's are
    // hidden, and the effects that show on events (the bass superstreak,
    // the Overdrive spotlight, a Unison phrase hit) until those are done [I].
    for (const char* n : {"bass_superstreak_plane.mesh", "spotlight_draw.grp", "unison_phrase_hit.grp", "smasher_fx.grp"})
        if (const scene::Object* o = obj(n)) pose_.setShowing(o, false);
    if (const scene::Object* o = obj(drums_ ? "guitar_draw.grp" : "drum_draw.grp")) pose_.setShowing(o, false);
    camera_.world = gfx::toMat4(pose_.world(cam_));
    camera_.yFov = cam_->yFov, camera_.nearPlane = cam_->nearPlane, camera_.farPlane = cam_->farPlane;
    camera_.aspect = float(viewW_) / float(viewH_);

    drawGroup(mr, cache, get<scene::Group>(drawGroup_), trackEnv_);
    drawBeatLines(mr, cache, song, nowMs);
    drawGroup(mr, cache, smasherGroup_, trackEnv_);
    drawGems(mr, cache, player, nowMs);
    if (sub) r.device().setViewport(vx, vy, vw, vh);
    viewW_ = vw, viewH_ = vh;
}
