// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "script/vm.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "data/qbkey.h"

namespace qs {

namespace {

constexpr uint32_t kChange = qbkey::key("change");

bool isEnd(Op op) { return op == kEndOfLine || op == kEndOfLineNumber || op == kEndScript; }

qb::Value none() { return qb::Value{}; }

}  // namespace

// The executor of one line: a cursor over the frame's tokens.
struct Exec {
    Vm& vm;
    ScriptRun& run;
    Frame* fr;
    const std::vector<Token>& t;
    size_t i;

    Exec(Vm& v, ScriptRun& r) : vm(v), run(r), fr(&r.frames.back()), t(fr->code->tokens), i(fr->pc) {}

    const Token& cur() const { return t[i]; }
    bool atEnd() const { return i >= t.size() || isEnd(t[i].op); }
    void skipNops() {
        while (i < t.size() && t[i].op == kNop) ++i;
    }

    // -- values ---------------------------------------------------------
    const qb::Value* local(uint32_t name) const {
        if (!fr->locals) return nullptr;
        const qb::Value* found = nullptr;
        for (const qb::Item& it : fr->locals->items)
            if (it.name == name) found = &it.value;
        return found;
    }

    qb::Value globalValue(uint32_t name) {
        if (const qb::Value* v = vm.global(name)) return *v;
        vm.logOnce("UNKNOWN global " + qbkey::name(name));
        return none();
    }

    static qb::Value member(const qb::Value& base0, uint32_t field, Vm& vm) {
        // `.field` on a key reads the global struct the key names [C data:
        // start_flow_manager's `<arg> current_flow_state . Create`].
        qb::Value base = base0;
        if ((base.type == qb::Type::Key || base.type == qb::Type::KeyPtr) && vm.global(base.key)) base = *vm.global(base.key);
        if (base.type == qb::Type::Struct && base.st) {
            if (const qb::Value* v = vm.globals().field(*base.st, field)) return *v;
        }
        return none();
    }

    qb::Value index(const qb::Value& base, const qb::Value& idx) {
        if (base.type == qb::Type::Array && base.arr) {
            const int k = int(Vm::number(&idx, -1));
            if (k >= 0 && size_t(k) < base.arr->values.size()) return base.arr->values[size_t(k)];
        }
        return none();
    }

    // A struct literal `{ ... }` (cursor on `{`).
    qb::Value structLiteral() {
        ++i;  // {
        auto st = std::make_shared<qb::Struct>();
        while (i < t.size() && t[i].op != kEndStruct) {
            const Op op = t[i].op;
            if (op == kEndOfLine || op == kEndOfLineNumber || op == kComma || op == kNop) {
                ++i;
                continue;
            }
            if (op == kEndScript) break;
            paramItem(*st);
        }
        if (i < t.size() && t[i].op == kEndStruct) ++i;
        qb::Value v;
        v.type = qb::Type::Struct;
        v.st = st;
        return v;
    }

    // An array literal `[ ... ]` (cursor on `[`).
    qb::Value arrayLiteral() {
        ++i;
        auto a = std::make_shared<qb::Array>();
        while (i < t.size() && t[i].op != kEndArray) {
            const Op op = t[i].op;
            if (op == kEndOfLine || op == kEndOfLineNumber || op == kComma || op == kNop) {
                ++i;
                continue;
            }
            if (op == kEndScript) break;
            qb::Value v = unary();
            if (a->type == qb::Type::None) a->type = v.type;
            a->values.push_back(std::move(v));
        }
        if (i < t.size() && t[i].op == kEndArray) ++i;
        qb::Value v;
        v.type = qb::Type::Array;
        v.arr = a;
        return v;
    }

