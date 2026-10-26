// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "script/script.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

#include "data/game_data.h"
#include "script/tasks.h"

// ---------------------------------------------------------------- loading

namespace {

// "a/b" + "../c/x.dta" -> "a/c/x.dta"; leading ".." stay (archive paths like
// ../../system/run/config/gen/macros.dtb). GH2's includes may use `\`.
std::string joinPath(const std::string& dir, std::string rel) {
    std::replace(rel.begin(), rel.end(), '\\', '/');
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

bool sameKey(const DataNode& a, const DataNode& b) {
    using T = DataNode::Type;
    if (a.isText() && b.isText()) return a.s == b.s;
    if (a.type != b.type) return false;
    switch (a.type) {
    case T::Int: return a.i == b.i;
    case T::Float: return a.f == b.f;
    case T::Var: return a.s == b.s;
    default: return false;
    }
}

}  // namespace

ScriptLoader::ScriptLoader(const GameData& data) : ScriptLoader(Reader([&data](const std::string& p) { return parseDtb(data.read(p)); })) {}

ScriptLoader::ScriptLoader(Reader reader) : reader_(std::move(reader)) {
    // GH2's 360 retail build [I] (the #ifdef blocks of gh2.dta and friends).
    for (const char* m : {"HX_XBOX", "_SHIP"}) {
        DataNode v;
        v.type = DataNode::Type::Array;
        v.children.push_back(DataNode{});
        v.children.back().i = 1;
        macros[m] = std::move(v);
    }
}

DataNode ScriptLoader::load(const std::string& dtaPath) {
    ++stats.files;
    return preprocessTree(reader_(dtbPath(dtaPath)), dtaPath);
}

DataNode ScriptLoader::preprocessTree(const DataNode& root, const std::string& dtaPath) {
    DataNode out;
    out.type = DataNode::Type::Array;
    preprocess(root, out, dirOf(dtaPath), 0);
    return out;
}

DataNode ScriptLoader::readFile(const std::string& path, int depth) {
    DataNode inc = reader_(dtbPath(path));
    DataNode out;
    out.type = DataNode::Type::Array;
    preprocess(inc, out, dirOf(path), depth + 1);
    return out;
}

void ScriptLoader::mergeArrays(DataNode& dst, const DataNode& src) {
    for (const DataNode& c : src.children) {
        if (c.type != DataNode::Type::Array || c.children.empty()) continue;
        DataNode* found = nullptr;
        for (DataNode& d : dst.children)
            if (d.type == DataNode::Type::Array && !d.children.empty() && sameKey(d.children[0], c.children[0])) {
                found = &d;
                break;
            }
        if (found) mergeArrays(*found, c);
        else dst.children.push_back(c);
    }
}

void ScriptLoader::preprocess(const DataNode& in, DataNode& out, const std::string& dir, int depth) {
    using T = DataNode::Type;
    if (depth > 32) {
        stats.errors.push_back("include depth > 32 in " + dir);
        return;
    }
    std::vector<bool> active;  // #ifdef nesting: whether each level is taken
    auto on = [&] { return std::all_of(active.begin(), active.end(), [](bool b) { return b; }); };
    const auto& ch = in.children;
    for (size_t i = 0; i < ch.size(); ++i) {
        const DataNode& n = ch[i];
        switch (n.type) {
        case T::IfDef: ++stats.conditionals; active.push_back(macros.count(n.s) != 0); continue;
        case T::IfNDef: ++stats.conditionals; active.push_back(macros.count(n.s) == 0); continue;
        case T::Else: if (!active.empty()) active.back() = !active.back(); continue;
        case T::EndIf: if (!active.empty()) active.pop_back(); continue;
        default: break;
        }
        if (!on()) {
            if (n.type == T::Define || n.type == T::Autorun) ++i;  // skip its value too
            continue;
        }
        switch (n.type) {
        case T::Define: {
            ++stats.defines;
            DataNode value;
            value.type = T::Array;
            if (i + 1 < ch.size()) {
                const DataNode& raw = ch[++i];
                if (raw.isList()) preprocess(raw, value, dir, depth + 1);
                else value.children.push_back(raw);
            }
            macros[n.s] = std::move(value);
            continue;
        }
        case T::Undef: ++stats.undefs; macros.erase(n.s); continue;
        case T::Autorun:
            // The format keeps a command after the node [I] (no GH2 file has one).
            ++stats.autoruns;
            if (i + 1 < ch.size() && ch[i + 1].type == T::Command) {
                DataNode cmd;
                cmd.type = T::Command;
                preprocess(ch[++i], cmd, dir, depth + 1);
                autoruns.push_back(std::move(cmd));
            }
            continue;
        case T::Include:
        case T::Merge: {
            std::string path = joinPath(dir, n.s);
            bool merge = n.type == T::Merge;
            ++(merge ? stats.merges : stats.includes);
            try {
                DataNode inc = readFile(path, depth);
                if (merge) mergeArrays(out, inc);
                else for (DataNode& c : inc.children) out.children.push_back(std::move(c));
            } catch (const std::exception& ex) {
                stats.errors.push_back(std::string(merge ? "#merge " : "#include ") + path + ": " + ex.what());
                SDL_Log("script: cannot %s %s: %s", merge ? "merge" : "include", path.c_str(), ex.what());
            }
            continue;
        }
        case T::Else:
        case T::EndIf:
        case T::IfDef:
        case T::IfNDef: continue;
        default: break;
        }
        if (n.type == T::Symbol) {  // macro use: splice its elements
            if (auto it = macros.find(n.s); it != macros.end()) {
                ++stats.macroUses;
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
    case Type::Symbol: return !s.empty() && s != "FALSE" && s != "kDataUnhandled";
    case Type::String: return true;
    case Type::Array: return true;
    case Type::Object: return object != nullptr;
    case Type::Code: return true;
    case Type::Null: return false;
    case Type::Unhandled: return false;
    }
    return false;
}

std::string Value::str() const {
    switch (type) {
    case Type::Int: return std::to_string(i);
    case Type::Float: {
        char b[32];
        std::snprintf(b, sizeof b, "%g", static_cast<double>(f));
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
    case Type::Code: return code && code->type == DataNode::Type::Var ? "$" + code->s : "{...}";
    case Type::Null: return "";
    case Type::Unhandled: return "kDataUnhandled";
    }
    return "";
}

bool Value::operator==(const Value& o) const {
    if (isUnhandled() || o.isUnhandled()) return isUnhandled() && o.isUnhandled();
    if (isNumber() && o.isNumber()) return number() == o.number();
    if (isText() && o.isText()) return s == o.s;
    if (type == Type::Object && o.type == Type::Object) return object == o.object;
    if (type == Type::Object && o.isText()) return object && object->name() == o.s;
    if (isText() && o.type == Type::Object) return o.object && o.object->name() == s;
    if (type == Type::Array && o.type == Type::Array) return *array == *o.array;
    if (type == Type::Null || o.type == Type::Null)
        return type == o.type || (o.isNumber() && o.number() == 0) || (isNumber() && number() == 0);
    // A missing object is 0 (Value::obj(nullptr) is the integer 0 already).
    return false;
}

// ---------------------------------------------------------------- objects

void ScriptObject::addHandlers(const DataNode& list, size_t first) {
    for (size_t k = first; k < list.children.size(); ++k) {
        const DataNode& h = list.children[k];
        if (h.type == DataNode::Type::Array && !h.children.empty() && h.children[0].isText())
            handlers_.insert(handlers_.begin(), &h);
    }
}

const DataNode* ScriptObject::handler(const std::string& message) const {
    for (const DataNode* h : handlers_)
        if (h->children[0].s == message) return h;
    return nullptr;
}

void ScriptObject::setTypeDef(const DataNode* def, size_t first) {
    typeDef_ = def;
    if (def) addHandlers(*def, first);
}

bool ScriptObject::hasProp(const std::string& key) const { return props.count(key) || handler(key); }

Value ScriptObject::getProp(Interp& in, const std::string& key) {
    if (auto it = props.find(key); it != props.end()) return it->second;
    // A definition's (key value) array: its value, evaluated when it is code
    // (ui_objects.dta's (normal_color {pack_color 1 0 0})); several values make a list.
    if (const DataNode* h = handler(key)) {
        if (h->children.size() == 2) {
            const DataNode& v = h->children[1];
            return v.type == DataNode::Type::Command ? in.eval(v) : in.toValue(v);
        }
        std::vector<Value> v;
        for (size_t k = 1; k < h->children.size(); ++k) v.push_back(in.toValue(h->children[k]));
        return Value::list(std::move(v));
    }
    return Value::integer(0);
}

void ScriptObject::setProp(Interp&, const std::string& key, const Value& v) { props[key] = v; }

bool StubObject::handleNative(Interp& in, const std::string& msg, std::vector<Value>&, Value& r) {
    if (msg == "get" || msg == "set" || msg == "has" || msg == "name") return false;  // the generic property messages
    r = Value::integer(0);
    in.onStub(name(), msg, r);
    return true;
}

// ---------------------------------------------------------------- interpreter

Interp::Interp() {
    const char* log = SDL_getenv("GD02_UILOG");
    logReport = log && *log && *log != '0';
    seed(0x47483221u);
    tasks_ = std::make_unique<TaskMgr>(*this);
    registerClass("Object", [](Interp&, const std::string& name, const DataNode&) { return std::make_unique<ScriptObject>(name); });
    installCoreBuiltins();
    installGh2Builtins();
}

Interp::~Interp() { tasks_.reset(); }

const DataNode& Interp::keep(DataNode tree) {
    trees_.push_back(std::move(tree));
    return trees_.back();
}

std::vector<std::string> Interp::builtinNames() const {
    std::vector<std::string> v;
    for (const auto& [k, f] : builtins_) v.push_back(k);
    return v;
}

void Interp::registerObject(ScriptObject* o) { objects_[o->name()] = o; }
void Interp::unregisterObject(ScriptObject* o) {
    for (auto it = objects_.begin(); it != objects_.end();)
        it = it->second == o ? objects_.erase(it) : std::next(it);
}
void Interp::unregisterName(const std::string& name, const ScriptObject* o) {
    if (auto it = objects_.find(name); it != objects_.end() && it->second == o) objects_.erase(it);
}

ScriptObject* Interp::findObject(const std::string& name) const {
    if (auto t = vars_.find("this"); t != vars_.end() && t->second.type == Value::Type::Object && t->second.object &&
                                      !t->second.object->nameScope.empty())
        if (auto s = objects_.find(ScriptObject::scoped(t->second.object->nameScope, name)); s != objects_.end()) return s->second;
    auto it = objects_.find(name);
    if (it != objects_.end()) return it->second;
    if (size_t c = name.find("::"); c != std::string::npos) {  // namespaces: try the plain name
        it = objects_.find(name.substr(c + 2));
        if (it != objects_.end()) return it->second;
        if (resolver) return resolver(name.substr(c + 2));
    }
    return resolver && !name.empty() ? resolver(name) : nullptr;
}

ScriptObject* Interp::adopt(std::unique_ptr<ScriptObject> o) {
    owned_.push_back(std::move(o));
    return owned_.back().get();
}

void Interp::destroyObject(ScriptObject* o) {
    if (!o) return;
    if (tasks_) tasks_->killFor(o);
    if (onDestroy) onDestroy(o);
    unregisterObject(o);
    for (auto it = owned_.begin(); it != owned_.end(); ++it)
        if (it->get() == o) {
            graveyard_.push_back(std::move(*it));
            owned_.erase(it);
            break;
        }
}

void Interp::seed(uint32_t s) {
    // splitmix32 -> xoshiro128** state: the same numbers on every platform.
    for (uint32_t& r : rng_) {
        s += 0x9E3779B9u;
        uint32_t z = s;
        z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13)) * 0xC2B2AE35u;
        r = z ^ (z >> 16);
    }
    if (!(rng_[0] | rng_[1] | rng_[2] | rng_[3])) rng_[0] = 1;
}

uint32_t Interp::nextRandom() {
    auto rotl = [](uint32_t x, int k) { return (x << k) | (x >> (32 - k)); };
    uint32_t result = rotl(rng_[1] * 5, 7) * 9;
    uint32_t t = rng_[1] << 9;
    rng_[2] ^= rng_[0];
    rng_[3] ^= rng_[1];
    rng_[1] ^= rng_[2];
    rng_[0] ^= rng_[3];
    rng_[2] ^= t;
    rng_[3] = rotl(rng_[3], 11);
    return result;
}

RandomGen* Interp::activeRandom() {
    auto t = vars_.find("this");
    return t != vars_.end() && t->second.type == Value::Type::Object && t->second.object ? t->second.object->random : nullptr;
}

float Interp::randomFloat(float lo, float hi) {
    if (RandomGen* g = activeRandom()) return g->real(lo, hi);
    float u = static_cast<float>(nextRandom() >> 8) * (1.0f / 16777216.0f);  // [0, 1)
    float v = lo + (hi - lo) * u;
    return v < hi || hi <= lo ? v : lo;
}

int Interp::randomInt(int lo, int hi) {
    if (RandomGen* g = activeRandom()) return g->integer(lo, hi);
    if (hi <= lo) return lo;
    uint32_t span = static_cast<uint32_t>(hi - lo);
    return lo + static_cast<int>(nextRandom() % span);
}

// ---- L1 report

void Interp::report(std::map<std::string, int>& counts, const char* kind, const std::string& key) {
    if (counts[key]++ == 0) {
        if (logReport) SDL_Log("ui: %s %s", kind, key.c_str());
        if (reportListener) reportListener(kind, key);
    }
}

void Interp::onUnhandled(const std::string& object, const std::string& message) {
    report(unhandled_, "UNHANDLED", object + " " + message);
}
void Interp::onStub(const std::string& object, const std::string& message, const Value&) {
    report(stubs_, "STUB", object + " " + message);
}
void Interp::onUnknown(const std::string& head) { report(unknown_, "UNKNOWN", head); }
void Interp::onError(const std::string& what) {
    errors_.push_back(what);
    SDL_Log("script: error: %s", what.c_str());
    if (reportListener) reportListener("ERROR", what);
}
void Interp::resetReport() {
    unhandled_.clear();
    stubs_.clear();
    unknown_.clear();
    errors_.clear();
}

// ---- evaluation

void Interp::run(const DataNode& script) {
    for (const DataNode& n : script.children)
        if (n.type == DataNode::Type::Command) {
            try {
                eval(n);
            } catch (const std::exception& ex) {
                onError(ex.what());
            }
        }
}

void Interp::runList(const DataNode& list, size_t first) {
    for (size_t k = first; k < list.children.size(); ++k)
        if (list.children[k].type == DataNode::Type::Command) {
            try {
                eval(list.children[k]);
            } catch (const std::exception& ex) {
                onError(ex.what());
            }
        }
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
    case T::Unhandled: return Value::unhandled();  // DTB node type 6
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
        Value key = n.children[0].isText() ? Value::symbol(n.children[0].s) : eval(n.children[0]);
        return self->getProp(*this, key.str());
    }
    default: return toValue(n);
    }
}

