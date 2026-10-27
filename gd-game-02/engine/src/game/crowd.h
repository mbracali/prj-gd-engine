// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/crowd_audio.h"
#include "char/character.h"
#include "script/random_gen.h"
#include "scene/milo.h"
#include "scene/world_objects.h"
#include "script/script.h"

class World;

// The venue's audience (docs/venues.md "Crowd"): GH2's WorldCrowd at run
// time, its script type (world_objects.dta WorldCrowd gh2 = crowd.dta), its
// archetype characters and the crowd audio. Drawing is render/crowd_view.h's.
//
// A WorldCrowd has archetypes (char/crowd/og/<name>.milo: a Character of
// type crowd with its skinned meshes, bones, hand variants, lighter flame
// and main.drv on the crowd clip set) and per archetype its placements.
// GH2 keeps ONE Character per archetype [C 0x82247310]: every placement of
// an archetype shows the same pose. Each frame (WorldCrowd draw 0x82247310)
// the archetype is rendered into its card texture (render/crowd_view.h) and
// drawn on the shown placements as cards (a RndMultiMesh of one quad), then
// drawn again in 3D at each of the shot's crowd3d placements.
//
//   set_fullness a b (0x82247C08): per archetype the first int(n2d x a)
//     card placements show (the rest wait in a spare list) and the first
//     int(n3d x b) 3D ones;
//   the shot's crowd3d pairs (archetype, card index) (CamShot StartAnim
//     0x82226120 -> 0x82247FD0): the cards go back, then those placements
//     move from the cards to the 3D list; fullness re-applied;
//   show_3d_only: the cards hidden and not rendered (crowd_hide);
//   rotate (the shot's crowd_face_camera): the archetype turns so its +y
//     points back along the camera's forward axis, z up (0x82247310);
//     otherwise it takes the placement mesh's rotation;
//   a placement's z is the character's middle: the character stands at
//     z - height / 2 (0x82247310) [C data: 47.5 = 95 / 2];
//   iterate_frac (frac script)... (0x822452E0): the archetypes shuffled,
//     the fractions scaled to their count and run in turn with $this = the
//     archetype (an accumulator from -0.5), `main.drv` its CharDriver
//     (play_group bad / ok / great / idle / lighter_slow / lighter_fast
//     with crowd.dta's play flags; set beat_scale from crowd_*_tempo).
//   The archetype's clips switch its hands ({$dude set_hand clap | devil |
//     fist | lighter}: char_objects.dta Character type crowd), lighter shows
//     lighter_flame.
//
// The world forwards crowd.dta's sinks (intro_start_msg, crowd_update,
// excitement, crowd_hide, crowd_lighters_*, crowd_*_tempo) once the
// WorldCrowd's `enter` asks for them ({$this dir} add_sinks ...). The
// game's excitement (Game +0x338, the world's `excitement`) is kept in
// [excitement_level]; every shot's start_shot runs crowd_update, which picks
// the fullness and the clip mix for that level.
//
// The camera director calls startShot(shot) as a shot starts (CamShot
// StartAnim's native part), then sends the shot `start_shot` (camshot.dta:
// {[crowd] crowd_update} and {[crowd] set rotate [crowd_face_camera]};
// a CamShot's [crowd] answers the WorldCrowd here).
//
// The crowd draws its random numbers (the shuffles, the drivers' clip
// picks, its scripts while an archetype is $this) from its own RandomGen
// (GH2 has one global generator, script/random_gen.h), so the band's, the
// director's and the world's picks do not move with it.
class Crowd {
public:
    explicit Crowd(World& world);
    ~Crowd();
    Crowd(const Crowd&) = delete;
    Crowd& operator=(const Crowd&) = delete;

