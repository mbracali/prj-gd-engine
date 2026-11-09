// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/rules.h"

#include <algorithm>
#include <stdexcept>

#include "data/dta.h"
#include "data/game_data.h"

namespace {

const char* const kDifficultySymbols[kDifficultyCount] = {
    "kDifficultyEasy", "kDifficultyMedium", "kDifficultyHard", "kDifficultyExpert"};

const DataNode& require(const DataNode& parent, std::string_view key) {
    const DataNode* n = parent.find(key);
    if (!n) throw std::runtime_error("game script is missing '" + std::string(key) + "'");
    return *n;
}

}  // namespace

const char* difficultyName(Difficulty d) {
    static const char* const names[] = {"Easy", "Medium", "Hard", "Expert"};
    return names[static_cast<int>(d)];
}

// 0x822CE5D8 [C]: see rules.h.
int Rules::coopStars(const std::string& song, int score) const {
    auto it = coopMaxScores.find(song);
    if (it == coopMaxScores.end() || coopThresholds.size() < 2) return 5;
    int t[2];
    for (size_t k = 0; k < 2; ++k)
        t[k] = int(coopThresholds[k].second * float(it->second[size_t(std::clamp(coopThresholds[k].first, 0, 3))]));
    if (score < t[1]) return score >= t[0] ? 4 : 3;
    return 5;
}

// GH2 0x822d66b0 [C]: table (0 -> 1) plus scoring.dtb streaks; the last entry
// whose count <= streak.
int Rules::multiplierForStreak(int streak) const {
    int mult = 1;
    for (const auto& [count, value] : streaks) {
        if (streak < count) break;
        mult = value;
    }
    return mult;
}

