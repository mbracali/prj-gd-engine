// SPDX-License-Identifier: LicenseRef-GDE-PolyForm-NC-1.0.0-plus-terms
// Guitar Dream Engine, https://github.com/mbracali/prj-gd-engine. See LICENSE and LICENSE-ADDITIONAL-TERMS.md.
#include "ui/song_hooks.h"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>

#include "audio/mixer.h"
#include "data/disc.h"
#include "data/pak.h"
#include "data/qb.h"
#include "data/qbkey.h"
#include "game/match.h"
#include "render/canvas.h"
#include "render/texture_cache.h"
#include "render/highway2d.h"
#include "script/vm.h"
#include "ui/front_end.h"
#include "ui/natives_sound.h"
#include "ui/song_screen.h"

namespace {
using qs::ScriptRun;
using qs::Vm;
uint32_t K(const char* s) { return qbkey::key(s); }
uint32_t K(const std::string& s) { return qbkey::key(s); }

const char* const kColors[5] = {"green", "red", "yellow", "blue", "orange"};
const char* const kFx[5] = {"g", "r", "y", "b", "o"};

qb::Value pairOf(float x, float y) {
    qb::Value v;
    v.type = qb::Type::Pair;
    v.f[0] = x, v.f[1] = y;
    return v;
}
qb::Value keyArray(std::initializer_list<const char*> keys) {
    qb::Value v;
    v.type = qb::Type::Array;
    v.arr = std::make_shared<qb::Array>();
    v.arr->type = qb::Type::Key;
    for (const char* k : keys) v.arr->values.push_back(Vm::makeKey(K(k)));
    return v;
}
uint32_t keyOf(const qb::Value* v) {
    if (!v) return 0;
    if (v->type == qb::Type::String || v->type == qb::Type::WString) return qbkey::key(v->str);
    return v->key;
}
qb::Struct* locals(ScriptRun& run) {
    if (run.frames.empty()) return nullptr;
    if (!run.frames.back().locals) run.frames.back().locals = std::make_shared<qb::Struct>();
    return run.frames.back().locals.get();
}
// GH3's fret pattern: green 0x10000, red 0x1000 .. orange 0x1 [C GetHeldPattern].
int32_t patternOf(uint8_t frets) {
    int32_t p = 0;
    for (int l = 0; l < 5; ++l)
        if (frets & (1u << l)) p |= 0x10000 >> (4 * l);
    return p;
}
// GD04_HOOKLOG=1: every event and spawn with its song time (development aid).
bool hookLog() {
    static const bool on = [] {
        const char* v = SDL_getenv("GD04_HOOKLOG");
        return v && *v && *v != '0';
    }();
    return on;
}
const char* statusName(int p) { return p ? "player2_status" : "player1_status"; }
const char* textOf(int p) { return p ? "p2" : "p1"; }
}  // namespace

SongHooks::SongHooks(FrontEnd& fe, Mixer& mixer, const Disc& disc, TextureCache& textures)
    : fe_(fe), mixer_(mixer), disc_(disc), tex_(textures) {
    fx_.setParentPos([this](uint32_t id, float& x, float& y) { return elementPos(id, x, y); });
    addNatives();
    setSoundEventListener([this](uint32_t ev) {
        if (active_) ++k_.sounds[qbkey::name(ev)];
    });
}

SongHooks::~SongHooks() { setSoundEventListener(nullptr); }

bool SongHooks::elementPos(uint32_t id, float& x, float& y) const {
    // An element's screen position: its pos summed up the tree (the song's
    // containers are unscaled) [I].
    const ui::Element* e = fe_.ui().find(id);
    if (!e) return false;
    x = 0, y = 0;
    for (; e; e = e->parent) x += e->x, y += e->y;
    return true;
}

void SongHooks::createProxy(uint32_t id, uint32_t parent) {
    // CreateScreenElement {Type = containerelement id parent pos (0,0) just [left top] z_priority 0}
    // [C script setup_highway: gem_container<p>].
    if (fe_.ui().find(id)) return;
    qb::Struct p;
    Vm::set(p, K("Type"), Vm::makeKey(K("containerelement")));
    Vm::set(p, K("id"), Vm::makeKey(id));
    Vm::set(p, K("parent"), Vm::makeKey(parent));
    Vm::set(p, K("pos"), pairOf(0, 0));
    Vm::set(p, K("just"), keyArray({"left", "top"}));
    Vm::set(p, K("z_priority"), Vm::makeFloat(0));
    fe_.vm().run(K("CreateScreenElement"), p);
    proxies_.push_back(id);
}

void SongHooks::destroyProxies() {
    for (auto it = proxies_.rbegin(); it != proxies_.rend(); ++it)
        if (fe_.ui().find(*it)) {
            qb::Struct p;
            Vm::set(p, K("id"), Vm::makeKey(*it));
            fe_.vm().run(K("DestroyScreenElement"), p);
        }
    proxies_.clear();
}

qb::Struct SongHooks::playerParams(int p) const {
    qb::Struct s;
    Vm::set(s, K("player_status"), Vm::makeKey(K(statusName(p))));
    Vm::set(s, K("player"), Vm::makeInt(p + 1));
    Vm::set(s, K("player_text"), Vm::makeString(textOf(p)));
    return s;
}

uint32_t SongHooks::spawnTracked(uint32_t script, const qb::Struct& params, const char* name, std::map<std::string, uint64_t>& counter) {
    const uint32_t id = fe_.vm().spawn(script, params);
    if (!id) return 0;
    for (auto& r : fe_.vm().runs())
        if (r->id == id) r->type = K("song_event_scripts");
    runs_.insert(id);
    ++counter[name ? std::string(name) : qbkey::name(script)];
    if (hookLog()) SDL_Log("hooks: %.0f ms spawn %s", nowMs_, qbkey::name(script).c_str());
    return id;
}

void SongHooks::broadcast(const char* event, int player, qb::Struct data) {
    // The C's LaunchEvent (0x821578E8, broadcast from system) of
    // <event><player_text> with its parameter struct: create_guitar_events'
    // handlers (event_spawner {event_spawned = i}) take it at once, as GH3's
    // call_script responses do [C 0x82266088; script create_guitar_events].
    const uint32_t key = K(std::string(event) + textOf(player));
    ++k_.events[event];
    if (hookLog()) SDL_Log("hooks: %.0f ms event %s%s", nowMs_, event, textOf(player));
    for (const FlowState::Handler& h : fe_.flow.handlers) {
        if (h.event != key) continue;
        qb::Struct params = h.params;
        for (const qb::Item& it : data.items) Vm::set(params, it.name, it.value);
        const uint32_t before = fe_.vm().runs().empty() ? 0 : fe_.vm().runs().back()->id;
        fe_.vm().run(h.script, params);
        // event_spawner spawned the hook (id = song_event_scripts): the song's.
        for (auto& r : fe_.vm().runs())
            if (r->id > before) runs_.insert(r->id);
    }
}