Value Interp::runBody(const std::vector<DataNode>& nodes, size_t first) {
    Value last = Value::integer(0);
    for (size_t k = first; k < nodes.size(); ++k) last = eval(nodes[k]);
    return last;
}

std::vector<Value> Interp::evalArgs(const DataNode& c, size_t first) {
    std::vector<Value> v;
    for (size_t k = first; k < c.children.size(); ++k) v.push_back(eval(c.children[k]));
    return v;
}

Value Interp::evalArg(const DataNode& c, size_t k) { return k < c.children.size() ? eval(c.children[k]) : Value::integer(0); }

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
    Value result;
    try {
        result = runBody(h.children, body);
    } catch (...) {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) vars_[it->first] = it->second;
        throw;
    }
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) vars_[it->first] = it->second;
    return result;
}

Value Interp::send(ScriptObject* o, const std::string& message, std::vector<Value> args, bool reportIt) {
    if (!o) return Value::integer(0);
    if (++depth_ > 200) {
        --depth_;
        onError("script recursion deeper than 200 at " + o->name() + " " + message);
        return Value::integer(0);
    }
    struct Depth {
        int& d;
        ~Depth() { --d; }
    } guard{depth_};
    const DataNode* h = o->handler(message);
    if (h) {
        Value r = callHandler(o, *h, args);
        if (!r.isUnhandled()) return r;
    }
    Value result = Value::integer(0);
    if (o->handleNative(*this, message, args, result)) return result;
    if (message == "get" && !args.empty()) return o->getProp(*this, args[0].str());
    if (message == "set" && args.size() >= 2) {
        o->setProp(*this, args[0].str(), args[1]);
        return args[1];
    }
    if (message == "has" && !args.empty()) return Value::integer(o->hasProp(args[0].str()) ? 1 : 0);
    if (message == "name") return Value::symbol(o->name());
    if (message == "class_name") return Value::symbol(o->className());
    if ((message == "thread_task" || message == "script_task") && !args.empty() && args[0].type == Value::Type::Code)
        return Value::obj(tasks_->threadTaskFromCode(args, kTaskUISeconds, o));
    if (!h && reportIt) onUnhandled(o->name(), message);
    return Value::unhandled();
}

