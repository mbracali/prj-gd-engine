// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "script/globals.h"

#include "data/disc.h"
#include "data/pak.h"
#include "data/qbkey.h"

void Globals::add(const qb::File& file) {
    for (const qb::Section& s : file.sections) values_[s.name] = s.value;
    ++files_;
}

void Globals::load(const Disc& disc) {
    Pak pak = Pak::load(disc, "DATA/COMPRESSED/PAK/qb.pak.xen");
    constexpr uint32_t kQb = qbkey::key(".qb");
    for (const PakEntry& e : pak.entries()) {
        if (e.type != kQb) continue;
        auto span = pak.data(e);
        add(qb::decode(span.first, span.second));
    }
}

std::vector<qb::Item> Globals::expand(const qb::Struct& st) const {
    std::vector<qb::Item> out;
    out.reserve(st.items.size());
    for (const qb::Item& it : st.items) {
        // `$name`, or an evaluated key (`( <arg> default_topscores )`, setup_songtags),
        // naming a global struct: its fields [C data; I: plain keys too].
        if (it.name == 0 && (it.value.type == qb::Type::KeyPtr || it.value.type == qb::Type::Key)) {
            const qb::Value* inc = find(it.value.key);
            if (inc && inc->type == qb::Type::Struct && inc->st) {
                std::vector<qb::Item> sub = expand(*inc->st);
                out.insert(out.end(), sub.begin(), sub.end());
                continue;
            }
        }
        // An unnamed struct (`{ ( $ default_bandtags ) }`) is included the
        // same way [C data: setup_bandtags' band info tags; I: the rule].
        if (it.name == 0 && it.value.type == qb::Type::Struct && it.value.st) {
            std::vector<qb::Item> sub = expand(*it.value.st);
            out.insert(out.end(), sub.begin(), sub.end());
            continue;
        }
        out.push_back(it);
    }
    return out;
}

const qb::Value* Globals::field(const qb::Struct& st, uint32_t name) const {
    const qb::Value* found = nullptr;
    for (const qb::Item& it : st.items) {
        if (it.name == name) found = &it.value;
        else if (it.name == 0 && it.value.type == qb::Type::KeyPtr) {
            const qb::Value* inc = find(it.value.key);
            if (inc && inc->type == qb::Type::Struct && inc->st)
                if (const qb::Value* v = field(*inc->st, name)) found = v;
        }
    }
    return found;
}

const qb::Value* Globals::resolve(const qb::Value& v) const {
    const qb::Value* cur = &v;
    for (int depth = 0; depth < 8 && cur->type == qb::Type::KeyPtr; ++depth) {
        const qb::Value* next = find(cur->key);
        if (!next) return nullptr;
        cur = next;
    }
    return cur;
}

float qbNumber(const Globals& g, uint32_t name, float fallback) {
    const qb::Value* v = g.find(name);
    if (!v) return fallback;
    if (v->type == qb::Type::Float) return v->f[0];
    if (v->type == qb::Type::Int) return float(v->i);
    return fallback;
}

int qbInt(const Globals& g, uint32_t name, int fallback) {
    const qb::Value* v = g.find(name);
    if (!v) return fallback;
    if (v->type == qb::Type::Int) return v->i;
    if (v->type == qb::Type::Float) return int(v->f[0]);
    return fallback;
}
