// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "script/tasks.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <stdexcept>

namespace {

std::string varName(const std::string& s) { return !s.empty() && s[0] == '$' ? s.substr(1) : s; }

}  // namespace

// What a {script_task ...} / {thread_task ...} asks for.
struct TaskMgr::Spec {
    bool thread = false;
    int units = kTaskSeconds;  // [I] the default when (units ...) is absent
    double delay = 0;
    bool once = true;
    std::string name;
    std::vector<const DataNode*> body;
    std::vector<std::string> preserve;
    ScriptObject* self = nullptr;
};

// A running task; also the $task object its script talks to.
struct TaskMgr::Task : ScriptObject {
    Task(TaskMgr& m, const Spec& s) : ScriptObject(s.name.empty() ? "task" : s.name, s.thread ? "ThreadTask" : "ScriptTask"), mgr(m) {
        thread = s.thread;
        units = std::clamp(s.units, 0, kTaskNumUnits - 1);
        once = s.once;
        body = s.body;
        self = s.self;
        registered = s.name;
    }
    TaskMgr& mgr;
    bool thread = false, once = true, dead = false;
    int units = kTaskSeconds;
    double wake = 0;    // due time in its units
    double cursor = 0;  // a thread's time while it runs (sleeps add up from it)
    bool resumeAtWake = false;
    double deadline = -1;  // wait_timeout
    std::vector<const DataNode*> body;
    std::vector<std::pair<std::string, Value>> vars;  // preserved variables, swapped in while it runs
    ScriptObject* self = nullptr;
    std::string registered;  // the name it answers to as an object
    size_t index = 0;
    // What the current statement asked for.
    bool sleepReq = false, waitHold = false, loopReq = false, exitReq = false;

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& a, Value& r) override {
        r = Value::integer(0);
        auto num = [&](size_t k) { return k < a.size() ? double(in.evalValue(a[k]).number()) : 0.0; };
        if (msg == "sleep") {
            wake = cursor + std::max(0.0, num(0));
            cursor = wake;
            resumeAtWake = true;
            sleepReq = true;
            return true;
        }
        if (msg == "wait") {
            waitHold = !a.empty() && in.evalValue(a[0]).truthy();
            if (waitHold) resumeAtWake = false;
            return true;
        }
        if (msg == "wait_timeout") {  // wait_timeout C T [$var]
            double now = mgr.time(units);
            if (deadline < 0) deadline = now + num(1);
            bool timedOut = now >= deadline;
            waitHold = !timedOut && !a.empty() && in.evalValue(a[0]).truthy();
            if (waitHold) {
                resumeAtWake = false;
            } else {
                deadline = -1;
                if (a.size() > 2) in.var(varName(a[2].str())) = Value::integer(timedOut ? 1 : 0);
            }
            return true;
        }
        if (msg == "loop") return loopReq = true, true;
        if (msg == "exit") return exitReq = true, true;
        if (msg == "set_name") {
            std::string n = a.empty() ? "" : a[0].str();
            if (!registered.empty()) in.unregisterName(registered, this);
            registered = n;
            rename(n.empty() ? "task" : n);
            if (!n.empty()) in.registerAs(n, this);
            return true;
        }
        if (msg == "units") return r = Value::integer(units), true;
        if (msg == "time") return r = Value::real(float(mgr.time(units))), true;
        return false;
    }
};

namespace {

// taskmgr: the script's view of the clocks.
class TaskMgrObject : public ScriptObject {
public:
    explicit TaskMgrObject(TaskMgr& m) : ScriptObject("taskmgr", "TaskMgr"), mgr_(m) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& a, Value& r) override {
        if (msg == "seconds") return r = Value::real(float(mgr_.time(kTaskSeconds))), true;
        if (msg == "beat" || msg == "beats") return r = Value::real(float(mgr_.time(kTaskBeats))), true;
        if (msg == "ui_seconds") return r = Value::real(float(mgr_.time(kTaskUISeconds))), true;
        if (msg == "tutorial_seconds") return r = Value::real(float(mgr_.time(kTaskTutorialSeconds))), true;
        if (msg == "time") return r = Value::real(float(mgr_.time(a.empty() ? 0 : a[0].integerValue()))), true;
        if (msg == "clear_tasks") return mgr_.clear(), r = Value::integer(0), true;
        return false;
    }

private:
    TaskMgr& mgr_;
};

}  // namespace

TaskMgr::TaskMgr(Interp& in) : in_(in), object_(std::make_unique<TaskMgrObject>(*this)) { in_.registerObject(object_.get()); }

TaskMgr::~TaskMgr() = default;

void TaskMgr::setTime(int units, double now) {
    if (units >= 0 && units < kTaskNumUnits) now_[units] = now;
}

double TaskMgr::time(int units) const { return units >= 0 && units < kTaskNumUnits ? now_[units] : 0; }

size_t TaskMgr::count() const {
    return size_t(std::count_if(tasks_.begin(), tasks_.end(), [](const auto& t) { return !t->dead; }));
}

bool TaskMgr::isTask(const ScriptObject* o) const {
    return std::any_of(tasks_.begin(), tasks_.end(), [&](const auto& t) { return t.get() == o; });
}

void TaskMgr::finish(Task& t) {
    if (t.dead) return;
    t.dead = true;
    if (!t.registered.empty()) in_.unregisterName(t.registered, &t);
}

bool TaskMgr::kill(ScriptObject* o) {
    for (auto& t : tasks_)
        if (t.get() == o) {
            if (t->dead) return false;
            finish(*t);
            return true;
        }
    return false;
}

void TaskMgr::killFor(const ScriptObject* self) {
    for (auto& t : tasks_)
        if (t->self == self) finish(*t);
}

