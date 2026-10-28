// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render/camera.h"
#include "render/scene_gpu.h"
#include "render/text_layout.h"
#include "scene/ui_styles.h"

class GameData;

namespace scene {
class Milo;
class MiloLibrary;
class UILabel;
class ScreenMask;
struct BandText;
}  // namespace scene

// A UI panel milo (a PanelDir, e.g. ui/gen/main.milo_xbox) drawn with its UI
// components: meshes and groups as the panel's RndDir draws them (the
// drawables no group holds, by draw order; a group draws its members in its
// list order), labels and buttons as their template Text (ui_styles.h) laid
// out with the component's text fields, the type's resource meshes for
// pictures, check boxes and sliders. The panel's ObjectDir subdirectories
// (../metacam.milo, ../meta_bricks.milo...) are loaded with it, and the
// UITriggers for ui_enter are played to their end, as when the panel has
// entered. The UI manager (ui/ui_manager.h) drives the rest: animation
// frames, showing, component states, list rows, text entries, sliders and
// check boxes, and an overlay (the help bar's buttons).
namespace gfx {

class UiView {
public:
    UiView(const GameData& data, scene::MiloLibrary& library, const scene::UiStyles& styles, const scene::Locale& locale,
           SceneCache& cache);
    ~UiView();

    // Loads the panel (throws when the milo is missing or broken).
    void load(const std::string& path);
    scene::Milo* milo() const { return milo_; }
    const std::vector<scene::Milo*>& subdirs() const { return subdirs_; }

    // An object of the panel, else of its subdirectories.
    scene::Object* find(const std::string& name) const;
    // The PanelDir's camera (in the panel or a subdirectory), else GH2's
    // ui camera (config/default.dta (ui (cam (fov 34.516) (y -768) (near 1)
    // (far 1000)))).
    Camera camera() const;
    std::string cameraName() const;
    // GH2's ui camera (config/default.dta), for panels without one.
    static Camera defaultCamera();
    // The panel's environ (ui.env), or the default one.
    LightEnv environment();

    // Replaces a label's / button's string (empty: its own).
    void setText(const std::string& component, const std::string& text);
    bool hasText(const std::string& component) const { return overrides_.count(component) != 0; }
    // The string a label shows now (its override, else its localized token / text).
    std::string text(const std::string& component) const;
    // The focused component (a button draws focus_color and focus_scale).
    void setFocus(const std::string& component) { focus_ = component; }

    // ---- driven by the UI manager
    ScenePose& pose() { return pose_; }
    // Plays an anim object (TransAnim, MatAnim, AnimFilter, Group / view) at `frame`.
    void setFrame(const std::string& object, float frame);
    // The last frame of its keys (a group: of its members), 0 when none.
    float endFrame(const std::string& object) const;
    // The frame last set on it (0 before any).
    float frame(const std::string& object) const;
    void setShowing(const std::string& object, bool showing);
    bool showing(const std::string& object) const;
    // set_local_pos / set_local_scale: replace one part of the object's local transform.
    void setLocalPos(const std::string& object, float x, float y, float z);
    void setLocalScale(const std::string& object, float x, float y, float z);
    // Replaces the rotation (and scale) of the object's local transform
    // with the Euler angles x y z (radians; MakeRotMatrix as 0x82269FC0).
    void setLocalEuler(const std::string& object, float x, float y, float z);
    // A component's state (BandButton colours: normal / focus / disabled / selecting_color).
    void setState(const std::string& component, scene::UiState state);
    void clearStates() { states_.clear(); }
    scene::UiState state(const std::string& component) const;
    void setCheck(const std::string& component, bool on) { checks_[component] = on; }
    // A slider's pod (its type's pod TransAnim at current / (steps - 1)).
    void setSlider(const std::string& component, int current, int steps);
    // UIPicture set_tex: another texture file (relative to ui/).
    void setPictureTex(const std::string& component, const std::string& file);
    // RndTex::SetBitmap on one of the panel's textures: the materials using
    // Tex `texName` draw the texture file `file` (relative to ui/) instead
    // (the endgame photo, GamePanel 0x822CC9B0).
    void setTexFile(const std::string& texName, const std::string& file);
    // An object's world transform as the panel poses it (a BandPlacer, a
    // UIProxy); false when the panel has no such object.
    bool worldXfm(const std::string& object, scene::Xfm& out) const;
    // The light environment of one of the panel's Environs (CharsysPanel
    // set_env); the panel's own when `environName` is not one.
    LightEnv environment(const std::string& environName);
    // A UIList's rows: per row the strings of its type's element texts and
    // the materials of its element meshes (empty: hidden), by element name.
    struct ListRow {
        std::map<std::string, std::string> texts;
        std::map<std::string, std::string> mats;
        bool header = false;
        bool disabled = false;  // the provider's IsActive is false: the type's "disabled" colours
        float slot = -1;  // its position in the list (rows after a header start one slot down; fractional while scrolling); -1: its index
    };
    void setList(const std::string& component, std::vector<ListRow> rows, int selectedRow);
    // The list's selected row (as set_list gave it), -1 without rows.
    int listRow(const std::string& component) const {
        auto it = lists_.find(component);
        return it == lists_.end() || it->second.rows.empty() ? -1 : it->second.selected;
    }
    // A BandTextEntry: the entered text and the character being picked.
    void setTextEntry(const std::string& component, const std::string& entered, const std::string& picking, bool done);

    // UITriggers of the panel and its subdirectories for an event (ui_enter,
    // ui_exit_forward...): the anim each plays.
    struct Trigger {
        const scene::Object* anim = nullptr;
        bool blockTransition = false;
        float endFrame = 0;
    };
    std::vector<Trigger> triggers(const std::string& event) const;

