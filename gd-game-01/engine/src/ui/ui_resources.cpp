// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/ui_resources.h"

#include <algorithm>
#include <SDL3/SDL.h>

#include "data/game_data.h"
#include "ui/pc_strings.h"
#include "script/script.h"

namespace {

const char* const kStateNames[] = {"normal", "focused", "selected", "disabled"};

std::array<float, 3> rgb(const DataNode& n, size_t first) {
    std::array<float, 3> c{};
    for (size_t k = 0; k < 3 && first + k < n.children.size(); ++k) c[k] = n.floatAt(first + k);
    return c;
}

}  // namespace

UiResources::UiResources(Renderer& renderer, const GameData& data, ScriptLoader& loader) {
    scene_ = std::make_unique<Scene>(data.read("ghui/gen/resources.rnd_ps2"));
    scene_->loadExternalTextures(data, "ghui");
    renderer_ = std::make_unique<SceneRenderer>(renderer, *scene_);
    renderer_->setUiTextures(true);
    // Upload the shared component textures now (sliders, toggles, help bar):
    // created on first draw, they stalled a screen's first transition frame (B57).
    for (const SceneObject& o : scene_->objects())
        if (o.mat)
            for (const MatStage& st : o.mat->stages)
                if (!st.tex.empty()) renderer_->textureHandle(st.tex);
    config_ = loader.load("ghui/config.dta");
    // (colors (text (normal r g b) (focused ...) (selected ...) (disabled ...)) ...)
    if (const DataNode* colors = config_.find("colors"))
        if (const DataNode* text = colors->find("text"))
            for (int s = 0; s < 4; ++s)
                if (const DataNode* c = text->find(kStateNames[s])) textColors_[size_t(s)] = rgb(*c, 1);
    try {
        DataNode locale = loader.load("ghui/eng/locale.dta");
        for (const DataNode& e : locale.children)
            if (e.isList() && e.children.size() >= 2 && e.children[0].isText() && e.children[1].isText())
                strings_[e.children[0].s] = e.children[1].s;
    } catch (const std::exception& ex) {
        SDL_Log("ui: no locale: %s", ex.what());
    }
    applyPcStrings(strings_);
}

std::array<std::array<float, 3>, 4> UiResources::parseColors(const DataNode* colors,
                                                              const std::array<std::array<float, 3>, 4>& base,
                                                              const char* key) const {
    auto out = base;
    const DataNode* tc = colors ? colors->find(key) : nullptr;
    if (!tc || tc->children.size() < 2) return out;
    if (tc->children[1].isText()) {  // a named colour set, e.g. "text"
        if (tc->children[1].s == "text") return textColors_;
        return out;
    }
    if (tc->children[1].isList()) {  // per state
        for (int s = 0; s < 4; ++s)
            if (const DataNode* c = tc->find(kStateNames[s])) out[size_t(s)] = rgb(*c, 1);
        return out;
    }
    auto c = rgb(*tc, 1);  // one colour for every state
    for (auto& s : out) s = c;
    return out;
}

UiResources::Style UiResources::parseStyle(const DataNode* component, const DataNode* style) const {
    Style st;
    st.colors = textColors_;
    if (component) {
        if (const DataNode* t = component->find("text")) st.textTemplate = t->textAt(1);
        st.colors = parseColors(component->find("colors"), st.colors);
    }
    if (style) {
        if (const DataNode* t = style->find("text")) st.textTemplate = t->textAt(1);
        if (const DataNode* f = style->find("font")) st.font = f->textAt(1);
        if (const DataNode* c = style->find("characters")) st.characters = c->textAt(1);
        if (const DataNode* m = style->find("mesh")) st.mesh = m->textAt(1);
        if (const DataNode* m = style->find("disabled_mesh")) st.disabledMesh = m->textAt(1);
        if (const DataNode* t = style->find("tex_on")) st.texOn = t->textAt(1);
        if (const DataNode* t = style->find("tex_off")) st.texOff = t->textAt(1);
        if (const DataNode* m = style->find("slider_mesh")) st.sliderMesh = m->textAt(1);
        if (const DataNode* m = style->find("pod_mesh")) st.podMesh = m->textAt(1);
        st.meshColors = parseColors(style->find("colors"), {{{1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}}}, "mesh_color");
        if (const DataNode* l = style->find("length"); l && l->children.size() > 1) st.length = l->children[1].i;
        st.colors = parseColors(style->find("colors"), st.colors);
    }
    return st;
}

