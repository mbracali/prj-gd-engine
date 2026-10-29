// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

// GH2's lip-sync and facial expressions: the FaceFX SDK 1.5 archives on the
// disc (songs/*/*.voc = FxAnim, char/*.fac = FxFaceGraph) and the runtime
// of FaceFxLipSyncServo (the characters' lip.servo), as the Xbox 360
// executable runs them. docs/facefx.md has the formats and the addresses;
// [C] = read in the code, [I] = inferred.
//
// What the band code calls (per character with a lip.servo):
//   1. setup:  LipSync::setup(graph, visemesList, entryNodeNames) once the
//      servo's .fac (resolveFacePath + loadFaceGraph) and its viseme clip
//      set are loaded. visemesList = loadServoVisemes(data, servo type).
//   2. song:   gh2 type: play(loadAnim(data, kBlinkTrackPath));
//              singer:   play(loadAnim(data, singerVocPath(midi_file)))
//              (no .voc on disc -> no play: the servo never poses).
//   3. per frame (in the character's pose order, after CharEyes / look-ats,
//      skipped when the character is hidden):
//        values[i] = eyeAngle(eye mesh i's local rotation, entries[i].axis)
//        if (servo.poll(songSeconds, uiSeconds, values, pose)) apply `pose`:
//          ScaleDown the `visemes` clip, ScaleAdd frame f with weight w for
//          each pose.frames entry, add pose.residual to every quaternion's
//          w (sign-matched), then RotateBy the `neutral` clip at its start
//          frame (positions +, quaternions neutral * q, euler angles +),
//          then the servo's weight (CharWeightable) as for other servos.
//   4. expressions (gh2 type only): ExpressionScript mirrors the type's dta
//      handlers (pick_expression, override_expression, ...); or run those
//      handlers through the interpreter and route `do_expression` /
//      `set_register` here.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "data/bytes.h"

class GameData;

namespace facefx {

// FxInvalidValue: "no value" in registers, node caches and track values [C].
constexpr float kInvalid = -2147483648.0f;

// Archive header: 'FACE', u32 SDK version (1500; 1200 in _blinktrack.voc and
// metal_singer.fac), FxString licensee, FxString project, u32 1000.
struct Header {
    uint32_t sdk = 0;
    std::string licensee, project;
    uint32_t licenseeVersion = 0;
};

// FxAnimKey (0x8239B500): time in seconds (song seconds for the .voc files),
// value and the Hermite slopes.
struct Key {
    float time = 0, value = 0, slopeIn = 0, slopeOut = 0;
};

// FxAnimCurve (Serialize 0x82390E40, Evaluate 0x82390318).
struct Curve {
    std::string name;
    uint32_t interp = 0;  // 0 Hermite (every curve on the disc), 1 linear
    std::vector<Key> keys;