Value Interp::send(const std::string& object, const std::string& message, std::vector<Value> args, bool reportIt) {
    ScriptObject* o = findObject(object);
    if (!o) {
        if (reportIt) onUnknown(object);
        return Value::unhandled();
    }
    return send(o, message, std::move(args), reportIt);
}

Value Interp::callFunc(const std::string& name, std::vector<Value> args) {
    auto it = funcs_.find(name);
    if (it == funcs_.end()) return Value::integer(0);
    const DataNode& f = *it->second;  // {func name [($params)] body...}
    size_t body = 2;
    std::vector<std::pair<std::string, Value>> saved;
    if (f.children.size() > 2 && f.children[2].type == DataNode::Type::Array &&
        std::all_of(f.children[2].children.begin(), f.children[2].children.end(),
                    [](const DataNode& c) { return c.type == DataNode::Type::Var; })) {
        const auto& params = f.children[2].children;
        for (size_t k = 0; k < params.size(); ++k) {
            std::string p = params[k].s[0] == '$' ? params[k].s.substr(1) : params[k].s;
            saved.push_back({p, vars_[p]});
            vars_[p] = k < args.size() ? args[k] : Value::integer(0);
        }
        body = 3;
    }
    if (++depth_ > 200) {
        --depth_;
        for (auto s = saved.rbegin(); s != saved.rend(); ++s) vars_[s->first] = s->second;
        onError("script recursion deeper than 200 in func " + name);
        return Value::integer(0);
    }
    Value r;
    try {
        r = runBody(f.children, body);
    } catch (...) {
        --depth_;
        for (auto s = saved.rbegin(); s != saved.rend(); ++s) vars_[s->first] = s->second;
        throw;
    }
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
            if (funcs_.count(head.s)) return callFunc(head.s, evalArgs(cmd, 1));
        }
        target = findObject(head.s);
        if (!target) {
            onUnknown(head.s);
            return Value::integer(0);
        }
    } else {
        Value h = eval(head);
        if (h.type == Value::Type::Object) target = h.object;
        else if (h.isText() && !h.s.empty()) {
            target = findObject(h.s);
            if (!target) onUnknown(h.s);
        }
        if (!target) return Value::integer(0);
    }
    if (ch.size() < 2) return Value::obj(target);
    std::string msg = eval(ch[1]).str();
    bool lazy = std::find(lazyMessages.begin(), lazyMessages.end(), msg) != lazyMessages.end();
    bool byName = std::find(byNameMessages.begin(), byNameMessages.end(), msg) != byNameMessages.end();
    std::vector<Value> args;
    for (size_t k = 2; k < ch.size(); ++k) {
        if (byName && ch[k].type == DataNode::Type::Var) args.push_back(Value::symbol(ch[k].s));
        else args.push_back(lazy && ch[k].type == DataNode::Type::Command ? toValue(ch[k]) : eval(ch[k]));
    }
    return send(target, msg, std::move(args));
}

Value Interp::newObject(const DataNode& c) {
    // {new Class name (def)...}: GH2's DataNew [I] (ObjectDir new + type definition).
    if (c.children.size() < 2) return Value::integer(0);
    std::string cls = evalArg(c, 1).str();
    std::string name = c.children.size() > 2 ? evalArg(c, 2).str() : std::string();
    ++newCounts_[cls];
    std::unique_ptr<ScriptObject> o;
    if (auto it = classes_.find(cls); it != classes_.end()) {
        o = it->second(*this, name, c);
    } else {
        onUnknown("new " + cls);
        o = std::make_unique<ScriptObject>(name, cls);
    }
    if (!o) return Value::integer(0);
    if (o->name() != name) o->rename(name);
    o->setTypeDef(&c, 3);
    if (!name.empty()) {
        if (auto it = objects_.find(name); it != objects_.end()) {
            // A second definition replaces the first (the game deletes the old object).
            bool owned = std::any_of(owned_.begin(), owned_.end(), [&](const auto& p) { return p.get() == it->second; });
            if (owned) destroyObject(it->second);
        }
    }
    ScriptObject* raw = adopt(std::move(o));
    if (!name.empty()) objects_[name] = raw;
    return Value::obj(raw);
}
