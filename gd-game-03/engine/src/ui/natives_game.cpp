// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
// The game's script objects: the game panel (GamePanel + GameConfig
// 0x822B8B18), gamecfg (modes.dta), the players' configs, player0 / player1
// / band (the results), beatmatch, and the track and HUD panels over the
// program's track and HUD (UiHost).
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>

#include "game/band_performer.h"
#include "game/player.h"
#include "game/song_db.h"
#include "input/input.h"
#include "scene/ui_styles.h"
#include "ui/campaign.h"
#include "ui/char_select.h"
#include "ui/natives.h"
#include "ui/ui_host.h"
#include "ui/ui_objects.h"

namespace ui {

namespace {

std::string nameOf(const Value& v) { return v.type == Value::Type::Object && v.object ? v.object->name() : v.str(); }
int intArg(const std::vector<Value>& a, size_t i, int def = 0) { return i < a.size() && a[i].isNumber() ? a[i].integerValue() : def; }

class GamePanelObj;
GamePanelObj* gGame = nullptr;

// {game get_player_config n}: the character, guitar, difficulty a player chose.
class PlayerConfigObj : public ScriptObject {
public:
    PlayerConfigObj(UiManager& ui, int index) : ScriptObject("player_config" + std::to_string(index), "PlayerConfig"), ui_(ui), i_(index) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        Session::PlayerCfg& p = ui_.session().players[size_t(i_)];
        r = Value::integer(0);
        if (msg == "set_difficulty") {
            // An int (kDifficulty*) or, from the two-player difficulty panels'
            // set_selection, the symbol easy / medium / hard / expert [I].
            int d = intArg(args, 0, 1);
            if (!args.empty() && !args[0].isNumber())
                for (int k = 0; k < 4; ++k)
                    if (nameOf(args[0]) == difficultySym(k)) d = k;
            return p.difficulty = std::clamp(d, 0, 3), true;
        }
        if (msg == "get_difficulty" || msg == "difficulty") return r = Value::integer(p.difficulty), true;
        if (chars::playerConfigNative(ui_, p, msg, args, r)) return true;
        if (msg == "set_track_type") return p.trackType = intArg(args, 0), true;
        if (msg == "get_track_type") return r = Value::integer(p.trackType), true;
        return false;
    }

private:
    UiManager& ui_;
    int i_;
};

// {game get_bank_loader} reset, {game video_provider} get_video_file.
class SmallObj : public ScriptObject {
public:
    using ScriptObject::ScriptObject;
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "reset") return true;
        if (msg == "get_video_file") {
            // store.dta's video items: video2 wg.wmv, video3 hmx.wmv (bonus_material_screen) [C data].
            const std::string v = args.empty() ? "" : args[0].str();
            r = Value::string(v == "video2" ? "wg.wmv" : v == "video3" ? "hmx.wmv" : "");
            return true;
        }
        return false;
    }
};

// The songs.dta entries (caption, venue) the game panel reads.
const DataNode* songEntry(UiManager& ui, const std::string& song) {
    static DataNode songs;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        try {
            songs = scene::loadPlainDta(ui.data(), "config/gen/songs.dtb");
        } catch (const std::exception&) {
        }
    }
    return songs.find(song);
}

}  // namespace

std::vector<std::string> songInstruments(UiManager& ui, const std::string& song) {
    std::vector<std::string> out;
    const DataNode* e = songEntry(ui, song);
    if (!e) return out;
    const DataNode* block = e->find(gameCfgValue(ui, "song_block").str());
    if (!block) block = e->find("song");
    const DataNode* tracks = block ? block->find("tracks") : nullptr;
    const DataNode* list = tracks && tracks->children.size() > 1 && tracks->children[1].isList() ? &tracks->children[1] : nullptr;
    if (list)
        for (const DataNode& t : list->children)
            if (t.isList() && !t.children.empty() && t.children[0].isText()) out.push_back(t.children[0].s);
    return out;
}

int trackTypeOf(const std::string& instrument) { return instrument == "bass" ? 0 : instrument == "rhythm" ? 1 : 2; }

namespace {
// get_track_sym (0x822BDDC0): the instrument player `p` plays.
Value trackSym(UiManager& ui, int p) {
    const Session::PlayerCfg& c = ui.session().players[size_t(p)];
    const std::vector<std::string> inst = songInstruments(ui, ui.session().song);
    if (c.track >= 0 && c.track < int(inst.size())) return Value::symbol(inst[size_t(c.track)]);
    return Value::symbol(c.trackType == 0 ? "bass" : c.trackType == 1 ? "rhythm" : "guitar");
}
}  // namespace

const char* difficultySym(int d) {
    static const char* const k[4] = {"easy", "medium", "hard", "expert"};
    return k[std::clamp(d, 0, 3)];
}

int songInfoIndex(UiManager& ui, const std::string& song) {
    const auto& songs = ui.host().songs();
    for (size_t i = 0; i < songs.size(); ++i)
        if (songs[i].id == song) return int(i);
    return -1;
}

Value gameCfgValue(UiManager& ui, const std::string& key) {
    Session& s = ui.session();
    if (auto it = s.cfgSet.find(key); it != s.cfgSet.end()) return it->second;
    if (key == "mode") return Value::symbol(s.mode);
    const DataNode* modes = ui.modes();
    for (const std::string& m : {s.mode, std::string("defaults")}) {
        const DataNode* mode = modes ? modes->find(m) : nullptr;
        const DataNode* v = mode ? mode->find(key) : nullptr;
        if (!v || v->children.size() < 2) continue;
        if (v->children.size() > 2) {  // a handler, e.g. multi_coop's (sp_engage ...)
            std::vector<Value> list;
            for (size_t k = 1; k < v->children.size(); ++k) list.push_back(ui.interp().toValue(v->children[k]));
            return Value::list(std::move(list));
        }
        const DataNode& n = v->children[1];
        return n.type == DataNode::Type::Command ? ui.interp().eval(n) : ui.interp().toValue(n);
    }
    return Value::integer(0);
}

