// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data/qb.h"

class Globals;

// Guitar Hero: World Tour's Career (the main mode). Rendering-free; the faithful
// port of the disc's own progression mechanic.
//
// Five careers share one structure (scripts\guitar\guitar_progression_*.qb):
// GH4_Career_<Part>_Songs (num_tiers=20; per-tier songs / encore / venue / poster
// / genre / paytoplay) and GH4_Career_<Part>_Progression_Unlock_Order (the
// freegigs that start unlocked and the unlocksets that each gig completion opens).
//
//   * A gig N plays tier N's songs; the gig is "complete" when every song in the
//     tier has > 2 stars at some difficulty (Progression_Career_Check_Gig_Complete).
//   * Completing a gig (SaveGigComplete) marks it started/completed and unlocks its
//     venue (venue_<zone> / <zone>_<part>); the Progression atom
//     (Progression_Career_Gig_Complete) then unlocks the next unlockset's gigs and
//     those tiers' songs.
//   * Winning a song (Progression_SongWon) stores the better stars/score and awards
//     cash through the cash-milestones table (cash_milestones.qb) into band_info.cash
//     — the shop's currency (Step 7.4).
//   * Each completed gig also grants a reward (progression_<part>_rewards,
//     unlockable_content.qb) via progression_add_new_unlocks: set_current_band_part_flags
//     ... unlocked for every car_piece — i.e. the 76 career-gated CAS items the store
//     flagged as a career dependency, plus characters/songs. This closes the 7.4 loop.
//   * Career movies (Bonus_Videos): the ten <part>_intro / <part>_outro cutscenes and
//     the In-GameMontage end-credits; the finale tier carries $end_with_credits.
namespace career {

enum class Part { Band, Guitar, Bass, Drums, Vocals };
constexpr int kNumParts = 5;
const char* partName(Part p);

struct Tier {
    int num = 0;
    std::vector<uint32_t> songs;
    uint32_t encore = 0;       // encorep1 (the tier's encore song), 0 if none
    uint32_t level = 0;        // load_z_* venue key, 0 if none
    uint32_t poster = 0;       // poster_texture
    uint32_t genre = 0;
    int defaultUnlocked = 0;   // songs unlocked from the start (free gigs)
    int paytoplay = 0;         // cash the gig costs to enter (bonus/pay-to-play gigs)
    bool endWithCredits = false;  // $end_with_credits (the career finale)
    bool createAGig = false;      // $createagig
    bool debugGig = false;        // $debug_gig
};

// One gig entry inside an unlockset (gigN / special_eventN / pay_to_playN).
struct GigRef {
    uint32_t name = 0;  // the 'gigN' name key
    int num = 0;        // the tier number it plays
    int forcedPos = -1; // forced_pos, -1 if none
    bool debugDontUnlock = false;
};

// One unlockset: the gigs that open together when the preceding gig completes.
struct UnlockSet {
    bool hasNone = false;  // a $None / None='none' placeholder set
    std::vector<GigRef> gigs;
};

// One progression_<part>_rewards entry — the content a completed gig unlocks.
struct Reward {
    uint32_t groupName = 0;
    std::vector<std::pair<uint32_t, uint32_t>> carPieces;  // (CAS part, desc_id)
    std::vector<uint32_t> characters;
    std::vector<uint32_t> songs;
};

struct Career {
    Part part = Part::Band;
    uint32_t flag = 0;      // Career_Band / Career_Guitar / ...
    uint32_t partKey = 0;   // $band / $Guitar / $Bass / $drum / $Vocals
    std::string prefix;     // "career_band" (gig-name prefix)
    int numTiers = 0;
    std::string initialMovie;  // 'band_intro' ...
    std::string endMovie;      // 'band_outro' ...
    std::vector<Tier> tiers;        // index 1..numTiers (0 unused)
    std::vector<UnlockSet> freegigs;   // freegigs + freegigs_last (unlocked at start)
    std::vector<UnlockSet> unlockSets; // unlockset1..N (each gig completion opens one)
    std::vector<Reward> rewards;       // progression_<part>_rewards
    const Tier* tier(int num) const;
};

// A career movie / cutscene (Bonus_Videos, unlockable_content.qb).
struct Movie {
    uint32_t id = 0;        // $band_intro ...
    std::string name;       // 'band_intro' -> DATA/MOVIES/BIK/band_intro.bik.xen
    bool defaultUnlocked = false;  // the $unlocked flag
};

// Load all five careers (band/guitar/bass/drums/vocals) from the disc globals.
std::vector<Career> load(const Globals& g);
const Career* find(const std::vector<Career>& cs, Part p);

// The Bonus_Videos movie inventory (career cutscenes + credits).
std::vector<Movie> movies(const Globals& g);

// Global-tag key builders (format_globaltag_gigname / _song_checksum).
uint32_t gigNameKey(const Career& c, int gigNum);
uint32_t songChecksumKey(const Career& c, uint32_t song, int diffIdx);

// ------------------------------------------------------------------ state ---
// Career progress lives in named global tags, exactly as the save does
// (save::Tags): gig tags (started/completed/unlocked/cash_earned), per-song
// (song+part+difficulty) star/score tags, and venue_<zone> flags.
using TagStore = std::unordered_map<uint32_t, qb::Struct>;

// What the results screen / judge produced for one won song — the inputs the
// cash-milestones evaluator reads.
struct Performance {
    int difficultyIndex = 4;   // 0 beginner .. 4 expert (index into the cash arrays)
    int stars = 5;             // 3/4/5 (update_song_star_rating)
    int score = 0;
    bool scoreImproved = true; // new score beats the saved score (Score milestone)
    bool perfect = false;      // 100% notes hit (perfect_bonus)
    bool neverInRed = false;   // health never dropped below 2/3 (never_in_red)
    bool neverUsedStarPower = false;
    bool fullStarPower = false;       // ended with >=100% star power
    int noteStreak = 0;        // best note run (note_streak multiplier = best_run/25)
};

// Cash a won song awards via the real cash_milestones table (data-derived; sums
// every earned milestone's cash[difficultyIndex] * multiplier). 0 on a missing table.
int cashForSong(const Globals& g, const Performance& p);

// Progression_Career_<Part>_Init: the career's free gigs (and their tiers' songs)
// start unlocked; a fresh band_info gets cash / career_earnings = 0.
void startCareer(TagStore& st, qb::Struct& bandInfo, const Career& c);

bool gigUnlocked(const TagStore& st, const Career& c, int gigNum);
bool gigStarted(const TagStore& st, const Career& c, int gigNum);
bool gigCompleted(const TagStore& st, const Career& c, int gigNum);
bool songUnlocked(const TagStore& st, const Career& c, uint32_t song);
int songStars(const TagStore& st, const Career& c, uint32_t song);  // best across difficulties
// Progression_Career_Check_Gig_Complete: every tier song has > 2 stars.
bool isGigComplete(const TagStore& st, const Career& c, int gigNum);
int gigsCompleted(const TagStore& st, const Career& c);

// Progression_SongWon: store the better stars/score for (song,part,difficulty),
// mark the song unlocked, and award the cash milestones into band_info.cash.
struct SongWonResult {
    int cashAwarded = 0;
    int newStars = 0;
    bool starsImproved = false;
};
SongWonResult recordSongResult(TagStore& st, qb::Struct& bandInfo, const Globals& g,
                               const Career& c, uint32_t song, const Performance& p);

// SaveGigComplete + Progression_Career_Gig_Complete + progression_add_new_unlocks:
// mark the gig complete, unlock its venue, open the next unlockset, and grant this
// gig's reward (CAS pieces -> band_info unlocked flags, characters, songs).
struct GigCompleteResult {
    bool gigNewlyCompleted = false;
    std::vector<int> unlockedGigs;
    std::vector<uint32_t> unlockedSongs;
    std::vector<std::pair<uint32_t, uint32_t>> unlockedCarPieces;
    std::vector<uint32_t> unlockedCharacters;
    uint32_t venueFlag = 0;  // venue_<zone> key set, 0 if none
    bool endCredits = false; // the finale (tier $end_with_credits, gig complete)
};
GigCompleteResult completeGig(TagStore& st, qb::Struct& bandInfo, const Globals& g,
                              const Career& c, int gigNum);

}  // namespace career
