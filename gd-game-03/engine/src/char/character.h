// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "char/char_driver.h"
#include "char/char_objects.h"
#include "char/char_walk.h"
#include "char/clip.h"
#include "char/facefx.h"
#include "char/rig.h"
#include "char/skeleton.h"
#include "scene/milo.h"

class GameData;

// A GH2 character at run time (docs/band.md): Character / BandCharacter
// (Handle 0x82152108, DrawShowing 0x82153090, Poll 0x82156B78).
//
// - Load: its own copy of char/<c>/og/gen/<c>.milo_ps2 (the proxies of a
//   venue each get one; two players of one character do not share bones),
//   the outfits its OutfitLoaders select merged in (0x82158F30: new objects
//   added; "bone_*" duplicates keep the character's, groups among them get
//   the outfit's members; other duplicates parented to a "bone_*" take the
//   outfit's data (the guitar's neck spots); the outfit's LOD and shadow
//   groups merged into the character's by index).
// - Animation: its drivers (CharDriver / CharDriverMidi, char_driver.h)
//   write the servo's channel buffer; the servo (CharServoBone, Poll
//   0x82189408) poses the bones (PoseMeshes 0x821A51E0) and applies the
//   root motion: with bone_facing_delta channels ("move self") the character
//   itself moves by the clip's root deltas (0x821886E8), else bone_facing is
//   put under the pelvis (0x82188640). The rest of the rig (IK, twists,
//   hair, look-ats) is chr::Rig, all in the dir's pollable order.
// - LOD (0x82153090): the first LOD whose screen size (x 1.09 below the
//   current LOD, x 0.91 from it on) the character's sphere reaches, not
//   below min_lod (set_min_lod); its group shown, the others hidden.
namespace chr {

class ClipLibrary;

class Character {
public:
    // `path`: the character milo (archive path). `outfits`: per OutfitLoader
    // name, the outfit milo to merge ("guitar.outfit" -> char/ng/guitars/
    // gen/guitar_sg.milo_ps2). Throws std::runtime_error when the
    // character cannot be loaded.
    Character(const GameData& data, const std::string& path, ClipLibrary& clips,
              const std::map<std::string, std::string>& outfits = {});
    ~Character();
    Character(const Character&) = delete;
    Character& operator=(const Character&) = delete;

    const std::string& path() const { return path_; }
    const std::string& name() const { return name_; }  // the character dir's name (alterna1)
    scene::Milo& milo() { return *milo_; }
    const scene::Directory& dir() const { return milo_->root(); }
    // Every object of the character's milo and its outfits' (guitar, kit).
    template <class F>
    void forEachObject(F&& f) {
        milo_->root().forEach(f);
        for (auto& m : outfits_) m->root().forEach(f);
    }
    const scene::CharacterObj& own() const { return *own_; }
    bool band() const { return own_->band; }
    const std::string& type() const { return own_->header.type; }  // guitarist / singer / ... / crowd / ui

    Skeleton& skeleton() { return skeleton_; }
    Rig& rig() { return *rig_; }
    Bones& bones() { return bones_; }
    ClipDriver* driver(const std::string& name);   // "main.drv", "left_hand.drv"...
    ClipDriver* mainDriver() { return driver("main.drv"); }
    const std::vector<std::unique_ptr<ClipDriver>>& drivers() const { return drivers_; }

    // Placement: the character's world transform (teleport: the waypoint's
    // world transform with the scale removed, 0x82151D08).
    const scene::Xfm& xfm() const { return skeleton_.root(); }
    void setXfm(const scene::Xfm& x);
    void teleport(const scene::Xfm& waypointWorld);
    // Teleport to a waypoint (0x82151D08): its transform, and the servo
    // regulates to it from then on.
    void teleport(const Spot& waypoint);

    // The servo's root motion (CharServoBone Poll 0x82189408) [C]. With the
    // clips' bone_facing_delta channels: "move self" (+0xc8, on at enter,
    // 0x821895C8) moves the character by the deltas (0x821886E8), else
    // bone_facing goes under the pelvis (0x82188640); a switch (+0xc9)
    // moves the facing between the pelvis and the character so the pose
    // stays where it is. While moving self, the regulate waypoint (+0xd8)
    // or the override pulls the character so that the clip's motion up to
    // the next transition (the override's frame) ends inside the
    // waypoint's area and angle tolerance (0x82188DC8).
    void setMoveSelf(bool on);  // FUN_82188620
    bool moveSelf() const { return moveSelf_; }
    void setRegulate(const Spot* s);  // Function_82150070(servo + 0xd0)
    const Spot* regulate() const { return hasRegulate_ ? &regulate_ : nullptr; }
    // regulate_override clip frame waypoint bone (0x82189A40 -> 0x82189928):
    // while `clip` plays, `bone`'s position at `frame` of it (from the root,
    // turned with the facing) is brought to the waypoint; empty: off.
    void setRegulateOverride(const scene::CharClipSamples* clip, float frame, const Spot* spot, const std::string& bone);
    // The CharWalk (`walk`, M9.6), null when the character has none.
    CharWalk* walk() { return walk_.get(); }
    // A bone's (or any object's) world transform; false when absent.
    bool worldOf(const std::string& name, scene::Xfm& out);

    // One poll: drivers, servo, rig (context: the song clock, camera).
    void poll(RigContext ctx);

    // Showing of the character's objects (set_showing, LOD groups, hide_drummer).
    bool showing(const scene::Object* o) const;
    // Whether the character set o's showing itself (its LOD switch,
    // set_showing), which then wins over the object's saved flag.
    bool showingSet(const scene::Object* o) const { return showing_.count(o) != 0; }
    void setShowing(const scene::Object* o, bool on) { showing_[o] = on; }
    void setShowing(const std::string& name, bool on);
    bool visible = true;  // the character as a whole (guitarist1 hidden...)