    // primary: literals, names (keys), <arg>, $global, (expr), {..}, [..].
    qb::Value primary() {
        skipNops();
        if (atEnd()) return none();
        const Token& k = t[i];
        qb::Value v;
        switch (k.op) {
        case kInteger: ++i; return Vm::makeInt(k.i());
        case kHexInteger: ++i; return Vm::makeInt(k.i());
        case kFloat: ++i; return Vm::makeFloat(k.f[0]);
        case kString: ++i; return Vm::makeString(k.str);
        case kLocalString:
        case kWideString:
            ++i;
            v = Vm::makeString(k.str);
            v.type = qb::Type::WString;
            return v;
        case kVector:
            ++i;
            v.type = qb::Type::Vector;
            std::copy(k.f, k.f + 3, v.f);
            return v;
        case kPair:
            ++i;
            v.type = qb::Type::Pair;
            v.f[0] = k.f[0], v.f[1] = k.f[1];
            return v;
        case kName: ++i; return Vm::makeKey(k.u);
        case kArg: {
            ++i;
            skipNops();
            if (i < t.size() && t[i].op == kName) {
                const uint32_t n = t[i++].u;
                if (const qb::Value* lv = local(n)) return *lv;
                return none();
            }
            return none();
        }
        case kAllArgs: {
            ++i;
            v.type = qb::Type::Struct;
            v.st = fr->locals ? std::make_shared<qb::Struct>(*fr->locals) : std::make_shared<qb::Struct>();
            return v;
        }
        case kGlobal: {
            ++i;
            skipNops();
            // $name, or $<arg> x (the global named by a local's key).
            uint32_t name = 0;
            if (i < t.size() && t[i].op == kName) name = t[i++].u;
            else {
                qb::Value inner = primary();
                if (inner.type == qb::Type::Key || inner.type == qb::Type::KeyPtr) name = inner.key;
            }
            return name ? globalValue(name) : none();
        }
        case kOpenParen: {
            ++i;
            qb::Value e = expression();
            skipNops();
            if (i < t.size() && t[i].op == kCloseParen) ++i;
            return e;
        }
        case kStartStruct: return structLiteral();
        case kStartArray: return arrayLiteral();
        case kInlineStruct:
        case kInlineStruct2:
            ++i;
            v.type = qb::Type::Struct;
            v.st = std::make_shared<qb::Struct>(*k.st);
            return v;
        case kRandom:
        case kRandom2:
        case kRandomNoRepeat:
        case kRandomPermute: return randomChoice();
        default:
            ++i;
            vm.logOnce(std::string("UNHANDLED token ") + std::to_string(int(k.op)) + " in an expression");
            return none();
        }
    }

    // Random(@a @b ...): weighted pick of the branches that follow; each
    // branch is an expression ended by a jump past the rest [I].
    qb::Value randomChoice() {
        const Token& k = t[i];
        const size_t after = i + 1;
        if (k.weights.empty()) {
            ++i;
            return none();
        }
        uint32_t total = 0;
        for (uint16_t w : k.weights) total += w ? w : 1;
        uint32_t r = uint32_t(vm.rng()() % std::max<uint32_t>(1, total));
        size_t pick = 0;
        for (size_t n = 0; n < k.weights.size(); ++n) {
            const uint32_t w = k.weights[n] ? k.weights[n] : 1;
            if (r < w) {
                pick = n;
                break;
            }
            r -= w;
        }
        // Offsets are relative to the end of the offset table [I].
        const uint32_t target = uint32_t(int32_t(k.end) + k.offsets[pick]);
        i = after;
        for (size_t n = after; n < t.size(); ++n)
            if (t[n].at == target) {
                i = n;
                break;
            }
        return unary();
    }

    qb::Value postfix() {
        qb::Value v = primary();
        for (;;) {
            skipNops();
            if (i < t.size() && t[i].op == kDot) {
                ++i;
                skipNops();
                if (i < t.size() && t[i].op == kName) v = member(v, t[i++].u, vm);
                else if (i < t.size() && t[i].op == kArg) {
                    qb::Value key = primary();
                    v = member(v, key.key, vm);
                }
                continue;
            }
            if (i < t.size() && t[i].op == kStartArray && (v.type == qb::Type::Array)) {
                ++i;
                qb::Value idx = expression();
                skipNops();
                if (i < t.size() && t[i].op == kEndArray) ++i;
                v = index(v, idx);
                continue;
            }
            break;
        }
        return v;
    }

    qb::Value unary() {
        skipNops();
        if (i < t.size() && t[i].op == kMinus) {
            ++i;
            qb::Value v = unary();
            if (v.type == qb::Type::Int) v.i = -v.i;
            else if (v.type == qb::Type::Float) v.f[0] = -v.f[0];
            return v;
        }
        if (i < t.size() && t[i].op == kNot) {
            ++i;
            qb::Value v = unary();
            return Vm::makeInt(Vm::truth(v) ? 0 : 1);
        }
        return postfix();
    }

