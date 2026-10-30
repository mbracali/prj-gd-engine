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
#include "scene/bitmap.h"
#include "scene/milo.h"

// How the PS2 TrackPanel (SLUS_215.86) places things; [C] read in the code
// at the address, [I] inferred, [?] open. The panel loads one scene,
// track/track.milo (ui/track_panel.dta), for every player.
//
// - Tree (panel init 0x0014faa8) [C]: a root group holds the camera
//   (track.cam, its planes from track_graphics cam, 0x00150df0) and the
//   track group (track.env; scaled by the two-player layout); in it, in draw
//   order, track_surface5.mesh and track_lane_lines5.mesh (0x00154c08), the
//   beat lines, track_side_rails5.mesh and the gems group (tails first, then
//   the gem template's mesh copies). The panel draws (0x001511c0) the root,
//   then the far-end fade (the venue strip at track_fade.trans saved before
//   the track and laid back over it with track_fade.mat), then a second
//   group: the smashers, the hit effects and lightning_track.view.
// - Units: y = song ms x 0.06 x track_speed (0x001524c8) [C]; track_speed is
//   the players' difficulties' track_graphics value averaged, x 1.5 with the
//   Hyper Speed cheat (0x00153a48, 0x0014faa8) [C]. The beat line and gem
//   groups move back by now_y each poll (0x00151670) [C]; here everything is
//   drawn relative to the now bar, which is the same picture.
// - Slots: x = (slot - 2) x 4, lefty: slot 4 - s (0x00152530, 0x00152500) [C].
// - Surface: track_surface.filt (track_surface.mnm's texture scroll) at frame
//   now_y (0x00151670 -> 0x00155470) [C]; track_anims.view (the gem glow,
//   the bonus spin) at now ms x 0.03 (0x00153ea0) [C].
// - Gems (manager poll 0x00156530) [C]: added while start_y < now_y +
//   horizon_y (110), freed when end_y < now_y + remove_y (-15). Each gem
//   (rep ctor 0x00158238, look 0x00158858, miss 0x00158c80, hit 0x00158cf8)
//   is a set of mesh copies at (slot x, start y, 0), each composed after the
//   mesh's own local transform (0x00158198): look normal: top (top.mesh, or
//   <colour>_hopo.mesh on a HOPO), body (<colour>_gem.mesh), glow
//   (glow.mesh); look bonus (Star Power): blue_hopo.mesh as the HOPO top,
//   gem_bonus.mesh, gem_bonus2.mesh; look star (an intact Star Power
//   phrase): star_base.mesh, <colour>_star.mesh, star2.mesh and the top
//   (top_star_black.mesh, <colour>_top_star.mesh on a HOPO). A miss swaps the
//   body for miss.mesh and the star for star_miss.mesh, the rest staying; a
//   hit hides them all. HOPO look: the chart's HOPO flag, not the first gem,
//   the previous gem the player's own (0x001562e8) [C].
// - The template draws its copies grouped by mesh in a fixed order (0x00159e28
//   builds them by part type 3, 4, 6, 0, 2, 1, 5) [C].
// - Star spin (0x0015bae8) [C]: star.filt at now ms x 0.03; star.tnm's
//   rotation replaces the rotation of star_base, star2 and the five
//   <colour>_star meshes (not star_miss).
// - Tails (template 0x0015bcf0, clip 0x00156970 / 0x00157e18) [C]:
//   tail02.mesh's cross-section, 30 segments (20 with two players [I: the
//   count]) of (horizon_y - remove_y) x 1.01 / segments; the tail starts
//   `clip` past the gem (2.0; while sustained now_y + nowbar_tail_clip -
//   start) and ends at the gem's end or the horizon. Materials (look
//   0x00158858, hit 0x00158cf8): tail_<colour> (tail_bonus for bonus gems,
//   tail_miss when missed); the glow line: tail_glow_tight (width
//   tail_glow_tight_width) on a star gem, while held tail_glow_<colour> /
//   tail_glow_star (width tail_glow_width).
// - Tail whammy (0x00158e50, the same in GH2 360's 0x8269EAD0) [C]: each
//   tail keeps 100 whammy samples (zeroed when it is set up, 0x00158ab0);
//   every poll while the tail is hit (state 2 held, 3 released) the next one
//   is the bar's value x -3.0. The bar is the guitar's axis clamped to <= 0
//   (0x00236758: rest 0, full press -1), so a sample is 0 .. 3 and a row at
//   distance d from the tail's start is (1 + sample[d / 2 polls ago]) wide:
//   the tail swells as the bar goes down and the bulges run down the tail.
//   The row 3 units before the tip takes half its sample. The glow line's
//   width eases (x 0.2 a poll) to tail_glow_width x (1 + (widest - 1) / 2)
//   while hit, 1 otherwise. A Star Power phrase's tail blends its colour to
//   tail_star's and its glow from tail_glow_star's to tail_glow_whammy's by
//   the panel's whammy blend (+-0.1 a poll toward the player's whammy flag,
//   0x00156530); tail_bonus.part emits 2 x that blend.
// - Beat lines (0x00152690, 0x0015d5b8, 0x0015c990) [C]: one every 240
//   ticks: half_beat_line5 between beats, beat_line5 on beats, bar_line5 on
//   measures, as copies of the mesh's geometry at (0, y, 0).
// - Smashers (0x0015caa0, 0x0015fd50) [C]: per slot a group at (slot x, 0,
//   0) with gem_smasher.mesh (track_graphics smasher mats: gem_smasher,
//   gem_smasher_<colour> while pressed) and smasher_rim.mesh (now_ring_
//   <colour>), and copies of gem_smasher.tnm / gem_smasher_hit.tnm.
// - Rails (0x0015dd70, poll 0x0015e190) [C]: side_rails_none.mnm /
//   _warning / _star / _warning_star.filt at frame song ticks / 480.
// - lightning_track.view (0x00150be8, poll 0x00151580) [C]: shown during
//   Star Power at (now - its start) x 0.03.
// - Effects (0x0015caa0) [C]: smash_normal / star / bonus views, combo1
//   smash_combo.view and combo2 smash_combo_lightning.view, its slot copies
//   turned by (slot - 2) x combo2_angle in one-player games only (0 with
//   several players); with two players the views' particles are scaled by
//   track_scale (0x0015d7c8).
// - Two players (0x0014faa8, use_dual_perspective TRUE): the track group
//   scaled by dual_perspective track_scale (0.76) and track.cam's screen
//   rect the player's half (x = player / players, width 1 / players,
//   0x00154b90) [C]; track_explode.cam for the explode, mirrored in x for the
//   second player (here the data's track_explode_p2.cam) [C].
// - blow_streak N: surface_flash_Nx.mnm and track_shake_Nx.tnm (on
//   track.cam) when a streak of multiplier N (2..4) ends [I: event].
// - Explode (0x00150ca8, poll 0x00151580) [C]: track_explode.filt at
//   (now - explode) x 0.03.
// - Surface texture (0x001536e0, 0x00153628, 0x00153888, 0x00155078) [C]:
//   setSurface below.
// - gem_sparkle and gem_bonus_spark1 / 2 [C: not drawn]: the gem template
//   (0x00159e28) copies gem_sparkle.mesh and .tnm (aimed at the copy),
//   gem_bonus_spark1 / 2.mesh with copies of their materials, their .tnm /
//   .mnm on those copies, and a group of the spark anims (template words
//   0x7f..0x87, 0x89), and reads track_graphics gem sparkle_len (word 0x8e).
//   Nothing else in SLUS_215.86 reads those words (every load / store at
//   their offsets is in the template's constructor or its destructor
//   0x0015b590); the copies are in no drawn group, nothing plays the anims,
//   and the scene's own meshes are members of gem_hide.grp only. The PS2
//   never shows them, so neither does this port.
namespace {

using gfx::Mat4;
using gfx::Vec3;

constexpr float kFrameRate = 30;        // effect timers: ms x 0.03
constexpr float kTailClip = 2.0f;       // gem record +0x18 floor (0x00157e18) [C]
constexpr float kWhammyScale = -3.0f;   // 0x00158ed0: lui 0xc040, sample = bar x -3.0 [C]
constexpr float kPollMs = 1000.0f / 60.0f;  // the game's poll (one a frame at 60 Hz) [I: NTSC rate]
constexpr float kGlowZ = 0.16f;

}  // namespace

