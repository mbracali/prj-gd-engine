// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/anim.h"
#include "data/band.h"
#include "game/chart.h"
#include "render/venue_camera.h"

class Globals;

// A band member's animation (docs/band.md "Animation"): GH3's anim tree for
// the member (guitarist_static_tree, drummer_static_tree,
// vocalist_static_tree in guitar_anim_tree.qb) and the scripts that drive it
// (hero_play_adjusting_random_anims, Strum_iterator, FretPos_iterator,
// FretFingers_iterator, Drum_iterator, Drum_cymbal_iterator,
// facial_anim_loop, the song's <song>_performance events), reimplemented as
// a function of the song clock: every event is processed at its own song
// time, the random choices come from a per-member seed, so the pose at a
// song time does not depend on the frame rate.
namespace bandanim {

struct Quat {
    float x = 0, y = 0, z = 0, w = 1;
};
struct Vec {
    float x = 0, y = 0, z = 0;
};
struct Xf {  // a local transform: rotation then translation (column vectors)
    Quat q;
    Vec t;
};
struct Mat34 {  // rows of a 3x4 affine matrix, p' = M [p 1]
    float m[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
};

Quat qmul(const Quat& a, const Quat& b);
Quat qconj(const Quat& q);
Vec qrot(const Quat& q, const Vec& v);
Quat nlerp(const Quat& a, const Quat& b, float t);
Mat34 matOf(const Xf& x);
Mat34 mmul(const Mat34& a, const Mat34& b);
Vec xform(const Mat34& m, const Vec& p);
Vec xformDir(const Mat34& m, const Vec& d);

// A skeleton in column-vector form. The .ske stores rotations for row
// vectors (docs/formats.md "Animation"): a file quaternion q is the
// rotation conj(q) here; the inverse bind matrices are transposed.
struct Rig {
    std::vector<int> parent;
    std::vector<Xf> bind;
    std::vector<Mat34> invBind;
    std::unordered_map<uint32_t, int> index;
    void init(const anim::Skeleton& s);
    int find(uint32_t name) const {
        auto it = index.find(name);
        return it == index.end() ? -1 : it->second;
    }
    size_t size() const { return bind.size(); }
};

using Pose = std::vector<Xf>;

// One clip playing in a tree node: GH3's "play" timers hold the last frame [I].
struct Play {
    const anim::Clip* clip = nullptr;
    double start = 0;  // song ms
    float blend = 0;   // s: the degenerateblend's cross-fade from what played before [I: linear]
    double end() const { return start + (clip ? double(clip->duration) * 1000.0 : 0.0); }
};

// A degenerateblend node: the clips it played, the newest last.
struct Node {
    std::vector<Play> plays;
    void play(const anim::Clip* c, double t, float blend);
    bool empty() const { return plays.empty(); }
};

// A modulate node's strength over time (modulate_startblend / modulate_setstrength).
struct Ramp {
    float from = 1, to = 1;
    double start = 0, length = 0;
    float at(double t) const;
    void set(float target, double t, double seconds);
};

// A degenerateblend node's pose over the bind pose (crowd peds, render/crowd.cpp).
void evalBody(const Rig& rig, const Node& n, double t, Pose& pose);
// Model-space bone matrices of a local pose.
void worldMatrices(const Rig& rig, const Pose& pose, std::vector<Mat34>& world);

class Animator {
public:
    struct Setup {
        const band::Model* model = nullptr;
        const Globals* g = nullptr;
        const Chart* chart = nullptr;
        const venue::PerfTimeline* perf = nullptr;  // null: normal
        std::vector<const qb::Struct*> performance;  // the song's <song>_performance entries
        uint32_t seed = 1;
        double startMs = 0;  // when the member is created (the song's lead-in)
        bool guitarPart = true;  // the bassist plays the guitar part (song bassist Morello / Slash) [C script]
    };
    bool init(const Setup& s, std::string& error);
    // Processes every event up to song time `ms` (monotonic).
    void advance(double ms);
    // The player's judge at song time `ms` (after advance(ms)): a missed
    // note plays Bad_Strums once until the next hit (handle_missed_note,
    // handle_hit_note_CFunc) [C script; I: the hit clears the flag].
    void playerNote(double ms, bool hit);
    // The pose at `ms` (>= the last advance): local transforms, then the
    // model-space bone matrices (`world`) and the skinning matrices
    // (world x inverse bind).
    void evaluate(double ms, std::vector<Mat34>& world, std::vector<Mat34>& skin) const;
    const Rig& rig() const { return rig_; }

