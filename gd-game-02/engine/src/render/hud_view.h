// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/player.h"
#include "render/particles.h"
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

// GH2's in-song HUD (config/modes.dta hud_file, loaded by the "hud" HudPanel
// of ui/hud_panel.dta): hud_sp (quickplay / career), hud_vs (face-off and
// pro face-off: two score meters and star meters around the lead meter),
// hud_coop (the band's score, streak, shared star meter and rock meter) or
// hud_practice (the streak display only). Each HUD is a PanelDir whose
// Band* objects are proxies of the display scenes (score_display,
// streak_display, star_meter, crowd_meter, lead_meter, coop_*), drawn from
// the HUD's own scenes through the UI camera and driven by the players'
// state the way the game's display classes are (addresses in hud_view.cpp),
// plus the HUD messages ("STAR POWER READY!", "SONG FAILED!") in the HUD's
// font.
class HudView {
public:
    ~HudView();
    // Loads hud/<hudFile> (e.g. "hud_sp.milo") and its proxies; throws
    // std::runtime_error.
    void load(const GameData& data, scene::MiloLibrary& library, const std::string& hudFile = "hud_sp.milo");
    bool loaded() const { return hud_ != nullptr; }
    // A new song. `introStartMs`: when the game's intro (extend_track)
    // starts; the meters slide in 1.8 s later (game.dta). None: shown at once.
    void reset(float introStartMs = -1e9f);
    // The song is won (hud_panel.dta game_won_msg: set_win_showing TRUE,
    // FALSE WIN_MESSAGE_SECS (3) later; the flag is youwin.mesh's showing,
    // 0x8268EBD8) [C].
    void won(float nowMs) { wonMs_ = nowMs; }
    // Player `player`'s events of this frame.
    void onEvents(const std::vector<PlayerEvent>& events, int player, float nowMs);
    // Draws the HUD over the bound 16:9 picture (its own camera, depth
    // cleared); the message text goes through the canvas. `players`: one or
    // two (the second for the two-player HUDs).
    void draw(gfx::Renderer& r, gfx::SceneCache& cache, Canvas& canvas, const std::vector<const Player*>& players, float nowMs);

private:
    enum class Kind { Score, Streak, Star, Crowd, Lead };
    // One Band* display: a proxied scene with its own pose (two displays can
    // show the same scene, e.g. both players' star meters).
    struct Part {
        Kind kind = Kind::Score;
        int player = 0;                        // whose state it shows
        const scene::Directory* dir = nullptr; // the proxied directory
        const scene::Object* proxy = nullptr;  // its proxy object in the HUD
        gfx::ScenePose pose;
        std::unordered_map<const scene::Mesh*, const scene::Mat*> matOverride;
        std::unordered_map<const scene::ParticleSys*, gfx::ParticleSet> particles;
        float shownStar = 0, shownRock = 0.5f;
        bool peak = false, peakKnown = false;
        int leadRange = 500;
    };
    struct Message {
        std::string text;
        float startMs = -1e9f;
    };

    void drawDir(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Directory& dir, const scene::Xfm& base,
                 const scene::Environ* env, Part* part);
    void drawObject(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Object* o, const scene::Xfm& base,
                    const scene::Environ* env, Part* part, int depth);
    void setEnv(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env, const gfx::ScenePose& pose);
    void drive(const std::vector<const Player*>& players, float nowMs);
    void driveScore(Part& p, int score);
    void driveStreak(Part& p, int streak, int multiplier, bool star);
    void driveStar(Part& p, const Player& player, float dt, float loop);
    void driveCrowd(Part& p, const Player& player, float dt, float loop, float nowMs);
    void driveLead(Part& p, int lead);
    static const scene::Object* in(const Part& p, const std::string& name);
    template <class T> static const T* in(const Part& p, const std::string& name) {
        const scene::Object* o = in(p, name);
        return o ? o->as<T>() : nullptr;
    }
    static void setMat(Part& p, const std::string& mesh, const std::string& mat);
    void drawMessages(Canvas& canvas, float nowMs);
    void drawMessage(Canvas& canvas, const scene::Text* text, const scene::Object* anim, const Message& m, float nowMs);
    void showMessage(int player, const std::string& text, float nowMs);

    scene::Milo* hud_ = nullptr;
    std::vector<std::unique_ptr<Part>> parts_;
    gfx::ScenePose pose_;  // the HUD's own objects
    gfx::Camera camera_;
    const scene::Environ* currentEnv_ = nullptr;
    bool frameSet_ = false;
    int viewW_ = 1280, viewH_ = 720;
    // Mesh animations (the star meter's tube glow) as per-frame geometry.
    std::unique_ptr<gfx::DynamicMesh> meshAnimBuffer_;
    std::unique_ptr<gfx::ParticleRenderer> particles_;
    gfx::RenderDevice* device_ = nullptr;
    gfx::SceneCache* cache_ = nullptr;
    float clockFrames_ = 0;

    // Display state.
    Message messages_[3];  // player 0, player 1, shared
    float lastMs_ = 0;
    float introStartMs_ = -1e9f;
    float wonMs_ = -1e9f;
    float meterShakeMs_ = -1e9f, scoreShakeMs_ = -1e9f;
    bool sharedStar_ = false;
};
