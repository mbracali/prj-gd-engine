// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_stage.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <cmath>
#include <cstdlib>
#include <exception>

#include "data/qbkey.h"
#include "render/gh3_shader.h"
#include "game/chart.h"
#include "script/globals.h"

VenueStage::~VenueStage() {
    if (device_ && shadow_) device_->destroyTarget(shadow_);
}

std::string VenueStage::chooseZone(const Globals& g) {
    if (const char* env = SDL_getenv("GD04_VENUE")) return env;
    if (const qb::Value* lvl = g.find(qbkey::key("current_level")); lvl && (lvl->type == qb::Type::Key || lvl->type == qb::Type::KeyPtr)) {
        const std::string z = venue::zoneOfLevel(g, lvl->key);
        if (!z.empty()) return z;
    }
    const std::vector<std::string> all = venue::venueZones(g);
    return all.empty() ? std::string() : all.front();
}

venue::Performance VenueStage::performance(float health) {
    // get_skill_level: health < 0.66 bad, > 1.33 good [C script; I: the crowd's
    // performance follows the same bands].
    return health < 0.66f ? venue::Performance::Poor : health > 1.33f ? venue::Performance::Good : venue::Performance::Medium;
}

bool VenueStage::start(const Disc& disc, const Globals& g, const std::string& zone, const Chart& chart, uint32_t songKey, double leadInMs,
                       gfx::RenderDevice* device, std::string& error) {
    try {
        venue_ = venue::load(disc, zone);
    } catch (const std::exception& e) {
        error = zone + ": " + e.what();
        return false;
    }
    perf_ = {};
    cameras_ = {};
    lights_ = {};
    cameras_.init(*venue_, g, chart, songKey ^ venue_->zoneKey, leadInMs);
    lights_.init(*venue_, g, chart);
    // The band (create_band, at the song's start: the gem scroller's lead-in) [C script].
    band_.reset();
    if (!SDL_getenv("GD04_NO_BAND")) {
        band_ = std::make_unique<Band>();
        std::string berr;
        const uint64_t t0 = SDL_GetTicks();
        if (!band_->start(disc, g, *venue_, chart, songKey, &perf_, -leadInMs, berr)) {
            SDL_Log("band: %s", berr.c_str());
            band_.reset();
        } else {
            std::string who;
            for (const Band::Entry& e : band_->members()) who += (who.empty() ? "" : ", ") + e.model->member.profileName;
            SDL_Log("band: %s (%llu ms)", who.c_str(), static_cast<unsigned long long>(SDL_GetTicks() - t0));
        }
    }
    if (band_) {
        Band* b = band_.get();
        cameras_.setBoneLookup([b](uint32_t member, uint32_t bone, double ms, venue::Vec3& out) {
            b->evaluate(ms);
            return b->bonePos(member, bone, out);
        });
    }
    // The crowd (create_crowd_models) and its cards: textures the crowd
    // cameras' renders replace (<zone>_crowd_models[].textureasset,
    // CreateViewportTextureOverride) [C script]; hidden without a crowd.
    crowd_.reset();
    if (!SDL_getenv("GD04_NO_CROWD")) {
        crowd_ = std::make_unique<Crowd>();
        std::string cerr;
        if (!crowd_->start(disc, g, *venue_, chart, &perf_, band_ ? &band_->library() : nullptr, -leadInMs, cerr)) {
            if (!cerr.empty()) SDL_Log("crowd: %s", cerr.c_str());
            crowd_.reset();
        }
    }
    // The zone's particle systems (pyro notes, performance changes).
    particles_.reset();
    if (!SDL_getenv("GD04_NO_PARTICLES")) {
        particles_ = std::make_unique<Particles>();
        std::string perr;
        if (!particles_->start(disc, g, *venue_, chart, &perf_, -leadInMs, perr)) {
            if (!perr.empty()) SDL_Log("particles: %s", perr.c_str());
            particles_.reset();
        }
    }
    hide_.clear();
    const qb::Value* cm = g.find(qbkey::key(zone + "_crowd_models"));
    if (cm && cm->type == qb::Type::Array && cm->arr)
        for (const qb::Value& e : cm->arr->values)
            if (e.type == qb::Type::Struct && e.st)
                if (const qb::Value* t = e.st->find(qbkey::key("textureasset"))) hide_.push_back(t->key);
    view_.reset();
    if (device && !attach(*device, disc, error)) return false;
    return true;
}

