// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <map>
#include <memory>
#include <string>

#include "render/scene_renderer.h"
#include "scene/anim.h"
#include "ui/ui_resources.h"

class GameData;

// A ghui panel's art: ghui/<name>.gh (meshes, external textures, LabelEx /
// ButtonEx / Text components), drawn through its own camera with the
// components' text laid out in the game's fonts.
class Panel {
public:
    // Throws std::runtime_error when the panel file is missing. `shared` is
    // ghui/common.gh (the UI's shared_rnd_files): its meta.cam, ui.cam and
    // ui.env replace the panel's own copies, so every panel follows the
    // transition animations.
    Panel(Renderer& renderer, const GameData& data, UiResources& res, const std::string& name, Scene* shared = nullptr);

    // Draws <name>.view (or the given view) through the panel's camera.
    void draw(const std::string& view = "");

    Scene& scene() { return *scene_; }
    // The panel camera's view-projection; false without a camera.
    bool viewProj(Mat4& vp) const;
    // Another camera of the panel (e.g. char_single.cam).
    bool cameraViewProj(const std::string& cam, Mat4& vp) const { return sr_->camera(cam, kScreenW / kScreenH, vp); }
    SceneRenderer& renderer() { return *sr_; }
    // The panel's ui.env ambient (the transition fade), for lit component meshes.
    std::array<float, 3> ambient() const;
    // Draws a mesh of the panel with another material at `world` (no stored transform).
    void drawMesh(const std::string& mesh, const std::string& material, const Mat4& vp, const Mat4& world);
    // Draws text with a resources template at `world`; returns its width.
    float drawText(const std::string& key, const std::string& text, const std::string& templateName,
                   const std::array<float, 3>& rgb, const Mat4& vp, const Mat4& world, float alpha = 1);
    // The width drawText would return, without drawing.
    float textWidth(const std::string& text, const std::string& templateName);
    const std::string& name() const { return name_; }

    // Component state, as the scripts and the navigator set it.
    void setState(const std::string& component, UiResources::State state) { states_[component] = state; }
    UiResources::State state(const std::string& component) const;
    // Text shown instead of the component's localized token.
    void setText(const std::string& component, const std::string& text) { texts_[component] = text; }
    std::string text(const std::string& component) const;
    void setShowing(const std::string& object, bool showing);
    // PictureEx set_tex_file: ghui/image/<file> replaces the picture's texture.
    void setTexFile(const std::string& component, const std::string& file);
    // Sets an animated object's frame (views drive their TransAnims).
    void setFrame(const std::string& object, float frame);
    // The frame last set on `object` (RndAnimatable frame), 0 before any.
    float frame(const std::string& object) const {
        auto it = frames_.find(object);
        return it == frames_.end() ? 0.0f : it->second;
    }
    // set_local_pos / set_local_scale (the tutorial boxes): the local transform, then the worlds below.
    void setLocalPos(const std::string& object, const std::array<float, 3>& pos);
    void setLocalScale(const std::string& object, const std::array<float, 3>& scale);
    void setLocalXfm(const std::string& object, const Xfm& local);
    // Mouse support: where each text (by key: a component's name, a list row
    // "<list>#<row>", "helpbar<k>") was drawn this frame, in 1280 x 720 menu units.
    struct Hit {
        std::string key;
        float x0, y0, x1, y1;
    };
    const std::vector<Hit>& hits() const { return hits_; }
    void clearHits() { hits_.clear(); }
    // The last key frame of an anim, or of a view's anims (0 when none).
    float endFrame(const std::string& object, int depth = 0);
    // Draws List components (rows come from the list's provider).
    using ListDraw = std::function<void(const SceneObject& list, const Mat4& viewProj, const Mat4& world)>;
    void setListDraw(ListDraw fn) { listDraw_ = std::move(fn); }

private:
    void drawComponent(const SceneObject& o, const Mat4& viewProj, const Mat4& model);
    float drawLaidOut(const std::string& key, const std::string& str, const std::string& fontName, TextStyle ts,
                      const UiObj* fitTo, const std::array<float, 3>& rgb, const Mat4& mvp, float alpha = 1);

    Renderer& renderer_;
    UiResources& res_;
    std::string name_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<SceneRenderer> sr_;
    std::unique_ptr<AnimPlayer> anims_;
    Scene* shared_ = nullptr;
    const SceneObject* sharedObject(const std::string& name) const;
    ListDraw listDraw_;
    std::string camera_;
    std::map<std::string, UiResources::State> states_;
    std::map<std::string, std::string> texts_;
    struct CachedText {
        std::string key;
        int mesh = 0;
        float width = 0;
        float sx = 1, sz = 1;  // LabelEx fit scale
        float x0 = 0, x1 = 0, z0 = 0, z1 = 0;  // the laid-out text's bounds (XZ plane)
    };
    std::map<std::string, CachedText> meshes_;  // by component
    std::vector<Hit> hits_;
    const GameData& data_;
    std::map<std::string, int> texFiles_;       // component -> texture handle
    std::map<std::string, int> texCache_;       // picture path -> texture handle
    std::map<std::string, float> frames_;       // setFrame, for the scripts' `frame`
};
