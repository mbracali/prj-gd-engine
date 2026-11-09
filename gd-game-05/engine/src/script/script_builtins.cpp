// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The script language's built-in functions: gd01's set (installCoreBuiltins)
// and what GH2's scripts add (installGh2Builtins).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "script/script.h"
#include "script/tasks.h"

namespace {

using T = DataNode::Type;

std::string varName(const DataNode& n) { return !n.s.empty() && n.s[0] == '$' ? n.s.substr(1) : n.s; }

Value arith(const Value& a, const Value& b, char op) {
    bool real = a.type == Value::Type::Float || b.type == Value::Type::Float || op == '/';
    float x = a.number(), y = b.number();
    float r = 0;
    switch (op) {
    case '+': r = x + y; break;
    case '-': r = x - y; break;
    case '*': r = x * y; break;
    case '/': r = y != 0 ? x / y : 0; break;
    case '%': r = y != 0 ? std::fmod(x, y) : 0; break;
    }
    if (op == '/' && a.type == Value::Type::Int && b.type == Value::Type::Int && b.i != 0 && a.i % b.i == 0)
        return Value::integer(a.i / b.i);
    if (op == '%' && a.type == Value::Type::Int && b.type == Value::Type::Int)
        return Value::integer(b.i != 0 ? a.i % b.i : 0);
    return real ? Value::real(r) : Value::integer(static_cast<int>(r));
}

// printf-style formatting with %s %d %i %x %f %g %e, flags and widths.
std::string format(const std::string& fmt, const std::vector<Value>& args) {
    std::string out;
    size_t a = 0;
    for (size_t k = 0; k < fmt.size(); ++k) {
        if (fmt[k] != '%' || k + 1 >= fmt.size()) {
            out += fmt[k];
            continue;
        }
        size_t end = k + 1;
        while (end < fmt.size() && std::strchr("0123456789.-+ #", fmt[end])) ++end;
        if (end >= fmt.size()) break;
        char conv = fmt[end];
        std::string spec = fmt.substr(k, end - k + 1);
        char buf[512];
        Value v = a < args.size() ? args[a] : Value::integer(0);
        switch (conv) {
        case '%': out += '%'; k = end; continue;
        case 'd':
        case 'i':
        case 'x':
        case 'X':
        case 'c': std::snprintf(buf, sizeof buf, spec.c_str(), v.integerValue()); ++a; break;
        case 'f':
        case 'g':
        case 'e': std::snprintf(buf, sizeof buf, spec.c_str(), static_cast<double>(v.number())); ++a; break;
        default: std::snprintf(buf, sizeof buf, (spec.substr(0, spec.size() - 1) + "s").c_str(), v.str().c_str()); ++a;
        }
        out += buf;
        k = end;
    }
    return out;
}

std::vector<Value>* listOf(const Value& v) { return v.type == Value::Type::Array ? v.array.get() : nullptr; }

// The variable or [property] a command writes to.
struct Place {
    Interp& in;
    const DataNode& node;
    Value get() {
        if (node.type == T::Var) return in.var(varName(node));
        if (node.type == T::Property && !node.children.empty())
            if (ScriptObject* self = in.var("this").object) return self->getProp(in, node.children[0].isText() ? node.children[0].s : in.eval(node.children[0]).str());
        return Value::integer(0);
    }
    bool set(const Value& v) {
        if (node.type == T::Var) {
            in.var(varName(node)) = v;
            return true;
        }
        if (node.type == T::Property && !node.children.empty()) {
            if (ScriptObject* self = in.var("this").object)
                self->setProp(in, node.children[0].isText() ? node.children[0].s : in.eval(node.children[0]).str(), v);
            return true;
        }
        // {set {elem_var array index} value}
        if (node.type == T::Command && node.children.size() >= 3 && node.children[0].isText() && node.children[0].s == "elem_var") {
            Value arr = in.eval(node.children[1]);
            int k = in.eval(node.children[2]).integerValue();
            if (auto* l = listOf(arr); l && k >= 0 && size_t(k) < l->size()) (*l)[size_t(k)] = v;
            return true;
        }
        return false;
    }
};

}  // namespace

