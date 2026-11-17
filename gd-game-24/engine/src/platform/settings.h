// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gd24's settings (the ENHANCED screen's basics for now): key = value lines
// in enhanced.txt in the save folder. Unknown keys are kept.
#pragma once

#include <map>
#include <string>
#include <vector>

class Settings {
public:
    struct Option {
        const char* key;
        const char* label;
        std::vector<const char*> values;  // stored values
        std::vector<const char*> names;   // what the screen shows
        const char* def;                  // the default value
    };
    // screen_mode (windowed / borderless / fullscreen), volume (0..100),
    // language (the song names: EN FR GE IT SP).
    static const std::vector<Option>& options();

    void load(const std::string& dir);
    void save() const;
    std::string get(const std::string& key) const;
    void set(const std::string& key, const std::string& value);
    // The option's value one step left (-1) or right (+1), stopping at the ends.
    void cycle(const std::string& key, int dir);
    std::string shown(const Option& o) const;

private:
    std::string path_;
    std::map<std::string, std::string> values_;
};
