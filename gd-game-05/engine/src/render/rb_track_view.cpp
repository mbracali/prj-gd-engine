// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/rb_track_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <functional>
#include <cmath>
#include <cstring>
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
    // The drums' four pads across the track (slots 1..4), the kick (slot 0)
    // its middle [I: the drum TrackDir shares the guitar's five slots; the
    // pads' spacing is the smasher plate's pads, not read].
    if (drums_ && slotX_[4] > slotX_[1]) {
        const float lo = slotX_[0], hi = slotX_[4], step = (hi - lo) / 4.0f;
        slotX_ = {0.5f * (lo + hi), lo + step * 0.5f, lo + step * 1.5f, lo + step * 2.5f, lo + step * 3.5f};
    }

    // config/track_graphics.dtb (gem (gems (guitar GUITAR_SET) (drum DRUM_SET)))
    // [C data]: per slot the widget of each kind.
    const DataNode cfg = parseDtb(data.read("config/gen/track_graphics.dtb"));
    if (const DataNode* sc = cfg.find("slot_colors"))
        for (size_t i = 0; i < 5 && i + 1 < sc->children.size(); ++i) slotColors_[i] = sc->textAt(i + 1);
    const DataNode* gem = cfg.find("gem");
    // The tails' values (TailBuilder RB 0x8281A970 reads them from (gem ...)) [C].
    if (gem) {
        auto num = [&](const char* k, float& v) {
            if (const DataNode* n = gem->find(k); n && n->children.size() > 1) v = n->floatAt(1);
        };
        num("tail_pulse_rate", tailCfg_.pulseRate), num("tail_pulse_smoothing", tailCfg_.pulseSmoothing);
        num("tail_offset_x", tailCfg_.offsetX), num("tail_min_alpha", tailCfg_.minAlpha), num("tail_max_alpha", tailCfg_.maxAlpha);
        num("tail_alpha_smoothing", tailCfg_.alphaSmoothing), num("tail_fade_distance", tailCfg_.fadeDistance);
        num("tail_min_freq", tailCfg_.minFreq), num("tail_max_freq", tailCfg_.maxFreq);
        num("tail_min_amp", tailCfg_.minAmp), num("tail_max_amp", tailCfg_.maxAmp);
    }
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
    colorSmashers();
    // The sustain tails' template and materials (track/gem_tail.milo) [C data].
    try {
        tails_ = &library.load("track/gen/gem_tail.milo_xbox");
        // tail02.mesh's first row (its lowest y), by x: the cross-section
        // every row of the tail repeats (0x8281A8C8 copies its vertices) [C].
        if (const scene::Mesh* t = tails_->find<scene::Mesh>("tail02.mesh"); t && t->verts.size() >= 4) {
            float y0 = 1e9f, y1 = 1e9f;
            for (const auto& v : t->verts) y0 = std::min(y0, v.pos[1]);
            for (const auto& v : t->verts)
                if (v.pos[1] > y0 + 1e-3f) y1 = std::min(y1, v.pos[1]);
            for (const auto& v : t->verts)
                if (std::fabs(v.pos[1] - y0) < 1e-3f) tailRow_.push_back({v.pos[0], v.pos[2], v.uv[0], v.uv[1]});
            for (const auto& v : t->verts)
                if (y1 < 1e9f && std::fabs(v.pos[1] - y1) < 1e-3f) tailRowV1_ = v.uv[1];
            std::sort(tailRow_.begin(), tailRow_.end(), [](const auto& a, const auto& b) { return a[0] < b[0]; });
        }
        if (SDL_getenv("GD05_TRACKLOG"))
            for (const char* n : {"tail_red.mat", "tail_glow_red.mat", "tail_star.mat"})
                if (const scene::Mat* mt = tails_->find<scene::Mat>(n))
                    SDL_Log("RB track: %s blend %d color %.2f %.2f %.2f %.2f tex %s texgen %d z %d cull %d next %s", n, mt->blend, double(mt->color[0]), double(mt->color[1]),
                            double(mt->color[2]), double(mt->color[3]), mt->diffuseTex.c_str(), mt->texGen, mt->zMode, int(mt->cull), mt->nextPass.c_str());
        if (SDL_getenv("GD05_TRACKLOG"))
            if (const scene::Mesh* t = tails_->find<scene::Mesh>("tail02.mesh"))
                for (const auto& v : t->verts)
                    SDL_Log("RB track: tail02 vert %.2f %.2f %.2f uv %.2f %.2f", double(v.pos[0]), double(v.pos[1]), double(v.pos[2]), double(v.uv[0]), double(v.uv[1]));
        // Its materials' textures are track_shared's too [I].
        scene::Milo& shared = library.load("track/gen/track_shared.milo_xbox");
        auto& linked = tails_->root().linked;
        if (std::find(linked.begin(), linked.end(), &shared.root()) == linked.end()) linked.push_back(&shared.root());
    } catch (const std::exception& e) {
        SDL_Log("RB track: gem_tail.milo: %s", e.what());
    }
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
                SDL_Log("  draw %s %s mat %s tex %s (%s) blend %d alpha %.2f color %.2f %.2f %.2f texgen %d z %d env %s next %s verts %d", o ? o->cls.c_str() : "?",
                        n.c_str(), m ? m->mat.c_str() : "", mt ? mt->diffuseTex.c_str() : "", tx ? tx->cls.c_str() : "missing", mt ? mt->blend : -1,
                        mt ? double(mt->color[3]) : 0.0, mt ? double(mt->color[0]) : 0.0, mt ? double(mt->color[1]) : 0.0, mt ? double(mt->color[2]) : 0.0,
                        mt ? mt->texGen : -1, mt ? mt->zMode : -1, mt ? mt->environMap.c_str() : "", mt ? mt->nextPass.c_str() : "", m ? int(m->verts.size()) : 0);
            }
    SDL_Log("RB track %s: y_per_second %.1f, y %.1f..%.1f, slots %.2f %.2f %.2f %.2f %.2f, draw %s, anim %s, camera %s", part.c_str(),
            double(yPerSecond_), double(bottomY_), double(topY_), double(slotX_[0]), double(slotX_[1]), double(slotX_[2]), double(slotX_[3]),
            double(slotX_[4]), drawGroup_.c_str(), animGroup_.c_str(), cam_->name.c_str());
}