void Interp::installCoreBuiltins() {
    auto arg = [](Interp& in, const DataNode& c, size_t k) { return in.evalArg(c, k); };

    // -- variables and definitions
    addBuiltin("set", [arg](Interp& in, const DataNode& c) {
        if (c.children.size() < 3) return Value::integer(0);
        Value v = arg(in, c, 2);
        Place{in, c.children[1]}.set(v);
        return v;
    });
    addBuiltin("func", [](Interp& in, const DataNode& c) {
        if (c.children.size() > 1 && c.children[1].isText()) in.funcs_[c.children[1].s] = &c;
        return Value::integer(0);
    });
    // {delete x}: a task ends, an object made by `new` is destroyed, a func is forgotten.
    addBuiltin("delete", [](Interp& in, const DataNode& c) {
        if (c.children.size() < 2) return Value::integer(0);
        Value v = c.children[1].isText() ? Value::symbol(c.children[1].s) : in.eval(c.children[1]);
        ScriptObject* o = v.type == Value::Type::Object ? v.object : in.findObject(v.str());
        if (o) {
            if (!in.tasks().kill(o)) in.destroyObject(o);
        } else if (v.isText()) {
            in.funcs_.erase(v.s);
        }
        return Value::integer(0);
    });
    for (const char* op : {"++", "--", "+=", "-=", "*=", "/="}) {
        std::string o = op;
        addBuiltin(o, [o, arg](Interp& in, const DataNode& c) {
            if (c.children.size() < 2) return Value::integer(0);
            Place p{in, c.children[1]};
            Value d = o == "++" || o == "--" ? Value::integer(1) : arg(in, c, 2);
            Value v = arith(p.get(), d, o[0] == '+' ? '+' : o[0] == '-' ? '-' : o[0] == '*' ? '*' : '/');
            p.set(v);
            return v;
        });
    }

    // -- control flow
    // A true `if` runs its body and returns 0; a false one returns
    // kDataUnhandled, so a handler ending in a false `if` passes the message
    // on (gd01, GH1's DataIf [C]; same in GH2 [I]).
    addBuiltin("if", [](Interp& in, const DataNode& c) {
        if (c.children.size() > 1 && in.eval(c.children[1]).truthy()) {
            in.runBody(c.children, 2);
            return Value::integer(0);
        }
        return Value::unhandled();
    });
    addBuiltin("unless", [](Interp& in, const DataNode& c) {
        if (c.children.size() > 1 && !in.eval(c.children[1]).truthy()) return in.runBody(c.children, 2);
        return Value::integer(0);
    });
    addBuiltin("if_else", [arg](Interp& in, const DataNode& c) {
        if (c.children.size() < 3) return Value::integer(0);
        return in.eval(c.children[1]).truthy() ? arg(in, c, 2) : arg(in, c, 3);
    });
    addBuiltin("do", [](Interp& in, const DataNode& c) {
        // {do [($a init) ($b) $c ...] body...}: leading arrays declare locals.
        size_t body = 1;
        std::vector<std::pair<std::string, Value>> saved;
        auto isDecl = [](const DataNode& d) {
            return d.type == T::Var || (d.type == T::Array && !d.children.empty() && d.children[0].type == T::Var && d.children.size() <= 2);
        };
        while (body < c.children.size() && isDecl(c.children[body]) && c.children[body].type == T::Array) {
            const DataNode& d = c.children[body];
            std::string name = varName(d.children[0]);
            Value init = d.children.size() > 1 ? in.eval(d.children[1]) : Value::integer(0);
            saved.push_back({name, in.vars_[name]});
            in.vars_[name] = init;
            ++body;
        }
        // gd01's form: one array holding all the declarations (($a 1) ($b)).
        if (body == 1 && c.children.size() > 1 && c.children[1].type == T::Array && !c.children[1].children.empty() &&
            std::all_of(c.children[1].children.begin(), c.children[1].children.end(),
                        [&](const DataNode& d) { return d.type == T::Var || (d.type == T::Array && isDecl(d)); })) {
            for (const DataNode& d : c.children[1].children) {
                const DataNode& v = d.type == T::Var ? d : d.children[0];
                std::string name = varName(v);
                Value init = d.type == T::Array && d.children.size() > 1 ? in.eval(d.children[1]) : Value::integer(0);
                saved.push_back({name, in.vars_[name]});
                in.vars_[name] = init;
            }
            body = 2;
        }
        Value r;
        try {
            r = in.runBody(c.children, body);
        } catch (...) {
            for (auto s = saved.rbegin(); s != saved.rend(); ++s) in.vars_[s->first] = s->second;
            throw;
        }
        for (auto s = saved.rbegin(); s != saved.rend(); ++s) in.vars_[s->first] = s->second;
        return r;
    });
    addBuiltin("switch", [](Interp& in, const DataNode& c) {
        if (c.children.size() < 2) return Value::integer(0);
        Value v = in.eval(c.children[1]);
        for (size_t k = 2; k < c.children.size(); ++k) {
            const DataNode& cs = c.children[k];
            if (cs.type != T::Array || cs.children.empty()) return in.runBody(c.children, k);  // the default
            const DataNode& key = cs.children[0];
            bool match = false;
            if (key.type == T::Array) {
                for (const DataNode& kk : key.children) match = match || in.eval(kk) == v;
            } else {
                match = in.eval(key) == v;
            }
            if (match) return in.runBody(cs.children, 1);
        }
        return Value::integer(0);
    });
    addBuiltin("cond", [](Interp& in, const DataNode& c) {
        for (size_t k = 1; k < c.children.size(); ++k) {
            const DataNode& cs = c.children[k];
            if (cs.type == T::Array && !cs.children.empty()) {
                if (in.eval(cs.children[0]).truthy()) return in.runBody(cs.children, 1);
            } else {
                return in.runBody(c.children, k);
            }
        }
        return Value::integer(0);
    });
    addBuiltin("while", [](Interp& in, const DataNode& c) {
        for (int guard = 0; guard < 100000 && c.children.size() > 1 && in.eval(c.children[1]).truthy(); ++guard)
            in.runBody(c.children, 2);
        return Value::integer(0);
    });
    addBuiltin("foreach", [](Interp& in, const DataNode& c) {
        if (c.children.size() < 3 || c.children[1].type != T::Var) return Value::integer(0);
        std::string name = varName(c.children[1]);
        Value list = in.eval(c.children[2]);
        Value saved = in.vars_[name];
        if (const auto* l = listOf(list)) {
            std::vector<Value> copy = *l;  // the body may change the list
            for (const Value& e : copy) {
                in.vars_[name] = in.evalValue(e);
                in.runBody(c.children, 3);
            }
        }
        in.vars_[name] = saved;
        return Value::integer(0);
    });
    addBuiltin("foreach_int", [](Interp& in, const DataNode& c) {
        if (c.children.size() < 4 || c.children[1].type != T::Var) return Value::integer(0);
        std::string name = varName(c.children[1]);
        int from = in.eval(c.children[2]).integerValue(), to = in.eval(c.children[3]).integerValue();
        Value saved = in.vars_[name];
        for (int k = from; k < to; ++k) {
            in.vars_[name] = Value::integer(k);
            in.runBody(c.children, 4);
        }
        in.vars_[name] = saved;
        return Value::integer(0);
    });
    addBuiltin("eval", [arg](Interp& in, const DataNode& c) { return in.evalValue(arg(in, c, 1)); });
    addBuiltin("with_namespace", [](Interp& in, const DataNode& c) { return in.runBody(c.children, 2); });

    // -- arithmetic and logic
    for (char op : std::string("+-*/%")) {
        addBuiltin(std::string(1, op), [op](Interp& in, const DataNode& c) {
            std::vector<Value> a = in.evalArgs(c);
            if (a.empty()) return Value::integer(0);
            if (op == '-' && a.size() == 1) return arith(Value::integer(0), a[0], '-');
            Value r = a[0];
            for (size_t k = 1; k < a.size(); ++k) r = arith(r, a[k], op);
            return r;
        });
    }
    addBuiltin("mod", [arg](Interp& in, const DataNode& c) { return arith(arg(in, c, 1), arg(in, c, 2), '%'); });
    auto compare = [arg](const char* name, auto fn) {
        return std::make_pair(std::string(name), Builtin([fn, arg](Interp& in, const DataNode& c) {
                                  return Value::integer(fn(arg(in, c, 1), arg(in, c, 2)) ? 1 : 0);
                              }));
    };
    for (auto [n, f] : {compare("==", [](const Value& a, const Value& b) { return a == b; }),
                        compare("!=", [](const Value& a, const Value& b) { return !(a == b); }),
                        compare("<", [](const Value& a, const Value& b) { return a.number() < b.number(); }),
                        compare(">", [](const Value& a, const Value& b) { return a.number() > b.number(); }),
                        compare("<=", [](const Value& a, const Value& b) { return a.number() <= b.number(); }),
                        compare(">=", [](const Value& a, const Value& b) { return a.number() >= b.number(); })})
        addBuiltin(n, f);
    addBuiltin("&&", [](Interp& in, const DataNode& c) {
        for (size_t k = 1; k < c.children.size(); ++k)
            if (!in.eval(c.children[k]).truthy()) return Value::integer(0);
        return Value::integer(1);
    });
    addBuiltin("||", [](Interp& in, const DataNode& c) {
        for (size_t k = 1; k < c.children.size(); ++k)
            if (in.eval(c.children[k]).truthy()) return Value::integer(1);
        return Value::integer(0);
    });
    addBuiltin("!", [arg](Interp& in, const DataNode& c) { return Value::integer(arg(in, c, 1).truthy() ? 0 : 1); });
    addBuiltin("|", [](Interp& in, const DataNode& c) {
        int r = 0;
        for (const Value& v : in.evalArgs(c)) r |= v.integerValue();
        return Value::integer(r);
    });
    addBuiltin("&", [](Interp& in, const DataNode& c) {
        std::vector<Value> a = in.evalArgs(c);
        int r = a.empty() ? 0 : a[0].integerValue();
        for (size_t k = 1; k < a.size(); ++k) r &= a[k].integerValue();
        return Value::integer(r);
    });
    addBuiltin("max", [](Interp& in, const DataNode& c) {
        std::vector<Value> a = in.evalArgs(c);
        Value r = a.empty() ? Value::integer(0) : a[0];
        for (const Value& v : a)
            if (v.number() > r.number()) r = v;
        return r;
    });
    addBuiltin("min", [](Interp& in, const DataNode& c) {
        std::vector<Value> a = in.evalArgs(c);
        Value r = a.empty() ? Value::integer(0) : a[0];
        for (const Value& v : a)
            if (v.number() < r.number()) r = v;
        return r;
    });
    addBuiltin("abs", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        return v.type == Value::Type::Float ? Value::real(std::fabs(v.f)) : Value::integer(std::abs(v.i));
    });
    addBuiltin("int", [arg](Interp& in, const DataNode& c) { return Value::integer(arg(in, c, 1).integerValue()); });
    addBuiltin("floor", [arg](Interp& in, const DataNode& c) { return Value::real(std::floor(arg(in, c, 1).number())); });
    addBuiltin("ceil", [arg](Interp& in, const DataNode& c) { return Value::real(std::ceil(arg(in, c, 1).number())); });
    // {random_int lo hi}: lo <= n < hi.
    addBuiltin("random_int", [arg](Interp& in, const DataNode& c) {
        return Value::integer(in.randomInt(arg(in, c, 1).integerValue(), arg(in, c, 2).integerValue()));
    });
    // {random_float lo hi}: lo <= x < hi; {random_float}: [0, 1) (gd01 ignored lo and hi).
    addBuiltin("random_float", [arg](Interp& in, const DataNode& c) {
        if (c.children.size() < 3) return Value::real(in.randomFloat(0, 1));
        return Value::real(in.randomFloat(arg(in, c, 1).number(), arg(in, c, 2).number()));
    });

    // -- arrays and strings
    addBuiltin("size", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        if (const auto* l = listOf(v)) return Value::integer(static_cast<int>(l->size()));
        return Value::integer(v.isText() ? static_cast<int>(v.s.size()) : 0);
    });
    addBuiltin("elem", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        int k = arg(in, c, 2).integerValue();
        if (const auto* l = listOf(v); l && k >= 0 && size_t(k) < l->size()) return (*l)[size_t(k)];
        return Value::integer(0);
    });
    addBuiltin("elem_var", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        int k = arg(in, c, 2).integerValue();
        if (const auto* l = listOf(v); l && k >= 0 && size_t(k) < l->size()) return (*l)[size_t(k)];
        return Value::integer(0);
    });
    addBuiltin("random_elem", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        if (const auto* l = listOf(v); l && !l->empty())
            return (*l)[size_t(in.randomInt(0, static_cast<int>(l->size())))];
        return Value::integer(0);
    });
    // {find array key1 key2 ...}: the sub-array whose first element matches, nested.
    auto findPath = [](Interp& in, const DataNode& c, bool& ok) {
        ok = false;
        if (c.children.size() < 2) return Value::integer(0);
        Value cur = in.eval(c.children[1]);
        for (size_t k = 2; k < c.children.size(); ++k) {
            Value key = in.eval(c.children[k]);
            const auto* l = listOf(cur);
            if (!l) return Value::integer(0);
            bool found = false;
            for (const Value& e : *l)
                if (const auto* sub = listOf(e); sub && !sub->empty() && (*sub)[0] == key) {
                    cur = e;
                    found = true;
                    break;
                }
            if (!found) return Value::integer(0);
        }
        ok = true;
        return cur;
    };
    addBuiltin("find", [findPath](Interp& in, const DataNode& c) {
        bool ok;
        return findPath(in, c, ok);
    });
    addBuiltin("find_exists", [findPath](Interp& in, const DataNode& c) {
        bool ok;
        Value v = findPath(in, c, ok);
        return ok ? v : Value::unhandled();
    });
    addBuiltin("push_back", [arg](Interp& in, const DataNode& c) {
        if (c.children.size() < 3) return Value::integer(0);
        Place p{in, c.children[1]};
        Value l = c.children[1].type == T::Var || c.children[1].type == T::Property ? p.get() : arg(in, c, 1);
        if (l.type != Value::Type::Array) {
            l = Value::list();
            p.set(l);
        }
        l.array->push_back(arg(in, c, 2));
        return l;
    });
    // {resize $array n}: shrinks or pads (with 0) the array.
    addBuiltin("resize", [arg](Interp& in, const DataNode& c) {
        if (c.children.size() < 3) return Value::integer(0);
        Place p{in, c.children[1]};
        Value l = c.children[1].type == T::Var || c.children[1].type == T::Property ? p.get() : arg(in, c, 1);
        if (l.type != Value::Type::Array) {
            l = Value::list();
            p.set(l);
        }
        l.array->resize(size_t(std::max(0, arg(in, c, 2).integerValue())), Value::integer(0));
        return l;
    });
    addBuiltin("sprintf", [](Interp& in, const DataNode& c) {
        std::vector<Value> a = in.evalArgs(c);
        if (a.empty()) return Value::string("");
        std::string f = a[0].str();
        a.erase(a.begin());
        return Value::string(format(f, a));
    });
    addBuiltin("sprint", [](Interp& in, const DataNode& c) {
        std::string s;
        for (const Value& v : in.evalArgs(c)) s += v.str();
        return Value::string(s);
    });
    addBuiltin("print", [](Interp& in, const DataNode& c) {
        std::string s;
        for (const Value& v : in.evalArgs(c)) s += v.str();
        while (!s.empty() && s.back() == '\n') s.pop_back();
        SDL_Log("script: %s", s.c_str());
        return Value::integer(0);
    });
    addBuiltin("localize", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        auto it = in.locale.find(v.str());
        return Value::string(it == in.locale.end() ? v.str() : it->second);
    });
    addBuiltin("symbol", [arg](Interp& in, const DataNode& c) { return Value::symbol(arg(in, c, 1).str()); });
    addBuiltin("str_elem", [arg](Interp& in, const DataNode& c) {
        std::string s = arg(in, c, 1).str();
        int k = arg(in, c, 2).integerValue();
        return Value::string(k >= 0 && size_t(k) < s.size() ? std::string(1, s[size_t(k)]) : "");
    });
    addBuiltin("strlen", [arg](Interp& in, const DataNode& c) {
        return Value::integer(static_cast<int>(arg(in, c, 1).str().size()));
    });

    // -- objects
    addBuiltin("exists", [](Interp& in, const DataNode& c) {
        if (c.children.size() < 2) return Value::integer(0);
        Value v = c.children[1].isText() ? Value::symbol(c.children[1].s) : in.eval(c.children[1]);
        if (v.type == Value::Type::Object) return Value::integer(v.object ? 1 : 0);
        return Value::integer(in.findObject(v.str()) || in.hasFunc(v.str()) ? 1 : 0);
    });
    addBuiltin("object", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        return v.type == Value::Type::Object ? v : Value::obj(in.findObject(v.str()));
    });
}

