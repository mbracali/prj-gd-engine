// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/track_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "data/dta.h"
#include "data/game_data.h"
#include "render/renderer.h"
#include "scene/milo.h"

// How GH2's track code (default.xex) places things; [C] read in the code at
// the address, [I] inferred, [?] open.
//
// - Units: y = song ms x 0.06 x track_speed (0x82699D50) [C]; the track's
//   y_per_second becomes 60 x track_speed at init (0x82363328 from
//   0x826967B8) [C], track_speed being track_graphics.dtb's value for the
//   difficulty (0x826932D0) [C]. TrackDir::Poll (0x823634D0) sets anim.grp
//   to frame now_y (rig_movement.tnm moves the rig, the camera and the
//   surface along; the surface texture scroll, the gem glow and the star
//   spin are filters in anim.grp) [C]. Gems and tails stay at absolute y;
//   here everything is drawn relative to the now bar (rig at rest), which
//   is the same picture.
// - Gems (0x826989A8) [C]: added while start_y < now_y + horizon_y (120),
//   freed when end_y < now_y + remove_y (-15); a widget instance is dropped
//   once y + base_length < now_y + bottom_y (TrackWidget poll 0x823662B0).
//   One instance per fret at the slot's x (TrackDir slots), z 0. A hit gem's
//   instance is removed at once (0x8269E2C8); a passed gem (gem-pass
//   callback 0x822E2FD0 -> 0x82695AA0) shows the miss widget.
// - Widget choice (0x826912E0) [C]: gem / hopo lists, index 5 (bonus) during
//   Star Power, 6 (miss) when missed, else the slot; star / star_hopo lists
//   for an intact Star Power phrase, index 5 (miss_star) when missed. HOPO
//   look: the chart's HOPO flag and not the first gem (0x82699348) [C].
//   Star: in a phrase whose "missed" latch is clear (0x82697620) [C]; Star
//   Power re-types live, unmissed gems to bonus (set_bonus_gems 0x82697D30).
// - Tails (template 0x826A0100, mesh 0x8269C2A0, rebuild 0x8269EAD0) [C]:
//   tail02.mesh's cross-section, 30 segments of (horizon_y - remove_y) x
//   1.01 / 30; the tail starts `clip` past the gem (2.0; while sustained
//   now_y + nowbar_tail_clip - start; frozen at release) and ends at the
//   gem's end or the horizon; the last row is the pointed tip. Whammy
//   (0x8269EAD0) [C]: each tail keeps 100 whammy samples; every poll while
//   it is hit (state 2 held, 3 released) the next one is the bar's value x
//   -3.0. The value is the player's (0x822C6FF0: player +0x108, the whammy
//   poll's 0x822C8438), 0 at rest down to -1 fully pressed (the pitch bend
//   0x8234FF18 takes (value - rest) / (rest + 1), -1 at the bottom), so a
//   sample is 0 .. 3 and a row at distance d from the tail's start is (1 +
//   sample[d / 2 polls ago]) wide: the tail swells and the bulges run down
//   it; the row 3 units before the tip takes half its sample. The glow
//   line's width eases (x 0.2 a poll) to its width x (1 + (widest - 1) / 2)
//   while hit, 1 otherwise. A Star Power phrase's tail blends its colour to
//   tail_star's, its glow from tail_glow_star's to tail_glow_whammy's, by
//   the panel's whammy blend (+-0.1 a poll toward the player's whammy flag);
//   tail_bonus.part emits 2 x that blend. Materials (0x8269E7F0 / 0x8269E2C8 / 0x8269E340):
//   tail_<colour> (tail_bonus for bonus gems, tail_miss when missed or
//   released); a glow line (line01) on the tail at z 0.16: shown while
//   held (tail_glow_<colour> / _star / _bonus, width 1.5) and always on a
//   Star Power tail (tail_glow_tight, width 0.7).
// - Smashers (0x8269D110) [C]: per slot gem_smasher.mesh (gem_smasher.mat,
//   gem_smasher_<colour>.mat while the fret is held) and smasher_rim.mesh
//   (now_ring_<colour>.mat); gem_smasher_hit.tnm on a hit (0x8269CF00),
//   gem_smasher.tnm on a miss with a fret (0x82697958); 30 fps.
// - Hit effects (0x8269B058, 0x8269C968) [C]: per fret the smash view at the
//   slot (smash_bonus in Star Power, smash_star for Star Power gems, else
//   smash_normal) at 30 fps; while a sustain is held burn_tail.view, its
//   state frame 10 (whammy on a Star Power tail) / 20 (bonus) / 0 (0x826A0868).
//   Their particle systems run as per-slot copies (render/particles.h).
// - Beat lines (0x82699F40, 0x8235C890) [C]: one every 240 ticks: half_beat
//   between beats, beat_line on beats, bar_line on measures; none ahead
//   before song time -2000 ms, ramping in until -1000 ms.
// - Side rails (0x8269A2F8) [C]: state none / star (Star Power) / warning
//   (rock meter warning) / warning_star, frame = song beats [mapping I].
// - lightning.grp shows only during Star Power (0x82695AF8) [C].
// - phrase_captured (0x822DC090 sets the phrase state 3 on the phrase's last
//   gem; 0x8269B058 with that state) [C]: per slot of that gem combo1
//   (smash_combo.view, its burst particles) and combo2
//   (smash_combo_lightning.view, the lightning, its slot copy turned by
//   (slot - 2) x combo2_angle, mirrored lefty,
//   0x8269D8C8) start at the hit, at (now - hit) x 0.03; combo2 ends at its
//   view's last frame (0x8269D758) [C].
// - blow_streak N (0x82696560) [C]: surface_flash_Nx.mnm and
//   track_shake_Nx.tnm when a streak of multiplier N (2..4) ends [I: event].
namespace {

using gfx::Mat4;
using gfx::Vec3;

constexpr float kFrameRate = 30;        // effect timers: ms x 0.03
constexpr float kTailClip = 2.0f;       // gem record +0x14 default [C]
constexpr float kWhammyScale = -3.0f;   // 0x8269EAD0: sample = whammy x -3.0 [C]
constexpr float kPollMs = 1000.0f / 60.0f;  // the game's poll (one a frame at 60 Hz) [I]
constexpr float kGlowZ = 0.16f;

}  // namespace

TrackView::TrackView() = default;
TrackView::~TrackView() = default;

const scene::Object* TrackView::obj(const std::string& name) const { return track_ ? track_->find(name) : nullptr; }

const scene::Mat* TrackView::slotMat(const char* format, int slot) const {
    char name[64];
    SDL_snprintf(name, sizeof name, format, slotColors_[size_t(std::clamp(slot, 0, 4))].c_str());
    return get<scene::Mat>(name);
}

