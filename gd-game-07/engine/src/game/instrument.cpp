// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/instrument.h"

#include <memory>

#include "data/qbkey.h"
#include "game/shop.h"
#include "script/globals.h"

namespace instrument {

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

// The store catalog entry for (part, descId), or null when the option is a free
// default (not priced and not locked — owned by everyone).
bool findCatalogItem(const Globals& g, uint32_t part, uint32_t descId, shop::Item& out) {
    for (const shop::Item& it : shop::catalog(g))
        if (it.part == part && it.descId == descId) { out = it; return true; }
    return false;
}

}  // namespace

const char* kindName(Kind k) {
    switch (k) {
        case Kind::Guitar: return "guitar";
        case Kind::Bass: return "bass";
        case Kind::Drums: return "drums";
        case Kind::Mic: return "mic";
    }
    return "?";
}

bool kindFromKey(uint32_t key, Kind& k) {
    if (key == K("guitar")) { k = Kind::Guitar; return true; }
    if (key == K("bass")) { k = Kind::Bass; return true; }
    if (key == K("drums")) { k = Kind::Drums; return true; }
    if (key == K("mic")) { k = Kind::Mic; return true; }
    return false;
}

std::vector<uint32_t> slots(Kind k) {
    switch (k) {
        case Kind::Guitar:
            return {K("CAS_Guitar_Body"), K("CAS_Guitar_Neck"), K("CAS_Guitar_Head"),
                    K("CAS_Guitar_Pickups"), K("CAS_Guitar_Pickguards"), K("CAS_Guitar_Bridges"),
                    K("CAS_Guitar_Knobs"), K("CAS_Guitar_Strings"), K("CAS_Guitar_Highway")};
        case Kind::Bass:
            return {K("CAS_Bass_Body"), K("CAS_Bass_Neck"), K("CAS_Bass_Head"),
                    K("CAS_Bass_Pickups"), K("CAS_Bass_Pickguards"), K("CAS_Bass_Bridges"),
                    K("CAS_Bass_Knobs"), K("CAS_Bass_Strings"), K("CAS_Bass_Highway")};
        case Kind::Drums:
            return {K("CAS_Drums"), K("CAS_Drums_Sticks"), K("CAS_Drums_Highway")};
        case Kind::Mic:
            return {K("CAS_Mic"), K("CAS_Mic_Stand")};
    }
    return {};
}

std::vector<uint32_t> options(const Globals& g, uint32_t part) {
    std::vector<uint32_t> out;
    const qb::Value* av = g.find(part);
    if (!av || av->type != qb::Type::Array || !av->arr) return out;
    for (const qb::Value& e : av->arr->values) {
        if (e.type != qb::Type::Struct || !e.st) continue;
        const uint32_t descId = keyOf(e.st->find(K("desc_id")));
        if (!descId || descId == K("none")) continue;
        out.push_back(descId);
    }
    return out;
}

uint32_t Instrument::descIdFor(uint32_t part) const {
    for (const auto& p : parts)
        if (p.first == part) return p.second;
    return 0;
}

void Instrument::set(uint32_t part, uint32_t descId) {
    for (auto& p : parts)
        if (p.first == part) { p.second = descId; return; }
    parts.emplace_back(part, descId);
}

const char* setPartName(SetPart s) {
    switch (s) {
        case SetPart::Ok: return "Ok";
        case SetPart::NotASlot: return "NotASlot";
        case SetPart::NotAnOption: return "NotAnOption";
        case SetPart::NotOwned: return "NotOwned";
    }
    return "?";
}

SetPart choose(Instrument& inst, const Globals& g, const qb::Struct& bandInfo, uint32_t part, uint32_t descId) {
    bool isSlot = false;
    for (uint32_t s : slots(inst.kind))
        if (s == part) { isSlot = true; break; }
    if (!isSlot) return SetPart::NotASlot;
    bool isOption = false;
    for (uint32_t o : options(g, part))
        if (o == descId) { isOption = true; break; }
    if (!isOption) return SetPart::NotAnOption;
    // Ownership: a priced/locked option must be owned; a free default is owned.
    shop::Item it;
    if (findCatalogItem(g, part, descId, it) && !shop::owned(bandInfo, it)) return SetPart::NotOwned;
    inst.set(part, descId);
    return SetPart::Ok;
}

Instrument makeDefault(const Globals& g, Kind kind) {
    Instrument inst;
    inst.kind = kind;
    for (uint32_t part : slots(kind)) {
        std::vector<uint32_t> opt = options(g, part);
        if (!opt.empty()) inst.parts.emplace_back(part, opt.front());
    }
    return inst;
}

qb::Struct instrumentAppearance(const Instrument& inst) {
    qb::Struct app;
    for (const auto& p : inst.parts) {
        qb::Item slot;
        slot.name = p.first;  // the CAS part key
        slot.value.type = qb::Type::Struct;
        slot.value.st = std::make_shared<qb::Struct>();
        qb::Item di;
        di.name = K("desc_id");
        di.value.type = qb::Type::Key;
        di.value.key = p.second;
        slot.value.st->items.push_back(di);
        app.items.push_back(slot);
    }
    return app;
}

qb::Struct toBandInfo(const Instrument& inst) {
    qb::Struct bi;
    {
        qb::Item t;
        t.name = K("instrument_type");
        t.value.type = qb::Type::Key;
        t.value.key = K(kindName(inst.kind));
        bi.items.push_back(t);
    }
    {
        qb::Item n;
        n.name = K("instrument_name");
        n.value.type = qb::Type::WString;
        n.value.str = inst.name;
        bi.items.push_back(n);
    }
    {
        qb::Item a;
        a.name = K("cas_current_appearance");
        a.value.type = qb::Type::Struct;
        a.value.st = std::make_shared<qb::Struct>(instrumentAppearance(inst));
        bi.items.push_back(a);
    }
    return bi;
}

Instrument fromBandInfo(const qb::Struct& bandInfo) {
    Instrument inst;
    if (const qb::Value* t = bandInfo.find(K("instrument_type"))) kindFromKey(keyOf(t), inst.kind);
    if (const qb::Value* n = bandInfo.find(K("instrument_name")))
        if (n->type == qb::Type::WString || n->type == qb::Type::String) inst.name = n->str;
    const qb::Value* app = bandInfo.find(K("cas_current_appearance"));
    if (!app || app->type != qb::Type::Struct || !app->st) return inst;
    // Restore in the kind's slot order (only slots the appearance carries).
    for (uint32_t part : slots(inst.kind)) {
        const qb::Value* slot = app->st->find(part);
        if (!slot || slot->type != qb::Type::Struct || !slot->st) continue;
        const uint32_t descId = keyOf(slot->st->find(K("desc_id")));
        if (descId) inst.parts.emplace_back(part, descId);
    }
    return inst;
}

}  // namespace instrument
