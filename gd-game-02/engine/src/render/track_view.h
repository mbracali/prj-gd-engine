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
// hits, the smash flames, beat and measure lines from the tempo map, and the
// Star Power look (lightning, rails).
class TrackView {
public:
    TrackView();
    ~TrackView();
    // Loads the scene and the config; throws std::runtime_error on failure.
    void load(const GameData& data, scene::MiloLibrary& library);
    bool loaded() const { return track_ != nullptr; }
    // A new song (clears the per-song state and effects).
    void reset();
    // The player's events of this frame (hits start smash effects, passes
    // turn gems into misses, releases cut tails).
    void onEvents(const std::vector<PlayerEvent>& events, const Player& player, float nowMs);
    // Draws the track with its own camera into the bound 16:9 picture.
    void draw(gfx::Renderer& r, gfx::SceneCache& cache, const Song& song, const Player& player, float nowMs, float trackSpeed);

private:
    struct Effect {
        int slot = 0;
        int kind = 0;  // 0 normal, 1 star, 2 bonus
        float startMs = 0;
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
    void drawEffects(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);

    scene::Milo* track_ = nullptr;
    gfx::ScenePose pose_;
    gfx::Camera camera_;
    const scene::Environ* currentEnv_ = nullptr;
    const scene::Environ* trackEnv_ = nullptr;
    bool frameSet_ = false;
    int viewW_ = 1280, viewH_ = 720;

    // TrackDir / track_graphics.dtb values.
    float yPerSecond_ = 60, bottomY_ = -10, horizonY_ = 120, removeY_ = -15;
    float nowbarTailClip_ = 1.5f;
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
    std::array<float, 100> whammyHist_{};
    int whammyIndex_ = 0;

    std::array<float, 5> hitMs_{};    // last hit per slot (smasher hit anim)
    std::array<float, 5> missMs_{};   // last miss per slot (smasher pop anim)
    std::vector<Effect> effects_;
    std::vector<GemState> gemState_;
    std::unordered_map<int, float> releaseClip_;  // released gem -> frozen tail start (relative y)
    float blowMs_ = -1e9f;
    int blowLevel_ = 0;

    std::vector<float> tailVerts_;
    std::vector<uint16_t> tailIndices_;
    std::vector<TailRange> tailRanges_;
    std::unique_ptr<gfx::DynamicMesh> tailMesh_;
};
