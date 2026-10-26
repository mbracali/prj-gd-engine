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
// eight band profiles, the high score tables, the bought / unlocked store
// items and the options (0x822B0BC0: lefty P1 / P2, band / guitar / fx
// volume indices, sync offset). The port keeps the same data as JSON
// (career.json in the save folder, format "gd02-save"), written atomically
// with the previous file kept as career.json.bak.
class Career {
public:
    static constexpr int kVersion = 17;  // GH2's save version [C]
    static constexpr int kProfiles = 8;
    static constexpr int kDifficulties = 4;
    static constexpr int kHighScores = 5;  // per song and difficulty (0x822A4988) [C]

    struct SongResult {
        int score = 0, stars = 0;
        bool passed = false;
    };
    struct Profile {
        bool inUse = false;
        std::string name;
        int cash = 0;
        int tutorialsDone = 0;
        std::array<int, kDifficulties> status{};  // venues completed per difficulty
        std::array<std::map<std::string, SongResult>, kDifficulties> results;
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
    };

    Career() { clear(); }
    void clear();  // a fresh save: no profiles, default options

    std::array<Profile, kProfiles> profiles;
    std::map<std::string, bool> items;  // unlocked / bought store items (store.dta symbols)
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
    // false when the JSON is not a gd02 save (or newer than this build).
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
