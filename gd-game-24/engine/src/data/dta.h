// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// Harmonix data arrays ("DTA") as they sit in a heap image (docs/FORMATS.md
// "The configuration in BEATMATCH.STR"). Every BEATMATCH image carries the
// game's whole configuration tree [C data, all 41 songs]: (system ...)
// (prototype ...) (songs (3sand7s (name ...) ... (preview 92200 122000)
// ...) ...) (beatmatcher ...) (game ... (track (y_per_second ...))) ...
//
// An array, 12+ bytes [C data]: {u32 pointer to its nodes, u32 (a file
// symbol [I]), u16 node count, u16 (1) ...}; a node, 8 bytes: {u32 value,
// u32 type}: 0 int, 1 float, 5 symbol (pointer to a C string), 0x10 / 0x11
// / 0x13 array (pointer to an array), 0x12 string [C: Harmonix's DataNode
// types, matching every node read].
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gd24 {

struct DtaNode {
    enum class Type { Int, Float, Symbol, String, Array, Other } type = Type::Other;
    int32_t i = 0;
    float f = 0;
    std::string s;               // symbol or string
    std::vector<DtaNode> array;  // Type::Array
    float number() const { return type == Type::Float ? f : float(i); }
    // (name ...) children: the first array whose first node is the symbol `name`.
    const DtaNode* find(const std::string& name) const;
};

class DtaImage {
public:
    // A heap image `size` bytes long loaded at `base`.
    DtaImage(const uint8_t* data, uint32_t size, uint32_t base) : d_(data), size_(size), base_(base) {}
    // The arrays whose first node is the symbol `head` (each read to `depth` levels).
    std::vector<DtaNode> arraysNamed(const std::string& head, int depth = 6) const;

private:
    bool ok(uint32_t a, uint32_t n = 4) const { return a >= base_ && uint64_t(a) + n <= uint64_t(base_) + size_; }
    uint32_t u32(uint32_t a) const;
    bool cstr(uint32_t a, std::string& out) const;
    bool readArray(uint32_t header, int depth, DtaNode& out) const;
    const uint8_t* d_;
    uint32_t size_, base_;
};

}  // namespace gd24
