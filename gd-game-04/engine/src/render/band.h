// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "data/band.h"
#include "data/qb.h"
#include "render/band_anim.h"
#include "render/band_view.h"
#include "render/venue_camera.h"

class Disc;
class Globals;
struct Chart;
namespace gfx {
class RenderDevice;
}

// A song's band on the venue's stage (docs/band.md): create_band's members,
// each at its start waypoint (<zone>_TRG_Waypoint_<member>_Start,
// get_start_node_id), animated by the song clock (render/band_anim.h) and
// drawn with the venue's band lights (render/band_view.h). The cameras ask
// it where a member's bone is (lookat / lookatbone).
class Band {
public:
    // Chooses and loads the members (not uploaded until attach). `perf`: the
    // crowd performance timeline the animations read (may be null). False
    // and `error` when a member cannot be loaded.
    bool start(const Disc& disc, const Globals& g, const venue::Venue& v, const Chart& chart, uint32_t songKey,
               const venue::PerfTimeline* perf, double startMs, std::string& error);
    // Loads the given members instead of create_band's (--chartest).
    bool startWith(const Disc& disc, const Globals& g, const venue::Venue* v, const Chart& chart, const std::vector<band::Member>& members,
                   const venue::PerfTimeline* perf, double startMs, std::string& error);
    bool attach(gfx::RenderDevice& device, std::string& error);
    void advance(double ms);
    // The player's note judged at `ms` (a 60 Hz step): the player's band member's missed-note strum.
    void playerNote(double ms, bool hit);
    void draw(double ms, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
              const venue::LightShow& show);

    // The world position of `bone` of the member whose object is `member`
    // (Guitarist, Bassist, Drummer, vocalist) at the last advance; false if
    // there is no such member or bone.
    bool bonePos(uint32_t member, uint32_t bone, venue::Vec3& out) const;

    struct Entry {
        std::unique_ptr<band::Model> model;
        bandanim::Animator anim;
        bandanim::Mat34 root;  // model -> world: the waypoint's position and rotation, the profile's scale
        bool placed = false;   // its waypoint exists
        int view = -1;
        std::vector<uint32_t> lightGroups;
        std::vector<bandanim::Mat34> world, skin;  // at the last evaluate
        double evaluatedAt = -1e18;
    };
    const std::vector<Entry>& members() const { return members_; }
    std::vector<Entry>& members() { return members_; }
    const BandView* view() const { return view_.get(); }
    // Evaluates every member's pose at `ms` (after advance).
    void evaluate(double ms);

private:
    band::Library lib_;
    std::vector<Entry> members_;
    std::vector<qb::File> songFiles_;  // the song's .qb (its performance array)
    std::unique_ptr<BandView> view_;
};