// PropAnim (RB rev 5) [C data, read here only as far as the smashers need,
// checked against every PropAnim of tracksystem.milo]: the object header,
// the RndAnimatable part, then keysets: the target's name, its property
// path (a one-symbol DataArray: 11 bytes then the symbol), the key type (0
// float, 1 colour, 2 object; 3 and 5 are not read: the keysets after one
// are skipped), a u32, for objects one more, the keys (value, frame), then
// one u32 (two after colour keys).
bool RbTrackView::applyPropAnim(const scene::Object& anim, float frame) {
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
        std::string target, prop;
        uint32_t ktype, count;
        if (!str(target)) return false;
        p += 11;
        if (!str(prop) || !u32(ktype) || !u32(v)) return false;
        if (ktype == 2 && !u32(v)) return false;
        if (!u32(count) || count > 4096) return false;
        const scene::Mat* mat = anim.dir ? anim.dir->resolve<scene::Mat>(target) : nullptr;
        if (ktype == 2) {
            std::string pick;
            for (uint32_t i = 0; i < count; ++i) {
                std::string name;
                float kf;
                if (!str(name) || !f32(kf)) return false;
                if (i == 0 || kf <= frame + 1e-3f) pick = name;
            }
            p += 4;
            if (mat && prop == "diffuse_tex")
                if (const scene::Tex* t = anim.dir->resolve<scene::Tex>(pick)) pose_.matState(mat).tex = t;
        } else if (ktype == 1) {
            std::array<float, 4> out{};
            std::array<float, 5> prev{};
            bool have = false;
            for (uint32_t i = 0; i < count; ++i) {
                std::array<float, 5> cur{};
                for (float& x : cur)
                    if (!f32(x)) return false;
                if (!have || cur[4] <= frame) {
                    out = {cur[0], cur[1], cur[2], cur[3]};
                } else if (prev[4] <= frame && cur[4] > frame) {
                    const float t = (frame - prev[4]) / (cur[4] - prev[4]);
                    for (size_t c = 0; c < 4; ++c) out[c] = prev[c] + (cur[c] - prev[c]) * t;
                }
                prev = cur, have = true;
            }
            p += 8;
            if (mat && prop == "color" && have) {
                gfx::ScenePose::MatState& st = pose_.matState(mat);
                st.hasColor = true;
                st.rgb[0] = out[0], st.rgb[1] = out[1], st.rgb[2] = out[2];
                st.hasAlpha = true;
                st.alpha = out[3];
            }
        } else if (ktype == 0) {
            p += size_t(count) * 8 + 4;
        } else {
            return true;  // a key type the smashers' colours do not need: the rest is skipped
        }
        if (p > r.size()) return false;
    }
    return true;
}

