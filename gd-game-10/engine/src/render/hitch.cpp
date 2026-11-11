// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/hitch.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

namespace hitch {
namespace {

constexpr double kSliceMs = 8.0;

struct State {
    std::thread::id renderThread;
    uint64_t frameStart = 0;
    bool frameOpen = false;
    std::vector<std::tuple<const char*, double, int>> scopes;  // this frame's: name, ms, count
    int depth = 0;
    double logMs = -1;  // GD10_HITCHLOG
    double lastWork = 0;
    bool sliceTaken = false;  // a load's piece ran this frame
    int hitches = 0;
    double longest = 0;
    std::string longestAt;
    std::map<std::string, std::pair<int, double>> byScreen;
};

State& st() {
    static State s;
    return s;
}

bool onRenderThread() { return st().frameOpen && std::this_thread::get_id() == st().renderThread; }

}  // namespace

void frameBegin() {
    State& s = st();
    if (s.logMs < 0) {
        const char* v = SDL_getenv("GD10_HITCHLOG");
        s.logMs = v && *v ? std::max(0.0, std::atof(v)) : 0;
    }
    s.renderThread = std::this_thread::get_id();
    s.frameStart = SDL_GetTicksNS();
    s.frameOpen = true;
    s.scopes.clear();
    s.depth = 0;
    s.sliceTaken = false;
}

double frameMs() { return st().frameOpen ? double(SDL_GetTicksNS() - st().frameStart) / 1e6 : 0; }

bool slice() {
    if (!onRenderThread()) return true;
    State& s = st();
    const bool ok = !s.sliceTaken || frameMs() < kSliceMs;
    s.sliceTaken = true;
    return ok;
}

void frameEnd(const char* screen) {
    State& s = st();
    if (!s.frameOpen) return;
    const double ms = frameMs();
    s.frameOpen = false;
    s.lastWork = ms;
    for (const auto& [n, t, c] : s.scopes)
        if (std::strcmp(n, "present") == 0) s.lastWork -= t;
    if (s.logMs > 0 && ms > s.logMs) {
        ++s.hitches;
        auto& b = s.byScreen[screen ? screen : "-"];
        ++b.first, b.second = std::max(b.second, ms);
        if (ms > s.longest) s.longest = ms, s.longestAt = screen ? screen : "-";
        std::string parts;
        for (const auto& [n, t, c] : s.scopes)
            if (t >= 1.0)
                parts += std::string(parts.empty() ? "" : ", ") + n + (c > 1 ? " x" + std::to_string(c) : std::string()) + " " +
                         std::to_string(int(t + 0.5));
        SDL_Log("hitch: %.1f ms on %s%s%s%s", ms, screen ? screen : "-", parts.empty() ? "" : " (", parts.c_str(), parts.empty() ? "" : ")");
    }
}

double lastWorkMs() { return st().lastWork; }

void report() {
    State& s = st();
    if (s.logMs <= 0) return;
    SDL_Log("hitches: %d frames over %.0f ms, longest %.1f ms (%s)", s.hitches, s.logMs, s.longest, s.longestAt.c_str());
    for (const auto& [scr, b] : s.byScreen) SDL_Log("hitches on %s: %d, longest %.1f ms", scr.c_str(), b.first, b.second);
}

Scope::Scope(const char* name) : name_(name), t0_(onRenderThread() ? SDL_GetTicksNS() : 0) {
    if (t0_) ++st().depth;
}

Scope::~Scope() {
    if (!t0_ || !onRenderThread()) return;
    State& s = st();
    --s.depth;
    const double ms = double(SDL_GetTicksNS() - t0_) / 1e6;
    for (auto& [n, t, c] : s.scopes)
        if (n == name_) {
            t += ms, ++c;
            return;
        }
    s.scopes.emplace_back(name_, ms, 1);
}

}  // namespace hitch
