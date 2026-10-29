// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/match.h"

#include <algorithm>
#include <cmath>

#include "data/qbkey.h"
#include "script/globals.h"

// The rules are docs/modes.md's, each with its source there.
namespace {

uint32_t K(const char* s) { return qbkey::key(s); }
const char* const kDiffKey[4] = {"easy", "medium", "hard", "expert"};

float numField(const Globals& g, const qb::Struct& st, const char* name, float fallback) {
    const qb::Value* v = g.field(st, K(name));
    if (v) v = g.resolve(*v);
    return v && (v->type == qb::Type::Int || v->type == qb::Type::Float) ? v->number() : fallback;
}

// A per-difficulty struct {easy = .. expert = ..}.
std::array<float, 4> perDiff(const Globals& g, const qb::Struct& st, const char* name) {
    std::array<float, 4> out{};
    const qb::Value* v = g.field(st, K(name));
    if (v) v = g.resolve(*v);
    if (v && v->st)
        for (int d = 0; d < 4; ++d) out[size_t(d)] = numField(g, *v->st, kDiffKey[d], 0);
    return out;
}

}  // namespace

const char* gameModeName(GameMode m) {
    switch (m) {
    case GameMode::Solo: return "solo";
    case GameMode::Coop: return "co-op";
    case GameMode::FaceOff: return "face-off";
    case GameMode::ProFaceOff: return "pro face-off";
    case GameMode::Battle: return "battle";
    case GameMode::Boss: return "boss battle";
    }
    return "?";
}

GameMode gameModeFromKey(uint32_t m, bool boss) {
    if (boss) return GameMode::Boss;
    if (m == K("p2_career") || m == K("p2_coop")) return GameMode::Coop;
    if (m == K("p2_faceoff")) return GameMode::FaceOff;
    if (m == K("p2_pro_faceoff")) return GameMode::ProFaceOff;
    if (m == K("p2_battle")) return GameMode::Battle;
    return GameMode::Solo;
}

bool BattleData::load(const Globals& g) {
    powerups.clear();
    const qb::Value* a = g.find(K("battlemode_powerups"));
    if (!a || a->type != qb::Type::Array || !a->arr) return false;
    for (const qb::Value& e : a->arr->values) {
        if (!e.st) continue;
        BattlePowerup p;
        if (const qb::Value* v = g.field(*e.st, K("name"))) p.name = v->key;
        if (const qb::Value* v = g.field(*e.st, K("name_text"))) p.text = v->str;
        if (const qb::Value* v = g.field(*e.st, K("alt_name_text"))) p.altText = v->str;
        if (const qb::Value* v = g.field(*e.st, K("card_texture"))) p.cardTexture = v->key;
        p.weight = int(numField(g, *e.st, "weight", 0));
        p.weightLow = int(numField(g, *e.st, "weight_low", 0));
        p.weightLosing = int(numField(g, *e.st, "weight_losing", 0));
        p.drainMs = int(numField(g, *e.st, "drain_time", 5000));
        p.immediate = numField(g, *e.st, "immediate", 0) != 0;
        p.noAiDamage = numField(g, *e.st, "no_ai_damage", 0) != 0;
        p.fireBolt = numField(g, *e.st, "fire_bolt", 0) != 0;
        for (int d = 0; d < 4; ++d) {
            const std::string dn = kDiffKey[d];
            p.flicker[size_t(d)] = int(numField(g, *e.st, (dn + "_flicker").c_str(), float(p.flicker[size_t(d)])));
            p.repair[size_t(d)] = int(numField(g, *e.st, (dn + "_repair").c_str(), 0));
            p.killRate[size_t(d)] = numField(g, *e.st, (dn + "_kill_rate").c_str(), p.killRate[size_t(d)]);
        }
        powerups.push_back(p);
    }
    for (int d = 0; d < 4; ++d)
        drainOffset[size_t(d)] = int(qbNumber(g, K(("battle_" + std::string(kDiffKey[d]) + "_drain_offset").c_str()), float(drainOffset[size_t(d)])));
    maxCards = std::max(1, qbInt(g, K("max_num_powerups"), 3));
    return powerups.size() == size_t(kAttacks);
}

bool BattleData::loadBoss(const Globals& g, uint32_t key, BossProps& out) const {
    const qb::Value* v = g.find(key);
    if (!v || !v->st) return false;
    out = BossProps{};
    out.key = key;
    if (const qb::Value* n = g.field(*v->st, K("character_name"))) out.name = n->str;
    out.gain = perDiff(g, *v->st, "gainpernote");
    out.loss = perDiff(g, *v->st, "losspernote");
    out.powerUpMissed = perDiff(g, *v->st, "PowerUpMissedNote");
    out.whammySpeed = perDiff(g, *v->st, "WhammySpeed");
    out.brokenSpeed = perDiff(g, *v->st, "BrokenStringSpeed");
    out.brokenMissed = perDiff(g, *v->st, "BrokenStringMissedNote");
    if (const qb::Value* p = g.field(*v->st, K("PowerUps")); p && p->arr)
        for (const qb::Value& k : p->arr->values)
            for (size_t i = 0; i < powerups.size(); ++i)
                if (powerups[i].name == k.key) out.powerUps.push_back(int(i));
    return true;
}

