// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "data/bytes.h"
#include "scene/objects.h"

// GH2's venue classes (world/ milos), as their Load routines in the Xbox 360
// executable read them (gd-game-03/ghidra/analysis; each Load found through the
// class's RTTI type descriptor -> vtable, Hmx::Object sub-vtable slot 9).
// Every record of every class on the disc decodes exactly to its end marker
// (--scenetest). Field names come from each class's SyncProperty strings;
// "Q:" marks names that are still guesses. The WorldDir directory record is
// decoded with the other directories (objects.h, DirObj::world).

namespace scene {

// An object reference a CamShot keyframe aims at: u32 (a small revision,
// < 0x1c), the object (ObjPtr name @ 0x8213dfe8) and a subpart symbol
// (a bone, @ 0x82359638), e.g. (guitarist0, bone_spine2.mesh).
struct CamTarget {
    uint32_t rev = 0;
    std::string object, subpart;
};

// CamShot v27 (Load @ 0x8222de20): a camera shot of the venue's director.
// Frames run at 30 a second. The object header's type is "band" and its
// properties are the director's flags (camshot.dta: distance, facing, solo,
// special, walk_ok, starpower_ok, postprocess, next_shot...).
class CamShot : public Object {
public:
    struct Keyframe {
        float duration = 0;      // frames held
        float blend = 0;         // frames blending in from the previous keyframe (or camera)
        float blendEase = 0;     // ease power of that blend
        float fieldOfView = 0;   // radians
        Xfm xfm;                 // the camera (world, or under `parent`)
        std::array<float, 2> screenOffset{};  // where the target is framed
        float blurDepth = 0, maxBlur = 0, focusBlurMultiplier = 0;  // depth of field
        std::vector<CamTarget> targets;
        CamTarget focalTarget;   // +0xec (Q: order of focal / parent from the field offsets)
        CamTarget parent;        // +0x9c: the keyframe moves with it
        bool useParentRotation = false;
        float shakeNoiseAmp = 0, shakeNoiseFreq = 0;
        std::array<float, 2> shakeMaxAngle{};
        float zoomFov = 0;
    };
    Animatable anim;
    std::vector<Keyframe> keyframes;
    bool looping = false;
    float pathEase = 1;
    float nearPlane = 1, farPlane = 1000;
    bool useDepthOfField = false;
    float filter = 0;         // the parent's low-pass
    float clampHeight = -1;   // Q: meaning
    std::string path;         // a TransAnim the camera follows
    float fadeTime = 0;       // Q: meaning
    std::string category;     // INTRO, flr_near_lft, SOLO_FAR...
    float selectionWeight = 1;
    std::vector<std::pair<uint32_t, uint32_t>> crowd3d;  // Q: 3D crowd entries
    uint32_t unused = 0;      // read (rev >= 8) and dropped
    std::vector<std::string> hideList;  // objects hidden while the shot runs
    std::string crowd;        // the WorldCrowd it shows
    std::string glowSpot;     // Q: a spotlight