namespace {

// section_provider (PS2 0x0011b5b8, its methods 0x0011b920..0x0011bba8)
// [C]: the practice sections of the song, row 0 the whole song
// (full_song), row n the song's section n - 1; a row before the start
// section (set_start_section n: rows from n + 1) and a section without a
// gem of the player's chart are inactive; section_after_last names the
// section after practice_sections' end one ('' after the last or the whole
// song). Loaded for the configured song, part and difficulty when a list
// takes it (load_practice_sections 0x0011b818).
class SectionProviderObj : public ListProvider {
public:
    explicit SectionProviderObj(UiManager& ui) : ListProvider("section_provider", "SectionProvider"), ui_(ui) {}
    void load() {
        const Session& s = ui_.session();
        const std::vector<std::string> inst = songInstruments(ui_, s.song);
        const int t = s.players[0].track;
        full_ = {};
        sections_ = ui_.host().songSections(s.song, t >= 0 && t < int(inst.size()) ? inst[size_t(t)] : "guitar", s.players[0].difficulty, full_);
        full_.name = "full_song";
        startRow_ = 0;
    }
    void initData() override { load(); }
    int numData() override { return int(sections_.size()) + 1; }
    const UiHost::Section& entry(int sec) const {  // 0x0011ba20: -1 the whole song
        return sec < 0 || sec >= int(sections_.size()) ? full_ : sections_[size_t(sec)];
    }
    std::string symbol(int row) override { return entry(row - 1).name; }
    bool active(int row) override {  // 0x0011bb68
        if (row < startRow_) return false;
        return row <= 0 || entry(row - 1).hasGems;
    }
    void rows(std::vector<gfx::UiView::ListRow>& out, int selected, int& selectedRow) override {
        out.assign(size_t(numData()), {});
        for (int i = 0; i < numData(); ++i) {
            out[size_t(i)].texts["list.txt"] = ui_.localize(symbol(i));
            out[size_t(i)].disabled = !active(i);
        }
        selectedRow = selected;
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "num_data") return r = Value::integer(numData()), true;
        if (msg == "get_symbol") return r = Value::symbol(symbol(intArg(args, 0))), true;
        if (msg == "is_active") return r = Value::integer(active(intArg(args, 0)) ? 1 : 0), true;
        if (msg == "set_start_section") return startRow_ = intArg(args, 0) + 1, true;
        if (msg == "section_after_last") {
            const Value ps = gameCfgValue(ui_, "practice_sections");
            const int last = ps.type == Value::Type::Array && ps.array && ps.array->size() > 1 ? in.evalValue((*ps.array)[1]).integerValue() : -1;
            return r = Value::symbol(last != -1 && last != int(sections_.size()) - 1 ? entry(last + 1).name : std::string()), true;
        }
        return false;
    }
    int startTick(int sec) const { return entry(sec).startTick; }

private:
    UiManager& ui_;
    std::vector<UiHost::Section> sections_;
    UiHost::Section full_;
    int startRow_ = 0;  // +0x54
};

// gamecfg practice_sections (n): the chosen section of index n (0 the
// start, 1 the end), -1 the whole song.
int practiceSection(UiManager& ui, int n) {
    const Value ps = gameCfgValue(ui, "practice_sections");
    if (ps.type != Value::Type::Array || !ps.array || int(ps.array->size()) <= n) return -1;
    return ui.interp().evalValue((*ps.array)[size_t(n)]).integerValue();
}

// GamePanel `game`: GH2's GamePanel with GameConfig's methods. Entering it
// loads the configured song through the host; its sinks get the game
// events (UiManager::gameEvent).
class GamePanelObj : public PanelObj, public SinkHolder {
public:
    GamePanelObj(UiManager& ui, const std::string& name) : PanelObj(ui, name, "GamePanel") {
        gGame = this;
        configs_[0] = std::make_unique<PlayerConfigObj>(ui, 0);
        configs_[1] = std::make_unique<PlayerConfigObj>(ui, 1);
        bank_ = std::make_unique<SmallObj>("bank_loader", "BankLoader");
        video_ = std::make_unique<SmallObj>("video_provider", "VideoProvider");
    }
    ~GamePanelObj() override {
        stopHolding();
        if (gGame == this) gGame = nullptr;
    }

    std::string fileName() override { return ""; }

    // GamePanel enter: the song loads (unless load_song loaded it already),
    // then the panel enters and the intro waits for the screen.
    void enter() override {
        if (!preloaded_) loadGame();
        preloaded_ = false;
        pendingBegin_ = true;
        PanelObj::enter();
    }

    void loadGame() {
        Session& s = ui.session();
        props["won"] = Value::integer(0);
        props["game_over"] = Value::integer(0);
        s.gameOver = false;
        UiHost::GameSetup setup;
        setup.song = s.song;
        setup.difficulty = s.players[0].difficulty;
        setup.autoplay = gameCfgValue(ui, "force_autoplay").truthy() || SDL_getenv("GD03_AUTOPLAY") != nullptr;
        const Career::Options& o = ui.career().options;
        setup.lefty = o.lefty[0];
        setup.syncOffsetMs = o.syncOffsetMs;
        setup.bandVolume = volume(o.bandVolumeIdx), setup.guitarVolume = volume(o.guitarVolumeIdx);
        setup.fastIntro = getProp(ui.interp(), "fast_intro").truthy();
        // The band's look: the players' configs (0x822B7670 change_chars).
        for (size_t p = 0; p < 2; ++p) {
            Session::PlayerCfg& pc = s.players[p];
            chars::validate(ui, pc);
            setup.outfit[p] = pc.outfit, setup.guitar[p] = pc.guitar, setup.skin[p] = pc.skin;
        }
        // The endgame photo (0x822CC730): with a win_preload_panel (quick
        // play, career), one picture per player of the game.
        s.winnerPhotos.clear();
        if (!gameCfgValue(ui, "win_preload_panel").str().empty())
            for (int p = 0; p < std::max(1, gameCfgValue(ui, "num_players").integerValue()) && p < 2; ++p)
                s.winnerPhotos.push_back(chars::pickPhoto(ui, s.players[size_t(p)]));
        // The encore (0x822CA6E0 / 0x822CA5D0): GamePanel's script sets
        // [encore_unlock_check] as it enters; the native reads the same.
        const bool career = gameCfgValue(ui, "mode").str() == "career";
        encoreCheck_ = gameCfgValue(ui, "encore_fx").truthy() &&
                       (getProp(ui.interp(), "force_encore").truthy() ||
                        (career && ui.interp().send("campaign", "encore_unlock_potential", {}).truthy()));
        setup.encoreCheck = encoreCheck_;
        setup.encoreFx = wantEncoreFx();
        setup.campaignFinal = career && ui.interp().send("campaign", "final_song", {}).truthy();
        // The mode, its players, their difficulties, parts, lefty and pads.
        setup.mode = gameCfgValue(ui, "mode").str();
        setup.numPlayers = std::clamp(gameCfgValue(ui, "num_players").integerValue(), 1, 2);
        const std::vector<std::string> inst = songInstruments(ui, s.song);
        for (size_t p = 0; p < 2; ++p) {
            setup.difficulties[p] = s.players[p].difficulty;
            const int t = s.players[p].track;
            setup.parts[p] = t >= 0 && t < int(inst.size()) ? inst[size_t(t)] : std::string("guitar");
            setup.leftys[p] = o.lefty[p];
            setup.pads[p] = s.players[p].padnum;
        }
        // The audio block (modes.dta song_block), skip_intro and practice's
        // speed and sections (PS2 0x00106840: practice_speeds[practice_speed]
        // / 100 of the song, else sound.dta's default_speeds) [C].
        setup.songBlock = gameCfgValue(ui, "song_block").str();
        setup.skipIntro = gameCfgValue(ui, "skip_intro").truthy();
        if (setup.mode == "practice") {
            const int i = songInfoIndex(ui, s.song);
            std::vector<int> speeds = i >= 0 ? ui.host().songs()[size_t(i)].practiceSpeeds : std::vector<int>{};
            if (speeds.size() < 4) speeds = {100, 90, 80, 60};  // config/sound.dta (practice_mode (default_speeds ...)) [C data]
            const int k = std::clamp(gameCfgValue(ui, "practice_speed").integerValue(), 0, 3);
            setup.practiceSpeed = float(speeds[size_t(k)]) / 100.0f;
            if (auto* sp = dynamic_cast<SectionProviderObj*>(ui.interp().findObject("section_provider"))) {
                if (sp->numData() <= 1) sp->load();
                setup.practiceStartTick = sp->entry(practiceSection(ui, 0)).startTick;
                setup.practiceEndTick = sp->entry(practiceSection(ui, 1)).endTick;
            }
        }
        skipIntro_ = setup.skipIntro;
        props["fast_intro"] = Value::integer(0);
        clearPlayerSinks();
        resetFaders();  // music_fader back at 0 dB with the music (the mixer's volume at load)
        std::string error;
        if (!ui.host().gameLoad(setup, error)) SDL_Log("game: %s", error.c_str());
    }
    void clearPlayerSinks();

