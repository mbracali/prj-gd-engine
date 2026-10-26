// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/player.h"
#include "render/scene_gpu.h"

class Canvas;
class GameData;
namespace scene {
class Directory;
class Milo;
class MiloLibrary;
}  // namespace scene
namespace gfx {
class Renderer;
}

// GH2's in-song HUD for one player (quickplay / career: config modes.dta
// hud_file hud_sp.milo, the "hud" HudPanel of ui/hud_panel.dta): the score
// meter (BandScoreDisplay digits, BandStreakDisplay multiplier and streak
// lights) on the left and the rock meter (BandCrowdMeterDir needle and
// lights, BandStarMeterDir amp tube) on the right, drawn from the HUD's own
// scenes through the UI camera and driven by the Player's state the way the
// game's display classes are (addresses in hud_view.cpp), plus the HUD
// messages ("STAR POWER READY!", "SONG FAILED!") in the HUD's font.
class HudView {
public:
    ~HudView();
    // Loads hud/hud_sp.milo and its proxies; throws std::runtime_error.
    void load(const GameData& data, scene::MiloLibrary& library);
    bool loaded() const { return hud_ != nullptr; }
    void reset();
    void onEvents(const std::vector<PlayerEvent>& events, const Player& player, float nowMs);
    // Draws the HUD over the bound 16:9 picture (its own camera, depth
    // cleared); the message text goes through the canvas.
    void draw(gfx::Renderer& r, gfx::SceneCache& cache, Canvas& canvas, const Player& player, float nowMs);

private:
    struct Part {
        const scene::Directory* dir = nullptr;  // the proxied directory
        const scene::Object* proxy = nullptr;   // its proxy object in hud_sp
    };

    void drawDir(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Directory& dir, const scene::Xfm& base,
                 const scene::Environ* env);
    void drawObject(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object* o, const scene::Xfm& base,
                    const scene::Environ* env, int depth);
    void setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env);
    void drive(const Player& player, float nowMs);
    void driveScore(int score);
    void driveStreak(int streak, int multiplier, bool star);
    const scene::Object* in(const Part& p, const std::string& name) const;
    template <class T> const T* in(const Part& p, const std::string& name) const {
        const scene::Object* o = in(p, name);
        return o ? o->as<T>() : nullptr;
    }
    void setMat(const Part& p, const std::string& mesh, const std::string& mat);
    void drawMessage(Canvas& canvas, float nowMs);
    void showMessage(const std::string& text, float nowMs);

    scene::Milo* hud_ = nullptr;
    Part score_, streak_, star_, crowd_;
    gfx::ScenePose pose_;
    gfx::Camera camera_;
    const scene::Environ* currentEnv_ = nullptr;
    bool frameSet_ = false;
    int viewW_ = 1280, viewH_ = 720;
    // Materials the display classes put on meshes.
    std::unordered_map<const scene::Mesh*, const scene::Mat*> matOverride_;
    // Mesh animations (the star meter's tube glow) as per-frame geometry.
    std::unordered_map<const scene::Mesh*, std::pair<const scene::MeshAnim*, float>> meshAnims_;
    std::unique_ptr<gfx::DynamicMesh> meshAnimBuffer_;
    gfx::RenderDevice* device_ = nullptr;
    gfx::SceneCache* cache_ = nullptr;

    // Display state.
    float messageMs_ = -1e9f;
    std::string message_;
    float shownRock_ = 0.5f, shownStar_ = 0;
    float lastMs_ = 0;
    float meterShakeMs_ = -1e9f, scoreShakeMs_ = -1e9f;
    int lastMultiplier_ = 1;
};
