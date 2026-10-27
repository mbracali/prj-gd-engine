// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "catalog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <sstream>

#include "platform.h"
#include "yaml.h"

namespace {

std::string lowerExt(const std::string& path) {
    size_t dot = path.find_last_of('.');
    std::string e = dot == std::string::npos ? std::string() : path.substr(dot);
    for (char& c : e) c = char(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return e;
}

std::vector<IsoSignature> signaturesOf(const yaml::Node& list, const std::string& game) {
    std::vector<IsoSignature> out;
    for (const yaml::Node& n : list.list) {
        IsoSignature s;
        s.game = n["game"].str(game);
        s.format = n["format"].str();
        s.ps2Boot = n["ps2_boot"].str();
        s.volumeId = n["volume_id"].str();
        s.xexTitleId = n["xex_title_id"].str();
        s.files = n["files"].strings();
        out.push_back(s);
    }
    return out;
}

}  // namespace

void Catalog::load() {
    games_.clear();
    discDirs_.clear();
    const std::string base = platform::baseDir();
    // A launcher build: gd-engine-games/ next to the program. A development
    // run: the repository around the program (local_builds/ is inside it).
    std::string repo;
    for (std::string dir = base; !dir.empty(); dir = platform::parentDir(dir)) {
        if (platform::isFile(platform::join(dir, "gd-engine-launcher/games.yaml"))) {
            repo = dir;
            break;
        }
        if (platform::parentDir(dir) == dir) break;
    }
    modulesDir_ = platform::join(base, "gd-engine-games");
    if (const char* env = SDL_getenv("GDUL_GAMES_YAML")) {
        gamesYaml_ = env, gamesRoot_ = platform::parentDir(env);
    } else if (platform::isFile(platform::join(modulesDir_, "games.yaml"))) {
        gamesYaml_ = platform::join(modulesDir_, "games.yaml"), gamesRoot_ = modulesDir_;
    } else if (!repo.empty()) {
        gamesYaml_ = platform::join(repo, "gd-engine-launcher/games.yaml"), gamesRoot_ = repo;
    }
    for (const std::string& info : {platform::join(modulesDir_, "launcher-info.yaml"),
                                    repo.empty() ? std::string() : platform::join(repo, "gd-engine-launcher/launcher-info.yaml")})
        if (yaml::Node li = yaml::load(info); li.kind == yaml::Node::Kind::Map) {
            launcherVersion_ = li["launcher_version"].str(launcherVersion_);
            break;
        }

    yaml::Node list = yaml::load(gamesYaml_);
    // Signatures for games whose own folder cannot carry them (games.yaml).
    std::vector<IsoSignature> extra = signaturesOf(list["extra_iso_signatures"], "");
    for (const std::string& rel : list["games"].strings()) {
        yaml::Node gi = yaml::load(platform::join(gamesRoot_, rel));
        if (gi.kind != yaml::Node::Kind::Map) {
            SDL_Log("gdul: cannot read %s", platform::join(gamesRoot_, rel).c_str());
            continue;
        }
        Game g;
        g.id = gi["id"].str();
        g.name = gi["name"].str(g.id);
        g.nick = gi["nick_name"].str();
        g.launch = gi["launch"].str();
        std::string platforms;
        for (const std::string& p : gi["platforms"].strings()) platforms += (platforms.empty() ? "" : ", ") + p;
        g.platforms = platforms;
        g.basedOn = gi["based_on"].str();
        g.version = gi["guitar_dream_version"].str();
        g.ready = gi["ready_to_play"].flag();
        g.completeness = std::clamp(gi["completeness"].num(), 0.0, 1.0);
        g.signatures = signaturesOf(gi["iso_signatures"], g.id);
        for (const IsoSignature& s : extra)
            if (s.game == g.id) g.signatures.push_back(s);
        g.modulePath = platform::join(modulesDir_, g.id + "/" + g.id + platform::kModuleExt);
        g.hasModule = platform::isFile(g.modulePath);
        games_.push_back(g);
    }

    discDirs_.push_back(platform::join(base, "original_isos"));
    if (!repo.empty() && platform::join(repo, "original_isos") != discDirs_[0]) discDirs_.push_back(platform::join(repo, "original_isos"));
    readConfig();
    rescanDiscs();
}

void Catalog::rescanDiscs() {
    discsFound_ = 0;
    for (Game& g : games_) g.iso.clear(), g.isoSource = Game::IsoSource::None;
    // A saved choice first, then a disc found in an original_isos folder.
    for (Game& g : games_) {
        auto it = config_.find("iso." + g.id);
        if (it != config_.end() && platform::isFile(it->second)) g.iso = it->second, g.isoSource = Game::IsoSource::Saved;
    }
    for (const std::string& dir : discDirs_)
        for (const std::string& f : platform::listFiles(dir)) {
            if (lowerExt(f) != ".iso") continue;
            Game* g = identify(probeDisc(f));
            if (!g) continue;
            ++discsFound_;
            if (g->iso.empty()) g->iso = f, g->isoSource = Game::IsoSource::Found;
        }
}

Game* Catalog::find(const std::string& id) {
    for (Game& g : games_)
        if (g.id == id) return &g;
    return nullptr;
}

Game* Catalog::identify(const DiscInfo& disc) {
    if (disc.format.empty()) return nullptr;
    for (Game& g : games_)
        for (const IsoSignature& s : g.signatures)
            if (s.matches(disc)) return &g;
    return nullptr;
}

void Catalog::saveIso(const std::string& id, const std::string& path) {
    config_["iso." + id] = path;
    writeConfig();
    if (Game* g = find(id)) g->iso = path, g->isoSource = Game::IsoSource::Saved;
}

std::string Catalog::configPath() const { return platform::join(platform::saveDir(), "config.txt"); }

void Catalog::readConfig() {
    config_.clear();
    size_t size = 0;
    void* data = SDL_LoadFile(configPath().c_str(), &size);
    if (!data) return;
    std::istringstream in(std::string(static_cast<const char*>(data), size));
    SDL_free(data);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq != std::string::npos && line[0] != '#') config_[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void Catalog::writeConfig() {
    std::string text = "# Guitar Dream Universal Launcher settings (iso.<game id>=<disc image>)\n";
    for (const auto& [k, v] : config_) text += k + "=" + v + "\n";
    SDL_SaveFile(configPath().c_str(), text.data(), text.size());
}
