// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "game/venue.h"
#include "render/scene_gpu.h"
#include "script/script.h"

struct DataNode;
class Crowd;

// A venue at run time, GH2's WorldDir (docs/venues.md): its clock, the
// animations its EventTriggers and scripts start, the EventTriggers
// themselves, and the `world` script object. Drawing is render/venue_view.h's.
//
// - Clock: the song's seconds (kTaskSeconds) and beats (kTaskBeats, from
//   the tempo map), set by the host every frame (setClock).
// - Animations (RndAnimatable::Animate / AnimTask) [I: the AnimTask fields
//   from GH2's EventTrigger and the `animate` script options; the frame
//   rules follow later Harmonix code]: an anim runs from its start to its end
//   frame (TransAnim / MatAnim / EnvAnim... keys; a Group: its members'; an
//   AnimFilter: (start - offset) / scale to (end - offset) / scale, endless
//   for loop / shuttle filters) at its rate: 0 k30_fps 30 frames a second,
//   1 k480_fpb 480 a beat, 2 k30_fps_ui, 3 k1_fpb 1 a beat [C data: rate
//   1 on the beat-driven anims; plan PLAN.md]. An AnimFilter maps its frame
//   to its anim's: f x scale + offset (scale = (end - start) / (period x
//   frames per unit) when it has a period), clamped (range), wrapped (loop)
//   or bounced (shuttle) into start..end. A new animation of an object
//   replaces the one running on it. Blending is not done (the new frames
//   apply at once).
// - EventTriggers (EventTrigger v10; registration 0x822578e8, fire
//   0x82259588 -> 0x82257d00 [C addresses, behaviour I]): each of the
//   chain's triggers answers its trigger_events; enable_events /
//   disable_events switch it on / off (on at first); with wait_for_events
//   a fired trigger waits for one of those. Firing plays its anims (with
//   their own filter when the entry enables one), shows objects, hides
//   others after their delays (seconds), calls its proxies (the message
//   goes to the proxy directory's script type and triggers), logs its
//   sounds (no sound banks yet) and fires next_random.
// - The `world` object: the venue's WorldDir type (world_objects.dta,
//   WORLD_OBJECT_BASE + world/<v>/<v>.dta) run by the interpreter, its
//   messages forwarded to the chain's RndDir types (lighting, chars, geom:
//   set_multiplayer...), then answered natively: the events above, plus the
//   director / lighting / crowd methods the next phase implements, which
//   answer quietly now and are reported as STUBs (L1). Venue objects are
//   script objects by name (sparks.filt animate, band_shadow1.mesh
//   set_showing, [env1] animate (dest 10) (period 0.01)...).
class World {
public:
    // `interp`: the UI's interpreter (the world's type comes from its boot
    // tree's (objects ...), `objects`); without one the world runs its own,
    // loading world/world_objects.dta.
    World(Venue& venue, Interp* interp = nullptr, const DataNode* objects = nullptr);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    Venue& venue() { return venue_; }
    const Venue& venue() const { return venue_; }
    gfx::ScenePose& pose() { return pose_; }
    const gfx::ScenePose& pose() const { return pose_; }
    Interp& interp() { return *interp_; }
    ScriptObject* script();  // the `world` object
    bool ownInterp() const { return own_ != nullptr; }

    // The clock, set before update(): song seconds and beats.
    void setClock(double seconds, double beats);
    double seconds() const { return seconds_; }
    double beats() const { return beats_; }
    // Runs the animations and delayed hides (and, with its own interpreter,
    // the script tasks).
    void update();

    // A world message from the game ({handle (world <msg> args...)}): the
    // script, then the natives (EventTriggers...). Returns the answer.
    Value message(const std::string& msg, std::vector<Value> args = {});
    // Fires the EventTriggers registered for `event` (natives only).
    void fire(const std::string& event);
    // Native handlers other modules add (the camera director and lighting
    // presets, game/camera_director.h; the crowd): tried in order after the
    // events and before the stubs; true when handled (answer in `r`).
    using NativeHandler = std::function<bool(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r)>;
    void addNatives(NativeHandler h) { natives_.push_back(std::move(h)); }
    // The same for messages to venue objects (a CamShot's `get duration`, a
    // PostProc's `select`); property reads ([category], {o get k}) come as
    // msg "get" with the key as the one argument.
    using ObjectHandler = std::function<bool(Interp& in, scene::Object& o, const std::string& msg, std::vector<Value>& args, Value& r)>;
    void addObjectNatives(ObjectHandler h) { objectNatives_.push_back(std::move(h)); }
    // Observers of every message that reaches the world's natives (its type
    // left it unhandled): the crowd audio (game/crowd.h) follows
    // intro_start_msg, excitement, music_start, game_won_msg... They run
    // first and do not answer.
    using Listener = std::function<void(Interp& in, const std::string& msg, const std::vector<Value>& args)>;
    void addListener(Listener l) { listeners_.push_back(std::move(l)); }
    // Message sinks ({$this dir} add_sinks $this msg...; MsgSource [I]): the
    // world forwards those messages, once its type has left them unhandled,
    // to the sink (the WorldCrowd's crowd.dta: intro_start_msg, crowd_update,
    // excitement, crowd_hide, crowd_lighters_*, crowd_*_tempo).
    void addSink(ScriptObject* sink, const std::vector<std::string>& messages);
    // The audience and its audio (game/crowd.h).
    Crowd& crowd() { return *crowd_; }
    // The script object of a venue object (created on first use).
    ScriptObject* scriptFor(scene::Object* o) { return objectFor(o); }
    // The world panel entered (WORLD_OBJECT_BASE enter + `start`): call
    // once the venue is up; with `players`, then single_player (or
    // multi_player), as world_panel's enter script sends.
    void start(bool multiplayer, bool players = true);

