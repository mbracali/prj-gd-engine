// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "data/bitmap.h"
#include "data/bytes.h"

// Scene files (.rnd_ps2): Harmonix object directories. Decoders mirror the
// game's Load routines for the versions shipped on the disc; see
// tools/rnd_classes/*.py for the annotated originals and FINDINGS.md.

// Row-vector affine transform: p' = p * rot + pos (rot rows are the axes).
struct Xfm {
    std::array<float, 9> rot{1, 0, 0, 0, 1, 0, 0, 0, 1};
    std::array<float, 3> pos{0, 0, 0};
};

// a then b: p * a * b for row vectors.
inline Xfm compose(const Xfm& a, const Xfm& b) {
    Xfm r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            float s = 0;
            for (int k = 0; k < 3; ++k) s += a.rot[i * 3 + k] * b.rot[k * 3 + j];
            r.rot[i * 3 + j] = s;
        }
    for (int j = 0; j < 3; ++j) {
        float s = b.pos[j];
        for (int k = 0; k < 3; ++k) s += a.pos[k] * b.rot[k * 3 + j];
        r.pos[j] = s;
    }
    return r;
}

using Quat = std::array<float, 4>;

// Normalised lerp along the shorter arc (FUN_0024aa38).
inline Quat nlerp(const Quat& a, Quat b, float t) {
    float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    if (dot < 0)
        for (float& v : b) v = -v;
    Quat q;
    float len = 0;
    for (int i = 0; i < 4; ++i) {
        q[i] = a[i] + (b[i] - a[i]) * t;
        len += q[i] * q[i];
    }
    len = len > 0 ? 1.0f / std::sqrt(len) : 1;
    for (float& v : q) v *= len;
    return q;
}

// FUN_0024ae78, into the game's row-major rotation.
inline void quatToRot(const Quat& q, std::array<float, 9>& m) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    m = {1 - 2 * y * y - 2 * z * z, 2 * x * y + 2 * z * w, 2 * x * z - 2 * y * w,
         2 * x * y - 2 * z * w, 1 - 2 * z * z - 2 * x * x, 2 * y * z + 2 * x * w,
         2 * x * z + 2 * y * w, 2 * y * z - 2 * x * w, 1 - 2 * x * x - 2 * y * y};
}

// The quaternion of a pure rotation (inverse of quatToRot).
inline Quat rotToQuat(const std::array<float, 9>& m) {
    float tr = m[0] + m[4] + m[8];
    Quat q;
    // Inverse of quatToRot: m[1] - m[3] = 4zw, m[5] - m[7] = 4xw, m[6] - m[2] = 4yw.
    if (tr > 0) {
        float s = std::sqrt(tr + 1) * 2;
        q = {(m[5] - m[7]) / s, (m[6] - m[2]) / s, (m[1] - m[3]) / s, 0.25f * s};
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = std::sqrt(1 + m[0] - m[4] - m[8]) * 2;
        q = {0.25f * s, (m[1] + m[3]) / s, (m[2] + m[6]) / s, (m[5] - m[7]) / s};
    } else if (m[4] > m[8]) {
        float s = std::sqrt(1 + m[4] - m[0] - m[8]) * 2;
        q = {(m[1] + m[3]) / s, 0.25f * s, (m[5] + m[7]) / s, (m[6] - m[2]) / s};
    } else {
        float s = std::sqrt(1 + m[8] - m[0] - m[4]) * 2;
        q = {(m[2] + m[6]) / s, (m[5] + m[7]) / s, 0.25f * s, (m[1] - m[3]) / s};
    }
    return q;
}

// The rotation by `angle` about Z (a .rotz channel), as quatToRot of
// (0, 0, sin(a/2), cos(a/2)).
inline void rotZToRot(float angle, std::array<float, 9>& m) {
    float c = std::cos(angle), s = std::sin(angle);
    m = {c, s, 0, -s, c, 0, 0, 0, 1};
}