    // Called after the panel has drawn (the help bar's buttons and labels).
    std::function<void(MaterialRenderer&)> overlay;
    // Text in a template Text's font and style at `world`; returns its width.
    // `key` keeps the layout between frames.
    float drawTemplate(MaterialRenderer& r, const std::string& key, const scene::Text& tmpl, const std::string& str,
                       const scene::Xfm& world, const float* rgb = nullptr, float alpha = 1);
    float templateWidth(const scene::Text& tmpl, const std::string& str);
    void drawMeshAt(MaterialRenderer& r, const scene::Mesh& mesh, const scene::Mat* mat, const scene::Xfm& world);
    // A resource milo named by ui_objects.dta ("helpbar.milo", "../ui/button.milo"), loaded once.
    scene::Milo* resource(const std::string& rel);

    // The ScreenMasks the last draw met (showing): colour (rgba, from the
    // mask and its material) and screen rectangle (0..1). The caller fills them.
    struct MaskDraw {
        std::array<float, 4> rgba{};
        std::array<float, 4> rect{0, 0, 1, 1};
        uint32_t texture = 0;  // the mat's diffuse texture (a TextureId), 0: plain colour
        int blend = 3;         // the mat's blend (material.h MatBlend; no mat: SrcAlpha)
    };
    const std::vector<MaskDraw>& masks() const { return masks_; }
    // When set, the panel's own ScreenMasks are drawn through it at their
    // place in the draw order (unlockvenue<n>.milo's black matte.mask is the
    // background); the subdirectories' (the metacam's edge) stay on top.
    std::function<void(const MaskDraw&)> maskNow;
    // Draws something else at a UIProxy's / BandPlacer's place in the panel's
    // draw order (the store's guitar and character, drawn in store.milo's
    // guitar.pxy / char_store.placer), by the object's name.
    std::map<std::string, std::function<void(MaterialRenderer&)>> objectDraws;

    // Draws the panel (after r.setFrame with camera() / environment()).
    void draw(MaterialRenderer& r);

    // Mouse support: where the interactive components (buttons, check boxes,
    // sliders, pictures, text entries; a list's rows as "<list>#<row>", a
    // template text by its key) were drawn this frame, in menu units
    // (1280 x 720), in draw order: the projected bounds of their text or mesh.
    struct Hit {
        std::string key;
        float x0, y0, x1, y1;
    };
    const std::vector<Hit>& hits() const { return hits_; }
    // Records `key` from a local box (lo..hi) placed by `world`.
    void addHit(const std::string& key, const float lo[3], const float hi[3], const Mat4& world);

    int textsDrawn() const { return textsDrawn_; }
    int meshesDrawn() const { return meshesDrawn_; }
    // Components whose template, font or string could not be found.
    const std::vector<std::string>& problems() const { return problems_; }

private:
    struct LabelText;
    struct ListState {
        std::vector<ListRow> rows;
        int selected = 0;
    };
    struct EntryState {
        std::string entered, picking;
        bool done = false;
    };
    void drawObject(MaterialRenderer& r, const scene::Object& o, int depth);
    void drawLabel(MaterialRenderer& r, const scene::UILabel& l, const scene::BandText* band, const float* labelColor);
    void drawText(MaterialRenderer& r, const scene::Text& t);
    void drawStyleMeshes(MaterialRenderer& r, const scene::Object& component);
    void drawList(MaterialRenderer& r, const scene::Object& list);
    void drawTextEntry(MaterialRenderer& r, const scene::Object& entry);
    void addMask(const scene::ScreenMask& m);
    scene::Xfm componentWorld(const scene::Object& o) const;
    std::string labelString(const scene::UILabel& l) const;
    const scene::Mat* findMat(const scene::Object& owner, const std::string& name) const;
    // Loads the milo's ObjectDir subdirectories and links them to its root
    // directory (scene::Directory::linked); returns them.
    std::vector<scene::Milo*> linkSubdirs(scene::Milo& milo, int depth);
    // A texture file named by a type (UIPicture tex_file, relative to ui/),
    // or nullptr.
    const scene::Tex* textureFile(const std::string& rel);
    void problem(const std::string& what);

    const GameData& data_;
    scene::MiloLibrary& lib_;
    const scene::UiStyles& styles_;
    const scene::Locale& locale_;
    SceneCache& cache_;
    TextDrawer texts_;
    ScenePose pose_;
    scene::Milo* milo_ = nullptr;
    std::vector<scene::Milo*> subdirs_;
    std::map<std::string, std::string> overrides_;
    std::string focus_;
    std::map<std::string, scene::UiState> states_;
    std::map<std::string, bool> checks_;
    std::map<std::string, std::pair<int, int>> sliders_;
    std::map<std::string, std::string> pictureTex_;
    std::map<std::string, ListState> lists_;
    std::map<std::string, EntryState> entries_;
    std::map<std::string, float> frames_;
    std::map<const scene::Object*, std::unique_ptr<LabelText>> labels_;
    std::map<std::string, std::unique_ptr<LabelText>> keyed_;
    std::map<std::string, std::unique_ptr<scene::Tex>> texFiles_;
    std::vector<MaskDraw> masks_;
    std::vector<Hit> hits_;
    Mat4 viewProj_;  // the frame's camera, for the hits
    void addLayoutHit(const std::string& key, const TextLayout& layout, const Mat4& world);
    int textsDrawn_ = 0, meshesDrawn_ = 0;
    std::vector<std::string> problems_;
};

}  // namespace gfx