TrackView::TrackView() = default;
TrackView::~TrackView() {
    if (fadeDevice_)
        for (FadeStrip& f : fade_)
            if (f.copy) fadeDevice_->destroyTarget(f.copy);
}

const scene::Object* TrackView::obj(const std::string& name) const { return track_ ? track_->find(name) : nullptr; }

const scene::Mat* TrackView::slotMat(const char* format, int slot) const {
    char name[64];
    SDL_snprintf(name, sizeof name, format, slotColors_[size_t(std::clamp(slot, 0, 4))].c_str());
    return get<scene::Mat>(name);
}

void TrackView::load(const GameData& data, scene::MiloLibrary& library, int player) {
    // ui/track_panel.dta: {new TrackPanel track_panel (file ../track/track.milo)}.
    player_ = player;
    track_ = nullptr;
    scene::Milo& track = library.load("track/gen/track.milo_ps2");
    track_ = &track;

    // config/track_graphics.dtb.
    const DataNode cfg = parseDtb(data.read("config/gen/track_graphics.dtb"));
    auto num = [&](const DataNode* parent, const char* key, float& out) {
        if (const DataNode* n = parent ? parent->find(key) : nullptr; n && n->children.size() > 1) out = n->floatAt(1);
    };
    yPerSecond_ = 60;  // 0.06 units a ms per unit of track_speed [C]
    num(&cfg, "horizon_y", horizonY_);
    num(&cfg, "remove_y", removeY_);
    bottomY_ = removeY_;
    num(&cfg, "nowbar_tail_clip", nowbarTailClip_);
    const DataNode* gem = cfg.find("gem");
    num(gem, "fps", gemFps_);
    num(gem, "tail_glow_width", tailGlowWidth_);
    num(gem, "tail_glow_tight_width", tailGlowTightWidth_);
    num(cfg.find("particles"), "combo2_angle", combo2Angle_);
    // combo2_angle applies with one player only (0x0015caa0) [C].
    if (player >= 0) combo2Angle_ = 0;
    if (const DataNode* sc = cfg.find("slot_colors"))
        for (size_t i = 0; i < 5 && i + 1 < sc->children.size(); ++i) slotColors_[i] = sc->textAt(i + 1);

    // The gem template (0x00159e28): per part of track_graphics gem parts,
    // per gem of the guitar set, "<gem>_<part>.mesh", else "<part>.mesh",
    // else the part's default type's mesh [C].
    auto mesh = [&](const std::string& n) { return get<scene::Mesh>(n); };
    std::array<std::string, 7> partNames{"gem", "glow", "miss", "top", "hopo", "star", "top_star"};
    std::array<int, 7> partDefault{-1, -1, 0, -1, 3, 0, 3};
    if (const DataNode* parts = gem ? gem->find("parts") : nullptr; parts && parts->children.size() > 1 && parts->children[1].isList()) {
        static const std::array<std::pair<const char*, int>, 7> kTypes{
            {{"kMeshGem", 0}, {"kMeshGemGlow", 1}, {"kMeshGemMiss", 2}, {"kMeshTop", 3}, {"kMeshHopoTop", 4}, {"kMeshStar", 5}, {"kMeshStarTop", 6}}};
        auto typeOf = [&](const DataNode* n) {
            // The macro's name, or its value (config/macros.dta).
            if (!n || n->children.size() < 2) return -1;
            if (n->children[1].type == DataNode::Type::Int) return n->children[1].i;
            if (!n->children[1].isText()) return -1;
            for (const auto& [name, v] : kTypes)
                if (n->children[1].s == name) return v;
            return -1;
        };
        for (const DataNode& p : parts->children[1].children) {
            if (!p.isList() || p.children.empty()) continue;
            const int t = typeOf(p.find("type"));
            if (t < 0 || t > 6 || !p.children[0].isText()) continue;
            partNames[size_t(t)] = p.textAt(0);
            const int d = typeOf(p.find("default"));
            partDefault[size_t(t)] = d >= 0 && d <= 6 ? d : -1;
        }
    }
    for (size_t t = 0; t < 7; ++t)
        for (size_t s = 0; s < 5; ++s) {
            const scene::Mesh* m = mesh(slotColors_[s] + "_" + partNames[t] + ".mesh");
            parts_[t][s] = m ? m : mesh(partNames[t] + ".mesh");
        }
    for (size_t t = 0; t < 7; ++t)
        for (size_t s = 0; s < 5; ++s)
            if (!parts_[t][s] && partDefault[t] >= 0) parts_[t][s] = parts_[size_t(partDefault[t])][s];
    if (!parts_[0][0] || !parts_[3][0]) throw std::runtime_error("track.milo: no gem meshes");
    bonusHopo_ = mesh("blue_hopo.mesh"), bonusGem_ = mesh("gem_bonus.mesh"), bonusGlow_ = mesh("gem_bonus2.mesh");
    star2_ = mesh("star2.mesh"), starBase_ = mesh("star_base.mesh"), topStarBlack_ = mesh("top_star_black.mesh");
    starMiss_ = mesh("star_miss.mesh");
    // The copies' order: part types 3, 4, 6, 0, 2, 1, 5, each type's extra
    // pieces after it [C 0x00159e28].
    partOrder_.clear();
    auto order = [&](const scene::Mesh* m) {
        if (m && !partOrder_.count(m)) partOrder_[m] = int(partOrder_.size());
    };
    for (int t : {3, 4, 6, 0, 2, 1, 5}) {
        for (size_t s = 0; s < 5; ++s) order(parts_[size_t(t)][s]);
        if (t == 3) order(bonusHopo_), order(topStarBlack_);
        if (t == 0) order(bonusGem_);
        if (t == 1) order(bonusGlow_);
        if (t == 5) order(star2_), order(starBase_), order(starMiss_);
    }
    beatMeshes_ = {mesh("quarter_beat_line5.mesh"), mesh("half_beat_line5.mesh"), mesh("beat_line5.mesh"), mesh("bar_line5.mesh")};
    surface_ = mesh("track_surface5.mesh"), laneLines_ = mesh("track_lane_lines5.mesh"), rails_ = mesh("track_side_rails5.mesh");
    if (!surface_) throw std::runtime_error("track.milo: no track_surface5.mesh");

    // The tail template (0x0015bcf0): tail02.mesh's vertices sorted, the
    // first half (one row) kept as the cross-section; 30 segments, fewer with
    // several players.
    if (const scene::Mesh* t = get<scene::Mesh>("tail02.mesh"); t && t->verts.size() >= 4) {
        std::vector<scene::MeshVertex> v = t->verts;
        std::sort(v.begin(), v.end(), [](const scene::MeshVertex& a, const scene::MeshVertex& b) {
            return a.pos[1] != b.pos[1] ? a.pos[1] < b.pos[1] : a.pos[0] < b.pos[0];
        });
        tailRow_.assign(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2));
    }
    tailSegments_ = player < 0 ? 30 : 20;
    tailSegLen_ = (horizonY_ - removeY_) * 1.01f / float(tailSegments_);

    // The camera: track.cam, its planes from track_graphics cam (0x00150df0).
    cam_ = get<scene::Cam>("track.cam");
    if (!cam_) throw std::runtime_error("track.milo: no track.cam");
    if (const DataNode* cam = cfg.find("cam")) {
        num(cam, "near_plane", nearPlane_);
        num(cam, "far_plane", farPlane_);
    }
    // Two players, dual perspective (track_graphics multiplayer
    // use_dual_perspective TRUE): the track scaled, the camera in the
    // player's half (0x0014faa8, 0x00154b90) [C].
    screenRect_ = {0, 0, 1, 1};
    trackScale_ = 1;
    if (player >= 0) {
        screenRect_ = {float(player) * 0.5f, 0, 0.5f, 1};
        trackScale_ = 0.76f;
        if (const DataNode* mp = cfg.find("multiplayer"))
            if (const DataNode* dp = mp->find("dual_perspective")) num(dp, "track_scale", trackScale_);
    }
    trackEnv_ = get<scene::Environ>("track.env");
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
    fadeDraws_ = surfaceOverrides_ = 0;
    explodeMs_ = -1e9f;
    bonusStartMs_ = -1e9f;
    // The effect copies (0x0015caa0 / 0x0015f340: one smash view of each kind per slot,
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
                    // born after frame 0 die on the next move [C in GH2 360's particle code, 0x82212A48].
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
            // Released early (as GH2 360's 0x82697710) [I]: the tail turns to tail_miss and
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