void TrackView::load(const GameData& data, scene::MiloLibrary& library, int player) {
    // ui/track_panel.dta: one player uses track_panel_1p.milo, two players
    // track_panel_2p.milo; each GHTrackDir there is a proxy of
    // track_new.milo whose first field is its player (-1, 0, 1) [C data].
    player_ = player;
    scene::Milo& panel = library.load(player < 0 ? "track/gen/track_panel_1p.milo_xbox" : "track/gen/track_panel_2p.milo_xbox");
    scene::Milo* track = nullptr;
    int index = 0;
    for (const auto& o : panel.root().objects) {
        if (o->cls != "GHTrackDir") continue;
        const auto* d = o->as<scene::DirObj>();
        const int owner = d && d->hasGHTrack ? d->ghTrack.unknown : index;
        ++index;
        if (player >= 0 && owner != player) continue;
        if ((track = library.proxy(panel, *o))) break;
    }
    if (!track) throw std::runtime_error("track panel: no track directory for this player");
    auto* self = track->root().self ? track->root().self->as<scene::DirObj>() : nullptr;
    if (!self || !self->hasTrack || !self->hasGHTrack) throw std::runtime_error("track_new: not a GHTrackDir");
    track_ = track;

    const scene::TrackDirData& td = self->track;
    yPerSecond_ = td.yPerSecond, bottomY_ = td.bottomY;
    for (size_t i = 0; i < slotX_.size() && i < td.slots.size(); ++i) slotX_[i] = td.slots[i][0];
    auto widgets = [&](const std::vector<std::string>& names) {
        std::vector<const scene::TrackWidget*> v;
        for (const std::string& n : names) v.push_back(get<scene::TrackWidget>(n));
        return v;
    };
    gemW_ = widgets(self->ghTrack.gemWidgets);
    hopoW_ = widgets(self->ghTrack.hopoWidgets);
    starW_ = widgets(self->ghTrack.starWidgets);
    starHopoW_ = widgets(self->ghTrack.starHopoWidgets);
    barW_ = get<scene::TrackWidget>("bar_line.wid");
    beatW_ = get<scene::TrackWidget>("beat_line.wid");
    halfBeatW_ = get<scene::TrackWidget>("half_beat_line.wid");

    // config/track_graphics.dtb.
    const DataNode cfg = parseDtb(data.read("config/gen/track_graphics.dtb"));
    auto num = [&](const DataNode* parent, const char* key, float& out) {
        if (const DataNode* n = parent ? parent->find(key) : nullptr; n && n->children.size() > 1) out = n->floatAt(1);
    };
    num(&cfg, "horizon_y", horizonY_);
    num(&cfg, "remove_y", removeY_);
    num(&cfg, "nowbar_tail_clip", nowbarTailClip_);
    const DataNode* gem = cfg.find("gem");
    num(gem, "fps", gemFps_);
    num(gem, "tail_glow_width", tailGlowWidth_);
    num(gem, "tail_glow_tight_width", tailGlowTightWidth_);
    num(cfg.find("particles"), "combo2_angle", combo2Angle_);
    if (const DataNode* sc = cfg.find("slot_colors"))
        for (size_t i = 0; i < 5 && i + 1 < sc->children.size(); ++i) slotColors_[i] = sc->textAt(i + 1);

    // The tail template (0x826A0100): tail02.mesh's vertices sorted, the
    // first half (one row) kept as the cross-section.
    if (const scene::Mesh* t = get<scene::Mesh>("tail02.mesh"); t && t->verts.size() >= 4) {
        std::vector<scene::MeshVertex> v = t->verts;
        std::sort(v.begin(), v.end(), [](const scene::MeshVertex& a, const scene::MeshVertex& b) {
            return a.pos[1] != b.pos[1] ? a.pos[1] < b.pos[1] : a.pos[0] < b.pos[0];
        });
        tailRow_.assign(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2));
    }
    tailSegLen_ = (horizonY_ - removeY_) * 1.01f / float(tailSegments_);

    // The camera: the single-player one, or the player's of the
    // multiplayer list (track_p1 / track_p2.cam, whose screen rects are the
    // left and right halves) [C data; selection I]. Its planes come from
    // track_graphics (cam (near_plane 50) (far_plane 200)) [C 0x82696330].
    std::string camName = self->ghTrack.spCamera.empty() ? "track.cam" : self->ghTrack.spCamera;
    if (player >= 0) camName = size_t(player) < self->ghTrack.mpCameras.size() ? self->ghTrack.mpCameras[size_t(player)]
                                                                               : (player == 0 ? "track_p1.cam" : "track_p2.cam");
    cam_ = get<scene::Cam>(camName);
    if (!cam_) throw std::runtime_error("track_new: no track camera " + camName);
    screenRect_ = cam_->screenRect;
    if (const DataNode* cam = cfg.find("cam")) {
        num(cam, "near_plane", nearPlane_);
        num(cam, "far_plane", farPlane_);
    }
    trackEnv_ = envOf(get<scene::Group>("draw.grp"), nullptr);
    reset();
}

void TrackView::reset(float introStartMs) {
    pose_.clear();
    lanesSet_ = false;
    hitMs_.fill(-1e9f);
    missMs_.fill(-1e9f);
    gemState_.clear();
    releaseClip_.clear();
    tailWhammy_.clear();
    whammyPolls_ = 0, whammyClockMs_ = -1e9f, starWhammyBlend_ = 0;
    blowMs_ = -1e9f, blowLevel_ = 0;
    introStartMs_ = introStartMs;
    extendAnim_ = {};
    starSmashes_ = starGlowLines_ = comboStarts_ = 0;
    explodeMs_ = -1e9f;
    // The effect copies (0x8269C968: one smash view of each kind per slot,
    // and the burn view), and the explode view's systems.
    static const char* const kViews[3] = {"smash_normal.view", "smash_star.view", "smash_bonus.view"};
    for (int f = 0; f < 5; ++f) {
        for (int k = 0; k < 3; ++k) {
            Smash& sm = smash_[size_t(f)][size_t(k)];
            sm.active = false, sm.startMs = -1e9f;
            sm.parts.build(obj(kViews[k]), uint32_t(f * 3 + k + 1));
        }
        burn_[size_t(f)].build(obj("burn_tail.view"), uint32_t(100 + f));
        for (Smash* c : {&combo1_[size_t(f)], &combo2_[size_t(f)]}) c->active = false, c->startMs = -1e9f;
        combo1_[size_t(f)].parts.build(obj("smash_combo.view"), uint32_t(400 + f));
        combo2_[size_t(f)].parts.build(obj("smash_combo_lightning.view"), uint32_t(410 + f));
        burnOn_[size_t(f)] = false;
    }
    tailSparks_.clear();
    explodeParts_.build(obj("track_explode.view"), 200);
}

