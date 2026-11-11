// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/tuning.h"

#include <SDL3/SDL_log.h>

#include <string>

#include "data/qbkey.h"
#include "script/globals.h"

bool Tuning::load(const Globals& g) {
    bool ok = true;
    auto num = [&](const std::string& name, float& out) {
        const qb::Value* v = g.find(qbkey::key(name));
        if (!v || (v->type != qb::Type::Float && v->type != qb::Type::Int)) {
            SDL_Log("tuning: %s missing", name.c_str());
            ok = false;
            return;
        }
        out = v->number();
    };
    auto integer = [&](const std::string& name, int& out) {
        float f = 0;
        num(name, f);
        out = int(f);
    };
    auto flag = [&](const std::string& name, bool& out) {
        float f = 0;
        num(name, f);
        out = f != 0;
    };
    num("check_time_early", checkTimeEarly);
    num("check_time_late", checkTimeLate);
    num("hammer_time_early", hammerTimeEarly);
    num("hammer_time_late", hammerTimeLate);
    num("split_late_percent", splitLatePercent);
    num("default_hammer_on_measure_scale", defaultHammerOnMeasureScale);
    num("strum_before_forming", strumBeforeForming);
    num("form_before_strumming", formBeforeStrumming);
    num("ignore_strums_after_hammers", ignoreStrumsAfterHammers);
    flag("disallow_tolerance_strums", disallowToleranceStrums);
    flag("clear_history_on_hit", clearHistoryOnHit);
    // gh4 (Metallica): default_lag_settings.xenon {gem_lag_ms input_lag_ms}
    // (-45 / -61) replace GH3's xenon_gem_offset / xenon_input_offset [C data
    // guitar_gems.qb; I: the same use].
    auto sub = [&](const qb::Value* v, const char* field) -> const qb::Value* {
        return v && v->type == qb::Type::Struct && v->st ? g.field(*v->st, qbkey::key(field)) : nullptr;
    };
    if (const qb::Value* x = sub(g.find(qbkey::key("default_lag_settings")), "xenon")) {
        if (const qb::Value* v = sub(x, "gem_lag_ms")) xenonGemOffset = int(v->number());
        if (const qb::Value* v = sub(x, "input_lag_ms")) xenonInputOffset = int(v->number());
    } else {
        integer("xenon_gem_offset", xenonGemOffset);
        integer("xenon_input_offset", xenonInputOffset);
    }
    num("whammy_shorten", whammyShorten);
    num("whammy_cutoff", whammyCutoff);
    num("whammy_min_hold_pct", whammyMinHoldPct);
    num("whammy_min_hold_mute_ms", whammyMinHoldMuteMs);
    num("whammy_min_hold_pct_short", whammyMinHoldPctShort);
    static const char* const kDiff[4] = {"easy", "medium", "hard", "expert"};
    // gh4: health_change_<kind> = {guitar = [5] bass drum vocals}, by
    // difficulty beginner .. expert (our easy .. expert are 1 .. 4) [C data
    // guitar_gems.qb; I: the index order]. GH3's per-difficulty scalars as a fallback.
    auto table = [&](const std::string& name, int d, float& out) {
        const qb::Value* v = sub(g.find(qbkey::key(name)), "guitar");
        if (v && v->type == qb::Type::Array && v->arr && v->arr->values.size() >= 5) {
            out = v->arr->values[size_t(d + 1)].number();
            return true;
        }
        return false;
    };
    for (int d = 0; d < 4; ++d) {
        const std::string s = kDiff[d];
        auto either = [&](const std::string& base, float& out) {
            if (!table(base, d, out)) num(base + "_" + s, out);
        };
        either("health_change_bad", healthBad[size_t(d)]);
        either("health_change_good", healthGood[size_t(d)]);
        either("health_change_star", healthStar[size_t(d)]);
        either("health_change_bad_battle", healthBadBattle[size_t(d)]);
        either("health_change_good_battle", healthGoodBattle[size_t(d)]);
        either("health_change_bad_boss", healthBadBoss[size_t(d)]);
        either("health_change_good_boss", healthGoodBoss[size_t(d)]);
    }
    num("health_poor_medium", healthPoorMedium);
    num("health_medium_good", healthMediumGood);
    if (!table("health_change_red_multi", 3, healthRedMulti)) num("health_change_red_multi", healthRedMulti);
    if (!table("health_change_green_multi", 3, healthGreenMulti)) num("health_change_green_multi", healthGreenMulti);
    num("health_invincible_time_percentage_easy", healthInvincibleEasy);
    num("health_invincible_time_percentage_medium", healthInvincibleMedium);
    num("health_invincible_minimum", healthInvincibleMinimum);
    num("star_power_whammy_add", starPowerWhammyAdd);
    num("star_power_whammy_add_coop", starPowerWhammyAddCoop);
    num("star_power_sequence_bonus", starPowerSequenceBonus);
    num("star_power_drain_rate", starPowerDrainRate);
    num("star_power_drain_rate_coop", starPowerDrainRateCoop);
    integer("star_power_coop_trigger_ms", starPowerCoopTriggerMs);
    integer("star_power_trigger_frames", starPowerTriggerFrames);
    // gh4: player_shared_defaults.star_tilt_threshold [C data].
    if (const qb::Value* v = sub(g.find(qbkey::key("player_shared_defaults")), "star_tilt_threshold")) starTiltThreshold = v->number();
    else num("star_tilt_threshold", starTiltThreshold);
    return ok;
}
