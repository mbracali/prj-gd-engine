// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

struct Json;

// The game's save data, as GH2's Campaign::Save (0x822869A0) writes it to
// save:\gh2save.dat (version 17, read back by 0x82287B00 when > 15) [C]:
// eight band profiles (their tours, cash and bought store items), the high
// score tables, pro face-off and the options (0x822B0BC0: lefty P1 / P2, band / guitar / fx
// volume indices, sync offset). The port keeps the same data as JSON
// (career.json in the save folder, format "gd03-save"), written atomically
// with the previous file kept as career.json.bak.
class Career {
public:
    static constexpr int kVersion = 17;  // GH2's save version [C]
    static constexpr int kProfiles = 8;
    static constexpr int kDifficulties = 4;
    static constexpr int kHighScores = 5;  // per song and difficulty (0x822A4988) [C]

    // One entry of a profile's per-difficulty table (16 bytes at record
    // +0x14 in GH2, made by 0x822AE698): a venue or a song (or a game mode)
    // of the campaign. The saved state only; what is a store song or an
    // encore comes from the config (ui/campaign.h).
    struct Entry {
        bool unlocked = false;   // +0xe bit 0x40
        bool completed = false;  // a venue's +0xe bit 0x08 (ae940 / ae8f8)
        int score = 0;           // a song's best score (+0x8)
        int stars = 0;           // the stars of the best score (+0xc & 0x7f): 3+ = passed (0x822AE8F8)
        bool perfect = false;    // +0xc bit 0x80: 100 % of the notes (the gold stars)
        int maxStars = 0;        // +0xd: the most stars at this or a higher difficulty (the star cash)
        bool known = false;      // made by the record's init (or read back): false until ui/campaign.h fills it
    };
    // The band's look at one difficulty (the profile's per-difficulty
    // record, 0x24 bytes at profile +0x20, saved by 0x822A8D00 / read by
    // 0x822A8D68 as four symbols) [C]: campaign set_character_info
    // (0x82284E88) copies player 0's config (character, outfit, guitar,
    // skin) into it, get_character_info (0x82284E00) back; an empty
    // character means the career at that difficulty was not started
    // (campaign new_campaign, 0x822A8A88).
    struct CharInfo {
        std::string character, outfit, guitar, skin;
    };
    struct Profile {
        bool inUse = false;
        std::string name;
        int cash = 0;       // +0x30
        int cashSpent = 0;  // +0x34 (campaign cash_spent)
        int tutorialsDone = 0;
        int lastDifficulty = 1;  // campaign last_difficulty (profile +0x2c) [C]; its first value [I]
        // The four per-difficulty records (profile +0x20, 0x24 bytes each):
        // the venues' and songs' entries (ui/campaign.h fills the defaults).
        std::array<std::map<std::string, Entry>, kDifficulties> entries;
        std::array<CharInfo, kDifficulties> chars;
        // The profile's unlock table (+0x38, 0x822A88C8): the store items
        // (guitars, skins, characters, outfits, videos) this band bought;
        // every other item of the table is unlocked from the start.
        std::map<std::string, bool> items;
    };
    struct HighScore {
        std::string name;
        int score = 0;
    };
    struct Options {
        std::array<bool, 2> lefty{};
        // Volume indices 0..11 (SLIDER_LEVELS 12); GH2's defaults [I: the
        // full-scale index for all three].
        int bandVolumeIdx = 11, guitarVolumeIdx = 11, fxVolumeIdx = 11;
        int syncOffsetMs = 0;  // options set_sync_offset (the lag screen stores -lag)
        bool stereo = true;    // this disc's audio settings (stereo.chk); mono folds the mix (audio/audio_device.h)
        // The video settings' progressive scan (p_scan.chk): kept and shown,
        // no effect on a PC's output; off as the PS2 boots [I].
        bool pscan = false;
    };

    Career() { clear(); }
    void clear();  // a fresh save: The Glitches only, default options

    // The port's band in every save (not in GH2): a fresh save has it, and a
    // save without it gets it in its last free slot (none free: not added).
    // Its look at every difficulty is `glitchesLook` (the UI sets GH2's
    // default one), starting cash 50 (config/campaign.dta).
    static constexpr const char* kGlitches = "The Glitches";
    CharInfo glitchesLook{"punk", "punk1", "", ""};
    bool ensureGlitches();  // true when it was added

    std::array<Profile, kProfiles> profiles;
    bool proFaceOff = false;            // pro face-off unlocked: a tour of all six tiers won (PS2 Campaign +0x78, 0x0012f130; GH2 +0x68), saved after the high scores
    // Co-op's record (Campaign +0x4c, one for the whole save, updated by
    // campaign finish_coop_song 0x822861A8 -> 0x822A6848) [C]: per song the
    // band's best score (+0x8, kept when higher), its stars (+0xc & 0x7f)
    // and the gold flag (+0xc bit 0x80: both players hit 100 % on Expert);
    // and the bass guitars co-op has awarded (the reward entries' bit 0x40,
    // 0x822A66E0: guitars.dta (require (songs n) (stars s))).
    struct CoopEntry {
        int score = 0;
        int stars = 0;
        bool perfect = false;
    };
    std::map<std::string, CoopEntry> coop;
    std::map<std::string, bool> coopAwards;
    Options options;
    std::string defaultName;            // highscores set_default_name
    int autosave = 1;

    // The high score table of a song at a difficulty (created with GH2's
    // defaults: highscore_dummy_0..4 with 50000 .. 10000 [C 0x822A4988]).
    std::vector<HighScore>& highScores(const std::string& song, int difficulty);
    // The rank (0..4) `score` would take, or -1.
    int checkHighScore(const std::string& song, int difficulty, int score);
    void addHighScore(const std::string& song, int difficulty, const std::string& name, int score);
    // The default names of the tables (localized highscore_dummy_%d).
    std::array<std::string, kHighScores> dummyNames{"Finn", "Alex D.", "Owen", "Kayley", "Liana"};

    int numProfiles() const;
    // L12: every change marks the save dirty (campaign needs_save).
    bool needsSave = false;
    void touch() { needsSave = true; }

    Json toJson() const;
    // false when the JSON is not a gd03 save (or newer than this build).
    bool fromJson(const Json& j);

private:
    std::map<std::string, std::array<std::vector<HighScore>, kDifficulties>> highScores_;
};

// The memory card: career.json in the save folder (or `dir`).
class SaveFile {
public:
    explicit SaveFile(std::string dir = "");
    const std::string& path() const { return path_; }
    bool exists() const;
    // Atomic write (temporary file then rename), keeping the previous file as .bak.
    bool write(const Career& career, std::string& error) const;
    // false with `corrupt` set when the file exists but cannot be read.
    bool read(Career& career, bool& corrupt) const;
    bool writable() const;

private:
    std::string dir_, path_;
};
