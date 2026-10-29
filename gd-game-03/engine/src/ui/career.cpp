// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/career.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "data/json.h"
#include "platform/paths.h"

void Career::clear() {
    profiles = {};
    proFaceOff = false;
    coop.clear();
    coopAwards.clear();
    options = Options{};
    defaultName.clear();
    highScores_.clear();
    autosave = 1;
    ensureGlitches();
    needsSave = false;
}

bool Career::ensureGlitches() {
    for (const Profile& p : profiles)
        if (p.inUse && p.name == kGlitches) return false;
    for (int s = kProfiles - 1; s >= 0; --s) {
        Profile& p = profiles[size_t(s)];
        if (p.inUse) continue;
        p = Profile{};
        p.inUse = true;
        p.name = kGlitches;
        p.cash = 50;
        for (CharInfo& c : p.chars) c = glitchesLook;
        touch();
        return true;
    }
    return false;
}

int Career::numProfiles() const {
    return int(std::count_if(profiles.begin(), profiles.end(), [](const Profile& p) { return p.inUse; }));
}

std::vector<Career::HighScore>& Career::highScores(const std::string& song, int difficulty) {
    auto& tables = highScores_[song];
    auto& t = tables[size_t(std::clamp(difficulty, 0, kDifficulties - 1))];
    if (t.empty())
        for (int i = 0; i < kHighScores; ++i) t.push_back({dummyNames[size_t(i)], 50000 - 10000 * i});
    return t;
}

int Career::checkHighScore(const std::string& song, int difficulty, int score) {
    auto& t = highScores(song, difficulty);
    for (size_t i = 0; i < t.size(); ++i)
        if (score > t[i].score) return int(i);
    return -1;
}

void Career::addHighScore(const std::string& song, int difficulty, const std::string& name, int score) {
    const int rank = checkHighScore(song, difficulty, score);
    if (rank < 0) return;
    auto& t = highScores(song, difficulty);
    t.insert(t.begin() + rank, {name, score});
    t.resize(kHighScores);
    touch();
}

Json Career::toJson() const {
    Json j = Json::object();
    j.set("format", Json::string("gd03-save"));
    j.set("version", Json::number(2));  // 2: the tours, bands' items and pro face-off (M10)
    j.set("gh2_save_version", Json::number(kVersion));
    j.set("autosave", Json::number(autosave));
    Json ps = Json::array();
    for (const Profile& p : profiles) {
        Json pj = Json::object();
        pj.set("in_use", Json::boolean(p.inUse));
        pj.set("name", Json::string(p.name));
        pj.set("cash", Json::number(p.cash));
        pj.set("cash_spent", Json::number(p.cashSpent));
        pj.set("tutorials_done", Json::number(p.tutorialsDone));
        pj.set("last_difficulty", Json::number(p.lastDifficulty));
        Json res = Json::array(), ch = Json::array();
        for (int d = 0; d < kDifficulties; ++d) {
            // The record's entries, as Campaign::Save writes every one of a
            // band in use (an empty slot keeps its defaults).
            Json r = Json::object();
            if (p.inUse)
                for (const auto& [sym, en] : p.entries[size_t(d)]) {
                    Json e = Json::object();
                    e.set("unlocked", Json::boolean(en.unlocked));
                    if (en.completed) e.set("completed", Json::boolean(true));
                    if (en.score) e.set("score", Json::number(en.score));
                    if (en.stars) e.set("stars", Json::number(en.stars));
                    if (en.perfect) e.set("perfect", Json::boolean(true));
                    if (en.maxStars) e.set("max_stars", Json::number(en.maxStars));
                    r.set(sym, e);
                }
            res.a.push_back(r);
            const CharInfo& ci = p.chars[size_t(d)];
            Json c = Json::object();
            c.set("character", Json::string(ci.character));
            c.set("outfit", Json::string(ci.outfit));
            c.set("guitar", Json::string(ci.guitar));
            c.set("skin", Json::string(ci.skin));
            ch.a.push_back(c);
        }
        pj.set("tour", res);
        pj.set("characters", ch);
        Json it = Json::object();
        for (const auto& [k, v] : p.items)
            if (v) it.set(k, Json::boolean(true));
        pj.set("items", it);
        ps.a.push_back(pj);
    }
    j.set("profiles", ps);
    j.set("pro_faceoff", Json::boolean(proFaceOff));
    Json co = Json::object();
    for (const auto& [song, e] : coop) {
        Json ej = Json::object();
        ej.set("score", Json::number(e.score));
        ej.set("stars", Json::number(e.stars));
        if (e.perfect) ej.set("perfect", Json::boolean(true));
        co.set(song, ej);
    }
    j.set("coop", co);
    Json ca = Json::object();
    for (const auto& [g, v] : coopAwards)
        if (v) ca.set(g, Json::boolean(true));
    j.set("coop_awards", ca);
    Json hs = Json::object();
    for (const auto& [song, tables] : highScores_) {
        Json diffs = Json::array();
        for (const auto& t : tables) {
            Json list = Json::array();
            for (const HighScore& h : t) {
                Json e = Json::object();
                e.set("name", Json::string(h.name));
                e.set("score", Json::number(h.score));
                list.a.push_back(e);
            }
            diffs.a.push_back(list);
        }
        hs.set(song, diffs);
    }
    j.set("highscores", hs);
    Json o = Json::object();
    o.set("lefty_p1", Json::boolean(options.lefty[0]));
    o.set("lefty_p2", Json::boolean(options.lefty[1]));
    o.set("band_volume_idx", Json::number(options.bandVolumeIdx));
    o.set("guitar_volume_idx", Json::number(options.guitarVolumeIdx));
    o.set("fx_volume_idx", Json::number(options.fxVolumeIdx));
    o.set("sync_offset", Json::number(options.syncOffsetMs));
    j.set("options", o);
    j.set("default_name", Json::string(defaultName));
    return j;
}

