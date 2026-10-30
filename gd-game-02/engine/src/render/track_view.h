// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/chart.h"
#include "game/player.h"
#include "render/particles.h"
#include "render/scene_gpu.h"

class GameData;
namespace scene {
class Milo;
class MiloLibrary;
}  // namespace scene
namespace gfx {
class Renderer;
}

// GH2's highway, drawn from the game's own track scene (ui/track_panel.dta:
// track/track_panel_1p.milo, whose GHTrackDir is a proxy of
// track/track_new.milo, "i_am_a_track") and config/track_graphics.dtb, the
// way the game's track code places things (addresses in track_view.cpp):
// the wooden surface scrolling with the song, the lane lines and rails, the
// chart's gems as the track's TrackWidgets (normal / HOPO / Star Power /
// Star Power HOPO / bonus during Star Power / missed), sustain tails built
// by code (bent by the whammy bar), the smashers reacting to held frets and
// hits, the smash effects (flame lights and particles), the burn and the
// Star Power tail sparks, beat and measure lines from the tempo map, the
// Star Power look (lightning, rails), the intro (extend sequence, smashers
// popping), the explode when the song is won, the lefty flip, and each
// player's track of the two-player panel.
class TrackView {
public:
    TrackView();
    ~TrackView();
    // Loads the scene and the config; throws std::runtime_error on failure.
    // `player`: -1 for one player (track_panel_1p, track.cam), else 0 / 1,
    // the player's track of track_panel_2p (track_new_1 / _2: its camera
    // track_p1 / track_p2.cam, which draws in the left / right half).
    void load(const GameData& data, scene::MiloLibrary& library, int player = -1);
    bool loaded() const { return track_ != nullptr; }
    // A new song (clears the per-song state and effects). `introStartMs`:
    // the song time the track's extend sequence starts (game.dta
    // extend_track; none: the track is out already).
    void reset(float introStartMs = -1e9f);
    // Lefty flip (the player's option): the slots mirror (0x82699E78,
    // 0x82691348) [C].
    void setLefty(bool lefty) { lefty_ = lefty; }
    // The song is won (game.dta game_won_msg: {track_panel explode}).
    void explode(float nowMs);
    // The scripts' animate on the track's extend anim (player.dta
    // start_using: range 2000 2025 over 0.8 s, the Star Power bump; the
    // barf cheat on every beat): frames from -> to from song time `nowMs`
    // over `periodMs`, the last frame held after.
    void animateExtend(float from, float to, float periodMs, float nowMs) { extendAnim_ = {from, to, periodMs, nowMs}; }
    bool exploded() const { return explodeMs_ > -1e8f; }
    // The player's events of this frame (hits start smash effects, passes
    // turn gems into misses, releases cut tails).
    void onEvents(const std::vector<PlayerEvent>& events, const Player& player, float nowMs);
    // Draws the track with its own camera into the bound 16:9 picture.
    void draw(gfx::Renderer& r, gfx::SceneCache& cache, const Song& song, const Player& player, float nowMs, float trackSpeed);
    // Particles drawn last frame (statistics).
    int particlesDrawn() const { return particlesDrawn_; }
    // Since reset (--walktest's star_phrase check): smash_star effects started
    // (Star Power phrase gems hit) and star tails' glow lines drawn over
    // their tails.
    int starSmashes() const { return starSmashes_; }
    int starGlowLines() const { return starGlowLines_; }
    // The held tail's widest row (1 at rest, up to 4 with the bar fully down)
    // and its glow line's width scale, as last drawn, and that frame's song
    // time (--walktest whammy_tail); 0 when no tail is held.
    float heldTailWidest() const { return heldTailWidest_; }
    float heldGlowScale() const { return heldGlowScale_; }
    float tailNowMs() const { return tailNowMs_; }
    // Since reset: phrase_captured combo effects started (per slot).
    int comboStarts() const { return comboStarts_; }
    // Where each lane was on the screen in the last frame drawn (menu units,
    // 1280 x 720): at the strike line and at the track's far end, for the
    // touch controls (input/input.h TouchLanes). False before a frame or
    // while the track is hidden.
    bool lanes(std::array<float, 5>& nearX, std::array<float, 5>& nearY, std::array<float, 5>& farX, std::array<float, 5>& farY) const;

private:
    // A smash view's copy for one slot (0x8269C968): normal, star or bonus.
    struct Smash {
        float startMs = -1e9f;
        bool active = false;
        gfx::ParticleSet parts;
    };
    // tail_bonus.part's copy on a held Star Power tail (0x8269EAD0).
    struct TailSparks {
        gfx::ParticleSet parts;
        float startY = 0, endY = 0;  // absolute track y of the tail's start and end
        bool live = false;
    };
    // Per gem of the chart.
    enum class GemState : uint8_t { Live, Missed, Released };
    struct TailRange {
        const scene::Mat* mat;
        int firstIndex, indexCount;
    };

