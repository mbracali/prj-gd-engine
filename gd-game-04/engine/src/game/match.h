// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "game/autoplay.h"
#include "game/chart.h"
#include "game/note_track.h"
#include "game/player.h"
#include "game/tuning.h"

class Globals;

// GH3's game modes (game_mode; boss songs set boss_battle) [C scripts
// main_menu_select_*, mp_select_mode_menu_select_mode, start_gem_scroller].
enum class GameMode : uint8_t { Solo, Coop, FaceOff, ProFaceOff, Battle, Boss };
const char* gameModeName(GameMode m);
// From game_mode's key (p2_career / p2_coop: co-op, p2_faceoff, p2_pro_faceoff, p2_battle) and boss_battle.
GameMode gameModeFromKey(uint32_t gameMode, bool boss);

// battlemode_powerups (guitar_battle.qb) [C data].
struct BattlePowerup {
    uint32_t name = 0;  // Lightning, DifficultyUp, DoubleNotes, PowerUpSteal, LeftyNotes, BrokenString, WhammyAttack, DeathLick
    std::string text, altText;
    int weight = 0, weightLow = 0, weightLosing = 0;
    int drainMs = 5000;
    std::array<int, 4> flicker{2, 2, 3, 3}, repair{0, 0, 0, 0};
    std::array<float, 4> killRate{0.4f, 0.3f, 0.25f, 0.2f};
    bool immediate = false, noAiDamage = false, fireBolt = false;
    uint32_t cardTexture = 0;
};
enum Attack : int { kLightning, kDifficultyUp, kDoubleNotes, kSteal, kLefty, kBrokenString, kWhammy, kDeathLick, kAttacks };

// A boss's props (Boss_TomMorello_Props ...) [C data guitar_boss.qb].
struct BossProps {
    uint32_t key = 0;
    std::string name;
    std::array<float, 4> gain{}, loss{}, powerUpMissed{}, whammySpeed{}, brokenSpeed{}, brokenMissed{};
    std::vector<int> powerUps;  // indices into the powerups table
};

struct BattleData {
    std::vector<BattlePowerup> powerups;
    std::array<int, 4> drainOffset{0, 1500, 3000, 4500};  // battle_*_drain_offset [C data]
    int maxCards = 3;                                      // max_num_powerups [C data]
    bool load(const Globals& g);
    bool loadBoss(const Globals& g, uint32_t props, BossProps& out) const;
};

struct MatchSetup {
    GameMode mode = GameMode::Solo;
    int players = 1;
    std::array<Part, 2> part{Part::Guitar, Part::Guitar};
    std::array<int, 2> difficulty{3, 3};
    std::array<bool, 2> autoplay{false, false};
    std::array<bool, 2> autoplayFires{true, true};  // battle: autoplay fires its cards (walks: player 2 holds them in sudden death)
    std::array<double, 2> travelMs{1500, 1500};  // each highway's scroll_time - destroy_time (the fill look-ahead)
    float hopoScale = 2.95f;
    bool noFail = false;
    bool useCoopTracks = false;                    // the song's use_coop_notetracks [C setup_gemarrays]
    // Battle: sudden death (every new card Death Drain), the saved health [C GuitarEvent_SongWon].
    bool suddenDeath = false;
    std::array<float, 2> startHealth{1.0f, 1.0f};
    uint32_t seed = 0x47483307;
};

// The players of one song and the rules between them: co-op's shared run
// and Star Power, face-off's sections and score scale, battle's cards and
// attacks, the boss bot. Rendering-free (L17); docs/modes.md.
class Match {
public:
    Match();
    ~Match();
    // `g` gives the battle tables and boss props (null: no battle data).
    bool start(const Chart& chart, const Tuning& tuning, const Globals* g, const MatchSetup& setup, uint32_t bossProps, std::string& err);
    // One 60 Hz game frame; `in` the players' live input (ignored for autoplay / the bot).
    void frame(double nowMs, double dtMs, const std::array<FrameInput, 2>& in);

    int players() const { return n_; }
    // The players' events of the last frame stay until this (the caller reads them first).
    void clearEvents() {
        for (int i = 0; i < n_; ++i) sides_[size_t(i)].player->clearEvents();
    }
    GameMode mode() const { return setup_.mode; }
    const MatchSetup& setup() const { return setup_; }
    Player& player(int i) { return *sides_[size_t(i)].player; }
    const Player& player(int i) const { return *sides_[size_t(i)].player; }
    const NoteTrack& track(int i) const { return sides_[size_t(i)].live; }
    // The input the judge got last frame (the frets shown held on the highway).
    const FrameInput& shownInput(int i) const { return sides_[size_t(i)].lastIn; }
    // The last note time + its length of either player's full chart (ms).
    double lastNoteEndMs() const { return lastNoteEnd_; }
    // Per player: the base score (CalcSongScoreInfo), Star Power phrases and notes of the full chart.
    double baseScore(int i) const { return sides_[size_t(i)].baseScore; }
    int phraseCount(int i) const { return sides_[size_t(i)].phraseCount; }
    int maxNotes(int i) const { return sides_[size_t(i)].maxNotes; }

    // The song is decided before its end: a co-op fail, a battle KO, a boss beaten / the player beaten.
    enum class Outcome { None, Failed, P1Wins, P2Wins };
    Outcome outcome() const { return outcome_; }
    // At the song's end: who won (face-off by score, battle by health) [C GuitarEvent_SongWon_Spawned].
    int winnerAtEnd() const;

