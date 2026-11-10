// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "render/ui_cloth.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "char/char_objects.h"

namespace gfx {

std::unique_ptr<UiCloth> UiCloth::make(const scene::Directory& dir, RenderDevice& device) {
    for (const auto& o : dir.objects)
        if (o->as<scene::CharHair>()) return std::unique_ptr<UiCloth>(new UiCloth(dir, device));
    return nullptr;
}

UiCloth::UiCloth(const scene::Directory& dir, RenderDevice& device) : dir_(dir), device_(device) {
    if (SDL_getenv("GD05_CLOTHLOG"))
        for (const auto& o : dir.objects)
            if (const scene::Trans* t = transOf(o.get()); t && (o->name == "Box01.mesh" || o->name == "popup_bg.mesh" || o->name == "cloth_controller.mesh"))
                SDL_Log("cloth: %s parent '%s', dir '%s' (self %s, trans %d, parent dir %d), resolves %s", o->name.c_str(), t->parent.c_str(), dir.name.c_str(),
                        dir.self ? dir.self->cls.c_str() : "-", dir.self && transOf(dir.self.get()) ? 1 : 0, dir.parent ? 1 : 0,
                        dir.resolve(t->parent) ? dir.resolve(t->parent)->cls.c_str() : "nothing");
    sk_.build(dir);
    rig_ = std::make_unique<chr::Rig>(dir, sk_);
    for (const auto& o : dir.objects) {
        if (const auto* c = o->as<scene::CharTransCopy>()) {
            Copy cp{sk_.find(c->src), sk_.find(c->dest)};
            if (cp.src >= 0 && cp.dest >= 0) copies_.push_back(cp);
        } else if (const auto* h = o->as<scene::CharHair>()) {
            for (const auto& s : h->strands)
                for (const auto& p : s.points)
                    if (const int b = sk_.find(p.bone); b >= 0) simulated_.push_back(b);
        }
    }
}

UiCloth::~UiCloth() = default;

void UiCloth::poll(ScenePose& pose, double seconds) {
    for (int i = 0; i < sk_.size(); ++i) sk_.setLocal(i, pose.local(sk_.object(i)));
    // CharTransCopy::Poll (RB 0x8233F6F8) [C]: the source's local transform
    // (the 64 bytes at +0x1C) copied onto the dest, which is then dirtied.
    for (const Copy& c : copies_) sk_.setLocal(c.dest, sk_.local(c.src));
    chr::RigContext ctx;
    ctx.dSeconds = float(seconds);
    ctx.seconds = seconds;
    rig_->poll(ctx);
    for (const Copy& c : copies_) pose.setLocal(sk_.object(c.dest), sk_.local(c.dest));
    for (int b : simulated_) pose.setLocal(sk_.object(b), sk_.local(b));
    ++polls_;
}

const GpuMesh* UiCloth::skinned(const scene::Mesh& mesh, const ScenePose& pose) {
    // Only the RenderDir's own meshes: the inline panel's copy of the cloth
    // (qp_sel_song's popup_bg.mesh, drawn into the texture) names bones its
    // own directory lacks, so it draws as it is.
    if (mesh.bones.empty() || mesh.verts.empty() || mesh.dir != &dir_) return nullptr;
    Skin& s = skins_[&mesh];
    if (!s.mesh) {
        s.packed = packVertices(mesh);
        for (const auto& f : mesh.faces) s.indices.insert(s.indices.end(), {f[0], f[1], f[2]});
        s.mesh = std::make_unique<DynamicMesh>(device_);
    }
    // Each bone's skinning transform: its offset (the inverse bind) then
    // its posed world transform, in the directory's space.
    std::vector<scene::Xfm> xf(mesh.bones.size());
    for (size_t i = 0; i < mesh.bones.size(); ++i) {
        const scene::Object* b = dir_.find<scene::Object>(mesh.bones[i].name);
        xf[i] = b ? scene::compose(mesh.bones[i].offset, pose.world(b)) : pose.world(&mesh);
    }
    s.work = s.packed;
    const size_t n = std::min(mesh.verts.size(), s.work.size() / 16);
    for (size_t i = 0; i < n; ++i) {
        const scene::MeshVertex& v = mesh.verts[i];
        float* o = &s.work[i * 16];
        float w[4] = {v.color[0], v.color[1], v.color[2], v.color[3]};
        float sum = w[0] + w[1] + w[2] + w[3];
        if (sum <= 1e-6f) w[0] = 1, sum = 1;
        float p[3] = {0, 0, 0}, nr[3] = {0, 0, 0}, t[3] = {0, 0, 0};
        for (int k = 0; k < 4; ++k) {
            if (w[k] <= 0) continue;
            const scene::Xfm& x = xf[v.bones[k] < xf.size() ? v.bones[k] : 0];
            const float wk = w[k] / sum;
            for (size_t c = 0; c < 3; ++c) {
                p[c] += wk * (v.pos[0] * x.rot[c] + v.pos[1] * x.rot[3 + c] + v.pos[2] * x.rot[6 + c] + x.pos[c]);
                nr[c] += wk * (v.normal[0] * x.rot[c] + v.normal[1] * x.rot[3 + c] + v.normal[2] * x.rot[6 + c]);
                t[c] += wk * (o[12] * x.rot[c] + o[13] * x.rot[3 + c] + o[14] * x.rot[6 + c]);
            }
        }
        const float nl = std::sqrt(nr[0] * nr[0] + nr[1] * nr[1] + nr[2] * nr[2]);
        const float tl = std::sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
        for (int c = 0; c < 3; ++c) {
            o[c] = p[c];
            o[3 + c] = nl > 0 ? nr[c] / nl : 0;
            o[12 + c] = tl > 0 ? t[c] / tl : 0;
        }
        o[6] = o[7] = o[8] = o[9] = 1;  // the weights were in the colour
    }
    s.gpu = s.mesh->upload(s.work, s.indices);
    s.gpu.skinned = false;
    return &s.gpu;
}

}  // namespace gfx