void RbTrackView::colorSmashers() {
    // gem_smasher.dta set_color [C data]: particle_slot_colors.anim at the
    // colour's index (green 0 .. orange 4), smasher_glow.anim at 20 x it.
    // The guitar's smashers are the slots' colours (slot_colors); the drums'
    // kick (gem_smasher0) orange, the pads red, yellow, blue, green [I: the
    // native caller of set_color is not read].
    static const char* kNames[5] = {"green", "red", "yellow", "blue", "orange"};
    for (const auto& [owner, plate] : track_->root().subdirs) {
        if (!plate || owner != (drums_ ? "smasher_plate_drum" : "smasher_plate_guitar")) continue;
        for (const auto& [o2, d] : plate->subdirs) {
            if (!d || o2.rfind("gem_smasher", 0) != 0 || o2.size() != 12) continue;
            const int slot = o2[11] - '0';
            if (slot < 0 || slot > 4) continue;
            if (drums_)
                if (const scene::Object* holder = plate->get(o2)) drumX_[size_t(slot)] = pose_.world(holder).pos[0];
            std::string color = drums_ ? (slot == 0 ? "orange" : slot == 1 ? "red" : slot == 2 ? "yellow" : slot == 3 ? "blue" : "green") : slotColors_[size_t(slot)];
            int index = 0;
            for (int i = 0; i < 5; ++i)
                if (color == kNames[i]) index = i;
            bool ok = true;
            if (const scene::Object* a = d->get("particle_slot_colors.anim")) ok = applyPropAnim(*a, float(index)) && ok;
            if (const scene::Object* a = d->get("smasher_glow.anim")) ok = applyPropAnim(*a, float(20 * index)) && ok;
            if (!ok) SDL_Log("RB track: %s: a PropAnim was not read", o2.c_str());
        }
    }
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
            if (static int n = 0; SDL_getenv("GD05_TRACKLOG") && n < 40 && depth > 0) {
                ++n;
                const scene::Mat* mt = m->dir ? m->dir->resolve<scene::Mat>(m->mat) : nullptr;
                SDL_Log("  smasher mesh %s (%s) mat '%s' %s color %.2f %.2f %.2f %.2f tex %s blend %d verts %d vcol %.2f %.2f %.2f %.2f", m->name.c_str(),
                        m->dir ? m->dir->name.c_str() : "", m->mat.c_str(), mt ? "ok" : "none", mt ? double(mt->color[0]) : 0.0, mt ? double(mt->color[1]) : 0.0,
                        mt ? double(mt->color[2]) : 0.0, mt ? double(mt->color[3]) : 0.0, mt ? mt->diffuseTex.c_str() : "", mt ? mt->blend : -1, int(m->verts.size()),
                        m->verts.empty() ? 0.0 : double(m->verts[0].color[0]), m->verts.empty() ? 0.0 : double(m->verts[0].color[1]),
                        m->verts.empty() ? 0.0 : double(m->verts[0].color[2]), m->verts.empty() ? 0.0 : double(m->verts[0].color[3]));
            }
            setEnv(mr, cache, env);
            gfx::drawMesh(mr, cache, *m, gfx::toMat4(pose_.world(m)), &pose_);
        } else if (auto* sub = o->as<scene::Group>()) {
            drawGroup(mr, cache, sub, env, depth + 1);
        } else if (o->as<scene::DirObj>()) {
            // An inline RndDir in the group (the smasher plate's
            // gem_smashers.grp: gem_smasher0..4, one fret each): its own
            // draw_order.grp under the object's transform [C data; I: RndDir
            // draws that group].
            for (const auto& [owner, d] : g->dir->subdirs)
                if (d && owner == o->name) {
                    pose_.setDirBase(d.get(), o);
                    if (const scene::Object* dg = d->get("draw_order.grp"))
                        if (auto* dgg = dg->as<scene::Group>()) drawGroup(mr, cache, dgg, env, depth + 1);
                }
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
        // RB colours the gems by the meshes themselves (each slot's mesh maps
        // its colour of prism_gems.tex): the slot colours swap materials only
        // for the tails (0x82819A48, called by the TailBuilder 0x8281A970)
        // [C]. Swapping prism_mat for prism_mat_<colour> drew the green gems
        // with prism_green.tex through the atlas' coordinates (GD05-029).
        (void)colorSlot;
        gfx::drawMesh(mr, cache, *m, gfx::toMat4(x0), &pose_);
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
        for (int b = 0; b < 5; ++b) {
            if (!(g.frets & (1u << b))) continue;
            const int f = player.laneOfBit[size_t(b)];
            const SlotSet& s = sets_[size_t(f)];
            const scene::TrackWidget* w = missed ? (hopo ? s.missHopo : s.miss) : star ? (hopo ? s.starHopo : s.star) : hopo ? s.hopo : s.normal;
            if (!w) w = s.normal;
            // The kick's widget spans the track from its middle [I].
            // Colour: the guitar's slot f; the drums' pads are red, yellow, blue, green (lanes 1-4), the kick orange [C data: slot_colors; I: the drums' order].
            const int color = missed || star ? -1 : drums_ ? (f == 0 ? 4 : f) : f;
            // The drums' four pads between the five slots (-6, -2, 2, 6 for
            // slots -8..8) [I: RB's drum slot positions are not read].
            // The drums' pads where the drum smashers stand (-7.5 -2.5 2.5
            // 7.5) [C data]; wave 01 put them at -6 -2 2 6 [I].
            const float x = !drums_ ? slotX(f) : (lefty_ ? -drumX_[size_t(f)] : drumX_[size_t(f)]);
            drawWidget(mr, cache, w, x, y, color);
        }
    }
}

