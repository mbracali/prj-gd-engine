// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <string>
#include <unordered_set>

#include "render/math.h"
#include "render/renderer.h"
#include "scene/scene.h"

// Uploads a Scene's textures and meshes and draws its objects.
class SceneRenderer {
public:
    SceneRenderer(Renderer& renderer, const Scene& scene);

    // Draws a View (recursively, in draw-list order) or a Mesh. `model` is
    // applied after each object's own world transform.
    // `env` is the environment selected before the object (as the venue's
    // set_singer_env does for characters).
    void draw(const std::string& name, const Mat4& viewProj, const Mat4& model = Mat4{},
              const EnvironObj* env = nullptr);

    struct MeshOptions {
        std::string material;  // replaces the mesh's material when set
        bool useWorld = true;  // apply the mesh's stored world transform
        float uvOffset[2] = {0, 0};
        bool repeatTexture = false;  // wrap the texture (for scrolling surfaces)
        float tint[4] = {1, 1, 1, 1};
        int texture = 0;  // replaces the stage-0 texture when set
        int geometry = 0;  // draws this renderer mesh (dynamicMesh) with the mesh's material
    };
    // Draws one mesh with options (used for gameplay elements).
    void drawMesh(const std::string& mesh, const Mat4& viewProj, const Mat4& model, const MeshOptions& options);

    // View-projection of a camera object; false if it does not exist.
    bool camera(const std::string& name, float aspect, Mat4& viewProj) const;
    // A camera framing every mesh, looking along +Y from in front and above.
    Mat4 overviewCamera(float aspect) const;
    // The first camera in the scene, or empty.
    std::string firstCamera() const;

    const Scene& scene() const { return scene_; }

    // Resolves names drawn lists and environments refer to (default: this
    // scene), so a venue's sections can use each other's lights.
    using Lookup = std::function<const SceneObject*(const std::string&)>;
    void setLookup(Lookup lookup) { lookup_ = std::move(lookup); }
    // Materials resolve through the lookup first (a template mesh drawn with another scene's material).
    void preferLookupMaterials(bool on) { preferLookup_ = on; }

