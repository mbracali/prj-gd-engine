// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "data/venue.h"
#include "render/band.h"
#include "render/crowd.h"
#include "render/particles.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"
#include "render/venue_view.h"

class Disc;
class Globals;
struct Chart;
namespace gfx {
class RenderDevice;
}

// A song's venue: the zone (data/venue.h), its camera cuts and light show
// driven by the song clock, drawn behind the highway (ui/song_screen.cpp).
class VenueStage {
public:
    VenueStage() = default;
    ~VenueStage();
    VenueStage(const VenueStage&) = delete;
    VenueStage& operator=(const VenueStage&) = delete;
    // The zone for the song: GD04_VENUE=<zone> (development aid), else the
    // level the scripts chose ($current_level: quickplay_start_song's random
    // venue, the career tier's venue), else the first listed venue.
    static std::string chooseZone(const Globals& g);
    // Loads the zone (and uploads it when `device` is given); false and
    // `error` on failure. Without a device nothing draws until attach().
    bool start(const Disc& disc, const Globals& g, const std::string& zone, const Chart& chart, uint32_t songKey, double leadInMs,
               gfx::RenderDevice* device, std::string& error);
    // Uploads the venue to `device` (once).
    bool attach(gfx::RenderDevice& device, const Disc& disc, std::string& error);
    // Advances the cuts and the light show to song time `ms` (monotonic).
    // `health`: the rock meter, 0..2 (the crowd's performance [I]), as the
    // 60 Hz step at `ms` set it; call it on every step and as often between
    // steps as wanted (the result does not depend on it).
    void update(double ms, float health);
    // Advances to `ms` with the last step's performance (a frame between steps).
    void advance(double ms);
    // The player's judged note at step time `ms` (after update(ms)): the band's guitarist reacts.
    void playerNote(double ms, bool hit) {
        if (band_) band_->playerNote(ms, hit);
    }
    void draw(double ms);

    venue::CamPose camera(double ms) const { return cameras_.pose(ms); }
    const venue::CameraDirector& cameras() const { return cameras_; }
    const venue::LightShow& lights() const { return lights_; }
    const venue::Venue& venue() const { return *venue_; }
    const VenueView* view() const { return view_.get(); }
    // The band on the stage (render/band.h; GD04_NO_BAND=1: none); null if it could not load.
    const Band* band() const { return band_.get(); }
    Band* band() { return band_.get(); }
    static venue::Performance performance(float health);

private:
    std::unique_ptr<venue::Venue> venue_;
    venue::CameraDirector cameras_;
    venue::LightShow lights_;
    std::unique_ptr<VenueView> view_;
    std::vector<venue::LightState> lightState_;
    std::vector<uint32_t> hide_;  // the crowd cards' textures
    venue::PerfTimeline perf_;
    std::unique_ptr<Band> band_;
    std::unique_ptr<Crowd> crowd_;  // the crowd's peds and their card renders (GD04_NO_CROWD=1: none)
    std::unique_ptr<Particles> particles_;  // the zone's particles and pyro (GD04_NO_PARTICLES=1: none)
    gfx::RenderDevice* device_ = nullptr;
    gfx::TargetId shadow_;  // the band's dynamic shadow map (GD04_NO_SHADOWS=1: none)
    void shadowPass(double ms);
public:
    const Crowd* crowd() const { return crowd_.get(); }
    const Particles* particles() const { return particles_.get(); }
};