void SongHooks::buildInputArray(SongScreen& s, int p) {
    // input_array<p>: per note [time, len green .. orange, hopo, next_time]
    // (the next chart note's time, 7,200,000 after the last) [C 0x821044C8 -> 0x82101770].
    const NoteTrack& t = s.match().track(p);
    PerPlayer& pl = pp_[size_t(p)];
    if (t.notes.size() == pl.arraySize) return;
    const uint32_t name = K(std::string("input_array") + textOf(p));
    qb::Value v;
    if (const qb::Value* cur = fe_.vm().global(name); cur && cur->type == qb::Type::Array && cur->arr && pl.arraySize) v = *cur;
    else v.type = qb::Type::Array, v.arr = std::make_shared<qb::Array>(), v.arr->type = qb::Type::Array;
    auto arr = std::make_shared<qb::Array>(*v.arr);
    arr->values.resize(std::min(arr->values.size(), pl.arraySize));
    for (size_t i = arr->values.size(); i < t.notes.size(); ++i) {
        const TrackNote& n = t.notes[i];
        qb::Value e;
        e.type = qb::Type::Array;
        e.arr = std::make_shared<qb::Array>();
        e.arr->type = qb::Type::Int;
        e.arr->values.push_back(Vm::makeInt(n.time));
        for (int l = 0; l < 5; ++l) e.arr->values.push_back(Vm::makeInt((n.frets & (1u << l)) ? std::max(1, n.length) : 0));
        e.arr->values.push_back(Vm::makeInt(n.hopo ? 1 : 0));
        e.arr->values.push_back(Vm::makeInt(i + 1 < t.notes.size() ? t.notes[i + 1].time : 7200000));
        arr->values.push_back(std::move(e));
    }
    // The previous last note's next_time now points at its successor.
    if (pl.arraySize && pl.arraySize < arr->values.size() && arr->values[pl.arraySize - 1].arr) {
        qb::Value fixed = arr->values[pl.arraySize - 1];
        fixed.arr = std::make_shared<qb::Array>(*fixed.arr);
        if (fixed.arr->values.size() > 7) fixed.arr->values[7] = Vm::makeInt(t.notes[pl.arraySize].time);
        arr->values[pl.arraySize - 1] = fixed;
    }
    v.arr = arr;
    fe_.vm().setGlobal(name, v);
    pl.arraySize = t.notes.size();
}

void SongHooks::syncStatus(SongScreen& s, int p) {
    // playerN_status as the C keeps it during the song [C data guitar.qb;
    // UpdateScoreFastPerFrame, 0x8210BB40, 0x8210C5B8, 0x8210FD18].
    const uint32_t name = K(statusName(p));
    const qb::Value* cur = fe_.vm().global(name);
    if (!cur || !cur->st) return;
    const PlayerStats& st = s.match().player(p).stats();
    const NoteTrack& t = s.match().track(p);
    qb::Value v = *cur;
    v.st = std::make_shared<qb::Struct>(*cur->st);
    Vm::set(*v.st, K("score"), Vm::makeFloat(float(st.score)));
    Vm::set(*v.st, K("current_run"), Vm::makeInt(st.currentRun));
    Vm::set(*v.st, K("best_run"), Vm::makeInt(st.bestRun));
    Vm::set(*v.st, K("notes_hit"), Vm::makeInt(st.notesHit));
    Vm::set(*v.st, K("total_notes"), Vm::makeInt(st.totalNotes));
    Vm::set(*v.st, K("current_health"), Vm::makeFloat(st.health));
    Vm::set(*v.st, K("star_power_amount"), Vm::makeFloat(st.starPower));
    Vm::set(*v.st, K("star_power_used"), Vm::makeInt(st.starPowerActive ? 1 : 0));
    Vm::set(*v.st, K("sp_phrases_hit"), Vm::makeInt(st.phrasesCaptured));
    Vm::set(*v.st, K("playline_song_beat_time"), Vm::makeFloat(t.beatAt(float(nowMs_))));
    Vm::set(*v.st, K("playline_song_measure_time"), Vm::makeFloat(t.measureAt(float(nowMs_))));
    fe_.vm().setGlobal(name, v);
}

void SongHooks::loadSongScripts(SongScreen& s) {
    // load_songqpak: the song pak's QB files (its event arrays) into the
    // globals [C script start_gem_scroller].
    const std::string path = "DATA/COMPRESSED/SONGS/" + s.song().name + "_song.pak.xen";
    try {
        const Pak pak = Pak::load(disc_, path);
        for (const PakEntry& e : pak.entries())
            if (e.type == K(".qb")) {
                auto [d, n] = pak.data(e);
                fe_.vm().globals().add(qb::decode(d, n));
            }
    } catch (const std::exception& ex) {
        SDL_Log("song hooks: %s: %s", path.c_str(), ex.what());
    }
}

