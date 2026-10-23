// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/scene_renderer.h"
#include "scene/particles.h"
#include "render/image_enhance.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

namespace {

uint8_t unit(float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); }

// RndMat blend (0 Dest, 1 Src, 2 Add, 3 SrcAlpha, 4 SrcAlphaAdd, 5 Subtract).
Blend blendFor(int b) {
    switch (b) {
    case 0: return Blend::None;
    case 1: return Blend::Opaque;
    case 2: return Blend::Add;
    case 3: return Blend::Alpha;
    case 4: return Blend::AddAlpha;
    case 5: return Blend::Subtract;
    default: return Blend::Alpha;
    }
}

}  // namespace

SceneRenderer::SceneRenderer(Renderer& renderer, const Scene& scene) : renderer_(renderer), scene_(scene) {
    for (const SceneObject& o : scene.objects()) {
        if (o.mesh) build(o.name, o.mesh->mat);
        if (o.view) viewMembers_.insert(o.view->draw.draws.begin(), o.view->draw.draws.end());
    }
}

std::vector<Vertex> SceneRenderer::vertices(const DrawMesh& dm) const {
    std::vector<Vertex> verts;
    verts.reserve(dm.geom->verts.size());
    for (const MeshVertex& v : dm.geom->verts) {
        float u = v.uv[0], w = v.uv[1];
        if (dm.stage && (dm.stage->texgen == 1 || dm.stage->texgen == 4)) {
            // VU1 texgen (docs/render_gems_dof.md §1): 1 = Xfm, rotate/scale about
            // the texture centre after the translation (u - tx, v + ty); 4 =
            // XfmOrigin, the same matrix without centring. texgen 0 uses raw UVs.
            const auto& x = dm.stage->xfm;
            float c = dm.stage->texgen == 1 ? 0.5f : 0.0f;
            float a = u - dm.bakedUv[0] - c, b = w + dm.bakedUv[1] - c;
            if (dm.stage->texgen == 4) a = u, b = w;
            float nu = x[0] * a - x[3] * b + c;
            float nw = -x[1] * a + x[4] * b + c;
            if (dm.stage->texgen == 4) nu += dm.bakedUv[0], nw += dm.bakedUv[1];
            u = nu, w = nw;
        }
        // The material colour is applied as the tint at draw time (it animates).
        // Vertex colours (meshes without bones) carry alpha too, e.g. the
        // menus' vignette overlays and the highway rails fading out.
        float r = 1, g = 1, b = 1, a = 1;
        if (dm.vertexColor) r = v.extra[0], g = v.extra[1], b = v.extra[2], a = v.extra[3];
        Vertex out{v.pos[0], v.pos[1], v.pos[2], u, w, unit(r), unit(g), unit(b), unit(a), v.normal[0],
                   v.normal[1], v.normal[2]};
        if (dm.skin) std::copy_n(v.extra, 4, out.w);
        verts.push_back(out);
    }
    return verts;
}