    void exit() override {
        PanelObj::exit();
        sinks_.clear();
        pendingBegin_ = false;
        ui.host().gameEnd();
    }

    void poll(double dtMs) override {
        PanelObj::poll(dtMs);
        // After every game panel has entered (their sinks are in): the intro.
        if (pendingBegin_ && ui.current() && !ui.inTransition()) {
            pendingBegin_ = false;
            if (ui.host().gameLoaded()) {
                ui.host().gameBegin();
                // skip_intro (practice): intro_skip instead of the intro [I: the
                // handlers practice_panel and game.dta have for it].
                if (skipIntro_) ui.gameEvent("intro_skip", {});
                else ui.gameEvent("intro_start_msg", {Value::integer(0), Value::integer(wantEncoreFx() ? 1 : 0)});  // 0x822CCA60
            }
        }
    }

    static float volume(int idx) { return float(std::clamp(idx, 0, 11)) / 11.0f; }

    // want_encore_fx (0x822CA5D0): the mode's encore_fx, and force_encore or
    // an encore whose venue's encore is unlocked.
    bool wantEncoreFx() {
        if (!gameCfgValue(ui, "encore_fx").truthy()) return false;
        return getProp(ui.interp(), "force_encore").truthy() || campaign::wantEncoreFx(ui);
    }
    bool encoreCheck_ = false, skipIntro_ = false;

    void dispatch(const std::string& msg, std::vector<Value> args) {
        if (msg == "game_won_msg") props["won"] = Value::integer(1);
        if (msg == "game_won_msg" || msg == "game_lost") props["game_over"] = Value::integer(1), ui.session().gameOver = true;
        ui.callScript(this, msg, args);
        // The sinks: (object, messages) pairs; no messages means all of them.
        toSinks(msg, args);
    }

    // To the sinks that want `msg`, from a copy: a sink a handler removes or
    // deletes on the way is skipped (still in sinks_).
    void toSinks(const std::string& msg, const std::vector<Value>& args) {
        const auto sinks = sinks_;
        for (const auto& [o, msgs] : sinks) {
            if (!msgs.empty() && !msgs.count(msg)) continue;
            if (std::none_of(sinks_.begin(), sinks_.end(), [o = o](const auto& p) { return p.first == o; })) continue;
            if (auto* p = dynamic_cast<PanelObj*>(o); p && !p->up) continue;
            // As MsgSource's export: the sink's handler, then (no handler, or
            // it returned kDataUnhandled) its native one, which is the
            // world's (its crowd: CrowdAudio is a game sink, intro_start_msg
            // / game_won_msg / game_lost, 0x822DF9D8) [C].
            ui.interp().send(o, msg, args, false);
        }
    }

