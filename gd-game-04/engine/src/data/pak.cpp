// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/pak.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <cctype>
#include <list>
#include <mutex>

#include <stdexcept>

#include "data/disc.h"
#include "data/qbkey.h"

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

constexpr uint32_t kLast = qbkey::key(".last");
constexpr uint32_t kLast2 = qbkey::key("last");

}  // namespace

std::shared_ptr<const Pak> Pak::shared(const Disc& disc, const std::string& pakPath) {
    static std::mutex mutex;
    static std::list<std::pair<std::string, std::shared_ptr<const Pak>>> cache;  // most recent first
    static const Disc* cacheDisc = nullptr;
    // The disc's names match in any case (LoadPak lowercases its path, the
    // musician tables keep Johnny_1): one entry for both.
    std::string key = pakPath;
    for (char& c : key) c = char(std::tolower(static_cast<unsigned char>(c)));
    auto find = [&]() -> std::shared_ptr<const Pak> {
        if (cacheDisc != &disc) cache.clear(), cacheDisc = &disc;
        for (auto it = cache.begin(); it != cache.end(); ++it)
            if (it->first == key) {
                cache.splice(cache.begin(), cache, it);
                return cache.front().second;
            }
        return nullptr;
    };
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (auto hit = find()) return hit;
    }
    // Read without the lock: a worker reading a character's pak (the
    // character select's LoadPak, MenuObjects) must not stall the main
    // thread's own hits. Two readers of one pak keep the first one cached.
    auto pak = std::make_shared<const Pak>(load(disc, pakPath));
    std::lock_guard<std::mutex> lock(mutex);
    if (auto hit = find()) return hit;
    cache.emplace_front(key, pak);
    constexpr size_t kBudget = size_t(160) << 20;  // a venue, its band and the menus' paks
    size_t used = 0;
    for (auto it = cache.begin(); it != cache.end();) {
        const bool pinned = it->first == "data/compressed/zones/global.pak.xen";
        used += pinned ? 0 : it->second->bytes();
        if (!pinned && it != cache.begin() && used > kBudget) it = cache.erase(it);
        else ++it;
    }
    return pak;
}

Pak Pak::load(const Disc& disc, const std::string& pakPath) {
    struct Timer {
        const std::string& p;
        uint64_t t0 = SDL_GetTicksNS();
        ~Timer() {
            static const bool on = [] { const char* v = SDL_getenv("GD04_FRAMELOG"); return v && *v && *v != '0'; }();
            if (const double ms = double(SDL_GetTicksNS() - t0) / 1e6; on && ms > 20) SDL_Log("pak: %s read in %.0f ms", p.c_str(), ms);
        }
    } timer{pakPath};
    Bytes header = disc.read(pakPath);
    size_t headerSize = header.size();
    const std::string suffix = ".pak.xen";
    if (pakPath.size() > suffix.size() && pakPath.compare(pakPath.size() - suffix.size(), suffix.size(), suffix) == 0) {
        std::string pab = pakPath.substr(0, pakPath.size() - suffix.size()) + ".pab.xen";
        if (disc.exists(pab)) {
            Bytes data = disc.read(pab);
            header.insert(header.end(), data.begin(), data.end());
        }
    }
    return fromBytes(std::move(header), headerSize, pakPath);
}

Pak Pak::fromBytes(Bytes buffer, size_t headerSize, std::string name) {
    Pak p;
    p.name_ = std::move(name);
    p.buf_ = std::move(buffer);
    for (size_t pos = 0; pos + 32 <= headerSize; pos += 32) {
        const uint8_t* e = p.buf_.data() + pos;
        PakEntry en;
        en.type = be32(e);
        if (en.type == kLast || en.type == kLast2) {
            p.ended_ = true;
            break;
        }
        en.offset = be32(e + 4);
        en.size = be32(e + 8);
        en.pakName = be32(e + 12);
        en.fullName = be32(e + 16);
        en.name = be32(e + 20);
        en.parent = be32(e + 24);
        en.flags = be32(e + 28);
        en.pos = pos;
        if (en.dataOffset() + en.size > p.buf_.size())
            throw std::runtime_error(p.name_ + ": entry " + std::to_string(p.entries_.size()) + " lies outside the data");
        p.entries_.push_back(en);
    }
    if (!p.ended_) throw std::runtime_error(p.name_ + ": no .last entry");
    return p;
}

const PakEntry* Pak::find(uint32_t key) const {
    for (const PakEntry& e : entries_)
        if (e.fullName == key) return &e;
    for (const PakEntry& e : entries_)
        if (e.name == key) return &e;
    return nullptr;
}

const PakEntry* Pak::findType(uint32_t key, uint32_t type) const {
    for (const PakEntry& e : entries_)
        if (e.type == type && (e.fullName == key || e.name == key)) return &e;
    return nullptr;
}