void SongHooks::begin(SongScreen& s) {
    song_ = &s;
    active_ = true, paused_ = false;
    k_ = Counters{};
    runs_.clear();
    fx_.clear();
    pp_ = {};
    players_ = s.match().players();
    nowMs_ = s.startMs() - 3000;
    const Match& m0 = s.match();
    lastCards_ = {}, lastFired_ = {}, lastByAttack_ = {};
    Vm& vm = fe_.vm();
    loadSongScripts(s);
    for (int p = 0; p < players_; ++p) {
        buildInputArray(s, p);
        syncStatus(s, p);
        PerPlayer& pl = pp_[size_t(p)];
        pl.lastStar = s.match().player(p).stats().starPower;
        // The guitar starts muted until the first note (guitar_volume 0); the
        // lefty flip; check_time_early x the speed factor [C scripts
        // start_gem_scroller, gem_scroller].
        if (const qb::Value* cur = vm.global(K(statusName(p))); cur && cur->st) {
            qb::Value v = *cur;
            v.st = std::make_shared<qb::Struct>(*cur->st);
            Vm::set(*v.st, K("guitar_volume"), Vm::makeInt(0));
            const int lefty = fe_.songRequest.lefty[p] && !(m0.mode() == GameMode::Boss && p == 1) ? 1 : 0;
            Vm::set(*v.st, K("lefthanded_gems"), Vm::makeInt(lefty));
            Vm::set(*v.st, K("lefthanded_button_ups"), Vm::makeInt(lefty));
            const float speed = Vm::number(vm.global(K("current_speedfactor")), 1);
            Vm::set(*v.st, K("check_time_early"), Vm::makeFloat(Vm::number(vm.global(K("check_time_early")), 0.116f) * speed));
            Vm::set(*v.st, K("check_time_late"), Vm::makeFloat(Vm::number(vm.global(K("check_time_late")), 0.1f) * speed));
            vm.setGlobal(K(statusName(p)), v);
        }
        const NoteTrack& t = s.match().track(p);
        for (size_t i = 0; i < t.notes.size(); ++i)
            if (t.notes[i].time >= s.startMs()) {
                pl.firstNote = int(i);
                break;
            }
        // gem_container<p>: the native highway's container, for the scripts' elements under it.
        createProxy(K(std::string("gem_container") + textOf(p)), K("root_window"));
    }
    // generate_pos_table's SetButtonData: button_up_models.<color>.pos_2d =
    // (the lane's playline x, highway_playline), left_pos_2d the mirrored
    // lane's; button_models.<color>.angle = Atan2(highway_height, top x -
    // playline x), left_angle the mirrored lane's [C script; the native
    // highway's lanes, render/highway2d] -- the scripts' bolts and sparks sit there.
    {
        auto setColor = [&](const char* model, int l, const std::vector<std::pair<uint32_t, qb::Value>>& fields) {
            const qb::Value* cur = vm.global(K(model));
            if (!cur || !cur->st) return;
            qb::Value v = *cur;
            v.st = std::make_shared<qb::Struct>(*cur->st);
            qb::Value c;
            if (const qb::Value* old = v.st->find(K(kColors[l]))) c = *old;
            c.type = qb::Type::Struct;
            c.st = c.st ? std::make_shared<qb::Struct>(*c.st) : std::make_shared<qb::Struct>();
            for (const auto& [k, fv] : fields) Vm::set(*c.st, k, fv);
            Vm::set(*v.st, K(kColors[l]), c);
            vm.setGlobal(K(model), v);
        };
        const Highway2D& hw = s.highway();
        for (int l = 0; l < 5; ++l) {
            float tx, ex, ty, pl, mtx, mex;
            hw.lane(l, tx, ex, ty, pl);
            hw.lane(4 - l, mtx, mex, ty, pl);
            const float angle = std::atan2(tx - ex, pl - ty) * 180.0f / 3.14159265f;
            const float mangle = std::atan2(mtx - mex, pl - ty) * 180.0f / 3.14159265f;
            setColor("button_up_models", l, {{K("pos_2d"), pairOf(ex, pl)}, {K("left_pos_2d"), pairOf(mex, pl)}});
            setColor("button_models", l, {{K("angle"), Vm::makeFloat(angle)}, {K("left_angle"), Vm::makeFloat(mangle)}});
        }
    }
    vm.setGlobal(K("star_power_ready_on_p1"), Vm::makeInt(0));
    vm.setGlobal(K("star_power_ready_on_p2"), Vm::makeInt(0));
    vm.setGlobal(K("Star_power_verb_is_on"), Vm::makeInt(0));
    vm.setGlobal(K("highway_pulse"), Vm::makeInt(0));
    lastCrowd_ = s.match().player(0).stats().health;
    vm.setGlobal(K("current_crowd"), Vm::makeFloat(lastCrowd_));
    mixer_.guitarFx().reset();
    pushedBuss_.clear();
    // start_gem_scroller's own sound setup: the venue's crowd echo / reverb
    // and the guitar buss dry [C script start_gem_scroller].
    vm.run(K("GuitarEvent_EnterVenue"), qb::Struct{});
    vm.run(K("GH3_Set_Guitar_Verb_And_Echo_to_Dry"), qb::Struct{});
    // The fretbar iterators start at the song's start.
    fretbar_ = preFretbar_ = 0;
    const NoteTrack& t0 = s.match().track(0);
    while (fretbar_ < t0.fretbars.size() && t0.fretbars[fretbar_] < s.startMs()) ++fretbar_;
    preFretbar_ = fretbar_;
    // The event arrays and note maps by their firing times [C scripts
    // gem_scroller (scripts_array), event_iterator, notemap_iterator].
    timed_.clear();
    timedAt_ = 0;
    const std::string song = s.song().name;
    const float gemOffset = float(s.song().gemOffset);
    const qb::Value* sa = vm.global(K("scripts_array"));
    // Already played by the native core: the band's performance clips
    // (render/band_anim reads <song>_performance) and the boss's Death
    // Drain (game/match) [C: the same scripts there].
    auto native = [](const std::string& arr, uint32_t scr) {
        if (scr == K("boss_battle_begin_deathlick")) return true;
        if (arr == "performance")
            return scr == K("Band_ChangeStance") || scr == K("Band_PlayAnim") || scr == K("Band_PlayFacialAnim") || scr == K("Band_PlayIdle");
        return false;
    };
    static const char* const kArrays[8] = {"scripts", "anim", "triggers", "cameras", "lightshow", "crowd", "drums", "performance"};
    if (sa && sa->arr)
        for (const qb::Value& e : sa->arr->values) {
            if (!e.st) continue;
            const qb::Value* nm = e.st->find(K("name"));
            if (!nm) continue;
            const std::string arr = nm->str;
            const char* arrName = "";
            for (const char* a : kArrays)
                if (arr == a) arrName = a;
            float lead = 0;
            if (const qb::Value* l = e.st->find(K("lead_ms"))) {
                if (l->type == qb::Type::KeyPtr || l->type == qb::Type::Key) lead = Vm::number(vm.global(l->key), 0);
                else lead = l->number();
            }
            const double offset = double(gemOffset + lead);
            // event_iterator: {time, scr, params} fires at time - offset.
            if (const qb::Value* a = vm.global(K(song + "_" + arr)); a && a->type == qb::Type::Array && a->arr)
                for (const qb::Value& ev : a->arr->values) {
                    if (!ev.st) continue;
                    const double t = Vm::number(ev.st->find(K("time")), 0) - offset;
                    const uint32_t scr = keyOf(ev.st->find(K("scr")));
                    if (!scr || t < s.startMs()) continue;
                    if (native(arr, scr)) {
                        ++k_.nativeSkipped;
                        continue;
                    }
                    Timed x;
                    x.time = t, x.script = scr, x.array = arrName;
                    Vm::set(x.params, K("time"), Vm::makeFloat(float(Vm::number(ev.st->find(K("time")), 0))));
                    if (const qb::Value* ps = ev.st->find(K("params")); ps && ps->st)
                        for (const qb::Item& it : ps->st->items) x.params.items.push_back(it);
                    timed_.push_back(std::move(x));
                }
            // notemap_iterator: <song>_<name>_notes [time, note, length(, velocity)]
            // through <Name>_AutoNoteMapping (crowd surges / applause, the drums' count-off).
            std::string mapName = arr;
            if (!mapName.empty()) mapName[0] = char(std::toupper(static_cast<unsigned char>(mapName[0])));
            const qb::Value* map = vm.global(K(mapName + "_AutoNoteMapping"));
            const qb::Value* notes = vm.global(K(song + "_" + arr + "_notes"));
            if (map && map->arr && notes && notes->arr)
                for (const qb::Value& n : notes->arr->values) {
                    if (!n.arr || n.arr->values.size() < 3) continue;
                    const double t = n.arr->values[0].number() - offset;
                    if (t < s.startMs()) continue;
                    const int note = int(n.arr->values[1].number());
                    for (const qb::Value& m : map->arr->values) {
                        if (!m.st || int(Vm::number(m.st->find(K("midinote")), -1)) != note) continue;
                        Timed x;
                        x.time = t, x.script = keyOf(m.st->find(K("scr"))), x.array = arrName;
                        if (const qb::Value* ps = m.st->find(K("params")); ps && ps->st) x.params = *ps->st;
                        Vm::set(x.params, K("length"), Vm::makeInt(int32_t(n.arr->values[2].number())));
                        Vm::set(x.params, K("velocity"), Vm::makeInt(n.arr->values.size() > 3 ? int32_t(n.arr->values[3].number()) : 100));
                        timed_.push_back(std::move(x));
                        break;
                    }
                }
        }
    std::stable_sort(timed_.begin(), timed_.end(), [](const Timed& a, const Timed& b) { return a.time < b.time; });
    size_t handlers = 0;
    for (const FlowState::Handler& h : fe_.flow.handlers)
        if (h.script == K("event_spawner")) ++handlers;
    SDL_Log("song hooks: %llu event handlers, %llu timed scripts (%llu played natively)", (unsigned long long)handlers,
            (unsigned long long)timed_.size(),
            (unsigned long long)k_.nativeSkipped);
}