    // A sink deleted while held: a world loaded outside a game (Enhanced
    // Options' backdrop adds itself on enter, `{game add_sink $this}`, and
    // is freed when the screen goes) no longer gets the next game's
    // intro_start_msg (the career crash, bug-fix 5).
    void sinkDestroyed(ScriptObject* o) override { removeSink(o); }

    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        Session& s = ui.session();
        UiHost& host = ui.host();
        r = Value::integer(0);
        const auto argStr = [&](size_t i) { return i < args.size() ? nameOf(args[i]) : std::string(); };
        // The players' parts (game.dta / world_objects.dta): guitar unless co-op
        // gives player 1 the bass [I: the part pick is M10's].
        if (msg == "get_track_sym") return r = trackSym(ui, std::clamp(intArg(args, 0), 0, 1)), true;
        if (msg == "get_track_type") return r = Value::integer(s.players[size_t(std::clamp(intArg(args, 0), 0, 1))].trackType), true;
        if (msg == "get_song") return r = Value::symbol(s.song), true;
        if (msg == "set_song") return s.song = argStr(0), true;
        if (msg == "get_song_index") return r = Value::integer(s.songIndex), true;
        if (msg == "set_song_index") {
            s.songIndex = intArg(args, 0, -1);
            if (auto* p = dynamic_cast<ListProvider*>(in.findObject("song_provider")); p && s.songIndex >= 0 && s.songIndex < p->numData())
                s.song = p->symbol(s.songIndex);
            return true;
        }
        if (msg == "set_need_intro" || msg == "retain_world_bank") return true;
        // The career's venue and song (0x822B42C8 / 0x822B89B8, ui/campaign.h);
        // the song's index in song_provider's list.
        if (msg == "set_career_venue") return campaign::setCareerVenue(ui), true;
        if (msg == "set_career_song") {
            campaign::setCareerSong(ui);
            if (auto* p = dynamic_cast<ListProvider*>(in.findObject("song_provider"))) s.songIndex = p->indexOf(s.song);
            return true;
        }
        if (msg == "get_venue_index") return r = Value::integer(campaign::venueIndex(ui, s.venue)), true;
        if (msg == "set_quickplay") {
            // The song's quickplay venue (songs.dta (quickplay (venue x))).
            const int i = songInfoIndex(ui, s.song);
            if (i >= 0) s.venue = host.songs()[size_t(i)].venue;
            // 0x822B86D0: player 0 plays the song's (quickplay (character_outfit X)
            // (guitar G) [(guitar_skin S)]), skin `default` when none.
            const DataNode* e = songEntry(ui, s.song);
            const DataNode* q = e ? e->find("quickplay") : nullptr;
            const DataNode* co = q ? q->find("character_outfit") : nullptr;
            const DataNode* g = q ? q->find("guitar") : nullptr;
            const DataNode* sk = q ? q->find("guitar_skin") : nullptr;
            if (co && co->children.size() > 1) chars::setCharacter(ui, s.players[0], co->textAt(1), false);
            if (g && g->children.size() > 1) chars::setGuitar(ui, s.players[0], g->textAt(1), sk && sk->children.size() > 1 ? sk->textAt(1) : "default", false);
            return true;
        }
        if (msg == "get_venue") return r = Value::symbol(s.venue), true;
        if (msg == "set_venue") return s.venue = argStr(0), true;
        if (msg == "get_player_config") return r = Value::obj(configs_[size_t(std::clamp(intArg(args, 0), 0, 1))].get()), true;
        if (msg == "get_difficulty") return r = Value::integer(s.players[size_t(std::clamp(intArg(args, 0), 0, 1))].difficulty), true;
        if (msg == "get_difficulty_sym")
            return r = Value::symbol(difficultySym(s.players[size_t(std::clamp(intArg(args, 0), 0, 1))].difficulty)), true;
        // GameConfig passes the look to player 0's config (0x822BDDC0).
        if (chars::playerConfigNative(ui, s.players[0], msg, args, r)) return true;
        // is_multiple_controllers (0x822B4248): more than one pad connected.
        // The port's keyboard plays two with its split layout (P2 on H J K L
        // ;, N / M, B, O: input/input.h), so two players are always
        // possible [I: the port's choice].
        if (msg == "is_multiple_controllers") return r = Value::integer(1), true;
        // multiplayer: num_players > 1; multiplayer_vs (0x822B37F8): and the mode's competitive [C].
        if (msg == "multiplayer" || msg == "multiplayer_vs")
            return r = Value::integer(gameCfgValue(ui, "num_players").integerValue() > 1 &&
                                      (msg == "multiplayer" || gameCfgValue(ui, "competitive").truthy())), true;
        if (msg == "num_players" || msg == "get_num_players") return r = Value::integer(std::max(1, gameCfgValue(ui, "num_players").integerValue())), true;
        if (msg == "get_controller") {
            // guitar when a guitar plays (main_panel then offers Training first), else joypad.
            bool guitar = false;
            for (const Input::Device& d : ui.input().devices())
                if (d.id.rfind("pad:", 0) == 0 && ui.input().gh2Type(d.id).find("joypad") == std::string::npos) guitar = true;
            return r = Value::symbol(guitar ? "guitar" : "joypad"), true;
        }
        if (msg == "is_missing_controller") return true;  // keyboards and controllers come and go freely in the port
        // PS2: true greys out MULTIPLAYER when pad 2 is missing; the port
        // assigns players in GAMEPADS, so it is never missing (DECISIONS D4).
        if (msg == "is_missing_multi_controller") return true;
        if (msg == "set_paused") {
            const bool p = !args.empty() && args[0].truthy();
            paused = p;
            host.gameSetPaused(p);
            // GamePanel's SetPaused (0x00107b90) with a song loaded sets the
            // beatmatch's pause too (0x00122310, the flag beatmatch
            // set_paused sets, 0x001231c8) [C]: tutorial_32's {game
            // set_paused FALSE} starts the song the lesson before paused.
            if (host.gameLoaded()) host.setMusicPaused(p);
            ui.gameEvent("on_set_paused", {Value::integer(p ? 1 : 0)});
            return true;
        }
        if (msg == "paused") return r = Value::integer(host.gamePaused() ? 1 : 0), true;
        if (msg == "set_realtime") return true;
        if (msg == "song_duration_sec") return r = Value::real(host.songDurationMs() / 1000.0f), true;
        if (msg == "get_song_text" || msg == "get_song_artist_text") {
            const int i = songInfoIndex(ui, s.song);
            if (i >= 0) r = Value::string(msg == "get_song_text" ? host.songs()[size_t(i)].name : host.songs()[size_t(i)].artist);
            else r = Value::string(s.song);
            return true;
        }
        if (msg == "get_song_caption") {
            const DataNode* e = songEntry(ui, s.song);
            const DataNode* c = e ? e->find("caption") : nullptr;
            r = Value::string(ui.localize(c && c->children.size() > 1 && c->children[1].isText() ? c->children[1].s : "mtv_made_famous"));
            return true;
        }
        if (msg == "want_encore_fx") return r = Value::integer(wantEncoreFx() ? 1 : 0), true;
        if (msg == "set_tutorial_running") return s.tutorialRunning = !args.empty() && args[0].truthy(), true;
        if (msg == "is_tutorial_running") return r = Value::integer(s.tutorialRunning ? 1 : 0), true;
        if (msg == "get_bank_loader") return r = Value::obj(bank_.get()), true;
        if (msg == "video_provider") return r = Value::obj(video_.get()), true;
        if (msg == "add_sink" || msg == "add_sinks") {
            // The sink as an object ({game add_sink $this}) or by name
            // (tutorial_24's {game add_sink tut_24_callback star_power_start}).
            if (!args.empty() && args[0].type != Value::Type::Object)
                if (ScriptObject* o = in.findObject(args[0].str())) args[0] = Value::obj(o);
            if (args.empty() || args[0].type != Value::Type::Object) return true;
            // A second add_sinks adds messages to the same sink (hud's peak_on / peak_off).
            std::set<std::string> msgs;
            for (size_t k = 1; k < args.size(); ++k) msgs.insert(args[k].str());
            for (auto& [o, m] : sinks_)
                if (o == args[0].object) {
                    if (msgs.empty() || m.empty()) m.clear();
                    else m.insert(msgs.begin(), msgs.end());
                    return true;
                }
            sinks_.push_back({args[0].object, msgs});
            return true;
        }
        if (msg == "remove_sink") {
            if (!args.empty() && args[0].type != Value::Type::Object)
                if (ScriptObject* o = in.findObject(args[0].str())) args[0] = Value::obj(o);
            if (!args.empty() && args[0].type == Value::Type::Object) removeSink(args[0].object);
            return true;
        }
        if (msg == "foreach_player_matcher") return true;
        if (msg == "foreach_player") return r = ui.foreachPlayer(args), true;  // GameConfig's, on the game (game.dta beat)
        // player.dta's {handle (game star_power_start / stop $player_num)}:
        // the HUD and the world get Star Power from the program (HudView,
        // WorldDriver), so it goes on only to the sinks that named it
        // (tutorial_24's {game add_sink tut_24_callback star_power_start})
        // [C: MsgSource 0x002c01f8; I: the all-message sinks left out].
        if (msg == "star_power_start" || msg == "star_power_stop") {
            const auto sinks = sinks_;
            for (const auto& [o, msgs] : sinks)
                if (msgs.count(msg) && std::any_of(sinks_.begin(), sinks_.end(), [o = o](const auto& p) { return p.first == o; }))
                    in.send(o, msg, args, false);
            return true;
        }
        // practice_section_provider (0x001275f0): GameConfig's section provider
        // (+0x20c); load_practice_sections (0x0011b818) loads it again.
        if (msg == "practice_section_provider" || msg == "load_practice_sections") {
            auto* sp = dynamic_cast<SectionProviderObj*>(in.findObject("section_provider"));
            if (sp && msg == "load_practice_sections") sp->load();
            return r = Value::obj(sp), true;
        }
        // load_song <song> (0x00109210 -> 0x00107f40): the game config's
        // song set, then the song loaded; the tutorials' switch_to_song
        // (game exit, load_song, wait for loaded, game enter) [C].
        if (msg == "load_song") {
            if (!args.empty()) s.song = argStr(0);
            loadGame();
            preloaded_ = true;
            return true;
        }
        if (msg == "loaded") return r = Value::integer(host.gameLoaded() ? 1 : 0), true;
        if (PanelObj::handleNative(in, msg, args, r)) return true;
        // What the game's script and natives leave goes on to the sinks:
        // game.dta's {handle ($this slide_meter_in)} reaches hud_panel,
        // {handle ($this intro_end)} the world [I: GH2's MsgSource
        // forwarding, seen in those scripts]. Only messages a sink names, and
        // intro_end, so that a query nobody answers stays unhandled (the
        // world takes every message).
        bool any = msg == "intro_end";
        for (const auto& [o, msgs] : sinks_) any = any || msgs.count(msg);
        if (!any) return false;
        toSinks(msg, args);
        return true;
    }

    void removeSink(ScriptObject* o) {
        sinks_.erase(std::remove_if(sinks_.begin(), sinks_.end(), [&](const auto& p) { return p.first == o; }), sinks_.end());
    }

