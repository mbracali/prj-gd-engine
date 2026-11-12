// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "data/venue.h"
#include "render/band.h"
#include "render/crowd.h"
#include "render/enhanced_fx.h"
#include "render/enhanced_lights.h"
#include "render/mocap_camera.h"
#include "render/particles.h"
#include "render/screen_fx.h"
#include "render/stage_peds.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"
#include "render/venue_view.h"

class Disc;
class Globals;
struct Chart;
namespace gfx {
class RenderDevice;
}

// A song's venue: the zone (data/venue.h), its camera cuts and light show
// driven by the song clock, drawn behind the highway (ui/song_screen.cpp).
class VenueStage {
public:
    VenueStage() = default;
    ~VenueStage();
    VenueStage(const VenueStage&) = delete;
    VenueStage& operator=(const VenueStage&) = delete;
    // The zone for the song: GD10_VENUE=<zone> (development aid), else the
    // level the scripts chose ($current_level: quickplay_start_song's random
    // venue, the career tier's venue), else the first listed venue.
    static std::string chooseZone(const Globals& g);
    // Enhanced > Enable Particles FX (off: no venue or band particles, from the next song).
    static inline bool particlesOn = true;
    // Enhanced Options' picture chain (render/enhanced_fx, gd02's) after
    // GH3's screen effects, and the world's resolution against the window
    // (Internal Resolution); set by the app.
    static inline gfx::PostSettings post;
    static inline int internalHeight = 0;  // 0: the window's
    // Enhanced: All Band Mates Cast Shadows, More Lights on Venues, Dust in Light Beams (gd02's extras).
    static inline gfx::EnhancedLights::Settings extras;
    // A player's Star Power on (Blur on Star Power), set each frame by the song.
    void setStarPower(bool on) { starPowerOn_ = on; }
    // A fixed camera instead of the director's (the Enhanced backdrop's shot); null: the director's.
    void setFixedCamera(const venue::CamPose* p) { fixedCam_ = p ? *p : venue::CamPose{}, hasFixedCam_ = p != nullptr; }
    // A lens shift in NDC x for this stage's picture (the camera stays; the
    // Enhanced backdrop's framing on the guitarist); 0: none.
    void setLensShift(float x) { lensShift_ = x; }
    const gfx::EnhancedFx* enhancedFx() const { return enh_.get(); }
    uint64_t teslaFrames = 0;  // frames with Tesla bolts drawn
    // Loads the zone (and uploads it when `device` is given); false and
    // `error` on failure. Without a device nothing draws until attach().
    // songStartMs: the song's first ms (a mid-song start: practice, Lou's
    // finish at marker 37); the band, crowd, particles and cameras begin
    // leadInMs before it (they used to begin at -leadInMs and step through
    // the whole song on the first frame: Lou's finish never ended in z_hell).
    bool start(const Disc& disc, const Globals& g, const std::string& zone, const Chart& chart, uint32_t songKey, double leadInMs,
               gfx::RenderDevice* device, std::string& error, double songStartMs = 0);
    // Uploads the venue to `device` (once).
    bool attach(gfx::RenderDevice& device, const Disc& disc, std::string& error);
    // The same, one part per call (the scene; the band; the shadow map and
    // screen FX; the particles; the stage peds; the crowd), so a frame
    // holds one part's upload rather than all of them (~250 ms on the
    // menus' backdrop and the song's first frame). True once all are in
    // (`error` set or no scene: failed).
    bool attachStep(gfx::RenderDevice& device, const Disc& disc, std::string& error);
    bool attached() const { return view_ && attachStep_ >= kAttachSteps; }
    bool attachDone() const { return !venue_ || attachStep_ >= kAttachSteps; }
    // Advances the cuts and the light show to song time `ms` (monotonic).
    // `health`: the rock meter, 0..2 (the crowd's performance [I]), as the
    // 60 Hz step at `ms` set it; call it on every step and as often between
    // steps as wanted (the result does not depend on it).
    // `starPower`: Star Power is on in a one-player song (its camera).
    void update(double ms, float health, bool starPower = false);
    // Advances to `ms` with the last step's performance (a frame between steps).
    void advance(double ms);
    // The player's judged note at step time `ms` (after update(ms)): the band's guitarist reacts.
    // `frets`: the hit note's frets (bit 0 green; the speakers' pulses).
    void playerNote(double ms, bool hit, uint8_t frets = 0) {
        if (band_) band_->playerNote(ms, hit);
        if (particles_ && hit) particles_->playerHit(ms, frets);
    }
    void draw(double ms);
    // The SongWon / SongLost transition (Common_SongWon_Transition /
    // Common_SongLost_Transition): play_win_anims / play_lose_anims, the
    // crowd back to idle, cameras_win / cameras_lose. `p1Won`: player 1 won
    // (the versus modes), `boss`: a boss battle [C data].
    void outro(double ms, bool won, bool versus, bool p1Won, bool boss);
    // play_intro_anims (the intro, encore and boss transitions): the band's
    // intro stance at once, then Stance_A after it. `p2Career`: two-player
    // career / co-op (player 2's member straight to Stance_A).
    void introAnims(double ms, bool p2Career);
    bool smallVenue();

