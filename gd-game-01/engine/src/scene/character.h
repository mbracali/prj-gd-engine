// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "render/scene_renderer.h"
#include "data/dta.h"
#include "scene/clip.h"

class GameData;

// A character (charsys/<model>/gen/<model>.rnd_ps2), optionally holding a
// guitar (charsys/guitars/gen/<guitar>.rnd_ps2), animated with a clip set
// from charsys/*_anims.dta (guitarists) or band_anims.dta. Skeleton: bones are Meshes whose
// `children` lists form the hierarchy; the guitar file repeats bone_pelvis /
// bone_pos_guitar and hangs off the character's copies. Skinned meshes blend
// up to four bones with inverse-bind offsets (see FINDINGS.md "Characters").
//
// Clips play through an AnimDriver chain (docs: re_chars, FUN_0017ff18 /
// FUN_0018a4b0): each clip's frame is phase-locked to the song beat, new clips
// enter at .acg graph edges and cross-fade over their blend width, and clips
// are picked least-recently-used. The twist servos then fix the arm skin.
//
// Drivers, servos, root motion, hands, faces and eyes follow
// docs/character_animation.md, guitarist_hands.md and faces_shadows.md.
class Character {
public:
    struct Spec {
        std::string model;      // charsys folder, e.g. "metal" or "metal_bass"
        std::string guitar;     // config/guitars.dta id, or empty
        std::string animsFile;  // e.g. "charsys/metal_anims.dta"
        std::string animsMacro; // the clip-set #define in it, e.g. "METAL_ANIMS"
        int tempo = 1;          // the song's anim_tempo (1 medium, 2 fast)
        uint32_t restrict = 0;  // clips must carry one of these flags (crowd restrict_flags)
        std::string folder;     // charsys folder when it differs from `model` (crowd)
        int servos = 2;         // 0 none (crowd), 1 upper-arm twists (bandbase.dta), 2 + fore-arm twists (charbase.dta)
        bool meta = false;      // menu character (`meta` property): only the kGuitarUI clips of <C>_UI_ANIMS
        bool hands = false;     // guitarist hand drivers and IK (<C>_HAND_ANIMS, charbase.dta)
    };
    // config/guitars.dta: a guitar (its first skin) or a skin -> (charsys/guitars
    // scene file name, material), through the skin's outfit and charsys GUITARS.
    static std::pair<std::string, std::string> guitarModel(const GameData& data, const std::string& guitarOrSkin);
    // charsys/<archetype>_anims.dta and its <ARCHETYPE>_ANIMS macro (hair_metal -> hair), for guitarists.
    static Spec guitaristSpec(const GameData& data, const std::string& archetype, const std::string& guitar, int tempo,
                              bool meta = false);
    // AnimDriver play modes (low 3 bits) and flags.
    static constexpr int kAnimReset = 1, kAnimNow = 2, kAnimFirst = 3, kAnimLongest = 4, kAnimLoop = 0x80,
                         kAnimGraphLoop = 0x100;
    // Throws std::runtime_error when the character cannot be loaded.
    Character(Renderer& renderer, const GameData& data, const Spec& spec);
    ~Character();

    // Clip categories (config/gen/macros.dta).
    static constexpr uint32_t kGuitarBad = 16384, kGuitarExtreme = 32768, kGuitarIdle = 65536,
                              kGuitarIntro = 131072, kGuitarLose = 262144, kGuitarNormal = 524288,
                              kGuitarSolo = 1048576, kGuitarWin = 2097152, kGuitarSpecial = 33554432;
    static constexpr uint32_t kGuitarUI = 16777216;
    static constexpr uint32_t kBandActive = 4, kBandAllbeat = 8, kBandDouble = 16, kBandHalf = 32,
                              kBandIdle = 64, kBandLose = 128, kBandWin = 256;

    // Where the character's root stands in the world.
    void setPlacement(const Xfm& placement) { placement_ = placement; }
    const Xfm& placement() const { return placement_; }
    // bone.servo use_delta (guitarists): the clips' bone_facing deltas move the
    // root (character_animation.md §1.4) instead of posing bone_facing.
    void setRootMotion(bool on) { rootMotion_ = on; }