private:
    std::array<std::unique_ptr<PlayerConfigObj>, 2> configs_;
    std::unique_ptr<SmallObj> bank_, video_;
    std::vector<std::pair<ScriptObject*, std::set<std::string>>> sinks_;
    bool pendingBegin_ = false;
    bool preloaded_ = false;  // load_song loaded the song: enter does not load it again
};

// TrackPanel / HudPanel: GH2's track and HUD panels, drawn by the program
// (render/track_view.h, render/hud_view.h) and steered by the scripts.
class HostPanelObj : public PanelObj {
public:
    HostPanelObj(UiManager& ui, const std::string& name, const std::string& cls, bool track) : PanelObj(ui, name, cls), track_(track) {
        anyComponent_ = true;  // meters_slide_in.view and friends: the HUD view animates them itself
    }
    std::string fileName() override { return ""; }  // the program loads ../track/*.milo and ../hud/*.milo itself
    bool drawsSomething() const override { return true; }
    void draw() override {
        if (!showing || !ui.host().gameLoaded()) return;
        if (track_) ui.host().drawTrack();
        else ui.host().drawHud();
    }
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        static const std::set<std::string> kTrack = {"reset", "set_frame", "animate", "explode", "pop_smasher", "set_smasher_glowing",
                                                     "sync_to_time", "intro_skip", "set_track_in", "set_track_out"};
        static const std::set<std::string> kHud = {"reset", "set_meters_showing", "set_win_showing", "show_message", "on_game_over",
                                                   "find", "intro_skip", "set_paused"};
        if (msg == "set_showing") {
            showing = !args.empty() && args[0].truthy();
            if (track_) ui.host().trackCommand(msg, args);
            else ui.host().hudCommand(msg, args);
            return true;
        }
        if (track_ ? kTrack.count(msg) : kHud.count(msg)) {
            if (msg == "set_paused") paused = !args.empty() && args[0].truthy();
            if (track_ && msg == "animate") {
                // The options evaluated ((range 2000 2025) (period 0.4): the barf cheat's beat bump).
                std::vector<Value> a;
                for (const Value& raw : args) {
                    Value v = in.evalValue(raw);
                    if (v.type == Value::Type::Array && v.array) {
                        std::vector<Value> l;
                        for (const Value& x : *v.array) l.push_back(in.evalValue(x));
                        v = Value::list(std::move(l));
                    }
                    a.push_back(v);
                }
                ui.host().trackCommand(msg, a);
                return true;
            }
            if (track_) ui.host().trackCommand(msg, args);
            else ui.host().hudCommand(msg, args);
            // HudPanel's reset ends with the script's on_reset (0x8268E440):
            // the HUD's showing (show_hud, the Performance Mode cheat) and meters.
            if (!track_ && msg == "reset") ui.callScript(this, "on_reset");
            return true;
        }
        return PanelObj::handleNative(in, msg, args, r);
    }

private:
    bool track_;
};

// Panels with nothing to draw (MidiLoaderPanel, TrackMaskPanel).
class EmptyPanelObj : public PanelObj {
public:
    using PanelObj::PanelObj;
    std::string fileName() override { return ""; }
};

