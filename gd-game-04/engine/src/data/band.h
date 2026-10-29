// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "data/anim.h"
#include "data/qb.h"
#include "data/scene.h"
#include "data/texture.h"

class Disc;
class Globals;

// GH3's band members (docs/band.md): who plays a song (create_band,
// scripts\guitar\guitar_character.qb), what each one is made of (the
// Musician_Profiles entry, its musician_body / musician_instrument
// descriptions, guitar_band_profile(_data).qb) and the files that holds:
// the body and instrument skins (.skin skinned to the profile's skeleton in
// ZONES/global.pak), their textures, and the animation clips of the body's
// anims pak, perm_anims.pak and the song's pak. Rendering-free.
namespace band {

enum class Role : uint8_t { Guitarist, Bassist, Drummer, Vocalist };
const char* roleName(Role r);

// One member as create_band chose it.
struct Member {
    Role role = Role::Guitarist;
    uint32_t object = 0;           // the composite object's name: Guitarist, Bassist, Drummer, vocalist
    const qb::Struct* profile = nullptr;  // the Musician_Profiles entry
    std::string profileName;       // "Axel"
    uint32_t body = 0;             // musician_body desc_id
    uint32_t instrument = 0;       // musician_instrument desc_id (0: none)
    uint32_t skeleton = 0;         // the .ske key (Skeleton, or skeleton2 for outfit 2)
    int outfit = 1, style = 1;
    float scale = 1;               // the profile's uniform scale [C script create_guitarist]
};

// The band of a one-player song in `zone` (create_band with
// current_num_players = 1): the guitarist from player1_status (character_id,
// outfit, style, instrument_id), the drummer, the bassist (the song's
// `bassist` profile or 'bassist'), the vocalist (the song's `singer`: none,
// female, bret; the _alt microphone in z_wikker, z_budokan, z_hell). z_credits:
// the guitarist alone. Empty when $disable_band = 1.
std::vector<Member> chooseBand(const Globals& g, const std::string& zone, uint32_t songKey);

// A Musician_Profiles entry by name (case-insensitive), or null.
const qb::Struct* findProfile(const Globals& g, const std::string& name);
// The musician_body / musician_instrument entry with this desc_id, or null.
const qb::Struct* bodyDesc(const Globals& g, uint32_t descId);
const qb::Struct* instrumentDesc(const Globals& g, uint32_t descId);
// A desc's 'pak\models\...\x.pak' as a disc path (DATA/COMPRESSED/PAK/x.pak.xen) [C data: every desc's pak is there].
std::string pakPath(const std::string& qbPath);

// Every playable body: one Member per musician_body entry with a profile
// (every character, outfit and style; the bassist, drummer and singers),
// and one per musician_instrument entry on a member of the matching type
// (--chartest).
std::vector<Member> everyBody(const Globals& g);
std::vector<Member> everyInstrument(const Globals& g);

using ClipMap = std::unordered_map<uint32_t, std::shared_ptr<const anim::Clip>>;

// What every member shares: the skeletons (ZONES/global.pak) and
// perm_anims.pak's clips (hands, strums, the rig defaults).
class Library {
public:
    // Throws std::runtime_error.
    void load(const Disc& disc);
    const anim::Skeleton* skeleton(uint32_t key) const {
        auto it = skeletons_.find(key);
        return it == skeletons_.end() ? nullptr : &it->second;
    }
    const ClipMap& permClips() const { return perm_; }
    // A song pak's clips (the singer's lip-sync facial anims), cached.
    const ClipMap& songClips(const Disc& disc, const std::string& song);

private:
    std::unordered_map<uint32_t, anim::Skeleton> skeletons_;
    ClipMap perm_;
    std::unordered_map<std::string, ClipMap> songs_;
};

// One part (body or instrument): its skin and where it came from.
struct Part {
    std::string pak;
    scene::Scene scene;
};

// A loaded member.
struct Model {
    Member member;
    const anim::Skeleton* skeleton = nullptr;
    std::vector<Part> parts;  // body first, then the instrument
    std::unordered_map<uint32_t, gh3tex::Texture> textures;
    ClipMap clips;  // the body pak's and anims pak's, then perm_anims and the song's
    const anim::Clip* clip(uint32_t key) const {
        auto it = clips.find(key);
        return it == clips.end() ? nullptr : it->second.get();
    }
};

// Loads a member's paks. `song`: the song whose pak holds extra clips ("" none).
// Null and `error` on failure.
std::unique_ptr<Model> loadModel(const Disc& disc, const Globals& g, Library& lib, const Member& m, const std::string& song,
                                 std::string& error);

}  // namespace band