    struct Stats {
        int bodyAnims = 0, strums = 0, frets = 0, chords = 0, drumHits = 0, cymbals = 0, facials = 0, perfEvents = 0;
        int missingClips = 0;
    };
    const Stats& stats() const { return stats_; }
    // The clip names a member asked for and did not have (chartest).
    const std::vector<uint32_t>& missing() const { return missing_; }
    uint32_t stance() const { return stance_; }
    uint32_t bodyClip() const { return body_.plays.empty() || !body_.plays.back().clip ? 0 : bodyKey_; }

private:
    enum class Ev : uint8_t {
        Strum, Fret, Chord, Perf, DrumArm, Cymbal,                                       // from the song
        BodyDone, TransitionDone, StrumDone, FaceDone, ChordRelease, ArmDone, Twist  // scheduled
    };
    struct Event {
        double t = 0;
        uint64_t seq = 0;
        Ev kind = Ev::Strum;
        int a = 0, b = 0;
        uint32_t gen = 0;
        const qb::Struct* params = nullptr;
        bool operator>(const Event& o) const { return t != o.t ? t > o.t : seq > o.seq; }
    };
    void schedule(double t, Ev kind, int a = 0, int b = 0, uint32_t gen = 0, const qb::Struct* params = nullptr);
    void handle(const Event& e);
    uint32_t random(uint32_t n);
    const anim::Clip* clip(uint32_t key);
    const qb::Value* animSet() const;
    const anim::Clip* tempoAnim(uint32_t anim, double t, uint32_t& key);
    void bodyIteration(double t);
    void bodyFinished(double t);
    void afterStance(double t, bool stanceChanged);
    void setArms(int disable, double t, float blend);
    void playAnimEvent(const qb::Struct& p, double t);
    void changeStanceEvent(uint32_t stance, bool noWait, double t);
    void strum(int length, double t);
    void facial(double t);
    bool guitarLike() const { return role_ == band::Role::Guitarist || role_ == band::Role::Bassist; }

    Rig rig_;
    const band::Model* model_ = nullptr;
    const Globals* g_ = nullptr;
    const venue::PerfTimeline* perf_ = nullptr;
    band::Role role_ = band::Role::Guitarist;
    std::vector<int32_t> fretbars_;
    std::vector<Event> fixed_;  // the song's events, sorted
    size_t fixedAt_ = 0;
    std::priority_queue<Event, std::vector<Event>, std::greater<Event>> queue_;
    uint64_t seq_ = 0;
    uint64_t rng_ = 1;
    double now_ = -1e18;
    Stats stats_;
    std::vector<uint32_t> missing_;

    // The info struct (guitarist_info, drummer_info, ...) [C script guitar_character.qb].
    uint32_t stance_ = 0, nextStance_ = 0, currentAnim_ = 0, nextAnim_ = 0, lastAnim_ = 0;
    bool cycle_ = true, cycleNext_ = true;
    int repeat_ = 1, nextRepeat_ = 1, disableArms_ = 0, armsDisabled_ = 0, nextDisableArms_ = 0;
    uint32_t strumStyle_ = 0;  // <style>_Strums
    const qb::Struct* fingerAnims_ = nullptr;
    const qb::Struct* fretAnims_ = nullptr;
    uint32_t bodyKey_ = 0;
    uint32_t bodyGen_ = 0, strumGen_ = 0, faceGen_ = 0, chordGen_ = 0, armGen_[2] = {0, 0};
    float slowTempo_ = 75, medTempo_ = 115, fastTempo_ = 145;
    int shortStrum_ = 150, medStrum_ = 600;
    float fingerPress_ = 0.07f, fingerRelease_ = 0.2f, fretBlend_ = 0.1f, drumBlend_ = 0.1f;
    float armBlendOutDelay_ = 0.25f, twistRate_ = 0.05f;
    int lastArmNote_[2] = {0, 0};
    bool missed_ = false;     // playing_missed_note
    int lastStrumLength_ = 0;  // last_strum_length: 0 short, 1 med, 2 long
    float twist_ = 0;

    // The tree's nodes.
    Node body_, face_, rightArm_, leftArm_, leftHand_, drumArm_[2], cymbal_[4];
    Ramp leftStrength_, rightStrength_;
    const anim::Clip* twistClip_ = nullptr;
    // Two-bone IK chains: bicep, forearm, palm -> target bone [C data Hero_Ik_params].
    struct Chain {
        int b0 = -1, b1 = -1, b2 = -1, target = -1;
        bool right = false;
    };
    std::vector<Chain> chains_;
};

}  // namespace bandanim
