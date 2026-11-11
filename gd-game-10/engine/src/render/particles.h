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
class LightShow;
}

// GH3's venue particles and pyro (docs/venues.md "Particles"): the zone's
// scripts create particle systems (CreateParticleSystem_fast with a
// GP_* parameter struct of scripts\fx\particles: emitter box, direction and
// spread, speed, force, life, sizes, colours, the quick material's texture
// and blend), from the light show's pyro notes (note 56 ->
// <zone>_lightshow_mapping.pyro_script with the performance) and the
// performance changes (<zone>_lightshow_mapping.transitions.performance:
// e.g. z_dive's sparks while the crowd is happy). The zone's scripts run on
// gd10's QScript VM stepped on the song clock at 60 Hz; each particle is a
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
    // ScreenFlash's FlashBS on bg_viewport (z_prison's flares): the bright_sat
    // brightness / contrast at `ms`; false when the instance is not up.
    bool flash(double ms, float& brightness, float& contrast) const;
    // obj_locktoobject {objectname offset} on a node (z_prison's caged dancer
    // on the moving cage): where it is at `ms` (its placed position moved by
    // the target's motion); false when not locked.
    bool lockedPos(uint32_t node, double ms, venue::Vec3& pos) const;
    // The venue's light show (lightvolume_getparams on a light housing).
    void setLightShow(const venue::LightShow* show) { show_ = show; }
    int drumKicks = 0, leftKicks = 0, scalePulses = 0, flashes = 0;  // drumkick events launched, obj_applyscaling calls (logged)
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
        float spread = 0;  // anglespread (or elevspread's range), degrees
        float lifeMin = 1, lifeMax = 1, rate = 0, lifetime = 0, delay = 0;
        int burst = 0;
        // sizerange [(start width, start height), (end width, end height)],
        // linear over the life, the same for every particle [C 822E37C8, 822E2390, 82427360].
        float w0 = 0.1f, h0 = 0.1f, w1 = 0.1f, h1 = 0.1f;
        float speedMin = 1, speedMax = 1;
        bool alignWithPath = false;
        float pathTime = 0;                   // pathfollowtime (system +0x7f0) [C]
        float rotMin = 0, rotMax = 0;         // rotvel (turns a second) [C 8233B120]
        float rotScale = 0;                   // rotveltimescale [C 823EF088]
        // bounceheight / bouncecoeff: the particles rebound off the plane y = height (world)
        // keeping coeff of their speed; off when the coeff is 0 or the height -99.9 [C data; I: the model].
        bool bounce = false;
        float bounceHeight = 0, bounceCoeff = 0;
        uint32_t color = 0xFFFFFFFF;          // RGBA
        // particlecolor / vertexweight: colour keys, each a pair (a, b) the
        // particle's random picks between; key 0 at life 0, key k at
        // vertexweight[k - 1] [C 821CCCD8, 822E37C8, 823EDEC0].
        struct Key {
            float t = 0;
            float a[4]{}, b[4]{};
        };
        std::vector<Key> keys;
        uint32_t texture = 0;                 // quickmaterial.diffusetexture
        bool additive = true;
        int blend = 1;          // blendmode: 0 diffuse, 1 add, 2 subtract, 3 blend, 4 modulate, 5 brighten [C 823B7768]
        float burn = 0;         // `burn`: burnvalue (premultiplied blend, the burn shader) [C]
        bool lit = false;       // the quick material's `lighting`: lit by the venue's lights (gh4) [C data]
        int layout = 0;         // texturelayout: 0 1x1, 1 2x2, 2 1x2, 3 2x1
        float animFps = 0;      // quadanimationfps
        float alphaCut = 0;
        // Type textureribbontrail (the Star Power swirls): a ribbon through
        // the particle's last `history` frames (82427E08) [C].
        bool ribbon = false;
        int history = 0;
    };

private:
    struct System {
        Params p;
        venue::Vec3 pos{};
        uint32_t name = 0, group = 0;     // CreateParticleSystem's name / groupid (the song scripts')
        uint32_t member = 0, bone = 0;    // ObjID / bone: follows that band member's bone
        double start = 0;
        uint64_t seed = 1;
        double end() const;
    };
    // A system; rate >= 0: a node's particleobject emitting `rate` a second
    // with no end and no burst (emitrate), else the params' own.
    void create(const venue::Vec3& pos, const qb::Value* params, double t, float rate = -1);