    venue::CamPose camera(double ms) const { return cameras_.pose(ms); }
    const venue::CameraDirector& cameras() const { return cameras_; }
    // Before start: the intro transition's cameras (cameras_encore, ...).
    void setIntroCameras(const std::string& prefix) { cameras_.setIntroCameras(prefix); }
    // The end transition's cameras from `ms` (cameras_preencore / _preboss).
    void outroCameras(double ms, const std::string& prefix) { cameras_.outro(ms, prefix); }
    // Star Power on for player `p` (0 / 1): handle_star_power on its band
    // member (the guitarist / the bassist) waits for the next camera cut,
    // then play_special_anim {Stance_A, Starpower, blendduration 0.0,
    // disable_arms 0} [C scripts guitarist_idle, handle_star_power,
    // CameraCuts_StartCallback's waiting_for_cameracut].
    void starPowerAnim(int p, double ms);
    // A band member's Obj_SwitchScript Transition_PlayAnim_Spawned (role 0 guitarist, 1 bassist).
    void playClip(int role, uint32_t clip, uint32_t stance, bool cycle, double ms);
    const venue::LightShow& lights() const { return lights_; }
    const venue::Venue& venue() const { return *venue_; }
    const VenueView* view() const { return view_.get(); }
    // The band on the stage (render/band.h; GD10_NO_BAND=1: none); null if it could not load.
    const Band* band() const { return band_.get(); }
    Band* band() { return band_.get(); }
    int shadowLight() const { return shadowLight_; }  // the index of the dynamic shadow's light (-1: none yet)
    // The casters (band draws) each dynamic shadow cascade drew last frame (venuetest).
    const std::array<size_t, 2>& shadowCascadeDraws() const { return shadowDraws_; }
    static venue::Performance performance(float health);

private:
    std::unique_ptr<venue::Venue> venue_;
    venue::CameraDirector cameras_;
    venue::MocapCameras mocap_;  // the camera lock targets (mocap / moment cameras)
    std::shared_ptr<const band::ClipMap> zoneCamClips_;  // the zone pak's clips (its transition camera anims)
    venue::LightShow lights_;
    std::unique_ptr<VenueView> view_;
    std::unique_ptr<VenueView> pendingView_;  // prepared (start), uploading (attachStep 0)
    static constexpr int kAttachSteps = 7;
    int attachStep_ = 0;
    std::vector<venue::LightState> lightState_;
    std::vector<uint32_t> hide_;  // the crowd cards' textures
    venue::PerfTimeline perf_;
    std::unique_ptr<Band> band_;
    std::unique_ptr<Crowd> crowd_;  // the crowd's peds and their card renders (GD10_NO_CROWD=1: none)
    std::unique_ptr<StagePeds> peds_;  // the stage peds and animated venue objects (GD10_NO_STAGEPEDS=1: none)
    std::shared_ptr<band::Library> pedLib_;
    std::unique_ptr<Particles> particles_;  // the zone's particles and pyro (GD10_NO_PARTICLES=1: none)
    gfx::RenderDevice* device_ = nullptr;
    std::array<gfx::TargetId, 2> shadow_{};  // the band's dynamic shadow cascades, 1024 / 512 (GD10_NO_SHADOWS=1: none)
    std::array<size_t, 2> shadowDraws_{};    // the casters each cascade drew last frame
    int shadowLight_ = -1;  // the shadow's light, chosen once per venue (VenueStage::shadowPass)
    int flashFrames = 0, outros = 0;       // frames drawn with ScreenFlash's FlashBS (venuetest)
    screenfx::ScreenFx fx_;    // GH3's screen effects (GD10_NO_SCREENFX=1: none)
    bool fxReady_ = false;
    bool starPower_ = false;
    const Globals* globals_ = nullptr;
    std::vector<std::pair<double, double>> lighterSpans_;  // Crowd_StartLighters .. StopLighters
    std::vector<uint32_t> lighterNodes_;  // <zone>_LIGHTER_Geo00..14
    bool lightersOn(double ms) const;
    screenfx::Params fxParams_;
    std::unique_ptr<gfx::EnhancedFx> enh_;
    std::unique_ptr<gfx::EnhancedLights> extras_;
    gfx::TargetId ldr_;
    int ldrW_ = 0, ldrH_ = 0;
    bool starPowerOn_ = false;
    std::array<int, 2> spAnimCuts_{-1, -1};  // handle_star_power waiting: the cut count it waits past (-1: none)
    venue::CamPose fixedCam_;
    bool hasFixedCam_ = false;
    float lensShift_ = 0;
    float starBlur_ = 0;
    double starBlurAt_ = 0;  // Default_TOD_Manager's screen_FX
    void shadowPass(double ms, const venue::CamPose& cam);
public:
    const Crowd* crowd() const { return crowd_.get(); }
    const Particles* particles() const { return particles_.get(); }
    Particles* particles() { return particles_.get(); }
    bool bonePos(uint32_t member, uint32_t bone, venue::Vec3& out) const;
};
