// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/venue_stage.h"

#include <SDL3/SDL_stdinc.h>

#include <exception>

#include "data/qbkey.h"
#include "game/chart.h"
#include "script/globals.h"

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
    // The crowd's cards: textures the crowd cameras' renders replace
    // (<zone>_crowd_models[].textureasset, CreateViewportTextureOverride) [C
    // script create_crowd_models]; no crowd yet (M6), so they are hidden.
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
    return true;
}

void VenueStage::update(double ms, float health) {
    if (!venue_) return;
    perf_.record(ms, performance(health));
    advance(ms);
}

void VenueStage::advance(double ms) {
    if (!venue_) return;
    cameras_.update(ms, perf_);
    lights_.update(ms, perf_);
}

void VenueStage::draw(double ms) {
    if (!view_) return;
    lights_.evaluate(ms, lightState_);
    view_->draw(cameras_.pose(ms), lightState_, lights_);
}
