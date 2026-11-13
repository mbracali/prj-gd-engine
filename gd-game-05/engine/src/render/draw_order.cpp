// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/draw_order.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

#include "render/scene_gpu.h"
#include "scene/milo.h"
#include "scene/objects.h"
#include "scene/ui_objects.h"

namespace gfx {

namespace {
float orderOf(const scene::Object* o) {
    const scene::Draw* d = drawOf(o);
    return d ? d->order : 0.0f;
}
}  // namespace

void sortDraws(std::vector<const scene::Object*>& roots) {
    std::stable_sort(roots.begin(), roots.end(), [&](const scene::Object* a, const scene::Object* b) { return orderOf(a) < orderOf(b); });
    for (size_t i = 0; i < roots.size();) {
        size_t j = i;
        while (j < roots.size() && orderOf(roots[j]) == orderOf(roots[i])) ++j;
        // Within [i, j): each class's places, refilled by name.
        std::map<std::string, std::vector<size_t>> places;
        for (size_t k = i; k < j; ++k) places[roots[k]->cls].push_back(k);
        for (auto& [cls, at] : places) {
            if (at.size() < 2) continue;
            std::vector<const scene::Object*> objs;
            for (size_t k : at) objs.push_back(roots[k]);
            std::sort(objs.begin(), objs.end(), [](const scene::Object* a, const scene::Object* b) { return std::strcmp(a->name.c_str(), b->name.c_str()) < 0; });
            for (size_t k = 0; k < at.size(); ++k) roots[at[k]] = objs[k];
        }
        i = j;
    }
}

std::vector<const scene::Object*> dirDrawList(const scene::Directory& dir) {
    std::set<std::string> held;
    for (const auto& o : dir.objects)
        if (const auto* g = o->as<scene::Group>()) held.insert(g->objects.begin(), g->objects.end());
    std::vector<const scene::Object*> roots;
    for (const auto& o : dir.objects)
        if (!held.count(o->name) && (o->as<scene::Mesh>() || o->as<scene::Group>() || o->as<scene::DirObj>() || o->as<scene::Text>() || o->as<scene::BandLabel>()))
            roots.push_back(o.get());
    sortDraws(roots);
    return roots;
}

int drawOrderInversions(const std::vector<const scene::Object*>& drawn) {
    int n = 0;
    for (size_t i = 0; i < drawn.size(); ++i)
        for (size_t j = i + 1; j < drawn.size(); ++j)
            if (orderOf(drawn[i]) > orderOf(drawn[j])) ++n;
    return n;
}

}  // namespace gfx