Match::Match() = default;
Match::~Match() = default;

bool Match::start(const Chart& chart, const Tuning& tuning, const Globals* g, const MatchSetup& setup, uint32_t bossProps, std::string& err) {
    setup_ = setup;
    tuning_ = &tuning;
    rng_.seed(setup.seed);
    n_ = setup.mode == GameMode::Solo ? 1 : 2;
    const bool battle = setup.mode == GameMode::Battle || setup.mode == GameMode::Boss;
    if (battle) {
        if (!g || !data_.load(*g)) {
            err = "the battle tables (battlemode_powerups) are missing";
            return false;
        }
        if (setup.mode == GameMode::Boss && !data_.loadBoss(*g, bossProps, boss_)) {
            err = "the boss props " + qbkey::name(bossProps) + " are missing";
            return false;
        }
    }
    for (int i = 0; i < n_; ++i) {
        Side& s = sides_[size_t(i)];
        s.diff = s.curDiff = std::clamp(setup.difficulty[size_t(i)], 0, 3);
        Part part = setup.part[size_t(i)];
        // Co-op on a song with use_coop_notetracks: the co-op parts [C setup_gemarrays].
        if (setup.mode == GameMode::Coop && setup.useCoopTracks) part = part == Part::Rhythm ? Part::RhythmCoop : Part::GuitarCoop;
        if (setup.mode == GameMode::Boss) part = i == 0 ? Part::Guitar : Part::Rhythm;  // bossbattle_init [C]
        if (chart.parts[size_t(part)].notes[size_t(s.diff)].empty() && (part == Part::GuitarCoop || part == Part::RhythmCoop))
            part = part == Part::GuitarCoop ? Part::Guitar : Part::Rhythm;
        if (battle) {
            // Every difficulty's chart, read by the fill as GH3's steppers do (Difficulty Up) [C gem_iterator].
            s.incremental = true;
            int offset = 0;
            s.live = NoteTrack{};
            for (int d = 0; d < 4; ++d) {
                s.src[size_t(d)] = buildTrack(chart, part, d, tuning, setup.hopoScale, true);
                s.phraseOffset[size_t(d)] = offset;
                offset += int(s.src[size_t(d)].phrases.size());
                for (const ChartPhrase& p : s.src[size_t(d)].phrases) s.live.phrases.push_back(p);
            }
            const NoteTrack& base = s.src[size_t(s.diff)];
            s.live.fretbars = base.fretbars, s.live.timesig = base.timesig, s.live.firstBeatMs = base.firstBeatMs;
            s.live.notes.reserve(base.notes.size() * 2);
            s.battle.phraseCard.assign(s.live.phrases.size(), -1);
            scoreInfo(s, base);
            s.phraseCount = int(base.phrases.size());
            if (!base.notes.empty()) lastNoteEnd_ = std::max(lastNoteEnd_, double(base.notes.back().time + base.notes.back().length));
        } else {
            s.live = buildTrack(chart, part, s.diff, tuning, setup.hopoScale);
            if (setup.mode == GameMode::FaceOff) s.live = faceOffTrack(s.live, i == 0 ? chart.faceOffP1 : chart.faceOffP2);
            scoreInfo(s, s.live);
            int last = -1;
            for (const TrackNote& n : s.live.notes)
                if (n.phrase >= 0 && n.phrase != last) last = n.phrase, ++s.phraseCount;
            if (!s.live.notes.empty()) lastNoteEnd_ = std::max(lastNoteEnd_, double(s.live.notes.back().time + s.live.notes.back().length));
        }
        if (s.live.notes.empty() && !s.incremental && setup.mode != GameMode::FaceOff) {
            err = std::string(partName(part)) + " " + difficultyName(s.diff) + " has no notes";
            return false;
        }
        s.player = std::make_unique<Player>(s.live, tuning, s.diff, setup.noFail && !(setup.mode == GameMode::Boss && i == 1),
                                            setup.inputOffsetMs);
    }
    // The rules per mode.
    for (int i = 0; i < n_; ++i) {
        Side& s = sides_[size_t(i)];
        PlayerRules r;
        switch (setup.mode) {
        case GameMode::Solo: break;
        case GameMode::Coop: {
            r.coop = true, r.negativeHealth = true, r.canFail = false;  // the Match fails on the average
            // Invincibility by both difficulties: both easy 20 %, easy / medium 10 % [C calc_health_invincible_time].
            const int a = sides_[0].diff, b = sides_[1].diff;
            r.invinciblePct = (a == 0 && b == 0) ? tuning.healthInvincibleEasy : (a <= 1 && b <= 1) ? tuning.healthInvincibleMedium : 0;
            break;
        }
        case GameMode::FaceOff:
        case GameMode::ProFaceOff:
            r.canFail = false;
            break;
        case GameMode::Battle:
            r.health = HealthMode::Battle, r.starPower = false;
            if (setup.suddenDeath) r.invinciblePct = 0;
            break;
        case GameMode::Boss:
            r.starPower = false;
            if (i == 0) {
                r.health = HealthMode::Boss;
            } else {
                // The boss: its props by current_difficulty (player 1's) x 0.02 [C 0x8210F6A0].
                const size_t d = size_t(sides_[0].diff);
                r.boss = true, r.bossGain = boss_.gain[d] * 0.02f, r.bossLoss = boss_.loss[d] * 0.02f;
                s.bot = true;
            }
            break;
        }
        // Face-off's score scale: (base_p1 + base_p2) x 0.5 / base_p [C 0x8210B9A0].
        if (setup.mode == GameMode::FaceOff && s.simBotScore > 0)
            r.scoreScale = float((sides_[0].simBotScore + sides_[1].simBotScore) * 0.5 / s.simBotScore);
        s.player->setRules(r);
        s.player->setSongLengthMs(lastNoteEnd_ + 3000.0);
        if (setup.mode == GameMode::Battle) s.player->setHealth(setup.startHealth[size_t(i)]);
        if (setup.mode == GameMode::Coop) s.player->setPartner(sides_[size_t(1 - i)].player.get());
        if (setup.autoplay[size_t(i)] && !s.bot) s.autoplay = std::make_unique<Autoplay>(s.live, true);
    }
    for (int i = 0; i < n_; ++i) sides_[size_t(i)].player->setSongLengthMs(lastNoteEnd_ + 3000.0);
    if (setup.mode == GameMode::Boss)
        for (const Chart::TimedScript& t : chart.scripts)
            if (t.script == K("boss_battle_begin_deathlick") && t.time >= setup.startMs) deathLickAt_ = t.time;
    // A mid-song start: the notes before it are not in play (the fill starts
    // there; the judge and autoplay skip them) [C gem_iterator starttime].
    if (setup.startMs > 0)
        for (int i = 0; i < n_; ++i) {
            Side& s = sides_[size_t(i)];
            if (s.incremental) s.filledTo = setup.startMs - 0.5;
            else s.player->startAt(setup.startMs);
            if (s.autoplay) s.autoplay->skipTo(setup.startMs);
        }
    return true;
}

