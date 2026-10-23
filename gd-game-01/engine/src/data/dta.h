// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "data/bytes.h"

// A node of Harmonix's DataArray script format, as stored in .dtb files.
struct DataNode {
    enum class Type {
        Int, Float, Var, Symbol, Unhandled, IfDef, Else, EndIf,
        Array, Command, String, Property, Define, Include, Merge, IfNDef,
    };

    Type type = Type::Int;
    int i = 0;
    float f = 0;
    std::string s;                  // symbol / string / variable / directive name
    std::vector<DataNode> children;  // Array, Command, Property

    bool isList() const { return type == Type::Array || type == Type::Command || type == Type::Property; }
    bool isText() const { return type == Type::Symbol || type == Type::String; }

    // The first child list whose first element is the symbol `key`, e.g.
    // find("points") on ((points (head 50)) ...). Returns nullptr if absent.
    const DataNode* find(std::string_view key) const;

    // Typed access to child `idx`; ints are accepted where floats are expected.
    int intAt(size_t idx) const;
    float floatAt(size_t idx) const;
    const std::string& textAt(size_t idx) const;
};

// Decrypts and parses a .dtb file. Throws std::runtime_error on malformed input.
DataNode parseDtb(const Bytes& encrypted);

// Parses an unencrypted serialised DataArray (e.g. a .seq file after its u32 0).
DataNode parseDataArray(const Bytes& plain);

// Parses DTA source text (the port's own script additions): {commands},
// (arrays), [properties], symbols, 'strings' / "strings", ints, floats,
// $variables and kDataUnhandled; ; starts a comment. The result is an Array
// of the top-level nodes. Throws std::runtime_error on unbalanced brackets.
DataNode parseDtaText(std::string_view text);

// Game path of the compiled form of a script: "config/x.dta" -> "config/gen/x.dtb".
std::string dtbPath(std::string_view dtaPath);
