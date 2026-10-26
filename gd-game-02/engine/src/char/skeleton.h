// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "scene/milo.h"
#include "scene/objects.h"

// A character's transforms: every object of its directory with a
// RndTransformable (bones "bone_*.mesh", spots, meshes, groups...), its
// local transform (what the servo and the IK change) and its world
// transform, kept lazily as RndTransformable does (a changed local dirties
// the subtree; WorldXfm recomputes up the parents). Objects whose Trans
// parent is the character directory itself (or nothing) hang off the root:
// the character's own world transform (its placement, teleport, root motion).
namespace chr {

class Skeleton {
public:
    // Every Trans-bearing object of `dir` (not its sub-directories').
    void build(const scene::Directory& dir);

    int find(const std::string& name) const;
    int size() const { return int(nodes_.size()); }
    const std::string& name(int i) const { return nodes_[size_t(i)].object->name; }
    const scene::Object* object(int i) const { return nodes_[size_t(i)].object; }
    int parent(int i) const { return nodes_[size_t(i)].parent; }
    const std::vector<int>& children(int i) const { return nodes_[size_t(i)].children; }

    const scene::Xfm& local(int i) const { return nodes_[size_t(i)].local; }
    void setLocal(int i, const scene::Xfm& x);
    // The bind (as loaded) local transform.
    const scene::Xfm& bind(int i) const { return nodes_[size_t(i)].bind; }
    const scene::Xfm& world(int i);
    // Sets a world transform (the local follows from the parent's world).
    void setWorld(int i, const scene::Xfm& w);

    // The character's world transform (the root of every top-level object).
    const scene::Xfm& root() const { return root_; }
    void setRoot(const scene::Xfm& x);

    // Re-parents i (-1: the root), keeping its local (bone_fret detached by
    // the fret IK...).
    void setParent(int i, int parent);

private:
    struct Node {
        const scene::Object* object = nullptr;
        int parent = -1;
        std::vector<int> children;
        scene::Xfm local, bind, world;
        bool dirty = true;
    };
    void dirty(int i);
    std::vector<Node> nodes_;
    std::unordered_map<std::string, int> index_;
    scene::Xfm root_;
};

// Xfm helpers (row vectors: p' = p rot + pos; rows are the local axes).
scene::Xfm inverse(const scene::Xfm& x);          // rigid or scaled (general 3x3 inverse)
void normalizeRows(scene::Xfm& x);                // rows to unit length (0x8214F7F0 [I])
std::array<float, 3> xformPoint(const scene::Xfm& x, const std::array<float, 3>& p);
std::array<float, 3> xformVector(const scene::Xfm& x, const std::array<float, 3>& v);

}  // namespace chr
