// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/dta.h"

class GameData;
class Interp;

// Loads a script the way the game's DataArray reader does: decrypts the .dtb,
// then applies the directives it keeps as nodes (#define, #include, #merge,
// #ifdef / #ifndef / #else / #endif) and expands macros in place (a macro's
// elements are spliced where its name appears). Macros persist in `macros`,
// so later files see earlier definitions (config/macros.dta and friends).
class ScriptLoader {
public:
    explicit ScriptLoader(const GameData& data) : data_(data) {}

    // `dtaPath` is the source name, e.g. "ghui/main.dta" (read from its .dtb).
    DataNode load(const std::string& dtaPath);
    std::map<std::string, DataNode> macros;  // name -> value array

private:
    void preprocess(const DataNode& in, DataNode& out, const std::string& dir, int depth);
    void expand(DataNode& n, int depth);
    const GameData& data_;
};

// A script value. Arrays hold values; code nodes (a {command} inside a
// literal array) stay unevaluated until something evaluates them.
struct Value {
    enum class Type { Int, Float, Symbol, String, Array, Object, Code, Null };
    Type type = Type::Int;
    int i = 0;
    float f = 0;
    std::string s;                                 // symbol / string
    std::shared_ptr<std::vector<Value>> array;
    class ScriptObject* object = nullptr;
    const DataNode* code = nullptr;

    static Value integer(int v) { Value x; x.i = v; return x; }
    static Value real(float v) { Value x; x.type = Type::Float; x.f = v; return x; }
    static Value symbol(std::string v) { Value x; x.type = Type::Symbol; x.s = std::move(v); return x; }
    static Value string(std::string v) { Value x; x.type = Type::String; x.s = std::move(v); return x; }
    static Value list(std::vector<Value> v = {}) {
        Value x;
        x.type = Type::Array;
        x.array = std::make_shared<std::vector<Value>>(std::move(v));
        return x;
    }
    static Value obj(ScriptObject* o) { Value x; x.type = o ? Type::Object : Type::Int; x.object = o; return x; }
    static Value null() { Value x; x.type = Type::Null; return x; }

    bool isNumber() const { return type == Type::Int || type == Type::Float; }
    bool isText() const { return type == Type::Symbol || type == Type::String; }
    float number() const { return type == Type::Float ? f : static_cast<float>(i); }
    int integerValue() const { return type == Type::Float ? static_cast<int>(f) : i; }
    bool truthy() const;
    std::string str() const;  // printable form
    bool operator==(const Value& o) const;
};

// Anything scripts can send messages to: {<object> <message> args...}.
// Scripts attach handlers (`add_handlers` / a definition's `handlers`
// blocks); natives answer in handleNative. Properties back get / set / has.
class ScriptObject {
public:
    explicit ScriptObject(std::string name) : name_(std::move(name)) {}
    virtual ~ScriptObject() = default;

    const std::string& name() const { return name_; }
    // Adds handler arrays: each is (message [($params)] body...).
    void addHandlers(const DataNode& list, size_t first = 0);
    const DataNode* handler(const std::string& message) const;

    // Native messages; return true when handled.
    virtual bool handleNative(Interp&, const std::string&, std::vector<Value>&, Value&) { return false; }

    std::map<std::string, Value> props;

private:
    std::string name_;
    std::vector<const DataNode*> handlers_;  // most recently added first
    std::vector<std::shared_ptr<DataNode>> owned_;
};

// Evaluates script code. Variables are global with dynamic scoping for
// function parameters and `do` locals (as Harmonix's DataArray does).
class Interp {
public:
    Interp();

    // Evaluates every top-level command of a loaded script (definitions,
    // add_handlers, func, set ...). The tree must outlive the interpreter.
    void run(const DataNode& script);
    Value eval(const DataNode& node);
    Value evalValue(const Value& v);  // evaluates Code values
    Value toValue(const DataNode& node);  // literal conversion (arrays keep code unevaluated)

    // Sends a message; evaluates the object's handler or its natives.
    Value send(ScriptObject* object, const std::string& message, std::vector<Value> args);
    Value send(const std::string& object, const std::string& message, std::vector<Value> args = {});

    void registerObject(ScriptObject* object);
    // Also reachable under another name (a panel's object_name).
    void registerAs(const std::string& name, ScriptObject* object) { objects_[name] = object; }
    void unregisterObject(ScriptObject* object);
    ScriptObject* findObject(const std::string& name) const;

    Value& var(const std::string& name) { return vars_[name]; }
    bool hasFunc(const std::string& name) const { return funcs_.count(name) != 0; }
    Value callFunc(const std::string& name, std::vector<Value> args);

    // Builtins are commands looked up by their first symbol.
    using Builtin = std::function<Value(Interp&, const DataNode& cmd)>;
    void addBuiltin(const std::string& name, Builtin fn) { builtins_[name] = std::move(fn); }

    // Messages whose {command} arguments are passed unevaluated, as Code
    // values (deferred bodies such as delay_task).
    std::vector<std::string> lazyMessages{"delay_task", "thread_task", "foreach_player", "foreach_player_matcher", "interp_task", "foreach_venue",
                                          "wait", "wait_timeout"};

    // Called for names no registered object has (e.g. a panel's components).
    std::function<ScriptObject*(const std::string&)> resolver;
    static bool unhandled(const Value& v) { return v.type == Value::Type::Symbol && v.s == "kDataUnhandled"; }

    // Localized strings ({localize token}), filled by the host.
    std::map<std::string, std::string> locale;

private:
    Value command(const DataNode& cmd);
    Value runBody(const std::vector<DataNode>& nodes, size_t first);
    Value callHandler(ScriptObject* self, const DataNode& h, std::vector<Value>& args);
    void installCoreBuiltins();

    std::map<std::string, Value> vars_;
    std::map<std::string, const DataNode*> funcs_;
    std::map<std::string, Builtin> builtins_;
    std::map<std::string, ScriptObject*> objects_;
    std::map<std::string, int> warned_;
    int depth_ = 0;
};
