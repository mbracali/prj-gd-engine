// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/venue.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdexcept>

#include "data/game_data.h"
#include "game/song_db.h"
#include "scene/ui_styles.h"

namespace venues {

const std::vector<std::string>& names() {
    // config/gh2.dta (venues battle small1 small2 theatre fest arena) [C data].
    static const std::vector<std::string> v = {"battle", "small1", "small2", "theatre", "fest", "arena"};
    return v;
}

bool known(const std::string& venue) { return std::find(names().begin(), names().end(), venue) != names().end(); }

const char* suffix(VenueVariant v) { return v == VenueVariant::Mp ? "_mp" : v == VenueVariant::Coop ? "_coop" : ""; }
const char* variantName(VenueVariant v) { return v == VenueVariant::Mp ? "mp" : v == VenueVariant::Coop ? "coop" : "sp"; }

std::string worldMilo(const std::string& venue, VenueVariant v) {
    return "world/" + venue + "/gen/" + venue + suffix(v) + ".milo_ps2";
}

std::string careerTier(const GameData& data, const std::string& song) {
    static ::DataNode campaign;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            campaign = scene::loadPlainDta(data, "config/campaign.dta");
        } catch (const std::exception& e) {
            SDL_Log("venue: config/campaign.dta: %s", e.what());
        }
    }
    const ::DataNode* order = campaign.find("order");
    if (!order) return {};
    for (size_t k = 1; k < order->children.size(); ++k) {
        const ::DataNode& t = order->children[k];
        if (!t.isList() || t.children.empty()) continue;
        for (size_t s = 1; s < t.children.size(); ++s)
            if (t.children[s].isText() && t.children[s].s == song) return t.children[0].s;
    }
    return {};
}

std::string forSong(const GameData& data, const SongInfo& song) {
    if (known(song.venue)) return song.venue;
    const std::string tier = careerTier(data, song.id);
    return known(tier) ? tier : "battle";
}

std::string fromEnv() {
    const char* v = SDL_getenv("GD03_VENUE");
    return v && known(v) ? std::string(v) : std::string();
}

}  // namespace venues

Venue::Venue(const GameData& data, std::string name, VenueVariant variant, const std::string& language)
    : data_(data), name_(std::move(name)), language_(language), variant_(variant),
      library_(std::make_unique<scene::MiloLibrary>(data)) {
    const std::string path = venues::worldMilo(name_, variant_);
    if (!data_.exists(path)) throw std::runtime_error("no venue " + path);
    loadChain(path, "", 0);
    for (size_t i = 0, n = parts_.size(); i < n; ++i) loadProxies(*parts_[i].milo, 0);
    // The chain's directories find each other's objects (FindObject
    // searches sub-directories): each links every other one, in chain order.
    for (const auto& p : parts_) {
        auto& linked = p.milo->root().linked;
        linked.clear();
        for (const auto& q : parts_)
            if (q.milo != p.milo) linked.push_back(&q.milo->root());
    }
}

void Venue::loadChain(const std::string& path, const std::string& from, int depth) {
    if (depth > 8) {
        problems_.push_back(path + ": sub-directories nested too deep");
        return;
    }
    for (const auto& p : parts_)
        if (p.path == path) return;  // loaded already
    scene::Milo* milo = nullptr;
    try {
        milo = &library_->load(path);
    } catch (const std::exception& e) {
        if (depth == 0) throw;
        problems_.push_back(e.what());
        return;
    }
    parts_.push_back({path, milo, from});
    const auto* own = milo->root().self ? milo->root().self->as<scene::DirObj>() : nullptr;
    if (!own) return;
    for (std::string sub : own->objectDir.subdirs) {
        // The per-language folder (stone's ../eng/starmessage.milo).
        if (size_t i = sub.find("/eng/"); i != std::string::npos) sub.replace(i, 5, "/" + language_ + "/");
        const std::string p = scene::findMiloRelative(data_, path, sub);
        if (!data_.exists(p)) {
            problems_.push_back(path + ": sub-directory " + sub + " -> " + p + " is missing");
            continue;
        }
        loadChain(p, path, depth + 1);
    }
}

void Venue::loadProxies(scene::Milo& milo, int depth) {
    if (depth > 4) return;
    for (const auto& o : milo.root().objects) {
        const auto* d = o->as<scene::DirObj>();
        if (!d || d->own || d->objectDir.proxyFile.empty() || d->objectDir.inlineProxy == 1 || d->objectDir.inlineProxy == 2) continue;
        const std::string p = scene::findMiloRelative(data_, milo.path(), d->objectDir.proxyFile);
        scene::Milo* m = nullptr;
        try {
            m = &library_->load(p);
        } catch (const std::exception& e) {
            problems_.push_back(milo.path() + ": proxy " + d->name + ": " + e.what());
            continue;
        }
        proxies_.push_back({d, m, p});
        if (std::find(uniqueProxyMilos_.begin(), uniqueProxyMilos_.end(), m) == uniqueProxyMilos_.end()) {
            uniqueProxyMilos_.push_back(m);
            loadProxies(*m, depth + 1);
        }
    }
}

const scene::DirObj* Venue::worldDir() const {
    const auto& self = world().root().self;
    return self ? self->as<scene::DirObj>() : nullptr;
}

const Venue::Proxy* Venue::proxyOf(const scene::Object* o) const {
    for (const auto& p : proxies_)
        if (p.object == o) return &p;
    return nullptr;
}

scene::Object* Venue::find(const std::string& name) const {
    for (const auto& p : parts_)
        if (scene::Object* o = p.milo->root().get(name)) return o;
    for (const scene::Milo* m : uniqueProxyMilos_)
        if (scene::Object* o = m->root().get(name)) return o;
    return nullptr;
}
