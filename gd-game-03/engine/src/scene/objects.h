// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "data/bytes.h"
#include "scene/anim.h"
#include "scene/bitmap.h"

// GH2's Harmonix scene classes, as their Load routines in the Xbox 360
// executable read them (docs/milo_and_textures.md; addresses are the
// functions in gd-game-03/ghidra/analysis). Each record starts with the class's own
// version; only the versions on the GH2 disc are decoded. Field names come
// from each class's property table (its SyncProperty strings) where the
// executable names them; "Q:" marks names that are still guesses.

namespace scene {

class Directory;

// A DataArray / DataNode as BinStream stores it (DataArray::Load @ 0x8231aa20,
// DataNode::Load @ 0x82318c28): used by the object property blocks.
struct DataNode {
    int type = 0;           // 0 int, 1 float, 2 var, 5 symbol, 6 kDataUnhandled, 0x10.. arrays, 0x12 string, ...
    uint32_t raw = 0;       // int / float bits
    std::string str;        // symbols, strings, vars, objects
    std::vector<DataNode> children;  // for array types (0x10 array, 0x11 command, 0x13 property)
    int line = 0;
    float asFloat() const;
};

// Hmx::Object::Load @ 0x82315350: u32 revision, symbol type, the property
// array (u8 present + DataArray), and (revision > 0) a note string.
// The usual 13 bytes are 1, "", no props, "".
struct ObjectHeader {
    int revision = 0;
    std::string type;
    bool hasProps = false;
    DataNode props;
    std::string note;
};

// Row-vector affine transform, as the game stores it: p' = p * rot + pos
// (rows of rot are the local axes).
struct Xfm {
    std::array<float, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<float, 3> pos{0, 0, 0};
};
Xfm compose(const Xfm& a, const Xfm& b);  // a then b

// RndTransformable v9 (Load @ 0x821b0a28).
struct Trans {
    Xfm local, world;
    // 0 None, 1 LocalRotate, 2 ParentWorld, 3 LookAtTarget, 4 ShadowTarget,
    // 5 BillboardZ, 6 BillboardXZ, 7 BillboardXYZ, 8 FastBillboardXYZ
    // (the v<9 remap in the Load routine; Q: names follow GH1's enum).
    int constraint = 0;
    std::string target;
    bool preserveScale = false;
    std::string parent;
};

// RndDrawable v3 (Load @ 0x821b36c0): showing, sphere, draw_order.
struct Draw {
    bool showing = true;
    std::array<float, 4> sphere{};  // centre xyz, radius
    float order = 0;
};

// RndAnimatable v4 (Load @ 0x821e96c0): two u32s. `rate` is the "rate"
// property (Q: enum values; v<4 stored a bool where true gave 0).
struct Animatable {
    uint32_t unknown = 0;  // Q: read for revision > 1, no property name found yet
    uint32_t rate = 0;
};

// Every object: its table entry plus the record bytes.
class Object {
public:
    virtual ~Object() = default;

    std::string cls, name;
    uint32_t version = 0;   // the record's first u32 (the class's own revision)
    ObjectHeader header;
    Directory* dir = nullptr;
    Bytes record;           // raw record (without the 0xADDEADDE marker)
    bool typed = false;     // a decoder exists for this class
    bool decoded = false;   // the decoder read the record exactly to its end
    std::string error;      // why decoding failed