// The inverse of a row-vector transform.
inline Xfm inverse(const Xfm& x) {
    const auto& m = x.rot;
    float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) +
                m[2] * (m[3] * m[7] - m[4] * m[6]);
    Xfm r;
    if (std::fabs(det) < 1e-12f) return r;
    float id = 1.0f / det;
    r.rot = {(m[4] * m[8] - m[5] * m[7]) * id, (m[2] * m[7] - m[1] * m[8]) * id, (m[1] * m[5] - m[2] * m[4]) * id,
             (m[5] * m[6] - m[3] * m[8]) * id, (m[0] * m[8] - m[2] * m[6]) * id, (m[2] * m[3] - m[0] * m[5]) * id,
             (m[3] * m[7] - m[4] * m[6]) * id, (m[1] * m[6] - m[0] * m[7]) * id, (m[0] * m[4] - m[1] * m[3]) * id};
    for (int j = 0; j < 3; ++j)
        r.pos[j] = -(x.pos[0] * r.rot[j] + x.pos[1] * r.rot[3 + j] + x.pos[2] * r.rot[6 + j]);
    return r;
}

inline void rotXToRot(float angle, std::array<float, 9>& m) {
    float c = std::cos(angle), s = std::sin(angle);
    m = {1, 0, 0, 0, c, s, 0, -s, c};
}

inline void rotYToRot(float angle, std::array<float, 9>& m) {
    float c = std::cos(angle), s = std::sin(angle);
    m = {c, 0, -s, 0, 1, 0, s, 0, c};
}

struct Transformable {
    Xfm local, world;
    std::vector<std::string> children;
    std::string parent;  // often the object itself; hierarchies use `children`
    // 0 None, 1 LocalRotate, 2 LookAtTarget, 3 ShadowTarget, 4 MirrorTarget,
    // 5 BillboardZ, 6 BillboardXZ, 7 BillboardXYZ, 8 FastBillboardXYZ (FUN_001da848).
    int constraint = 0;
    bool preserveScale = false;
};

struct Drawable {
    bool showing = true;
    std::vector<std::string> draws;  // for groups: the member objects
    std::array<float, 4> sphere{};   // bounding sphere center + radius
};

struct TexObj {
    std::string file;
    int type = 1;  // 1 Regular, 2 Rendered, 4 Movie, 8 BackBuffer, 0x18 FrontBuffer, 0x22 RenderedNoZ
    Image image;   // empty for render targets
};

struct MatStage {
    int blend = 2, texgen = 0, wrap = 0;
    std::array<float, 12> xfm{};
    std::string tex;
};

struct MatObj {
    std::vector<MatStage> stages;
    int blend = 1;   // 0 Dest, 1 Src, 2 Add, 3 SrcAlpha, 4 SrcAlphaAdd, 5 Subtract
    std::array<float, 4> color{1, 1, 1, 1};
    bool useEnviron = false, cull = true, alphaCutout = false, alphaWrite = false;
    bool vertAmb = false, vertDyn = false;  // ambient / lit terms use the vertex colour instead of the material's
    int zmode = 1;   // 0 Disable, 1 Normal, 2 Transparent, 3 Force, 4 Decal
    int multipass = 0;  // 0 Multitexture, 1 MultipassSrc (later passes DECAL), 2 MultipassLit (MODULATE, lit)
};

struct MeshVertex {
    float pos[3], normal[3], extra[4], uv[2];  // extra: bone weights, or RGBA color without bones
};

struct MeshObj {
    Transformable trans;
    Drawable draw;
    std::string mat, geomOwner;
    std::vector<MeshVertex> verts;
    std::vector<std::array<uint16_t, 3>> faces;
    struct Bone { std::string name; Xfm offset; };
    std::vector<Bone> bones;
    int revision = 0;  // bumped when a MeshAnim changes the vertices
};

// RndAnimatable: frame filters (applied in order) and child animations.
struct AnimFilter {
    int type = 0;  // 0 ScaleOffset, 1 MinMaxLoop, 2 ZeroOrder, 3 FirstOrder, 4 SecondOrder
    float a = 0, b = 0, c = 0;
    bool loop = false;
};

struct Animatable {
    std::vector<AnimFilter> filters;
    std::vector<std::string> anims;
};

template <size_t N> struct AnimKey {
    std::array<float, N> v;
    float frame;
};

// TransAnim v4, MatAnim v5, LightAnim v1, EnvAnim v3 and CamAnim v0 records
// (tools/rnd_classes/animation.py). Unused key lists stay empty.
struct AnimObj {
    enum class Kind { Trans, Mat, Light, Env, Cam, Mesh, Particle };
    Kind kind = Kind::Trans;
    Animatable anim;
    std::string target;  // the animated object
    std::string owner;   // the anim whose keys are used (empty or self = own keys)