// A mesh copy (0x00158198) [C]: the mesh's own local transform composed with
// the copy's place (x, y, 0); the star spin replaces the local rotation.
void TrackView::drawPart(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Mesh* m, float x, float y, const scene::Xfm* rot) {
    if (!m) return;
    scene::Xfm l = m->trans.local;
    if (rot) l.rot = rot->rot;
    scene::Xfm at;
    at.pos = {x, y, 0};
    gfx::drawMesh(mr, cache, *m, gfx::toMat4(scene::compose(l, at)), &pose_);
}

// The player's surface texture [C unless marked]. The panel's load
// (0x001536b0 -> 0x001536e0) queues, per player, the bitmap
// track/surfaces/<s>_keep.bmp, <s> being gamecfg's track_surface_override
// for the mode (the mode's entry, else defaults: '' but in practice and the
// tutorial, rosewood) or, when that is the empty symbol, the player
// config's character (+0x3c, an outfit name as alterna1; the same field
// names ui/image/og/photo_<s><n>_keep.bmp, 0x00106378). The load waits for
// them (0x00153628); post-load (0x00153888) hands player i's bitmap to
// track i's constructor (0x00153a48 -> 0x0014faa8, its fifth argument, kept
// at +8 of the track's config at +0x28, 0x00152478); the surface
// (0x00155078) copies track_surface.tex and track_surface.mat, gives the
// copied texture that bitmap (0x001d8d78) and the copied material that
// texture (0x001554a8). Every player's track so shows its own surface;
// nothing else uses track_surface.tex. A bitmap missing from the disc keeps
// the scene's own texture here [I: the PS2 would fail the load].
void TrackView::setSurface(const GameData& data, const std::string& mode, const std::string& character, const std::string& songId) {
    std::string name;
    // gamecfg's mode lookup: the mode's own entry, else defaults (as the
    // script side's gameCfgValue).
    try {
        const DataNode modes = parseDtb(data.read("config/gen/modes.dtb"));
        bool found = false;
        for (const std::string& m : {mode, std::string("defaults")}) {
            for (const DataNode& e : modes.children) {
                if (!e.isList() || e.children.empty() || !e.children[0].isText() || e.children[0].s != m) continue;
                if (const DataNode* o = e.find("track_surface_override")) name = o->textAt(1), found = true;
            }
            if (found) break;
        }
    } catch (const std::exception& e) {
        SDL_Log("track: modes.dtb: %s", e.what());
    }
    if (name.empty()) name = character;
    // An unchosen character: the song's quickplay character_outfit, else
    // alterna1 (as game/band.cpp, set_quickplay) [C there].
    if (name.empty()) {
        try {
            DataNode root = parseDtb(data.read("config/gen/songs.dtb"));
            resolveConditionals(root, {"HX_XBOX", "_SHIP"});
            for (const DataNode& e : root.children) {
                if (!e.isList() || e.children.empty() || !e.children[0].isText() || e.children[0].s != songId) continue;
                if (const DataNode* q = e.find("quickplay"))
                    if (const DataNode* c = q->find("character_outfit")) name = c->textAt(1);
            }
        } catch (const std::exception& e) {
            SDL_Log("track: songs.dtb: %s", e.what());
        }
        if (name.empty()) name = "alterna1";
    }
    if (name == surfaceName_ && surfaceTex_) return;
    surfaceName_ = name;
    surfaceTex_ = nullptr;
    // The bitmaps live as long as the program: the scene cache keys its
    // GPU textures by the Tex's address [I: a port choice].
    static std::unordered_map<std::string, std::unique_ptr<scene::Tex>> loaded;
    const std::string path = "track/surfaces/gen/" + name + "_keep.bmp_ps2";
    auto it = loaded.find(path);
    if (it == loaded.end()) {
        std::unique_ptr<scene::Tex> tex;
        if (data.exists(path)) {
            try {
                const Bytes b = data.read(path);
                ByteReader r(b);
                tex = std::make_unique<scene::Tex>();
                tex->name = tex->file = "track/surfaces/" + name + "_keep.bmp";
                tex->bitmap = scene::readBitmap(r);
                tex->width = tex->bitmap.width, tex->height = tex->bitmap.height;
                tex->decoded = true;
            } catch (const std::exception& e) {
                SDL_Log("track: %s: %s", path.c_str(), e.what());
                tex.reset();
            }
        } else {
            SDL_Log("track: no surface %s", path.c_str());
        }
        it = loaded.emplace(path, std::move(tex)).first;
    }
    surfaceTex_ = it->second.get();
}