void Match::scoreInfo(Side& s, const NoteTrack& t) {
    // CalcSongScoreInfo: base_score = 50 x frets a note plus its sustain's
    // points (25 a beat); sim_bot_score the same at a perfect run's
    // multiplier (x1 / x2 / x3 / x4 at 10 / 20 / 30); max_notes the notes
    // [C 0x8210AF70, 0x8210B9A0].
    int run = 0;
    for (const TrackNote& n : t.notes) {
        const double pts = 50.0 * n.fretCount() + (n.sustain ? n.maxSustainPoints : 0);
        s.baseScore += pts;
        ++run;
        s.simBotScore += pts * (run < 10 ? 1 : run < 20 ? 2 : run < 30 ? 3 : 4);
    }
    s.maxNotes = int(t.notes.size());
}

uint8_t Match::doubled(uint8_t f, int diff) const {
    // Double Notes: a fret added per GH3's table, by the victim's difficulty [C 0x82104970-0x82104AF4].
    enum : uint8_t { G = 1, R = 2, Y = 4, B = 8, O = 16 };
    const bool easy = diff == 0, medium = diff == 1;
    switch (f) {
    case G: return G | R;
    case R: return R | Y;
    case Y: return easy ? (R | Y) : (Y | B);
    case B: return easy ? B : medium ? (Y | B) : (B | O);
    case O: return (easy || medium) ? O : (Y | O);
    case G | R: return G | R | Y;
    case G | Y: return easy ? (G | R | Y) : (G | Y | B);
    case R | Y: return easy ? (G | R | Y) : (R | Y | B);
    case G | B: return G | R | B;
    case R | B: return medium ? (R | Y | B) : (R | B | O);
    case Y | B: return medium ? (R | Y | B) : (Y | B | O);
    case B | O: return Y | B | O;
    case Y | O: return R | Y | O;
    case R | O: return R | B | O;
    default: return f;
    }
}

