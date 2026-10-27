// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "scene/milo.h"

class GameData;
struct SongInfo;

// A GH2 venue as the game loads it (docs/venues.md). ui/game.dta's
// world_panel loads ../world/<v>/<v>.milo (one player), <v>_mp.milo
// (face-off / pro face-off: (game multiplayer_vs)) or <v>_coop.milo (the
// other two-player modes); finish_load names its directory `world`.
// That WorldDir (world/<v>/gen/<v>[_mp|_coop].milo_xbox: CamShots, PostProc,
// camera path TransAnims) names its sub-directories in its ObjectDir data,
// and each of those names the next, so a venue is a chain [C data]:
//   <v>.milo -> ../og/<v>_lighting.milo (ng/gen/: Spotlights, LightPresets,
//   beams, EventTriggers) -> ../../<v>_chars.milo (band slots, WorldCrowd,
//   Waypoints, the band's lights) -> ../og/<v>_geom.milo (the stage and its
//   effects) -> ../../<v>_bank.milo (sound effects);
// stone's geom also names ../eng/starmessage.milo (one per language, the
// engine swaps the folder). Directories are found through each other as
// ObjectDir::FindObject searches sub-directories [I: GH2's subdirectory
// search, the same as later Harmonix code]. RndDir objects with a proxy file
// (big's flash pots, small1's bottle throws, the drummer explode) load their
// own milos ("og/" folders resolve to "ng/", milo.h).
enum class VenueVariant { Single, Mp, Coop };

namespace venues {

// The eight venues in career order (config/campaign.dta (order ...)).
const std::vector<std::string>& names();
bool known(const std::string& venue);
const char* suffix(VenueVariant v);  // "", "_mp", "_coop"
const char* variantName(VenueVariant v);  // "sp", "mp", "coop"
// world/<v>/gen/<v><suffix>.milo_xbox
std::string worldMilo(const std::string& venue, VenueVariant v);
// The venue a song plays in outside the career: its (quickplay (venue x)),
// else the career tier that holds it (campaign.dta), else battle.
std::string forSong(const GameData& data, const SongInfo& song);
// The career tier a song is in (its venue), or "" (campaign.dta (order ...)).
std::string careerTier(const GameData& data, const std::string& song);
// Development aid: GD02_VENUE=<venue> plays every song there ("" if unset or unknown).
std::string fromEnv();

}  // namespace venues

class Venue {
public:
    // A milo of the chain: the world directory, then each sub-directory in
    // load order, depth first.
    struct Part {
        std::string path;   // archive path
        scene::Milo* milo = nullptr;
        std::string from;   // the milo that named it ("" for the world)
    };
    // A proxy directory object and the milo it loads.
    struct Proxy {
        const scene::DirObj* object = nullptr;
        scene::Milo* milo = nullptr;
        std::string path;
    };

    // Loads the chain and the proxies; throws std::runtime_error when the
    // world milo itself cannot be loaded. Missing sub-directories and
    // proxies are listed in problems(). `language`: eng fre deu ita esl.
    Venue(const GameData& data, std::string name, VenueVariant variant, const std::string& language = "eng");

    const std::string& name() const { return name_; }
    VenueVariant variant() const { return variant_; }
    const std::string& language() const { return language_; }
    scene::Milo& world() const { return *parts_.front().milo; }
    const scene::DirObj* worldDir() const;  // the WorldDir's own record
    const std::vector<Part>& parts() const { return parts_; }
    const std::vector<Proxy>& proxies() const { return proxies_; }
    const std::vector<std::string>& problems() const { return problems_; }
    scene::MiloLibrary& library() { return *library_; }
    const GameData& data() const { return data_; }
    // The proxy entry of a proxy directory object, or nullptr.
    const Proxy* proxyOf(const scene::Object* o) const;

    // An object of the venue by name: the chain's directories in order (the
    // world first), then the proxies' milos.
    scene::Object* find(const std::string& name) const;
    template <class T> T* find(const std::string& name) const {
        scene::Object* o = find(name);
        return o ? o->as<T>() : nullptr;
    }
    // Every object of the chain and the proxies (each milo once).
    template <class F> void forEach(F&& fn) const {
        for (const auto& p : parts_) p.milo->root().forEach(fn);
        for (const auto& p : uniqueProxyMilos_) p->root().forEach(fn);
    }
    template <class T> std::vector<T*> all() const {
        std::vector<T*> v;
        forEach([&](scene::Object& o) {
            if (T* t = o.as<T>()) v.push_back(t);
        });
        return v;
    }

private:
    void loadChain(const std::string& path, const std::string& from, int depth);
    void loadProxies(scene::Milo& milo, int depth);

    const GameData& data_;
    std::string name_, language_;
    VenueVariant variant_;
    std::unique_ptr<scene::MiloLibrary> library_;
    std::vector<Part> parts_;
    std::vector<Proxy> proxies_;
    std::vector<scene::Milo*> uniqueProxyMilos_;
    std::vector<std::string> problems_;
};
