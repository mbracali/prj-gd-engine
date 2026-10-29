// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/fx2d.h"

#include <algorithm>
#include <cmath>

#include "data/qbkey.h"
#include "render/canvas.h"
#include "render/texture_cache.h"

namespace {
uint32_t K(const char* s) { return qbkey::key(s); }
constexpr float kPi = 3.14159265f;

const qb::Value* find(const qb::Struct& p, const char* name) { return p.find(K(name)); }
float num(const qb::Struct& p, const char* name, float fallback) {
    const qb::Value* v = find(p, name);
    return v && (v->type == qb::Type::Int || v->type == qb::Type::Float) ? v->number() : fallback;
}
void pair(const qb::Struct& p, const char* name, float& x, float& y) {
    const qb::Value* v = find(p, name);
    if (!v) return;
    if (v->type == qb::Type::Pair || v->type == qb::Type::Vector) x = v->f[0], y = v->f[1];
    else if (v->type == qb::Type::Int || v->type == qb::Type::Float) x = y = v->number();
}
void rgba(const qb::Struct& p, const char* name, std::array<float, 4>& out) {
    const qb::Value* v = find(p, name);
    if (!v || v->type != qb::Type::Array || !v->arr) return;
    for (size_t i = 0; i < 4 && i < v->arr->values.size(); ++i) out[i] = v->arr->values[i].number();
}
uint32_t keyOf(const qb::Value* v) {
    if (!v) return 0;
    if (v->type == qb::Type::String || v->type == qb::Type::WString) return qbkey::key(v->str);
    return v->key;
}
}  // namespace

void Fx2D::create(const qb::Struct& p, double now) {
    System s;
    s.id = keyOf(find(p, "id"));
    if (s.id) destroy(s.id, false);  // a system of the same id is replaced [I]
    s.parent = keyOf(find(p, "parent"));
    s.material = keyOf(find(p, "material"));
    pair(p, "pos", s.x, s.y);
    s.z = num(p, "z_priority", 8);
    rgba(p, "start_color", s.c0);
    rgba(p, "end_color", s.c1);
    pair(p, "start_scale", s.s0x, s.s0y);
    pair(p, "end_scale", s.s1x, s.s1y);
    s.angleSpread = num(p, "start_angle_spread", 0);
    s.rotMin = num(p, "min_rotation", 0), s.rotMax = num(p, "max_rotation", 0);
    s.radius0 = num(p, "emit_start_radius", 0), s.radius1 = num(p, "emit_radius", 0);
    s.rate = std::max(0.001f, num(p, "emit_rate", 0.02f));
    s.dir = num(p, "emit_dir", 0), s.spread = num(p, "emit_spread", 0);
    s.velocity = num(p, "velocity", 0);
    pair(p, "friction", s.fx, s.fy);
    s.life = std::max(0.001f, num(p, "time", 0.25f));
    s.nextEmit = now;
    if (parentPos_ && s.parent) parentPos_(s.parent, s.px, s.py);
    systems_.push_back(std::move(s));
    ++created;
}

void Fx2D::destroy(uint32_t id, bool whenEmpty) {
    for (auto it = systems_.begin(); it != systems_.end();) {
        if (id == K("all") || it->id == id) {
            if (whenEmpty) {
                it->emitting = false, it->killWhenEmpty = true;
            } else {
                it = systems_.erase(it);
                continue;
            }
        }
        ++it;
    }
}

bool Fx2D::exists(uint32_t id) const {
    for (const System& s : systems_)
        if (s.id == id) return true;
    return false;
}

void Fx2D::step(double now) {
    std::uniform_real_distribution<float> u01(0, 1);
    for (auto it = systems_.begin(); it != systems_.end();) {
        System& s = *it;
        // A system on an element that is gone goes with it [I].
        if (s.parent && parentPos_ && !parentPos_(s.parent, s.px, s.py)) {
            it = systems_.erase(it);
            continue;
        }
        while (s.emitting && s.nextEmit <= now) {
            Particle q;
            q.born = s.nextEmit;
            const float a = (s.dir + (u01(rng_) - 0.5f) * s.spread) * kPi / 180;
            const float r = s.radius0 + (s.radius1 - s.radius0) * u01(rng_);
            const float ra = u01(rng_) * 2 * kPi;
            q.x = s.x + r * std::cos(ra), q.y = s.y + r * std::sin(ra);
            q.vx = s.velocity * 60 * std::sin(a), q.vy = -s.velocity * 60 * std::cos(a);
            q.angle = (u01(rng_) - 0.5f) * s.angleSpread;
            q.spin = s.rotMin + (s.rotMax - s.rotMin) * u01(rng_);
            s.live.push_back(q);
            ++particles;
            s.nextEmit += s.rate * 1000.0;
        }
        const double life = s.life * 1000.0;
        s.live.erase(std::remove_if(s.live.begin(), s.live.end(), [&](const Particle& q) { return now - q.born >= life; }), s.live.end());
        if (s.killWhenEmpty && s.live.empty()) {
            it = systems_.erase(it);
            continue;
        }
        ++it;
    }
}

void Fx2D::draw(Canvas& c, const TextureCache& tex, double now) const {
    for (const System& s : systems_) {
        uint32_t key = tex.materialTexture(s.material);
        if (!key) key = s.material;
        const gfx::TextureId id = tex.find(key);
        if (!id.id) continue;
        int tw = 32, th = 32;
        tex.size(key, tw, th);
        for (const Particle& q : s.live) {
            const float t = float((now - q.born) / 1000.0);
            if (t < 0 || t >= s.life) continue;
            const float f = t / s.life;
            // friction's y pulls the particle down (x 60 px/s^2); its x is
            // left out (0 in every script but one) [I: render/highway2d's sparks].
            const float x = s.px + q.x + q.vx * t;
            const float y = s.py + q.y + q.vy * t + 0.5f * s.fy * 60 * t * t;
            const float sx = (s.s0x + (s.s1x - s.s0x) * f) * float(tw) * 0.5f, sy = (s.s0y + (s.s1y - s.s0y) * f) * float(th) * 0.5f;
            const float rot = (q.angle + q.spin * t) * kPi / 180, cr = std::cos(rot), sr = std::sin(rot);
            uint8_t col[4];
            for (int k = 0; k < 4; ++k) col[k] = uint8_t(std::clamp(s.c0[size_t(k)] + (s.c1[size_t(k)] - s.c0[size_t(k)]) * f, 0.0f, 255.0f));
            auto at = [&](float ox, float oy, float u, float v) {
                return Vertex{x + ox * cr - oy * sr, y + ox * sr + oy * cr, 0, u, v, col[0], col[1], col[2], col[3]};
            };
            const Vertex A = at(-sx, -sy, 0, 0), B = at(sx, -sy, 1, 0), C = at(sx, sy, 1, 1), D = at(-sx, sy, 0, 1);
            const Vertex v[6] = {A, B, C, A, C, D};
            c.draw(v, 6, int(id.id), Blend::Add);
        }
    }
}
