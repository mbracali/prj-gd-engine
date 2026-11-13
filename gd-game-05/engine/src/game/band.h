// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "char/character.h"
#include "char/facefx.h"
#include "char/patch.h"
#include "scene/milo.h"
#include "scene/world_objects.h"
#include "scene/objects.h"
#include "script/script.h"

class World;
class GameData;
namespace scene {
class Waypoint;
}
class MidiParserSet;
class RbMidiParserSet;
struct MidiFile;
struct Song;
struct SongInfo;

// GH2's band on stage (docs/band.md): the venue's band slots filled with
// characters (game change_chars 0x822B7670), their scripts
// (char/char_objects.dta: BandCharacter guitarist, Character singer /
// keyboardist / bassist / drummer), GuitarCharacter's native side (Handle
// 0x822D2FE8, Poll 0x822D2B90, Enter 0x822D3B18), the MidiParsers that
// drive them (config/midi_parsers.dta: hands, fret position, the band
// tracks' text events) and the clip events ($dude hit_snare...).
//
// Slots [C 0x822B7670]: the <v>_chars directory's Character / BandCharacter
// objects other than the crowd's. guitarist<p> <- player p's character
// (char/<c>/og/<c>.milo); singer / bassist / drummer <- the song's
// (band ...) (songs.dta) else gh2.dta (default_band ...): a name with
// "sing" or "key" goes to the singer slot, "drum" to the drummer, "bass"
// to the bassist; with two players only the drummer is placed. Slots left
// empty are deleted.
class Band {
public:
    struct Config {
        int players = 1;                       // 1, or 2 (face-off / co-op)
        std::array<std::string, 2> character;  // outfit names (alterna1...); empty: the song's quickplay one
        std::array<std::string, 2> guitar;     // guitars.dta guitar (lespaul...); empty: quickplay's
        std::array<std::string, 2> skin;       // its skin (lp_cherry...); empty: the first
        std::array<bool, 2> bass{};            // the player plays the bass track (bass_strum)
        bool campaignFinal = false;            // the career's last song (win_finals, kWinGameWalkSpot)
        bool encore = false;                   // an encore follows (kEncoreWalkSpot, post-game idle)
        uint32_t seed = 0x42414e44u;           // the band's random numbers
        // Rock Band: the premade character per instrument (guitar, bass,
        // drum, vocals -> characters.dtb prefab name); empty: the port's pick.
        std::map<std::string, std::string> rbPrefab;
        // A player's own character per instrument (the creator's): gender,
        // skin colour index and the outfit (category -> piece).
        struct RbLook {
            std::string gender = "male";
            int skin = 0;
            uint32_t attitude = 0;  // the creator's kGenre* value (macros.dta: kGenreRocker 256 ...); 0: the premade's
            float weight = 0.5f, height = 0.5f;  // the creator's sliders (char/deform.h)
            std::map<std::string, std::string> outfit;
            std::map<std::string, chr::Patch> patches;  // the tattoos (location -> patch)
        };
        std::map<std::string, RbLook> rbLooks;
    };
    // Rock Band's premade characters whose names start with `instrument`
    // (guitar0, guitar1 ... in config/gen/characters.dtb).
    static std::vector<std::string> rbPrefabs(const GameData& data, const std::string& instrument);
    // A Rock Band character's outfit milos (char/<gender>/outfits/<cat>/gen/
    // <piece>.milo_xbox; the instrument's own: char/instruments/<cat>/gen/,
    // a drum kit the venue kind's <kind>_drums_<piece>) [C data], keyed in
    // order ("+00"...); `missing` gets the paths the disc lacks.
    static std::map<std::string, std::string> rbOutfitMilos(const GameData& data, const std::string& gender, const std::string& instrument,
                                                          const std::vector<std::pair<std::string, std::string>>& pieces,
                                                          const std::string& kind, std::vector<std::string>* missing = nullptr);
    // The skin tone on the character's placeholder skin materials (a
    // premade's skin_color_index, or `skinIndex`); see rb_band.cpp.
    static void rbSkin(const GameData& data, chr::Character& c, const DataNode* prefab, int skinIndex = -1);
    // The character's patches (location -> layers, char/patch.h) resolved
    // for its PatchRenderers (render/char_skin.h); renders its skins again.
    static void rbPatches(const GameData& data, chr::Character& c, const std::map<std::string, chr::Patch>& patches);
    // ui.dta's (patch ...) block: (sets ...), (stickers ...), (patch_layer ...), (tattoos ...); null without one.
    static const DataNode* rbPatchConfig(const GameData& data);
    // A sticker category's milo (archive path) and its stickers (Tex objects of type sticker, in order).
    static std::string rbStickerMilo(const GameData& data, const std::string& category);
    // The premade character each instrument got (Rock Band).
    const std::map<std::string, std::string>& rbPrefabUsed() const { return rbPrefabUsed_; }