void Match::fill(Side& s, int idx, double now) {
    if (!s.incremental) return;
    // GH3's fill_input_array: a note enters the input array (L + 1000) ms
    // before its time, from the chart of the difficulty in force [C
    // 0x821044C8; gem_scroller params].
    const double horizon = now + setup_.travelMs[size_t(idx)] + 1000.0;
    BattleSide& b = s.battle;
    for (;;) {
        const NoteTrack& src = s.src[size_t(s.curDiff)];
        size_t& p = s.pos[size_t(s.curDiff)];
        if (p >= src.notes.size() || src.notes[p].time > horizon) break;
        TrackNote n = src.notes[p++];
        if (n.time <= s.filledTo) continue;  // passed while another difficulty was read
        if (n.phrase >= 0) {
            n.phrase += s.phraseOffset[size_t(s.curDiff)];
            // battlemode_creation_selection: a phrase gem made while it is -1
            // rolls it (battlemode_select / bossbattle_select); every phrase
            // gem carries it as battle_gem, and the hit of the last one awards
            // the last one's. A gem outside the phrases and a completed phrase
            // set it back to -1, so a phrase made before the one ahead of it
            // completes keeps that one's card [C 0x821217A0-0x82121A98,
            // 0x82121B40, 0x8211E870, 0x8211F074].
            if (b.selection < 0) b.selection = select(idx);
            b.phraseCard[size_t(n.phrase)] = b.selection;
        } else {
            b.selection = -1;
        }
        if (b.doubleUntil >= 0) {
            // Double Notes: HOPOs off, a fret added [C 0x821044C8].
            n.frets = doubled(n.frets, s.diff);
            n.hopo = false;
        }
        // Difficulty Up waits for the sustains already on the highway [C 0x821213A8].
        if (n.sustain) b.holdDiffUp = std::max(b.holdDiffUp, double(n.time) + n.length);
        s.live.notes.push_back(n);
        b.flicker.push_back(0);
        s.botMiss.push_back(0);
        s.filledTo = n.time;
    }
}

int Match::select(int who) {
    // battlemode_select / bossbattle_select [C scripts].
    const Side& me = sides_[size_t(who)];
    const int victimDiff = sides_[size_t(other(who))].diff;
    const int top = me.battle.cards.empty() ? -1 : me.battle.cards.back();
    const bool bossRules = setup_.mode == GameMode::Boss;
    std::array<int, kAttacks> w{};
    int total = 0;
    for (int i = 0; i < kAttacks && i < int(data_.powerups.size()); ++i) {
        const BattlePowerup& p = data_.powerups[size_t(i)];
        if (victimDiff == 3 && i == kDifficultyUp) continue;
        if (bossRules && std::find(boss_.powerUps.begin(), boss_.powerUps.end(), i) == boss_.powerUps.end()) continue;
        if (bossRules || me.player->stats().health > 0.5f) w[size_t(i)] = (i == me.battle.lastSelected || i == top) ? p.weightLow : p.weight;
        else w[size_t(i)] = p.weightLosing;
        total += w[size_t(i)];
    }
    if (setup_.suddenDeath && setup_.mode == GameMode::Battle) return kDeathLick;
    if (total <= 0) return 0;
    int r = std::uniform_int_distribution<int>(0, total - 1)(rng_);
    for (int i = 0; i < kAttacks; ++i) {
        r -= w[size_t(i)];
        if (r < 0) {
            sides_[size_t(who)].battle.lastSelected = i;
            return i;
        }
    }
    return 0;
}

void Match::award(int who, int card, double now, bool steal) {
    // battlemode_ready: a stack of max_num_powerups, a full hand loses its top [C script].
    if (card < 0 || card >= int(data_.powerups.size())) return;
    BattleSide& b = sides_[size_t(who)].battle;
    if (int(b.cards.size()) >= data_.maxCards) b.cards.pop_back();
    b.cards.push_back(card);
    ++counters_.cards[size_t(who)];
    if (steal) ++counters_.stolen[size_t(who)];
    // The attack's name, 0.3 s later for 3 s (Lefty: "Righty Flip" while the victim is flipped).
    const BattlePowerup& p = data_.powerups[size_t(card)];
    b.text = (card == kLefty && sides_[size_t(other(who))].battle.leftyGems && !p.altText.empty()) ? p.altText : p.text;
    b.textAt = now + 300;
}

