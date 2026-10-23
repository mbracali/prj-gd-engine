// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <random>
#include <string>
#include <vector>

#include "scene/scene.h"

class Character;
struct SongEvent;

// The guitarist's walks between the venue's walk spots (CharWalk at
// char+0xdc and GuitarCharacter_v13; docs/camera_walk_crowd_lighting.md §2,
// docs/character_polish_plan.md §4). The last spot is a hub every walk passes
// through. A walk is a turn clip, walk-cycle loops and a stop clip, laid out
// in 2-beat steps to cover the path (with a speed correction for the rest);
// the clips' bone_facing root motion moves the guitarist, steering keeps him
// on the path, and a pose hold keeps where he stopped across mood clips.
class GuitarWalk {
public:
    struct Config {
        bool allow = false;           // venues.dta allow_walks (and a single player)
        int soloPoint = -1;           // venues.dta solo_walk_point
        bool loseTeleport = false;    // venues.dta lose_teleport
        // charbase.dta walk_delays (min, max seconds) by excitement; min < 0 = never.
        std::array<std::array<float, 2>, 5> delays{{{-1, -1}, {-1, -1}, {35, 55}, {20, 40}, {-1, -1}}};
    };
    // `spots` are the walk_spot_NN worlds (normalised rows); `events` the song's EVENTS.
    GuitarWalk(std::vector<Xfm> spots, const Config& config, const std::vector<SongEvent>& events);

    // FUN_00281cd8: single player starts on walk_spot_01 (the solo point if the first solo is very early).
    int startSpot() const { return start_; }
    const Xfm& spot(int i) const { return spots_[size_t(i)]; }

    // Each frame before the mood logic (GuitarCharacter_v13 order): the random
    // walks and the pre-solo walks. Returns true while a walk is being planned
    // or its chain queued (states 1 and 2): the mood clips wait.
    // `state` is the guitarist state (0 idle, 1 playing, 2 wail, 3 solo, 4 game over).
    bool update(Character& c, float beat, float ms, float ticks, int state, int excitement,
                const std::vector<std::string>& events);
    // After the character poll: the walk poll (FUN_00197388: layout, chain
    // queueing, steering) and the pose hold (FUN_001958b0).
    void poll(Character& c, float beat);

    // FUN_00197548: from the turn becoming current through the stop clip.
    bool walking(const Character& c) const;
    // FUN_0018ef80: the nearest spot to the root, hub included (0-based).
    int nearestSpot(const std::array<float, 3>& pos, bool withHub = true) const;
    // FUN_001971e8: stops a walk (and drops its unstarted queued clip).
    void cancel(Character& c);
    // lose_teleport (arena_game.dta, the lose01 shot): within 72 units of the
    // singer's pelvis, snap to the nearest non-hub spot.
    void loseTeleport(Character& c, const std::array<float, 3>& singerPelvis);

private:
    using V3 = std::array<float, 3>;
    struct WalkClip {
        int clip;
        float start;  // the frame of this clip at which it enters
        float src;    // the frame of the previous clip at which it starts
    };
    bool startWalkTo(Character& c, int dest, int excitement, int mode);
    bool layout(Character& c);
    void steer(Character& c, float dBeat);
    void hold(Character& c, float dBeat);
    void guard(Character& c, float dBeat);

public:
    int state() const { return state_; }
    // Puts a standing guitarist exactly back on his spot; true when he moved.
    bool snapHome(Character& c);

private:
    void setSegment(int seg);
    void roll(float now);
    float yawOf(const Xfm& x) const;

    std::vector<Xfm> spots_;
    Config config_;
    std::mt19937 rng_{std::random_device{}()};
    int start_ = 0, hub_ = 0;
    std::vector<float> preSolo1_, preSolo2_;  // ticks
    size_t nextPre1_ = 0, nextPre2_ = 0;
    int pre1_ = 0, pre2_ = 0;
    float noWalkUntil_ = -1e30f;
    float lastReset_ = 0, now_ = 0;
    std::array<float, 5> rolled_{};
    // CharWalk (char+0xdc): state 0 idle, 1 planned, 2 queueing the chain, 3 stop queued.
    int state_ = 0, prevState_ = 0;
    std::vector<WalkClip> chain_;  // turn, walk (loops), stop
    std::vector<V3> path_;
    int k_ = 0, seg_ = 1, planClip_ = -1;
    V3 dir_{0, 1, 0};
    float corr_ = 0, destYaw_ = 0;
    // The pose hold (char+0xd8): position and facing kept across mood clips.
    struct Hold {
        bool hasPos = false, hasYaw = false;
        V3 pos{};
        float yaw = 0;
    } hold_;
    float lastBeat_ = 0;
    bool polled_ = false, gameOver_ = false;
    float busyBeats_ = 0;  // beats spent in a walk (the watchdog)
};

// [I] The port's standing guard: eases a character's root back to `pos` /
// `yaw` (a spot) and never lets it be more than 60 degrees or 15 units off,
// so the clips' root motion cannot turn a standing guitarist around.
void standGuard(Character& c, const std::array<float, 3>& pos, float yaw, float dBeat);