void SongHooks::eventArrays(SongScreen&, double now) {
    // TimeMarkerReached: GetSongTimeMs + time_offset >= the entry's time;
    // a script spawns with {time params} (id song_event_scripts), a C
    // function is called [C scripts event_iterator, notemap_iterator].
    while (timedAt_ < timed_.size() && timed_[timedAt_].time <= now) {
        Timed& x = timed_[timedAt_++];
        if (fe_.vm().code(x.script)) {
            spawnTracked(x.script, x.params, nullptr, k_.arrays);
        } else if (fe_.vm().hasNative(x.script)) {
            fe_.vm().run(x.script, x.params);
            ++k_.arrays[qbkey::name(x.script)];
        } else {
            fe_.vm().logOnce("song hooks: no script or C function " + qbkey::name(x.script) + " (" + x.array + ")");
        }
    }
}

void SongHooks::step(SongScreen& s, double now) {
    if (!active_ || song_ != &s) return;
    nowMs_ = now;
    Vm& vm = fe_.vm();
    const Match& m = s.match();
    const bool coop = m.mode() == GameMode::Coop;
    for (int p = 0; p < players_; ++p) {
        buildInputArray(s, p);
        syncStatus(s, p);
    }
    // current_crowd: player 1's health (two players outside battle / boss: the
    // mean); crossing crowd_poor_medium / crowd_medium_good spawns the crowd's
    // transition sound and its loop change, not in face-off [C 0x8210F200].
    {
        float crowd = m.player(0).stats().health;
        if (players_ == 2 && !m.battleMode()) crowd = (crowd + m.player(1).stats().health) * 0.5f;
        vm.setGlobal(K("current_crowd"), Vm::makeFloat(crowd));
        const float pm = Vm::number(vm.global(K("crowd_poor_medium")), 0.66f), mg = Vm::number(vm.global(K("crowd_medium_good")), 1.33f);
        const char* sfx = nullptr;
        const char* state = nullptr;
        if (lastCrowd_ < pm && crowd >= pm) sfx = "crowd_transition_sfx_poor_to_medium", state = "neutral";
        if (lastCrowd_ < mg && crowd >= mg) sfx = "crowd_transition_sfx_medium_to_good", state = "good";
        if (lastCrowd_ >= pm && crowd < pm) sfx = "crowd_transition_sfx_medium_to_poor", state = "bad";
        if (lastCrowd_ >= mg && crowd < mg) sfx = "crowd_transition_sfx_good_to_medium", state = "neutral";
        if (sfx && m.mode() != GameMode::FaceOff && m.mode() != GameMode::ProFaceOff) {
            qb::Struct cp;
            Vm::set(cp, K("crowd_looping_state"), Vm::makeKey(K(state)));
            Vm::set(cp, K("player"), Vm::makeInt(1));
            spawnTracked(K(sfx), cp, sfx, k_.spawned);
            spawnTracked(K("change_crowd_looping_sfx"), cp, "change_crowd_looping_sfx", k_.spawned);
        }
        lastCrowd_ = crowd;
    }
    for (int p = 0; p < players_; ++p) {
        const Player& pl = m.player(p);
        const PlayerStats& st = pl.stats();
        const NoteTrack& t = m.track(p);
        PerPlayer& pp = pp_[size_t(p)];
        const uint32_t ps = K(statusName(p));
        const uint32_t songArr = K(std::string("input_array") + textOf(p));
        auto base = [&](int note) {
            qb::Struct d;
            Vm::set(d, K("guitar_stream"), Vm::makeKey(K(s.song().name + (m.player(p).rules().boss ? "_rhythm" : "_guitar"))));
            Vm::set(d, K("array_entry"), Vm::makeInt(std::max(0, note)));
            Vm::set(d, K("player_status"), Vm::makeKey(ps));
            Vm::set(d, K("song"), Vm::makeKey(songArr));
            return d;
        };
        pp.taps.clear();
        for (const PlayerEvent& e : pl.events())
            if (e.kind == PlayerEvent::Kind::Hopo) pp.taps.insert(e.note);
        for (const PlayerEvent& e : pl.events()) {
            const TrackNote* n = e.note >= 0 && size_t(e.note) < t.notes.size() ? &t.notes[size_t(e.note)] : nullptr;
            switch (e.kind) {
            case PlayerEvent::Kind::Hit: {
                if (!n) break;
                // hit_notes, then a hit_note per gem of the note [C 0x8211EC00].
                const int hammer = pp.taps.count(e.note) ? 1 : 0;
                qb::Struct d = base(e.note);
                Vm::set(d, K("pattern"), Vm::makeInt(patternOf(n->frets)));
                Vm::set(d, K("off_note"), Vm::makeFloat(float(now - n->time)));
                Vm::set(d, K("whammy_length"), Vm::makeFloat(n->sustain ? float(n->holdMs) : 0.0f));
                Vm::set(d, K("hammer_strum"), Vm::makeInt(hammer));
                broadcast("hit_notes", p, d);
                const qb::Value* models = vm.global(K("button_up_models"));
                const qb::Value* psv = vm.global(ps);
                const bool lefty = psv && psv->st && Vm::number(vm.globals().field(*psv->st, K("lefthanded_button_ups")), 0) != 0;
                for (int l = 0; l < 5; ++l) {
                    if (!(n->frets & (1u << l))) continue;
                    qb::Struct h;
                    if (models && models->st)
                        if (const qb::Value* c = vm.globals().field(*models->st, K(kColors[l])); c && c->st)
                            if (const qb::Value* pos = vm.globals().field(*c->st, K(lefty ? "left_pos_2d" : "pos_2d"))) Vm::set(h, K("pos"), *pos);
                    Vm::set(h, K("color"), Vm::makeKey(K(kColors[l])));
                    Vm::set(h, K("entry"), Vm::makeInt(e.note));
                    Vm::set(h, K("player"), Vm::makeInt(p + 1));
                    Vm::set(h, K("player_text"), Vm::makeString(textOf(p)));
                    Vm::set(h, K("hammer_strum"), Vm::makeInt(hammer));
                    Vm::set(h, K("fx_id"), Vm::makeKey(K(std::string("fx_") + kFx[l] + textOf(p))));
                    broadcast("hit_note", p, h);
                }
                // A sustain held: whammy_on {pattern time ...} [C 0x8211C7E8].
                if (n->sustain) {
                    qb::Struct w = base(e.note);
                    Vm::set(w, K("pattern"), Vm::makeInt(patternOf(n->frets)));
                    Vm::set(w, K("time"), Vm::makeFloat(float(n->holdMs) / 1000.0f));
                    Vm::set(w, K("player"), Vm::makeInt(p + 1));
                    Vm::set(w, K("player_text"), Vm::makeString(textOf(p)));
                    broadcast("whammy_on", p, w);
                }
                break;
            }
            case PlayerEvent::Kind::Miss: {
                qb::Struct d = base(e.note);
                Vm::set(d, K("silent_miss"), Vm::makeInt(0));
                Vm::set(d, K("bum_note"), Vm::makeInt(e.strum ? 1 : 0));
                broadcast("missed_note", p, d);
                break;
            }
            case PlayerEvent::Kind::Overstrum: {
                qb::Struct d = base(std::min<int>(e.note, int(t.notes.size()) - 1));
                Vm::set(d, K("songtime"), Vm::makeFloat(float(now / 1000.0)));
                broadcast("unnecessary_note", p, d);
                break;
            }
            case PlayerEvent::Kind::StarHit: {
                qb::Struct d;
                Vm::set(d, K("array_entry"), Vm::makeInt(e.note));
                Vm::set(d, K("player_status"), Vm::makeKey(ps));
                Vm::set(d, K("song"), Vm::makeKey(songArr));
                broadcast("star_hit_note", p, d);
                break;
            }
            case PlayerEvent::Kind::StarPhrase: {
                qb::Struct d;
                Vm::set(d, K("array_entry"), Vm::makeInt(e.note));
                Vm::set(d, K("player_status"), Vm::makeKey(ps));
                Vm::set(d, K("song"), Vm::makeKey(songArr));
                broadcast("star_sequence_bonus", p, d);
                break;
            }
            case PlayerEvent::Kind::StarMiss: {
                qb::Struct d;
                Vm::set(d, K("array_entry"), Vm::makeInt(e.note));
                Vm::set(d, K("song"), Vm::makeKey(songArr));
                broadcast("star_miss_note", p, d);
                break;
            }
            case PlayerEvent::Kind::SustainDone:
            case PlayerEvent::Kind::SustainLost: {
                if (!n) break;
                qb::Struct w = base(e.note);
                Vm::set(w, K("pattern"), Vm::makeInt(patternOf(n->frets)));
                Vm::set(w, K("time"), Vm::makeFloat(float(n->holdMs) / 1000.0f));
                Vm::set(w, K("player_text"), Vm::makeString(textOf(p)));
                Vm::set(w, K("finished"), Vm::makeInt(e.kind == PlayerEvent::Kind::SustainDone ? 1 : 0));
                broadcast("whammy_off", p, w);
                break;
            }
            case PlayerEvent::Kind::StarPowerOn:
                // check_for_star_power's activation spawns
                // star_power_activate_and_drain {player_status player_text player}
                // [C 0x82113244 / 0x82113460]; its drain is overwritten by the
                // judge's amount each frame (the native drain).
                spawnTracked(K("star_power_activate_and_drain"), playerParams(p), "star_power_activate_and_drain", k_.spawned);
                break;
            default: break;
            }
        }
        // The multiplier: guitarevent_multiplier<n>xon when the run reaches
        // 10 / 20 / 30, <n>xoff (the multiplier it had) when a miss or an
        // overstrum ends a run of 10 or more; co-op: the shared run, for
        // both players' statuses [C 0x8210BF70, 0x8210C9F8, 0x8210CB10].
        if (!(coop && p == 1)) {
            const int run = st.multiplierRun;
            std::vector<std::pair<const char*, int>> fire;
            if (run < pp.lastRun) {
                if (pp.lastRun >= 30) fire.push_back({"guitarevent_multiplier4xoff", 0});
                else if (pp.lastRun >= 20) fire.push_back({"guitarevent_multiplier3xoff", 0});
                else if (pp.lastRun >= 10) fire.push_back({"guitarevent_multiplier2xoff", 0});
            }
            const int from = run < pp.lastRun ? 0 : pp.lastRun;
            for (int k : {10, 20, 30})
                if (from < k && run >= k)
                    fire.push_back({k == 30 ? "guitarevent_multiplier4xon" : k == 20 ? "guitarevent_multiplier3xon" : "guitarevent_multiplier2xon", 0});
            for (auto& [script, unused] : fire) {
                (void)unused;
                for (int q = 0; q < (coop ? 2 : 1); ++q) {
                    qb::Struct sp;
                    Vm::set(sp, K("player_status"), Vm::makeKey(K(statusName(coop ? q : p))));
                    spawnTracked(K(script), sp, script, k_.spawned);
                }
            }
            pp.lastRun = run;
        }
        // Star Power ready: show_star_power_ready {player_status} when the
        // amount reaches 50 [C 0x8210C7A0].
        if (pp.lastStar < 50.0f && st.starPower >= 50.0f && pl.rules().starPower) {
            qb::Struct sp;
            Vm::set(sp, K("player_status"), Vm::makeKey(ps));
            spawnTracked(K("show_star_power_ready"), sp, "show_star_power_ready", k_.spawned);
        }
        pp.lastStar = st.starPower;
        // The first gem: first_gem {player_status gem_id} as Create2DGem makes
        // it, scroll_time - destroy_time before its time; firstnote_window_open
        // {song player_status} check_time_early before it [C 0x82121C28, 0x82120300].
        if (pp.firstNote >= 0 && size_t(pp.firstNote) < t.notes.size()) {
            const TrackNote& fn = t.notes[size_t(pp.firstNote)];
            const Highway2D& hw = p ? s.highway2() : s.highway();
            if (!pp.firstGem && now >= double(fn.time) - hw.travelMs() - s.song().gemOffset) {
                pp.firstGem = true;
                int fret = 0;
                while (fret < 4 && !(fn.frets & (1u << fret))) ++fret;
                pp.firstGemId = K(std::string(kColors[fret]) + "_first_gem" + textOf(p));
                createProxy(pp.firstGemId, K(std::string("gem_container") + textOf(p)));
                qb::Struct d;
                Vm::set(d, K("player_status"), Vm::makeKey(ps));
                Vm::set(d, K("gem_id"), Vm::makeKey(pp.firstGemId));
                if (fn.phrase >= 0) d.items.push_back(qb::Item{0, Vm::makeKey(K("is_star"))});
                broadcast("first_gem", p, d);
            }
            const qb::Value* psv = vm.global(ps);
            const float early = psv && psv->st ? Vm::number(vm.globals().field(*psv->st, K("check_time_early")), 0.1f) : 0.1f;
            if (!pp.firstWindow && now >= double(fn.time) - early * 1000.0) {
                pp.firstWindow = true;
                qb::Struct d;
                Vm::set(d, K("song"), Vm::makeKey(songArr));
                Vm::set(d, K("player_status"), Vm::makeKey(ps));
                broadcast("firstnote_window_open", p, d);
            }
        }
    }
    // The fretbar iterators (player 1): GuitarEvent_PreFretbar
    // prefretbar_time early, GuitarEvent_Fretbar on the beat (gem offset
    // included) [C script gem_scroller, fretbar_iterator].
    {
        const NoteTrack& t = m.track(0);
        const double gemOffset = s.song().gemOffset;
        const double pre = Vm::number(vm.global(K("prefretbar_time")), 0.25f) * 1000.0;
        while (preFretbar_ < t.fretbars.size() && now >= double(t.fretbars[preFretbar_]) - pre - gemOffset) {
            ++preFretbar_;
            spawnTracked(K("GuitarEvent_PreFretbar"), playerParams(0), "GuitarEvent_PreFretbar", k_.spawned);
        }
        while (fretbar_ < t.fretbars.size() && now >= double(t.fretbars[fretbar_]) - gemOffset) {
            ++fretbar_;
            spawnTracked(K("GuitarEvent_Fretbar"), playerParams(0), "GuitarEvent_Fretbar", k_.spawned);
        }
    }
    // Battle / boss: the native Match plays the cards and attacks
    // (battlemode_ready, battle_trigger_on and the attack scripts would
    // play them twice); their sounds are the scripts': a card awarded,
    // Battle_Power_Awarded_SFX_P<n>; an attack, the receiver's crowd
    // reaction and the attack's GH_BattleMode_Player<n>_SFX_* start
    // [C scripts battlemode_ready, battle_trigger_on, battle_lightning ...].
    if (m.battleMode()) {
        static const char* const kAttackSfx[8] = {"Shake_Start", "DiffUp_Start", "DoubleNotes_Start", "Steal", "LeftyNotes_Start",
                                                 "BrokenString_Start", "Whammy_Start", "Death_Drain"};
        const Match::Counters& c = m.counters();
        for (int p = 0; p < players_; ++p) {
            const size_t i = size_t(p);
            for (; lastCards_[i] < c.cards[i]; ++lastCards_[i]) {
                qb::Struct e;
                Vm::set(e, K("event"), Vm::makeKey(K(p ? "Battle_Power_Awarded_SFX_P2" : "Battle_Power_Awarded_SFX_P1")));
                vm.run(K("SoundEvent"), e);
                ++k_.spawned["battle card sound"];
            }
            for (int a = 0; a < 8 && a < kAttacks; ++a)
                for (; lastByAttack_[i][size_t(a)] < c.byAttack[i][size_t(a)]; ++lastByAttack_[i][size_t(a)]) {
                    const int victim = 1 - p;
                    qb::Struct r;
                    Vm::set(r, K("receiving_player"), Vm::makeInt(victim + 1));
                    Vm::set(r, K("receiving_player_current_crowd_level"), Vm::makeFloat(m.player(victim).stats().health));
                    spawnTracked(K("GH3_Battle_Play_Crowd_Reaction_SFX"), r, "GH3_Battle_Play_Crowd_Reaction_SFX", k_.spawned);
                    // The steal's sound is the stealer's [C script battle_steal].
                    const int who = a == kSteal ? p : victim;
                    const std::string sfx = std::string("GH_BattleMode_Player") + (who ? "2" : "1") + "_SFX_" + kAttackSfx[a];
                    spawnTracked(K(sfx), qb::Struct{}, sfx.c_str(), k_.spawned);
                    if (a == kDeathLick)
                        spawnTracked(K(victim ? "GH_BattleMode_Start_Heartbeat_P2" : "GH_BattleMode_Start_Heartbeat_P1"), qb::Struct{},
                                     "GH_BattleMode_Start_Heartbeat", k_.spawned);
                }
        }
    }
    eventArrays(s, now);
    fx_.step(now);
    // Runs spawned by the song's runs are the song's too (GH3 pauses them together).
    for (auto& r : vm.runs())
        if (r->spawnedBy && runs_.count(r->spawnedBy)) runs_.insert(r->id);
}

