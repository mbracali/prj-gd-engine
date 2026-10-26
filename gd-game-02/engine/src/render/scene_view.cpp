// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/scene_view.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

#include "app_ids.h"
#include "data/game_data.h"
#include "devaids/screenshot.h"
#include "render/canvas.h"
#include "scene/milo.h"

namespace {

using gfx::Mat4;
using gfx::Vec3;

Mat4 toMat4(const scene::Xfm& x) { return Mat4::fromRnd(x.rot.data(), x.pos.data()); }

const scene::Trans* transOf(const scene::Object* o) {
    if (!o) return nullptr;
    if (auto* m = o->as<scene::Mesh>()) return &m->trans;
    if (auto* t = o->as<scene::TransObj>()) return &t->trans;
    if (auto* g = o->as<scene::Group>()) return &g->trans;
    if (auto* c = o->as<scene::Cam>()) return &c->trans;
    if (auto* l = o->as<scene::Light>()) return &l->trans;
    return nullptr;
}

// An object's world transform: its local one under its parent's world when
// the parent is in this scene, else the world transform stored with it.
scene::Xfm worldXfm(const scene::Object* o, int depth = 0) {
    const scene::Trans* t = transOf(o);
    if (!t) return {};
    if (!t->parent.empty() && depth < 32 && o->dir)
        if (const scene::Object* p = o->dir->resolve(t->parent); p && p != o && transOf(p))
            return scene::compose(t->local, worldXfm(p, depth + 1));
    return t->world;
}

struct Drawn {
    gfx::GpuMesh gpu;
    gfx::Material mat;
    Mat4 bones[4];
    int boneCount = 1;
    Vec3 center;
    bool blended = false;
};

class SceneView {
public:
    int run(const std::string& dataPath, const std::string& miloPath);

private:
    gfx::TextureId texture(const scene::Object* owner, const std::string& name);
    void build();
    void environment();
    void frame();

