// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "char/skeleton.h"

#include <algorithm>
#include <cmath>

#include "render/scene_gpu.h"

namespace chr {

void Skeleton::build(const scene::Directory& dir) {
    nodes_.clear();
    index_.clear();
    for (const auto& o : dir.objects) {
        const scene::Trans* t = gfx::transOf(o.get());
        if (!t) continue;
        Node n;
        n.object = o.get();
        n.local = n.bind = t->local;
        index_.emplace(o->name, int(nodes_.size()));
        nodes_.push_back(n);
    }
    for (size_t i = 0; i < nodes_.size(); ++i) {
        const scene::Trans* t = gfx::transOf(nodes_[i].object);
        if (!t || t->parent.empty() || t->parent == dir.name) continue;
        auto it = index_.find(t->parent);
        if (it == index_.end() || it->second == int(i)) continue;
        nodes_[i].parent = it->second;
        nodes_[size_t(it->second)].children.push_back(int(i));
    }
}

int Skeleton::find(const std::string& name) const {
    auto it = index_.find(name);
    return it == index_.end() ? -1 : it->second;
}

void Skeleton::dirty(int i) {
    Node& n = nodes_[size_t(i)];
    if (n.dirty) return;
    n.dirty = true;
    for (int c : n.children) dirty(c);
}

void Skeleton::setLocal(int i, const scene::Xfm& x) {
    nodes_[size_t(i)].local = x;
    nodes_[size_t(i)].dirty = false;  // force the subtree walk
    dirty(i);
}

const scene::Xfm& Skeleton::world(int i) {
    Node& n = nodes_[size_t(i)];
    if (n.dirty) {
        n.world = scene::compose(n.local, n.parent >= 0 ? world(n.parent) : root_);
        n.dirty = false;
    }
    return n.world;
}

void Skeleton::setWorld(int i, const scene::Xfm& w) {
    const int p = nodes_[size_t(i)].parent;
    const scene::Xfm parentWorld = p >= 0 ? world(p) : root_;
    setLocal(i, scene::compose(w, inverse(parentWorld)));
}

void Skeleton::setRoot(const scene::Xfm& x) {
    root_ = x;
    for (size_t i = 0; i < nodes_.size(); ++i)
        if (nodes_[i].parent < 0) {
            nodes_[i].dirty = false;
            dirty(int(i));
        }
}

void Skeleton::setParent(int i, int parent) {
    Node& n = nodes_[size_t(i)];
    if (n.parent >= 0) {
        auto& c = nodes_[size_t(n.parent)].children;
        c.erase(std::remove(c.begin(), c.end(), i), c.end());
    }
    n.parent = parent;
    if (parent >= 0) nodes_[size_t(parent)].children.push_back(i);
    n.dirty = false;
    dirty(i);
}

scene::Xfm inverse(const scene::Xfm& x) {
    const auto& m = x.rot;
    const float a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], k = m[8];
    const float A = e * k - f * h, B = -(d * k - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-20f) det = 1e-20f;
    const float id = 1.0f / det;
    scene::Xfm r;
    r.rot = {A * id, -(b * k - c * h) * id, (b * f - c * e) * id,
             B * id, (a * k - c * g) * id, -(a * f - c * d) * id,
             C * id, -(a * h - b * g) * id, (a * e - b * d) * id};
    for (int j = 0; j < 3; ++j) {
        float s = 0;
        for (int q = 0; q < 3; ++q) s += x.pos[size_t(q)] * r.rot[size_t(q * 3 + j)];
        r.pos[size_t(j)] = -s;
    }
    return r;
}

void normalizeRows(scene::Xfm& x) {
    for (int r = 0; r < 3; ++r) {
        float* row = &x.rot[size_t(r * 3)];
        const float l = std::sqrt(row[0] * row[0] + row[1] * row[1] + row[2] * row[2]);
        if (l > 1e-12f)
            for (int k = 0; k < 3; ++k) row[k] /= l;
    }
}

std::array<float, 3> xformPoint(const scene::Xfm& x, const std::array<float, 3>& p) {
    std::array<float, 3> r;
    for (int j = 0; j < 3; ++j) r[size_t(j)] = p[0] * x.rot[size_t(j)] + p[1] * x.rot[size_t(3 + j)] + p[2] * x.rot[size_t(6 + j)] + x.pos[size_t(j)];
    return r;
}

std::array<float, 3> xformVector(const scene::Xfm& x, const std::array<float, 3>& v) {
    std::array<float, 3> r;
    for (int j = 0; j < 3; ++j) r[size_t(j)] = v[0] * x.rot[size_t(j)] + v[1] * x.rot[size_t(3 + j)] + v[2] * x.rot[size_t(6 + j)];
    return r;
}

}  // namespace chr
