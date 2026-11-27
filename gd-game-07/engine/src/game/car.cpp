// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/car.h"

#include <memory>

#include "data/qbkey.h"
#include "game/shop.h"
#include "script/globals.h"

namespace car {

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

// A key value's key (Key / KeyPtr), or 0.
uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

// A struct carries flag `key`: a named item, or a bare unnamed Key item (the
// disc's `$locked` form) — same test as game/shop.cpp.
bool hasFlag(const qb::Struct& s, uint32_t key) {
    for (const qb::Item& it : s.items) {
        if (it.name == key) return true;
        if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && it.value.key == key) return true;
    }
    return false;
}

// A deep, independent copy of a value (the default profile is a shared global;
// a created character must not alias it).
qb::Value clone(const qb::Value& v);
qb::Struct clone(const qb::Struct& s) {
    qb::Struct out;
    out.items.reserve(s.items.size());
    for (const qb::Item& it : s.items) {
        qb::Item ci;
        ci.name = it.name;
        ci.value = clone(it.value);
        out.items.push_back(std::move(ci));
    }
    return out;
}
qb::Value clone(const qb::Value& v) {
    qb::Value out = v;  // scalars, key, str, f[]
    if (v.type == qb::Type::Struct) {
        out.st = v.st ? std::make_shared<qb::Struct>(clone(*v.st)) : nullptr;
    } else if (v.type == qb::Type::Array) {
        if (v.arr) {
            out.arr = std::make_shared<qb::Array>();
            out.arr->type = v.arr->type;
            out.arr->values.reserve(v.arr->values.size());
            for (const qb::Value& x : v.arr->values) out.arr->values.push_back(clone(x));
        } else {
            out.arr = nullptr;
        }
    }
    // Scripts are never part of a profile; share the pointer if present.
    return out;
}

// A named item in a struct, created (as the given type) if absent.
qb::Value& ensureItem(qb::Struct& s, uint32_t name, qb::Type type) {
    for (qb::Item& it : s.items)
        if (it.name == name) return it.value;
    qb::Item it;
    it.name = name;
    it.value.type = type;
    s.items.push_back(it);
    return s.items.back().value;
}

// The savegame's custom_profiles array (created characters), or null.
const qb::Array* customArray(const qb::Struct& savegame) {
    const qb::Value* v = savegame.find(K(kCustomProfiles));
    return v && v->type == qb::Type::Array && v->arr ? v->arr.get() : nullptr;
}
// The same, created if absent.
qb::Array& ensureCustomArray(qb::Struct& savegame) {
    qb::Value& v = ensureItem(savegame, K(kCustomProfiles), qb::Type::Array);
    if (v.type != qb::Type::Array || !v.arr) {
        v.type = qb::Type::Array;
        v.arr = std::make_shared<qb::Array>();
    }
    v.arr->type = qb::Type::Struct;
    return *v.arr;
}

// A created profile's mutable struct by name key, or null.
qb::Struct* findMutable(qb::Struct& savegame, uint32_t id) {
    qb::Value* v = nullptr;
    for (qb::Item& it : savegame.items)
        if (it.name == K(kCustomProfiles)) v = &it.value;
    if (!v || v->type != qb::Type::Array || !v->arr) return nullptr;
    for (qb::Value& e : v->arr->values)
        if (e.type == qb::Type::Struct && e.st && keyOf(e.st->find(K("name"))) == id) return e.st.get();
    return nullptr;
}

// The CAS option struct for (part, descId) in the globals, or null.
const qb::Struct* optionStruct(const Globals& g, uint32_t part, uint32_t descId) {
    const qb::Value* a = g.find(part);
    if (!a || a->type != qb::Type::Array || !a->arr) return nullptr;
    for (const qb::Value& e : a->arr->values)
        if (e.type == qb::Type::Struct && e.st && keyOf(e.st->find(K("desc_id"))) == descId) return e.st.get();
    return nullptr;
}

}  // namespace