void Match::fire(int who, double now) {
    // battle_trigger_on / bossbattle_trigger_on: the top card on the other player [C scripts].
    BattleSide& b = sides_[size_t(who)].battle;
    if (b.cards.empty()) return;
    const int card = b.cards.back();
    b.cards.pop_back();
    const int victim = other(who);
    const int vdiff = sides_[size_t(victim)].diff;
    const int drain = data_.powerups[size_t(card)].drainMs + (setup_.mode == GameMode::Boss ? 0 : data_.drainOffset[size_t(vdiff)]);
    b.finalBlow = card;
    ++b.attacks;
    // attack_bolt: fire_bolt cards (60 deg), Death Drain's own (80 deg) [C scripts battle_trigger_on, battle_death_lick].
    if (data_.powerups[size_t(card)].fireBolt) b.boltAt = now, b.boltAngle = 60;
    if (card == kDeathLick) b.boltAt = now, b.boltAngle = 80;
    ++counters_.fired[size_t(who)];
    ++counters_.byAttack[size_t(who)][size_t(card)];
    startAttack(victim, card, drain, now);
    if (setup_.mode == GameMode::Boss && who == 0 && !data_.powerups[size_t(card)].noAiDamage) bossDamage(card, drain, now);
}

void Match::startAttack(int v, int card, int drain, double now) {
    Side& s = sides_[size_t(v)];
    BattleSide& b = s.battle;
    const int d = s.diff;
    const BattlePowerup& p = data_.powerups[size_t(card)];
    ++b.received;
    ++counters_.received[size_t(v)];
    b.highwayPushAt = now;  // hammer_highway: every attack [C script battle_trigger_on]
    const double L = setup_.travelMs[size_t(v)];
    auto extend = [&](double& until) { until = until > now ? until + drain : now + drain; };
    switch (card) {
    case kLightning:  // Amp Overload [C battle_lightning]
        extend(b.shakeUntil);
        b.flickerF = p.flicker[size_t(d)];
        break;
    case kDifficultyUp:  // [C battle_up_difficulty]: expert gets Double Notes
        if (d >= 3) {
            extend(b.doubleUntil);
        } else if (b.diffUpWaiting || b.diffUpActive) {
            b.diffUpUntil += drain;
        } else {
            b.diffUpWaiting = true;
            b.diffUpUntil = drain;  // a length until the switch
        }
        break;
    case kDoubleNotes: extend(b.doubleUntil); break;
    case kSteal: b.stealAt = now + 1000; break;  // [C battle_steal: ~1 s of animation]
    case kLefty:  // [C battle_lefty_notes]
        if (b.leftyUntil > now) {
            b.leftyUntil += drain;
        } else if (b.leftyEnd < 0) {
            b.leftyGems = !b.leftyGems;
            b.leftyUntil = now + drain;
            b.leftyButtonsAt = now + L - 500;
        }
        break;
    case kBrokenString: {  // [C battle_broken_string]
        const int lanes = d == 0 ? 3 : d == 1 ? 4 : 5;
        std::vector<int> free;
        for (int k = 0; k < lanes; ++k)
            if (b.broken[size_t(k)] == 0) free.push_back(k);
        if (free.empty())
            for (int k = 0; k < lanes; ++k) free.push_back(k);
        const int k = free[size_t(std::uniform_int_distribution<int>(0, int(free.size()) - 1)(rng_))];
        b.broken[size_t(k)] += p.repair[size_t(d)];
        break;
    }
    case kWhammy:  // [C battle_whammy_attack]
        if (b.whammyLeft < 1) b.whammyLeft = p.repair[size_t(d)];
        else if (b.whammyLeft < 15) b.whammyLeft += p.repair[size_t(d)];
        break;
    case kDeathLick:  // [C battle_death_lick]
        if (b.deathLickRate < 0) b.deathLickRate = p.killRate[size_t(d)], b.deathLickNext = now, b.deathAt = now;
        else b.deathLickRate *= 0.5;
        break;
    default: break;
    }
}

void Match::bossDamage(int card, int drain, double now) {
    // bossbattle_ai_damage [C script].
    Side& bot = sides_[1];
    const size_t d = size_t(sides_[0].diff);
    BossDamage bd;
    bd.card = card;
    if (data_.powerups[size_t(card)].immediate) bd.start = now, bd.startIndex = bot.lastResponse;
    else bd.start = now + setup_.travelMs[1] + 1000.0, bd.startIndex = bot.live.notes.size();
    bd.end = bd.start + drain;
    bd.pct = boss_.powerUpMissed[d];
    if (card == kBrokenString) bd.end = bd.start + 60000, bd.pct = boss_.brokenMissed[d];
    bd.nextAt = bd.start;
    bossDamage_.push_back(bd);
}

