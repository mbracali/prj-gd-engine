// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "ui/career.h"

// GH2's career rules (docs/career_and_store.md): the Campaign object
// (handler 0x82287F58), its config (CampaignData at Campaign +0x2c,
// 0x822A3910: config/campaign.dta, store.dta's songs, the downloads), the
// profiles' per-difficulty records (0x822AAD68) and unlock table (+0x38,
// 0x822A88C8), on the save's data (ui/career.h). Addresses are default.xex's.
namespace ui {

class UiManager;

namespace campaign {

struct Config {
    // (order (battle surrender ...) ...): the venues and their songs; the
    // last song of each is its encore (0x822A9FA0).
    std::vector<std::pair<std::string, std::vector<std::string>>> tiers;
    std::vector<std::string> storeSongs, downloads;  // store.dta (song ...), (downloads ...)
    // (required_songs (kDifficultyEasy 4 5) ...): the songs a venue needs,
    // the last tier's second (0x822A3468).
    std::array<std::array<int, 2>, Career::kDifficulties> required{};
    int startingCash = 0;  // (cash (starting 50))
    // (cash (star_awards (kDifficultyMedium 150 200 250))): 3, 4, 5 stars
    // (0x822A2838); (status_awards (... 100 200 ...)): status 1.. (0x822A28B8).
    std::array<std::array<int, 3>, Career::kDifficulties> starAwards{};
    std::array<std::vector<int>, Career::kDifficulties> statusAwards;
    // store.dta: per category (guitar skin song character outfit video)
    // its items and prices, in order.
    std::map<std::string, std::vector<std::pair<std::string, int>>> store;
    // The profile's unlock table (0x822A7B30 / 7C60 / 7EC8 / 7FA8): every
    // guitar, skin, character, outfit and store video, and whether the
    // store sells it (then it is locked until bought).
    std::map<std::string, bool> tableItems;
    // guitars.dta's (require tour_passed | tour_5_star) (difficulty d)
    // guitars (0x822A8078).
    struct Reward {
        std::string guitar;
        bool fiveStar = false;
        int difficulty = 0;
    };
    std::vector<Reward> rewards;
};
const Config& config(UiManager& ui);

// What finish_song reports (0x822A6BD0's struct, filled by 0x822A8078).
struct FinishResult {
    bool changed = false;    // +0x0: a new best score or a perfect run
    std::string newVenue;    // +0x4: the venue the song unlocked
    int newStatus = 0;       // +0x8: the status reached (0: none)
    int statusAward = 0;     // +0xc: the status cash
    int cash = 0;            // +0x10: the stars' cash
    std::string cashReason;  // +0x14: award_<n>_star
};

// The band chosen (Campaign +0x48), nullptr when none.
Career::Profile* profile(UiManager& ui);
// The player's difficulty (gamecfg, player 0).
int difficulty(UiManager& ui);
// A profile's record at difficulty `d`, its defaults filled (0x822A9E90 /
// 0x822A9FA0: the first venue and its songs unlocked).
std::map<std::string, Career::Entry>& record(UiManager& ui, Career::Profile& p, int d);

// The config's lists.
int numTiers(UiManager& ui, int d);  // 0x822A2BC8: easy has no last tier
int venueIndex(UiManager& ui, const std::string& venue);
std::string venueOf(UiManager& ui, const std::string& song);  // 0x822A2A10
const std::vector<std::string>& songsOf(UiManager& ui, const std::string& venue);
std::string encoreOf(UiManager& ui, const std::string& venue);  // 0x822A96A0
bool isStoreSong(UiManager& ui, const std::string& song);       // 0x822A3240
bool isDownload(UiManager& ui, const std::string& song);        // 0x822A33C0
bool isEncore(UiManager& ui, const std::string& song);          // 0x822A8BB8 (the record's flag)
int requiredSongs(UiManager& ui, int d, const std::string& venue);
// Every song: the tiers', the store's, the downloads (0x822A3650).
std::vector<std::string> allSongs(UiManager& ui);

// The record's rules at difficulty `d`.
bool passed(UiManager& ui, Career::Profile& p, int d, const std::string& sym);  // 0x822AE8F8
int numPassed(UiManager& ui, Career::Profile& p, int d, const std::string& venue);  // 0x822A8F88
bool unlockedAt(UiManager& ui, Career::Profile& p, int d, const std::string& sym);  // 0x822A9B38
int tiersCompleted(UiManager& ui, Career::Profile& p, int d);  // 0x822A9750
bool allFiveStars(UiManager& ui, Career::Profile& p, int d);   // 0x822A91A8
int status(UiManager& ui, Career::Profile& p, int d);          // 0x822A9D30

// Campaign::IsUnlocked (0x82285CC0): the all-access cheat, pro face-off,
// in career the band's (its table, else its record at the difficulty),
// else any band's at any difficulty.
bool isUnlocked(UiManager& ui, const std::string& sym);
// A store item a band can buy, as the profile's table holds it.
bool inTable(UiManager& ui, const std::string& sym);

// campaign finish_song (0x82286C28 -> 0x822A8078): the band's song result.
FinishResult finishSong(UiManager& ui, int score, int stars, int percentHit);
// campaign cheat_beat_song (0x82284CC0 -> 0x822AA888).
void cheatBeatSong(UiManager& ui, const std::string& song, int score);
// campaign buy_item (0x822A7278).
void buyItem(UiManager& ui, const std::string& item, int price);
// store.dta's price of an item (StorePanel price, 0x8229D4A8); -1 if none.
int price(UiManager& ui, const std::string& category, const std::string& item);

// The song lists: career (0x822A75D0 type 1 -> 0x822AA758, filtered by
// 0x822AA3F8) and quick play (0x82287600).
std::vector<std::string> careerSongs(UiManager& ui, bool storeMode);
std::vector<std::string> quickplaySongs(UiManager& ui);

// game set_career_venue (0x822B42C8) / set_career_song (0x822B89B8).
void setCareerVenue(UiManager& ui);
void setCareerSong(UiManager& ui);
// GamePanel want_encore_fx (0x822CA5D0) without force_encore.
bool wantEncoreFx(UiManager& ui);

}  // namespace campaign
}  // namespace ui
