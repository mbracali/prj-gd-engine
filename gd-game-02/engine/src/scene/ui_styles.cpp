// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "scene/ui_styles.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "data/game_data.h"
#include "scene/milo.h"
#include "scene/objects.h"

namespace scene {

namespace {

// "ui/" + "..\config\macros.dta" -> "config/macros.dta".
std::string joinPath(const std::string& dir, std::string rel) {
    std::replace(rel.begin(), rel.end(), '\\', '/');
    std::vector<std::string> parts;
    auto push = [&](const std::string& p) {
        size_t s = 0;
        while (s <= p.size()) {
            size_t e = p.find('/', s);
            if (e == std::string::npos) e = p.size();
            std::string part = p.substr(s, e - s);
            if (part == "..") {
                if (!parts.empty()) parts.pop_back();
            } else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            s = e + 1;
        }
    };
    if (rel.empty() || rel[0] != '/') push(dir);
    push(rel);
    std::string r;
    for (const auto& p : parts) r += (r.empty() ? "" : "/") + p;
    return r;
}

std::string dirOf(const std::string& path) {
    size_t s = path.rfind('/');
    return s == std::string::npos ? std::string() : path.substr(0, s);
}

// Source-style path of a dta ("ui/gen/ui_objects.dtb" -> "ui/ui_objects.dta").
std::string sourcePath(std::string p) {
    if (p.size() > 4 && p.compare(p.size() - 4, 4, ".dtb") == 0) {
        p = p.substr(0, p.size() - 4) + ".dta";
        size_t g = p.rfind("/gen/");
        if (g != std::string::npos) p.erase(g, 4);
    }
    return p;
}

using Defines = std::map<std::string, ::DataNode>;

::DataNode readDtb(const GameData& data, const std::string& sourceDta) {
    ::DataNode root = parseDtb(data.read(dtbPath(sourceDta)));
    resolveConditionals(root, {"HX_XBOX", "_SHIP"});
    return root;
}

void collectDefines(const GameData& data, const std::string& sourceDta, const ::DataNode& root, Defines& defs, int depth) {
    if (depth > 8) return;
    const auto& c = root.children;
    for (size_t i = 0; i < c.size(); ++i) {
        if (c[i].type == ::DataNode::Type::Include) {
            const std::string inc = joinPath(dirOf(sourceDta), c[i].s);
            if (data.exists(dtbPath(inc))) collectDefines(data, inc, readDtb(data, inc), defs, depth + 1);
        } else if (c[i].type == ::DataNode::Type::Define && i + 1 < c.size()) {
            defs[c[i].s] = c[i + 1];
            ++i;
        }
    }
}

::DataNode expand(const ::DataNode& n, const Defines& defs, int depth) {
    ::DataNode r = n;
    if (!n.isList()) return r;
    r.children.clear();
    for (const auto& ch : n.children) {
        if (ch.type == ::DataNode::Type::Symbol && depth < 16) {
            auto it = defs.find(ch.s);
            if (it != defs.end()) {
                const ::DataNode body = expand(it->second, defs, depth + 1);
                if (body.isList()) r.children.insert(r.children.end(), body.children.begin(), body.children.end());
                else r.children.push_back(body);
                continue;
            }
        }
        r.children.push_back(expand(ch, defs, depth + 1));
    }
    // {pack_color r g b}
    if (r.type == ::DataNode::Type::Command && r.children.size() == 4 && r.children[0].type == ::DataNode::Type::Symbol &&
        r.children[0].s == "pack_color") {
        ::DataNode v;
        v.type = ::DataNode::Type::Int;
        v.i = packColor(r.floatAt(1), r.floatAt(2), r.floatAt(3));
        return v;
    }
    return r;
}

// A milo object's property array (scene::DataNode: key symbol, value,
// key, value...; e.g. main's (normal_color 2304882 focus_color ...)):
// `key`'s value, or nullptr.
const DataNode* propValue(const DataNode& props, const std::string& key) {
    const auto& c = props.children;
    for (size_t i = 0; i + 1 < c.size(); i += 2)
        if (c[i].type == 5 && c[i].str == key) return &c[i + 1];
    return nullptr;
}

}  // namespace

int packColor(float r, float g, float b) {
    auto c = [](float v) { return int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)) & 0xff; };
    return c(r) | c(g) << 8 | c(b) << 16;
}

std::array<float, 3> unpackColor(int p) {
    constexpr float k = 0.003921569f;  // as 0x82122920
    return {float(p & 0xff) * k, float(p >> 8 & 0xff) * k, float(p >> 16 & 0xff) * k};
}

::DataNode loadPlainDta(const GameData& data, const std::string& path) {
    const std::string src = sourcePath(path);
    ::DataNode root = readDtb(data, src);
    Defines defs;
    collectDefines(data, src, root, defs, 0);
    ::DataNode out;
    out.type = ::DataNode::Type::Array;
    const auto& c = root.children;
    for (size_t i = 0; i < c.size(); ++i) {
        if (c[i].type == ::DataNode::Type::Define) {
            ++i;
            continue;
        }
        if (c[i].type == ::DataNode::Type::Include) continue;
        ::DataNode one;
        one.type = ::DataNode::Type::Array;
        one.children.push_back(c[i]);
        ::DataNode e = expand(one, defs, 0);
        out.children.insert(out.children.end(), e.children.begin(), e.children.end());
    }
    return out;
}

