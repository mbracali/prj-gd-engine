// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/save.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "data/json.h"
#include "data/qbkey.h"
#include "platform/paths.h"

namespace save {

namespace {

std::filesystem::path fsPath(const std::string& s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }

std::string hex(uint32_t k) {
    char b[12];
    std::snprintf(b, sizeof b, "%08x", k);
    return b;
}

Json toJson(const qb::Value& v) {
    Json j = Json::object();
    j.set("t", Json::string(qb::typeName(v.type)));
    switch (v.type) {
    case qb::Type::Int: j.set("v", Json::number(v.i)); break;
    case qb::Type::Float: j.set("v", Json::number(v.f[0])); break;
    case qb::Type::Key:
    case qb::Type::KeyPtr:
    case qb::Type::StrPtr:
        j.set("v", Json::string(hex(v.key)));
        if (const std::string* n = qbkey::names().find(v.key)) j.set("name", Json::string(*n));
        break;
    case qb::Type::String:
    case qb::Type::WString: j.set("v", Json::string(v.str)); break;
    case qb::Type::Pair:
    case qb::Type::Vector: {
        Json a = Json::array();
        for (int k = 0; k < (v.type == qb::Type::Pair ? 2 : 3); ++k) a.a.push_back(Json::number(v.f[k]));
        j.set("v", a);
        break;
    }
    case qb::Type::Struct: {
        Json a = Json::array();
        if (v.st)
            for (const qb::Item& it : v.st->items) {
                Json e = toJson(it.value);
                e.set("k", Json::string(hex(it.name)));
                a.a.push_back(e);
            }
        j.set("v", a);
        break;
    }
    case qb::Type::Array: {
        Json a = Json::array();
        if (v.arr)
            for (const qb::Value& x : v.arr->values) a.a.push_back(toJson(x));
        j.set("v", a);
        break;
    }
    default: break;
    }
    return j;
}

uint32_t parseHex(const std::string& s) { return uint32_t(std::stoul(s, nullptr, 16)); }

qb::Value fromJson(const Json& j) {
    qb::Value v;
    const std::string t = j["t"].str();
    const Json& x = j["v"];
    if (t == "int") v.type = qb::Type::Int, v.i = int32_t(x.num());
    else if (t == "float") v.type = qb::Type::Float, v.f[0] = float(x.num());
    else if (t == "key" || t == "keyptr" || t == "strptr") {
        v.type = t == "key" ? qb::Type::Key : t == "keyptr" ? qb::Type::KeyPtr : qb::Type::StrPtr;
        v.key = parseHex(x.str());
    } else if (t == "string" || t == "wstring") {
        v.type = t == "string" ? qb::Type::String : qb::Type::WString;
        v.str = x.str();
    } else if (t == "pair" || t == "vector") {
        v.type = t == "pair" ? qb::Type::Pair : qb::Type::Vector;
        for (size_t k = 0; k < x.a.size() && k < 3; ++k) v.f[k] = float(x.a[k].num());
    } else if (t == "struct") {
        v.type = qb::Type::Struct;
        v.st = std::make_shared<qb::Struct>();
        for (const Json& e : x.a) v.st->items.push_back(qb::Item{parseHex(e["k"].str()), fromJson(e)});
    } else if (t == "array") {
        v.type = qb::Type::Array;
        v.arr = std::make_shared<qb::Array>();
        for (const Json& e : x.a) v.arr->values.push_back(fromJson(e));
        if (!v.arr->values.empty()) v.arr->type = v.arr->values.front().type;
    } else if (t == "none") {
        v.type = qb::Type::None;  // a script left a field unset; kept as it was
    } else {
        throw std::runtime_error("unknown value type " + t);
    }
    return v;
}

}  // namespace

namespace {
std::string& folderOverride() {
    static std::string f;
    return f;
}
}  // namespace

void setFolder(const std::string& dir) { folderOverride() = dir; }

std::string path() {
    const std::string dir = folderOverride().empty() ? prefDir() : folderOverride();
    return dir.empty() ? "" : dir + "save.json";
}

bool exists() {
    const std::string p = path();
    std::error_code ec;
    return !p.empty() && std::filesystem::exists(fsPath(p), ec);
}

namespace {
Json tagsJson(const Tags& tags) {
    Json t = Json::object();
    for (const auto& [k, st] : tags) {
        qb::Value v;
        v.type = qb::Type::Struct;
        v.st = std::make_shared<qb::Struct>(st);
        Json e = toJson(v);
        if (const std::string* n = qbkey::names().find(k)) e.set("name", Json::string(*n));
        t.set(hex(k), e);
    }
    return t;
}
bool tagsFrom(const Json& j, Tags& out, std::string& error) {
    for (const auto& [k, e] : j.o) {
        const qb::Value v = fromJson(e);
        if (v.type != qb::Type::Struct || !v.st) return error = "a tag is not a struct", false;
        out[parseHex(k)] = *v.st;
    }
    return true;
}
}  // namespace

bool write(const Tags& tags, const Sections& sections, std::string& error) {
    const std::string p = path();
    if (p.empty()) return error = "no save folder", false;
    Json root = Json::object();
    root.set("format", Json::string(kFormat));
    root.set("version", Json::number(kVersion));
    root.set("tags", tagsJson(tags));
    Json secs = Json::object();
    for (const auto& [k, t] : sections) {
        Json e = Json::object();
        if (const std::string* n = qbkey::names().find(k)) e.set("name", Json::string(*n));
        e.set("tags", tagsJson(t));
        secs.set(hex(k), e);
    }
    root.set("sections", secs);
    const std::string tmp = p + ".tmp";
    {
        std::ofstream f(fsPath(tmp), std::ios::binary);
        f << root.dump(1);
        if (!f) return error = "cannot write " + tmp, false;
    }
    std::error_code ec;
    if (std::filesystem::exists(fsPath(p), ec))
        std::filesystem::copy_file(fsPath(p), fsPath(p + ".bak"),
                                   std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::rename(fsPath(tmp), fsPath(p), ec);
    if (ec) return error = ec.message(), false;
    return true;
}

bool read(Tags& tags, Sections& sections, std::string& error) {
    const std::string p = path();
    std::ifstream f(fsPath(p), std::ios::binary);
    if (!f) return error = "no save", false;
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        const Json root = Json::parse(ss.str());
        if (root["format"].str() != kFormat) return error = "not a gd04 save", false;
        if (root["version"].num() > kVersion) return error = "a newer save version", false;
        Tags out;
        if (!tagsFrom(root["tags"], out, error)) return false;
        Sections secs;
        for (const auto& [k, e] : root["sections"].o)
            if (!tagsFrom(e["tags"], secs[parseHex(k)], error)) return false;
        tags = std::move(out);
        sections = std::move(secs);
        return true;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

}  // namespace save
