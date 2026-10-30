// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/camera.h"
#include "scene/objects.h"
#include "scene/world_objects.h"
#include "script/script.h"

class World;
namespace scene {
class CamShot;
class PostProc;
}  // namespace scene

// GH2's camera director (WorldDir; docs/venues.md "Camera director"): the
// WorldDir's shot methods and CamShot playback. Which shot runs when is the
// venue's script (WORLD_OBJECT_BASE: intro_start_msg, downbeat /
// check_camera_shot with camera_durations by excitement, beat / check_shot,
// one_bar_to, band_jump, sync_*, lighters, game_won_msg, game_lost;
// camshot.dta: start_shot, shot_ok, check_shot, shot_over / next_shot);
// this answers it:
//
//   pick_shot cats [filter] (0x82268DE0 -> 0x82268CA8 -> 0x82268648): per
//     category (0x822682F8, per shot 0x82267748) the shots not used yet
//     that pass the filter ((prop value) or (prop (values...)) against the
//     shot's properties, 0x822660D8) and `shot_ok` (camshot.dta -> cam_shot_ok);
//     when none is left in a category its used shots pass again (a no-repeat
//     bag); one weighted by selection_weight (0x822662C0) is marked used and
//     started unless it is the current one. None: "No acceptable camera shot".
//   force_shot (0x82266A10), current_shot, cycle_shot (0x82266958), find_shot
//   (0x82268418), set_pick_shot / toggle_pick_shot, play_camera_sequence
//   (0x82265D58), toggle_free_cam (no free camera: picking pauses);
//   the functions cam_shot_ok (0x822B2538) and cam_check_shot (0x822B2240).
//
// Until the band exists (M9) the band queries come from the players:
// guitarist0 is "in Star Power" (clip flag 0x80000) while player 0's Star
// Power runs, never far (0x4000), and stands on its start waypoint.
//
// Playback (CamShot SetFrame 0x8222a068, 30 frames a second from the
// shot's start): keyframe i holds its duration, then blends over its blend
// to the next (0x82226448; the blend eased by its blend_ease, a sigmoid
// 0x8226F988); each keyframe's transform (0x82226988) is its xfm (with a
// path: the first keyframe's xfm under the path TransAnim, eased by
// path_ease), moved by its parent (low-passed by the shot's filter;
// translation only unless use_parent_rotation); the camera looks at its
// targets' mean with the keyframe's own up (0x82224BD8) and is moved in its
// own plane so they sit at screen_offset (0x82227440); field of view
// lerped, zoom_fov added; depth of field from the focal targets; the shake
// spring (0x82224D18); near / far. See frameShot / keyframeXfm.
class CameraDirector {
public:
    explicit CameraDirector(World& world, uint32_t seed = 0x43414d31u);
    ~CameraDirector();
    CameraDirector(const CameraDirector&) = delete;
    CameraDirector& operator=(const CameraDirector&) = delete;

    // Without GH2's menus (the world's own interpreter): `game` and
    // `gamecfg` for WORLD_OBJECT_BASE, which cuts only {if {exists game}}
    // (pick_new_shot): multiplayer, the tracks (guitar), no encore fx, the
    // mode (gamecfg get mode: `multi_vs` for face-off, else `single` /
    // `coop`), no campaign win. Ignored when the names exist.
    // `parts`: the players' instruments (game get_track_sym n); multiplayer_vs
    // is a competitive two-player mode (0x822B37F8).
    void provideGameObjects(bool multiplayer, const std::string& mode, std::array<std::string, 2> parts = {"guitar", "guitar"});

    // What the band would answer (M9): player 0's Star Power.
    void setStarPower(bool on) { starPower_ = on; }
    // The shots' camera shake shown (Enhanced Options > Disable Shaky Camera turns it off).
    void setShake(bool on) { shake_ = on; }
    bool shake() const { return shake_; }
    bool starPower() const { return starPower_; }

    // Once a frame after World::update: plays the current shot.
    void update();

    const scene::CamShot* shot() const { return current_; }
    const gfx::Camera& camera() const { return camera_; }
    bool hasCamera() const { return hasCamera_; }
    // The PostProc the shot selected (nullptr: reset_postproc).
    const scene::PostProc* postProc() const { return postProc_; }
    // Depth of field this frame (on: a focal target).
    struct Dof {
        bool on = false;
        float focus = 0, blurDepth = 0.5f, maxBlur = 0;
    };
    const Dof& dof() const { return dof_; }

