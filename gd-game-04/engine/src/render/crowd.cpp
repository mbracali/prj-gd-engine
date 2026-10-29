// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/crowd.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <exception>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "game/chart.h"
#include "script/globals.h"

namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }
constexpr double kFrameMs = 1000.0 / 60.0;

const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }
uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }
std::string strOf(const qb::Value* v) {
    return v && (v->type == qb::Type::String || v->type == qb::Type::WString) ? v->str : std::string();
}

// 'Real_Crowd\Crowd_Ped_01.skin' -> the pak entry's full name key,
// qbkey("models\Real_Crowd\Crowd_Ped_01.skin") [C data: the zone paks' entries].
uint32_t modelKey(const std::string& model) {
    std::string s = "models\\";
    for (char c : model)
        if (!(c == '\\' && !s.empty() && s.back() == '\\')) s += c == '/' ? '\\' : c;
    return qbkey::key(s);
}

// Loads one skin and its texture dictionary (the entry's parent) from a pak.
bool loadSkin(const Pak& pak, uint32_t key, band::Model& m, std::string& error) {
    const PakEntry* e = pak.findType(key, K(".skin"));
    if (!e) {
        error = "no skin " + qbkey::names().get(key);
        return false;
    }
    auto [p, size] = pak.data(*e);
    band::Part part;
    part.scene = scene::parse(p, size, scene::Kind::Skin);
    m.parts.push_back(std::move(part));
    if (e->parent)
        if (const PakEntry* t = pak.findType(e->parent, K(".tex"))) {
            auto [tp, ts] = pak.data(*t);
            for (gh3tex::Texture& tx : gh3tex::decodeTexDict(tp, ts))
                if (tx.supported && !tx.levels.empty()) m.textures.emplace(tx.key, std::move(tx));
        }
    return true;
}

bandanim::Quat yRot(float degrees) {
    const float a = degrees * 3.14159265f / 360.0f;
    return {0, std::sin(a), 0, std::cos(a)};
}

}  // namespace

uint32_t Crowd::random(Ped& p, uint32_t n) {
    // A per-ped generator (GetRandomValue's is not reproduced) [I].
    p.rng = p.rng * 6364136223846793005ull + 1442695040888963407ull;
    return n ? uint32_t((p.rng >> 33) % n) : 0;
}