bool VenueStage::attach(gfx::RenderDevice& device, const Disc& disc, std::string& error) {
    if (view_ || !venue_) return bool(view_);
    view_ = std::make_unique<VenueView>();
    if (!view_->init(device, disc, *venue_, hide_, error)) {
        view_.reset();
        return false;
    }
    if (band_) {
        std::string berr;
        if (!band_->attach(device, berr)) SDL_Log("band: %s", berr.c_str());
    }
    // The dynamic shadow map: shadow_map_size (1024, 512) of
    // Default_TOD_Manager's one_camera mode, the shadow_big style 1024 x 1024
    // depth only [C data tod_profiles.qb; I: one map for both cascades].
    if (device_ && shadow_) device_->destroyTarget(shadow_);
    shadow_ = {};
    device_ = &device;
    if (!SDL_getenv("GD04_NO_SHADOWS")) {
        std::string serr;
        gfx::TargetDesc td;
        td.width = td.height = 1024;
        td.depth = true;
        shadow_ = device.createTarget(td, serr);
        if (!shadow_) SDL_Log("venue: no shadow map: %s", serr.c_str());
    }
    if (particles_) {
        std::string perr;
        if (!particles_->attach(device, disc, perr)) {
            SDL_Log("particles: %s", perr.c_str());
            particles_.reset();
        }
    }
    if (crowd_) {
        std::string cerr;
        if (!crowd_->attach(device, gh3shader::library(disc), cerr)) {
            SDL_Log("crowd: %s", cerr.c_str());
            crowd_.reset();
        }
    }
    return true;
}

void VenueStage::update(double ms, float health) {
    if (!venue_) return;
    perf_.record(ms, performance(health));
    advance(ms);
}

void VenueStage::advance(double ms) {
    if (!venue_) return;
    if (band_) band_->advance(ms);
    if (crowd_) crowd_->advance(ms);
    if (particles_) particles_->advance(ms);
    cameras_.update(ms, perf_);
    lights_.update(ms, perf_);
}