    // -- co-op Star Power: the raise-axe prompt (show_coop_raise_axe_for_starpower) [C 0x82112850].
    double raiseAxeStart() const { return raiseAxeStart_; }
    bool raiseAxeShowing(double now) const { return raiseAxeStart_ >= 0 && now < raiseAxeStart_ + kRaiseAxeMs; }
    static constexpr double kRaiseAxeMs = 1240;

    // -- battle state (the HUD and highway read it)
    struct BattleSide {
        std::vector<int> cards;  // the hand, a stack (top = back)
        int lastSelected = -1, finalBlow = -1, attacks = 0, received = 0;
        double shakeUntil = -1, doubleUntil = -1, leftyUntil = -1, stealAt = -1;
        double diffUpUntil = -1, holdDiffUp = 0;
        bool diffUpWaiting = false, diffUpActive = false;
        bool leftyGems = false;   // gems drawn mirrored (lefthanded_gems)
        double leftyButtonsAt = -1, leftyBackAt = -1;
        bool leftyButtons = false;  // the fret buttons mirrored (lefthanded_button_ups)
        double leftyEnd = -1;
        std::array<int, 5> broken{};  // presses left per broken fret
        uint8_t brokenMask = 0;
        int whammyLeft = -1;
        bool whammyUp = true;
        double deathLickRate = -1, deathLickNext = -1;
        int flickerF = 0;
        std::vector<int8_t> flicker;  // per live note: >0 hidden for that many flicker events
        int flickerFrame = 0;
        std::vector<int> phraseCard;  // the card rolled for each phrase (-1 none)
        int selection = -1;
        uint8_t lastRaw = 0;
        double highwayPushAt = -1e9;  // hammer_highway (the attack hit)
        std::string text;             // the attack name shown
        double textAt = -1e9;
    };
    // One player: the charts, the live input array, the judge, the bot.
    struct Side {
        std::array<NoteTrack, 4> src;  // battle / boss: every difficulty (Difficulty Up)
        bool incremental = false;      // battle / boss: notes enter `live` as the fill window reaches them
        NoteTrack live;
        std::array<size_t, 4> pos{};
        std::array<int, 4> phraseOffset{};
        double filledTo = -1e18;
        int diff = 3, curDiff = 3;
        std::unique_ptr<Player> player;
        std::unique_ptr<Autoplay> autoplay;
        FrameInput lastIn;
        double baseScore = 0, simBotScore = 0;
        int phraseCount = 0, maxNotes = 0;
        BattleSide battle;
        // The boss bot (CheckButtonsBoss 0x821096E8) [C].
        bool bot = false;
        size_t botNext = 0, lastResponse = 0;
        uint8_t botPattern = 0;
        bool botStrum = false;
        int botHammer = 0, botDelay = 100;
        double botHold = 0, botWhammyAt = 0, botBrokenAt = 0;
        std::vector<uint8_t> botMiss;  // per live note: frets the bot leaves out (ApplyBossBattleGemMisses)
    };
    const BattleSide& battle(int i) const { return sides_[size_t(i)].battle; }
    bool battleMode() const { return setup_.mode == GameMode::Battle || setup_.mode == GameMode::Boss; }
    const BattleData& battleData() const { return data_; }
    const BossProps& boss() const { return boss_; }
    // A live note hidden by Amp Overload's flicker [C 0x821195C0].
    bool gemHidden(int side, size_t note) const;
    // Counters (walks): attacks fired / received per player, cards awarded.
    struct Counters {
        std::array<int, 2> fired{}, received{}, cards{}, stolen{}, repaired{}, forcedMisses{};
        std::array<std::array<int, kAttacks>, 2> byAttack{};
        int suddenDeaths = 0, raiseAxe = 0, coopActivations = 0;
    };
    const Counters& counters() const { return counters_; }

private:
    void fill(Side& s, int idx, double now);
    void battleFrame(double now, double dt, std::array<FrameInput, 2>& in);
    void award(int who, int card, double now, bool steal);
    void fire(int who, double now);
    int select(int who);
    void startAttack(int victim, int card, int drainMs, double now);
    void bossInput(Side& s, double now, double dt, FrameInput& in);
    void bossDamage(int card, int drainMs, double now);
    void applyBossMisses(double now);
    int other(int i) const { return 1 - i; }
    uint8_t doubled(uint8_t frets, int diff) const;
    static void scoreInfo(Side& s, const NoteTrack& t);

    MatchSetup setup_;
    const Tuning* tuning_ = nullptr;
    std::array<Side, 2> sides_;
    int n_ = 1;
    BattleData data_;
    BossProps boss_;
    Outcome outcome_ = Outcome::None;
    double lastNoteEnd_ = 0;
    double raiseAxeStart_ = -1;
    std::array<bool, 2> starReady_{};
    std::mt19937 rng_;
    Counters counters_;
    double deathLickAt_ = -1;  // the boss song's scripted Death Drain (boss_battle_begin_deathlick)
    struct BossDamage {
        int card = 0;
        double start = 0, end = 0, nextAt = 0;
        size_t startIndex = 0;
        float pct = 0, fraction = 0;
    };
    std::vector<BossDamage> bossDamage_;
};