const SceneRenderer::DrawMesh* SceneRenderer::build(const std::string& name, const std::string& matName,
                                                     bool forceRepeat) {
    std::string key = name + '|' + matName + (forceRepeat ? "|r" : "");
    if (auto it = meshes_.find(key); it != meshes_.end()) {
        DrawMesh& c = it->second;
        bool rows = c.stage && (c.stage->texgen == 1 || c.stage->texgen == 4) &&
                    !std::equal(c.bakedRows.begin(), c.bakedRows.end(), c.stage->xfm.begin());
        if (c.mesh && c.geom && (c.geom->revision != c.revision || rows)) {  // a MeshAnim moved the vertices, a MatAnim the UV rows
            c.revision = c.geom->revision;
            if (rows) std::copy_n(c.stage->xfm.begin(), 9, c.bakedRows.begin());
            renderer_.updateMesh(c.mesh, vertices(c), c.indices);
        }
        return c.mesh ? &c : nullptr;
    }
    DrawMesh& dm = meshes_[key];
    const SceneObject* o = scene_.find(name);
    if (!o || !o->mesh) return nullptr;
    const MeshObj& m = *o->mesh;
    const MeshObj* geom = &m;
    if (!m.geomOwner.empty() && m.geomOwner != name) {
        const SceneObject* owner = scene_.find(m.geomOwner);
        if (owner && owner->mesh) geom = owner->mesh.get();
    }
    if (geom->faces.empty()) return nullptr;

    dm.obj = &m;
    dm.repeat = forceRepeat;
    const MatStage* stage = nullptr;
    const SceneObject* matObj = preferLookup_ && lookup_ && !matName.empty() ? lookup_(matName) : nullptr;
    if (!matObj) matObj = scene_.find(matName);
    if (!matObj && lookup_ && !matName.empty()) matObj = lookup_(matName);
    const MatObj* own = matName == "#own" && ownMats_.count(name) ? ownMats_[name].get() : matName == "#override" ? overrideMat_ : nullptr;
    if (own || (matObj && matObj->mat)) {
        const MatObj& mat = own ? *own : *matObj->mat;
        dm.mat = &mat;
        dm.state.blend = blendFor(mat.blend);
        switch (mat.zmode) {
        case 0: dm.state.depthTest = false, dm.state.depthWrite = false; break;
        case 2: dm.state.depthWrite = false; break;
        case 3: dm.state.depthTest = false; break;
        case 4: dm.state.depthWrite = false, dm.state.decal = true; break;
        default: break;
        }
        dm.state.alphaCutout = mat.alphaCutout;
        if (!mat.stages.empty()) {
            stage = &mat.stages[0];
            dm.state.texture = texture(stage->tex, forceRepeat || stage->wrap == 1);
            dm.texName = stage->tex;
            dm.texture = dm.state.texture;
            if (stage->texgen == 1 || stage->texgen == 4) dm.bakedUv[0] = stage->xfm[9], dm.bakedUv[1] = stage->xfm[10];
        }
    }

    dm.vertexColor = geom->bones.empty() && m.bones.empty();
    if (!m.bones.empty()) dm.skin = &m;
    else if (!geom->bones.empty()) dm.skin = geom;
    dm.geom = geom;
    dm.revision = geom->revision;
    dm.stage = stage;
    if (stage) std::copy_n(stage->xfm.begin(), 9, dm.bakedRows.begin());
    std::vector<Vertex> verts = vertices(dm);
    std::vector<uint32_t> idx;
    idx.reserve(geom->faces.size() * 3);
    for (const auto& f : geom->faces)
        if (f[0] < verts.size() && f[1] < verts.size() && f[2] < verts.size()) idx.insert(idx.end(), {f[0], f[1], f[2]});
    dm.mesh = renderer_.createMesh(verts, idx);
    if (dm.mat)
        for (size_t i = 1; i < dm.mat->stages.size(); ++i) {
            const MatStage& st = dm.mat->stages[i];
            if (st.blend == 0) continue;  // "Dest" stages draw nothing
            DrawMesh::Pass p;
            p.stage = i;
            p.mesh = dm.mesh;
            if (st.texgen == 1 || st.texgen == 4) {
                DrawMesh tmp = dm;
                tmp.stage = &st;
                tmp.bakedUv[0] = p.bakedUv[0] = st.xfm[9];
                tmp.bakedUv[1] = p.bakedUv[1] = st.xfm[10];
                p.mesh = renderer_.createMesh(vertices(tmp), idx);
            }
            dm.passes.push_back(p);
        }
    dm.indices = std::move(idx);
    return &dm;
}

// Environ (texgen 5, VU 0x139): rows U_i = (S[0][i], -S[2][i], S[1][i]) of the
// stage rotation, offset 0.5; Sphere (2) uses the camera axes [I].
void SceneRenderer::texgen(Renderer::RenderState& st, const MatStage& s) const {
    if (s.texgen != 5 && s.texgen != 2) return;
    st.texgen = s.texgen;
    const auto& x = s.xfm;
    for (int i = 0; i < 3; ++i) {  // column i = U_i
        st.texU[i * 3 + 0] = x[size_t(0 + i)];
        st.texU[i * 3 + 1] = -x[size_t(6 + i)];
        st.texU[i * 3 + 2] = x[size_t(3 + i)];
    }
    st.texO[0] = st.texO[1] = 0.5f;
    for (int k = 0; k < 3; ++k) {
        st.eye[k] = camera_.pos[size_t(k)];
        st.camRight[k] = camera_.rot[size_t(k)];
        st.camUp[k] = camera_.rot[size_t(6 + k)];
    }
}

void SceneRenderer::drawPasses(const DrawMesh& dm, const float* mvp, const Renderer::RenderState& base) {
    if (!dm.mat) return;
    const MatObj& mat = *dm.mat;
    for (const DrawMesh::Pass& p : dm.passes) {
        if (p.stage >= mat.stages.size()) continue;
        const MatStage& s = mat.stages[p.stage];
        Renderer::RenderState st = base;
        st.texture = texture(s.tex, s.wrap == 1);
        st.blend = blendFor(s.blend);
        // Texture function: DECAL (texture alone, unlit) unless MultipassLit.
        st.texDecal = st.texture && mat.multipass != 2 ? 1 : 0;
        if (mat.multipass != 2) st.lighting = nullptr;
        st.colorScale = 1;
        // Fogged only when the pass blend is Src / SrcAlpha; never writes depth;
        // tests LEQUAL except for zmodes Disable / Force.
        st.fog = base.fog && (s.blend == 1 || s.blend == 3);
        st.depthTest = mat.zmode != 0 && mat.zmode != 3;
        st.depthWrite = false;
        st.uvOffset[0] = st.uvOffset[1] = 0;
        if (s.texgen == 1) st.uvOffset[0] = -(s.xfm[9] - p.bakedUv[0]), st.uvOffset[1] = s.xfm[10] - p.bakedUv[1];
        else if (s.texgen == 4) st.uvOffset[0] = s.xfm[9] - p.bakedUv[0], st.uvOffset[1] = s.xfm[10] - p.bakedUv[1];
        st.texgen = 0;
        texgen(st, s);
        renderer_.drawMesh(p.mesh, mvp, st);
    }
}