    // Animations.
    struct AnimArgs {
        float blend = 0, delay = 0;
        bool wait = false;
        bool filter = false;     // explicit range below (an EventTrigger filter, `animate (range a b)`)
        int rate = -1;           // -1: the anim's own
        float start = 0, end = 0, scale = 1, period = 0;
        int type = 0;            // 0 range, 1 loop, 2 shuttle
        bool fromCurrent = false;  // `(dest d)`: from the current frame to `end`
        int units = -1;          // kTaskSeconds / kTaskBeats override (-1: from the rate)
    };
    void animate(const scene::Object* anim, const AnimArgs& args);
    void stopAnimation(const scene::Object* anim);
    bool animating(const scene::Object* anim) const;
    int runningAnims() const { return int(tasks_.size()); }
    // Sets an anim's frame now (AnimFilter frames go through the filter).
    void setFrame(const scene::Object* anim, float frame);
    float currentFrame(const scene::Object* anim) const;

    // An EventTrigger by name (or nullptr) and firing it directly.
    void trigger(const scene::EventTrigger* t);
    void setTriggerEnabled(const scene::EventTrigger* t, bool on);

    // Shows / hides a drawable (Draw showing).
    void setShowing(const scene::Object* o, bool on) { pose_.setShowing(o, on); }

    // The lighting preset state applied as plain state (render reads it):
    // keyframe `k` of `p` on its spotlights, environs and lights.
    void applyPreset(const scene::LightPreset& p, size_t k);
    // Per spotlight: what the last preset set (the beams are the next phase's).
    struct SpotState {
        float intensity = 0;
        std::array<float, 4> color{};
        std::array<float, 4> rotation{};
        std::string target;
        bool flare = false;
    };
    const std::unordered_map<const scene::Spotlight*, SpotState>& spots() const { return spots_; }
    // A spotlight's state for writing (game/light_presets.h blends presets into it).
    SpotState& spotState(const scene::Spotlight* s) { return spots_[s]; }

    // The first shot of a category (INTRO first, else any) and the INTRO
    // lighting preset (else the first preset): the static look before the
    // director (next phase).
    const scene::CamShot* firstShot(const std::string& category = "INTRO") const;
    const scene::LightPreset* firstPreset(const std::string& category = "INTRO") const;
    std::vector<const scene::CamShot*> shots() const;

    // Statistics for the tests and logs.
    struct Stats {
        int eventsFired = 0, triggersFired = 0, animsStarted = 0, proxyCalls = 0, sounds = 0, shows = 0, hides = 0;
        std::map<std::string, int> events;  // world events fired
    };
    const Stats& stats() const { return stats_; }
    // Log the trigger activity (GD02_WORLDLOG=1).
    bool log = false;

    // Frames per unit of an Animatable rate, and the task units it runs in.
    static float framesPerUnit(int rate);
    static int unitsOf(int rate);

private:
    struct AnimTask {
        const scene::Object* anim = nullptr;
        double startTime = 0;   // in `units`
        int units = 0;          // kTaskSeconds / kTaskBeats
        float fpu = 30;
        float from = 0, to = 0, scale = 1;
        int type = 0;
        bool filtered = false;  // the task maps its own frames (from..to) like a filter
        bool started = false;
    };
    struct TriggerState {
        bool enabled = true;
        bool armed = false;  // fired, waiting for wait_for_events
    };
    struct DelayedHide {
        const scene::Object* object;
        double at;  // seconds
    };
    class WorldObj;
    class VenueObj;
    friend class WorldObj;
    friend class VenueObj;

    double now(int units) const;
    void runTask(AnimTask& t);
    float startFrameOf(const scene::Object* anim) const;
    float endFrameOf(const scene::Object* anim) const;
    int rateOf(const scene::Object* anim) const;
    void registerTriggers();
    void proxyCall(const std::string& proxy, const std::string& call);
    ScriptObject* objectFor(scene::Object* o);
    ScriptObject* resolveName(const std::string& name);
    void loadOwnScripts();
    void attach();
    Value native(const std::string& msg, std::vector<Value>& args, bool& handled);
    void showObjects(Interp& in, std::vector<Value>& args);

    Venue& venue_;
    gfx::ScenePose pose_;
    std::unique_ptr<Interp> own_;
    Interp* interp_ = nullptr;
    const DataNode* objects_ = nullptr;      // (objects ...) / world_objects.dta tree
    std::unique_ptr<WorldObj> world_;
    std::vector<std::unique_ptr<ScriptObject>> dirObjs_;  // the chain's RndDir types (lighting, chars, geom)
    std::unordered_map<const scene::Object*, std::unique_ptr<VenueObj>> objs_;
    std::function<ScriptObject*(const std::string&)> prevResolver_;
    ScriptObject* prevWorld_ = nullptr;
    double seconds_ = 0, beats_ = 0;
    std::vector<AnimTask> tasks_;
    std::unordered_map<const scene::Object*, float> frames_;  // last frame set per anim
    std::multimap<std::string, const scene::EventTrigger*> byEvent_, byEnable_, byDisable_, byWaitFor_;
    std::unordered_map<const scene::EventTrigger*, TriggerState> triggers_;
    std::vector<DelayedHide> hides_;
    std::unordered_map<const scene::Spotlight*, SpotState> spots_;
    Stats stats_;
    std::vector<NativeHandler> natives_;
    std::vector<ObjectHandler> objectNatives_;
    std::vector<Listener> listeners_;
    std::multimap<std::string, ScriptObject*> sinks_;
    std::unique_ptr<Crowd> crowd_;
};
