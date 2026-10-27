// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "catalog.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <sstream>
#include <thread>
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

// A disc image chosen through Android's file dialog (a content:// URI),
// copied into the app's original_isos/ by a worker thread, as gd02's own
// Android build does: the games open their disc with fopen.
struct Catalog::DiscCopy {
    std::string id, uri, dest;
    std::atomic<long long> done{0}, total{0};
    std::atomic<int> state{0};  // 0 copying, 1 done, 2 failed
    std::atomic<bool> cancel{false};
    std::string error;  // set before state becomes 2
    Uint64 reportedAt = 0;
    std::thread worker;

    void run() {
        const std::string part = dest + ".part";
        SDL_IOStream* in = SDL_IOFromFile(uri.c_str(), "rb");
        SDL_IOStream* out = in ? SDL_IOFromFile(part.c_str(), "wb") : nullptr;
        bool ok = in && out;
        if (!ok) error = SDL_GetError();
        if (ok) total = SDL_GetIOSize(in);
        std::vector<char> buf(size_t(4) << 20);
        while (ok && !cancel) {
            const size_t n = SDL_ReadIO(in, buf.data(), buf.size());
            if (n == 0) {
                ok = SDL_GetIOStatus(in) == SDL_IO_STATUS_EOF;
                if (!ok) error = SDL_GetError();
                break;
            }
            if (SDL_WriteIO(out, buf.data(), n) != n) {
                ok = false, error = SDL_GetError();
                break;
            }
            done += (long long)n;
        }
        if (in) SDL_CloseIO(in);
        if (out && !SDL_CloseIO(out) && ok) ok = false, error = SDL_GetError();
        if (ok && cancel) ok = false, error = "cancelled";
        if (ok && !SDL_RenamePath(part.c_str(), dest.c_str())) ok = false, error = SDL_GetError();
        if (!ok) SDL_RemovePath(part.c_str());
        state = ok ? 1 : 2;
    }
};

Catalog::Catalog() = default;
Catalog::~Catalog() {
    if (copy_) {
        copy_->cancel = true;
        copy_->worker.join();
    }
}

void Catalog::startCopy(const std::string& id, const std::string& uri) {
    if (copy_) {
        copy_->cancel = true;
        copy_->worker.join();
    }
    const std::string dir = discDirs_.empty() ? platform::saveDir() : discDirs_.front();
    SDL_CreateDirectory(dir.c_str());
    copy_ = std::make_shared<DiscCopy>();
    copy_->id = id, copy_->uri = uri, copy_->dest = platform::join(dir, id + ".iso");
    copy_->reportedAt = SDL_GetTicks();
    copy_->worker = std::thread(&DiscCopy::run, copy_.get());
}

std::string Catalog::pollCopy() {
    if (!copy_) return {};
    DiscCopy& c = *copy_;
    const int state = c.state;
    if (state == 0) {
        if (SDL_GetTicks() - c.reportedAt < 1000) return {};
        c.reportedAt = SDL_GetTicks();
        char line[128];
        std::snprintf(line, sizeof line, "Copying the disc image into the app... %lld / %lld MB", c.done.load() / 1048576,
                      c.total.load() / 1048576);
        return line;
    }
    c.worker.join();
    std::string message;
    if (state == 1) {
        saveIso(c.id, c.dest);
        message = "Disc image copied: " + c.id + " is ready.";
    } else {
        SDL_Log("gdul: copying %s failed: %s", c.uri.c_str(), c.error.c_str());
        message = "Could not copy the disc image: " + c.error;
    }
    copy_.reset();
    return message;
}

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
#ifdef __ANDROID__
        g.modulePath = platform::nativeLibDir() + "lib" + g.id + platform::kModuleExt;  // in the APK
#else
        g.modulePath = platform::join(modulesDir_, g.id + "/" + g.id + platform::kModuleExt);
#endif
        g.hasModule = platform::isFile(g.modulePath);
        games_.push_back(g);
    }

#ifdef __ANDROID__
    // The app's own folder on the shared storage (Android/data/<app>/files/),
    // where chosen discs are copied and where one can be put by hand.
    if (const char* ext = SDL_GetAndroidExternalStoragePath(); ext && *ext)
        discDirs_.push_back(platform::join(ext, "original_isos"));
    else
        discDirs_.push_back(platform::join(platform::saveDir(), "original_isos"));
#elif defined(__APPLE__)
    // In the .app (base is <folder>/<name>.app/Contents/Resources/): the
    // original_isos folder next to the app.
    discDirs_.push_back(platform::join(platform::parentDir(platform::parentDir(platform::parentDir(base))), "original_isos"));
#else
    discDirs_.push_back(platform::join(base, "original_isos"));
#endif
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
    if (path.rfind("content://", 0) == 0) return startCopy(id, path);
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