void SceneRenderer::drawMesh(const std::string& mesh, const Mat4& viewProj, const Mat4& model, const MeshOptions& opt) {
    const SceneObject* o = scene_.find(mesh);
    if (!o || !o->mesh) return;
    const DrawMesh* dm = build(mesh, opt.material.empty() ? o->mesh->mat : opt.material, opt.repeatTexture);
    if (!dm) return;
    Mat4 w = opt.useWorld ? model * world(*dm) : model;
    Mat4 mvp = viewProj * w;
    Renderer::RenderState st = liveState(*dm);
    setMatFlags(st, dm->mat);  // the vertex-colour rule and colour scale, as in drawObject
    st.model = w.m.data();
    if (dm->stage) texgen(st, *dm->stage);
    if (opt.texture) st.texture = opt.texture;
    st.uvOffset[0] += opt.uvOffset[0];
    st.uvOffset[1] += opt.uvOffset[1];
    for (int i = 0; i < 4; ++i) st.tint[i] *= opt.tint[i];
    if (opt.geometry) {
        renderer_.drawMesh(opt.geometry, mvp.m.data(), st);
        return;
    }
    renderer_.drawMesh(dm->mesh, mvp.m.data(), st);
    drawPasses(*dm, mvp.m.data(), st);
}

Renderer::RenderState SceneRenderer::liveState(const DrawMesh& dm) {
    Renderer::RenderState st = dm.state;
    if (!dm.mat) return st;
    std::copy_n(dm.mat->color.data(), 4, st.tint);
    if (!dm.mat->stages.empty()) {
        const MatStage& s = dm.mat->stages[0];
        if (s.tex != dm.texName) {
            dm.texName = s.tex;
            dm.texture = texture(s.tex, dm.repeat || s.wrap == 1);
        }
        st.texture = dm.texture;
        // Animated translation (MatAnim scrolls) relative to what was baked: u - tx, v + ty.
        if (s.texgen == 1) st.uvOffset[0] = -(s.xfm[9] - dm.bakedUv[0]), st.uvOffset[1] = s.xfm[10] - dm.bakedUv[1];
        else if (s.texgen == 4) st.uvOffset[0] = s.xfm[9] - dm.bakedUv[0], st.uvOffset[1] = s.xfm[10] - dm.bakedUv[1];
    }
    return st;
}

void SceneRenderer::replaceTexture(const std::string& tex, const Image& image) {
    if (image.width <= 0) return;
    std::map<int, int> swap;  // old handle -> new
    for (bool repeat : {false, true}) {
        std::string key = tex + (repeat ? "#r" : "");
        auto it = textures_.find(key);
        if (it == textures_.end() && repeat) continue;
        int id = renderer_.createTexture(image, repeat);
        if (it != textures_.end()) swap[it->second] = id;
        textures_[key] = id;
    }
    for (auto& [key, dm] : meshes_)
        if (dm.texName == tex)
            if (auto it = swap.find(dm.texture); it != swap.end()) dm.texture = dm.state.texture = it->second;
}

int SceneRenderer::texture(const std::string& name, bool repeat) {
    if (name.empty()) return 0;
    const bool enhance = uiTextures_ && renderer_.uiEnhance();
    std::string key = name + (repeat ? "#r" : "") + (enhance ? "#e" : "");
    if (auto it = textures_.find(key); it != textures_.end()) return it->second;
    const SceneObject* t = scene_.find(name);
    if (!t && lookup_) t = lookup_(name);
    int id = 0;
    if (t && t->tex && t->tex->image.width > 0)
        id = renderer_.createTexture(enhance ? enhanceImage(t->tex->image) : t->tex->image, repeat);
    textures_[key] = id;
    return id;
}

void SceneRenderer::draw(const std::string& name, const Mat4& viewProj, const Mat4& model, const EnvironObj* env) {
    env_ = env;
    if (const SceneObject* o = scene_.find(name)) drawObject(*o, viewProj, model, 0);
}

const float* SceneRenderer::boneMatrices(const MeshObj& skin) {
    for (size_t i = 0; i < 4; ++i) {
        float* dst = bones_ + i * 16;
        std::fill_n(dst, 16, 0.0f);
        if (i >= skin.bones.size() || skin.bones[i].name.empty()) continue;
        const SceneObject* b = resolve(skin.bones[i].name);
        const Transformable* t = b ? b->transformable() : nullptr;
        if (!t) continue;
        Xfm x = compose(skin.bones[i].offset, t->world);  // the bone's inverse bind, then its pose
        Mat4 m = Mat4::fromRowXfm(x.rot, x.pos);
        std::copy_n(m.m.data(), 16, dst);
    }
    return bones_;
}

