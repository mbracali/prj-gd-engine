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

// The highway of GH2's PS2 engine (SLUS_215.86), drawn from the game's own
// track scene (ui/track_panel.dta: TrackPanel on track/track.milo, a
// PanelDir of plain meshes and views) and config/track_graphics.dtb, the way
// the TrackPanel code places things (addresses in track_view.cpp): the
// surface scrolling with the song, the lane lines and rails, the chart's
// gems built from per-colour mesh parts (track_graphics gem parts: body,
// glow, top, miss, HOPO top, star, star top, plus the bonus and star
// pieces), sustain tails built by code (bent by the whammy bar), the
// smashers reacting to held frets and hits, the smash effects (flame lights
// and particles), the burn and the Star Power tail sparks, beat and measure
// lines from the tempo map, the Star Power lightning and rails, the intro
// (extend sequence, smashers popping), the explode when the song is won, the
// lefty flip, and each player's track of the two-player (dual perspective)
// layout.
class TrackView {
public:
    TrackView();
    ~TrackView();
    // Loads the scene and the config; throws std::runtime_error on failure.
    // `player`: -1 for one player, else 0 / 1, the player's track of the
    // two-player layout (its half of the screen).
    void load(const GameData& data, scene::MiloLibrary& library, int player = -1);
    bool loaded() const { return track_ != nullptr; }
    // A new song (clears the per-song state and effects). `introStartMs`:
    // the song time the track's extend sequence starts (game.dta
    // extend_track; none: the track is out already).
    void reset(float introStartMs = -1e9f);
    // Lefty flip (the player's option): slot s draws at slot 4 - s
    // (0x00152500) [C].
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
    // Since reset: phrase_captured combo effects started (per slot).
    int comboStarts() const { return comboStarts_; }
    // Since reset: frames whose far-end fade was laid over the track (the
    // venue strips saved before it, 0x001511c0).
    int fadeDraws() const { return fadeDraws_; }
    // The player's track surface (0x001536e0, 0x00153888, 0x00155078):
    // track/surfaces/<name>_keep.bmp as track_surface.mat's texture, <name>
    // the mode's track_surface_override (config/modes.dta: rosewood in
    // practice and the tutorial) or else the player's character (outfit
    // name, alterna1...; empty: the song's quickplay one). `mode`: gamecfg's
    // mode (quickplay, practice...).
    void setSurface(const GameData& data, const std::string& mode, const std::string& character, const std::string& songId);
    const std::string& surfaceName() const { return surfaceName_; }
    // Since reset: frames whose surface drew with that texture.
    int surfaceOverrides() const { return surfaceOverrides_; }

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
    // A mesh at `at` composed after its own local transform (`rot`: its
    // rotation replaced, the star spin).
    void drawPart(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Mesh* m, float x, float y, const scene::Xfm* rot = nullptr);
    void drawSurface(gfx::MaterialRenderer& mr, gfx::SceneCache& cache);
    void drawBeatLines(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Song& song, float nowMs);
    void drawGems(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void buildTail(const Gem& g, int gemIndex, int slot, const Player& player, float nowMs);
    void drawTails(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawSmashers(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawCombo(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, float nowMs);
    void drawEffects(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawParticles(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawLightning(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const Player& player, float nowMs);
    void drawExplode(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, float nowMs);
    float slotX(int slot) const { return slotX_[size_t(lefty_ ? 4 - slot : slot)]; }
    // The far-end fade (0x001511c0): fadeSave copies the picture's strips
    // around track_fade.trans before the track is drawn (false: none this
    // frame), fadeDraw lays them back over it.
    bool fadeSave(gfx::Renderer& r);
    void fadeDraw(gfx::MaterialRenderer& mr, gfx::SceneCache& cache);

    // A strip of the fade: its rectangle in the bound picture's pixels and
    // the target holding its copy.
    struct FadeStrip {
        int x = 0, y = 0, w = 0, h = 0;
        gfx::TargetId copy;
        int copyW = 0, copyH = 0;
    };
    std::array<FadeStrip, 2> fade_;  // track_fade.tex below the point, track_fade2.tex above it
    gfx::RenderDevice* fadeDevice_ = nullptr;
    std::unique_ptr<gfx::DynamicMesh> fadeMesh_;
    int fadeDraws_ = 0;
    int viewX_ = 0, viewY_ = 0;  // the camera's viewport origin (picture pixels)

    scene::Milo* track_ = nullptr;
    int player_ = -1;
    bool lefty_ = false;
    const scene::Cam* cam_ = nullptr;
    std::array<float, 4> screenRect_{0, 0, 1, 1};
    float trackScale_ = 1;  // dual perspective: track_scale
    float nearPlane_ = 50, farPlane_ = 200;
    gfx::ScenePose pose_;
    gfx::Camera camera_;
    const scene::Environ* currentEnv_ = nullptr;
    const scene::Environ* trackEnv_ = nullptr;  // track.env
    bool frameSet_ = false;
    int viewW_ = 1280, viewH_ = 720;

    // track_graphics.dtb values.
    float yPerSecond_ = 60, bottomY_ = -10, horizonY_ = 120, removeY_ = -15;
    float nowbarTailClip_ = 1.5f;
    float tailGlowWidth_ = 1.5f, tailGlowTightWidth_ = 0.7f;
    float gemFps_ = 30;
    std::array<float, 5> slotX_{-8, -4, 0, 4, 8};  // (slot - 2) x 4 (0x00152530)
    std::array<std::string, 5> slotColors_{"green", "red", "yellow", "blue", "orange"};
    float speed_ = 1;
    float nowY_ = 0;  // the song position in track units (the rig's y)

    // The gem template (0x00159e28): per part type (track_graphics gem
    // parts: kMeshGem 0, kMeshGemGlow 1, kMeshGemMiss 2, kMeshTop 3,
    // kMeshHopoTop 4, kMeshStar 5, kMeshStarTop 6) and slot, the mesh
    // "<colour>_<part>.mesh", else "<part>.mesh", else the part's default.
    std::array<std::array<const scene::Mesh*, 5>, 7> parts_{};
    const scene::Mesh *bonusHopo_ = nullptr, *bonusGem_ = nullptr, *bonusGlow_ = nullptr, *topStarBlack_ = nullptr;
    const scene::Mesh *star2_ = nullptr, *starBase_ = nullptr, *starMiss_ = nullptr;
    // The template's draw order of its mesh copies (one per distinct mesh).
    std::unordered_map<const scene::Mesh*, int> partOrder_;
    // Beat line meshes: quarter, half beat, beat, bar (0x0015c4a0).
    std::array<const scene::Mesh*, 4> beatMeshes_{};
    const scene::Mesh *surface_ = nullptr, *laneLines_ = nullptr, *rails_ = nullptr;
    // The surface texture (setSurface); null: the scene's own track_surface.tex.
    const scene::Tex* surfaceTex_ = nullptr;
    std::string surfaceName_;
    int surfaceOverrides_ = 0;
    float bonusStartMs_ = -1e9f;  // Star Power's start (the lightning flipbook)

    // The tail template: tail02.mesh's cross-section (sorted by x).
    std::vector<scene::MeshVertex> tailRow_;
    int tailSegments_ = 30;
    float tailSegLen_ = 4.545f;
    std::array<float, 100> whammyHist_{};
    int whammyIndex_ = 0;

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
