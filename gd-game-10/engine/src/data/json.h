// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

// A minimal JSON value for the save file: null, bool, number, string, array, object.
struct Json {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Json> a;
    std::map<std::string, Json> o;

    static Json boolean(bool v) { Json j; j.type = Type::Bool; j.b = v; return j; }
    static Json number(double v) { Json j; j.type = Type::Number; j.n = v; return j; }
    static Json string(std::string v) { Json j; j.type = Type::String; j.s = std::move(v); return j; }
    static Json array() { Json j; j.type = Type::Array; return j; }
    static Json object() { Json j; j.type = Type::Object; return j; }

    // Object member, or a null value.
    const Json& operator[](const std::string& key) const;
    Json& set(const std::string& key, Json v) { type = Type::Object; return o[key] = std::move(v); }
    bool has(const std::string& key) const { return type == Type::Object && o.count(key); }
    double num(double def = 0) const { return type == Type::Number ? n : type == Type::Bool ? (b ? 1 : 0) : def; }
    bool truthy(bool def = false) const { return type == Type::Bool ? b : type == Type::Number ? n != 0 : def; }
    const std::string& str() const { return s; }

    std::string dump(int indent = 0) const;
    // Throws std::runtime_error on malformed text.
    static Json parse(const std::string& text);
};