// The surface and its lane lines (0x00154c08: parented to the track group,
// in this order), the beat lines, then the rails (0x0014faa8).
void TrackView::drawSurface(gfx::MaterialRenderer& mr, gfx::SceneCache& cache) {
    setEnv(mr, cache, trackEnv_);
    // The player's surface bitmap on track_surface.mat (setSurface), over
    // whatever its anims set.
    if (surfaceTex_)
        if (const scene::Mat* m = get<scene::Mat>("track_surface.mat")) {
            pose_.matState(m).tex = surfaceTex_;
            ++surfaceOverrides_;
        }
    for (const scene::Mesh* m : {surface_, laneLines_})
        if (m) gfx::drawMesh(mr, cache, *m, gfx::toMat4(pose_.local(m)), &pose_);
}

void TrackView::drawBeatLines(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Song& song, float nowMs) {
    setEnv(mr, cache, trackEnv_);
    // Added while y < now_y + horizon_y, removed when y < now_y + remove_y
    // (0x00152a28) [C]; one every 240 ticks from tick 0 to the song's end
    // (0x0015d5b8, kinds 0x0015d688: 1 half beat, 2 beat, 3 bar) [C].
    const int division = std::max(2, song.tempo.division());
    const long half = division / 2;
    const double fromMs = nowMs + removeY_ / (yPerSecond_ * speed_) * 1000.0f;
    const double toMs = nowMs + horizonY_ / (yPerSecond_ * speed_) * 1000.0f;
    long tick = std::max(0L, long(song.tempo.msToTick(std::max(0.0, fromMs))) / half * half);
    for (int guard = 0; guard < 4096; ++guard, tick += half) {
        const double ms = song.tempo.tickToMs(double(tick));
        if (ms >= toMs || ms > song.endMs + 1) break;
        const float y = yOf(float(ms) - nowMs);
        if (y < removeY_) continue;
        int kind = 1;
        if (tick % division == 0) {
            const double m = song.tempo.tickToMeasure(double(tick));
            kind = std::abs(m - std::round(m)) < 1e-6 ? 3 : 2;
        }
        // A copy of the mesh's geometry at (0, y, 0) (0x0015c990) [C].
        if (const scene::Mesh* m = beatMeshes_[size_t(kind)]) gfx::drawMesh(mr, cache, *m, Mat4::translation(0, y, 0), &pose_);
    }
    if (rails_) gfx::drawMesh(mr, cache, *rails_, gfx::toMat4(pose_.local(rails_)), &pose_);
}