// PsEnviron_v5 (0x1a2528) + the material pass setup (FUN_002ebcb0,
// FUN_0019d3b8): a material is lit when an environ is selected and
// use_environ || !vert_amb (decal stages never are). Without use_environ only
// the ambient-only program runs, with ambient = 1. The first point light
// replaces every directional light; otherwise up to 3 directionals, in list
// order, each D = -normalize(world +Y row) (the light travels along +Y).
void SceneRenderer::setMatFlags(Renderer::RenderState& st, const MatObj* mat) const {
    // Fog: the current environ's, when enabled (GS FOGCOL = fog_color).
    if (env_ && env_->fogEnable && env_->fogEnd != env_->fogStart) {
        st.fog = true;
        st.fogStart = env_->fogStart;
        st.fogEnd = env_->fogEnd;
        std::copy_n(env_->fogColor.data(), 3, st.fogColor);
    }
    if (!mat) return;
    // Unlit pass (VU program 0x614): !use_environ && vert_amb sends the vertex
    // colour untouched; the material colour is not used.
    if (!mat->useEnviron && mat->vertAmb) st.tint[0] = st.tint[1] = st.tint[2] = st.tint[3] = 1;
    st.useEnviron = mat->useEnviron;
    st.vertAmb = mat->vertAmb;
    st.vertDyn = mat->vertDyn;
    // Stage-0 blend 3 ("Multiply2") sends colour to the GS at x255, not x128.
    st.colorScale = !mat->stages.empty() && mat->stages[0].blend == 3 ? 255.0f / 128.0f : 1.0f;
    // Stage-0 blend 1 ("Src") selects TFX DECAL: the texture colour alone
    // (docs/material_stages.md), e.g. the HUD's cyan-tinted streak bulbs.
    st.texDecal = !mat->stages.empty() && mat->stages[0].blend == 1 ? 2 : 0;
}

const Lighting* SceneRenderer::lightingFor(const MatObj* mat) {
    if (!mat || !env_) return nullptr;
    if (!(mat->useEnviron || !mat->vertAmb)) return nullptr;
    if (!mat->stages.empty() && mat->stages[0].blend == 1) return nullptr;
    Lighting& l = lighting_;
    for (int i = 0; i < 3; ++i) l.ambient[i] = mat->useEnviron ? env_->ambient[size_t(i)] : 1.0f;
    l.count = 0;
    if (!mat->useEnviron) return &l;
    const LightObj* point = nullptr;
    std::vector<const LightObj*> dirs;
    for (const std::string& name : env_->lights) {
        const SceneObject* o = lookup_ ? lookup_(name) : scene_.find(name);
        if (!o || !o->light) continue;
        if (o->light->type == 0) {
            point = o->light.get();
            break;
        }
        if (o->light->type == 1 && dirs.size() < 3) dirs.push_back(o->light.get());
    }
    auto add = [&](const LightObj& lt) {
        std::copy_n(lt.color.data(), 3, l.color[l.count]);
        l.color[l.count][3] = lt.range;
        ++l.count;
    };
    if (point) {
        for (int i = 0; i < 3; ++i) l.dir[0][i] = point->trans.world.pos[size_t(i)];
        l.dir[0][3] = 0;
        add(*point);
        return &l;
    }
    for (const LightObj* lt : dirs) {
        const Xfm& w = lt->trans.world;
        float len = std::sqrt(w.rot[3] * w.rot[3] + w.rot[4] * w.rot[4] + w.rot[5] * w.rot[5]);
        if (len <= 0) continue;
        for (int i = 0; i < 3; ++i) l.dir[l.count][i] = -w.rot[size_t(3 + i)] / len;
        l.dir[l.count][3] = 1;
        add(*lt);
    }
    return &l;
}

