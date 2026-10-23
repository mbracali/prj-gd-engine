// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/dta.h"
#include "script/script.h"

class AnimPlayer;
class GameData;

// The arena's venue logic: the venue script (venues/<v>/gen/<v>.dta, its
// funcs and handlers), the global rules of arena/gen/venue.dta (lighting per
// crowd excitement and song section, applied on the next downbeat), the
// sections' animation views, and the switch_anim slots (FUN_0017a1d0 /
// FUN_0017a518). See FINDINGS.md "Venues".
//
// The script runs on the full interpreter (conditionals, switch, variables,
// random_int, funcs with arguments): `arena` is a native object (anim slots,
// delay_task, set_singer_env, the venue's handlers), `game anim_task` animates
// a frame range, `char_sys get_spot` and `arena cam_msg` answer from the
// venue, and every scene object is a script object (set_showing, set_frame,
// set_steps, remove_anim, add_trans, unhook_anim_parents). docs/venue_fx.md.
class VenueScript {
public:
    enum Excitement { Boot, Bad, Okay, Great, Peak };

    VenueScript(const GameData& data, const std::string& venue);
    ~VenueScript();

    struct Section {
        std::string name;  // views are <name>.view, <name>_rt.view, <name>_transparent.view
        std::string file;  // scene file name in the venue's gen folder
    };
    // The venue's own section plus those its script loads (e.g. lighting).
    const std::vector<Section>& sections() const { return sections_; }

    // What the script asks the venue about.
    std::function<int(const std::string& character)> getSpot;  // char_sys get_spot <char>
    std::function<std::string()> cameraShot;                     // arena cam_msg
    // flare set_steps n (docs/venue_fx.md §3).
    std::function<void(const std::string& flare, int steps)> setFlareSteps;

    // Binds the loaded scenes and runs finish_loading and intro_start.
    void start(AnimPlayer& anims, int excitement);

    // Advances to song time `ms` (= `ticks`): song events up to now,
    // excitement changes, downbeats, the section views, the anim slots and
    // tasks, then the transforms.
    void update(float ms, float ticks, int excitement, const std::vector<std::string>& events, bool downbeat);

    // Sends a message to the arena: the global rules, then the venue's handlers.
    void message(const std::string& name);
    // {arena hit_gem} with $slot = the gem's lowest fret (PlayerMatcher_v3).
    void hitGem(int slot);

    // Face-off: the lights stay on the Great preset (venue.dta downbeat).
    void setMultiplayer(bool on) { multiplayer_ = on; }
    // The environment lighting the band ({arena set_singer_env <env>}).
    const std::string& singerEnv() const { return singerEnv_; }

    // Crowd excitement for a rock meter level (FUN_0010d370).
    static int excitementFor(float rockMeter, float bootLevel);

private:
    class ArenaObj;
    class SceneProxy;

    // An arena anim slot (FUN_0017a078 layout).
    struct Slot {
        std::string anim;
        float min = 0, max = 0, prevMin = 0, prevMax = 0;
        float scale = 1, prevScale = 1, offset = 0, prevOffset = 0;
        float start = 0, blendEnd = 0;
        int mode = 0, prevMode = 0;  // 0 range (clamp), 1 loop, 2 shuttle
        bool rt = false;             // driven by song ms instead of ticks
    };
    struct Task {
        std::string anim;
        float from, to, t0, period;
    };
    struct Delayed {
        float tick;
        std::vector<const DataNode*> body;
    };

    bool handler(const std::string& name);
    void switchAnim(const std::vector<Value>& args, bool rt);
    void lightsPerExcitement(int excitement);
    float clock(bool rt) const { return rt ? ms_ : ticks_; }
    static float slotFrame(float t, float min, float max, float& scale, float& offset, int mode);

    std::string venue_;
    std::vector<Section> sections_;
    std::unique_ptr<ScriptLoader> loader_;
    DataNode script_;
    Interp interp_;
    std::unique_ptr<ArenaObj> arena_;
    std::unique_ptr<ScriptObject> game_, charSys_;
    std::map<std::string, std::unique_ptr<SceneProxy>> proxies_;

    AnimPlayer* anims_ = nullptr;
    std::map<std::string, Slot> slots_;
    std::vector<Task> tasks_;
    std::vector<Delayed> delayed_;
    float ms_ = 0, ticks_ = 0;
    int excitement_ = Okay, lastExcitement_ = Okay;
    std::string songPos_ = "verse";
    std::string singerEnv_;
    bool lightsNeedUpdate_ = false, multiplayer_ = false;
};