    // TransAnim
    std::vector<AnimKey<4>> rotKeys;  // quaternions x, y, z, w
    std::vector<AnimKey<3>> transKeys, scaleKeys;
    bool transSpline = false, scaleSpline = false, repeatTrans = false, followPath = false, rotSlerp = false;

    // MatAnim
    struct Stage {
        std::vector<AnimKey<3>> trans, scale, rot;
        std::vector<std::pair<std::string, float>> tex;  // texture, frame
    };
    std::vector<Stage> stages;

    std::vector<AnimKey<4>> colorKeys;  // Mat base colour (RGB), Light colour, Environ ambient
    std::vector<AnimKey<1>> floatKeys;  // Mat alpha, Cam fov
    std::vector<AnimKey<4>> fogColorKeys;
    std::vector<AnimKey<2>> fogRangeKeys;

    // MeshAnim (RndMeshAnim_v8 @ 0x1f9940): per key, every vertex's position / UV / colour.
    struct VertKeys {
        std::vector<std::vector<std::array<float, 3>>> points;
        std::vector<std::vector<std::array<float, 2>>> uvs;
        std::vector<std::vector<std::array<float, 4>>> colors;
        std::vector<float> pointFrames, uvFrames, colorFrames;
    };
    VertKeys vert;

    // ParticleSysAnim (RndParticleSysAnim_v8 @ 0x1fe528, version 2): colour keys
    // keep the system's low-high spread; the others set (low, high).
    std::vector<AnimKey<4>> startColorKeys, endColorKeys;
    std::vector<AnimKey<2>> emitKeys, speedKeys, lifeKeys, startSizeKeys;
};

// RndParticleSys (version 22, docs/hit_fx.md §5): the emitter parameters
// (each a (low, high) range a particle draws from) and the live particles.
struct ParticleSysObj {
    Animatable anim;
    Transformable trans;
    Drawable draw;
    std::array<float, 2> life{}, speed{}, pitch{}, yaw{}, emitRate{}, startSize{}, endSize{};
    std::array<float, 3> posLow{}, posHigh{}, force{};
    std::array<float, 4> startLow{}, startHigh{}, endLow{}, endHigh{}, plane{}, midLow{}, midHigh{};
    bool collide = false, bubble = false;
    std::string mat, mesh;
    int type = 0, maxParticles = 0;
    float growRatio = 0, shrinkRatio = 0, midRatio = 0, relativeMotion = 0;
    std::array<float, 2> bubblePeriod{}, bubbleSize{};

    // Runtime (PsParticleSys): the particle pool, the last frame, the emit accumulator.
    struct Particle {
        std::array<float, 4> color, dColor, midSlope;
        std::array<float, 3> pos, vel, bubbleAxis;
        float bubbleFreq = 0, bubblePhase = 0, death = 0, birth = 0, size = 0, dSize = 0;
        float growEnd = 0, growRate = 0, shrinkStart = 0, shrinkRate = 0, midTime = 0;
    };
    std::vector<Particle> live;
    // preserve_particles: a static list baked at load (FUN_001a2a20), drawn, never simulated.
    struct Static {
        std::array<float, 3> pos;
        std::array<float, 4> color;
        float size;
    };
    std::vector<Static> preserved;
    float last = -9999999.0f, accum = 0;
    uint16_t lfsr = 0x1234;
};

// RndFlare (version 3, docs/venue_fx.md §3): a screen-space glow whose
// brightness steps up while its centre is visible (Z-buffer readback).
struct FlareObj {
    Transformable trans;
    Drawable draw;
    std::string mat;
    std::array<float, 2> sizes{}, range{};
    int steps = 1;
    // Runtime: last readback, brightness count, 60 Hz step clock.
    bool visible = false;
    int count = 0;
    float clock = -1;
};

// RndMorph (version 3, docs/faces_shadows.md A.1): pose meshes with weight keys; frame k = pose k.
struct MorphObj {
    struct Pose {
        std::string mesh;
        std::vector<AnimKey<2>> keys;  // (weight, frame) stored as v[0] = weight
    };
    std::vector<Pose> poses;
    std::string target;
    float intensity = 1;
};

struct ViewObj {
    Animatable anim;
    Transformable trans;
    Drawable draw;
    std::string env;
};