void SceneRenderer::drawObject(const SceneObject& o, const Mat4& viewProj, const Mat4& model, int depth) {
    if (depth > 32) return;  // guard against cyclic draw lists
    if (o.environment) {
        env_ = o.environment.get();
        return;
    }
    if (o.particles) {
        if (o.particles->draw.showing && particlesEnabled()) drawParticles(o, viewProj, model);
        return;
    }
    if (o.flare) {
        if (o.flare->draw.showing) drawFlare(o, viewProj, model);
        return;
    }
    if (o.ui || o.text) {
        if (custom_ && o.drawable() && o.drawable()->showing) custom_(o, viewProj, model);
        return;
    }
    if (o.view) {
        if (!o.view->draw.showing) return;
        if (depth > 0 && std::find(skipped_.begin(), skipped_.end(), o.name) != skipped_.end()) return;
        for (const std::string& child : o.view->draw.draws)
            if (const SceneObject* c = resolve(child)) drawObject(*c, viewProj, model, depth + 1);
        return;
    }
    if (o.mesh) {
        if (!o.mesh->draw.showing) return;
        const DrawMesh* dm = build(o.name, overrideMat_ ? "#override" : ownMats_.count(o.name) ? "#own" : o.mesh->mat);
        if (!dm) return drawChildren(o.mesh->draw, viewProj, model, depth);
        // Skinned vertices go straight to world space through their bones.
        Mat4 w = dm->skin ? model : model * world(*dm);
        if (!dm->skin && hasCamera_ && dm->obj->trans.constraint >= 5) {
            Xfm b = billboard(dm->obj->trans);
            w = model * Mat4::fromRowXfm(b.rot, b.pos);
        }
        Mat4 mvp = viewProj * w;
        Renderer::RenderState st = liveState(*dm);
        st.lighting = lightingFor(dm->mat);
        setMatFlags(st, dm->mat);
        st.model = w.m.data();
        // A negative scale (mirrored tiles) reverses the winding.
        {
            const auto& m = w.m;
            float det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) + m[8] * (m[1] * m[6] - m[5] * m[2]);
            st.mirrored = det < 0;
        }
        if (dm->skin) st.bones = boneMatrices(*dm->skin);
        // Skinned vertices are already in world space (uModel = identity for texgen).
        Mat4 identity;
        if (dm->skin) st.model = identity.m.data();
        if (dm->stage) texgen(st, *dm->stage);
        if (additiveGain_ != 1 && (st.blend == Blend::Add || st.blend == Blend::AddAlpha))
            for (int k = 0; k < 3; ++k) st.tint[k] *= additiveGain_;
        renderer_.drawMesh(dm->mesh, mvp.m.data(), st);
        drawPasses(*dm, mvp.m.data(), st);
        drawChildren(o.mesh->draw, viewProj, model, depth);
        return;
    }
    if (o.multiMesh && o.multiMesh->draw.showing) {
        const SceneObject* inst = scene_.find(o.multiMesh->mesh);
        if (!inst || !inst->mesh) return;
        const DrawMesh* dm = build(inst->name, inst->mesh->mat);
        if (!dm) return;
        Renderer::RenderState st = liveState(*dm);
        st.lighting = lightingFor(dm->mat);
        setMatFlags(st, dm->mat);
        for (const Xfm& x : o.multiMesh->instances) {
            Mat4 w = model * Mat4::fromRowXfm(x.rot, x.pos);
            Mat4 mvp = viewProj * w;
            st.model = w.m.data();
            if (dm->stage) texgen(st, *dm->stage);
            renderer_.drawMesh(dm->mesh, mvp.m.data(), st);
            drawPasses(*dm, mvp.m.data(), st);
        }
    }
}

// RndDrawable's draw list belongs to every drawable, not only to views: a mesh
// draws its list after itself (e.g. the Garden's stage top, arrow03.mesh ->
// arrow03.1..7; Toxic's cliff.mesh -> cliff.1.mesh). A child that a view also
// lists (e.g. Toxic's tarp*.1 in venue_transparent.view) is left to the view, so
// translucent pieces are not blended twice.
void SceneRenderer::drawChildren(const Drawable& d, const Mat4& viewProj, const Mat4& model, int depth) {
    for (const std::string& child : d.draws)
        if (!viewMembers_.count(child))
            if (const SceneObject* c = resolve(child)) drawObject(*c, viewProj, model, depth + 1);
}

bool SceneRenderer::camera(const std::string& name, float aspect, Mat4& viewProj) const {
    const SceneObject* o = scene_.find(name);
    if (!o || !o->cam) return false;
    const CamObj& c = *o->cam;
    // The game's fov is horizontal for its 4:3 screen (PS2); derive the vertical one.
    float fovY = 2.0f * std::atan(std::tan(c.fov * 0.5f) / (4.0f / 3.0f));
    viewProj = Mat4::perspective(fovY, aspect, c.nearPlane, c.farPlane) *
               Mat4::cameraView(c.trans.world.rot, c.trans.world.pos);
    return true;
}

std::string SceneRenderer::firstCamera() const {
    for (const SceneObject& o : scene_.objects())
        if (o.cam) return o.name;
    return {};
}