void SongHooks::frame(SongScreen& s, double now) {
    if (!active_ || song_ != &s) return;
    // The scripts' big bolts: adopted (hidden in the element tree, drawn by draw()).
    bolts_.erase(std::remove_if(bolts_.begin(), bolts_.end(), [this](const Bolt& b) { return !fe_.ui().find(b.id); }), bolts_.end());
    for (int p = 0; p < players_; ++p)
        for (int n = 0; n < 5; ++n) {
            const uint32_t id = K(std::string("big_bolt") + textOf(p) + std::to_string(n));
            ui::Element* e = fe_.ui().find(id);
            if (!e || e->hidden) continue;
            e->hidden = true;
            bolts_.push_back(Bolt{id, now});
        }
    // The proxies follow the native highway: gem_container<p> its container,
    // the first gem's element its gem.
    for (int p = 0; p < players_; ++p) {
        const Highway2D& hw = p ? s.highway2() : s.highway();
        float cx = 0, cy = 0;
        hw.containerPos(now, cx, cy);
        if (ui::Element* c = fe_.ui().find(K(std::string("gem_container") + textOf(p)))) c->x = cx, c->y = cy;
        PerPlayer& pp = pp_[size_t(p)];
        if (!pp.firstGemId) continue;
        ui::Element* g = fe_.ui().find(pp.firstGemId);
        if (!g) continue;
        const NoteTrack& t = s.match().track(p);
        const bool gone = s.match().player(p).current() > size_t(pp.firstNote);
        float x = 0, y = 0, sc = 1;
        const TrackNote& fn = t.notes[size_t(pp.firstNote)];
        int fret = 0;
        while (fret < 4 && !(fn.frets & (1u << fret))) ++fret;
        if (gone || !hw.gemAt(fn.time, fret, now, x, y, sc)) {
            qb::Struct d;
            Vm::set(d, K("id"), Vm::makeKey(pp.firstGemId));
            fe_.vm().run(K("DestroyScreenElement"), d);
            proxies_.erase(std::remove(proxies_.begin(), proxies_.end(), pp.firstGemId), proxies_.end());
            pp.firstGemId = 0;
            continue;
        }
        // first_gem_fx puts its flare at (66, 20) in the gem sprite (a star
        // gem (255, 170) / battle (125, 170)): the element's origin is set so
        // that point lands on the gem's centre (the proxy is unscaled) [I:
        // the sprite's just and dims].
        (void)sc;
        const bool star = fn.phrase >= 0;
        const float ox = star ? (s.match().battleMode() ? 125.0f : 255.0f) : 66.0f, oy = star ? 170.0f : 20.0f;
        g->x = x - cx - ox, g->y = y - cy - oy;
    }
}