    // The length in frames: every keyframe's duration and blend.
    float lengthFrames() const;
    // A director flag from the header's properties (camshot.dta), e.g.
    // flag("facing") -> "left"; empty when absent.
    std::string flag(const std::string& key) const;
};

// LightPreset v9 (Load @ 0x82243660): a lighting look, keyframes of every
// spotlight, environment and light of its lists.
class LightPreset : public Object {
public:
    struct SpotEntry {
        float intensity = 0;
        std::array<float, 4> rotation{};  // quaternion
        std::array<float, 4> color{};
        std::string target, subpart;
        bool flare = false;
    };
    struct EnvEntry {
        std::array<float, 4> ambient{};
        bool fogEnable = false;
        float fogStart = 0, fogEnd = 0;
        std::array<float, 4> fogColor{};
    };
    struct LightEntry {
        std::array<float, 4> rotation{};  // quaternion
        std::array<float, 3> position{};
        std::array<float, 4> color{};
        float range = 0;
        float falloff = 0;  // Q: name
    };
    struct Keyframe {
        float duration = 0, fadeOut = 0;  // frames (rate 1: 480 a beat; 0: 30 a second)
        std::vector<SpotEntry> spots;     // per `spotlights`
        std::vector<EnvEntry> environs;   // per `environs`
        std::vector<LightEntry> lights;   // per `lights`
        std::string description;
    };
    Animatable anim;
    std::vector<Keyframe> keyframes;
    std::vector<std::string> spotlights, environs, lights;
    bool looping = false;
    std::string category;   // INTRO, VERSE, CHORUS...
    std::vector<std::string> adjectives;  // blackout strobe flare...
    std::string trigger;
    float fadeIn = 0;       // in 480ths of a beat
    int minExcitement = 0, maxExcitement = 0;
    bool manual = false, locked = false;
};

// Spotlight v25 (Load @ 0x82238098): a stage light with its beam, floor
// spot, flare and lens.
class Spotlight : public Object {
public:
    Draw draw;
    Trans trans;
    float spotScale = 0, spotHeight = 0;
    struct Beam {
        bool isCone = false;
        float length = 0, bottomRadius = 0, topRadius = 0;
        float topSideBorder = 0, bottomSideBorder = 0, bottomBorder = 0;
        std::string material;
        float offset = 0;
        std::array<float, 2> angleOffset{};
        float brighten = 0;
        std::string xsection;
        float expand = 0;
        std::vector<std::string> cutouts;
    } beam;
    std::string lightCan, target;
    float lightCanOffset = 0;
    std::array<float, 4> color{};
    float intensity = 0;
    std::string spotMaterial;
    float dampingConstant = 0;
    std::string targetSubpart;
    std::string flareMaterial;
    std::array<float, 2> flareSize{}, flareRange{};
    uint32_t flareSteps = 0;
    float flareOffset = 0;
    bool flareEnabled = false;
    bool flareVisibilityTest = false;  // stored inverted (Q)
    float lensSize = 0, lensOffset = 0;
    std::string lensMaterial;
    std::vector<std::string> additionalObjects;
    bool targetShadow = false, animateFromPreset = false;
};

// WorldCrowd v6 (Load @ 0x8224a090): the audience, archetype characters
// placed over a mesh. Its object header comes last, not first.
class WorldCrowd : public Object {
public:
    Draw draw;
    std::string placementMesh;
    uint32_t num = 0;
    bool rotate = false;
    struct Character {
        std::string character;  // archetype milo, e.g. char/crowd/og/crowd_male01.milo
        float height = 0, density = 0, radius = 0;
        std::vector<Xfm> placements;
    };
    std::vector<Character> characters;
    float unknown = 0;  // Q: f32 before show_3d_only
    bool show3dOnly = false;
};

// EventTrigger v10 (Load @ 0x8225d708): what a venue event (hit_p0_fret1,
// chorus, game_won...) starts.
class EventTrigger : public Object {
public:
    struct AnimEntry {
        std::string anim;
        float blend = 0;
        bool wait = false;   // Q: u8 / f32 order
        float delay = 0;
        bool enable = false; // the filter below is used
        uint32_t rate = 0;
        float start = 0, end = 0, scale = 1;
        std::string type;    // range / loop / shuttle
        float period = 0;
    };
    std::vector<std::string> triggerEvents;
    std::vector<AnimEntry> anims;
    std::vector<std::string> sounds, shows;
    std::vector<std::pair<std::string, float>> hideDelays;  // object, delay (seconds)
    std::vector<std::string> enableEvents, disableEvents, waitForEvents;
    std::string nextRandom;
    std::vector<std::pair<std::string, std::string>> proxyCalls;  // proxy dir, message
};

// RndPostProc v9 (Load @ 0x821ec558 -> 0x821eb3d0): a shot's bloom, colour
// transform and noise.
class PostProc : public Object {
public:
    std::array<float, 4> bloomThreshold{};  // rgb + w
    float bloomScale = 0, bloomIntensity = 0, bloomKernelMult = 0;
    std::string luminanceMap;
    std::array<float, 12> colorXfm{};
    std::array<float, 2> intensity{}, secsRange{}, noiseBaseScale{};  // Q: names
    float unknownD0 = 0;    // Q: +0xd0
    float noiseIntensity = 0;
    std::string noiseMap;
    float hue = 0, saturation = 0, lightness = 0, contrast = 0, brightness = 0;
    float threshold = 0, duration = 0, emulateFps = 0;  // Q: names
};

// RndFlare v4 (Load @ 0x8220ddb8): a lens flare sprite.
class Flare : public Object {
public:
    Trans trans;
    Draw draw;
    std::string mat;
    std::array<float, 2> size{}, range{};
    uint32_t steps = 0;
};

// RndLightAnim v2 (Load @ 0x82205cc0): a light's colour over time.
class LightAnim : public Object {
public:
    Animatable anim;
    std::string light, keysOwner;
    std::vector<Key<4>> colorKeys;
};

// RndEnvAnim v4 (Load @ 0x8221fbe0): an environment's ambient and fog over time.
class EnvAnim : public Object {
public:
    Animatable anim;
    std::string environName, keysOwner;
    std::vector<Key<4>> ambientKeys, fogColorKeys;
    std::vector<Key<2>> fogRangeKeys;  // (start, end)
};

// RndCamAnim v2 (Load @ 0x82206b78): field-of-view keys (the two on the
// disc have none; not played).
class CamAnim : public Object {
public:
    Animatable anim;
    std::string cam, keysOwner;
    std::vector<Key<1>> fovKeys;
};

// RndSet v0 (Load @ 0x8221b188): a named list of objects.
class SetObj : public Object {
public:
    std::vector<std::string> objects;
};

// RndCubeTex v1 (PreLoad @ 0x822092e0, PostLoad @ 0x82209618): six faces
// (right, left, top, bottom, front, back = +x -x +y -y +z -z), stored inline.
class CubeTex : public Object {
public:
    uint32_t faceSize = 0, bpp = 0;
    std::array<std::string, 6> files;
    bool inlineBitmaps = false;
    std::array<Bitmap, 6> faces;
};

// Waypoint v3 (Load @ 0x82197a38): a spot the band walks to.
class Waypoint : public Object {
public:
    Draw draw;
    Trans trans;
    uint32_t flags = 0;
    std::vector<std::string> connections;
    float radius = 0;
    std::array<float, 2> unknown{};  // Q: two f32
};

// Classes decoded here (objects.cpp asks before its own table).
bool isWorldClass(const std::string& cls);
std::unique_ptr<Object> makeWorldObject(const std::string& cls);
// Decodes `o` (cls set, version read) from record[pos] (after the version);
// advances pos. Throws on malformed data.
void readWorldObject(Object& o, const Bytes& record, size_t& pos);

}  // namespace scene
