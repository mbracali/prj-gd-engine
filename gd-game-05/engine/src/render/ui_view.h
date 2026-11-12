// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cmath>
#include <functional>
#include <map>
#include <set>
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

class UiCloth;

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
    // The panel's environ (ui.env), or the UI's own (defaultEnvironment).
    LightEnv environment();
    // RB's UIManager makes one environ for the UI, its ambient from config
    // (ui (env (ambient 1 1 1))) (UIManager::Init RB 0x82259408 sets
    // env->data+0x3c from it) [C]: panels without an environ of their own
    // draw in it (GD05-049: the port's fallback, GH2's 0.3 ambient, made
    // every label of such a panel grey).
    static void setDefaultAmbient(float r, float g, float b);
    static LightEnv defaultEnvironment();

    // Replaces a label's / button's string (empty: its own).
    void setText(const std::string& component, const std::string& text);
    // UILabel set_wrap_width (0x8214B118): the label's Text wraps at this width.
    void setWrapWidth(const std::string& component, float width);
    bool hasText(const std::string& component) const { return overrides_.count(component) != 0; }
    // The string a label shows now (its override, else its localized token / text).
    std::string text(const std::string& component) const;
    // The focused component (a button draws focus_color and focus_scale).
    void setFocus(const std::string& component) { focus_ = component; }
    // The panel holds the UI's focus (RB's BandHighlight: normal, else disabled).
    void setUiFocus(bool on) { uiFocus_ = on; }

    // ---- driven by the UI manager
    ScenePose& pose() { return pose_; }
    // Plays an anim object (TransAnim, MatAnim, AnimFilter, Group / view) at `frame`.
    void setFrame(const std::string& object, float frame);
    // The last frame of its keys (a group: of its members), 0 when none.
    float endFrame(const std::string& object) const;
    // The frame last set on it (0 before any).
    float frame(const std::string& object) const;
    void setShowing(const std::string& object, bool showing);
    void setShowing(const scene::Object* object, bool showing) { if (object) pose_.setShowing(object, showing); }
    bool showing(const scene::Object* object) const { return object && pose_.showing(object); }
    // `name` in the inline directory `scope` (RB's player slots: slot0..3
    // of seldiff_solo.milo, each its own copy of seldiffpart_player.milo);
    // nullptr when there is no such directory or object.
    scene::Object* findIn(const std::string& scope, const std::string& name) const;
    std::vector<scene::Object*> byClassIn(const std::string& scope, const std::string& cls) const;
    // A player slot's directory shown or not (MultiFocusScreen: a slot no
    // player has is hidden); a hidden one's TexRenderer draws nothing and
    // the meshes showing its texture are hidden.
    void setScopeShowing(const std::string& scope, bool showing) {
        if (showing) hiddenScopes_.erase(scope);
        else hiddenScopes_.insert(scope);
    }
    bool showing(const std::string& object) const;
    // set_local_pos / set_local_scale: replace one part of the object's local transform.
    void setLocalPos(const std::string& object, float x, float y, float z);
    void setLocalScale(const std::string& object, float x, float y, float z);
    // Replaces the rotation (and scale) of the object's local transform
    // with the Euler angles x y z (radians; MakeRotMatrix as 0x82269FC0).
    void setLocalEuler(const std::string& object, float x, float y, float z);
    // A component's state (BandButton colours: normal / focus / disabled / selecting_color).
    void setState(const std::string& component, scene::UiState state);
    // How far a selection has gone (0..1 over select_frames): a BandButton
    // type's selecting_anim plays over it (RB 0x822D4848).
    void setSelectProgress(const std::string& component, float t) { selectProgress_[component] = t; }
    void clearStates() { states_.clear(); }
    scene::UiState state(const std::string& component) const;
    void setCheck(const std::string& component, bool on) { checks_[component] = on; }
    // A slider's pod (its type's pod TransAnim at current / (steps - 1)).
    void setSlider(const std::string& component, int current, int steps);
    // UIPicture set_tex: another texture file (relative to ui/).
    void setPictureTex(const std::string& component, const std::string& file);
    // `{<mesh> set_mat <mat>}` (RndMesh's mat): the Mat the mesh draws with.
    void setMeshMat(const std::string& mesh, const std::string& mat) { meshMat_[mesh] = mat; }
    std::string meshMat(const std::string& mesh) const {
        const auto it = meshMat_.find(mesh);
        return it == meshMat_.end() ? std::string() : it->second;
    }
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
        // Space below the row on top of the list's spacing: RB's UIList lays
        // each row out at its index x spacing + the gaps its provider gives
        // the rows above (0x82271C58, the provider's vfunc +0x24; RB's
        // SongProvider: band.dta's song_select_gap, 0x82400180) [C].
        float gap = 0;
    };
    // The selected row's highlight as last drawn (the rb_songlist_highlight
    // walk): its quad's extent along the list in the panel's world space.
    struct ListHighlight {
        bool drawn = false;
        int selected = -1;
        float lo = 0, hi = 0;
    };
    ListHighlight listHighlight(const std::string& component) const {
        auto it = listHighlights_.find(component);
        return it == listHighlights_.end() ? ListHighlight{} : it->second;
    }
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
    // RndAnimatable::Animate (RB 0x8221BE38) [C]: from `startFrame` (the
    // anim's current frame for type dest) to `endFrame` at `fps` frames a
    // second in the rate's clock (`rate`: 0 song seconds, 1 / 3 beats, 2 UI
    // seconds, 4 tutorial), after `delay` (same units); loop wraps between
    // the two (type loop).
    struct Trigger {
        const scene::Object* anim = nullptr;
        bool blockTransition = false;
        float endFrame = 0;
        float startFrame = 0, fps = 30, delay = 0;
        int rate = 2;
        bool loop = false, dest = false, wait = false;
        // UITrigger::Trigger (RB 0x82262B28) [C]: a block_transition
        // trigger blocks (IsBlocking 0x82262918) for the longest of its
        // anims' whole frame ranges / 30 s, whatever its own range.
        float blockSeconds = 0;
        // Seconds (of the rate's clock) from start to end.
        float seconds() const { return fps > 0 ? std::abs(endFrame - startFrame) / fps : 0; }
    };
    std::vector<Trigger> triggers(const std::string& event) const;

    // Called after the panel has drawn (the help bar's buttons and labels).
    std::function<void(MaterialRenderer&)> overlay;
    // Text in a template Text's font and style at `world`; returns its width.
    // `key` keeps the layout between frames.
    // RB's RenderDir (qp_selsong.milo): after draw(), the texture its
    // TexRenderer drew the inline panel into and where it shows on the
    // screen (1280 x 720 units); id 0 for other panels.
    struct Rendered {
        TextureId texture{};
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        bool bottomUp = true;
    };
    const Rendered& rendered() const { return rendered_; }
    // The cloth's polls so far (0: none; walk checks).
    int clothPolls() const;
    // The cloth mesh's screen bounds last frame (1280 x 720 units); false without a cloth.
    bool clothRect(float& x0, float& y0, float& x1, float& y1) const {
        if (!clothPolls() || clothX1_ <= clothX0_) return false;
        x0 = clothX0_, y0 = clothY0_, x1 = clothX1_, y1 = clothY1_;
        return true;
    }
    float drawTemplate(MaterialRenderer& r, const std::string& key, const scene::Text& tmpl, const std::string& str,
                       const scene::Xfm& world, const float* rgb = nullptr, float alpha = 1, int align = -1,
                       const scene::BandText* style = nullptr);
    float templateWidth(const scene::Text& tmpl, const std::string& str);
    void drawMeshAt(MaterialRenderer& r, const scene::Mesh& mesh, const scene::Mat* mat, const scene::Xfm& world);
    // The width drawTemplate gives `str` (no draw).
    float templateWidth(const std::string& key, const scene::Text& tmpl, const std::string& str);
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
    // The skinned cloth mesh's screen bounds (clothX0_..): through viewProj_.
    void clothBounds(const scene::Mesh& mesh);

    int textsDrawn() const { return textsDrawn_; }
    int meshesDrawn() const { return meshesDrawn_; }
    // The rb_text_colors walk: every text of the last frame as drawn.
    void setTextProbes(bool on) { texts_.probes = on ? &textProbes_ : nullptr; }
    const std::vector<TextProbe>& textProbes() const { return textProbes_; }
    // Components whose template, font or string could not be found.
    const std::vector<std::string>& problems() const { return problems_; }