    // Loads the band into `world`'s venue for `song` (its (band ...),
    // quickplay character and guitar, anim tempo). `objects` is the tree
    // holding char_objects.dta's types (the UI's (objects ...)); without it
    // the band loads char/char_objects.dta itself.
    Band(World& world, const GameData& data, const SongInfo& song, Config config, const DataNode* objects = nullptr);
    ~Band();
    Band(const Band&) = delete;
    Band& operator=(const Band&) = delete;

    // The song's MIDI for the parsers (call once before enter()).
    void loadSong(const MidiFile& midi, const Song* song);
    // The characters' enter (GuitarCharacter::Enter, then their dta enter):
    // after the world has started.
    void enter();
    // One frame at song time: parsers, GuitarCharacter polls, the
    // characters' drivers / servos / rigs. `camera`: for the LODs.
    void update(double seconds, double beats, const std::array<float, 3>* camera = nullptr);

    // The slots: guitarist0, guitarist1, singer, bassist, drummer.
    struct Member {
        std::string slot;                      // the venue object's name
        const scene::CharacterObj* object = nullptr;  // the slot in <v>_chars
        std::unique_ptr<chr::Character> character;
        std::string type;                      // guitarist / singer / keyboardist / bassist / drummer
        int player = -1;                       // guitarists
        std::string miloPath;                  // the character's milo (its proxies resolve from it)
        std::vector<std::string> pieces;       // RB: the outfit pieces' milos it was built from (the walks' checks)
        // The character's effect dirs (cheat_headflames, hand_flames_L / _R,
        // god_particles...): a RndDir proxying a shared milo (char/shared/og),
        // parented to a bone; script start / stop (char_objects.dta) [C data].
        // Drawn by BandView.
        struct Fx {
            const scene::DirObj* obj = nullptr;
            std::unique_ptr<scene::Milo> milo;  // the proxied effect
            double start = 0;                   // world seconds at the last start
            bool on = false;
        };
        std::map<std::string, std::unique_ptr<Fx>> fx;
        std::unique_ptr<ScriptObject> script;
        std::unique_ptr<facefx::LipSync> lip;
        std::unique_ptr<facefx::ExpressionScript> expr;
        std::string shotGroup;  // RB: the camera shot's clip group for this member (BandCamShot target anim)
        // Rock Band's CompositeCharacter state (RB 0x822E5908 / 0x822E4A80):
        // the play flags (+0x254: clip_flags kValidGuitar / kValidBass, the
        // attitude kGenre*, the play mode kPlay*), the clip group (+0x268),
        // the driver playing it (+0x264: main.drv, or the drummer's
        // main_add.drv while he plays), the mode the parser asked for
        // (cur_play) and whether the player plays (playing).
        uint32_t rbFlags = 0;
        std::string rbGroup;
        chr::ClipDriver* rbActive = nullptr;
        uint32_t rbCurPlay = 0;
        bool rbPlaying = true;
        std::vector<std::unique_ptr<ScriptObject>> rbParts;  // the parsers' sinks (rb_band.cpp)
        // A BandCamShot target's freeze (the intro's freeze frame): the pose
        // held from frozenAt (world seconds) on; return: the place to go back
        // to when the next shot starts.
        bool frozen = false;
        double frozenAt = 0;
        bool hasReturn = false;
        scene::Xfm returnTo;
    };
    // A BandCamShot's start for its targets (RB 0x822D8108) [C data]: the
    // members' clip groups and teleports (rbShotTargets), each target's
    // freeze (after freeze_delay seconds) and return; the members a shot
    // before froze or moved with return go back first.
    void rbShotStart(const scene::BandCamShot& shot, bool teleports = true);
    // Rock Band's BandCamShot start (RB 0x822D8108) [C data; I: no IK, props,
    // fast-forward or return]: each target's member plays its clip group
    // (stand, closeup, sit ...) and, with teleport, moves to the target's `to`.
    void rbShotTargets(const std::vector<std::pair<std::string, std::string>>& groups, const std::vector<std::pair<std::string, scene::Xfm>>& moves);
    const std::vector<std::unique_ptr<Member>>& members() const { return members_; }
    Member* member(const std::string& slot) const;
    chr::Character* character(const std::string& slot) const;
    // The slot a venue object is (nullptr: not a band member).
    Member* memberOf(const scene::Object* slotObject) const;

