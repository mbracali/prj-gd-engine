// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The games the launcher lists (games.yaml -> each game-info.yaml), the disc
// image known for each one and where it came from, and the launcher's saved
// settings (config.txt in its save folder).
#pragma once

#include <map>
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

    void saveIso(const std::string& id, const std::string& path);
    const std::vector<std::string>& discDirs() const { return discDirs_; }
    std::string launcherVersion() const { return launcherVersion_; }
    std::string gamesYaml() const { return gamesYaml_; }
    std::string configPath() const;
    int discsFound() const { return discsFound_; }

private:
    void readConfig();
    void writeConfig();
    std::vector<Game> games_;
    std::vector<std::string> discDirs_;
    std::map<std::string, std::string> config_;
    std::string gamesYaml_, gamesRoot_, modulesDir_, launcherVersion_ = "0.0.0";
    int discsFound_ = 0;
};
