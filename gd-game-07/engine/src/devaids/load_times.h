// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <ctime>

// Load times (GD10-034): one log line per phase of a load the player waits
// on (start to the first frame, to the main menu, a song's request to its
// first note, a song's end to the set list), with the wall time and the
// process's CPU time (every thread) since the program started, so a busy
// machine's slow wall clock can be told from work done. Always on (a few
// lines a run).
namespace loadtimes {

inline double cpuMs() { return double(std::clock()) * 1000.0 / double(CLOCKS_PER_SEC); }

// "load: <what> at <wall> ms (cpu <cpu> ms)".
inline void mark(const char* what) {
    SDL_Log("load: %s at %.0f ms (cpu %.0f ms)", what, double(SDL_GetTicksNS()) / 1e6, cpuMs());
}

// A span: "load: <what> took <wall> ms (cpu <cpu> ms)" from begin() to end().
struct Span {
    double wall0 = -1, cpu0 = 0;
    void begin() { wall0 = double(SDL_GetTicksNS()) / 1e6, cpu0 = cpuMs(); }
    bool running() const { return wall0 >= 0; }
    double end(const char* what) {
        if (wall0 < 0) return -1;
        const double wall = double(SDL_GetTicksNS()) / 1e6 - wall0;
        SDL_Log("load: %s took %.0f ms (cpu %.0f ms)", what, wall, cpuMs() - cpu0);
        wall0 = -1;
        return wall;
    }
};

}  // namespace loadtimes
