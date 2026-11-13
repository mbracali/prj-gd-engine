// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/gem_materials.h"

#include <string>

#include "data/qbkey.h"
#include "script/globals.h"

namespace gemmat {
namespace {

uint32_t K(const char* s) { return qbkey::key(s); }

const qb::Array* arrayOf(const qb::Value* v) { return v && v->type == qb::Type::Array && v->arr ? v->arr.get() : nullptr; }
const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }
uint32_t keyOf(const qb::Value* v) {
    return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0;
}

// The gem set's colours: element -> colour (standard_gems, standard_stars,
// standard_fx: the sets the cheats menu swaps; the defaults without a cheat
// [I: the native that applies Gem_Set]).
using Colors = std::unordered_map<uint32_t, std::array<float, 4>>;

std::array<float, 4> vec4(const qb::Array& a) {
    std::array<float, 4> c{1, 1, 1, 1};
    for (size_t i = 0; i < 4 && i < a.values.size(); ++i) c[i] = a.values[i].number();
    return c;
}

bool colorOf(const Globals& g, const Colors& set, const qb::Value* v, std::array<float, 4>& out) {
    if (!v) return false;
    if (const qb::Array* a = arrayOf(v)) {
        out = vec4(*a);
        return true;
    }
    if (v->type == qb::Type::KeyPtr || v->type == qb::Type::Key) {
        if (auto it = set.find(v->key); it != set.end()) {
            out = it->second;
            return true;
        }
        if (const qb::Array* a = arrayOf(g.find(v->key))) {  // col_white, col_half_alpha ...
            out = vec4(*a);
            return true;
        }
    }
    return false;
}

// One materialprops list into the passes (m_samppass<n>diffuse,
// m_pspass<n>materialcolor) and the flip-book (m_ucells, m_vcells, m_FPS).
void applyProps(const Globals& g, const Colors& set, const qb::Array& props, Material& m) {
    static const uint32_t kTex[3] = {K("m_samppass0diffuse"), K("m_samppass1diffuse"), K("m_samppass2diffuse")};
    static const uint32_t kCol[3] = {K("m_pspass0materialcolor"), K("m_pspass1materialcolor"), K("m_pspass2materialcolor")};
    for (const qb::Value& pv : props.values) {
        const qb::Struct* p = structOf(&pv);
        if (!p) continue;
        const uint32_t name = keyOf(p->find(K("name")));
        for (size_t i = 0; i < 3; ++i) {
            if (name == kTex[i]) m.passes[i].texture = keyOf(p->find(K("textureproperty")));
            if (name == kCol[i]) colorOf(g, set, p->find(K("vectorproperty")), m.passes[i].color);
        }
        if (const qb::Value* f = p->find(K("floatproperty"))) {
            if (name == K("m_ucells")) m.ucells = f->number();
            if (name == K("m_vcells")) m.vcells = f->number();
            if (name == K("m_fps")) m.fps = f->number();
        }
    }
}

}  // namespace

bool Library::load(const Globals& g) {
    materials_.clear(), byName_.clear(), lanes_ = {};
    Colors set;
    for (const char* s : {"standard_gems", "standard_stars", "standard_fx"})
        if (const qb::Array* a = arrayOf(g.find(K(s))))
            for (const qb::Value& e : a->values)
                if (const qb::Struct* st = structOf(&e))
                    if (const qb::Array* c = arrayOf(st->find(K("color")))) set[keyOf(st->find(K("element")))] = vec4(*c);
    for (const char* group : {"global_materials", "ingame_materials"}) {
        const qb::Array* arr = arrayOf(g.find(K(group)));
        if (!arr) continue;
        for (const qb::Value& e : arr->values) {
            const qb::Struct* st = structOf(&e);
            if (!st) continue;
            Material base;
            base.passes.resize(3);
            if (const qb::Struct* bd = structOf(st->find(K("base_data")))) {
                base.templ = keyOf(bd->find(K("template")));
                base.additive = keyOf(bd->find(K("blendmode"))) == K("add");
            }
            if (const qb::Array* bp = arrayOf(st->find(K("base_props")))) applyProps(g, set, *bp, base);
            auto add = [&](uint32_t name, const qb::Array* props) {
                Material m = base;
                m.name = name;
                if (props) applyProps(g, set, *props, m);
                std::vector<Pass> kept;
                for (const Pass& p : m.passes)
                    if (p.texture) kept.push_back(p);
                m.passes = std::move(kept);
                byName_[name] = materials_.size();
                materials_.push_back(std::move(m));
            };
            if (const qb::Array* list = arrayOf(st->find(K("list")))) {
                for (const qb::Value& le : list->values)
                    if (const qb::Struct* ls = structOf(&le)) add(keyOf(ls->find(K("name"))), arrayOf(ls->find(K("materialprops"))));
            } else if (const qb::Struct* bd = structOf(st->find(K("base_data")))) {
                add(keyOf(bd->find(K("name"))), nullptr);
            }
        }
    }
    // button_models[0]: the five guitar lanes [C data guitar_gems.qb].
    const qb::Array* bm = arrayOf(g.find(K("button_models")));
    const qb::Struct* models = bm && !bm->values.empty() ? structOf(&bm->values[0]) : nullptr;
    if (!models || materials_.empty()) return false;
    static const char* const kLanes[5] = {"green", "red", "yellow", "blue", "orange"};
    for (int l = 0; l < 5; ++l) {
        const qb::Struct* s = structOf(models->find(K(kLanes[l])));
        if (!s) return false;
        Lane& ln = lanes_[size_t(l)];
        ln.gem = keyOf(s->find(K("gem_material")));
        ln.hammer = keyOf(s->find(K("gem_hammer_material")));
        ln.star = keyOf(s->find(K("star_material")));
        ln.starHammer = keyOf(s->find(K("star_hammer_material")));
        ln.spGem = keyOf(s->find(K("star_power_material")));
        ln.spHammer = keyOf(s->find(K("star_power_hammer_material")));
        ln.whammy = keyOf(s->find(K("whammy_material")));
        ln.spWhammy = keyOf(s->find(K("star_power_whammy_material")));
    }
    const qb::Array* bu = arrayOf(g.find(K("button_up_models")));
    const qb::Struct* ups = bu && !bu->values.empty() ? structOf(&bu->values[0]) : nullptr;
    for (int l = 0; ups && l < 5; ++l) {
        const qb::Struct* s = structOf(ups->find(K(kLanes[l])));
        if (!s) continue;
        Lane& ln = lanes_[size_t(l)];
        ln.lip = keyOf(s->find(K("material_lip")));
        ln.mid = keyOf(s->find(K("material_mid")));
        ln.head = keyOf(s->find(K("material_head")));
        ln.headLit = keyOf(s->find(K("material_head_lit")));
        ln.neck = keyOf(s->find(K("material_neck")));
        ln.down = keyOf(s->find(K("material_down")));
        if (const qb::Value* sc = s->find(K("scale"))) ln.scale = sc->number();
    }
    return find(lanes_[0].gem) != nullptr;
}

}  // namespace gemmat