std::vector<uint32_t> editableSlots(const Globals& g, bool female) {
    std::vector<uint32_t> out;
    const qb::Value* mel = g.find(K("master_editable_list"));
    if (!mel || mel->type != qb::Type::Array || !mel->arr) return out;
    for (const qb::Value& pe : mel->arr->values) {
        if (pe.type != qb::Type::Struct || !pe.st) continue;
        const uint32_t part = keyOf(pe.st->find(K("part")));
        if (!part) continue;
        const std::string name = qbkey::names().get(part);
        // Gendered body/anim slots: CAS_Female_* only for a female, CAS_Male_*
        // only for a male; every other slot is common to both genders.
        if (name.rfind("CAS_Female_", 0) == 0) {
            if (!female) continue;
        } else if (name.rfind("CAS_Male_", 0) == 0) {
            if (female) continue;
        }
        out.push_back(part);
    }
    return out;
}

bool isEditableSlot(const Globals& g, bool female, uint32_t part) {
    for (uint32_t p : editableSlots(g, female))
        if (p == part) return true;
    return false;
}

uint32_t newCharacter(const Globals& g, qb::Struct& savegame, bool female) {
    const qb::Value* def = g.find(K(female ? "default_custom_musician_profile_female" : "default_custom_musician_profile_male"));
    if (!def || def->type != qb::Type::Struct || !def->st) return 0;

    // new_custom_character_name: the first free custom_character_<N>.
    uint32_t id = 0;
    for (int n = 0; n < kMaxCustomCharacters; ++n) {
        const uint32_t cand = K(("custom_character_" + std::to_string(n)).c_str());
        if (customIndex(savegame, cand) < 0) {
            id = cand;
            break;
        }
    }
    if (!id) return 0;  // 100 characters already

    // add_new_custom_profile: a deep copy of the default profile with its name
    // set, appended to custom_profiles.
    qb::Struct prof = clone(*def->st);
    qb::Value& nm = ensureItem(prof, K("name"), qb::Type::Key);
    nm.type = qb::Type::Key;
    nm.key = id;

    qb::Array& arr = ensureCustomArray(savegame);
    qb::Value e;
    e.type = qb::Type::Struct;
    e.st = std::make_shared<qb::Struct>(std::move(prof));
    arr.values.push_back(std::move(e));
    return id;
}

const qb::Struct* profile(const qb::Struct& savegame, uint32_t id) {
    const qb::Array* a = customArray(savegame);
    if (!a) return nullptr;
    for (const qb::Value& e : a->values)
        if (e.type == qb::Type::Struct && e.st && keyOf(e.st->find(K("name"))) == id) return e.st.get();
    return nullptr;
}

int customIndex(const qb::Struct& savegame, uint32_t id) {
    const qb::Array* a = customArray(savegame);
    if (!a) return -1;
    for (size_t i = 0; i < a->values.size(); ++i) {
        const qb::Value& e = a->values[i];
        if (e.type == qb::Type::Struct && e.st && keyOf(e.st->find(K("name"))) == id) return int(i);
    }
    return -1;
}

int count(const qb::Struct& savegame) {
    const qb::Array* a = customArray(savegame);
    return a ? int(a->values.size()) : 0;
}

bool isFemale(const qb::Struct& savegame, uint32_t id) {
    const qb::Struct* p = profile(savegame, id);
    if (!p) return false;
    const qb::Value* app = p->find(K("appearance"));
    if (!app || app->type != qb::Type::Struct || !app->st) return false;
    const qb::Value* ph = app->st->find(K("cas_physique"));
    if (!ph || ph->type != qb::Type::Struct || !ph->st) return false;
    return keyOf(ph->st->find(K("desc_id"))) == K("FemalePhysique");
}

const char* setName(Set s) {
    switch (s) {
        case Set::Ok: return "Ok";
        case Set::NoSuchCharacter: return "NoSuchCharacter";
        case Set::NotEditable: return "NotEditable";
        case Set::NoSuchOption: return "NoSuchOption";
        case Set::NotOwned: return "NotOwned";
    }
    return "?";
}

