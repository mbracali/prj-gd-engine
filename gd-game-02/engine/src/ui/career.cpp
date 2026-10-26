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
    items.clear();
    options = Options{};
    defaultName.clear();
    highScores_.clear();
    autosave = 1;
    needsSave = false;
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
    j.set("format", Json::string("gd02-save"));
    j.set("version", Json::number(1));
    j.set("gh2_save_version", Json::number(kVersion));
    j.set("autosave", Json::number(autosave));
    Json ps = Json::array();
    for (const Profile& p : profiles) {
        Json pj = Json::object();
        pj.set("in_use", Json::boolean(p.inUse));
        pj.set("name", Json::string(p.name));
        pj.set("cash", Json::number(p.cash));
        pj.set("tutorials_done", Json::number(p.tutorialsDone));
        Json st = Json::array(), res = Json::array();
        for (int d = 0; d < kDifficulties; ++d) {
            st.a.push_back(Json::number(p.status[size_t(d)]));
            Json r = Json::object();
            for (const auto& [song, sr] : p.results[size_t(d)]) {
                Json e = Json::object();
                e.set("score", Json::number(sr.score));
                e.set("stars", Json::number(sr.stars));
                e.set("passed", Json::boolean(sr.passed));
                r.set(song, e);
            }
            res.a.push_back(r);
        }
        pj.set("status", st);
        pj.set("results", res);
        ps.a.push_back(pj);
    }
    j.set("profiles", ps);
    Json it = Json::object();
    for (const auto& [k, v] : items) it.set(k, Json::boolean(v));
    j.set("items", it);
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
    if (j.type != Json::Type::Object || j["format"].str() != "gd02-save" || j["version"].num() > 1) return false;
    Career c;
    c.autosave = int(j["autosave"].num(1));
    const Json& ps = j["profiles"];
    for (size_t i = 0; i < ps.a.size() && i < size_t(kProfiles); ++i) {
        const Json& pj = ps.a[i];
        Profile& p = c.profiles[i];
        p.inUse = pj["in_use"].truthy();
        p.name = pj["name"].str();
        p.cash = int(pj["cash"].num());
        p.tutorialsDone = int(pj["tutorials_done"].num());
        for (size_t d = 0; d < size_t(kDifficulties); ++d) {
            if (d < pj["status"].a.size()) p.status[d] = int(pj["status"].a[d].num());
            if (d < pj["results"].a.size())
                for (const auto& [song, e] : pj["results"].a[d].o)
                    p.results[d][song] = {int(e["score"].num()), int(e["stars"].num()), e["passed"].truthy()};
        }
    }
    for (const auto& [k, v] : j["items"].o) c.items[k] = v.truthy();
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
