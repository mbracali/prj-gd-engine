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
    if (teslaFrames || (particles_ && particles_->scriptedCreated))
        SDL_Log("venue: %llu scripted particle systems (%llu ribbon segments drawn), %llu frames with Tesla bolts",
                (unsigned long long)(particles_ ? particles_->scriptedCreated : 0),
                (unsigned long long)(particles_ ? particles_->ribbonSegments : 0), (unsigned long long)teslaFrames);
    if (view_ && (view_->stats().raytraced || view_->stats().projected))
        SDL_Log("venue: %llu raytraced beam draws, %llu projector draws", (unsigned long long)view_->stats().raytraced,
                (unsigned long long)view_->stats().projected);
    fx_.shutdown();
    if (device_ && shadow_) device_->destroyTarget(shadow_);
}

void VenueStage::playClip(int role, uint32_t clip, uint32_t stance, bool cycle, double ms) {
    if (band_) band_->playClip(role == 1 ? band::Role::Bassist : band::Role::Guitarist, clip, stance, cycle, ms);
}

bool VenueStage::bonePos(uint32_t member, uint32_t bone, venue::Vec3& out) const { return band_ && band_->bonePos(member, bone, out); }

bool VenueStage::smallVenue() {
    // UseSmallVenueAnims: z_party, z_dive, z_video, z_prison, z_hell, and
    // z_artdeco after the encore (LS_ENCORE_POST) [C script].
    const std::string& z = venue_ ? venue_->zone : std::string();
    return z == "z_party" || z == "z_dive" || z == "z_video" || z == "z_prison" || z == "z_hell" ||
           (z == "z_artdeco" && lights_.flag(qbkey::key("LS_ENCORE_POST")));
}

void VenueStage::introAnims(double ms, bool p2Career) {
    if (!band_) return;
    using band::Role;
    const uint32_t intro = qbkey::key(smallVenue() ? "intro_smstg" : "intro"), a = qbkey::key("Stance_A");
    // z_artdeco after the encore: the guitarist straight to Stance_A [C script play_intro_anims].
    const bool guitaristIntro = !(venue_ && venue_->zone == "z_artdeco" && lights_.flag(qbkey::key("LS_ENCORE_POST")));
    // player1 / player2_status.band_member: Guitarist / Bassist in the port's two-player career.
    band_->changeStance(Role::Guitarist, guitaristIntro ? intro : a, true, ms);
    band_->changeStance(Role::Bassist, guitaristIntro && p2Career ? a : intro, true, ms);
    band_->changeStance(Role::Vocalist, intro, true, ms);
    band_->changeStance(Role::Drummer, qbkey::key("intro"), true, ms);
    for (Role r : {Role::Guitarist, Role::Bassist, Role::Vocalist, Role::Drummer}) band_->changeStance(r, a, false, ms);
}