    // Director / script queries (GuitarCharacter 0x822D1180 / 0x822D13B0).
    bool playingStarpower(const std::string& slot) const;     // 0x80000 weight > 0 and 0x4000 weight <= 0
    float flagWeight(const std::string& slot, uint32_t flags) const;  // main.drv's clip weights
    bool actuallyWalking(const std::string& slot) const;      // CharWalk 0x8218A568
    // A band object's world transform: a member, or one of its bones /
    // objects (`subpart`, e.g. bone_spine2.mesh). False when not a member.
    bool objectXfm(const std::string& name, const std::string& subpart, scene::Xfm& out) const;

    // The player's rock meter rating (0..1) for "bad" (0x822C6DC0); the host sets it.
    std::function<float(int player)> rating;
    // Face-off (0x822C6DC0) [C]: the opponent's lead on this player and the
    // lead that counts as losing badly; unset outside the competitive modes.
    std::function<int(int player)> lead;
    std::function<float()> leadLimit;
    // The game's excitement (Game +0x338, set by 0x822CBAC8 from 0x822CAAB0:
    // 0 Boot .. 3 Great; the world's `excitement` message) for walk_delays,
    // interact_probs, the walk speed and the faces. Unset: {game get excitement}.
    std::function<int()> excitement;

    // Waypoint::Nearest (0x82195548): the waypoint with any of `mask`'s
    // flags nearest `p` (squared distance; ties: the first in GH2's global
    // list, World::waypoints()).
    const scene::Waypoint* nearestWaypoint(const std::array<float, 3>& p, uint32_t mask) const;
    // Walks started (walk task starts that planned a walk), for the tests.
    int walksStarted() const { return walksStarted_; }
    int interactsStarted() const { return interactsStarted_; }
    // How far (x y) from its target each finished walk ended.
    const std::vector<float>& walkArrivals() const { return walkArrivals_; }
    // Each clip a main driver starts: (beat, slot, clip) for the golden logs.
    std::function<void(double beat, const std::string& slot, const std::string& clip)> onClip;
    bool log = false;  // GD05_BANDLOG=1

    // Player p deployed Star Power (the game -> GuitarCharacter 0x822D25A8).
    void starPowerStart(int player);
    // The song's result, before game_over reaches the band ({$this player} won).
    void setResult(bool won) { won_ = won, over_ = true; }

    MidiParserSet* parsers() { return parsers_.get(); }
    // Messages that fired a member's EventTriggers (the drum kit), by event.
    const std::map<std::string, int>& triggerCounts() const { return triggerCounts_; }
    const std::vector<std::string>& problems() const { return problems_; }
    double beat() const { return beat_; }

