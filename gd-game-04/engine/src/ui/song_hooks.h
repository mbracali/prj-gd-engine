// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "data/qb.h"
#include "ui/fx2d.h"

class Canvas;
class Disc;
class FrontEnd;
class Mixer;
class SongScreen;
class TextureCache;

// GH3's own song scripts over the port's native game core (D10,
// docs/FINDINGS.md "The song path: scripts vs C"). The native judge
// (game/match, game/player) produces what GH3's C functions decide; this
// raises what those C functions raise, on the front end's VM:
//  - the gameplay events (hit_notes, hit_note, missed_note,
//    unnecessary_note, star_hit_note, star_sequence_bonus, star_miss_note,
//    whammy_on / _off, first_gem, firstnote_window_open), with the C's
//    parameters, to the handlers create_guitar_events registered
//    (event_spawner -> GuitarEvent_*) [C 0x8211EC00, 0x8211F328,
//    0x8211F7B0, 0x8211E4E8, 0x8211E8B8, 0x8211C9D0, 0x82121C58, 0x82120260];
//  - the scripts the C spawns (guitarevent_multiplier<n>xon / xoff,
//    show_star_power_ready, star_power_activate_and_drain, the crowd's
//    crowd_transition_sfx_* / change_crowd_looping_sfx) [C 0x8210BB40,
//    0x8210C800, 0x8210CB10, 0x8210C5B8, 0x821131E4, 0x8210F200];
//  - the fretbar iterators' GuitarEvent_PreFretbar / GuitarEvent_Fretbar,
//    the song's event arrays (<song>_scripts, _anim, _triggers, _cameras,
//    _lightshow, _crowd, _drums, _performance and the crowd / drums note
//    maps) at their times, the venue's GuitarEvent_EnterVenue [C scripts
//    gem_scroller, event_iterator, notemap_iterator, start_gem_scroller];
// with playerN_status kept current from the judge every frame, and the
// natives those scripts call (sound busses, 2D particle systems, the
// CFuncs whose work the native core already did).
class SongHooks {
public:
    SongHooks(FrontEnd& fe, Mixer& mixer, const Disc& disc, TextureCache& textures);
    ~SongHooks();
    // The song started (SongScreen::start succeeded).
    void begin(SongScreen& s);
    // One 60 Hz judge step (SongScreen::onStep).
    void step(SongScreen& s, double stepMs);
    // A rendered frame at song time `nowMs`: the proxy elements follow the
    // native highway (call before the front end's frame).
    void frame(SongScreen& s, double nowMs);
    // The 2D particle systems over the HUD and the scripts' elements.
    void draw(Canvas& c, double nowMs);
    // The song is over (`killed`: quit / restart from the pause menu).
    void end(bool killed);
    // The pause menu: the song's scripts wait (GH3 pauses its game scripts).
    void setPaused(bool paused);
    bool active() const { return active_; }

    struct Counters {
        std::map<std::string, uint64_t> events;    // gameplay events raised, by name
        std::map<std::string, uint64_t> spawned;   // scripts the C side spawned, by name
        std::map<std::string, uint64_t> arrays;    // event-array / note-map scripts run, by name
        std::map<std::string, uint64_t> sounds;    // SoundEvent events played during the song, by name
        uint64_t nativeSkipped = 0;                // event-array entries the native core already plays
        uint64_t bussEffects = 0, bussParams = 0, particles2d = 0, absorbed = 0;
    };
    const Counters& counters() const { return k_; }
    // The counters as one log line (song end).
    std::string summary() const;

private:
    void addNatives();
    void broadcast(const char* event, int player, qb::Struct data);
    uint32_t spawnTracked(uint32_t script, const qb::Struct& params, const char* counterName, std::map<std::string, uint64_t>& counter);
    void syncStatus(SongScreen& s, int p);
    void buildInputArray(SongScreen& s, int p);
    void loadSongScripts(SongScreen& s);
    void eventArrays(SongScreen& s, double nowMs);
    void createProxy(uint32_t id, uint32_t parent);
    void destroyProxies();
    bool elementPos(uint32_t id, float& x, float& y) const;
    qb::Struct playerParams(int p) const;

    FrontEnd& fe_;
    Mixer& mixer_;
    const Disc& disc_;
    TextureCache& tex_;
    SongScreen* song_ = nullptr;
    bool active_ = false, paused_ = false;
    int players_ = 1;
    double nowMs_ = 0;
    Fx2D fx_;
    Counters k_;
    std::set<uint32_t> runs_;  // the runs spawned for the song (and theirs)
    std::vector<uint32_t> proxies_;
    struct PerPlayer {
        int lastRun = 0;
        float lastStar = 0;
        size_t arraySize = 0;
        bool firstGem = false, firstWindow = false;
        uint32_t firstGemId = 0;
        int firstNote = -1;
        std::set<int> taps;  // notes of this step hit by a hammer-on / pull-off
    };
    std::array<PerPlayer, 2> pp_{};
    float lastCrowd_ = 1;
    std::array<int, 2> lastCards_{}, lastFired_{};
    std::array<std::array<int, 8>, 2> lastByAttack_{};
    size_t fretbar_ = 0, preFretbar_ = 0;
    struct Timed {
        double time = 0;
        uint32_t script = 0;
        qb::Struct params;
        const char* array = "";
    };
    std::vector<Timed> timed_;  // the event arrays and note maps, by time
    size_t timedAt_ = 0;
    std::vector<std::pair<float, uint32_t>> pushedBuss_;  // PushSoundBussParams' stack (the guitar buss's vol)
    uint64_t soundsAtStart_ = 0;
    // GuitarEvent_StarSequenceBonus's big_bolt<p><n> sprites: drawn here as
    // sys_Big_Bolt01's flip-book (the screen elements stay hidden).
    struct Bolt {
        uint32_t id = 0;
        double born = 0;
    };
    std::vector<Bolt> bolts_;
};