    // Walking (docs/camera_walk_crowd_lighting.md §2): direct clip access.
    static constexpr uint32_t kWalkTurn = 32, kWalkStop = 64, kWalkWalk = 128, kWalkLeft = 256, kWalkRight = 512,
                              kWalkForward = 1024, kWalkBackward = 2048, kWalkNormal = 4096, kWalkExtreme = 8192;
    std::vector<int> clipsWith(uint32_t flags, bool matchTempo) const;
    const std::string& clipNameOf(int clip) const { return clips_[size_t(clip)].clip->name(); }
    uint64_t clipUsed(int clip) const { return clips_[size_t(clip)].used; }
    // Queues a clip through the AnimDriver transition (kAnimFirst / kAnimLongest) and marks it used.
    void playClip(int clip, int mode);
    // The newest clip in the chain (-1 when none).
    int headClip() const { return chain_.empty() ? -1 : chain_.back().clip; }
    // The newest clip that has started blending in.
    int currentClip() const;
    // bone_facing root motion of `clip` from frame f0 to f1, in the clip's
    // starting frame (FUN_001883d8): displacement and yaw change.
    void rootDelta(int clip, float f0, float f1, std::array<float, 3>& pos, float& yaw) const;
    float clipStart(int clip) const { return clips_[size_t(clip)].clip->startFrame(); }
    float clipEnd(int clip) const { return clips_[size_t(clip)].clip->endFrame(); }
    // Plays the least-recently-used clip carrying `flags` with an AnimDriver mode.
    void play(uint32_t flags, int mode);
    // Starts the LRU clip of `flags` so its blend completes on `eventBeat`
    // (GuitarCharacter FUN_002856a0 / FUN_0017fd18).
    void playAt(uint32_t flags, float eventBeat);
    // Advances the drivers to song beat `beat` (tick / 480) times the frame
    // scale (set_frame_scale) and time `ms`, then poses the skeleton. Frames
    // advance by the beat delta times the scale; only the phase target jumps
    // when the scale changes (realign absorbs that).
    void poll(float beat, float ms, float frameScale = 1);
    // main.drv (realign 1) (band_chars.dta: drummer, keyboard; AnimDriver_v0 @0x17ff18).
    void setRealign(bool on) { realign_ = on; }

    // The walk's view of the main AnimDriver (CharWalk, docs/character_polish_plan.md §4.9).
    struct DriverInfo {
        int clip = -1;
        float frame = 0, delay = 0, blend = 0;
    };
    struct Transition {
        float src = 0, dest = 0;
        bool found = false;
    };
    // FUN_00180670: the newest driver that has started blending.
    DriverInfo newestStarted() const;
    // FUN_00180630: the driver queued after the newest started one, if any (and the one it follows).
    bool pendingAfterNewest(DriverInfo* pending, DriverInfo* before) const;
    // FUN_0017f1d0 without pushing: the transition from `fromClip` at `fromFrame` to `toClip`.
    Transition transition(int fromClip, float fromFrame, int toClip, int mode) const;
    // FUN_0017efe0: an explicit .acg edge only (Longest cost).
    Transition explicitEdge(int fromClip, int toClip) const;
    // FUN_0017fd18: pushes `clip` starting at `frame` after `delay` frames of the newest driver.
    void queueClip(int clip, float frame, float delay);
    // Drops a queued driver that has not started (CharWalk Cancel).
    void popUnstarted();
    float clipBlendWidth(int clip) const;
    void touchClip(int clip) { clips_[size_t(clip)].used = ++clock_; }
    // Crowd animate (FUN_00173508): `clip` started at `frame` (a random phase).
    void startAt(int clip, int mode, float frame);
    bool clipLoaded(int clip) const { return clip >= 0 && size_t(clip) < clips_.size() && clips_[size_t(clip)].clip; }
    bool rootMotion() const { return rootMotion_; }
    // A clip change is queued or blending in.
    bool blending() const { return chain_.size() > 1; }
    // Hand drivers (guitarists, docs/guitarist_hands.md).
    static constexpr int kLeftHand = 0, kRightHand = 1;
    bool hasHands() const { return !handClips_.empty(); }
    void handPlay(int side, const std::string& clip, int mode, float delay, bool pushed);
    const std::string& handHead(int side) const;
    float handBlendWidth(const std::string& clip) const;
    // The detached bone_fret's world (= local) transform.
    void setFretBone(const Xfm& world) { fretBone_ = world, hasFretBone_ = true; }
    // The newest clip has reached its last frame.
    bool atEnd() const { return !chain_.empty() && chain_.back().frame >= clips_[size_t(chain_.back().clip)].clip->endFrame(); }
    // The flags of the newest clip (0 when none).
    uint32_t clipFlags() const;
    // Simple driving (crowd): plays `category` (kAnimNow on change) and, once
    // a clip has blended in, queues the next one (kAnimLongest) unless `oneShot`.
    void update(float beat, float ms, uint32_t category, bool oneShot = false);
    // Draws the body and guitar; `env` lights materials that use an environment.
    void draw(const Mat4& viewProj, const EnvironObj* env);

