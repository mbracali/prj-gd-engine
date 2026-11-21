// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// GH3's geometry files (port of tools/scn.py; docs in the scn.py header):
// .scn (venues, scene parts), .mdl (props), .skin (characters, instruments),
// all one "scene" format, and .hkc (a Havok 4.6.0 binary packfile, parsed
// and validated only). Loader in the XEX: Function_822D7E78 @ 0x822d7e78
// (scene), Function_822B27E8 @ 0x822b27e8 (materials), Function_823B9A28 @
// 0x823b9a28 (vertex declaration from the mesh's vertex flags).
namespace scene {

enum class Kind { Scn, Mdl, Skin };

struct Material {
    uint32_t key = 0;     // material checksum (meshes refer to it)
    uint32_t shader = 0;  // template key, looked up at load (+0xa0)
    std::vector<std::array<float, 4>> constsA, constsB;  // two vec4 arrays (+0xa8/+0xac, +0xb0/+0xb4)
    std::vector<uint32_t> textures;  // texture keys, resolved in the parent .tex (+0xb8/+0xbc)
    // Raw words, meaning not known yet [I]: +0xc8 (passed with the template), +0xcc, +0xd0 (a key),
    // +0xd4 (float), +0xd8, +0xdc, +0xe8 (flags), +0xec, +0xf0.
    uint32_t c8 = 0, cc = 0, d0 = 0, d8 = 0, dc = 0, e8 = 0, ec = 0, f0 = 0;
    float d4 = 0;
};

struct Sector {
    uint32_t key = 0, flags = 0, index = 0xFFFFFFFF;
    std::array<float, 4> sphere{};
    std::array<float, 3> bmin{}, bmax{};  // from the sector's geom record
    uint32_t firstMesh = 0, meshCount = 0;
    uint32_t geom5c = 0;  // [I]
};

// One vertex element (D3D declaration usage codes: 0 position, 1 blend weight,
// 2 blend indices, 3 normal, 5 texcoord, 6 tangent, 7 binormal, 10 colour).
enum class ElemType : uint8_t { Float1, Float2, Float3, Float4, Half2, Half4, Hend3n, Udhen3n, Ubyte4, Color };
struct VertexElement {
    uint8_t stream = 0;  // 1 = the GPU stream of a skinned mesh (flag 0x80)
    uint16_t offset = 0;
    ElemType type = ElemType::Float3;
    uint8_t size = 0, usage = 0, index = 0;
};
std::vector<VertexElement> vertexDeclaration(uint32_t flags);

struct SkinGroup {  // CPU-skinning vertices sharing the same bones
    std::vector<uint8_t> bones;  // 1..3 bone indices
    uint32_t count = 0;
};

struct Mesh {
    std::array<float, 4> sphere{};
    uint32_t material = 0;  // Material::key
    uint32_t vertexFlags = 0;
    uint32_t word10 = 0, word18 = 0, word58 = 0, word68 = 0, word78 = 0;  // raw [I]
    std::vector<VertexElement> decl;
    uint32_t stride = 0;  // of the vertex block (stream 1 for skins)

    // Decoded vertices (count = positions.size()).
    std::vector<std::array<float, 3>> positions, normals;
    std::vector<std::array<float, 3>> positions2;               // second position (flags bits 12-13), rare
    std::vector<std::array<float, 3>> tangents[2], binormals[2];
    std::vector<std::array<uint8_t, 4>> colors;                 // r, g, b, a
    std::vector<std::array<float, 4>> texcoords[8];             // by usage index (Half2/Float2: z = w = 0)
    std::vector<std::array<uint8_t, 4>> blendIndices;           // skins: bones of the vertex (unused = 0)
    std::vector<std::array<float, 4>> blendWeights;             // skins: weights (sum 1)

    std::vector<uint16_t> indices;  // triangle strips; 0x7fff restarts a strip
    std::vector<SkinGroup> skinGroups;

    std::vector<std::array<uint32_t, 3>> triangles() const;  // strips -> list, degenerate ones dropped
};

struct Scene {
    Kind kind = Kind::Scn;
    std::vector<Material> materials;
    std::array<float, 3> bmin{}, bmax{};
    std::array<float, 4> sphere{};
    std::vector<Sector> sectors;
    std::vector<Mesh> meshes;
    std::vector<uint32_t> meshOrder;  // scene +0x48 table (a permutation of the meshes) [I: draw order]
};

// Throws std::runtime_error on anything the disc's files never contain
// (every header word, offset, padding byte and table is checked).
Scene parse(const uint8_t* data, size_t size, Kind kind);

struct HavokSection {
    std::string name;
    uint32_t start = 0, dataSize = 0, end = 0;
};
struct Havok {
    std::vector<HavokSection> sections;
    std::string contentsClass;                 // "hkPhysicsData"
    std::vector<std::string> objectClasses;    // class of each virtual fixup in __data__
};
// .hkc: packfile header, section table, class names and every fixup table checked.
Havok parseHavok(const uint8_t* data, size_t size);

}  // namespace scene