// One tail's poll (RB 0x8281C928) [C]: while held, at pulse_rate samples a
// second (TaskMgr time; the port's song clock), the whammy smoothed by
// pulse_smoothing, the frequency and amplitude between their min and max
// by it, a sample sin(phase) x amplitude written to the 300-entry ring and
// the phase advanced by frequency / pulse_rate; the alpha smoothed towards
// the whammy by alpha_smoothing once a poll, between min and max alpha.
// Not held: phase 0, alpha min_alpha. [I: the lerps' direction (the
// decompiler shows fnmsubs); the held / whammy states 2 and 3 as held.]
void RbTrackView::pollTail(TailState& t, bool held, float whammy, float nowMs) {
    const TailCfg& c = tailCfg_;
    if (!held) {
        t.phase = 0, t.smoothA = 0, t.clockMs = -1;
        return;
    }
    const double step = 1000.0 / std::max(1.0f, c.pulseRate);
    if (t.clockMs < 0 || nowMs < t.clockMs - 1000) t.clockMs = nowMs;
    for (int guard = 0; t.clockMs + step <= nowMs && guard < 256; ++guard) {
        t.clockMs += step;
        t.smoothW += (whammy - t.smoothW) * c.pulseSmoothing;
        const float freq = c.minFreq + (c.maxFreq - c.minFreq) * t.smoothW;
        const float amp = c.minAmp + (c.maxAmp - c.minAmp) * t.smoothW;
        t.head = (t.head + 1) % 300;
        t.ring[size_t(t.head)] = std::sin(t.phase) * amp;
        t.phase += freq / std::max(1.0f, c.pulseRate);
    }
    t.smoothA += (whammy - t.smoothA) * c.alphaSmoothing;
}