    // An archetype's character: the <v>_chars proxy the WorldCrowd names
    // (CharData +0x10, ObjPtr<Character>; proxy file ../../../char/crowd/og/
    // <name>.milo). Crowds naming the same archetype share it [C data: fest,
    // stone].
    struct Actor {
        std::string name;               // crowd_male01
        std::unique_ptr<chr::Character> character;  // null when it does not load
        const scene::Environ* env = nullptr;  // the proxy's environ (none: the crowd's)
        std::string group = "ok";       // the clip group main.drv last played
        float beatScale = 1;
        std::string hand = "clap";      // set_hand's [hand]
        bool lighter = false;           // lighter_flame started
        int clipsStarted = 0;
        std::unique_ptr<ScriptObject> object;  // $this / $dude
        std::vector<std::unique_ptr<ScriptObject>> parts;  // its directory's objects (hand meshes...)
    };
    struct Archetype {
        std::string name;               // the proxy's name (crowd_male01)
        Actor* actor = nullptr;         // null when it does not load
        scene::Milo* milo = nullptr;    // the character's milo (null: not loaded)
        float height = 0;
        std::vector<scene::Xfm> placements;
        std::vector<int> cards;  // placement indices drawn as cards, in list order
        std::vector<int> threeD; // the shot's 3D placements
    };
    struct State {
        const scene::WorldCrowd* crowd = nullptr;
        std::vector<Archetype> archetypes;
        float full2d = 1, full3d = 1;
        bool show3dOnly = false, rotate = false;
        ScriptObject* script = nullptr;
        // The placements shown: archetype, placement.
        std::vector<std::pair<int, int>> shown() const;
        int shownCount() const;
        int total() const;
    };
    const std::vector<State>& crowds() const { return crowds_; }
    State* find(const std::string& name);
    const std::vector<std::unique_ptr<Actor>>& actors() const { return actors_; }
    // Bumped whenever a crowd's shown set changes (render/crowd_view.h rebuilds).
    int revision() const { return revision_; }

    // The WorldCrowds' `enter` (crowd.dta: add_sinks); World::start calls it.
    void enter();
    // CamShot StartAnim's native part (0x82247FD0): the shot's crowd gets
    // its crowd3d pairs as its 3D list. `runScript`: also what camshot.dta's
    // start_shot does for the crowd (crowd_update, rotate) for a host that
    // does not send start_shot.
    void startShot(const scene::CamShot* shot, bool runScript = false);
    void setFullness(State& s, float a, float b);

    CrowdAudio& audio() { return *audio_; }
    // Each frame, on the world's clock: the crowd audio and the archetypes'
    // polls (drivers, servo) at the world's beat / seconds.
    void update();
    // Each clip an archetype's main.drv starts: (beat, archetype, clip).
    std::function<void(double beat, const std::string& archetype, const std::string& clip)> onClip;
    // Each hand change ({$dude set_hand}).
    std::function<void(double beat, const std::string& archetype, const std::string& hand)> onHand;
    RandomGen& random() { return rng_; }

private:
    bool objectNative(Interp& in, scene::Object& o, const std::string& msg, std::vector<Value>& args, Value& r);
    void iterateFrac(Interp& in, State& s, std::vector<Value>& args);
    void listen(Interp& in, const std::string& msg, const std::vector<Value>& args);
    void reactionSound(const std::string& name);
    Actor* actor(const std::string& name);
    void runClipScript(Actor& a, const std::string& script);
    class ArchetypeObj;
    class PartObj;
    friend class ArchetypeObj;
    friend class PartObj;
    bool attract_ = false;  // gamecfg mode attract: reactions at 0 dB (0x822DF140)

    World& world_;
    std::vector<State> crowds_;
    std::unique_ptr<CrowdAudio> audio_;
    std::unique_ptr<ScriptObject> audioObj_;
    ScriptObject* prevAudioObj_ = nullptr;
    std::vector<std::unique_ptr<ScriptObject>> owned_;
    std::unique_ptr<chr::ClipLibrary> clips_;
    std::vector<std::unique_ptr<Actor>> actors_;
    const DataNode* objects_ = nullptr;  // char/char_objects.dta (Character type crowd)
    std::map<std::string, DataNode> scripts_;  // parsed clip scripts
    RandomGen rng_{0x43524f57u};
    double seconds_ = 0, beats_ = 0;
    bool first_ = true, entered_ = false;
    int revision_ = 0;
    int excitement_ = 2;
    std::string reaction_;  // the reaction sound playing (0x822DF350 stops it for the next)
};
