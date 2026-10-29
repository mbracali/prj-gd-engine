// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/port_settings.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "data/json.h"

const std::vector<EnhancedSetting>& enhancedSettings() {
    static const std::vector<EnhancedSetting> k = {
        {"mouse", {"on", "off"}},
        {"touch", {"on", "off"}},
        {"rockband", {"off", "on"}},
        {"xinput", {"on", "off"}},
        {"screen_mode", {"windowed", "fullscreen", "borderless"}},
        {"max_fps", {"60", "120", "240"}},
        {"fps_counter", {"off", "on"}},
        {"internal_res", {"1080", "720", "800", "900", "1440", "2160"}},
        {"sharp_ui", {"off", "on"}},
        {"antialiasing", {"none", "fxaa", "ssaa", "taa"}},
        {"ai_upscaling", {"none"}},
        {"ai_upscaling_mode", {"quality", "balanced", "performance"}},
        {"bokeh", {"none", "regular", "stronger"}},
        {"particles", {"on", "off"}},
        {"bloom", {"off", "low", "medium", "high"}},
        {"cinematic_color", {"off", "on"}},
        {"color_grade", {"off", "warm", "cold", "vintage", "noir", "neon"}},
        {"film_grain", {"off", "light", "strong"}},
        {"chromatic", {"off", "on"}},
        {"light_streaks", {"off", "on"}},
        {"ssao", {"off", "on"}},
        {"sharpening", {"off", "low", "high"}},
        {"star_blur", {"none", "light", "medium", "high"}},
        {"shadows_all", {"off", "on"}},
        {"more_lights", {"off", "subtle", "strong"}},
        {"light_dust", {"off", "on"}},
        {"ground_fog", {"none", "low", "full", "super"}},
        {"no_shaky_cam", {"off", "on"}},
        {"no_fail", {"off", "on"}},
        {"autoplay", {"off", "on"}},
        {"hopo_mode", {"original", "forgiving"}},
        {"audio_offset", {}, -300, 500},
        {"video_offset", {}, -300, 500},
    };
    return k;
}

const EnhancedSetting* enhancedSetting(const std::string& key) {
    for (const EnhancedSetting& e : enhancedSettings())
        if (key == e.key) return &e;
    return nullptr;
}

bool enhancedValid(const EnhancedSetting& s, const std::string& value) {
    if (s.values.empty()) {
        char* end = nullptr;
        const long ms = std::strtol(value.c_str(), &end, 10);
        return !value.empty() && end && *end == 0 && ms >= s.lo && ms <= s.hi;
    }
    for (const char* v : s.values)
        if (value == v) return true;
    return false;
}

std::vector<std::pair<std::string, std::string>> enhancedProfile(bool deck) {
    // gd01's profileValues' rows (gd02's copy) [C: gd-game-01/engine/src/app.cpp].
    // Every enhancement off: the original look by default (owner,
    // 2026-10-29, GD04-029); the controls keep their defaults. Max FPS is the
    // original's 60. The platform rows below only change the window and the
    // touch controls.
    std::map<std::string, std::string> d = {
        {"antialiasing", "none"}, {"ai_upscaling", "none"}, {"bokeh", "none"}, {"particles", "on"}, {"cinematic_color", "off"},
        {"color_grade", "off"}, {"chromatic", "off"}, {"light_streaks", "off"}, {"more_lights", "off"}, {"no_shaky_cam", "off"},
        {"screen_mode", "windowed"}, {"max_fps", "60"}, {"fps_counter", "off"}, {"internal_res", "1080"}, {"sharp_ui", "off"},
        {"bloom", "off"}, {"film_grain", "off"}, {"ssao", "off"}, {"sharpening", "off"}, {"star_blur", "none"},
        {"shadows_all", "off"}, {"light_dust", "off"}, {"ground_fog", "none"}, {"touch", "off"},
    };
    auto set = [&](std::initializer_list<std::pair<const char*, const char*>> kv) {
        for (const auto& [k, v] : kv) d[k] = v;
    };
    auto handheld = [&] { set({{"screen_mode", "fullscreen"}}); };
#if defined(__ANDROID__)
    (void)deck;
    handheld();
    set({{"internal_res", "900"}, {"touch", "on"}});
#else
#if defined(_WIN32) || defined(__APPLE__)
    d["internal_res"] = "1440";
#endif
    if (deck) {
        handheld();
        set({{"internal_res", "800"}});
    }
#endif
    return {d.begin(), d.end()};
}

std::string enhancedDefault(const std::string& key, bool deck) {
    static const auto desktop = enhancedProfile(false), handheld = enhancedProfile(true);
    for (const auto& [k, v] : deck ? handheld : desktop)
        if (k == key) return v;
    const EnhancedSetting* s = enhancedSetting(key);
    if (!s) return {};
    return s->values.empty() ? "0" : s->values.front();
}

void EnhancedStore::load(const std::string& dir) {
    dir_ = dir;
    values_.clear();
    if (dir_.empty()) return;
    std::ifstream in(path(), std::ios::binary);
    if (!in) return;
    std::stringstream ss;
    ss << in.rdbuf();
    try {
        const Json root = Json::parse(ss.str());
        for (const auto& [k, v] : root["settings"].o) {
            const EnhancedSetting* s = enhancedSetting(k);
            const std::string value = v.type == Json::Type::Number ? std::to_string(long(v.n)) : v.str();
            if (s && enhancedValid(*s, value)) values_[k] = value;
        }
    } catch (const std::exception& e) {
        SDL_Log("enhanced: %s: %s (defaults kept)", path().c_str(), e.what());
    }
}

std::string EnhancedStore::get(const std::string& key) const {
    auto it = values_.find(key);
    return it != values_.end() ? it->second : enhancedDefault(key);
}

void EnhancedStore::set(const std::string& key, const std::string& value) {
    const EnhancedSetting* s = enhancedSetting(key);
    if (!s || !enhancedValid(*s, value)) return;
    values_[key] = value;
    save();
}

void EnhancedStore::save() const {
    if (dir_.empty()) return;
    Json root = Json::object(), settings = Json::object();
    root.set("version", Json::number(1));
    for (const auto& [k, v] : values_) settings.set(k, Json::string(v));
    root.set("settings", settings);
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    const std::string tmp = path() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << root.dump(2) << "\n";
        if (!out) return;
    }
    std::filesystem::rename(tmp, path(), ec);
    if (ec) SDL_Log("enhanced: cannot save %s: %s", path().c_str(), ec.message().c_str());
}
