// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>

class Globals;

// GH3's gameplay tuning, read from the QB globals (scripts/guitar/
// guitar_tweaks.qb) [C data]; what each value does is in
// docs/FINDINGS.md "Gameplay". Times in seconds as the data has them.
enum class HealthMode { Normal, Battle, Boss };

struct Tuning {
    float checkTimeEarly = 0, checkTimeLate = 0;
    float hammerTimeEarly = 0, hammerTimeLate = 0;
    float splitLatePercent = 0;
    float defaultHammerOnMeasureScale = 0;
    float strumBeforeForming = 0, formBeforeStrumming = 0, ignoreStrumsAfterHammers = 0;
    bool disallowToleranceStrums = false, clearHistoryOnHit = false;
    int xenonGemOffset = 0, xenonInputOffset = 0;  // ms
    float whammyShorten = 0, whammyCutoff = 0, whammyMinHoldPct = 0, whammyMinHoldMuteMs = 0, whammyMinHoldPctShort = 0;
    // health_change_{bad,good,star}[_battle|_boss]_{easy..expert}
    std::array<float, 4> healthBad{}, healthGood{}, healthStar{};
    std::array<float, 4> healthBadBattle{}, healthGoodBattle{}, healthBadBoss{}, healthGoodBoss{};
    float healthPoorMedium = 0, healthMediumGood = 0, healthRedMulti = 0, healthGreenMulti = 0;
    float healthInvincibleEasy = 0, healthInvincibleMedium = 0, healthInvincibleMinimum = 0;
    float starPowerWhammyAdd = 0, starPowerWhammyAddCoop = 0, starPowerSequenceBonus = 0;
    float starPowerDrainRate = 0, starPowerDrainRateCoop = 0;
    int starPowerCoopTriggerMs = 0, starPowerTriggerFrames = 0;
    float starTiltThreshold = 0;

    // Reads every value; returns false (and logs the name) when one is missing.
    bool load(const Globals& g);
};