void SongHooks::draw(Canvas& c, double nowMs) {
    // Not over the pause menu (its elements are drawn before these) [I: GH3's z order].
    if (!active_ || paused_) return;
    // The big bolts: sys_Big_Bolt01, 8 frames at 15 fps, additive (the
    // material's flip-book, render/highway2d's Star Power bolts), the frame's
    // size x the element's scale (star_power_bolt_scale), just centre bottom,
    // turned by its rot_angle [C script GuitarEvent_StarSequenceBonus; frame order I].
    constexpr uint32_t kBolt = 0x9D12571C;
    const gfx::TextureId id = tex_.find(kBolt);
    int bw = 1024, bh = 512;
    tex_.size(kBolt, bw, bh);
    for (const Bolt& b : bolts_) {
        const ui::Element* e = fe_.ui().find(b.id);
        float x = 0, y = 0;
        if (!e || !id.id || !elementPos(b.id, x, y)) continue;
        const int frame = int((nowMs - b.born) / 1000.0 * 15) % 8;
        const float u0 = float(frame) / 8, u1 = u0 + 1.0f / 8;
        const float w = float(bw) / 8 * e->sx, h = float(bh) * e->sy;
        const float a = e->rot * 3.14159265f / 180, cs = std::cos(a), sn = std::sin(a);
        const uint8_t al = uint8_t(std::clamp(e->alpha * float(e->rgba[3]), 0.0f, 255.0f));
        auto at = [&](float lx, float ly, float u, float v) {
            return Vertex{x + lx * cs - ly * sn, y + lx * sn + ly * cs, 0, u, v, e->rgba[0], e->rgba[1], e->rgba[2], al};
        };
        const Vertex A = at(-w / 2, -h, u0, 0), B = at(w / 2, -h, u1, 0), C = at(w / 2, 0, u1, 1), D = at(-w / 2, 0, u0, 1);
        const Vertex q[6] = {A, B, C, A, C, D};
        c.draw(q, 6, int(id.id), Blend::Add);
    }
    fx_.draw(c, tex_, nowMs);
}

