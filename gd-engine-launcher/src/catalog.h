// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The games the launcher lists (games.yaml -> each game-info.yaml), the disc
// image known for each one and where it came from, and the launcher's saved
// settings (config.txt in its save folder).
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "iso_probe.h"

struct Game {
    std::string id;           // gd-game-02
    std::string name, nick;   // "Guitar Hero II"
    std::string launch;       // 2007-04-03
    std::string platforms;    // "PlayStation 2, Xbox 360"
    std::string basedOn;      // Xbox 360
    std::string version;      // guitar_dream_version
    bool ready = false;       // ready_to_play
    double completeness = 0;  // 0..1
    std::vector<IsoSignature> signatures;

    std::string modulePath;  // gd-engine-games/<id>/<id>.so|.dll
    bool hasModule = false;

    std::string iso;        // the disc image to play, or ""
    enum class IsoSource { None, Saved, Found } isoSource = IsoSource::None;

    std::string title() const { return nick.empty() ? name : nick; }
    bool playable() const { return ready && hasModule && !iso.empty(); }
    bool highlighted() const { return ready && !iso.empty(); }
};

class Catalog {
public:
    Catalog();
    ~Catalog();
    // Finds games.yaml (a launcher build's gd-engine-games/, else the
    // repository's gd-engine-launcher/ above the program), reads every game,
    // the saved settings, and looks for disc images.
    void load();
    void rescanDiscs();

    std::vector<Game>& games() { return games_; }
    const std::vector<Game>& games() const { return games_; }
    Game* find(const std::string& id);
    // The game whose signature matches the disc, or nullptr.
    Game* identify(const DiscInfo& disc);

    // A content:// URI (Android's file dialog) is copied into the app's
    // original_isos/ first (the games read files), then saved; see pollCopy.
    void saveIso(const std::string& id, const std::string& path);
    // That copy's progress, about once a second, then its result; "" when
    // there is nothing new (and always on desktops).
    std::string pollCopy();
    const std::vector<std::string>& discDirs() const { return discDirs_; }
    // Where disc images are looked for, and what was there at the last scan.
    struct DiscDir {
        std::string path, what;  // what: "next to the app", ...
        std::string problem;     // "" or why it could not be read ("not there", ...)
        int isos = 0, known = 0; // .iso files; those of a game the launcher knows
    };
    const std::vector<DiscDir>& discDirInfo() const { return discDirInfo_; }
    // macOS: the app runs from a translocated (read-only, random) copy;
    // originalApp() is its real place when macOS says it ("" if not).
    bool translocated() const { return translocated_; }
    const std::string& runningApp() const { return runningApp_; }
    const std::string& originalApp() const { return originalApp_; }
    std::string launcherVersion() const { return launcherVersion_; }
    std::string gamesYaml() const { return gamesYaml_; }
    std::string configPath() const;
    int discsFound() const { return discsFound_; }

private:
    void readConfig();
    void writeConfig();
    struct DiscCopy;
    std::shared_ptr<DiscCopy> copy_;  // shared_ptr: DiscCopy is complete in catalog.cpp only
    void startCopy(const std::string& id, const std::string& uri);
    std::vector<Game> games_;
    std::vector<std::string> discDirs_;
    std::vector<DiscDir> discDirInfo_;
    bool translocated_ = false;
    std::string runningApp_, originalApp_;
    void addDiscDir(const std::string& dir, const std::string& what);
    std::map<std::string, std::string> config_;
    std::string gamesYaml_, gamesRoot_, modulesDir_, launcherVersion_ = "0.0.0";
    int discsFound_ = 0;
};
