// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "data/bytes.h"
#include "scene/objects.h"

// GH2's character classes (char/ milos, the venues' band slots), as their
// Load routines in the Xbox 360 executable read them (docs/band.md; each
// Load / PreLoad / PostLoad found through the class's RTTI type descriptor
// -> vtable). Every record of every class on the disc decodes exactly to its
// end marker (--chartest). Field names come from each class's SyncProperty
// strings where the executable names them; "Q:" marks names that are still
// guesses. ObjPtr fields are object names (resolved in the character's
// directory at run time).
//
// Record = the class's own u32 revision, then its fields. "hdr" below is the
// Hmx::Object header (0x82315350, Object::header); "Wt" is CharWeightable
// (0x8216A190: u32 rev, f32 weight, rev > 1: ObjPtr weight_owner).

namespace scene {

// CharWeightable (0x8216A190).
struct CharWeightable {
    uint32_t revision = 0;
    float weight = 1;
    std::string weightOwner;  // rev > 1: the weightable whose weight is used (empty / self = own)
};

// ---- clips --------------------------------------------------------------

// CharBonesSamples (header 0x821A3030, data 0x821A1500): the channels of a
// set and its samples, decoded to floats.
struct CharBonesSamples {
    // Channel types by name suffix (0x8215D520; table 0x82741638).
    enum Type { kPos = 0, kScale = 1, kQuat = 2, kRotX = 3, kRotY = 4, kRotZ = 5, kDRotX = 6, kDRotY = 7, kDRotZ = 8, kNone = 10 };
    struct Bone {
        std::string name;  // "bone_pelvis.quat": the bone ("bone_pelvis") and the channel type
        float weight = 0;  // Q: name
        int type = kNone;
    };
    std::vector<Bone> bones;            // sorted by type
    std::array<uint32_t, 10> offs{};    // first bone of each type; offs[9] = bones.size()
    uint32_t compression = 0;           // 1 on the disc: quats s16 / 32767, rotations s16 / 1638.4 rad
    uint32_t count = 0;                 // samples
    int width = 0;                      // floats per sample: 3 per pos / scale bone, 4 per quat (x y z w), 1 per rotation
    std::vector<int> offset;            // per bone: its first float in a sample
    std::vector<float> values;          // count x width
    const float* sample(uint32_t i) const { return values.data() + size_t(i) * size_t(width); }
    // The bone part of a channel name: "bone_pelvis.quat" -> "bone_pelvis".
    static std::string boneOf(const std::string& channel);
    static int typeOf(const std::string& channel);  // 0x8215D520
};

// CharClipSamples v11 on GH2 360 / v10 on the PS2 disc (0x82162C68) = CharClip v5 (0x821A0BF0) + three bone
// sets. Frames are beats at the authored tempo for beat clips; realtime
// clips run frames_per_sec frames a second.
class CharClipSamples : public Object {
public:
    // CharClip
    uint32_t clipRevision = 0;
    float startFrame = 0, endFrame = 0, framesPerSec = 30;
    uint32_t flags = 0;         // clip+0x24: kTempo*, kWalk*, kBand*... (config/gen/macros.dta)
    uint32_t playFlags = 0;     // clip+0x28: default blend 0xf | loop 0xf0 | time (realtime, beat align) 0xf600
    float blendWidth = 0;       // clip+0x30
    float range = 0;            // Q: name (rev > 3)
    bool relative = false;      // Q: name (rev > 4)
    struct Transition {
        std::string dest;                             // the clip it leads to
        std::vector<std::pair<float, float>> nodes;   // (frame in this clip, frame in dest)
    };
    std::vector<Transition> transitions;
    std::string enterEvent, exitEvent;                // script text "{...}" or empty
    struct Event {
        float frame = 0;
        std::string script;                           // "{handle ($dude hit_snare)}"
    };
    std::vector<Event> events;
    // Bone sets: full (+0x7c) sampled over the clip, one (+0x15c) a single
    // static sample, third (+0x23c) deltas (no channels on the disc).
    CharBonesSamples full, one, third;

