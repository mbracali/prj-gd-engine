// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "game/chart.h"
#include "render/scene_gpu.h"

class GameData;
namespace scene {
class Milo;
class MiloLibrary;
class Object;
class Group;
class Environ;
class Cam;
class Mesh;
class Mat;
class TrackWidget;
}  // namespace scene
namespace gfx {
class Renderer;
class MaterialRenderer;
class SceneCache;
}  // namespace gfx

// Rock Band's highway (M4, GD05-006), drawn from the game's own track scene:
// ui/track_panel.dta's track/trackpanel.milo, whose TrackDirs guitar / bass
// / drum are proxies of track/tracksystem.milo [C data], and
// config/track_graphics.dtb's gem sets (the widget of each slot for normal,
// HOPO, Overdrive (star), missed gems; the drums' kick across the track)
// [C data]. Placement as GH2's (render/track_view.cpp): the TrackDir's
// y_per_second and slots, its anim group posed at the song position (the
// surface scroll), widgets at (slot x, y). Approximate in wave 01 [I]:
// no sustain tails, no smasher reactions, no Overdrive / solo / fill / BRE
// looks, one player's track centred (RB's 2-4 player layouts are not done).
class RbTrackView {
public:
    // What the view needs of a player's judge.
    struct PlayerView {
        const Chart* chart = nullptr;
        std::function<bool(int gem)> hit;      // hit: not drawn
        std::function<bool(int gem)> missed;   // passed unhit: the miss widget
        std::function<bool(int phrase)> phraseIntact;  // Overdrive phrase still capturable: star widgets
        bool overdriveActive = false;
    };

    RbTrackView();
    ~RbTrackView();
    // `part`: "guitar", "bass" or "drum". Throws std::runtime_error on failure.
    void load(const GameData& data, scene::MiloLibrary& library, const std::string& part);
    bool loaded() const { return track_ != nullptr; }
    const std::string& part() const { return part_; }
    void setLefty(bool lefty) { lefty_ = lefty; }
    void draw(gfx::Renderer& r, gfx::SceneCache& cache, const Song& song, const PlayerView& player, float nowMs, float trackSpeed);

private:
    struct SlotSet {
        const scene::TrackWidget *normal = nullptr, *hopo = nullptr, *star = nullptr, *starHopo = nullptr, *miss = nullptr,
                                 *missHopo = nullptr;
    };
    const scene::Object* obj(const std::string& name) const;
    template <class T> const T* get(const std::string& name) const;
    float yOf(float dtMs) const { return dtMs * 0.001f * yPerSecond_ * speed_; }
    float slotX(int slot) const;
    void setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env);
    void drawGroup(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Group* g, const scene::Environ* env, int depth = 0);
    void drawWidget(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::TrackWidget* w, float x, float y, int colorSlot = -1);
    // The slot's colour of a gem material (prism_mat.mat -> prism_mat_green.mat), or null.
    const scene::Mat* slotMat(const scene::Mesh& m, int slot) const;
    void drawBeatLines(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Song& song, float nowMs);
    void drawGems(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const PlayerView& player, float nowMs);

    scene::Milo* track_ = nullptr;
    std::string part_;
    bool drums_ = false, lefty_ = false;
    const scene::Cam* cam_ = nullptr;
    std::array<float, 4> screenRect_{0, 0, 1, 1};
    gfx::ScenePose pose_;
    gfx::Camera camera_;
    const scene::Environ* currentEnv_ = nullptr;
    const scene::Environ* trackEnv_ = nullptr;
    bool frameSet_ = false;
    int viewW_ = 1280, viewH_ = 720;
    float yPerSecond_ = 60, bottomY_ = -10, topY_ = 120, speed_ = 1, nowY_ = 0;
    std::array<float, 5> slotX_{-8, -4, 0, 4, 8};
    std::array<SlotSet, 5> sets_{};
    std::array<std::string, 5> slotColors_{"green", "red", "yellow", "blue", "orange"};
    const scene::TrackWidget *barW_ = nullptr, *beatW_ = nullptr, *halfBeatW_ = nullptr;
    std::string drawGroup_, animGroup_;
    const scene::Group* smasherGroup_ = nullptr;  // smasher_plate_<instrument>'s draw_order.grp
};
