// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_stage.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>

#include "data/qbkey.h"
#include "render/gh3_shader.h"
#include "game/chart.h"
#include "script/globals.h"

VenueStage::~VenueStage() {
    fx_.shutdown();
    if (device_ && shadow_) device_->destroyTarget(shadow_);
}

namespace {

float num(const qb::Struct& s, const char* name, float def) {
    const qb::Value* v = s.find(qbkey::key(name));
    return v && (v->type == qb::Type::Float || v->type == qb::Type::Int) ? v->number() : def;
}

// An [r, g, b] array of 0..255 as 0..1 (Function_8241BC20: x DAT_82007b38) [C].
void rgb(const qb::Struct& s, const char* name, float out[3], float scale) {
    const qb::Value* v = s.find(qbkey::key(name));
    if (!v || v->type != qb::Type::Array || !v->arr) return;
    for (size_t k = 0; k < 3 && k < v->arr->values.size(); ++k) out[k] = v->arr->values[k].number() * scale;
}

// One screen_FX entry into the effect parameters [C data tod_profiles.qb;
// the field offsets: Function_8241BC20 (simple_bloom)].
void readFx(const qb::Struct& e, screenfx::Params& p, int& brightSlot) {
    const qb::Value* type = e.find(qbkey::key("Type"));
    if (!type) return;
    const bool on = num(e, "ON", 1) > 0;
    if (type->key == qbkey::key("simple_bloom")) {
        screenfx::Bloom& b = p.bloom;
        b.on = on;
        rgb(e, "color", b.cutoff, 1.0f / 255.0f);
        rgb(e, "strength", b.strength, 1.0f / 255.0f);
        b.bloom1 = num(e, "bloom1", 0), b.bloom2 = num(e, "bloom2", 0);
        b.falloff1 = num(e, "falloff1", 0), b.falloff2 = num(e, "falloff2", 0);
        b.materialFilter = num(e, "materialfilter", 0) > 0;
        b.autoLuminance = num(e, "autoluminancemod", 0) > 0;
        b.distanceMod = num(e, "distancemod", 0);
    } else if (type->key == qbkey::key("dof")) {
        screenfx::Dof& d = p.dof;
        d.on = on;
        d.backDist = num(e, "backdist", 1), d.frontDist = num(e, "frontdist", 0);
        d.strength = num(e, "strength", 0), d.blurRadius = num(e, "blurradius", 0);
        const qb::Value* r = e.find(qbkey::key("blurresolution"));
        d.half = r && r->key == qbkey::key("half");
    } else if (type->key == qbkey::key("bright_sat") && brightSlot < 2) {
        screenfx::BrightSat& bs = p.brightSat[brightSlot++];
        bs.on = on;
        bs.contrast = num(e, "Contrast", 1), bs.brightness = num(e, "Brightness", 1);
        bs.saturation = num(e, "saturation", 1), bs.hue = num(e, "hue", 1);
        rgb(e, "red_mix", bs.mix[0], 1), rgb(e, "green_mix", bs.mix[1], 1), rgb(e, "blue_mix", bs.mix[2], 1);
    }
}

}  // namespace

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
    // The lighters: <song>_performance's Crowd_StartLighters / Crowd_StopLighters [C data, 12 songs].
    lighterSpans_.clear();
    if (band_)
        for (const qb::File& f : band_->songFiles())
            if (const qb::Value* pv = f.find(qbkey::key(chart.song + "_performance")); pv && pv->type == qb::Type::Array && pv->arr) {
                double on = -1;
                for (const qb::Value& e : pv->arr->values) {
                    if (e.type != qb::Type::Struct || !e.st) continue;
                    const qb::Value* scr = e.st->find(qbkey::key("scr"));
                    const qb::Value* tm = e.st->find(qbkey::key("time"));
                    if (!scr || !tm) continue;
                    if (scr->key == qbkey::key("Crowd_StartLighters") && on < 0) on = tm->number();
                    else if (scr->key == qbkey::key("Crowd_StopLighters") && on >= 0) lighterSpans_.push_back({on, tm->number()}), on = -1;
                }
                if (on >= 0) lighterSpans_.push_back({on, 1e18});
            }
    if (crowd_) crowd_->setLighterSpans(lighterSpans_);
    // crowd_create_lighters: <zone>_LIGHTER_Geo00..14 created and hidden,
    // shown by Crowd_ToggleLighters (not in z_artdeco) [C script].
    lighterNodes_.clear();
    if (zone != "z_artdeco")
        for (int i = 0; i < 15; ++i) {
            char nm[64];
            std::snprintf(nm, sizeof nm, "%s_LIGHTER_Geo%02d", zone.c_str(), i);
            if (venue_->node(qbkey::key(nm))) lighterNodes_.push_back(qbkey::key(nm));
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
    // The stage peds: their trigger scripts run on the zone VM (Particles).
    peds_.reset();
    if (particles_ && !SDL_getenv("GD04_NO_STAGEPEDS")) {
        const band::Library* lib = band_ ? &band_->library() : nullptr;
        std::string serr;
        try {
            if (!lib) {
                pedLib_ = std::make_unique<band::Library>();
                pedLib_->load(disc);
                lib = pedLib_.get();
            }
            peds_ = std::make_unique<StagePeds>();
            if (!peds_->start(disc, g, *venue_, *lib, serr)) {
                if (!serr.empty()) SDL_Log("%s", serr.c_str());
                peds_.reset();
            }
        } catch (const std::exception& ex) {
            SDL_Log("stage peds: %s", ex.what());
            peds_.reset();
        }
        if (peds_) {
            StagePeds* sp = peds_.get();
            particles_->setAnimHook([sp](uint32_t obj, uint32_t anim, float blend, double t) { return sp->play(obj, anim, blend, t); });
        }
    }
    // The venue viewport's screen effects: enable_bg_viewport_properties ->
    // TOD_Proxim_Update_LightFX_Viewport fxParam = Default_TOD_Manager (its
    // screen_FX: bright_sat, simple_bloom) [C scripts tod_proxims.qb,
    // guitar_*.qb]; global_brightness (bright_sat, Brightness 0.5 + 0.1 x
    // SE_Brightness) is viewport 0's, not the venue's [C].
    fxParams_ = {};
    globals_ = &g;
    if (const qb::Value* tod = g.find(qbkey::key("Default_TOD_Manager")); tod && tod->type == qb::Type::Struct && tod->st)
        if (const qb::Value* fx = tod->st->find(qbkey::key("screen_FX")); fx && fx->type == qb::Type::Array && fx->arr) {
            int slot = 0;
            for (const qb::Value& e : fx->arr->values)
                if (e.type == qb::Type::Struct && e.st) readFx(*e.st, fxParams_, slot);
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
    fxReady_ = !SDL_getenv("GD04_NO_SCREENFX") && fx_.init(device, gh3shader::library(disc));
    if (particles_) {
        std::string perr;
        if (!particles_->attach(device, disc, perr)) {
            SDL_Log("particles: %s", perr.c_str());
            particles_.reset();
        }
    }
    if (peds_) {
        std::string perr;
        if (!peds_->attach(device, gh3shader::library(disc), perr)) {
            SDL_Log("stage peds: %s", perr.c_str());
            peds_.reset();
            if (particles_) particles_->setAnimHook(nullptr);
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

bool VenueStage::lightersOn(double ms) const {
    if (crowd_) return crowd_->lightersOn(std::floor(ms / (1000.0 / 60.0)) * (1000.0 / 60.0));
    bool span = false;
    for (const auto& [a, b] : lighterSpans_) span = span || (ms >= a && ms < b);
    return span && perf_.at(ms) != venue::Performance::Poor;
}

void VenueStage::update(double ms, float health, bool starPower) {
    if (!venue_) return;
    perf_.record(ms, performance(health));
    advance(ms);
    if (starPower != starPower_) {
        starPower_ = starPower;
        cameras_.starPower(ms, starPower);
    }
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
    const bool fx = fxReady_ && fx_.begin();
    lights_.evaluate(ms, lightState_);
    const venue::CamPose cam = cameras_.pose(ms);
    shadowPass(ms);
    if (crowd_) {
        crowd_->render(ms, cam, *venue_, lightState_, lights_);
        for (uint32_t card : hide_) view_->setOverride(card, crowd_->texture(card));
    }
    view_->setTime(ms / 1000.0);
    {
        const bool on = !lighterNodes_.empty() && lightersOn(ms);
        const std::vector<uint32_t>* ln = &lighterNodes_;
        const Particles* pt = particles_.get();
        view_->setNodeVisibility([ln, on, pt](uint32_t node) {
            if (std::find(ln->begin(), ln->end(), node) != ln->end()) return on ? 1 : 0;
            return pt ? pt->visibility(node) : -1;
        });
    }
    if (particles_) {
        Particles* pt = particles_.get();
        view_->setNodeMotion([pt, ms](uint32_t node, gfx::Mat4& d) { return pt->nodeMotion(node, ms, d); });
    }
    view_->draw(cam, lightState_, lights_);
    if (band_) band_->draw(ms, cam, *venue_, lightState_, lights_);
    if (peds_) peds_->draw(ms, cam, *venue_, lightState_, lights_, particles_.get());
    view_->drawVolumes(cam, lights_);
    if (particles_) particles_->draw(cam, ms);
    if (fx) {
        // venue_DOF: the shot's dof (a DOF_*_tod_manager's screen_FX[0]), else
        // DOF_Off_TOD_Manager's, set when the cut starts [C CameraCuts_StartCallback].
        screenfx::Params p = fxParams_;
        const uint32_t dofKey = cameras_.shot().dof ? cameras_.shot().dof : qbkey::key("DOF_Off_TOD_Manager");
        if (globals_)
            if (const qb::Value* m = globals_->find(dofKey); m && m->type == qb::Type::Struct && m->st)
                if (const qb::Value* fx = m->st->find(qbkey::key("screen_FX")); fx && fx->type == qb::Type::Array && fx->arr && !fx->arr->values.empty())
                    if (const qb::Value& e = fx->arr->values.front(); e.type == qb::Type::Struct && e.st) {
                        int slot = 2;
                        readFx(*e.st, p, slot);
                    }
        p.nearZ = 0.05f, p.farZ = 4000.0f;  // VenueView::globals' projection
        fx_.end(p);
    }
}