void TrackView::explode(float nowMs) {
    if (exploded()) return;
    explodeMs_ = nowMs;
    explodeParts_.reset();
}

void TrackView::onEvents(const std::vector<PlayerEvent>& events, const Player& player, float nowMs) {
    const auto& gems = player.chart().gems;
    if (gemState_.size() != gems.size()) gemState_.assign(gems.size(), GemState::Live);
    for (const PlayerEvent& e : events) {
        const bool known = e.gem >= 0 && size_t(e.gem) < gems.size();
        switch (e.kind) {
        case PlayerEvent::Kind::Hit: {
            if (!known) break;
            const Gem& g = gems[size_t(e.gem)];
            const int kind = player.starPowerActive() ? 2 : (g.phrase >= 0 && player.phraseIntact(g.phrase) ? 1 : 0);
            for (int f = 0; f < 5; ++f)
                if (g.frets & (1u << f)) {
                    hitMs_[size_t(f)] = nowMs;
                    // The slot's copy restarts from frame 0: its particles
                    // born after frame 0 die on the next move [C 0x82212A48].
                    Smash& sm = smash_[size_t(f)][size_t(kind)];
                    sm.startMs = nowMs, sm.active = true;
                    if (kind == 1) ++starSmashes_;
                }
            break;
        }
        case PlayerEvent::Kind::PhraseCaptured: {
            if (!known) break;
            const Gem& g = gems[size_t(e.gem)];
            for (int f = 0; f < 5; ++f)
                if (g.frets & (1u << f)) {
                    for (Smash* c : {&combo1_[size_t(f)], &combo2_[size_t(f)]}) c->startMs = nowMs, c->active = true;
                    ++comboStarts_;
                }
            break;
        }
        case PlayerEvent::Kind::Pass:
            if (known) gemState_[size_t(e.gem)] = GemState::Missed;
            break;
        case PlayerEvent::Kind::Miss:
            // A miss pops the smashers of the frets held [I: which slot].
            for (int f = 0; f < 5; ++f)
                if (player.heldFrets() & (1u << f)) missMs_[size_t(f)] = nowMs;
            break;
        case PlayerEvent::Kind::HeldNoteReleased:
            // Released early (0x82697710): the tail turns to tail_miss and
            // its start freezes where it was.
            if (known && e.value < 1.0f) {
                const Gem& g = gems[size_t(e.gem)];
                if (nowMs < g.ms + g.durationMs) {
                    gemState_[size_t(e.gem)] = GemState::Released;
                    releaseClip_[e.gem] = std::max(kTailClip, yOf(nowMs - g.ms));
                }
            }
            break;
        case PlayerEvent::Kind::EndStreak:
            // blow_streak: the multiplier the streak had reached [I].
            if (e.value >= 10) blowMs_ = nowMs, blowLevel_ = std::min(4, 1 + int(e.value) / 10);
            break;
        default: break;
        }
    }
}

void TrackView::setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env) {
    if (env == currentEnv_ && frameSet_) return;
    currentEnv_ = env;
    frameSet_ = true;
    gfx::LightEnv le;
    if (env) le = cache.lightEnv(*env, &pose_);
    mr.setFrame(camera_, le, viewW_, viewH_);
}

const scene::Environ* TrackView::envOf(const scene::Group* g, const scene::Environ* inherited) const {
    if (g && !g->environment.empty() && g->dir)
        if (auto* e = g->dir->resolve<scene::Environ>(g->environment)) return e;
    return inherited;
}

void TrackView::drawGroup(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Group* g, const scene::Environ* env) {
    if (!g || !pose_.showing(g) || !g->dir) return;
    env = envOf(g, env);
    for (const std::string& n : g->objects) {
        const scene::Object* o = g->dir->resolve(n);
        if (!o || o == g || !pose_.showing(o)) continue;
        if (auto* m = o->as<scene::Mesh>()) {
            setEnv(mr, cache, env);
            gfx::drawMesh(mr, cache, *m, gfx::toMat4(pose_.world(m)), &pose_);
        } else if (auto* sub = o->as<scene::Group>()) {
            drawGroup(mr, cache, sub, env);
        }
    }
}

// TrackWidget instances (0x82363A88) [C]: the widget's meshes at the
// instance's position (identity rotation; the meshes' own transforms are
// where they were modelled and are not used; widgets that allow rotation
// keep the meshes' animated rotation, the star spin).
void TrackView::drawWidget(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::TrackWidget* w, float x, float y) {
    if (!w || !w->dir) return;
    for (const std::string& n : w->meshes) {
        const scene::Mesh* m = w->dir->resolve<scene::Mesh>(n);
        if (!m || !pose_.showing(m)) continue;
        scene::Xfm x0;
        if (w->allowRotation) x0.rot = pose_.local(m).rot;
        x0.pos = {x, y, 0};
        gfx::drawMesh(mr, cache, *m, gfx::toMat4(x0), &pose_);
    }
}

