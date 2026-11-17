// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "platform/settings.h"

#include <algorithm>
#include <cstdio>
#include <fstream>

const std::vector<Settings::Option>& Settings::options() {
    static const std::vector<Option> kOptions = {
        {"screen_mode", "Screen mode", {"windowed", "borderless", "fullscreen"}, {"Windowed", "Full Screen (No Border)", "Full Screen"}, "windowed"},
        {"volume", "Volume", {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"},
         {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100%"}, "100"},
        {"language", "Song names", {"EN", "FR", "GE", "IT", "SP"}, {"English", "French", "German", "Italian", "Spanish"}, "EN"},
    };
    return kOptions;
}

void Settings::load(const std::string& dir) {
    path_ = dir.empty() ? std::string() : dir + "enhanced.txt";
    values_.clear();
    if (path_.empty()) return;
    std::ifstream in(path_);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#') continue;
        auto trim = [](std::string s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\r')) s.pop_back();
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            return s;
        };
        values_[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
}

void Settings::save() const {
    if (path_.empty()) return;
    std::ofstream out(path_, std::ios::trunc);
    out << "# gd24 settings (Enhanced)\n";
    for (const auto& [k, v] : values_) out << k << " = " << v << "\n";
}

std::string Settings::get(const std::string& key) const {
    if (auto it = values_.find(key); it != values_.end()) return it->second;
    for (const Option& o : options())
        if (key == o.key) return o.def;
    return "";
}

void Settings::set(const std::string& key, const std::string& value) { values_[key] = value; }

void Settings::cycle(const std::string& key, int dir) {
    for (const Option& o : options()) {
        if (key != o.key) continue;
        const std::string cur = get(key);
        int i = 0;
        for (size_t k = 0; k < o.values.size(); ++k)
            if (cur == o.values[k]) i = int(k);
        const int n = int(o.values.size());
        set(key, o.values[size_t(std::clamp(i + dir, 0, n - 1))]);
    }
}

std::string Settings::shown(const Option& o) const {
    const std::string cur = get(o.key);
    for (size_t k = 0; k < o.values.size(); ++k)
        if (cur == o.values[k]) return o.names[k];
    return cur;
}
