// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
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

// The venue's animated game objects (docs/venues.md "Stage peds"): the
// zone's gameobject nodes with a skinned Model (StagePeds\SPed_*.skin: the
// security guards, stage divers, devil girls, the caged dancer, the hoop
// girl; Venues\*\Ven_*.skin: the art deco dragon and knights, the hell
// puppet and hammer), created at the level's start, on the skeleton their
// profile's creation params name, lit by the profile's light group. Their
// trigger scripts (run by render/particles.h's VM on the node) play their
// clips: GameObj_PlayAnim / GameObj_WaitAnimFinished, or
// crowd_play_adjusting_random_anims with a Crowd_Profiles anim set.
class StagePeds {
public:
    StagePeds() = default;
    ~StagePeds() { shutdown(); }
    StagePeds(const StagePeds&) = delete;
    StagePeds& operator=(const StagePeds&) = delete;
    // False when the zone has none (or none loads; `error` says why).
    bool start(const Disc& disc, const Globals& g, const venue::Venue& v, const band::Library& lib, std::string& error);
    bool attach(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib, std::string& error);
    void shutdown();
    // GameObj_PlayAnim on node `obj` at song time `t`: the clip's length
    // (ms), or < 0 when `obj` is not one of these objects or has no such clip.
    double play(uint32_t obj, uint32_t anim, float blend, double t);
    // `scripts`: the zone VM's hide / Unhide (null: none).
    void draw(double ms, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
              const venue::LightShow& show, const class Particles* scripts);
    size_t size() const { return peds_.size(); }

private:
    struct Ped {
        uint32_t node = 0;
        std::unique_ptr<band::Model> model;
        bandanim::Rig rig;
        bandanim::Node body;
        bandanim::Mat34 root;
        std::vector<bandanim::Mat34> world, skin;
        std::vector<uint32_t> lightGroups;
        int view = -1;
    };
    std::vector<Ped> peds_;
    std::unordered_map<uint32_t, size_t> byNode_;
    gfx::RenderDevice* dev_ = nullptr;
    BandView view_;
};
