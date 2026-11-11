// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <map>
#include <functional>
#include <memory>
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
        std::array<int, 5> laneOfBit{0, 1, 2, 3, 4};  // the gem's bit -> the track's lane
        int heldGem = -1;  // the sustain being held (its tail shortens from the now bar)
        float whammy = 0;  // the whammy bar (0 rest .. 1): the held tail's wobble and brightness
    };

    RbTrackView();
    ~RbTrackView();
    // `part`: "guitar", "bass" or "drum". Throws std::runtime_error on failure.
    void load(const GameData& data, scene::MiloLibrary& library, const std::string& part);
    bool loaded() const { return track_ != nullptr; }
    const std::string& part() const { return part_; }
    void setLefty(bool lefty) { lefty_ = lefty; }
    // The track's intro (gem_track.dta play_intro: intro.trig plays
    // intro_anim.grp): the song time it starts; before it the track is
    // retracted (not drawn). Unset: the track is out.
    void setIntroAt(float songMs) { introAtMs_ = songMs; }
    // The players' layout (wave 02): the share of the screen the track
    // draws in (x, y, w, h from the top left), else its camera's own.
    void setScreenRect(const std::array<float, 4>& r) { layout_ = r, hasLayout_ = true; }
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
    void drawTails(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const PlayerView& player, float nowMs);

    scene::Milo* track_ = nullptr;
    std::string part_;
    bool drums_ = false, lefty_ = false;
    float introAtMs_ = -1e30f;
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
    // The drums' lanes: the kick (0) and the pads where smasher_plate_drum's
    // gem_smasher0..4 stand [C data].
    std::array<float, 5> drumX_{0, -7.5f, -2.5f, 2.5f, 7.5f};
    std::array<float, 4> layout_{0, 0, 1, 1};
    bool hasLayout_ = false;
    std::array<SlotSet, 5> sets_{};
    std::array<std::string, 5> slotColors_{"green", "red", "yellow", "blue", "orange"};
    const scene::TrackWidget *barW_ = nullptr, *beatW_ = nullptr, *halfBeatW_ = nullptr;
    std::string drawGroup_, animGroup_;
    // The sustain tails (track/gem_tail.milo's tail02.mesh and tail_<colour>.mat).
    scene::Milo* tails_ = nullptr;
    std::unique_ptr<gfx::DynamicMesh> tailMesh_;
    std::vector<float> tailVerts_;
    std::vector<uint16_t> tailIndices_;
    struct TailRange {
        const scene::Mat* mat;
        int firstIndex, indexCount;
    };
    std::vector<TailRange> tailRanges_;
    // RB's tail (TailBuilder RB 0x8281A970 / 0x8281A8C8, poll 0x8281C928,
    // rows 0x8281C4A0) [C]: track_graphics' (gem ...) tail values.
    struct TailCfg {
        float pulseRate = 64, pulseSmoothing = 0.5f, offsetX = 0, minAlpha = 0.5f, maxAlpha = 1, alphaSmoothing = 0.25f,
              fadeDistance = 20, minFreq = 15, maxFreq = 4.5f, minAmp = 0.25f, maxAmp = 1.5f;
        int segments = 90;          // 0x5A [C 0x8281A8C8]
        float segLen = 4.4444447f;  // 0x408E38E4 [C 0x8281A8C8]
    } tailCfg_;
    // One tail's wobble (0x8281C928): 300 samples written at pulse_rate a
    // second while held, the phase, the smoothed whammy and alpha.
    struct TailState {
        std::array<float, 300> ring{};
        int head = 0;
        float phase = 0, smoothW = 0, smoothA = 0;
        double clockMs = -1;
        bool seen = false;
    };
    std::map<long, TailState> tailStates_;
    std::vector<std::array<float, 4>> tailRow_;  // tail02.mesh's cross-section: x, z, u, v (by x)
    float tailRowV1_ = 1;                        // the template's v at its second row
    float lastTailMs_ = -1e9f;
    void pollTail(TailState& t, bool held, float whammy, float nowMs);
    // The smashers' colours: each gem_smasherN's PropAnims set to its slot
    // colour's frame (gem_smasher.dta set_color).
    void colorSmashers();
    // A PropAnim's material keys at `frame` (diffuse_tex: the step key's
    // Tex; color: linear) into pose_; false when its record is not read.
    bool applyPropAnim(const scene::Object& anim, float frame);
    const scene::Group* smasherGroup_ = nullptr;  // smasher_plate_<instrument>'s draw_order.grp
};