Mat4 SceneRenderer::overviewCamera(float aspect) const {
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    for (const SceneObject& o : scene_.objects()) {
        if (!o.mesh || o.mesh->draw.sphere[3] <= 0) continue;
        const auto& sp = o.mesh->draw.sphere;  // world-space bounding sphere
        for (int i = 0; i < 3; ++i) {
            lo[i] = std::min(lo[i], sp[i] - sp[3]);
            hi[i] = std::max(hi[i], sp[i] + sp[3]);
        }
    }
    float c[3], r = 1;
    for (int i = 0; i < 3; ++i) {
        c[i] = (lo[i] + hi[i]) * 0.5f;
        r = std::max(r, (hi[i] - lo[i]) * 0.5f);
    }
    std::array<float, 3> eye{c[0], c[1] - 1.8f * r, c[2] + 0.6f * r};
    float f[3] = {c[0] - eye[0], c[1] - eye[1], c[2] - eye[2]};
    float fl = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    for (float& v : f) v /= fl;
    float right[3] = {f[1], -f[0], 0};  // forward x up(0,0,1)
    float rl = std::sqrt(right[0] * right[0] + right[1] * right[1]);
    right[0] /= rl, right[1] /= rl;
    float up[3] = {right[1] * f[2] - 0 * f[1], 0 * f[0] - right[0] * f[2], right[0] * f[1] - right[1] * f[0]};
    std::array<float, 9> rot{right[0], right[1], right[2], f[0], f[1], f[2], up[0], up[1], up[2]};
    return Mat4::perspective(0.9f, aspect, r * 0.05f, r * 6) * Mat4::cameraView(rot, eye);
}

MatObj* SceneRenderer::ownMaterial(const std::string& mesh) {
    if (auto it = ownMats_.find(mesh); it != ownMats_.end()) return it->second.get();
    const SceneObject* o = scene_.find(mesh);
    if (!o || !o->mesh) return nullptr;
    const SceneObject* m = scene_.find(o->mesh->mat);
    auto copy = std::make_unique<MatObj>(m && m->mat ? *m->mat : MatObj{});
    return (ownMats_[mesh] = std::move(copy)).get();
}

