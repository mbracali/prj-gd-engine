// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "render/venue_camera.h"
#include "render/venue_lights.h"

class Canvas;
class Disc;
class MenuObjects;
class Globals;
class VenueView;
namespace qs {
class Vm;
}
namespace ui {
class UiSystem;
}
namespace venue {
struct Venue;
}

// GH3's menu 3D scenes: the zone the scripts made current (z_soundcheck:
// the store, the character select and hub) drawn behind the screen
// elements while the background viewport element (bg_viewport,
// setup_bg_viewport) is up, through the camera PlayIGCCam starts (a pose,
// or a control script morphing it with ccam_domorph / ccam_waitmorph)
// [C scripts: create_store_menu, store_camera_script, the character
// select's ch_view_cam], with the composite objects the scripts put in it
// (the characters, the store's guitar: ui/menu_objects.h); a camera pose
// may be locked to an object's bone (LockTo, locktobone).
class MenuScene {
public:
    MenuScene(const Disc& disc, Globals& globals, qs::Vm& vm, ui::UiSystem& ui);
    ~MenuScene();
    void addNatives();
    // The zones map's current zone (SetPakManCurrentBlock map = zones).
    void setZone(uint32_t zone) { zone_ = zone; }
    // A song plays (its band is the song's; the menus' objects wait).
    void setSuspended(bool on);
    void update(double dtMs);
    // Before the screen elements: the zone through the camera, if the
    // viewport element is shown and a camera plays.
    void draw(Canvas& c);

private:
    struct Pose {
        venue::Vec3 pos{};
        venue::Quat rot{0, 0, 0, 1};
        float fov = 72;
        uint32_t lockTo = 0, bone = 0;  // LockTo an object (its bone): pos / rot in its frame
    };
    static bool readPose(const void* params, Pose& out);
    const Disc& disc_;
    Globals& globals_;
    qs::Vm& vm_;
    ui::UiSystem& ui_;
    uint32_t zone_ = 0, loadedZone_ = 0;
    std::unique_ptr<venue::Venue> venue_;
    std::unique_ptr<VenueView> view_;
    venue::LightShow lights_;
    std::vector<venue::LightState> lightState_;
    bool failed_ = false;  // the zone did not load (logged once)
    // The camera: a morph from `from_` to `to_` over `duration_` ms.
    bool playing_ = false;
    uint32_t controlRun_ = 0;  // the control script's run
    Pose from_, to_;
    double t_ = 0, duration_ = 0, clockMs_ = 0;
    int motion_ = 0;
    Pose pose();
    Pose world(const Pose& p);
    // The current zone's nodes (GetWaypointPos, the objects); false if it does not load.
    bool ensureVenue();
    std::unique_ptr<MenuObjects> objects_;
};
