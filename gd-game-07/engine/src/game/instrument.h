// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "data/qb.h"

class Globals;

// Guitar Hero: World Tour's Create-A-Rockstar instrument editor (the
// "Create-A-Guitar" / CAG side of the CAS appearance system). Rendering-free;
// the faithful model of composing an instrument out of CAS parts, naming it,
// and saving it. Reuses the catalog + ownership from game/shop.h.
//
// An instrument is built from the CAS part arrays (one option chosen per slot):
//   Guitar/Bass: Body, Neck, Head, Pickups, Pickguards, Bridges, Knobs,
//                Strings, Highway (the CAS_Guitar_* / CAS_Bass_* arrays)
//   Drums:       CAS_Drums, CAS_Drums_Sticks, CAS_Drums_Highway
//   Mic:         CAS_Mic, CAS_Mic_Stand
// [C data: the CAS_<Instr>_<Slot> global arrays; cas_parts.qb]
//
// The chosen option per part is the disc's `cas_current_appearance` struct
// (part -> { desc_id, ... }); GetCASAppearancePart(part) returns that entry
// [C scripts cas_appearance.qb]. A part can only be chosen if it is OWNED
// (is_part_unlocked_purchased, cas_utils.qb — the store purchase / career
// unlock the same way the Rock Store sells them). The name is `instrument_name`
// (a wide string) and the kind is `instrument_type`; both are saved with the
// appearance into band_info (the same versioned save shop writes). [C scripts]
namespace instrument {

enum class Kind { Guitar, Bass, Drums, Mic };
const char* kindName(Kind k);            // "guitar" / "bass" / "drums" / "mic" (the instrument_type key)
bool kindFromKey(uint32_t key, Kind& k);  // the inverse, for load

// The CAS part arrays (slots) that make up an instrument of this kind, in the
// editor's slot order.
std::vector<uint32_t> slots(Kind k);

// Every selectable option (desc_id) of a CAS part, from its global array — all
// options, not only the priced/locked ones the store sells. `$None`/0 skipped.
std::vector<uint32_t> options(const Globals& g, uint32_t part);

// A composed instrument: a kind, a name, and a chosen desc_id per slot.
struct Instrument {
    Kind kind = Kind::Guitar;
    std::string name;
    std::vector<std::pair<uint32_t, uint32_t>> parts;  // part -> chosen desc_id, slot order
    uint32_t descIdFor(uint32_t part) const;
    void set(uint32_t part, uint32_t descId);  // set/replace a slot's choice
};

enum class SetPart { Ok, NotASlot, NotAnOption, NotOwned };
const char* setPartName(SetPart s);

// Choose `descId` for `part` on `inst`. The part must be a slot of inst.kind,
// `descId` must be an option of that part, and it must be OWNED (free default
// options are owned by default; priced/locked parts need the store purchase /
// career unlock — shop::owned against `bandInfo`). Mutates `inst` only on Ok.
SetPart choose(Instrument& inst, const Globals& g, const qb::Struct& bandInfo, uint32_t part, uint32_t descId);

// A fresh instrument of `kind` with each slot set to its first (default) option.
Instrument makeDefault(const Globals& g, Kind kind);

// Faithful persistence: write the instrument as a band_info-style struct
// (`instrument_type` key, `instrument_name` wstring, `cas_current_appearance`
// struct of part -> { desc_id }); read it back into an Instrument (slot order
// restored for the kind). Round-trips through game/save.
qb::Struct toBandInfo(const Instrument& inst);
Instrument fromBandInfo(const qb::Struct& bandInfo);

}  // namespace instrument