// ---- UiStyles ---------------------------------------------------------------

void UiStyles::load(const GameData& data) { root_ = loadPlainDta(data, "ui/ui_objects.dta"); }

const ::DataNode* UiStyles::classNode(const std::string& cls) const { return root_.find(cls); }

UiStyle UiStyles::style(const std::string& cls, const std::string& typeIn) const {
    UiStyle s;
    s.cls = cls;
    s.type = typeIn;
    const ::DataNode* c = classNode(cls);
    if (!c) return s;
    if (const ::DataNode* rf = c->find("resource_file"); rf && rf->children.size() > 1 && rf->children[1].isText())
        s.classResourceFile = rf->textAt(1);
    if (s.type.empty()) {
        s.type = "default";
        if (const ::DataNode* init = c->find("init"))
            if (const ::DataNode* t = init->find("type"); t && t->children.size() > 1 && t->children[1].isText()) s.type = t->textAt(1);
    }
    const ::DataNode* types = c->find("types");
    s.node = types ? types->find(s.type) : nullptr;
    if (!s.node) return s;
    auto text = [&](const char* key) -> std::string {
        const ::DataNode* n = s.node->find(key);
        return n && n->children.size() > 1 && n->children[1].isText() ? n->textAt(1) : std::string();
    };
    s.resourceFile = text("resource_file");
    s.mesh = text("mesh");
    s.texFile = text("tex_file");
    if (const ::DataNode* texts = s.node->find("texts"))
        for (size_t i = 1; i < texts->children.size(); ++i) {
            const ::DataNode& t = texts->children[i];
            if (t.isList() && t.children.size() > 1 && t.children[1].isText()) {
                s.textTemplate = t.textAt(1);
                break;
            }
        }
    if (s.textTemplate.empty()) s.textTemplate = text("text");  // BandTextEntry / BandSlider
    return s;
}

const ::DataNode* UiStyles::typeProperty(const std::string& cls, const std::string& type, const std::string& key) const {
    UiStyle s = style(cls, type);
    return s.node ? s.node->find(key) : nullptr;
}

const ::DataNode* UiStyles::dirTypeProperty(const Object& dir, const std::string& key) const {
    const std::string type = dir.header.type.empty() ? std::string("GH2") : dir.header.type;
    const ::DataNode* n = typeProperty("PanelDir", type, key);
    return n && n->children.size() > 1 ? &n->children[1] : nullptr;
}

std::array<float, 3> UiStyles::buttonColor(const Object& dir, UiState state) const {
    static const char* keys[] = {"normal_color", "focus_color", "disabled_color", "selecting_color"};
    const std::string key = keys[std::clamp(int(state), 0, 3)];
    if (dir.header.hasProps)
        if (const DataNode* v = propValue(dir.header.props, key); v && v->type == 0) return unpackColor(int(v->raw));
    if (const ::DataNode* v = dirTypeProperty(dir, key); v && v->type == ::DataNode::Type::Int) return unpackColor(v->i);
    return {1, 1, 1};
}

float UiStyles::focusScale(const Object& dir) const {
    if (dir.header.hasProps)
        if (const DataNode* v = propValue(dir.header.props, "focus_scale"); v && (v->type == 0 || v->type == 1)) return v->asFloat();
    if (const ::DataNode* v = dirTypeProperty(dir, "focus_scale")) {
        if (v->type == ::DataNode::Type::Float) return v->f;
        if (v->type == ::DataNode::Type::Int) return float(v->i);
    }
    return 1;
}

std::string UiStyles::resourcePath(const GameData& data, const std::string& rel) {
    return findMiloRelative(data, "ui/x.milo", rel);
}

// ---- Locale -----------------------------------------------------------------

const std::vector<std::string>& Locale::languages() {
    static const std::vector<std::string> v = {"eng", "fre", "deu", "ita", "esl"};
    return v;
}

void Locale::load(const GameData& data, const std::string& language) {
    const std::string main = "ui/" + language + "/locale.dta";
    if (!data.exists(dtbPath(main))) throw std::runtime_error("no locale for language '" + language + "'");
    strings_.clear();
    language_ = language;
    for (const std::string& f : {"ui/" + language + "/locale_milo.dta", main}) {
        if (!data.exists(dtbPath(f))) continue;
        ::DataNode root = loadPlainDta(data, f);
        for (const auto& e : root.children)
            if (e.isList() && e.children.size() >= 2 && e.children[0].isText() && e.children[1].isText())
                strings_[e.children[0].s] = e.children[1].s;
    }
}

const std::string* Locale::find(const std::string& token) const {
    auto it = strings_.find(token);
    return it == strings_.end() ? nullptr : &it->second;
}

std::string Locale::text(const std::string& token) const {
    const std::string* s = find(token);
    return s ? *s : token;
}

}  // namespace scene