private:
    struct LabelText;
    void setProbeStyle(const scene::BandText* band, const LabelText& lt);
    struct ListState {
        std::vector<ListRow> rows;
        int selected = 0;
        int first = 0;  // the first row shown (a list longer than its display_num scrolls)
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
    void drawHighlight(MaterialRenderer& r, const scene::Object& highlight);
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
    std::map<std::string, float> wrapWidths_;  // set_wrap_width
    std::string focus_;
    bool uiFocus_ = true;
    std::map<std::string, scene::UiState> states_;
    std::map<std::string, bool> checks_;
    std::map<std::string, std::pair<int, int>> sliders_;
    std::map<std::string, std::string> pictureTex_;
    std::map<std::string, std::string> meshMat_;  // setMeshMat
    std::map<std::string, ListState> lists_;
    std::map<std::string, ListHighlight> listHighlights_;
    std::map<std::string, EntryState> entries_;
    std::map<std::string, float> frames_;
    std::map<const scene::Object*, std::unique_ptr<LabelText>> labels_;
    std::map<std::string, std::unique_ptr<LabelText>> keyed_;
    std::map<std::string, std::unique_ptr<scene::Tex>> texFiles_;
    std::vector<MaskDraw> masks_;
    std::vector<Hit> hits_;
    std::vector<TextProbe> textProbes_;
    Mat4 viewProj_;  // the frame's camera, for the hits
    TargetId renderTarget_{};
    Rendered rendered_;
    // RB's RenderDir drawn as the original: every TexRenderer's panel into
    // its texture, then the RenderDir's own objects through its camera.
    bool drawRenderDir3d(MaterialRenderer& r);
    void drawRoots(MaterialRenderer& r, const scene::Directory& dir);
    std::map<const scene::Object*, TargetId> rdTargets_;
    std::set<std::string> hiddenScopes_;
    std::set<const scene::Directory*> texRendered_;  // the TexRenderers' panels
    // A RenderDir's swaying cloth (render/ui_cloth.h), polled once a frame.
    std::unique_ptr<UiCloth> cloth_;
    std::map<std::string, float> selectProgress_;
    bool clothChecked_ = false;
    double clothLastMs_ = -1;
    // The cloth mesh's screen bounds (1280 x 720 units) last frame: where
    // the texture's hits go (the list's rows under the mouse) [I].
    float clothX0_ = 0, clothY0_ = 0, clothX1_ = 0, clothY1_ = 0;
    void addLayoutHit(const std::string& key, const TextLayout& layout, const Mat4& world);
    int textsDrawn_ = 0, meshesDrawn_ = 0;
    std::vector<std::string> problems_;
};

}  // namespace gfx
