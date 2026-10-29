// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "data/venue.h"

#include <cmath>
#include <stdexcept>

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"
#include "script/globals.h"

namespace venue {
namespace {

constexpr uint32_t K(const char* s) { return qbkey::key(s); }

Quat axisAngle(int axis, float a) {
    Quat q{0, 0, 0, std::cos(a * 0.5f)};
    q[size_t(axis)] = std::sin(a * 0.5f);
    return q;
}

Vec3 vec(const qb::Value* v, Vec3 fallback = {}) {
    if (!v || (v->type != qb::Type::Vector && v->type != qb::Type::Pair)) return fallback;
    return {v->f[0], v->f[1], v->type == qb::Type::Vector ? v->f[2] : 0.0f};
}

float num(const qb::Value* v, float fallback) {
    if (!v || (v->type != qb::Type::Float && v->type != qb::Type::Int)) return fallback;
    return v->number();
}

uint32_t keyOf(const qb::Value* v) { return v && v->type == qb::Type::Key ? v->key : 0; }

bool hasFlag(const qb::Struct& st, uint32_t flag) {
    for (const qb::Item& it : st.items)
        if (it.name == 0 && it.value.type == qb::Type::Key && it.value.key == flag) return true;
    return false;
}

void readNodes(Venue& v, const qb::Array& arr) {
    for (const qb::Value& e : arr.values) {
        if (e.type != qb::Type::Struct || !e.st) continue;
        const qb::Struct& st = *e.st;
        Node n;
        n.st = &st;
        n.name = keyOf(st.find(K("name")));
        n.cls = keyOf(st.find(K("class")));
        n.type = keyOf(st.find(K("type")));
        n.pos = vec(st.find(K("pos")));
        n.angles = vec(st.find(K("angles")));
        n.rot = quatFromAngles(n.angles);
        n.lightGroup = keyOf(st.find(K("lightgroup")));
        n.createdAtStart = hasFlag(st, K("createdatstart"));
        n.createdOn = keyOf(st.find(K("createdonprogress")));
        if (!n.createdOn) n.createdOn = keyOf(st.find(K("createdfromvariable")));
        if (!n.name) continue;
        v.nodeIndex.emplace(n.name, v.nodes.size());
        v.nodes.push_back(n);

        if (n.cls != K("levellight")) continue;
        Light l;
        l.name = n.name;
        l.group = n.lightGroup;
        const uint32_t t = n.type;
        l.type = t == K("ambient") ? LightType::Ambient : t == K("dir") ? LightType::Directional : t == K("spot") ? LightType::Spot
                                                                                                                 : LightType::Point;
        l.pos = n.pos;
        Quat q = n.rot;
        if (const qb::Value* rq = st.find(K("rotationquat")); rq && rq->type == qb::Type::Array && rq->arr && rq->arr->values.size() == 4)
            for (size_t i = 0; i < 4; ++i) q[i] = rq->arr->values[i].number();
        l.dir = rotate(q, {0, 0, 1});
        l.color = vec(st.find(K("lightcolor")), {1, 1, 1});
        l.intensity = num(st.find(K("intensity")), 0);
        l.attenStart = num(st.find(K("attenstart")), 0);
        l.attenEnd = num(st.find(K("attenend")), 1);
        l.hotspot = num(st.find(K("hotspot")), 0);
        l.field = num(st.find(K("field")), 0);
        const std::string& nm = qbkey::names().get(n.name);
        std::string lower;
        for (char c : nm) lower.push_back(char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
        l.vertexBake = lower.find("_vc_") != std::string::npos;
        l.createdAtStart = n.createdAtStart;
        l.createdOn = n.createdOn;
        v.lights.push_back(l);
    }
}

}  // namespace

Quat mul(const Quat& a, const Quat& b) {
    return {a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1], a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0],
            a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3], a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2]};
}

Quat quatFromAngles(const Vec3& a) { return mul(mul(axisAngle(1, a[1]), axisAngle(2, a[2])), axisAngle(0, a[0])); }