    static int prec(Op op) {
        switch (op) {
        case kMultiply: case kDivide: return 6;
        case kAdd: case kMinus: return 5;
        case kLessThan: case kLessThanEqual: case kGreaterThan: case kGreaterThanEqual: return 4;
        case kSameAs: case kNotEqual: case kEquals: return 3;  // `=` in an expression is a comparison [C data]
        case kAnd: case kAnd2: return 2;
        case kOr: case kOr2: case kXor: return 1;
        default: return 0;
        }
    }

    qb::Value binary(Op op, const qb::Value& a, const qb::Value& b) {
        const bool anyFloat = a.type == qb::Type::Float || b.type == qb::Type::Float;
        auto numeric = [](const qb::Value& v) { return v.type == qb::Type::Int || v.type == qb::Type::Float; };
        switch (op) {
        case kAdd:
        case kMinus:
        case kMultiply:
        case kDivide: {
            if ((a.type == qb::Type::Vector || a.type == qb::Type::Pair) && (b.type == a.type) && (op == kAdd || op == kMinus)) {
                qb::Value r = a;
                for (int k = 0; k < 3; ++k) r.f[k] = op == kAdd ? a.f[k] + b.f[k] : a.f[k] - b.f[k];
                return r;
            }
            if ((a.type == qb::Type::Vector || a.type == qb::Type::Pair) && numeric(b) && (op == kMultiply || op == kDivide)) {
                qb::Value r = a;
                const float s = b.number();
                for (int k = 0; k < 3; ++k) r.f[k] = op == kMultiply ? a.f[k] * s : (s != 0 ? a.f[k] / s : 0);
                return r;
            }
            if (numeric(a) && (b.type == qb::Type::Vector || b.type == qb::Type::Pair) && op == kMultiply) {
                qb::Value r = b;
                for (int k = 0; k < 3; ++k) r.f[k] = b.f[k] * a.number();
                return r;
            }
            if ((a.type == qb::Type::String || a.type == qb::Type::WString) && op == kAdd) {
                qb::Value r = a;
                r.str += b.str;
                return r;
            }
            if (!numeric(a) || !numeric(b)) {
                vm.logOnce(std::string("UNHANDLED arithmetic on ") + qb::typeName(a.type) + " and " + qb::typeName(b.type));
                return none();
            }
            if (anyFloat || op == kDivide) {
                const float x = a.number(), y = b.number();
                float r = op == kAdd ? x + y : op == kMinus ? x - y : op == kMultiply ? x * y : (y != 0 ? x / y : 0);
                // Integer division stays integer when both are ints [I].
                if (!anyFloat && op == kDivide) return Vm::makeInt(b.i ? a.i / b.i : 0);
                return Vm::makeFloat(r);
            }
            const int32_t x = a.i, y = b.i;
            return Vm::makeInt(op == kAdd ? x + y : op == kMinus ? x - y : x * y);
        }
        case kLessThan: return Vm::makeInt(a.number() < b.number());
        case kLessThanEqual: return Vm::makeInt(a.number() <= b.number());
        case kGreaterThan: return Vm::makeInt(a.number() > b.number());
        case kGreaterThanEqual: return Vm::makeInt(a.number() >= b.number());
        case kSameAs:
        case kEquals:
        case kNotEqual: {
            bool eq;
            if (numeric(a) && numeric(b)) eq = a.number() == b.number();
            else if (a.type != b.type) eq = false;
            else if (a.type == qb::Type::String || a.type == qb::Type::WString) eq = a.str == b.str;
            else if (a.type == qb::Type::Key || a.type == qb::Type::KeyPtr) eq = a.key == b.key;
            else if (a.type == qb::Type::Vector || a.type == qb::Type::Pair) eq = a.f[0] == b.f[0] && a.f[1] == b.f[1] && a.f[2] == b.f[2];
            else eq = false;
            return Vm::makeInt(op != kNotEqual ? eq : !eq);
        }
        case kAnd:
        case kAnd2: return Vm::makeInt(Vm::truth(a) && Vm::truth(b));
        case kOr:
        case kOr2: return Vm::makeInt(Vm::truth(a) || Vm::truth(b));
        case kXor: return Vm::makeInt(Vm::truth(a) != Vm::truth(b));
        default: return none();
        }
    }