// A player's track ({player0 track}: its GHTrackDir, 0x82695D48): its
// extend anim ({{$this track} extend_anim} animate (range 2000 2025): the
// Star Power bump, player.dta start_using) goes to the program's track view;
// set_bonus_gems and on_miss_phrase the view does itself on the players'
// events (render/track_view.h) [C].
class TrackDirObj : public ScriptObject {
public:
    TrackDirObj(UiManager& ui, int player) : ScriptObject("track_dir" + std::to_string(player), "GHTrackDir"), ui_(ui), p_(player) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "extend_anim") return r = Value::obj(this), true;
        if (msg == "animate") {
            std::vector<Value> a{Value::integer(p_)};
            for (const Value& raw : args) {
                Value v = in.evalValue(raw);
                if (v.type == Value::Type::Array && v.array) {
                    std::vector<Value> l;
                    for (const Value& x : *v.array) l.push_back(in.evalValue(x));
                    v = Value::list(std::move(l));
                }
                a.push_back(v);
            }
            ui_.host().trackCommand("player_animate", a);
            return true;
        }
        return msg == "set_bonus_gems" || msg == "on_miss_phrase";
    }

private:
    UiManager& ui_;
    int p_;
};

// player0 / player1 / band: a player's results for the end screens; the
// co-op band (index -1, host gameBand: its score, stars 0x822CE5D8,
// streak and meter), player 0 outside co-op.
class PlayerObj : public ScriptObject {
public:
    PlayerObj(UiManager& ui, const std::string& name, int index)
        : ScriptObject(name, "Player"), ui_(ui), i_(index), track_(ui, std::max(0, index)) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        // player.dta's handlers: {$this track}, {$this player_matcher} (0x822C7060).
        if (msg == "track") return r = Value::obj(&track_), true;
        if (msg == "player_matcher") return r = Value::obj(in.findObject("player_matcher" + std::to_string(std::max(0, i_)))), true;
        if (i_ < 0)
            if (const BandPerformer* b = ui_.host().gameBand()) {
                r = Value::integer(0);
                if (msg == "score") return r = Value::integer(b->score()), true;
                if (msg == "num_stars") return r = Value::integer(b->stars()), true;
                if (msg == "star_rating") {
                    const int n = std::clamp(b->stars(), 0, 5);
                    return r = Value::string(std::string(size_t(n), '*') + std::string(size_t(5 - n), '_')), true;
                }
                if (msg == "longest_streak") return r = Value::integer(b->longestStreak()), true;
                if (msg == "current_streak") return r = Value::integer(b->streak()), true;
                if (msg == "base_multiplier") return r = Value::integer(b->baseMultiplier()), true;
                if (msg == "crowd_rating") return r = Value::real(b->rockMeter()), true;
                if (msg == "failed") return r = Value::integer(b->failed() ? 1 : 0), true;
                if (msg == "win") return r = Value::integer(b->failed() ? 0 : 1), true;
            }
        return playerNative(in, msg, args, r);
    }

private:
    bool playerNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) {
        const int index = std::max(0, i_);
        const Player* p = ui_.host().gamePlayer(index);
        r = Value::integer(0);
        const int gems = ui_.host().gameCounter(index, "gems");
        if (msg == "score") return r = Value::integer(p ? p->score() : 0), true;
        if (msg == "percent_hit") return r = Value::integer(p && gems > 0 ? p->notesHit() * 100 / gems : 0), true;
        if (msg == "num_stars") return r = Value::integer(p ? p->stars() : 0), true;
        if (msg == "star_rating") {
            const int n = std::clamp(p ? p->stars() : 0, 0, 5);
            return r = Value::string(std::string(size_t(n), '*') + std::string(size_t(5 - n), '_')), true;
        }
        if (msg == "longest_streak") return r = Value::integer(p ? p->longestStreak() : 0), true;
        if (msg == "current_streak") return r = Value::integer(p ? p->streak() : 0), true;
        if (msg == "gems_hit") return r = Value::integer(p ? p->notesHit() : 0), true;
        if (msg == "gems_passed") return r = Value::integer(p ? p->gemsPassed() : 0), true;
        if (msg == "percent_complete") {
            const float dur = ui_.host().songDurationMs();
            const double t = ui_.host().gameTimeMs();
            return r = Value::integer(dur > 0 ? std::clamp(int(t * 100 / dur), 0, 100) : 0), true;
        }
        if (msg == "avg_multiplier") {
            // The mean multiplier: score over base score [I].
            return r = Value::real(p && p->baseScore() > 0 ? float(p->score()) / float(p->baseScore()) : 1.0f), true;
        }
        if (msg == "sp_phrases" || msg == "streaks_broken") return r = Value::integer(ui_.host().gameCounter(index, msg)), true;
        if (msg == "lead_time_sec") return r = Value::real(0), true;
        if (msg == "base_multiplier") return r = Value::integer(p ? p->baseMultiplier() : 1), true;
        if (msg == "in_star_mode") return r = Value::integer(p && p->starPowerActive() ? 1 : 0), true;
        if (msg == "star_power_ready") return r = Value::integer(p && p->starPowerReady() ? 1 : 0), true;
        if (msg == "crowd_rating") return r = Value::real(p ? p->rockMeter() : 0.5f), true;
        if (msg == "crowd_rating_active") return r = Value::integer(1), true;
        if (msg == "win") return r = Value::integer(p && !p->failed() ? 1 : 0), true;
        if (msg == "failed") return r = Value::integer(p && p->failed() ? 1 : 0), true;
        if (sinks_.handle(ui_.interp(), msg, args)) return true;
        if (msg == "set_crowd_rating" || msg == "set_crowd_rating_active" || msg == "on_downbeat" || msg == "on_game_over" || msg == "load")
            return true;
        // The Star Power natives the tutorials use (0x001131a0) [C]:
        // fill_star_power / empty_star_power / set_star_power <n> set the
        // meter (0x0011ef18, clamped to 0..1); set_star_power_deploy_rate
        // and set_star_power_phrase_boost replace the meter lost per measure
        // while deployed (Star Power +0x50, 0x0011dd30) and the meter a
        // captured phrase adds (+0x54, 0x0011e2b8) for this song.
        if (msg == "fill_star_power" || msg == "empty_star_power" || msg == "set_star_power") {
            const float v = msg == "fill_star_power" ? 1.0f : msg == "empty_star_power" ? 0.0f : args.empty() ? 0.0f : args[0].number();
            ui_.host().setPlayerStarPower(index, "meter", v);
            return true;
        }
        if (msg == "set_star_power_deploy_rate" || msg == "set_star_power_phrase_boost") {
            ui_.host().setPlayerStarPower(index, msg == "set_star_power_deploy_rate" ? "deploy_rate" : "phrase_boost",
                                          args.empty() ? 0.0f : args[0].number());
            return true;
        }
        // The tutorials' switches (PS2 0x001131a0 -> 0x00111cd8 / 0x00111ce0) [C].
        if (msg == "set_whammystarpowerenabled" || msg == "set_mercuryswitchenabled") {
            ui_.host().setPlayerSwitch(index, msg == "set_whammystarpowerenabled" ? "whammy_star_power" : "mercury_switch",
                                       !args.empty() && args[0].truthy());
            return true;
        }
        // What the player's handlers leave (player.dta's {handle ($this
        // whammy_start_callback)}...) and its own held_note_released_callback
        // (0x00112348) go to its sinks (MsgSource) [C].
        if (msg.size() > 9 && msg.compare(msg.size() - 9, 9, "_callback") == 0) {
            sinks_.send(ui_.interp(), msg, args);
            return true;
        }
        return false;
    }