    // Face morphs (GameCharacter face, docs/faces_shadows.md A): `faceFile` is the
    // face_file ("charsys/metal/metal_face.rnd"); heroes change expression with the
    // crowd excitement, a singer opens the mouth on the T1 GEMS note-108 events.
    struct FaceEvent {
        float startMs, endMs;
    };
    void loadFace(const GameData& data, const std::string& faceFile, bool singer);
    // Eyes (charbase.dta create_eyes, docs/faces_shadows.md B): guitarists in songs.
    // Mode per camera shot: 0 off (follow the head), 1 camera, 2 forward, 3 crowd.
    // CrowdChar_v3 `swap (mat (set0...) (set1...) ...)`: meshes using a material of
    // another set get set m's material at the same position.
    void swapMaterials(const std::vector<std::vector<std::string>>& sets, int m);
    // Eye mode 3 (crowd): a target from the 3D crowd, or nothing.
    std::function<std::optional<std::array<float, 3>>()> eyeCrowdTarget;
    void enableEyes();
    // charbase.dta show_shadow (docs/faces_shadows.md C): <c>_shadow.rnd skinned by this
    // character's bones, flattened along `dir` onto z = root.z + 3 (shadow_plane_height)
    // and drawn depth-only (charsys_shadow.mat has alpha 0) right after the character.
    void loadShadow(const GameData& data, const std::string& shadowFile);
    void drawShadow(const Mat4& viewProj, const std::array<float, 3>& dir);
    // [I] The same flattened shadow for a character without a <c>_shadow.rnd
    // (the band): its own body drawn with `shadowMat` (charsys_shadow.mat).
    void drawBodyShadow(const Mat4& viewProj, const std::array<float, 3>& dir, const MatObj* shadowMat);
    // charsys_shadow.mat of the loaded shadow, or null.
    const MatObj* shadowMaterial() const;
    void setEyeMode(int mode);
    void setEyeCamera(const std::array<float, 3>& pos) { eyeCamera_ = pos; }
    void setFaceExcitement(int excitement) { faceExcitement_ = excitement; }
    void setFaceEvents(std::vector<FaceEvent> events);

    // Shows or hides one of the character's meshes (crowd heads, hands).
    void setShowing(const std::string& mesh, bool showing);
    // The hand pose of the current clip (HAND_CLAP 0, HAND_FIST 1, HAND_HORN 2), or -1.
    int hand() const;