    // FxAnimCurve::Evaluate [C]: the first key's value up to it, the last
    // key's from it on, else the Hermite segment. `hint` is the curve's cached
    // segment index (the SDK keeps it in the curve); pass the same variable
    // every call (start ~0u) for the SDK's search order.
    float evaluate(float t, uint32_t& hint) const;
    float evaluate(float t) const {
        uint32_t h = ~0u;
        return evaluate(t, h);
    }
    // FxAnimCurve bounds (0x823904E0): first / last key times, (0, 0) if empty.
    void bounds(float& start, float& end) const;
};

// FxAnim (Serialize 0x82385468, version 3; 0 in _blinktrack.voc).
struct Anim {
    Header header;
    std::string name;  // e.g. "badreputation", "blinkTrack"
    uint16_t version = 0;
    std::vector<Curve> curves;
    std::vector<std::pair<std::string, float>> boneWeights;  // empty on the disc
    // +0x3c / +0x40: 0.16 / 0.22 in every file. The SDK's blend-in / blend-out
    // times [I]; nothing GH2 runs reads them (no reader in the player tick
    // 0x82387540, the instance tick 0x82388098 or the servo) [C, by search].
    float blendInTime = 0, blendOutTime = 0;
    std::string audioPath, analysisText;  // empty on the disc
    uint32_t u68 = 0;                     // 0xFFFFFFFF in version 3
    // FxAnim::FindBounds (0x82382E50) [C]: min / max over the curves' bounds
    // (an empty curve counts as 0..0), (0, 1) when there are no curves.
    float startTime = 0, endTime = 0;
    int find(const std::string& curveName) const;
};

// FxBone (0x8238ECA8) in a bone pose node: GH2 replaces every pose node's
// bones from the viseme clip set at load (0x821A7DD8), so these are unused.
struct Bone {
    std::string name;
    float pos[3] = {0, 0, 0}, rot[4] = {0, 0, 0, 1}, scale[3] = {0, 0, 0};
};

// FxFaceGraphNodeLink (0x82391788): input node, link function and parameters.
struct Link {
    uint16_t version = 0;
    std::string input;
    std::string function;  // "linear" for every link on the disc
    std::vector<float> params;
};

struct UserProperty {
    std::string name;
    uint16_t version = 0;
    uint32_t type = 0, intValue = 0;
    float floatValue = 0;
    std::vector<std::string> strings;
};

// FxFaceGraphNode (0x82393528) + FxBonePoseNode (0x8238FF70) / FxCombinerNode (0x8238D658).
struct Node {
    std::string className;   // "FxBonePoseNode" / "FxCombinerNode"
    uint32_t objectSize = 0;  // the u32 after the class name (0x60 / 0x50; 0x5c in SDK 1200)
    std::string name;
    uint16_t version = 0;
    float min = 0, max = 1;
    uint32_t inputOp = 0;  // 0 sum, 1 multiply (every node on the disc: 0)
    std::vector<Link> links;
    std::vector<UserProperty> props;
    uint16_t classVersion = 0;
    std::vector<Bone> bones;  // FxBonePoseNode only
    bool isBonePose() const { return className == "FxBonePoseNode"; }
};

// FxFaceGraph (Serialize 0x8238D250).
struct FaceGraph {
    Header header;
    std::vector<Node> nodes;
    int find(const std::string& nodeName) const;
};

// Parsers: each reads its archive to the exact end. Throw std::runtime_error.
Anim parseAnim(const Bytes& file);
FaceGraph parseFaceGraph(const Bytes& file);
Anim loadAnim(const GameData& data, const std::string& arkPath);
FaceGraph loadFaceGraph(const GameData& data, const std::string& arkPath);

// Link function evaluation [C: "linear" 0x823964C8, fmadds]: no parameter ->
// x, one -> m*x, two -> m*x + b, any other count -> x. Other functions throw
// at LipSync::setup (none on the disc).
float evaluateLink(const Link& link, float x);

// ---- paths

// The gh2 (guitarist) type's change_song: {$this set_song _blinktrack} ->
// "songs/_blinktrack/_blinktrack.voc" (format "%s/%s%s.voc", 0x821915D8) [C].
extern const char* const kBlinkTrackPath;
// The singer type's change_song: game set_lipsync_song (0x822B6A18) ->
// set_song(current song, "", 1, song known): "<dir of midi_file>/<midi_file
// base without extension>.voc" ("%s/%s.voc"); for a song the song list does
// not know, "songs/<song>/<song>.voc" [C].
std::string singerVocPath(const std::string& midiFile);
std::string unknownSongVocPath(const std::string& song);
// The servo's .fac path (a FilePath relative to the character milo's folder,
// e.g. "../../../guitarist.fac" from char/alterna1/og/gen/alterna1.milo_ps2
// -> char/guitarist.fac); "og" folders fall back to "ng" as for milos.
std::string resolveFacePath(const GameData& data, const std::string& miloArkPath, const std::string& rel);
// The servo type's `visemes` list (char/gen/char_objects.dtb,
// (FaceFxLipSyncServo (types (<type> (visemes (LIST)))))): gh2 ->
// EXPRESSIONS_LIST (11 names), singer -> VISEMES_LIST (16). Empty if absent.
std::vector<std::string> loadServoVisemes(const GameData& data, const std::string& servoType);

// The eye-entry register values (0x821A71D0 -> 0x82269988 / 0x822699B8 /
// 0x822699E8) [C]: rot = the entry object's transform rows (x, y, z axes,
// scene::Xfm::rot layout; the matrix at object +0x1c, the local transform
// [I]). axis 0: atan2(y.z, y.y); 1: atan2(-x.z, z.z); 2: -atan2(y.x, y.y);
// other axes: 0.
float eyeAngle(const float rot[9], uint32_t axis);

// ---- runtime

// FxRegisterOp [C: node GetValue 0x82391AB8]. Eye registers use Replace
// (0x821A71D0). The op of the servo's own expression registers is not
// visible in the decompiled calls; it does not matter: those nodes have no
// curve and no links, so add, multiply and replace all give the register.
enum class RegisterOp : uint32_t { Add = 0, Multiply = 1, Replace = 2 };

// The weights the lip servo feeds the viseme clip set this frame
// (0x821A7978): every bone pose node of the graph, in graph order, whose
// value is not 0, as (clip frame, weight); residual = 1 - sum |weight|.
struct FacePose {
    struct Frame {
        int frame = 0;  // `visemes` clip frame = index in the type's visemes list
        float weight = 0;
        int node = 0;  // graph node index (diagnostics)
    };
    std::vector<Frame> frames;
    float residual = 1;
};

// One character's FaceFxLipSyncServo runtime: the face graph as the servo
// rebuilds it (FxActor), the FxActorInstance (registers, FxAnimPlayer) and
// the servo's expression cross-fade.
class LipSync {
public:
    // Servo load (0x82190DB0) [C]: the .fac graph; then, for each name of the
    // type's visemes list, the bone pose node of that name (created when the
    // graph has none) gets clip frame i (0x821A7DD8); then a combiner node for
    // each servo entry's node name the graph lacks (0x8218E6B8), registered
    // as a register. Throws on a link function other than "linear".
    void setup(const FaceGraph& graph, const std::vector<std::string>& visemes, const std::vector<std::string>& entryNodes);
    bool ready() const { return ready_; }