Rules loadRules(const GameData& data) {
    Rules r;
    RbScoring& rb = r.rb;
    DataNode scoring = parseDtb(data.read("config/gen/scoring.dtb"));

    // (points (<instrument> (head n) (tail n)) ...) [C data]; GH2's single
    // head / tail are the guitar's.
    for (size_t i = 0; i < require(scoring, "points").children.size(); ++i) {
        const DataNode& e = require(scoring, "points").children[i];
        if (!e.isList() || e.children.empty() || !e.children[0].isText()) continue;
        RbScoring::Points pts;
        if (const DataNode* h = e.find("head")) pts.head = h->intAt(1);
        if (const DataNode* t = e.find("tail")) pts.tail = t->intAt(1);
        rb.points[e.children[0].s] = pts;
    }
    if (auto it = rb.points.find("guitar"); it != rb.points.end()) r.headPoints = it->second.head, r.tailPoints = it->second.tail;

    const DataNode& errorClass = require(scoring, "error_class");
    r.goodMs = require(errorClass, "good").intAt(1);
    r.perfectMs = require(errorClass, "perfect").intAt(1);

    if (const DataNode* tug = scoring.find("tug_of_war")) {
        if (const DataNode* m = tug->find("max_delta"); m && m->children.size() > 1 && m->children[1].isList())
            for (int d = 0; d < kDifficultyCount && size_t(d) < m->children[1].children.size(); ++d) rb.tugMaxDelta[size_t(d)] = m->children[1].intAt(size_t(d));
        if (const DataNode* t = tug->find("tie_window"); t && t->children.size() > 1 && t->children[1].isList())
            for (int d = 0; d < kDifficultyCount && size_t(d) < t->children[1].children.size(); ++d) rb.tugTieWindow[size_t(d)] = t->children[1].intAt(size_t(d));
    }
    if (const DataNode* b = scoring.find("bonuses")) {
        if (const DataNode* m = b->find("max_bonus")) rb.maxBonus = m->intAt(1);
        auto list = [](const DataNode* e, std::vector<int>& out) {
            if (e && e->children.size() > 1 && e->children[1].isList())
                for (size_t i = 0; i < e->children[1].children.size(); ++i) out.push_back(e->children[1].intAt(i));
        };
        list(b->find("multiplier"), rb.bandMultiplier);
        list(b->find("crowd_boost"), rb.bandCrowdBoost);
    }

    // (streaks (mechanic ...) (multipliers (<mode> (count mult) ...) ...) (energy (<instrument> (count energy) ...) ...)).
    const DataNode& streaks = require(scoring, "streaks");
    if (const DataNode* m = streaks.find("multipliers"))
        for (size_t i = 1; i < m->children.size(); ++i) {
            const DataNode& e = m->children[i];
            if (!e.isList() || e.children.empty() || !e.children[0].isText()) continue;
            auto& v = rb.streakMultipliers[e.children[0].s];
            for (size_t k = 1; k < e.children.size(); ++k) v.emplace_back(e.children[k].intAt(0), e.children[k].intAt(1));
        }
    if (const DataNode* m = streaks.find("energy"))
        for (size_t i = 1; i < m->children.size(); ++i) {
            const DataNode& e = m->children[i];
            if (!e.isList() || e.children.empty() || !e.children[0].isText()) continue;
            auto& v = rb.streakEnergy[e.children[0].s];
            for (size_t k = 1; k < e.children.size(); ++k) v.emplace_back(e.children[k].intAt(0), e.children[k].floatAt(1));
        }
    if (auto it = rb.streakMultipliers.find("singleplayer"); it != rb.streakMultipliers.end()) r.streaks = it->second;

    if (const DataNode* v = scoring.find("vocals"))
        for (size_t i = 1; i < v->children.size(); ++i) {
            const DataNode& e = v->children[i];
            if (!e.isList() || e.children.empty() || !e.children[0].isText()) continue;
            std::vector<float> nums;
            for (size_t k = 1; k < e.children.size(); ++k) {
                const DataNode& n = e.children[k];
                if (n.type == DataNode::Type::Int) nums.push_back(float(n.i));
                else if (n.type == DataNode::Type::Float) nums.push_back(n.f);
                else if (n.isText()) nums.push_back(n.s == "TRUE" ? 1.0f : 0.0f);
            }
            rb.vocals[e.children[0].s] = nums;
        }
    if (const DataNode* solo = scoring.find("solo")) {
        auto awards = [](const DataNode* block, std::vector<RbScoring::Award>& out) {
            const DataNode* a = block ? block->find("awards") : nullptr;
            if (!a) return;
            for (size_t i = 1; i < a->children.size(); ++i) {
                const DataNode& e = a->children[i];
                if (e.isList() && e.children.size() >= 3) out.push_back({e.intAt(0), e.intAt(1), e.textAt(2)});
            }
        };
        const DataNode* g = solo->find("guitar");
        awards(g, rb.guitarSolo);
        awards(solo->find("tambourine"), rb.tambourine);
        if (g) {
            if (const DataNode* x = g->find("reward")) rb.soloReward = x->floatAt(1);
            if (const DataNode* x = g->find("penalty")) rb.soloPenalty = x->floatAt(1);
        }
    }
    if (const DataNode* u = scoring.find("unison_phrase")) {
        if (const DataNode* x = u->find("reward")) rb.unisonReward = x->floatAt(1);
        if (const DataNode* x = u->find("penalty")) rb.unisonPenalty = x->floatAt(1);
    }

    // (crowd (<difficulty> (default ...) (drum ...) (vocals ...)) (weights ...) ...).
    const DataNode& crowd = require(scoring, "crowd");
    static const char* const kKinds[kCrowdKindCount] = {"default", "drum", "vocals"};
    for (int d = 0; d < kDifficultyCount; ++d) {
        const DataNode& dn = require(crowd, kDifficultySymbols[d]);
        for (int k = 0; k < kCrowdKindCount; ++k) {
            const DataNode& n = require(dn, kKinds[k]);
            CrowdRules& c = rb.crowd[size_t(d)][size_t(k)];
            c.noteWeight = require(n, "note_weight").floatAt(1);
            const DataNode& range = require(n, "range");
            c.rangeLow = range.floatAt(1);
            c.rangeHigh = range.floatAt(2);
            c.freeRide = require(n, "free_ride").floatAt(1);
            c.greatLevel = require(n, "great_level").floatAt(1);
            c.okayLevel = require(n, "okay_level").floatAt(1);
            c.badLevel = require(n, "bad_level").floatAt(1);
            c.warningLevel = require(n, "warning_level").floatAt(1);
            c.loseLevel = require(n, "lose_level").floatAt(1);
            c.phraseWeight = require(n, "phrase_weight").floatAt(1);
            c.initialDisplayLevel = require(n, "initial_display_level").floatAt(1);
        }
        r.solo.crowd[size_t(d)] = rb.crowd[size_t(d)][0];
    }
    if (const DataNode* w = crowd.find("weights"))
        for (size_t i = 1; i < w->children.size(); ++i) {
            const DataNode& e = w->children[i];
            if (!e.isList() || e.children.size() < 2 || !e.children[1].isList()) continue;
            auto& v = rb.crowdWeights[e.intAt(0)];
            for (size_t k = 0; k < e.children[1].children.size(); ++k) v.push_back(e.children[1].floatAt(k));
        }
    if (const DataNode* x = crowd.find("multiplayer_lose_level")) rb.multiplayerLoseLevel = x->floatAt(1);
    if (const DataNode* x = crowd.find("save_level")) rb.saveLevel = x->floatAt(1);
    if (const DataNode* x = crowd.find("time_to_return_from_brink")) rb.timeToReturnFromBrink = x->floatAt(1);
    if (const DataNode* x = crowd.find("vocals_to_return_from_brink")) rb.vocalsToReturnFromBrink = x->intAt(1);
    if (const DataNode* x = crowd.find("crowd_loss_per_sec")) rb.crowdLossPerSec = x->floatAt(1);
    if (const DataNode* x = crowd.find("clap_early_amount_ms")) rb.clapEarlyMs = x->intAt(1);

    // (star_power ...): Overdrive [C data].
    const DataNode& sp = require(scoring, "star_power");
    StarPowerRules& s = r.solo.starPower;
    s.rechargeRate = require(sp, "recharge_rate").floatAt(1);
    s.deployRate = require(sp, "deploy_rate").floatAt(1);
    s.phrase = require(sp, "star_phrase").floatAt(1);
    s.commonPhrase = require(sp, "common_phrase").floatAt(1);
    s.fillBoost = require(sp, "fill_boost").floatAt(1);
    s.whammyRate = require(sp, "whammy_rate").floatAt(1);
    s.readyLevel = require(sp, "ready_level").floatAt(1);
    s.multiplier = require(sp, "multiplier").intAt(1);
    s.crowdBoost = require(sp, "crowd_boost").floatAt(1);
    s.minLength = require(sp, "min_length").floatAt(1);
    s.whammySpeed = require(sp, "whammy_speed").floatAt(1);
    s.whammyTimeout = require(sp, "whammy_timeout").floatAt(1);
    s.crashFillReadyHits = require(sp, "crash_fill_ready_hits").intAt(1);
    r.coop = r.solo;

    if (const DataNode* be = scoring.find("band_energy")) {
        if (const DataNode* x = be->find("deploy_beats")) rb.deployBeats = x->intAt(1);
        if (const DataNode* x = be->find("deploy_bonus")) rb.deployBonus = x->intAt(1);
        if (const DataNode* x = be->find("spotlight_phrase")) rb.spotlightPhrase = x->floatAt(1);
        if (const DataNode* x = be->find("unison_phrase")) rb.unisonPhrase = x->floatAt(1);
        if (const DataNode* x = be->find("deploy_threshold")) rb.deployThreshold = x->floatAt(1);
        if (const DataNode* x = be->find("save_energy")) rb.saveEnergy = x->floatAt(1);
    }
    if (const DataNode* c = scoring.find("coda")) {
        if (const DataNode* x = c->find("point_rate")) rb.codaPointRate = x->intAt(1);
        if (const DataNode* x = c->find("max_mash_ms")) rb.codaMaxMashMs = x->intAt(1);
    }

    // star_thresholds.dtb (scoring.dtb #includes it into star_ratings) [C data].
    if (data.exists("config/gen/star_thresholds.dtb")) {
        DataNode st = parseDtb(data.read("config/gen/star_thresholds.dtb"));
        if (const DataNode* t = st.find("thresholds"))
            for (size_t i = 1; i < t->children.size(); ++i) rb.starThresholds.push_back(t->floatAt(i));
        if (const DataNode* bt = st.find("band_thresholds"))
            for (size_t i = 1; i < bt->children.size(); ++i) {
                const DataNode& e = bt->children[i];
                if (!e.isList() || e.children.empty() || !e.children[0].isList()) continue;
                std::vector<std::string> who;
                for (const DataNode& n : e.children[0].children)
                    if (n.isText()) who.push_back(n.s);
                std::vector<float> v;
                for (size_t k = 1; k < e.children.size(); ++k) v.push_back(e.floatAt(k));
                rb.bandStarThresholds.emplace_back(std::move(who), std::move(v));
            }
    }

    // (watcher (slop 100) ...) [C data]; RB's parser has no hopo_threshold
    // key: songs.dtb gives one per song, else the parser's default (RB
    // 0x824A67A8) [I: 170 kept from GH2 until read].
    DataNode beatmatcher = parseDtb(data.read("config/gen/beatmatcher.dtb"));
    r.slopMs = require(require(beatmatcher, "watcher"), "slop").floatAt(1);
    if (const DataNode* parser = beatmatcher.find("parser"))
        if (const DataNode* h = parser->find("hopo_threshold")) r.hopoThresholdTicks = h->intAt(1);

    DataNode track = parseDtb(data.read("config/gen/track_graphics.dtb"));
    if (const DataNode* speed = track.find("track_speed"))
        for (int d = 0; d < kDifficultyCount; ++d)
            if (const DataNode* v = speed->find(kDifficultySymbols[d])) r.trackSpeed[size_t(d)] = v->floatAt(1);

    return r;
}

Rules rulesForInstrument(const Rules& rules, const std::string& instrument) {
    Rules r = rules;
    if (auto it = rules.rb.points.find(instrument); it != rules.rb.points.end()) r.headPoints = it->second.head, r.tailPoints = it->second.tail;
    const char* table = instrument == "bass" ? "bass" : instrument == "vocals" ? "vocals" : "singleplayer";
    if (auto it = rules.rb.streakMultipliers.find(table); it != rules.rb.streakMultipliers.end()) r.streaks = it->second;
    return r;
}
