// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// GH3's atoms: a named list of steps (an atom: a script to run once) with
// their conditions (depends_on: other atoms complete, or a script that
// returns true). The career registers GH3_Career_Progression as
// "Progression" when a difficulty is chosen, and Progression_SongWon /
// SongFailed update it: that is what completes a tier's songs, unlocks the
// encore (Progression_TierEncoreUnlock: current_transition = preencore) or
// the boss, completes the tier and unlocks the next one [C scripts
// guitar_progression.qb, guitar_coop.qb]. The engine side, from the XEX:
//   RegisterAtoms 0x821237F0, UpdateAtoms 0x821182A8 -> 0x822B0978 (the
//   list in order; an atom still running stops the pass), an atom's update
//   0x8239F040, its conditions 0x8239EBE0 / 0x82422B18 / 0x8239E740, active
//   0x8239EDB8 (the atoms waiting on it disabled, 0x8239ECB0), complete
//   0x8239E8F8 (those relocked and updated at once, 0x8239F158), the script
//   atom 0x82422C08 (spawned and run now 0x82422D08, complete when its
//   script has ended 0x82422D90), the conditions' classes 0x82422998 (the
//   `required` array, indexed by SetProgressionDifficulty's index),
//   0x82423090 (atom: complete) and 0x824238C0 (scr: its value; permanent
//   by default), goal `all` 0x824458C0 [C].
// The achievement list (Achievement_Atoms) is not run: it only awards the
// Xbox achievements, dropped in the port (GD04-013).
#include "script/atoms.h"

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "data/qbkey.h"
#include "script/vm.h"

namespace {

using qs::ScriptRun;
using qs::Vm;
constexpr uint32_t K(const char* s) { return qbkey::key(s); }

enum class State { Locked, Active, Complete, Disabled };

struct Dep {
    bool isAtom = false;
    std::vector<size_t> atoms;  // isAtom: the atoms that must be complete
    uint32_t scr = 0;           // else: the script whose value it is
    qb::Struct params;
    std::vector<int> required;  // per difficulty; empty: always required
    bool permanent = true;
    bool satisfied = false;
};

struct Atom {
    uint32_t name = 0;
    uint32_t script = 0;
    qb::Struct params;
    bool toolOnly = false;
    State state = State::Locked;
    std::vector<Dep> deps;
    std::vector<size_t> waiting;  // the atoms with a condition on this one
    uint32_t run = 0;             // its script's run (active)
};

struct Graph {
    std::vector<Atom> atoms;
    bool updating = false;
};

// A flag in a struct: a named item or a nameless key (StructureContains).
bool hasFlag(const Globals& g, const qb::Struct& st, uint32_t key) {
    if (g.field(st, key)) return true;
    for (const qb::Item& it : g.expand(st))
        if (it.name == 0 && (it.value.type == qb::Type::Key || it.value.type == qb::Type::KeyPtr) && it.value.key == key) return true;
    return false;
}

class Atoms {
public:
    Atoms(Vm& vm, std::shared_ptr<int> difficulty) : vm_(vm), difficulty_(std::move(difficulty)) {}

