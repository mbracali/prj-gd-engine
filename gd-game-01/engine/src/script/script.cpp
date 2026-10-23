// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "script/script.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

#include "data/game_data.h"

// ---------------------------------------------------------------- loading

namespace {

// "a/b/" + "../c/x.dta" -> "a/c/x.dta"; leading ".." stay (archive paths
// like ../../system/run/config/gen/macros.dtb).
std::string joinPath(const std::string& dir, const std::string& rel) {
    std::vector<std::string> parts;
    auto push = [&](const std::string& s) {
        size_t start = 0;
        while (start <= s.size()) {
            size_t slash = s.find('/', start);
            std::string p = s.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (p == "..") {
                if (!parts.empty() && parts.back() != "..") parts.pop_back();
                else parts.push_back("..");
            } else if (!p.empty() && p != ".") {
                parts.push_back(p);
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
    };
    push(dir);
    push(rel);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) out += (i ? "/" : "") + parts[i];
    return out;
}

std::string dirOf(const std::string& path) {
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

}  // namespace

DataNode ScriptLoader::load(const std::string& dtaPath) {
    DataNode root = parseDtb(data_.read(dtbPath(dtaPath)));
    DataNode out;
    out.type = DataNode::Type::Array;
    preprocess(root, out, dirOf(dtaPath), 0);
    return out;
}

void ScriptLoader::preprocess(const DataNode& in, DataNode& out, const std::string& dir, int depth) {
    using T = DataNode::Type;
    if (depth > 32) return;
    std::vector<bool> active;  // #ifdef nesting: whether each level is taken
    auto on = [&] { return std::all_of(active.begin(), active.end(), [](bool b) { return b; }); };
    const auto& ch = in.children;
    for (size_t i = 0; i < ch.size(); ++i) {
        const DataNode& n = ch[i];
        switch (n.type) {
        case T::IfDef: active.push_back(macros.count(n.s) != 0); continue;
        case T::IfNDef: active.push_back(macros.count(n.s) == 0); continue;
        case T::Else: if (!active.empty()) active.back() = !active.back(); continue;
        case T::EndIf: if (!active.empty()) active.pop_back(); continue;
        default: break;
        }
        if (!on()) {
            if (n.type == T::Define) ++i;  // skip its value too
            continue;
        }
        if (n.type == T::Define) {
            DataNode value;
            value.type = T::Array;
            if (i + 1 < ch.size()) {
                DataNode raw = ch[++i];
                preprocess(raw, value, dir, depth + 1);
            }
            macros[n.s] = std::move(value);
            continue;
        }
        if (n.type == T::Include || n.type == T::Merge) {
            std::string path = joinPath(dir, n.s);
            try {
                DataNode inc = parseDtb(data_.read(dtbPath(path)));
                preprocess(inc, out, dirOf(path), depth + 1);
            } catch (const std::exception& ex) {
                SDL_Log("script: cannot include %s: %s", path.c_str(), ex.what());
            }
            continue;
        }
        if (n.type == T::Symbol) {  // macro use: splice its elements
            if (auto it = macros.find(n.s); it != macros.end()) {
                for (const DataNode& c : it->second.children) out.children.push_back(c);
                continue;
            }
        }
        if (n.isList()) {
            DataNode copy;
            copy.type = n.type;
            preprocess(n, copy, dir, depth + 1);
            out.children.push_back(std::move(copy));
        } else {
            out.children.push_back(n);
        }
    }
}

// ---------------------------------------------------------------- values

bool Value::truthy() const {
    switch (type) {
    case Type::Int: return i != 0;
    case Type::Float: return f != 0;
    case Type::Symbol: return !s.empty() && s != "FALSE";
    case Type::String: return true;
    case Type::Array: return true;
    case Type::Object: return object != nullptr;
    case Type::Code: return true;
    case Type::Null: return false;
    }
    return false;
}

std::string Value::str() const {
    switch (type) {
    case Type::Int: return std::to_string(i);
    case Type::Float: {
        char b[32];
        std::snprintf(b, sizeof b, "%g", f);
        return b;
    }
    case Type::Symbol:
    case Type::String: return s;
    case Type::Object: return object ? object->name() : "";
    case Type::Array: {
        std::string out = "(";
        for (size_t k = 0; k < array->size(); ++k) out += (k ? " " : "") + (*array)[k].str();
        return out + ")";
    }
    case Type::Code: return "{...}";
    case Type::Null: return "";
    }
    return "";
}

bool Value::operator==(const Value& o) const {
    if (isNumber() && o.isNumber()) return number() == o.number();
    if (isText() && o.isText()) return s == o.s;
    if (type == Type::Object && o.type == Type::Object) return object == o.object;
    if (type == Type::Object && o.isText()) return object && object->name() == o.s;
    if (isText() && o.type == Type::Object) return o.object && o.object->name() == s;
    if (type == Type::Array && o.type == Type::Array) return *array == *o.array;
    if (type == Type::Null || o.type == Type::Null) return type == o.type || (o.isNumber() && o.number() == 0) ||
                                                          (isNumber() && number() == 0);
    return false;
}

// ---------------------------------------------------------------- objects

void ScriptObject::addHandlers(const DataNode& list, size_t first) {
    for (size_t k = first; k < list.children.size(); ++k) {
        const DataNode& h = list.children[k];
        if (h.isList() && !h.children.empty() && h.children[0].isText()) handlers_.insert(handlers_.begin(), &h);
    }
}

const DataNode* ScriptObject::handler(const std::string& message) const {
    for (const DataNode* h : handlers_)
        if (h->children[0].s == message) return h;
    return nullptr;
}

// ---------------------------------------------------------------- interpreter

Interp::Interp() { installCoreBuiltins(); }

void Interp::registerObject(ScriptObject* o) { objects_[o->name()] = o; }
void Interp::unregisterObject(ScriptObject* o) {
    if (auto it = objects_.find(o->name()); it != objects_.end() && it->second == o) objects_.erase(it);
}
ScriptObject* Interp::findObject(const std::string& name) const {
    auto it = objects_.find(name);
    if (it != objects_.end()) return it->second;
    if (size_t c = name.find("::"); c != std::string::npos) {  // namespaces: try the plain name
        it = objects_.find(name.substr(c + 2));
        if (it != objects_.end()) return it->second;
        if (resolver) return resolver(name.substr(c + 2));
    }
    return resolver ? resolver(name) : nullptr;
}

void Interp::run(const DataNode& script) {
    for (const DataNode& n : script.children)
        if (n.type == DataNode::Type::Command) eval(n);
}

Value Interp::toValue(const DataNode& n) {
    using T = DataNode::Type;
    switch (n.type) {
    case T::Int: return Value::integer(n.i);
    case T::Float: return Value::real(n.f);
    case T::Symbol: return Value::symbol(n.s);
    case T::String: return Value::string(n.s);
    case T::Array: {
        std::vector<Value> v;
        v.reserve(n.children.size());
        for (const DataNode& c : n.children) v.push_back(toValue(c));
        return Value::list(std::move(v));
    }
    case T::Unhandled: return Value::symbol("kDataUnhandled");  // DTB node type 6
    case T::Command:
    case T::Property:
    case T::Var: {
        Value v;
        v.type = Value::Type::Code;
        v.code = &n;
        return v;
    }
    default: return Value::null();
    }
}

Value Interp::evalValue(const Value& v) { return v.type == Value::Type::Code ? eval(*v.code) : v; }

Value Interp::eval(const DataNode& n) {
    using T = DataNode::Type;
    switch (n.type) {
    case T::Var: {
        std::string name = !n.s.empty() && n.s[0] == '$' ? n.s.substr(1) : n.s;
        auto it = vars_.find(name);
        return it == vars_.end() ? Value::integer(0) : it->second;
    }
    case T::Command: return command(n);
    case T::Property: {  // [prop] of $this
        ScriptObject* self = vars_["this"].object;
        if (!self || n.children.empty()) return Value::integer(0);
        auto it = self->props.find(n.children[0].s);
        return it == self->props.end() ? Value::integer(0) : it->second;
    }
    default: return toValue(n);
    }
}

Value Interp::runBody(const std::vector<DataNode>& nodes, size_t first) {
    Value last = Value::integer(0);
    for (size_t k = first; k < nodes.size(); ++k) last = eval(nodes[k]);
    return last;
}

Value Interp::callHandler(ScriptObject* self, const DataNode& h, std::vector<Value>& args) {
    // (message [($a $b)] body...)
    size_t body = 1;
    std::vector<std::pair<std::string, Value>> saved;
    saved.push_back({"this", vars_["this"]});
    vars_["this"] = Value::obj(self);
    if (h.children.size() > 1 && h.children[1].type == DataNode::Type::Array &&
        std::all_of(h.children[1].children.begin(), h.children[1].children.end(),
                    [](const DataNode& c) { return c.type == DataNode::Type::Var; })) {
        const auto& params = h.children[1].children;
        for (size_t k = 0; k < params.size(); ++k) {
            std::string name = params[k].s[0] == '$' ? params[k].s.substr(1) : params[k].s;
            saved.push_back({name, vars_[name]});
            vars_[name] = k < args.size() ? args[k] : Value::integer(0);
        }
        body = 2;
    }
    Value result = runBody(h.children, body);
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) vars_[it->first] = it->second;
    return result;
}

Value Interp::send(ScriptObject* o, const std::string& message, std::vector<Value> args) {
    if (!o) return Value::integer(0);
    if (++depth_ > 200) {
        --depth_;
        return Value::integer(0);
    }
    Value result = Value::integer(0);
    if (const DataNode* h = o->handler(message)) {
        result = callHandler(o, *h, args);
    } else if (o->handleNative(*this, message, args, result)) {
    } else if (message == "get" && !args.empty()) {
        auto it = o->props.find(args[0].str());
        result = it == o->props.end() ? Value::integer(0) : it->second;
    } else if (message == "set" && args.size() >= 2) {
        o->props[args[0].str()] = args[1];
    } else if (message == "has" && !args.empty()) {
        result = Value::integer(o->props.count(args[0].str()) ? 1 : 0);
    } else if (message == "name") {
        result = Value::symbol(o->name());
    } else {
        std::string key = o->name() + "." + message;
        if (warned_[key]++ == 0) SDL_Log("script: %s does not handle %s", o->name().c_str(), message.c_str());
    }
    --depth_;
    return result;
}

Value Interp::send(const std::string& object, const std::string& message, std::vector<Value> args) {
    return send(findObject(object), message, std::move(args));
}

Value Interp::callFunc(const std::string& name, std::vector<Value> args) {
    auto it = funcs_.find(name);
    if (it == funcs_.end()) return Value::integer(0);
    const DataNode& f = *it->second;  // {func name [($params)] body...}
    size_t body = 2;
    std::vector<std::pair<std::string, Value>> saved;
    if (f.children.size() > 2 && f.children[2].type == DataNode::Type::Array) {
        const auto& params = f.children[2].children;
        for (size_t k = 0; k < params.size(); ++k) {
            if (params[k].type != DataNode::Type::Var) continue;
            std::string p = params[k].s[0] == '$' ? params[k].s.substr(1) : params[k].s;
            saved.push_back({p, vars_[p]});
            vars_[p] = k < args.size() ? args[k] : Value::integer(0);
        }
        body = 3;
    }
    if (++depth_ > 200) {
        --depth_;
        return Value::integer(0);
    }
    Value r = runBody(f.children, body);
    --depth_;
    for (auto s = saved.rbegin(); s != saved.rend(); ++s) vars_[s->first] = s->second;
    return r;
}

Value Interp::command(const DataNode& cmd) {
    const auto& ch = cmd.children;
    if (ch.empty()) return Value::integer(0);
    const DataNode& head = ch[0];
    ScriptObject* target = nullptr;
    if (head.type == DataNode::Type::Symbol || head.type == DataNode::Type::String) {
        if (head.type == DataNode::Type::Symbol) {
            if (auto b = builtins_.find(head.s); b != builtins_.end()) return b->second(*this, cmd);
            if (funcs_.count(head.s)) {
                std::vector<Value> args;
                for (size_t k = 1; k < ch.size(); ++k) args.push_back(eval(ch[k]));
                return callFunc(head.s, std::move(args));
            }
        }
        target = findObject(head.s);
        if (!target) {
            if (warned_[head.s]++ == 0) SDL_Log("script: unknown object or function '%s'", head.s.c_str());
            return Value::integer(0);
        }
    } else {
        Value h = eval(head);
        if (h.type == Value::Type::Object) target = h.object;
        else if (h.isText()) target = findObject(h.s);
        if (!target) return Value::integer(0);
    }
    if (ch.size() < 2) return Value::obj(target);
    Value msg = eval(ch[1]);
    bool lazy = std::find(lazyMessages.begin(), lazyMessages.end(), msg.str()) != lazyMessages.end();
    // foreach_player $p {...} / interp_task ms $var from to {...} bind a variable by name.
    static const std::vector<std::string> kByName = {"foreach_player", "foreach_player_matcher", "interp_task", "foreach_venue",
                                                     "finish_song", "next_cash_award", "get_highscore", "wait_timeout"};
    bool byName = std::find(kByName.begin(), kByName.end(), msg.str()) != kByName.end();
    std::vector<Value> args;
    for (size_t k = 2; k < ch.size(); ++k) {
        if (byName && ch[k].type == DataNode::Type::Var) args.push_back(Value::symbol(ch[k].s));
        else args.push_back(lazy && ch[k].type == DataNode::Type::Command ? toValue(ch[k]) : eval(ch[k]));
    }
    return send(target, msg.str(), std::move(args));
}

// ---------------------------------------------------------------- builtins

namespace {

std::mt19937& rng() {
    static std::mt19937 r{1234};
    return r;
}

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
    return real ? Value::real(r) : Value::integer(static_cast<int>(r));
}

// printf-style formatting with %s %d %i %f %x and widths.
std::string format(const std::string& fmt, const std::vector<Value>& args) {
    std::string out;
    size_t a = 0;
    for (size_t k = 0; k < fmt.size(); ++k) {
        if (fmt[k] != '%' || k + 1 >= fmt.size()) {
            out += fmt[k];
            continue;
        }
        size_t end = k + 1;
        while (end < fmt.size() && std::string("0123456789.-+ ").find(fmt[end]) != std::string::npos) ++end;
        if (end >= fmt.size()) break;
        char conv = fmt[end];
        std::string spec = fmt.substr(k, end - k + 1);
        char buf[256];
        Value v = a < args.size() ? args[a] : Value::integer(0);
        switch (conv) {
        case '%': out += '%'; k = end; continue;
        case 'd':
        case 'i':
        case 'x':
        case 'X': std::snprintf(buf, sizeof buf, spec.c_str(), v.integerValue()); ++a; break;
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

const std::vector<Value>* listOf(const Value& v) { return v.type == Value::Type::Array ? v.array.get() : nullptr; }

}  // namespace

void Interp::installCoreBuiltins() {
    auto evalArgs = [](Interp& in, const DataNode& c, size_t from = 1) {
        std::vector<Value> v;
        for (size_t k = from; k < c.children.size(); ++k) v.push_back(in.eval(c.children[k]));
        return v;
    };
    auto arg = [](Interp& in, const DataNode& c, size_t k) {
        return k < c.children.size() ? in.eval(c.children[k]) : Value::integer(0);
    };

    // -- variables and definitions
    addBuiltin("set", [arg](Interp& in, const DataNode& c) {
        if (c.children.size() < 3) return Value::integer(0);
        Value v = arg(in, c, 2);
        const DataNode& t = c.children[1];
        if (t.type == DataNode::Type::Var) in.vars_[varName(t)] = v;
        else if (t.type == DataNode::Type::Property && !t.children.empty()) {
            if (ScriptObject* self = in.vars_["this"].object) self->props[t.children[0].s] = v;
        }
        return v;
    });
    addBuiltin("func", [](Interp& in, const DataNode& c) {
        if (c.children.size() > 1 && c.children[1].isText()) in.funcs_[c.children[1].s] = &c;
        return Value::integer(0);
    });
    addBuiltin("delete", [](Interp& in, const DataNode& c) {
        if (c.children.size() > 1 && c.children[1].isText()) in.funcs_.erase(c.children[1].s);
        return Value::integer(0);
    });
    for (const char* op : {"++", "--", "+=", "-=", "*=", "/="}) {
        std::string o = op;
        addBuiltin(o, [o, arg](Interp& in, const DataNode& c) {
            if (c.children.size() < 2 || c.children[1].type != DataNode::Type::Var) return Value::integer(0);
            Value& v = in.vars_[varName(c.children[1])];
            Value d = o == "++" || o == "--" ? Value::integer(1) : arg(in, c, 2);
            v = arith(v, d, o[0] == '+' ? '+' : o[0] == '-' ? '-' : o[0] == '*' ? '*' : '/');
            return v;
        });
    }

    // -- control flow
    // FUN_002313a0: a true `if` runs its body and returns 0; a false one
    // returns kDataUnhandled, so a handler ending in a false `if` passes the
    // message on (e.g. the pause screen's BUTTON_DOWN_MSG).
    addBuiltin("if", [](Interp& in, const DataNode& c) {
        if (c.children.size() > 1 && in.eval(c.children[1]).truthy()) {
            in.runBody(c.children, 2);
            return Value::integer(0);
        }
        return Value::symbol("kDataUnhandled");
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
        size_t body = 1;
        std::vector<std::pair<std::string, Value>> saved;
        if (c.children.size() > 1 && c.children[1].type == DataNode::Type::Array) {
            // (($a init) ($b)) locals
            bool decls = true;
            for (const DataNode& d : c.children[1].children)
                if (!(d.type == DataNode::Type::Array && !d.children.empty() && d.children[0].type == DataNode::Type::Var) &&
                    d.type != DataNode::Type::Var)
                    decls = false;
            if (decls) {
                for (const DataNode& d : c.children[1].children) {
                    const DataNode& v = d.type == DataNode::Type::Var ? d : d.children[0];
                    std::string name = varName(v);
                    saved.push_back({name, in.vars_[name]});
                    in.vars_[name] = d.type == DataNode::Type::Array && d.children.size() > 1 ? in.eval(d.children[1])
                                                                                               : Value::integer(0);
                }
                body = 2;
            }
        }
        Value r = in.runBody(c.children, body);
        for (auto s = saved.rbegin(); s != saved.rend(); ++s) in.vars_[s->first] = s->second;
        return r;
    });
    addBuiltin("switch", [](Interp& in, const DataNode& c) {
        if (c.children.size() < 2) return Value::integer(0);
        Value v = in.eval(c.children[1]);
        for (size_t k = 2; k < c.children.size(); ++k) {
            const DataNode& cs = c.children[k];
            if (cs.type != DataNode::Type::Array || cs.children.empty()) {
                return in.runBody(c.children, k);  // the default: every remaining command
            }
            const DataNode& key = cs.children[0];
            bool match = false;
            if (key.type == DataNode::Type::Array) {
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
            if (cs.type == DataNode::Type::Array && !cs.children.empty()) {
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
        if (c.children.size() < 3 || c.children[1].type != DataNode::Type::Var) return Value::integer(0);
        std::string name = varName(c.children[1]);
        Value list = in.eval(c.children[2]);
        Value saved = in.vars_[name];
        if (const auto* l = listOf(list))
            for (const Value& e : *l) {
                in.vars_[name] = in.evalValue(e);
                in.runBody(c.children, 3);
            }
        in.vars_[name] = saved;
        return Value::integer(0);
    });
    addBuiltin("foreach_int", [](Interp& in, const DataNode& c) {
        if (c.children.size() < 4 || c.children[1].type != DataNode::Type::Var) return Value::integer(0);
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
        addBuiltin(std::string(1, op), [op, evalArgs](Interp& in, const DataNode& c) {
            std::vector<Value> a = evalArgs(in, c);
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
    addBuiltin("|", [evalArgs](Interp& in, const DataNode& c) {
        int r = 0;
        for (const Value& v : evalArgs(in, c)) r |= v.integerValue();
        return Value::integer(r);
    });
    addBuiltin("&", [evalArgs](Interp& in, const DataNode& c) {
        std::vector<Value> a = evalArgs(in, c);
        int r = a.empty() ? 0 : a[0].integerValue();
        for (size_t k = 1; k < a.size(); ++k) r &= a[k].integerValue();
        return Value::integer(r);
    });
    addBuiltin("max", [evalArgs](Interp& in, const DataNode& c) {
        std::vector<Value> a = evalArgs(in, c);
        Value r = a.empty() ? Value::integer(0) : a[0];
        for (const Value& v : a)
            if (v.number() > r.number()) r = v;
        return r;
    });
    addBuiltin("min", [evalArgs](Interp& in, const DataNode& c) {
        std::vector<Value> a = evalArgs(in, c);
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
    addBuiltin("random_int", [arg](Interp& in, const DataNode& c) {
        int lo = arg(in, c, 1).integerValue(), hi = arg(in, c, 2).integerValue();
        if (hi <= lo) return Value::integer(lo);
        return Value::integer(std::uniform_int_distribution<int>(lo, hi - 1)(rng()));
    });
    addBuiltin("random_float", [](Interp&, const DataNode&) {
        return Value::real(std::uniform_real_distribution<float>(0, 1)(rng()));
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
    addBuiltin("random_elem", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        if (const auto* l = listOf(v); l && !l->empty())
            return (*l)[std::uniform_int_distribution<size_t>(0, l->size() - 1)(rng())];
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
        return ok ? v : Value::symbol("kDataUnhandled");
    });
    addBuiltin("push_back", [arg](Interp& in, const DataNode& c) {
        if (c.children.size() < 3 || c.children[1].type != DataNode::Type::Var) return Value::integer(0);
        Value& l = in.vars_[varName(c.children[1])];
        if (l.type != Value::Type::Array) l = Value::list();
        l.array->push_back(arg(in, c, 2));
        return l;
    });
    // {resize $array n}: shrinks or pads (with 0) the array variable.
    addBuiltin("resize", [arg](Interp& in, const DataNode& c) {
        if (c.children.size() < 3 || c.children[1].type != DataNode::Type::Var) return Value::integer(0);
        Value& l = in.vars_[varName(c.children[1])];
        if (l.type != Value::Type::Array) l = Value::list();
        l.array->resize(size_t(std::max(0, arg(in, c, 2).integerValue())), Value::integer(0));
        return l;
    });
    addBuiltin("sprintf", [evalArgs](Interp& in, const DataNode& c) {
        std::vector<Value> a = evalArgs(in, c);
        if (a.empty()) return Value::string("");
        std::string f = a[0].str();
        a.erase(a.begin());
        return Value::string(format(f, a));
    });
    addBuiltin("sprint", [evalArgs](Interp& in, const DataNode& c) {
        std::string s;
        for (const Value& v : evalArgs(in, c)) s += v.str();
        return Value::string(s);
    });
    addBuiltin("print", [evalArgs](Interp& in, const DataNode& c) {
        std::string s;
        for (const Value& v : evalArgs(in, c)) s += v.str();
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
    addBuiltin("exists", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        if (v.type == Value::Type::Object) return Value::integer(v.object ? 1 : 0);
        return Value::integer(in.findObject(v.str()) || in.hasFunc(v.str()) ? 1 : 0);
    });
    addBuiltin("object", [arg](Interp& in, const DataNode& c) {
        Value v = arg(in, c, 1);
        return v.type == Value::Type::Object ? v : Value::obj(in.findObject(v.str()));
    });
}