const UiResources::Style& UiResources::style(const std::string& component, const std::string& name) {
    std::string key = component + "|" + name;
    if (auto it = styles_.find(key); it != styles_.end()) return it->second;
    const DataNode* comps = config_.find("components");
    const DataNode* comp = comps ? comps->find(component) : nullptr;
    const DataNode* styles = comp ? comp->find("styles") : nullptr;
    const DataNode* st = styles ? styles->find(name) : nullptr;
    if (!st && styles) st = styles->find("default");
    return styles_[key] = parseStyle(comp, st);
}

const TextObj* UiResources::textTemplate(const std::string& name) const {
    const SceneObject* o = scene_->find(name);
    return o ? o->text.get() : nullptr;
}

const FontMetrics* UiResources::font(const std::string& name) {
    if (auto it = fonts_.find(name); it != fonts_.end()) return it->second.get();
    const SceneObject* f = scene_->find(name);
    const FontMetrics* result = nullptr;
    if (f && f->font) {
        const SceneObject* mat = scene_->find(f->font->mat);
        const SceneObject* tex = mat && mat->mat && !mat->mat->stages.empty() ? scene_->find(mat->mat->stages[0].tex) : nullptr;
        static const Image empty;
        fonts_[name] = std::make_unique<FontMetrics>(*f->font, tex && tex->tex ? tex->tex->image : empty);
        result = fonts_[name].get();
    } else {
        fonts_[name] = nullptr;
    }
    return result;
}

int UiResources::fontTexture(const std::string& name) {
    const SceneObject* f = scene_->find(name);
    if (!f || !f->font) return 0;
    const SceneObject* mat = scene_->find(f->font->mat);
    if (!mat || !mat->mat || mat->mat->stages.empty()) return 0;
    return renderer_->textureHandle(mat->mat->stages[0].tex);
}

int UiResources::fontZMode(const std::string& name) const {
    const SceneObject* f = scene_->find(name);
    const SceneObject* mat = f && f->font ? scene_->find(f->font->mat) : nullptr;
    return mat && mat->mat ? mat->mat->zmode : 0;
}

std::array<float, 4> UiResources::fontMatColor(const std::string& name) const {
    const SceneObject* f = scene_->find(name);
    const SceneObject* mat = f && f->font ? scene_->find(f->font->mat) : nullptr;
    return mat && mat->mat ? mat->mat->color : std::array<float, 4>{1, 1, 1, 1};
}

bool UiResources::fontUsesEnviron(const std::string& name) const {
    const SceneObject* f = scene_->find(name);
    const SceneObject* mat = f && f->font ? scene_->find(f->font->mat) : nullptr;
    return mat && mat->mat && mat->mat->useEnviron;
}

void UiResources::drawMeshAt(const std::string& mesh, const Mat4& viewProj, const Mat4& world,
                             const std::array<float, 3>& tint, const std::array<float, 3>& ambient) {
    SceneRenderer::MeshOptions opt;
    opt.useWorld = false;
    opt.tint[0] = tint[0], opt.tint[1] = tint[1], opt.tint[2] = tint[2];
    const SceneObject* o = scene_->find(mesh);
    const SceneObject* mat = o && o->mesh ? scene_->find(o->mesh->mat) : nullptr;
    if (mat && mat->mat && mat->mat->useEnviron)
        for (int k = 0; k < 3; ++k) opt.tint[k] *= std::clamp(ambient[size_t(k)], 0.0f, 1.0f);
    renderer_->drawMesh(mesh, viewProj, world, opt);
}

std::string UiResources::localize(const std::string& token) const {
    auto it = strings_.find(token);
    return it == strings_.end() ? token : it->second;
}

void UiResources::drawMesh(const std::string& mesh, const Mat4& viewProj, const Mat4& world, int texture,
                           const std::array<float, 3>& ambient) {
    const SceneObject* o = scene_->find(mesh);
    if (!o || !o->mesh) return;
    // Lit materials take the panel's ui.env ambient (the transition fade-in), as the fonts do.
    const SceneObject* mat = scene_->find(o->mesh->mat);
    const bool lit = mat && mat->mat && mat->mat->useEnviron;
    // The component's transform replaces the mesh's layout position in resources.rnd_ps2.
    const Transformable& t = o->mesh->trans;
    Xfm undo = compose(inverse(t.world), t.local);
    SceneRenderer::MeshOptions opt;
    opt.texture = texture;
    if (lit)
        for (int k = 0; k < 3; ++k) opt.tint[k] = std::clamp(ambient[size_t(k)], 0.0f, 1.0f);
    renderer_->drawMesh(mesh, viewProj, world * Mat4::fromRowXfm(undo.rot, undo.pos), opt);
}