public:
    // The song scripts' CreateParticleSystem (the Star Power stage FX, the
    // finger flames): `member` / `bone` non-zero, the system follows that
    // band member's bone (bonePos) [C scripts; I: the emitter carries the
    // particles already out with it]. Returns false without such params.
    bool createScripted(uint32_t name, uint32_t group, const qb::Value* params, const venue::Vec3& pos, uint32_t member, uint32_t bone, double t);
    // DestroyParticlesByGroupID / a name's system: gone at once.
    void destroyNamed(uint32_t name);
    // <name> : emitrate rate = 0 (no new particles; the ones out live on).
    void stopEmitting(uint32_t name, double t);
    bool hasNamed(uint32_t name) const;
    void destroyGroup(uint32_t group);
    std::function<bool(uint32_t member, uint32_t bone, venue::Vec3& out)> bonePos;
    // The light reaching a point (rgb, 1 = unlit white), for `lighting` materials.
    std::function<void(const venue::Vec3& at, float rgb[3])> lightAt;
    // EngineWind (the Star Power hearts / bats / butterflies / peace swirl):
    // a twist about the wind's vertical axis plus its velocity, on the
    // particles within `range` (inv_distance attenuation) [C scripts; I: the
    // field's model]. Create replaces, modify moves it, remove ends it.
    struct Wind {
        venue::Vec3 pos{}, velocity{};
        float speed = 0, tangentWeight = 0, range = 0;
    };
    void setWind(uint32_t name, const Wind& w) { winds_[name] = w; }
    Wind* wind(uint32_t name) {
        auto it = winds_.find(name);
        return it == winds_.end() ? nullptr : &it->second;
    }
    void removeWind(uint32_t name) { winds_.erase(name); }
    // CreateTeslaEffectObject / SetTeslaEffectObject (the default Star Power:
    // bolts from the guitar to the zone's TeslaNodes), drawn by VenueStage.
    struct Tesla {
        venue::Vec3 start{}, end{};
        float width = 0.1f;
    };
    std::unordered_map<uint32_t, Tesla> teslas;
    uint64_t scriptedCreated = 0;
    uint64_t ribbonSegments = 0;  // textureribbontrail quads drawn (all frames)
    uint32_t shadowLight = 0;  // SetShadowDirFromLight's light (0: none)
    // The camera director's script shots (CameraDirector::startScript): the
    // zone VM runs the control script; its ccam_domorph / ccam_waitmorph
    // reach the director through these.
    std::function<void(const qb::Struct& params, double ms)> camMorph;
    std::function<bool(double ms)> camMorphDone;
    uint32_t spawnCameraScript(uint32_t script);
    void killRun(uint32_t run);
private:
    std::unordered_map<uint32_t, Wind> winds_;
public:
private:
    // The particleobject nodes (z_party's GFX fires / smoke / sparks): the
    // system emitting now (index into systems_ by id), its params, position.
    struct NodeEmitter {
        venue::Vec3 pos{};
        qb::Value params;
        uint64_t system = 0;  // the running system's seed (0: none)
        float rate = 0;
        float ownRate = 0;    // its params' emit_rate (createdatstart / Create)
    };
    std::unordered_map<uint32_t, NodeEmitter> nodeEmitters_;
    struct Lock {
        uint32_t target = 0;
        venue::Vec3 offset{};
    };
    std::unordered_map<uint32_t, Lock> locks_;
    const venue::LightShow* show_ = nullptr;
    struct Flash {
        bool on = false;
        float b0 = 1, c0 = 1, b1 = 1, c1 = 1;  // from / to
        double t0 = 0, dur = 0;
    } flash_;
    void setEmitRate(uint32_t node, float rate, double t);
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
    // The drummer's kicks: (time, drumkick_left / _right) (Drum_iterator_CFunc).
    std::vector<std::pair<double, uint32_t>> kicks_;
    size_t kickAt_ = 0;
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
