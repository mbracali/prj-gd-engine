// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

class GameData;
struct Json;

// Career mode (the `campaign` object, Campaign_v1 @0x1395e0; docs/career.md):
// 8 profile slots, per-difficulty careers (character, guitar, cash, the 116
// unlockable items with beaten / best score / stars), tier unlocks, status,
// cash awards and the per-song high-score tables. Saved as JSON (docs/career.md §4.5).
class Campaign {
public:
    enum Category { kVenue, kSong, kCharacter, kGuitar, kSkin, kVideo };
    struct Item {
        Category cat = kSong;
        std::string sym;
        bool isStore = false, unlocked = false, global = false, beaten = false;
        int score = 0, stars = 0;
    };
    struct Career {
        std::string character, guitar, skin;  // character "" = a new campaign
        int cash = 0;
        std::vector<Item> items;
    };
    struct Options {
        bool lefty[2] = {false, false};
        float band = 1, guitar = 1, fx = 1;
        bool stereo = true;
    };
    struct Profile {
        bool empty = true;
        std::string name;
        int tutorialsDone = 0, lastDifficulty = 0;
        Options options;
        std::array<Career, 4> careers;
    };
    struct CashAward {
        int amount;
        std::string reason;
    };
    struct HighScore {
        std::string name;
        int score;
    };
    struct FinishResult {
        bool first = false;
        std::string newVenue;
        int newStatus = 0;
    };

    // Reads config/campaign.dta, store.dta, characters.dta, guitars.dta. Throws std::runtime_error.
    explicit Campaign(const GameData& data);

    // -- config (CampaignData)
    const std::vector<std::string>& venues() const { return venues_; }
    std::vector<std::string> songsOf(const std::string& venue) const;
    const std::vector<std::string>& storeSongs() const { return storeSongs_; }
    std::vector<std::string> careerSongs(int diff) const;  // the songs of the first numVenues(diff) tiers
    std::string venueOf(const std::string& song) const;
    int venueIndex(const std::string& venue) const;
    bool isStoreSong(const std::string& song) const;
    int numVenues(int diff) const { return diff == 0 ? int(venues_.size()) - 1 : int(venues_.size()); }
    int required(int diff, const std::string& venue) const;
    std::string nextVenue(const std::string& venue, int diff) const;
    int price(const std::string& category, const std::string& item) const;  // store.dta, 0 if not sold
    std::string guitarOfSkin(const std::string& skin) const;

    // -- profiles
    // The port's ninth band (not saved): everything unlocked, bought and beaten.
    static constexpr int kGlitchesSlot = 8;
    int slot() const { return slot_; }
    void setSlot(int s);
    Profile& profile() { return profiles_[size_t(slot_)]; }
    Profile& profile(int s) { return profiles_[size_t(s)]; }
    int numProfiles() const;
    int emptySlot() const;
    bool hasProfileName(const std::string& name, bool editing) const;
    void deleteSlot(int s);
    Career& career(int diff) { return profile().careers[size_t(diff)]; }

    // -- per difficulty (CampaignState)
    Item* item(int diff, const std::string& sym);
    bool isUnlocked(int diff, const std::string& sym);
    bool isUnlockedAny(const std::string& sym);
    // store.dta: a bonus video's movie file (FUN_0011bb00), or "".
    std::string videoFile(const std::string& sym) const {
        auto it = videoFiles_.find(sym);
        return it == videoFiles_.end() ? "" : it->second;
    }
    bool newCampaign(int diff) { return career(diff).character.empty(); }
    int beatenInVenue(int diff, const std::string& venue);
    bool venueComplete(int diff, const std::string& venue);
    int completedVenues(int diff);
    int status(int diff);
    int songsBeaten(int diff);
    int totalSongs(int diff) const { return 5 * numVenues(diff); }
    int careerScore(int diff);
    bool finalSong(int diff, const std::string& song);
    std::string unlockNextVenue(int diff, const std::string& song);
    // FUN_0013dd80: `starsOf(score)` gives 3/4/5 for the current song.
    FinishResult finishSong(int diff, const std::string& song, int score, const std::function<int(int)>& starsOf);
    bool hasCashAward() const { return !awards_.empty(); }
    bool nextCashAward(CashAward& out);
    bool buyItem(int diff, const std::string& sym, int price);
    void unlockGlobalItem(const std::string& sym);
    void addCash(int diff, int amount);

    // -- high scores (HighScoreDB; per song, slot, difficulty, 5 entries)
    int checkHighScore(const std::string& song, int diff, int score);  // insert rank, -1 if none
    void addHighScore(const std::string& song, int diff, const std::string& name, int score);
    const std::vector<HighScore>& highScores(const std::string& song, int diff);

    // -- flags and saving
    bool needsSave = false, allAccess = false, tutorialAccess = false, skipTutorials = false;
    std::deque<CashAward> awards_;
    // The save file (one file = the memory card's `data`); false when missing or unreadable.
    bool load(const std::string& path);
    bool save(const std::string& path);

private:
    std::vector<Item> defaultItems(int diff) const;
    Profile defaultProfile(int slot) const;
    Profile glitchesProfile() const;
    int starCum(int diff, int stars) const;

    std::vector<std::string> venues_;
    std::map<std::string, std::vector<std::string>> venueSongs_;
    std::vector<std::string> storeSongs_, characters_, guitars_, skins_, videos_;
    std::map<std::string, std::string> videoFiles_;  // store.dta (video <sym> (file X))
    std::map<std::string, std::map<std::string, int>> prices_;  // category -> item -> price
    std::map<std::string, std::string> skinGuitar_;
    std::array<int, 4> requiredSongs_{4, 4, 5, 5};
    std::array<std::vector<int>, 4> starAwards_, statusAwards_;
    int startingCash_ = 0;
    std::array<Profile, 9> profiles_;  // 8 save slots + The Glitches
    int slot_ = 0;
    // song -> [slot * 4 + diff] -> 5 entries
    std::map<std::string, std::array<std::vector<HighScore>, 36>> highScores_;
};
