// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "render/camera.h"
#include "render/scene_gpu.h"

// GH2's particle systems (RndParticleSys v30), run the way the game runs
// them (default.xex; [C] read in the code, [I] inferred):
//
// - Poll 0x822158A8 [C]: nothing for "preserved" systems; a pre-spawning
//   system warms up once (0x822146E8); otherwise move (0x82212A48) then emit
//   (0x82214620) with frame = the animation frame (frame-driven systems,
//   the default) or the task clock in seconds x 30, and dt = frames since
//   the last poll.
// - Emit 0x82214620 [C]: a count accumulates random(emit rate) x dt; each
//   whole unit starts a particle (0x82213E58) until the pool (max
//   particles) is full, which drops the rest.
// - A particle (0x82213E58) [C]: life random(life) frames; position random
//   in the box (pos low..high, per axis) or a point of the emitter mesh;
//   direction (-sin yaw cos pitch, cos yaw cos pitch, sin pitch) x
//   random(speed); colour random per channel between start low and high,
//   end colour likewise; size random(start size), changing by
//   random(delta size) over its life (never below 0); bubbles: a wobble of
//   random(bubble size) in the xz plane with a period of random(bubble
//   period) frames; spin (v30): a start angle and random(spin) turns a
//   minute (random sign). Position, direction and wobble then go through
//   the emission transform: the system's world transform for world-space
//   systems (relative motion 0), identity for systems living in their
//   parent's space (relative motion 1, the parent usually the system).
// - "Fancy" particles (type 1) [C]: size grows from 0 over grow ratio of
//   the life, changes by delta size, then shrinks to 0 from shrink ratio of
//   the life; the colour goes start -> mid colour (at mid colour ratio) ->
//   end; speed x (1 - drag) and spin x (1 - spin damping) every 30 frames.
// - Move 0x82212A48 [C]: a particle dies at its end or when the frame goes
//   back to or before its birth (an effect restarted from frame 0);
//   position += velocity x dt, plus the bubble wobble's derivative; off a
//   bounce plane (the "_bounce" Trans's z axis) the velocity reflects;
//   velocity += force x dt (force in world space).
// - ParticleSysAnim::SetFrame 0x82201190 [C]: start / end colour keys move
//   the low colour to the key and keep the low..high spread; emit rate,
//   speed, life and start size keys set the (low, high) ranges.
// - Drawing (DxParticleSys, particles.fx) [C]: a camera-facing quad per
//   particle, `size` wide, turned by its angle, uv (0,0) top left, in the
//   particle's colour (vertex colour, "prelit"), through the standard
//   material shader with the system's Mat.
namespace gfx {

class MaterialRenderer;

class ParticleSystem {
public:
    explicit ParticleSystem(const scene::ParticleSys& def, uint32_t seed = 0x9E3779B9u);
    const scene::ParticleSys& def() const { return *def_; }

    // The ranges a ParticleSysAnim changes, per running copy.
    struct Params {
        std::array<float, 2> life, speed, emitRate, startSize;
        std::array<float, 4> startColorLow, startColorHigh, endColorLow, endColorHigh;
    };
    Params& params() { return params_; }

    // Kills every particle and restores the definition's ranges.
    void reset();
    // RndAnimatable::SetFrame: the frame a frame-driven system runs on.
    void setFrame(float frame) { frame_ = frame; }
    // ParticleSysAnim::SetFrame on this copy.
    void applyAnim(const scene::ParticleSysAnim& anim, float frame);
    // Where the system is this frame: its world transform and the world
    // transform of its relative-motion parent (the same for most).
    void setTransform(const scene::Xfm& world, const scene::Xfm& parentWorld);
    // The bounce plane, in world space (normal, offset: dot(p, n) + d < 0
    // is behind it); `on` false removes it.
    void setBouncePlane(bool on, Vec3 normal = {}, float d = 0);
    // A mesh emitter: particles start on its faces (world transform of the
    // mesh), moving along the face normal [I: 0x821C6000 not traced].
    void setEmitterMesh(const scene::Mesh* mesh, const scene::Xfm& meshWorld);
    // Poll: `clockFrames` is the task clock (seconds x 30), used by systems
    // that are not frame-driven.
    void poll(float clockFrames);
    // Stops or resumes emitting (an effect being hidden) [I].
    void setEmitting(bool on) { emitting_ = on; }

    int count() const { return int(alive_.size()); }
    // Removes the particles whose position (in the particles' space) matches.
    void removeIf(const std::function<bool(const Vec3&)>& pred);
    // Particles are in world space (false), or in the draw space below.
    bool localSpace() const { return local_; }
    const scene::Xfm& drawSpace() const { return drawSpace_; }

