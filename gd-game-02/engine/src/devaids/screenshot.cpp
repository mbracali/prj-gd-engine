// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "devaids/screenshot.h"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "render/canvas.h"

namespace {

// GD02_SCREENSHOT_AT's song times ("15000" or "15000,15100,...").
std::vector<double> shotTimes() {
    std::vector<double> v;
    const char* at = SDL_getenv("GD02_SCREENSHOT_AT");
    for (const char* p = at; p && *p;) {
        char* end = nullptr;
        const double t = std::strtod(p, &end);
        if (end == p) break;
        v.push_back(t);
        p = *end == ',' ? end + 1 : end;
    }
    return v;
}

}  // namespace

bool captureDebugScreenshot(Canvas& canvas, bool inSong, double songMs) {
    const char* file = SDL_getenv("GD02_SCREENSHOT");
    if (!file) return false;
    static const std::vector<double> times = shotTimes();
    static size_t next = 0;
    std::string path = file;
    if (!times.empty()) {
        if (!inSong || next >= times.size() || songMs < times[next]) return false;
        // Several times: <file>_<ms>.bmp each, quitting after the last.
        if (times.size() > 1) {
            const size_t dot = path.rfind('.');
            const std::string tag = "_" + std::to_string(long(times[next]));
            path = dot == std::string::npos ? path + tag : path.substr(0, dot) + tag + path.substr(dot);
        }
        while (next < times.size() && songMs >= times[next]) ++next;
    }
    if (SDL_Surface* shot = canvas.readPixels()) {
        if (!SDL_SaveBMP(shot, path.c_str())) SDL_Log("GD02_SCREENSHOT: cannot save %s: %s", path.c_str(), SDL_GetError());
        else SDL_Log("GD02_SCREENSHOT: saved %s at song time %.0f ms", path.c_str(), songMs);
        SDL_DestroySurface(shot);
    } else {
        SDL_Log("GD02_SCREENSHOT: cannot read the frame: %s", SDL_GetError());
    }
    return times.empty() || next >= times.size();
}

bool saveScreenshot(Canvas& canvas, const std::string& path) {
    SDL_Surface* shot = canvas.readPixels();
    if (!shot) return false;
    const bool ok = SDL_SaveBMP(shot, path.c_str());
    SDL_DestroySurface(shot);
    return ok;
}

bool captureMenuScreenshot(Canvas& canvas, double wallMs, double songMs) {
    const char* file = SDL_getenv("GD02_SCREENSHOT");
    if (!file) return false;
    struct Shot {
        double at;
        bool song;
        std::string tag;
    };
    static std::vector<Shot> shots = [] {
        std::vector<Shot> v;
        const char* at = SDL_getenv("GD02_SCREENSHOT_AT");
        for (const char* p = at; p && *p;) {
            const bool song = *p == 's';
            if (song) ++p;
            char* end = nullptr;
            const double t = std::strtod(p, &end);
            if (end == p) break;
            v.push_back({t, song, std::string(song ? "s" : "") + std::to_string(long(t))});
            p = *end == ',' ? end + 1 : end;
        }
        return v;
    }();
    static size_t next = 0;
    std::string path = file;
    if (!shots.empty()) {
        if (next >= shots.size()) return true;
        const Shot& s = shots[next];
        if (s.song ? (songMs < -1e8 || songMs < s.at) : wallMs < s.at) return false;
        if (shots.size() > 1) {
            const size_t dot = path.rfind('.');
            path = dot == std::string::npos ? path + "_" + s.tag : path.substr(0, dot) + "_" + s.tag + path.substr(dot);
        }
        ++next;
    }
    if (saveScreenshot(canvas, path)) SDL_Log("GD02_SCREENSHOT: saved %s", path.c_str());
    else SDL_Log("GD02_SCREENSHOT: cannot save %s: %s", path.c_str(), SDL_GetError());
    return shots.empty() || next >= shots.size();
}
