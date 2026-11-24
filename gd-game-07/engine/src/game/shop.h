// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <vector>

#include "data/qb.h"

class Globals;

// Guitar Hero: World Tour's in-game store (the "Rock Store" / GH Store).
// Rendering-free; the faithful port of the disc's own purchase mechanic.
//
// What the store sells is the Create-A-Rocker (CAR/CAS) catalog: clothing,
// accessories, heads/torsos/legs/shoes, and instrument parts (guitars, basses,
// drums, mics) and their finishes. Each option in a CAS part array
// (master_editable_list, scripts\game\cas\cas_parts.qb) carries an optional
// `price` (cash) and an optional `$locked` flag (career-gated). [C data]
//
// The currency is the current band's `cash` (band_info.cash), earned in the
// career (Progression cash milestones / gig earnings; increase_band_money) —
// career is a later onboarding step, so cash earning is a dependency.
//
// A purchase (the disc's continue_purchase_item, scripts\engine\menu\listmenu.qb):
//   has_enough_money(price)  ->  decrease_band_money(price)
//   ->  set_current_band_part_flags(part, desc_id, purchased)
// persisted to band_info in the save (SetGlobalTags / SaveToMemoryCard).
// Ownership is is_part_unlocked_purchased (cas_utils.qb): an item with
// `$locked` needs the save's `unlocked` flag (a career reward) AND, if it has a
// `$price`, the `purchased` flag; an item without `$locked` is unlocked by
// default and one without `$price` is purchased by default. [C scripts
// is_part_unlocked / is_part_purchased / is_part_unlocked_purchased]
namespace shop {

// One purchasable / unlockable catalog item from the real disc data.
struct Item {
    uint32_t part = 0;          // the CAS part array name (master_editable_list entry)
    uint32_t descId = 0;        // the option's desc_id
    uint32_t frontendDesc = 0;  // its localized display-name key (frontend_desc), 0 if none
    int price = 0;              // cash cost (0 when not priced)
    bool priced = false;        // the option has a `price`
    bool locked = false;        // the option has a `$locked` flag (career-gated)
};

// Every CAS option on the disc that is priced and/or locked, in
// master_editable_list order (desc_id $None options skipped). Reads the static
// globals (qb.pak); invents nothing.
std::vector<Item> catalog(const Globals& g);

// The current band's cash balance (band_info.cash), 0 if unset.
int cash(const qb::Struct& bandInfo);
// has_enough_money: price <= cash.
bool hasEnoughMoney(const qb::Struct& bandInfo, int price);

// is_part_unlocked: no `$locked` -> true; else the save's `unlocked` flag.
bool partUnlocked(const qb::Struct& bandInfo, const Item& item);
// is_part_purchased: no `price` -> true; else the save's `purchased` flag.
bool partPurchased(const qb::Struct& bandInfo, const Item& item);
// is_part_unlocked_purchased: owned & usable in the rest of the game.
bool owned(const qb::Struct& bandInfo, const Item& item);

enum class Buy {
    Ok,             // purchased: cash deducted, `purchased` flag set
    NotPriced,      // not a store item (no price)
    NotUnlocked,    // locked and not career-unlocked yet (career dependency)
    AlreadyOwned,   // already purchased
    NotEnoughCash,  // price > cash
};
const char* buyName(Buy b);

// Purchase `item` for the current band (continue_purchase_item). Mutates
// `bandInfo`: deducts cash and sets the `purchased` flag in
// band_info.unlock_purchased.<part> for desc_id. No state changes unless the
// result is Ok.
Buy purchase(qb::Struct& bandInfo, const Item& item);

// Grant the career `unlocked` flag for `item` (a progression reward:
// set_current_band_part_flags ... unlocked). Separate from a cash purchase; the
// career that awards these is a later onboarding step.
void grantUnlock(qb::Struct& bandInfo, const Item& item);

}  // namespace shop
