// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "audio/crowd_audio.h"
#include "scene/milo.h"
#include "scene/world_objects.h"
#include "script/script.h"

class World;

// The venue's audience (docs/venues.md "Crowd"): GH2's WorldCrowd at run
// time, its script type (world_objects.dta WorldCrowd gh2 = crowd.dta) and
// the crowd audio. Drawing is render/crowd_view.h's.
//
// A WorldCrowd has archetypes (char/crowd/og/<name>.milo: a Character, its
// skinned meshes and bones) and per archetype its placements. GH2 draws a
// placement either as a 2D card (a RndMultiMesh instance of a quad whose
// texture it renders from the archetype each frame, 0x82247310) or, for the
// shot's crowd3d pairs, as the 3D character; the port draws both as the 3D
// character (render/crowd_view.h).
//
//   set_fullness a b (0x82247C08): per archetype the first int(n2d x a)
//     card placements show (the rest wait in a spare list) and the first
//     int(n3d x b) 3D ones;
//   the shot's crowd3d pairs (archetype, card index) (CamShot StartAnim
//     0x82226120 -> 0x82247FD0): the cards go back, then those placements
//     move from the cards to the 3D list; fullness re-applied;
//   show_3d_only: the cards hidden (crowd_hide);
//   rotate (the shot's crowd_face_camera): each placement turns so its +y
//     points back along the camera's forward axis, z up (0x82247310);
//     otherwise the placement's own rotation;
//   a placement's z is the character's middle: the character stands at
//     z - height / 2 (0x82247310) [C data: 47.5 = 95 / 2];
//   iterate_frac (frac script)... (0x822452E0): the archetypes shuffled,
//     the fractions scaled to their count and run in turn with $this = the
//     archetype (an accumulator from -0.5), `main.drv` its CharDriver: the
//     clip group it plays (play_group bad / ok / great / idle /
//     lighter_slow / lighter_fast) and its beat_scale (crowd_*_tempo) are
//     kept per archetype; the clips are the band's work (M9), so the
//     characters stand in their bind pose.
//
// The world forwards crowd.dta's sinks (intro_start_msg, crowd_update,
// excitement, crowd_hide, crowd_lighters_*, crowd_*_tempo) once the
// WorldCrowd's `enter` asks for them ({$this dir} add_sinks ...).
//
// The camera director calls startShot(shot) as a shot starts (CamShot
// StartAnim's native part), then sends the shot `start_shot` (camshot.dta:
// {[crowd] crowd_update} and {[crowd] set rotate [crowd_face_camera]};
// a CamShot's [crowd] answers the WorldCrowd here).
class Crowd {
public:
    explicit Crowd(World& world);
    ~Crowd();
    Crowd(const Crowd&) = delete;
    Crowd& operator=(const Crowd&) = delete;

    struct Archetype {
        std::string name;
        scene::Milo* milo = nullptr;  // null when it does not load
        float height = 0;
        std::vector<scene::Xfm> placements;
        std::vector<int> cards;  // placement indices drawn as cards, in list order
        std::vector<int> threeD; // the shot's 3D placements
        std::string group = "ok";  // the clip group main.drv plays
        float beatScale = 1;
        std::unique_ptr<ScriptObject> object, driver;
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
    // Each frame, on the world's clock.
    void update();

private:
    bool objectNative(Interp& in, scene::Object& o, const std::string& msg, std::vector<Value>& args, Value& r);
    void iterateFrac(Interp& in, State& s, std::vector<Value>& args);
    void listen(Interp& in, const std::string& msg, const std::vector<Value>& args);

    World& world_;
    std::vector<State> crowds_;
    std::unique_ptr<CrowdAudio> audio_;
    std::unique_ptr<ScriptObject> audioObj_;
    ScriptObject* prevAudioObj_ = nullptr;
    std::vector<std::unique_ptr<ScriptObject>> owned_;
    int revision_ = 0;
    int excitement_ = 2;
};