    // set_song's load (0x8218E150) [C]: the FxActorInstance is created on the
    // first song and kept; PlayAnim (0x82387748) binds the curves to the
    // graph's nodes by name (0x823901E8; unmatched curves drive nothing) and
    // starts the player (state 2, start time unset).
    void play(std::shared_ptr<const Anim> anim);
    const Anim* anim() const { return anim_.get(); }
    // FxAnimPlayer not stopped (state != 4, 0x823874D8): the servo poses only then.
    bool playing() const { return hasInstance_ && state_ != 4; }

    // FxActorInstance::SetRegister (0x82388650) [C]. Returns false when the
    // graph has no node of that name. interp > 0 cross-fades over that many
    // UI seconds (the servo always passes 0).
    bool setRegister(const std::string& node, float value, RegisterOp op = RegisterOp::Add, float interp = 0);

    // do_expression name blend (0x8218F380) [C], `now` = TaskMgr seconds
    // (song seconds, kTaskSeconds): nothing when name is the current
    // expression; else the current one (weight min(1, (now - start) / (end -
    // start)) at this moment) becomes the previous one, name the current, and
    // the fade runs from now to now + blend.
    void doExpression(const std::string& name, float blendSeconds, float now);
    const std::string& currentExpression() const { return cur_; }

    // Poll (0x8218E7E0) [C]. songSeconds = kTaskSeconds, uiSeconds =
    // kTaskUISeconds (TaskMgr +0x28 -> +0x08 / +0x28). entryValues[i] is the
    // register value of the setup's entryNodes[i] (eyeAngle()). Returns false
    // (pose nothing, as GH2) when no song was played or its player stopped;
    // the caller also skips hidden characters (GH2 checks the Character's
    // showing flag). Else fills `out`. Order [C]: entry registers (Replace),
    // expression registers (current = f, previous = (1 - f) * its weight at
    // the switch; f = min(1, (song - start) / (end - start))), instance tick
    // (0x82388098: registers, then the player: time = song seconds when >= 0,
    // else UI seconds since the first tick minus the anim's negative start;
    // past the anim's end the player stops, curves not evaluated this frame),
    // registers into the nodes (0x82387F50), node values (0x82391AB8), pose list.
    bool poll(float songSeconds, float uiSeconds, const float* entryValues, size_t entryCount, FacePose& out);

    // Node values of the last poll (or evaluate()) by graph index; diagnostics.
    int nodeCount() const { return int(nodes_.size()); }
    const std::string& nodeName(int i) const { return nodes_[size_t(i)].name; }
    bool nodeIsBonePose(int i) const { return nodes_[size_t(i)].bonePose; }
    int nodeFrame(int i) const { return nodes_[size_t(i)].frame; }
    int findNode(const std::string& name) const;
    float nodeValue(int i);