void VenueStage::outro(double ms, bool won, bool versus, bool p1Won, bool boss) {
    // UseSmallVenueAnims: z_party, z_dive, z_video, z_prison, z_hell, and
    // z_artdeco after the encore (LS_ENCORE_POST) -> Win_SmStg / Lose_SmStg [C script].
    const bool small = smallVenue();
    const uint32_t win = qbkey::key(small ? "Win_SmStg" : "win"), lose = qbkey::key(small ? "Lose_SmStg" : "lose");
    if (band_) {
        // play_win_anims / play_lose_anims: one player or co-op, both members
        // the same; a boss battle: the guitarist the player's outcome, the
        // bassist (the boss) the other; the versus modes by who won [C scripts].
        // play_outro kills the strum / fret / finger / drum iterators and
        // event_iterator: no more strums, drum hits or performance stance
        // changes over the win / lose anims [C script play_outro].
        band_->endSongEvents(ms);
        uint32_t g = won ? win : lose, b = g;
        if (boss) b = won ? lose : win;
        else if (versus) g = p1Won ? win : lose, b = p1Won ? lose : win;
        // blendduration 0.0; the drummer and the singer too, `win` / `lose`
        // whatever the venue (desired_twist 0) [C scripts play_win_anims,
        // play_lose_anims: they stayed in the song's loop].
        band_->playSpecial(band::Role::Guitarist, g, qbkey::key("idle"), ms, 0.0f);
        band_->playSpecial(band::Role::Bassist, b, qbkey::key("idle"), ms, 0.0f);
        const uint32_t rest = qbkey::key(won ? "win" : "lose");
        band_->playSpecial(band::Role::Drummer, rest, qbkey::key("idle"), ms, 0.0f);
        band_->playSpecial(band::Role::Vocalist, rest, qbkey::key("idle"), ms, 0.0f);
    }
    // Crowd_AllPlayAnim Anim = idle: the lighters end [C data].
    if (crowd_) crowd_->setLighterSpans({});
    cameras_.outro(ms, won ? "cameras_win" : "cameras_lose");
    ++outros;
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
                       gfx::RenderDevice* device, std::string& error, double songStartMs) {
    const double from = songStartMs - leadInMs;  // the venue's first ms
    try {
        venue_ = venue::load(disc, zone);
    } catch (const std::exception& e) {
        error = zone + ": " + e.what();
        return false;
    }
    perf_ = {};
    cameras_ = {};
    lights_ = {};
    cameras_.init(*venue_, g, chart, songKey ^ venue_->zoneKey, leadInMs, songStartMs);
    lights_.init(*venue_, g, chart);
    // The band (create_band, at the song's start: the gem scroller's lead-in) [C script].
    band_.reset();
    if (!SDL_getenv("GD04_NO_BAND")) {
        band_ = std::make_unique<Band>();
        std::string berr;
        const uint64_t t0 = SDL_GetTicks();
        if (!band_->start(disc, g, *venue_, chart, songKey, &perf_, from, berr)) {
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
        if (!crowd_->start(disc, g, *venue_, chart, &perf_, band_ ? &band_->library() : nullptr, from, cerr)) {
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
    if (!SDL_getenv("GD04_NO_PARTICLES") && particlesOn) {
        particles_ = std::make_unique<Particles>();
        std::string perr;
        particles_->setLightShow(&lights_);
        particles_->bonePos = [this](uint32_t member, uint32_t bone, venue::Vec3& out) { return band_ && band_->bonePos(member, bone, out); };
        // The camera's script shots run on the zone VM (CameraCuts_NewIntro, the encores, the boss cameras).
        particles_->camMorph = [this](const qb::Struct& p, double ms) { cameras_.scriptMorph(p, ms); };
        particles_->camMorphDone = [this](double ms) { return cameras_.scriptMorphDone(ms); };
        cameras_.startScript = [this](uint32_t script, double) { return particles_ ? particles_->spawnCameraScript(script) : 0u; };
        cameras_.stopScript = [this](uint32_t run) {
            if (particles_) particles_->killRun(run);
        };
        if (!particles_->start(disc, g, *venue_, chart, &perf_, from, perr)) {
            if (!perr.empty()) SDL_Log("particles: %s", perr.c_str());
            particles_.reset();
        }
        cameras_.retryScript();  // the intro's, started before the VM was there
    }
    // The stage peds: their trigger scripts run on the zone VM (Particles).
    peds_.reset();
    if (particles_ && !SDL_getenv("GD04_NO_STAGEPEDS")) {
        const band::Library* lib = band_ ? &band_->library() : nullptr;
        std::string serr;
        try {
            if (!lib) {
                pedLib_ = band::Library::shared(disc);
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
    globals_ = &g;
    screenfx::readViewport(g, qbkey::key("Default_TOD_Manager"), fxParams_);
    hide_.clear();
    const qb::Value* cm = g.find(qbkey::key(zone + "_crowd_models"));
    if (cm && cm->type == qb::Type::Array && cm->arr)
        for (const qb::Value& e : cm->arr->values)
            if (e.type == qb::Type::Struct && e.st)
                if (const qb::Value* t = e.st->find(qbkey::key("textureasset"))) hide_.push_back(t->key);
    view_.reset();
    attachStep_ = 0;  // a restarted stage uploads again (attachStep)
    if (device && !attach(*device, disc, error)) return false;
    return true;
}

bool VenueStage::attach(gfx::RenderDevice& device, const Disc& disc, std::string& error) {
    if (!venue_) return bool(view_);
    while (!attachStep(device, disc, error))
        if (!view_) return false;
    return bool(view_);
}

bool VenueStage::attachStep(gfx::RenderDevice& device, const Disc& disc, std::string& error) {
    if (!venue_) return true;
    if (attachStep_ >= kAttachSteps) return true;
    switch (attachStep_) {
    case 0:
        view_ = std::make_unique<VenueView>();
        if (!view_->init(device, disc, *venue_, hide_, error)) {
            view_.reset();
            attachStep_ = kAttachSteps;  // failed: nothing more to upload
            return true;
        }
        break;
    case 1:
        if (band_) {
            std::string berr;
            if (!band_->attach(device, berr)) SDL_Log("band: %s", berr.c_str());
        }
        break;
    case 2: {
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
        break;
    }
    case 3:
        if (particles_) {
            std::string perr;
            if (!particles_->attach(device, disc, perr)) {
                SDL_Log("particles: %s", perr.c_str());
                particles_.reset();
            }
        }
        break;
    case 4:
        if (peds_) {
            std::string perr;
            if (!peds_->attach(device, gh3shader::library(disc), perr)) {
                SDL_Log("stage peds: %s", perr.c_str());
                peds_.reset();
                if (particles_) particles_->setAnimHook(nullptr);
            }
        }
        break;
    case 5:
        if (crowd_) {
            std::string cerr;
            if (!crowd_->attach(device, gh3shader::library(disc), cerr)) {
                SDL_Log("crowd: %s", cerr.c_str());
                crowd_.reset();
            }
        }
        break;
    }
    return ++attachStep_ >= kAttachSteps;
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

void VenueStage::starPowerAnim(int p, double) {
    if (p >= 0 && p < 2 && band_) spAnimCuts_[size_t(p)] = cameras_.cuts();
}

void VenueStage::advance(double ms) {
    if (!venue_) return;
    if (band_) band_->advance(ms);
    if (crowd_) crowd_->advance(ms);
    if (particles_) particles_->advance(ms);
    cameras_.update(ms, perf_);
    // handle_star_power: at the next camera cut (CameraCuts_StartCallback),
    // the Starpower anim once, cut in, the arms on; then Stance_A's idle
    // [C scripts handle_star_power, play_special_anim; I: player 1's band
    // member the guitarist, player 2's the bassist].
    for (int p = 0; p < 2; ++p)
        if (spAnimCuts_[size_t(p)] >= 0 && cameras_.cuts() != spAnimCuts_[size_t(p)] && band_) {
            spAnimCuts_[size_t(p)] = -1;
            band_->playSpecial(p == 0 ? band::Role::Guitarist : band::Role::Bassist, qbkey::key("Stance_A"), qbkey::key("Starpower"), ms,
                               0.0f, 0);
        }
    lights_.update(ms, perf_);
}

void VenueStage::shadowPass(double ms) {
    view_->setShadow({}, gfx::Mat4{}, 1024, 0.35f);
    if (band_ && band_->view()) band_->view()->setShadow({}, gfx::Mat4{}, 1024, 0.35f);
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
    // SetShadowDirFromLight (a zone's node script) names it outright [C scripts].
    if (particles_ && particles_->shadowLight)
        for (size_t i = 0; i < v.lights.size(); ++i)
            if (v.lights[i].name == particles_->shadowLight) best = int(i);
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
    // The receivers compare a little nearer the light than the map: without a
    // bias a surface shadows itself in speckles at the map's 1.4 cm texels (the
    // band's self-shadowing) [I: GH3's bias, in its projection or its shadow
    // pass's depth-bias state, is not decoded; 4 cm].
    static const float biasM = SDL_getenv("GD04_SHADOW_BIAS") ? float(std::atof(SDL_getenv("GD04_SHADOW_BIAS"))) : 0.04f;
    gfx::Mat4 bias;
    bias.at(2, 3) = -biasM / depth;
    const gfx::Mat4 recv = bias * vp;
    view_->setShadow(dev.targetDepth(shadow_), recv, 1024, intensity);
    if (band_->view()) band_->view()->setShadow(dev.targetDepth(shadow_), recv, 1024, intensity);
}

void VenueStage::draw(double ms) {
    if (!view_) return;
    // Enhanced's chain: on when any of its effects is (and it has not failed).
    const bool chain = post.any() && device_ && !(enh_ && enh_->failed());
    float internalScale = 1.0f;
    if (internalHeight > 0 && device_) {
        int vx, vy, vw, vh;
        device_->currentViewport(vx, vy, vw, vh);
        if (vh > 0) internalScale = float(internalHeight) / float(vh);
    }
    const bool fx = fxReady_ && fx_.begin(std::clamp(internalScale * (chain ? post.renderScale() : 1.0f), 0.25f, 4.0f));
    if (fx && chain) {
        if (!enh_) enh_ = std::make_unique<gfx::EnhancedFx>(*device_);
        const std::array<float, 2> j = enh_->jitter(post, fx_.sceneW(), fx_.sceneH());
        VenueView::jitterX = j[0], VenueView::jitterY = j[1];
    } else {
        VenueView::jitterX = VenueView::jitterY = 0;
    }
    lights_.evaluate(ms, lightState_);
    const venue::CamPose cam = hasFixedCam_ ? fixedCam_ : cameras_.pose(ms);
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
    static const bool noProjectors = SDL_getenv("GD04_NO_PROJECTORS") != nullptr;  // development aid: compare without them
    if (!noProjectors) view_->drawProjectors(cam, lights_);
    view_->drawVolumes(cam, lights_);
    static const bool noRays = SDL_getenv("GD04_NO_RAYTRACE") != nullptr;  // development aid: compare without the `volume` beams
    if (fx && !noRays && view_->hasRaytraced()) view_->drawRaytraced(cam, lights_, fx_.depthSnapshot());
    if (particles_) particles_->draw(cam, ms);
    // gd02's extras (EnhancedLights) work in GH2's space (z up, ~44 units a
    // metre): the band's feet and the camera mapped there, and back by the matrix [I].
    constexpr float kU = 44.0f;
    auto to02 = [](const venue::Vec3& p, float k) { return gfx::Vec3{p[0] * k, p[2] * k, p[1] * k}; };
    auto lightsView = [&] {
        gh3shader::Globals g;
        gfx::Mat4 vp;
        VenueView::cameraGlobals(cam, device_->info().clipDepthZeroToOne, ms / 1000.0, g, &vp);
        gfx::Mat4 back;  // GH2 space -> GH3 world
        back.at(0, 0) = 1 / kU, back.at(1, 1) = 0, back.at(2, 2) = 0, back.at(1, 2) = 1 / kU, back.at(2, 1) = 1 / kU;
        gfx::LightsView lv;
        lv.viewProj = vp * back;
        lv.pos = to02(cam.pos, kU);
        lv.right = to02(venue::rotate(cam.rot, {-1, 0, 0}), 1);
        lv.up = to02(venue::rotate(cam.rot, {0, 1, 0}), 1);
        return lv;
    };
    // The Tesla Star Power's bolts (Create / SetTeslaEffectObject: width =
    // 0.15 x the length, the zigzag) [C script Create_StarPower_TeslaFX; I: the look].
    if (device_ && particles_ && !particles_->teslas.empty()) {
        std::vector<gfx::EnhancedLights::Bolt> bolts;
        for (const auto& [name, t] : particles_->teslas)
            bolts.push_back({to02(t.start, kU), to02(t.end, kU), std::min(t.width, 1.5f) * kU * 0.35f, name});
        if (!extras_) extras_ = std::make_unique<gfx::EnhancedLights>(*device_);
        extras_->drawBolts(bolts, lightsView(), float(ms / 1000.0), 0.012f * kU, 0.06f * kU);
        teslaFrames += 1;
    }
    if (extras.any() && band_ && device_) {
        std::vector<gfx::Vec3> feet;
        for (const Band::Entry& e : band_->members())
            if (e.placed) feet.push_back(to02({e.root.m[3], e.root.m[7], e.root.m[11]}, kU));
        const gfx::LightsView lv = lightsView();
        if (!extras_) extras_ = std::make_unique<gfx::EnhancedLights>(*device_);
        extras_->draw(extras, feet, lv, false, float(ms / 1000.0));
    }
    if (fx) {
        // venue_DOF: the shot's dof (a DOF_*_tod_manager's screen_FX[0]), else
        // DOF_Off_TOD_Manager's, set when the cut starts [C CameraCuts_StartCallback].
        screenfx::Params p = fxParams_;
        if (globals_) screenfx::readDof(*globals_, cameras_.shot().dof, p);
        // ScreenFlash (z_prison's flares): FlashBS on the venue viewport, a
        // bright_sat of its own after the TOD's [C scripts misclightutils.qb].
        if (float b = 1, c = 1; particles_ && particles_->flash(ms, b, c)) {
            screenfx::BrightSat& bs = p.brightSat[1];
            bs = screenfx::BrightSat{};
            bs.on = true, bs.brightness = b, bs.contrast = c;
            ++flashFrames;
        }
        p.nearZ = 0.05f, p.farZ = 4000.0f;  // VenueView::globals' projection
        // Enhanced > Bokeh Effect: GH3's depth of field off, as is, or twice as strong [I].
        // None / Light / Medium (as is) / Strong / Stronger [I: the scale].
        static constexpr float kBokeh[5] = {0, 0.6f, 1, 1.5f, 2};
        if (post.bokeh <= 0) p.dof.on = false;
        else p.dof.blurRadius *= kBokeh[std::min(post.bokeh, 4)];
        if (!post.original) p.bloom.on = false, p.brightSat[0].on = p.brightSat[1].on = false;  // Remove All Effects
        if (!post.gameBloom) p.bloom.on = false;  // Enhanced > Bloom: None
        VenueView::jitterX = VenueView::jitterY = 0;
        bool chained = false;
        if (chain && enh_) {
            const int sw = fx_.sceneW(), sh = fx_.sceneH();
            if (!ldr_ || ldrW_ != sw || ldrH_ != sh) {
                if (ldr_) device_->destroyTarget(ldr_);
                std::string err;
                gfx::TargetDesc d;
                d.width = sw, d.height = sh;
                ldr_ = device_->createTarget(d, err);
                ldrW_ = sw, ldrH_ = sh;
            }
            if (ldr_) {
                fx_.end(p, ldr_);
                gfx::PostFrame f;
                gh3shader::Globals g;
                gfx::Mat4 vp;
                VenueView::cameraGlobals(cam, false, ms / 1000.0, g, &vp);
                f.viewProj = vp, f.hasCamera = true;
                f.timeSec = float(double(SDL_GetTicksNS()) / 1e9);
                // Blur on Star Power: in over 0.4 s, out over 0.6 s (gd02's) [I].
                const double now = double(SDL_GetTicksNS()) / 1e9;
                const float dt = starBlurAt_ > 0 ? float(std::clamp(now - starBlurAt_, 0.0, 0.1)) : 0.0f;
                starBlurAt_ = now;
                starBlur_ = std::clamp(starBlur_ + (starPowerOn_ ? dt / 0.4f : -dt / 0.6f), 0.0f, 1.0f);
                f.starBlur = starBlur_;
                int ox, oy, ow, oh;
                fx_.outerViewport(ox, oy, ow, oh);
                chained = enh_->process(device_->targetTexture(ldr_), fx_.sceneDepth(), sw, sh, ox, oy, ow, oh, post, f, fx_.outer());
                if (!chained) SDL_Log("enhanced fx: the chain failed; the venue is drawn without it");
            }
        }
        if (!chained) fx_.end(p);
    }
}