void VenueStage::shadowPass(double ms) {
    view_->setShadow({}, gfx::Mat4{}, 1024, 0.35f);
    if (!shadow_ || !band_ || !device_) return;
    // The shadow light: the strongest of the band group's directional lights
    // pointing down [I: GH3's SetShadowDirFromLight is not called by the
    // zones' scripts; the C code's choice is not known].
    const venue::Venue& v = *venue_;
    int best = -1;
    float bestPower = 0;
    for (size_t i = 0; i < v.lights.size() && i < lightState_.size(); ++i) {
        const venue::Light& l = v.lights[i];
        if (l.type != venue::LightType::Directional || l.group != qbkey::key("band") || l.dir[1] > -0.2f) continue;
        if (!l.createdAtStart && !(l.createdOn && lights_.flag(l.createdOn))) continue;
        const venue::LightState& st = lightState_[i];
        const float p = st.intensity * (st.color[0] + st.color[1] + st.color[2]);
        if (p > bestPower) bestPower = p, best = int(i);
    }
    static const bool log = SDL_getenv("GD04_SHADOW_LOG") != nullptr;
    if (log) SDL_Log("shadow: light %s", best >= 0 ? qbkey::names().get(v.lights[size_t(best)].name).c_str() : "none");
    if (best < 0) return;
    venue::Vec3 centre{};
    int n = 0;
    for (const Band::Entry& e : band_->members())
        if (e.placed) centre[0] += e.root.m[3], centre[1] += e.root.m[7], centre[2] += e.root.m[11], ++n;
    if (!n) return;
    for (float& c : centre) c /= float(n);
    const venue::Vec3 d = v.lights[size_t(best)].dir;
    const float dl = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const venue::Vec3 f{d[0] / dl, d[1] / dl, d[2] / dl};
    venue::Vec3 r{f[2], 0, -f[0]};  // across the light, level
    const float rl = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rl < 1e-4f) r = {1, 0, 0};
    else r[0] /= rl, r[2] /= rl;
    const venue::Vec3 u{f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    const float back = 30.0f, halfSize = 7.0f, depth = 60.0f;
    const venue::Vec3 eye{centre[0] - f[0] * back, centre[1] + 1.0f - f[1] * back, centre[2] - f[2] * back};
    gfx::Mat4 view, proj;
    const venue::Vec3 rows[3] = {r, u, f};
    for (int a = 0; a < 3; ++a) {
        for (int c = 0; c < 3; ++c) view.at(a, c) = rows[a][size_t(c)];
        view.at(a, 3) = -(rows[a][0] * eye[0] + rows[a][1] * eye[1] + rows[a][2] * eye[2]);
    }
    // Orthographic, Direct3D depth 0..1 (the casters' shaders convert it
    // when they export; the receivers compare z / w with the map: lit where
    // not deeper, 4 taps blended bilinearly) [C: the DynamicShadowReciever
    // microcode; I: the engine's matrices]. The casters draw upside down:
    // the receivers' v = 0.5 - 0.5 y reads OpenGL target rows bottom up.
    proj.at(0, 0) = 1.0f / halfSize, proj.at(1, 1) = 1.0f / halfSize, proj.at(2, 2) = 1.0f / depth;
    const gfx::Mat4 vp = proj * view;
    gfx::Mat4 casterProj = proj;
    casterProj.at(1, 1) = -casterProj.at(1, 1);
    const gfx::Mat4 casterVp = casterProj * view;
    gh3shader::Globals g;
    g.setMatrix(gh3shader::kRegView, view, 4);
    g.setMatrix(gh3shader::kRegProjection, casterProj, 4);
    g.setMatrix(gh3shader::kRegViewProj, casterVp, 4);
    g.setMatrix(gh3shader::kRegInvView, view.inverseAffine(), 4);
    g.set(gh3shader::kRegDistanceLodScaler, 1, 0, 0, 0);
    gfx::RenderDevice& dev = *device_;
    const gfx::TargetId oldTarget = dev.currentTarget();
    int vx, vy, vw, vh;
    dev.currentViewport(vx, vy, vw, vh);
    dev.setTarget(shadow_);
    dev.setViewport(0, 0, 1024, 1024);
    const float white[4] = {1, 1, 1, 1};
    dev.clear(white, true);
    band_->drawShadow(ms, g, v, lightState_, lights_);
    dev.setTarget(oldTarget);
    dev.setViewport(vx, vy, vw, vh);
    static const float intensity = SDL_getenv("GD04_SHADOW_INTENSITY") ? float(std::atof(SDL_getenv("GD04_SHADOW_INTENSITY"))) : 0.35f;
    view_->setShadow(dev.targetDepth(shadow_), vp, 1024, intensity);
}

void VenueStage::draw(double ms) {
    if (!view_) return;
    lights_.evaluate(ms, lightState_);
    const venue::CamPose cam = cameras_.pose(ms);
    shadowPass(ms);
    if (crowd_) {
        crowd_->render(ms, cam, *venue_, lightState_, lights_);
        for (uint32_t card : hide_) view_->setOverride(card, crowd_->texture(card));
    }
    view_->setTime(ms / 1000.0);
    view_->draw(cam, lightState_, lights_);
    if (band_) band_->draw(ms, cam, *venue_, lightState_, lights_);
    view_->drawVolumes(cam, lights_);
    if (particles_) particles_->draw(cam, ms);
}