void Match::applyBossMisses(double now) {
    Side& bot = sides_[1];
    for (size_t k = 0; k < bossDamage_.size();) {
        BossDamage& bd = bossDamage_[k];
        if (now <= bd.nextAt - 200) {
            ++k;
            continue;
        }
        bool stop = false;
        if (bd.card == kBrokenString) {
            stop = true;
            for (int c : bot.battle.broken) stop = stop && c < 3;
        }
        if (!stop) {
            // ApplyBossBattleGemMisses: pct % of the fret-gems in [start, end) left out [C 0x82103EB8].
            std::vector<std::pair<size_t, uint8_t>> gems;
            for (size_t i = bd.startIndex; i < bot.live.notes.size(); ++i) {
                const TrackNote& n = bot.live.notes[i];
                if (n.time < bd.start || n.time >= bd.end) continue;
                for (int f = 0; f < 5; ++f)
                    if ((n.frets >> f) & 1) gems.push_back({i, uint8_t(1u << f)});
            }
            const float want = float(gems.size()) * bd.pct * 0.01f + bd.fraction;
            int count = int(want);
            bd.fraction = want - float(count);
            std::shuffle(gems.begin(), gems.end(), rng_);
            for (int c = 0; c < count && c < int(gems.size()); ++c) {
                bot.botMiss[gems[size_t(c)].first] |= gems[size_t(c)].second;
                ++counters_.forcedMisses[1];
            }
            bd.start = now + setup_.travelMs[1] + 1000.0;
            bd.nextAt = bd.start;
            bd.startIndex = bot.live.notes.size();
            if (bd.card != kBrokenString && bd.start >= bd.end) stop = true;
        }
        if (stop) bossDamage_.erase(bossDamage_.begin() + std::ptrdiff_t(k));
        else ++k;
    }
}

void Match::bossInput(Side& s, double now, double dtMs, FrameInput& in) {
    // CheckButtonsBoss: each planned note as it reaches the early edge of
    // the window; HOPO runs tapped 4 times, strummed on the 5th; the frets
    // held for the length + 1 s (CheckBossCutoff) [C 0x821096E8, 0x82109A78].
    const double early = tuning_->checkTimeEarly * 1000.0;
    if (s.botNext < s.live.notes.size() && s.live.notes[s.botNext].time <= now + early) {
        const TrackNote& n = s.live.notes[s.botNext];
        s.lastResponse = s.botNext;
        s.botPattern = uint8_t(n.frets & ~s.botMiss[s.botNext]);
        if (n.hopo && s.botHammer < 4) ++s.botHammer, s.botStrum = false;
        else s.botStrum = true, s.botHammer = 0;
        s.botHold = n.length > 60 ? n.length / 1000.0 + 1.0 : 1.0;
        ++s.botNext;
    } else {
        s.botHold -= dtMs / 1000.0;
        if (s.botHold <= 0) s.botPattern = 0;
    }
    in = FrameInput{};
    in.frets = s.botPattern;
    in.strum = s.botStrum;
    s.botStrum = false;
    // The boss's repairs (battle_whammy_attack / battle_broken_string, boss branches) [C].
    BattleSide& b = s.battle;
    const size_t d = size_t(sides_[0].diff);
    if (b.whammyLeft > 0) {
        if (now - s.botWhammyAt >= boss_.whammySpeed[d]) s.botWhammyAt = now, in.whammy = 1;
    } else if (b.brokenMask && now - s.botBrokenAt >= boss_.brokenSpeed[d]) {
        s.botBrokenAt = now;
        in.frets = b.brokenMask;  // each broken fret pressed once
        b.lastRaw = 0;
    }
    // Boss firing: a first card after 100 frames, a second at once [C 0x82108948].
    in.starPower = false;
    const size_t cards = b.cards.size();
    if (cards == 0) s.botDelay = 100;
    else if (cards == 1) {
        if (s.botDelay > 0) --s.botDelay;
        else in.starPower = true;
    } else if (cards == 2) {
        in.starPower = true;
    }
    // A press (the trigger fires on the edge): released every other frame.
    if (in.starPower && s.lastIn.starPower) in.starPower = false;
}

bool Match::gemHidden(int side, size_t note) const {
    const BattleSide& b = sides_[size_t(side)].battle;
    return note < b.flicker.size() && b.flicker[note] > 0;
}