    // An object of the character or its guitar (e.g. "spot_neck_fret20.mesh").
    const SceneObject* find(const std::string& name) const;
    // Where names the character doesn't have are looked up (the venue's lights).
    void setWorldLookup(SceneRenderer::Lookup lookup) { world_ = std::move(lookup); }
    const std::string& clipName() const;

private:
    struct Clip {
        std::unique_ptr<AnimClip> clip;
        uint32_t flags;
        int hand = -1;
        int set = 0;        // which clip set of the macro
        int index = -1;     // position in the clip set (the .acg numbering)
        uint64_t used = 0;  // LRU stamp
    };
    struct Edge {  // .acg: {dest_index, src_frame, dest_frame}
        int dest;
        float src, destFrame;
    };
    struct Driver {  // AnimClipDriver
        int clip, mode;
        float frame, delay, blend, dEff = 0;
        int align = -1;  // the driver's copy of the beat-align nibble (-1: the clip's)
    };
    int select(uint32_t flags);
    void loadHandClips(const GameData& data, const DataNode& anims, const Spec& spec);
    int handClip(const std::string& name) const;
    void pollChain(std::vector<Clip>& clips, std::vector<Driver>& chain, float beat, float dBeat, float dSec);
    void blendChain(const std::vector<Clip>& clips, const std::vector<Driver>& chain, float weight);
    float flagWeight(uint32_t flag) const;
    void ik(const char* side, const std::string& dest, float frac);
    void updateSubtree(const std::string& name);
    void start(int clip, int mode);
    void pose();
    void servos();
    void updateWorld();
    struct Face;
    std::unique_ptr<Face> face_;
    Renderer* renderer_ = nullptr;
    std::unique_ptr<Scene> shadow_;
    std::unique_ptr<SceneRenderer> shadowRenderer_;
    std::vector<std::pair<SceneObject*, std::string>> shadowRigid_;  // rigid shadow pieces and their character bone
    // Eyes: on, mode, the next re-target tick, the look target, each eye's last world rotation.
    bool eyes_ = false;
    int eyeMode_ = 3;
    float eyeNext_ = -1;
    std::array<float, 3> eyeTarget_{}, eyeCamera_{};
    std::array<std::array<float, 9>, 2> eyeRot_{};
    bool eyeRotSet_ = false;
    std::mt19937 eyeRng_{std::random_device{}()};
    void pollEyes();
    int faceExcitement_ = 2;
    void pollFace(float dSec, float ms);

    std::unique_ptr<Scene> body_, guitar_;
    std::unique_ptr<SceneRenderer> bodyRenderer_, guitarRenderer_;
    std::map<std::string, SceneObject*> objects_;  // name -> owner (the character's copy first)
    std::map<std::string, std::string> parent_;
    std::map<std::string, Xfm> bind_;               // bind-pose locals
    std::vector<std::string> bodyDraws_, guitarDraws_;
    std::vector<Clip> clips_;
    SceneRenderer::Lookup world_;
    int tempo_;
    Xfm placement_;
    std::vector<std::vector<Edge>> graph_;  // set 0's transitions, by clip-set index
    std::vector<Driver> chain_;             // oldest first; back() is the head
    std::vector<Clip> handClips_;
    std::vector<Driver> hands_[2];
    std::map<std::string, Xfm> buffer_;     // bone.servo's persistent buffer (starts at bind)
    std::map<std::string, std::vector<std::string>> children_;
    Xfm fretBone_;
    bool hasFretBone_ = false, fretDetached_ = false;
    float wL_ = 0, wR_ = 0;                 // IK flag weights of the main clips (FUN_00286f58)
    struct IkState {
        bool init = false;
        std::array<float, 3> handPos{};
        float a2b2 = 0, inv2ab = 0;
    } ik_[2];
    uint64_t clock_ = 0;
    float beat_ = 0, ms_ = 0, rawBeat_ = 0;
    bool realign_ = false;
    float realignLast_ = 1e30f;
    void realign(float beat);
    bool polled_ = false;
    int servoLevel_ = 2;
    uint32_t category_ = 0;
    std::vector<AnimClip::Value> scratch_;
    bool rootMotion_ = false;
    void applyRootMotion();
    bool facingAt(const AnimClip& c, float frame, std::array<float, 3>& pos, float& rotz) const;
};
