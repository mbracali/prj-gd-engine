// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/band.h"
#include "data/qb.h"
#include "game/chart.h"
#include "render/band.h"
#include "render/venue_camera.h"
#include "render/venue_lights.h"
#include "script/vm.h"

class Disc;
class Globals;
class Pak;
class BandView;
namespace gfx {
class RenderDevice;
}
namespace venue {
struct Venue;
}

// GH3's composite objects as the menus use them (docs/ui.md §9): the
// characters of the character select, the hub, change outfit / guitar
// (create_guitarist -> create_band_member: CreateCompositeObject with an
// AnimTree, obj_initmodelfromprofile, anim_inittree) and the store's display
// guitar (store_do_guitar_load: a Skeleton + Model object turned by
// RotateGuitar) [C scripts]. Built on the song band's own pieces (one
// loader, one animator, one renderer): band::memberOf / band::loadModel,
// Band::Entry with bandanim::Animator (its body loop is GH3's
// hero_play_adjusting_random_anims / facial_anim_loop, so the scripts that
// would run them on the object are not run), Band::accessorySim, BandView.
// The objects' member functions (obj_*, anim_*, hero_*, Die, hide) go
// through the VM's objectCall ahead of the screen elements'.
class MenuObjects {
public:
    // `waypoint`: a node of the current zone (GetWaypointPos / Dir), false
    // when there is none.
    using WaypointFn = std::function<bool(uint32_t name, venue::Vec3& pos, venue::Quat& rot)>;
    MenuObjects(const Disc& disc, Globals& globals, qs::Vm& vm, WaypointFn waypoint);
    ~MenuObjects();
    void addNatives();
    // While a song plays the objects are kept but not loaded (the song's
    // band is render/band.h's); they load when the menus draw them again.
    void setSuspended(bool on) { suspended_ = on; }
    void update(double dtMs);
    // Draws the shown objects into the bound target, after the zone.
    void draw(gfx::RenderDevice& dev, const venue::CamPose& cam, const venue::Venue& v, const std::vector<venue::LightState>& lights,
              const venue::LightShow& show);
    // An object's frame (its bone's when `bone` is set: ccam LockTo /
    // locktobone), false if there is no such object or bone.
    bool frame(uint32_t object, uint32_t bone, venue::Vec3& pos, venue::Quat& rot);
    bool exists(uint32_t name) const { return objects_.count(name) != 0; }
    size_t count() const { return objects_.size(); }

private:
    struct Object {
        uint32_t name = 0, type = 0;  // object_type: bandmember, hud_model_3d
        venue::Vec3 pos{};
        venue::Quat rot{0, 0, 0, 1};
        float scale = 1;
        bool hidden = false;
        uint32_t skeleton = 0;     // the Skeleton component's SkeletonName
        bool animTree = false;     // an AnimTree component
        uint32_t lightGroup = 0;   // the Model component's / obj_initmodelfromprofile's lightgroup
        std::shared_ptr<qb::Struct> profile;  // obj_initmodelfromprofile's struct (flattened)
        bool treeWanted = false;   // anim_inittree
        bool loadFailed = false;
        float bodySpeed = 1;       // bodytimer's timer_setspeed
        double animMs = 0;         // the object's animation clock
        std::unique_ptr<Band::Entry> entry;  // the loaded model (and animator with a tree)
        bool animated = false;
    };
    bool member(qs::ScriptRun& run, Object& o, uint32_t method, qb::Struct& p, bool& handled);
    void load(Object& o);
    void unload(Object& o);
    void startAnim(Object& o);
    bandanim::Mat34 root(const Object& o) const;
    void evaluate(Object& o);

    const Disc& disc_;
    Globals& globals_;
    qs::Vm& vm_;
    WaypointFn waypoint_;
    std::function<bool(qs::Vm&, qs::ScriptRun&, uint32_t, uint32_t, qb::Struct&, bool&)> next_;  // the screen elements'
    std::unordered_map<uint32_t, std::unique_ptr<Object>> objects_;
    band::Library lib_;
    bool libLoaded_ = false, libFailed_ = false;
    std::unique_ptr<Pak> globalPak_;  // the ragdolls (Band::accessorySim)
    std::unique_ptr<BandView> view_;
    gfx::RenderDevice* dev_ = nullptr;
    std::vector<int> freed_;  // BandView members to free at the next draw
    Chart chart_;             // no song: no notes, no tempo (120 bpm) [I]
    double clockMs_ = 0;
    bool suspended_ = false;
};
