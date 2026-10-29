// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "data/bytes.h"

// GH3's compiled QScript files (.qb; port of tools/qb.py, docs/formats.md
// "QB"). A file is a list of global sections, each a named value: numbers,
// keys, strings, pairs / vectors, structures, arrays and scripts (bytecode,
// run by the script VM, script/).
namespace qb {

enum class Type : uint8_t {
    None = 0,
    Int = 1,
    Float = 2,
    String = 3,
    WString = 4,
    Pair = 5,
    Vector = 6,
    Script = 7,
    Struct = 0xA,
    Array = 0xC,
    Key = 0xD,
    KeyPtr = 0x1A,  // a reference to a global by name (its value is looked up)
    StrPtr = 0x1B,
};
const char* typeName(Type t);

struct Struct;
struct Array;

struct ScriptCode {
    uint32_t checksum = 0;
    Bytes code;  // unpacked bytecode
};

struct Value {
    Type type = Type::None;
    int32_t i = 0;       // Int
    float f[3] = {0, 0, 0};  // Float (f[0]), Pair, Vector
    uint32_t key = 0;    // Key, KeyPtr, StrPtr
    std::string str;     // String; WString as UTF-8
    std::shared_ptr<Struct> st;
    std::shared_ptr<Array> arr;
    std::shared_ptr<ScriptCode> script;

    float number() const { return type == Type::Float ? f[0] : float(i); }
};

struct Item {
    uint32_t name = 0;  // 0: unnamed (a flag)
    Value value;
};

struct Struct {
    std::vector<Item> items;
    const Value* find(uint32_t name) const {
        for (const Item& it : items)
            if (it.name == name) return &it.value;
        return nullptr;
    }
};

struct Array {
    Type type = Type::None;
    std::vector<Value> values;
};

struct Section {
    uint32_t name = 0;
    Value value;
};

struct File {
    uint32_t fileKey = 0;
    std::vector<Section> sections;
    const Value* find(uint32_t name) const {
        for (const Section& s : sections)
            if (s.name == name) return &s.value;
        return nullptr;
    }
};

// Decodes one .qb file; throws std::runtime_error. `unusedBytes`, when
// given, receives how many bytes no section, item or value accounted for
// (the format tests require 0).
File decode(const uint8_t* data, size_t size, size_t* unusedBytes = nullptr);

// Neversoft's LZSS, as the scripts are stored (qb.py lzss).
Bytes lzss(const uint8_t* src, size_t size, size_t outSize);

}  // namespace qb