    qb::Value expression(int minPrec = 1) {
        qb::Value left = unary();
        for (;;) {
            skipNops();
            if (i >= t.size()) break;
            const Op op = t[i].op;
            const int p = prec(op);
            if (p < minPrec || p == 0) break;
            ++i;
            qb::Value right = expression(p + 1);
            left = binary(op, left, right);
        }
        return left;
    }

    // -- parameter lists ---------------------------------------------------
    // One item of a parameter list or struct literal.
    void paramItem(qb::Struct& out) {
        skipNops();
        const Token& k = t[i];
        if (k.op == kName && i + 1 < t.size() && t[i + 1].op == kEquals) {
            i += 2;
            qb::Value v = paramValue();
            Vm::set(out, k.u, std::move(v));
            return;
        }
        if (k.op == kAllArgs) {
            ++i;
            if (fr->locals)
                for (const qb::Item& it : fr->locals->items) Vm::set(out, it.name, it.value);
            return;
        }
        if (k.op == kInlineStruct || k.op == kInlineStruct2 || k.op == kStartStruct) {
            qb::Value v = paramValue();
            if (v.st)
                for (qb::Item it : vm.globals().expand(*v.st)) {
                    // `$name` inside an inline struct is a key reference: the
                    // global's value at call time [I].
                    if (it.value.type == qb::Type::KeyPtr)
                        if (const qb::Value* gv = vm.global(it.value.key)) it.value = *gv;
                    if (it.name) Vm::set(out, it.name, std::move(it.value));
                    else out.items.push_back(std::move(it));
                }
            return;
        }
        if (k.op == kName) {
            // A flag: an unnamed key.
            ++i;
            out.items.push_back(qb::Item{0, Vm::makeKey(k.u)});
            return;
        }
        // A positional value.
        qb::Value v = paramValue();
        out.items.push_back(qb::Item{0, std::move(v)});
    }

    qb::Value paramValue() {
        skipNops();
        return unary();
    }