    const scene::Object* obj(const std::string& name) const;
    template <class T> const T* get(const std::string& name) const {
        const scene::Object* o = obj(name);
        return o ? o->as<T>() : nullptr;
    }
    float yOf(float dtMs) const { return dtMs * 0.001f * yPerSecond_ * speed_; }
    const scene::Mat* slotMat(const char* format, int slot) const;
    void setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env);
    const scene::Environ* envOf(const scene::Group* g, const scene::Environ* inherited) const;
    void drawGroup(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Group* g, const scene::Environ* env);
    void drawWidget(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::TrackWidget* w, float x, float y);
    void drawBeatLines(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Song& song, float nowMs);
    void drawGems(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void buildTail(const Gem& g, int gemIndex, int slot, const Player& player, float nowMs);
    void drawTails(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawSmashers(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawCombo(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, float nowMs);
    void drawEffects(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawParticles(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawExplode(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, float nowMs);
    float slotX(int slot) const { return lefty_ ? -slotX_[size_t(slot)] : slotX_[size_t(slot)]; }

    scene::Milo* track_ = nullptr;
    int player_ = -1;
    bool lefty_ = false;
    const scene::Cam* cam_ = nullptr;
    std::array<float, 4> screenRect_{0, 0, 1, 1};
    float nearPlane_ = 50, farPlane_ = 200;
    gfx::ScenePose pose_;
    gfx::Camera camera_;
    const scene::Environ* currentEnv_ = nullptr;
    const scene::Environ* trackEnv_ = nullptr;
    bool frameSet_ = false;
    int viewW_ = 1280, viewH_ = 720;

    // TrackDir / track_graphics.dtb values.
    float yPerSecond_ = 60, bottomY_ = -10, horizonY_ = 120, removeY_ = -15;
    float nowbarTailClip_ = 1.5f;
    std::array<float, 20> lanes_{};  // lanes(): near x / y, far x / y per slot
    bool lanesSet_ = false;
    float tailGlowWidth_ = 1.5f, tailGlowTightWidth_ = 0.7f;
    float gemFps_ = 30;
    std::array<float, 5> slotX_{-8, -4, 0, 4, 8};
    std::array<std::string, 5> slotColors_{"green", "red", "yellow", "blue", "orange"};
    float speed_ = 1;
    float nowY_ = 0;  // the song position in track units (the rig's y)

    // GHTrackDir widget lists: 5 frets then bonus and miss (gem / hopo), or
    // 5 frets then miss (star / star hopo).
    std::vector<const scene::TrackWidget*> gemW_, hopoW_, starW_, starHopoW_;
    const scene::TrackWidget *barW_ = nullptr, *beatW_ = nullptr, *halfBeatW_ = nullptr;

    // The tail template: tail02.mesh's cross-section (sorted by x).
    std::vector<scene::MeshVertex> tailRow_;
    int tailSegments_ = 30;
    float tailSegLen_ = 4.545f;
    // Each tail's whammy history (100 samples and their index, zeroed when
    // the tail is set up) and its glow line's width scale, keyed gem x 8 +
    // slot; advanced one sample a poll while the tail is hit (0x8269EAD0) [C].
    struct TailWhammy {
        std::array<float, 100> hist{};
        int index = 0;
        float glowScale = 1;
        bool seen = false;
    };
    std::unordered_map<int, TailWhammy> tailWhammy_;
    int whammyPolls_ = 0;           // 60 Hz polls due this frame
    float whammyClockMs_ = -1e9f;   // the last poll's song time
    float starWhammyBlend_ = 0;     // the panel's whammy blend: +-0.1 a poll [C]
    float heldTailWidest_ = 0, heldGlowScale_ = 0, tailNowMs_ = 0;

    std::array<float, 5> hitMs_{};    // last hit per slot (smasher hit anim)
    std::array<float, 5> missMs_{};   // last miss per slot (smasher pop anim)
    std::array<std::array<Smash, 3>, 5> smash_;
    // The phrase_captured effects per slot (0x8269AB80 / 0x8269B058):
    // combo1 smash_combo.view, combo2 smash_combo_lightning.view.
    std::array<Smash, 5> combo1_, combo2_;
    float combo2Angle_ = 12;  // track_graphics combo2_angle, degrees
    std::array<gfx::ParticleSet, 5> burn_;
    std::array<bool, 5> burnOn_{};
    std::unordered_map<int, TailSparks> tailSparks_;  // gem x 8 + slot
    gfx::ParticleSet explodeParts_;
    float explodeMs_ = -1e9f;
    float introStartMs_ = -1e9f;
    struct ExtendAnim {
        float from = 0, to = 0, periodMs = 0, startMs = -1e9f;
    } extendAnim_;
    std::unique_ptr<gfx::ParticleRenderer> particles_;
    int particlesDrawn_ = 0;
    int starSmashes_ = 0, starGlowLines_ = 0, comboStarts_ = 0;
    std::vector<GemState> gemState_;
    std::unordered_map<int, float> releaseClip_;  // released gem -> frozen tail start (relative y)
    float blowMs_ = -1e9f;
    int blowLevel_ = 0;

    std::vector<float> tailVerts_;
    std::vector<uint16_t> tailIndices_;
    std::vector<TailRange> tailRanges_;
    std::unique_ptr<gfx::DynamicMesh> tailMesh_;
};
