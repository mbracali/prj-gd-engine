// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "catalog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <vector>

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

Catalog::Catalog() = default;
Catalog::~Catalog() = default;

void Catalog::load() {
    games_.clear();
    discDirs_.clear();
    discDirInfo_.clear();
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

    // The build's manifest.json (build.sh): there in every launcher build,
    // not in a development run from the repository.
    std::string manifest;
    {
        size_t size = 0;
        if (void* data = SDL_LoadFile(platform::join(modulesDir_, "manifest.json").c_str(), &size)) {
            manifest.assign(static_cast<const char*>(data), size);
            SDL_free(data);
        }
    }

    const bool devRun = manifest.empty() && !repo.empty();
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
#ifdef __ANDROID__
        g.modulePath = platform::nativeLibDir() + "lib" + g.id + platform::kModuleExt;  // in the APK
#else
        g.modulePath = platform::join(modulesDir_, g.id + "/" + g.id + platform::kModuleExt);
#endif
        g.hasModule = platform::isFile(g.modulePath);
        // Every launcher build carries the module of every ready game, on
        // every system (scripts/build.sh): one missing is an incomplete
        // download, whatever the manifest says. Only a development run (the
        // repository, no build's manifest) may lack modules.
        g.moduleMissing = g.ready && !g.hasModule && !devRun;
        games_.push_back(g);
    }

#ifdef __ANDROID__
    // The app's own folder on the shared storage (Android/data/<app>/files/),
    // where a disc can be put by hand (a computer over USB). Load ISO reads a
    // disc where it is instead (a content:// document, never copied).
    if (const char* ext = SDL_GetAndroidExternalStoragePath(); ext && *ext)
        addDiscDir(platform::join(ext, "original_isos"), "the app's folder");
    else
        addDiscDir(platform::join(platform::saveDir(), "original_isos"), "the app's folder");
#else
#ifdef __APPLE__
    // In an .app (base is <folder>/<name>.app/Contents/Resources/): the
    // original_isos folder next to the app, where the download has it. An app
    // run from where a download put it is translocated (a read-only copy at
    // a random path) until it is moved with the Finder: then next to its
    // real place, when macOS says where that is.
    runningApp_ = platform::appBundle();
    if (!runningApp_.empty()) {
        translocated_ = platform::isTranslocated(runningApp_);
        if (translocated_) {
            originalApp_ = platform::originalAppBundle(runningApp_);
            if (!originalApp_.empty()) addDiscDir(platform::join(platform::parentDir(originalApp_), "original_isos"), "next to the app");
            SDL_Log("gdul: macOS runs the app from a translocated copy (%s); its real place: %s", runningApp_.c_str(),
                    originalApp_.empty() ? "unknown" : originalApp_.c_str());
        }
        addDiscDir(platform::join(platform::parentDir(runningApp_), "original_isos"), translocated_ ? "next to the translocated copy" : "next to the app");
        addDiscDir(platform::join(base, "original_isos"), "inside the app");
    } else {
        addDiscDir(platform::join(base, "original_isos"), "next to the program");
    }
#else
    addDiscDir(platform::join(base, "original_isos"), "next to the program");
#endif
    // Always writable, whatever the program's folder is (Program Files, a
    // read-only image, a translocated app).
    addDiscDir(platform::join(platform::saveDir(), "original_isos"), "in the launcher's save folder");
#endif
    if (!repo.empty()) addDiscDir(platform::join(repo, "original_isos"), "in the repository");
    readConfig();
    rescanDiscs();
}

void Catalog::rescanDiscs() {
    discsFound_ = 0;
    for (Game& g : games_) g.iso.clear(), g.isoSource = Game::IsoSource::None;
    // A saved choice first, then a disc found in an original_isos folder.
    for (Game& g : games_) {
        auto it = config_.find("iso." + g.id);
        if (it == config_.end()) continue;
        if (platform::isFile(it->second)) g.iso = it->second, g.isoSource = Game::IsoSource::Saved;
        else SDL_Log("gdul: the saved disc of %s cannot be read now: %s", g.id.c_str(), it->second.c_str());
    }
    for (DiscDir& d : discDirInfo_) {
        d.problem.clear(), d.isos = d.known = 0;
        for (const std::string& f : platform::listFiles(d.path, d.problem)) {
            if (lowerExt(f) != ".iso") continue;
            ++d.isos;
            Game* g = identify(probeDisc(f));
            if (!g) continue;
            ++d.known, ++discsFound_;
            if (g->iso.empty()) g->iso = f, g->isoSource = Game::IsoSource::Found;
        }
    }
    for (Game& g : games_) g.isoName = g.iso.empty() ? std::string() : platform::displayName(g.iso);
}

void Catalog::addDiscDir(const std::string& dir, const std::string& what) {
    // The same folder once (a build inside the repository, say).
    auto same = [](std::string a, std::string b) {
        while (a.size() > 1 && (a.back() == '/' || a.back() == '\\')) a.pop_back();
        while (b.size() > 1 && (b.back() == '/' || b.back() == '\\')) b.pop_back();
        std::error_code ec;
        return a == b || std::filesystem::equivalent(std::filesystem::u8path(a), std::filesystem::u8path(b), ec);
    };
    for (const std::string& d : discDirs_)
        if (same(d, dir)) return;
    discDirs_.push_back(dir);
    discDirInfo_.push_back({dir, what, "", 0, 0});
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
    if (Game* g = find(id)) g->iso = path, g->isoName = platform::displayName(path), g->isoSource = Game::IsoSource::Saved;
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
