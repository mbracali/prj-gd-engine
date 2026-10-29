// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/qb.h"
#include "data/scene.h"

class Disc;
class Globals;

// A GH3 venue ("zone"): DATA/COMPRESSED/ZONES/<zone>.pak.xen, as the pak
// manager loads it for `SetPakManCurrentBlock map=zones pak=<zone>`
// (docs/venues.md). Rendering-free: the scenes (.scn), the node arrays
// (.nqb: where each object sits, the waypoints, the lights) and the zone's
// QB files (camera shots, light snapshots, light show mapping).
//
// World space is GH3's: y up, metres; a camera or light looks down its
// local +z [C data: docs/venues.md "Conventions"].
namespace venue {

using Vec3 = std::array<float, 3>;
using Quat = std::array<float, 4>;  // x, y, z, w

// A node's rotation from its `angles` (radians): q = qy * qz * qx [C data:
// every level light's `rotationquat` equals this product of its angles].
Quat quatFromAngles(const Vec3& angles);
Vec3 rotate(const Quat& q, const Vec3& v);
Quat mul(const Quat& a, const Quat& b);

struct Node {
    uint32_t name = 0, cls = 0, type = 0;
    Vec3 pos{}, angles{};
    Quat rot{0, 0, 0, 1};
    uint32_t lightGroup = 0;  // key; qbkey("none") for most
    bool createdAtStart = false;
    uint32_t createdOn = 0;   // createdonprogress / createdfromvariable: a node flag (0: none)
    const qb::Struct* st = nullptr;
};

enum class LightType : uint8_t { Ambient, Directional, Point, Spot };
struct Light {
    uint32_t name = 0, group = 0;
    LightType type = LightType::Point;
    Vec3 pos{}, dir{0, -1, 0};       // dir: where it shines, rotationquat x (0, -1, 0)
    Vec3 color{1, 1, 1};             // lightcolor (0..1)
    float intensity = 1, attenStart = 7.5f, attenEnd = 15, hotspot = 45, field = 45;
    float specular = 1;              // specularintensity
    float shadow = 1;                // shadowintensity (shadowdensity)
    bool dynamic = false;            // `dynamic`: lights every object (no box)
    bool shadowLight = false;        // `shadow`: ranked x 50000
    Vec3 boxCenter{}, boxHalf{};     // the box of the objects it may light
    bool vertexBake = false;         // a "_VC_" light (its name; lit at run time like any other) [C: 822BE020 makes no difference]
    bool createdAtStart = false;
    uint32_t createdOn = 0;
};

// One scene file's geometry, and where each of its sectors sits.
struct Part {
    std::string path;
    scene::Scene scene;
    // Per sector: the node placing it (-1: none) and whether its vertices
    // are local to that node (sector flag 0x20 [C data: levelobject sectors
    // have it and node-relative bounds; levelgeometry sectors are in world
    // space]).
    std::vector<int> sectorNode;
    std::vector<bool> sectorLocal;
};

struct Venue {
    std::string zone;     // "z_dive"
    uint32_t zoneKey = 0;
    std::vector<Part> parts;  // <zone>.scn, then <zone>_gfx.scn
    std::vector<Node> nodes;
    std::vector<Light> lights;
    std::vector<qb::File> files;  // the zone's QB files (scripts, cameras, snapshots)

    const Node* node(uint32_t name) const {
        auto it = nodeIndex.find(name);
        return it == nodeIndex.end() ? nullptr : &nodes[it->second];
    }
    // A section of the zone's QB files (null if none).
    const qb::Value* find(uint32_t name) const;
    // `<zone><suffix>` as a key (AppendSuffixToChecksum / FormatText).
    uint32_t key(const std::string& suffix) const;

    std::unordered_map<uint32_t, size_t> nodeIndex;
};

// The zone of a level key (LevelZones.<level>.name, e.g. load_z_dive ->
// "z_dive"); "" when unknown [C script get_level_prefix].
std::string zoneOfLevel(const Globals& g, uint32_t levelKey);

// Loads a zone; throws std::runtime_error.
std::unique_ptr<Venue> load(const Disc& disc, const std::string& zone);

// The venues the game lists (LevelZones without $debug_only), in order.
std::vector<std::string> venueZones(const Globals& g);

}  // namespace venue