void Match::battleFrame(double now, double dt, std::array<FrameInput, 2>& in) {
    (void)dt;
    for (int i = 0; i < n_; ++i) {
        Side& s = sides_[size_t(i)];
        BattleSide& b = s.battle;
        // Amp Overload: every 3 frames the gems flicker in runs of [F, 2F-1] [C 0x821195C0].
        if (b.shakeUntil >= 0) {
            if (now > b.shakeUntil) {
                b.shakeUntil = -1;
                std::fill(b.flicker.begin(), b.flicker.end(), int8_t(0));
            } else if (++b.flickerFrame % 3 == 0) {
                const int F = std::max(1, b.flickerF);
                std::uniform_int_distribution<int> r(0, F - 1);
                for (size_t k = s.player->current(); k < b.flicker.size(); ++k) {
                    int8_t& c = b.flicker[k];
                    if (c == 0) c = int8_t(std::uniform_int_distribution<int>(0, 1)(rng_) ? F + r(rng_) : -(F + r(rng_)));
                    if (c > 0) {
                        if (--c == 0) c = int8_t(-(F + r(rng_)));
                    } else if (c < 0) {
                        if (++c == 0) c = int8_t(F + r(rng_));
                    }
                }
            }
        }
        if (b.doubleUntil >= 0 && now > b.doubleUntil) b.doubleUntil = -1;
        // Difficulty Up: the switch after the held sustains, back after the drain [C battle_up_difficulty].
        if (b.diffUpWaiting && now > b.holdDiffUp) {
            b.diffUpWaiting = false, b.diffUpActive = true;
            s.curDiff = std::min(3, s.diff + 1);
            b.diffUpUntil = now + b.diffUpUntil;
        }
        if (b.diffUpActive && now > b.diffUpUntil) b.diffUpActive = false, s.curDiff = s.diff, b.diffUpUntil = -1;
        // Lefty Flip: gems at once, the buttons a highway later, both back after the drain [C battle_lefty_notes].
        if (b.leftyButtonsAt >= 0 && now > b.leftyButtonsAt) b.leftyButtons = !b.leftyButtons, b.leftyButtonsAt = -1;
        if (b.leftyUntil >= 0 && now > b.leftyUntil) {
            b.leftyUntil = -1;
            b.leftyGems = !b.leftyGems;
            b.leftyEnd = now + setup_.travelMs[size_t(i)];
        }
        if (b.leftyEnd >= 0 && now > b.leftyEnd - 500) {
            if (b.leftyButtonsAt >= 0) b.leftyButtonsAt = -1;
            else b.leftyButtons = !b.leftyButtons;
            b.leftyEnd = -1;
        }
        // Steal: the victim's top card after the animation [C battle_steal].
        if (b.stealAt >= 0 && now > b.stealAt) {
            b.stealAt = -1;
            BattleSide& thief = sides_[size_t(other(i))].battle;
            (void)thief;
            if (!b.cards.empty()) {
                const int c = b.cards.back();
                b.cards.pop_back();
                award(other(i), c, now, true);
            }
        }
        // Broken String: each broken fret pressed down N times (no strum) [C update_broken_button].
        const uint8_t raw = in[size_t(i)].frets;
        const uint8_t pressed = uint8_t(raw & ~b.lastRaw);
        b.lastRaw = raw;
        for (int k = 0; k < 5; ++k)
            if (b.broken[size_t(k)] > 0 && ((pressed >> k) & 1))
                if (--b.broken[size_t(k)] == 0) ++counters_.repaired[size_t(i)];
        // Whammy: all frets dead until whammied past half N times [C battle_whammy_attack].
        const bool up = in[size_t(i)].whammy >= 0.5f;
        if (b.whammyLeft > 0 && up && b.whammyUp) {
            if (--b.whammyLeft == 0) b.whammyLeft = -1, ++counters_.repaired[size_t(i)];
        }
        b.whammyUp = !up;
        b.brokenMask = 0;
        for (int k = 0; k < 5; ++k)
            if (b.broken[size_t(k)] > 0) b.brokenMask |= uint8_t(1u << k);
        s.player->setDeadFrets(uint8_t(b.brokenMask | (b.whammyLeft > 0 ? 0x1F : 0)));
        // Death Drain: a miss's worth of health every kill_rate s until the victim dies [C battle_death_lick].
        if (b.deathLickRate > 0 && now >= b.deathLickNext && s.player->stats().health > 0) {
            s.player->damage(now);
            b.deathLickNext = now + b.deathLickRate * 1000.0;
        }
    }
    if (setup_.mode == GameMode::Boss) {
        // The boss song's scripted Death Drain on the player (0.1 s) [C boss_battle_begin_deathlick].
        if (deathLickAt_ >= 0 && now >= deathLickAt_) {
            BattleSide& b = sides_[0].battle;
            if (b.deathLickRate < 0) b.deathLickRate = 0.1, b.deathLickNext = now, b.deathAt = now;
            sides_[1].battle.boltAt = now, sides_[1].battle.boltAngle = 80;
            deathLickAt_ = -1;
        }
        applyBossMisses(now);
    }
}

