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
#include "char/clip.h"
#include "char/rig.h"
#include "char/skeleton.h"
#include "scene/milo.h"

class GameData;

// A GH2 character at run time (docs/band.md): Character / BandCharacter
// (Handle 0x82152108, DrawShowing 0x82153090, Poll 0x82156B78).
//
// - Load: its own copy of char/<c>/ng/gen/<c>.milo_xbox (the proxies of a
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
    // gen/guitar_sg.milo_xbox). Throws std::runtime_error when the
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
    // A bone's (or any object's) world transform; false when absent.
    bool worldOf(const std::string& name, scene::Xfm& out);

    // One poll: drivers, servo, rig (context: the song clock, camera).
    void poll(RigContext ctx);

    // Showing of the character's objects (set_showing, LOD groups, hide_drummer).
    bool showing(const scene::Object* o) const;
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

    // Stats.
    int outfitObjects() const { return outfitObjects_; }
    std::vector<std::string> unresolvedBones() const;

private:
    void loadOutfit(const GameData& data, const std::string& outfitPath);
    void poseMeshes();
    void servoRoot();

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
    int lod_ = -1;
    int outfitObjects_ = 0;
    // The servo: channel -> bone (skeleton index), and the root channels.
    std::vector<int> channelBone_;
    size_t channelsMapped_ = 0;
    bool moveSelf_ = false;
    int facingPos_ = -1, facingRot_ = -1, facingDeltaPos_ = -1, facingDeltaRot_ = -1;
    int pelvis_ = -1;
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