public:
    // A new song: the PS2 makes its players with the game, so no sink stays [C: 0x001107d8].
    void clearSinks() { sinks_.clear(); }

private:
    UiManager& ui_;
    int i_;
    TrackDirObj track_;
    MsgSinks sinks_;
};

// player_matcher0 / 1 (vtable 0x8204a1ac's role on this disc): the
// handlers are beatmatch.dta's player_matcher_handlers; the gameplay's
// gem_hit_callback ($slot $hopoable $hopoed, 0x00115c40),
// gem_miss_callback (0x00116160), gem_pass_callback (0x00116778) and what
// the hopo / swingAtHopo handlers pass on (hopo_callback,
// swingAtHopo_callback) go to the sinks [C]. on_start_starpower /
// on_stop_starpower tell the player's character, which the program's band
// does on the Star Power events (game/world_driver.cpp).
class MatcherObj : public ScriptObject {
public:
    explicit MatcherObj(const std::string& name) : ScriptObject(name, "PlayerMatcher") {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (sinks_.handle(in, msg, args)) return true;
        if (msg == "auto_solo" || msg == "set_auto_solo" || msg == "on_start_starpower" || msg == "on_stop_starpower" ||
            msg == "refresh_track_buttons")
            return true;
        if (msg.size() > 9 && msg.compare(msg.size() - 9, 9, "_callback") == 0) {
            sinks_.send(in, msg, args);
            return true;
        }
        return false;
    }
    void clearSinks() { sinks_.clear(); }

private:
    MsgSinks sinks_;
};

// gamecfg: GameConfig's mode table (config/modes.dta) and players.
class GameCfgObj : public ScriptObject {
public:
    explicit GameCfgObj(UiManager& ui) : ScriptObject("gamecfg", "GameConfig"), ui_(ui) {}
    bool handleNative(Interp& in, const std::string& msg, std::vector<Value>& args, Value& r) override {
        Session& s = ui_.session();
        r = Value::integer(0);
        const auto argStr = [&](size_t i) { return i < args.size() ? nameOf(args[i]) : std::string(); };
        // A path ((practice_sections 1)): the element of a list value (GameConfig 0x822B8B18 get / set
        // through DataArray paths) [C data: practice.dta].
        const bool path = !args.empty() && args[0].type == Value::Type::Array && args[0].array && args[0].array->size() >= 2;
        if (path && (msg == "get" || msg == "set")) {
            const std::string key = (*args[0].array)[0].str();
            const size_t idx = size_t(std::max(0, (*args[0].array)[1].integerValue()));
            Value v = gameCfgValue(ui_, key);
            std::vector<Value> list = v.type == Value::Type::Array && v.array ? *v.array : std::vector<Value>{};
            if (msg == "get") return r = idx < list.size() ? in.evalValue(list[idx]) : Value::integer(0), true;
            if (args.size() < 2) return true;
            if (list.size() <= idx) list.resize(idx + 1, Value::integer(0));
            list[idx] = args[1];
            s.cfgSet[key] = Value::list(std::move(list));
            return true;
        }
        if (msg == "get") return r = gameCfgValue(ui_, argStr(0)), true;
        if (msg == "set") {
            if (args.size() < 2) return true;
            if (argStr(0) == "mode") {
                s.mode = args[1].str();
                s.cfgSet.clear();  // a new mode: its own table
            } else {
                s.cfgSet[argStr(0)] = args[1];
            }
            return true;
        }
        if (msg == "get_song") return r = Value::symbol(s.song), true;
        if (msg == "multiplayer") return r = Value::integer(gameCfgValue(ui_, "num_players").integerValue() > 1 ? 1 : 0), true;
        if (msg == "get_num_players") return r = Value::integer(std::max(1, gameCfgValue(ui_, "num_players").integerValue())), true;
        if (msg == "set_player") {
            const int p = std::clamp(intArg(args, 0), 0, 1);
            s.players[size_t(p)].padnum = intArg(args, 1, p);
            return true;
        }
        if (msg == "get_difficulty") return r = Value::integer(s.players[size_t(std::clamp(intArg(args, 0), 0, 1))].difficulty), true;
        if (msg == "get_difficulty_sym")
            return r = Value::symbol(difficultySym(s.players[size_t(std::clamp(intArg(args, 0), 0, 1))].difficulty)), true;
        if (msg == "set_track") {
            // 0x822B6BD0: the player plays the song's instrument n
            // (song_provider get_instrument), its track type from it.
            Session::PlayerCfg& p = s.players[size_t(std::clamp(intArg(args, 0), 0, 1))];
            p.track = intArg(args, 1);
            const std::vector<std::string> inst = songInstruments(ui_, s.song);
            p.trackType = p.track >= 0 && p.track < int(inst.size()) ? trackTypeOf(inst[size_t(p.track)]) : 2;
            return true;
        }
        if (msg == "set_track_type") return s.players[size_t(std::clamp(intArg(args, 0), 0, 1))].trackType = intArg(args, 1), true;
        if (msg == "get_track_type") return r = Value::integer(s.players[size_t(std::clamp(intArg(args, 0), 0, 1))].trackType), true;
        if (msg == "get_track_sym") return r = trackSym(ui_, std::clamp(intArg(args, 0), 0, 1)), true;
        if (msg == "get_player_config") return r = in.send("game", "get_player_config", args), true;
        if (msg == "foreach_player") return r = ui_.foreachPlayer(args), true;
        if (msg == "win_campaign_song") return r = Value::integer(0), true;
        // get_section_bounds $sec $start $end (0x00127138) [C]: section $sec's
        // start and end ms into $start / $end (-1 the whole song); the
        // variables come by name (Interp::byNameMessages).
        if (msg == "get_section_bounds") {
            auto var = [&](size_t i) {
                std::string n = i < args.size() ? args[i].str() : std::string();
                return n.size() > 1 && n[0] == '$' ? n.substr(1) : n;
            };
            const int sec = !args.empty() && args[0].isNumber() ? args[0].integerValue() : in.var(var(0)).integerValue();
            auto* sp = dynamic_cast<SectionProviderObj*>(in.findObject("section_provider"));
            const UiHost::Section e = sp ? sp->entry(sec) : UiHost::Section{};
            if (args.size() > 1) in.var(var(1)) = Value::real(e.startMs);
            if (args.size() > 2) in.var(var(2)) = Value::real(e.endMs);
            return true;
        }
        // A handler of the mode's table (practice's full_speed_block,
        // pre_processed_audio: GameConfig evaluates the mode's entry) [C data: modes.dta].
        if (const DataNode* modes = ui_.modes())
            for (const std::string& m : {s.mode, std::string("defaults")})
                if (const DataNode* mode = modes->find(m); mode && mode->find(msg)) return r = gameCfgValue(ui_, msg), true;
        return false;
    }

private:
    UiManager& ui_;
};