    float length() const { return endFrame - startFrame; }
};

// The reference pose of m9/f1/clips.py pose() (for --chartest's golden
// values, not the runtime): the 'one' set's sample 0 under the full set at
// t = (frame - start) / (end - start) clamped, lerped between the samples
// around t (n - 1); quats normalised. Channel name -> 3 / 4 / 1 floats.
std::map<std::string, std::vector<float>> referencePose(const CharClipSamples& clip, float frame);

// CharBone v3 / PS2 v2, no weight (0x8215FDF0): a clip set's skeleton entry ("bone_x.trans").
class CharBone : public Object {
public:
    Trans trans;
    bool position = false, scale = false;  // Q: names
    uint32_t rotation = 0, deltaRotation = 9;  // channel types (9 none); Q: names
    float weight = 1;  // rev 2: not stored, 1.0
};

// CharClipGroup v1 (0x821775A8): a named list of clips.
class CharClipGroup : public Object {
public:
    std::vector<std::string> clips;
    uint32_t flags = 0;  // 0, or 0xcdcdcdcd (uninitialised in the tool: ignore)
};

// CharClipFilter (0x82163F20): object header only.
class CharClipFilter : public Object {};

// The CharClipSet directory's own record (PreLoad 0x82167468: u32 16, PS2 14 +
// ObjectDir v17 PreLoad; PostLoad 0x82168E08: ObjectDir PostLoad, hdr, then
// these). Decoded from Directory::self by decodeClipSetDir (the per-clip
// entries need the directory's clip count).
struct CharClipSetData {
    bool decoded = false;
    std::string error;
    ObjectDirData objectDir;
    ObjectHeader header;          // header.type: guitarist / band / crowd / guitarist_strum / guitarist_ui / ""
    float blendWidth = 0;
    uint32_t playFlags = 0;       // default play flags
    uint32_t defaultFlags = 0;    // Q: name (rev >= 15)
    struct Entry {
        std::string name;
        uint32_t flags = 0;       // = the clip's flags [C data]
        uint32_t unknown = 0;     // Q: second u32 (open question 3)
    };
    std::vector<Entry> clips;
    bool moveSelf = false;
    struct Recenter {
        std::vector<std::string> targets, average;  // Q: names
        bool slide = false;                         // Q: name
    } recenter;
    std::string type;             // = header.type (rev >= 10)
    uint32_t typeVersion = 0;     // guitarist 15, band 4, crowd 1, guitarist_strum 1, guitarist_ui 2
};
// `self` is the CharClipSet milo's Directory::self; `clipCount` the number of
// CharClipSamples in the directory.
CharClipSetData decodeClipSetDir(const Object& self, size_t clipCount);

// ---- characters ---------------------------------------------------------

// Character v10 / PS2 v9, no CharacterTest block (PreLoad 0x82153CF8, PostLoad 0x82157678) and BandCharacter
// v1 (PreLoad 0x82126048, then Character's): a character directory. Its own
// record (char/<c>/og/gen/<c>.milo) carries the RndDir data, LODs, shadow and
// the editor's CharacterTest block; a band slot in a venue's <v>_chars milo
// is a proxy (RndDir data only: placement, the proxy file
// "../../../char/<c>/og/<c>.milo", the environ).
class CharacterObj : public Object {
public:
    bool band = false;            // BandCharacter
    bool own = false;             // the directory's own record
    ObjectDirData objectDir;
    RndDirData rnd;
    struct Lod {
        float screenSize = 0;     // 0x82156F50
        std::string group;        // ObjPtr<RndGroup>
    };
    std::vector<Lod> lods;        // own record only
    std::string shadow;           // ObjPtr<RndGroup> 0x82131680
    bool selfShadow = false;
    std::string sphereBase;       // ObjPtr<RndTransformable>
    struct Test {                 // CharacterTest v6 (0x8219B7E0), editor only
        std::string driver, clip1, clip2, teleportTo, showDistMap, sym6c;
        std::vector<std::string> walkPath;
        uint32_t transition = 0, internalTransition = 0, showFootsteps = 0, u74 = 0;
        bool cycleTransition = false, b69 = false, b6a = false, b6b = false, b70 = false, b71 = false;
    } test;
};
// Decodes a Character / BandCharacter directory's own record (Directory::self
// of a char milo, which the milo parser keeps raw). Throws on malformed data.
std::unique_ptr<CharacterObj> decodeCharacterDir(const Object& self);

// CharDriver v5 / PS2 v3 (Load 0x8216A728, PreLoad 0x8216BE80, PostLoad 0x8216C800).
class CharDriver : public Object {
public:
    CharWeightable weightable;
    std::string bones;            // ObjPtr<CharBones>: the servo it writes ("bone.servo")
    std::string clips;            // FilePath of the CharClipSet ("../../anims/alterna1_main.milo")
    bool realign = false;         // rev >= 2 (+0x54); TRUE only on metal_drummer main.drv
    bool unknown55 = false;       // rev >= 5 (+0x55); 0 on the disc (Q: skips the bones' ScaleDown)
    std::string starvedClip;      // PostLoad rev >= 4: ObjPtr<CharClip> (Q: name)
};

// CharDriverMidi v3 (0x82184AB0 / Pre 0x82185590 / Post 0x82185600).
class CharDriverMidi : public CharDriver {
public:
    std::string inactiveClip;     // ObjPtr<CharClip> (Q: name; set_inactive_clip)
};

// CharIKHand v5 / PS2 v2: one target, no finger (0x82174470).
class CharIKHand : public Object {
public:
    CharWeightable weightable;
    std::string hand, finger;     // ObjPtr<Trans>
    std::vector<std::string> targets;
    bool orient = false, stretch = false, scaleX = false, moveElbow = true;  // Q: names
};

// CharIKMidi v4 (0x82184950).
class CharIKMidi : public Object {
public:
    std::string bone;             // ObjPtr<Trans> ("bone_fret.mesh")
};

// CharHair v2 (0x8217FAF0).
class CharHair : public Object {
public:
    std::array<float, 6> params{};  // Q: stiffness, torsion, inertia, gravity, weight, friction
    struct Point {
        std::array<float, 3> pos{};
        std::string bone;
        float length = 0, f50 = 0;
        std::string collide;
        float f60 = 0, f64 = 0;
    };
    struct Strand {
        std::string root;
        float angle = 0;
        std::vector<Point> points;
        std::array<float, 9> baseMat{}, baseMat2{};
    };
    std::vector<Strand> strands;
    bool simulate = false;
};

// CharLookAt v3 / PS2 v2, allowRoll true (0x8216EBB0).
class CharLookAt : public Object {
public:
    CharWeightable weightable;
    std::string source, pivot, dest;
    std::array<float, 8> values{};  // Q: half_time, min/max yaw, min/max pitch, then 3 more
    bool allowRoll = true;          // Q: name; rev < 3 (PS2): true
};

// CharEyes v4 / PS2 v3, no interests (0x82172708).
class CharEyes : public Object {
public:
    std::vector<std::string> eyes;  // CharLookAt names
    std::string viewDirection;
    std::vector<std::pair<std::string, uint32_t>> interests;  // none on the disc
};

// CharWeightSetter v3 / PS2 v2, invert false (0x8218DFC0).
class CharWeightSetter : public Object {
public:
    CharWeightable weightable;
    std::string driver;             // ObjPtr<CharDriver>
    uint32_t flags = 0;             // kGuitarLeftIK 0x400000 / kGuitarRightIK 0x800000
    bool invert = false;            // Q: name
};

// CharUpperTwist v1 (0x82182C00).
class CharUpperTwist : public Object {
public:
    std::string upperArm, twist1, twist2;
};

// CharForeTwist v1 (0x82182018).
class CharForeTwist : public Object {
public:
    float offset = 0;               // degrees (+-90)
    std::string hand, twist2;
};

// CharServoBone v1 (0x82188870) and CharWalk v1 (0x82189F80): header only.
class CharServoBone : public Object {};
class CharWalk : public Object {};

// CharPosConstraint v2 (0x82179178).
class CharPosConstraint : public Object {
public:
    std::vector<std::string> targets;
    std::string src;
    std::array<float, 3> boxMin{}, boxMax{};
};

// CharIKRod v2 (0x821806F8).
class CharIKRod : public Object {
public:
    std::string leftEnd, rightEnd;
    float destPos = 0;
    std::string sideAxis;           // Q: name
    bool vertical = false;
    std::string dest;
    std::array<float, 12> xfm{};
};

// OutfitLoader v2 / PS2 v1, no category names (PreLoad 0x8215B528, Load 0x82157CC8).
class OutfitLoader : public Object {
public:
    std::string path;               // base folder ("../../../ng")
    struct Category {
        uint8_t current = 0, next = 0;
        std::string name;
        std::vector<std::pair<uint8_t, uint8_t>> items;
    };
    std::vector<Category> categories;
};

// FaceFxLipSyncServo v5 (Load 0x8218E378, PreLoad 0x8218FAF0, PostLoad 0x82192470).
class FaceFxLipSyncServo : public Object {
public:
    CharWeightable weightable;
    std::string faceGraph;          // ".fac" path
    std::string visemeSet;          // viseme CharClipSet path
    struct Entry {
        std::string object;
        uint32_t axis = 0;
        std::string node;
    };
    std::vector<Entry> entries;
};

// Classes decoded here (objects.cpp asks before its own table).
bool isCharClass(const std::string& cls);
std::unique_ptr<Object> makeCharObject(const std::string& cls);
// Decodes `o` (cls set, version read) from record[pos] (after the version);
// advances pos. Throws on malformed data.
void readCharObject(Object& o, const Bytes& record, size_t& pos);

}  // namespace scene