void TrackView::drawGems(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs) {
    setEnv(mr, cache, trackEnv_);
    const auto& gems = player.chart().gems;
    const bool bonus = player.starPowerActive();
    const float aheadMs = horizonY_ / (yPerSecond_ * speed_) * 1000.0f;
    const float behindMs = removeY_ / (yPerSecond_ * speed_) * 1000.0f;
    auto first = std::lower_bound(gems.begin(), gems.end(), nowMs + behindMs, [](const Gem& g, float v) { return g.ms < v; });
    auto last = std::lower_bound(first, gems.end(), nowMs + aheadMs, [](const Gem& g, float v) { return g.ms < v; });
    // The star spin: star.filt at now x 0.03, star.tnm's rotation (on
    // star_base.mesh in the scene) given to the spinning parts (0x0015bae8).
    pose_.setFrame(obj("star.filt"), nowMs * 0.001f * kFrameRate);
    const scene::Xfm spin = starBase_ ? pose_.local(starBase_) : scene::Xfm{};
    if (starBase_) pose_.resetLocal(starBase_);
    struct Copy {
        int order;
        const scene::Mesh* mesh;
        float x, y;
        bool spin;
    };
    std::vector<Copy> copies;
    auto add = [&](const scene::Mesh* m, float x, float y, bool spins = false) {
        if (!m) return;
        auto it = partOrder_.find(m);
        copies.push_back({it == partOrder_.end() ? int(partOrder_.size()) : it->second, m, x, y, spins});
    };
    // Far to near within each mesh's copies.
    for (auto it = last; it != first;) {
        --it;
        const int i = int(it - gems.begin());
        const Gem& g = *it;
        if (player.gemHit(i) || !player.owned(i)) continue;
        const float y = yOf(g.ms - nowMs);
        const bool missed = size_t(i) < gemState_.size() && gemState_[size_t(i)] == GemState::Missed;
        const bool hopo = g.hopo && i > 0 && player.owned(i - 1);
        // Looks (0x001568a0, set_bonus_gems, on_miss_phrase): bonus during
        // Star Power, star in an intact phrase, else normal.
        const bool asBonus = bonus && !missed;
        const bool star = !asBonus && g.phrase >= 0 && player.phraseIntact(g.phrase);
        for (int f = 0; f < 5; ++f) {
            if (!(g.frets & (1u << f))) continue;
            const size_t s = size_t(f);
            const float x = slotX(f);
            if (star) {
                add(starBase_, x, y, true);
                add(hopo ? parts_[6][s] : topStarBlack_, x, y);
                if (missed) add(starMiss_, x, y);
                else add(parts_[5][s], x, y, true);
                add(star2_, x, y, true);
            } else {
                add(!hopo ? parts_[3][s] : asBonus ? bonusHopo_ : parts_[4][s], x, y);
                add(missed ? parts_[2][s] : asBonus ? bonusGem_ : parts_[0][s], x, y);
                add(asBonus ? bonusGlow_ : parts_[1][s], x, y);
            }
        }
    }
    std::stable_sort(copies.begin(), copies.end(), [](const Copy& a, const Copy& b) { return a.order < b.order; });
    for (const Copy& c : copies) drawPart(mr, cache, c.mesh, c.x, c.y, c.spin ? &spin : nullptr);
}

