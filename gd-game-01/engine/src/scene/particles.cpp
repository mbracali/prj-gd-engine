// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/particles.h"

#include <algorithm>
#include <cmath>

namespace {

using V3 = std::array<float, 3>;
constexpr float kNoFrame = -9999999.0f;
constexpr float kTwoPi = 6.2831853f;

// FUN_0024a0c8: a + u * (b - a), u from a 16-bit LFSR.
float rnd(ParticleSysObj& p, float a, float b) {
    uint16_t x = p.lfsr;
    for (int i = 0; i < 16; ++i) {
        uint16_t bit = ((x >> 0) ^ (x >> 2) ^ (x >> 3) ^ (x >> 5)) & 1u;
        x = uint16_t((x >> 1) | (bit << 15));
    }
    p.lfsr = x ? x : 0xace1u;
    return a + float(x) / 65536.0f * (b - a);
}

// Row-vector transforms (Xfm rows are the basis vectors).
V3 dirOf(const Xfm& m, const V3& v) {
    return {v[0] * m.rot[0] + v[1] * m.rot[3] + v[2] * m.rot[6], v[0] * m.rot[1] + v[1] * m.rot[4] + v[2] * m.rot[7],
            v[0] * m.rot[2] + v[1] * m.rot[5] + v[2] * m.rot[8]};
}
V3 pointOf(const Xfm& m, const V3& v) {
    V3 d = dirOf(m, v);
    return {d[0] + m.pos[0], d[1] + m.pos[1], d[2] + m.pos[2]};
}

// FUN_001d0a60.
void update(ParticleSysObj& p, float dt, float now, const Xfm& space) {
    // vel += P3x3 · (force · dt), P's rows dotted with the force.
    V3 f{p.force[0] * dt, p.force[1] * dt, p.force[2] * dt};
    V3 df{space.rot[0] * f[0] + space.rot[1] * f[1] + space.rot[2] * f[2],
          space.rot[3] * f[0] + space.rot[4] * f[1] + space.rot[5] * f[2],
          space.rot[6] * f[0] + space.rot[7] * f[1] + space.rot[8] * f[2]};
    std::erase_if(p.live, [&](const ParticleSysObj::Particle& q) { return now >= q.death || now <= q.birth; });
    for (auto& q : p.live) {
        for (int k = 0; k < 3; ++k) q.pos[size_t(k)] += q.vel[size_t(k)] * dt;
        if (p.bubble) {
            float s = std::sin(now * q.bubbleFreq + q.bubblePhase + kTwoPi / 4) * q.bubbleFreq * dt;
            for (int k = 0; k < 3; ++k) q.pos[size_t(k)] += q.bubbleAxis[size_t(k)] * s;
        }
        if (p.collide) {
            const auto& n = p.plane;
            float side = n[0] * q.pos[0] + n[1] * q.pos[1] + n[2] * q.pos[2] + n[3];
            float into = q.vel[0] * n[0] + q.vel[1] * n[1] + q.vel[2] * n[2];
            if (side < 0 && into < 0)
                for (int k = 0; k < 3; ++k) q.vel[size_t(k)] -= 2 * into * n[size_t(k)];
        }
        for (int k = 0; k < 3; ++k) q.vel[size_t(k)] += df[size_t(k)];
        if (p.type == 1) {
            const auto& slope = now < q.midTime ? q.midSlope : q.dColor;
            for (int k = 0; k < 4; ++k) q.color[size_t(k)] = std::clamp(q.color[size_t(k)] + slope[size_t(k)] * dt, 0.0f, 1.0f);
            q.size += (now < q.growEnd ? q.growRate : now < q.shrinkStart ? q.dSize : q.shrinkRate) * dt;
        } else {
            for (int k = 0; k < 4; ++k) q.color[size_t(k)] += q.dColor[size_t(k)] * dt;
            q.size += q.dSize * dt;
        }
    }
}

// FUN_001d0060 (+ FUN_001d0990).
void emit(ParticleSysObj& p, float dt, float now, const Xfm& spawn) {
    p.accum += rnd(p, p.emitRate[0], p.emitRate[1]) * dt;
    while (p.accum >= 1) {
        if (int(p.live.size()) >= p.maxParticles) {
            p.accum = 0;
            break;
        }
        ParticleSysObj::Particle q{};
        q.birth = now;
        q.death = now + rnd(p, p.life[0], p.life[1]);
        V3 pos{rnd(p, p.posLow[0], p.posHigh[0]), rnd(p, p.posLow[1], p.posHigh[1]), rnd(p, p.posLow[2], p.posHigh[2])};
        float pitch = rnd(p, p.pitch[0], p.pitch[1]), yaw = rnd(p, p.yaw[0], p.yaw[1]);
        V3 dir{-std::cos(pitch) * std::sin(yaw), std::cos(pitch) * std::cos(yaw), std::sin(pitch)};
        float speed = rnd(p, p.speed[0], p.speed[1]);
        q.vel = dirOf(spawn, {dir[0] * speed, dir[1] * speed, dir[2] * speed});
        q.pos = pointOf(spawn, pos);
        if (p.bubble) {
            float period = rnd(p, p.bubblePeriod[0], p.bubblePeriod[1]);
            q.bubbleFreq = period != 0 ? kTwoPi / period : 0;
            q.bubblePhase = rnd(p, 0, kTwoPi);
            float a = rnd(p, 0, kTwoPi), r = rnd(p, p.bubbleSize[0], p.bubbleSize[1]);
            q.bubbleAxis = dirOf(spawn, {std::sin(a) * r, 0, std::cos(a) * r});
            for (int k = 0; k < 3; ++k) q.pos[size_t(k)] += q.bubbleAxis[size_t(k)] * std::sin(q.bubblePhase);
            q.bubblePhase -= now * q.bubbleFreq;
        }
        for (int k = 0; k < 4; ++k) q.color[size_t(k)] = rnd(p, p.startLow[size_t(k)], p.startHigh[size_t(k)]);
        q.size = rnd(p, p.startSize[0], p.startSize[1]);
        q.dSize = std::max(rnd(p, p.endSize[0], p.endSize[1]), -q.size);
        std::array<float, 4> end;
        for (int k = 0; k < 4; ++k) end[size_t(k)] = rnd(p, p.endLow[size_t(k)], p.endHigh[size_t(k)]);
        float life = q.death - q.birth, inv = life != 0 ? 1.0f / life : 0.0f;
        if (p.type == 1) {
            q.growEnd = p.growRatio != 0 ? q.birth + life * p.growRatio : 0;
            if (p.growRatio != 0) {
                q.growRate = q.growEnd != q.birth ? q.size / (q.growEnd - q.birth) : 0;
                q.size = 0;
            }
            q.shrinkStart = p.shrinkRatio == 1 ? 1 : q.birth + life * p.shrinkRatio;
            float target = p.growRatio != 0 ? q.growRate * (q.growEnd - q.birth) : q.size;
            q.shrinkRate = q.shrinkStart != q.death ? (target + q.dSize) / (q.shrinkStart - q.death) : 0;
            q.midTime = q.birth + life * p.midRatio;
            std::array<float, 4> mid;
            for (int k = 0; k < 4; ++k) mid[size_t(k)] = rnd(p, p.midLow[size_t(k)], p.midHigh[size_t(k)]);
            float toEnd = q.death - q.midTime, toMid = q.midTime - q.birth;
            for (int k = 0; k < 4; ++k) {
                q.dColor[size_t(k)] = toEnd != 0 ? (end[size_t(k)] - mid[size_t(k)]) / toEnd : 0;
                q.midSlope[size_t(k)] = toMid != 0 ? (mid[size_t(k)] - q.color[size_t(k)]) / toMid : 0;
            }
        } else {
            for (int k = 0; k < 4; ++k) q.dColor[size_t(k)] = (end[size_t(k)] - q.color[size_t(k)]) * inv;
        }
        q.dSize *= inv;
        p.live.push_back(q);
        p.accum -= 1;
    }
}

}  // namespace

namespace {
bool gParticlesEnabled = true;
}  // namespace

void setParticlesEnabled(bool on) { gParticlesEnabled = on; }
bool particlesEnabled() { return gParticlesEnabled; }

void particleSetFrame(ParticleSysObj& p, float frame, const Xfm& emitter) {
    if (!gParticlesEnabled) {
        p.live.clear();
        p.last = frame;
        return;
    }
    // relative_motion 1: particles live in the emitter's space (P = its world,
    // spawned with identity); 0: spawned into world space through the emitter.
    bool relative = p.relativeMotion >= 1;
    Xfm identity;
    if (p.last != kNoFrame) {
        float dt = frame - p.last;
        if (dt != 0) update(p, dt, frame, relative ? emitter : identity);
        if (dt > 0) emit(p, dt, frame, relative ? identity : emitter);
    }
    p.last = frame;
}