void Interp::installGh2Builtins() {
    auto arg = [](Interp& in, const DataNode& c, size_t k) { return in.evalArg(c, k); };

    addBuiltin("new", [](Interp& in, const DataNode& c) { return in.newObject(c); });
    // {handle ($obj msg args...)...}: sends each message; nobody answering is fine.
    addBuiltin("handle", [](Interp& in, const DataNode& c) {
        Value last = Value::integer(0);
        for (size_t k = 1; k < c.children.size(); ++k) {
            const DataNode& a = c.children[k];
            if (a.type != T::Array || a.children.size() < 2) continue;
            Value target = in.eval(a.children[0]);
            ScriptObject* o = target.type == Value::Type::Object ? target.object : in.findObject(target.str());
            if (!o) continue;
            std::string msg = in.eval(a.children[1]).str();
            last = in.send(o, msg, in.evalArgs(a, 2), false);
        }
        return last;
    });
    // {find_obj a b c}: object a, then its child b ({a find b}), then c.
    addBuiltin("find_obj", [](Interp& in, const DataNode& c) {
        ScriptObject* o = nullptr;
        for (size_t k = 1; k < c.children.size(); ++k) {
            Value v = in.eval(c.children[k]);
            if (k == 1) {
                o = v.type == Value::Type::Object ? v.object : in.findObject(v.str());
            } else {
                Value r = in.send(o, "find", {Value::symbol(v.str())}, false);
                o = r.type == Value::Type::Object ? r.object : nullptr;
            }
            if (!o) return Value::integer(0);
        }
        return Value::obj(o);
    });

    // -- arrays
    addBuiltin("array", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        if (const auto* l = listOf(v)) return Value::list(*l);  // a copy
        return Value::list(std::vector<Value>(size_t(std::max(0, v.integerValue())), Value::integer(0)));
    });
    addBuiltin("set_elem", [arg](Interp& in, const DataNode& c) {
        Value l = arg(in, c, 1), v = arg(in, c, 3);
        int k = arg(in, c, 2).integerValue();
        if (auto* a = listOf(l); a && k >= 0 && size_t(k) < a->size()) (*a)[size_t(k)] = v;
        return v;
    });
    addBuiltin("insert_elem", [arg](Interp& in, const DataNode& c) {
        Value l = arg(in, c, 1), v = arg(in, c, 3);
        int k = arg(in, c, 2).integerValue();
        if (auto* a = listOf(l); a && k >= 0 && size_t(k) <= a->size()) a->insert(a->begin() + k, v);
        return v;
    });
    // {remove_elem array value}: removes the first element equal to value.
    addBuiltin("remove_elem", [arg](Interp& in, const DataNode& c) {
        Value l = arg(in, c, 1), v = arg(in, c, 2);
        if (auto* a = listOf(l))
            if (auto it = std::find(a->begin(), a->end(), v); it != a->end()) a->erase(it);
        return Value::integer(0);
    });
    // {find_elem array value [$index]}: TRUE if present; $index = its position.
    addBuiltin("find_elem", [arg](Interp& in, const DataNode& c) {
        Value l = arg(in, c, 1), v = arg(in, c, 2);
        if (const auto* a = listOf(l))
            for (size_t k = 0; k < a->size(); ++k)
                if ((*a)[k] == v) {
                    if (c.children.size() > 3) Place{in, c.children[3]}.set(Value::integer(int(k)));
                    return Value::integer(1);
                }
        return Value::integer(0);
    });

    // -- strings
    addBuiltin("has_substr", [arg](Interp& in, const DataNode& c) {
        return Value::integer(arg(in, c, 1).str().find(arg(in, c, 2).str()) != std::string::npos ? 1 : 0);
    });
    // {strneq a b n}: the first n characters are equal (char_objects.dta's "drummer" meshes).
    addBuiltin("strneq", [arg](Interp& in, const DataNode& c) {
        std::string a = arg(in, c, 1).str(), b = arg(in, c, 2).str();
        int n = arg(in, c, 3).integerValue();
        return Value::integer(std::strncmp(a.c_str(), b.c_str(), size_t(std::max(0, n))) == 0 ? 1 : 0);
    });
    addBuiltin("printf", [](Interp& in, const DataNode& c) {
        std::vector<Value> a = in.evalArgs(c);
        if (a.empty()) return Value::integer(0);
        std::string f = a[0].str();
        a.erase(a.begin());
        std::string s = format(f, a);
        while (!s.empty() && s.back() == '\n') s.pop_back();
        SDL_Log("script: %s", s.c_str());
        return Value::integer(0);
    });
    addBuiltin("notify", [](Interp& in, const DataNode& c) {
        std::string s;
        for (const Value& v : in.evalArgs(c)) s += v.str();
        SDL_Log("script: notify: %s", s.c_str());
        return Value::integer(0);
    });
    // {fail msg...}: fatal in the game; logged and counted here.
    addBuiltin("fail", [](Interp& in, const DataNode& c) {
        std::string s;
        for (const Value& v : in.evalArgs(c)) s += v.str();
        in.onError("fail: " + s);
        return Value::integer(0);
    });

    // -- tasks
    addBuiltin("script_task", [](Interp& in, const DataNode& c) { return Value::obj(in.tasks().scriptTask(c)); });
    addBuiltin("thread_task", [](Interp& in, const DataNode& c) { return Value::obj(in.tasks().threadTask(c)); });

    // -- pure natives
    addBuiltin("system_language", [](Interp& in, const DataNode&) { return Value::symbol(in.language); });
    // {pack_color r g b}: 0x00BBGGRR (Hmx::Color pack) [I].
    addBuiltin("pack_color", [arg](Interp& in, const DataNode& c) {
        auto ch = [&](size_t k) { return std::clamp(static_cast<int>(arg(in, c, k).number() * 255.0f), 0, 255); };
        return Value::integer(ch(1) | (ch(2) << 8) | (ch(3) << 16));
    });
    // {option_str name $var}: a command-line option; the port has none.
    addBuiltin("option_str", [](Interp&, const DataNode&) { return Value::integer(0); });
    for (const char* noop : {"set_loader_period", "set_key_cheats_enabled", "spew", "heap_report", "heap_dump", "print_symbol_table"})
        addBuiltin(noop, [](Interp&, const DataNode&) { return Value::integer(0); });
}