    // pick_shot / force_shot from C++.
    const scene::CamShot* pickShot(const std::vector<std::string>& categories, const std::vector<std::pair<std::string, Value>>& filter);
    // The WorldDir's shot picker on its own (0x822682F8: weighted draw among
    // the unused acceptable shots, the pick marked used, +0xf6), without
    // starting the shot: GamePanel's start (0x822CCA60) times its intro clock
    // by the INTRO shot it picks this way [C].
    const scene::CamShot* drawShot(const std::vector<std::string>& categories, const std::vector<std::pair<std::string, Value>>& filter = {});
    void forceShot(const scene::CamShot* shot);
    bool camShotOk(const scene::CamShot& shot);
    bool camCheckShot(const scene::CamShot& shot);

    // Each shot started, as "shot <name> (<category>)" (the golden log).
    std::function<void(const std::string&)> onShot;
    // For the crowd: a shot started (its crowd, crowd3d, crowd_face_camera).
    std::function<void(const scene::CamShot&)> onShotStarted;
    int shotsStarted() const { return started_; }
    int failedPicks() const { return failedPicks_; }

    // The shot's camera at `frame` with its targets' current places, without
    // the filter and the shake (the viewer, --venuetest's camera checks).
    static gfx::Camera evaluate(const World& world, const scene::CamShot& shot, float frame);

private:
    struct ShotState {
        bool used = false;
        bool startedSent = false, overSent = false;
        double start = 0;  // seconds (or beats for rate 1 shots)
        // per keyframe: the filtered target and parent (+0x8c / +0xac), shake
        std::vector<std::array<float, 3>> target;
        std::vector<scene::Xfm> parent;
        std::vector<bool> filtered;
        float shakePos[3] = {}, shakeVel[3] = {}, shakeTgt[3] = {};
        float shakeAng[3] = {}, shakeAngVel[3] = {}, shakeAngTgt[3] = {};
        float shakeSteps = 0;  // 1/60 s frames not yet run
    };
    // A camera before the shake: transform, field of view, the keyframe pair.
    struct Framing {
        scene::Xfm xfm;
        float fov = 0.8f;
        bool valid = false;
        size_t a = 0, b = 0;
        float t = 0;
    };
    struct KeyframeCam {
        scene::Xfm xfm;
        std::array<float, 3> target{};
        bool hasTarget = false;
    };

    bool native(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r);
    bool objectNative(Interp& in, scene::Object& o, const std::string& msg, std::vector<Value>& args, Value& r);
    void startShot(const scene::CamShot* shot);
    void endShot();
    Value prop(const scene::CamShot& shot, const std::string& key);
    bool passes(const scene::CamShot& shot, const std::vector<std::pair<std::string, Value>>& filter);
    void play(const scene::CamShot& shot, ShotState& st, float frame, float dt);
    static bool targetPoint(const World& world, const scene::CamTarget& t, std::array<float, 3>& out);
    static void keyframeXfm(const World& world, const scene::CamShot& shot, ShotState* st, size_t k, float frame, const Framing* prev,
                            float dt, bool look, KeyframeCam& out);
    static void frameShot(const World& world, const scene::CamShot& shot, ShotState* st, float frame, const Framing* prev, float dt,
                          Framing& out, Dof* dof);
    double now(const scene::CamShot& shot) const;
    float random01();
    float shakeRandom01();
    uint32_t random();

    World& world_;
    std::vector<const scene::CamShot*> shots_;  // the world directory's, in order
    std::unordered_map<const scene::CamShot*, ShotState> state_;
    const scene::CamShot* current_ = nullptr;
    const scene::PostProc* postProc_ = nullptr;
    gfx::Camera camera_;
    Framing last_;  // the camera of the last frame (the filter's screen test)
    bool hasCamera_ = false;
    Dof dof_;
    bool starPower_ = false;
    bool shake_ = true;
    bool pickEnabled_ = true, freeCam_ = false;
    std::vector<const scene::CamShot*> sequence_;
    double nearestBeat_ = -1e30;               // cam_check_shot's nearest walk spot, once per beat
    const scene::Waypoint* nearest_ = nullptr;
    double lastSeconds_ = 0;
    uint32_t rng_;
    uint32_t shakeRng_ = 0x5348414bu;
    int started_ = 0, failedPicks_ = 0;
    std::unique_ptr<ScriptObject> rnd_, player0_, game_, gamecfg_;
    ScriptObject *prevRnd_ = nullptr, *prevPlayer0_ = nullptr;
};