bool Crowd::start(const Disc& disc, const Globals& g, const venue::Venue& v, const Chart& chart, const venue::PerfTimeline* perf,
                  const band::Library* shared, double startMs, std::string& error) {
    shutdown();
    peds_.clear();
    g_ = &g;
    perf_ = perf;
    fretbars_ = chart.fretbars;
    if (g.find(K("disable_crowd")) && qbNumber(g, K("disable_crowd"), 0) == 1) return false;  // create_crowd_models [C]
    const qb::Value* arr = g.find(qbkey::key(v.zone + "_crowd_models"));
    if (!arr || arr->type != qb::Type::Array || !arr->arr || arr->arr->values.empty()) {
        error = v.zone + ": no crowd models";
        return false;
    }
    lib_ = shared;
    try {
        if (!lib_) {
            ownLib_ = std::make_unique<band::Library>();
            ownLib_->load(disc);
            lib_ = ownLib_.get();
        }
        const anim::Skeleton* ske = lib_->skeleton(K("GH3_Ped"));  // obj_security_creation_params SkeletonName [C]
        if (!ske) {
            error = "no GH3_Ped skeleton";
            return false;
        }
        const Pak zone = Pak::load(disc, "DATA/COMPRESSED/ZONES/" + v.zone + ".pak.xen");
        const Pak global = Pak::load(disc, "DATA/COMPRESSED/ZONES/global.pak.xen");
        slowTempo_ = qbNumber(g, K("slow_tempo"), 75), medTempo_ = qbNumber(g, K("med_tempo"), 115), fastTempo_ = qbNumber(g, K("fast_tempo"), 145);
        blendTempo_ = qbNumber(g, K("Crowd_BlendTime_TempoChange"), 0.67f), blendBad_ = qbNumber(g, K("Crowd_BlendTime_Bad"), 0.5f);
        blendSpecial_ = qbNumber(g, K("Crowd_BlendTime_Special"), 0.4f), blendSlow_ = qbNumber(g, K("Crowd_BlendTime_Slow"), 0.6f);
        blendMed_ = qbNumber(g, K("Crowd_BlendTime_Med"), 0.4f), blendFast_ = qbNumber(g, K("Crowd_BlendTime_Fast"), 0.2f);
        const qb::Struct* profiles = structOf(g.find(K("Crowd_Profiles")));
        const size_t n = arr->arr->values.size();
        // style imposter_rendering (128 x 256), imposter_rendering_highres
        // (256 x 512) with at most six peds on Xenon [C scripts
        // create_crowd_models, tod_profiles.qb Default_TOD_Manager.styles].
        width_ = n <= 6 ? 256 : 128;
        height_ = width_ * 2;
        for (size_t i = 0; i < n; ++i) {
            const qb::Struct* e = structOf(&arr->arr->values[i]);
            if (!e || e->find(K("remap_only"))) continue;
            Ped p;
            p.name = keyOf(e->find(K("name")));
            p.card = keyOf(e->find(K("textureasset")));
            const std::string model = strOf(e->find(K("Model")));
            if (!p.card || model.empty()) continue;
            p.model = std::make_unique<band::Model>();
            p.model->skeleton = ske;
            if (!loadSkin(zone, modelKey(model), *p.model, error)) return false;
            // The clapping hands (switchonatomic lhand_clap / rhand_clap) [C script].
            p.lighterModel = std::make_unique<band::Model>();
            p.lighterModel->skeleton = ske;
            if (!loadSkin(zone, modelKey(model), *p.lighterModel, error)) return false;
            for (const char* hand : {"Real_Crowd\\Crowd_HandL_Clap.skin", "Real_Crowd\\Crowd_HandR_Clap.skin"}) {
                std::string herr;
                if (!loadSkin(global, modelKey(hand), *p.model, herr)) SDL_Log("crowd: %s", herr.c_str());
            }
            // The lighter: Crowd_AllSetHand hand = right Type = lighter (rhand_lighter) [C script].
            for (const char* hand : {"Real_Crowd\\Crowd_HandL_Clap.skin", "Real_Crowd\\Crowd_HandR_Lighter.skin"}) {
                std::string herr;
                if (!loadSkin(global, modelKey(hand), *p.lighterModel, herr)) SDL_Log("crowd: %s", herr.c_str());
            }
            for (const auto& [k, c] : lib_->permClips()) p.model->clips.emplace(k, c);
            p.rig.init(*ske);
            // The trigger script (<zone>_Crowd_Peds): Crowd_Profiles.<name>.anim_set [C script].
            if (const qb::Struct* prof = profiles ? structOf(profiles->find(p.name)) : nullptr)
                p.animSet = structOf(g.find(keyOf(prof->find(K("anim_set")))));
            if (!p.animSet) p.animSet = structOf(g.find(K("crowd_animations")));
            // pos = (-500, -200, 0) + (0, -100, 0) x index, Quat (0, 1, 0); roty
            // turns it (obj_setorientation y) [C script; I: degrees, absolute].
            p.pos = {-500.0f, -200.0f - 100.0f * float(i), 0.0f};
            const qb::Value* ry = e->find(K("roty"));
            const bandanim::Quat q = ry ? yRot(ry->number()) : yRot(180.0f);
            bandanim::Xf xf{q, {p.pos[0], p.pos[1], p.pos[2]}};
            p.root = bandanim::matOf(xf);
            p.rng = 0x9E3779B97F4A7C15ull ^ (uint64_t(p.name) << 7) ^ i;
            p.nextAt = startMs;
            peds_.push_back(std::move(p));
        }
    } catch (const std::exception& ex) {
        error = std::string("crowd: ") + ex.what();
        peds_.clear();
        return false;
    }
    return !peds_.empty();
}

bool Crowd::attach(gfx::RenderDevice& dev, std::shared_ptr<const matlib::Library> lib, std::string& error) {
    dev_ = &dev;
    view_.setLibrary(std::move(lib));
    view_.setView(float(width_) / float(height_), true, true);
    for (Ped& p : peds_) {
        p.view = view_.add(dev, *p.model, error);
        if (p.view < 0) return false;
        if (p.lighterModel) {
            std::string lerr;
            p.lighterView = view_.add(dev, *p.lighterModel, lerr);
        }
        gfx::TargetDesc td;
        td.width = width_, td.height = height_;
        td.depth = true;
        p.target = dev.createTarget(td, error);
        if (!p.target) return false;
    }
    return true;
}

void Crowd::shutdown() {
    if (dev_)
        for (Ped& p : peds_)
            if (p.target) dev_->destroyTarget(p.target), p.target = {};
    view_.shutdown();
    dev_ = nullptr;
}

