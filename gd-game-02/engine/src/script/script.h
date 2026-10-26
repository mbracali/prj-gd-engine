// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// GH2's script language (Harmonix DataArray): the loader with its
// preprocessor, script values and objects, and the interpreter. Ported from
// gd01 (gd-game-01/engine/src/script) with GH2's changes: backslash include
// paths, HX_XBOX / _SHIP defined, #undef / #autorun, #merge as DataArray
// merge, `new <Class>` objects, `handle`, script_task / thread_task on a
// TaskMgr shared by the UI and the world (script/tasks.h), and one place that
// reports unhandled messages, stubs and unknown names (L1).

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/dta.h"

class GameData;
class Interp;
class TaskMgr;

// Loads a script the way the game's DataArray reader does: decrypts the .dtb,
// then applies the directives it keeps as nodes and expands macros in place
// (a macro's elements are spliced where its name appears; macro values are
// expanded when defined). Macros persist in `macros`, so later files see
// earlier definitions (config/macros.dta and friends), as in the game.
//   #include f   splices f's preprocessed contents here (path relative to the
//                including file; `\` separators accepted);
//   #merge f     merges f into the enclosing array (mergeArrays);
//   #define N v  / #undef N; #ifdef / #ifndef / #else / #endif on the macros;
//   #autorun {c} keeps c in `autoruns` for the host to run after loading.
// HX_XBOX and _SHIP are predefined (GH2's 360 retail build) [I].
class ScriptLoader {
public:
    // Reads the parsed tree of a compiled script, by its .dtb archive path.
    using Reader = std::function<DataNode(const std::string& dtbPath)>;

    explicit ScriptLoader(const GameData& data);
    explicit ScriptLoader(Reader reader);

    // `dtaPath` is the source name, e.g. "ui/ui.dta" (read from ui/gen/ui.dtb).
    // Throws std::runtime_error if that file cannot be read; failed includes
    // and merges are logged and counted in `stats` instead.
    DataNode load(const std::string& dtaPath);
    // Preprocesses an already parsed tree as if it were the file `dtaPath`.
    DataNode preprocessTree(const DataNode& root, const std::string& dtaPath);

    // DataArray merge (#merge): each non-empty array of `src` whose first
    // element matches the first element of an array of `dst` is merged into
    // that array recursively; the others are appended. `dst`'s values win;
    // non-array elements of `src` are dropped.
    static void mergeArrays(DataNode& dst, const DataNode& src);

    std::map<std::string, DataNode> macros;  // name -> value (an Array of the spliced elements)
    std::vector<DataNode> autoruns;           // #autorun commands, in load order

    struct Stats {
        int files = 0, includes = 0, merges = 0, defines = 0, undefs = 0, conditionals = 0, autoruns = 0;
        int macroUses = 0, unknownDirectives = 0;
        std::vector<std::string> errors;  // failed includes / merges
    };
    Stats stats;

private:
    void preprocess(const DataNode& in, DataNode& out, const std::string& dir, int depth);
    DataNode readFile(const std::string& path, int depth);
    Reader reader_;
};

// A script value. Arrays hold values; code nodes (a {command}, $var or [prop]
// inside a literal array) stay unevaluated until something evaluates them.
struct Value {
    enum class Type { Int, Float, Symbol, String, Array, Object, Code, Null, Unhandled };
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
    // kDataUnhandled: what a message nobody handles returns; false in conditions.
    static Value unhandled() { Value x; x.type = Type::Unhandled; return x; }

    bool isNumber() const { return type == Type::Int || type == Type::Float; }
    bool isText() const { return type == Type::Symbol || type == Type::String; }
    bool isUnhandled() const { return type == Type::Unhandled || (type == Type::Symbol && s == "kDataUnhandled"); }
    float number() const { return type == Type::Float ? f : static_cast<float>(i); }
    int integerValue() const { return type == Type::Float ? static_cast<int>(f) : i; }
    bool truthy() const;
    std::string str() const;  // printable form
    bool operator==(const Value& o) const;
};

// Anything scripts can send messages to: {<object> <message> args...}.
// Script handlers come from a definition's arrays (`new`'s type definition,
// an objects.dta type, add_handlers): (message [($params)] body...); natives
// answer in handleNative. Properties ([prop], get / set) are in `props`, then
// the definition's (prop value) arrays.
class ScriptObject {
public:
    explicit ScriptObject(std::string name, std::string className = "Object")
        : name_(std::move(name)), class_(std::move(className)) {}
    virtual ~ScriptObject() = default;

