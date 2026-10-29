// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/qb.h"
#include "game/chart.h"
#include "render/device.h"
#include "render/rmath.h"
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
    // The zone's moving level objects: the node trigger scripts (a node's
    // triggerscript runs on it when it is created) and the performance
    // scripts turn and move them with obj_rotx / obj_roty / obj_rotz,
    // obj_rotate, obj_movetopos, obj_setposition (fans, hub caps, the doll,
    // the globes, ...). The node's world transform change at song time `ms`
    // (false: it does not move). Frame-independent: the scripts run on the
    // 60 Hz steps, the motion is evaluated at `ms`.
    bool nodeMotion(uint32_t node, double ms, gfx::Mat4& delta) const;
    size_t movingNodes() const { return motion_.size(); }
    // The player hit a note with these frets (bit 0 green .. 4 orange) at
    // song time `ms` (the speakers' Venue_Pulse* scripts: AddPulseEvent on
    // hitnote_<colour>).
    void playerHit(double ms, uint8_t frets);
    // hide / Unhide on a node by the zone's scripts: 0 hidden, 1 shown, -1 never set.
    int visibility(uint32_t node) const {
        auto it = visibility_.find(node);
        return it == visibility_.end() ? -1 : it->second;
    }
    // GameObj_PlayAnim on a node (render/stage_peds.h): the clip's length in
    // ms, < 0 when not handled.
    void setAnimHook(std::function<double(uint32_t obj, uint32_t anim, float blend, double t)> f) { animHook_ = std::move(f); }
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
        int blend = 1;          // blendmode: 0 diffuse, 1 add, 2 subtract, 3 blend, 4 modulate, 5 brighten [C 823B7768]
        float burn = 0;         // `burn`: burnvalue (premultiplied blend, the burn shader) [C]
        int layout = 0;         // texturelayout: 0 1x1, 1 2x2, 2 1x2, 3 2x1
        float animFps = 0;      // quadanimationfps
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
    // One rotation axis (degrees) or the position: linear in song time.
    struct Axis {
        float a0 = 0;      // the angle at t0
        double t0 = 0;
        float rate = 0;    // degrees a second (signed)
        bool target = false;
        float to = 0;      // where a turn by `angle` / to `absolute` stops
        float at(double t) const;
        bool done(double t) const;
    };
    struct Motion {
        Axis rot[3];
        venue::Vec3 from{}, to{};  // obj_movetopos: world position from / to
        double m0 = 0, mDur = 0;
        bool moved = false;
        venue::Vec3 pos(double t) const;
        // Scaling: obj_applyscaling / obj_morphscaling, and AddPulseEvent's
        // pulse on hit notes (`pulseFrets`: the colours it listens to).
        float s0 = 1, sTo = 1;
        double sT0 = 0, sDur = 0;
        uint8_t pulseFrets = 0;
        float pulseAmount = 1, pulseTime = 0.1f;
        float scale(double t) const;
    };
    std::vector<std::pair<double, uint8_t>> hits_;
    std::unordered_map<uint32_t, int> visibility_;
    std::function<double(uint32_t, uint32_t, float, double)> animHook_;
    std::unordered_map<uint32_t, double> animEnd_;  // GameObj_WaitAnimFinished
    std::vector<int32_t> fretbars_;
    float pulse(const Motion& m, double ms) const;
    std::unordered_map<uint32_t, Motion> motion_;
    void addObjectNatives();
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