void TrackView::drawBeatLines(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Song& song, float nowMs) {
    setEnv(mr, cache, envOf(get<scene::Group>("bar_widgets.grp"), trackEnv_));
    // Look-ahead: none before -2000 ms, full from -1000 ms.
    const float ramp = std::clamp((nowMs + 2000.0f) / 1000.0f, 0.0f, 1.0f);
    const float aheadY = horizonY_ * ramp;
    if (aheadY <= 0) return;
    const int division = std::max(2, song.tempo.division());
    const long half = division / 2;
    const double fromMs = nowMs + (bottomY_ - 1) / (yPerSecond_ * speed_) * 1000.0f;
    const double toMs = nowMs + aheadY / (yPerSecond_ * speed_) * 1000.0f;
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

void TrackView::drawGems(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs) {
    setEnv(mr, cache, envOf(get<scene::Group>("gem_widgets.grp"), trackEnv_));
    const auto& gems = player.chart().gems;
    const bool bonus = player.starPowerActive();
    const float aheadMs = horizonY_ / (yPerSecond_ * speed_) * 1000.0f;
    const float behindMs = (bottomY_ - 1) / (yPerSecond_ * speed_) * 1000.0f;
    auto first = std::lower_bound(gems.begin(), gems.end(), nowMs + behindMs, [](const Gem& g, float v) { return g.ms < v; });
    auto last = std::lower_bound(first, gems.end(), nowMs + aheadMs, [](const Gem& g, float v) { return g.ms < v; });
    // Far to near, so nearer gems draw over farther ones.
    for (auto it = last; it != first;) {
        --it;
        const int i = int(it - gems.begin());
        const Gem& g = *it;
        if (player.gemHit(i) || !player.owned(i)) continue;
        const float y = yOf(g.ms - nowMs);
        const bool missed = size_t(i) < gemState_.size() && gemState_[size_t(i)] == GemState::Missed;
        const bool hopo = g.hopo && i > 0;
        const bool asBonus = bonus && !missed;
        const bool star = !asBonus && g.phrase >= 0 && player.phraseIntact(g.phrase);
        for (int f = 0; f < 5; ++f) {
            if (!(g.frets & (1u << f))) continue;
            const scene::TrackWidget* w = nullptr;
            if (star) {
                const auto& set = hopo ? starHopoW_ : starW_;
                const size_t k = missed ? 5 : size_t(f);
                w = k < set.size() ? set[k] : nullptr;
            } else {
                const auto& set = hopo ? hopoW_ : gemW_;
                const size_t k = asBonus ? 5 : missed ? 6 : size_t(f);
                w = k < set.size() ? set[k] : nullptr;
            }
            drawWidget(mr, cache, w, slotX(f), y);
        }
    }
}

// One fret's tail of gem `gi` (0x8269EAD0): rows of the cross-section from
// the clip to the end, the last row the tip; then the glow line over it.
void TrackView::buildTail(const Gem& g, int gi, int slot, const Player& player, float nowMs) {
    if (tailRow_.empty()) return;
    const float startRel = yOf(g.ms - nowMs);
    const float endRel = yOf(g.ms + g.durationMs - nowMs);
    const bool hit = player.gemHit(gi);
    const bool held = hit && player.sustainGem() == gi;
    const GemState state = size_t(gi) < gemState_.size() ? gemState_[size_t(gi)] : GemState::Live;
    float clip = kTailClip;
    if (held) {
        if (endRel <= nowbarTailClip_) return;  // done: freed
        clip = std::max(kTailClip, nowbarTailClip_ - startRel);
    } else if (hit) {
        auto it = releaseClip_.find(gi);
        if (it == releaseClip_.end()) return;  // completed
        clip = it->second;
    }
    const float s = std::max(clip, removeY_ - startRel);
    const float e = std::min(endRel, horizonY_) - startRel;
    if (e <= s + 0.01f) return;
    const float L = e - s;
    const float base = startRel + s;  // the tail's start, relative to the now bar
    const float x0 = slotX(slot);

    const bool dead = state != GemState::Live;
    const bool bonusGem = !dead && player.starPowerActive();
    const bool starGem = !bonusGem && g.phrase >= 0 && player.phraseIntact(g.phrase);
    const scene::Mat* mat = dead ? get<scene::Mat>("tail_miss.mat") : bonusGem ? get<scene::Mat>("tail_bonus.mat") : slotMat("tail_%s.mat", slot);
    if (!mat) return;
    float rgb[3] = {mat->color[0], mat->color[1], mat->color[2]};
    // A hit Star Power tail blends to tail_star's colour by the whammy blend
    // (0x8269EAD0: colour + (tail_star - colour) x blend) [C].
    if (hit && starGem && starWhammyBlend_ > 0)
        if (const scene::Mat* st = get<scene::Mat>("tail_star.mat"))
            for (int k = 0; k < 3; ++k) rgb[k] += (st->color[size_t(k)] - rgb[k]) * starWhammyBlend_;
    // The tail's whammy history: advanced while hit (held or released) [C].
    TailWhammy* tw = nullptr;
    if (hit) {
        tw = &tailWhammy_[gi * 8 + slot];
        tw->seen = true;
        const float sample = -std::clamp(player.whammyBar(), 0.0f, 1.0f) * kWhammyScale;
        for (int k = 0; k < whammyPolls_; ++k) {
            tw->index = (tw->index + 1) % 100;
            tw->hist[size_t(tw->index)] = sample;
        }
    }

    auto vertex = [&](float x, float y, float z, const float* n, const float* c, float u, float v) {
        tailVerts_.insert(tailVerts_.end(), {x, y, z, n[0], n[1], n[2], c[0], c[1], c[2], c[3], u, v, 1, 0, 0, 1});
    };

    // The glow line (a flat strip on the tail).
    const scene::Mat* glow = nullptr;
    float width = 0;
    if (!dead) {
        if (held) {
            glow = bonusGem ? get<scene::Mat>("tail_glow_bonus.mat") : starGem ? get<scene::Mat>("tail_glow_star.mat") : slotMat("tail_glow_%s.mat", slot);
            width = tailGlowWidth_;
        } else if (starGem) {
            glow = get<scene::Mat>("tail_glow_tight.mat");
            if (!glow) glow = get<scene::Mat>("tail_glow_star.mat");
            width = tailGlowTightWidth_;
        }
    }
    // The glow line goes on after the tail's faces, so it draws over them:
    // drawn first, the solid tail (wave B #4) hid it, and with it the star
    // tails' cyan core and the held star tail's blue glow [I: the order, as
    // GH1's RndLine over its tail (FUN_001609b0); the line exists to show].
    // The line ends 4 x its width (last poll's) before the horizon.
    const float glowEnd = std::min(e, horizonY_ - startRel - 4 * width * (tw ? tw->glowScale : 1.0f));
    int bodyRange = -1;  // this tail's faces in tailRanges_, once pushed
    auto glowLine = [&] {
        if (!glow || glowEnd <= s || tailVerts_.size() / 16 + 4 >= 65000) return;
        const uint16_t b = uint16_t(tailVerts_.size() / 16);
        // The line's colour (0x8269EAD0) [C]: on a star tail tail_glow_star's
        // blended to tail_glow_whammy's by the whammy blend, else its own
        // material's; the alpha x 0.99.
        std::array<float, 4> lc = glow->color;
        if (starGem) {
            const scene::Mat* gs = get<scene::Mat>("tail_glow_star.mat");
            const scene::Mat* gw = get<scene::Mat>("tail_glow_whammy.mat");
            if (gs) lc = gs->color;
            if (gs && gw)
                for (size_t k = 0; k < 4; ++k) lc[k] += (gw->color[k] - lc[k]) * starWhammyBlend_;
        }
        const float n[3] = {0, 0, 1}, c[4] = {lc[0], lc[1], lc[2], lc[3] * 0.99f};
        const float hw = width * 0.5f, y0 = startRel + s, y1 = startRel + glowEnd;
        // [I] RndLine's uv: u across the line, the middle row of the
        // texture (line01's soft blob) all along it.
        vertex(x0 - hw, y0, kGlowZ, n, c, 0, 0.5f);
        vertex(x0 + hw, y0, kGlowZ, n, c, 1, 0.5f);
        vertex(x0 - hw, y1, kGlowZ, n, c, 0, 0.5f);
        vertex(x0 + hw, y1, kGlowZ, n, c, 1, 0.5f);
        const int first = int(tailIndices_.size());
        tailIndices_.insert(tailIndices_.end(), {b, uint16_t(b + 1), uint16_t(b + 2), uint16_t(b + 1), uint16_t(b + 3), uint16_t(b + 2)});
        // Counted only when it follows its own tail's faces (drawn over them).
        if (starGem && bodyRange >= 0 && size_t(bodyRange) + 1 == tailRanges_.size()) ++starGlowLines_;
        tailRanges_.push_back({glow, first, 6});
    };

    // Rows as 0x8269EAD0 builds them [C]: k = int(L / segment), r = L - k x
    // segment; max(1, segments - k) rows at the start, then rows at r, r +
    // segment, ... up to segments - 1 rows in all, a row 3 units past the
    // next step (half its whammy sample) and the tip (width 0) one segment on.
    // Each row is (1 + its whammy sample) wide, the sample y / 2 polls old.
    const int V = int(tailRow_.size());
    const int N = tailSegments_;
    const int k = std::max(0, int(L / tailSegLen_));
    struct Row {
        float y, sx;
    };
    std::vector<Row> rows;
    auto sample = [&](float y) {
        if (!tw) return 0.0f;
        const int idx = ((tw->index - int(y * 0.5f)) % 100 + 100) % 100;
        return tw->hist[size_t(idx)];
    };
    rows.push_back({0, 1 + sample(0)});
    float widest = rows.back().sx;
    float y = L - float(k) * tailSegLen_;
    int i = std::max(1, N - k);
    while (i < N - 1) {
        ++i;
        rows.push_back({y, 1 + sample(y)});
        widest = std::max(widest, rows.back().sx);
        y += tailSegLen_;
    }
    float tip = y;
    if (i < N) {
        rows.push_back({y + 3.0f, 1 + 0.5f * sample(y)});
        tip = y + tailSegLen_;
    }
    rows.push_back({tip, 0});
    // The glow line's width: eased toward (1 + (widest - 1) / 2) while hit.
    if (tw) {
        for (int p = 0; p < whammyPolls_; ++p) tw->glowScale += ((widest - 1) * 0.5f + 1 - tw->glowScale) * 0.2f;
        width *= tw->glowScale;
    }
    if (held) heldTailWidest_ = std::max(heldTailWidest_, widest), heldGlowScale_ = tw ? tw->glowScale : 1.0f;
    if (tailVerts_.size() / 16 + rows.size() * size_t(V) >= 65000) {
        glowLine();
        return;
    }
    const uint16_t b = uint16_t(tailVerts_.size() / 16);
    for (const Row& row : rows)
        for (const scene::MeshVertex& v : tailRow_) {
            const float c[4] = {rgb[0], rgb[1], rgb[2], v.color[3]};
            vertex(x0 + v.pos[0] * row.sx, base + row.y, v.pos[2], v.normal, c, v.uv[0], v.uv[1]);
        }
    // The faces as the template mesh 0x8269C2A0 builds them: (a, a+1, a+V)
    // and (a+V, a+1, a+V+1) [C]. The other winding is culled: only the glow
    // line showed, which looked like a translucent tail.
    const int first = int(tailIndices_.size());
    for (size_t j = 0; j + 1 < rows.size(); ++j)
        for (int c = 0; c + 1 < V; ++c) {
            const uint16_t a = uint16_t(b + j * size_t(V) + size_t(c));
            const uint16_t av = uint16_t(a + V);
            tailIndices_.insert(tailIndices_.end(), {a, uint16_t(a + 1), av, av, uint16_t(a + 1), uint16_t(av + 1)});
        }
    bodyRange = int(tailRanges_.size());
    tailRanges_.push_back({mat, first, int(tailIndices_.size()) - first});
    glowLine();
}

void TrackView::drawTails(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs) {
    tailVerts_.clear(), tailIndices_.clear(), tailRanges_.clear();
    // The polls since the last frame (60 Hz on the song clock), and the
    // whammy blend's step per poll toward the player's whammy flag [C].
    if (nowMs < whammyClockMs_ || nowMs - whammyClockMs_ > 100 * kPollMs) whammyClockMs_ = nowMs - kPollMs;
    whammyPolls_ = 0;
    while (nowMs - whammyClockMs_ >= kPollMs) whammyClockMs_ += kPollMs, ++whammyPolls_;
    for (int p = 0; p < whammyPolls_; ++p)
        starWhammyBlend_ = player.whammying() ? std::min(1.0f, starWhammyBlend_ + 0.1f) : std::max(0.0f, starWhammyBlend_ - 0.1f);
    for (auto& [key, t] : tailWhammy_) t.seen = false;
    heldTailWidest_ = heldGlowScale_ = 0, tailNowMs_ = nowMs;
    const auto& gems = player.chart().gems;
    const float aheadMs = horizonY_ / (yPerSecond_ * speed_) * 1000.0f;
    auto last = std::lower_bound(gems.begin(), gems.end(), nowMs + aheadMs, [](const Gem& g, float v) { return g.ms < v; });
    for (auto it = gems.begin(); it != last; ++it) {
        const Gem& g = *it;
        if (g.noSustain || g.durationMs <= 0) continue;
        if (yOf(g.ms + g.durationMs - nowMs) < removeY_) continue;
        const int i = int(it - gems.begin());
        if (!player.owned(i)) continue;
        for (int f = 0; f < 5; ++f)
            if (g.frets & (1u << f)) buildTail(g, i, f, player, nowMs);
    }
    // A tail no longer drawn is gone; a new one starts from a zeroed history.
    for (auto it = tailWhammy_.begin(); it != tailWhammy_.end();) it = it->second.seen ? std::next(it) : tailWhammy_.erase(it);
    const scene::Mesh* tmpl = get<scene::Mesh>("tail02.mesh");
    if (tailRanges_.empty() || !tmpl) return;
    if (!tailMesh_) tailMesh_ = std::make_unique<gfx::DynamicMesh>(cache.device());
    const gfx::GpuMesh all = tailMesh_->upload(tailVerts_, tailIndices_);
    setEnv(mr, cache, trackEnv_);
    for (const TailRange& t : tailRanges_) {
        gfx::GpuMesh part = all;
        part.firstIndex = t.firstIndex, part.indexCount = t.indexCount;
        gfx::drawMesh(mr, cache, *tmpl, Mat4::identity(), &pose_, t.mat, nullptr, &part);
    }
}

void TrackView::drawSmashers(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs) {
    const scene::Mesh* smasher = get<scene::Mesh>("gem_smasher.mesh");
    const scene::Mesh* rim = get<scene::Mesh>("smasher_rim.mesh");
    if (!smasher) return;
    const scene::Object* hitAnim = obj("gem_smasher_hit.tnm");
    const scene::Object* popAnim = obj("gem_smasher.tnm");
    const float hitEnd = gfx::ScenePose::endFrame(hitAnim), popEnd = gfx::ScenePose::endFrame(popAnim);
    setEnv(mr, cache, envOf(get<scene::Group>("track.view"), trackEnv_));
    for (int s = 0; s < 5; ++s) {
        // The extend sequence pops the smashers one by one, 1.3 s + 0.1 s per
        // slot after it starts, and lights them (set_smasher_glowing) until
        // refresh_track_buttons at 2.5 s (track_panel.dta) [C data; the glow
        // as the pressed look is I].
        float popMs = missMs_[size_t(s)];
        bool glowing = false;
        if (introStartMs_ > -1e8f) {
            const float pop = introStartMs_ + 1300.0f + 100.0f * float(s);
            if (nowMs >= pop && pop > popMs) popMs = pop;
            glowing = nowMs >= pop && nowMs < introStartMs_ + 2500.0f;
        }
        const bool held = (player.heldFrets() & (1u << s)) || glowing;
        const float sinceHit = (nowMs - hitMs_[size_t(s)]) * 0.001f * kFrameRate;
        const float sinceMiss = (nowMs - popMs) * 0.001f * kFrameRate;
        pose_.resetLocal(smasher);
        if (sinceHit >= 0 && sinceHit < hitEnd) pose_.setFrame(hitAnim, sinceHit);
        else if (sinceMiss >= 0 && sinceMiss < popEnd) pose_.setFrame(popAnim, sinceMiss);
        const Mat4 offset = Mat4::translation(slotX(s), 0, 0);
        const scene::Mat* body = held ? slotMat("gem_smasher_%s.mat", s) : get<scene::Mat>("gem_smasher.mat");
        gfx::drawMesh(mr, cache, *smasher, offset * gfx::toMat4(pose_.world(smasher)), &pose_, body);
        if (rim) gfx::drawMesh(mr, cache, *rim, offset * gfx::toMat4(pose_.world(rim)), &pose_, slotMat("now_ring_%s.mat", s));
    }
    pose_.resetLocal(smasher);
}

void TrackView::drawEffects(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs) {
    setEnv(mr, cache, nullptr);
    // The burn while a sustain is held: burn_tail.view (its flame light
    // here, its particles in drawParticles) at the slots of the held gem.
    const int sg = player.sustainGem();
    const auto& gems = player.chart().gems;
    std::array<bool, 5> burning{};
    if (sg >= 0 && size_t(sg) < gems.size())
        if (const scene::Group* burn = get<scene::Group>("burn_tail.view")) {
            const Gem& g = gems[size_t(sg)];
            const bool starWhammy = g.phrase >= 0 && player.phraseIntact(g.phrase) && player.whammying();
            pose_.setFrame(obj("burn_tail_state.view"), starWhammy ? 10.0f : player.starPowerActive() ? 20.0f : 0.0f);
            pose_.setFrame(obj("burn_castlight.filt"), nowMs * 0.001f * kFrameRate);
            for (int f = 0; f < 5; ++f) {
                if (!(g.frets & (1u << f))) continue;
                burning[size_t(f)] = true;
                const Mat4 offset = Mat4::translation(slotX(f), 0, 0);
                for (const std::string& n : burn->objects)
                    if (const scene::Mesh* m = burn->dir->resolve<scene::Mesh>(n))
                        gfx::drawMesh(mr, cache, *m, offset * gfx::toMat4(pose_.world(m)), &pose_);
            }
        }
    // The burn view's particles follow its state frame (the ParticleSysAnims
    // of burn_tail_state.view) and run on the clock; a new hold starts them
    // afresh, a release hides them [I].
    for (int f = 0; f < 5; ++f) {
        gfx::ParticleSet& b = burn_[size_t(f)];
        if (burning[size_t(f)] && !burnOn_[size_t(f)]) b.reset();
        burnOn_[size_t(f)] = burning[size_t(f)];
        if (!burning[size_t(f)]) continue;
        b.sync(pose_);
        b.setFrame(nowMs * 0.001f * kFrameRate);
    }

    // Smash views (0x8269CD60): each slot's copy at frame (now - hit) x 0.03.
    static const char* const kViews[3] = {"smash_normal.view", "smash_star.view", "smash_bonus.view"};
    for (int f = 0; f < 5; ++f)
        for (int k = 0; k < 3; ++k) {
            Smash& sm = smash_[size_t(f)][size_t(k)];
            if (!sm.active) continue;
            const scene::Group* view = get<scene::Group>(kViews[k]);
            if (!view) continue;
            const float frame = std::max(0.0f, (nowMs - sm.startMs) * 0.001f * kFrameRate);
            pose_.setFrame(view, frame);
            sm.parts.sync(pose_);
            if (frame > 9) continue;  // the flame light's anims are over
            const Mat4 offset = Mat4::translation(slotX(f), 0, 0);
            for (const std::string& n : view->objects)
                if (const scene::Mesh* m = view->dir->resolve<scene::Mesh>(n))
                    gfx::drawMesh(mr, cache, *m, offset * gfx::toMat4(pose_.world(m)), &pose_);
        }
}

// The phrase_captured lightning (combo2): the bolts stand up from the
// smashers, each slot's copy leaning out by (slot - 2) x combo2_angle
// (about the track's forward axis) [I: the axis; C: the angle].
void TrackView::drawCombo(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, float nowMs) {
    const scene::Group* view = get<scene::Group>("smash_combo_lightning.view");
    const scene::Group* burst = get<scene::Group>("smash_combo.view");
    const float end = gfx::ScenePose::endFrame(view);
    for (int f = 0; f < 5; ++f) {
        Smash& c1 = combo1_[size_t(f)];
        if (c1.active && burst) {
            pose_.setFrame(burst, std::max(0.0f, (nowMs - c1.startMs) * 0.001f * kFrameRate));
            c1.parts.sync(pose_);
        }
        Smash& c = combo2_[size_t(f)];
        if (!c.active || !view) continue;
        const float frame = std::max(0.0f, (nowMs - c.startMs) * 0.001f * kFrameRate);
        if (frame > end) {
            c.active = false;
            continue;
        }
        pose_.setFrame(view, frame);
        const float a = float(f - 2) * combo2Angle_ * 0.017453292f * (lefty_ ? -1.0f : 1.0f);
        Mat4 rot;
        rot.at(0, 0) = std::cos(a), rot.at(0, 2) = std::sin(a);
        rot.at(2, 0) = -std::sin(a), rot.at(2, 2) = std::cos(a);
        const Mat4 offset = Mat4::translation(slotX(f), 0, 0) * rot;
        for (const std::string& n : view->objects)
            if (const scene::Mesh* m = view->dir->resolve<scene::Mesh>(n))
                gfx::drawMesh(mr, cache, *m, offset * gfx::toMat4(pose_.world(m)), &pose_);
    }
}

// The track's particles, simulated in the game's track space (the rig, and
// with it the smashers, moving forward by now_y: world-space particles stay
// on the track and scroll towards the player, as in the game) and drawn
// with the rig at rest (an offset of -now_y).
void TrackView::drawParticles(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs) {
    if (!particles_) particles_ = std::make_unique<gfx::ParticleRenderer>(cache.device());
    particles_->resetStats();
    setEnv(mr, cache, nullptr);
    const float clock = nowMs * 0.001f * kFrameRate;
    const Mat4 offset = Mat4::translation(0, -nowY_, 0);
    auto at = [&](float x, float y) {
        scene::Xfm b;
        b.pos = {x, y, 0};
        return b;
    };
    // Burn (behind the smash sparks).
    for (int f = 0; f < 5; ++f) {
        if (!burnOn_[size_t(f)]) continue;
        burn_[size_t(f)].update(pose_, at(slotX(f), nowY_), clock);
        burn_[size_t(f)].draw(*particles_, mr, cache, camera_, offset, &pose_);
    }
    // tail_bonus.part on held Star Power tails (0x8269EAD0) [C]: a copy per
    // tail at the tail's start, emitting 2 x the panel's whammy blend
    // (0x8269EAD0: blend x 2.0 at +0x15c / +0x160) [C], particles past the
    // tail's end removed.
    const auto& gems = player.chart().gems;
    const int sg = player.sustainGem();
    for (auto& [key, t] : tailSparks_) t.live = false;
    if (sg >= 0 && size_t(sg) < gems.size()) {
        const Gem& g = gems[size_t(sg)];
        const bool starTail = !player.starPowerActive() && g.phrase >= 0 && player.phraseIntact(g.phrase);
        if (starTail && obj("tail_bonus.part"))
            for (int f = 0; f < 5; ++f) {
                if (!(g.frets & (1u << f))) continue;
                TailSparks& t = tailSparks_[sg * 8 + f];
                if (t.parts.empty()) t.parts.build(obj("tail_bonus.part"), uint32_t(sg * 8 + f + 300));
                t.live = true;
                const float gemY = yOf(g.ms);
                t.startY = std::max(gemY + kTailClip, nowY_ + nowbarTailClip_);
                t.endY = std::min(yOf(g.ms + g.durationMs), nowY_ + horizonY_);
                const float w = starWhammyBlend_;
                for (gfx::ParticleSystem& ps : t.parts.systems()) ps.params().emitRate = {w * 2, w * 2};
            }
    }
    for (auto it = tailSparks_.begin(); it != tailSparks_.end();) {
        TailSparks& t = it->second;
        t.parts.setEmitting(t.live);
        t.parts.setFrame(clock);
        // The copy sits at the tail's start (its own Trans replaced) [C].
        const int slot = it->first % 8;
        scene::Xfm base = at(slotX(slot), t.startY);
        for (gfx::ParticleSystem& ps : t.parts.systems()) {
            scene::Xfm w = pose_.world(&ps.def());
            w.pos = {0, 0, 0};
            w = scene::compose(w, base);
            ps.setTransform(w, w);
            ps.poll(clock);
            const float endY = t.endY;
            ps.removeIf([endY](const gfx::Vec3& p) { return p.y > endY; });
        }
        if (!t.live && t.parts.count() == 0) {
            it = tailSparks_.erase(it);
            continue;
        }
        t.parts.draw(*particles_, mr, cache, camera_, offset, nullptr);
        ++it;
    }
    // The phrase_captured burst (combo1).
    for (int f = 0; f < 5; ++f) {
        Smash& c = combo1_[size_t(f)];
        if (!c.active) continue;
        c.parts.update(pose_, at(slotX(f), nowY_), clock);
        c.parts.draw(*particles_, mr, cache, camera_, offset, &pose_);
        if ((nowMs - c.startMs) > 3000 && c.parts.count() == 0) c.active = false;
    }
    // Smash sparks.
    for (int f = 0; f < 5; ++f)
        for (int k = 0; k < 3; ++k) {
            Smash& sm = smash_[size_t(f)][size_t(k)];
            if (!sm.active) continue;
            sm.parts.update(pose_, at(slotX(f), nowY_), clock);
            sm.parts.draw(*particles_, mr, cache, camera_, offset, &pose_);
            if ((nowMs - sm.startMs) > 3000 && sm.parts.count() == 0) sm.active = false;
        }
    particlesDrawn_ = particles_->drawnParticles();
}

// track_explode.view (0x82695988, poll 0x82695C08) [C]: shown, parented
// to the rig, and played by track_explode.filt at (now - explode) x 0.03;
// the track and its gems are hidden.
void TrackView::drawExplode(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, float nowMs) {
    const scene::Group* view = get<scene::Group>("track_explode.view");
    if (!view) return;
    const float f = (nowMs - explodeMs_) * 0.001f * kFrameRate;
    pose_.setFrame(obj("track_explode.filt"), f);
    pose_.setShowing(view, true);
    explodeParts_.sync(pose_);
    const scene::Environ* env = envOf(view, nullptr);
    setEnv(mr, cache, env);
    for (const std::string& n : view->objects)
        if (const scene::Mesh* m = view->dir->resolve<scene::Mesh>(n); m && pose_.showing(m))
            gfx::drawMesh(mr, cache, *m, gfx::toMat4(pose_.world(m)), &pose_);
    if (!particles_) particles_ = std::make_unique<gfx::ParticleRenderer>(cache.device());
    particles_->resetStats();
    explodeParts_.update(pose_, scene::Xfm{}, nowMs * 0.001f * kFrameRate);
    setEnv(mr, cache, nullptr);
    explodeParts_.draw(*particles_, mr, cache, camera_, Mat4::identity(), &pose_);
    particlesDrawn_ = particles_->drawnParticles();
}

void TrackView::draw(gfx::Renderer& r, gfx::SceneCache& cache, const Song& song, const Player& player, float nowMs, float trackSpeed) {
    if (!track_) return;
    particlesDrawn_ = 0;
    // game.dta game_lost: {track_panel set_showing FALSE} [C].
    if (player.failed()) return;
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    // The camera's screen rect (two players: each track in its half) [C data;
    // the projection keeping the rect's own aspect is I].
    const bool sub = screenRect_ != std::array<float, 4>{0, 0, 1, 1};
    viewW_ = std::max(1, int(float(vw) * screenRect_[2])), viewH_ = std::max(1, int(float(vh) * screenRect_[3]));
    if (sub) r.device().setViewport(vx + int(float(vw) * screenRect_[0]), vy + int(float(vh) * screenRect_[1]), viewW_, viewH_);
    frameSet_ = false;
    speed_ = trackSpeed;
    nowY_ = yOf(nowMs);
    if (gemState_.size() != player.chart().gems.size()) gemState_.assign(player.chart().gems.size(), GemState::Live);
    gfx::MaterialRenderer& mr = r.materials();

    // anim.grp at frame now_y (TrackDir::Poll); the rig itself stays at rest.
    pose_.setFrame(obj("anim.grp"), nowY_);
    for (const char* n : {"rig.grp", "cameras.grp", "track.cam"})
        if (const scene::Object* o = obj(n)) pose_.resetLocal(o);
    // The extend sequence (track_panel.dta animate_track_out: the panel's
    // frame 0 -> TRACK_END_FRAME 1920 in a second; extend_track_normal.tnm
    // brings the cameras in) [C data]; at rest it holds frame 1920.
    {
        float frame = 1920;
        if (introStartMs_ > -1e8f) frame = std::clamp((nowMs - introStartMs_) / 1000.0f * 1920.0f, 0.0f, 1920.0f);
        if (extendAnim_.startMs > -1e8f && nowMs >= extendAnim_.startMs) {
            const float k = extendAnim_.periodMs > 0 ? std::min(1.0f, (nowMs - extendAnim_.startMs) / extendAnim_.periodMs) : 1.0f;
            frame = extendAnim_.from + (extendAnim_.to - extendAnim_.from) * k;
        }
        pose_.setFrame(obj("extend_track_normal.tnm"), frame);
    }
    // blow_streak: the surface flash and the camera shake.
    if (const scene::Mat* m = get<scene::Mat>("track_surface.mat")) pose_.matState(m).hasColor = false;
    if (blowLevel_ >= 2) {
        const float f = (nowMs - blowMs_) * 0.001f * kFrameRate;
        char name[48];
        if (f >= 0 && f <= 15) {
            SDL_snprintf(name, sizeof name, "surface_flash_%dx.mnm", blowLevel_);
            pose_.setFrame(obj(name), f);
        }
        if (f >= 0 && f <= 25) {
            SDL_snprintf(name, sizeof name, "track_shake_%dx.tnm", blowLevel_);
            pose_.setFrame(obj(name), f);
        }
    }

    // Star Power: the lightning and the Star Power surface light; the rails.
    const bool star = player.starPowerActive();
    if (const scene::Object* l = obj("lightning.grp")) pose_.setShowing(l, star);
    if (const scene::Object* l = obj("wood_track_surface_starpower.lit")) pose_.setShowing(l, star);
    const bool warning = player.rockMeter() < player.warningLevel();
    static const char* const kRails[4] = {"side_rails_none.mnm", "side_rails_star.filt", "side_rails_warning.filt",
                                          "side_rails_warning_star.filt"};
    const float beats = float(song.tempo.msToTick(std::max(0.0f, nowMs)) / std::max(1, song.tempo.division()));
    pose_.setFrame(obj(kRails[(star ? 1 : 0) + (warning ? 2 : 0)]), beats);

    camera_.world = gfx::toMat4(pose_.world(cam_));
    camera_.yFov = cam_->yFov, camera_.nearPlane = nearPlane_, camera_.farPlane = farPlane_;
    camera_.aspect = float(viewW_) / float(viewH_);
    // Two players watch the explode through track_explode.cam (0x826967B8
    // makes it for two players only, mirrored for the second player: the
    // data's track_explode_p2.cam) [C; the _p2 cam standing in for the
    // mirror is I].
    if (exploded() && player_ >= 0)
        if (const scene::Cam* ec = get<scene::Cam>(player_ == 1 ? "track_explode_p2.cam" : "track_explode.cam")) {
            camera_.world = gfx::toMat4(pose_.world(ec));
            camera_.yFov = ec->yFov;
        }

    if (exploded()) {
        // 0x82695988: the gems and the track's own drawing are hidden, the
        // explode view drawn (with the track camera in one player, 0x826957F0).
        drawExplode(mr, cache, nowMs);
    } else {
        // draw.grp's order: surfaces, rails and lane lines, bar widgets, tails,
        // gem widgets, lightning; then the smashers, effects and particles.
        for (const char* g : {"wood_track_surface_starpower.grp", "wood_track_surface.grp", "track_surface.grp"})
            drawGroup(mr, cache, get<scene::Group>(g), trackEnv_);
        drawBeatLines(mr, cache, song, nowMs);
        drawTails(mr, cache, player, nowMs);
        drawGems(mr, cache, player, nowMs);
        drawGroup(mr, cache, get<scene::Group>("lightning.grp"), trackEnv_);
        drawSmashers(mr, cache, player, nowMs);
        drawEffects(mr, cache, player, nowMs);
        drawCombo(mr, cache, nowMs);
        drawParticles(mr, cache, player, nowMs);
    }
    // The lanes on the screen for the touch controls: each slot's line at
    // the strike line and at the horizon, through the track camera, in the
    // picture's menu units.
    lanesSet_ = !exploded();
    if (lanesSet_) {
        const Mat4 vp = camera_.viewProj(false);
        auto toMenu = [&](float x, float y, float& mx, float& my) {
            const float c[4] = {vp.at(0, 0) * x + vp.at(0, 1) * y + vp.at(0, 3), vp.at(1, 0) * x + vp.at(1, 1) * y + vp.at(1, 3), 0,
                                vp.at(3, 0) * x + vp.at(3, 1) * y + vp.at(3, 3)};
            const float w = std::max(c[3], 1e-4f);
            mx = (screenRect_[0] + screenRect_[2] * (c[0] / w * 0.5f + 0.5f)) * 1280.0f;
            my = (screenRect_[1] + screenRect_[3] * (0.5f - c[1] / w * 0.5f)) * 720.0f;
        };
        for (int f = 0; f < 5; ++f) {
            toMenu(slotX(f), 0, lanes_[size_t(f)], lanes_[size_t(5 + f)]);
            toMenu(slotX(f), horizonY_, lanes_[size_t(10 + f)], lanes_[size_t(15 + f)]);
        }
    }
    if (sub) r.device().setViewport(vx, vy, vw, vh);
    viewW_ = vw, viewH_ = vh;
}

bool TrackView::lanes(std::array<float, 5>& nearX, std::array<float, 5>& nearY, std::array<float, 5>& farX, std::array<float, 5>& farY) const {
    if (!lanesSet_) return false;
    for (size_t f = 0; f < 5; ++f) nearX[f] = lanes_[f], nearY[f] = lanes_[5 + f], farX[f] = lanes_[10 + f], farY[f] = lanes_[15 + f];
    return true;
}