// One fret's tail of gem `gi` (built in the gem poll 0x00158e50 as in GH2 360's 0x8269EAD0): rows of the cross-section from
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
    // (0x00158e50: (1 - blend) x colour + blend x tail_star) [C].
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
    // The line ends 4 x its width (last poll's, 0x00158b98) before the horizon.
    const float glowEnd = std::min(e, horizonY_ - startRel - 4 * width * (tw ? tw->glowScale : 1.0f));
    int bodyRange = -1;  // this tail's faces in tailRanges_, once pushed
    auto glowLine = [&] {
        if (!glow || glowEnd <= s || tailVerts_.size() / 16 + 4 >= 65000) return;
        const uint16_t b = uint16_t(tailVerts_.size() / 16);
        // The line's colour (0x00158e50) [C]: on a star tail tail_glow_star's
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

    // Rows as 0x00158e50 builds them [C]: k = int(L / segment), r = L - k x
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
    // The faces as GH2 360's template mesh (0x8269C2A0) builds them: (a, a+1, a+V)
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
    setEnv(mr, cache, trackEnv_);
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
        gfx::drawMesh(mr, cache, *smasher, offset * gfx::toMat4(pose_.local(smasher)), &pose_, body);
        if (rim) gfx::drawMesh(mr, cache, *rim, offset * gfx::toMat4(pose_.local(rim)), &pose_, slotMat("now_ring_%s.mat", s));
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

    // Smash views (0x0015f340; GH2 360 0x8269CD60): each slot's copy at frame (now - hit) x 0.03.
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
    // tail_bonus.part on held Star Power tails (loaded by 0x00159e28; placed as GH2 360's 0x8269EAD0): a copy per
    // tail at the tail's start, emitting 2 x the panel's whammy blend
    // (0x00158e50: param blend + blend at +0x178 / +0x17c) [C], particles
    // past the tail's end removed.
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

// track_explode.view (0x00150ca8, poll 0x00151580) [C]: shown, parented
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

// lightning_track.view (0x00150be8): its copy shown during Star Power, at
// (now - Star Power's start) x 0.03 (0x00151580) [C].
void TrackView::drawLightning(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs) {
    if (!player.starPowerActive()) {
        bonusStartMs_ = -1e9f;
        return;
    }
    if (bonusStartMs_ < -1e8f) bonusStartMs_ = nowMs;
    const scene::Group* view = get<scene::Group>("lightning_track.view");
    if (!view || !view->dir) return;
    pose_.setFrame(view, (nowMs - bonusStartMs_) * 0.001f * kFrameRate);
    setEnv(mr, cache, trackEnv_);
    for (const std::string& n : view->objects)
        if (const scene::Mesh* m = view->dir->resolve<scene::Mesh>(n)) gfx::drawMesh(mr, cache, *m, gfx::toMat4(pose_.world(m)), &pose_);
}

// The far-end fade (TrackPanel draw 0x001511c0) [C unless marked]. While the
// track shows, track_fade.trans (y = 120 on track_surface5.view: the
// horizon) is projected through track.cam (0x00154fe8, always the normal
// camera, not the explode one) to a point in the camera's screen rect, in
// the PS2's 640 x 480 frame (Rnd defaults 0x001d4af8; width > 512 picks
// track_fade_hires.tex, 0x00150850). Two strips are copied from the frame,
// venue only, with a GS local-to-local transfer (0x001a0da8): the size of
// track_fade(_hires).tex (256 x 32) from (x - 128, y - 2), and the size of
// track_fade2.tex (128 x 16) just above it, ending at y - 2. Both are skipped
// when the lower strip would reach the frame's bottom. After the root is
// drawn, each strip goes back at its place through Rnd's rect draw
// (0x0019b090): the lower one with track_fade.mat, its corners white, the
// bottom ones with alpha 0 (the venue opaque at the horizon, fading to the
// track below it), the upper one with track_fade2.mat in full; each one
// then gets track_add.mat (add blend, colour 0.02) over it. The rect draw
// takes the corner colours only for prelit materials and the material's
// colour otherwise (the flag it tests, Mat +0x9c, is prelit: RndMat::Load
// 0x001bfc40 reads use_environ to +0x40 then prelit to +0x9c, and the
// property handler 0x001c13c8 maps "prelit" to +0x9c) [C]; both fade
// materials set it and track_add.mat does not.
// Here the strips scale with the picture's height (480 PS2 lines) [I], and
// the frame's alpha is not used (the copies sample opaque) [I: the PS2's
// frame alpha is not known].
bool TrackView::fadeSave(gfx::Renderer& r) {
    const scene::TransObj* at = get<scene::TransObj>("track_fade.trans");
    if (!at || !cam_) return false;
    // The point through track.cam (camera_ holds the explode camera while
    // two players see the explode).
    gfx::Camera c = camera_;
    if (exploded() && player_ >= 0) {
        c.world = gfx::toMat4(pose_.world(cam_));
        if (trackScale_ != 1 && trackScale_ > 0) {
            const Vec3 p = c.world.translationPart();
            c.world.at(0, 3) = p.x / trackScale_, c.world.at(1, 3) = p.y / trackScale_, c.world.at(2, 3) = p.z / trackScale_;
        }
    }
    const scene::Xfm w = pose_.world(at);
    const Vec3 eye = c.view().transformPoint({w.pos[0], w.pos[1], w.pos[2]});
    if (eye.y <= 0) return false;  // behind the camera (Rnd camera space looks down +y)
    const float t = std::tan(c.yFov * 0.5f);
    const float nx = eye.x / (eye.y * t * c.aspect), ny = eye.z / (eye.y * t);
    int px, py, pw, ph;
    r.pictureRect(px, py, pw, ph);
    if (ph <= 0) return false;
    const float s = float(ph) / 480.0f;  // our pixels per PS2 line
    // In PS2 pixels, from the picture's top-left, truncated as the PS2 does.
    const int x = int((float(viewX_ - px) + (nx + 1) * 0.5f * float(viewW_)) / s);
    const int y = int((float(viewY_ - py) + (1 - ny) * 0.5f * float(viewH_)) / s);
    const int hires[2] = {256, 32}, top[2] = {128, 16};
    const int lowY = y - 2;
    if (lowY + hires[1] >= 480) return false;
    const int rects[2][4] = {{x - hires[0] / 2, lowY, hires[0], hires[1]}, {x - top[0] / 2, lowY - top[1], top[0], top[1]}};
    gfx::RenderDevice& dev = r.device();
    fadeDevice_ = &dev;
    const gfx::TargetId prev = dev.currentTarget();
    int vx, vy, vw, vh;
    dev.currentViewport(vx, vy, vw, vh);
    bool rebind = false;
    for (size_t i = 0; i < 2; ++i) {
        FadeStrip& f = fade_[i];
        const int x0 = px + int(std::lround(float(rects[i][0]) * s)), y0 = py + int(std::lround(float(rects[i][1]) * s));
        const int x1 = px + int(std::lround(float(rects[i][0] + rects[i][2]) * s)), y1 = py + int(std::lround(float(rects[i][1] + rects[i][3]) * s));
        f.x = x0, f.y = y0, f.w = std::max(1, x1 - x0), f.h = std::max(1, y1 - y0);
        if (!f.copy || f.copyW != f.w || f.copyH != f.h) {
            // Making a target unbinds the frame: bound again below.
            if (f.copy) dev.destroyTarget(f.copy);
            std::string error;
            gfx::TargetDesc d;
            d.width = f.w, d.height = f.h, d.opaque = true;
            f.copy = dev.createTarget(d, error);
            f.copyW = f.w, f.copyH = f.h;
            rebind = true;
            if (!f.copy) SDL_Log("track: fade strip %dx%d: %s", f.w, f.h, error.c_str());
        }
    }
    if (rebind) {
        dev.setTarget(prev);
        dev.setViewport(vx, vy, vw, vh);
    }
    if (!fade_[0].copy || !fade_[1].copy) return false;
    for (const FadeStrip& f : fade_) dev.copyToTarget(f.x, f.y, f.w, f.h, f.copy);
    return true;
}

void TrackView::fadeDraw(gfx::MaterialRenderer& mr, gfx::SceneCache& cache) {
    const scene::Mat* add = get<scene::Mat>("track_add.mat");
    const scene::Mat* mats[2] = {get<scene::Mat>("track_fade.mat"), get<scene::Mat>("track_fade2.mat")};
    // Each rect as a quad in front of the camera covering its pixels (the
    // camera's viewport, so the planes and aspect are the track's own).
    std::vector<float> verts;
    std::vector<uint16_t> indices;
    const float d = camera_.nearPlane * 2;
    const float t = std::tan(camera_.yFov * 0.5f);
    for (size_t i = 0; i < 2; ++i) {
        const FadeStrip& f = fade_[i];
        auto corner = [&](int qx, int qy, float u, float v, float alpha) {
            const float nx = 2.0f * float(qx - viewX_) / float(viewW_) - 1, ny = 1 - 2.0f * float(qy - viewY_) / float(viewH_);
            const Vec3 p = camera_.world.transformPoint({nx * d * t * camera_.aspect, d, ny * d * t});
            verts.insert(verts.end(), {p.x, p.y, p.z, 0, 0, 1, 1, 1, 1, alpha, u, v, 1, 0, 0, 1});
        };
        const uint16_t b = uint16_t(i * 4);
        // The lower strip's bottom corners are clear (Rnd's rect draw: its
        // last colour argument for the bottom row).
        const float bottomAlpha = i == 0 ? 0.0f : 1.0f;
        corner(f.x, f.y, 0, 0, 1);
        corner(f.x, f.y + f.h, 0, 1, bottomAlpha);
        corner(f.x + f.w, f.y, 1, 0, 1);
        corner(f.x + f.w, f.y + f.h, 1, 1, bottomAlpha);
        indices.insert(indices.end(), {b, uint16_t(b + 1), uint16_t(b + 2), uint16_t(b + 2), uint16_t(b + 1), uint16_t(b + 3)});
    }
    if (!fadeMesh_) fadeMesh_ = std::make_unique<gfx::DynamicMesh>(cache.device());
    const gfx::GpuMesh all = fadeMesh_->upload(verts, indices);
    // A 2D rect: no environ, no depth.
    mr.setFrame(camera_, gfx::LightEnv{}, viewW_, viewH_);
    frameSet_ = false;
    const Mat4 bones[1] = {Mat4::identity()};
    for (size_t i = 0; i < 2; ++i) {
        gfx::GpuMesh part = all;
        part.firstIndex = int(i * 6), part.indexCount = 6;
        for (const scene::Mat* mat : {mats[i], add}) {
            if (!mat) continue;
            gfx::Material m = cache.material(*mat, &pose_);
            if (mat != add) m.diffuseTex = cache.device().targetTexture(fade_[i].copy);
            m.zMode = gfx::ZMode::Disable;
            m.cull = false;
            m.texWrap = gfx::TexWrap::Clamp;
            mr.draw(part, bones, m, {1});
        }
    }
    ++fadeDraws_;
}

void TrackView::draw(gfx::Renderer& r, gfx::SceneCache& cache, const Song& song, const Player& player, float nowMs, float trackSpeed) {
    if (!track_) return;
    particlesDrawn_ = 0;
    // game.dta game_lost: {track_panel set_showing FALSE} [C data].
    if (player.failed()) return;
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    // Two players: track.cam's screen rect is the player's half (0x00154b90)
    // [C]; the projection keeping the rect's own aspect is [I].
    const bool sub = screenRect_ != std::array<float, 4>{0, 0, 1, 1};
    viewW_ = std::max(1, int(float(vw) * screenRect_[2])), viewH_ = std::max(1, int(float(vh) * screenRect_[3]));
    viewX_ = vx + int(float(vw) * screenRect_[0]), viewY_ = vy + int(float(vh) * screenRect_[1]);
    if (sub) r.device().setViewport(viewX_, viewY_, viewW_, viewH_);
    frameSet_ = false;
    speed_ = trackSpeed;
    nowY_ = yOf(nowMs);
    if (gemState_.size() != player.chart().gems.size()) gemState_.assign(player.chart().gems.size(), GemState::Live);
    gfx::MaterialRenderer& mr = r.materials();

    // The surface scroll at now_y, the gem anims at now x 0.03 (0x00151670,
    // 0x00153ea0) [C].
    pose_.setFrame(obj("track_surface.filt"), nowY_);
    pose_.setFrame(obj("track_anims.view"), nowMs * 0.001f * kFrameRate);
    pose_.resetLocal(cam_);
    // The extend sequence (track_panel.dta animate_track_out: the panel's
    // frame 0 -> TRACK_END_FRAME 1920 in a second; extend_track_normal.tnm
    // moves track.cam) [C data]; at rest it holds frame 1920.
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

    // The rails (0x0015e190): their state's anim at song ticks / 480.
    const bool star = player.starPowerActive();
    const bool warning = player.rockMeter() < player.warningLevel();
    static const char* const kRails[4] = {"side_rails_none.mnm", "side_rails_warning.filt", "side_rails_star.filt",
                                          "side_rails_warning_star.filt"};
    const float beats = float(song.tempo.msToTick(std::max(0.0f, nowMs)) / 480.0);
    pose_.setFrame(obj(kRails[(warning ? 1 : 0) + (star ? 2 : 0)]), beats);

    const scene::Cam* cam = cam_;
    // Two players watch the explode through track_explode.cam (0x0014faa8
    // makes it for several players only, mirrored for the second player:
    // the data's track_explode_p2.cam) [C; the _p2 cam standing in for the
    // mirror is I].
    if (exploded() && player_ >= 0)
        if (const scene::Cam* ec = get<scene::Cam>(player_ == 1 ? "track_explode_p2.cam" : "track_explode.cam")) cam = ec;
    camera_.world = gfx::toMat4(pose_.world(cam));
    camera_.yFov = cam->yFov, camera_.nearPlane = nearPlane_, camera_.farPlane = farPlane_;
    camera_.aspect = float(viewW_) / float(viewH_);
    // The track group scaled by track_scale about the origin, the camera
    // (outside it) not: the same picture as the camera's place and planes
    // divided by the scale.
    if (trackScale_ != 1 && trackScale_ > 0) {
        const Vec3 p = camera_.world.translationPart();
        camera_.world.at(0, 3) = p.x / trackScale_, camera_.world.at(1, 3) = p.y / trackScale_, camera_.world.at(2, 3) = p.z / trackScale_;
        camera_.nearPlane /= trackScale_, camera_.farPlane /= trackScale_;
    }

    // The far-end fade's strips are saved before the root draws and laid
    // back after it, under the second group (0x001511c0) [C].
    const bool fade = fadeSave(r);
    if (exploded()) {
        drawExplode(mr, cache, nowMs);
        if (fade) fadeDraw(mr, cache);
    } else {
        // The root: surface, lane lines, beat lines, rails, tails, gems;
        // then the fade, then the second group: smashers, effects, lightning.
        drawSurface(mr, cache);
        drawBeatLines(mr, cache, song, nowMs);
        drawTails(mr, cache, player, nowMs);
        drawGems(mr, cache, player, nowMs);
        if (fade) fadeDraw(mr, cache);
        drawSmashers(mr, cache, player, nowMs);
        drawEffects(mr, cache, player, nowMs);
        drawCombo(mr, cache, nowMs);
        drawParticles(mr, cache, player, nowMs);
        drawLightning(mr, cache, player, nowMs);
    }
    // The lanes on the screen for the touch controls: each slot's line at
    // the strike line and at the horizon, through the track camera, in the
    // picture's menu units (the frame's 1280 x 720: in 4:3 the picture is its
    // middle 960).
    lanesSet_ = !exploded();
    if (lanesSet_) {
        const Mat4 vp = camera_.viewProj(false);
        int fx, fy, fw, fh;
        r.frameRect(fx, fy, fw, fh);
        const float picX0 = 1280.0f * float(vx - fx) / float(std::max(1, fw)), picW = 1280.0f * float(vw) / float(std::max(1, fw));
        auto toMenu = [&](float x, float y, float& mx, float& my) {
            const float c[4] = {vp.at(0, 0) * x + vp.at(0, 1) * y + vp.at(0, 3), vp.at(1, 0) * x + vp.at(1, 1) * y + vp.at(1, 3), 0,
                                vp.at(3, 0) * x + vp.at(3, 1) * y + vp.at(3, 3)};
            const float w = std::max(c[3], 1e-4f);
            mx = picX0 + (screenRect_[0] + screenRect_[2] * (c[0] / w * 0.5f + 0.5f)) * picW;
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
