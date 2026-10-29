// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/qb.h"
#include "game/chart.h"
#include "render/device.h"
#include "render/venue_camera.h"
#include "script/globals.h"

class Disc;
namespace qs {
class Vm;
}
namespace venue {
struct Venue;
}

// GH3's venue particles and pyro (docs/venues.md "Particles"): the zone's
// scripts create particle systems (CreateParticleSystem_fast with a
// GP_* parameter struct of scripts\fx\particles: emitter box, direction and
// spread, speed, force, life, sizes, colours, the quick material's texture
// and blend), from the light show's pyro notes (note 56 ->
// <zone>_lightshow_mapping.pyro_script with the performance) and the
// performance changes (<zone>_lightshow_mapping.transitions.performance:
// e.g. z_dive's sparks while the crowd is happy). The zone's scripts run on
// gd04's QScript VM stepped on the song clock at 60 Hz; each particle is a
// function of its system's start time and seed, so frames do not change
// what is drawn.
class Particles {
public:
    Particles();
    ~Particles();
    bool start(const Disc& disc, const Globals& g, const venue::Venue& v, const Chart& chart, const venue::PerfTimeline* perf,
               double startMs, std::string& error);
    bool attach(gfx::RenderDevice& dev, const Disc& disc, std::string& error);
    void shutdown();
    // Runs the zone's scripts and creates systems up to song time `ms`.
    void advance(double ms);
    // Draws the live particles (blended, depth tested, no depth writes).
    void draw(const venue::CamPose& cam, double ms);
    static gfx::ProgramDesc programDesc();
    size_t systems() const { return systems_.size(); }
    size_t created() const { return created_; }

    // One CreateParticleSystem_fast's parameters, decoded.
    struct Params {
        venue::Vec3 box{}, target{0, 1, 0}, force{};
        float spread = 0;  // anglespread, degrees
        float lifeMin = 1, lifeMax = 1, rate = 0, lifetime = 0, delay = 0;
        int burst = 0;
        float sizeStart = 0.1f, sizeStartVar = 0, sizeEnd = 0.1f, sizeEndVar = 0;
        float speedMin = 1, speedMax = 1;
        bool alignWithPath = false;
        float pathTime = 0.1f;
        uint32_t color = 0xFFFFFFFF;          // RGBA
        std::vector<uint32_t> gradient;       // particlecolor, over the life
        uint32_t texture = 0;                 // quickmaterial.diffusetexture
        bool additive = true;
        float alphaCut = 0;
    };

private:
    struct System {
        Params p;
        venue::Vec3 pos{};
        double start = 0;
        uint64_t seed = 1;
        double end() const;
    };
    void create(const venue::Vec3& pos, const qb::Value* params, double t);
    void step(double t);

    std::unique_ptr<Globals> g_;  // qb.pak's globals plus the zone's scripts
    std::unique_ptr<qs::Vm> vm_;
    const venue::Venue* v_ = nullptr;
    const venue::PerfTimeline* perf_ = nullptr;
    uint32_t pyroScript_ = 0;
    std::vector<uint32_t> perfScripts_;
    std::vector<double> pyroTimes_;
    size_t pyroAt_ = 0, perfAt_ = 0;
    venue::Performance lastPerf_ = venue::Performance::Medium;
    double vmTime_ = 0;
    double offsetMs_ = 100;
    std::vector<System> systems_;
    size_t created_ = 0;
    uint64_t nextSeed_ = 0x2545F4914F6CDD1Dull;

    gfx::RenderDevice* dev_ = nullptr;
    gfx::ProgramId program_;
    gfx::BufferId vb_;
    size_t vbSize_ = 0;
    std::unordered_map<uint32_t, gfx::TextureId> textures_;
    gfx::TextureId white_;
    std::vector<float> scratch_;
};