// beatmatch: the music and the players' input while the song plays.
class BeatmatchObj : public ScriptObject {
public:
    explicit BeatmatchObj(UiManager& ui) : ScriptObject("beatmatch", "Beatmatch"), ui_(ui) {}
    bool handleNative(Interp&, const std::string& msg, std::vector<Value>& args, Value& r) override {
        r = Value::integer(0);
        if (msg == "set_paused") return ui_.host().setMusicPaused(!args.empty() && args[0].truthy()), true;
        if (msg == "paused") return r = Value::integer(ui_.host().musicPaused() ? 1 : 0), true;
        if (msg == "set_music_speed") return ui_.host().setMusicSpeed(args.empty() ? 1.0f : args[0].number()), true;
        if (msg == "set_volume") return ui_.host().setMusicVolume(args.empty() ? 0.0f : args[0].number(), 0), true;
        if (msg == "update_practice_speed" || msg == "jump" || msg == "set_shuttle") return true;
        if (msg == "shuttle_active") return true;
        return false;
    }

private:
    UiManager& ui_;
};

// The PS2 makes its players and matchers with each game (0x001107d8), so
// a new song starts with no sinks on them [C].
void GamePanelObj::clearPlayerSinks() {
    Interp& in = ui.interp();
    for (const char* n : {"player0", "player1"})
        if (auto* p = dynamic_cast<PlayerObj*>(in.findObject(n))) p->clearSinks();
    for (const char* n : {"player_matcher0", "player_matcher1"})
        if (auto* m = dynamic_cast<MatcherObj*>(in.findObject(n))) m->clearSinks();
}

}  // namespace

std::unique_ptr<ScriptObject> makeGameClass(UiManager& ui, const std::string& cls, const std::string& name) {
    if (cls == "GamePanel") return std::make_unique<GamePanelObj>(ui, name);
    if (cls == "TrackPanel") return std::make_unique<HostPanelObj>(ui, name, cls, true);
    if (cls == "HudPanel") return std::make_unique<HostPanelObj>(ui, name, cls, false);
    if (cls == "TrackMaskPanel" || cls == "MidiLoaderPanel") return std::make_unique<EmptyPanelObj>(ui, name, cls);
    return nullptr;
}

void installGameNatives(UiManager& ui, Interp& in, std::vector<std::unique_ptr<ScriptObject>>& owned) {
    auto add = [&](std::unique_ptr<ScriptObject> o) {
        in.registerObject(o.get());
        owned.push_back(std::move(o));
    };
    add(std::make_unique<GameCfgObj>(ui));
    add(std::make_unique<PlayerObj>(ui, "player0", 0));
    add(std::make_unique<PlayerObj>(ui, "player1", 1));
    add(std::make_unique<MatcherObj>("player_matcher0"));
    add(std::make_unique<MatcherObj>("player_matcher1"));
    add(std::make_unique<PlayerObj>(ui, "band", -1));
    add(std::make_unique<BeatmatchObj>(ui));
    add(std::make_unique<SectionProviderObj>(ui));
}

bool MsgSinks::handle(Interp& in, const std::string& msg, const std::vector<Value>& args) {
    if (msg != "add_sink" && msg != "remove_sink") return false;
    ScriptObject* o = args.empty() ? nullptr : args[0].type == Value::Type::Object ? args[0].object : in.findObject(args[0].str());
    if (!o) return true;
    if (msg == "remove_sink") {
        sinkDestroyed(o);
        return true;
    }
    std::vector<std::string> msgs;
    for (size_t k = 1; k < args.size(); ++k) msgs.push_back(args[k].str());
    for (auto& [s, m] : sinks_)
        if (s == o) {
            if (msgs.empty() || m.empty()) m.clear();
            else m.insert(m.end(), msgs.begin(), msgs.end());
            return true;
        }
    sinks_.push_back({o, msgs});
    return true;
}

void MsgSinks::send(Interp& in, const std::string& msg, const std::vector<Value>& args) {
    const auto sinks = sinks_;
    for (const auto& [o, msgs] : sinks) {
        if (!msgs.empty() && std::find(msgs.begin(), msgs.end(), msg) == msgs.end()) continue;
        if (std::none_of(sinks_.begin(), sinks_.end(), [o = o](const auto& p) { return p.first == o; })) continue;
        in.send(o, msg, args, false);
    }
}

void MsgSinks::sinkDestroyed(ScriptObject* sink) {
    sinks_.erase(std::remove_if(sinks_.begin(), sinks_.end(), [&](const auto& p) { return p.first == sink; }), sinks_.end());
}

void gameEventImpl(UiManager&, const std::string& msg, std::vector<Value> args) {
    if (gGame) gGame->dispatch(msg, std::move(args));
}

void gameScriptImpl(UiManager& ui, const std::string& msg, std::vector<Value> args) {
    if (gGame) ui.callScript(gGame, msg, std::move(args));
}

}  // namespace ui
