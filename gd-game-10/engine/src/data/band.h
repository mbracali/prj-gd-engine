// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
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
class Pak;

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

    // gh4 (World Tour engine, docs/band.md "gh4"): the character is its
    // appearance's CAS parts (band_builder_create_band, create_band_member).
    bool gh4 = false;
    uint32_t part = 0;                     // guitar, rhythm, bass, drum, vocals (band_builder part)
    std::vector<std::string> meshes;       // the parts' 'models/...skin' paths (body first)
    const qb::Struct* animPart = nullptr;  // the anim struct's member: anim_set, strum_anims, finger_anims, ...
    std::vector<std::string> animPaks;     // disc paths of the paks with its clips
    bool female = false;                   // Band_IsFemale: is_female of the full body / FemalePhysique
    uint32_t animStruct = 0;               // the body's anim_struct (car_male_anim_struct, ...)
};

// The band of a one-player song in `zone` (create_band with
// current_num_players = 1): the guitarist from player1_status (character_id,
// outfit, style, instrument_id), the drummer, the bassist (the song's
// `bassist` profile or 'bassist'), the vocalist (the song's `singer`: none,
// female, bret; the _alt microphone in z_wikker, z_budokan, z_hell). z_credits:
// the guitarist alone. Empty when $disable_band = 1.
std::vector<Member> chooseBand(const Globals& g, const std::string& zone, uint32_t songKey);

// A member from a profile struct as create_band_member gets it (the
// Musician_Profiles entry with musician_body / musician_instrument desc_ids
// and outfit; the menus' composite objects, ui/menu_objects.h): the role
// from the object's name (else the profile's type). `profile` must outlive
// the member; ids not in the desc arrays become 0.
Member memberOf(const Globals& g, uint32_t object, const qb::Struct& profile);
// A Musician_Profiles entry by name (case-insensitive), or null.
const qb::Struct* findProfile(const Globals& g, const std::string& name);
// The musician_body / musician_instrument entry with this desc_id, or null.
const qb::Struct* bodyDesc(const Globals& g, uint32_t descId);
const qb::Struct* instrumentDesc(const Globals& g, uint32_t descId);
// A desc's 'pak\models\...\x.pak' as a disc path (DATA/COMPRESSED/PAK/x.pak.xen) [C data: every desc's pak is there].
std::string pakPath(const std::string& qbPath);

// gh4: band_builder_create_band's members for `song` (the song struct's
// band, player 1's character on its part), empty when there is no gh4
// profile data (then chooseBand is GH3's).
std::vector<Member> chooseBandGh4(const Globals& g, uint32_t songKey);
// gh4: a member from a musician profile (Preset_Musician_Profiles_*: name,
// appearance) playing `part` (guitar, rhythm, bass, drum, vocals; 0: none,
// the front end's). `songKey` 0: no song.
bool memberGh4(const Globals& g, const qb::Struct& profile, uint32_t object, uint32_t part, uint32_t songKey, Member& out);
// gh4: a musician profile by its name key (find_profile_by_id), or null.
const qb::Struct* findProfileGh4(const Globals& g, uint32_t id);
// get_anim_struct_member's song struct (<anim_struct>_<song name>, its _alt)
// looked up in the globals, else in `songFiles` (the song pak's QB, which a
// song loads into the globals only later: load_songqpak): the member's
// animPart and its pak. False when neither has it.
bool songAnimPart(const Globals& g, const std::vector<qb::File>& songFiles, uint32_t songKey, Member& m);

// Every playable body: one Member per musician_body entry with a profile
// (every character, outfit and style; the bassist, drummer and singers),
// and one per musician_instrument entry on a member of the matching type
// (--chartest).
std::vector<Member> everyBody(const Globals& g);
// gh4: every locked preset talent (an appearance with CAS_Full_Body) on each
// of its allowed_parts, as memberGh4 builds it for `songKey` (--chartest).
std::vector<Member> everyTalentGh4(const Globals& g, uint32_t songKey);
std::vector<Member> everyInstrument(const Globals& g);

using ClipMap = std::unordered_map<uint32_t, std::shared_ptr<const anim::Clip>>;

// What every member shares: the skeletons (ZONES/global.pak) and
// perm_anims.pak's clips (hands, strums, the rig defaults).
// The .ska clips of a pak, by short and full name key.
ClipMap readPakClips(const Pak& pak);

class Library {
public:
    // Throws std::runtime_error.
    void load(const Disc& disc);
    // One library per disc, loaded once and shared by the menus' characters,
    // every song's band, crowd and peds (it took 0.5 s each time: owner 1.7).
    // Thread-safe (a song's venue loads on a worker thread). Throws.
    static std::shared_ptr<Library> shared(const Disc& disc);
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
    std::mutex songsMutex_;  // songClips' cache (a shared library)
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

// Loads a member's paks (body 0 with an instrument: the instrument alone). `song`: the song whose pak holds extra clips ("" none).
// Null and `error` on failure.
std::unique_ptr<Model> loadModel(const Disc& disc, const Globals& g, Library& lib, const Member& m, const std::string& song,
                                 std::string& error);
// A copy of a model loadModel kept (the last 8), or null: nothing is read.
// Thread-safe, as loadModel is.
std::unique_ptr<Model> cachedModel(const Disc& disc, const Member& m, const std::string& song);

}  // namespace band
