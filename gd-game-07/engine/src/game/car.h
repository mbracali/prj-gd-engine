// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data/qb.h"

class Globals;

// Guitar Hero: World Tour's Create-A-Rocker (CAR): the character-creation
// model. Rendering-free; the faithful port of the disc's own create / edit /
// name / save mechanic. The 3-D editor UI (the z_soundcheck board menus) is the
// same deferred live-mouse surface as the other onboarding steps — this models
// the DATA and the mutations under it.
//
// A created character is a musician **profile** struct, the same shape
// band.cpp's memberGh4 consumes:
//   { name, fullname, allowed_parts, appearance { <CAS part> = { desc_id } }, blurb, genre }
// New characters live in the savegame's **custom_profiles** global array
// (add_new_custom_profile, scripts\guitar\guitar_band_profile_scripts.qb); the
// save round-trips the whole array. A new character starts from
// default_custom_musician_profile_male / _female (guitar_band_profile.qb) and
// is given a fresh `custom_character_<N>` name (new_custom_character_name,
// first free 0..99). Editable slots are master_editable_list
// (scripts\game\cas\cas_parts.qb) filtered to the character's gender
// (cas_physique = Male/FemalePhysique → the CAS_Male_* vs CAS_Female_* parts).
// Each slot's option must be owned (is_part_unlocked_purchased, game/shop.h).
// [C data / scripts; see docs/FINDINGS.md "Onboarding Step 7.5".]
namespace car {

// The savegame global array of created characters.
constexpr const char* kCustomProfiles = "custom_profiles";
// The most custom characters a savegame can hold (new_custom_character_name
// searches custom_character_0..99) [C script].
constexpr int kMaxCustomCharacters = 100;

// The editable CAS slots for a gender, in master_editable_list order: the
// common slots (cas_body, cas_physique, CAS_Full_Body, CAS_Eyes), the gender's
// body/anim slots (CAS_Female_* or CAS_Male_*) and the instrument slots +
// CAS_Band_Logo. Reads the disc's master_editable_list; invents nothing.
std::vector<uint32_t> editableSlots(const Globals& g, bool female);
// Is `part` one of this gender's editable slots?
bool isEditableSlot(const Globals& g, bool female, uint32_t part);

// Create a new `female`/male character from default_custom_musician_profile_*,
// give it a fresh custom_character_<N> name (new_custom_character_name) and
// append it to savegame.custom_profiles (add_new_custom_profile). Returns its
// name key, or 0 on failure (no default profile, or 100 characters already).
uint32_t newCharacter(const Globals& g, qb::Struct& savegame, bool female);

// The created profile with name key `id`, or null.
const qb::Struct* profile(const qb::Struct& savegame, uint32_t id);
// is_completely_custom_musician: the index of `id` in custom_profiles, or -1.
int customIndex(const qb::Struct& savegame, uint32_t id);
// The number of created characters (custom_profiles size).
int count(const qb::Struct& savegame);
// The gender of a created character (its cas_physique == FemalePhysique).
bool isFemale(const qb::Struct& savegame, uint32_t id);

enum class Set {
    Ok,               // the slot was set
    NoSuchCharacter,  // `id` is not a created character
    NotEditable,      // `part` is not an editable slot for this character's gender
    NoSuchOption,     // `descId` is not an option in `part`'s CAS array
    NotOwned,         // the option is locked / not purchased (is_part_unlocked_purchased)
};
const char* setName(Set s);

// Set an editable slot to a catalog option (modify_custom_profile_appearance:
// appearance.<part>.desc_id = descId). Validates the slot is editable for the
// character's gender, the option exists in the part array, and — when
// `bandInfo` is given — that the option is owned per game/shop.h
// (is_part_unlocked_purchased). Pass bandInfo = nullptr to skip the ownership
// gate (an always-available default option). No change unless the result is Ok.
Set setPart(const Globals& g, qb::Struct& savegame, uint32_t id, uint32_t part, uint32_t descId, const qb::Struct* bandInfo);

// The desc_id currently in a character's slot, or 0 if unset.
uint32_t partOf(const qb::Struct& savegame, uint32_t id, uint32_t part);

// Set the character's display name (modify_custom_profile_fullname). False if
// `id` is not a created character.
bool setFullName(qb::Struct& savegame, uint32_t id, const std::string& name);
std::string fullName(const qb::Struct& savegame, uint32_t id);

// Remove a created character (delete_custom_profile). False if not found.
bool deleteCharacter(qb::Struct& savegame, uint32_t id);

}  // namespace car