    // Appends this system's quads (16 floats per vertex: position, normal,
    // colour, uv, tangent; 4 vertices and 6 indices a particle) facing a
    // camera whose right and up axes (world) are given.
    void appendQuads(Vec3 camRight, Vec3 camUp, std::vector<float>& verts, std::vector<uint16_t>& indices) const;

private:
    struct Particle {
        float color[4], colorVel[4];
        Vec3 pos, vel;
        Vec3 bubbleDir;
        float bubbleFreq = 0, bubblePhase = 0;
        float death = 0, birth = 0;
        float size = 0, sizeVel = 0;
        float angle = 0, spinVel = 0;
        float growEnd = 0, growVel = 0, shrinkStart = 0, shrinkVel = 0;
        float midVel[4] = {0, 0, 0, 0};
        float midFrame = 0;
    };

    float random();                  // 0..1
    float random(float lo, float hi) { return lo + (hi - lo) * random(); }
    float random(const std::array<float, 2>& r) { return random(r[0], r[1]); }
    void init(Particle& p, float frame);
    void move(float frame, float dt);
    void emit(float frame, float dt);
    void warmUp(float now);

    const scene::ParticleSys* def_;
    Params params_;
    uint32_t rng_;
    std::vector<Particle> pool_;
    std::vector<uint32_t> alive_;    // pool indices, birth order
    std::vector<uint32_t> free_;
    float emitCount_ = 0;
    float frame_ = 0, lastFrame_ = 0, lastClock_ = -1;
    bool started_ = false, warmUpPending_ = false, emitting_ = true;
    bool local_ = false;
    scene::Xfm emission_, drawSpace_;
    Vec3 forceRows_[3];              // world force -> particle space (rows)
    bool bounce_ = false;
    Vec3 bounceN_;
    float bounceD_ = 0;
    const scene::Mesh* emitterMesh_ = nullptr;
    scene::Xfm emitterWorld_;
};

// Draws particle systems: their quads go into a buffer refilled every draw
// (GH2 refills its particle vertex buffer every frame too).
class ParticleRenderer {
public:
    explicit ParticleRenderer(RenderDevice& device) : mesh_(device) {}
    // Draws `sys` with its Mat (resolved in its directory; `matOverride`
    // replaces it) seen from `camera`; `offset` is applied after the
    // particles' own space (e.g. drawing a track at rest while the game's
    // rig moves). The frame (camera, lights) must be set on `mr` already.
    void draw(MaterialRenderer& mr, SceneCache& cache, const ParticleSystem& sys, const Camera& camera,
              const Mat4& offset = Mat4::identity(), const ScenePose* pose = nullptr);
    int drawnParticles() const { return drawn_; }
    void resetStats() { drawn_ = 0; }

private:
    DynamicMesh mesh_;
    std::vector<float> verts_;
    std::vector<uint16_t> indices_;
    int drawn_ = 0;
};

// The particle systems of a view (a Group and its sub-groups) with the
// ParticleSysAnims that drive them, as one running copy: GH2 copies effect
// views per use (one per slot for the smash effects). SetFrame on the view
// through a ScenePose (ScenePose::setFrame) records the frames; sync()
// applies them to this copy.
class ParticleSet {
public:
    ParticleSet() = default;
    explicit ParticleSet(const scene::Object* root, uint32_t seed = 1) { build(root, seed); }
    // Collects the ParticleSys and ParticleSysAnim objects reachable from
    // `root` (a Group's members, recursively; or the system itself).
    void build(const scene::Object* root, uint32_t seed = 1);
    bool empty() const { return systems_.empty(); }
    void reset();
    // Applies the frames the pose recorded for the anims and systems.
    void sync(const ScenePose& pose);
    // Sets every system's frame (a frame-driven copy that is not reached by
    // a view's SetFrame).
    void setFrame(float frame);
    // Places the systems (their world under `base`, from the pose) and polls.
    void update(const ScenePose& pose, const scene::Xfm& base, float clockFrames);
    void setEmitting(bool on);
    void draw(ParticleRenderer& pr, MaterialRenderer& mr, SceneCache& cache, const Camera& camera,
              const Mat4& offset = Mat4::identity(), const ScenePose* pose = nullptr);
    int count() const;
    std::vector<ParticleSystem>& systems() { return systems_; }

private:
    std::vector<ParticleSystem> systems_;
    std::vector<const scene::ParticleSysAnim*> anims_;
};

}  // namespace gfx
