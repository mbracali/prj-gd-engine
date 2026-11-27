// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "game/shop.h"

#include <memory>

#include "data/qbkey.h"
#include "script/globals.h"

namespace shop {

namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

// A key value's key, or 0 (Key / KeyPtr).
uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

// The struct carries flag `key`: a named item `key`, or a bare flag item (an
// unnamed Key item whose key is `key`, the way the disc stores `$locked` and
// the way set_current_band_part_flags' `{ unlocked }` splices in).
bool hasFlag(const qb::Struct& s, uint32_t key) {
    for (const qb::Item& it : s.items) {
        if (it.name == key) return true;
        if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && it.value.key == key) return true;
    }
    return false;
}

void addFlag(qb::Struct& s, uint32_t key) {
    if (hasFlag(s, key)) return;
    qb::Item it;
    it.name = 0;
    it.value.type = qb::Type::Key;
    it.value.key = key;
    s.items.push_back(it);
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

// The save's per-item flag struct for (part, desc_id): band_info.unlock_purchased
// . <part> is an array of { desc_id, unlocked?, purchased? }. Null when absent.
const qb::Struct* entryFlags(const qb::Struct& bandInfo, uint32_t part, uint32_t descId) {
    const qb::Value* up = bandInfo.find(K("unlock_purchased"));
    if (!up || up->type != qb::Type::Struct || !up->st) return nullptr;
    const qb::Value* pa = up->st->find(part);
    if (!pa || pa->type != qb::Type::Array || !pa->arr) return nullptr;
    for (const qb::Value& e : pa->arr->values)
        if (e.type == qb::Type::Struct && e.st && keyOf(e.st->find(K("desc_id"))) == descId) return e.st.get();
    return nullptr;
}

// The same entry, created if absent (the save grows as the original's
// set_current_band_part_flags / initilize_current_band_unlock_purchase grows it).
qb::Struct& ensureEntry(qb::Struct& bandInfo, uint32_t part, uint32_t descId) {
    qb::Value& up = ensureItem(bandInfo, K("unlock_purchased"), qb::Type::Struct);
    if (up.type != qb::Type::Struct || !up.st) up.type = qb::Type::Struct, up.st = std::make_shared<qb::Struct>();
    qb::Value& pa = ensureItem(*up.st, part, qb::Type::Array);
    if (pa.type != qb::Type::Array || !pa.arr) pa.type = qb::Type::Array, pa.arr = std::make_shared<qb::Array>();
    pa.arr->type = qb::Type::Struct;
    for (qb::Value& e : pa.arr->values)
        if (e.type == qb::Type::Struct && e.st && keyOf(e.st->find(K("desc_id"))) == descId) return *e.st;
    qb::Value e;
    e.type = qb::Type::Struct;
    e.st = std::make_shared<qb::Struct>();
    qb::Item di;
    di.name = K("desc_id");
    di.value.type = qb::Type::Key;
    di.value.key = descId;
    e.st->items.push_back(di);
    pa.arr->values.push_back(e);
    return *pa.arr->values.back().st;
}

}  // namespace

std::vector<Item> catalog(const Globals& g) {
    std::vector<Item> out;
    const qb::Value* mel = g.find(K("master_editable_list"));
    if (!mel || mel->type != qb::Type::Array || !mel->arr) return out;
    for (const qb::Value& pe : mel->arr->values) {
        if (pe.type != qb::Type::Struct || !pe.st) continue;
        const uint32_t part = keyOf(pe.st->find(K("part")));
        if (!part) continue;
        const qb::Value* av = g.find(part);
        if (!av || av->type != qb::Type::Array || !av->arr) continue;
        for (const qb::Value& e : av->arr->values) {
            if (e.type != qb::Type::Struct || !e.st) continue;
            const uint32_t descId = keyOf(e.st->find(K("desc_id")));
            if (!descId || descId == K("none")) continue;
            const qb::Value* pr = e.st->find(K("price"));
            const bool priced = pr && pr->type == qb::Type::Int;
            const bool locked = hasFlag(*e.st, K("locked"));
            if (!priced && !locked) continue;  // a free, always-available default option
            Item it;
            it.part = part;
            it.descId = descId;
            it.price = priced ? pr->i : 0;
            it.priced = priced;
            it.locked = locked;
            if (const qb::Value* fd = e.st->find(K("frontend_desc"))) it.frontendDesc = fd->type == qb::Type::Int ? uint32_t(fd->i) : fd->key;
            out.push_back(it);
        }
    }
    return out;
}

int cash(const qb::Struct& bandInfo) {
    const qb::Value* c = bandInfo.find(K("cash"));
    return c && c->type == qb::Type::Int ? c->i : 0;
}

bool hasEnoughMoney(const qb::Struct& bandInfo, int price) { return price <= cash(bandInfo); }

bool partUnlocked(const qb::Struct& bandInfo, const Item& item) {
    if (!item.locked) return true;
    const qb::Struct* f = entryFlags(bandInfo, item.part, item.descId);
    return f && hasFlag(*f, K("unlocked"));
}

bool partPurchased(const qb::Struct& bandInfo, const Item& item) {
    if (!item.priced) return true;
    const qb::Struct* f = entryFlags(bandInfo, item.part, item.descId);
    return f && hasFlag(*f, K("purchased"));
}

bool owned(const qb::Struct& bandInfo, const Item& item) { return partUnlocked(bandInfo, item) && partPurchased(bandInfo, item); }

const char* buyName(Buy b) {
    switch (b) {
        case Buy::Ok: return "Ok";
        case Buy::NotPriced: return "NotPriced";
        case Buy::NotUnlocked: return "NotUnlocked";
        case Buy::AlreadyOwned: return "AlreadyOwned";
        case Buy::NotEnoughCash: return "NotEnoughCash";
    }
    return "?";
}

Buy purchase(qb::Struct& bandInfo, const Item& item) {
    if (!item.priced) return Buy::NotPriced;
    if (!partUnlocked(bandInfo, item)) return Buy::NotUnlocked;
    if (partPurchased(bandInfo, item)) return Buy::AlreadyOwned;
    if (!hasEnoughMoney(bandInfo, item.price)) return Buy::NotEnoughCash;
    // decrease_band_money: band_info.cash -= price.
    const int newCash = cash(bandInfo) - item.price;
    qb::Value& c = ensureItem(bandInfo, K("cash"), qb::Type::Int);
    c.type = qb::Type::Int;
    c.i = newCash;
    // set_current_band_part_flags(part, desc_id, purchased).
    addFlag(ensureEntry(bandInfo, item.part, item.descId), K("purchased"));
    return Buy::Ok;
}

void grantUnlock(qb::Struct& bandInfo, const Item& item) { addFlag(ensureEntry(bandInfo, item.part, item.descId), K("unlocked")); }

void grantUnlockPart(qb::Struct& bandInfo, uint32_t part, uint32_t descId) { addFlag(ensureEntry(bandInfo, part, descId), K("unlocked")); }

bool partUnlockedFlag(const qb::Struct& bandInfo, uint32_t part, uint32_t descId) {
    const qb::Struct* f = entryFlags(bandInfo, part, descId);
    return f && hasFlag(*f, K("unlocked"));
}

}  // namespace shop