Set setPart(const Globals& g, qb::Struct& savegame, uint32_t id, uint32_t part, uint32_t descId, const qb::Struct* bandInfo) {
    qb::Struct* p = findMutable(savegame, id);
    if (!p) return Set::NoSuchCharacter;
    if (!isEditableSlot(g, isFemale(savegame, id), part)) return Set::NotEditable;

    // $none clears the slot; any other option must exist in the part array.
    const bool clearing = descId == 0 || descId == K("none");
    if (!clearing) {
        const qb::Struct* opt = optionStruct(g, part, descId);
        if (!opt) return Set::NoSuchOption;
        // Ownership (is_part_unlocked_purchased): a priced/locked option needs
        // the save's purchased/unlocked flags; a free, unlocked default is
        // owned by construction. Skipped when no save context is given.
        if (bandInfo) {
            shop::Item si;
            si.part = part;
            si.descId = descId;
            si.priced = opt->find(K("price")) && opt->find(K("price"))->type == qb::Type::Int;
            si.price = si.priced ? opt->find(K("price"))->i : 0;
            si.locked = hasFlag(*opt, K("locked"));
            if ((si.priced || si.locked) && !shop::owned(*bandInfo, si)) return Set::NotOwned;
        }
    }

    // modify_custom_profile_appearance: appearance.<part>.desc_id = descId.
    qb::Value& app = ensureItem(*p, K("appearance"), qb::Type::Struct);
    if (app.type != qb::Type::Struct || !app.st) {
        app.type = qb::Type::Struct;
        app.st = std::make_shared<qb::Struct>();
    }
    qb::Value& slot = ensureItem(*app.st, part, qb::Type::Struct);
    if (slot.type != qb::Type::Struct || !slot.st) {
        slot.type = qb::Type::Struct;
        slot.st = std::make_shared<qb::Struct>();
    }
    qb::Value& di = ensureItem(*slot.st, K("desc_id"), qb::Type::Key);
    di.type = qb::Type::Key;
    di.key = clearing ? K("none") : descId;
    return Set::Ok;
}

uint32_t partOf(const qb::Struct& savegame, uint32_t id, uint32_t part) {
    const qb::Struct* p = profile(savegame, id);
    if (!p) return 0;
    const qb::Value* app = p->find(K("appearance"));
    if (!app || app->type != qb::Type::Struct || !app->st) return 0;
    const qb::Value* slot = app->st->find(part);
    if (!slot || slot->type != qb::Type::Struct || !slot->st) return 0;
    return keyOf(slot->st->find(K("desc_id")));
}

bool setFullName(qb::Struct& savegame, uint32_t id, const std::string& name) {
    qb::Struct* p = findMutable(savegame, id);
    if (!p) return false;
    qb::Value& fn = ensureItem(*p, K("fullname"), qb::Type::WString);
    fn.type = qb::Type::WString;
    fn.str = name;
    return true;
}

std::string fullName(const qb::Struct& savegame, uint32_t id) {
    const qb::Struct* p = profile(savegame, id);
    if (!p) return {};
    const qb::Value* fn = p->find(K("fullname"));
    if (!fn) return {};
    if (fn->type == qb::Type::String || fn->type == qb::Type::WString) return fn->str;
    return {};
}

bool deleteCharacter(qb::Struct& savegame, uint32_t id) {
    for (qb::Item& it : savegame.items) {
        if (it.name != K(kCustomProfiles) || it.value.type != qb::Type::Array || !it.value.arr) continue;
        auto& vals = it.value.arr->values;
        for (size_t i = 0; i < vals.size(); ++i)
            if (vals[i].type == qb::Type::Struct && vals[i].st && keyOf(vals[i].st->find(K("name"))) == id) {
                vals.erase(vals.begin() + i);
                return true;
            }
    }
    return false;
}

}  // namespace car