struct CamObj {
    Transformable trans;
    float nearPlane = 1, farPlane = 1000, fov = 0.8f;  // fov in radians
};

struct EnvironObj {
    std::vector<std::string> lights;
    std::array<float, 4> ambient{0, 0, 0, 1};
    float fogStart = 0, fogEnd = 0;
    std::array<float, 4> fogColor{};
    bool fogEnable = false;
};

struct LightObj {
    Transformable trans;
    std::array<float, 4> color{1, 1, 1, 1};
    float range = 0;
    int type = 0;  // 0 Point, 1 Directional
};

struct MultiMeshObj {
    Drawable draw;
    std::string mesh;
    std::vector<Xfm> instances;
};

// RndFont v7: a glyph sheet (the material's texture) of `chars` in cells.
struct FontObj {
    std::string mat;
    float cellWidth = 0, cellHeight = 0, deprecatedSize = 0, space = 0;
    std::string chars;
    std::map<std::pair<char, char>, float> kerning;
};

// RndText v15.
struct TextObj {
    Transformable trans;
    Drawable draw;
    std::string font, text;
    uint32_t align = 0x11;  // high nibble 1 top / 2 middle / 4 bottom; low 1 left / 2 centre / 4 right
    std::array<float, 4> color{1, 1, 1, 1};
    float wrapWidth = 0, leading = 1, italics = 0, size = 1;
    bool markup = false;
    int capsMode = 0;
};

// The ghui component classes (tools/ui_panels.py).
struct UiObj {
    enum class Kind { Label, Button, Picture, PictureToggle, List, Slider, TextEntry, CreditList, Placer };
    Kind kind = Kind::Label;
    Transformable trans;
    Drawable draw;
    std::string style;   // ghui/config.dta style symbol
    std::string text;    // localisation token
    std::string symbol, object;
    uint32_t align = 0x11;
    float leading = 1, width = 0, fitWidth = 0, fitHeight = 0, spacing = 0;
    bool fit = false, caps = false;
    int count = 0, mode = 0;
    uint32_t jitterSeed = 0;
    std::array<float, 3> jitter{};  // max x offset, z offset, rotation (degrees)
};

struct SceneObject {
    std::string type, name;
    std::unique_ptr<TexObj> tex;
    std::unique_ptr<MatObj> mat;
    std::unique_ptr<MeshObj> mesh;
    std::unique_ptr<ViewObj> view;
    std::unique_ptr<CamObj> cam;
    std::unique_ptr<EnvironObj> environment;
    std::unique_ptr<LightObj> light;
    std::unique_ptr<MultiMeshObj> multiMesh;
    std::unique_ptr<AnimObj> anim;
    std::unique_ptr<FontObj> font;
    std::unique_ptr<TextObj> text;
    std::unique_ptr<UiObj> ui;
    std::unique_ptr<ParticleSysObj> particles;
    std::unique_ptr<MorphObj> morph;
    std::unique_ptr<FlareObj> flare;

    // The object's transform / drawable parts, or nullptr.
    Transformable* transformable();
    const Transformable* transformable() const { return const_cast<SceneObject*>(this)->transformable(); }
    Drawable* drawable();
    const Drawable* drawable() const { return const_cast<SceneObject*>(this)->drawable(); }
};

class Scene {
public:
    // Parses a scene file (compressed or not). Objects of classes without a
    // decoder keep only their type and name. Throws std::runtime_error.
    explicit Scene(const Bytes& file);

    const SceneObject* find(const std::string& name) const;
    SceneObject* find(const std::string& name) { return const_cast<SceneObject*>(std::as_const(*this).find(name)); }
    const std::vector<SceneObject>& objects() const { return objects_; }
    // Loads the textures a panel references by file ("image/x.bmp" ->
    // <dir>/image/gen/x.bmp_ps2) into the Tex objects that have no image.
    void loadExternalTextures(const class GameData& data, const std::string& dir);
    std::vector<SceneObject>& objects() { return objects_; }
    // Adds an object (the port's own UI additions). Up to kSpareObjects fit
    // without moving the others, so pointers into the scene stay valid; null
    // past that or when the name is taken.
    static constexpr size_t kSpareObjects = 8;
    SceneObject* add(SceneObject&& o);
    int undecoded() const { return undecoded_; }

private:
    std::vector<SceneObject> objects_;
    std::map<std::string, size_t> byName_;
    int undecoded_ = 0;
};
