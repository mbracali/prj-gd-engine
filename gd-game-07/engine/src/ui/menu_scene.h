// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <future>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <string>
#include <vector>

#include "render/device.h"
#include "render/screen_fx.h"
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
    bool pointerToViewport(uint32_t viewport, float x, float y, float& vx, float& vy);
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
        float screenOffset[2] = {0, 0};  // screenoffset: the frustum's lens shift (x right, y down, half-screens)
        uint32_t lockTo = 0, bone = 0;  // LockTo an object (its bone): pos / rot in its frame
    };
    static bool readPose(const void* params, Pose& out);
    const Disc& disc_;
    Globals& globals_;
    qs::Vm& vm_;
    ui::UiSystem& ui_;
    uint32_t zone_ = 0, loadedZone_ = 0;
    std::shared_ptr<venue::Venue> venue_;
    std::shared_ptr<VenueView> view_;
    // The view loading (no-stutter rule): prepared on a worker, then uploaded
    // a slice a frame (VenueView::initStep); a zone left before it is in is
    // dropped once its worker is done (discard_).
    std::shared_ptr<VenueView> pendingView_;
    std::future<void> prepareWork_;
    std::vector<std::pair<std::future<void>, std::shared_ptr<VenueView>>> discard_;
    void dropPending();
    venue::LightShow lights_;
    std::vector<venue::LightState> lightState_;
    // Z_Soundcheck_LightShow's cycle: the menu zone steps its mood's snapshots
    // (first, second, Third, Fourth) every 2 s, blended over 0.1 s [C script];
    // armed when the lights init. Without it the menu froze on the first
    // snapshot (a static mood, fire-dominant) instead of the animated show the
    // 360 runs behind the main menu. GD07_MENU_NO_LIGHTCYCLE=1 freezes it.
    double nextSnapMs_ = -1;
    bool failed_ = false;  // the zone did not load (logged once)
    // Create / Kill {name = node} (SafeCreate, the store's Z_SoundCheck_G_RMM_Kill_me):
    // 1 created, 0 killed; EnableLightGroup / DisableLightGroup: the groups switched off.
    std::unordered_map<uint32_t, int> nodeVis_;
    // Level objects moved by the zone's scripts (World Tour's sound stage:
    // the menu board, the crowd boards drop in with obj_movetorelpos /
    // obj_movetopos, obj_locktoobject, Obj_WaitMove) [C scripts
    // z_soundcheck_scripts.qb; I: linear moves, translation only, as the songs'].
    struct NodeMove {
        venue::Vec3 from{}, to{};
        double m0 = 0, dur = 0;
        venue::Vec3 pos(double t) const;
    };
    std::unordered_map<uint32_t, NodeMove> moves_;
    std::unordered_map<uint32_t, uint32_t> locks_;  // node -> the node it follows
    bool nodeDelta(uint32_t node, venue::Vec3& d, int depth = 0) const;
    std::unordered_set<uint32_t> lightsOff_;
    // The camera: a morph from `from_` to `to_` over `duration_` ms.
    bool playing_ = false;
    uint32_t controlRun_ = 0;  // the control script's run
    uint32_t camName_ = 0;  // the PlayIGCCam camera's name (KillCamAnim)
    Pose from_, to_;
    double t_ = 0, duration_ = 0, clockMs_ = 0;
    int motion_ = 0;
    Pose pose();
    Pose world(const Pose& p);
    // The current zone's nodes (GetWaypointPos, the objects); false if it does
    // not load (or, without `wait`, is still loading on its worker).
    bool ensureVenue(bool wait = true);
    uint32_t loadingZone_ = 0;
    std::future<std::shared_ptr<venue::Venue>> venueWork_;
    std::vector<std::future<std::shared_ptr<venue::Venue>>> venueDiscard_;
    std::unique_ptr<MenuObjects> objects_;
    // bg_viewport's screen effects (setup_bg_viewport -> enable_bg_viewport_properties:
    // DOF_Off, Default_TOD_Manager's BnS and sbloom) and the characters'
    // dynamic shadow, as the songs' venue (render/venue_stage.cpp).
    screenfx::ScreenFx fx_;
    screenfx::Params fxParams_;
    bool fxTried_ = false, fxReady_ = false;
    gfx::RenderDevice* dev_ = nullptr;
    gfx::TargetId shadow_;
    // World Tour's UI viewport textures (create_viewport_ui): one 1280 x 720
    // target per overridden zone texture, drawn each frame before the zone.
    std::unordered_map<uint32_t, gfx::TargetId> uiTargets_;  // zone texture -> target
    void drawUiViewports(Canvas& c, gfx::RenderDevice& dev);
    void shadowPass(gfx::RenderDevice& dev);
    void releaseGpu();
};