// FUN_001da848: constraints >= 5 rebuild the world rotation from the camera.
void SceneRenderer::drawParticles(const SceneObject& o, const Mat4& viewProj, const Mat4& model) {
    const ParticleSysObj& p = *o.particles;
    if ((p.live.empty() && p.preserved.empty()) || !hasCamera_) return;
    // P: the emitter's world for relative motion, else world space.
    Mat4 space = p.relativeMotion >= 1 ? model * Mat4::fromRowXfm(p.trans.world.rot, p.trans.world.pos) : model;
    // The camera's right / up in world space (rows 0 and 2).
    const auto& c = camera_.rot;
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    verts.reserve(p.live.size() * 4);
    idx.reserve(p.live.size() * 6);
    // VU program 0x5cc: the particle colour (times the environ ambient when the
    // mat uses it); no material colour, no fog.
    std::array<float, 3> amb{1, 1, 1};
    const SceneObject* mo = resolve(p.mat);
    if (mo && mo->mat && mo->mat->useEnviron && env_)
        for (int k = 0; k < 3; ++k) amb[size_t(k)] = env_->ambient[size_t(k)];
    struct Q {
        std::array<float, 3> pos;
        std::array<float, 4> color;
        float size;
    };
    std::vector<Q> qs;
    qs.reserve(p.live.size() + p.preserved.size());
    for (const auto& q : p.live) qs.push_back({q.pos, q.color, q.size});
    for (const auto& q : p.preserved) qs.push_back({q.pos, q.color, q.size});
    for (const auto& q : qs) {
        float h = std::max(q.size, 0.0f) * 0.5f;
        if (h <= 0) continue;
        const auto& m = space.m;  // column-major
        float cx = m[0] * q.pos[0] + m[4] * q.pos[1] + m[8] * q.pos[2] + m[12];
        float cy = m[1] * q.pos[0] + m[5] * q.pos[1] + m[9] * q.pos[2] + m[13];
        float cz = m[2] * q.pos[0] + m[6] * q.pos[1] + m[10] * q.pos[2] + m[14];
        uint8_t r = unit(q.color[0] * amb[0]), g = unit(q.color[1] * amb[1]), b = unit(q.color[2] * amb[2]), a = unit(q.color[3]);
        uint32_t base = uint32_t(verts.size());
        const float corner[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};
        const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        for (int k = 0; k < 4; ++k) {
            Vertex v{};
            float sx = corner[k][0] * h, sz = corner[k][1] * h;
            v.x = cx + c[0] * sx + c[6] * sz;
            v.y = cy + c[1] * sx + c[7] * sz;
            v.z = cz + c[2] * sx + c[8] * sz;
            v.u = uv[k][0], v.v = uv[k][1];
            v.r = r, v.g = g, v.b = b, v.a = a;
            verts.push_back(v);
        }
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    if (idx.empty()) return;
    int& mesh = particleMeshes_[o.name];
    if (mesh) renderer_.updateMesh(mesh, verts, idx);
    else mesh = renderer_.createMesh(verts, idx);
    Renderer::RenderState st;
    st.depthWrite = false;
    if (mo && mo->mat) {
        const MatObj& mat = *mo->mat;
        st.blend = blendFor(mat.blend);
        if (mat.zmode == 0) st.depthTest = false;
        if (!mat.stages.empty()) st.texture = texture(mat.stages[0].tex, false);
    } else {
        st.blend = Blend::AddAlpha;
    }
    renderer_.drawMesh(mesh, viewProj.m.data(), st);
}

void SceneRenderer::drawFlare(const SceneObject& o, const Mat4& viewProj, const Mat4& model) {
    FlareObj& f = *const_cast<SceneObject&>(o).flare;
    const auto& p = f.trans.world.pos;
    const Mat4 mvp = viewProj * model;
    const auto& m = mvp.m;
    float cx = m[0] * p[0] + m[4] * p[1] + m[8] * p[2] + m[12];
    float cy = m[1] * p[0] + m[5] * p[1] + m[9] * p[2] + m[13];
    float cz = m[2] * p[0] + m[6] * p[1] + m[10] * p[2] + m[14];
    float cw = m[3] * p[0] + m[7] * p[1] + m[11] * p[2] + m[15];
    float W = float(renderer_.viewportWidth()), H = float(renderer_.viewportHeight());
    // 1. The visibility test (read back after the pass; a frame late, as on the PS2).
    bool onScreen = cw > 0 && std::fabs(cx) < cw && std::fabs(cy) < cw && std::fabs(cz) <= cw;
    if (onScreen) {
        float sx = (cx / cw * 0.5f + 0.5f), sy = (cy / cw * 0.5f + 0.5f);
        flareTests_.push_back({&f, int(sx * W), int(sy * H), cz / cw * 0.5f + 0.5f});
    } else {
        f.visible = false;
    }
    // 2. count steps once per 60 Hz frame toward visible / hidden.
    float now = float(SDL_GetTicks());
    int frames = f.clock < 0 ? 1 : int((now - f.clock) * 0.06f);
    if (f.clock < 0 || frames > 0) f.clock = f.clock < 0 ? now : f.clock + frames / 0.06f;
    for (int k = 0; k < std::min(frames, 600); ++k) f.count = std::clamp(f.count + (f.visible ? 1 : -1), 0, f.steps);
    if (f.count == 0 || cw <= 0) {
        if (cw <= 0) f.count = 0;
        return;
    }
    // 4. Brightness = distance fade (r0 near, r1 far) * count / steps, into the mat colour.
    const SceneObject* mo = resolve(f.mat);
    if (!mo || !mo->mat) return;
    MatObj& mat = const_cast<MatObj&>(*mo->mat);
    float t = f.range[0] == f.range[1] ? 1.0f : std::clamp((cw - f.range[1]) / (f.range[0] - f.range[1]), 0.0f, 1.0f);
    float b = t * float(f.count) / float(f.steps);
    mat.color[0] = mat.color[1] = mat.color[2] = b;
    // 6. Size: s0, toward s1 as the flare's +Y faces the camera.
    float s = f.sizes[0];
    if (f.sizes[1] != f.sizes[0] && hasCamera_) {
        const auto& fy = f.trans.world.rot;
        float d = -(camera_.rot[3] * fy[3] + camera_.rot[4] * fy[4] + camera_.rot[5] * fy[5]);
        s = f.sizes[0] + (f.sizes[1] - f.sizes[0]) * std::max(0.0f, d);
    }
    // 7. A square on the 4:3 picture, s of its width, at the nearest Z.
    float px = s * H * (4.0f / 3.0f);
    float hx = px / W, hy = px / H;  // NDC half extents
    float nx = cx / cw, ny = cy / cw;
    std::vector<Vertex> v(4);
    const float corner[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}}, uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int k = 0; k < 4; ++k) {
        v[size_t(k)] = Vertex{nx + corner[k][0] * hx, ny + corner[k][1] * hy, 0, uv[k][0], uv[k][1], 255, 255, 255, 255};
    }
    int& mesh = particleMeshes_[o.name];
    std::vector<uint32_t> idx{0, 1, 2, 0, 2, 3};
    if (mesh) renderer_.updateMesh(mesh, v, idx);
    else mesh = renderer_.createMesh(v, idx);
    Renderer::RenderState st;
    st.blend = blendFor(mat.blend);
    st.depthTest = false;
    st.depthWrite = false;
    if (!mat.stages.empty()) st.texture = texture(mat.stages[0].tex, false);
    std::copy_n(mat.color.data(), 4, st.tint);
    st.useEnviron = false, st.vertAmb = false;  // the mat colour
    Mat4 identity;
    renderer_.drawMesh(mesh, identity.m.data(), st);
}

int SceneRenderer::dynamicMesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    if (dynamicUsed_ < dynamic_.size()) {
        int m = dynamic_[dynamicUsed_++];
        renderer_.updateMesh(m, vertices, indices);
        return m;
    }
    dynamic_.push_back(renderer_.createMesh(vertices, indices));
    ++dynamicUsed_;
    return dynamic_.back();
}

