// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class Globals;

// gh4's highway materials, built by script data (guitar_material.qb
// ingame_materials / global_materials): each a template with up to three
// passes, each pass a grey texture (z_in_game's basic_gems.tex) tinted by a
// colour; the colours come from the gem set (standard_gems, standard_stars,
// standard_fx) [C data]. The passes are drawn in order, alpha-blended (or
// added, blendmode add) [I: the 3Pass shaders' combine, MaterialLibrary not
// decoded].
namespace gemmat {

struct Pass {
    uint32_t texture = 0;
    std::array<float, 4> color{1, 1, 1, 1};
};

struct Material {
    uint32_t name = 0, templ = 0;
    std::vector<Pass> passes;  // pass 0, 1, 2 (missing textures dropped)
    bool additive = false;
    float ucells = 1, vcells = 1, fps = 0;  // AnimatedTexture: a flip-book
};

// One lane's materials from button_models (green .. orange).
struct Lane {
    uint32_t gem = 0, hammer = 0, star = 0, starHammer = 0, spGem = 0, spHammer = 0, whammy = 0, spWhammy = 0;
    // The now-bar button (button_up_models, guitar_highway.qb): scale -1 mirrors [C data].
    uint32_t lip = 0, mid = 0, head = 0, headLit = 0, neck = 0, down = 0;
    float scale = 1;
};

class Library {
public:
    // Reads the globals; false when the gh4 tables are missing (GH3 data).
    bool load(const Globals& g);
    const Material* find(uint32_t name) const {
        auto it = byName_.find(name);
        return it == byName_.end() ? nullptr : &materials_[it->second];
    }
    const Lane& lane(int l) const { return lanes_[size_t(l)]; }
    size_t size() const { return materials_.size(); }

private:
    std::vector<Material> materials_;
    std::unordered_map<uint32_t, size_t> byName_;
    std::array<Lane, 5> lanes_{};
};

}  // namespace gemmat
