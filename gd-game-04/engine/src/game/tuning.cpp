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
    integer("xenon_gem_offset", xenonGemOffset);
    integer("xenon_input_offset", xenonInputOffset);
    num("whammy_shorten", whammyShorten);
    num("whammy_cutoff", whammyCutoff);
    num("whammy_min_hold_pct", whammyMinHoldPct);
    num("whammy_min_hold_mute_ms", whammyMinHoldMuteMs);
    num("whammy_min_hold_pct_short", whammyMinHoldPctShort);
    static const char* const kDiff[4] = {"easy", "medium", "hard", "expert"};
    for (int d = 0; d < 4; ++d) {
        const std::string s = kDiff[d];
        num("health_change_bad_" + s, healthBad[size_t(d)]);
        num("health_change_good_" + s, healthGood[size_t(d)]);
        num("health_change_star_" + s, healthStar[size_t(d)]);
        num("health_change_bad_battle_" + s, healthBadBattle[size_t(d)]);
        num("health_change_good_battle_" + s, healthGoodBattle[size_t(d)]);
        num("health_change_bad_boss_" + s, healthBadBoss[size_t(d)]);
        num("health_change_good_boss_" + s, healthGoodBoss[size_t(d)]);
    }
    num("health_poor_medium", healthPoorMedium);
    num("health_medium_good", healthMediumGood);
    num("health_change_red_multi", healthRedMulti);
    num("health_change_green_multi", healthGreenMulti);
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
    num("star_tilt_threshold", starTiltThreshold);
    return ok;
}
