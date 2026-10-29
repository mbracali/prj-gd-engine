// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "data/band.h"
#include "data/matlib.h"
#include "render/band_anim.h"
#include "render/band_view.h"
#include "render/device.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"

class Disc;
class Globals;
struct Chart;

// GH3's crowd (create_crowd_models, update_crowd_model_cam,
// crowd_play_adjusting_random_anims in guitar_crowd.qb; docs/venues.md
// "Crowd"): a few 3D crowd peds (<zone>_crowd_models: a skin of the zone
// pak on the GH3_Ped skeleton, clapping hands from global.pak) stand far
// below the venue, each rendered every frame by its own camera (20 degree
// field of view, 3.5 m away, turned with the venue camera) into a 128 x 256
// (six peds or fewer: 256 x 512) viewport texture that replaces one of the venue's tw_billboard cards
// (CreateViewportTextureOverride): every card quad of the crowd shows one
// of these renders. The peds dance their Crowd_Profiles anim set, picked by
// the rock meter (bad / normal / good) and the song's tempo.
class Crowd {
public:
    Crowd() = default;
    ~Crowd() { shutdown(); }
    Crowd(const Crowd&) = delete;
    Crowd& operator=(const Crowd&) = delete;
    // Loads the zone's crowd models; `shared`: the band's skeletons and
    // clips (null: loaded here). False and `error` when the zone has none.
    bool start(const Disc& disc, const Globals& g, const venue::Venue& v, const Chart& chart, const venue::PerfTimeline* perf,
               const band::Library* shared, double startMs, std::string& error);
    // Uploads the peds and makes their render targets.
    bool attach(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib, std::string& error);
    void shutdown();
    // Processes the peds' animation events up to song time `ms`.
    void advance(double ms);
    // Renders each ped into its texture for the venue camera `cam` (call
    // before the venue draws; restores the bound target and viewport).
    void render(double ms, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
                const venue::LightShow& show);
    // The texture replacing a card texture (textureasset key); empty if none.
    gfx::TextureId texture(uint32_t cardTexture) const;
    size_t size() const { return peds_.size(); }
    // The song's Crowd_StartLighters .. Crowd_StopLighters spans (its
    // performance array): crowd_monitor_performance holds the lighters up
    // (right hand rhand_lighter, the anim set's `special`) while the crowd is
    // not bad (call before advance) [C scripts guitar_crowd.qb].
    void setLighterSpans(std::vector<std::pair<double, double>> spans) { lighterSpans_ = std::move(spans); }
    bool lightersOn(double t) const;

private:
    struct Ped {
        uint32_t name = 0, card = 0;
        std::unique_ptr<band::Model> model;
        bandanim::Rig rig;
        bandanim::Node body;
        const qb::Struct* animSet = nullptr;
        uint32_t lastSpeed = 0;
        bool first = true;
        double nextAt = -1e18;  // the current clip's end
        uint64_t rng = 1;
        bandanim::Mat34 root;
        venue::Vec3 pos{};
        std::vector<bandanim::Mat34> world, skin;
        int view = -1, lighterView = -1;
        std::unique_ptr<band::Model> lighterModel;  // the same ped with the right hand's lighter
        bool lighters = false;  // the state its anims follow
        double checkedAt = -1e18;  // the last game frame crowd_monitor_performance looked at
        gfx::TargetId target;
    };
    const anim::Clip* pick(Ped& p, double t, float& blend);
    uint32_t random(Ped& p, uint32_t n);

    const Globals* g_ = nullptr;
    const venue::PerfTimeline* perf_ = nullptr;
    std::unique_ptr<band::Library> ownLib_;
    const band::Library* lib_ = nullptr;
    std::vector<int32_t> fretbars_;
    float slowTempo_ = 75, medTempo_ = 115, fastTempo_ = 145;
    float blendTempo_ = 0.67f, blendBad_ = 0.5f, blendSpecial_ = 0.4f, blendSlow_ = 0.6f, blendMed_ = 0.4f, blendFast_ = 0.2f;
    int width_ = 128, height_ = 256;  // the imposter viewport
    std::vector<Ped> peds_;
    std::vector<std::pair<double, double>> lighterSpans_;
    gfx::RenderDevice* dev_ = nullptr;
    BandView view_;
};
