// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/qb.h"

class Disc;

// The QScript global namespace: every section of every QB file the game
// loads at boot (PAK/qb.pak.xen + its .pab; the language paks qb_<lang>
// overlay their strings). A later file's section replaces an earlier one
// of the same name, the way loading a script file redefines its globals
// [I: confirm the redefinition order in the XEX's QB loader].
class Globals {
public:
    // Loads qb.pak; throws std::runtime_error.
    void load(const Disc& disc);
    // Adds one decoded file's sections (song paks' .mid.qb, zone scripts).
    void add(const qb::File& file);

    const qb::Value* find(uint32_t name) const {
        auto it = values_.find(name);
        return it == values_.end() ? nullptr : &it->second;
    }
    // The global `name`, created (empty) if missing: writes by `change`.
    qb::Value* ensure(uint32_t name) { return &values_[name]; }
    size_t size() const { return values_.size(); }
    size_t files() const { return files_; }

    // A struct's items with its unnamed key references expanded: an unnamed
    // `<name>` item inside a struct stands for the items of the global
    // struct `name` (gh3_songlist_props = {<download_songlist_props>
    // <permanent_songlist_props>}) [I: QScript's struct include; confirm in
    // the VM's struct builder].
    std::vector<qb::Item> expand(const qb::Struct& st) const;
    // A struct item by name after expansion (later items win), or null.
    const qb::Value* field(const qb::Struct& st, uint32_t name) const;
    // Follows KeyPtr values to the global they name (depth-limited).
    const qb::Value* resolve(const qb::Value& v) const;

private:
    std::unordered_map<uint32_t, qb::Value> values_;
    size_t files_ = 0;
};

// Helpers for reading tuning values.
float qbNumber(const Globals& g, uint32_t name, float fallback);
int qbInt(const Globals& g, uint32_t name, int fallback);