// The sustain tails as RB builds them (0x8281C4A0) [C]: tail02.mesh's
// cross-section repeated every segment (90 of 4.444), from the gem (the now
// bar while held) to the tail's end; a row d from the start is (1 +
// ring[head - d / 2]) wide (the wobble runs down the tail), the last
// fade_distance fade out, the alpha the poll's. tail_<colour>.mat (the
// slot's colour, 0x82819A48), tail_miss once missed; an Overdrive phrase's
// tail takes tail_star's colour [I: RB blends it by a value not read]. The
// glow line (tail_glow_*) is not drawn [I]. The drums have no tails.
void RbTrackView::drawTails(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const PlayerView& player, float nowMs) {
    if (!player.chart || !tails_ || drums_) return;
    const scene::Mesh* tmpl = tails_->find<scene::Mesh>("tail02.mesh");
    if (!tmpl) return;
    if (nowMs < lastTailMs_ - 1000) tailStates_.clear();  // a restart
    lastTailMs_ = nowMs;
    tailVerts_.clear(), tailIndices_.clear(), tailRanges_.clear();
    const auto& gems = player.chart->gems;
    const float aheadMs = topY_ / (yPerSecond_ * speed_) * 1000.0f;
    auto last = std::lower_bound(gems.begin(), gems.end(), nowMs + aheadMs, [](const Gem& g, float v) { return g.ms < v; });
    const TailCfg& c = tailCfg_;
    static const std::vector<std::array<float, 4>> kFlat{{-0.5f, 0.05f, 0, 0}, {0.5f, 0.05f, 1, 0}};
    const auto& row = tailRow_.size() >= 2 ? tailRow_ : kFlat;
    for (auto& [k, t] : tailStates_) t.seen = false;
    for (auto it = gems.begin(); it != last; ++it) {
        Gem g = *it;
        // Development aid: GD05_TAILTEST=1 gives every gem a 600 ms tail.
        static const bool tailTest = SDL_getenv("GD05_TAILTEST") != nullptr;
        if (tailTest && !drums_) g.noSustain = false, g.durationMs = 600;
        if (g.noSustain || g.durationMs <= 0) continue;
        const float endRel = yOf(g.ms + g.durationMs - nowMs);
        if (endRel < bottomY_) continue;
        const int i = int(it - gems.begin());
        const bool hit = player.hit && player.hit(i);
        const bool held = hit && player.heldGem == i;
        if (hit && !held) continue;  // done or let go
        const bool missed = player.missed && player.missed(i);
        const bool star = !missed && g.phrase >= 0 && (!player.phraseIntact || player.phraseIntact(g.phrase));
        const float y0 = held ? 0.0f : std::max(yOf(g.ms - nowMs), bottomY_);
        const float y1 = std::min(endRel, topY_);
        if (y1 <= y0 + 0.01f) continue;
        TailState& ts = tailStates_[long(i)];
        ts.seen = true;
        pollTail(ts, held, player.whammy, nowMs);
        const float alpha = held ? c.minAlpha + (c.maxAlpha - c.minAlpha) * ts.smoothA : c.minAlpha;
        const float len = endRel - y0;  // the whole tail, its far end may be past the top
        for (int b = 0; b < 5; ++b) {
            if (!(g.frets & (1u << b))) continue;
            const int f = player.laneOfBit[size_t(b)];
            const std::string name = missed ? "tail_miss.mat" : "tail_" + slotColors_[size_t(f)] + ".mat";
            const scene::Mat* mat = tails_->find<scene::Mat>(name);
            if (!mat) continue;
            const scene::Mat* starMat = star ? tails_->find<scene::Mat>("tail_star.mat") : nullptr;
            const float* col = starMat ? starMat->color.data() : mat->color.data();
            const float x = slotX(f) + c.offsetX;
            const uint16_t base = uint16_t(tailVerts_.size() / 16);
            const size_t n = row.size();
            int rows = 0;
            for (int sgi = 0; sgi <= c.segments; ++sgi) {
                float d = float(sgi) * c.segLen;
                const bool lastRow = d >= y1 - y0;
                if (lastRow) d = y1 - y0;
                const float y = y0 + d;
                // 0x8281BC48: a row's vertices are the template row's, moved
                // sideways by the row's sample (x + (1 + sample) - 1), its uv
                // kept; the last row (0x8281BCD8) closes at the centre.
                const float shift = held ? ts.ring[size_t(((ts.head - int(d * 0.5f)) % 300 + 300) % 300)] : 0.0f;
                float a = alpha;
                if (len - d < c.fadeDistance) a *= std::max(0.0f, (len - d) / c.fadeDistance);
                for (const auto& p : row)
                    tailVerts_.insert(tailVerts_.end(),
                                      {x + (lastRow ? 0.0f : p[0]) + shift, y, p[1], 0, 0, 1, col[0], col[1], col[2], col[3] * a, p[2], p[3], 1, 0, 0, 1});
                ++rows;
                if (lastRow) break;
            }
            if (base + rows * n > 65000) break;
            const int first = int(tailIndices_.size());
            for (int r2 = 0; r2 + 1 < rows; ++r2)
                for (size_t q = 0; q + 1 < n; ++q) {
                    const uint16_t a0 = uint16_t(base + r2 * n + q), a1 = uint16_t(a0 + 1), b0 = uint16_t(a0 + n), b1 = uint16_t(b0 + 1);
                    for (uint16_t k : {a0, a1, b1, a0, b1, b0}) tailIndices_.push_back(k);
                }
            tailRanges_.push_back({mat, first, int(tailIndices_.size()) - first});
        }
    }
    for (auto it = tailStates_.begin(); it != tailStates_.end();) it = it->second.seen ? std::next(it) : tailStates_.erase(it);
    if (static int logs = 0; SDL_getenv("GD05_TRACKLOG") && logs < 6 && !tailRanges_.empty()) {
        ++logs;
        SDL_Log("RB track: %d tail strips, %d verts, held gem %d, row %d verts x %.2f..%.2f z %.2f, v1 %.2f", int(tailRanges_.size()), int(tailVerts_.size() / 16),
                player.heldGem, int(row.size()), double(row.front()[0]), double(row.back()[0]), double(row.front()[1]), double(tailRowV1_));
    }
    if (tailRanges_.empty()) return;
    if (!tailMesh_) tailMesh_ = std::make_unique<gfx::DynamicMesh>(cache.device());
    const gfx::GpuMesh all = tailMesh_->upload(tailVerts_, tailIndices_);
    setEnv(mr, cache, trackEnv_);
    for (const TailRange& t : tailRanges_) {
        gfx::GpuMesh part = all;
        part.firstIndex = t.firstIndex, part.indexCount = t.indexCount;
        gfx::drawMesh(mr, cache, *tmpl, gfx::Mat4::identity(), &pose_, t.mat, nullptr, &part);
    }
}