void SongHooks::setPaused(bool paused) {
    if (!active_ || paused == paused_) return;
    paused_ = paused;
    for (auto& r : fe_.vm().runs())
        if (runs_.count(r->id)) r->paused = paused;
}

void SongHooks::end(bool killed) {
    if (!active_) return;
    SDL_Log("song hooks: %s%s", summary().c_str(), killed ? " (killed)" : "");
    // kill_gem_scroller: the song's scripts (id song_event_scripts and theirs),
    // the Star Power sound and the venue's busses off [C scripts
    // kill_gem_scroller, GuitarEvent_KillSong, GuitarEvent_ExitVenue].
    for (auto& r : fe_.vm().runs())
        if (runs_.count(r->id) || r->type == K("song_event_scripts")) r->finished = true;
    runs_.clear();
    fx_.clear();
    bolts_.clear();
    destroyProxies();
    fe_.vm().run(K("GH_Star_Power_Verb_Off"), qb::Struct{});
    fe_.vm().run(K("GuitarEvent_ExitVenue"), qb::Struct{});
    mixer_.guitarFx().reset();
    active_ = false;
    song_ = nullptr;
}

std::string SongHooks::summary() const {
    auto list = [](const std::map<std::string, uint64_t>& m) {
        std::string s;
        for (const auto& [n, c] : m) s += (s.empty() ? "" : ", ") + n + " " + std::to_string(c);
        return s.empty() ? std::string("none") : s;
    };
    uint64_t sounds = 0;
    for (const auto& [n, c] : k_.sounds) sounds += c;
    return "events {" + list(k_.events) + "}; spawned {" + list(k_.spawned) + "}; event arrays {" + list(k_.arrays) + "} (" +
           std::to_string(k_.nativeSkipped) + " native); sounds " + std::to_string(sounds) + " {" + list(k_.sounds) + "}; buss effects " +
           std::to_string(k_.bussEffects) + ", buss params " + std::to_string(k_.bussParams) + ", 2D particle systems " +
           std::to_string(fx_.created) + " (" + std::to_string(fx_.particles) + " particles), native CFuncs absorbed " +
           std::to_string(k_.absorbed);
}