    void add(uint32_t name, const qb::Value& list) {
        Graph gr;
        const Globals& g = vm_.globals();
        if (list.type != qb::Type::Array || !list.arr) return;
        std::unordered_map<uint32_t, size_t> index;
        for (const qb::Value& v : list.arr->values) {
            if (v.type != qb::Type::Struct || !v.st) continue;
            Atom a;
            if (const qb::Value* n = g.field(*v.st, K("name"))) a.name = n->key;
            if (const qb::Value* t = g.field(*v.st, K("Type")); t && t->key != K("scr"))
                vm_.logOnce("atoms: atom type " + qbkey::name(t->key) + " run as scr");
            if (const qb::Value* s = g.field(*v.st, K("atom_script"))) a.script = s->key;
            if (const qb::Value* p = g.field(*v.st, K("atom_params")); p && p->st) a.params = resolved(*p->st);
            // A retail disc: cd_only atoms run, tool_only ones complete
            // without their script [C 0x8239E960; the CD build].
            a.toolOnly = hasFlag(g, *v.st, K("tool_only"));
            index[a.name] = gr.atoms.size();
            gr.atoms.push_back(std::move(a));
        }
        size_t at = 0;
        for (const qb::Value& v : list.arr->values) {
            if (v.type != qb::Type::Struct || !v.st) continue;
            Atom& a = gr.atoms[at];
            if (const qb::Value* d = g.field(*v.st, K("depends_on")); d && d->arr) {
                for (const qb::Value& dv : d->arr->values) {
                    if (dv.type != qb::Type::Struct || !dv.st) continue;
                    Dep dep;
                    const qb::Value* t = g.field(*dv.st, K("Type"));
                    dep.isAtom = t && t->key == K("atom");
                    if (const qb::Value* goal = g.field(*dv.st, K("goal_type")); goal && goal->key != K("all"))
                        vm_.logOnce("atoms: goal " + qbkey::name(goal->key) + " read as all");
                    if (dep.isAtom) {
                        auto link = [&](uint32_t k) {
                            auto it = index.find(k);
                            if (it == index.end()) {
                                vm_.logOnce("atoms: no atom " + qbkey::name(k));
                                return;
                            }
                            dep.atoms.push_back(it->second);
                            std::vector<size_t>& w = gr.atoms[it->second].waiting;
                            if (w.empty() || w.back() != at) w.push_back(at);
                        };
                        if (const qb::Value* x = g.field(*dv.st, K("atom"))) {
                            if (x->arr)
                                for (const qb::Value& e : x->arr->values) link(e.key);
                            else
                                link(x->key);
                        }
                    } else {
                        if (t && t->key != K("scr")) vm_.logOnce("atoms: condition type " + qbkey::name(t->key) + " run as scr");
                        if (const qb::Value* s = g.field(*dv.st, K("scr"))) dep.scr = s->key;
                        if (const qb::Value* p = g.field(*dv.st, K("params")); p && p->st) dep.params = resolved(*p->st);
                        if (const qb::Value* p = g.field(*dv.st, K("permanent"))) dep.permanent = Vm::number(p, 1) != 0;
                    }
                    if (const qb::Value* r = g.field(*dv.st, K("required")); r && r->arr)
                        for (const qb::Value& e : r->arr->values) dep.required.push_back(int(e.number()));
                    a.deps.push_back(std::move(dep));
                }
            }
            ++at;
        }
        graphs_[name] = std::move(gr);
    }

    void clear() { graphs_.clear(); }

    // UpdateAtoms: every atom in the list's order; one still running stops
    // the pass (the next update goes on from it) [C 0x822B0978].
    void update(uint32_t name, ScriptRun& run) {
        auto it = graphs_.find(name);
        if (it == graphs_.end() || it->second.updating) return;
        Graph& gr = it->second;
        gr.updating = true;
        for (size_t i = 0; i < gr.atoms.size(); ++i) {
            step(gr, i, run, 0);
            if (gr.atoms[i].state == State::Active) break;
        }
        gr.updating = false;
    }

private:
    // KeyPtr values (`numsongstoprogress = <GH3_Career_NumSongToProgress>`)
    // passed as the global they name.
    qb::Struct resolved(const qb::Struct& st) const {
        qb::Struct out = st;
        for (qb::Item& it : out.items)
            if (it.name != 0 && it.value.type == qb::Type::KeyPtr)
                if (const qb::Value* v = vm_.globals().resolve(it.value)) it.value = *v;
        return out;
    }

    bool running(uint32_t id) {
        if (!id) return false;
        for (const auto& r : vm_.runs())
            if (r->id == id && !r->finished) return true;
        return false;
    }

    // A condition met: not required at this difficulty, or satisfied
    // [C 0x8239E740].
    bool met(const Dep& d) const {
        const int diff = *difficulty_;
        if (!d.required.empty() && diff >= 0 && size_t(diff) < d.required.size() && d.required[size_t(diff)] == 0) return true;
        return d.satisfied;
    }

