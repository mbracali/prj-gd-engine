// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// gh4 (Metallica) descinterface screen elements (docs/ui.md "Descs").
//
// `CreateScreenElement {type = descinterface desc = '<name>' ...}` builds the
// element tree of the global uidesc_<name> (a QB file ui\uidesc_<name>.qb in
// a zone or UI pak): {descversion name rect aliases props materials
// formatversion elements}, `elements` = {props = {...} children = [...]}
// recursively, each props struct a screen element's own (type, local_id,
// pos, pos_anchor, just, dims, texture, text, font, ...) [C data: every
// uidesc on the disc]. `aliases` name a descendant by a path of local ids
// (desc_resolvealias); `props` name a property of a descendant (`target`),
// set through se_setprops on the desc element [C data; I: the C's rules].
#include "ui/ui_system.h"

#include <algorithm>
#include <memory>

#include "data/qbkey.h"
#include "script/vm.h"

namespace ui {
namespace {

uint32_t K(const char* s) { return qbkey::key(s); }
const qb::Struct* structOf(const qb::Value* v) { return v && v->type == qb::Type::Struct && v->st ? v->st.get() : nullptr; }
uint32_t keyOf(const qb::Value* v) { return v && (v->type == qb::Type::Key || v->type == qb::Type::KeyPtr) ? v->key : 0; }

uint32_t nextDescId() {
    static uint32_t n = 0xA0000000u;
    return n++;
}

const qb::Struct* descOf(qs::Vm& vm, uint32_t desc) { return desc ? structOf(vm.global(desc)) : nullptr; }

}  // namespace

bool UiSystem::buildDesc(Element& e, const std::string& name) {
    const uint32_t key = qbkey::key("uidesc_" + name);
    const qb::Struct* d = descOf(vm_, key);
    if (!d) return false;
    const qb::Struct* root = structOf(d->find(K("elements")));
    if (!root) return false;
    e.desc = key;
    // The desc element is the tree's root: its type and props (the call's
    // own pos / z_priority, applied again after, win) [I].
    if (const qb::Struct* props = structOf(root->find(K("props")))) {
        qb::Struct q = *props;
        q.items.erase(std::remove_if(q.items.begin(), q.items.end(), [](const qb::Item& it) { return it.name == K("type"); }), q.items.end());
        if (const qb::Value* t = props->find(K("type")); t && t->key) e.type = t->key;
        applyProps(e, q, true);
    }
    struct Todo {
        Element* parent;
        const qb::Struct* node;
    };
    std::vector<Todo> todo;
    auto addChildren = [&todo](Element* parent, const qb::Struct* node) {
        const qb::Value* ch = node->find(K("children"));
        if (!ch || !ch->arr) return;
        for (const qb::Value& c : ch->arr->values)
            if (c.st) todo.push_back({parent, c.st.get()});
    };
    addChildren(&e, root);
    for (size_t i = 0; i < todo.size(); ++i) {
        const Todo t = todo[i];
        const qb::Struct* props = structOf(t.node->find(K("props")));
        auto c = std::make_unique<Element>();
        c->id = nextDescId();
        c->type = props ? keyOf(props->find(K("type"))) : K("containerelement");
        if (!c->type) c->type = K("containerelement");
        c->parent = t.parent;
        Element* raw = c.get();
        byId_[raw->id] = raw;
        t.parent->children.push_back(std::move(c));
        if (props) applyProps(*raw, *props, true);
        ++stats_.created;
        addChildren(raw, t.node);
    }
    // The original applies each subtree before the next sibling; the order
    // above (breadth first) only matters for z ties [I].
    return true;
}

// A path [{validatelocalid = <root>}, {index = n validatelocalid = <id>}, ...]
// from the desc element [C data: the aliases' and props' paths].
Element* UiSystem::descPath(Element& e, const qb::Value* path) {
    if (!path || !path->arr) return nullptr;
    Element* at = &e;
    bool first = true;
    for (const qb::Value& step : path->arr->values) {
        const qb::Struct* s = structOf(&step);
        if (!s) return nullptr;
        const uint32_t lid = keyOf(s->find(K("validatelocalid")));
        if (first) {
            first = false;
            if (!lid || at->localId == lid) continue;  // the root itself
        }
        Element* next = nullptr;
        const qb::Value* idx = s->find(K("index"));
        if (idx && idx->type == qb::Type::Int && idx->i >= 0 && size_t(idx->i) < at->children.size()) {
            Element* c = at->children[size_t(idx->i)].get();
            if (!lid || c->localId == lid) next = c;
        }
        if (!next)
            for (auto& c : at->children)
                if (c->localId == lid) {
                    next = c.get();
                    break;
                }
        if (!next) return nullptr;
        at = next;
    }
    return at;
}

Element* UiSystem::descAlias(Element& e, uint32_t alias) {
    const qb::Struct* d = descOf(vm_, e.desc);
    const qb::Value* aliases = d ? d->find(K("aliases")) : nullptr;
    if (aliases && aliases->arr)
        for (const qb::Value& a : aliases->arr->values)
            if (const qb::Struct* s = structOf(&a); s && keyOf(s->find(K("name"))) == alias) return descPath(e, s->find(K("path")));
    // A local id below the desc (no alias of that name) [I].
    std::vector<Element*> stack{&e};
    while (!stack.empty()) {
        Element* x = stack.back();
        stack.pop_back();
        if (x != &e && x->localId == alias) return x;
        for (auto& c : x->children) stack.push_back(c.get());
    }
    return nullptr;
}

void UiSystem::descSetProps(Element& e, const qb::Struct& p) {
    const qb::Struct* d = descOf(vm_, e.desc);
    const qb::Value* props = d ? d->find(K("props")) : nullptr;
    qb::Struct own;
    for (const qb::Item& it : p.items) {
        const qb::Struct* match = nullptr;
        if (it.name && props && props->arr)
            for (const qb::Value& pv : props->arr->values)
                if (const qb::Struct* s = structOf(&pv); s && keyOf(s->find(K("name"))) == it.name) {
                    match = s;
                    break;
                }
        if (!match) {
            if (it.name != K("desc") && it.name != K("type") && it.name != K("parent") && it.name != K("id")) own.items.push_back(it);
            continue;
        }
        Element* target = descPath(e, match->find(K("path")));
        const uint32_t prop = keyOf(match->find(K("target")));
        if (!target || !prop) continue;
        qb::Struct one;
        qs::Vm::set(one, prop, it.value);
        applyProps(*target, one, false);
    }
    if (!own.items.empty()) applyProps(e, own, false);
}

// The value of a desc's named prop (its target on its path element) [I: the
// targets the scripts read: pos, scale, alpha, rgba, dims].
bool UiSystem::descGetProp(Element& e, uint32_t name, qb::Value& out) {
    const qb::Struct* d = descOf(vm_, e.desc);
    const qb::Value* props = d ? d->find(K("props")) : nullptr;
    if (!props || !props->arr) return false;
    for (const qb::Value& pv : props->arr->values) {
        const qb::Struct* s = structOf(&pv);
        if (!s || keyOf(s->find(K("name"))) != name) continue;
        Element* t = descPath(e, s->find(K("path")));
        const uint32_t prop = keyOf(s->find(K("target")));
        if (!t) return false;
        auto pair = [&out](float a, float b) {
            out = qb::Value{};
            out.type = qb::Type::Pair;
            out.f[0] = a, out.f[1] = b;
        };
        if (prop == K("pos")) pair(t->x, t->y);
        else if (prop == K("scale")) pair(t->sx, t->sy);
        else if (prop == K("dims")) pair(t->w, t->h);
        else if (prop == K("alpha")) out = qs::Vm::makeFloat(t->alpha);
        else if (prop == K("rot_angle")) out = qs::Vm::makeFloat(t->rot);
        else if (prop == K("text")) out = qs::Vm::makeString(t->text);
        else if (prop == K("rgba")) {
            out = qb::Value{};
            out.type = qb::Type::Array;
            out.arr = std::make_shared<qb::Array>();
            out.arr->type = qb::Type::Int;
            for (uint8_t b : t->rgba) out.arr->values.push_back(qs::Vm::makeInt(b));
        } else {
            return false;
        }
        return true;
    }
    return false;
}

}  // namespace ui