    // LOD (0x82153090): picks and shows the LOD group for `screenSize`.
    void updateLod(float screenSize);
    int lod() const { return lod_; }
    int minLod = 0;  // set_min_lod
    // The character's bounding sphere in world space (its RndDir sphere
    // under sphere_base / the root).
    std::array<float, 4> worldSphere();

    // The groups drawn: the LOD groups and other drawables (not the shadow).
    const scene::Group* shadowGroup() const { return shadow_; }
    const scene::Environ* slotEnviron = nullptr;  // the venue slot's environ (band.env...)
    // Script hooks the band sets: clip scripts ($dude), starved.
    std::function<void(const std::string& script)> runScript;
    std::function<void(const std::string& msg)> sendMessage;

    // The face: the lip.servo (FaceFxLipSyncServo, Poll 0x8218E7E0) [C].
    // The constructor finds the servo, its viseme clip set (`visemes` and
    // `neutral` clips), the set's channels (its own buffer, 0x821A8368) and
    // the eye entries; the band attaches the FaceFX runtime. Each poll at
    // the servo's place in the rig order, unless the character is hidden:
    // the entries' angles (eyeAngle of their local rotation), LipSync::poll,
    // then (0x821A7978) ScaleDown `visemes` to 0, ScaleAdd each pose frame
    // with its weight, residual onto every quaternion's w (sign-matched),
    // the `neutral` clip at its start as an additive layer (0x82161F98 ->
    // 0x8215E6A0: positions +, quaternions neutral * q, angles +), and the
    // bones posed from that buffer (0x821A51E0). No pose when the player has
    // stopped or no song was played.
    const scene::FaceFxLipSyncServo* faceServo() const { return faceServo_; }
    ClipSet* faceClips() const { return faceSet_; }
    void attachFace(facefx::LipSync* lip) { lip_ = lip; }
    facefx::LipSync* lipSync() const { return lip_; }
    Bones& faceBones() { return faceBones_; }
    const facefx::FacePose& lastFace() const { return lastFace_; }
    int facePoses() const { return facePoses_; }  // polls that posed the face

    // Stats.
    int outfitObjects() const { return outfitObjects_; }
    std::vector<std::string> unresolvedBones() const;

private:
    void loadOutfit(const GameData& data, const std::string& outfitPath);
    void poseMeshes(Bones& bones, std::vector<int>& channelBone, size_t& mapped);
    void servoRoot(float dBeats);
    void regulateRoot(float dBeats);
    std::array<float, 3> boneOffsetAt(const scene::CharClipSamples& clip, float frame, const std::string& bone);
    void pollFace(const RigContext& ctx);

    std::string path_, name_;
    std::unique_ptr<scene::Milo> milo_;
    std::vector<std::unique_ptr<scene::Milo>> outfits_;
    std::unique_ptr<scene::CharacterObj> own_;
    Skeleton skeleton_;
    Bones bones_;
    std::unique_ptr<Rig> rig_;
    std::vector<std::unique_ptr<ClipDriver>> drivers_;
    std::unordered_map<std::string, ClipDriver*> driverByName_;
    std::unordered_map<const scene::Object*, bool> showing_;
    std::vector<const scene::Group*> lodGroups_;
    const scene::Group* shadow_ = nullptr;
    const scene::Group* outfitShadow_ = nullptr;  // an outfit's, when the character has none
    int lod_ = -1;
    int outfitObjects_ = 0;
    // The servo: channel -> bone (skeleton index), and the root channels.
    std::vector<int> channelBone_;
    size_t channelsMapped_ = 0;
    bool moveSelf_ = false;
    int facingPos_ = -1, facingRot_ = -1, facingDeltaPos_ = -1, facingDeltaRot_ = -1;
    int pelvis_ = -1;
    bool moveChanged_ = false;  // servo +0xc9
    bool hasRegulate_ = false;
    Spot regulate_;             // servo +0xd8
    struct Override {           // servo +0xdc..+0x104
        const scene::CharClipSamples* clip = nullptr;
        float frame = 0;
        bool hasSpot = false;
        Spot spot;
        std::array<float, 3> offset{};
    } override_;
    std::unique_ptr<CharWalk> walk_;
    // The face (lip.servo).
    const scene::FaceFxLipSyncServo* faceServo_ = nullptr;
    ClipSet* faceSet_ = nullptr;
    const scene::CharClipSamples* visemes_ = nullptr;
    const scene::CharClipSamples* neutral_ = nullptr;
    facefx::LipSync* lip_ = nullptr;
    Bones faceBones_;
    std::vector<int> faceChannelBone_;
    size_t faceMapped_ = 0;
    std::vector<std::pair<int, uint32_t>> faceEntries_;  // (bone, axis)
    facefx::FacePose lastFace_;
    int facePoses_ = 0;
};

// The clip sets, loaded once per path and shared by the characters that
// use them (CharDriver clips FilePaths).
class ClipLibrary {
public:
    explicit ClipLibrary(const GameData& data) : milos_(data) {}
    // `rel` relative to `fromArkPath` (og -> ng); nullptr when missing.
    ClipSet* get(const std::string& fromArkPath, const std::string& rel);
    const std::map<std::string, std::unique_ptr<ClipSet>>& sets() const { return sets_; }
    scene::MiloLibrary& milos() { return milos_; }

private:
    scene::MiloLibrary milos_;
    std::map<std::string, std::unique_ptr<ClipSet>> sets_;
};

}  // namespace chr