void RbTrackView::draw(gfx::Renderer& r, gfx::SceneCache& cache, const Song& song, const PlayerView& player, float nowMs, float trackSpeed) {
    if (!track_) return;
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    const std::array<float, 4> rect = hasLayout_ ? layout_ : screenRect_;
    const bool sub = rect != std::array<float, 4>{0, 0, 1, 1};
    viewW_ = std::max(1, int(float(vw) * rect[2])), viewH_ = std::max(1, int(float(vh) * rect[3]));
    if (sub) r.device().setViewport(vx + int(float(vw) * rect[0]), vy + int(float(vh) * rect[1]), viewW_, viewH_);
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
    // The intro (tracksystem.milo intro.trig -> intro_anim.grp, 30 frames a
    // second [I: the rate]): camera_intro.tnm brings cameras.grp from 300
    // down the track to 0 by frame 20 (an overshoot to -6 at 15) and
    // intro_anim.anim shows static.grp from frame 0.1 [C data]. Retracted
    // before it: not drawn (track_panel.dta set_track_in) [I].
    if (introAtMs_ > -1e29f) {
        const float f = (nowMs - introAtMs_) * 0.03f;
        if (f < 0) {
            if (sub) r.device().setViewport(vx, vy, vw, vh);
            viewW_ = vw, viewH_ = vh;
            return;
        }
        if (const scene::Object* ia = obj("intro_anim.grp")) pose_.setFrame(ia, std::min(f, 20.0f));
        if (const scene::Object* st = obj("static.grp")) pose_.setShowing(st, f >= 0.1f);
    }
    camera_.world = gfx::toMat4(pose_.world(cam_));
    camera_.yFov = cam_->yFov, camera_.nearPlane = cam_->nearPlane, camera_.farPlane = cam_->farPlane;
    camera_.aspect = float(viewW_) / float(viewH_);
    if (static bool logged = false; !logged && SDL_getenv("GD05_TRACKLOG")) {
        logged = true;
        const gfx::Mat4 vp = camera_.viewProj(false);
        for (float y : {bottomY_, 0.0f, topY_}) {
            const float cy = vp.at(1, 1) * y + vp.at(1, 3), cw = vp.at(3, 1) * y + vp.at(3, 3);
            SDL_Log("RB track: y %.1f at screen %.3f (0 top .. 1 bottom), view %d x %d rect %.2f %.2f %.2f %.2f", double(y), double(0.5f - 0.5f * cy / cw),
                    viewW_, viewH_, double(rect[0]), double(rect[1]), double(rect[2]), double(rect[3]));
        }
        if (smasherGroup_) {
            SDL_Log("RB track: smasher group %s showing %d, %d objects", smasherGroup_->name.c_str(), int(pose_.showing(smasherGroup_)), int(smasherGroup_->objects.size()));
            std::function<void(const scene::Group*, int)> dump = [&](const scene::Group* g, int depth) {
                for (const std::string& n : g->objects) {
                    const scene::Object* o = g->dir ? g->dir->resolve(n) : nullptr;
                    if (!o) { SDL_Log("%*s%s: missing", depth * 2, "", n.c_str()); continue; }
                    const scene::Xfm w = pose_.world(o);
                    const scene::Trans* t = gfx::transOf(o);
                    SDL_Log("%*s%s %s showing %d at %.2f %.2f %.2f parent %s", depth * 2, "", o->cls.c_str(), n.c_str(), int(pose_.showing(o)), double(w.pos[0]), double(w.pos[1]),
                            double(w.pos[2]), t ? t->parent.c_str() : "-");
                    if (auto* sg = o->as<scene::Group>(); sg && depth < 3) dump(sg, depth + 1);
                }
            };
            dump(smasherGroup_, 1);
        }
        else SDL_Log("RB track: no smasher group");
    }

    drawGroup(mr, cache, get<scene::Group>(drawGroup_), trackEnv_);
    drawBeatLines(mr, cache, song, nowMs);
    drawGroup(mr, cache, smasherGroup_, trackEnv_);
    drawTails(mr, cache, player, nowMs);
    drawGems(mr, cache, player, nowMs);
    if (sub) r.device().setViewport(vx, vy, vw, vh);
    viewW_ = vw, viewH_ = vh;
}
