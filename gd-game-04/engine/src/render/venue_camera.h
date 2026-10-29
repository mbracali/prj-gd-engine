// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "data/venue.h"
#include "game/chart.h"

class Globals;

// GH3's camera cuts during a song (scripts\guitar\guitar_cameras.qb,
// docs/venues.md "Camera cuts"), reimplemented as a function of the song
// clock: the shot sequence depends only on the chart, the venue's camera
// arrays and the performance at each cut, never on the frame rate. A cut
// happens at its exact song time (CameraCuts_WaitScript's change time or a
// camera note), however many frames the caller skips.
namespace venue {

enum class Performance : uint8_t { Poor, Medium, Good };

// The crowd's performance over song time, as the 60 Hz steps set it: a
// value holds from its step's time on, so decisions taken at any song time
// read the same value whatever the frame rate.
struct PerfTimeline {
    std::vector<std::pair<double, Performance>> changes;  // ascending times
    void record(double ms, Performance p) {
        if (changes.empty() || changes.back().second != p) changes.push_back({ms, p});
    }
    Performance at(double ms) const {
        Performance p = changes.empty() ? Performance::Medium : changes.front().second;
        for (const auto& [t, v] : changes) {
            if (t > ms) break;
            p = v;
        }
        return p;
    }
};

// A camera: position, rotation (looks down its local +z, y up) and the
// horizontal field of view in degrees [I: horizontal].
struct CamPose {
    Vec3 pos{};
    Quat rot{0, 0, 0, 1};
    float fov = 72;
};

class CameraDirector {
public:
    // `leadInMs`: the time before song time 0 the transition starts (the
    // intro transition's length: fastintro 3000 ms).
    void init(const Venue& v, const Globals& g, const Chart& chart, uint32_t seed, double leadInMs);
    // Advances to song time `ms` (monotonic), processing every camera note
    // and every cut up to it in time order (a cut picks its camera array by
    // the performance at its own time).
    void update(double ms, const PerfTimeline& perf);
    // The camera at song time `ms` (>= the last update's time).
    CamPose pose(double ms) const;

    struct Shot {
        double start = 0, end = 0;  // song ms; end: when CameraCuts_WaitScript lets it go
        uint32_t array = 0;         // the camera array's key
        int index = 0;
        uint32_t name = 0, type = 0, script = 0;
    };
    const Shot& shot() const { return shot_; }
    int cuts() const { return cuts_; }
    const std::vector<Shot>& history() const { return history_; }
    // Where a band member's bone is at a song time (render/band.h): the
    // cameras' lookat / lookatbone aim at it. Unset: the authored Quat.
    using BoneLookup = std::function<bool(uint32_t member, uint32_t bone, double ms, Vec3& out)>;
    void setBoneLookup(BoneLookup f) { lookup_ = std::move(f); }
    // The member and bone the shot playing frames (lookat / lookatbone of its first camera; 0: none).
    uint32_t lookatMember() const { return keys_.empty() ? 0 : keys_.front().lookat; }
    uint32_t lookatBone() const { return keys_.empty() ? 0 : keys_.front().lookatBone; }

private:
    struct Keyed {  // a camera position: pos / Quat / FOV in the frame of LockTo
        Vec3 pos{};
        Quat rot{0, 0, 0, 1};
        float fov = 72;
        uint32_t lockTo = 0;
        uint32_t lookat = 0, lookatBone = 0;  // lookat / lookatbone (0: none)
        float screenOffset[2] = {0, 0};       // screenoffset: where the bone sits on screen
        bool valid = false;
    };
    CamPose aim(const CamPose& p, const Keyed& k0, const Keyed& k1, float mix, double ms) const;
    Keyed readCam(const qb::Struct* st) const;
    CamPose world(const Keyed& k) const;
    uint32_t findArray(const std::string& prefix, const char* perf) const;
    void setDefaultCuts();
    void applyNote(const ChartEvent& e);
    void setArrayPrefix(const qb::Struct* params, int length);
    void startShot(double t);
    double calcTime(double t);
    uint32_t random(uint32_t n);
    const qb::Value* global(uint32_t name) const;

    const Venue* v_ = nullptr;
    const Globals* g_ = nullptr;
    std::vector<ChartEvent> notes_;
    std::vector<int32_t> downbeats_;  // lightshow note 58 [C script CameraCuts_GetNextDownbeat]
    size_t noteAt_ = 0, downbeatAt_ = 0;
    uint64_t rng_ = 1;
    double now_ = -1e18;
    const PerfTimeline* perf_ = nullptr;
    uint32_t targetNode_ = 0;

    // The scripts' globals (CameraCuts_*).
    std::string prefix_ = "cameras";
    uint32_t goodArray_ = 0, normalArray_ = 0, poorArray_ = 0;
    uint32_t forceType_ = 0, nextName_ = 0, lastType_ = 0;
    bool changeNow_ = false, enabled_ = true;
    double nextTimeMs_ = 0;       // CameraCuts_NextTime
    double changeTime_ = 1e18;    // CameraCuts_ChangeTime
    double transitionEnd_ = 0;    // EndWithDefaultCamera / SyncWithNoteCameras at song time 0
    bool transitionDone_ = false;

    // The shot playing.
    Shot shot_;
    int cuts_ = 0;
    std::vector<Shot> history_;
    enum class Ctl : uint8_t { Static, TwoCam, HandCam, HandCamZoom2, QuickZoom, Pan, Orbit, OrbitMorph, Spline };
    Ctl ctl_ = Ctl::Static;
    std::vector<Keyed> keys_;     // cam1 / cam2 / ... (or the single params pose)
    double calcAt_ = 0, camTime_ = 0;  // when CalcTime ran and the camera_time it returned (s)
    float zoomFov_ = 0;           // HandCam's random zoom target (0: none)
    float orbitRad_ = 0;          // total orbit angle
    bool orbit1_ = true, orbit2_ = true, handCam_ = false;
    BoneLookup lookup_;
};

}  // namespace venue