    const std::string& name() const { return name_; }
    const std::string& className() const { return class_; }
    void rename(std::string name) { name_ = std::move(name); }

    // Adds handler arrays list.children[first...]; later additions win.
    void addHandlers(const DataNode& list, size_t first = 0);
    const DataNode* handler(const std::string& message) const;
    // The `new` definition ({new Class name (...)...}) or null.
    const DataNode* typeDef() const { return typeDef_; }
    void setTypeDef(const DataNode* def, size_t first);

    // Native messages; return true when handled (result in `r`).
    virtual bool handleNative(Interp&, const std::string& /*msg*/, std::vector<Value>& /*args*/, Value& /*r*/) { return false; }
    // Properties: [name], {o get name}, {o set name v}. Natives override to
    // expose their state; the defaults use `props` then the definition.
    virtual Value getProp(Interp& in, const std::string& key);
    virtual void setProp(Interp& in, const std::string& key, const Value& v);
    bool hasProp(const std::string& key) const;

    std::map<std::string, Value> props;

private:
    std::string name_, class_;
    std::vector<const DataNode*> handlers_;  // most recently added first
    const DataNode* typeDef_ = nullptr;
};

// Answers every message it has no handler for with 0, reported as a STUB
// (Interp::onStub). For objects a host does not implement yet, and for
// --scripttest.
class StubObject : public ScriptObject {
public:
    using ScriptObject::ScriptObject;
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override;
};

// Evaluates script code. Variables are global with dynamic scoping for
// handler / func parameters and `do` locals (as Harmonix's DataArray does).
//
// Hosts (the UI manager, the world) plug in through:
//   registerObject / registerAs         named objects ({game ...}, {ui ...});
//   registerClass                       `{new Class name (defs)...}` factories;
//   addBuiltin                          native functions ({play_sfx ...});
//   resolver                            names no registered object has (a panel's components);
//   tasks()                             the TaskMgr: set its clocks, poll it every frame;
//   onUnhandled / onStub / onUnknown    the L1 report (GD02_UILOG=1 logs each key once).
class Interp {
public:
    Interp();
    ~Interp();
    Interp(const Interp&) = delete;
    Interp& operator=(const Interp&) = delete;

    // Keeps a loaded tree alive for as long as the interpreter (handlers and
    // tasks point into it) and returns it.
    const DataNode& keep(DataNode tree);
    // Evaluates every top-level command of a script (definitions, func, set,
    // new ...). Errors are logged and counted (errors()).
    void run(const DataNode& script);
    // Runs the commands of a list from `first` on (e.g. ui.dta's (init ...)).
    void runList(const DataNode& list, size_t first = 0);
    Value eval(const DataNode& node);
    Value evalValue(const Value& v);       // evaluates Code values
    Value toValue(const DataNode& node);   // literal conversion (arrays keep code unevaluated)

    // Sends a message: the object's script handler, then (when it is absent
    // or returns kDataUnhandled) its natives, then get / set / has / name.
    // Unanswered messages return kDataUnhandled and, if `report`, go to onUnhandled.
    Value send(ScriptObject* object, const std::string& message, std::vector<Value> args, bool report = true);
    Value send(const std::string& object, const std::string& message, std::vector<Value> args = {}, bool report = true);
    // Runs a handler array (message [($params)] body...) with $this = self.
    Value callHandler(ScriptObject* self, const DataNode& h, std::vector<Value>& args);

    // ---- objects
    void registerObject(ScriptObject* object);
    void registerAs(const std::string& name, ScriptObject* object) { objects_[name] = object; }
    void unregisterObject(ScriptObject* object);
    void unregisterName(const std::string& name, const ScriptObject* object);
    ScriptObject* findObject(const std::string& name) const;
    // Interp-owned objects (made by `new`); {delete name} destroys them.
    ScriptObject* adopt(std::unique_ptr<ScriptObject> object);
    void destroyObject(ScriptObject* object);
    // Called for names no registered object has (e.g. a panel's components).
    std::function<ScriptObject*(const std::string&)> resolver;
    // Called before an object is destroyed by {delete} or a replacing `new`.
    std::function<void(ScriptObject*)> onDestroy;