// One pass of crowd_play_adjusting_random_anims: anim_set.idle.<skill>.<speed>,
// a random entry; the blend by what changed [C script guitar_crowd.qb].
const anim::Clip* Crowd::pick(Ped& p, double t, float& blend) {
    blend = -1;
    if (!p.animSet) return nullptr;
    const venue::Performance perf = perf_ ? perf_->at(t) : venue::Performance::Medium;
    const uint32_t skill = perf == venue::Performance::Poor ? K("bad") : perf == venue::Performance::Good ? K("good") : K("normal");
    double beat = 500;
    if (fretbars_.size() >= 2) {
        size_t i = size_t(std::upper_bound(fretbars_.begin(), fretbars_.end(), int32_t(t)) - fretbars_.begin());
        i = std::clamp<size_t>(i, 1, fretbars_.size() - 1);
        beat = std::max(1, fretbars_[i] - fretbars_[i - 1]);
    }
    const double bpm = 60000.0 / beat;
    const uint32_t speed = bpm < (slowTempo_ + medTempo_) * 0.5 ? K("slow") : bpm < (medTempo_ + fastTempo_) * 0.5 ? K("med") : K("fast");
    const qb::Struct* idle = structOf(g_->field(*p.animSet, p.lighters ? K("special") : K("idle")));
    const qb::Struct* bySkill = idle ? structOf(g_->field(*idle, skill)) : nullptr;
    const qb::Value* list = bySkill ? g_->field(*bySkill, speed) : nullptr;
    if (!list || list->type != qb::Type::Array || !list->arr || list->arr->values.empty()) return nullptr;
    const uint32_t key = keyOf(&list->arr->values[random(p, uint32_t(list->arr->values.size()))]);
    if (p.first) blend = blendTempo_;  // old_speed undefined: the tempo change blend
    else if (speed != p.lastSpeed) blend = blendTempo_;
    else if (skill == K("bad")) blend = blendBad_;
    else if (p.lighters) blend = blendSpecial_;
    else if (speed == K("slow")) blend = blendSlow_;
    else if (speed == K("med")) blend = blendMed_;
    else blend = blendFast_;
    p.first = false;
    p.lastSpeed = speed;
    return p.model->clip(key);
}

bool Crowd::lightersOn(double t) const {
    bool span = false;
    for (const auto& [a, b] : lighterSpans_) span = span || (t >= a && t < b);
    return span && (!perf_ || perf_->at(t) != venue::Performance::Poor);
}

void Crowd::advance(double ms) {
    for (size_t i = 0; i < peds_.size(); ++i) {
        Ped& p = peds_[i];
        auto playUntil = [&](double t) {
            int guard = 0;
            while (p.nextAt <= t && guard++ < 64) {
                float blend = 0;
                const anim::Clip* c = pick(p, p.nextAt, blend);
                if (c) {
                    p.body.play(c, p.nextAt, std::max(0.0f, blend));
                    // Only the newest clips matter for the cross-fade.
                    if (p.body.plays.size() > 3) p.body.plays.erase(p.body.plays.begin(), p.body.plays.end() - 3);
                    p.nextAt += std::max(kFrameMs, double(c->duration) * 1000.0);  // GameObj_WaitAnimFinished
                } else {
                    p.nextAt += kFrameMs;
                }
            }
        };
        if (lighterSpans_.empty()) {
            playUntil(ms);
            continue;
        }
        // crowd_monitor_performance, every game frame: a change of the
        // lighters restarts each ped's anims (Crowd_AllPlayAnim: one game
        // frame apart, ped by ped) [C script; I: on the 60 Hz grid].
        if (p.checkedAt < -1e17) p.checkedAt = std::floor(ms / kFrameMs) * kFrameMs - kFrameMs;
        while (p.checkedAt + kFrameMs <= ms) {
            p.checkedAt += kFrameMs;
            playUntil(p.checkedAt);
            if (lightersOn(p.checkedAt) != p.lighters) {
                p.lighters = !p.lighters;
                p.nextAt = p.checkedAt + kFrameMs * double(i + 1);
            }
        }
        playUntil(ms);
    }
}

