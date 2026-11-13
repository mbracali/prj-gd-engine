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
#include "render/particles.h"
#include "render/scene_gpu.h"

class GameData;
class TempoMap;
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
class EventTrigger;
class ParticleSys;
class Directory;
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
        uint32_t heldFrets = 0;  // the frets held (the chart's bits): their smashers glow
        std::function<bool(int gem)> struck;  // the player hit it (the hit flames); unset: `hit`
        bool soloActive = false;  // in a solo section (player.dta solo_start / solo_end)
    };

    // The fret board's effects, counted for the walks (GD05-045): hits whose
    // smashers launched no particle, deploys that showed no deploy visual.
    struct FxStats {
        int hits = 0, hitsNoFx = 0, misses = 0, burns = 0, deploys = 0, deploysNoFx = 0, phrases = 0, glows = 0;
        // GD05-096: hits whose smasher effects drew particles within 100 ms
        // (seen) or not (unseen); particles the smashers' effects drew.
        int hitsSeen = 0, hitsUnseen = 0;
        int soloStarts = 0, soloStops = 0;  // guitar_solo_start / _stop.trig fired
        long fxParticlesDrawn = 0;
    };
    static FxStats& fxStats();

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
    // The panel's track_configuration (trackpanel.milo's 1_player_wide ...)
    // as track_paneldir.dta's update gives it to each gem track, through
    // gem_track.dta's set_pitch / set_fov / set_cam_pos / set_fade [C data]:
    // rotater.grp's pitch, game.cam's y_fov and place, track.env's fade and
    // the camera's far plane, top_y = fade_start + fade_length + 5; and
    // configure_tracks' y_per_second = top_y / view_time (view_time_easy ..
    // view_time_expert by the players' mean difficulty) [C data].
    struct Config {
        float pitch = -44, fov = 15, yPos = -229, zPos = 47, fadeStart = 330, fadeLength = 100;
        float viewTimeEasy = 2.4f, viewTimeExpert = 1.5f;
    };
    void configure(const Config& c, float meanDifficulty);
    bool configured() const { return configured_; }
    // The walks' now-bar probe: one normal gem in each lane at the now bar
    // (y 0), drawn through the last frame's camera and view; how many lanes.
    int drawNowBarGems(gfx::Renderer& r, gfx::SceneCache& cache);
    float nowBarScreenY() const { return nowBarY_; }  // the now bar's height in the view (0 top .. 1 bottom), last draw
    void draw(gfx::Renderer& r, gfx::SceneCache& cache, const Song& song, const PlayerView& player, float nowMs, float trackSpeed);

    // The walks' checks (GD05-070). The parts of the draw group as the last
    // frame drew them, and as the data orders them: static.grp's members,
    // its now_bar.grp as the smasher plate's setup_draworder leaves it
    // (smasher_plate.dta: smasher_plate.mesh, before_gems.grp = every
    // smasher's base_bottom.grp then every base_top.grp; gem_smashers.grp
    // and after_hide.grp hidden), widgets.grp's tails and gems, and
    // smasher_fx.grp with after_gems.grp (gem_track.dta track_reset: every
    // smasher's effects.grp) [C data].
    const std::vector<std::string>& drawOrder() const { return drawLog_; }
    std::vector<std::string> expectedDrawOrder() const;
    // The Overdrive (star) tails the last frame drew, and those drawn with
    // tail_bonus.mat (the TailBuilder's star material, RB 0x8281BEC8).
    int starTails() const { return starTails_; }
    int starTailsBonus() const { return starTailsBonus_; }
    // The last frame's tails again (star ones only, or the others), through
    // its camera: the walks' colour probe.
    void drawLastTails(gfx::Renderer& r, gfx::SceneCache& cache, bool star);

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
    bool configured_ = false;
    float camFov_ = 0, camFar_ = 0, envFadeStart_ = 0, envFadeEnd_ = 0, nowBarY_ = -1;
    const scene::Environ* fadeEnv_ = nullptr;  // track.env (set_fade)
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
        bool star = false;
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

    // ---- The track's effects (GD05-045) ----
    // tracksystem.milo's EventTriggers (the track's own and each
    // gem_smasherN's), played as RB plays them: their anims as tasks at 30
    // frames a second, shows / hides, and the particle_trigger /
    // part_launcher types launching particles [C data: gem_smasher.dta,
    // gem_track.dta, particle_launcher.dta, rnd_objects.dta].
    struct AnimTask {
        const scene::Object* anim = nullptr;
        float from = 0, to = 0, scale = 1;
        double t0 = 0;  // song ms it started
        int type = 0;   // 0 range (holds the end), 1 loop
        int rate = 0;   // the entry's RndAnimatable rate (render/anim_rate.h)
    };
    struct FxDir {
        const scene::Directory* dir = nullptr;
        std::vector<AnimTask> tasks;
        gfx::ParticleSet parts;  // the directory's particle systems, one running copy
        bool burning = false, glowing = false;
    };
    std::array<FxDir, 5> smashFx_;
    FxDir trackFx_;
    std::map<const scene::Object*, float> animFrame_;  // the last frame each anim was set to
    std::map<const scene::ParticleSys*, gfx::ParticleSystem*> fxSystem_;
    std::unique_ptr<gfx::ParticleRenderer> particles_;
    bool drawParticles_ = false;
    const TempoMap* tempo_ = nullptr;  // the song's (draw), for the beat rates
    int fxLaunchedNow_ = 0;  // particles launched by the triggers being fired
    int hitsPending_ = 0;    // hits not yet seen drawn (fxStats)
    bool wasSolo_ = false;
    float hitsPendingMs_ = 0;
    float lastFxMs_ = -1e30f;
    // What the view saw last frame (the events are its changes).
    std::vector<uint8_t> gemSeen_;  // bit 0 struck, 1 missed
    const Chart* seenChart_ = nullptr;
    std::vector<int> phraseLast_;   // each Overdrive phrase's last gem
    bool wasOd_ = false;
    int lastHeld_ = -1;
    uint32_t lastHeldSlots_ = 0;
    void loadFx();
    void fxReset(FxDir& fx);
    // Fires the directory's trigger `name`; false when it has none.
    bool fxTrigger(FxDir& fx, const std::string& name, float nowMs);
    void fxTrigger(FxDir& fx, const scene::EventTrigger& t, float nowMs);
    void fxAnimate(FxDir& fx, const scene::Object* anim, float from, float to, int type, float scale, float nowMs, int rate = 0);
    void fxSetFrame(FxDir& fx, const scene::Object* anim, float frame);
    int fxLaunch(FxDir& fx, const scene::Object* launcher);
    void fxEvents(const PlayerView& player, float nowMs);
    void fxUpdate(float nowMs);
    void drawSmasherFx(gfx::MaterialRenderer& mr, gfx::SceneCache& cache);
    void drawSmasherPlate(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Environ* env);
    // A member of the draw group the view draws itself, at its place.
    bool drawPart(gfx::MaterialRenderer& mr, gfx::SceneCache& cache, const scene::Group* g, const scene::Environ* env);
    std::vector<std::string> drawLog_;
    const Song* curSong_ = nullptr;
    const PlayerView* curPlayer_ = nullptr;
    float curNowMs_ = 0;
    int starTails_ = 0, starTailsBonus_ = 0;
    // A PropAnim's keys at `frame` into pose_ (and `fx`'s particle copies).
    bool applyPropAnimFx(const scene::Object& anim, float frame, FxDir* fx);
};
