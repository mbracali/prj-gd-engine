// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "data/dta.h"
#include "render/math.h"
#include "scene/scene.h"

class AnimPlayer;

// The venue camera: shots from venues/<v>/gen/camera.dta ({set $camedit.<group>
// ({arena switch_cam <path> <shot> (start ..) ...} ...)}) on the paths of
// campaths.rnd_ps2, chosen the way arena/gen/camera.dta does (intro shot,
// then a new shot from a weighted pool every few bars, solo shots before a
// solo). Shot math follows VenueCam (FUN_0016f3b8 switch, FUN_0016e390 per
// frame); see FINDINGS.md "Venue camera".
class VenueCamera {
public:
    // `camPaths` is arena/gen/cam_paths.dtb; `durations` the venue's
    // camera_durations (bars, per excitement) from arena/gen/venues.dtb.
    VenueCamera(const DataNode& cameraScript, const DataNode& camPaths, const DataNode* durations);

    bool hasShots() const { return !groups_.empty(); }
    // Resolves objects of other namespaces ("guitarist0::spot_neck_fret20.mesh").
    using Lookup = std::function<const SceneObject*(const std::string&)>;
    void setCharacterLookup(Lookup lookup) { characters_ = std::move(lookup); }

    void start(AnimPlayer& anims, float ms, float ticks);
    // Tutorials (camera_tutorial.dta intro_skip): the group's first shot, kept
    // for the whole session (no picking).
    void setFixedGroup(const std::string& group) { fixedGroup_ = group; }
    // camera_multiplayer.dta active_players_changed: 0 / 3 MULTIPLAYER, 1
    // MULTIPLAYER_0 (P1's close shot), 2 MULTIPLAYER_1.
    void activePlayers(int mask);
    // Song events for the director: a downbeat, and one bar before a section.
    void downbeat(int excitement);
    void upcomingSection(const std::string& section, int excitement);
    void excitementChanged() {
        if (!ended_) aboutToSwitch_ = true;
    }
    // arena/gen/camera.dta game_over: FINAL_WIN at once, WIN after 1750 ms,
    // LOSE at once; the director stops picking (bars_left 100).
    void gameOver(bool won, bool finalWin, bool grim);
    // Called with the shot name before every switch (the shots' pre-switch events, e.g. lose_teleport).
    std::function<void(const std::string&)> preSwitch;
    // What the native shot rules look at (CameraSystem FUN_00110040).
    struct State {
        bool starMode = false;      // player 0 in Star Power: no shot changes on downbeats
        bool specialClip = false;   // the guitarist plays a kGuitarSpecial clip: Near shots only
        bool walking = false;       // the guitarist is walking: shots with walk_ok 0 are out
        int walkSpot = 0;           // nearest walk spot (0-based, hub included)
        int excitement = 2;
    };
    void setState(const State& s) { state_ = s; }
    // The beat handler (one call per beat line, before downbeat and one_bar_to):
    // beat_check and the excitement re-pick.
    void beat(int beatsLeft, int beatNumber, int excitement);
    // The venue's intro_camera_flags (the side the first picked shot must avoid).
    void setIntroFlags(unsigned flags) { introFlags_ = flags; }
    bool hideCrowd() const { return current_.hideCrowd; }

    // The camera's view-projection at song time `ms` (`ticks`).
    Mat4 viewProj(float ms, float ticks, float aspect);
    const std::string& shotName() const { return current_.name; }
    int eyesMode() const { return current_.eyes; }
    int crowdRegion() const { return current_.crowdRegion; }
    // The projection's x scale (1 / tan(fovX / 2)) of the last viewProj.
    float projX() const { return projX_; }
    // Depth of field (docs/render_gems_dof.md §4): the window depth (0..1) of
    // the focus plane at twice the camera-to-subject distance, or 0 when off.
    float dofDepth() const { return dofDepth_; }
    // arena set_depth_of_field B overrides the shot's setting until the next shot.
    void setDepthOfField(bool on) { dofOverride_ = on ? 1 : 0; }
    // Enhanced Settings "Stronger Bokeh": the focus plane nearer the subject
    // (the original focuses at twice its distance).
    void setFocusScale(float s) { focusScale_ = s; }
    // Off: the (shaky 1) shots hold still (Enhanced Settings "Disable Shaky Camera").
    void setShakyCamera(bool on) { shakyEnabled_ = on; }
    // The camera's world transform from the last viewProj (rows right, forward, up).
    const Xfm& transform() const { return xfm_; }

private:
    struct Shot {
        std::string path, name;
        float start = 0, end = 100, duration = 0;
        bool loop = false, realTime = false;
        std::array<float, 3> offsetIn{}, offsetOut{};
        std::array<float, 2> singerIn{}, singerOut{};
        float nearPlane = 10, farPlane = 10000;
        float fovIn = 0.785398f, fovOut = 0.785398f;  // radians
        float ease = 0;
        bool dof = false;  // enable_dof
        bool walkOk = true, lowExcitementOk = true, hideCrowd = false;
        bool shaky = false;
        int eyes = 3;  // the guitarist's eye mode (default 3, crowd)
        int crowdRegion = -1;  // crowd switch_region (-1 = the best-scoring region)  // shaky_cam1.tnm while the shot lasts (FUN_0016f248)
        std::vector<int> badWalkSpots;
        unsigned flags = 0;  // $camera.groups
        bool fresh = true;   // not yet used in this round of its pool
    };
    struct Pool {
        float weight;
        std::vector<Shot*> shots;
    };
    struct PathInfo {
        std::string parent, target;  // object names; empty = the band focus
    };

    void switchTo(const Shot& shot);
    void pick(unsigned wanted);
    bool valid(const Shot& s) const;
    bool compatible(const Shot& s) const;
    bool compatibleNow(const Shot& s) const;
    int barsFor(int excitement);
    float shotFrame(float t) const;
    std::array<float, 3> focus();
    bool worldOf(const std::string& name, Xfm& out);

    std::map<std::string, std::vector<Shot>> groups_;
    std::map<std::string, PathInfo> paths_;
    std::vector<std::array<int, 2>> durations_;
    AnimPlayer* anims_ = nullptr;
    Lookup characters_;
    std::mt19937 rng_{std::random_device{}()};
    std::vector<Pool> pools_;
    State state_;
    std::array<float, 3> focus_{};
    bool focusSet_ = false;
    Mat4 lastVp_;
    float lastMs_ = 0;

    Shot current_;
    const Shot* currentShot_ = nullptr;
    const Shot* pickerCurrent_ = nullptr;  // the last shot the picker chose (CameraSystem current)
    unsigned introFlags_ = 0;
    std::string fixedGroup_;
    int beatNumber_ = 0;
    Xfm xfm_;
    float t0_ = 0, t1_ = 0, f0_ = 0, f1_ = 0;  // the shot's frame task
    float ms_ = 0, ticks_ = 0;
    int barsLeft_ = 0;
    bool solo_ = false, aboutToSwitch_ = false, started_ = false;
    float dofDepth_ = 0;
    int dofOverride_ = -1;
    float focusScale_ = 2.0f;
    bool ended_ = false;
    float winAtMs_ = -1;
    // Shaky cam: on for the current shot, its start on the real-time clock, this frame's shake.
    bool shakeOn_ = false, shakyEnabled_ = true;
    float shakeStartMs_ = 0;
    std::array<float, 3> shake_{};
    float lastShakeMs_ = 0;
    float projX_ = 1;
};