void Crowd::render(double ms, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
                   const venue::LightShow& show) {
    if (!dev_ || peds_.empty()) return;
    gfx::RenderDevice& d = *dev_;
    const gfx::TargetId oldTarget = d.currentTarget();
    int vx, vy, vw, vh;
    d.currentViewport(vx, vy, vw, vh);
    // update_crowd_model_cam: the venue camera's direction turned about y by
    // 25 x its x component, the crowd camera 3.5 m from the ped's chest
    // height (model_pos + (0, 1, 0)) [C script; I: the camera stands on the
    // venue camera's side of the ped, looking along that direction].
    const venue::Vec3 f = venue::rotate(cam.rot, {0, 0, 1});
    const float ang = f[0] * 25.0f * 3.14159265f / 180.0f;
    venue::Vec3 at{f[0] * std::cos(ang) + f[2] * std::sin(ang), f[1], -f[0] * std::sin(ang) + f[2] * std::cos(ang)};
    const float len = std::sqrt(at[0] * at[0] + at[1] * at[1] + at[2] * at[2]);
    for (float& c : at) c /= std::max(len, 1e-6f);
    // The camera's rotation: +z along `at`, +y up, +x = y x z (screen left).
    venue::Vec3 x{at[2], 0, -at[0]};
    const float xl = std::sqrt(x[0] * x[0] + x[2] * x[2]);
    if (xl < 1e-5f) x = {1, 0, 0};
    else x[0] /= xl, x[2] /= xl;
    const venue::Vec3 y{at[1] * x[2] - at[2] * x[1], at[2] * x[0] - at[0] * x[2], at[0] * x[1] - at[1] * x[0]};
    const float m00 = x[0], m11 = y[1], m22 = at[2];
    venue::Quat q;
    const float tr = m00 + m11 + m22;
    if (tr > 0) {
        const float s = std::sqrt(tr + 1.0f) * 2;
        q = {(y[2] - at[1]) / s, (at[0] - x[2]) / s, (x[1] - y[0]) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
        q = {0.25f * s, (y[0] + x[1]) / s, (at[0] + x[2]) / s, (y[2] - at[1]) / s};
    } else if (m11 > m22) {
        const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
        q = {(y[0] + x[1]) / s, 0.25f * s, (at[1] + y[2]) / s, (at[0] - x[2]) / s};
    } else {
        const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
        q = {(at[0] + x[2]) / s, (at[1] + y[2]) / s, 0.25f * s, (x[1] - y[0]) / s};
    }
    view_.setTime(ms / 1000.0);
    const float clear[4] = {0, 0, 0, 0};
    for (Ped& p : peds_) {
        if (!p.target || p.view < 0) continue;
        bandanim::Pose pose = p.rig.bind;
        bandanim::evalBody(p.rig, p.body, ms, pose);
        bandanim::worldMatrices(p.rig, pose, p.world);
        p.skin.resize(p.world.size());
        for (size_t b = 0; b < p.world.size(); ++b) p.skin[b] = bandanim::mmul(p.world[b], p.rig.invBind[b]);
        venue::CamPose pc;
        pc.pos = {p.pos[0] - at[0] * 3.5f, p.pos[1] + 1.0f - at[1] * 3.5f, p.pos[2] - at[2] * 3.5f};
        pc.rot = q;
        pc.fov = 20.0f;  // sethfov hfov = 20.0 [C]
        d.setTarget(p.target);
        d.setViewport(0, 0, width_, height_);
        d.clear(clear, true);
        BandView::Instance in;
        in.member = p.lighters && p.lighterView >= 0 ? p.lighterView : p.view;
        in.skin = &p.skin;
        in.root = p.root;
        in.lightGroups = {K("Crowd")};  // lightgroup = Crowd [C script]
        view_.draw(pc, {in}, v, lights, show);
    }
    d.setTarget(oldTarget);
    d.setViewport(vx, vy, vw, vh);
    // Development aid: GD04_CROWD_DEBUG=1 draws the peds' views in the frame's corner.
    static const bool debug = SDL_getenv("GD04_CROWD_DEBUG") != nullptr;
    if (debug) {
        int k = 0;
        view_.setView(float(width_) / float(height_), true, false);
        for (Ped& p : peds_) {
            venue::CamPose pc;
            pc.pos = {p.pos[0] - at[0] * 3.5f, p.pos[1] + 1.0f - at[1] * 3.5f, p.pos[2] - at[2] * 3.5f};
            pc.rot = q;
            pc.fov = 20.0f;
            d.setViewport(vx + 120 * k++, vy, 120, 240);
            BandView::Instance in;
            in.member = p.view;
            in.skin = &p.skin;
            in.root = p.root;
            in.lightGroups = {K("Crowd")};
            view_.draw(pc, {in}, v, lights, show);
        }
        d.setViewport(vx, vy, vw, vh);
        view_.setView(float(width_) / float(height_), true, true);
    }
}

gfx::TextureId Crowd::texture(uint32_t card) const {
    for (const Ped& p : peds_)
        if (p.card == card && p.target && dev_) return dev_->targetTexture(p.target);
    return {};
}