    template <class T> T* as() { return dynamic_cast<T*>(this); }
    template <class T> const T* as() const { return dynamic_cast<const T*>(this); }
};

// RndTex v10 (PreLoad @ 0x821d62d0, PostLoad @ 0x821d6438).
class Tex : public Object {
public:
    int width = 0, height = 0, bpp = 0;
    std::string file;       // source path, e.g. "../textures/gem_green.bmp"
    float mipMapK = 0;      // Q: name from GH1 (usually -8)
    int type = 1;           // 1 Regular, 0x22 RenderedNoZ (track fade targets), GH1's enum
    bool useExtPath = false;
    Bitmap bitmap;          // empty for placeholders filled at runtime
};

// RndMat v28 (Load @ 0x821eebb8; names from SyncProperty @ 0x821ed930).
class Mat : public Object {
public:
    int blend = 1;                    // Q: enum (GH1: 0 Dest, 1 Src, 2 Add, 3 SrcAlpha, 4 SrcAlphaAdd, 5 Subtract)
    std::array<float, 4> color{1, 1, 1, 1};  // RGB + alpha
    bool useEnviron = false, prelit = false;
    int zMode = 1;
    bool alphaCut = false, alphaWrite = false;
    int texGen = 0, texWrap = 0;
    Xfm texXfm;
    std::string diffuseTex, nextPass;
    bool intensify = false, cull = true;
    float emissiveMultiplier = 1;
    std::array<float, 3> specularRgb{};
    float specularPower = 0;
    std::string normalMap, emissiveMap, specularMap, customDiffuseMap, environMap;
    bool perPixelLit = false, normalize = false;
    int stencilMode = 0;
};

struct MeshVertex {
    float pos[3], normal[3];
    float color[4];  // vertex colour RGBA, or bone weights when the mesh has bones
    float uv[2];
};

// RndMesh v28 (Load @ 0x821c2b60).
class Mesh : public Object {
public:
    Trans trans;
    Draw draw;
    std::string mat, geomOwner;
    uint32_t mutableParts = 0, volume = 0;
    std::vector<MeshVertex> verts;
    std::vector<std::array<uint16_t, 3>> faces;
    std::vector<uint8_t> groupSizes;   // faces per group (u8 each)
    struct Bone {
        std::string name;
        Xfm offset;
    };
    std::vector<Bone> bones;           // up to 4
};

// RndTransformable on its own (class "Trans", v9).
class TransObj : public Object {
public:
    Trans trans;
};

// RndGroup v12 (Load @ 0x821dc6d0).
class Group : public Object {
public:
    Animatable anim;
    Trans trans;
    Draw draw;
    std::vector<std::string> objects;
    std::string environment, lod;  // "environ" and "lod" properties
    float lodScreenSize = 0;
};

// RndCam v12 (Load @ 0x821d8c38).
class Cam : public Object {
public:
    Trans trans;
    float nearPlane = 1, farPlane = 1000, yFov = 0.8f;  // y_fov in radians
    std::array<float, 4> screenRect{0, 0, 1, 1};  // Q: name (x, y, w, h)
    std::array<float, 2> zRange{0, 1};            // Q: name
    std::string targetTex;                        // Q: name
};

// RndEnviron v5 (Load @ 0x821d9f68; names from SyncProperty @ 0x821da558).
class Environ : public Object {
public:
    std::vector<std::string> lights;
    std::array<float, 4> ambientColor{0, 0, 0, 1};
    float fogStart = 0, fogEnd = 0;
    std::array<float, 4> fogColor{};
    bool fogEnable = false;
    bool animateFromPreset = false;
    bool fadeOut = false;
    float fadeStart = 0, fadeEnd = 0;
};

// RndLight v9 (Load @ 0x8220c3e8; names from SyncProperty @ 0x8220c888).
class Light : public Object {
public:
    Trans trans;
    std::array<float, 4> color{1, 1, 1, 1};
    float range = 0;
    int type = 0;  // Q: GH1: 0 Point, 1 Directional; GH2 adds spot types
    bool animateColorFromPreset = false, animatePositionFromPreset = false;
    float topRadius = 0, botRadius = 0, softness = 0, displacement = 0;
    std::string texture;
    std::vector<std::string> shadowcasters;
};

// RndTransAnim v6 (Load @ 0x821b6230).
class TransAnim : public Object {
public:
    Animatable anim;
    std::string trans;      // the animated Trans
    std::vector<Key<4>> rotKeys;    // quaternion x y z w
    std::vector<Key<3>> transKeys, scaleKeys;
    std::string keysOwner;  // the TransAnim whose keys are used (empty / self = own)
    bool transSpline = false, repeatTrans = false, scaleSpline = false, followPath = false, rotSlerp = false;  // Q: names from GH1
};

// RndMatAnim v7 (Load @ 0x82204590).
class MatAnim : public Object {
public:
    Animatable anim;
    std::string mat, keysOwner;
    std::vector<Key<4>> colorKeys;   // RGBA
    std::vector<Key<1>> alphaKeys;
    std::vector<Key<3>> transKeys, scaleKeys, rotKeys;  // texture transform
    std::vector<TexKey> texKeys;
};

// RndAnimFilter v1 (Load @ 0x82207d88; names from SyncProperty @ 0x822081f8).
class AnimFilter : public Object {
public:
    Animatable animatable;
    std::string anim;  // the filtered animation
    float scale = 1, offset = 0, start = 0, end = 0, period = 0;
    int type = 0;      // Q: enum (later Harmonix code: 0 Range, 1 Loop, 2 Shuttle)
    // The filtered frame for `frame` (0x822079B0); `framesPerUnit` of the
    // filter's rate, for a period.
    float filter(float frame, float framesPerUnit = 30) const;
    float scaleOf(float framesPerUnit = 30) const;
    float endFrame(float framesPerUnit = 30) const;
};

// RndParticleSys v30 (Load @ 0x82216ad0). Ranges are (low, high) pairs a
// particle draws from; names follow GH1's v22 record, whose fields GH2 keeps
// in the same order, and the object layout the particle code reads (init
// 0x82213E58, move 0x82212A48, emit 0x82214620, poll 0x822158A8): times are
// frames (30 a second), speeds units per frame, angles radians.
class ParticleSys : public Object {
public:
    Animatable anim;
    Trans trans;
    Draw draw;
    std::array<float, 2> life{}, speed{}, pitch{}, yaw{}, emitRate{}, startSize{}, deltaSize{};
    std::array<float, 3> posLow{}, posHigh{}, force{};
    std::array<float, 4> startColorLow{}, startColorHigh{}, endColorLow{}, endColorHigh{};
    std::string bounce;     // v26+: the collision plane's Trans ("%s_bounce.trans")
    std::string mat;
    int type = 0;           // 1: the "fancy" particles (grow / shrink, mid colour, drag, spin)
    float growRatio = 0, shrinkRatio = 0, midColorRatio = 0;
    std::array<float, 4> midColorLow{}, midColorHigh{};
    int maxParticles = 0;
    std::array<float, 2> bubblePeriod{}, bubbleSize{};
    bool bubble = false;
    bool spin = false;                  // v30: particles turn (object +0x27f)
    std::array<float, 2> spinSpeed{};   // v30: turns a minute (x 2 pi / 1800 per frame), random sign
    float spinDamping = 0;              // v30: spin x (1 - d) every 30 frames
    float drag = 0;                     // v30: speed x (1 - d) every 30 frames
    float relativeMotion = 0;           // 1: particles live in the parent's space; 0: world space
    std::string relativeParent;         // v27+: that parent (usually the system itself)
    std::string mesh;                   // mesh emitter
    bool frameDrive = true;             // v28+: runs on its animation frame (else on the clock x 30)
    bool preSpawn = false;              // v29+: warms up on the first poll
    struct Preserved {
        std::array<float, 3> pos;
        std::array<float, 4> color;
        float size;
    };
    std::vector<Preserved> preserved;  // "preserve particles": baked at load, drawn as is
};

// RndParticleSysAnim v3 (Load @ 0x82201bc0).
class ParticleSysAnim : public Object {
public:
    Animatable anim;
    std::string partSys, keysOwner;
    std::vector<Key<4>> startColorKeys, endColorKeys;
    std::vector<Key<2>> emitRateKeys, speedKeys, lifeKeys, startSizeKeys;
};

// RndMeshAnim v1 (Load @ 0x821fe530): per key, every vertex's position / UV / colour.
class MeshAnim : public Object {
public:
    Animatable anim;
    std::string mesh, keysOwner;
    template <class T> struct VertKey {
        std::vector<T> values;
        float frame = 0;
    };
    std::vector<VertKey<std::array<float, 3>>> pointKeys;
    std::vector<VertKey<std::array<float, 2>>> uvKeys;
    std::vector<VertKey<std::array<float, 4>>> colorKeys;
};

// RndFont v15 (Load @ 0x821d4cd8): a glyph sheet (the material's texture)
// with a stored glyph table (render/text_layout.h has the layout rules).
class Font : public Object {
public:
    std::string mat;
    std::array<float, 2> cellSize{};  // one cell in texels (+0x1048)
    float deprecatedSize = 0;
    float spacing = 0;      // added between every glyph pair, cell widths (+0x1044; Q: name)
    std::string chars;
    std::vector<std::pair<uint32_t, float>> kerning;  // (first | second << 8, amount in cell widths)
    std::string textureOwner;  // the font whose glyph table and kerning are used (itself when empty)
    bool monospace = false;    // v11+ (+0x1068): advance 1 cell, no kerning (Q: name)
    bool unknown1074 = false;  // v15+ (+0x1074; Q: not used by the layout)
    uint32_t texWidth = 0, texHeight = 0;  // v13+: the sheet size the cells were made for
    std::array<float, 2> cellUv{};         // v14+: one cell in UV (+0x106C)
    struct Glyph {
        float x = 0, y = 0;     // UV of the cell's top-left corner
        float width = 0;        // quad width in cell widths
        float advance = 0;      // pen advance in cell widths
    };
    std::array<Glyph, 256> glyphs{};
};

// RndText v17 (Load @ 0x821d1a68).
class Text : public Object {
public:
    Draw draw;
    Trans trans;
    std::string font, text;
    uint32_t align = 0;
    std::array<float, 4> color{1, 1, 1, 1};
    float wrapWidth = 0, leading = 1, italics = 0, size = 1;
    uint32_t fixedLength = 0;
    bool markup = false;
    uint32_t capsMode = 0;
};

// The ObjectDir family. A directory record is the class chain's PreLoad data
// then its PostLoad data (each class calls its base first). Some fields are
// only stored when the record is the directory's own (the first record of a
// milo, `own == true`), not when the directory object is a proxy in another
// directory's table.
//
// ObjectDir v17 (PreLoad @ 0x82322598, PostLoad @ 0x8231ff08).
struct ObjectDirData {
    std::vector<Xfm> viewports;  // editor views (Transform + an unused u32 each)
    uint32_t currentViewport = 0;
    std::string proxyFile;        // "../track_new.milo" for proxies
    std::vector<std::string> subdirs;
    // 1 (or 2) with a proxy file: the proxied directory is stored inline,
    // right after this PreLoad data (the crowd lighters).
    uint32_t inlineProxy = 0;
    std::string postRef0, postRef1;  // Q: two object names PostLoad resolves (e.g. "anim.grp", "track.cam")
};
// RndDir v9 (PreLoad @ 0x821ba438, PostLoad @ 0x821ba4c8).
struct RndDirData {
    Animatable anim;
    Draw draw;
    Trans trans;
    std::string environment;  // "environ"
};
// PanelDir v2 (PreLoad @ 0x82374378, PostLoad @ 0x823743f0); own record only.
struct PanelDirData {
    std::string cam;
    std::string enterTrigger;  // Q: symbol, "ui_enter" everywhere
};
// TrackDir v5 (PreLoad @ 0x82364010, PostLoad @ 0x82365268; names from
// SyncProperty @ 0x82364dd0); own record only.
struct TrackDirData {
    std::string drawGroup, animGroup;
    float yPerSecond = 0, topY = 0, bottomY = 0;
    std::vector<std::array<float, 2>> slots;  // per fret: (x, z) Q: second value
    bool warnOnResort = false;
    std::string legacyWidget;  // v>3: a string and a u32 the loader reads and drops
    uint32_t legacyValue = 0;
};
// GHTrackDir v3 (PreLoad @ 0x8213d488, PostLoad @ 0x8213b648).
struct GHTrackDirData {
    int32_t unknown = 0;  // Q: u32 read for v>1 (0 in track_new, -1 in the panel proxies)
    std::string spCamera;              // own record only
    std::vector<std::string> mpCameras;
    // TrackWidgets per slot: 5 frets, then bonus and miss (gem / hopo), or
    // 5 frets then miss (star / star hopo).
    std::vector<std::string> gemWidgets, hopoWidgets, starWidgets, starHopoWidgets;
};

// GH2's HUD directories, each a RndDir with its own fields read before the
// RndDir PreLoad. The fields are kept in load order: object names (`refs`,
// lists flattened with a count in `values`) and numbers (`values`). Names are
// open (Q:), the order per class is:
//   BandCrowdMeterDir v3 (PreLoad @ 0x82139630): u32; own: 4 anims (filters), 1 group.
//   BandScoreDisplay v3 (@ 0x8212d5e8): own: mesh list, mat list, mat; u32, u32.
//   BandStarMeterDir v5 (@ 0x821321a8): own: 3 groups, 2 anims, 1 group; u32, u32, u8, u8.
//   BandStreakDisplay v4 (@ 0x8212e780): own: matanim, anim, mat, obj, mat list,
//     obj list, mat, mat list, obj, obj, mat, mat, obj, obj, mat, u32; u32, u8, u32, u32.
//   BandLeadMeter v8 (@ 0x82128490): u32; own: 6 anims/objs, 4 objs/mats.
struct HudDirData {
    std::vector<std::string> refs;
    std::vector<uint32_t> values;
};
// WorldDir v12 (PreLoad @ 0x8224e5c0, PostLoad @ 0x8224de60; the venue's
// own directory, world/<v>/gen/<v>[_mp|_coop].milo): PreLoad reads u32 0,
// u32 1.0f (Q: names) and the HUD milo, then PanelDir's; PostLoad adds
// after PanelDir's the default camera transform, sub-directory hides and
// bitmaps (own record only).
struct WorldDirData {
    uint32_t unknown0 = 0, unknown1 = 0;  // Q: 0 and 0x3f800000 everywhere
    std::string hudFile;                   // e.g. "../../../hud/hud_1p_nocam.milo"
    Xfm camXfm;
    std::vector<std::string> subdirHides;
    std::vector<std::pair<std::string, std::string>> subdirBitmaps;
};

class DirObj : public Object {
public:
    bool own = false;  // the directory's own record
    ObjectDirData objectDir;
    bool hasRnd = false, hasPanel = false, hasTrack = false, hasGHTrack = false;
    RndDirData rnd;
    PanelDirData panel;
    TrackDirData track;
    GHTrackDirData ghTrack;
    bool hasHud = false;
    HudDirData hud;
    bool hasWorld = false;
    WorldDirData world;
};

// TrackWidget v4 (Load @ 0x82367260; names from SyncProperty @ 0x82366a58):
// a highway element type (gem, tail part, bar line...) drawn as copies of its meshes.
class TrackWidget : public Object {
public:
    Draw draw;
    std::vector<std::string> meshes;
    std::string environment;  // "environ"
    float baseLength = 0;
    bool multiMesh = false, allowRotation = false;
};

// Directory classes decoded by decodeDir (ObjectDir, RndDir, PanelDir, GHTrackDir,
// WorldDir and the Band* HUD directories above).
bool isDirClass(const std::string& cls);
// Decodes a directory record that starts at stream[start]. `inlineDir` is
// called with the position where an inline proxied directory starts and
// returns the position after it. On return `end` is the position of the
// record's end marker (checked). Throws on malformed data.
std::unique_ptr<DirObj> decodeDir(const std::string& cls, const std::string& name, const Bytes& stream, size_t start,
                                  bool own, const std::function<size_t(size_t)>& inlineDir, size_t& end);

// The shared sub-records for decoders in other files (scene/ui_objects.cpp):
// each reads at record[pos] and advances pos; throws on malformed data.
ObjectHeader readObjectHeaderAt(const Bytes& record, size_t& pos);
void readTransAt(const Bytes& record, size_t& pos, Trans& t);
void readDrawAt(const Bytes& record, size_t& pos, Draw& d);

// Classes this decoder knows (all objects of these classes are typed).
bool isTypedClass(const std::string& cls);
// Creates and decodes an object of `cls` from its record. Never throws:
// failures are recorded in error / decoded.
std::unique_ptr<Object> decodeObject(const std::string& cls, const std::string& name, const uint8_t* data, size_t size);

}  // namespace scene