    // Every condition, in order, up to the first unmet one [C 0x8239EBE0];
    // a permanent condition already met is not asked again [C 0x82422B18].
    bool conditions(Graph& gr, Atom& a, ScriptRun& run) {
        for (Dep& d : a.deps) {
            if (!(d.permanent && !d.isAtom && met(d))) {
                if (d.isAtom) {
                    d.satisfied = true;
                    for (size_t k : d.atoms)
                        if (gr.atoms[k].state != State::Complete) d.satisfied = false;
                } else {
                    qb::Struct p = d.params;
                    d.satisfied = d.scr && vm_.condition(run, d.scr, p);
                }
            }
            if (!met(d)) return false;
        }
        return true;
    }

    // One atom's update [C 0x8239F040].
    void step(Graph& gr, size_t i, ScriptRun& run, int depth) {
        if (depth > 64) return;
        Atom& a = gr.atoms[i];
        if (a.state == State::Complete || a.state == State::Disabled) return;
        if (a.state == State::Locked && conditions(gr, a, run)) {
            if (a.toolOnly) {
                complete(gr, i, run, depth);
            } else {
                // Active: the atoms waiting on it are disabled until it is
                // complete, then its script runs now [C 0x8239EDB8, 0x82422D08].
                a.state = State::Active;
                for (size_t w : a.waiting)
                    if (gr.atoms[w].state != State::Complete) gr.atoms[w].state = State::Disabled;
                if (const char* v = SDL_getenv("GD04_UILOG"); v && *v && *v != '0')
                    SDL_Log("atoms: %s runs %s", qbkey::name(a.name).c_str(), qbkey::name(a.script).c_str());
                a.run = vm_.spawn(a.script, a.params);
                if (a.run) vm_.runNow(a.run);
            }
        }
        if (gr.atoms[i].state == State::Active && !running(gr.atoms[i].run)) complete(gr, i, run, depth);
    }

    // Complete: the atoms it disabled are locked again and updated at once
    // [C 0x8239E8F8, 0x8239F158, 0x82422BB8].
    void complete(Graph& gr, size_t i, ScriptRun& run, int depth) {
        gr.atoms[i].state = State::Complete;
        gr.atoms[i].run = 0;
        const std::vector<size_t> waiting = gr.atoms[i].waiting;
        for (size_t w : waiting) {
            if (gr.atoms[w].state != State::Disabled) continue;
            gr.atoms[w].state = State::Locked;
            for (Dep& d : gr.atoms[w].deps) d.satisfied = false;
            step(gr, w, run, depth + 1);
        }
    }

    Vm& vm_;
    std::shared_ptr<int> difficulty_;
    std::unordered_map<uint32_t, Graph> graphs_;
};

}  // namespace

void addAtomNatives(qs::Vm& vm, std::shared_ptr<int> difficulty) {
    auto atoms = std::make_shared<Atoms>(vm, std::move(difficulty));
    // RegisterAtoms name = Progression $GH3_Career_Progression: the list is
    // the call's nameless value (a list, or the global it names) [C scripts
    // career_select_difficulty_flow_state_func, coop_...].
    vm.addNative("RegisterAtoms", [atoms](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* n = Vm::param(p, K("name"));
        if (!n || n->key != K("Progression")) return true;  // achievement: not run (GD04-013)
        for (const qb::Item& it : p.items) {
            if (it.name != 0) continue;
            const qb::Value* list = &it.value;
            if (list->type == qb::Type::Key || list->type == qb::Type::KeyPtr) list = v.global(list->key);
            if (list && list->type == qb::Type::Array) {
                atoms->add(n->key, *list);
                return true;
            }
        }
        v.logOnce("atoms: RegisterAtoms Progression without a list");
        return true;
    });
    vm.addNative("DeRegisterAtoms", [atoms](Vm&, ScriptRun&, qb::Struct&) {
        atoms->clear();
        return true;
    });
    vm.addNative("UpdateAtoms", [atoms](Vm&, ScriptRun& run, qb::Struct& p) {
        if (const qb::Value* n = Vm::param(p, K("name"))) atoms->update(n->key, run);
        return true;
    });
}
