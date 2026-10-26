// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "render/material.h"
#include "scene/objects.h"

// Milo scenes on the GPU, shared by the scene viewer, the track and the HUD:
// - SceneCache uploads textures and meshes once and maps Mat objects to the
//   renderer's materials;
// - ScenePose holds what GH2's animation objects change (local transforms,
//   material colour / alpha / texture transform / texture, showing), set by
//   playing TransAnim / MatAnim / AnimFilter / Group objects at a frame, the
//   way the game's SetFrame calls do;
// - drawMesh draws a mesh with its material and the material's next passes.
namespace gfx {

Mat4 toMat4(const scene::Xfm& x);
const scene::Trans* transOf(const scene::Object* o);
const scene::Draw* drawOf(const scene::Object* o);

class ScenePose {
public:
    struct MatState {
        bool hasColor = false, hasAlpha = false, hasXfm = false;
        float rgb[3] = {1, 1, 1};
        float alpha = 1;
        scene::Xfm texXfm;
        const scene::Tex* tex = nullptr;
    };

    void clear();
    // Plays `anim` at `frame`: TransAnim (its Trans), MatAnim (its Mat),
    // AnimFilter (the filtered anim at the filtered frame), Group (every
    // member, as RndGroup::SetFrame does). Other classes are ignored.
    void setFrame(const scene::Object* anim, float frame);
    // The last frame of an anim object's keys (0 when it has none).
    static float endFrame(const scene::Object* anim);

    void setLocal(const scene::Object* o, const scene::Xfm& x) { local_[o] = x; }
    void resetLocal(const scene::Object* o) { local_.erase(o); }
    scene::Xfm local(const scene::Object* o) const;
    // Local transforms composed up the parent chain (parents resolved in the
    // object's directory; a directory itself counts as the identity).
    scene::Xfm world(const scene::Object* o) const;

    const MatState* mat(const scene::Mat* m) const;
    MatState& matState(const scene::Mat* m) { return mats_[m]; }

    void setShowing(const scene::Object* o, bool showing) { showing_[o] = showing; }
    bool showing(const scene::Object* o) const;

    // MeshAnim objects played on a mesh: the anim and its frame (the vertex
    // positions are built by the caller, meshAnimVertices).
    struct MeshAnimState {
        const scene::MeshAnim* anim = nullptr;
        float frame = 0;
    };
    const MeshAnimState* meshAnim(const scene::Mesh* m) const;

private:
    void transAnim(const scene::TransAnim& a, float frame);
    void matAnim(const scene::MatAnim& a, float frame);

    std::unordered_map<const scene::Object*, scene::Xfm> local_;
    std::unordered_map<const scene::Mat*, MatState> mats_;
    std::unordered_map<const scene::Object*, bool> showing_;
    std::unordered_map<const scene::Mesh*, MeshAnimState> meshAnims_;
};

// A mesh's vertices (packVertices' layout) with the positions of a MeshAnim
// at `frame` (linear between its point keys).
std::vector<float> meshAnimVertices(const scene::Mesh& geometry, const scene::MeshAnim& anim, float frame);

class SceneCache {
public:
    explicit SceneCache(RenderDevice& device) : device_(device) {}
    ~SceneCache();
    SceneCache(const SceneCache&) = delete;
    SceneCache& operator=(const SceneCache&) = delete;

    TextureId texture(const scene::Tex* tex);
    TextureId texture(const scene::Object* owner, const std::string& name);
    // The mesh's geometry (its geometry owner's when it has none), or
    // nullptr when there is nothing to draw.
    const GpuMesh* mesh(const scene::Mesh* m);
    // A Mat as the renderer's material, with the pose's overrides.
    Material material(const scene::Mat& mat, const ScenePose* pose = nullptr);
    // An Environ with its lights (lights placed by the pose).
    LightEnv lightEnv(const scene::Environ& e, const ScenePose* pose = nullptr);

    RenderDevice& device() { return device_; }
    int skippedTextures() const { return skippedTextures_; }
    size_t textureCount() const { return textures_.size(); }

private:
    RenderDevice& device_;
    std::map<const scene::Tex*, TextureId> textures_;
    std::map<const scene::Mesh*, std::unique_ptr<GpuMesh>> meshes_;
    std::vector<BufferId> buffers_;
    int skippedTextures_ = 0;
};

// The texture-coordinate transform GH2 derives from a Mat's tex_xfm for its
// tex_gen (the shader's gTexCoord; scene_gpu.cpp).
scene::Xfm texCoordXfm(const scene::Xfm& texXfm, int texGen);

// GH2's vertex layout plus per-vertex tangents (xyz, handedness) from the
// uv layout, for normal maps: 16 floats per vertex.
std::vector<float> packVertices(const scene::Mesh& m);

// Draws `mesh` (rigid) at `world` with `mat` (default: the mesh's own) and
// every material its next_pass chain names. `tint` multiplies the colour
// and alpha (1 1 1 1: none). `geometry` replaces the mesh's cached
// geometry (meshes built or animated per frame).
void drawMesh(MaterialRenderer& r, SceneCache& cache, const scene::Mesh& mesh, const Mat4& world, const ScenePose* pose,
              const scene::Mat* mat = nullptr, const float* tint = nullptr, const GpuMesh* geometry = nullptr);

// A vertex buffer refilled every frame (built tails, animated vertices):
// GH2's 64-byte vertex layout with tangents, 16-bit indices.
class DynamicMesh {
public:
    explicit DynamicMesh(RenderDevice& device) : device_(device) {}
    ~DynamicMesh();
    DynamicMesh(const DynamicMesh&) = delete;
    DynamicMesh& operator=(const DynamicMesh&) = delete;
    // Uploads `verts` (16 floats each) and `indices`; returns the mesh
    // covering all of them (valid until the next upload).
    GpuMesh upload(const std::vector<float>& verts, const std::vector<uint16_t>& indices);

private:
    RenderDevice& device_;
    BufferId vb_, ib_;
    size_t vbSize_ = 0, ibSize_ = 0;
};

}  // namespace gfx