bool Career::fromJson(const Json& j) {
    if (j.type != Json::Type::Object || j["format"].str() != "gd03-save" || j["version"].num() > 2) return false;
    Career c;
    c.profiles = {};  // the file's bands only (then The Glitches when it has none)
    c.autosave = int(j["autosave"].num(1));
    const Json& ps = j["profiles"];
    for (size_t i = 0; i < ps.a.size() && i < size_t(kProfiles); ++i) {
        const Json& pj = ps.a[i];
        Profile& p = c.profiles[i];
        p.inUse = pj["in_use"].truthy();
        p.name = pj["name"].str();
        p.cash = int(pj["cash"].num());
        p.cashSpent = int(pj["cash_spent"].num());
        p.tutorialsDone = int(pj["tutorials_done"].num());
        p.lastDifficulty = int(pj["last_difficulty"].num(1));
        for (size_t d = 0; d < size_t(kDifficulties); ++d) {
            if (d < pj["tour"].a.size())
                for (const auto& [sym, e] : pj["tour"].a[d].o) {
                    Entry& en = p.entries[d][sym];
                    en.known = true;
                    en.unlocked = e["unlocked"].truthy();
                    en.completed = e["completed"].truthy();
                    en.score = int(e["score"].num());
                    en.stars = int(e["stars"].num());
                    en.perfect = e["perfect"].truthy();
                    en.maxStars = int(e["max_stars"].num());
                }
            // 0.1.1's saves: the songs' best scores (quick play) as results.
            if (d < pj["results"].a.size())
                for (const auto& [song, e] : pj["results"].a[d].o) {
                    Entry& en = p.entries[d][song];
                    en.score = std::max(en.score, int(e["score"].num()));
                    en.stars = std::max(en.stars, e["passed"].truthy() ? std::max(3, int(e["stars"].num())) : 0);
                    en.maxStars = std::max(en.maxStars, en.stars);
                }
            if (d < pj["characters"].a.size()) {
                const Json& c = pj["characters"].a[d];
                p.chars[d] = {c["character"].str(), c["outfit"].str(), c["guitar"].str(), c["skin"].str()};
            }
        }
        for (const auto& [k, v] : pj["items"].o)
            if (v.truthy()) p.items[k] = true;
    }
    // 0.1.1's saves kept the bought items for the whole save: each band gets them.
    for (const auto& [k, v] : j["items"].o)
        if (v.truthy())
            for (Profile& p : c.profiles)
                if (p.inUse) p.items[k] = true;
    c.proFaceOff = j["pro_faceoff"].truthy();
    for (const auto& [song, e] : j["coop"].o) c.coop[song] = {int(e["score"].num()), int(e["stars"].num()), e["perfect"].truthy()};
    for (const auto& [g, v] : j["coop_awards"].o)
        if (v.truthy()) c.coopAwards[g] = true;
    for (const auto& [song, diffs] : j["highscores"].o) {
        auto& tables = c.highScores_[song];
        for (size_t d = 0; d < diffs.a.size() && d < size_t(kDifficulties); ++d)
            for (const Json& e : diffs.a[d].a) tables[d].push_back({e["name"].str(), int(e["score"].num())});
        for (auto& t : tables)
            if (!t.empty()) t.resize(kHighScores);
    }
    const Json& o = j["options"];
    c.options.lefty = {o["lefty_p1"].truthy(), o["lefty_p2"].truthy()};
    c.options.bandVolumeIdx = std::clamp(int(o["band_volume_idx"].num(11)), 0, 11);
    c.options.guitarVolumeIdx = std::clamp(int(o["guitar_volume_idx"].num(11)), 0, 11);
    c.options.fxVolumeIdx = std::clamp(int(o["fx_volume_idx"].num(11)), 0, 11);
    c.options.syncOffsetMs = int(o["sync_offset"].num());
    c.defaultName = j["default_name"].str();
    c.dummyNames = dummyNames;
    c.glitchesLook = glitchesLook;
    c.needsSave = false;
    c.ensureGlitches();  // an older save gets the band (and is saved again)
    *this = std::move(c);
    return true;
}

SaveFile::SaveFile(std::string dir) : dir_(dir.empty() ? prefDir() : std::move(dir)) { path_ = dir_ + "career.json"; }

bool SaveFile::exists() const {
    std::error_code ec;
    return std::filesystem::exists(path_, ec);
}

bool SaveFile::writable() const {
    std::error_code ec;
    return !dir_.empty() && std::filesystem::is_directory(dir_, ec);
}

bool SaveFile::write(const Career& career, std::string& error) const {
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            error = "cannot write " + tmp;
            return false;
        }
        f << career.toJson().dump(1) << "\n";
        f.flush();
        if (!f) {
            error = "cannot write " + tmp;
            return false;
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(path_, ec)) std::filesystem::copy_file(path_, path_ + ".bak", std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        error = "cannot replace " + path_ + ": " + ec.message();
        return false;
    }
    return true;
}

bool SaveFile::read(Career& career, bool& corrupt) const {
    corrupt = false;
    std::ifstream f(path_, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        Json j = Json::parse(ss.str());
        if (career.fromJson(j)) return true;
    } catch (const std::exception&) {
    }
    corrupt = true;
    return false;
}