    // Draws objects this renderer has no drawing for (UI components, Text)
    // when they come up in a draw list.
    using CustomDraw = std::function<void(const SceneObject&, const Mat4& viewProj, const Mat4& model)>;
    void setCustomDraw(CustomDraw fn) { custom_ = std::move(fn); }
    // RndLine: a flat strip of `width` from p0 to p1 (in the XY plane) with a material
    // of this scene (u across the strip, v along it).
    // `color` (RGBA) replaces the material colour when given.
    void drawLine(const std::string& mat, const Mat4& viewProj, const std::array<float, 3>& p0,
                  const std::array<float, 3>& p1, float width, const float* color = nullptr);
    // Per-frame geometry (tails, lines): each call fills the next buffer of a
    // pool that resetDynamic() rewinds once a frame, so no buffer is rewritten
    // while the GPU may still read it in the same frame.
    int dynamicMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void resetDynamic() { dynamicUsed_ = 0; }
    // Menu and font scenes: their textures follow Renderer::uiEnhance.
    void setUiTextures(bool on) { uiTextures_ = on; }
    // Replaces a Tex object's picture (e.g. the endgame winner photo) for every mesh using it.
    void replaceTexture(const std::string& tex, const Image& image);
    // Texture handle for a Tex object of this scene (0 when missing).
    int textureHandle(const std::string& tex) { return texture(tex, false); }

private:
    struct DrawMesh {
        int mesh = 0;
        Renderer::RenderState state;
        const MeshObj* obj = nullptr;  // for the current world transform
        const MatObj* mat = nullptr;   // for the current colour, texture and UV offset
        bool repeat = false;
        float bakedUv[2] = {0, 0};     // stage-0 UV translation baked into the vertices
        std::array<float, 9> bakedRows{};  // stage-0 UV rows baked into the vertices (texgen 1 / 4)
        const MeshObj* skin = nullptr;  // the mesh whose bones skin this geometry
        mutable std::string texName;   // current stage-0 texture (MatAnims switch it)
        mutable int texture = 0;
        const MeshObj* geom = nullptr;  // the vertex owner
        int revision = 0;               // geom->revision when uploaded
        const MatStage* stage = nullptr;
        bool vertexColor = true;
        std::vector<uint32_t> indices;
        // Stages 1+ with a blend (FUN_0019d3b8): each a pass over the mesh;
        // texgen 1/4 stages get their own UV bake.
        struct Pass {
            size_t stage = 1;
            int mesh = 0;
            float bakedUv[2] = {0, 0};
        };
        std::vector<Pass> passes;
    };
    // Per-vertex texgen state (Environ / Sphere) for a stage drawn at `world`.
    void texgen(Renderer::RenderState& st, const MatStage& s) const;
    // The later passes of a material after pass 0 (`base` = pass 0's state).
    void drawPasses(const DrawMesh& dm, const float* mvp, const Renderer::RenderState& base);
    std::vector<Vertex> vertices(const DrawMesh& dm) const;
    // The draw state with the material's current (animated) values.
    Renderer::RenderState liveState(const DrawMesh& dm);
    Mat4 world(const DrawMesh& dm) const { return Mat4::fromRowXfm(dm.obj->trans.world.rot, dm.obj->trans.world.pos); }
    // The GPU mesh for `mesh` drawn with material `mat` (cached), or nullptr.
    const DrawMesh* build(const std::string& mesh, const std::string& mat, bool forceRepeat = false);
    int texture(const std::string& name, bool repeat);
    void drawObject(const SceneObject& o, const Mat4& viewProj, const Mat4& model, int depth);
    void drawChildren(const Drawable& d, const Mat4& viewProj, const Mat4& model, int depth);
    // The 4 bone matrices (offset x bone world) of a skinned mesh, column-major.
    const float* boneMatrices(const MeshObj& skin);
    const SceneObject* resolve(const std::string& name) const { return lookup_ ? lookup_(name) : scene_.find(name); }
    // Lighting from the selected environment for materials that use it, or nullptr.
    const Lighting* lightingFor(const MatObj* mat);
public:
    // A private copy of a mesh's material that only this mesh draws with
    // (created on first use from its current material).
    MatObj* ownMaterial(const std::string& mesh);
    // The camera's world transform, for billboard constraints (rows right, forward, up).
    void setCamera(const Xfm& camera) { camera_ = camera, hasCamera_ = true; }
    // Views not drawn when reached through another view (drawn separately later).
    void skipView(const std::string& view) { skipped_.push_back(view); }
    // Every mesh draws with this material instead of its own while set (a
    // character's flattened shadow with charsys_shadow.mat); null restores.
    void setMaterialOverride(const MatObj* mat) { overrideMat_ = mat; }
    // [I] Enhanced Settings > More Lights on Venues: additive materials (light
    // cones, glows, flares) are drawn this much brighter.
    void setAdditiveGain(float gain) { additiveGain_ = gain; }
private:
    const MatObj* overrideMat_ = nullptr;
    float additiveGain_ = 1;
    std::map<std::string, std::unique_ptr<MatObj>> ownMats_;
    Xfm camera_;
    bool hasCamera_ = false;
    bool preferLookup_ = false;
    std::vector<std::string> skipped_;
    std::unordered_set<std::string> viewMembers_;  // objects some view of this scene draws
    Xfm billboard(const Transformable& t) const;
    // PsParticleSys draw (PsParticleSys_t1_v5 @ 0x1a2fd8): each live particle a
    // camera-facing square of side `size` in its colour, with the system's mat.
    void drawParticles(const SceneObject& o, const Mat4& viewProj, const Mat4& model);
    // RndFlare_v5: queue the centre's visibility test, step the brightness, draw the square.
    void drawFlare(const SceneObject& o, const Mat4& viewProj, const Mat4& model);
    struct FlareTest {
        FlareObj* flare;
        int x, y;
        float depth;
    };
    std::vector<FlareTest> flareTests_;
    static constexpr int kFlareLag = 3;
    struct FlareBatch {
        std::vector<FlareTest> tests;
        unsigned buffer = 0;
        size_t capacity = 0;
    };
    FlareBatch flareRing_[kFlareLag];
    int flareSlot_ = 0;
public:
    // FUN_0019ae48 (end of the frame's scene pass): read back the queued flare tests.
    void resolveFlares();
private:
    std::map<std::string, int> particleMeshes_;
    std::vector<int> dynamic_;
    bool uiTextures_ = false;
    size_t dynamicUsed_ = 0;
    void setMatFlags(Renderer::RenderState& st, const MatObj* mat) const;

    Renderer& renderer_;
    const Scene& scene_;
    std::map<std::string, int> textures_;
    std::map<std::string, DrawMesh> meshes_;  // key: mesh name + '|' + material
    Lookup lookup_;
    CustomDraw custom_;
    const EnvironObj* env_ = nullptr;  // drawing an Environ selects it for what follows
    Lighting lighting_;
    float bones_[64];
};
