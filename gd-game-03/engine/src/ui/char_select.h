// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ui/natives.h"

namespace scene {
class Directory;
class Mesh;
}  // namespace scene

// The band's look (docs/band.md "Characters, guitars and photos"): GH2's
// character and guitar data (config/gh2.dta (characters ...), guitars.dta,
// store.dta), the player configs' character / outfit / guitar / skin
// (PlayerConfig 0x822BDDC0), and the classes of the select screens:
// character_provider (0x822C1530), CharsysPanel (0x8228E8A8, char_single /
// char_multi), GuitarSelectPanel (0x82293B10) and GuitarDisplayPanel
// (0x822922B8).
namespace ui::chars {

struct Skin {
    std::string name, outfit, mat;  // guitars.dta (<skin> (outfit O) (mat M))
};
struct Guitar {
    std::string name, type;  // (type guitar | bass | grim | cheat)
    std::vector<Skin> skins;
};
struct Data {
    // (characters (punk (punk1) (punk2)) ...): each character's outfits.
    std::vector<std::pair<std::string, std::vector<std::string>>> characters;
    std::vector<Guitar> guitars;
    // store.dta's guitar / skin / character / outfit items (bought to unlock).
    std::vector<std::string> store;
};
const Data& data(UiManager& ui);

// Campaign::IsUnlocked (0x82285CC0, ui/campaign.h) for the band's items:
// the profile's unlock table (+0x38), where an item the store sells is
// locked until the band buys it. `known` (when given) is set to 1 when
// `sym` is a character, outfit, guitar or skin.
bool unlocked(UiManager& ui, const std::string& sym, int* known = nullptr);
// The character an outfit belongs to and its index (0x822BD5E0); "" when none.
std::string characterOf(UiManager& ui, const std::string& outfit, int* index = nullptr);
// A character's outfit `i` (0x822BD9A0, clamped to the last) / unlocked
// outfit count (character_provider num_outfits 0x822BF568).
std::string outfitAt(UiManager& ui, const std::string& character, int i);
int numOutfits(UiManager& ui, const std::string& character);
const Guitar* guitar(UiManager& ui, const std::string& name);
// What a guitar outfit shows on its own (GuitarDisplayPanel, the store's
// guitars): a Character directory drawn at its first LOD, as the band's
// characters are (render/band_view.cpp meshesToDraw): the LOD group's
// meshes in the group's order, the loose roots by draw order, never the
// shadow group (Character LODs 0x82156F50, shadow 0x82131680 [C]; the
// shadow group goes to the shadow buffer only [I]). Before wave B the
// panel drew every showing mesh in file order: flyingv_v2's strings.mesh
// (src_alpha, depth written, mostly clear) came before guitar.mesh and cut
// the neck under the strings out, and the shadow meshes drew too.
std::vector<const scene::Mesh*> displayMeshes(const scene::Directory& outfit);
// The guitars a player can pick (0x822C43B0): the grim character plays the
// scythe only; else the unlocked guitars of the player's part (guitar, or
// bass for kTrackBass), in guitars.dta order.
std::vector<std::string> guitars(UiManager& ui, const Session::PlayerCfg& p);
// A guitar's unlocked skins (0x822C6000).
std::vector<std::string> skins(UiManager& ui, const std::string& guitar);

// set_character (0x822BD8B8): the outfit and its character; `reset`
// re-checks the guitar against the new character's guitars.
void setCharacter(UiManager& ui, Session::PlayerCfg& p, const std::string& outfit, bool reset);
// set_guitar (0x822BDAA0): skin `default` (or empty) is the guitar's first.
void setGuitar(UiManager& ui, Session::PlayerCfg& p, const std::string& guitar, const std::string& skin, bool reset);
// 0x822BD490: a guitar the player does not have becomes the first one, a
// skin the guitar does not have its first.
void validate(UiManager& ui, Session::PlayerCfg& p);

// The PlayerConfig messages about the look (get_character ... get_total_num_skins).
bool playerConfigNative(UiManager& ui, Session::PlayerCfg& p, const std::string& msg, std::vector<Value>& args, Value& r);

// The endgame photo of player `p` (GamePanel 0x822CC730): one of the
// outfit's four pictures, ui/image/og/photo_<outfit><0..3>_keep.bmp,
// picked by RandomInt(0, 4).
std::string pickPhoto(UiManager& ui, const Session::PlayerCfg& p);

// CharsysPanel, GuitarSelectPanel, GuitarDisplayPanel; nullptr for other classes.
std::unique_ptr<ScriptObject> makeClass(UiManager& ui, const std::string& cls, const std::string& name);
// character_provider and the reset_hair function.
void installNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned);

}  // namespace ui::chars