void SceneRenderer::drawLine(const std::string& matName, const Mat4& viewProj, const std::array<float, 3>& p0,
                             const std::array<float, 3>& p1, float width, const float* color) {
    const SceneObject* mo = resolve(matName);
    if (!mo || !mo->mat) return;
    const MatObj& mat = *mo->mat;
    float dx = p1[0] - p0[0], dy = p1[1] - p0[1], len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    float nx = -dy / len * width * 0.5f, ny = dx / len * width * 0.5f;
    // Point colours = the colour, alpha x 0.99 (vertex colour; the line mats use it).
    const float* col = color ? color : mat.color.data();
    uint8_t r = unit(col[0]), g = unit(col[1]), b = unit(col[2]), a = unit(col[3] * 0.99f);
    std::vector<Vertex> v{
        {p0[0] - nx, p0[1] - ny, p0[2], 0, 0, r, g, b, a}, {p0[0] + nx, p0[1] + ny, p0[2], 1, 0, r, g, b, a},
        {p1[0] + nx, p1[1] + ny, p1[2], 1, 1, r, g, b, a}, {p1[0] - nx, p1[1] - ny, p1[2], 0, 1, r, g, b, a}};
    std::vector<uint32_t> idx{0, 1, 2, 0, 2, 3};
    int mesh = dynamicMesh(v, idx);
    Renderer::RenderState st;
    st.blend = blendFor(mat.blend);
    st.depthWrite = false;
    if (mat.zmode == 0 || mat.zmode == 3) st.depthTest = false;
    if (!mat.stages.empty()) st.texture = texture(mat.stages[0].tex, mat.stages[0].wrap == 1);
    st.useEnviron = false, st.vertAmb = true;  // vertex colour
    renderer_.drawMesh(mesh, viewProj.m.data(), st);
}

void SceneRenderer::resolveFlares() {
    // A ring of pixel-pack buffers: this frame's tests are read back
    // kFlareLag frames later, so the GPU never stalls on the readback.
    FlareBatch& b = flareRing_[flareSlot_];
    if (!b.tests.empty()) {
        std::vector<float> depths = renderer_.mapDepths(b.buffer, b.tests.size());
        if (depths.size() == b.tests.size())
            for (size_t i = 0; i < depths.size(); ++i) b.tests[i].flare->visible = depths[i] >= b.tests[i].depth;
    }
    std::vector<std::array<int, 2>> pts;
    for (const FlareTest& t : flareTests_) pts.push_back({t.x, t.y});
    renderer_.readDepthsAsync(b.buffer, b.capacity, pts);
    b.tests = std::move(flareTests_);
    flareTests_.clear();
    flareSlot_ = (flareSlot_ + 1) % kFlareLag;
}

Xfm SceneRenderer::billboard(const Transformable& t) const {
    using V = std::array<float, 3>;
    auto row = [](const std::array<float, 9>& m, int i) { return V{m[size_t(i * 3)], m[size_t(i * 3 + 1)], m[size_t(i * 3 + 2)]}; };
    auto len = [](const V& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
    auto norm = [&](V v) {
        float l = len(v);
        if (l > 0) v = {v[0] / l, v[1] / l, v[2] / l};
        return v;
    };
    auto cross = [](const V& a, const V& b) { return V{a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; };
    const Xfm& w = t.world;
    V pos = w.pos, toObj = {pos[0] - camera_.pos[0], pos[1] - camera_.pos[1], pos[2] - camera_.pos[2]};
    V x, y, z;
    switch (t.constraint) {
    case 5:  // BillboardZ: spin about its own Z
        z = t.preserveScale ? norm(row(w.rot, 2)) : row(w.rot, 2);
        x = norm(cross(toObj, z));
        y = cross(z, x);
        break;
    case 6:  // BillboardXZ
        y = norm(toObj);
        x = norm(cross(y, V{0, 0, 1}));
        z = cross(x, y);
        break;
    case 7:  // BillboardXYZ
        y = norm(toObj);
        x = norm(cross(y, row(camera_.rot, 2)));
        z = cross(x, y);
        break;
    default:  // 8 FastBillboardXYZ: the camera's rotation
        x = row(camera_.rot, 0), y = row(camera_.rot, 1), z = row(camera_.rot, 2);
        break;
    }
    if (t.preserveScale) {
        float sx = len(row(w.rot, 0)), sy = len(row(w.rot, 1)), sz = len(row(w.rot, 2));
        for (int k = 0; k < 3; ++k) x[size_t(k)] *= sx, y[size_t(k)] *= sy, z[size_t(k)] *= sz;
    }
    Xfm out;
    out.rot = {x[0], x[1], x[2], y[0], y[1], y[2], z[0], z[1], z[2]};
    out.pos = pos;
    return out;
}