    qb::Struct params() {
        qb::Struct p;
        while (true) {
            skipNops();
            if (atEnd()) break;
            const Op op = t[i].op;
            if (op == kEndIf || op == kFastElse || op == kElseIf || op == kCloseParen) break;
            const size_t before = i;
            paramItem(p);
            if (i == before) ++i;  // never stall
        }
        return p;
    }
};

// -- Vm ------------------------------------------------------------------

Vm::Vm(Globals& globals) : g_(globals) {
    const char* v = SDL_getenv("GD04_UILOG");
    uiLog_ = v && *v && *v != '0';
}

void Vm::addNative(const std::string& name, Native fn) { natives_[qbkey::key(name)] = std::move(fn); }

void Vm::logOnce(const std::string& what) {
    if (!logged_.insert(what).second) return;
    if (uiLog_) SDL_Log("qscript: %s", what.c_str());
}

const qb::Value* Vm::param(const qb::Struct& p, uint32_t name) {
    const qb::Value* found = nullptr;
    for (const qb::Item& it : p.items)
        if (it.name == name) found = &it.value;
    return found;
}

bool Vm::flag(const qb::Struct& p, uint32_t name) {
    for (const qb::Item& it : p.items)
        if (it.name == 0 && (it.value.type == qb::Type::Key) && it.value.key == name) return true;
    return false;
}

const qb::Value* Vm::positional(const qb::Struct& p, size_t index) {
    size_t n = 0;
    for (const qb::Item& it : p.items)
        if (it.name == 0 && it.value.type != qb::Type::Key) {
            if (n++ == index) return &it.value;
        }
    return nullptr;
}

void Vm::set(qb::Struct& s, uint32_t name, qb::Value v) {
    if (name) {
        for (qb::Item& it : s.items)
            if (it.name == name) {
                it.value = std::move(v);
                return;
            }
    }
    s.items.push_back(qb::Item{name, std::move(v)});
}

qb::Value Vm::makeInt(int32_t i) {
    qb::Value v;
    v.type = qb::Type::Int;
    v.i = i;
    return v;
}
qb::Value Vm::makeFloat(float f) {
    qb::Value v;
    v.type = qb::Type::Float;
    v.f[0] = f;
    return v;
}
qb::Value Vm::makeKey(uint32_t k) {
    qb::Value v;
    v.type = qb::Type::Key;
    v.key = k;
    return v;
}
qb::Value Vm::makeString(std::string s) {
    qb::Value v;
    v.type = qb::Type::String;
    v.str = std::move(s);
    return v;
}

float Vm::number(const qb::Value* v, float fallback) {
    if (!v) return fallback;
    if (v->type == qb::Type::Int) return float(v->i);
    if (v->type == qb::Type::Float) return v->f[0];
    return fallback;
}

bool Vm::truth(const qb::Value& v) {
    switch (v.type) {
    case qb::Type::None: return false;
    case qb::Type::Int: return v.i != 0;
    case qb::Type::Float: return v.f[0] != 0;
    default: return true;
    }
}

const qb::Value* Vm::global(uint32_t name) const {
    const qb::Value* v = g_.find(name);
    if (v && v->type == qb::Type::KeyPtr) return g_.resolve(*v);
    return v;
}

void Vm::setGlobal(uint32_t name, qb::Value v) { *g_.ensure(name) = std::move(v); }

std::shared_ptr<const CodeCache> Vm::code(uint32_t name) {
    auto it = codes_.find(name);
    if (it != codes_.end()) return it->second;
    const qb::Value* v = g_.find(name);
    if (!v || v->type != qb::Type::Script || !v->script) return nullptr;
    auto c = std::make_shared<CodeCache>();
    c->name = name;
    try {
        c->tokens = tokenize(v->script->code.data(), v->script->code.size());
    } catch (const std::exception& e) {
        logOnce("BAD script " + qbkey::name(name) + ": " + e.what());
        return nullptr;
    }
    codes_[name] = c;
    return c;
}

bool Vm::callName(ScriptRun& run, uint32_t name, qb::Struct& params, bool& result) {
    ++stats_.calls;
    auto nat = natives_.find(name);
    if (nat != natives_.end()) {
        ++stats_.nativeCalls;
        result = nat->second(*this, run, params);
        return true;
    }
    if (auto c = code(name)) {
        ++stats_.scriptCalls;
        Frame f;
        f.code = std::move(c);
        f.locals = std::make_shared<qb::Struct>(std::move(params));
        if (run.frames.size() > 200) {
            logOnce("script call depth over 200 at " + qbkey::name(name));
            result = false;
            return true;
        }
        run.frames.push_back(std::move(f));
        result = true;
        return true;
    }
    if (unhandledNames_.insert(name).second) {
        stats_.unhandled = unhandledNames_.size();
        logOnce("UNHANDLED call " + qbkey::name(name));
    }
    result = false;
    return false;
}

bool Vm::run(uint32_t name, const qb::Struct& params, bool* result) {
    auto c = code(name);
    if (!c) {
        if (natives_.count(name)) {
            ScriptRun tmp;
            qb::Struct p = params;
            const bool r = natives_[name](*this, tmp, p);
            if (result) *result = r;
            return true;
        }
        return false;
    }
    auto r = std::make_unique<ScriptRun>();
    r->id = nextId_++;
    r->name = name;
    Frame f;
    f.code = c;
    f.locals = std::make_shared<qb::Struct>(params);
    r->frames.push_back(std::move(f));
    const bool alive = step(*r, 1 << 20);
    if (result) *result = true;
    if (alive) runs_.push_back(std::move(r));
    return true;
}

uint32_t Vm::spawn(uint32_t name, const qb::Struct& params, uint32_t spawnedBy) {
    auto c = code(name);
    if (!c) {
        if (unhandledNames_.insert(name).second) logOnce("UNHANDLED spawn " + qbkey::name(name));
        return 0;
    }
    auto r = std::make_unique<ScriptRun>();
    r->id = nextId_++;
    r->name = name;
    r->spawnedBy = spawnedBy;
    Frame f;
    f.code = c;
    f.locals = std::make_shared<qb::Struct>(params);
    r->frames.push_back(std::move(f));
    const uint32_t id = r->id;
    runs_.push_back(std::move(r));
    return id;
}

void Vm::update(double dtMs) {
    clockMs_ += dtMs;
    // Scripts spawned during this update start next frame (the list grows).
    const size_t n = runs_.size();
    for (size_t k = 0; k < n && k < runs_.size(); ++k) {
        ScriptRun& r = *runs_[k];
        if (r.finished || r.paused) continue;
        if (r.waitFrames > 0) {
            --r.waitFrames;
            if (r.waitFrames > 0) continue;
        }
        if (r.waitUntilMs >= 0) {
            if (clockMs_ < r.waitUntilMs) continue;
            r.waitUntilMs = -1;
        }
        if (r.waitFor) {
            if (!r.waitFor()) continue;
            r.waitFor = nullptr;
        }
        if (!step(r, 100000)) r.finished = true;
    }
    runs_.erase(std::remove_if(runs_.begin(), runs_.end(), [](const std::unique_ptr<ScriptRun>& r) { return r->finished; }),
                runs_.end());
}

void Vm::kill(uint32_t id) {
    for (auto& r : runs_)
        if (r->id == id) r->finished = true;
}

void Vm::killByName(uint32_t name) {
    for (auto& r : runs_)
        if (r->name == name) r->finished = true;
}

bool Vm::running(uint32_t name) const {
    for (auto& r : runs_)
        if (r->name == name && !r->finished) return true;
    return false;
}

bool Vm::callForValue(ScriptRun& run, uint32_t name, qb::Struct& params) {
    auto nat = natives_.find(name);
    if (nat != natives_.end()) {
        ++stats_.nativeCalls;
        return nat->second(*this, run, params);
    }
    if (auto c = code(name)) {
        // A script in a condition runs to its end at once [I]; a wait inside
        // it is ignored (logged).
        ScriptRun sub;
        sub.id = 0;
        sub.name = name;
        Frame f;
        f.code = std::move(c);
        f.locals = std::make_shared<qb::Struct>(params);
        sub.frames.push_back(std::move(f));
        if (step(sub, 1 << 16)) logOnce("WAIT inside a script used as a condition: " + qbkey::name(name));
        return true;
    }
    if (unhandledNames_.insert(name).second) {
        stats_.unhandled = unhandledNames_.size();
        logOnce("UNHANDLED call " + qbkey::name(name));
    }
    return false;
}

bool Vm::evalCondition(Exec& ex) {
    bool negate = false;
    ex.skipNops();
    while (ex.i < ex.t.size() && ex.t[ex.i].op == kNot) {
        negate = !negate;
        ++ex.i;
        ex.skipNops();
    }
    bool value;
    const size_t at = ex.i;
    if (at < ex.t.size() && ex.t[at].op == kName && (natives_.count(ex.t[at].u) || (g_.find(ex.t[at].u) &&
                                                     g_.find(ex.t[at].u)->type == qb::Type::Script))) {
        ++ex.i;
        qb::Struct p = ex.params();
        ++stats_.calls;
        value = callForValue(ex.run, ex.t[at].u, p);
    } else {
        value = truth(ex.expression());
    }
    // The rest of the line belongs to the condition.
    while (ex.i < ex.t.size() && !isEnd(ex.t[ex.i].op)) ++ex.i;
    return negate ? !value : value;
}

// Runs lines until the script waits (true) or ends (false).
bool Vm::step(ScriptRun& run, int budget) {
    while (budget-- > 0) {
        if (run.finished) return false;
        if (run.frames.empty()) return false;
        if (run.waitFrames > 0 || run.waitUntilMs >= 0 || run.waitFor) return true;
        Frame& fr = run.frames.back();
        const auto& t = fr.code->tokens;
        if (fr.pc >= t.size()) {
            run.frames.pop_back();
            continue;
        }
        const Token& k = t[fr.pc];
        auto jumpTo = [&](uint32_t target) {
            // Targets are byte offsets just past a token (or on an elseif).
            for (size_t n = 0; n < t.size(); ++n)
                if (t[n].at == target) {
                    fr.pc = n;
                    fr.arrivedByJump = true;
                    return;
                }
            fr.pc = t.size();
            logOnce("bad jump in " + qbkey::name(fr.code->name));
        };
        const bool byJump = fr.arrivedByJump;
        fr.arrivedByJump = false;
        switch (k.op) {
        case kNop:
        case kEndOfLine:
        case kEndOfLineNumber:
        case kEndIf:
        case kEndSwitch:
        case kDebugInfo:
            ++fr.pc;
            continue;
        case kEndScript: {
            std::shared_ptr<qb::Struct> done = fr.locals;
            run.frames.pop_back();
            (void)done;
            if (run.frames.empty()) return false;
            ++stats_.lines;
            continue;
        }
        case kReturn: {
            Exec ex(*this, run);
            ++ex.i;
            qb::Struct ret = ex.params();
            run.frames.pop_back();
            if (run.frames.empty()) return false;
            Frame& caller = run.frames.back();
            if (!caller.locals) caller.locals = std::make_shared<qb::Struct>();
            for (qb::Item& it : ret.items) set(*caller.locals, it.name, std::move(it.value));
            continue;
        }
        case kFastIf: {
            Exec ex(*this, run);
            ++ex.i;
            const bool c = evalCondition(ex);
            if (c) fr.pc = ex.i;
            else jumpTo(k.at + 1 + k.u);
            ++stats_.lines;
            continue;
        }
        case kFastElse:
            jumpTo(k.at + 1 + k.u);
            continue;
        case kElseIf: {
            if (!byJump) {
                jumpTo(k.at + 3 + k.u2);  // the branch before ran: past the endif
                continue;
            }
            Exec ex(*this, run);
            ++ex.i;
            const bool c = evalCondition(ex);
            if (c) fr.pc = ex.i;
            else jumpTo(k.at + 1 + k.u);
            continue;
        }
        case kShortJump:
            jumpTo(k.at + 1 + k.u);
            continue;
        case kJump:
            jumpTo(k.at + 1 + k.u);
            continue;
        case kBegin:
            fr.loops.push_back(Frame::Loop{fr.pc + 1, -2});
            ++fr.pc;
            continue;
        case kRepeat: {
            Exec ex(*this, run);
            ++ex.i;
            int64_t count = -1;
            if (!ex.atEnd()) {
                qb::Value v = ex.expression();
                if (v.type == qb::Type::Int || v.type == qb::Type::Float) count = int64_t(v.number());
                else {
                    // A count that is not a number ends the loop [I].
                    logOnce("repeat count is not a number in " + qbkey::name(fr.code->name));
                    count = 1;
                }
            }
            if (fr.loops.empty()) {
                fr.pc = ex.i;
                continue;
            }
            Frame::Loop& lp = fr.loops.back();
            if (lp.remaining == -2) lp.remaining = count < 0 ? -1 : count;
            if (lp.remaining > 0) --lp.remaining;
            if (lp.remaining == 0) {
                fr.loops.pop_back();
                fr.pc = ex.i;
            } else {
                fr.pc = lp.start;
                // An endless loop without a wait would hang: the budget ends it.
            }
            ++stats_.lines;
            continue;
        }
        case kBreak: {
            // Leave the innermost loop: skip to the token after its repeat line.
            int depth = 0;
            size_t n = fr.pc + 1;
            for (; n < t.size(); ++n) {
                if (t[n].op == kBegin) ++depth;
                else if (t[n].op == kRepeat) {
                    if (depth == 0) break;
                    --depth;
                }
            }
            if (!fr.loops.empty()) fr.loops.pop_back();
            fr.pc = n;
            while (fr.pc < t.size() && !isEnd(t[fr.pc].op)) ++fr.pc;
            continue;
        }
        case kSwitch: {
            Exec ex(*this, run);
            ++ex.i;
            qb::Value v = ex.expression();
            // Walk the cases: `case shortjump(+n) <value>` ... `default`.
            size_t n = ex.i;
            bool matched = false;
            while (n < t.size()) {
                if (t[n].op == kCase) {
                    // case, then a short jump to the next case, then the value.
                    size_t vpos = n + 1;
                    uint32_t nextCase = 0;
                    if (vpos < t.size() && t[vpos].op == kShortJump) {
                        nextCase = t[vpos].at + 1 + t[vpos].u;
                        ++vpos;
                    }
                    Exec cv(*this, run);
                    cv.i = vpos;
                    qb::Value c = cv.expression();
                    bool eq = false;
                    if (c.type == v.type || ((c.type == qb::Type::Int || c.type == qb::Type::Float) &&
                                             (v.type == qb::Type::Int || v.type == qb::Type::Float))) {
                        Exec tmp(*this, run);
                        eq = truth(tmp.binary(kSameAs, v, c));
                    }
                    if (eq) {
                        fr.pc = cv.i;
                        matched = true;
                        break;
                    }
                    if (!nextCase) break;
                    size_t m = n;
                    for (; m < t.size(); ++m)
                        if (t[m].at == nextCase) break;
                    // The jump lands after the short jump that ends the previous case body.
                    n = m;
                    continue;
                }
                if (t[n].op == kDefault) {
                    fr.pc = n + 1;
                    if (n + 1 < t.size() && t[n + 1].op == kShortJump) fr.pc = n + 2;
                    matched = true;
                    break;
                }
                if (t[n].op == kEndSwitch) {
                    fr.pc = n + 1;
                    matched = true;
                    break;
                }
                ++n;
            }
            if (!matched) fr.pc = n;
            ++stats_.lines;
            continue;
        }
        case kCase:
        case kDefault:
            // Falling into the next case: a body ended without its jump [I].
            ++fr.pc;
            continue;
        default:
            break;
        }
        // An ordinary line: an assignment or a call.
        ++stats_.lines;
        Exec ex(*this, run);
        ex.skipNops();
        const Token& first = t[ex.i];
        if (first.op == kArg || (first.op == kName && ex.i + 1 < t.size() && t[ex.i + 1].op == kEquals)) {
            // <arg> x = value / x = value: sets a local.
            uint32_t name = 0;
            if (first.op == kArg) {
                ++ex.i;
                ex.skipNops();
                if (ex.i < t.size() && t[ex.i].op == kName) name = t[ex.i++].u;
            } else {
                name = first.u;
                ++ex.i;
            }
            ex.skipNops();
            if (ex.i < t.size() && t[ex.i].op == kEquals) {
                ++ex.i;
                qb::Value v = ex.expression();
                if (!fr.locals) fr.locals = std::make_shared<qb::Struct>();
                set(*fr.locals, name, std::move(v));
            } else if (first.op == kArg) {
                // `<arg> f params...`: calls the script / native the local names
                // [C data: start_flow_manager's `<arg> create_func player = ...`].
                const qb::Value* fn = ex.local(name);
                if (fn && (fn->type == qb::Type::Key || fn->type == qb::Type::KeyPtr)) {
                    const uint32_t callee = fn->key;
                    qb::Struct p = ex.params();
                    while (ex.i < t.size() && !isEnd(t[ex.i].op)) ++ex.i;
                    fr.pc = ex.i;
                    bool result = false;
                    callName(run, callee, p, result);
                    continue;
                }
                logOnce("UNHANDLED call through a non-key local in " + qbkey::name(fr.code->name));
            } else {
                logOnce("UNHANDLED statement in " + qbkey::name(fr.code->name));
            }
            while (ex.i < t.size() && !isEnd(t[ex.i].op)) ++ex.i;
            fr.pc = ex.i;
            continue;
        }
        if (first.op == kInlineStruct || first.op == kInlineStruct2) {
            // A line that is only a struct: the script's default parameters,
            // kept where the caller gave none [I: setup_setlisttags etc.].
            if (!fr.locals) fr.locals = std::make_shared<qb::Struct>();
            for (qb::Item it : g_.expand(*first.st)) {
                if (it.value.type == qb::Type::KeyPtr)
                    if (const qb::Value* gv = global(it.value.key)) it.value = *gv;
                if (it.name && param(*fr.locals, it.name)) continue;
                if (it.name) set(*fr.locals, it.name, std::move(it.value));
                else fr.locals->items.push_back(std::move(it));
            }
            fr.pc = ex.i + 1;
            continue;
        }
        if (first.op == kName) {
            ++ex.i;
            qb::Struct p = ex.params();
            while (ex.i < t.size() && !isEnd(t[ex.i].op)) ++ex.i;
            fr.pc = ex.i;  // continue after the line when the call returns
            bool result = false;
            const size_t depth = run.frames.size();
            callName(run, first.u, p, result);
            (void)depth;
            continue;
        }
        logOnce("UNHANDLED line start token " + std::to_string(int(first.op)) + " in " + qbkey::name(fr.code->name));
        while (ex.i < t.size() && !isEnd(t[ex.i].op)) ++ex.i;
        fr.pc = ex.i < fr.pc + 1 ? fr.pc + 1 : ex.i;
    }
    return true;  // out of budget: treat as waiting a frame
}

}  // namespace qs