void Match::frame(double now, double dt, const std::array<FrameInput, 2>& live) {
    std::array<FrameInput, 2> in = live;
    for (int i = 0; i < n_; ++i) fill(sides_[size_t(i)], i, now);
    for (int i = 0; i < n_; ++i) {
        Side& s = sides_[size_t(i)];
        if (s.bot) bossInput(s, now, dt, in[size_t(i)]);
        else if (s.autoplay) {
            in[size_t(i)] = s.autoplay->input(now, dt, *s.player);
            if (battleMode()) {
                // Autoplay fires its cards as they come (a press every other
                // frame) and repairs as a player would: the broken frets
                // pressed, the whammy worked (every other frame).
                in[size_t(i)].starPower = setup_.autoplayFires[size_t(i)] && !s.battle.cards.empty() && !s.lastIn.starPower;
                const bool odd = (s.battle.flickerFrame++ & 1) != 0;
                if (s.battle.brokenMask && odd) in[size_t(i)].frets |= s.battle.brokenMask;
                if (s.battle.whammyLeft > 0) in[size_t(i)].whammy = odd ? 1.0f : 0.0f;
            } else if (setup_.mode == GameMode::Coop) {
                // Co-op autoplay: raise the axe when both can (the prompt's rule).
                in[size_t(i)].starPower = s.player->stats().starPower >= 50.0f && !s.player->stats().starPowerActive && !s.lastIn.starPower;
            }
        }
    }
    if (battleMode()) battleFrame(now, dt, in);
    for (int i = 0; i < n_; ++i) {
        Side& s = sides_[size_t(i)];
        s.player->frame(now, dt, in[size_t(i)]);
        s.lastIn = in[size_t(i)];
    }
    // Co-op Star Power: a trigger readies its player and shows the raise-axe
    // prompt; both ready while it shows: Star Power for both [C 0x82112850, 0x821131E4].
    if (setup_.mode == GameMode::Coop) {
        // The ready flags drop when the prompt ends [C 0x82112C30].
        if (raiseAxeStart_ >= 0 && !raiseAxeShowing(now)) starReady_ = {false, false}, raiseAxeStart_ = -1;
        for (int i = 0; i < 2; ++i)
            for (const PlayerEvent& e : sides_[size_t(i)].player->events())
                if (e.kind == PlayerEvent::Kind::Trigger && sides_[size_t(i)].player->stats().starPower >= 50.0f &&
                    !sides_[size_t(i)].player->stats().starPowerActive) {
                    starReady_[size_t(i)] = true;
                    if (raiseAxeStart_ < 0) raiseAxeStart_ = now, ++counters_.raiseAxe;
                }
        if (starReady_[0] && starReady_[1] && raiseAxeStart_ >= 0) {
            sides_[0].player->activateStarPower(now);
            sides_[1].player->activateStarPower(now);
            ++counters_.coopActivations;
            starReady_ = {false, false};
            raiseAxeStart_ = -1;
        }
    }
    if (battleMode()) {
        for (int i = 0; i < n_; ++i) {
            Side& s = sides_[size_t(i)];
            for (const PlayerEvent& e : s.player->events()) {
                if (e.kind == PlayerEvent::Kind::BattlePhrase && e.note >= 0) {
                    const int ph = s.live.notes[size_t(e.note)].phrase;
                    if (ph >= 0) award(i, s.battle.phraseCard[size_t(ph)], now, false);
                    s.battle.selection = -1;  // battlemode_creation_selection [C 0x8211E870]
                } else if (e.kind == PlayerEvent::Kind::Trigger) {
                    fire(i, now);
                }
            }
        }
    }
    // Decided early?
    if (outcome_ == Outcome::None) {
        const float h1 = sides_[0].player->stats().health, h2 = n_ > 1 ? sides_[1].player->stats().health : 1.0f;
        switch (setup_.mode) {
        case GameMode::Solo:
            if (sides_[0].player->stats().failed) outcome_ = Outcome::Failed;
            break;
        case GameMode::Coop:
            // Fail on the average [C 0x8210FD18].
            if (!setup_.noFail && (h1 + h2) * 0.5f <= 0) outcome_ = Outcome::Failed;
            break;
        case GameMode::Battle:
            // KO: a player at 0 and the healths differ [C 0x8210FD18, GuitarEvent_SongFailed].
            if ((h1 <= 0 || h2 <= 0) && h1 != h2) outcome_ = h2 > h1 ? Outcome::P2Wins : Outcome::P1Wins;
            break;
        case GameMode::Boss:
            if (h2 <= 0) outcome_ = Outcome::P1Wins;  // song_wonp2 [C]
            else if (h1 <= 0 && !setup_.noFail) outcome_ = Outcome::Failed;
            break;
        default: break;
        }
    }
}

int Match::winnerAtEnd() const {
    if (n_ < 2) return 0;
    if (setup_.mode == GameMode::FaceOff || setup_.mode == GameMode::ProFaceOff) {
        const int64_t a = sides_[0].player->stats().score, b = sides_[1].player->stats().score;
        return a > b ? 0 : b > a ? 1 : -1;
    }
    if (setup_.mode == GameMode::Battle || setup_.mode == GameMode::Boss)
        return sides_[1].player->stats().health > sides_[0].player->stats().health ? 1 : 0;
    return 0;
}