    // The face graph alone at anim time t (curves -> nodes, registers as they
    // are, no player state): what facefx.py `eval` computes. Returns the node
    // values in graph order.
    std::vector<float> evaluate(float animTime);

private:
    struct RLink {
        int input = -1;
        Link link;
    };
    struct RNode {
        std::string name;
        bool bonePose = false;
        float min = 0, max = 1;
        uint32_t inputOp = 0;
        std::vector<RLink> links;
        int frame = -1;  // FxBonePoseNode +0x5c
        // per-frame state (FxFaceGraphNode +0x0c value, +0x10 track, +0x1c / +0x20 register)
        float value = kInvalid, track = kInvalid, reg = kInvalid;
        uint32_t regOp = 0;
    };
    struct Register {  // FxActorInstance register entry (0x28 bytes)
        int node = -1;
        float start = kInvalid, value = kInvalid, next = kInvalid, current = kInvalid;
        float invDuration = 0, nextInvDuration = 0, startTime = kInvalid;
        uint32_t op = 0;
    };

    float getValue(int i);
    void clearNodes();
    void evaluateCurves(float t);
    void tickRegisters(float now);
    void tickPlayer(float uiSeconds, float songSeconds);
    void applyRegisters();

    bool ready_ = false;
    std::vector<RNode> nodes_;
    std::vector<int> entryNodes_;
    std::vector<Register> registers_;
    std::shared_ptr<const Anim> anim_;
    std::vector<int> curveNode_;        // per anim curve: bound node or -1
    std::vector<uint32_t> curveHint_;   // per anim curve: cached segment
    bool hasInstance_ = false;
    int state_ = 4;                     // FxAnimPlayer +0x34
    bool started_ = false;              // +0x30
    double playerStart_ = kInvalid;     // +0x10 (UI seconds)
    float playerOffset_ = kInvalid;     // +0x2c
    float playerTime_ = kInvalid;       // +0x28
    // expression cross-fade (servo +0x138..+0x148)
    std::string cur_, prev_;
    float curStart_ = 0, curEnd_ = 0, prevWeight_ = 0;
};

// The gh2 servo type's expression scripts (char_objects.dta FaceFxLipSyncServo
// types gh2) [C data], for a band code that does not run them through the
// interpreter. Character enter / intro_start_msg / intro_skip call
// pickExpression; ow_face_on -> overrideExpression("OwFace"); ow_face_off ->
// resumeRandomExpression.
class ExpressionScript {
public:
    // GH2's random_int (lo <= n < hi) and random_float (lo <= x < hi): the
    // script interpreter's generator.
    std::function<int(int, int)> randomInt;
    std::function<float(float, float)> randomFloat;

    // pick_expression: (re)starts expression_task (kTaskSeconds): do_pick_expression,
    // sleep random_float 1.5 2.5, loop. The first run is the next update().
    void pickExpression(float now);
    // Runs the task when due. gameExists = {exists game}; excitement =
    // {game get excitement} (0 Boot, 1 Bad, 2 Okay, 3 Great, 4 Peak).
    void update(float now, bool gameExists, int excitement, LipSync& servo);
    // override_expression name: do_expression name 1, override on.
    void overrideExpression(const std::string& name, float now, LipSync& servo);
    // resume_random_expression: if override: off, do_pick_expression.
    void resumeRandomExpression(float now, bool gameExists, int excitement, LipSync& servo);
    // do_pick_expression: unless overridden (and only with a game): Boot / Bad ->
    // pick_bad_expression (expressionBad1..3), Okay+ -> pick_good_expression
    // (expressionGood1..5), blend 0.25 s. rand = random_int(1, n); if rand >=
    // last_expression: rand++ (last_expression starts at -1 and is shared by
    // both lists: the first pick is never 1).
    void doPickExpression(float now, bool gameExists, int excitement, LipSync& servo);

    bool overridden() const { return override_; }
    int lastExpression() const { return last_; }
    bool taskRunning() const { return task_; }
    double nextRun() const { return wake_; }

private:
    bool override_ = false;
    int last_ = -1;
    bool task_ = false;
    double wake_ = 0;
    bool resumeAtWake_ = false;
};

}  // namespace facefx
