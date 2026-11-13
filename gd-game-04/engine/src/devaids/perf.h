// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>

namespace gfx {
class RenderDevice;
}

// GD04_PERF=1: a frame-time breakdown logged every 2 s (GD04-121): the
// frames a second, the frame's CPU time, the time spent in each part of a
// song's frame (perf::Scope) and what the device did (draws, uploads,
// present, the wait for the GPU). GD04_PERF=2 also finishes the GPU's work
// at each scope's ends, so a scope's time includes the GPU's for its draws
// (slower overall: for finding the costly passes, not for the frame rate).
namespace perf {

enum Slot : int {
    SongTick,       // the song's rules, input, audio sync (and VenueUpdate)
    VenueUpdate,    // VenueStage::update at each 60 Hz step: cameras, lights, crowd, band
    AdvBand,        // of the venue's updates (each step and each frame): the band's animation
    AdvCrowd,       // the crowd's
    AdvParticles,   // the particles'
    AdvShow,        // the cameras and the light show
    Venue,          // VenueStage::draw, all of it
    Shadow,         // the band's two shadow cascades
    Crowd,          // the crowd's impostor cards
    VenueMeshes,    // the venue's own meshes (VenueView::draw)
    Band,           // the band (skinning and draws)
    Peds,           // the stage peds
    Lights,         // projectors, volumes and raytraced beams
    Particles,      // particles and the Enhanced extras
    ScreenFx,       // GH3's screen effects and Enhanced's chain
    Highway,        // the 2D highway and HUD
    Scripts,        // GH3's scripts and screen elements over the song
    Present,        // Canvas::end: the 2D flush, the present, pacing
    Count
};

bool on();
// GD04_PERF=2: finish the GPU's work at each scope's ends.
bool gpuSync();
void add(Slot s, uint64_t ns);
void setDevice(gfx::RenderDevice* d);

class Scope {
public:
    explicit Scope(Slot s);
    ~Scope() { stop(); }
    void stop();  // ends it early
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    Slot slot_;
    uint64_t start_ = 0;
};

// Called once a frame by the app with the frame's CPU time (to the
// present); logs the breakdown every 2 s. `where`: the screen.
void frameDone(uint64_t frameNs, const std::string& where);

}  // namespace perf
