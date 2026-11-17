// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The graphics objects of the STREAMS heap images (docs/FORMATS.md
// "Graphics in the heap images"): textures and GE display-list meshes, found
// by their own headers in a chunk loaded at a known address.
//
// Texture object, 0x54 bytes [C data, COMMON.STR's 46]: +0x00 u32 0x54,
// +0x04 u16 width, height, +0x0E u8 bits per pixel (4 or 8), +0x11 u8 flags,
// +0x20 u8 log2 sizes, +0x30 list node, name at +0x3D; the pixels follow at
// +0x50, PSP-swizzled (16-byte x 8-row blocks), then the palette (RGBA8888,
// alpha 0..0x80 = 0..1) [C: the decoded images are the gems, rails, badges].
//
// Mesh block [C data]: {u32 size, u32 vertices, u32 primitives, u32 0, u32
// vertex data offset, 1, -1, ...} (skinned meshes: {..., 1, offset, -1}), a GE display list (VTYPE, PRIM..., RET)
// after it, the vertices at the offset. Vertex formats follow the GE's VTYPE
// rules (weights, UV, colour, normal, position; each aligned to its size).
// Positions are normalized (8 / 16-bit: -1..1); the mesh object's bounds
// give the scale [I, not applied yet].
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gd24 {

struct HeapChunk {
    const uint8_t* data = nullptr;
    uint32_t size = 0, base = 0;  // base: its PSP RAM address
    bool contains(uint32_t a, uint32_t n = 1) const { return a >= base && uint64_t(a) + n <= uint64_t(base) + size; }
    const uint8_t* at(uint32_t a) const { return data + (a - base); }
};

struct PspTexture {
    std::string name;
    uint32_t address = 0;
    int width = 0, height = 0, bpp = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, unswizzled
};

struct PspVertex {
    float u = 0, v = 0;
    float x = 0, y = 0, z = 0;  // normalized for 8 / 16-bit formats
    uint32_t color = 0xFFFFFFFF;  // ABGR8888 (R in the low byte), from the GE's 565 / 5551 / 4444 / 8888
};

struct PspMesh {
    std::string name;  // the nearest name before the block [I]
    uint32_t address = 0, vtype = 0;
    uint32_t material = 0xFFFFFFFF;  // ABGR: the list's ambient colour / alpha (vertices without a colour)
    struct Prim {
        int type = 0, count = 0;  // GE: 3 triangles, 4 strip, 5 fan
    };
    std::vector<Prim> prims;
    std::vector<PspVertex> vertices;
};

// One texture object / mesh block at `address` (false: not one); `strict`
// also wants the header words of the common form (a search over raw memory).
bool decodeTexture(const HeapChunk& c, uint32_t address, PspTexture& tex);
bool decodeMesh(const HeapChunk& c, uint32_t address, PspMesh& m, bool strict = false);
// Every one in the chunk, found by their headers alone.
std::vector<PspTexture> findTextures(const HeapChunk& c);
std::vector<PspMesh> findMeshes(const HeapChunk& c);

}  // namespace gd24
