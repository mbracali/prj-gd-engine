// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

#include "data/dta.h"

class GameData;

namespace scene {

class Object;

// The object types of ui/ui_objects.dta (included by config/objects.dta),
// read as plain data (macros expanded, {pack_color r g b} evaluated; the
// types' script handlers are left to the interpreter). A UI component's
// `type` (its object block's type symbol, e.g. "impactor") picks one entry
// of (<Class> (types (<type> ...))); the class may name a resource_file of
// its own (BandButton: ../ui/button.milo with button.tnm). The resource
// loader (@ 0x82372D40) reads the type's `texts` (text templates, with a
// font per state for sliders) and `meshes` (a material per state).
//
// The text chain of a BandLabel / BandButton: type -> resource_file (e.g.
// impactor.milo = ui/gen/impactor.milo_ps2, holding a Font and the template
// Text impactor.txt) -> the component's Text copies the template's font,
// italics and markup (@ 0x8214CB50); size, alignment, leading, wrap width,
// caps and kerning come from the component (@ 0x8214C610).
struct UiStyle {
    std::string cls, type;
    const ::DataNode* node = nullptr;   // (<type> ...), or nullptr when unknown
    std::string resourceFile;         // the type's resource_file, as written ("impactor.milo")
    std::string classResourceFile;    // the class's own resource_file ("../ui/button.milo")
    std::string textTemplate;         // the first (texts (text "x.txt")) entry
    std::string mesh;                 // (mesh "...") of UIPicture / CheckBox types
    std::string texFile;              // UIPicture (tex_file "...")
};

// Component states (UIComponent: Normal 0, Focused 1, Disabled 2, Selecting 3).
enum class UiState { Normal = 0, Focused = 1, Disabled = 2, Selecting = 3 };

class UiStyles {
public:
    // Loads ui/gen/ui_objects.dtb (and the macros it includes). Throws on
    // a missing or broken file.
    void load(const GameData& data);
    bool loaded() const { return !root_.children.empty(); }

    // (<cls> (types (<type> ...))): `type` empty -> the class's init type
    // ((init (type default))) or "default".
    UiStyle style(const std::string& cls, const std::string& type) const;
    // A type's property (e.g. "focus_scale"), or nullptr.
    const ::DataNode* typeProperty(const std::string& cls, const std::string& type, const std::string& key) const;
    // A class's own property's value (e.g. BandButton's focus_anim_duration), or nullptr.
    const ::DataNode* classProperty(const std::string& cls, const std::string& key) const;

    // BandButton's text colour for a state (@ 0x82122920): the property
    // normal_color / focus_color / disabled_color / selecting_color of the
    // button's directory (its own property array, then its PanelDir type's
    // default), a packed colour r | g << 8 | b << 16.
    std::array<float, 3> buttonColor(const Object& panelDir, UiState state) const;
    // focus_scale of the button's directory (1 when unset).
    float focusScale(const Object& panelDir) const;

    // The archive path of a resource file named in ui_objects.dta
    // ("impactor.milo" -> "ui/gen/impactor.milo_ps2"): relative to ui/.
    static std::string resourcePath(const GameData& data, const std::string& rel);

private:
    const ::DataNode* classNode(const std::string& cls) const;
    const ::DataNode* dirTypeProperty(const Object& dir, const std::string& key) const;
    ::DataNode root_;
};

// {pack_color r g b} as GH2 stores it (0..1 each -> r | g << 8 | b << 16).
int packColor(float r, float g, float b);
std::array<float, 3> unpackColor(int packed);

// Reads a .dtb as plain data: #ifdef resolved (HX_XBOX, _SHIP), #include'd
// files' #defines and the file's own expanded where their names appear,
// {pack_color r g b} replaced by its int. `path` is a source-style or
// archive path ("ui/ui_objects.dta" or "ui/gen/ui_objects.dtb").
::DataNode loadPlainDta(const GameData& data, const std::string& path);

// The locale (ui/<lang>/gen/locale.dtb: (token "string") pairs; GH2 reads
// ../ui/eng/locale_milo.dta and ../ui/eng/locale.dta from config/gh2.dta,
// system_language picks eng fre deu ita esl).
class Locale {
public:
    // Loads the language's tables (locale_milo then locale). Throws if the
    // language has no locale.dtb.
    void load(const GameData& data, const std::string& language = "eng");
    const std::string& language() const { return language_; }
    // The localized string, or nullptr when the token is missing.
    const std::string* find(const std::string& token) const;
    // The localized string, or the token itself when missing.
    std::string text(const std::string& token) const;
    size_t size() const { return strings_.size(); }
    const std::map<std::string, std::string>& strings() const { return strings_; }
    static const std::vector<std::string>& languages();  // eng fre deu ita esl

private:
    std::string language_;
    std::map<std::string, std::string> strings_;
};

}  // namespace scene
