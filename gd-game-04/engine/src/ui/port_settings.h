// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

// The Enhanced Options' settings (gd01's keys, guidelines/universal_launcher.md
// "Enhanced options: parity"; gd02's table, gd-game-02/engine/src/ui/
// enhanced_panel.cpp), kept in enhanced.json in the save folder. Not GH3's.
//
// A setting's values: a choice's, "off" / "on" for a check box, or none for
// a number (ms) from `lo` to `hi`. The first value is the base default
// (gd01's enhancedDefaults); the build's profile (enhancedDefault) wins.
struct EnhancedSetting {
    const char* key;
    std::vector<const char*> values;
    int lo = 0, hi = 0;
};
const std::vector<EnhancedSetting>& enhancedSettings();
const EnhancedSetting* enhancedSetting(const std::string& key);
bool enhancedValid(const EnhancedSetting& s, const std::string& value);
// gd01's profileValues: desktop (Linux, Windows, macOS) or a handheld (the
// Steam Deck: `deck`; Android). Only the profile's keys.
std::vector<std::pair<std::string, std::string>> enhancedProfile(bool deck);
std::string enhancedDefault(const std::string& key, bool deck = false);

// The values, saved at once on every change (enhanced.json: {"version": 1,
// "settings": {key: value}}, written to a temporary file and renamed). A
// value missing or not valid keeps its default.
class EnhancedStore {
public:
    // `dir`: the folder, with a trailing separator ("" : nothing is saved).
    void load(const std::string& dir);
    std::string get(const std::string& key) const;
    bool on(const std::string& key) const { return get(key) == "on"; }
    void set(const std::string& key, const std::string& value);
    std::string path() const { return dir_.empty() ? std::string() : dir_ + "enhanced.json"; }

private:
    void save() const;
    std::string dir_;
    std::map<std::string, std::string> values_;
};
