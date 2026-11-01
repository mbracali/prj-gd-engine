// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "game/player.h"

// Scripted guitar input at song times, like gd01's GD01_KEYS "s<song ms>:<key>":
// "ms:key,ms:key,...", with key one of
//   +green / -green (also red, yellow, blue, orange): press / release a fret,
//   green (no sign): press, then release 1 ms later,
//   strum, starpower, whammy=<0..1> (the bar's value), +whammykey /
//   -whammykey (a digital whammy: a key or button held / let go).
// Items are applied in time order (ties keep their written order). Used by
// the self-test's rule checks and by GD03_KEYS (app.cpp).
class KeyScript {
public:
    KeyScript() = default;
    explicit KeyScript(const std::string& text) {
        size_t start = 0;
        while (start < text.size()) {
            size_t comma = text.find(',', start);
            std::string item = text.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            parseItem(item);
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        std::stable_sort(steps_.begin(), steps_.end(), [](const Step& a, const Step& b) { return a.t < b.t; });
    }

    bool empty() const { return steps_.empty(); }
    bool valid() const { return valid_; }
    float lastMs() const { return steps_.empty() ? 0.0f : steps_.back().t; }

    // Applies every step up to time `t`, each at its own timestamp (the
    // player is updated to that time first).
    void feed(Player& p, float t) {
        while (next_ < steps_.size() && steps_[next_].t <= t) {
            const Step& s = steps_[next_++];
            p.update(s.t);
            switch (s.kind) {
            case Step::Down: p.fretDown(s.fret, s.t); break;
            case Step::Up: p.fretUp(s.fret, s.t); break;
            case Step::Strum: p.strum(s.t); break;
            case Step::StarPower: p.starPowerButton(s.t); break;
            case Step::Whammy: p.setWhammyValue(s.value, s.t); break;
            case Step::WhammyKey: p.setWhammy(s.value > 0); break;
            }
        }
    }

private:
    struct Step {
        float t;
        enum Kind { Down, Up, Strum, StarPower, Whammy, WhammyKey } kind;
        int fret = -1;
        float value = 0;
    };

    void parseItem(const std::string& item) {
        size_t colon = item.find(':');
        if (colon == std::string::npos) {
            valid_ = valid_ && item.empty();
            return;
        }
        std::string at = item.substr(0, colon), name = item.substr(colon + 1);
        if (!at.empty() && at[0] == 's') at = at.substr(1);  // gd01's "s<song ms>" spelling
        const float t = std::strtof(at.c_str(), nullptr);
        if (name == "strum") return steps_.push_back({t, Step::Strum});
        if (name == "starpower") return steps_.push_back({t, Step::StarPower});
        if (name.rfind("whammy=", 0) == 0) return steps_.push_back({t, Step::Whammy, -1, std::strtof(name.c_str() + 7, nullptr)});
        if (name == "+whammykey" || name == "-whammykey") return steps_.push_back({t, Step::WhammyKey, -1, name[0] == '+' ? 1.0f : 0.0f});
        int edge = 0;
        if (!name.empty() && (name[0] == '+' || name[0] == '-')) edge = name[0] == '+' ? 1 : -1, name = name.substr(1);
        static const char* const kFrets[5] = {"green", "red", "yellow", "blue", "orange"};
        for (int f = 0; f < 5; ++f) {
            if (name != kFrets[f]) continue;
            if (edge >= 0) steps_.push_back({t, Step::Down, f});
            if (edge <= 0) steps_.push_back({edge == 0 ? t + 1 : t, Step::Up, f});
            return;
        }
        valid_ = false;
    }

    std::vector<Step> steps_;
    size_t next_ = 0;
    bool valid_ = true;
};
