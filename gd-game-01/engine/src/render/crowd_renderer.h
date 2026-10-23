// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "scene/character.h"

class AnimPlayer;
class GameData;

// The venue crowd (arena/crowd.dta + the Crowd code at 0x170650..0x174db8;
// see FINDINGS.md "Crowd", docs/crowd.md and docs/crowd3d.md). Each venue's
// Crowd01..05.mm MultiMeshes place flat crowd members; Crowd0N.mm shows
// archetype N-1 (crowd_male01 ... crowd_female03) as a 128x256 impostor
// rendered from the animated 3D model every frame onto a quad
// crowd_flat_height tall.
//
// The `crowd instance` pool (10 to 17 characters, each with its head and
// outfit) stands as 3D members on the instances inside the active
// crowd_limitsNN region, whose sprites are removed from the flat blocks; the
// region follows each camera shot (its crowd_region, or the best-scoring one).
// How much of the crowd is shown and how it moves follow the excitement
// (update_crowd).
class CrowdRenderer {
public:
    // `venue` resolves the venue's objects; `venueName` picks the pool size.
    // Throws std::runtime_error when the venue has no crowd.
    CrowdRenderer(Renderer& renderer, const GameData& data, AnimPlayer& venue, const std::string& venueName);

    // `newShot`: a camera switch (switch_region with `region`, -1 = auto, then
    // update_crowd). `camera` / `projX` (1 / tan(fovX / 2)) score the regions.
    void update(float beat, float ms, int excitement, bool newShot, int region, const Xfm& camera, float projX);
    // Renders the impostors from the main camera's world transform (call
    // outside the main pass), then draw() draws the members and the camera-facing quads.
    void draw(const Mat4& viewProj);
    void renderImpostors(const Xfm& camera);
    // FUN_00172cc8: a random active member's position + (0, 0, 48), for the eyes.
    std::optional<std::array<float, 3>> randomMemberTarget();
    // crowd.dta's crowd_half_tempo / crowd_double_tempo / crowd_normal_tempo song
    // events: {crowd set_frame_scale 0.5 / 2 / 1} (FUN_001736c8).
    void tempoEvent(const std::string& event);
    // $cheat_crowd at venue load (long_cheats.dta; crowd.dta finish_loading):
    // 1 every member gets head 6 (monkey), 2 head 7 (skull), 0 their own.
    static void setCheatCrowd(int mode);

private:
    struct Member {
        std::unique_ptr<Character> character;
        std::vector<std::string> heads;
        int target = 0;  // impostor render target (archetypes 0..5)
        int hand = -1;
        bool visible = true;
    };
    struct Block {
        const MultiMeshObj* mm;
        int archetype;
        std::list<int> shown, hidden, removed;  // instance indices (FUN_00172ec8 / FUN_001727b0 splices)
    };
    struct Region {
        std::vector<std::pair<int, int>> insts;  // (block, instance)
        std::array<float, 3> centre{};
        float radius = 0, floorZ = 0;
    };

    void buildRegions();
    void switchRegion(int idx, const Xfm& camera, float projX);
    void setSizes(float full, float flat);
    void place(int member, size_t spot);
    void animate(const std::array<float, 4>& weights);
    void returnToPool(int member);

    Renderer& renderer_;
    AnimPlayer& venue_;
    std::vector<Member> members_;  // crowd0.. in creation order; 0..5 are the impostor archetypes
    std::list<int> pool_;          // LIFO: the last created first
    std::vector<int> active_;      // active_[i] stands on the region's instance i
    std::vector<int> leaving_;     // members cut at Boot: they stay until off screen
    std::map<std::string, uint64_t> lru_;  // main::crowd.cset, shared by every member
    uint64_t lruClock_ = 0;
    int excitement_ = 2;
    float frameScale_ = 1;
    std::vector<Block> blocks_;
    std::vector<Region> regions_;
    int region_ = -1;
    float full_ = 1, flat_ = 1;
    int quad_ = 0;
    float height_ = 100.8f;
    std::array<float, 9> camRot_{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<float, 9> crowdFrame_{1, 0, 0, 0, 1, 0, 0, 0, 1};  // M: the blocks' average facing
    std::mt19937 rng_{99};
};