void TaskMgr::clear() {
    for (auto& t : tasks_) finish(*t);
}

ScriptObject* TaskMgr::start(const Spec& s) {
    auto t = std::make_unique<Task>(*this, s);
    t->wake = time(t->units) + s.delay;
    t->resumeAtWake = true;
    t->vars.push_back({"this", Value::obj(s.self)});
    for (const std::string& p : s.preserve) t->vars.push_back({p, in_.var(p)});
    if (!s.name.empty()) {
        // A new task under a live task's name replaces the name (DELETE_TASK runs first in GH2's scripts).
        in_.registerAs(s.name, t.get());
    }
    tasks_.push_back(std::move(t));
    return tasks_.back().get();
}

namespace {

void readSpec(Interp& in, const DataNode& cmd, size_t first, std::vector<const DataNode*>& body, int& units, double& delay,
              bool& once, std::string& name, std::vector<std::string>& preserve) {
    for (size_t k = first; k < cmd.children.size(); ++k) {
        const DataNode& a = cmd.children[k];
        if (a.type != DataNode::Type::Array || a.children.empty() || !a.children[0].isText()) continue;
        const std::string& key = a.children[0].s;
        auto value = [&] { return a.children.size() > 1 ? in.eval(a.children[1]) : Value::integer(0); };
        if (key == "script") {
            for (size_t n = 1; n < a.children.size(); ++n) body.push_back(&a.children[n]);
        } else if (key == "units") {
            Value v = value();
            if (v.isNumber()) units = v.integerValue();
            else if (v.str() == "kTaskBeats") units = kTaskBeats;
            else if (v.str() == "kTaskUISeconds") units = kTaskUISeconds;
            else if (v.str() == "kTaskTutorialSeconds") units = kTaskTutorialSeconds;
            else units = kTaskSeconds;
        } else if (key == "delay") {
            delay = value().number();
        } else if (key == "once") {
            once = value().truthy();
        } else if (key == "name") {
            name = a.children.size() > 1 ? (a.children[1].isText() ? a.children[1].s : in.eval(a.children[1]).str()) : "";
        } else if (key == "preserve") {
            for (size_t n = 1; n < a.children.size(); ++n)
                if (a.children[n].type == DataNode::Type::Var) preserve.push_back(varName(a.children[n].s));
        }
    }
}

}  // namespace

ScriptObject* TaskMgr::scriptTask(const DataNode& cmd, size_t first) {
    Spec s;
    readSpec(in_, cmd, first, s.body, s.units, s.delay, s.once, s.name, s.preserve);
    s.self = in_.var("this").object;
    return start(s);
}

ScriptObject* TaskMgr::threadTask(const DataNode& cmd, size_t first) {
    Spec s;
    s.thread = true;
    readSpec(in_, cmd, first, s.body, s.units, s.delay, s.once, s.name, s.preserve);
    s.self = in_.var("this").object;
    return start(s);
}

ScriptObject* TaskMgr::threadTaskFromCode(const std::vector<Value>& args, int units, ScriptObject* self) {
    Spec s;
    s.thread = true;
    s.units = units;
    s.self = self;
    for (const Value& v : args)
        if (v.type == Value::Type::Code && v.code) s.body.push_back(v.code);
    return start(s);
}

void TaskMgr::poll() {
    if (polling_) return;
    polling_ = true;
    size_t n = tasks_.size();  // tasks made during this poll start on the next
    for (size_t k = 0; k < n && k < tasks_.size(); ++k) {
        Task& t = *tasks_[k];
        if (t.dead) continue;
        double now = time(t.units);
        if (t.wake > now) continue;
        run(t, now);
    }
    polling_ = false;
    // Finished tasks are dropped; their objects may still be in a variable
    // ($task after the run), so they are kept until the next poll.
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [](const std::unique_ptr<Task>& t) { return t->dead && t->index == size_t(-1); }),
                 tasks_.end());
    for (auto& t : tasks_)
        if (t->dead) t->index = size_t(-1);
}

void TaskMgr::run(Task& t, double now) {
    std::vector<std::pair<std::string, Value>> saved;
    saved.push_back({"task", in_.var("task")});
    for (const auto& [name, value] : t.vars) {
        saved.push_back({name, in_.var(name)});
        in_.var(name) = value;
    }
    in_.var("task") = Value::obj(&t);
    try {
        if (!t.thread) {
            Value last = Value::integer(0);
            for (const DataNode* n : t.body) {
                last = in_.eval(*n);
                if (t.dead) break;
            }
            if (t.once || !last.truthy()) finish(t);
            else t.wake = now;
        } else {
            t.cursor = t.resumeAtWake ? t.wake : now;
            t.resumeAtWake = false;
            int guard = 0;
            while (!t.dead && t.index < t.body.size()) {
                t.sleepReq = t.waitHold = t.loopReq = t.exitReq = false;
                in_.eval(*t.body[t.index]);
                if (t.dead) break;
                if (t.exitReq) {
                    finish(t);
                    break;
                }
                if (t.loopReq) {
                    t.index = 0;
                } else if (t.waitHold) {
                    t.wake = now;
                    break;
                } else {
                    ++t.index;
                }
                if (t.sleepReq) break;
                if (++guard > 10000) {  // a loop without a sleep: resume next poll
                    t.wake = now;
                    break;
                }
            }
            if (!t.dead && t.index >= t.body.size()) finish(t);
        }
    } catch (const std::exception& ex) {
        in_.onError(std::string("task ") + t.name() + ": " + ex.what());
        finish(t);
    }
    for (auto& [name, value] : t.vars) value = in_.var(name);  // the task keeps its own copies
    for (auto it = saved.rbegin(); it != saved.rend(); ++it) in_.var(it->first) = it->second;
}