    struct GuitarState;
    struct Task;
    class CharObj;
    class PartObj;
    class PlayerObj;
    friend class CharObj;
    friend class PartObj;

private:
    void assemble(const GameData& data, const SongInfo& song);
    bool assembleRb(const GameData& data, const SongInfo& song);  // game/rb_band.cpp
    void rbPoll(Member& m);
    // Rock Band's band at run time (game/rb_band.cpp).
    void rbLoadSong(const MidiFile& midi, const Song* song);
    void rbEnter(Member& m);
    void rbChangePlay(Member& m, uint32_t mode);                       // char_objects.dta change_play
    void rbChangeGroup(Member& m, const std::string& group, uint32_t flags, bool force);  // RB 0x822E5908
    void rbPlayGroup(Member& m);                                       // RB 0x822E4A80
    bool rbDriverClip(Member& m, const std::string& driver, scene::CharClipSamples* clip, float length);  // RB 0x82342168
    class RbPart;
    friend class RbPart;
    std::unique_ptr<RbMidiParserSet> rbParsers_;
    // GD05_BANDCHECK=1: per-frame measurements of the band (rb_band.cpp),
    // reported when the band goes. GD05_BAND_LEGACY=1: the band as before
    // wave G06 (no parsers, GH2's weight setters, no draw_only, no RB face),
    // to show the checks failing without the fixes.
    struct RbCheck;
    std::shared_ptr<RbCheck> rbCheck_;
    void rbCheckFrame();
    void rbCheckReport();
public:
    static bool rbLegacy();
private:
    void makeScripts(const DataNode* objects);
    void registerDir(Member& m, const scene::Directory& d);
    void setupFace(Member& m, scene::Object* servo);
    void pollFaces();
    void filterClips(Member& m);
    void guitarEnter(Member& m);
    void guitarPoll(Member& m, float dBeats);
    void gameOverPoll(Member& m);
    bool guitarMessage(Member& m, const std::string& msg, std::vector<Value>& args, Value& r);
    void setMode(Member& m, const std::string& group);
    void clearMode(Member& m, const std::string& group);
    void oneShot(Member& m, const std::string& group);
    void setGameOver(Member& m, const std::string& group);
    std::string currentGroup(Member& m);
    bool doingBadly(const Member& m);
    int lastLead_[2] = {0, 0};  // the player's +0x104
    void runClipScript(Member& m, const std::string& script);
    void fireTriggers(Member& m, const std::string& event);
    scene::Xfm slotXfm(const Member& m) const;
    // Walks (M9.6).
    chr::Spot spotOf(const scene::Waypoint* w) const;
    const scene::Waypoint* findWaypoint(uint32_t mask) const;           // 0x821954F8
    std::vector<const scene::Waypoint*> connections(const scene::Waypoint* w) const;
    std::vector<chr::Spot> searchPath(const scene::Waypoint* from, uint32_t target, uint32_t pass);  // 0x82196940
    float maxDistance(uint32_t from, uint32_t to) const;                 // 0x82195618
    int currentExcitement() const;
    double beatOfMs(double ms) const;
    float clipBeats(float frames, const scene::CharClipSamples* clip, const GuitarState& st) const;  // 0x822D1C88
    void walkSchedule(Member& m);                                        // 0x822D16B8
    void rollWalkDelays(GuitarState& st);                                // 0x822D12E8
    void addTask(Member& m, std::unique_ptr<Task> t);                    // 0x82198348
    void pollTasks(Member& m);                                           // 0x82197FB0
    void startTask(Member& m, Task& t);
    void taskPoll(Member& m, Task& t);
    void endTask(Member& m, Task& t);
    void walkStart(Member& m, Task& t);                                  // 0x822E4E78
    std::unique_ptr<Task> newWalk(Member& m, float start, float end, float priority, uint32_t flags);  // 0x822E4898
    std::unique_ptr<Task> newTimed(Member& m, int kind, scene::CharClipSamples* clip, float priority); // 0x822E4AF8 / 0x822E4C80
    float nextSoloEvent(const Member& m, float before) const;            // 0x822D1518

    World& world_;
    const GameData& data_;
    Config config_;
    std::string venueName_;
    std::string midiFile_;  // songs.dta midi_file (the singer's .voc)
    int animTempo_ = 1;
    std::vector<std::string> bandList_;
    std::unique_ptr<chr::ClipLibrary> clips_;
    std::vector<std::unique_ptr<Member>> members_;
    std::map<Member*, std::unique_ptr<GuitarState>> guitar_;
    std::unique_ptr<MidiParserSet> parsers_;
    std::vector<std::unique_ptr<ScriptObject>> parts_;  // scoped sub-objects (main.drv...)
    std::array<std::unique_ptr<ScriptObject>, 2> players_;  // {$this player}
    std::map<std::string, DataNode> scripts_;            // parsed clip scripts
    const DataNode* objects_ = nullptr;
    chr::Random rng_;
    std::vector<std::string> problems_;
    std::map<std::string, int> triggerCounts_;
    double seconds_ = 0, beat_ = 0;
    float dBeats_ = 0;
    const Song* song_ = nullptr;
    int walksStarted_ = 0, interactsStarted_ = 0;
    std::vector<float> walkArrivals_;
    bool first_ = true, entered_ = false;
    bool won_ = false, over_ = false;
    std::map<std::string, std::string> rbPrefabUsed_;
};