void SongHooks::addNatives() {
    Vm& vm = fe_.vm();
    // The CFuncs whose work the native judge has already done: they only
    // count here [C addresses; the native equivalents in game/player.cpp,
    // game/match.cpp, render/highway2d.cpp, audio/mixer.cpp].
    // CrowdIncrease / CrowdDecrease (0x8210FCC0 / 0x82110570: the health),
    // NoteFX (0x8211BAE8: the hit flame, render/highway2d), LaunchGemEvent
    // (0x8211A570: the gems' Star Power look), UpdateNixie (0x82106F20).
    for (const char* n : {"CrowdIncrease", "CrowdDecrease", "NoteFX", "LaunchGemEvent", "UpdateNixie"})
        vm.addNative(n, [this](Vm&, ScriptRun&, qb::Struct&) {
            ++k_.absorbed;
            return true;
        });
    // StarPowerOn player = n (0x8211DA70): the highway's Star Power colour and
    // containers (render/highway2d) and, in a one-player song,
    // do_starpower_camera (render/venue_camera's starPower) -- native.
    vm.addNative("StarPowerOn", [this](Vm&, ScriptRun&, qb::Struct&) {
        ++k_.absorbed;
        return true;
    });
    // GuitarEvent_HitNotes_CFunc (0x82110D98): guitar_volume = 100 (not for
    // the online player 2), the health of a note without a sustain
    // (0x8210F6A0: the judge's) and the buttons' pixels (render/highway2d);
    // true -> the script's UpdateGuitarVolume (the mixer's unmute is native).
    vm.addNative("GuitarEvent_HitNotes_CFunc", [this](Vm& v, ScriptRun&, qb::Struct& p) {
        ++k_.absorbed;
        const qb::Value* ps = Vm::param(p, K("player_status"));
        const uint32_t name = ps ? keyOf(ps) : 0;
        if (const qb::Value* cur = name ? v.global(name) : nullptr; cur && cur->st) {
            qb::Value nv = *cur;
            nv.st = std::make_shared<qb::Struct>(*cur->st);
            Vm::set(*nv.st, K("guitar_volume"), Vm::makeInt(100));
            v.setGlobal(name, nv);
        }
        return true;
    });
    // Create2DParticleSystem / Destroy2DParticleSystem [C 0x8210EEB8, 0x8211AA28].
    vm.addNative("Create2DParticleSystem", [this](Vm&, ScriptRun&, qb::Struct& p) {
        fx_.create(p, nowMs_);
        return true;
    });
    vm.addNative("Destroy2DParticleSystem", [this](Vm&, ScriptRun&, qb::Struct& p) {
        const qb::Value* id = Vm::param(p, K("id"));
        if (id) fx_.destroy(keyOf(id), Vm::flag(p, K("kill_when_empty")));
        return true;
    });
    // WhammyFXOn / WhammyFXOff {song array_entry player_status pattern ...}
    // (0x8211C1A0 / 0x8211C390): per fret of the pattern a spark system at the
    // fret's button (button_up_models.<color>.pos_2d, left_pos_2d when
    // lefty) in gem_container<p>, with whammy_particle_params, id
    // <color>WhammyFXID01<p> [C; the second system 02 I: one here].
    auto whammy = [this](Vm& v, qb::Struct& p, bool on) {
        const uint32_t ps = keyOf(Vm::param(p, K("player_status")));
        const qb::Value* st = v.global(ps);
        if (!st || !st->st) return true;
        const qb::Value* text = v.globals().field(*st->st, K("Text"));
        const std::string pt = text ? text->str : "p1";
        const int pattern = int(Vm::number(Vm::param(p, K("pattern")), 0));
        const bool lefty = Vm::number(v.globals().field(*st->st, K("lefthanded_button_ups")), 0) != 0;
        const qb::Value* models = v.global(K("button_up_models"));
        const qb::Value* params = v.global(K("whammy_particle_params"));
        for (int l = 0; l < 5; ++l) {
            if (!(pattern & (0x10000 >> (4 * l)))) continue;
            const uint32_t id = K(std::string(kColors[l]) + "WhammyFXID01" + pt);
            if (!on) {
                fx_.destroy(id, true);
                continue;
            }
            qb::Struct c;
            if (params && params->st) c = *params->st;
            Vm::set(c, K("id"), Vm::makeKey(id));
            Vm::set(c, K("parent"), Vm::makeKey(K("gem_container" + pt)));
            if (models && models->st)
                if (const qb::Value* m = v.globals().field(*models->st, K(kColors[l])); m && m->st)
                    if (const qb::Value* pos = v.globals().field(*m->st, K(lefty ? "left_pos_2d" : "pos_2d"))) Vm::set(c, K("pos"), *pos);
            fx_.create(c, nowMs_);
        }
        return true;
    };
    vm.addNative("WhammyFXOn", [whammy](Vm& v, ScriptRun&, qb::Struct& p) { return whammy(v, p, true); });
    vm.addNative("WhammyFXOff", [whammy](Vm& v, ScriptRun&, qb::Struct& p) { return whammy(v, p, false); });
    vm.addNative("WhammyFXOffAll", [this](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* st = v.global(keyOf(Vm::param(p, K("player_status"))));
        const qb::Value* text = st && st->st ? v.globals().field(*st->st, K("Text")) : nullptr;
        for (const char* c : kColors) fx_.destroy(K(std::string(c) + "WhammyFXID01" + (text ? text->str : "p1")), true);
        return true;
    });
    // setsoundbusseffects effect = {effect name params} | effect = $<global>
    // [time = s]: the guitar buss's GuitarEcho1 / GuitarReverb1 drive
    // audio/guitar_fx; the crowd buss's CrowdEcho1 / InGameReverb1 are kept
    // (the port's sound effects are not on busses) [C scripts GH_Star_Power_Verb_On, GuitarEvent_EnterVenue].
    vm.addNative("setsoundbusseffects", [this](Vm& v, ScriptRun&, qb::Struct& p) {
        const qb::Value* e = Vm::param(p, K("effect"));
        if (e && (e->type == qb::Type::Key || e->type == qb::Type::KeyPtr)) e = v.global(e->key);
        if (!e || !e->st) return true;
        ++k_.bussEffects;
        const qb::Struct& es = *e->st;
        const uint32_t name = keyOf(v.globals().field(es, K("name")));
        GuitarFx::Params gp = mixer_.guitarFx().params();
        auto num = [&](const char* n, float& out) {
            if (const qb::Value* x = v.globals().field(es, K(n)); x && (x->type == qb::Type::Int || x->type == qb::Type::Float)) out = x->number();
        };
        if (name == K("GuitarEcho1")) {
            num("delay", gp.echoDelayMs), num("DecayRatio", gp.echoDecay), num("Drymix", gp.echoDry), num("Wetmix", gp.echoWet);
        } else if (name == K("GuitarReverb1")) {
            num("ReverbLevel", gp.reverbLevelMb), num("ReflectionsLevel", gp.reflectionsLevelMb), num("DecayTime", gp.decayTime);
            num("ReflectionsDelay", gp.reflectionsDelay), num("ReverbDelay", gp.reverbDelay);
        } else {
            return true;
        }
        mixer_.guitarFx().set(gp, Vm::number(Vm::param(p, K("time")), 0));
        return true;
    });
    // SetSoundBussParams {<BussSet> time = s}: Right_Notes_Player1's vol (dB)
    // is the guitar buss's volume (Star_Power_BussSet -2) [C data]; the
    // others are not modelled. PushSoundBussParams / PopSoundBussParams keep
    // a stack of it [C 0x8227CE58, 0x8227D368].
    vm.addNative("SetSoundBussParams", [this](Vm& v, ScriptRun&, qb::Struct& p) {
        ++k_.bussParams;
        for (const qb::Item& it : p.items) {
            const qb::Value* val = &it.value;
            if (it.name == 0 && (val->type == qb::Type::Key || val->type == qb::Type::KeyPtr)) val = v.global(val->key);
            if (!val || val->type != qb::Type::Struct || !val->st) continue;
            if (const qb::Value* rn = v.globals().field(*val->st, K("Right_Notes_Player1")); rn && rn->st)
                if (const qb::Value* vol = rn->st->find(K("vol"))) {
                    GuitarFx::Params gp = mixer_.guitarFx().params();
                    gp.bussDb = vol->number();
                    mixer_.guitarFx().set(gp, Vm::number(Vm::param(p, K("time")), 0));
                }
        }
        return true;
    });
    vm.addNative("PushSoundBussParams", [this](Vm&, ScriptRun&, qb::Struct&) {
        pushedBuss_.push_back({mixer_.guitarFx().params().bussDb, 0});
        return true;
    });
    vm.addNative("PopSoundBussParams", [this](Vm&, ScriptRun&, qb::Struct&) {
        if (pushedBuss_.empty()) return true;
        GuitarFx::Params gp = mixer_.guitarFx().params();
        gp.bussDb = pushedBuss_.back().first;
        pushedBuss_.pop_back();
        mixer_.guitarFx().set(gp, 0.1f);
        return true;
    });
    // get_song_tempo_cfunc -> beat_duration (ms): the beat at the song's
    // time [C 0x82108B00; I: the fretbar interval there].
    vm.addNative("get_song_tempo_cfunc", [this](Vm&, ScriptRun& run, qb::Struct&) {
        float beat = 500;
        if (song_) beat = song_->match().track(0).beatAt(float(nowMs_));
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("beat_duration"), Vm::makeFloat(beat));
        return true;
    });
    // GetSongTime -> songtime (s) [C 0x822710F0].
    vm.addNative("GetSongTime", [this](Vm&, ScriptRun& run, qb::Struct&) {
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("songtime"), Vm::makeFloat(float(fe_.songClockMs / 1000.0)));
        return true;
    });
    // GetStrumPattern entry = n song = <input array> -> strum: the frets of
    // that entry as a pattern [C 0x82106698].
    vm.addNative("GetStrumPattern", [](Vm& v, ScriptRun& run, qb::Struct& p) {
        const int entry = int(Vm::number(Vm::param(p, K("entry")), 0));
        const qb::Value* a = v.global(keyOf(Vm::param(p, K("song"))));
        int32_t strum = 0;
        if (a && a->arr && entry >= 0 && size_t(entry) < a->arr->values.size() && a->arr->values[size_t(entry)].arr) {
            const auto& e = a->arr->values[size_t(entry)].arr->values;
            for (int l = 0; l < 5 && size_t(l + 1) < e.size(); ++l)
                if (e[size_t(l + 1)].number() > 0) strum |= 0x10000 >> (4 * l);
        }
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("strum"), Vm::makeInt(strum));
        return true;
    });
    // KillPulsateStarPowerBulbs: the meter's bulbs are render/hud2d's (native).
    vm.addNative("KillPulsateStarPowerBulbs", [this](Vm&, ScriptRun&, qb::Struct&) {
        ++k_.absorbed;
        return true;
    });
    // SafeGetUniqueCompositeObjectID / GetUniqueCompositeObjectID preferredid = x -> uniqueid = x [I].
    vm.addNative("GetUniqueCompositeObjectID", [](Vm&, ScriptRun& run, qb::Struct& p) {
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("uniqueid"), Vm::makeKey(keyOf(Vm::param(p, K("preferredid")))));
        return true;
    });
    vm.addNative("SafeGetUniqueCompositeObjectID", [](Vm&, ScriptRun& run, qb::Struct& p) {
        if (qb::Struct* l = locals(run)) Vm::set(*l, K("uniqueid"), Vm::makeKey(keyOf(Vm::param(p, K("preferredid")))));
        return true;
    });
    // The 3D effects the song scripts ask for that need the venue / band /
    // crowd renderers' support (render/particles, render/crowd): counted, not
    // drawn yet -- CreateParticleSystem_fast on a band member's bone (the 4x
    // finger flames, the Star Power stage FX), Crowd_PlayAnim (the crowd's
    // Star Power anim), EngineWind, LightShow_SetTime from <song>_lightshow.
    for (const char* n : {"CreateParticleSystem_fast", "CreateParticleSystem", "DestroyParticlesByGroupID", "DeleteTeslaEffectObject",
                          "Crowd_PlayAnim", "EngineWind", "LightShow_SetTime"})
        vm.addNative(n, [this, n](Vm&, ScriptRun&, qb::Struct&) {
            ++k_.spawned[std::string("(3D) ") + n];
            return true;
        });
}
