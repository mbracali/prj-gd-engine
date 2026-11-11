// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <random>
#include <vector>

#include "data/qb.h"

class Canvas;
class TextureCache;

// GH3's 2D particle systems (Create2DParticleSystem 0x8210EEB8 /
// Destroy2DParticleSystem 0x8211AA28): the song scripts' sparks, stars and
// flares on the highway (first_gem_fx, GuitarEvent_StarSequenceBonus's big
// bolts, WhammyFXOn's sustain sparks). A system emits sprites of its
// material from `pos` (relative to its parent screen element) every
// emit_rate seconds, each living `time` seconds while its colour, scale
// and rotation run from start to end values [C data: the scripts' params;
// the motion's units I, as render/highway2d's hit sparks: velocity x 60
// px/s, friction x 60 px/s^2, emit_dir / emit_spread in degrees from up].
// Stepped on the game's 60 Hz frames; drawn over the HUD (z 8).
class Fx2D {
public:
    // Where a parent element is now (screen units), false if it is gone.
    using ParentPos = std::function<bool(uint32_t parent, float& x, float& y)>;
    void setParentPos(ParentPos f) { parentPos_ = std::move(f); }
    // Create2DParticleSystem {id pos parent material ... }.
    void create(const qb::Struct& params, double nowMs);
    // Destroy2DParticleSystem id [kill_when_empty]; id = all: every system.
    void destroy(uint32_t id, bool whenEmpty);
    bool exists(uint32_t id) const;
    // Emits and ages up to `nowMs` (call once a game frame).
    void step(double nowMs);
    void draw(Canvas& c, const TextureCache& tex, double nowMs) const;
    void clear() { systems_.clear(); }
    size_t live() const { return systems_.size(); }
    uint64_t created = 0, particles = 0;

private:
    struct Particle {
        double born = 0;
        float x = 0, y = 0, vx = 0, vy = 0, angle = 0, spin = 0;
    };
    struct System {
        uint32_t id = 0, parent = 0, material = 0;
        float x = 0, y = 0, z = 8;
        std::array<float, 4> c0{255, 255, 255, 255}, c1{255, 255, 255, 0};
        float s0x = 1, s0y = 1, s1x = 1, s1y = 1;
        float angleSpread = 0, rotMin = 0, rotMax = 0, radius0 = 0, radius1 = 0;
        float rate = 0.02f, dir = 0, spread = 0, velocity = 0, fx = 0, fy = 0, life = 0.25f;
        bool emitting = true, killWhenEmpty = false;
        double nextEmit = 0;
        float px = 0, py = 0;  // the parent's position at the last step
        std::vector<Particle> live;
    };
    std::vector<System> systems_;
    ParentPos parentPos_;
    std::mt19937 rng_{0x47483332};
};
