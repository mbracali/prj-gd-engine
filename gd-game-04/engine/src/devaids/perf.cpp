// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "devaids/perf.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "render/device.h"

namespace perf {
namespace {

int level() {
    static const int v = [] {
        const char* e = SDL_getenv("GD04_PERF");
        return e && *e ? std::atoi(e) : 0;
    }();
    return v;
}

constexpr const char* kNames[Count] = {"tick", "venue-update", "adv-band", "adv-crowd", "adv-particles", "adv-show", "venue", "shadow", "crowd", "venue-meshes", "band", "peds", "lights",
                                       "particles", "screen-fx", "highway+hud", "scripts", "present"};
uint64_t gSlot[Count] = {};
gfx::RenderDevice* gDevice = nullptr;
uint64_t gStart = 0, gFrameNs = 0, gFrameMax = 0;
int gFrames = 0;

}  // namespace

bool on() { return level() > 0; }
bool gpuSync() { return level() > 1; }
void add(Slot s, uint64_t ns) { gSlot[s] += ns; }
void setDevice(gfx::RenderDevice* d) { gDevice = d; }

Scope::Scope(Slot s) : slot_(s) {
    if (!on()) return;
    if (gpuSync() && gDevice) gDevice->finishGpu();
    start_ = SDL_GetTicksNS();
}

void Scope::stop() {
    if (!start_) return;
    if (gpuSync() && gDevice) gDevice->finishGpu();
    gSlot[slot_] += SDL_GetTicksNS() - start_;
    start_ = 0;
}

void frameDone(uint64_t frameNs, const std::string& where) {
    if (!on()) return;
    const uint64_t now = SDL_GetTicksNS();
    if (!gStart) gStart = now;
    ++gFrames, gFrameNs += frameNs, gFrameMax = std::max(gFrameMax, frameNs);
    if (now - gStart < 2'000'000'000ull) return;
    const double n = gFrames, secs = double(now - gStart) / 1e9;
    std::string parts;
    for (int i = 0; i < Count; ++i) {
        if (!gSlot[i]) continue;
        char b[48];
        std::snprintf(b, sizeof b, " %s %.2f", kNames[i], double(gSlot[i]) / 1e6 / n);
        parts += b;
    }
    SDL_Log("perf (%s): %.1f fps, frame %.2f ms (max %.1f); ms a frame:%s", where.c_str(), n / secs, double(gFrameNs) / 1e6 / n,
            double(gFrameMax) / 1e6, parts.c_str());
    if (gDevice) {
        const gfx::DevicePerf p = gDevice->takePerf();
        const double f = std::max(1, p.frames);
        SDL_Log("perf device: %.0f draws, uniforms %.0f KB, buffers %.0f KB, textures %.0f KB, %.1f made a frame; present %.2f ms "
                "(pacing %.2f), GPU wait %.2f ms",
                double(p.draws) / f, double(p.uniformBytes) / 1024 / f, double(p.bufferBytes) / 1024 / f, double(p.textureBytes) / 1024 / f,
                double(p.created) / f, double(p.presentNs) / 1e6 / f, double(p.paceNs) / 1e6 / f, double(p.fenceNs) / 1e6 / f);
    }
    std::fill(std::begin(gSlot), std::end(gSlot), 0);
    gStart = now, gFrames = 0, gFrameNs = 0, gFrameMax = 0;
}

}  // namespace perf
