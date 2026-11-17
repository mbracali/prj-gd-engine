// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The highway's models from STREAMS/COMMON.STR (docs/FORMATS.md "Graphics in
// the heap images"): the mesh objects, their materials and textures.
//
// Object pointers point 0x80 past the object as the file has it (COMMON's
// chunk bases 0x08F36600 / 0x091C32E0) [C: every mesh -> material ->
// texture -> block link below lands on an object header that way].
//
// Mesh object [C data, 50 in COMMON]: +0x00 u32 0x4D, +0x10 0x77359400,
// +0x30 f32[3] box min, +0x40 f32[3] box max, +0x68 name pointer, +0x6C
// type (2 skinned, 3 prop), +0x7C u32 n, +0x80 n x {material pointer, mesh
// block pointer}. The block's positions are normalized: the scale is the
// box's largest |coordinate| [C: x 127 / 32767 -> the box's extremes for
// every mesh checked, 8 and 16-bit].
// Material, 0x80+ bytes [C data]: +0x00 u32 0x53, +0x0C flags (byte 1: 1
// alpha-blended, 3 additive [I: the 3s are the glows, rails, track surface,
// beat bars, failed X, all with opaque grey / colour textures]; byte 2: 1
// on the track's Overdrive layer [I: hidden until Overdrive]), +0x1C
// texture pointer, name at +0x80.
// Model space: x across, y along the highway (+y away from the player [I]),
// z up [C: the gems' and plates' z >= 0, the track's y is its long side].
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "data/psp_gfx.h"

namespace gd24 {

class Umd;

struct HwSubMesh {
    std::string material, texture;
    uint32_t flags = 0;
    int textureIndex = -1;  // into HighwayAssets::textures
    PspMesh mesh;           // positions already scaled to model units
    bool additive() const { return ((flags >> 8) & 0xFF) == 3; }
    bool overdriveLayer() const { return ((flags >> 16) & 0xFF) == 1; }
};

struct HwModel {
    std::string name;
    int type = 0;
    float scale = 1, min[3] = {}, max[3] = {};
    std::vector<HwSubMesh> subs;
};

struct HighwayAssets {
    std::vector<PspTexture> textures;
    std::map<std::string, HwModel> models;

    bool load(const Umd& umd, std::string* error);
    const HwModel* model(const std::string& name) const;
    int texture(const std::string& name) const;  // -1: none
    // The strike line's lane marks on track_plane_A: the four lane centres
    // (x, left to right) and the line's y; false when not found.
    bool laneMarks(float laneX[4], float& strikeY) const;
};

}  // namespace gd24
