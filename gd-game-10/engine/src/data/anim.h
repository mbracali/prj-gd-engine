// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// GH3 skeletons (.ske) and animations (.ska), port of tools/anim.py (see its
// notes for the [C]/[I] marks). All parsers throw std::runtime_error on
// malformed data.
namespace anim {

using Quat = std::array<float, 4>;  // x, y, z, w
using Vec3 = std::array<float, 3>;

// Bind pose in the file's convention: with M(q) the usual (column-vector)
// rotation matrix of q, a bone's local matrix for row vectors is
// [M(q) 0; t 1], World = Local * World(parent), invBind = World^-1 [C: all 35].
struct Bone {
    uint32_t name = 0, parentName = 0, mirrorName = 0;  // QB keys (0: none)
    int parent = -1, mirror = -1;                        // indices (-1: none)
    uint8_t flags = 0;                                   // [I] bone class (5, 7 twist, 8 finger, ...)
    Vec3 pos{};
    Quat rot{0, 0, 0, 1};
    std::array<float, 16> invBind{};  // row-major, row vectors
};

struct Skeleton {
    std::vector<Bone> bones;
    int find(uint32_t nameKey) const;  // -1 when absent
};

Skeleton parseSkeleton(const uint8_t* data, size_t size);

struct RotKey {
    float frame = 0;
    Quat q{0, 0, 0, 1};
};
struct PosKey {
    float frame = 0;
    Vec3 v{};
};
struct Track {
    std::vector<RotKey> rot;  // empty: the bone is not animated in rotation
    std::vector<PosKey> pos;  // [I] offsets from the skeleton's bind translation
};

struct Clip {
    static constexpr float kFps = 60.0f;  // key times are frames [C: last key == duration * 60]
    uint32_t flags = 0;
    float duration = 0;     // seconds
    bool singlePose = false;  // flags & 0x10000000: one float key per track
    std::array<float, 4> headerA{}, headerB{};  // [I] meaning unknown
    std::vector<Track> bones;
    std::vector<uint8_t> mask;  // flags & 0x80000: 1 = bone driven by this clip; empty otherwise
};

Clip parseClip(const uint8_t* data, size_t size);

struct BonePose {
    Quat rot{0, 0, 0, 1};
    Vec3 pos{};
    bool hasRot = false, hasPos = false;  // false: track empty (use the bind pose)
};

// Local pose of every bone at `seconds` (clamped to the clip): rotations
// normalised-lerped along the shortest arc [I], translations lerped [I].
void sample(const Clip& clip, float seconds, std::vector<BonePose>& out);

}  // namespace anim