    // ---- classes: {new Class name (defs)...}. The factory makes the object;
    // the interpreter names it, attaches the definition's handlers and
    // properties (setTypeDef), owns and registers it. "Object" is built in.
    using ClassFactory = std::function<std::unique_ptr<ScriptObject>(Interp&, const std::string& name, const DataNode& def)>;
    void registerClass(const std::string& className, ClassFactory factory) { classes_[className] = std::move(factory); }
    bool hasClass(const std::string& className) const { return classes_.count(className) != 0; }
    // Objects made per class (for reports).
    const std::map<std::string, int>& newCounts() const { return newCounts_; }

    // ---- variables, funcs, builtins
    Value& var(const std::string& name) { return vars_[name]; }
    bool hasFunc(const std::string& name) const { return funcs_.count(name) != 0; }
    Value callFunc(const std::string& name, std::vector<Value> args);
    using Builtin = std::function<Value(Interp&, const DataNode& cmd)>;
    void addBuiltin(const std::string& name, Builtin fn) { builtins_[name] = std::move(fn); }
    bool hasBuiltin(const std::string& name) const { return builtins_.count(name) != 0; }
    std::vector<std::string> builtinNames() const;
    // Evaluated arguments of a builtin's command from `first` on.
    std::vector<Value> evalArgs(const DataNode& cmd, size_t first = 1);
    Value evalArg(const DataNode& cmd, size_t k);

    // Messages whose {command} arguments are passed unevaluated, as Code values.
    std::vector<std::string> lazyMessages{"delay_task", "thread_task", "script_task", "foreach_player", "foreach_player_matcher",
                                          "interp_task", "foreach_venue", "wait", "wait_timeout", "iterate"};
    // Messages whose $var arguments are passed by name (as symbols).
    std::vector<std::string> byNameMessages{"foreach_player", "foreach_player_matcher", "interp_task", "foreach_venue",
                                            "finish_song", "next_cash_award", "get_highscore", "wait_timeout", "iterate"};

    // ---- host settings
    std::map<std::string, std::string> locale;  // {localize token}
    std::string language = "eng";               // {system_language}: eng fre deu ita esl
    // Deterministic random numbers (random_int, random_float, random_elem).
    void seed(uint32_t s);
    uint32_t nextRandom();
    float randomFloat(float lo, float hi);  // [lo, hi)
    int randomInt(int lo, int hi);          // [lo, hi)

    TaskMgr& tasks() { return *tasks_; }

    // ---- L1: one hook point for everything the port does not do yet. Each
    // key is counted; GD02_UILOG=1 logs it once ("ui: UNHANDLED obj msg").
    void onUnhandled(const std::string& object, const std::string& message);
    void onStub(const std::string& object, const std::string& message, const Value& value);
    void onUnknown(const std::string& head);
    void onError(const std::string& what);  // script errors ({fail ...}, exceptions)
    const std::map<std::string, int>& unhandledCounts() const { return unhandled_; }
    const std::map<std::string, int>& stubCounts() const { return stubs_; }
    const std::map<std::string, int>& unknownCounts() const { return unknown_; }
    const std::vector<std::string>& errors() const { return errors_; }
    void resetReport();
    // Optional observer (tests, dev overlays): kind is "UNHANDLED", "STUB", "UNKNOWN" or "ERROR".
    std::function<void(const char* kind, const std::string& key)> reportListener;
    bool logReport = false;  // GD02_UILOG=1

private:
    friend class TaskMgr;
    Value command(const DataNode& cmd);
    Value runBody(const std::vector<DataNode>& nodes, size_t first);
    Value newObject(const DataNode& cmd);
    void installCoreBuiltins();
    void installGh2Builtins();
    void report(std::map<std::string, int>& counts, const char* kind, const std::string& key);

    std::map<std::string, Value> vars_;
    std::map<std::string, const DataNode*> funcs_;
    std::map<std::string, Builtin> builtins_;
    std::map<std::string, ScriptObject*> objects_;
    std::map<std::string, ClassFactory> classes_;
    std::map<std::string, int> newCounts_;
    std::vector<std::unique_ptr<ScriptObject>> owned_;
    std::vector<std::unique_ptr<ScriptObject>> graveyard_;  // destroyed objects stay allocated (values may point at them)
    std::deque<DataNode> trees_;
    std::map<std::string, int> unhandled_, stubs_, unknown_;
    std::vector<std::string> errors_;
    std::unique_ptr<TaskMgr> tasks_;
    uint32_t rng_[4] = {};
    int depth_ = 0;
};
