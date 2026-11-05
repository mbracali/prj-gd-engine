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
    // The doc's ranges (universal_mechanisms.md 1.2.2, gd03's): the first
    // value is the default, the original game's look (GD04-029): windowed, 60
    // FPS, the window's resolution, every effect off (Bokeh Medium and
    // Particles Original are GH3's own). The controls start on.
    static const std::vector<EnhancedSetting> k = {
        {"mouse", {"on", "off"}},
        {"touch", {"off", "on"}},
        {"shake_whammy", {"on", "off"}},
        {"screen_mode", {"windowed", "fullscreen", "borderless"}},
        {"max_fps", {"60", "120", "144", "240", "unlimited"}},
        {"fps_counter", {"off", "on"}},
        {"hdr", {"none", "emulated", "true"}},
        {"internal_res", {"0", "720", "800", "900", "1080", "1440", "2160"}},
        {"sharp_ui", {"off", "on"}},
        {"antialiasing", {"none", "fxaa", "ssaa", "taa"}},
        {"ai_upscaling", {"none", "fsr1"}},
        {"ai_upscaling_mode", {"quality", "balanced", "performance"}},
        {"bokeh", {"medium", "none", "light", "strong", "stronger"}},
        {"particles", {"original", "none", "2x"}},
        {"bloom", {"none", "original", "2x", "3x"}},
        {"cinematic_color", {"off", "on"}},
        {"color_grade", {"off", "warm", "cold", "vintage", "noir", "neon", "sepia"}},
        {"film_grain", {"none", "light", "medium", "strong", "stronger"}},
        {"chromatic", {"none", "light", "medium", "strong"}},
        {"ssao", {"none", "light", "medium", "strong", "stronger"}},
        {"sharpening", {"off", "low", "high"}},
        {"star_blur", {"none", "light", "medium", "strong", "stronger"}},
        {"shadows_all", {"off", "on"}},
        {"more_lights", {"none", "1.5x", "2x", "2.5x"}},
        {"light_dust", {"none", "1.5x", "2x", "2.5x"}},
        {"no_shaky_cam", {"off", "on"}},
        {"no_effects", {"off", "on"}},
        {"no_fail", {"off", "on"}},
        {"autoplay", {"off", "on"}},
        {"dev_debug_menu", {"off", "on"}},
        {"hopo_mode", {"original", "forgiving"}},
        {"cut_content", {"off", "on"}},
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
    // The defaults (the first values: the original look) with the platform's
    // window and touch rows: a handheld starts full screen at 800p.
    std::map<std::string, std::string> d;
    for (const EnhancedSetting& e : enhancedSettings())
        if (!e.values.empty()) d[e.key] = e.values.front();
#if defined(__ANDROID__)
    (void)deck;
    d["screen_mode"] = "fullscreen", d["internal_res"] = "900", d["touch"] = "on";
#else
    if (deck) d["screen_mode"] = "fullscreen", d["internal_res"] = "800", d["touch"] = "on";
#endif
    return {d.begin(), d.end()};
}

namespace {
using Values = std::map<std::string, std::string>;
// Every effect at its lowest, at the original's look and at its highest (gd03's, gd01's).
const Values& lowest() {
    static const Values v = {{"bokeh", "none"},       {"particles", "none"},  {"bloom", "none"},       {"cinematic_color", "off"},
                             {"film_grain", "none"},  {"chromatic", "none"},  {"ssao", "none"},        {"star_blur", "none"},
                             {"shadows_all", "off"},  {"more_lights", "none"}, {"light_dust", "none"}, {"color_grade", "off"},
                             {"sharpening", "off"},   {"antialiasing", "none"}, {"ai_upscaling", "none"}, {"sharp_ui", "off"}, {"no_effects", "off"}};
    return v;
}
Values originalLook() {
    Values v = lowest();
    v["bokeh"] = "medium";  // GH3's own depth of field
    v["particles"] = "original";
    return v;
}
const Values& highest() {
    static const Values v = {{"bokeh", "stronger"},   {"particles", "2x"},      {"bloom", "3x"},        {"cinematic_color", "on"},
                             {"film_grain", "stronger"}, {"chromatic", "strong"}, {"ssao", "stronger"},   {"star_blur", "stronger"},
                             {"shadows_all", "on"},   {"more_lights", "2.5x"},   {"light_dust", "2.5x"}, {"color_grade", "off"},
                             {"sharpening", "low"},   {"antialiasing", "taa"},   {"ai_upscaling", "none"}, {"sharp_ui", "on"}, {"no_effects", "off"}};
    return v;
}
std::vector<std::pair<std::string, std::string>> with(Values base, std::initializer_list<std::pair<const char*, const char*>> over) {
    for (const auto& [k, v] : over) base[k] = v;
    return {base.begin(), base.end()};
}
}  // namespace

std::vector<std::pair<std::string, std::string>> enhancedPreset(const std::string& mode) {
    if (mode == "steam_deck")
        return with(originalLook(), {{"screen_mode", "fullscreen"}, {"max_fps", "60"}, {"internal_res", "800"}, {"bloom", "original"},
                                     {"cinematic_color", "on"}, {"star_blur", "light"}, {"antialiasing", "fxaa"}, {"sharp_ui", "on"},
                                     {"fps_counter", "off"}});
    if (mode == "original") return with(originalLook(), {{"max_fps", "60"}});
    if (mode == "clean")
        return with(lowest(), {{"internal_res", "1080"}, {"max_fps", "120"}, {"particles", "original"}, {"antialiasing", "fxaa"}, {"sharp_ui", "on"}});
    if (mode == "max_hd") return with(highest(), {{"internal_res", "1080"}, {"max_fps", "240"}});
    if (mode == "max_4k") return with(highest(), {{"internal_res", "2160"}, {"max_fps", "240"}});
    if (mode == "budget") return with(lowest(), {{"max_fps", "60"}, {"internal_res", "720"}});
    if (mode == "good_old_days")  // every effect, in sepia, at the Xbox 360's 720p
        return with(highest(), {{"color_grade", "sepia"}, {"internal_res", "720"}, {"max_fps", "60"}, {"antialiasing", "none"}});
    return {};
}

std::string migrateEnhancedValue(const std::string& key, const std::string& v) {
    // 0.0.3's values (before the universal menu), as gd03 maps gd02 0.1.7's.
    static const std::map<std::string, std::map<std::string, std::string>> m = {
        {"bokeh", {{"regular", "medium"}}},
        {"particles", {{"on", "original"}, {"off", "none"}}},
        {"bloom", {{"off", "none"}, {"low", "original"}, {"medium", "2x"}, {"high", "3x"}}},
        {"film_grain", {{"off", "none"}}},
        {"chromatic", {{"on", "medium"}, {"off", "none"}}},
        {"ssao", {{"on", "medium"}, {"off", "none"}}},
        {"star_blur", {{"high", "strong"}}},
        {"more_lights", {{"off", "none"}, {"subtle", "1.5x"}, {"strong", "2x"}}},
        {"light_dust", {{"on", "1.5x"}, {"off", "none"}}},
    };
    if (auto it = m.find(key); it != m.end())
        if (auto jt = it->second.find(v); jt != it->second.end()) return jt->second;
    return v;
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
            const std::string value = migrateEnhancedValue(k, v.type == Json::Type::Number ? std::to_string(long(v.n)) : v.str());
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