    Canvas canvas_;
    std::unique_ptr<GameData> data_;
    std::unique_ptr<scene::Milo> milo_;
    std::string miloPath_;
    std::map<const scene::Tex*, gfx::TextureId> textures_;
    std::vector<Drawn> drawn_;
    std::vector<gfx::BufferId> buffers_;
    std::vector<scene::Cam*> cams_;
    size_t cam_ = 0;
    gfx::Camera autoCam_;
    gfx::LightEnv env_;
    std::string envName_;
    int skippedTextures_ = 0;
};

gfx::TextureId SceneView::texture(const scene::Object* owner, const std::string& name) {
    if (name.empty() || !owner->dir) return {};
    auto* tex = owner->dir->resolve<scene::Tex>(name);
    if (!tex) return {};
    if (auto it = textures_.find(tex); it != textures_.end()) return it->second;
    gfx::TextureId id;
    const scene::Bitmap& b = tex->bitmap;
    gfx::TexFormat format = gfx::TexFormat::RGBA8;
    bool known = true;
    switch (b.format) {
    case scene::Bitmap::Format::DXT1: format = gfx::TexFormat::BC1; break;
    case scene::Bitmap::Format::DXT5: format = gfx::TexFormat::BC3; break;
    case scene::Bitmap::Format::ATI2: format = gfx::TexFormat::BC5; break;
    default: known = false; break;
    }
    if (known && !b.empty()) {
        std::vector<gfx::TexLevel> levels;
        for (size_t i = 0; i < b.levels.size(); ++i) levels.push_back({b.level(i), b.levels[i].size});
        std::string error;
        id = canvas_.renderer().device().createTexture({b.width, b.height, int(levels.size()), format, gfx::TexKind::Tex2D},
                                                       levels.data(), error);
        if (!id) SDL_Log("view: texture %s: %s", tex->name.c_str(), error.c_str());
    } else {
        ++skippedTextures_;
    }
    textures_[tex] = id;
    return id;
}

// Per-vertex tangents (xyz, handedness) from the uv layout, for normal maps.
std::vector<float> packVertices(const scene::Mesh& m) {
    const size_t n = m.verts.size();
    std::vector<Vec3> tan(n), bit(n);
    for (const auto& f : m.faces) {
        if (f[0] >= n || f[1] >= n || f[2] >= n) continue;
        const scene::MeshVertex &a = m.verts[f[0]], &b = m.verts[f[1]], &c = m.verts[f[2]];
        const Vec3 e1{b.pos[0] - a.pos[0], b.pos[1] - a.pos[1], b.pos[2] - a.pos[2]};
        const Vec3 e2{c.pos[0] - a.pos[0], c.pos[1] - a.pos[1], c.pos[2] - a.pos[2]};
        const float du1 = b.uv[0] - a.uv[0], dv1 = b.uv[1] - a.uv[1], du2 = c.uv[0] - a.uv[0], dv2 = c.uv[1] - a.uv[1];
        const float det = du1 * dv2 - du2 * dv1;
        if (std::fabs(det) < 1e-12f) continue;
        const float r = 1.0f / det;
        const Vec3 t = (e1 * dv2 - e2 * dv1) * r, s = (e2 * du1 - e1 * du2) * r;
        for (int k = 0; k < 3; ++k) tan[f[k]] = tan[f[k]] + t, bit[f[k]] = bit[f[k]] + s;
    }
    std::vector<float> out;
    out.reserve(n * 16);
    for (size_t i = 0; i < n; ++i) {
        const scene::MeshVertex& v = m.verts[i];
        const Vec3 nrm{v.normal[0], v.normal[1], v.normal[2]};
        Vec3 t = gfx::normalize(tan[i] - nrm * gfx::dot(nrm, tan[i]));
        if (gfx::dot(t, t) < 0.5f) t = gfx::normalize(gfx::cross(nrm, std::fabs(nrm.z) < 0.9f ? Vec3{0, 0, 1} : Vec3{1, 0, 0}));
        const float w = gfx::dot(gfx::cross(nrm, t), bit[i]) < 0 ? -1.0f : 1.0f;
        out.insert(out.end(), {v.pos[0], v.pos[1], v.pos[2], v.normal[0], v.normal[1], v.normal[2], v.color[0], v.color[1],
                               v.color[2], v.color[3], v.uv[0], v.uv[1], t.x, t.y, t.z, w});
    }
    return out;
}

void SceneView::build() {
    gfx::RenderDevice& dev = canvas_.renderer().device();
    Vec3 lo, hi;
    for (scene::Mesh* m : milo_->root().all<scene::Mesh>()) {
        if (!m->decoded || !m->draw.showing) continue;
        const scene::Mesh* geom = m;
        if (m->verts.empty() && !m->geomOwner.empty() && m->dir)
            if (auto* owner = m->dir->resolve<scene::Mesh>(m->geomOwner)) geom = owner;
        if (geom->verts.empty() || geom->faces.empty()) continue;

        Drawn d;
        const std::vector<float> verts = packVertices(*geom);
        d.gpu.vertices = dev.createBuffer(gfx::BufferKind::Vertex, verts.size() * sizeof(float), verts.data(), false);
        d.gpu.indices = dev.createBuffer(gfx::BufferKind::Index, geom->faces.size() * 6, geom->faces.data(), false);
        buffers_.push_back(d.gpu.vertices), buffers_.push_back(d.gpu.indices);
        d.gpu.indexCount = int(geom->faces.size() * 3);
        d.gpu.tangents = true;

        const Mat4 world = toMat4(worldXfm(m));
        d.bones[0] = world;
        if (!geom->bones.empty()) {
            d.gpu.skinned = true;
            d.boneCount = int(std::min<size_t>(geom->bones.size(), 4));
            for (int i = 0; i < d.boneCount; ++i) {
                const auto& bone = geom->bones[size_t(i)];
                const scene::Object* bo = m->dir ? m->dir->resolve(bone.name) : nullptr;
                d.bones[i] = bo && transOf(bo) ? toMat4(scene::compose(bone.offset, worldXfm(bo))) : world;
            }
        }

        if (auto* mat = m->dir ? m->dir->resolve<scene::Mat>(m->mat) : nullptr) {
            gfx::Material& g = d.mat;
            g.blend = gfx::MatBlend(std::clamp(mat->blend, 0, 7));
            g.color[0] = mat->color[0], g.color[1] = mat->color[1], g.color[2] = mat->color[2], g.alpha = mat->color[3];
            g.useEnviron = mat->useEnviron, g.prelit = mat->prelit;
            g.alphaCut = mat->alphaCut, g.alphaWrite = mat->alphaWrite;
            g.intensify = mat->intensify, g.cull = mat->cull;
            g.perPixelLit = mat->perPixelLit, g.normalize = mat->normalize;
            g.zMode = gfx::ZMode(std::clamp(mat->zMode, 0, 4));
            g.texGen = gfx::TexGen(std::clamp(mat->texGen, 0, 5));
            g.texWrap = gfx::TexWrap(std::clamp(mat->texWrap, 0, 4));
            g.texXfm = toMat4(mat->texXfm);
            g.emissiveMultiplier = mat->emissiveMultiplier;
            for (int k = 0; k < 3; ++k) g.specularRgb[k] = mat->specularRgb[size_t(k)];
            g.specularPower = mat->specularPower;
            g.diffuseTex = texture(mat, mat->diffuseTex);
            g.normalMap = texture(mat, mat->normalMap);
            g.emissiveMap = texture(mat, mat->emissiveMap);
            g.specularMap = texture(mat, mat->specularMap);
            g.customDiffuseMap = texture(mat, mat->customDiffuseMap);
            // Environment maps are CubeTex objects: not decoded yet.
        }
        d.blended = d.mat.blend != gfx::MatBlend::Src;

        Vec3 sum;
        for (const auto& v : geom->verts) sum = sum + Vec3{v.pos[0], v.pos[1], v.pos[2]};
        d.center = world.transformPoint(sum * (1.0f / float(geom->verts.size())));
        drawn_.push_back(d);
    }
    // A camera that frames most meshes (their centres between the 10th and
    // 90th percentile on each axis, so a sky dome does not set the size),
    // from the front (-y) and a little above.
    if (!drawn_.empty()) {
        auto pct = [&](float Vec3::*axis, float q) {
            std::vector<float> v;
            for (const Drawn& d : drawn_) v.push_back(d.center.*axis);
            std::sort(v.begin(), v.end());
            return v[size_t(q * float(v.size() - 1))];
        };
        lo = {pct(&Vec3::x, 0.1f), pct(&Vec3::y, 0.1f), pct(&Vec3::z, 0.1f)};
        hi = {pct(&Vec3::x, 0.9f), pct(&Vec3::y, 0.9f), pct(&Vec3::z, 0.9f)};
    }
    const Vec3 center = (lo + hi) * 0.5f;
    const float radius = std::max(1.0f, std::sqrt(gfx::dot(hi - lo, hi - lo)) * 0.5f);
    autoCam_ = gfx::Camera::lookAt(center + Vec3{0, -1.8f * radius, 0.5f * radius}, center);
    autoCam_.nearPlane = radius * 0.01f;
    autoCam_.farPlane = radius * 50;
    for (scene::Cam* c : milo_->root().all<scene::Cam>())
        if (c->decoded) cams_.push_back(c);
}

void SceneView::environment() {
    scene::Environ* e = nullptr;
    for (scene::Environ* x : milo_->root().all<scene::Environ>())
        if (x->decoded) {
            e = x;
            break;
        }
    if (!e) return;
    envName_ = e->name;
    env_.envAmbient = {e->ambientColor[0], e->ambientColor[1], e->ambientColor[2]};
    env_.fog = e->fogEnable;
    for (int k = 0; k < 4; ++k) env_.fogColor[k] = e->fogColor[size_t(k)];
    env_.fogStart = e->fogStart, env_.fogEnd = e->fogEnd;
    env_.fade = e->fadeOut, env_.fadeStart = e->fadeStart, env_.fadeEnd = e->fadeEnd;
    int point = 0, dir = 0;
    for (const std::string& name : e->lights) {
        auto* l = e->dir ? e->dir->resolve<scene::Light>(name) : nullptr;
        if (!l || env_.lightCount >= 7) continue;
        const scene::Xfm w = worldXfm(l);
        gfx::Light& g = env_.lights[env_.lightCount++];
        g.color = {l->color[0], l->color[1], l->color[2]};
        g.range = l->range;
        // [I] GH1's light types: 0 point, 1 directional. A directional light
        // shines along its +y axis, so the vector towards it is -y.
        if (l->type == 1) {
            g.vec = gfx::normalize(Vec3{-w.rot[3], -w.rot[4], -w.rot[5]});
            ++dir;
        } else {
            g.vec = {w.pos[0], w.pos[1], w.pos[2]};
            ++point;
        }
    }
    // GH2 picks one light kind per environment.
    env_.directional = dir >= point;
}

void SceneView::frame() {
    gfx::Camera cam = autoCam_;
    std::string camName = "auto";
    if (cam_ < cams_.size()) {
        const scene::Cam* c = cams_[cam_];
        cam.world = toMat4(worldXfm(c));
        cam.yFov = c->yFov, cam.nearPlane = c->nearPlane, cam.farPlane = c->farPlane;
        camName = c->name;
    }
    const SDL_Color clear = env_.fog ? SDL_Color{Uint8(env_.fogColor[0] * 255), Uint8(env_.fogColor[1] * 255), Uint8(env_.fogColor[2] * 255), 255}
                                     : SDL_Color{24, 24, 30, 255};
    canvas_.begin(clear);
    gfx::Renderer& r = canvas_.renderer();
    int vx, vy, vw, vh;
    r.pictureRect(vx, vy, vw, vh);
    r.materials().setFrame(cam, env_, vw, vh);
    const Vec3 eye = cam.position();
    std::vector<const Drawn*> blended;
    for (const Drawn& d : drawn_) {
        if (d.blended) blended.push_back(&d);
        else r.materials().draw(d.gpu, d.bones, d.mat, {d.boneCount});
    }
    // Blended meshes far to near.
    std::sort(blended.begin(), blended.end(), [&](const Drawn* a, const Drawn* b) {
        const Vec3 da = a->center - eye, db = b->center - eye;
        return gfx::dot(da, da) > gfx::dot(db, db);
    });
    for (const Drawn* d : blended) r.materials().draw(d->gpu, d->bones, d->mat, {d->boneCount});

    char line[256];
    std::snprintf(line, sizeof line, "%s  camera %s (%zu/%zu, Left/Right)  %zu meshes  environ %s, %d lights", miloPath_.c_str(),
                  camName.c_str(), cams_.empty() ? 0 : cam_ + 1, cams_.size(), drawn_.size(), envName_.empty() ? "none" : envName_.c_str(),
                  env_.lightCount);
    canvas_.text(12, 8, 1.5f, {235, 235, 240, 255}, line);
}

int SceneView::run(const std::string& dataPath, const std::string& miloPath) {
    miloPath_ = miloPath;
    try {
        data_ = std::make_unique<GameData>(dataPath);
        milo_ = scene::loadMilo(*data_, miloPath);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "%s --view: %s\n", app::kProgram, ex.what());
        return 2;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "%s --view: cannot start video: %s\n", app::kProgram, SDL_GetError());
        return 2;
    }
    SDL_Window* window = SDL_CreateWindow(app::kWindowTitle, int(kScreenW), int(kScreenH), SDL_WINDOW_RESIZABLE | gfx::prepareWindow());
    std::string error;
    if (!window || !canvas_.init(window, error)) {
        std::fprintf(stderr, "%s --view: cannot open the window: %s\n", app::kProgram, window ? error.c_str() : SDL_GetError());
        if (window) SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    build();
    environment();
    SDL_Log("view: %s: %d meshes, %d textures (%d not decodable), %d cameras, environ '%s' with %d %s lights",
            miloPath.c_str(), int(drawn_.size()), int(textures_.size()), skippedTextures_, int(cams_.size()), envName_.c_str(), env_.lightCount,
            env_.directional ? "directional" : "point");
    if (const char* c = SDL_getenv("GD02_VIEW_CAM")) cam_ = size_t(std::atoi(c));  // development aid: start camera
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type != SDL_EVENT_KEY_DOWN) continue;
            const size_t n = cams_.size() + 1;  // the cameras, then the framing one
            if (e.key.key == SDLK_ESCAPE) running = false;
            if (e.key.key == SDLK_RIGHT) cam_ = (cam_ + 1) % n;
            if (e.key.key == SDLK_LEFT) cam_ = (cam_ + n - 1) % n;
        }
        frame();
        if (captureDebugScreenshot(canvas_, false, 0)) running = false;
        canvas_.end();
    }
    for (auto& [tex, id] : textures_) canvas_.renderer().device().destroyTexture(id);
    for (gfx::BufferId b : buffers_) canvas_.renderer().device().destroyBuffer(b);
    canvas_.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace

int runSceneView(const std::string& dataPath, const std::string& miloPath) { return SceneView().run(dataPath, miloPath); }