Vec3 rotate(const Quat& q, const Vec3& v) {
    // v + 2w (u x v) + 2 u x (u x v)
    const Vec3 u{q[0], q[1], q[2]};
    const Vec3 t{2 * (u[1] * v[2] - u[2] * v[1]), 2 * (u[2] * v[0] - u[0] * v[2]), 2 * (u[0] * v[1] - u[1] * v[0])};
    return {v[0] + q[3] * t[0] + (u[1] * t[2] - u[2] * t[1]), v[1] + q[3] * t[1] + (u[2] * t[0] - u[0] * t[2]),
            v[2] + q[3] * t[2] + (u[0] * t[1] - u[1] * t[0])};
}

const qb::Value* Venue::find(uint32_t name) const {
    for (const qb::File& f : files)
        if (const qb::Value* v = f.find(name)) return v;
    return nullptr;
}

uint32_t Venue::key(const std::string& suffix) const { return qbkey::key(zone + suffix); }

std::string zoneOfLevel(const Globals& g, uint32_t levelKey) {
    const qb::Value* zones = g.find(K("LevelZones"));
    if (!zones || zones->type != qb::Type::Struct || !zones->st) return "";
    const qb::Value* level = g.field(*zones->st, levelKey);
    if (!level || level->type != qb::Type::Struct || !level->st) return "";
    const qb::Value* name = g.field(*level->st, K("name"));
    return name && name->type == qb::Type::String ? name->str : "";
}

std::vector<std::string> venueZones(const Globals& g) {
    std::vector<std::string> out;
    const qb::Value* zones = g.find(K("LevelZones"));
    if (!zones || zones->type != qb::Type::Struct || !zones->st) return out;
    for (const qb::Item& it : g.expand(*zones->st)) {
        if (it.value.type != qb::Type::Struct || !it.value.st || hasFlag(*it.value.st, K("debug_only"))) continue;
        const qb::Value* name = g.field(*it.value.st, K("name"));
        if (name && name->type == qb::Type::String) out.push_back(name->str);
    }
    return out;
}

std::unique_ptr<Venue> load(const Disc& disc, const std::string& zone) {
    auto v = std::make_unique<Venue>();
    v->zone = zone;
    v->zoneKey = qbkey::key(zone);
    const std::string path = "DATA/COMPRESSED/ZONES/" + zone + ".pak.xen";
    if (!disc.exists(path)) throw std::runtime_error("no " + path);
    const Pak pak = Pak::load(disc, path);
    for (const PakEntry& e : pak.entries()) {
        auto [p, size] = pak.data(e);
        if (e.type == K(".qb") || e.type == K(".nqb")) v->files.push_back(qb::decode(p, size));
    }
    // The node arrays (<zone>.qb, <zone>_gfx.qb: .nqb entries) [C data].
    for (const qb::File& f : v->files)
        for (const qb::Section& s : f.sections) {
            if (s.value.type != qb::Type::Array || !s.value.arr || s.value.arr->type != qb::Type::Struct) continue;
            const auto& vals = s.value.arr->values;
            if (!vals.empty() && vals[0].st && vals[0].st->find(K("class")) && vals[0].st->find(K("pos"))) readNodes(*v, *s.value.arr);
        }
    for (const PakEntry& e : pak.entries()) {
        if (e.type != K(".scn")) continue;
        auto [p, size] = pak.data(e);
        Part part;
        part.path = qbkey::names().get(e.fullName);
        part.scene = scene::parse(p, size, scene::Kind::Scn);
        for (const scene::Sector& s : part.scene.sectors) {
            auto it = v->nodeIndex.find(s.key);
            part.sectorNode.push_back(it == v->nodeIndex.end() ? -1 : int(it->second));
            part.sectorLocal.push_back((s.flags & 0x20) != 0);
        }
        v->parts.push_back(std::move(part));
    }
    if (v->parts.empty()) throw std::runtime_error(path + ": no scene");
    return v;
}

}  // namespace venue
